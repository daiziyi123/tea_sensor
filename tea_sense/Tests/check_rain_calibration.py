"""Verify all user-provided one-second OK records, never infer 100ms logs."""
import json
import re
from pathlib import Path
samples=json.loads((Path(__file__).parent/"rain_calibration_samples.json").read_text(encoding="utf-8"))
full,zero=samples["full_5v"],samples["zero_0v"]
assert (len(full),sum(full),min(full),max(full))==(66,2065968,30506,31823)
assert (len(zero),sum(zero),min(zero),max(zero),zero.count(0))==(84,12078,0,666,40)
rounded=lambda values:(sum(values)*100+len(values)//2)//len(values)
assert rounded(zero)==14379 and rounded(full)==3130255 and rounded(full)-rounded(zero)==3115876
header=(Path(__file__).parents[1]/"Sensors/Inc/sensor_rain.h").read_text(encoding="utf-8")
for name,value in (("RAW_ZERO_X100",14379),("RAW_FULL_X100",3130255),("INTENSITY_MAX_X100",800)):
    assert int(re.search(r"#define SENSOR_RAIN_"+name+r"\s+(\d+)U",header)[1])==value
print("PASS: 66/84 printed OK records, sums2065968/12078, all40 valid zeros, endpoints14379/3130255 span3115876")
