import asyncio
import argparse
import hmac
import logging
from contextlib import asynccontextmanager
from dataclasses import replace
from datetime import datetime, timedelta, timezone

import uvicorn
from fastapi import FastAPI, HTTPException, Query, Request, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse, Response
from fastapi.staticfiles import StaticFiles

from .config import ROOT, Settings
from .protocol import WINDOW_EXAMPLE, V2_EXAMPLE, V2_STATISTICS_EXAMPLE, METRICS
from .storage import Store, utc_now
from .tcp_server import TcpReceiver


def create_app(settings=None):
    settings = settings or Settings.from_env()
    store = Store(settings.database, mysql_dsn=settings.mysql_dsn,
                  mysql_ssl_ca=settings.mysql_ssl_ca)
    subscribers = set()

    def publish(event):
        for queue in tuple(subscribers):
            if queue.full():
                while not queue.empty():
                    queue.get_nowait()
                queue.put_nowait({"type": "resync"})
            else:
                queue.put_nowait(event)

    receiver = TcpReceiver(settings, store, publish)

    @asynccontextmanager
    async def lifespan(app):
        await asyncio.to_thread(store.initialize)
        await receiver.start()
        app.state.receiver, app.state.store = receiver, store
        try:
            yield
        finally:
            await receiver.close()

    app = FastAPI(title="Tea Sense", lifespan=lifespan, docs_url=None, redoc_url=None,
                  openapi_url=None)

    def valid_view_token(token):
        return not settings.view_token or (isinstance(token, str) and hmac.compare_digest(
            token.encode(), settings.view_token.encode()))

    @app.middleware("http")
    async def protect_api(request: Request, call_next):
        if request.url.path.startswith("/api/") and request.url.path != "/api/config":
            if not valid_view_token(request.headers.get("X-View-Token")):
                return Response('{"detail":"unauthorized"}', status_code=401,
                                media_type="application/json")
        response = await call_next(request)
        response.headers["X-Content-Type-Options"] = "nosniff"
        response.headers["Referrer-Policy"] = "same-origin"
        response.headers["Cache-Control"] = "no-store"
        return response

    @app.get("/healthz")
    async def health():
        return {"ok": True}

    @app.get("/api/config")
    async def config():
        return {"tcp_port": settings.tcp_port, "online_seconds": settings.online_seconds,
                "business_window_ms": 180000,
                "view_auth_required": bool(settings.view_token),
                "gps_enabled": bool(settings.ingest_token and settings.gps_imei_bindings),
                "gps_stale_seconds": settings.gps_stale_seconds,
                "gps_coordinate_system": settings.gps_coordinate_system,
                "ingest_auth_required": bool(settings.ingest_token), "example": V2_STATISTICS_EXAMPLE,
                "legacy_v2_example": V2_EXAMPLE, "legacy_example": WINDOW_EXAMPLE, "metrics": METRICS,
                "export_limit":10000, "run_identity":"boot_id or inferred counter reset; ambiguous replays cannot be globally deduplicated"}

    @app.get("/api/overview")
    async def overview(hold_latest: bool = False):
        latest, counts = await asyncio.gather(asyncio.to_thread(store.latest),
                                              asyncio.to_thread(store.counts))
        cutoff = (datetime.now(timezone.utc) - timedelta(seconds=settings.online_seconds)).isoformat()
        for row in latest:
            row["online"] = row["received_at"] >= cutoff
            row["data"] = await asyncio.to_thread(store.current, row["device_id"], hold_latest=hold_latest)
            row["location"] = await asyncio.to_thread(store.current_location, row["device_id"], settings.gps_stale_seconds)
        return {"server_time": utc_now(), "counts": counts, "devices": latest,
                "connections": len(receiver.writers), "accepted": receiver.accepted,
                "rejected": receiver.rejected, "duplicates": receiver.duplicates,
                "gps": dict(receiver.gps_counts)}

    @app.get("/api/location")
    async def location(device_id: str = Query(..., pattern=r"^[A-Za-z0-9_-]{1,64}$")):
        return await asyncio.to_thread(store.current_location, device_id, settings.gps_stale_seconds)

    def since_time(hours):
        return (datetime.now(timezone.utc) - timedelta(hours=hours)).isoformat(timespec="milliseconds")

    @app.get("/api/readings")
    async def readings(device_id: str | None = Query(None, max_length=64),
                       hours: int = Query(24, ge=1, le=168),
                       limit: int = Query(200, ge=1, le=1000),
                       before_id: int | None = Query(None, ge=1),
                       status: str | None = Query(None, max_length=40)):
        rows = await asyncio.to_thread(store.history, device_id=device_id, since=since_time(hours),
                                       status=status, limit=limit + 1, before_id=before_id)
        has_more = len(rows) > limit
        rows = rows[:limit]
        return {"items": rows, "has_more": has_more,
                "next_before_id": rows[-1]["id"] if rows and has_more else None}

    @app.get("/api/measurements")
    async def measurements(device_id: str | None = Query(None,max_length=64),
                           metric: str | None = Query(None,max_length=40),
                           hours: int = Query(24,ge=1,le=168),limit: int = Query(200,ge=1,le=1000),
                           before_id: int | None = Query(None,ge=1)):
        if metric is not None and metric not in METRICS: raise HTTPException(422,"Unknown metric")
        rows=await asyncio.to_thread(store.measurements,device_id=device_id,metric=metric,since=since_time(hours),limit=limit+1,before_id=before_id)
        more=len(rows)>limit
        rows=rows[:limit]
        return {"items":rows,"has_more":more,"next_before_id":rows[-1]["id"] if more and rows else None}

    @app.get("/api/export.csv")
    async def export(device_id: str | None = Query(None, max_length=64),
                     hours: int = Query(24, ge=1, le=168),metric: str | None = Query(None,max_length=40)):
        if metric is not None and metric not in METRICS: raise HTTPException(422,"Unknown metric")
        data = await asyncio.to_thread(store.csv, device_id=device_id, metric=metric,since=since_time(hours), limit=10000)
        return Response(data, media_type="text/csv; charset=utf-8",
                        headers={"Content-Disposition": 'attachment; filename="tea_sense_readings.csv"'})

    @app.websocket("/ws")
    async def websocket(websocket: WebSocket):
        # Web UI is same-origin; proxy deployments must preserve Host.
        origin = websocket.headers.get("origin")
        if origin and origin.split("://", 1)[-1] != websocket.headers.get("host"):
            await websocket.close(code=1008)
            return
        await websocket.accept()
        queue = asyncio.Queue(maxsize=32)
        try:
            auth = await asyncio.wait_for(websocket.receive_json(), 5)
            if not isinstance(auth, dict) or not valid_view_token(auth.get("token")):
                await websocket.close(code=1008)
                return
            subscribers.add(queue)
            await websocket.send_json({"type": "ready"})
            while True:
                try:
                    event = await asyncio.wait_for(queue.get(), 10)
                except asyncio.TimeoutError:
                    event = {"type": "ping", "time": utc_now()}
                await websocket.send_json(event)
        except (WebSocketDisconnect, asyncio.TimeoutError, RuntimeError, ValueError):
            pass
        finally:
            subscribers.discard(queue)

    @app.get("/")
    async def index():
        return FileResponse(ROOT / "web/index.html")

    app.mount("/assets", StaticFiles(directory=ROOT / "web"), name="assets")
    return app


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Tea Sense web and TCP receiver")
    parser.add_argument("--host")
    parser.add_argument("--port", type=int)
    parser.add_argument("--strictPort", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    settings = Settings.from_env()
    # Preview launchers may supply their own HTTP listener; TCP is unchanged.
    if args.host is not None or args.port is not None:
        settings = replace(settings,
                           http_host=args.host if args.host is not None else settings.http_host,
                           http_port=args.port if args.port is not None else settings.http_port)
    settings.validate()
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(name)s %(levelname)s %(message)s")
    # Exactly one process owns the TCP listener and in-process live updates.
    uvicorn.run(create_app(settings), host=settings.http_host, port=settings.http_port,
                workers=1, log_level="info", ws_max_size=4096)
