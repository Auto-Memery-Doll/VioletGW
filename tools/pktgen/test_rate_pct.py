#!/usr/bin/env python3
"""Unit tests for rate_pct.py."""
from __future__ import annotations

from rate_pct import format_rate_tag, max_wire_pps, mpps_to_rate_pct, wire_bits


def assert_close(got: float, want: float, tol: float = 1e-3) -> None:
    if abs(got - want) > tol:
        raise AssertionError(f"got={got} want={want} tol={tol}")


def main() -> None:
    # 46B frame → wire 70B → 560 bits; 10G → ~17.857 Mpps
    assert wire_bits(46) == 560
    assert_close(max_wire_pps(46, 10.0) / 1e6, 17.857142, tol=1e-4)

    # 5% of line rate ≈ 0.893 Mpps — still above typical vmxnet3 ~0.68 Mpps
    assert_close(mpps_to_rate_pct(0.892857, 46, 10.0), 5.0, tol=1e-3)

    # 5% of measured baseline 0.68 Mpps → ~0.034 Mpps → ~0.19% line rate
    assert_close(mpps_to_rate_pct(0.034, 46, 10.0), 0.1904, tol=1e-3)

    # Full blast target at baseline still a few % of 10G wire, not 100
    assert_close(mpps_to_rate_pct(0.68, 46, 10.0), 3.808, tol=1e-2)

    assert format_rate_tag(100.0) == "100"
    assert format_rate_tag(0.1904) == "0.1904"
    print("OK rate_pct")


if __name__ == "__main__":
    main()
