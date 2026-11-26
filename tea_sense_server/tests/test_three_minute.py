"""Current statistics contract; legacy fixtures/tests keep their original 3s meaning."""
import copy
import asyncio
import csv
import io
import json
import socket
import tempfile
import unittest
from datetime import datetime, timezone, timedelta
from pathlib import Path
from unittest.mock import patch
from types import SimpleNamespace
from fastapi.testclient import TestClient
from app.config import Settings
from app.main import create_app
from app.protocol import METRICS, ReadingV2, encode_frame
from app.storage import Store


def frame(seq=1):
    end=seq*180000
    data={}
    methods=dict(soil_moisture_pct="median",soil_ph="median",wind_speed_mps="mean",
                 wind_direction_deg="circular_mean",rain_intensity_mm_min="time_weighted_mean")
    values=dict(soil_moisture_pct=20,soil_ph=6,wind_speed_mps=3,wind_direction_deg=0,
                rain_intensity_mm_min=.33,temperature_c=19.4,air_humidity_rh_pct=70.8,
                light_lux=1249,pressure_kpa=103.73,pm25_ug_m3=22,pm10_ug_m3=23)
    for key in METRICS:
        method=methods.get(key,"latest")
        item=dict(value=values[key],status="OK",sample_seq=seq,sample_t_ms=end,age_ms=0,
                  statistics=dict(method=method))
        if method=="latest":
            item.update(cached=False,latest_query_status="OK",latest_query_seq=seq,latest_query_t_ms=end)
        else:
            short=method=="median"
            item["statistics"].update(window_ms=5000 if short else 180000,start_ms=end-(5000 if short else 180000),
                                      end_ms=end,expected_count=5 if short else 1800,valid_count=5 if short else 1800,
                                      used_count=1 if short else 1800)
            if method=="time_weighted_mean": item["statistics"]["valid_duration_ms"]=180000
        if key=="wind_speed_mps": item["wind_speed_sample_max_mps"]=4
        data[key]=item
    return dict(schema_version=2,device_id="demo_statistics",seq=seq,t_ms=end,window_ms=180000,
                data=data,token="fixture-ingest",boot_id="fixture-statistics")


class ThreeMinuteTests(unittest.TestCase):
    def test_methods_partial_direction_null_and_peak(self):
        value=frame()
        for state in ("CALM","VARIABLE","WINDOW_INSUFFICIENT"):
            value["data"]["wind_direction_deg"].update(value=None,status=state)
            self.assertIsNone(ReadingV2.model_validate(value).data.wind_direction_deg.value)
        wind=value["data"]["wind_speed_mps"]
        wind.update(status="PARTIAL");wind["statistics"].update(valid_count=1440,used_count=1440)
        self.assertEqual(ReadingV2.model_validate(value).data.wind_speed_mps.value,3)
        for mutation in (lambda v:v["data"]["soil_ph"].pop("statistics"),
                         lambda v:v["data"]["wind_speed_mps"]["statistics"].update(valid_count=1439),
                         lambda v:v["data"]["soil_ph"]["statistics"].update(method="mean"),
                         lambda v:v["data"]["soil_ph"].update(value=None,status="CALM"),
                         lambda v:v["data"]["wind_speed_mps"].update(wind_speed_sample_max_mps=None),
                         lambda v:v["data"]["rain_intensity_mm_min"]["statistics"].update(valid_duration_ms=144000)):
            invalid=frame();mutation(invalid)
            with self.assertRaises(ValueError): ReadingV2.model_validate(invalid)

    def test_snapshot_quality_frozen_and_cached_source_not_duplicated(self):
        with tempfile.TemporaryDirectory() as directory:
            store=Store(Path(directory)/"isolated.db");store.initialize()
            first=frame();row,_=store.insert(ReadingV2.model_validate(first))
            second=frame(2);pressure=second["data"]["pressure_kpa"]
            pressure.update(sample_seq=1,sample_t_ms=180000,age_ms=180000,cached=True,status="CACHED",
                            latest_query_status="CRC_ERROR")
            row,_=store.insert(ReadingV2.model_validate(second))
            self.assertEqual(len(store.measurements(metric="pressure_kpa")),1)
            with store.connect() as db:
                old=(datetime.now(timezone.utc)-timedelta(seconds=179)).isoformat()
                db.execute("UPDATE readings SET received_at=? WHERE id=?",(old,row["id"]))
            current=store.current("demo_statistics")
            self.assertEqual(current["temperature_c"]["status"],"OK")
            self.assertTrue(current["temperature_c"]["quality_frozen"])
            self.assertGreater(current["temperature_c"]["age_ms"],178000)
            self.assertEqual((current["pressure_kpa"]["status"],current["pressure_kpa"]["latest_query_status"]),("CACHED","CRC_ERROR"))
            exported=list(csv.DictReader(io.StringIO(store.csv())))
            self.assertIn("statistics",exported[0]);self.assertIn("wind_speed_sample_max_mps",exported[0])
            self.assertNotIn("fixture-ingest",store.csv())
            third=frame(3);third["data"]["pressure_kpa"].update(value=None,status="STALE",sample_seq=1,sample_t_ms=180000,age_ms=360000,cached=True)
            store.insert(ReadingV2.model_validate(third))
            self.assertEqual(store.current("demo_statistics")["pressure_kpa"]["status"],"STALE")
            late=frame(2);late["seq"]=4
            # A newly received older window must not replace source-current data.
            store.insert(ReadingV2.model_validate(late))
            self.assertEqual(store.current("demo_statistics")["pressure_kpa"]["status"],"STALE")

    def test_schema3_additive_migration_does_not_relabel_history(self):
        from test_v2 import snapshot
        with tempfile.TemporaryDirectory() as directory:
            store=Store(Path(directory)/"migration.db");store.initialize()
            row,_=store.insert(ReadingV2.model_validate(snapshot()))
            with store.connect() as db:
                payload=db.execute("SELECT payload FROM readings").fetchone()[0]
                db.execute("ALTER TABLE measurements DROP COLUMN result_metadata")
                db.execute("PRAGMA user_version=3")
            store.initialize();store.initialize()
            with store.connect() as db:
                self.assertEqual(db.execute("PRAGMA user_version").fetchone()[0],4)
                self.assertEqual(db.execute("SELECT payload FROM readings").fetchone()[0],payload)
            self.assertEqual(store.history()[0]["window_ms"],3000)
            self.assertNotIn("statistics",store.measurements()[0])

    def test_tcp_ack_api_push_and_540_seconds(self):
        with tempfile.TemporaryDirectory() as directory:
            settings=Settings(database=Path(directory)/"api.db",tcp_port=0,ingest_token="fixture-ingest")
            self.assertEqual((settings.online_seconds,settings.authenticated_idle_seconds,settings.frame_timeout_seconds,
                              settings.idle_seconds,settings.gps_stale_seconds),(540,600,300,300,540))
            app=create_app(settings)
            with TestClient(app) as client:
                port=app.state.receiver.server.sockets[0].getsockname()[1]
                with client.websocket_connect("/ws") as ws:
                    ws.send_json({"token":""});self.assertEqual(ws.receive_json()["type"],"ready")
                    with socket.create_connection(("127.0.0.1",port),timeout=3) as connection:
                        connection.sendall(encode_frame(frame()))
                        stream=connection.makefile("rb");ack=json.loads(stream.readline())
                        self.assertTrue(ack["ok"]);self.assertEqual(ws.receive_json()["type"],"reading")
                        connection.sendall(encode_frame(frame()));self.assertTrue(json.loads(stream.readline())["duplicate"]);stream.close()
                    overview=client.get("/api/overview").json();device=overview["devices"][0]
                    self.assertTrue(device["online"]);self.assertEqual(device["data"]["wind_speed_mps"]["wind_speed_sample_max_mps"],4)
                    config=client.get("/api/config").json()
                    self.assertEqual((config["online_seconds"],config["business_window_ms"],config["example"]["window_ms"]),(540,180000,180000))
                    ReadingV2.model_validate(config["example"])
                    self.assertEqual(config["legacy_v2_example"]["window_ms"],3000)
                    self.assertEqual(device["location"]["status"],"NO_DATA")
                self.assertEqual(app.state.receiver.accepted,1)


class ConnectionTimingTests(unittest.IsolatedAsyncioTestCase):
    async def test_authenticated_idle_and_incomplete_frame_are_separate(self):
        from app.tcp_server import TcpReceiver
        with tempfile.TemporaryDirectory() as directory:
            store=Store(Path(directory)/"clock.db");store.initialize()
            receiver=TcpReceiver(Settings(database=store.path,ingest_token="fixture-ingest"),store,lambda _:None)
            clock=[1000.0];timeouts=[]
            class Reader:
                chunks=[(0,encode_frame(frame())),(180,b'{"unfinished":'),(1,b'')]
                async def read(self, _):
                    elapsed,data=self.chunks.pop(0);clock[0]+=elapsed;return data
            class Writer:
                output=[]
                def write(self, data): self.output.append(json.loads(data))
                async def drain(self): pass
                def close(self): pass
                async def wait_closed(self): pass
            async def controlled_wait(coro, timeout):
                if coro.cr_code.co_name=="read": timeouts.append(timeout)
                return await coro
            with patch("app.tcp_server.time",SimpleNamespace(monotonic=lambda:clock[0])),patch("app.tcp_server.asyncio.wait_for",controlled_wait):
                writer=Writer();await receiver.handle(Reader(),writer)
            self.assertEqual(timeouts,[300,600,300])
            self.assertTrue(writer.output[0]["ok"])
            self.assertEqual(writer.output[-1]["error"],"incomplete_frame")
            self.assertEqual(receiver.accepted,1)
