#!/usr/bin/env python3
"""
Listens on a TCP socket for audio play commands from the companion board.
Plays WAV data via aplay on /dev/snd/pcmC0D0p (Dreame built-in speaker).

Protocol (per connection):
  4 bytes big-endian uint32  — payload length
  N bytes                    — WAV file data

Usage: python3 audio_server.py [--host 0.0.0.0] [--port 9999]
"""

import argparse
import socket
import struct
import subprocess
import tempfile
import os

def handle(conn: socket.socket) -> None:
    raw = conn.recv(4)
    if len(raw) < 4:
        return
    length = struct.unpack(">I", raw)[0]
    data = b""
    while len(data) < length:
        chunk = conn.recv(min(4096, length - len(data)))
        if not chunk:
            break
        data += chunk
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
        f.write(data)
        tmp = f.name
    try:
        subprocess.run(["aplay", "-D", "hw:0,0", tmp], check=False)
    finally:
        os.unlink(tmp)

def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=9999)
    args = ap.parse_args()

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as srv:
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind((args.host, args.port))
        srv.listen(1)
        print(f"audio_server listening on {args.host}:{args.port}")
        while True:
            conn, addr = srv.accept()
            with conn:
                handle(conn)

if __name__ == "__main__":
    main()
