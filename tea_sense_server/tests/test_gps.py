"""GPS integration uses fictional IMEIs, explicit fixture keys and isolated SQLite/TCP."""
import asyncio
import copy
import json
import os
import socket
import sqlite3
import tempfile
import unittest
from dataclasses import replace
from datetime import datetime, timedelta, timezone
from pathlib import Path
from unittest.mock import patch

import httpx
from fastapi.testclient import TestClient

from app.config import Settings
from app.json_stream import FrameTooLarge, JsonObjectStream
from app.location import LocationError, parse_location
from app.main import create_app
from app.protocol import V2_EXAMPLE, encode_frame
from app.storage import Store
from tools.backup_database import backup_database

IMEI = "990000000000001"
OTHER_IMEI = "990000000000002"
DEVICE = "demo_gps_001"
NOW = datetime(2026, 11, 23, 3, 18, 23, tzinfo=timezone.utc)


def native(fix="2026-11-23 3:17:21", gps=None, imei=IMEI, **extra):
    return dict(addr="1", cmd="upload", IMEI=int(imei), time=fix,
                gps=[114.384038, 30.491893] if gps is None else gps,
                gsmloc=[0.0, 0.0], gsminfo=[460, 0, 28709, 10004164], **extra)


def sensor(seq=1, device=DEVICE, token="fixture-ingest"):
    value = copy.deepcopy(V2_EXAMPLE)
    value.update(device_id=device, seq=seq, token=token, boot_id="gps-test")
    return value


class GpsParsingTests(unittest.TestCase):
    def test_observed_samples_utc_order_and_beijing(self):
        first = parse_location(native(), now=NOW)
        second = parse_location(native("2026-11-23 3:18:21", [114.384035, 30.491890]), now=NOW)
        self.assertEqual((first["longitude"], first["latitude"]), (114.384038, 30.491893))
        self.assertEqual((second["longitude"], second["latitude"]), (114.384035, 30.491890))
        self.assertEqual(first["fix_time_utc"], "2026-11-23T03:17:21.000+00:00")
        a, b = (datetime.fromisoformat(row["fix_time_utc"]) for row in (first, second))
        self.assertEqual((b - a).total_seconds(), 60)
        self.assertEqual(a.astimezone(timezone(timedelta(hours=8))).strftime("%Y-%m-%d %H:%M:%S"), "2026-11-23 11:17:21")
        self.assertEqual(first["coordinate_system"], "UNKNOWN")
        self.assertFalse(any(key in first for key in ("IMEI", "satellite_num", "accuracy", "altitude", "gsmloc")))

    def test_invalid_and_zero_coordinates_and_time(self):
        invalid = [dict(gps=point) for point in ([0, 0], [True, 30], [114, False], ["114", 30],
                   [181, 30], [114, -91], [float("nan"), 30], [114, float("inf")],
                   [114], [114, 30, 0], None, {}, [10**400, 30])]
        # native's default gps argument substitutes None, so construct invalid fields explicitly.
        invalid += [dict(time=value) for value in (None, "2026-02-30 3:17:21", "2026-11-23T03:17:21Z", "", 123)]
        invalid += [dict(cmd="other"), dict(IMEI=True), dict(IMEI="bad")]
        for change in invalid:
            with self.subTest(change=change), self.assertRaises(LocationError):
                parse_location({**native(), **change}, now=NOW)
        for point in ([0, 30], [114, 0], [-180, -90], [180, 90]):
            self.assertEqual(parse_location(native(gps=point), now=NOW)["longitude"], point[0])
        with self.assertRaisesRegex(LocationError, "FUTURE_TIME"):
            parse_location(native("2026-11-23 3:23:24"), now=NOW)

    def test_stream_every_byte_boundary_crlf_and_escaped_brackets(self):
        payload = json.dumps({"text": '中文 } [ { \\ " \\"', "nested": [{"a": [1, 2]}]}, ensure_ascii=False).encode()
        for cut in range(len(payload) + 1):
            stream = JsonObjectStream(8192)
            self.assertEqual(list(stream.feed(payload[:cut])) + list(stream.feed(payload[cut:])), [payload])
        stream = JsonObjectStream(8192)
        self.assertEqual(list(stream.feed(payload + b"\r")), [payload])
        self.assertEqual(list(stream.feed(b"\n" + payload + payload + b"\r\n")), [payload, payload])
        stream = JsonObjectStream(8192)
        self.assertEqual([frame for byte in payload for frame in stream.feed(bytes([byte]))], [payload])
        stream = JsonObjectStream(8192)
        self.assertEqual(list(stream.feed(b"not json\n{}")), [b"not json\n", b"{}"]) 

    def test_stream_length_limit_and_incomplete_state(self):
        self.assertEqual(list(JsonObjectStream(2).feed(b"{}\r\n{}")), [b"{}", b"{}"])
        for payload in (b"x" * 33, b'{"text":"' + b"x" * 33):
            with self.assertRaises(FrameTooLarge):
                list(JsonObjectStream(32).feed(payload))
        stream = JsonObjectStream(32)
        self.assertEqual(list(stream.feed(b'{"a":')), [])
        self.assertEqual(bytes(stream.buffer), b'{"a":')

    def test_private_settings_validation_and_no_key_requirement_bypass(self):
        base = Settings(ingest_token="fixture-ingest", gps_imei_bindings={IMEI: DEVICE})
        base.validate()
        for change in (dict(ingest_token=""), dict(gps_imei_bindings={"bad": DEVICE}),
                       dict(gps_imei_bindings={IMEI: "bad device"}), dict(gps_imei_bindings=[]),
                       dict(gps_stale_seconds=0), dict(gps_coordinate_system="guessed")):
            with self.subTest(change=change), self.assertRaises(ValueError):
                replace(base, **change).validate()
        self.assertNotIn(IMEI, repr(base))
        with patch.dict(os.environ, {"INGEST_TOKEN": "fixture-ingest", "GPS_IMEI_BINDINGS": json.dumps({IMEI: DEVICE}),
                                    "GPS_STALE_SECONDS": "180", "GPS_COORDINATE_SYSTEM": "UNKNOWN"}, clear=True):
            with patch("app.config.load_env"):
                self.assertEqual(Settings.from_env().gps_imei_bindings, {IMEI: DEVICE})


class GpsStorageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.path = Path(self.temp.name) / "gps.db"
        self.store = Store(self.path)
        self.store.initialize()
        self.first = parse_location(native(), now=NOW)
        self.second = parse_location(native("2026-11-23 3:18:21", [114.384035, 30.491890]), now=NOW)

    def tearDown(self):
        self.temp.cleanup()

    def test_duplicate_late_conflict_errors_and_independent_ages(self):
        received = NOW.isoformat()
        self.assertEqual(self.store.insert_location(DEVICE, self.second, received), "OK")
        original = self.store.current_location(DEVICE, now=NOW)
        later = (NOW + timedelta(seconds=600)).isoformat()
        self.assertEqual(self.store.insert_location(DEVICE, self.second, later), "DUPLICATE")
        self.assertEqual(self.store.insert_location(DEVICE, self.first, later), "OUT_OF_ORDER")
        conflict = dict(self.second, longitude=115)
        self.assertEqual(self.store.insert_location(DEVICE, conflict, later), "FIX_CONFLICT")
        self.store.record_location_error(DEVICE, "NO_FIX", later)
        current = self.store.current_location(DEVICE, now=NOW + timedelta(seconds=600))
        self.assertEqual(current["fix_time_utc"], original["fix_time_utc"])
        self.assertEqual(current["received_at_utc"], received)
        self.assertEqual(current["last_received_at_utc"], later)
        self.assertEqual((current["status"], current["last_report_status"], current["latest_report_error"]), ("STALE", "NO_FIX", True))
        self.assertEqual((current["fix_age_seconds"], current["received_age_seconds"], current["last_received_age_seconds"]), (602, 600, 0))
        self.assertEqual((current["received_count"], current["duplicate_count"], current["out_of_order_count"], current["rejected_count"]), (5, 1, 1, 2))
        with self.store.connect() as db:
            self.assertEqual(db.execute("SELECT count(*) FROM locations").fetchone()[0], 2)
        self.assertEqual(self.store.counts(), dict(total=0, devices=0, measurements=0))
        self.assertNotIn("longitude", self.store.csv())

    def test_migration_restart_and_backup_restore_preserve_sensor_history(self):
        # Rehearse upgrade of schema2, not the user's database.
        with self.store.connect() as db:
            db.execute("DROP TABLE locations")
            db.execute("DROP TABLE location_receipts")
            db.execute("PRAGMA user_version=2")
        from app.protocol import ReadingV2
        row, _ = self.store.insert(ReadingV2.model_validate(sensor()))
        before = self.store.history()
        with self.store.connect() as db:
            payload = db.execute("SELECT payload FROM readings WHERE id=?", (row["id"],)).fetchone()[0]
        self.store.initialize()
        self.store.insert_location(DEVICE, self.first, NOW.isoformat())
        self.store.record_location_error(DEVICE, "INVALID_TIME", NOW.isoformat())
        restarted = Store(self.path)
        restarted.initialize()
        self.assertEqual(restarted.history(), before)
        self.assertEqual(restarted.current_location(DEVICE, now=NOW)["longitude"], 114.384038)
        with restarted.connect() as db:
            self.assertEqual(db.execute("PRAGMA user_version").fetchone()[0], 4)
            self.assertEqual(db.execute("SELECT payload FROM readings WHERE id=?", (row["id"],)).fetchone()[0], payload)
        backup = Path(self.temp.name) / "backup.db"
        result = backup_database(self.path, backup)
        self.assertEqual((result["locations"], result["location_receipts"]), (1, 1))
        restored = Path(self.temp.name) / "restored.db"
        backup_database(backup, restored)
        other = Store(restored)
        other.initialize()
        self.assertEqual(other.current_location(DEVICE, now=NOW), restarted.current_location(DEVICE, now=NOW))
        self.assertEqual(other.history(), before)


class GpsTcpTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.settings = Settings(database=Path(self.temp.name) / "gps.db", tcp_port=0,
                                 ingest_token="fixture-ingest", view_token="fixture-view",
                                 gps_imei_bindings={IMEI: DEVICE, OTHER_IMEI: "demo_other"})
        self.app = create_app(self.settings)
        self.lifespan = self.app.router.lifespan_context(self.app)
        await self.lifespan.__aenter__()
        self.receiver = self.app.state.receiver
        self.port = self.receiver.server.sockets[0].getsockname()[1]
        self.http = httpx.AsyncClient(transport=httpx.ASGITransport(app=self.app), base_url="http://test",
                                     headers={"X-View-Token": "fixture-view"})
        self.connections = []

    async def asyncTearDown(self):
        for writer in self.connections:
            writer.close()
            await writer.wait_closed()
        await self.http.aclose()
        await self.lifespan.__aexit__(None, None, None)
        self.temp.cleanup()

    async def connect(self):
        reader, writer = await asyncio.open_connection("127.0.0.1", self.port)
        self.connections.append(writer)
        return reader, writer

    async def ack(self, reader):
        return json.loads(await asyncio.wait_for(reader.readline(), 2))

    async def wait_count(self, key, expected):
        for _ in range(100):
            if self.receiver.gps_counts[key] >= expected:
                return
            await asyncio.sleep(0.01)
        self.fail(f"GPS {key} did not reach {expected}")

    async def current(self, device=DEVICE):
        result = await self.http.get("/api/location", params={"device_id": device})
        self.assertEqual(result.status_code, 200)
        return result.json()

    async def test_mixed_no_newline_fragments_gps_before_auth_and_no_gps_ack(self):
        reader, writer = await self.connect()
        first = json.dumps(native()).encode()
        v2 = encode_frame(sensor()).rstrip(b"\n")
        second = encode_frame(native("2026-11-23 3:18:21", [114.384035, 30.491890]))
        writer.write(first + b"\r"); await writer.drain()
        await self.wait_count("before_auth", 1)
        self.assertEqual((await self.current())["status"], "NO_DATA")
        writer.write(b"\n" + v2[:35]); await writer.drain()
        writer.write(v2[35:] + first + second[:17]); await writer.drain()
        writer.write(second[17:] + encode_frame(sensor(2))); await writer.drain()
        self.assertEqual((await self.ack(reader))["seq"], 1)
        self.assertEqual((await self.ack(reader))["seq"], 2)  # No extra GPS ACK between V2 ACKs.
        await self.wait_count("accepted", 2)
        current = await self.current()
        self.assertEqual((current["longitude"], current["latitude"]), (114.384035, 30.491890))
        self.assertEqual(current["fix_time_utc"], "2026-11-23T03:18:21.000+00:00")
        overview = (await self.http.get("/api/overview")).json()
        self.assertEqual(overview["counts"]["total"], 2)
        self.assertEqual(overview["devices"][0]["location"]["fix_time_utc"], current["fix_time_utc"])
        public = json.dumps(overview) + json.dumps((await self.http.get("/api/config")).json())
        self.assertNotIn(IMEI, public)
        self.assertNotIn("fixture-ingest", public)
        self.assertEqual((await self.current("demo_other"))["status"], "NO_DATA")
        writer.write_eof()
        self.assertEqual(await asyncio.wait_for(reader.read(), 2), b"")

    async def test_unknown_mismatch_reconnection_and_bad_token(self):
        reader, writer = await self.connect()
        writer.write(encode_frame(sensor())); await writer.drain(); self.assertTrue((await self.ack(reader))["ok"])
        writer.write(encode_frame(native(imei="990000000000099")) + encode_frame(native(imei=OTHER_IMEI)))
        await writer.drain(); await self.wait_count("binding_mismatch", 1)
        self.assertEqual(self.receiver.gps_counts["unknown_imei"], 1)
        self.assertIsNone((await self.current())["longitude"])
        other_reader, other_writer = await self.connect()
        other_writer.write(encode_frame(native())); await other_writer.drain(); await self.wait_count("before_auth", 1)
        other_writer.write(encode_frame(sensor(token="wrong"))); await other_writer.drain()
        self.assertEqual((await self.ack(other_reader))["error"], "unauthorized")
        writer.write(encode_frame(native()) + encode_frame(sensor(2))); await writer.drain()
        self.assertEqual((await self.ack(reader))["seq"], 2)
        self.assertEqual((await self.current())["received_count"], 1)

    async def test_malformed_native_diagnostics_and_sensors_keep_working(self):
        reader, writer = await self.connect()
        writer.write(encode_frame(sensor()) + encode_frame(native())); await writer.drain()
        await self.ack(reader); await self.wait_count("accepted", 1)
        original = await self.current()
        changes = [dict(gps=[0, 0]), dict(gps=[True, 30]), dict(gps=[float("nan"), 30]),
                   dict(gps=[114, float("inf")]), dict(time="invalid"), dict(time="2099-01-01 0:00:00"), dict(cmd="other")]
        for index, change in enumerate(changes, 1):
            writer.write(json.dumps({**native(), **change}).encode()); await writer.drain()
            await self.wait_count("invalid", index)
        writer.write(encode_frame(sensor(2))); await writer.drain()
        self.assertEqual((await self.ack(reader))["seq"], 2)
        current = await self.current()
        self.assertEqual(current["fix_time_utc"], original["fix_time_utc"])
        self.assertEqual(current["received_at_utc"], original["received_at_utc"])
        self.assertEqual(current["last_report_status"], "INVALID_COMMAND")
        self.assertTrue(current["latest_report_error"])
        self.assertEqual(current["rejected_count"], 7)

    async def test_duplicate_does_not_refresh_fix_and_sensor_online_is_separate(self):
        reader, writer = await self.connect()
        old = (datetime.now(timezone.utc) - timedelta(seconds=600)).strftime("%Y-%m-%d %H:%M:%S")
        report = encode_frame(native(old))
        writer.write(encode_frame(sensor()) + report); await writer.drain()
        await self.ack(reader); await self.wait_count("accepted", 1)
        original = await self.current()
        writer.write(report + encode_frame(sensor(2))); await writer.drain()
        await self.ack(reader); await self.wait_count("duplicates", 1)
        current = await self.current()
        self.assertEqual((current["status"], current["received_at_utc"]), ("STALE", original["received_at_utc"]))
        self.assertEqual(current["duplicate_count"], 1)
        overview = (await self.http.get("/api/overview")).json()
        self.assertTrue(overview["devices"][0]["online"])
        self.assertEqual(overview["devices"][0]["location"]["status"], "STALE")
        with self.app.state.store.connect() as db:
            self.assertEqual(db.execute("SELECT count(*) FROM locations").fetchone()[0], 1)
        self.assertNotIn("longitude", (await self.http.get("/api/export.csv")).text)

    async def test_location_endpoint_auth_and_legacy_v1_does_not_authenticate_gps(self):
        self.assertEqual((await self.http.get("/api/location", params={"device_id": DEVICE}, headers={"X-View-Token": "wrong"})).status_code, 401)
        self.assertEqual((await self.http.get("/api/location", params={"device_id": "bad id"})).status_code, 422)
        from app.protocol import EXAMPLE
        reader, writer = await self.connect()
        writer.write(encode_frame({**EXAMPLE, "device_id": DEVICE, "token": "fixture-ingest"}) + encode_frame(native()))
        await writer.drain(); self.assertTrue((await self.ack(reader))["ok"])
        await self.wait_count("before_auth", 1)
        self.assertIsNone((await self.current())["longitude"])

    async def test_unfinished_object_deadline_and_oversized_object(self):
        self.receiver.settings = replace(self.settings, idle_seconds=0.1)
        reader, writer = await self.connect()
        writer.write(b'{"text":"'); await writer.drain()

        async def drip():
            try:
                for _ in range(30):
                    writer.write(b"x"); await writer.drain()
                    await asyncio.sleep(0.02)
            except ConnectionError:
                pass

        task = asyncio.create_task(drip())
        try:
            result = json.loads(await asyncio.wait_for(reader.readline(), 0.5))
            self.assertEqual(result["error"], "idle_timeout")
        finally:
            task.cancel()
            await asyncio.gather(task, return_exceptions=True)
        self.receiver.settings = self.settings
        reader, writer = await self.connect()
        writer.write(b'{"gps":[' + b"1," * 4200); await writer.drain()
        self.assertEqual((await self.ack(reader))["error"], "frame_too_large")


class GpsWebSocketTests(unittest.TestCase):
    def test_committed_location_event_and_restart_api_recovery(self):
        with tempfile.TemporaryDirectory() as temp:
            settings = Settings(database=Path(temp)/"gps.db", tcp_port=0, ingest_token="fixture-ingest",
                                gps_imei_bindings={IMEI: DEVICE})
            with TestClient(create_app(settings)) as client:
                with client.websocket_connect("/ws") as ws:
                    ws.send_json({})
                    self.assertEqual(ws.receive_json()["type"], "ready")
                    port = client.app.state.receiver.server.sockets[0].getsockname()[1]
                    with socket.create_connection(("127.0.0.1", port), timeout=3) as connection:
                        connection.sendall(encode_frame(sensor()) + encode_frame(native()))
                        with connection.makefile("rb") as stream:
                            self.assertTrue(json.loads(stream.readline())["ok"])
                        self.assertEqual(ws.receive_json()["type"], "reading")
                        event = ws.receive_json()
                        self.assertEqual(event["type"], "location")
                        self.assertEqual(event["location"]["longitude"], 114.384038)
                        self.assertNotIn(IMEI, json.dumps(event))
                        before = client.get("/api/location", params={"device_id": DEVICE}).json()
                        sensor_seen = client.get("/api/overview").json()["devices"][0]["received_at"]
                        connection.sendall(encode_frame({**native(), "gps": [0, 0]}))
                        error = ws.receive_json()
                        self.assertTrue(error["location"]["latest_report_error"])
                        self.assertEqual(client.get("/api/overview").json()["devices"][0]["received_at"], sensor_seen)
            with TestClient(create_app(settings)) as restarted:
                after = restarted.get("/api/location", params={"device_id": DEVICE}).json()
                self.assertEqual(after["fix_time_utc"], before["fix_time_utc"])
                self.assertEqual(after["longitude"], before["longitude"])
                self.assertTrue(after["latest_report_error"])


if __name__ == "__main__":
    unittest.main()
