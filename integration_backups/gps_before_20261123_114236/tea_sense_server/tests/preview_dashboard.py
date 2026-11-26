"""Isolated eleven-metric browser fixture: demo only, temporary DB, no .env."""
import argparse
import asyncio
import json
import sys
import uuid
from contextlib import asynccontextmanager, suppress
from datetime import datetime, timedelta, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import uvicorn
from app.config import Settings
from app.main import create_app
from app.protocol import ReadingV2, encode_frame
from app.storage import Store
from tools.send_sample import snapshot

parser = argparse.ArgumentParser()
parser.add_argument("--directory", type=Path, required=True)
parser.add_argument("--view-token", default="")
args = parser.parse_args()
args.directory.mkdir(parents=True, exist_ok=True)
settings = Settings(database=args.directory / "preview.db", http_port=18000,
                    tcp_port=19000, ingest_token="preview-test-token",
                    view_token=args.view_token, online_seconds=10)
store = Store(settings.database)
store.initialize()
if not store.counts()["total"]:
    now = datetime.now(timezone.utc)
    for index in range(1250):
        message = snapshot(index+1)
        if index == 1210:
            message["data"]["soil_moisture_pct"].update(value=None, status="WINDOW_INSUFFICIENT")
        row, _ = store.insert(ReadingV2(**message))
        timestamp = now-timedelta(seconds=(1249-index)*3+(90 if index<1180 else 0))
        with store.connect() as db:
            db.execute("UPDATE readings SET received_at=? WHERE id=?", (timestamp.isoformat(), row["id"]))
            measurements = db.execute("SELECT id,age_ms FROM measurements WHERE reading_id=?", (row["id"],)).fetchall()
            for point in measurements:
                observed = timestamp-timedelta(milliseconds=point["age_ms"])
                db.execute("UPDATE measurements SET received_at=?,observed_at=? WHERE id=?",
                           (timestamp.isoformat(), observed.isoformat(), point["id"]))
    store.insert(ReadingV2(**snapshot(1, "demo_backup_001", mode="zero")))
control = args.directory / "control.json"
control.write_text('{"mode":"fresh"}', encoding="utf-8")
app = create_app(settings)
original_lifespan = app.router.lifespan_context

async def feed():
    with store.connect() as db:
        sequence = max(2000, db.execute("SELECT coalesce(max(seq),0) FROM readings WHERE device_id='demo_h743_001'").fetchone()[0])
    session_id = uuid.uuid4().hex[:12]
    previous_mode = None
    run = 0
    while True:
        mode = json.loads(control.read_text(encoding="utf-8-sig")).get("mode", "fresh")
        if mode != previous_mode:
            run += 1
            previous_mode = mode
        if mode != "pause":
            sequence += 1
            message = snapshot(sequence, boot_id="preview-"+session_id+"-"+str(run), mode=mode)
            message["token"] = "preview-test-token"
            reader, writer = await asyncio.open_connection("127.0.0.1", 19000)
            writer.write(encode_frame(message))
            await writer.drain()
            ack = json.loads(await reader.readline())
            assert ack["ok"] is True
            writer.close()
            await writer.wait_closed()
        await asyncio.sleep(3)

@asynccontextmanager
async def preview_lifespan(application):
    async with original_lifespan(application):
        task = asyncio.create_task(feed())
        try:
            yield
        finally:
            task.cancel()
            with suppress(asyncio.CancelledError):
                await task

app.router.lifespan_context = preview_lifespan
uvicorn.run(app, host="127.0.0.1", port=18000, log_level="warning")
