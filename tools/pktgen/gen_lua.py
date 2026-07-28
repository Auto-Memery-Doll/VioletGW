#!/usr/bin/env python3
"""Generate one pktgen Lua case (M01–M06: hot | flows)."""
from __future__ import annotations

import argparse
from pathlib import Path

HEADER = """\
-- pktgen C bindings are already registered as global `pktgen`.
-- Avoid loading Pktgen.lua helpers (needs pktgen source cwd).

pktgen.screen("off")

local port = "0"
local measure_sec = {measure_sec}
local warmup_sec = {warmup_sec}
local pkt_size = {frame_size}
local rate_pct = {rate_pct}

local function port_pkts(p)
  -- pktgen 26.x: portStats(portlist) returns table keyed by port id;
  -- each entry has .curr.opackets / .curr.ipackets
  local s = pktgen.portStats(p)
  local row = s[0] or s[tonumber(p)]
  local curr = row.curr
  return curr.opackets, curr.ipackets
end

pktgen.set(port, "count", 0)
pktgen.set(port, "rate", rate_pct)
pktgen.set_type(port, "ipv4")
pktgen.set_proto(port, "udp")
pktgen.page("range")

pktgen.range.ip_proto(port, "udp")
pktgen.range.pkt_size(port, "start", pkt_size)
pktgen.range.pkt_size(port, "min", pkt_size)
pktgen.range.pkt_size(port, "max", pkt_size)
pktgen.range.pkt_size(port, "inc", 0)

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

pktgen.range.src_mac(port, "start", "{src_mac}")
pktgen.range.src_mac(port, "inc", "00:00:00:00:00:00")
pktgen.range.src_mac(port, "min", "{src_mac}")
pktgen.range.src_mac(port, "max", "{src_mac}")

pktgen.range.dst_mac(port, "start", "{dst_mac}")
pktgen.range.dst_mac(port, "inc", "00:00:00:00:00:00")
pktgen.range.dst_mac(port, "min", "{dst_mac}")
pktgen.range.dst_mac(port, "max", "{dst_mac}")

{src_port_range}

pktgen.set_range(port, "on")

pktgen.start(port)
pktgen.delay(warmup_sec * 1000)
pktgen.stop(port)
pktgen.delay(200)

local base_tx, base_rx = port_pkts(port)

pktgen.start(port)
pktgen.delay(measure_sec * 1000)
pktgen.stop(port)
pktgen.delay(200)

local fin_tx, fin_rx = port_pkts(port)
local tx = fin_tx - base_tx
local rx = fin_rx - base_rx
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
  "PKTGEN_SUMMARY case={case_id} pattern={pattern} payload={payload} " ..
  "seconds=%d warmup=%d flows={flows} " ..
  "client_sent=%d client_received=%d lost=%d loss_rate_pct=%.4f " ..
  "sent_pps=%.0f received_pps=%.0f offered_bps=%.0f received_bps=%.0f frame_bytes=%d",
  measure_sec, warmup_sec,
  tx, rx, lost, loss_pct, sent_pps, recv_pps, offered_bps, received_bps, fb))

pktgen.quit()
"""

SRC_PORT_HOT = """\
pktgen.range.src_port(port, "start", {sport})
pktgen.range.src_port(port, "inc", 0)
pktgen.range.src_port(port, "min", {sport})
pktgen.range.src_port(port, "max", {sport})"""

SRC_PORT_FLOWS = """\
pktgen.range.src_port(port, "start", {sport_min})
pktgen.range.src_port(port, "inc", 1)
pktgen.range.src_port(port, "min", {sport_min})
pktgen.range.src_port(port, "max", {sport_max})"""


def frame_size(payload: int) -> int:
    return 14 + 20 + 8 + payload


def src_port_block(pattern: str, flows: int) -> str:
    base = 4000
    if pattern == "hot":
        return SRC_PORT_HOT.format(sport=base)
    if pattern == "flows":
        if flows < 1:
            raise ValueError("flows must be >= 1")
        return SRC_PORT_FLOWS.format(
            sport_min=base, sport_max=base + flows - 1
        )
    raise ValueError(f"unknown pattern {pattern}")


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--case", required=True)
    p.add_argument("--pattern", required=True, choices=("hot", "flows"))
    p.add_argument("--payload", type=int, required=True)
    p.add_argument("--seconds", type=int, default=30)
    p.add_argument("--warmup", type=int, default=5)
    p.add_argument("--flows", type=int, default=1)
    p.add_argument("--rate", type=float, default=100.0)
    p.add_argument("--src-ip", default="10.0.0.1")
    p.add_argument("--dst-ip", default="192.168.1.100")
    p.add_argument("--dst-port", type=int, default=53)
    p.add_argument("--src-mac", default="02:00:00:00:00:03")
    p.add_argument("--dst-mac", default="02:00:00:00:00:01")
    p.add_argument("-o", "--output", type=Path, required=True)
    args = p.parse_args()

    body = HEADER.format(
        measure_sec=args.seconds,
        warmup_sec=args.warmup,
        frame_size=frame_size(args.payload),
        rate_pct=f"{args.rate:.6g}",
        dst_ip=args.dst_ip,
        src_ip=args.src_ip,
        dst_port=args.dst_port,
        src_mac=args.src_mac,
        dst_mac=args.dst_mac,
        src_port_range=src_port_block(args.pattern, args.flows),
        case_id=args.case,
        pattern=args.pattern,
        payload=args.payload,
        flows=args.flows,
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(body)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
