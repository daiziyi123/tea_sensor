import os
from dataclasses import dataclass
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
    ingest_token: str = ""
    view_token: str = ""
    online_seconds: int = 60
    idle_seconds: int = 300
    max_connections: int = 50
    max_frame_bytes: int = 8192

    @classmethod
    def from_env(cls):
        load_env()
        config = cls(
            http_host=os.getenv("HTTP_HOST", "127.0.0.1"),
            http_port=int(os.getenv("PORT", os.getenv("HTTP_PORT", "8000"))),
            tcp_host=os.getenv("TCP_HOST", "127.0.0.1"),
            tcp_port=int(os.getenv("TCP_PORT", "9000")),
            database=Path(os.getenv("DATABASE_PATH", str(ROOT / "data/tea_sense.db"))),
            ingest_token=os.getenv("INGEST_TOKEN", ""),
            view_token=os.getenv("VIEW_TOKEN", ""),
            online_seconds=int(os.getenv("ONLINE_SECONDS", "60")),
        )
        config.validate()
        return config

    def validate(self):
        if not 1 <= self.http_port <= 65535 or not 1 <= self.tcp_port <= 65535:
            raise ValueError("Port must be between 1 and 65535")
        if self.http_port == self.tcp_port:
            raise ValueError("HTTP_PORT and TCP_PORT must differ")
        if self.online_seconds < 1:
            raise ValueError("ONLINE_SECONDS must be positive")
        if self.tcp_host not in ("127.0.0.1", "::1", "localhost") and not self.ingest_token:
            raise ValueError("Set INGEST_TOKEN before exposing TCP outside loopback")
        if (self.http_host not in ("127.0.0.1", "::1", "localhost")
                and not self.view_token and os.getenv("LOCAL_PREVIEW") != "1"):
            raise ValueError("Set VIEW_TOKEN before exposing the dashboard outside loopback")
