import csv
import io
import json
import sqlite3
from contextlib import contextmanager
from datetime import datetime, timezone, timedelta
from pathlib import Path
from .protocol import Reading, ReadingV2, WINDOW_PUBLIC_FIELDS, METRICS, no_data_result


def utc_now():
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


class Store:
    def __init__(self, path: Path):
        self.path = path

    @contextmanager
    def connect(self):
        db = sqlite3.connect(self.path, timeout=10)
        db.row_factory = sqlite3.Row
        try:
            with db:
                yield db
        finally:
            db.close()

    def initialize(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        with self.connect() as db:
            db.execute("PRAGMA journal_mode=WAL")
            db.executescript("""
                CREATE TABLE IF NOT EXISTS readings (
                  id INTEGER PRIMARY KEY, received_at TEXT NOT NULL,device_id TEXT NOT NULL,
                  boot_id TEXT,seq INTEGER NOT NULL,t_ms INTEGER NOT NULL,channel TEXT NOT NULL,
                  adc_raw INTEGER,moisture_pct REAL,status TEXT NOT NULL,range TEXT,payload TEXT NOT NULL,
                  UNIQUE(device_id,boot_id,seq));
                CREATE INDEX IF NOT EXISTS ix_readings_device_time ON readings(device_id,received_at DESC,id DESC);
                CREATE INDEX IF NOT EXISTS ix_readings_time ON readings(received_at DESC,id DESC);
            """)
            columns={row["name"] for row in db.execute("PRAGMA table_info(readings)")}
            for name,declaration in (("schema_version","INTEGER NOT NULL DEFAULT 1"),("run_id","INTEGER")):
                if name not in columns:
                    db.execute("ALTER TABLE readings ADD COLUMN "+name+" "+declaration)
            db.executescript("""
                CREATE TABLE IF NOT EXISTS device_runs(
                  id INTEGER PRIMARY KEY,device_id TEXT NOT NULL,boot_id TEXT,last_seq INTEGER NOT NULL,
                  last_tick INTEGER NOT NULL,tick_unwrapped INTEGER NOT NULL,reason TEXT NOT NULL);
                CREATE TABLE IF NOT EXISTS measurements(
                  id INTEGER PRIMARY KEY,reading_id INTEGER NOT NULL REFERENCES readings(id),
                  run_id INTEGER NOT NULL REFERENCES device_runs(id),device_id TEXT NOT NULL,
                  metric TEXT NOT NULL,value REAL,unit TEXT NOT NULL,status TEXT NOT NULL,range TEXT,
                  sample_seq INTEGER NOT NULL,sample_t_ms INTEGER NOT NULL,age_ms INTEGER NOT NULL,
                  received_at TEXT NOT NULL,observed_at TEXT NOT NULL,source_order INTEGER NOT NULL,
                  identity TEXT NOT NULL UNIQUE);
                CREATE INDEX IF NOT EXISTS ix_measurements_history ON measurements(device_id,metric,received_at DESC,id DESC);
                CREATE INDEX IF NOT EXISTS ix_measurements_current ON measurements(run_id,metric,source_order DESC,id DESC);
            """)
            if db.execute("PRAGMA user_version").fetchone()[0]<2:
                for row in db.execute("SELECT * FROM readings WHERE schema_version=1 ORDER BY id").fetchall():
                    if db.execute("SELECT 1 FROM measurements WHERE identity=?",(f'legacy:{row["id"]}',)).fetchone():
                        continue
                    payload=json.loads(row["payload"])
                    payload.pop("token",None)
                    self._measure(db,Reading.model_validate(payload),row["id"],row["received_at"],legacy=True)
                db.execute("PRAGMA user_version=2")

    @staticmethod
    def _signed_delta(new, old):
        return ((new-old+2147483648)%4294967296)-2147483648

    def _run(self, db, reading):
        if reading.boot_id is not None:
            row=db.execute("SELECT * FROM device_runs WHERE device_id=? AND boot_id=? ORDER BY id DESC LIMIT 1",
                           (reading.device_id,reading.boot_id)).fetchone()
        else:
            row=db.execute("SELECT * FROM device_runs WHERE device_id=? ORDER BY id DESC LIMIT 1",
                           (reading.device_id,)).fetchone()
            if row and row["boot_id"] is not None:
                row=None
        reason="boot_id" if reading.boot_id else "initial"
        # A replay and an MCU restart can be indistinguishable without boot_id.
        # Reconnection alone never changes a run. Infer only an early-boot rewind
        # of both counters after an established run; keep other old frames ordered.
        if (row and reading.boot_id is None and reading.seq<=3 and reading.t_ms<=30000
            and row["last_seq"]>=5 and row["last_tick"]>=60000
            and self._signed_delta(reading.seq,row["last_seq"])<0
            and self._signed_delta(reading.t_ms,row["last_tick"])<0):
            row=None
            reason="inferred_counter_reset"
        if row is None:
            cursor=db.execute("INSERT INTO device_runs(device_id,boot_id,last_seq,last_tick,tick_unwrapped,reason) VALUES(?,?,?,?,?,?)",
                (reading.device_id,reading.boot_id,reading.seq,reading.t_ms,reading.t_ms,reason))
            return cursor.lastrowid,reading.t_ms
        tick=row["tick_unwrapped"]+self._signed_delta(reading.t_ms,row["last_tick"])
        if self._signed_delta(reading.seq,row["last_seq"])>0 and self._signed_delta(reading.t_ms,row["last_tick"])>=0:
            db.execute("UPDATE device_runs SET last_seq=?,last_tick=?,tick_unwrapped=? WHERE id=?",
                       (reading.seq,reading.t_ms,tick,row["id"]))
        return row["id"],tick

    def _measure(self, db, reading, reading_id, received_at, legacy=False):
        run,tick=self._run(db,reading)
        db.execute("UPDATE readings SET run_id=? WHERE id=?",(run,reading_id))
        if legacy:
            data={"soil_moisture_pct":dict(value=reading.moisture_pct,status=reading.status,
                sample_seq=reading.seq,sample_t_ms=reading.t_ms,age_ms=0,range=reading.range)}
        else:
            data=reading.data.model_dump()
        for metric,item in data.items():
            if item["status"]=="NO_DATA":
                continue
            identity=f"legacy:{reading_id}" if legacy else f'{run}:{metric}:{item["sample_seq"]}:{item["sample_t_ms"]}'
            prior=db.execute("SELECT * FROM measurements WHERE identity=?",(identity,)).fetchone()
            if prior:
                observed=(datetime.fromisoformat(received_at)-timedelta(milliseconds=item["age_ms"])).isoformat(timespec="milliseconds")
                if observed<prior["observed_at"]:
                    db.execute("UPDATE measurements SET observed_at=? WHERE id=?",(observed,prior["id"]))
                # Aging a cached OK result does not create a new measurement.
                if item["status"]!="STALE" and (prior["status"]!=item["status"] or prior["value"]!=item["value"]
                                               or prior["range"]!=item.get("range")):
                    raise ValueError("sequence_conflict")
                continue
            observed=(datetime.fromisoformat(received_at)-timedelta(milliseconds=item["age_ms"])).isoformat(timespec="milliseconds")
            order=tick+self._signed_delta(item["sample_t_ms"],reading.t_ms)
            db.execute("""INSERT INTO measurements
                (reading_id,run_id,device_id,metric,value,unit,status,range,sample_seq,sample_t_ms,age_ms,received_at,observed_at,source_order,identity)
                VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)""",
                (reading_id,run,reading.device_id,metric,item["value"],METRICS[metric]["unit"],item["status"],
                 item.get("range"),item["sample_seq"],item["sample_t_ms"],item["age_ms"],received_at,observed,order,identity))

    def insert(self, reading):
        v2=isinstance(reading,ReadingV2)
        payload=reading.model_dump(exclude_none=not v2)
        received=utc_now()
        values=(received,reading.device_id,reading.boot_id,reading.seq,reading.t_ms,
                "MULTI" if v2 else reading.channel,None if v2 else reading.adc_raw,
                None if v2 else reading.moisture_pct,"SNAPSHOT" if v2 else reading.status,
                None if v2 else reading.range,json.dumps(payload,ensure_ascii=False),2 if v2 else 1)
        with self.connect() as db:
            cursor=db.execute("""INSERT OR IGNORE INTO readings
                (received_at,device_id,boot_id,seq,t_ms,channel,adc_raw,moisture_pct,status,range,payload,schema_version)
                VALUES(?,?,?,?,?,?,?,?,?,?,?,?)""",values)
            duplicate=cursor.rowcount==0
            if duplicate:
                row=db.execute("SELECT * FROM readings WHERE device_id=? AND boot_id=? AND seq=?",
                               (reading.device_id,reading.boot_id,reading.seq)).fetchone()
                stored=json.loads(row["payload"])
                # Old persisted V2 lacks the optional rain object. Compare
                # normalized models, without rewriting original payload/history.
                if v2:
                    stored=ReadingV2.model_validate(stored).model_dump()
                if stored!=payload:
                    raise ValueError("sequence_conflict")
            else:
                self._measure(db,reading,cursor.lastrowid,received,legacy=not v2)
                row=db.execute("SELECT * FROM readings WHERE id=?",(cursor.lastrowid,)).fetchone()
        return self.public(row),duplicate

    @staticmethod
    def public(row):
        value=dict(row)
        payload=json.loads(value.pop("payload","{}"))
        if payload.get("schema_version")==2:
            value["data"]={"rain_intensity_mm_min":no_data_result().model_dump(),**payload["data"]}
            value["window_ms"]=payload["window_ms"]
        for field in WINDOW_PUBLIC_FIELDS:
            if field in payload:
                value[field]=payload[field]
        return value

    def latest(self):
        with self.connect() as db:
            rows=db.execute("""SELECT r.* FROM readings r JOIN
                (SELECT device_id,MAX(id) id FROM readings GROUP BY device_id) t ON r.id=t.id ORDER BY r.id DESC""").fetchall()
        return [self.public(row) for row in rows]

    def history(self, device_id=None, since=None, status=None, limit=100, before_id=None):
        conditions,args=[],[]
        for expression,value in (("device_id=?",device_id),("received_at>=?",since),("status=?",status),("id<?",before_id)):
            if value is not None:
                conditions.append(expression)
                args.append(value)
        where=" WHERE "+" AND ".join(conditions) if conditions else ""
        with self.connect() as db:
            rows=db.execute("SELECT * FROM readings"+where+" ORDER BY id DESC LIMIT ?",[*args,limit]).fetchall()
        return [self.public(row) for row in rows]

    def counts(self):
        with self.connect() as db:
            row=db.execute("SELECT count(*) total,count(DISTINCT device_id) devices FROM readings").fetchone()
            points=db.execute("SELECT count(*) FROM measurements").fetchone()[0]
        return {**dict(row),"measurements":points}

    @staticmethod
    def measurement_public(row):
        value=dict(row)
        value.pop("identity",None)
        value["name"]=METRICS[value["metric"]]["name"]
        return value

    def measurements(self, device_id=None, metric=None, since=None, status=None, limit=100, before_id=None):
        conditions,args=[],[]
        for expression,value in (("device_id=?",device_id),("metric=?",metric),("received_at>=?",since),("status=?",status),("id<?",before_id)):
            if value is not None:
                conditions.append(expression)
                args.append(value)
        where=" WHERE "+" AND ".join(conditions) if conditions else ""
        with self.connect() as db:
            rows=db.execute("SELECT * FROM measurements"+where+" ORDER BY id DESC LIMIT ?",[*args,limit]).fetchall()
        return [self.measurement_public(row) for row in rows]

    def current(self, device_id):
        result={key:dict(value=None,status="NO_DATA",sample_seq=None,sample_t_ms=None,age_ms=None,unit=spec["unit"]) for key,spec in METRICS.items()}
        with self.connect() as db:
            run=db.execute("SELECT max(id) FROM device_runs WHERE device_id=?",(device_id,)).fetchone()[0]
            if run is None:
                return result
            for key,spec in METRICS.items():
                row=db.execute("SELECT * FROM measurements WHERE run_id=? AND metric=? ORDER BY source_order DESC,id DESC LIMIT 1",(run,key)).fetchone()
                if row:
                    value=self.measurement_public(row)
                    value["age_ms"]=max(0,int((datetime.now(timezone.utc)-datetime.fromisoformat(value["observed_at"])).total_seconds()*1000))
                    if value["status"]=="OK" and value["age_ms"]>spec["stale_ms"]:
                        value["status"]="STALE"
                        value["value"]=None
                    result[key]=value
        return result

    def csv(self, **filters):
        rows=self.measurements(**filters)
        output=io.StringIO(newline="")
        fields=["id","reading_id","received_at","device_id","metric","name","value","unit","status","range","sample_seq","sample_t_ms","age_ms"]
        writer=csv.DictWriter(output,fieldnames=fields,extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)
        return "\ufeff"+output.getvalue()
