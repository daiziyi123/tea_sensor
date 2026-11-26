"""Run the real application sources with a fake HAL, never on board hardware."""
import os
import sys
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
subprocess.run([sys.executable,str(ROOT/"Tests/check_rain_calibration.py")],check=True)
GCC = os.environ.get("APP_TEST_GCC") or shutil.which("gcc")
if not GCC:
    candidate = Path(r"D:\Qt\Tools\mingw1310_64\bin\gcc.exe")
    if candidate.exists():
        GCC = str(candidate)
if not GCC:
    raise SystemExit("Set APP_TEST_GCC to a native GCC executable")
env = dict(os.environ)
env["PATH"] = str(Path(GCC).resolve().parent) + os.pathsep + env.get("PATH", "")
sources = sorted(source for layer in ("App", "Sensors", "Bsp") for source in (ROOT / layer / "Src").glob("*.c"))
sources += [ROOT / "Tests/host_stdio.c", ROOT / "Middlewares/Third_Party/coreJSON/core_json.c"]
base = [GCC, "-std=c11", "-Wall", "-Wextra", "-Werror", "-fno-builtin", "-D__USE_MINGW_ANSI_STDIO=0",
        "-I" + str(ROOT / "Tests/stubs"), "-I" + str(ROOT / "App/Inc"),
        "-I" + str(ROOT / "Sensors/Inc"), "-I" + str(ROOT / "Bsp/Inc"), "-I" + str(ROOT / "Middlewares/Third_Party/coreJSON")]
variants = {
    "auto_direction": [],
    "gas_route_enabled": ["-DAPP_WEATHER_GAS_ENABLED=1"],
    "calibration_independent_of_vref": ["-DBSP_ADC_VREF_MV=2500"],
    "short_hardware_timeouts": ["-DBSP_ADC_POLL_TIMEOUT_MS=1",
        "-DBSP_CONSOLE_TX_TIMEOUT_MS=2", "-DBSP_CONSOLE_TC_TIMEOUT_MS=3"],
    "manual_fake_gpio": [
        "-DBSP_RS485_DIRECTION_MODE=1", "-DBSP_RS485_MANUAL_WIRING_CONFIRMED=1",
        "-DBSP_RS485_GPIO_PORT=(&fake_gpio)", "-DBSP_RS485_GPIO_PIN=1",
        "-DBSP_RS485_GPIO_CLOCK_ENABLE=TestEnableGpioClock",
        "-DBSP_RS485_TX_LEVEL=GPIO_PIN_SET", "-DBSP_RS485_RX_LEVEL=GPIO_PIN_RESET"],
}
with tempfile.TemporaryDirectory(prefix="tea_sense_host_") as tmp:
    Path(tmp, "app_secrets.h").write_text('#define APP_INGEST_TOKEN_CONFIGURED 1\n#define APP_INGEST_TOKEN_JSON "\\\"test-upload-token\\\""\n')
    base[1:1] = ["-I" + tmp]
    for name, options in variants.items():
        for test in ("soil_host_test", "dtu_host_test"):
            exe = Path(tmp) / (name + "_" + test + ".exe")
            interception = ["-include", str(ROOT / "Tests/stubs/stdio_fault.h")] if test == "dtu_host_test" else []
            subprocess.run(base + options + interception + list(map(str, sources)) +
                           [str(ROOT / "Tests" / (test + ".c")), "-lm", "-o", str(exe)], check=True, env=env)
            print(name + ": " + test, flush=True)
            subprocess.run([str(exe)], check=True, env=env)
    exe=Path(tmp)/"statistics.exe"
    pure_stats=[str(ROOT/p) for p in ("App/Src/app_statistics.c","Tests/statistics_host_test.c","Sensors/Src/sensor_soil.c","Sensors/Src/sensor_wind_speed.c","Sensors/Src/sensor_ph.c","Sensors/Src/sensor_rain.c","Sensors/Src/sensor_wind_direction.c")]
    subprocess.run(base+pure_stats+["-lm","-o",str(exe)],check=True,env=env)
    subprocess.run([str(exe)],check=True,env=env)
    Path(tmp,"app_secrets.h").write_text("#define APP_INGEST_TOKEN_CONFIGURED 1\n#define APP_INGEST_TOKEN_JSON "+json.dumps(json.dumps("\x01"*256))+"\n")
    exe=Path(tmp)/"largest_rain_frame.exe"
    pure=[str(ROOT/p) for p in ("App/Src/app_upload.c","App/Src/app_statistics.c","Middlewares/Third_Party/coreJSON/core_json.c","Tests/upload_wire_fixture.c")]
    pure += [str(ROOT/"Sensors/Src"/name) for name in ("sensor_soil.c","sensor_wind_speed.c","sensor_ph.c","sensor_wind_direction.c","sensor_rain.c")]
    subprocess.run(base+['-DAPP_DEVICE_ID="'+("demo_"+"x"*59)+'"']+pure+["-lm","-o",str(exe)],check=True,env=env)
    payload=subprocess.check_output([str(exe),"1"],env=env)
    assert json.loads(payload)["token"]=="\x01"*256 and len(json.loads(payload)["data"])==11
    assert len(payload)<6142
    print("PASS: eleven-field frame with maximum escaped256-character token /64-character ID fits6142 bytes; length="+str(len(payload)))
    Path(tmp,"app_secrets.h").write_text('#define APP_INGEST_TOKEN_CONFIGURED 1\n#define APP_INGEST_TOKEN_JSON "\\\"test-upload-token\\\""\n')
    # The unconfirmed manual configuration must be rejected at compile time.
    denied = subprocess.run(base + ["-DBSP_RS485_DIRECTION_MODE=1", "-c",
        str(ROOT / "Bsp/Src/bsp_rs485_dir.c"), "-o", str(Path(tmp) / "denied.o")],
        capture_output=True, text=True, env=env)
    assert denied.returncode != 0 and "Confirm automatic circuit isolation" in denied.stderr
    print("PASS: unconfirmed manual mode is blocked at compile time")
    # Endpoint constants stay in the module; reject a zero-span configuration.
    invalid_header = Path(tmp) / "sensor_soil.h"
    invalid_header.write_text((ROOT / "Sensors/Inc/sensor_soil.h").read_text().replace(
        "#define SENSOR_SOIL_RAW_WET 31146U", "#define SENSOR_SOIL_RAW_WET 436U"))
    denied = subprocess.run(base[:1] + ["-I" + tmp] + base[1:] + ["-c",
        str(ROOT / "Sensors/Src/sensor_soil.c"), "-o", str(Path(tmp) / "invalid_calibration.o")],
        capture_output=True, text=True, env=env)
    assert denied.returncode != 0 and "must be greater than" in denied.stderr
    print("PASS: zero calibration span is rejected at compile time")
    invalid_header.unlink()
    Path(tmp, "app_secrets.h").write_text('#define APP_INGEST_TOKEN_CONFIGURED 0\n#define APP_INGEST_TOKEN_JSON "\\\"\\\""\n')
    missing = Path(tmp) / "missing_key.exe"
    subprocess.run(base + [str(ROOT / "App/Src/app_upload.c"), str(ROOT / "App/Src/app_statistics.c"), str(ROOT / "Middlewares/Third_Party/coreJSON/core_json.c"),
        str(ROOT / "Tests/upload_wire_fixture.c")] + [str(ROOT / "Sensors/Src" / name) for name in
        ("sensor_soil.c","sensor_wind_speed.c","sensor_wind_direction.c","sensor_ph.c","sensor_rain.c")] + ["-lm", "-o", str(missing)], check=True, env=env)
    subprocess.run([str(missing), "missing"], check=True, env=env)
    print("PASS: missing private key explicitly disables JSON upload")
