import asyncio
import hmac
import logging
import time

from pydantic import ValidationError

from .protocol import encode_frame, parse_frame

logger = logging.getLogger("tea.tcp")


class TcpReceiver:
    def __init__(self, settings, store, publish):
        self.settings, self.store, self.publish = settings, store, publish
        self.server = None
        self.writers = set()
        self.tasks = set()
        self.accepted = self.rejected = self.duplicates = 0

    async def start(self):
        self.server = await asyncio.start_server(self.handle, self.settings.tcp_host,
                                                self.settings.tcp_port,
                                                limit=self.settings.max_frame_bytes)

    async def close(self):
        if self.server:
            self.server.close()
            await self.server.wait_closed()
        for writer in list(self.writers):
            writer.close()
        for task in list(self.tasks):
            task.cancel()
        if self.tasks:
            await asyncio.gather(*list(self.tasks), return_exceptions=True)

    @staticmethod
    async def reply(writer, value):
        writer.write(encode_frame(value))
        await asyncio.wait_for(writer.drain(), 5)

    async def handle(self, reader, writer):
        task = asyncio.current_task()
        self.tasks.add(task)
        admitted = len(self.writers) < self.settings.max_connections
        try:
            if not admitted:
                await self.reply(writer, {"ok": False, "error": "server_busy"})
                return
            self.writers.add(writer)
            # Per-connection rate cap plus bounded frames/connections for this small deployment.
            window, count, failures = time.monotonic(), 0, 0
            while True:
                try:
                    frame = await asyncio.wait_for(reader.readuntil(b"\n"), self.settings.idle_seconds)
                except asyncio.IncompleteReadError as error:
                    if error.partial:
                        await self.reply(writer, {"ok": False, "error": "incomplete_frame"})
                    break
                except asyncio.LimitOverrunError:
                    self.rejected += 1
                    await self.reply(writer, {"ok": False, "error": "frame_too_large"})
                    break
                except asyncio.TimeoutError:
                    await self.reply(writer, {"ok": False, "error": "idle_timeout"})
                    break
                if len(frame) > self.settings.max_frame_bytes:
                    self.rejected += 1
                    await self.reply(writer, {"ok": False, "error": "frame_too_large"})
                    break
                if not frame.strip():
                    continue
                now = time.monotonic()
                if now - window >= 1:
                    window, count = now, 0
                count += 1
                if count > 20:
                    self.rejected += 1
                    await self.reply(writer, {"ok": False, "error": "rate_limited"})
                    break
                try:
                    reading = parse_frame(frame)
                    if self.settings.ingest_token and not hmac.compare_digest(
                            (reading.token or "").encode(), self.settings.ingest_token.encode()):
                        self.rejected += 1
                        await self.reply(writer, {"ok": False, "error": "unauthorized"})
                        break
                except (ValueError, UnicodeError, ValidationError):
                    self.rejected += 1
                    failures += 1
                    await self.reply(writer, {"ok": False, "error": "invalid_message"})
                    if failures >= 5:
                        break
                    continue
                try:
                    row, duplicate = await asyncio.to_thread(self.store.insert, reading)
                except ValueError:
                    self.rejected += 1
                    await self.reply(writer, {"ok": False, "error": "sequence_conflict", "seq": reading.seq})
                    continue
                except Exception:
                    logger.exception("Database write failed")
                    await self.reply(writer, {"ok": False, "error": "storage_error", "seq": reading.seq})
                    continue
                if duplicate:
                    self.duplicates += 1
                else:
                    self.accepted += 1
                # ACK means the transaction has committed, not merely socket receipt.
                await self.reply(writer, {"ok": True, "device_id": reading.device_id,
                                        "seq": reading.seq, "id": row["id"],
                                        "duplicate": duplicate})
                if not duplicate:
                    self.publish({"type": "reading", "reading": row})
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
