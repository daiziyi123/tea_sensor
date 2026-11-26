"""Bounded JSON-object framing over bytes; validation remains in the protocol layer."""


class FrameTooLarge(ValueError):
    pass


class JsonObjectStream:
    def __init__(self, maximum):
        self.maximum = maximum
        self.buffer = bytearray()
        self.stack = []
        self.quoted = self.escaped = self.invalid = False

    def feed(self, chunk):
        # Scan once across recv boundaries. ASCII quotes/brackets cannot occur
        # inside a multibyte UTF-8 code point; decode only after a complete frame.
        for byte in chunk:
            if not self.buffer:
                if byte in b" \t\r\n":
                    continue
                self.invalid = byte != ord("{")
            self.buffer.append(byte)
            if len(self.buffer) > self.maximum:
                raise FrameTooLarge()
            if self.invalid:
                complete = byte == ord("\n")
            elif self.quoted:
                if self.escaped:
                    self.escaped = False
                elif byte == ord("\\"):
                    self.escaped = True
                elif byte == ord('"'):
                    self.quoted = False
                complete = False
            else:
                complete = False
                if byte == ord('"'):
                    self.quoted = True
                elif byte in b"{[":
                    self.stack.append(byte)
                elif byte in b"}]":
                    if not self.stack or (self.stack.pop(), byte) not in (
                            (ord("{"), ord("}")), (ord("["), ord("]"))):
                        self.invalid = True
                    else:
                        complete = not self.stack
            if complete:
                frame = bytes(self.buffer)
                self.buffer.clear()
                self.stack.clear()
                self.quoted = self.escaped = self.invalid = False
                yield frame
