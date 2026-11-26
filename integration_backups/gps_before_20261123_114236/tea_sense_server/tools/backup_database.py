"""Consistent SQLite backup/verification; never loads .env or overwrites a file."""
from contextlib import closing
import argparse
import json
import os
import sqlite3
import time
from pathlib import Path

def verify_database(path):
    path=Path(path).resolve(strict=True)
    with closing(sqlite3.connect(path.as_uri()+"?mode=ro",uri=True,timeout=5)) as db:
        check=db.execute("PRAGMA quick_check").fetchall()
        if check!=[("ok",)]:
            raise ValueError("SQLite quick_check failed")
        tables={row[0] for row in db.execute("SELECT name FROM sqlite_master WHERE type='table'")}
        result={"quick_check":"ok","schema_version":db.execute("PRAGMA user_version").fetchone()[0]}
        for table in ("readings","measurements"):
            if table in tables:
                result[table]=db.execute("SELECT count(*) FROM "+table).fetchone()[0]
        return result

def backup_database(source,destination,timeout_seconds=60):
    source=Path(source).resolve(strict=True)
    destination=Path(destination).resolve()
    if source==destination:
        raise ValueError("Source and destination must differ")
    if not 0<timeout_seconds<=300:
        raise ValueError("Backup timeout must be within 0..300 seconds")
    # Check source before exclusively reserving a new file.
    verify_database(source)
    descriptor=os.open(destination,os.O_CREAT|os.O_EXCL|os.O_WRONLY,0o600)
    os.close(descriptor)
    try:
        deadline=time.monotonic()+timeout_seconds
        def progress(status,remaining,total):
            if time.monotonic()>deadline:
                raise TimeoutError("SQLite backup deadline exceeded")
        with closing(sqlite3.connect(source.as_uri()+"?mode=ro",uri=True,timeout=5)) as src:
            with closing(sqlite3.connect(destination,timeout=5)) as dst:
                src.backup(dst,pages=128,progress=progress,sleep=.05)
        return verify_database(destination)
    except BaseException:
        # This file was exclusively created by this call; existing targets are
        # rejected above and are never changed, including on verification errors.
        destination.unlink(missing_ok=True)
        raise

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    actions=parser.add_subparsers(dest="action",required=True)
    backup=actions.add_parser("backup")
    backup.add_argument("--source",type=Path,required=True)
    backup.add_argument("--destination",type=Path,required=True)
    verify=actions.add_parser("verify")
    verify.add_argument("path",type=Path)
    args=parser.parse_args()
    try:
        result=(backup_database(args.source,args.destination) if args.action=="backup" else verify_database(args.path))
    except (OSError,ValueError,sqlite3.Error,TimeoutError):
        parser.exit(1,"ERROR: database backup/verification failed; check paths, exclusive target and database integrity.\n")
    print(json.dumps(result,ensure_ascii=False))

if __name__=="__main__":
    main()
