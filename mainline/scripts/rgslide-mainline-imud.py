#!/usr/bin/env python3
"""Serve RG Slide motion over DSU using the CM4's SIPC sensor stream."""

import argparse
import errno
import math
import os
import select
import signal
import socket
import struct
import time
import zlib
from contextlib import contextmanager, suppress

PIPE = "/dev/spipe_ch0"
SYNC = b"\x7e" * 4
VERSION = 0xCD
DATA = 4
ACCEL, GYRO = 1, 4
RECORD_SIZE = 34
SAMPLE_PERIOD_US = 20_000
CLIENT_TIMEOUT = 5
DSU_VERSION, DSU_INFO, DSU_DATA = 0x100000, 0x100001, 0x100002


def checksum(data):
    """Internet checksum over big-endian words, padded on the right."""
    if len(data) % 2:
        data += b"\0"
    total = sum(word for (word,) in struct.iter_unpack(">H", data))
    while total >> 16:
        total = (total & 0xFFFF) + (total >> 16)
    return (~total) & 0xFFFF


def frame(sensor, command, payload=b"\xff"):
    header = SYNC + struct.pack(">BBH", sensor, command, len(payload))
    return (
        header + struct.pack(">H", checksum(header))
        + payload + struct.pack(">H", checksum(payload))
    )


class SensorHub:
    def __init__(self, fd):
        self.fd = fd
        self.buffer = bytearray()

    def send(self, sensor, command, payload=b"\xff"):
        remaining = memoryview(frame(sensor, command, payload))
        deadline = time.monotonic() + 1
        while remaining:
            _, ready, _ = select.select([], [self.fd], [], max(0, deadline - time.monotonic()))
            if not ready:
                raise TimeoutError("CM4 command timed out")
            try:
                written = os.write(self.fd, remaining)
            except OSError as error:
                if error.errno not in (errno.EAGAIN, errno.EBUSY):
                    raise
                written = 0
            remaining = remaining[written:]
            if remaining and time.monotonic() >= deadline:
                raise TimeoutError("CM4 command timed out")

    def receive(self):
        try:
            chunk = os.read(self.fd, 4096)
        except OSError as error:
            if error.errno in (errno.EAGAIN, errno.ENODATA):
                return
            raise
        if not chunk:
            raise OSError(errno.EPIPE, "CM4 sensor pipe closed")
        self.buffer.extend(chunk)
        while True:
            offset = self.buffer.find(SYNC)
            if offset == -1:
                del self.buffer[:-3]
                return
            del self.buffer[:offset]
            if len(self.buffer) < 10:
                return
            _, command, size, header_sum = struct.unpack_from(">BBHH", self.buffer, 4)
            if size > 0x7E7 or checksum(self.buffer[:8]) != header_sum:
                del self.buffer[0]
                continue
            end = 10 + size + (2 if size else 0)
            if len(self.buffer) < end:
                return
            payload = bytes(self.buffer[10:10 + size])
            if size and checksum(payload) != struct.unpack_from(">H", self.buffer, end - 2)[0]:
                del self.buffer[0]
                continue
            del self.buffer[:end]
            yield command, payload

    def version(self):
        self.send(0xFF, VERSION)
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            ready, _, _ = select.select([self.fd], [], [], max(0, deadline - time.monotonic()))
            if ready:
                for command, payload in self.receive():
                    if command == VERSION and len(payload) == 4:
                        return struct.unpack("<I", payload)[0]
        raise TimeoutError("CM4 firmware version did not respond")

    def samples(self):
        for command, payload in self.receive():
            if command != DATA or len(payload) % RECORD_SIZE:
                continue
            for offset in range(0, len(payload), RECORD_SIZE):
                handle = payload[offset]
                if handle in (ACCEL, GYRO):
                    axes = struct.unpack_from("<3f", payload, offset + 2)
                    timestamp, = struct.unpack_from("<Q", payload, offset + 26)
                    if all(map(math.isfinite, axes)):
                        yield handle, axes, timestamp


@contextmanager
def streaming():
    fd = os.open(PIPE, os.O_RDWR | os.O_NONBLOCK)
    hub = SensorHub(fd)
    enabled = []
    try:
        print(f"CM4 firmware version {hub.version()}", flush=True)
        for handle in (ACCEL, GYRO):
            hub.send(handle, 2, struct.pack("<Iq", SAMPLE_PERIOD_US, 0))
            enabled.append(handle)
            hub.send(handle, 0)
        yield hub
    finally:
        for handle in enabled:
            with suppress(OSError):
                hub.send(handle, 1)
        os.close(fd)


def dsu_packet(message, payload):
    body = struct.pack("<I", message) + payload
    packet = struct.pack("<4sHHII", b"DSUS", 1001, len(body), 0, 0x5247534C) + body
    return packet[:8] + struct.pack("<I", zlib.crc32(packet)) + packet[12:]


def slot_metadata(slot):
    if slot == 0:
        return struct.pack("<BBBB6sB", 0, 2, 2, 1, b"\x02\x7b\x5a\x0c\x18\x5b", 5)
    return bytes([slot]) + b"\0" * 10


def serve(host, port):
    clients = {}
    stream = hub = None
    motion = {}
    sequence = 0
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
        server.bind((host, port))
        try:
            while True:
                now = time.monotonic()
                clients = {address: seen for address, seen in clients.items()
                           if now - seen < CLIENT_TIMEOUT}
                if hub and not clients:
                    stream.__exit__(None, None, None)
                    stream = hub = None
                    motion.clear()
                if clients and not hub:
                    stream = streaming()
                    hub = stream.__enter__()
                readers = [server, hub.fd] if hub else [server]
                ready, _, _ = select.select(readers, [], [], 1)
                if server in ready:
                    request, address = server.recvfrom(256)
                    if len(request) >= 20 and request[:4] == b"DSUC":
                        message, = struct.unpack_from("<I", request, 16)
                        if message == DSU_VERSION:
                            server.sendto(dsu_packet(message, struct.pack("<H", 1001)), address)
                        elif message == DSU_INFO and len(request) >= 24:
                            count, = struct.unpack_from("<I", request, 20)
                            for slot in request[24:24 + min(count, 4)]:
                                server.sendto(dsu_packet(message, slot_metadata(slot) + b"\0"), address)
                        elif message == DSU_DATA:
                            clients[address] = time.monotonic()
                if hub and hub.fd in ready:
                    updated = False
                    for handle, axes, _ in hub.samples():
                        motion[handle] = axes
                        updated = True
                    if updated and ACCEL in motion and GYRO in motion:
                        sequence = (sequence + 1) & 0xFFFFFFFF
                        accel = [value / 9.80665 for value in motion[ACCEL]]
                        gyro = [math.degrees(value) for value in motion[GYRO]]
                        timestamp_us = time.monotonic_ns() // 1000
                        payload = (slot_metadata(0) + struct.pack("<BI", 1, sequence)
                                   + b"\0" * 32
                                   + struct.pack("<Q6f", timestamp_us, *accel, *gyro))
                        for address in clients:
                            with suppress(OSError):
                                server.sendto(dsu_packet(DSU_DATA, payload), address)
        finally:
            if hub:
                stream.__exit__(None, None, None)


def probe():
    counts = {ACCEL: 0, GYRO: 0}
    with streaming() as hub:
        deadline = time.monotonic() + 3
        while min(counts.values()) < 10 and time.monotonic() < deadline:
            ready, _, _ = select.select([hub.fd], [], [], max(0, deadline - time.monotonic()))
            if ready:
                for handle, axes, timestamp in hub.samples():
                    counts[handle] += 1
                    if counts[handle] == 1:
                        print(f"handle={handle} axes={axes} cm4_timestamp={timestamp}", flush=True)
    print(f"Samples: accelerometer={counts[ACCEL]} gyroscope={counts[GYRO]}")
    return 0 if min(counts.values()) >= 10 else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", action="store_true", help="read ten samples from each sensor")
    args = parser.parse_args()

    def stop(signum, frame):
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, stop)
    try:
        if args.probe:
            return probe()
        serve(os.environ.get("RGSLIDE_IMU_HOST", "127.0.0.1"),
              int(os.environ.get("RGSLIDE_IMU_PORT", "26760")))
    except KeyboardInterrupt:
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
