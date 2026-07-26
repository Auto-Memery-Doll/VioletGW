#!/usr/bin/env python3
"""Minimal UDP echo server (upstream backend for nginx stream bench)."""
from __future__ import annotations

import argparse
import socket
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description="UDP echo server")
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=19053)
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
    except OSError:
        pass
    sock.bind((args.bind, args.port))
    print(f"udp_echo listening on {args.bind}:{args.port}", file=sys.stderr, flush=True)

    while True:
        data, addr = sock.recvfrom(65535)
        sock.sendto(data, addr)


if __name__ == "__main__":
    raise SystemExit(main())
