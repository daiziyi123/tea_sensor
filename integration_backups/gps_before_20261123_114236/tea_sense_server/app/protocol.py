import json
from typing import Literal
import math

from pydantic import BaseModel, ConfigDict, Field, ValidationError, field_validator, model_validator

WINDOW_FIELDS = ("window_ms", "sample_count", "valid_count", "used_count")
WINDOW_DIAGNOSTIC_FIELDS = ("sample_error_count", "cleanup_error_count", "last_sample_error")
WINDOW_PUBLIC_FIELDS = WINDOW_FIELDS + WINDOW_DIAGNOSTIC_FIELDS
ADC_ERRORS = Literal["OK", "ADC_CAL_FAILED", "ADC_START_FAILED", "ADC_POLL_FAILED",
                     "ADC_STOP_FAILED", "ADC_OUT_OF_RANGE"]

class Reading(BaseModel):
    model_config = ConfigDict(extra="forbid", strict=True)
    device_id: str = Field(pattern=r"^[A-Za-z0-9_-]{1,64}$")
    seq: int = Field(ge=0, le=4294967295)
    t_ms: int = Field(ge=0, le=4294967295)
    sensor: Literal["soil_moisture"] = "soil_moisture"
    channel: Literal["AD0"] = "AD0"
    adc_raw: int | None = Field(default=None, ge=0, le=65535)
    moisture_pct: float | None = Field(default=None, ge=0, le=100, allow_inf_nan=False)
    status: str = Field(default="OK", pattern=r"^[A-Z][A-Z0-9_]{0,39}$")
    range: Literal["OK", "LOW", "HIGH"] | None = None
    window_ms: int | None = Field(default=None, ge=1, le=3600000)
    sample_count: int | None = Field(default=None, ge=0, le=1000000)
    valid_count: int | None = Field(default=None, ge=0, le=1000000)
    used_count: int | None = Field(default=None, ge=0, le=1000000)
    sample_error_count: int | None = Field(default=None, ge=0, le=1000000)
    cleanup_error_count: int | None = Field(default=None, ge=0, le=1000000)
    last_sample_error: ADC_ERRORS | None = None
    boot_id: str | None = Field(default=None, pattern=r"^[A-Za-z0-9_-]{1,64}$")
    token: str | None = Field(default=None, max_length=256, exclude=True)

    @model_validator(mode="after")
    def check_validity(self):
        if self.status == "OK":
            if self.adc_raw is None or self.moisture_pct is None:
                raise ValueError("OK requires adc_raw and moisture_pct")
        elif self.adc_raw is not None or self.moisture_pct is not None or self.range is not None:
            raise ValueError("Failed acquisition requires null values and no range")
        present = self.model_fields_set.intersection(WINDOW_FIELDS)
        diagnostics = self.model_fields_set.intersection(WINDOW_DIAGNOSTIC_FIELDS)
        if present:
            if len(present) != len(WINDOW_FIELDS) or any(getattr(self, key) is None for key in WINDOW_FIELDS):
                raise ValueError("Window fields must be a complete non-null group")
            if self.valid_count > self.sample_count or self.used_count > self.valid_count:
                raise ValueError("Invalid window counts")
            if self.status == "OK":
                if self.valid_count < 3 or self.used_count != self.valid_count - 2:
                    raise ValueError("Valid window removes exactly two samples")
            elif self.used_count != 0:
                raise ValueError("Invalid window requires used_count=0")
        elif self.status == "WINDOW_INSUFFICIENT":
            raise ValueError("WINDOW_INSUFFICIENT requires window metadata")
        if diagnostics:
            if not present or len(diagnostics) != len(WINDOW_DIAGNOSTIC_FIELDS) or any(getattr(self, key) is None for key in WINDOW_DIAGNOSTIC_FIELDS):
                raise ValueError("Window diagnostics require the complete window and diagnostic groups")
            if self.sample_error_count != self.sample_count - self.valid_count or self.cleanup_error_count > self.sample_error_count:
                raise ValueError("Inconsistent window error counts")
            if (self.last_sample_error == "OK") != (self.sample_error_count == 0):
                raise ValueError("Last sample error must agree with the error count")
        return self


def reject_constants(value):
    raise ValueError("Non-finite JSON number")


def unique_pairs(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("Duplicate JSON key")
        result[key] = value
    return result


def parse_frame(frame: bytes) -> Reading:
    value = json.loads(frame.decode("utf-8"), parse_constant=reject_constants,
                       object_pairs_hook=unique_pairs)
    if not isinstance(value, dict):
        raise ValueError("Object required")
    if "schema_version" not in value:
        return Reading.model_validate(value)
    if type(value["schema_version"]) is not int or value["schema_version"] != 2:
        raise ValueError("Unsupported schema version")
    return ReadingV2.model_validate(value)


def encode_frame(value: dict) -> bytes:
    return (json.dumps(value, ensure_ascii=False, allow_nan=False,
                       separators=(",", ":")) + "\n").encode("utf-8")


EXAMPLE = {
    "device_id": "tea_h743_001", "seq": 1, "t_ms": 1000,
    "channel": "AD0", "adc_raw": 15791, "moisture_pct": 50.0,
    "status": "OK"
}

WINDOW_EXAMPLE = {**EXAMPLE, "t_ms": 3000, "window_ms": 3000,
                  "sample_count": 30, "valid_count": 30, "used_count": 28,
                  "sample_error_count": 0, "cleanup_error_count": 0, "last_sample_error": "OK"}


METRICS = {
 "soil_moisture_pct":dict(name="土壤湿度",unit="%",decimals=2,period_ms=3000,stale_ms=9000,minimum=0,maximum=100,group="main"),
 "soil_ph":dict(name="土壤pH",unit="",decimals=2,period_ms=3000,stale_ms=9000,minimum=3,maximum=9,group="main"),
 "wind_speed_mps":dict(name="风速",unit="m/s",decimals=2,period_ms=3000,stale_ms=9000,minimum=0,maximum=30,group="main"),
 "wind_direction_deg":dict(name="风向角度",unit="°",decimals=2,period_ms=3000,stale_ms=9000,minimum=0,maximum=360,group="main"),
 "rain_intensity_mm_min":dict(name="降雨强度",unit="mm/min",decimals=2,period_ms=3000,stale_ms=9000,minimum=0,maximum=8,group="main"),
 "temperature_c":dict(name="气温",unit="℃",decimals=1,period_ms=10000,stale_ms=30000,minimum=-40,maximum=80,group="weather"),
 "air_humidity_rh_pct":dict(name="空气湿度",unit="%RH",decimals=1,period_ms=10000,stale_ms=30000,minimum=0,maximum=100,group="weather"),
 "light_lux":dict(name="光照",unit="lx",decimals=0,period_ms=10000,stale_ms=30000,minimum=0,maximum=4294967294,group="weather"),
 "pressure_kpa":dict(name="气压",unit="kPa",decimals=2,period_ms=60000,stale_ms=180000,minimum=30,maximum=110,group="weather"),
 "pm25_ug_m3":dict(name="PM2.5",unit="μg/m³",decimals=0,period_ms=30000,stale_ms=90000,minimum=0,maximum=65534,group="weather"),
 "pm10_ug_m3":dict(name="PM10",unit="μg/m³",decimals=0,period_ms=30000,stale_ms=90000,minimum=0,maximum=65534,group="weather"),
}
MetricStatus=Literal["OK","NO_DATA","WINDOW_INSUFFICIENT","DIRECTION_UNDEFINED","WINDOW_CAPACITY","INVALID_VALUE","STALE","TIMEOUT","CRC_ERROR","FRAME_ERROR","MODBUS_EXCEPTION","UART_ERROR","UART_BUSY","UART_TIMEOUT","RX_NOT_READY","IRQ_DISABLED","RX_OVERFLOW"]
class PhysicalResult(BaseModel):
    model_config=ConfigDict(extra="forbid",strict=True)
    value: int | float | None
    status: MetricStatus
    sample_seq: int | None=Field(ge=0,le=4294967295)
    sample_t_ms: int | None=Field(ge=0,le=4294967295)
    age_ms: int | None=Field(ge=0,le=4294967295)
    range: Literal["LOW","OK","HIGH"] | None=None
    @model_validator(mode="after")
    def check_result(self):
        if self.value is not None and (isinstance(self.value,bool) or not math.isfinite(self.value)): raise ValueError("Finite numeric value required")
        if (self.status=="OK") != (self.value is not None): raise ValueError("Only OK carries a value")
        if self.status=="NO_DATA":
            if any(v is not None for v in (self.sample_seq,self.sample_t_ms,self.age_ms,self.range)): raise ValueError("NO_DATA has no source result")
        elif any(v is None for v in (self.sample_seq,self.sample_t_ms,self.age_ms)): raise ValueError("Completed identity and age required")
        if self.status!="OK" and self.range is not None: raise ValueError("Failure has no range")
        return self
def no_data_result():
    return PhysicalResult(value=None,status="NO_DATA",sample_seq=None,sample_t_ms=None,age_ms=None)

class PhysicalData(BaseModel):
    model_config=ConfigDict(extra="forbid",strict=True)
    soil_moisture_pct: PhysicalResult
    wind_speed_mps: PhysicalResult
    wind_direction_deg: PhysicalResult
    soil_ph: PhysicalResult
    # V2-compatible optional addition: old ten-field devices have no source identity.
    rain_intensity_mm_min: PhysicalResult=Field(default_factory=no_data_result)
    temperature_c: PhysicalResult
    air_humidity_rh_pct: PhysicalResult
    light_lux: PhysicalResult
    pressure_kpa: PhysicalResult
    pm25_ug_m3: PhysicalResult
    pm10_ug_m3: PhysicalResult
    @model_validator(mode="after")
    def check_bounds(self):
        for key,spec in METRICS.items():
            item=getattr(self,key)
            if item.value is not None:
                if not spec["minimum"]<=item.value<=spec["maximum"] or (key=="wind_direction_deg" and item.value>=360): raise ValueError("Physical value outside bounds")
                if spec["decimals"]==0 and not float(item.value).is_integer(): raise ValueError("Integer measurement required")
            if spec["group"]=="weather" and item.range is not None: raise ValueError("Weather has no range")
        return self
class ReadingV2(BaseModel):
    model_config=ConfigDict(extra="forbid",strict=True)
    schema_version: Literal[2]
    device_id: str=Field(pattern=r"^[A-Za-z0-9_-]{1,64}$")
    seq: int=Field(ge=0,le=4294967295)
    t_ms: int=Field(ge=0,le=4294967295)
    window_ms: Literal[3000]
    data: PhysicalData
    boot_id: str | None=Field(default=None,pattern=r"^[A-Za-z0-9_-]{1,64}$")
    token: str | None=Field(default=None,max_length=256,exclude=True)
    @field_validator("schema_version","window_ms",mode="before")
    @classmethod
    def integer_literals(cls,value):
        if type(value) is not int: raise ValueError("Integer required")
        return value
V2_EXAMPLE={"schema_version":2,"device_id":"demo_h743_001","seq":1,"t_ms":3000,"window_ms":3000,
 "data":{key:dict(value=None,status="NO_DATA",sample_seq=None,sample_t_ms=None,age_ms=None) for key in METRICS}}
