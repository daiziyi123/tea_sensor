"""V2 simulator: labelled demo, independent weather identities, no .env loading."""
import argparse
import json
import math
import os
import socket
import time
import uuid

PERIODS = {"temperature_c": 10000, "air_humidity_rh_pct": 10000, "light_lux": 10000,
           "pressure_kpa": 60000, "pm25_ug_m3": 30000, "pm10_ug_m3": 30000}

def snapshot(sequence, device="demo_h743_001", boot_id=None, mode="fresh"):
    tick = sequence * 3000
    base = {"soil_moisture_pct": round(50 + 12 * math.sin(sequence / 60), 2),
            "soil_ph": 6.0, "rain_intensity_mm_min": 4.0, "wind_speed_mps": 15.0, "wind_direction_deg": (359 + sequence) % 360,
            "temperature_c": 19.4, "air_humidity_rh_pct": 70.8, "light_lux": 1249,
            "pressure_kpa": 103.73, "pm25_ug_m3": 22, "pm10_ug_m3": 23}
    data = {}
    for metric, value in base.items():
        period = PERIODS.get(metric, 3000)
        source_tick = tick // period * period
        item = dict(value=0 if mode == "zero" and metric not in ("soil_ph", "pressure_kpa") else value,
                    status="OK", sample_seq=source_tick // period + 1,
                    sample_t_ms=source_tick, age_ms=tick-source_tick)
        if mode == "failure" or (mode == "mixed" and metric in ("soil_ph", "air_humidity_rh_pct", "rain_intensity_mm_min")):
            # Failed completions have their own identity and real completion time.
            item.update(value=None, status="TIMEOUT" if metric in PERIODS else "WINDOW_INSUFFICIENT")
        if mode == "mixed" and metric == "wind_speed_mps":
            item["value"] = 0
        if mode == "stale" and (metric in PERIODS or metric == "rain_intensity_mm_min"):
            item.update(value=None, status="STALE", age_ms=period*4)
        if metric in ("soil_ph", "wind_speed_mps", "rain_intensity_mm_min") and item["value"] is not None:
            item["range"] = "OK"
        data[metric] = item
    if mode == "legacy":
        data.pop("rain_intensity_mm_min")
    message = dict(schema_version=2, device_id=device, seq=sequence, t_ms=tick, window_ms=3000, data=data)
    if boot_id is not None:
        message["boot_id"] = boot_id
    return message

def main():
    parser = argparse.ArgumentParser(description="SIMULATED V2 data; never sensor measurements")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9000)
    parser.add_argument("--device", default="demo_h743_001")
    parser.add_argument("--count", type=int, default=1)
    parser.add_argument("--interval", type=float, default=3)
    parser.add_argument("--boot-id", help="Optional simulator boot identity; generated if omitted")
    parser.add_argument("--fragment", action="store_true")
    args = parser.parse_args()
    if not args.device.startswith("demo_"):
        parser.error("simulator device must start with demo_")
    if args.count < 1 or args.interval < 0:
        parser.error("count must be positive; interval must be nonnegative")
    token = os.getenv("INGEST_TOKEN", "")
    boot_id = args.boot_id or uuid.uuid4().hex[:16]
    print("SIMULATED V2 data; device=" + args.device)
    with socket.create_connection((args.host, args.port), timeout=10) as connection:
        reader = connection.makefile("rb")
        for sequence in range(1, args.count + 1):
            message = snapshot(sequence, args.device, boot_id)
            if token:
                message["token"] = token
            wire = (json.dumps(message, separators=(",", ":")) + "\r\n").encode()
            if args.fragment:
                for offset in range(0, len(wire), 7):
                    connection.sendall(wire[offset:offset+7])
                    time.sleep(.002)
            else:
                connection.sendall(wire)
            response = reader.readline(8193)
            if len(response) > 8192 or not response.endswith(b"\n"):
                raise RuntimeError("Missing or oversized ACK")
            ack = json.loads(response)
            if (ack.get("ok") is not True or type(ack.get("seq")) is not int
                    or ack["seq"] != sequence or ack.get("device_id") != args.device
                    or type(ack.get("id")) is not int or ack["id"] < 1
                    or type(ack.get("duplicate")) is not bool):
                raise RuntimeError("ACK rejected or identity mismatch")
            print(f"TX seq={sequence} metrics=11; ACK id={ack['id']} duplicate={ack['duplicate']}")
            if sequence < args.count:
                time.sleep(args.interval)

if __name__ == "__main__":
    main()
