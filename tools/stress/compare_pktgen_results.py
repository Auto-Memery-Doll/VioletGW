#!/usr/bin/env python3
"""Merge pktgen client results for fg and nginx (same generator)."""
from __future__ import annotations

import csv
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DOCS = ROOT / "docs"
OUT = DOCS / "client-comparison-pktgen.json"

CASES = [
    ("C01", "hot", 4),
    ("C02", "multi", 4),
    ("C03", "newflow", 4),
    ("C04", "bidir", 4),
    ("C05", "hot", 512),
    ("C06", "hot", 4096),
    ("C07", "newflow", 512),
    ("C08", "newflow", 4096),
]


def load_file(path: Path) -> dict[str, dict]:
    out: dict[str, dict] = {}
    if not path.exists():
        return out
    with path.open(newline="") as f:
        for row in csv.DictReader(f):
            out[row["case_id"]] = {
                "sut": row["sut"],
                "client_sent": int(row["client_sent"]),
                "client_received": int(row["client_received"]),
                "loss_rate_pct": float(row["loss_rate_pct"]),
                "received_pps": float(row["received_pps"]),
                "sent_pps": float(row["sent_pps"]),
                "received_bps": float(row["received_bps"]),
                "offered_bps": float(row["offered_bps"]),
            }
    return out


def main() -> int:
    fg = load_file(DOCS / "pktgen-client-results_fg.csv")
    nginx = load_file(DOCS / "pktgen-client-results_nginx.csv")
    merged = []
    for case_id, mode, payload in CASES:
        f = fg.get(case_id)
        n = nginx.get(case_id)
        entry = {
            "case_id": case_id,
            "mode": mode,
            "payload_bytes": payload,
            "fg": f,
            "nginx": n,
        }
        if f and n and f["received_pps"] > 0:
            entry["nginx_vs_fg_received_pps_ratio"] = round(
                n["received_pps"] / f["received_pps"], 3
            )
        merged.append(entry)
    OUT.write_text(json.dumps(merged, indent=2))
    print(f"Wrote {OUT} (fg={len(fg)} nginx={len(nginx)} cases)")
    return 0 if fg and nginx else 1


if __name__ == "__main__":
    sys.exit(main())
