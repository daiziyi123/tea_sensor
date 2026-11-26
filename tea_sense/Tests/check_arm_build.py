"""Rebuild the single MDK target and audit its real C library output path."""
import os
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
MDK = ROOT / "MDK-ARM"
TOOL_BIN = Path(os.environ.get("BSP_ARM_BIN", r"D:\Program Files\Keil_v5\ARM\ARMCC\bin"))
UV4 = Path(os.environ.get("APP_MDK_UV4", str(TOOL_BIN.parents[2] / "UV4/UV4.exe")))
for tool in (UV4, TOOL_BIN / "fromelf.exe"):
    if not tool.is_file():
        raise SystemExit(f"Missing MDK tool: {tool}; set APP_MDK_UV4 / BSP_ARM_BIN")
project = ET.parse(MDK / "tea_sense.uvprojx")
assert project.findtext(".//useUlib") == "0", "Retarget requires standard Arm C library"
log = Path(os.environ.get("APP_ARM_BUILD_LOG", str(ROOT / "Docs/validation/printf_mdk_rebuild.log")))
log.parent.mkdir(parents=True, exist_ok=True)
print("Full MDK rebuild from current sources (no flash)", flush=True)
result = subprocess.run([str(UV4), "-r", str(MDK / "tea_sense.uvprojx"),
    "-t", "tea_sense", "-o", str(log)],
    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
print(log.read_text(encoding="utf-8", errors="replace"), flush=True)
if result.returncode != 0:
    raise SystemExit(f"MDK returned {result.returncode}; inspect {log}")
image = MDK / "tea_sense/tea_sense.axf"
symbols = subprocess.run([str(TOOL_BIN / "fromelf.exe"), "--text", "-s", str(image)],
                         capture_output=True, text=True, check=True).stdout
disassembly = subprocess.run([str(TOOL_BIN / "fromelf.exe"), "--text", "-c", str(image)],
                             capture_output=True, text=True, check=True).stdout
for symbol in ("HAL_UART_Transmit", "HAL_UART_Transmit_IT"):
    assert re.search(r"\b" + symbol + r"\b", symbols), symbol
assert re.search(r"\b(?:__2)?snprintf\b", symbols), "Cloud JSON formatter missing"
assert not re.search(r"\b(?:__2)?printf\s+0x", symbols), "Console printf frontend remains linked"
assert not re.search(r"\bBspConsole_(?:Init|Process|Send)\b", symbols), "Unused console transport remains linked"
assert not re.search(r"\b(?:_sys_io|_semihost|_printf_fp|_printf_double)", symbols)
assert not re.search(r"\b(?:SVC|SWI|BKPT)\b", disassembly), "Debugger/semihost instruction in image"
mapping = (MDK / "tea_sense/tea_sense.map").read_text(errors="replace")
assert not re.search(r"^\s*[^\r\n]*sys_io\.o", mapping, re.M), "Arm semihost I/O linked"
for symbol in ("_sys_exit",):
    assert re.search(r"^\s*" + symbol + r"\s+0x[^\r\n]*bsp_console\.o", mapping, re.M), symbol
assert "App_Process" in mapping and "AppUpload_Format" in mapping
print("PASS: cloud snprintf/UART transport retained; console printf/transport removed; no semihost or float formatting")
