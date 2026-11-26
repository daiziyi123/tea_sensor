"""Check active source layering and MDK paths; historical artifacts are excluded."""
from pathlib import Path
import re
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
LAYERS = ("App", "Sensors", "Bsp")
ALLOWED = {"App": set(LAYERS), "Sensors": {"Sensors", "Bsp"}, "Bsp": {"Bsp"}}
headers = {path.name: path for layer in LAYERS
           for path in (ROOT / layer / "Inc").glob("*.h")}
sources = [path for layer in LAYERS for path in (ROOT / layer / "Src").glob("*.c")]
old_symbols = re.compile(
    r"\b(?:SoilApp_|SoilAdc_|Usart2Rs485_|USART2_RS485_|Rs485Direction_|"
    r"SOIL_(?:APP_|MODE_|ADC_|UART_|SENSOR_|RS485_)|"
    r"soil_sensor_app|soil_adc\b|soil_app_config|usart2_rs485|rs485_direction)")
removed_runtime_symbols = re.compile(
    r"\b(?:APP_" r"MODE\w*|App" r"Log\w*|app_" r"log\b|"
    r"TEA_SENSE_USART2_" r"TEST|SensorSoilRead" r"Mode|SENSOR_SOIL_READ_\w+)")
dependencies = {}
for layer in LAYERS:
    assert {path.name for path in (ROOT / layer).iterdir()} == {"Inc", "Src"}
    for path in sorted((ROOT / layer).rglob("*")):
        if not path.is_file():
            continue
        assert path.parent.name in ("Inc", "Src"), path
        assert re.fullmatch(r"[a-z][a-z0-9_]*\.[ch]", path.name), path
        text = path.read_text(encoding="utf-8")
        assert not old_symbols.search(text), path
        assert not removed_runtime_symbols.search(text), path
        assert not re.search(r"^\s*extern\s+", text, re.M), path
        if layer != "Bsp":
            assert not re.search(r"\b(?:HAL_|__HAL_|[A-Z]+_HandleTypeDef)", text), path
        dependencies.setdefault(path.stem, set())
        for name in re.findall(r'^#include "([^"]+)"', text, re.M):
            if name not in headers:
                if name == "core_json.h":
                    assert layer == "App" and path.name == "app_upload.c"
                    continue
                assert layer == "Bsp", f"Non-Bsp hardware include: {path}: {name}"
                continue
            target = headers[name]
            assert target.parent.parent.name in ALLOWED[layer], (path, target)
            if target.stem != path.stem:
                dependencies[path.stem].add(target.stem)

def check_cycles(module, visiting, visited):
    assert module not in visiting, f"Circular module dependency: {module}"
    if module in visited:
        return
    visiting.add(module)
    for dependency in dependencies[module]:
        check_cycles(dependency, visiting, visited)
    visiting.remove(module)
    visited.add(module)

visited = set()
for module in dependencies:
    check_cycles(module, set(), visited)
print("PASS: flat Inc/Src layers, include boundaries, no HAL above Bsp, no cycles or writable externs")

mdk = ROOT / "MDK-ARM"
project = ET.parse(mdk / "tea_sense.uvprojx")
paths = project.findtext(".//Cads/VariousControls/IncludePath").split(";")
for path in paths:
    assert (mdk / path).is_dir(), path
for layer in LAYERS:
    assert (ROOT / layer / "Inc").resolve() in {(mdk / path).resolve() for path in paths}
listed = []
for group in project.findall(".//Groups/Group"):
    for file in group.findall("Files/File"):
        path = (mdk / file.findtext("FilePath")).resolve()
        assert path.is_file(), path
        listed.append(path)
        if path in sources:
            assert group.findtext("GroupName") == path.parent.parent.name
assert len(listed) == len(set(listed)), "Duplicate MDK source"
assert set(sources) == {path for path in listed if path.parent.parent.name in LAYERS}
options = ET.parse(mdk / "tea_sense.uvoptx")
for path in options.findall(".//PathWithFileName"):
    assert (mdk / path.text).is_file(), path.text
for path in (ROOT / "Tests").glob("*"):
    if path.suffix in (".c", ".py") and path.name != Path(__file__).name:
        assert not old_symbols.search(path.read_text(encoding="utf-8")), path
        assert not removed_runtime_symbols.search(path.read_text(encoding="utf-8")), path
for path in (mdk / "tea_sense.uvprojx", mdk / "tea_sense.uvoptx", ROOT / "Core/Src/main.c"):
    assert not old_symbols.search(path.read_text(encoding="utf-8")), path
    assert not removed_runtime_symbols.search(path.read_text(encoding="utf-8")), path
print("PASS: MDK include/source/IDE paths, layer groups, no duplicates or replaced symbols in active code")
print("PASS: single runtime flow; removed application selector and business logging module have no active references")

ioc = (ROOT / "tea_sense.ioc").read_text(encoding="utf-8")
for setting in ("ProjectManager.DeletePrevious=false", "ProjectManager.BackupPrevious=true",
                "ProjectManager.KeepUserCode=true", "ProjectManager.FirmwarePackage=STM32Cube FW_H7 V1.12.1"):
    assert setting in ioc, setting
assert "PA3.GPIO_Label=RAIN_AD5" in ioc and "PA3.Signal=ADCx_INP15" in ioc
assert "ADC1_INP15,IN15-Single-Ended" in ioc
assert not re.search(r"^PE15\.|RAIN_PULSE|^NVIC\.EXTI15_10_IRQn=",ioc,re.M)
print("PASS: CubeMX flags/package; rain PA3 analog AD5 replaces removed PE15 pulse/EXTI")

assert "NVIC.USART6_IRQn=true" in ioc
usart_source = (ROOT / "Core/Src/usart.c").read_text(encoding="utf-8")
interrupt_source = (ROOT / "Core/Src/stm32h7xx_it.c").read_text(encoding="utf-8")
main_source = (ROOT / "Core/Src/main.c").read_text(encoding="utf-8")
assert "HAL_NVIC_EnableIRQ(USART6_IRQn)" in usart_source
assert re.search(r"void USART6_IRQHandler\(void\).*?HAL_UART_IRQHandler\(&huart6\)", interrupt_source, re.S)
for setting in ("huart6.Init.BaudRate = 115200;", "huart6.Init.WordLength = UART_WORDLENGTH_8B;",
                "huart6.Init.StopBits = UART_STOPBITS_1;", "huart6.Init.Parity = UART_PARITY_NONE;"):
    assert setting in usart_source, setting
assert "PC6.Signal=USART6_TX" in ioc and "PC7.Signal=USART6_RX" in ioc
assert main_source.index("MX_USART6_UART_Init();") < main_source.index("App_Init();")
dtu_source = (ROOT / "Bsp/Src/bsp_dtu.c").read_text(encoding="utf-8")
assert not re.search(r"HAL_GPIO|BspRs485Dir|HAL_UART_Init|Receive_DMA|Transmit_DMA|HAL_Delay", dtu_source)
assert "HAL_UART_Receive_IT(&huart6, &s_rx_byte, 1U)" in dtu_source
print("PASS: generated USART6 IRQ/init/pins/115200-8N1 consistent; DTU uses one-byte IT RX, no GPIO/DMA/reinit")

adc_source = (ROOT / "Core/Src/adc.c").read_text(encoding="utf-8")
for setting in ("hadc1.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV8;", "hadc1.Init.Resolution = ADC_RESOLUTION_16B;",
                "hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;", "hadc1.Init.ContinuousConvMode = DISABLE;",
                "hadc1.Init.NbrOfConversion = 1;", "hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;",
                "sConfig.Channel = ADC_CHANNEL_5;", "sConfig.Rank = ADC_REGULAR_RANK_1;",
                "sConfig.SamplingTime = ADC_SAMPLETIME_387CYCLES_5;", "GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;",
                "GPIO_InitStruct.Pull = GPIO_NOPULL;", "WIND_SPEED_AD2_Pin", "WIND_DIR_AD3_Pin", "PH_AD4_Pin", "RAIN_AD5_Pin"):
    assert setting in adc_source, setting
assert "PA6.Signal=ADCx_INP3" in ioc and "PB1.Signal=ADCx_INP5" in ioc
assert "PA5.Signal=ADCx_INP19" in ioc
assert "PA4.Signal=ADCx_INP18" in ioc
assert "PA3.Signal=ADCx_INP15" in ioc
pin_header = (ROOT / "Core/Inc/main.h").read_text(encoding="utf-8")
assert "#define WIND_SPEED_AD2_Pin GPIO_PIN_6" in pin_header and "#define WIND_SPEED_AD2_GPIO_Port GPIOA" in pin_header
assert "#define SOIL_AD0_Pin GPIO_PIN_1" in pin_header and "#define SOIL_AD0_GPIO_Port GPIOB" in pin_header
assert "#define WIND_DIR_AD3_Pin GPIO_PIN_5" in pin_header and "#define WIND_DIR_AD3_GPIO_Port GPIOA" in pin_header
assert "#define PH_AD4_Pin GPIO_PIN_4" in pin_header and "#define PH_AD4_GPIO_Port GPIOA" in pin_header
assert "#define RAIN_AD5_Pin GPIO_PIN_3" in pin_header and "#define RAIN_AD5_GPIO_Port GPIOA" in pin_header
gpio_source=(ROOT/"Core/Src/gpio.c").read_text(encoding="utf-8")
for source in (pin_header,gpio_source,interrupt_source,(ROOT/"Core/Inc/stm32h7xx_it.h").read_text(encoding="utf-8")):
    assert "RAIN_PULSE" not in source and "EXTI15_10" not in source
assert re.search(r"GPIO_InitStruct.Pin = RAIN_AD5_Pin[^;]*;\s*GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;\s*GPIO_InitStruct.Pull = GPIO_NOPULL;\s*HAL_GPIO_Init\(GPIOA,",adc_source)
assert not re.search(r"Mcu.IP\d+=ADC2", ioc) and "MX_ADC2_Init" not in main_source
adc_bsp = (ROOT / "Bsp/Src/bsp_adc.c").read_text(encoding="utf-8")
assert "BspAdc_ReadRaw(BspAdcInput input)" in adc_bsp
assert "case BSP_ADC_INPUT_SOIL_AD0: channel.Channel = ADC_CHANNEL_5" in adc_bsp
assert "case BSP_ADC_INPUT_WIND_SPEED_AD2: channel.Channel = ADC_CHANNEL_3" in adc_bsp
assert "case BSP_ADC_INPUT_WIND_DIRECTION_AD3: channel.Channel = ADC_CHANNEL_19" in adc_bsp
assert "case BSP_ADC_INPUT_PH_AD4: channel.Channel = ADC_CHANNEL_18" in adc_bsp
assert "case BSP_ADC_INPUT_RAIN_AD5: channel.Channel = ADC_CHANNEL_15" in adc_bsp
assert adc_bsp.index("HAL_ADC_Stop(&hadc1)") < adc_bsp.index("HAL_ADC_ConfigChannel(&hadc1, &channel)") < adc_bsp.index("HAL_ADC_Start(&hadc1)")
assert not re.search(r"HAL_Delay|HAL_ADC_Init|Start_DMA|Start_IT", adc_bsp)
assert "BspAdc_ReadRaw(BSP_ADC_INPUT_SOIL_AD0)" in (ROOT / "Sensors/Src/sensor_soil.c").read_text()
assert "BspAdc_ReadRaw(BSP_ADC_INPUT_WIND_SPEED_AD2)" in (ROOT / "Sensors/Src/sensor_wind_speed.c").read_text()
print("PASS: PB1/PA6/PA5/PA4/PA3 generated analog/no-pull configuration, ADC1 single-rank baseline, explicit channel5/channel3/channel19/channel18/channel15 selection")

for peripheral,handle in (("USART3","huart3"),("UART4","huart4")):
    assert f"{peripheral}.BaudRate=4800" in ioc
    assert f"NVIC.{peripheral}_IRQn=true" in ioc
    assert f"HAL_NVIC_EnableIRQ({peripheral}_IRQn)" in usart_source
    assert re.search(r"void "+peripheral+r"_IRQHandler\(void\).*?HAL_UART_IRQHandler\(&"+handle+r"\)",interrupt_source,re.S)
    for setting in (f"{handle}.Init.BaudRate = 4800;",f"{handle}.Init.WordLength = UART_WORDLENGTH_8B;",
                    f"{handle}.Init.StopBits = UART_STOPBITS_1;",f"{handle}.Init.Parity = UART_PARITY_NONE;"):
        assert setting in usart_source
for setting in ("PD8.Signal=USART3_TX","PD9.Signal=USART3_RX","PC10.Signal=UART4_TX","PC11.Signal=UART4_RX"):
    assert setting in ioc
custom_sources=list((ROOT/"Bsp/Src").glob("*.c"))+list((ROOT/"Core/Src").glob("*.c"))
for callback in ("HAL_UART_RxCpltCallback","HAL_UART_ErrorCallback","HAL_UART_TxCpltCallback"):
    assert sum(len(re.findall(r"\bvoid\s+"+callback+r"\s*\(",p.read_text(encoding="utf-8"))) for p in custom_sources)==1
weather_bsp=(ROOT/"Bsp/Src/bsp_weather_rs485.c").read_text(encoding="utf-8")
assert not re.search(r"HAL_Delay|HAL_UART_Init|Receive_DMA|Transmit_DMA|BspRs485Dir|HAL_GPIO",weather_bsp)
for handle in ("huart3","huart4"):
    assert f"uart==&{handle}" in (ROOT/"Bsp/Src/bsp_uart_irq.c").read_text(encoding="utf-8")
for module in ("app_weather","sensor_weather","bsp_weather_rs485"):
    assert sum(path.stem==module for path in listed)==1
print("PASS: generated USART3/UART4 pins/4800-8N1/IRQs consistent; single HAL dispatch, fixed IT routes, no direction GPIO/DMA/reinit")

assert "HAL_UART_Transmit_IT(&huart6" in dtu_source
assert "HAL_UART_AbortTransmit(&huart6)" in dtu_source
assert "HAL_UART_Transmit(&huart6" not in dtu_source
app_source=(ROOT/"App/Src/app_main.c").read_text(encoding="utf-8")
config=(ROOT/"App/Inc/app_config.h").read_text(encoding="utf-8")
assert "APP_CONSOLE_PERIOD_MS" not in config and "s_last_console_tick_ms" not in app_source
assert "ADC_SAMPLE" not in app_source and "ADC_WINDOW" not in app_source
assert "AD4_SAMPLE" not in app_source and "s_last_ad4_print_tick_ms" not in app_source
for source in list((ROOT/"App/Src").glob("*.c"))+list((ROOT/"Sensors/Src").glob("*.c")):
    body=source.read_text(encoding="utf-8")
    assert not re.search(r"\b(?:printf|fprintf|puts|putchar|BspConsole_(?:Init|Process|Send))\s*\(",body), source
print("PASS: no runtime console formatting, initialization, polling or sends; upload snprintf retained")
assert "#define APP_WEATHER_GAS_ENABLED 0" in config
for path in (ROOT/"Sensors").rglob("*.[ch]"):
    assert "app_config.h" not in path.read_text(encoding="utf-8")
upload=(ROOT/"App/Src/app_upload.c").read_text(encoding="utf-8")
upload_header=(ROOT/"App/Inc/app_upload.h").read_text(encoding="utf-8")
assert "app_secrets.h" not in upload_header and "APP_INGEST_TOKEN" not in upload_header
assert not re.search(r"s_dtu_|App_Dtu|App_UploadWindow|BspDtu_",app_source)
assert "AppUpload_Process" in upload and "BspTime_GetTickMs()" in upload
assert "BSP_DTU_RECOVERY_PERIOD_MS 60000U" in (ROOT/"Bsp/Inc/bsp_config.h").read_text()
assert "BSP_ADC_CAL_RECOVERY_PERIOD_MS 60000U" in (ROOT/"Bsp/Inc/bsp_config.h").read_text()
print("PASS: transport/ACK diagnostics belong to AppUpload; no credentials in public header; bounded Bsp recovery")
print("PASS: async single-owner TX/abort and unique TC dispatch; gas disabled; Sensors independent")

third=ROOT/"Middlewares/Third_Party/coreJSON"
assert sum(path.name=="core_json.c" for path in listed)==1
assert third/"core_json.c" in listed
assert third.resolve() in {(mdk/path).resolve() for path in paths}
assert not (ROOT/"App/Src/core_json.c").exists() and not (ROOT/"App/Inc/core_json.h").exists()
assert (third/"LICENSE").is_file()
for path in list((ROOT/"Sensors").rglob("*.[ch]"))+list((ROOT/"Bsp").rglob("*.[ch]")):
    assert "app_data.h" not in path.read_text(encoding="utf-8")
data=(ROOT/"App/Inc/app_data.h").read_text(encoding="utf-8")
assert re.findall(r"#include (.+)",data)==["<stdint.h>"]
assert not (ROOT/"App/Src/app_data.c").exists()
print("PASS: one neutral App model, version source, and isolated byte-preserved coreJSON dependency")

assert "AD5_SAMPLE" not in app_source and "APP_RAIN_RAW_PRINT_PERIOD_MS" not in config
assert "rain_intensity_mm_min" in data and "rain_intensity_mm_min" in upload
rain_source=(ROOT/"Sensors/Src/sensor_rain.c").read_text(encoding="utf-8")
assert not re.search(r"BspAdc_ReadRaw|printf|\bHAL_|app_config",rain_source)
assert sum(path.stem=="sensor_rain" for path in listed)==1
assert app_source.count("BspAdc_ReadRaw(BSP_ADC_INPUT_RAIN_AD5)")==1
print("PASS: single AD5 sample feeds qualified rain window/eleven-field V2; no raw timer or output")
