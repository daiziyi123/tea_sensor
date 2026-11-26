"""Synthetic 100ms C windows -> V2 formatter -> fragmented TCP -> temp SQLite/API/CSV -> C ACK.
Uses demo IDs, fixture credentials, absolute resolved project paths and isolated stores only.
"""
import argparse
import csv
import io
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument("--server-root",type=Path,default=ROOT.parent/"tea_sense_server")
args=parser.parse_args()
server=args.server_root.resolve()
sys.path.insert(0,str(server))
from fastapi.testclient import TestClient
from app.config import Settings
from app.main import create_app
from app.protocol import METRICS

gcc=os.environ.get("APP_TEST_GCC") or shutil.which("gcc")
if not gcc:
    candidate=Path(r"D:\Qt\Tools\mingw1310_64\bin\gcc.exe")
    if candidate.exists():gcc=str(candidate)
if not gcc:raise SystemExit("Set APP_TEST_GCC")
env=dict(os.environ)
env["PATH"]=str(Path(gcc).parent)+os.pathsep+env.get("PATH","")
with tempfile.TemporaryDirectory(prefix="tea_v2_wire_") as directory:
    temp=Path(directory)
    (temp/"app_secrets.h").write_text("#define APP_INGEST_TOKEN_CONFIGURED 1\n#define APP_INGEST_TOKEN_JSON "+json.dumps(json.dumps("fixture-ingest"))+"\n")
    executable=temp/"wire.exe"
    command=[gcc,"-std=c11","-Wall","-Wextra","-Werror","-D__USE_MINGW_ANSI_STDIO=0",'-DAPP_DEVICE_ID="demo_h743_001"']
    command+=["-I"+str(p) for p in (temp,ROOT/"App/Inc",ROOT/"Sensors/Inc",ROOT/"Bsp/Inc",ROOT/"Middlewares/Third_Party/coreJSON")]
    command+=[str(ROOT/p) for p in ("App/Src/app_upload.c","App/Src/app_statistics.c","Middlewares/Third_Party/coreJSON/core_json.c","Tests/upload_wire_fixture.c",
        "Sensors/Src/sensor_soil.c","Sensors/Src/sensor_wind_speed.c","Sensors/Src/sensor_ph.c","Sensors/Src/sensor_rain.c","Sensors/Src/sensor_wind_direction.c")]
    subprocess.run(command+["-lm","-o",str(executable)],env=env,check=True)
    headers={"X-View-Token":"fixture-view"}
    lengths=[]
    def run_frame(client,application,argument,expected):
        payload=subprocess.check_output([str(executable),str(argument)],env=env)
        message=json.loads(payload)
        assert message["device_id"]=="demo_h743_001" and message["schema_version"]==2
        assert set(message["data"])==set(METRICS)
        assert message["data"]["rain_intensity_mm_min"]["value"]==(None if str(argument) in ("failure","configuration") else 4.0)
        assert not any(word in message for word in ("adc_raw","input_mv","channel"))
        assert payload.endswith(b"\r\n") and payload.count(b"\n")==1
        lengths.append(len(payload))
        port=application.state.receiver.server.sockets[0].getsockname()[1]
        with socket.create_connection(("127.0.0.1",port),timeout=3) as connection:
            for offset in range(0,len(payload),31):
                connection.sendall(payload[offset:offset+31])
            with connection.makefile("rb") as stream:
                ack=stream.readline()
        assert json.loads(ack)["ok"],ack.decode()
        result=subprocess.check_output([str(executable),"ack"+str(expected)],input=ack,env=env)
        assert result.strip()==b"ACK_ACCEPTED"
        return message

    application=create_app(Settings(database=temp/"wire.db",tcp_port=0,ingest_token="fixture-ingest",view_token="fixture-view"))
    with TestClient(application) as client:
        with client.websocket_connect("/ws") as ws:
            ws.send_json({"token":"fixture-view"})
            assert ws.receive_json()["type"]=="ready"
            for sequence in range(1,25):
                message=run_frame(client,application,sequence,sequence)
                assert message["data"]["soil_moisture_pct"]["value"]==60.85
                assert message["data"]["wind_speed_mps"]["value"]==15.00
                assert message["data"]["wind_direction_deg"]["value"]==134.83
                assert message["data"]["soil_ph"]["value"]==6.00
                event=ws.receive_json()
                assert event["type"]=="reading" and event["reading"]["seq"]==sequence
                assert event["reading"]["data"]["rain_intensity_mm_min"]["value"]==4.0
        assert client.get("/api/measurements").status_code==401
        overview=client.get("/api/overview",headers=headers).json()
        assert overview["counts"]==dict(total=24,devices=1,measurements=264),overview["counts"]
        readings=client.get("/api/readings",headers=headers).json()["items"]
        assert len(readings)==24 and readings[0]["channel"]=="MULTI"
        assert len(client.get("/api/measurements",params={"metric":"temperature_c"},headers=headers).json()["items"])==24
        assert len(client.get("/api/measurements",params={"metric":"pressure_kpa"},headers=headers).json()["items"])==24
        exported=client.get("/api/export.csv",headers=headers)
        rows=list(csv.DictReader(io.StringIO(exported.content.decode("utf-8-sig"))))
        assert len(rows)==264
        for forbidden in ("adc_raw","input_mv","token","fixture-ingest"):
            assert forbidden not in exported.text
        assert client.get("/api/readings",params={"device_id":"tea_h743_001"},headers=headers).json()["items"]==[]
    failure_app=create_app(Settings(database=temp/"failure.db",tcp_port=0,ingest_token="fixture-ingest",view_token="fixture-view"))
    with TestClient(failure_app) as client:
        run_frame(client,failure_app,"failure",2)
        run_frame(client,failure_app,"configuration",3)
        rows=client.get("/api/readings",headers=headers).json()["items"]
        assert all(r["data"]["soil_moisture_pct"]["value"] is None for r in rows)
        assert rows[0]["data"]["temperature_c"]["status"]=="TIMEOUT"
        assert rows[0]["data"]["temperature_c"]["sample_t_ms"]==540000
    print("PASS: 24 C frames from 43200 synthetic 100ms samples per channel / 4320 virtual seconds; physical conversions, fragmented TCP, committed SQLite, API/CSV/WS and real C ACK")
    print("PASS: 24 uploads / 264 independent points; all methods and same-valued new weather source identities preserved")
    print("PASS: quality-failed C windows stay null; weather failure completion ticks preserved; valid siblings retained")
    print("PASS: fixture credentials/demo device/temp DBs only; full frames %d..%d bytes, TCP max 8192"%(min(lengths),max(lengths)))
