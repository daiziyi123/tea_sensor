"""Rain V2 optional extension: all stores/listeners use temp fixtures."""
import copy
import csv
import io
import json
import math
import unittest
from datetime import datetime,timedelta,timezone
from fastapi.testclient import TestClient
from app.protocol import parse_frame,encode_frame
import test_v2 as legacy
snapshot=legacy.snapshot

def rain_snapshot(seq=1,value=4.0,status="OK"):
    message=snapshot(seq)
    message["data"]["rain_intensity_mm_min"]=dict(value=value,status=status,sample_seq=seq,sample_t_ms=seq*3000,age_ms=0)
    if value is not None:message["data"]["rain_intensity_mm_min"]["range"]="OK"
    return message

class RainTests(unittest.TestCase):
    setUp=legacy.V2Tests.setUp
    tearDown=legacy.V2Tests.tearDown
    send=legacy.V2Tests.send
    get=legacy.V2Tests.get

    def test_legacy_ten_missing_rain_has_no_point_or_zero(self):
        self.assertTrue(self.send(snapshot())["ok"])
        current=self.get("/api/overview")["devices"][0]["data"]["rain_intensity_mm_min"]
        self.assertEqual(current["status"],"NO_DATA")
        for key in ("value","sample_seq","sample_t_ms","age_ms"):self.assertIsNone(current[key])
        self.assertEqual(self.get("/api/measurements",metric="rain_intensity_mm_min")["items"],[])
        self.assertEqual(self.get("/api/overview")["counts"]["measurements"],10)
        self.assertEqual(self.get("/api/readings")["items"][0]["data"]["rain_intensity_mm_min"]["status"],"NO_DATA")

    def test_new_eleven_frame_ws_api_csv_and_unit(self):
        with self.client.websocket_connect("/ws") as ws:
            ws.send_json({"token":"fixture-view"})
            self.assertEqual(ws.receive_json()["type"],"ready")
            ack=self.send(rain_snapshot(),True)
            event=ws.receive_json()
            self.assertEqual(event["type"],"reading")
            self.assertEqual(event["reading"]["data"]["rain_intensity_mm_min"]["value"],4)
        self.assertTrue(ack["ok"])
        config=self.get("/api/config")
        self.assertEqual(len(config["metrics"]),11)
        spec=config["metrics"]["rain_intensity_mm_min"]
        self.assertEqual((spec["unit"],spec["minimum"],spec["maximum"],spec["period_ms"],spec["stale_ms"]),("mm/min",0,8,3000,9000))
        point=self.get("/api/measurements",metric="rain_intensity_mm_min")["items"][0]
        self.assertEqual((point["value"],point["unit"],point["sample_seq"],point["sample_t_ms"]),(4,"mm/min",1,3000))
        rows=list(csv.DictReader(io.StringIO(self.client.get("/api/export.csv",params={"metric":"rain_intensity_mm_min"},headers=self.headers).content.decode("utf-8-sig"))))
        self.assertEqual(len(rows),1)
        self.assertEqual((float(rows[0]["value"]),rows[0]["unit"]),(4,"mm/min"))
        self.assertEqual(self.get("/api/overview")["counts"]["measurements"],11)

    def test_rain_strict_validation_and_valid_zero(self):
        for value in (True,False,float("nan"),float("inf"),-0.01,8.01):
            message=rain_snapshot(value=value)
            with self.assertRaises(ValueError):parse_frame(encode_frame(message))
        for change in (
            lambda m:m["data"]["rain_intensity_mm_min"].update(value=None),
            lambda m:m["data"]["rain_intensity_mm_min"].update(status="TIMEOUT"),
            lambda m:m["data"].update(rain_intensity_mm_min=None),
            lambda m:m["data"]["rain_intensity_mm_min"].update(sample_seq=True),
            lambda m:m["data"].update(rainfall_mm={}),
        ):
            message=rain_snapshot();change(message)
            with self.assertRaises(ValueError):parse_frame(encode_frame(message))
        message=rain_snapshot(value=0)
        message["data"]["rain_intensity_mm_min"]["range"]="LOW"
        self.assertTrue(self.send(message)["ok"])
        current=self.get("/api/overview")["devices"][0]["data"]["rain_intensity_mm_min"]
        self.assertEqual((current["value"],current["status"],current["range"]),(0,"OK","LOW"))

    def test_cached_identity_new_equal_sample_failure_and_stale(self):
        first=rain_snapshot();self.send(first)
        second=rain_snapshot(2)
        second["data"]["rain_intensity_mm_min"]=copy.deepcopy(first["data"]["rain_intensity_mm_min"])
        second["data"]["rain_intensity_mm_min"]["age_ms"]=3000
        self.send(second)
        self.assertEqual(len(self.get("/api/measurements",metric="rain_intensity_mm_min")["items"]),1)
        self.send(rain_snapshot(3))
        self.assertEqual(len(self.get("/api/measurements",metric="rain_intensity_mm_min")["items"]),2)
        self.send(rain_snapshot(4,None,"WINDOW_INSUFFICIENT"))
        current=self.get("/api/overview")["devices"][0]["data"]["rain_intensity_mm_min"]
        self.assertIsNone(current["value"]);self.assertEqual(current["status"],"WINDOW_INSUFFICIENT")
        later=rain_snapshot(5)
        later["data"]["rain_intensity_mm_min"]=copy.deepcopy(first["data"]["rain_intensity_mm_min"])
        later["data"]["rain_intensity_mm_min"]["age_ms"]=12000
        self.send(later)
        self.assertEqual(self.get("/api/overview")["devices"][0]["data"]["rain_intensity_mm_min"]["status"],"WINDOW_INSUFFICIENT")
        self.send(rain_snapshot(6))
        with self.store.connect() as db:
            db.execute("UPDATE measurements SET observed_at=? WHERE metric=?",( (datetime.now(timezone.utc)-timedelta(seconds=10)).isoformat(),"rain_intensity_mm_min"))
        self.assertEqual(self.get("/api/overview")["devices"][0]["data"]["rain_intensity_mm_min"]["status"],"STALE")

    def test_preupgrade_payload_ids_schema_and_boot_retry_preserved(self):
        message=snapshot();message["boot_id"]="fixture-legacy"
        ack=self.send(message)
        # Emulate the actual persisted pre-extension V2 shape, only in this temp DB.
        with self.store.connect() as db:
            payload=json.loads(db.execute("SELECT payload FROM readings WHERE id=?",(ack["id"],)).fetchone()[0])
            payload["data"].pop("rain_intensity_mm_min",None)
            original=json.dumps(payload,ensure_ascii=False)
            db.execute("UPDATE readings SET payload=? WHERE id=?",(original,ack["id"]))
        self.store.initialize();self.store.initialize()
        retry=self.send(message)
        self.assertTrue(retry["ok"] and retry["duplicate"])
        self.assertEqual(retry["id"],ack["id"])
        with self.store.connect() as db:
            self.assertEqual(db.execute("PRAGMA user_version").fetchone()[0],4)
            self.assertEqual(db.execute("SELECT payload FROM readings WHERE id=?",(ack["id"],)).fetchone()[0],original)
            self.assertEqual(db.execute("SELECT count(*) FROM measurements WHERE metric='rain_intensity_mm_min'").fetchone()[0],0)
            self.assertEqual(db.execute("SELECT count(*) FROM readings").fetchone()[0],1)
