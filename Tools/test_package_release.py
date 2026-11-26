"""Release policy/integrity tests use invented fixtures, never production secrets."""
import hashlib
import json
import os
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path
from package_release import build,extract,inventory,policy,verify,REQUIRED

class PackageTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.root=Path(self.temp.name)/"workspace";self.root.mkdir()
        for relative in REQUIRED:
            path=self.root/relative;path.parent.mkdir(parents=True,exist_ok=True);path.write_text("fixture source\n",encoding="utf-8")
        (self.root/"tea_sense/App/Inc/app_version.h").write_text(
            '#define APP_FIRMWARE_VERSION "0.9.7-fixture"\n#define APP_PROTOCOL_SCHEMA_VERSION 2U\n#define APP_CALIBRATION_REVISION "fixture-cal"\n')
        (self.root/"tea_sense_server/app/storage.py").write_text('version="PRAGMA user_version=2"\n')
        self.output=Path(self.temp.name)/"new-release"
    def tearDown(self):self.temp.cleanup()
    def fixture(self,relative,text="SENSITIVE_FIXTURE_DO_NOT_PACKAGE"):
        path=self.root/relative;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(text);return path
    def test_source_and_dependencies_included_private_generated_excluded(self):
        for relative in ("tea_sense/App/Inc/app_secrets.h","tea_sense_server/.env",
                         "tea_sense_server/data/live.db","tea_sense_server/data/live.db-wal",
                         "tea_sense/Core/Inc/Backup/main.h","tea_sense_server/.venv/pyvenv.cfg",
                         "tea_sense/MDK-ARM/tea_sense/old.hex","tea_sense_server/app/other_secrets.json"):
            self.fixture(relative)
        manifest=build(self.root,self.output)
        paths={item["path"] for item in manifest["files"]}
        self.assertTrue(set(REQUIRED)<=paths)
        self.assertEqual(manifest["versions"]["firmware"],"0.9.7-fixture")
        self.assertEqual(manifest["versions"]["calibration_revision"],"fixture-cal")
        with zipfile.ZipFile(self.output/"tea_sense_source.zip") as archive:
            self.assertTrue(all(b"SENSITIVE_FIXTURE_DO_NOT_PACKAGE" not in archive.read(name) for name in archive.namelist()))
        self.assertFalse(manifest["firmware_binary"]["provided"])
        unpacked=Path(self.temp.name)/"unpacked";extract(self.output,unpacked)
        self.assertTrue((unpacked/"tea_sense_server/web/app.js").exists())
        self.assertTrue((unpacked/"tea_sense/MDK-ARM/RTE/_tea_sense/RTE_Components.h").exists())
    def test_existing_output_and_source_recursion_rejected(self):
        self.output.mkdir();sentinel=self.output/"keep";sentinel.write_text("keep")
        with self.assertRaises(FileExistsError):build(self.root,self.output)
        self.assertEqual(sentinel.read_text(),"keep")
        with self.assertRaises(ValueError):build(self.root,self.root/"tea_sense"/"release")
        with self.assertRaises(ValueError):build(self.root,self.root/"Docs"/"release")
    def rewrite_archive(self,transform):
        archive=self.output/"tea_sense_source.zip"
        with zipfile.ZipFile(archive) as z:payloads={n:z.read(n) for n in z.namelist()}
        transform(payloads)
        with zipfile.ZipFile(archive,"w") as z:
            for n,payload in payloads.items():z.writestr(n,payload)
        (self.output/"tea_sense_source.zip.sha256").write_text(hashlib.sha256(archive.read_bytes()).hexdigest()+"  tea_sense_source.zip\n")
    def test_hash_tampering_and_missing_file_rejected(self):
        build(self.root,self.output)
        self.rewrite_archive(lambda p:p.__setitem__("README.md",b"modified"))
        with self.assertRaisesRegex(ValueError,"file hash"):verify(self.output)
        self.rewrite_archive(lambda p:p.pop("tea_sense_server/web/app.js"))
        with self.assertRaisesRegex(ValueError,"entries"):verify(self.output)
    def test_zip_escape_and_extraction_overwrite_rejected(self):
        build(self.root,self.output)
        with self.assertRaises(FileExistsError):extract(self.output,self.root)
        manifest=json.loads((self.output/"manifest.json").read_text())
        item=manifest["files"][0];old=item["path"];item["path"]="../escape"
        raw=json.dumps(manifest).encode()
        def mutate(payloads):
            payloads["../escape"]=payloads.pop(old);payloads["manifest.json"]=raw
        self.rewrite_archive(mutate);(self.output/"manifest.json").write_bytes(raw)
        with self.assertRaisesRegex(ValueError,"Unsafe"):verify(self.output)
    def test_junction_or_symlink_not_followed(self):
        outside=Path(self.temp.name)/"outside";outside.mkdir();(outside/"steal.h").write_text("OUTSIDE")
        link=self.root/"tea_sense/App/outside"
        if os.name=="nt":
            # Fixture-only junction creation, using one native Windows shell.
            result=subprocess.run(["cmd","/d","/c","mklink","/J",str(link),str(outside)],capture_output=True)
            self.assertEqual(result.returncode,0)
        else:link.symlink_to(outside,target_is_directory=True)
        try:
            entry=next(x for x in inventory(self.root) if x["path"].endswith("/outside"))
            self.assertFalse(entry["include"])
            build(self.root,self.output)
            self.assertFalse(any("steal.h" in x["path"] for x in verify(self.output)["files"]))
        finally:
            if os.name=="nt":os.rmdir(link)
            else:link.unlink()
    def test_required_source_missing_rejected(self):
        (self.root/"tea_sense/Middlewares/Third_Party/coreJSON/core_json.c").unlink()
        with self.assertRaisesRegex(ValueError,"Required source"):build(self.root,self.output)
        self.assertFalse(self.output.exists())

if __name__=="__main__":unittest.main()
