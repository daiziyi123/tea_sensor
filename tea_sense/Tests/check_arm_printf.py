"""Optional CPU emulation of the real linked cloud-only application, with fake HAL.

Requires Python unicorn (or APP_ARM_EMULATOR_PATH pointing to its package dir)
and a fresh MDK build. No target board or debugger is used. Only main.o and formatter fixture credentials are replaced in a temporary image;
App/Sensors/Bsp code and Arm libc are production. No hardware timing is emulated.
"""
import os
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

sys.stdout.reconfigure(encoding="utf-8")

if os.environ.get("APP_ARM_EMULATOR_PATH"):
    sys.path.insert(0, os.environ["APP_ARM_EMULATOR_PATH"])
try:
    from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
    from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_SP
except ImportError:
    raise SystemExit("Optional ARM printf execution requires Python unicorn / APP_ARM_EMULATOR_PATH")

ROOT = Path(__file__).resolve().parents[1]
MDK = ROOT / "MDK-ARM"
TOOLS = Path(os.environ.get("BSP_ARM_BIN", r"D:\Program Files\Keil_v5\ARM\ARMCC\bin"))
HARNESS = (ROOT / "Tests/arm_integration_main.c").read_text(encoding="utf-8")
versions=(ROOT/"App/Inc/app_version.h").read_text(encoding="utf-8")
firmware_version=re.search(r'APP_FIRMWARE_VERSION "(.*?)"',versions)[1]
calibration_revision=re.search(r'APP_CALIBRATION_REVISION "(.*?)"',versions)[1]
schema=int(re.search(r'APP_PROTOCOL_SCHEMA_VERSION (\d+)',versions)[1])
startup=f"Tea Sense 固件：{firmware_version}，协议：V{schema}，标定：{calibration_revision}"

def elf_sections_and_symbols(binary):
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", binary)
    sections = [struct.unpack_from("<IIIIIIIIII", binary, header[6] + index * header[11])
                for index in range(header[12])]
    symbols = {}
    for section in sections:
        if section[1] != 2:
            continue
        strings_section = sections[section[6]]
        strings = binary[strings_section[4]:strings_section[4] + strings_section[5]]
        for offset in range(section[4], section[4] + section[5], section[9]):
            name, address, size, info, other, index = struct.unpack_from("<IIIBBH", binary, offset)
            label = strings[name:strings.find(b"\0", name)].decode(errors="replace")
            if address:
                symbols[label] = address
    return sections, symbols

with tempfile.TemporaryDirectory(prefix="tea_sense_arm_printf_") as folder:
    folder = Path(folder)
    source, obj = folder / "test_main.c", folder / "test_main.o"
    source.write_text(HARNESS, encoding="utf-8")
    (folder / "app_secrets.h").write_text("#define APP_INGEST_TOKEN_CONFIGURED 1\n#define APP_INGEST_TOKEN_JSON "+json.dumps(json.dumps("fixture-arm"))+"\n")
    project = ET.parse(MDK / "tea_sense.uvprojx")
    compile_command = [str(TOOLS / "armcc.exe"), "--cpu", "Cortex-M7.fp.dp", "--c99", "--gnu", "-O1", "--split_sections"]
    compile_command += ["-D" + definition for definition in project.findtext(".//Cads/VariousControls/Define").split(",")]
    for include in project.findtext(".//Cads/VariousControls/IncludePath").split(";"):
        compile_command += ["-I", str((MDK / include).resolve())]
    compile_command += ["-I",str(folder), '-DAPP_DEVICE_ID="demo_h743_001"']
    # Put fixture credentials before App/Inc for both the harness and exact production formatter.
    compile_command[1:1]=["-I",str(folder)]
    subprocess.run(compile_command + ["-c", str(source), "-o", str(obj)], check=True)
    upload_obj=folder/"app_upload.o"
    subprocess.run(compile_command+["-c",str(ROOT/"App/Src/app_upload.c"),"-o",str(upload_obj)],check=True)
    response = (MDK / "tea_sense/tea_sense.lnp").read_text()
    assert response.count('"tea_sense\\main.o"') == 1
    response = response.replace('"tea_sense\\main.o"', '"' + str(obj) + '"')
    assert response.count('"tea_sense\\app_upload.o"')==1
    response=response.replace('"tea_sense\\app_upload.o"','"'+str(upload_obj)+'"')
    image = folder / "printf.axf"
    response = re.sub(r"--list[^\r\n]*", lambda _: f'--list "{folder / "printf.map"}" -o "{image}"', response)
    link = folder / "printf.lnp"
    link.write_text(response)
    subprocess.run([str(TOOLS / "armlink.exe"), "--via", str(link)], cwd=MDK, check=True)
    binary = image.read_bytes()
    sections, symbols = elf_sections_and_symbols(binary)
    cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    for address, size in ((0x08000000, 0x200000), (0x20000000, 0x20000),
                          (0x24000000, 0x80000), (0x40000000, 0x10000), (0xE000E000, 0x2000)):
        cpu.mem_map(address, size)
    for section in sections:
        if section[2] & 2 and section[1] != 8 and section[5]:
            cpu.mem_write(section[3], binary[section[4]:section[4] + section[5]])
    cpu.mem_write(0xE000ED88, struct.pack("<I", 0xF00000))
    cpu.reg_write(UC_ARM_REG_SP, symbols["__initial_sp"])
    captured=bytearray()
    console_trace=bytearray()
    frames=[]
    checkpoints=[]
    minimum_sp=[symbols["__initial_sp"]]
    minimum_pc=[0]
    application_started=[False]
    calls=dict(adc=0,config=0,stop=0,calibration=0,write=0,malloc=0,dtu_tx=0,dtu_rx_starts=0,
               weather_tx=0,weather_rx_starts=0,soil_reads=0,wind_reads=0,ad3_reads=0,ad4_reads=0,ad5_reads=0)
    def read_word(name):return struct.unpack("<I",cpu.mem_read(symbols[name],4))[0]
    def write_word(name,value):cpu.mem_write(symbols[name],struct.pack("<I",value))
    def return_value(value):
        cpu.reg_write(UC_ARM_REG_R0,value)
        cpu.reg_write(UC_ARM_REG_PC,cpu.reg_read(UC_ARM_REG_LR))
    def check_console(stage,output):
        assert output == b"", "Firmware emitted unexpected console data"
        assert read_word("g_test_printf_errors")==0
        if stage<=6:
            assert read_word("g_test_acks")==stage-1
        else:
            assert read_word("g_test_acks")==6 and read_word("g_test_rounds")==10802
    def handle(name):
        if name=="HAL_GetTick":return_value(read_word("g_test_tick_ms"))
        elif name=="HAL_UART_GetState":
            cpu.mem_write(symbols["huart2"],struct.pack("<I",0x40004400))
            cpu.mem_write(0x4000441C,struct.pack("<I",1<<6))
            return_value(0x20)
        elif name=="HAL_UART_Transmit_IT":
            assert cpu.reg_read(UC_ARM_REG_R0)==symbols["huart6"] and calls["dtu_rx_starts"]>0
            length=cpu.reg_read(UC_ARM_REG_R2)
            payload=bytes(cpu.mem_read(cpu.reg_read(UC_ARM_REG_R1),length))
            assert length==len(payload) and 1000<length<=6142 and payload.endswith(b"\r\n")
            message=json.loads(payload)
            assert set(message)=={"schema_version","device_id","seq","t_ms","window_ms","data","token"}
            sequence=calls["dtu_tx"]+1
            assert message["seq"]==sequence and message["t_ms"]==sequence*180000
            assert message["schema_version"]==2 and message["device_id"]=="demo_h743_001" and message["token"]=="fixture-arm"
            data=message["data"]
            assert len(data)==11 and "adc_raw" not in message
            assert message["window_ms"]==180000
            assert data["soil_moisture_pct"]["value"]==(0 if sequence==2 else 60.85)
            assert data["soil_moisture_pct"]["statistics"]["method"]=="median"
            assert data["soil_ph"]["value"]==6
            assert data["wind_speed_mps"]["value"]==15 and data["wind_speed_mps"]["wind_speed_sample_max_mps"]==15
            assert data["wind_direction_deg"]["value"]==134.83
            assert data["rain_intensity_mm_min"]["value"]==4
            for key,method in (("wind_speed_mps","mean"),("wind_direction_deg","circular_mean"),("rain_intensity_mm_min","time_weighted_mean")):
                assert data[key]["statistics"]["method"]==method
                assert data[key]["statistics"]["end_ms"]==sequence*180000
            for key in ("temperature_c","air_humidity_rh_pct","light_lux","pressure_kpa","pm25_ug_m3","pm10_ug_m3"):
                assert data[key]["statistics"]["method"]=="latest" and data[key]["status"]=="OK"
            ack=json.dumps(dict(ok=True,device_id="demo_h743_001",seq=sequence,id=sequence,duplicate=False),separators=(",",":")).encode()+b"\r\n"
            assert len(ack)<=512
            cpu.mem_write(symbols["g_test_ack"],ack);write_word("g_test_ack_length",len(ack));write_word("g_test_ack_ready",1)
            write_word("g_test_tx_pending",1)
            write_word("g_test_tx_due",read_word("g_test_tick_ms")+(length*10000+115199)//115200)
            del message["token"]
            frames.append(message)
            calls["dtu_tx"]+=1
            return_value(0)
        elif name=="HAL_UART_Transmit":
            uart=cpu.reg_read(UC_ARM_REG_R0)
            if uart==symbols["huart3"]:
                request=bytes(cpu.mem_read(cpu.reg_read(UC_ARM_REG_R1),cpu.reg_read(UC_ARM_REG_R2)))
                register=request[3]
                golden={0:"010300000002C40B",2:"01030002000265CB",20:"010300140002840F",11:"0103000B0001F5C8"}
                assert request.hex().upper()==golden[register]
                words={0:(708,194),2:(0,1249),20:(22,23),11:(10373,)}[register]
                response=bytearray((1,3,len(words)*2))
                for word in words:response.extend(word.to_bytes(2,"big"))
                crc=65535
                for byte in response:
                    crc^=byte
                    for _ in range(8):crc=(crc>>1)^0xA001 if crc&1 else crc>>1
                response.extend(crc.to_bytes(2,"little"))
                cpu.mem_write(symbols["g_test_weather_reply"],bytes(response))
                cpu.mem_write(symbols["g_test_weather_length"],struct.pack("<I",len(response)))
                calls["weather_tx"]+=1;return_value(0);return
            raise AssertionError("Unexpected console UART transmission")
        elif name=="HAL_UART_Receive_IT":
            uart=cpu.reg_read(UC_ARM_REG_R0)
            assert cpu.reg_read(UC_ARM_REG_R2)==1
            if uart==symbols["huart3"]:
                cpu.mem_write(symbols["g_test_weather_rx"],struct.pack("<I",cpu.reg_read(UC_ARM_REG_R1)))
                calls["weather_rx_starts"]+=1
            else:
                assert uart==symbols["huart6"],"Gas route must remain disabled"
                write_word("g_test_dtu_rx_byte",cpu.reg_read(UC_ARM_REG_R1));calls["dtu_rx_starts"]+=1
            return_value(0)
        elif name in ("HAL_UART_AbortReceive","HAL_UART_AbortTransmit"):return_value(0)
        elif name=="HAL_ADCEx_Calibration_Start":calls["calibration"]+=1;return_value(0)
        elif name=="HAL_ADC_ConfigChannel":
            channel=struct.unpack("<I",cpu.mem_read(cpu.reg_read(UC_ARM_REG_R1),4))[0]
            expected=read_word(("g_test_soil_channel","g_test_wind_channel","g_test_ad3_channel","g_test_ad4_channel","g_test_ad5_channel")[calls["config"]%5])
            assert channel==expected
            calls["selected"]=channel;calls["config"]+=1;return_value(0)
        elif name=="HAL_ADC_Start":calls["adc"]+=1;return_value(0)
        elif name=="HAL_ADC_PollForConversion":
            return_value(3 if (calls["selected"]==read_word("g_test_soil_channel") and read_word("g_test_poll_error")) or (calls["selected"]==read_word("g_test_ad5_channel") and read_word("g_test_ad5_poll_error")) else 0)
        elif name=="HAL_ADC_GetValue":
            for label,channel in (("soil","g_test_soil_channel"),("wind","g_test_wind_channel"),("ad3","g_test_ad3_channel"),("ad4","g_test_ad4_channel"),("ad5","g_test_ad5_channel")):
                if calls["selected"]==read_word(channel):
                    calls[label+"_reads"]+=1
                    return_value(read_word("g_test_raw" if label=="soil" else "g_test_"+label+"_raw"))
                    return
            raise AssertionError("Unknown ADC input")
        elif name=="HAL_ADC_Stop":
            calls["stop"]+=1
            return_value(1 if calls.get("selected")==read_word("g_test_ad5_channel") and read_word("g_test_ad5_poll_error") and calls["stop"]%2==0 else 0)
        elif name=="_sys_write":calls["write"]+=1
        elif name=="malloc":calls["malloc"]+=1
        elif name=="TestArm_Observe":
            stage=cpu.reg_read(UC_ARM_REG_R0);check_console(stage,bytes(captured));checkpoints.append(stage)
            console_trace.extend(captured);captured.clear()
        elif name=="TestArm_Done":cpu.emu_stop()
        elif name in ("Error_Handler","_sys_exit"):raise AssertionError("Unexpected ARM failure: "+name)
    names=["HAL_GetTick","HAL_UART_GetState","HAL_UART_Transmit","HAL_UART_Transmit_IT","HAL_UART_Receive_IT","HAL_UART_AbortReceive","HAL_UART_AbortTransmit",
           "HAL_ADCEx_Calibration_Start","HAL_ADC_ConfigChannel","HAL_ADC_Start","HAL_ADC_PollForConversion","HAL_ADC_GetValue","HAL_ADC_Stop",
           "_sys_write","malloc","TestArm_Observe","TestArm_Done","Error_Handler","_sys_exit"]
    addresses={symbols[name]&~1:name for name in names if name in symbols}
    def hook(instance,address,size,user):
        sp=cpu.reg_read(UC_ARM_REG_SP)
        if address == (symbols["main"] & ~1):
            application_started[0]=True
        if application_started[0] and 0x20000000<=sp<minimum_sp[0]:
            minimum_sp[0]=sp;minimum_pc[0]=address
        if address in addresses:handle(addresses[address])
    cpu.hook_add(UC_HOOK_CODE,hook)
    cpu.emu_start(symbols["__main"]|1,0,count=1000000000)
    assert checkpoints==[1,2,3,4,5,6,7],(checkpoints,calls)
    assert calls["calibration"]==1 and calls["config"]==54010 and calls["stop"]==108020
    assert calls["dtu_tx"]==6 and calls["weather_tx"]>=200 and calls["malloc"]==0 and calls["write"]==0,calls
    assert calls["ad5_reads"]==10802, calls
    evidence=Path(os.environ.get("APP_ARM_EVIDENCE_DIR", str(ROOT/"Docs/validation")))
    evidence.mkdir(parents=True,exist_ok=True)
    prefix=os.environ.get("APP_ARM_EVIDENCE_PREFIX","integration")
    (evidence/(prefix+"_arm_console.txt")).write_bytes(console_trace)
    (evidence/(prefix+"_arm_frames_redacted.json")).write_text(json.dumps(frames,ensure_ascii=False,indent=2),encoding="utf-8")
    print(console_trace.decode("utf-8"),end="")
    print("PASS: real ARM/C library 180s V2/metadata, six uploads/ACKs, 10802 rounds, calibrated values, no console/malloc")
    nearest=sorted(((address&~1,name) for name,address in symbols.items() if (address&~1)<=minimum_pc[0]),reverse=True)[:1]
    assert symbols["__initial_sp"]-minimum_sp[0] <= 800, "Application stack must leave >=224 bytes in configured 1KiB stack" 
    print("Observed application CPU stack bytes:",symbols["__initial_sp"]-minimum_sp[0],"PC",hex(minimum_pc[0]),nearest,"initial_sp",hex(symbols["__initial_sp"]))
