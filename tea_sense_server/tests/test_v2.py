"""V2 integration and incremental migration tests; only fixture tokens/temp DBs."""
import copy
import csv
import io
import json
import socket
import sqlite3
import tempfile
import unittest
from pathlib import Path
from fastapi.testclient import TestClient
from app.config import Settings
from app.main import create_app
from app.protocol import METRICS, V2_EXAMPLE, Reading, ReadingV2, encode_frame, parse_frame
from app.storage import Store


def snapshot(seq=1):
    result=copy.deepcopy(V2_EXAMPLE)
    result["data"].pop("rain_intensity_mm_min",None)  # explicit legacy V2 fixture
    result.update(seq=seq,t_ms=seq*3000,token="fixture-ingest")
    values=dict(soil_moisture_pct=60.85,wind_speed_mps=15.0,wind_direction_deg=134.83,
                soil_ph=6.0,temperature_c=19.4,air_humidity_rh_pct=70.8,light_lux=1249,
                pressure_kpa=103.73,pm25_ug_m3=22,pm10_ug_m3=23)
    for key,value in values.items():
        source=seq if METRICS[key]["group"]=="main" else 10
        tick=seq*3000 if METRICS[key]["group"]=="main" else 1000
        result["data"][key]=dict(value=value,status="OK",sample_seq=source,sample_t_ms=tick,age_ms=seq*3000-tick)
    return result


class V2Tests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.path=Path(self.temp.name)/"isolated.db"
        self.app=create_app(Settings(database=self.path,tcp_port=0,ingest_token="fixture-ingest",view_token="fixture-view"))
        self.client=TestClient(self.app)
        self.client.__enter__()
        self.headers={"X-View-Token":"fixture-view"}
        self.port=self.app.state.receiver.server.sockets[0].getsockname()[1]
        self.store=self.app.state.store

    def tearDown(self):
        self.client.__exit__(None,None,None)
        self.temp.cleanup()

    def send(self,message,fragment=False):
        wire=encode_frame(message)
        with socket.create_connection(("127.0.0.1",self.port),timeout=3) as connection:
            if fragment:
                for start in range(0,len(wire),17):
                    connection.sendall(wire[start:start+17])
            else:
                connection.sendall(wire)
            with connection.makefile("rb") as stream:
                return json.loads(stream.readline())

    def get(self,path,**params):
        return self.client.get(path,params=params,headers=self.headers).json()

    def test_ten_values_and_public_api_csv(self):
        ack=self.send(snapshot(),True)
        self.assertTrue(ack["ok"])
        rows=self.get("/api/readings")["items"]
        self.assertEqual(rows[0]["id"],ack["id"])
        self.assertEqual(rows[0]["schema_version"],2)
        self.assertEqual(rows[0]["channel"],"MULTI")
        self.assertIsNone(rows[0]["adc_raw"])
        self.assertEqual(len(rows[0]["data"]),11)
        points=self.get("/api/measurements")["items"]
        self.assertEqual(len(points),10)
        self.assertEqual({p["metric"] for p in points},set(METRICS)-{"rain_intensity_mm_min"})
        current=self.get("/api/overview")["devices"][0]["data"]
        self.assertEqual(current["pressure_kpa"]["value"],103.73)
        self.assertEqual(self.get("/api/overview")["counts"]["total"],1)
        exported=self.client.get("/api/export.csv",headers=self.headers)
        records=list(csv.DictReader(io.StringIO(exported.content.decode("utf-8-sig"))))
        self.assertEqual(len(records),10)
        for forbidden in ("adc_raw","input_mv","token","fixture-ingest"):
            self.assertNotIn(forbidden,exported.text)
            self.assertNotIn(forbidden,json.dumps(rows[0]["data"]))
        config=self.get("/api/config")
        self.assertEqual(len(config["metrics"]),11)
        self.assertEqual(config["metrics"]["soil_ph"]["unit"],"")
        self.assertNotIn("fixture-ingest",json.dumps(config))
        self.assertEqual(self.client.get("/api/measurements").status_code,401)

    def test_strict_values_and_complete_fixed_keys(self):
        for modify in (
            lambda m:m["data"]["soil_ph"].update(value=True),
            lambda m:m["data"]["soil_ph"].update(value=0),
            lambda m:m["data"]["pressure_kpa"].update(value=1037.3),
            lambda m:m["data"]["wind_direction_deg"].update(value=360),
            lambda m:m["data"]["temperature_c"].update(value=None),
            lambda m:m["data"]["soil_ph"].update(status="PENDING"),
            lambda m:m["data"]["soil_ph"].update(status="TIMEOUT"),
            lambda m:m.update(schema_version=2.0),
            lambda m:m.update(seq=True),
            lambda m:m["data"].pop("pm10_ug_m3"),
            lambda m:m["data"].update(co2_ppm={}),
            lambda m:m["data"]["soil_ph"].update(age_ms=True),
        ):
            message=snapshot()
            modify(message)
            with self.assertRaises(ValueError):
                parse_frame(encode_frame(message))
        raw=encode_frame(snapshot()).replace(b'"value":6.0',b'"value":NaN')
        with self.assertRaises(ValueError):
            parse_frame(raw)
        raw=encode_frame(snapshot()).replace(b'"schema_version":2',b'"schema_version":2,"schema_version":2')
        with self.assertRaises(ValueError):
            parse_frame(raw)

    def test_independent_field_failure_zero_and_no_data(self):
        message=snapshot()
        message["data"]["temperature_c"].update(value=None,status="INVALID_VALUE")
        message["data"]["wind_speed_mps"]["value"]=0
        message["data"]["pressure_kpa"]=dict(value=None,status="NO_DATA",sample_seq=None,sample_t_ms=None,age_ms=None)
        self.assertTrue(self.send(message)["ok"])
        current=self.get("/api/overview")["devices"][0]["data"]
        self.assertIsNone(current["temperature_c"]["value"])
        self.assertEqual(current["air_humidity_rh_pct"]["value"],70.8)
        self.assertEqual(current["wind_speed_mps"]["value"],0)
        self.assertEqual(current["pressure_kpa"]["status"],"NO_DATA")

    def test_cached_result_dedup_and_age_never_reset(self):
        self.send(snapshot(1))
        self.send(snapshot(2))
        counts=self.get("/api/overview")["counts"]
        self.assertEqual(counts["total"],2)
        self.assertEqual(counts["measurements"],14)
        points=self.get("/api/measurements",metric="temperature_c")["items"]
        self.assertEqual(len(points),1)
        current=self.get("/api/overview")["devices"][0]["data"]
        self.assertGreaterEqual(current["temperature_c"]["age_ms"],5000)
        self.assertLess(current["temperature_c"]["age_ms"],7000)
        message=snapshot(5)
        for key in ("temperature_c","air_humidity_rh_pct"):
            message["data"][key].update(sample_seq=11,sample_t_ms=11000,age_ms=4000)
        self.send(message)
        self.assertEqual(len(self.get("/api/measurements",metric="temperature_c")["items"]),2)

    def test_new_failure_overrides_cache_and_stale_is_per_metric(self):
        self.send(snapshot(1))
        message=snapshot(2)
        message["data"]["temperature_c"].update(value=None,status="TIMEOUT",sample_seq=11,sample_t_ms=6000,age_ms=0)
        self.send(message)
        current=self.get("/api/overview")["devices"][0]["data"]
        self.assertEqual(current["temperature_c"]["status"],"TIMEOUT")
        self.assertIsNone(current["temperature_c"]["value"])
        self.assertEqual(current["air_humidity_rh_pct"]["status"],"OK")
        message=snapshot(12)
        self.send(message)
        current=self.get("/api/overview")["devices"][0]["data"]
        self.assertEqual(current["air_humidity_rh_pct"]["status"],"STALE")
        self.assertEqual(current["pressure_kpa"]["status"],"OK")

    def test_boot_retry_conflict_transaction_and_no_boot_upload_counts(self):
        message=snapshot()
        message["boot_id"]="fixture-boot"
        first=self.send(message)
        duplicate=self.send(message)
        self.assertEqual(first["id"],duplicate["id"])
        self.assertTrue(duplicate["duplicate"])
        message["data"]["soil_ph"]["value"]=7
        self.assertEqual(self.send(message)["error"],"sequence_conflict")
        self.assertEqual(self.store.counts()["total"],1)
        message=snapshot(2)
        # Same source identity with conflicting weather values must roll back the whole upload.
        message["boot_id"]="fixture-boot"
        message["data"]["light_lux"]["value"]=99
        self.assertEqual(self.send(message)["error"],"sequence_conflict")
        self.assertEqual(self.store.counts()["total"],1)
        message=snapshot()
        self.send(message);self.send(message)
        self.assertEqual(self.store.counts()["total"],3)

    def test_run_boundaries_wrap_reconnect_and_old_result_order(self):
        self.send(snapshot(25))
        first=self.store.latest()[0]["run_id"]
        self.send(snapshot(26))
        self.assertEqual(self.store.latest()[0]["run_id"],first)
        self.send(snapshot(1))
        self.assertNotEqual(self.store.latest()[0]["run_id"],first)
        self.assertEqual(len(self.store.measurements(metric="temperature_c")),2)
        self.send(snapshot(20))
        self.send(snapshot(21))
        old=snapshot(19)
        old["data"]["soil_moisture_pct"]["value"]=2
        self.send(old)
        self.assertEqual(self.store.current("demo_h743_001")["soil_moisture_pct"]["value"],60.85)
        # Counter/tick wrap is forward progress, not a restart.
        message=snapshot()
        message.update(device_id="demo_wrap",seq=4294967294,t_ms=4294967290)
        for key in message["data"]:
            item=message["data"][key]
            item.update(sample_seq=4294967294,sample_t_ms=4294967290,age_ms=0)
        self.send(message)
        run=next(r["run_id"] for r in self.store.latest() if r["device_id"]=="demo_wrap")
        message.update(seq=0,t_ms=2)
        for item in message["data"].values():
            item.update(sample_seq=0,sample_t_ms=2,age_ms=0)
        self.send(message)
        self.assertEqual(next(r["run_id"] for r in self.store.latest() if r["device_id"]=="demo_wrap"),run)

    def test_measurement_pagination_metric_and_csv(self):
        self.send(snapshot(1))
        self.send(snapshot(2))
        page=self.get("/api/measurements",metric="soil_ph",limit=1)
        self.assertTrue(page["has_more"])
        older=self.get("/api/measurements",metric="soil_ph",limit=1,before_id=page["next_before_id"])
        self.assertLess(older["items"][0]["id"],page["items"][0]["id"])
        export=self.client.get("/api/export.csv",params={"metric":"soil_ph"},headers=self.headers)
        rows=list(csv.DictReader(io.StringIO(export.content.decode("utf-8-sig"))))
        self.assertEqual(len(rows),2)
        self.assertTrue(all(row["metric"]=="soil_ph" and row["unit"]=="" for row in rows))
        self.assertEqual(self.client.get("/api/measurements",params={"metric":"co2_ppm"},headers=self.headers).status_code,422)

    def test_migration_preserves_old_ids_payload_and_is_idempotent(self):
        path=Path(self.temp.name)/"legacy.db"
        reading=Reading(device_id="legacy",seq=1,t_ms=3000,adc_raw=0,moisture_pct=0)
        payload=json.dumps(reading.model_dump(exclude_none=True))
        with sqlite3.connect(path) as db:
            db.execute("CREATE TABLE readings(id INTEGER PRIMARY KEY,received_at TEXT NOT NULL,device_id TEXT NOT NULL,boot_id TEXT,seq INTEGER NOT NULL,t_ms INTEGER NOT NULL,channel TEXT NOT NULL,adc_raw INTEGER,moisture_pct REAL,status TEXT NOT NULL,range TEXT,payload TEXT NOT NULL,UNIQUE(device_id,boot_id,seq))")
            db.execute("INSERT INTO readings VALUES(42,'2026-11-14T00:00:00+00:00','legacy',NULL,1,3000,'AD0',0,0,'OK',NULL,?)",(payload,))
        db.close()
        store=Store(path)
        store.initialize();store.initialize()
        self.assertEqual(store.counts()["total"],1)
        self.assertEqual(store.counts()["measurements"],1)
        with store.connect() as db:
            row=db.execute("SELECT id,payload FROM readings").fetchone()
            self.assertEqual(row["id"],42)
            self.assertEqual(row["payload"],payload)
            self.assertEqual(db.execute("PRAGMA user_version").fetchone()[0],4)
        self.assertEqual(store.measurements()[0]["value"],0)
        self.assertEqual(store.measurements()[0]["metric"],"soil_moisture_pct")


if __name__=="__main__":
    unittest.main()
