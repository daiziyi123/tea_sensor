"""Copy only INGEST_TOKEN from an authorized local .env into ignored firmware config."""
import argparse
import json
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--env", type=Path, required=True)
args = parser.parse_args()
token = ""
for line in args.env.read_text(encoding="utf-8-sig").splitlines():
    line = line.strip()
    if line and not line.startswith("#") and "=" in line:
        key, value = line.split("=", 1)
        if key.strip() == "INGEST_TOKEN":
            token = value.strip().strip("\"'")
if not token or len(token) > 256:
    raise SystemExit("Private upload configuration incomplete: INGEST_TOKEN missing or exceeds 256 characters")
target = Path(__file__).resolve().parents[1] / "App/Inc/app_secrets.h"
literal = json.dumps(json.dumps(token, ensure_ascii=True), ensure_ascii=True)
target.write_text("#ifndef APP_SECRETS_H\n#define APP_SECRETS_H\n"
    "/* Private: do not print or commit. */\n#define APP_INGEST_TOKEN_CONFIGURED 1\n"
    f"#define APP_INGEST_TOKEN_JSON {literal}\n#endif\n", encoding="utf-8")
print("Local upload credential configured; value withheld. Server .env was not modified.")
