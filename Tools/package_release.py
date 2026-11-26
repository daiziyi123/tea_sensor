"""Controlled source-only release: fixed rules, no private configuration or binaries."""
import argparse
import hashlib
import json
import os
import re
import stat
import subprocess
import zipfile
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath

DOCS = {"README.md","wiring_power_interfaces.md","device_calibration_register.md",
        "protocol_v2.md","build_operations.md","acceptance_records.md",
        "release_test_results.json","release_record.json","source_manifest.sha256",
        "release_host.log","release_architecture.log","release_arm_build.log","release_arm_cpu.log",
        "release_wire.log","release_server.log","release_node.log","release_node_syntax.log",
        "release_package_tests.log","release_isolated_host.log","release_isolated_architecture.log","release_arm_console.txt","release_arm_frames_redacted.json"}
FIRM_DOCS={"code_architecture.md","integration_v2.md","coreJSON-LICENSE.txt","app_secrets.h.example"}
DOCS.update({"rain_integration_validation.md","rain_test_results.json","rain_release_record.json",
             "rain_protection_audit.json","rain_changed_files.json","rain_source_manifest.sha256",
             "rain_host.log","rain_architecture.log","rain_arm_build.log","rain_arm_build_check.log",
             "rain_arm_cpu.log","rain_wire.log","rain_server.log","rain_node.log","rain_node_syntax.log",
             "rain_package_tests.log","rain_isolated_host.log","rain_isolated_architecture.log",
             "rain_arm_console.txt","rain_arm_frames_redacted.json"})
DOCS.update({"rain_dashboard_1920.jpg","rain_dashboard_1280.jpg","rain_dashboard_390.jpg",
             "rain_dashboard_fullscreen.jpg","rain_dashboard_zero.jpg","rain_dashboard_failure.jpg",
             "rain_dashboard_stale.jpg","rain_dashboard_legacy.jpg","rain_dashboard_history.jpg",
             "rain_dashboard_reconnected.jpg"})
FIRM_DIRS={"App","Sensors","Bsp","Core","Drivers","MDK-ARM","Middlewares","RTE"}
SERVER_DIRS={"app","web","tests","tools"}
EXTENSIONS={".c",".h",".s",".ld",".sct",".scf",".uvprojx",".uvoptx",".rteconfig",".xml",
            ".ini",".py",".js",".css",".html",".json",".md",".txt",".cmd",".bat",".yml",".yaml",".toml"}
REQUIRED=("README.md","start_server.cmd","Tools/package_release.py",
          "tea_sense/App/Inc/app_data.h","tea_sense/App/Inc/app_version.h",
          "tea_sense/App/Src/app_main.c","tea_sense/App/Src/app_upload.c",
          "tea_sense/Sensors/Inc/sensor_rain.h","tea_sense/Sensors/Src/sensor_rain.c",
          "tea_sense/Tests/rain_cases.h","tea_sense/Tests/rain_calibration_samples.json",
          "tea_sense/Tests/check_rain_calibration.py","tea_sense_server/tests/test_rain.py",
          "tea_sense/Bsp/Src/bsp_uart_irq.c","tea_sense/Core/Src/main.c",
          "tea_sense/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_uart.c",
          "tea_sense/MDK-ARM/tea_sense.uvprojx","tea_sense/MDK-ARM/tea_sense.uvoptx",
          "tea_sense/MDK-ARM/startup_stm32h743xx.s","tea_sense/MDK-ARM/RTE/_tea_sense/RTE_Components.h",
          "tea_sense/Middlewares/Third_Party/coreJSON/core_json.c",
          "tea_sense/Middlewares/Third_Party/coreJSON/core_json.h",
          "tea_sense/Middlewares/Third_Party/coreJSON/LICENSE",
          "tea_sense/Tests/run_host_tests.py","tea_sense/Tests/check_architecture.py",
          "tea_sense_server/app/main.py","tea_sense_server/app/protocol.py",
          "tea_sense_server/app/storage.py","tea_sense_server/requirements.txt",
          "tea_sense_server/requirements-dev.txt","tea_sense_server/web/app.js",
          "tea_sense_server/web/index.html","tea_sense_server/web/style.css")

def linked(path):
    info=path.lstat()
    return stat.S_ISLNK(info.st_mode) or bool(getattr(info,"st_file_attributes",0)&0x400)

def policy(relative):
    parts=PurePosixPath(relative).parts
    lower=[p.lower() for p in parts]
    name=parts[-1];base=name.lower()
    if any(p in {".git",".svn",".venv","__pycache__","backup","backups","history","debug","objects","listings"}
           for p in lower):
        return False,"backup/cache/developer history"
    if ("secret" in base and base!="app_secrets.h.example") or (base.startswith(".env") and base!=".env.example"):
        return False,"private configuration: separately provisioned"
    if any(word in base for word in ("private","credential","secrets_export","dtu_config")):
        return False,"private/export configuration"
    if ".db" in base or base.endswith((".sqlite",".sqlite3","-wal","-shm",".bak",".zip",".7z")):
        return False,"database/backup/archive"
    if base.endswith((".axf",".hex",".elf",".bin",".o",".obj",".map",".lst",".lnp",".uvguix")) or ".uvguix." in base:
        return False,"source preview excludes firmware binaries and generated build products"
    if len(parts)==1:
        return (name in {"README.md","start_server.cmd","integration_notes.md"},"reviewed root entry" if name in {"README.md","start_server.cmd","integration_notes.md"} else "outside fixed release scope")
    top=parts[0]
    if top=="Docs":
        return (len(parts)==2 and name in DOCS,"current handover/evidence" if len(parts)==2 and name in DOCS else "historical/intermediate evidence")
    if top=="Tools":
        return (len(parts)==2 and name in {"package_release.py","test_package_release.py"},"release tooling")
    if top=="tea_sense":
        if len(parts)>=3 and parts[1]=="Docs":
            return (len(parts)==3 and name in FIRM_DOCS,"current firmware guide/example" if len(parts)==3 and name in FIRM_DOCS else "historical firmware material")
        if len(parts)==2:
            return (name in {"tea_sense.ioc",".mxproject",".gitignore"},"firmware generation/configuration")
        if parts[1] not in FIRM_DIRS|{"Tests"}:
            return False,"outside firmware source roots"
        if len(parts)>=3 and parts[1]=="MDK-ARM" and parts[2]=="tea_sense":
            return False,"generated target output"
    elif top=="tea_sense_server":
        if len(parts)==2:
            return (name in {"README.md","STM32_INTEGRATION.md","VALIDATION.md","requirements.txt","requirements-dev.txt",".env.example",".gitignore",".dockerignore","Dockerfile"},"server entry/example/dependencies")
        if parts[1] not in SERVER_DIRS:
            return False,"server data/deployment/history excluded"
    else:
        return False,"outside fixed project roots"
    return (Path(name).suffix.lower() in EXTENSIONS or name in {"LICENSE","LICENSE.md","Makefile"},"reviewed source/static asset" if Path(name).suffix.lower() in EXTENSIONS or name in {"LICENSE","LICENSE.md","Makefile"} else "unapproved extension")

def inventory(root):
    """Do not follow symlinks, junctions or any Windows reparse point."""
    root=Path(root).absolute()
    if linked(root):
        raise ValueError("Linked project root is not allowed")
    items=[]
    def walk(folder):
        for path in sorted(folder.iterdir(),key=lambda p:p.name):
            relative=path.relative_to(root).as_posix()
            if linked(path):
                items.append(dict(path=relative,include=False,reason="symlink/junction/reparse point rejected"))
                continue
            if path.is_dir():
                # Traverse only the named project/docs/tool trees. Never inspect
                # unrelated backups, output packages or private data subtrees.
                if folder==root and path.name not in {"tea_sense","tea_sense_server","Docs","Tools"}:
                    items.append(dict(path=relative+"/",include=False,reason="outside fixed project roots"));continue
                if path.name.lower() in {".git",".svn",".venv","__pycache__","backup","backups","history","data","objects","listings"} or relative=="tea_sense/MDK-ARM/tea_sense":
                    items.append(dict(path=relative+"/",include=False,reason="private data/cache/backup/generated directory"));continue
                walk(path)
            elif path.is_file():
                include,reason=policy(relative)
                items.append(dict(path=relative,include=include,reason=reason))
            else:
                items.append(dict(path=relative,include=False,reason="not a regular file"))
    walk(root)
    return items

def versions(root):
    source=(Path(root)/"tea_sense/App/Inc/app_version.h").read_text(encoding="utf-8")
    def string(name):
        match=re.search(r'^\s*#define\s+'+name+r'\s+"([^"\r\n]+)"\s*$',source,re.M)
        if not match:raise ValueError("Missing authoritative version: "+name)
        return match[1]
    schema=re.search(r'^\s*#define\s+APP_PROTOCOL_SCHEMA_VERSION\s+(\d+)[uU]?\s*$',source,re.M)
    if not schema:raise ValueError("Missing protocol schema source")
    storage=(Path(root)/"tea_sense_server/app/storage.py").read_text(encoding="utf-8")
    schemas=re.findall(r'PRAGMA user_version\s*=\s*(\d+)',storage)
    if not schemas:raise ValueError("Database schema not independently recorded in storage.py")
    return dict(firmware=string("APP_FIRMWARE_VERSION"),protocol_schema=int(schema[1]),
                calibration_revision=string("APP_CALIBRATION_REVISION"),database_schema=max(map(int,schemas)))

def git_identity(folder):
    try:
        result=subprocess.run(["git","-C",str(folder),"rev-parse","HEAD"],capture_output=True,text=True,timeout=10)
        if result.returncode:return {"git":False,"identity":"controlled source hashes; no repository"}
        dirty=subprocess.run(["git","-C",str(folder),"status","--porcelain"],capture_output=True,text=True,check=True,timeout=10)
        return {"git":True,"commit":result.stdout.strip(),"dirty":bool(dirty.stdout)}
    except FileNotFoundError:
        return {"git":None,"identity":"git unavailable; controlled source hashes"}

def safe_output(root,destination):
    destination=Path(destination).absolute()
    if destination.exists() or destination.is_symlink():
        raise FileExistsError("Output already exists")
    for parent in destination.parents:
        if parent.exists() and linked(parent):raise ValueError("Linked output parent")
    resolved=destination.resolve()
    for name in ("tea_sense","tea_sense_server","Docs","Tools"):
        if resolved.is_relative_to((Path(root)/name).resolve()):
            raise ValueError("Output cannot be inside included source trees")
    return destination

def build(root,destination):
    root=Path(root).absolute();destination=safe_output(root,destination)
    items=inventory(root);selected=[item["path"] for item in items if item["include"]]
    missing=set(REQUIRED)-set(selected)
    if missing:raise ValueError("Required source missing: "+", ".join(sorted(missing)))
    metadata=versions(root)
    destination.mkdir(parents=False)
    entries=[]
    with zipfile.ZipFile(destination/"tea_sense_source.zip","w",zipfile.ZIP_DEFLATED) as archive:
        for relative in sorted(selected):
            path=root/relative
            # Recheck after inventory; no selected linked ancestor may redirect.
            for check in [path,*list(path.parents)[:len(PurePosixPath(relative).parts)-1]]:
                if linked(check):raise ValueError("Source became a link")
            payload=path.read_bytes()
            archive.writestr(relative,payload)
            entries.append(dict(path=relative,sha256=hashlib.sha256(payload).hexdigest(),size=len(payload)))
        tests=[]
        record=root/"Docs/rain_test_results.json"
        if not record.is_file():record=root/"Docs/release_test_results.json"
        if record.is_file():
            for test in json.loads(record.read_text(encoding="utf-8")):
                tests.append({key:test[key] for key in ("test","exit_code","log") if key in test})
        server_rows=[e for e in entries if e["path"].startswith("tea_sense_server/") and e["path"].endswith((".py",".txt",".js",".html",".css"))]
        server_hash=hashlib.sha256(json.dumps(server_rows,sort_keys=True).encode()).hexdigest()
        manifest=dict(format_version=1,kind="source-preview",generated_utc=datetime.now(timezone.utc).isoformat(),
                      versions=metadata,server_version={"declared":None,"source_sha256":server_hash},
                      workspace={name:git_identity(root/name) for name in ("tea_sense","tea_sense_server")},
                      firmware_binary={"provided":False,"reason":"source-only preview; private-config firmware requires separate controlled delivery"},
                      configuration="Private files omitted; provision app_secrets.h and .env separately. No claim of immediate production upload.",
                      tests=tests,unverified=["real board/ISR stack high water","power/wiring","rain midpoint/spray/reference accuracy","4G/network/restarts","wind/pH accuracy and north reference","24-72h endurance"],
                      files=entries)
        encoded=(json.dumps(manifest,ensure_ascii=False,indent=2)+"\n").encode("utf-8")
        archive.writestr("manifest.json",encoded)
    (destination/"manifest.json").write_bytes(encoded)
    digest=hashlib.sha256((destination/"tea_sense_source.zip").read_bytes()).hexdigest()
    (destination/"tea_sense_source.zip.sha256").write_text(digest+"  tea_sense_source.zip\n",encoding="ascii")
    verify(destination)
    return manifest

def verify(directory):
    directory=Path(directory)
    if linked(directory):raise ValueError("Linked package directory")
    raw=(directory/"manifest.json").read_bytes();manifest=json.loads(raw)
    paths=[item["path"] for item in manifest["files"]]
    if len(paths)!=len(set(paths)):raise ValueError("Duplicate manifest path")
    archive_path=directory/"tea_sense_source.zip"
    expected=(directory/"tea_sense_source.zip.sha256").read_text(encoding="ascii").split()[0]
    if hashlib.sha256(archive_path.read_bytes()).hexdigest()!=expected:raise ValueError("Archive SHA-256 mismatch")
    with zipfile.ZipFile(archive_path) as archive:
        names=archive.namelist()
        if len(names)!=len(set(names)) or set(names)!=set(paths)|{"manifest.json"}:
            raise ValueError("Archive entries do not match manifest")
        if archive.read("manifest.json")!=raw:raise ValueError("Inner and outer manifest differ")
        for item in manifest["files"]:
            name=item["path"];parts=PurePosixPath(name).parts
            if not parts or name.startswith("/") or "\\" in name or any(p in {"..","."} or ":" in p for p in parts):
                raise ValueError("Unsafe archive path")
            if not policy(name)[0]:raise ValueError("Unapproved release entry")
            info=archive.getinfo(name)
            if stat.S_ISLNK(info.external_attr>>16):raise ValueError("Linked archive entry")
            payload=archive.read(name)
            if len(payload)!=item["size"] or hashlib.sha256(payload).hexdigest()!=item["sha256"]:
                raise ValueError("Delivered file hash mismatch: "+name)
    return manifest

def extract(directory,destination):
    manifest=verify(directory);destination=Path(destination).absolute()
    if destination.exists() or destination.is_symlink():raise FileExistsError("Extraction target exists")
    for parent in destination.parents:
        if parent.exists() and linked(parent):raise ValueError("Linked extraction parent")
    destination.mkdir()
    with zipfile.ZipFile(Path(directory)/"tea_sense_source.zip") as archive:
        for item in manifest["files"]:
            path=destination/item["path"];path.parent.mkdir(parents=True,exist_ok=True)
            with path.open("xb") as stream:stream.write(archive.read(item["path"]))
    return destination

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    sub=parser.add_subparsers(dest="action",required=True)
    preview=sub.add_parser("preview");preview.add_argument("--root",type=Path,default=Path(__file__).resolve().parents[1])
    create=sub.add_parser("build");create.add_argument("--root",type=Path,default=Path(__file__).resolve().parents[1]);create.add_argument("--output",type=Path,required=True)
    check=sub.add_parser("verify");check.add_argument("--package-dir",type=Path,required=True)
    unpack=sub.add_parser("extract");unpack.add_argument("--package-dir",type=Path,required=True);unpack.add_argument("--destination",type=Path,required=True)
    args=parser.parse_args()
    try:
        if args.action=="preview":
            for item in inventory(args.root):print(json.dumps(item,ensure_ascii=False))
        elif args.action=="build":
            manifest=build(args.root,args.output);print(json.dumps({"kind":manifest["kind"],"files":len(manifest["files"]),"versions":manifest["versions"]}))
        elif args.action=="verify":print("PASS:",len(verify(args.package_dir)["files"]),"file hashes")
        else:print("Extracted:",extract(args.package_dir,args.destination))
    except (OSError,ValueError,KeyError,zipfile.BadZipFile,subprocess.SubprocessError) as error:
        parser.exit(1,"ERROR: "+str(error)+"\n")

if __name__=="__main__":
    main()
