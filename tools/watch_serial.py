"""Dev helper for the TEMPORARY Task 4 harness (pio env watch_harness).

Needs pyserial (PlatformIO's own Python has it):
  %USERPROFILE%\\.platformio\\penv\\Scripts\\python.exe tools\\watch_serial.py ...

  listen [--secs N] [--reset]            print serial output for N seconds
  cmd "<line>" ["<line>" ...] [--secs N]  send harness commands, then listen
  upload <file.mp3> <slot> [--force]      write a file into a slot over serial
                                          (refused by the watch if the slot has a file
                                          unless --force)
The port is opened with DTR/RTS released so the watch is NOT reset unless --reset.
"""
import argparse
import os
import sys
import time

import serial


def open_port(port, reset):
    s = serial.Serial()
    s.port = port
    s.baudrate = 115200
    s.timeout = 0.1
    s.dtr = False
    s.rts = False
    s.open()
    if reset:  # EN low pulse via RTS (auto-reset circuit)
        s.rts = True
        time.sleep(0.1)
        s.rts = False
    return s


def pump(s, secs, until=None):
    end = time.time() + secs
    buf = b""
    while time.time() < end:
        data = s.read(4096)
        if data:
            sys.stdout.write(data.decode("utf-8", "replace"))
            sys.stdout.flush()
            buf += data
            if until and until in buf:
                return buf
    return buf


def wait_line(s, tokens, secs):
    end = time.time() + secs
    line = b""
    while time.time() < end:
        c = s.read(1)
        if not c:
            continue
        if c == b"\n":
            t = line.strip()
            for tok in tokens:
                if t.startswith(tok):
                    return t.decode("utf-8", "replace")
            if t:
                print(t.decode("utf-8", "replace"))
            line = b""
        else:
            line += c
    return None


def upload(s, path, slot, force):
    data = open(path, "rb").read()
    s.reset_input_buffer()
    s.write(f"{'upf' if force else 'up'} {slot} {len(data)}\n".encode())
    r = wait_line(s, [b"READY", b"UPLOAD ERR"], 5)
    if r != "READY":
        print("watch refused:", r)
        return 1
    t0 = time.time()
    for off in range(0, len(data), 1024):
        s.write(data[off:off + 1024])
        r = wait_line(s, [b"K", b"UPLOAD ERR"], 10)
        if r != "K":
            print("upload failed at", off, r)
            return 1
    r = wait_line(s, [b"UPLOAD OK", b"UPLOAD ERR"], 10)
    print(r, f"{len(data)} bytes in {time.time() - t0:.1f}s")
    return 0 if r == "UPLOAD OK" else 1


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM8")
    ap.add_argument("--secs", type=float, default=5)
    ap.add_argument("--reset", action="store_true")
    sub = ap.add_subparsers(dest="op", required=True)
    sub.add_parser("listen")
    c = sub.add_parser("cmd")
    c.add_argument("lines", nargs="+")
    c.add_argument("--gap", type=float, default=0.3)
    u = sub.add_parser("upload")
    u.add_argument("file")
    u.add_argument("slot", type=int)
    u.add_argument("--force", action="store_true")
    a = ap.parse_args()

    s = open_port(a.port, a.reset)
    try:
        if a.op == "listen":
            pump(s, a.secs)
        elif a.op == "cmd":
            for ln in a.lines:
                s.write((ln + "\n").encode())
                pump(s, a.gap)
            pump(s, a.secs)
        elif a.op == "upload":
            if not os.path.isfile(a.file):
                print("no such file", a.file)
                return 1
            return upload(s, a.file, a.slot, a.force)
    finally:
        s.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
