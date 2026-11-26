from contextlib import closing
"""Backup and restoration rehearsal in isolated WAL databases only."""
import sqlite3
import tempfile
import unittest
from pathlib import Path
from tools.backup_database import backup_database,verify_database

class BackupTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.root=Path(self.temp.name)
        self.source=self.root/"live.db"
        self.live=sqlite3.connect(self.source)
        self.live.execute("PRAGMA journal_mode=WAL")
        self.live.execute("PRAGMA user_version=2")
        self.live.execute("CREATE TABLE readings(id INTEGER PRIMARY KEY,payload TEXT)")
        self.live.execute("INSERT INTO readings VALUES(42,'fixture data')")
        self.live.commit()

    def tearDown(self):
        self.live.close()
        self.temp.cleanup()

    def test_live_wal_backup_and_temp_restore(self):
        self.live.execute("INSERT INTO readings VALUES(43,'uncommitted')")
        backup=self.root/"backup.db"
        self.assertEqual(backup_database(self.source,backup)["readings"],1)
        self.live.rollback()
        self.live.execute("INSERT INTO readings VALUES(44,'later')")
        self.live.commit()
        restored=self.root/"restore.db"
        self.assertEqual(backup_database(backup,restored),{"quick_check":"ok","schema_version":2,"readings":1})
        with closing(sqlite3.connect(restored)) as db:
            self.assertEqual(db.execute("SELECT * FROM readings").fetchall(),[(42,"fixture data")])
        self.assertEqual(verify_database(self.source)["readings"],2)

    def test_existing_destination_and_same_source_protected(self):
        target=self.root/"keep.db"
        target.write_bytes(b"KEEP EXACTLY")
        with self.assertRaises(FileExistsError):
            backup_database(self.source,target)
        self.assertEqual(target.read_bytes(),b"KEEP EXACTLY")
        with self.assertRaises(ValueError):
            backup_database(self.source,self.source)
        self.assertEqual(verify_database(self.source)["readings"],1)

    def test_invalid_source_and_missing_path(self):
        bad=self.root/"bad.db";bad.write_bytes(b"not sqlite")
        destination=self.root/"never.db"
        with self.assertRaises(sqlite3.DatabaseError):
            backup_database(bad,destination)
        self.assertFalse(destination.exists())
        with self.assertRaises(FileNotFoundError):
            backup_database(self.root/"missing.db",destination)
        self.assertFalse(destination.exists())

    def test_invalid_deadline_does_not_create_destination(self):
        target=self.root/"never.db"
        with self.assertRaises(ValueError):
            backup_database(self.source,target,0)
        self.assertFalse(target.exists())
