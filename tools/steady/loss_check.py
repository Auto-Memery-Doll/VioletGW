#!/usr/bin/env python3
"""Steady UDP loss check against VioletGW VIP.

Binds to the SSH/management NIC IP (ens33). Add VIP static ARP first — see README.

  sudo python3 tools/steady/loss_check.py --echo --publish
"""

from __future__ import annotations

import argparse
import os
import select
import socket
import struct
import subprocess
import sys
import threading
import time

def bind_udp(host: str, port: int = 0) -> socket.socket:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((host, port))
    sock.setblocking(False)
    return sock


def echo_loop(sock: socket.socket, stop: threading.Event) -> None:
    while not stop.is_set():
        ready, _, _ = select.select([sock], [], [], 0.2)
        if not ready:
            continue
        try:
            data, addr = sock.recvfrom(65535)
            sock.sendto(data, addr)
        except (BlockingIOError, OSError):
            pass


def publish_upstream(vgwcp: str, ip: str, port: int) -> None:
    cmd = [vgwcp, "-upstream", f"{ip}:{port}"]
    print(f"[steady] publish: {' '.join(cmd)}", file=sys.stderr)
    subprocess.check_call(cmd)


def run(args: argparse.Namespace) -> int:
    local_ip = args.local_ip or '192.168.56.135'
    vip = args.vip
    vip_port = args.vip_port
    echo_port = args.echo_port
    payload_pad = max(0, args.payload - 8)

    echo_sock = None
    echo_stop = threading.Event()
    echo_thread = None

    if args.echo:
        echo_sock = bind_udp(local_ip, echo_port)
        echo_port = echo_sock.getsockname()[1]
        echo_thread = threading.Thread(
            target=echo_loop, args=(echo_sock, echo_stop), daemon=True
        )
        echo_thread.start()
        print(f"[steady] echo {local_ip}:{echo_port}", file=sys.stderr)

    client = bind_udp(local_ip, args.src_port)
    src_port = client.getsockname()[1]
    print(
        f"[steady] client {local_ip}:{src_port} -> {vip}:{vip_port} "
        f"count={args.count} pps={args.pps}",
        file=sys.stderr,
    )

    if args.publish:
        publish_upstream(args.vgwcp, local_ip, echo_port if args.echo else vip_port)
        time.sleep(0.3)

    interval = 1.0 / args.pps if args.pps > 0 else 0.0
    sent = 0
    recv_ok = 0
    pending: dict[int, float] = {}
    t0 = time.monotonic()

    while sent < args.count or pending:
        now = time.monotonic()
        for seq, ts in list(pending.items()):
            if now - ts > args.timeout:
                del pending[seq]

        if sent < args.count and (
            interval <= 0 or sent == 0 or (now - t0) >= sent * interval
        ):
            body = struct.pack("!I", sent) + (b"x" * payload_pad)
            try:
                client.sendto(body, (vip, vip_port))
            except OSError as exc:
                print(f"[steady] send failed: {exc}", file=sys.stderr)
                break
            pending[sent] = now
            sent += 1

        ready, _, _ = select.select([client], [], [], 0.01)
        if ready:
            try:
                data, _ = client.recvfrom(65535)
            except BlockingIOError:
                continue
            if len(data) >= 4:
                (seq,) = struct.unpack("!I", data[:4])
                if seq in pending:
                    del pending[seq]
                    recv_ok += 1

        if sent >= args.count and not pending:
            break

    deadline = time.monotonic() + args.timeout
    while pending and time.monotonic() < deadline:
        ready, _, _ = select.select([client], [], [], 0.05)
        if not ready:
            continue
        try:
            data, _ = client.recvfrom(65535)
        except BlockingIOError:
            continue
        if len(data) >= 4:
            (seq,) = struct.unpack("!I", data[:4])
            if seq in pending:
                del pending[seq]
                recv_ok += 1

    lost = sent - recv_ok
    loss_pct = (100.0 * lost / sent) if sent else 0.0
    elapsed = time.monotonic() - t0
    print(
        f"sent={sent} recv={recv_ok} lost={lost} "
        f"loss_rate={loss_pct:.3f}% elapsed_s={elapsed:.3f}"
    )

    if echo_sock is not None:
        echo_stop.set()
        if echo_thread is not None:
            echo_thread.join(timeout=1.0)
        echo_sock.close()
    client.close()

    if args.max_loss_pct is not None and loss_pct > args.max_loss_pct:
        return 1
    return 0


def main() -> int:
    env = os.environ
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument(
        "--local-ip",
        default=env.get("MGMT_IP") or None,
        help="SSH NIC IPv4 (default: first addr on MGMT_IFACE / ens33)",
    )
    p.add_argument("--src-port", type=int, default=0, help="client UDP port (0=ephemeral)")
    p.add_argument("--vip", default=env.get("VIP_IP", "192.168.1.100"))
    p.add_argument("--vip-port", type=int, default=int(env.get("VIP_PORT", "53")))
    p.add_argument("--count", type=int, default=1000)
    p.add_argument("--pps", type=float, default=100.0)
    p.add_argument("--timeout", type=float, default=1.0)
    p.add_argument("--payload", type=int, default=64, help="UDP payload bytes (>=8)")
    p.add_argument("--echo", action="store_true", help="UDP echo on local IP (vgw upstream)")
    p.add_argument(
        "--echo-port",
        type=int,
        default=int(env.get("UPSTREAM_PORT", "53")),
    )
    p.add_argument("--publish", action="store_true", help="vgwcp -upstream before sending")
    p.add_argument(
        "--vgwcp",
        default=env.get(
            "VGWCP_BIN",
            os.path.join(
                os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                "vgwcp",
                "vgwcp",
            ),
        ),
    )
    p.add_argument("--max-loss-pct", type=float, default=None)
    args = p.parse_args()
    if args.payload < 8:
        p.error("--payload must be >= 8")
    if args.count < 1:
        p.error("--count must be >= 1")
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
