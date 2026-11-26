"""Tea Sense storage — dual backend (SQLite local or MySQL cloud).

Selection rule:
  * Store("mysql://user:pass@host:port/dbname") → PyMySQL with InnoDB.
  * Store(Path(...).db) or other local path       → sqlite3 legacy backend.

Cursor protocol abstraction: both sqlite3.Connection (when used as a cursor
context) and pymysql.cursors.DictCursor expose .execute() / .fetchone() /
.fetchall() / .lastrowid / .rowcount uniformly. Callers must NEVER chain
fetchone() off the return value of execute(), because pymysql.execute()
returns an int (affected row count), not the cursor.
"""
from __future__ import annotations
import csv
import io
import json
import re
import sqlite3
from contextlib import contextmanager
from dataclasses import dataclass, field
from datetime import datetime, timezone, timedelta
from pathlib import Path
from typing import Any

from .protocol import Reading, ReadingV2, WINDOW_PUBLIC_FIELDS, METRICS, no_data_result


def utc_now():
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


# ---------------------------------------------------------------------------
# DSN parsing
# ---------------------------------------------------------------------------

_MYSQL_DSN_RE = re.compile(
    r"^mysql://(?P<user>[^:@/]+):(?P<password>[^@/]+)@"
    r"(?P<host>[^:/]+)(?::(?P<port>\d+))?/(?P<database>[^?]+)"
)


@dataclass
class _MysqlTarget:
    kind: str = "mysql"
    host: str = ""
    port: int = 3306
    user: str = ""
    password: str = field(default="", repr=False)
    database: str = ""
    ssl_ca: Path | None = None


@dataclass
class _SqliteTarget:
    kind: str = "sqlite"
    path: Path = Path()


def _parse_target(target, *, require_mysql=False):
    if isinstance(target, Path):
        return _SqliteTarget(path=target)
    if isinstance(target, _MysqlTarget):
        return target
    if isinstance(target, str):
        m = _MYSQL_DSN_RE.fullmatch(target)
        if m:
            return _MysqlTarget(
                host=m.group("host"),
                port=int(m.group("port") or 3306),
                user=m.group("user"),
                password=m.group("password"),
                database=m.group("database"),
            )
        if require_mysql or target.startswith("mysql://"):
            raise ValueError("Invalid MYSQL_DSN; expected mysql://user:password@host:port/database")
        return _SqliteTarget(path=Path(target))
    raise TypeError(f"Unsupported storage target: {target!r}")


# ---------------------------------------------------------------------------
# Backend helpers
# ---------------------------------------------------------------------------

def _connect(target):
    if target.kind == "mysql":
        import pymysql
        import pymysql.cursors
        tls = {}
        if target.ssl_ca is not None:
            tls = {"ssl_ca": str(target.ssl_ca), "ssl_verify_identity": True}
        return pymysql.connect(
            host=target.host, port=target.port,
            user=target.user, password=target.password,
            database=target.database, charset="utf8mb4",
            cursorclass=pymysql.cursors.DictCursor,
            autocommit=False, connect_timeout=10, read_timeout=30, write_timeout=30,
            **tls,
        )
    db = sqlite3.connect(str(target.path), timeout=10)
    db.row_factory = sqlite3.Row
    return db


def _row_get(row, key, default=None):
    if row is None:
        return default
    if isinstance(row, sqlite3.Row):
        try:
            return row[key]
        except (KeyError, IndexError):
            return default
    if isinstance(row, dict):
        return row.get(key, default)
    if hasattr(row, "keys"):
        try:
            return row[key]
        except Exception:
            return default
    return default


# ---------------------------------------------------------------------------
# Store
# ---------------------------------------------------------------------------

class Store:
    def __init__(self, target, *, mysql_dsn=None, mysql_ssl_ca=None):
        self.target = _parse_target(mysql_dsn, require_mysql=True) if mysql_dsn else _parse_target(target)
        if mysql_ssl_ca is not None:
            if self.target.kind != "mysql":
                raise ValueError("MYSQL_SSL_CA requires MySQL")
            self.target.ssl_ca = mysql_ssl_ca

    # ----- connection ----------------------------------------------------
    @contextmanager
    def connect(self):
        conn = _connect(self.target)
        mysql_cursor = None
        try:
            if self.target.kind == "mysql":
                mysql_cursor = conn.cursor()
                yield mysql_cursor
                conn.commit()
            else:
                yield conn
                conn.commit()
        except Exception:
            try:
                if self.target.kind == "mysql":
                    conn.rollback()
            except Exception:
                pass
            raise
        finally:
            if mysql_cursor is not None:
                mysql_cursor.close()
            conn.close()

    # ----- schema --------------------------------------------------------
    def initialize(self):
        if self.target.kind == "sqlite":
            self._init_sqlite()
        else:
            self._init_mysql()

    def _init_sqlite(self):
        self.target.path.parent.mkdir(parents=True, exist_ok=True)
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
            columns = {row["name"] for row in db.execute("PRAGMA table_info(readings)").fetchall()}
            for name, declaration in (("schema_version", "INTEGER NOT NULL DEFAULT 1"), ("run_id", "INTEGER")):
                if name not in columns:
                    db.execute("ALTER TABLE readings ADD COLUMN " + name + " " + declaration)
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
            columns = {row["name"] for row in db.execute("PRAGMA table_info(measurements)").fetchall()}
            if "result_metadata" not in columns:
                db.execute("ALTER TABLE measurements ADD COLUMN result_metadata TEXT")
            # GPS tables (schema v3)
            db.executescript("""
                CREATE TABLE IF NOT EXISTS locations(
                  id INTEGER PRIMARY KEY,device_id TEXT NOT NULL,
                  longitude REAL NOT NULL,latitude REAL NOT NULL,source TEXT NOT NULL,
                  coordinate_system TEXT NOT NULL,fix_time_utc TEXT NOT NULL,
                  received_at_utc TEXT NOT NULL,UNIQUE(device_id,fix_time_utc));
                CREATE INDEX IF NOT EXISTS ix_locations_current ON locations(device_id,fix_time_utc DESC,id DESC);
                CREATE TABLE IF NOT EXISTS location_receipts(
                  device_id TEXT PRIMARY KEY,last_received_at_utc TEXT NOT NULL,
                  last_report_status TEXT NOT NULL,received_count INTEGER NOT NULL DEFAULT 0,
                  rejected_count INTEGER NOT NULL DEFAULT 0,duplicate_count INTEGER NOT NULL DEFAULT 0,
                  out_of_order_count INTEGER NOT NULL DEFAULT 0);
            """)

    def _init_mysql(self):
        with self.connect() as db:
            # GPS tables (idempotent)
            db.execute("""
                CREATE TABLE IF NOT EXISTS locations (
                  id BIGINT(20) NOT NULL AUTO_INCREMENT PRIMARY KEY,
                  device_id VARCHAR(64) NOT NULL,
                  longitude DOUBLE NOT NULL, latitude DOUBLE NOT NULL,
                  source VARCHAR(32) NOT NULL, coordinate_system VARCHAR(32) NOT NULL,
                  fix_time_utc VARCHAR(32) NOT NULL, received_at_utc VARCHAR(32) NOT NULL,
                  UNIQUE KEY uniq_device_fix (device_id, fix_time_utc),
                  KEY ix_locations_current (device_id, fix_time_utc DESC, id DESC)
                ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
            """)
            db.execute("""
                CREATE TABLE IF NOT EXISTS location_receipts (
                  device_id VARCHAR(64) NOT NULL PRIMARY KEY,
                  last_received_at_utc VARCHAR(32) NOT NULL,
                  last_report_status VARCHAR(32) NOT NULL,
                  received_count INT(11) NOT NULL DEFAULT 0,
                  rejected_count INT(11) NOT NULL DEFAULT 0,
                  duplicate_count INT(11) NOT NULL DEFAULT 0,
                  out_of_order_count INT(11) NOT NULL DEFAULT 0
                ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
            """)
            self._ensure_col(db, "readings", "schema_version", "INT NOT NULL DEFAULT 1")
            self._ensure_col(db, "readings", "run_id", "BIGINT(20)")
            self._ensure_col(db, "measurements", "result_metadata", "MEDIUMTEXT")

    def _ensure_col(self, db, table: str, column: str, type_sql: str):
        db.execute(f"SHOW COLUMNS FROM `{table}` LIKE %s", (column,))
        if db.fetchone() is None:
            db.execute(f"ALTER TABLE `{table}` ADD COLUMN `{column}` {type_sql}")

    # ----- GPS helpers ---------------------------------------------------
    @staticmethod
    def _location_receipt(target, db, device_id, received, status):
        rejected = status not in ("OK", "DUPLICATE", "OUT_OF_ORDER")
        if target.kind == "mysql":
            db.execute("""
                INSERT INTO location_receipts
                  (device_id, last_received_at_utc, last_report_status,
                   received_count, rejected_count, duplicate_count, out_of_order_count)
                VALUES (%s, %s, %s, 1, %s, %s, %s)
                ON DUPLICATE KEY UPDATE
                  last_received_at_utc=VALUES(last_received_at_utc),
                  last_report_status=VALUES(last_report_status),
                  received_count=received_count+1,
                  rejected_count=rejected_count+VALUES(rejected_count),
                  duplicate_count=duplicate_count+VALUES(duplicate_count),
                  out_of_order_count=out_of_order_count+VALUES(out_of_order_count)
            """, (device_id, received, status, int(rejected), int(status == "DUPLICATE"), int(status == "OUT_OF_ORDER")))
        else:
            db.execute("""INSERT INTO location_receipts
                (device_id,last_received_at_utc,last_report_status,received_count,
                 rejected_count,duplicate_count,out_of_order_count) VALUES(?,?,?,1,?,?,?)
                ON CONFLICT(device_id) DO UPDATE SET
                  last_received_at_utc=excluded.last_received_at_utc,
                  last_report_status=excluded.last_report_status,
                  received_count=received_count+1,
                  rejected_count=rejected_count+excluded.rejected_count,
                  duplicate_count=duplicate_count+excluded.duplicate_count,
                  out_of_order_count=out_of_order_count+excluded.out_of_order_count""",
                (device_id, received, status, int(rejected), int(status == "DUPLICATE"), int(status == "OUT_OF_ORDER")))

    def record_location_error(self, device_id, status, received_at=None):
        with self.connect() as db:
            self._location_receipt(self.target, db, device_id, received_at or utc_now(), status)

    def insert_location(self, device_id, location, received_at=None):
        received = received_at or utc_now()
        ph = "?" if self.target.kind == "sqlite" else "%s"
        with self.connect() as db:
            db.execute(f"SELECT * FROM locations WHERE device_id={ph} AND fix_time_utc={ph}", (device_id, location["fix_time_utc"]))
            prior = db.fetchone()
            if prior:
                prior_long = _row_get(prior, "longitude")
                prior_lat = _row_get(prior, "latitude")
                status = "DUPLICATE" if (prior_long, prior_lat) == (location["longitude"], location["latitude"]) else "FIX_CONFLICT"
            else:
                db.execute(f"SELECT max(fix_time_utc) FROM locations WHERE device_id={ph}", (device_id,))
                last_row = db.fetchone()
                latest = _row_get(last_row, "max(fix_time_utc)")
                status = "OUT_OF_ORDER" if latest and location["fix_time_utc"] < latest else "OK"
                placeholders = "?,?,?,?,?,?,?" if self.target.kind == "sqlite" else "%s,%s,%s,%s,%s,%s,%s"
                db.execute(
                    f"INSERT INTO locations (device_id,longitude,latitude,source,coordinate_system,fix_time_utc,received_at_utc) VALUES({placeholders})",
                    (device_id, location["longitude"], location["latitude"], location["source"],
                     location["coordinate_system"], location["fix_time_utc"], received)
                )
            self._location_receipt(self.target, db, device_id, received, status)
        return status

    def current_location(self, device_id, stale_seconds=540, now=None):
        now = now or datetime.now(timezone.utc)
        ph = "?" if self.target.kind == "sqlite" else "%s"
        with self.connect() as db:
            db.execute(f"SELECT * FROM locations WHERE device_id={ph} ORDER BY fix_time_utc DESC,id DESC LIMIT 1", (device_id,))
            row = db.fetchone()
            db.execute(f"SELECT * FROM location_receipts WHERE device_id={ph}", (device_id,))
            receipt = db.fetchone()
        result = dict(device_id=device_id, longitude=None, latitude=None, source="GPS",
                      coordinate_system="UNKNOWN", fix_time_utc=None, received_at_utc=None,
                      status="NO_DATA", fix_age_seconds=None, received_age_seconds=None,
                      last_received_at_utc=None, last_received_age_seconds=None,
                      last_report_status=None, latest_report_error=False,
                      received_count=0, rejected_count=0, duplicate_count=0, out_of_order_count=0)
        age = lambda text: max(0, (now - datetime.fromisoformat(text)).total_seconds())
        if row:
            for k in result:
                if isinstance(row, sqlite3.Row):
                    if k in row.keys():
                        result[k] = row[k]
                elif isinstance(row, dict) and k in row:
                    result[k] = row[k]
            result["fix_age_seconds"] = age(_row_get(row, "fix_time_utc"))
            result["received_age_seconds"] = age(_row_get(row, "received_at_utc"))
            result["status"] = "STALE" if max(result["fix_age_seconds"], result["received_age_seconds"]) > stale_seconds else "FRESH"
        if receipt:
            for k in ("device_id", "last_received_at_utc", "last_report_status",
                      "received_count", "rejected_count", "duplicate_count", "out_of_order_count"):
                result[k] = _row_get(receipt, k, result.get(k))
            result["last_received_age_seconds"] = age(result["last_received_at_utc"])
            result["latest_report_error"] = result["last_report_status"] not in ("OK", "DUPLICATE", "OUT_OF_ORDER")
        return result

    # ----- core ---------------------------------------------------------
    @staticmethod
    def _signed_delta(new, old):
        return ((new - old + 2147483648) % 4294967296) - 2147483648

    def _run(self, db, reading):
        ph = "?" if self.target.kind == "sqlite" else "%s"
        if reading.boot_id is not None:
            db.execute(f"SELECT * FROM device_runs WHERE device_id={ph} AND boot_id={ph} ORDER BY id DESC LIMIT 1", (reading.device_id, reading.boot_id))
            row = db.fetchone()
        else:
            db.execute(f"SELECT * FROM device_runs WHERE device_id={ph} ORDER BY id DESC LIMIT 1", (reading.device_id,))
            row = db.fetchone()
            if row and _row_get(row, "boot_id") is not None:
                row = None
        reason = "boot_id" if reading.boot_id else "initial"
        if row is None:
            db.execute(
                "INSERT INTO device_runs(device_id,boot_id,last_seq,last_tick,tick_unwrapped,reason) VALUES(?,?,?,?,?,?)" if self.target.kind == "sqlite"
                else "INSERT INTO device_runs(device_id,boot_id,last_seq,last_tick,tick_unwrapped,reason) VALUES(%s,%s,%s,%s,%s,%s)",
                (reading.device_id, reading.boot_id, reading.seq, reading.t_ms, reading.t_ms, reason)
            )
            return db.lastrowid, reading.t_ms
        tick = _row_get(row, "tick_unwrapped", 0) + self._signed_delta(reading.t_ms, _row_get(row, "last_tick", 0))
        if self._signed_delta(reading.seq, _row_get(row, "last_seq", 0)) > 0 and self._signed_delta(reading.t_ms, _row_get(row, "last_tick", 0)) >= 0:
            db.execute(
                "UPDATE device_runs SET last_seq=?,last_tick=?,tick_unwrapped=? WHERE id=?" if self.target.kind == "sqlite"
                else "UPDATE device_runs SET last_seq=%s,last_tick=%s,tick_unwrapped=%s WHERE id=%s",
                (reading.seq, reading.t_ms, tick, _row_get(row, "id"))
            )
        return _row_get(row, "id"), tick

    def _measure(self, db, reading, reading_id, received_at, legacy=False):
        run, tick = self._run(db, reading)
        ph = "?" if self.target.kind == "sqlite" else "%s"
        db.execute(f"UPDATE readings SET run_id={ph} WHERE id={ph}", (run, reading_id))
        if legacy:
            data = {"soil_moisture_pct": dict(value=reading.moisture_pct, status=reading.status,
                    sample_seq=reading.seq, sample_t_ms=reading.t_ms, age_ms=0, range=reading.range)}
        else:
            data = reading.data.model_dump()
        placeholders = ",".join([ph] * 16)
        insert_sql = (
            f"""INSERT INTO measurements
                (reading_id,run_id,device_id,metric,value,unit,status,`range`,sample_seq,sample_t_ms,age_ms,received_at,observed_at,source_order,identity,result_metadata)
                VALUES({placeholders})"""
        )
        for metric, item in data.items():
            if item["status"] == "NO_DATA":
                continue
            identity = f"legacy:{reading_id}" if legacy else f'{run}:{metric}:{item["sample_seq"]}:{item["sample_t_ms"]}'
            modern = not legacy and reading.window_ms == 180000
            method = (item.get("statistics") or {}).get("method")
            if modern and method != "latest":
                identity = f'{run}:{metric}:window:{reading.seq}:{reading.t_ms}'
            db.execute(f"SELECT * FROM measurements WHERE identity={ph}", (identity,))
            prior = db.fetchone()
            observed = (datetime.fromisoformat(received_at) - timedelta(milliseconds=item["age_ms"])).isoformat(timespec="milliseconds")
            if prior:
                prior_observed = _row_get(prior, "observed_at")
                if observed < prior_observed:
                    db.execute(f"UPDATE measurements SET observed_at={ph} WHERE id={ph}", (observed, _row_get(prior, "id")))
                prior_value = _row_get(prior, "value")
                prior_status = _row_get(prior, "status")
                prior_range = _row_get(prior, "range")
                if item["status"] != "STALE" and ((not modern and prior_status != item["status"]) or prior_value != item["value"] or prior_range != item.get("range")):
                    raise ValueError("sequence_conflict")
                continue
            order = tick + self._signed_delta(item["sample_t_ms"], reading.t_ms)
            db.execute(insert_sql, (
                reading_id, run, reading.device_id, metric, item["value"], METRICS[metric]["unit"], item["status"],
                item.get("range"), item["sample_seq"], item["sample_t_ms"], item["age_ms"], received_at, observed, order, identity,
                json.dumps({k: v for k, v in item.items() if k in ("statistics", "wind_speed_sample_max_mps", "cached", "latest_query_status", "latest_query_seq", "latest_query_t_ms") and v is not None}, ensure_ascii=False)
            ))

    def insert(self, reading):
        v2 = isinstance(reading, ReadingV2)
        payload = reading.model_dump(exclude_none=not v2)
        received = utc_now()
        values = (received, reading.device_id, reading.boot_id, reading.seq, reading.t_ms,
                  "MULTI" if v2 else reading.channel, None if v2 else reading.adc_raw,
                  None if v2 else reading.moisture_pct, "SNAPSHOT" if v2 else reading.status,
                  None if v2 else reading.range, json.dumps(payload, ensure_ascii=False), 2 if v2 else 1)
        ph = "?" if self.target.kind == "sqlite" else "%s"
        cols = "received_at,device_id,boot_id,seq,t_ms,channel,adc_raw,moisture_pct,status,`range`,payload,schema_version"
        sql = (
            f"INSERT OR IGNORE INTO readings ({cols}) VALUES({','.join([ph]*12)})"
            if self.target.kind == "sqlite"
            else f"INSERT IGNORE INTO readings ({cols}) VALUES({','.join([ph]*12)})"
        )
        with self.connect() as db:
            db.execute(sql, values)
            affected = db.rowcount
            duplicate = (affected == 0)
            prior_row = None
            if duplicate:
                if reading.boot_id is not None:
                    db.execute(f"SELECT * FROM readings WHERE device_id={ph} AND boot_id={ph} AND seq={ph}", (reading.device_id, reading.boot_id, reading.seq))
                else:
                    db.execute(f"SELECT * FROM readings WHERE device_id={ph} AND seq={ph}", (reading.device_id, reading.seq))
                prior_row = db.fetchone()
            else:
                lid = db.lastrowid
                self._measure(db, reading, lid, received, legacy=not v2)
                db.execute(f"SELECT * FROM readings WHERE id={ph}", (lid,))
                prior_row = db.fetchone()
        return self.public(prior_row), duplicate

    @staticmethod
    def public(row):
        if row is None:
            return None
        if isinstance(row, sqlite3.Row):
            payload_str = row["payload"] if "payload" in row.keys() else None
            value = dict(row)
        elif isinstance(row, dict):
            payload_str = row.get("payload")
            value = dict(row)
        else:
            payload_str = _row_get(row, "payload")
            value = {}
            for k in ("id", "received_at", "device_id", "boot_id", "seq", "t_ms", "channel",
                      "adc_raw", "moisture_pct", "status", "range", "payload", "schema_version", "run_id"):
                value[k] = _row_get(row, k)
        payload = json.loads(payload_str) if isinstance(payload_str, str) else (payload_str or {})
        if payload.get("schema_version") == 2:
            value["data"] = {"rain_intensity_mm_min": no_data_result().model_dump(), **payload["data"]}
            value["window_ms"] = payload["window_ms"]
        for field in WINDOW_PUBLIC_FIELDS:
            if field in payload:
                value[field] = payload[field]
        return value

    def latest(self):
        with self.connect() as db:
            db.execute("""
                SELECT r.* FROM readings r JOIN
                    (SELECT device_id, MAX(id) AS id FROM readings GROUP BY device_id) t
                ON r.id = t.id ORDER BY r.id DESC
            """)
            rows = db.fetchall()
        return [self.public(row) for row in rows]

    def history(self, device_id=None, since=None, status=None, limit=100, before_id=None):
        ph = "?" if self.target.kind == "sqlite" else "%s"
        conditions, args = [], []
        for expression, value in (("device_id=" + ph, device_id), ("received_at>=" + ph, since), ("status=" + ph, status), ("id<" + ph, before_id)):
            if value is not None:
                conditions.append(expression)
                args.append(value)
        where = " WHERE " + " AND ".join(conditions) if conditions else ""
        with self.connect() as db:
            db.execute(f"SELECT * FROM readings{where} ORDER BY id DESC LIMIT {ph}", [*args, limit])
            rows = db.fetchall()
        return [self.public(row) for row in rows]

    def counts(self):
        with self.connect() as db:
            db.execute("SELECT count(*) total,count(DISTINCT device_id) devices FROM readings")
            row = db.fetchone()
            db.execute("SELECT count(*) FROM measurements")
            points = db.fetchone()
        total = _row_get(row, "total", 0)
        devices = _row_get(row, "devices", 0)
        meas = _row_get(points, "count(*)", 0) if isinstance(points, sqlite3.Row) else (list(points.values())[0] if points else 0)
        return {"total": total, "devices": devices, "measurements": meas}

    @staticmethod
    def measurement_public(row):
        if row is None:
            return None
        value = dict(row) if isinstance(row, dict) else {}
        if isinstance(row, sqlite3.Row):
            value = dict(row)
        meta = value.pop("result_metadata", None)
        value.update(json.loads(meta or "{}"))
        metric_key = value.get("metric")
        value["name"] = METRICS.get(metric_key, {}).get("name", metric_key or "")
        return value

    def measurements(self, device_id=None, metric=None, since=None, status=None, limit=100, before_id=None):
        ph = "?" if self.target.kind == "sqlite" else "%s"
        conditions, args = [], []
        for expression, value in (("device_id=" + ph, device_id), ("metric=" + ph, metric), ("received_at>=" + ph, since), ("status=" + ph, status), ("id<" + ph, before_id)):
            if value is not None:
                conditions.append(expression)
                args.append(value)
        where = " WHERE " + " AND ".join(conditions) if conditions else ""
        with self.connect() as db:
            db.execute(f"SELECT * FROM measurements{where} ORDER BY id DESC LIMIT {ph}", [*args, limit])
            rows = db.fetchall()
        return [self.measurement_public(row) for row in rows]

    def _hold_last_values(self, db, device_id, metrics, ph):
        for key, latest in metrics.items():
            if latest["value"] is not None or latest["status"] in ("CALM", "VARIABLE"):
                continue
            db.execute(f"""
                SELECT * FROM measurements WHERE device_id={ph} AND metric={ph}
                AND value IS NOT NULL AND status IN ('OK', 'PARTIAL', 'CACHED')
                ORDER BY observed_at DESC, id DESC LIMIT 1
            """, (device_id, key))
            row = db.fetchone()
            if row is not None:
                value = self.measurement_public(row)
                value["age_ms"] = max(0, int((datetime.now(timezone.utc) -
                                             datetime.fromisoformat(value["observed_at"])).total_seconds() * 1000))
                value["showing_last_value"] = True
                metrics[key] = value

    def current(self, device_id, *, hold_latest=False):
        result = {key: dict(value=None, status="NO_DATA", sample_seq=None, sample_t_ms=None, age_ms=None, unit=spec["unit"])
                  for key, spec in METRICS.items()}
        ph = "?" if self.target.kind == "sqlite" else "%s"
        with self.connect() as db:
            db.execute(f"""
                SELECT r.* FROM readings r JOIN device_runs d ON r.run_id=d.id
                WHERE r.device_id={ph} AND r.seq=d.last_seq AND r.t_ms=d.last_tick
                ORDER BY d.id DESC,r.id DESC LIMIT 1
            """, (device_id,))
            latest = db.fetchone()
            if latest:
                frame = self.public(latest)
                if frame.get("window_ms") == 180000:
                    elapsed = max(0, int((datetime.now(timezone.utc) - datetime.fromisoformat(frame["received_at"])).total_seconds() * 1000))
                    for key, item in frame["data"].items():
                        result[key] = {**item, "unit": METRICS[key]["unit"], "quality_frozen": True, "frame_window_ms": 180000,
                                     "age_ms": None if item["age_ms"] is None else item["age_ms"] + elapsed}
                    if hold_latest:
                        self._hold_last_values(db, device_id, result, ph)
                    return result
            db.execute(f"SELECT max(id) FROM device_runs WHERE device_id={ph}", (device_id,))
            run_row = db.fetchone()
            run = _row_get(run_row, "max(id)") if run_row else None
            if run is None:
                return result
            for key, spec in METRICS.items():
                db.execute(f"SELECT * FROM measurements WHERE run_id={ph} AND metric={ph} ORDER BY source_order DESC,id DESC LIMIT 1", (run, key))
                mrow = db.fetchone()
                if mrow:
                    value = self.measurement_public(mrow)
                    value["age_ms"] = max(0, int((datetime.now(timezone.utc) - datetime.fromisoformat(value["observed_at"])).total_seconds() * 1000))
                    if not hold_latest and value["status"] == "OK" and value["age_ms"] > spec["stale_ms"]:
                        value["status"] = "STALE"
                        value["value"] = None
                    result[key] = value
            if hold_latest:
                self._hold_last_values(db, device_id, result, ph)
        return result

    def csv(self, **filters):
        rows = self.measurements(**filters)
        output = io.StringIO(newline="")
        fields = ["id", "reading_id", "received_at", "device_id", "metric", "name", "value", "unit", "status", "range",
                  "sample_seq", "sample_t_ms", "age_ms", "statistics", "wind_speed_sample_max_mps",
                  "cached", "latest_query_status", "latest_query_seq", "latest_query_t_ms"]
        writer = csv.DictWriter(output, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        for row in rows:
            if row.get("statistics") is not None:
                row["statistics"] = json.dumps(row["statistics"], ensure_ascii=False, separators=(",", ":"))
        writer.writerows(rows)
        return "\ufeff" + output.getvalue()
