"""Native DTU location validation. No IMEI or original payload is persisted/public."""
import math
import re
from datetime import datetime, timezone, timedelta


class LocationError(ValueError):
    pass


def native_imei(message):
    value = message.get("IMEI")
    if type(value) not in (str, int) or not re.fullmatch(r"[0-9]{15}", str(value)):
        raise LocationError("INVALID_IMEI")
    return str(value)


def parse_location(message, coordinate_system="UNKNOWN", now=None, future_seconds=300):
    if message.get("cmd") != "upload":
        raise LocationError("INVALID_COMMAND")
    native_imei(message)
    gps = message.get("gps")
    if (not isinstance(gps, list) or len(gps) != 2 or
            any(type(value) not in (int, float) or
                (type(value) is float and not math.isfinite(value)) for value in gps) or
            not -180 <= gps[0] <= 180 or not -90 <= gps[1] <= 90):
        raise LocationError("INVALID_COORDINATES")
    if gps == [0, 0]:
        raise LocationError("NO_FIX")
    text = message.get("time")
    if not isinstance(text, str) or not re.fullmatch(r"[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{1,2}:[0-9]{2}:[0-9]{2}", text):
        raise LocationError("INVALID_TIME")
    try:
        # Observed convention for this DTU/firmware only: its naive time is UTC.
        fix = datetime.strptime(text, "%Y-%m-%d %H:%M:%S").replace(tzinfo=timezone.utc)
    except ValueError:
        raise LocationError("INVALID_TIME") from None
    now = now or datetime.now(timezone.utc)
    if fix > now + timedelta(seconds=future_seconds):
        raise LocationError("FUTURE_TIME")
    return dict(longitude=gps[0], latitude=gps[1], source="GPS",
                coordinate_system=coordinate_system,
                fix_time_utc=fix.isoformat(timespec="milliseconds"))
