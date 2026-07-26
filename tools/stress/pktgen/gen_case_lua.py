#!/usr/bin/env python3
"""Generate pktgen Lua script for one client-side bench case (C01–C08)."""
from __future__ import annotations

import argparse
from pathlib import Path

HEADER = """\
package.path = package.path .. ";?.lua;test/?.lua;app/?.lua;../?.lua;scripts/?.lua"
require "Pktgen"

pktgen.screen("off")

local port = "0"
local measure_sec = {measure_sec}
local warmup_sec = {warmup_sec}
local pkt_size = {frame_size}
local rate_pct = {rate_pct}

pktgen.set(port, "size", pkt_size)
pktgen.set(port, "rate", rate_pct)
pktgen.set(port, "count", 0)
pktgen.set_proto(port, "udp")
pktgen.page("range")

pktgen.range.dst_ip(port, "start", "{dst_ip}")
pktgen.range.dst_ip(port, "inc", "0.0.0.0")
pktgen.range.dst_ip(port, "min", "{dst_ip}")
pktgen.range.dst_ip(port, "max", "{dst_ip}")

pktgen.range.src_ip(port, "start", "{src_ip}")
pktgen.range.src_ip(port, "inc", "0.0.0.0")
pktgen.range.src_ip(port, "min", "{src_ip}")
pktgen.range.src_ip(port, "max", "{src_ip}")

pktgen.range.dst_port(port, "start", {dst_port})
pktgen.range.dst_port(port, "inc", 0)
pktgen.range.dst_port(port, "min", {dst_port})
pktgen.range.dst_port(port, "max", {dst_port})

{src_port_range}

pktgen.set_range(port, "on")

-- Warmup (stats baseline)
pktgen.start(port)
pktgen.delay(warmup_sec * 1000)
pktgen.stop(port)
pktgen.delay(200)

local base = pktgen.portStats(port, "port")[0]
local base_tx = base.opackets
local base_rx = base.ipackets

pktgen.start(port)
pktgen.delay(measure_sec * 1000)
pktgen.stop(port)
pktgen.delay(200)

local fin = pktgen.portStats(port, "port")[0]
local tx = fin.opackets - base_tx
local rx = fin.ipackets - base_rx
local lost = tx - rx
if lost < 0 then lost = 0 end
local loss_pct = 0.0
if tx > 0 then loss_pct = (lost * 100.0) / tx end
local sent_pps = tx / measure_sec
local recv_pps = rx / measure_sec
local fb = pkt_size
local offered_bps = sent_pps * fb * 8
local received_bps = recv_pps * fb * 8

print(string.format(
  "PKTGEN_SUMMARY sut={sut} case={case_id} mode={mode} payload={payload} " ..
  "seconds=%d warmup=%d flows={flows} " ..
  "client_sent=%d client_received=%d lost=%d loss_rate_pct=%.4f " ..
  "sent_pps=%.0f received_pps=%.0f offered_bps=%.0f received_bps=%.0f frame_bytes=%d",
  measure_sec, warmup_sec,
  tx, rx, lost, loss_pct, sent_pps, recv_pps, offered_bps, received_bps, fb))
"""

SRC_PORT_HOT = """\
pktgen.range.src_port(port, "start", {sport})
pktgen.range.src_port(port, "inc", 0)
pktgen.range.src_port(port, "min", {sport})
pktgen.range.src_port(port, "max", {sport})"""

SRC_PORT_MULTI = """\
pktgen.range.src_port(port, "start", {sport_min})
pktgen.range.src_port(port, "inc", 1)
pktgen.range.src_port(port, "min", {sport_min})
pktgen.range.src_port(port, "max", {sport_max})"""

SRC_PORT_NEWFLOW = """\
pktgen.range.src_port(port, "start", {sport_min})
pktgen.range.src_port(port, "inc", 1)
pktgen.range.src_port(port, "min", {sport_min})
pktgen.range.src_port(port, "max", {sport_max})"""


def frame_size(payload: int) -> int:
    return 14 + 20 + 8 + payload  # eth + ipv4 + udp + payload


def src_port_block(mode: str, flows: int) -> str:
    base = 4000
    newflow_base = 50000
    if mode == "hot":
        return SRC_PORT_HOT.format(sport=base)
    if mode in ("multi", "bidir"):
        return SRC_PORT_MULTI.format(sport_min=base, sport_max=base + flows - 1)
    if mode == "newflow":
        span = min(flows, 65530 - newflow_base + 1)
        return SRC_PORT_NEWFLOW.format(
            sport_min=newflow_base, sport_max=newflow_base + span - 1
        )
    raise ValueError(f"unknown mode {mode}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sut", required=True)
    parser.add_argument("--case", required=True)
    parser.add_argument("--mode", required=True)
    parser.add_argument("--payload", type=int, required=True)
    parser.add_argument("--seconds", type=int, default=30)
    parser.add_argument("--warmup", type=int, default=5)
    parser.add_argument("--flows", type=int, default=1000)
    parser.add_argument("--rate", type=int, default=100, help="pktgen rate percent")
    parser.add_argument("-o", "--output", type=Path, required=True)
    args = parser.parse_args()

    body = HEADER.format(
        measure_sec=args.seconds,
        warmup_sec=args.warmup,
        frame_size=frame_size(args.payload),
        rate_pct=args.rate,
        dst_ip="192.168.1.100",
        src_ip="10.0.0.1",
        dst_port=53,
        src_port_range=src_port_block(args.mode, args.flows),
        sut=args.sut,
        case_id=args.case,
        mode=args.mode,
        payload=args.payload,
        flows=args.flows,
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(body)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
