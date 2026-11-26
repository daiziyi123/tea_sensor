import os
import json
import re
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def load_env():
    path = ROOT / ".env"
    if path.exists():
        for line in path.read_text(encoding="utf-8-sig").splitlines():
            line = line.strip()
            if line and not line.startswith("#") and "=" in line:
                key, value = line.split("=", 1)
                os.environ.setdefault(key.strip(), value.strip().strip('"\''))


@dataclass(frozen=True)
class Settings:
    http_host: str = "127.0.0.1"
    http_port: int = 8000
    tcp_host: str = "127.0.0.1"
    tcp_port: int = 9000
    database: Path = ROOT / "data" / "tea_sense.db"
    mysql_dsn: str | None = field(default=None, repr=False)
    mysql_ssl_ca: Path | None = None
    ingest_token: str = ""
    view_token: str = ""
    online_seconds: int = 540
    idle_seconds: int = 300
    authenticated_idle_seconds: int = 600
    frame_timeout_seconds: int = 300
    max_connections: int = 50
    max_frame_bytes: int = 8192
    gps_imei_bindings: dict[str, str] = field(default_factory=dict, repr=False)
    gps_coordinate_system: str = "UNKNOWN"
    gps_stale_seconds: int = 540
    gps_future_tolerance_seconds: int = 300

    @classmethod
    def from_env(cls):
        load_env()
        mysql_ssl_ca = os.getenv("MYSQL_SSL_CA")
        config = cls(
            http_host=os.getenv("HTTP_HOST", "127.0.0.1"),
            http_port=int(os.getenv("PORT", os.getenv("HTTP_PORT", "8000"))),
            tcp_host=os.getenv("TCP_HOST", "127.0.0.1"),
            tcp_port=int(os.getenv("TCP_PORT", "9000")),
            database=Path(os.getenv("DATABASE_PATH", str(ROOT / "data/tea_sense.db"))),
            mysql_dsn=os.getenv("MYSQL_DSN") or None,
            mysql_ssl_ca=Path(mysql_ssl_ca) if mysql_ssl_ca else None,
            ingest_token=os.getenv("INGEST_TOKEN", ""),
            view_token=os.getenv("VIEW_TOKEN", ""),
            online_seconds=int(os.getenv("ONLINE_SECONDS", "540")),
            authenticated_idle_seconds=int(os.getenv("AUTHENTICATED_IDLE_SECONDS", "600")),
            gps_imei_bindings=json.loads(os.getenv("GPS_IMEI_BINDINGS", "") or "{}"),
            gps_coordinate_system=os.getenv("GPS_COORDINATE_SYSTEM", "UNKNOWN"),
            gps_stale_seconds=int(os.getenv("GPS_STALE_SECONDS", "540")),
            gps_future_tolerance_seconds=int(os.getenv("GPS_FUTURE_TOLERANCE_SECONDS", "300")),
        )
        config.validate()
        return config

    def validate(self):
        if self.mysql_ssl_ca is not None and not self.mysql_dsn:
            raise ValueError("MYSQL_SSL_CA requires MYSQL_DSN")
        if not 1 <= self.http_port <= 65535 or not 1 <= self.tcp_port <= 65535:
            raise ValueError("Port must be between 1 and 65535")
        if self.http_port == self.tcp_port:
            raise ValueError("HTTP_PORT and TCP_PORT must differ")
        if self.authenticated_idle_seconds < 600:
            raise ValueError("Authenticated idle timeout must allow at least 600 seconds")
        if self.online_seconds < 1:
            raise ValueError("ONLINE_SECONDS must be positive")
        if (not isinstance(self.gps_imei_bindings, dict) or
                any(not isinstance(imei, str) or not re.fullmatch(r"[0-9]{15}", imei) or
                    not isinstance(device, str) or not re.fullmatch(r"[A-Za-z0-9_-]{1,64}", device)
                    for imei, device in self.gps_imei_bindings.items())):
            raise ValueError("GPS_IMEI_BINDINGS must map 15-digit strings to device IDs")
        if self.gps_imei_bindings and not self.ingest_token:
            raise ValueError("GPS bindings require INGEST_TOKEN authentication")
        if self.gps_coordinate_system not in ("UNKNOWN", "WGS84", "GCJ-02", "BD-09"):
            raise ValueError("Unsupported GPS_COORDINATE_SYSTEM")
        if self.gps_stale_seconds < 1 or not 0 <= self.gps_future_tolerance_seconds <= 3600:
            raise ValueError("Invalid GPS freshness or future tolerance")
        if self.tcp_host not in ("127.0.0.1", "::1", "localhost") and not self.ingest_token:
            raise ValueError("Set INGEST_TOKEN before exposing TCP outside loopback")
        if (self.http_host not in ("127.0.0.1", "::1", "localhost") and not self.view_token
                and os.getenv("PUBLIC_DASHBOARD") != "1" and os.getenv("LOCAL_PREVIEW") != "1"):
            raise ValueError("Set VIEW_TOKEN or explicitly enable PUBLIC_DASHBOARD=1")
