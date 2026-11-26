import asyncio
import hmac
import json
import logging
import time
from datetime import datetime

from pydantic import ValidationError

from .json_stream import FrameTooLarge, JsonObjectStream
from .location import LocationError, native_imei, parse_location
from .protocol import ReadingV2, encode_frame, parse_frame, unique_pairs
from .storage import utc_now

logger = logging.getLogger("tea.tcp")


class TcpReceiver:
    def __init__(self, settings, store, publish):
        self.settings, self.store, self.publish = settings, store, publish
        self.server = None
        self.writers = set()
        self.tasks = set()
        self.accepted = self.rejected = self.duplicates = 0
        self.gps_counts = dict.fromkeys(("accepted", "duplicates", "out_of_order", "rejected",
                                        "before_auth", "unknown_imei", "binding_mismatch", "invalid", "disabled"), 0)

    async def start(self):
        self.server = await asyncio.start_server(self.handle, self.settings.tcp_host,
                                                self.settings.tcp_port,
                                                limit=self.settings.max_frame_bytes)

    async def close(self):
        if self.server:
            self.server.close()
        for writer in list(self.writers):
            writer.close()
        for task in list(self.tasks):
            task.cancel()
        if self.tasks:
            await asyncio.gather(*list(self.tasks), return_exceptions=True)
        if self.server:
            await self.server.wait_closed()

    @staticmethod
    async def reply(writer, value):
        writer.write(encode_frame(value))
        await asyncio.wait_for(writer.drain(), 5)

    def reject_gps(self, reason, device_id=None):
        self.gps_counts["rejected"] += 1
        self.gps_counts[reason] += 1
        # Never log native payloads, private bindings, IMEI or authentication keys.
        logger.warning("GPS rejected device=%s reason=%s", device_id or "unassociated", reason)

    async def native_location(self, message, authenticated_device):
        # GPS has no token: neither a prior connection nor an IMEI alone authorizes it.
        if not self.settings.ingest_token or not self.settings.gps_imei_bindings:
            self.reject_gps("disabled")
            return
        if authenticated_device is None:
            self.reject_gps("before_auth")
            return
        try:
            imei = native_imei(message)
        except LocationError:
            self.reject_gps("invalid", authenticated_device)
            return
        device_id = self.settings.gps_imei_bindings.get(imei)
        if device_id is None:
            self.reject_gps("unknown_imei", authenticated_device)
            return
        if device_id != authenticated_device:
            self.reject_gps("binding_mismatch", authenticated_device)
            return
        received = utc_now()
        try:
            try:
                location = parse_location(message, self.settings.gps_coordinate_system,
                                          datetime.fromisoformat(received), self.settings.gps_future_tolerance_seconds)
            except LocationError as error:
                self.reject_gps("invalid", device_id)
                await asyncio.to_thread(self.store.record_location_error, device_id, str(error), received)
            else:
                status = await asyncio.to_thread(self.store.insert_location, device_id, location, received)
                if status == "FIX_CONFLICT":
                    self.reject_gps("invalid", device_id)
                else:
                    self.gps_counts[{"OK": "accepted", "DUPLICATE": "duplicates", "OUT_OF_ORDER": "out_of_order"}[status]] += 1
                logger.info("GPS device=%s status=%s fix_time_utc=%s", device_id, status, location["fix_time_utc"])
            current = await asyncio.to_thread(self.store.current_location, device_id, self.settings.gps_stale_seconds)
            self.publish({"type": "location", "device_id": device_id, "location": current})
        except Exception:
            logger.exception("Location storage failed for device=%s", device_id)
        # Native GPS never receives a V2 ACK, including invalid or duplicate fixes.

    async def process_frame(self, frame, writer, authenticated_device):
        try:
            # Route first; GPS validation rejects non-finite coordinates without
            # generating a sensor ACK. Sensor parsing still uses strict JSON rules.
            value = json.loads(frame.decode("utf-8"), object_pairs_hook=unique_pairs)
            if not isinstance(value, dict):
                raise ValueError("Object required")
            if "schema_version" not in value and any(key in value for key in ("IMEI", "cmd", "gps")):
                await self.native_location(value, authenticated_device)
                return authenticated_device, "valid"
            reading = parse_frame(frame)
        except (ValueError, UnicodeError, ValidationError, RecursionError):
            self.rejected += 1
            await self.reply(writer, {"ok": False, "error": "invalid_message"})
            return authenticated_device, "invalid"
        if self.settings.ingest_token and not hmac.compare_digest(
                (reading.token or "").encode(), self.settings.ingest_token.encode()):
            self.rejected += 1
            await self.reply(writer, {"ok": False, "error": "unauthorized"})
            return authenticated_device, "close"
        if isinstance(reading, ReadingV2) and self.settings.ingest_token:
            authenticated_device = reading.device_id
        try:
            row, duplicate = await asyncio.to_thread(self.store.insert, reading)
        except ValueError:
            self.rejected += 1
            await self.reply(writer, {"ok": False, "error": "sequence_conflict", "seq": reading.seq})
            return authenticated_device, "valid"
        except Exception:
            logger.exception("Database write failed")
            await self.reply(writer, {"ok": False, "error": "storage_error", "seq": reading.seq})
            return authenticated_device, "valid"
        if duplicate:
            self.duplicates += 1
        else:
            self.accepted += 1
        # ACK still means the original sensor transaction has committed.
        await self.reply(writer, {"ok": True, "device_id": reading.device_id,
                                "seq": reading.seq, "id": row["id"], "duplicate": duplicate})
        if not duplicate:
            self.publish({"type": "reading", "reading": row})
        return authenticated_device, "valid"

    async def handle(self, reader, writer):
        task = asyncio.current_task()
        self.tasks.add(task)
        admitted = len(self.writers) < self.settings.max_connections
        try:
            if not admitted:
                await self.reply(writer, {"ok": False, "error": "server_busy"})
                return
            self.writers.add(writer)
            window, count, failures = time.monotonic(), 0, 0
            stream = JsonObjectStream(self.settings.max_frame_bytes)
            authenticated_device = None
            deadline = time.monotonic() + self.settings.idle_seconds
            frame_deadline = None
            while True:
                try:
                    # A slowly dripped partial object cannot extend the frame deadline.
                    chunk = await asyncio.wait_for(reader.read(4096), max(0, min(deadline, frame_deadline or deadline) - time.monotonic()))
                except asyncio.TimeoutError:
                    await self.reply(writer, {"ok": False, "error": "idle_timeout"})
                    break
                if not chunk:
                    if stream.buffer:
                        await self.reply(writer, {"ok": False, "error": "incomplete_frame"})
                    break
                try:
                    if not stream.buffer:
                        frame_deadline = time.monotonic() + self.settings.frame_timeout_seconds
                    for frame in stream.feed(chunk):
                        now = time.monotonic()
                        frame_deadline = None
                        if now - window >= 1:
                            window, count = now, 0
                        count += 1
                        if count > 20:
                            self.rejected += 1
                            await self.reply(writer, {"ok": False, "error": "rate_limited"})
                            return
                        authenticated_device, outcome = await self.process_frame(frame, writer, authenticated_device)
                        deadline = now + (self.settings.authenticated_idle_seconds if authenticated_device else self.settings.idle_seconds)
                        failures += outcome == "invalid"
                        if outcome == "close" or failures >= 5:
                            return
                    if stream.buffer and frame_deadline is None:
                        frame_deadline = time.monotonic() + self.settings.frame_timeout_seconds
                    elif not stream.buffer:
                        frame_deadline = None
                except FrameTooLarge:
                    self.rejected += 1
                    await self.reply(writer, {"ok": False, "error": "frame_too_large"})
                    break
        except (ConnectionError, asyncio.TimeoutError):
            pass
        finally:
            self.writers.discard(writer)
            self.tasks.discard(task)
            writer.close()
            try:
                await writer.wait_closed()
            except ConnectionError:
                pass
