"""Isolated browser fixture: fictional identity, explicit keys, temporary directory/ports."""
import argparse
import asyncio
import json
import sys
from contextlib import asynccontextmanager, suppress
from datetime import datetime, timedelta, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import uvicorn
from app.config import Settings
from app.main import create_app
from app.protocol import ReadingV2, encode_frame
from tools.send_sample import snapshot

parser = argparse.ArgumentParser()
parser.add_argument("--directory", type=Path, required=True)
parser.add_argument("--http-port", type=int, default=18763)
parser.add_argument("--tcp-port", type=int, default=19763)
args = parser.parse_args()
args.directory.mkdir(parents=True, exist_ok=True)
control = args.directory / "control.json"
control.write_text('{"mode":"fresh"}', encoding="utf-8")
settings = Settings(database=args.directory / "gps_preview.db", http_port=args.http_port,
                    tcp_port=args.tcp_port, ingest_token="preview-fixture",
                    gps_imei_bindings={"990000000000001": "demo_gps_001"})
app = create_app(settings)
original_lifespan = app.router.lifespan_context


async def feed():
    store = app.state.store
    store.insert(ReadingV2.model_validate(snapshot(1, "demo_other")))
    reader, writer = await asyncio.open_connection("127.0.0.1", settings.tcp_port)
    seq, previous, gps_sent = 0, None, 0
    try:
        while True:
            seq += 1
            message = snapshot(seq, "demo_gps_001", boot_id="gps-preview")
            message["token"] = "preview-fixture"
            writer.write(encode_frame(message)); await writer.drain()
            assert json.loads(await reader.readline())["ok"]
            mode = json.loads(control.read_text(encoding="utf-8-sig")).get("mode", "fresh")
            now = datetime.now(timezone.utc)
            if mode != previous or (mode == "fresh" and seq - gps_sent >= 20):
                report = dict(cmd="upload", IMEI=990000000000001,
                              time=now.strftime("%Y-%m-%d %H:%M:%S"), gps=[114.384038, 30.491893])
                if mode == "stale":
                    # Fixture clock preparation, never performed on a production database.
                    with store.connect() as db:
                        rows = db.execute("SELECT id,fix_time_utc,received_at_utc FROM locations WHERE device_id=? ORDER BY fix_time_utc",
                                          ("demo_gps_001",)).fetchall()
                        for row in rows:
                            shifted = [(datetime.fromisoformat(row[key]) - timedelta(seconds=240)).isoformat(timespec="milliseconds")
                                       for key in ("fix_time_utc", "received_at_utc")]
                            db.execute("UPDATE locations SET fix_time_utc=?,received_at_utc=? WHERE id=?", (*shifted, row["id"]))
                    app.state.receiver.publish({"type": "location", "device_id": "demo_gps_001"})
                else:
                    if mode == "invalid":
                        report["gps"] = [0, 0]
                    writer.write(json.dumps(report).encode())  # Intentionally no newline.
                    await writer.drain()
                gps_sent = seq
                previous = mode
            await asyncio.sleep(3)
    finally:
        writer.close()
        await writer.wait_closed()


@asynccontextmanager
async def fixture_lifespan(application):
    async with original_lifespan(application):
        task = asyncio.create_task(feed())
        try:
            yield
        finally:
            task.cancel()
            with suppress(asyncio.CancelledError):
                await task


app.router.lifespan_context = fixture_lifespan
uvicorn.run(app, host="127.0.0.1", port=args.http_port, log_level="warning")
