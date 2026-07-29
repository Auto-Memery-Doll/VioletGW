#!/usr/bin/env python3
"""Convert target Mpps → pktgen line-rate % (matches pktgen wire-size math)."""
from __future__ import annotations

import argparse

# From pktgen app/pktgen.h: IFG+SFD+preamble+FCS
PKT_OVERHEAD_SIZE = 12 + 1 + 7 + 4  # 24


def wire_bits(frame_bytes: int) -> int:
    return (frame_bytes + PKT_OVERHEAD_SIZE) * 8


def max_wire_pps(frame_bytes: int, link_gbps: float) -> float:
    return (link_gbps * 1e9) / wire_bits(frame_bytes)


def mpps_to_rate_pct(
    target_mpps: float,
    frame_bytes: int,
    link_gbps: float = 10.0,
) -> float:
    """pktgen rate % for a target Mpps; clamped to [0.01, 100]."""
    if target_mpps <= 0:
        return 0.01
    max_pps = max_wire_pps(frame_bytes, link_gbps)
    rate = (target_mpps * 1e6 / max_pps) * 100.0
    return max(0.01, min(100.0, rate))


def format_rate_tag(rate_pct: float) -> str:
    """Filename-safe tag for a rate percentage."""
    if abs(rate_pct - round(rate_pct)) < 1e-9:
        return str(int(round(rate_pct)))
    return f"{rate_pct:.4g}"


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--target-mpps", type=float, required=True)
    p.add_argument("--frame-bytes", type=int, required=True)
    p.add_argument("--link-gbps", type=float, default=10.0)
    p.add_argument("--tag", action="store_true", help="print filename tag only")
    args = p.parse_args()
    rate = mpps_to_rate_pct(args.target_mpps, args.frame_bytes, args.link_gbps)
    if args.tag:
        print(format_rate_tag(rate))
    else:
        # Enough precision for pktgen strtod; strip trailing zeros via %g
        print(f"{rate:.6g}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
