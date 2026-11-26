import asyncio
import csv
import io
import json
import sqlite3
import socket
import tempfile
import unittest
from dataclasses import replace
from contextlib import closing
from pathlib import Path
from unittest.mock import patch

import httpx
from fastapi.testclient import TestClient
from starlette.websockets import WebSocketDisconnect

from app.config import Settings
from app.main import create_app
from app.protocol import EXAMPLE, WINDOW_EXAMPLE, encode_frame


class ConfigTests(unittest.TestCase):
    @patch.dict("os.environ", {}, clear=True)
    def test_listener_overrides_keep_validation(self):
        settings = Settings()
        settings.validate()
        for override in ({"http_host": "0.0.0.0"}, {"http_port": 0},
                         {"http_port": 9000}, {"tcp_host": "0.0.0.0"}):
            with self.assertRaises(ValueError):
                replace(settings, **override).validate()


class LiveUpdateTests(unittest.TestCase):
    def test_websocket_auth_and_committed_reading(self):
        with tempfile.TemporaryDirectory() as directory:
            app = create_app(Settings(database=Path(directory) / "live.db", tcp_port=0,
                                      ingest_token="test-ingest", view_token="test-view"))
            with TestClient(app) as client:
                with client.websocket_connect("/ws") as websocket:
                    websocket.send_json({"token": "wrong"})
                    with self.assertRaises(WebSocketDisconnect):
                        websocket.receive_json()
                with client.websocket_connect("/ws") as websocket:
                    websocket.send_json({"token": "test-view"})
                    self.assertEqual(websocket.receive_json()["type"], "ready")
                    port = app.state.receiver.server.sockets[0].getsockname()[1]
                    with socket.create_connection(("127.0.0.1", port), timeout=3) as connection:
                        connection.sendall(encode_frame({**EXAMPLE, "token": "test-ingest"}))
                        ack = json.loads(connection.makefile("rb").readline())
                    event = websocket.receive_json()
                    self.assertEqual(event["type"], "reading")
                    self.assertEqual(event["reading"]["id"], ack["id"])
                    self.assertNotIn("token", event["reading"])
                    result = client.get("/api/readings", headers={"X-View-Token": "test-view"})
                    self.assertEqual(result.json()["items"][0]["id"], ack["id"])
                    with socket.create_connection(("127.0.0.1", port), timeout=3) as connection:
                        connection.sendall(encode_frame({**WINDOW_EXAMPLE, "seq": 2, "token": "test-ingest"}))
                        with connection.makefile("rb") as stream:
                            window_ack = json.loads(stream.readline())
                    event = websocket.receive_json()["reading"]
                    self.assertEqual(event["id"], window_ack["id"])
                    self.assertEqual((event["sample_count"], event["used_count"]), (30, 28))
                    self.assertNotIn("token", event)


class ServerTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.path = Path(self.temp.name) / "readings.db"
        self.app = create_app(Settings(database=self.path, tcp_port=0,
                                       ingest_token="test-ingest", view_token="test-view"))
        self.lifespan = self.app.router.lifespan_context(self.app)
        await self.lifespan.__aenter__()
        self.port = self.app.state.receiver.server.sockets[0].getsockname()[1]
        self.http = httpx.AsyncClient(transport=httpx.ASGITransport(app=self.app),
                                      base_url="http://test", headers={"X-View-Token": "test-view"})
        self.connections = []

    async def asyncTearDown(self):
        for writer in self.connections:
            writer.close()
            await writer.wait_closed()
        await self.http.aclose()
        await self.lifespan.__aexit__(None, None, None)
        self.temp.cleanup()

    async def connection(self):
        reader, writer = await asyncio.open_connection("127.0.0.1", self.port)
        self.connections.append(writer)
        return reader, writer

    def message(self, **overrides):
        return {**EXAMPLE, "token": "test-ingest", **overrides}

    async def receive(self, reader):
        return json.loads(await asyncio.wait_for(reader.readline(), 2))

    async def send(self, reader, writer, **overrides):
        writer.write(encode_frame(self.message(**overrides)))
        await writer.drain()
        return await self.receive(reader)

    async def test_fragmented_and_coalesced_frames(self):
        reader, writer = await self.connection()
        first = encode_frame(self.message(seq=1))
        writer.write(first[:13]); await writer.drain()
        await asyncio.sleep(0.01)
        self.assertEqual(self.app.state.receiver.accepted, 0)
        writer.write(first[13:] + encode_frame(self.message(seq=2)))
        await writer.drain()
        self.assertEqual((await self.receive(reader))["seq"], 1)
        self.assertEqual((await self.receive(reader))["seq"], 2)
        result = (await self.http.get("/api/readings")).json()
        self.assertEqual([row["seq"] for row in result["items"]], [2, 1])

    async def test_ack_only_after_persistence_and_no_token_storage(self):
        reader, writer = await self.connection()
        ack = await self.send(reader, writer)
        self.assertTrue(ack["ok"])
        with closing(sqlite3.connect(self.path)) as db:
            text = db.execute("SELECT payload FROM readings WHERE id=?", (ack["id"],)).fetchone()[0]
        self.assertNotIn("test-ingest", text)
        self.assertNotIn("token", json.loads(text))

    async def test_auth_and_public_config(self):
        reader, writer = await self.connection()
        self.assertEqual((await self.send(reader, writer, token="wrong"))["error"], "unauthorized")
        response = await self.http.get("/api/readings", headers={"X-View-Token": "wrong"})
        self.assertEqual(response.status_code, 401)
        config = (await self.http.get("/api/config", headers={"X-View-Token": ""})).json()
        self.assertTrue(config["view_auth_required"])
        self.assertNotIn("test-view", json.dumps(config))

    async def test_invalid_frame_then_recovery(self):
        reader, writer = await self.connection()
        writer.write(b"not json\n"); await writer.drain()
        self.assertEqual((await self.receive(reader))["error"], "invalid_message")
        self.assertTrue((await self.send(reader, writer))["ok"])

    async def test_strict_validation_and_nonfinite(self):
        for change in [{"seq": True}, {"moisture_pct": 101}, {"adc_raw": -1},
                       {"moisture_pct": None}, {"unexpected": 1}]:
            reader, writer = await self.connection()
            writer.write(encode_frame(self.message(**change))); await writer.drain()
            self.assertEqual((await self.receive(reader))["error"], "invalid_message")
        reader, writer = await self.connection()
        writer.write(b'{"moisture_pct":NaN}\n'); await writer.drain()
        self.assertEqual((await self.receive(reader))["error"], "invalid_message")

    async def test_failure_data_and_valid_zero(self):
        reader, writer = await self.connection()
        self.assertTrue((await self.send(reader, writer, adc_raw=0, moisture_pct=0.0, range="LOW"))["ok"])
        self.assertTrue((await self.send(reader, writer, seq=2, status="ADC_POLL_FAILED",
                                        adc_raw=None, moisture_pct=None))["ok"])
        rows = (await self.http.get("/api/readings")).json()["items"]
        self.assertIsNone(rows[0]["moisture_pct"])
        self.assertEqual(rows[1]["moisture_pct"], 0)

    async def test_optional_boot_id_deduplicates_and_detects_conflict(self):
        reader, writer = await self.connection()
        first = await self.send(reader, writer, boot_id="boot1")
        again = await self.send(reader, writer, boot_id="boot1")
        self.assertEqual(first["id"], again["id"])
        self.assertTrue(again["duplicate"])
        conflict = await self.send(reader, writer, boot_id="boot1", adc_raw=9)
        self.assertEqual(conflict["error"], "sequence_conflict")
        self.assertTrue((await self.send(reader, writer, boot_id="boot2"))["ok"])

    async def test_without_boot_id_seq_can_restart(self):
        reader, writer = await self.connection()
        await self.send(reader, writer)
        await self.send(reader, writer)
        self.assertEqual((await self.http.get("/api/overview")).json()["counts"]["total"], 2)

    async def test_oversized_and_unterminated_frames(self):
        reader, writer = await self.connection()
        writer.write(b"x" * 9000); await writer.drain()
        self.assertEqual((await self.receive(reader))["error"], "frame_too_large")
        reader, writer = await self.connection()
        writer.write(b'{"device_id":"unfinished"'); await writer.drain(); writer.write_eof()
        self.assertEqual((await self.receive(reader))["error"], "incomplete_frame")
        self.assertEqual(self.app.state.receiver.accepted, 0)

    async def test_pagination_and_csv(self):
        reader, writer = await self.connection()
        for seq in range(5):
            await self.send(reader, writer, seq=seq)
        page = (await self.http.get("/api/readings?limit=2")).json()
        self.assertTrue(page["has_more"])
        second = (await self.http.get(f'/api/readings?limit=2&before_id={page["next_before_id"]}')).json()
        self.assertTrue(set(r["id"] for r in page["items"]).isdisjoint(r["id"] for r in second["items"]))
        exported = await self.http.get("/api/export.csv")
        self.assertTrue(exported.text.startswith("\ufeffid,"))
        self.assertEqual(len(exported.text.splitlines()), 6)

    async def test_rate_limit(self):
        reader, writer = await self.connection()
        writer.write(b"".join(encode_frame(self.message(seq=i)) for i in range(21)))
        await writer.drain()
        results = [await self.receive(reader) for _ in range(21)]
        self.assertEqual(results[-1]["error"], "rate_limited")

    async def test_window_metadata_payload_and_legacy_compatibility(self):
        reader, writer = await self.connection()
        old_ack = await self.send(reader, writer)
        new_ack = await self.send(reader, writer, **{key: value for key, value in WINDOW_EXAMPLE.items()
                                                    if key not in ("device_id", "seq")}, seq=2)
        self.assertTrue(new_ack["ok"])
        rows = (await self.http.get("/api/readings")).json()["items"]
        self.assertEqual(rows[0]["id"], new_ack["id"])
        self.assertEqual([rows[0][key] for key in ("window_ms", "sample_count", "valid_count", "used_count")], [3000, 30, 30, 28])
        self.assertEqual(rows[1]["id"], old_ack["id"])
        self.assertNotIn("window_ms", rows[1])
        overview = (await self.http.get("/api/overview")).json()
        self.assertEqual(overview["devices"][0]["used_count"], 28)
        with closing(sqlite3.connect(self.path)) as db:
            payload = json.loads(db.execute("SELECT payload FROM readings WHERE id=?", (new_ack["id"],)).fetchone()[0])
            columns = [row[1] for row in db.execute("PRAGMA table_info(readings)")]
        self.assertNotIn("window_ms", columns)
        self.assertEqual(payload["sample_count"], 30)
        self.assertNotIn("token", payload)
        exported = (await self.http.get("/api/export.csv")).content.decode("utf-8-sig")
        records = list(csv.DictReader(io.StringIO(exported)))
        self.assertEqual(records[0]["metric"], "soil_moisture_pct")
        self.assertEqual(records[1]["metric"], "soil_moisture_pct")

    async def test_window_quality_failure_zero_and_diagnostics(self):
        reader, writer = await self.connection()
        metadata = {"window_ms": 3000, "sample_count": 30, "valid_count": 24, "used_count": 22,
                    "sample_error_count": 6, "cleanup_error_count": 1, "last_sample_error": "ADC_POLL_FAILED"}
        self.assertTrue((await self.send(reader, writer, adc_raw=0, moisture_pct=0.0, range="LOW", **metadata))["ok"])
        metadata.update(valid_count=23, used_count=0, sample_error_count=7)
        self.assertTrue((await self.send(reader, writer, seq=2, status="WINDOW_INSUFFICIENT", adc_raw=None,
                                        moisture_pct=None, **metadata))["ok"])
        metadata.update(sample_count=0, valid_count=0, used_count=0, sample_error_count=0,
                        cleanup_error_count=0, last_sample_error="OK")
        self.assertTrue((await self.send(reader, writer, seq=3, status="WINDOW_INSUFFICIENT", adc_raw=None,
                                        moisture_pct=None, **metadata))["ok"])
        rows = (await self.http.get("/api/readings")).json()["items"]
        self.assertIsNone(rows[0]["adc_raw"])
        self.assertEqual(rows[1]["last_sample_error"], "ADC_POLL_FAILED")
        self.assertEqual(rows[2]["moisture_pct"], 0.0)

    async def test_window_strict_group_validation(self):
        from app.protocol import Reading
        from pydantic import ValidationError
        invalid = [{"sample_count": 1}, {"window_ms": None}, {"window_ms": 0}, {"window_ms": 3600001},
                   {"sample_count": True}, {"sample_count": -1}, {"sample_count": 1000001},
                   {"valid_count": 31}, {"used_count": 29}, {"valid_count": 2, "used_count": 0},
                   {"sample_error_count": 1}, {"cleanup_error_count": 1}, {"last_sample_error": "ADC_UNKNOWN"},
                   {"status": "WINDOW_INSUFFICIENT"}, {"status": "ADC_STOP_FAILED", "adc_raw": None,
                    "moisture_pct": None, "used_count": 2}]
        for change in invalid:
            with self.subTest(change=change), self.assertRaises(ValidationError):
                Reading.model_validate({**WINDOW_EXAMPLE, **change})
        for change in ({"window_ms": 3000}, {"sample_error_count": 0},
                       {"status": "WINDOW_INSUFFICIENT", "adc_raw": None, "moisture_pct": None}):
            with self.assertRaises(ValidationError):
                Reading.model_validate({**EXAMPLE, **change})


if __name__ == "__main__":
    unittest.main()
