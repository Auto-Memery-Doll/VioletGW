#!/usr/bin/env python3
"""Generate markdown report + Cursor canvas from pktgen bench CSVs."""
from __future__ import annotations

import argparse
import csv
import json
import sys
from datetime import datetime, timezone
from pathlib import Path


CASE_ORDER = [
    ("C01", "hot", 4),
    ("C02", "multi", 4),
    ("C03", "newflow", 4),
    ("C04", "bidir", 4),
    ("C05", "hot", 512),
    ("C06", "hot", 4096),
    ("C07", "newflow", 512),
    ("C08", "newflow", 4096),
]

CANVAS_PATH = Path.home() / ".cursor/projects/home-violet-flow-gateway/canvases/pktgen-bench-results.canvas.tsx"


def load_csv(path: Path) -> dict[str, dict]:
    if not path.exists():
        return {}
    rows: dict[str, dict] = {}
    with path.open(newline="") as f:
        for row in csv.DictReader(f):
            rows[row["case_id"]] = row
    return rows


def fnum(row: dict | None, key: str) -> float:
    if not row:
        return 0.0
    return float(row.get(key, 0) or 0)


def write_markdown(
    out: Path,
    fg: dict[str, dict],
    nginx: dict[str, dict],
    measure_sec: int,
    warmup_sec: int,
) -> None:
    ts = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
    lines = [
        "# Pktgen bench report",
        "",
        f"Generated: {ts}",
        "",
        "## Environment",
        "",
        "| Parameter | Value |",
        "|-----------|-------|",
        f"| Measure window | {measure_sec} s |",
        f"| Warmup | {warmup_sec} s |",
        "| Client | DPDK pktgen 23.10.2 + net_tap |",
        "| Topology | veth fg-pktgen0 ↔ fg-sut0 |",
        "",
        "## Results (client view)",
        "",
        "| Case | Mode | Payload | fg recv pps | nginx recv pps | fg loss % | nginx loss % | nginx/fg pps |",
        "|------|------|---------|-------------|----------------|-----------|--------------|--------------|",
    ]
    for case_id, mode, payload in CASE_ORDER:
        f = fg.get(case_id)
        n = nginx.get(case_id)
        fg_pps = fnum(f, "received_pps")
        ng_pps = fnum(n, "received_pps")
        ratio = f"{ng_pps / fg_pps:.2f}" if fg_pps > 0 and ng_pps > 0 else "—"
        lines.append(
            f"| {case_id} | {mode} | {payload} B | "
            f"{fg_pps:.0f} | {ng_pps:.0f} | "
            f"{fnum(f, 'loss_rate_pct'):.2f} | {fnum(n, 'loss_rate_pct'):.2f} | {ratio} |"
        )
    lines.extend(["", "## Raw CSV", "", "- `docs/pktgen-client-results_fg.csv`", "- `docs/pktgen-client-results_nginx.csv`", ""])
    out.write_text("\n".join(lines))


def write_canvas(
    path: Path,
    fg: dict[str, dict],
    nginx: dict[str, dict],
    measure_sec: int,
    warmup_sec: int,
) -> None:
    categories = [c[0] for c in CASE_ORDER]
    fg_pps = [round(fnum(fg.get(c), "received_pps")) for c in categories]
    ng_pps = [round(fnum(nginx.get(c), "received_pps")) for c in categories]
    fg_loss = [round(fnum(fg.get(c), "loss_rate_pct"), 2) for c in categories]
    ng_loss = [round(fnum(nginx.get(c), "loss_rate_pct"), 2) for c in categories]

    meta = {
        "measure_sec": measure_sec,
        "warmup_sec": warmup_sec,
        "generated": datetime.now(timezone.utc).isoformat(),
    }
    data_json = json.dumps(
        {"categories": categories, "fg_pps": fg_pps, "nginx_pps": ng_pps, "fg_loss": fg_loss, "nginx_loss": ng_loss, "meta": meta},
        indent=2,
    )

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        f"""import {{ BarChart, Grid, H1, H2, Row, Stack, Stat, Table, Text, mergeStyle, useHostTheme }} from "cursor/canvas";

const DATA = {data_json} as const;

export default function PktgenBenchResults() {{
  const t = useHostTheme();
  const rows = DATA.categories.map((id, i) => ({{
    case_id: id,
    fg_pps: DATA.fg_pps[i],
    nginx_pps: DATA.nginx_pps[i],
    fg_loss: DATA.fg_loss[i],
    nginx_loss: DATA.nginx_loss[i],
    ratio: DATA.fg_pps[i] > 0 ? (DATA.nginx_pps[i] / DATA.fg_pps[i]).toFixed(2) : "—",
  }}));

  return (
    <Stack gap={24} style={{ padding: 24, color: t.text }}>
      <Stack gap={8}>
        <H1>Pktgen bench — flow_gateway vs nginx</H1>
        <Text style={{ color: t.textMuted }}>
          Source: run_all_pktgen_bench.sh · measure {{DATA.meta.measure_sec}}s · warmup {{DATA.meta.warmup_sec}}s · {{DATA.meta.generated}}
        </Text>
      </Stack>
      <Row gap={16}>
        <Stat label="fg cases" value={{String(DATA.categories.length)}} tone="info" />
        <Stat
          label="avg fg recv pps"
          value={{String(Math.round(DATA.fg_pps.reduce((a, b) => a + b, 0) / DATA.fg_pps.length))}}
          tone="success"
        />
        <Stat
          label="avg nginx recv pps"
          value={{String(Math.round(DATA.nginx_pps.reduce((a, b) => a + b, 0) / DATA.nginx_pps.length))}}
          tone="neutral"
        />
      </Row>
      <Grid columns={{2}} gap={24}>
        <Stack gap={8}>
          <H2>Received pps by case</H2>
          <Text style={{ color: t.textMuted }}>Y-axis: packets/s at pktgen RX (client goodput)</Text>
          <BarChart
            categories={{DATA.categories}}
            series={{[
              {{ name: "flow_gateway", data: DATA.fg_pps, tone: "success" }},
              {{ name: "nginx", data: DATA.nginx_pps, tone: "info" }},
            ]}}
            height={{280}}
          />
        </Stack>
        <Stack gap={8}>
          <H2>Loss rate (%)</H2>
          <Text style={{ color: t.textMuted }}>Y-axis: loss_rate_pct = (sent − recv) / sent × 100</Text>
          <BarChart
            categories={{DATA.categories}}
            series={{[
              {{ name: "flow_gateway", data: DATA.fg_loss, tone: "warning" }},
              {{ name: "nginx", data: DATA.ng_loss, tone: "danger" }},
            ]}}
            height={{280}}
            valueSuffix="%"
          />
        </Stack>
      </Grid>
      <Stack gap={8}>
        <H2>Detail table</H2>
        <Table
          columns={{[
            {{ key: "case_id", header: "Case" }},
            {{ key: "fg_pps", header: "fg recv pps", align: "right" }},
            {{ key: "nginx_pps", header: "nginx recv pps", align: "right" }},
            {{ key: "fg_loss", header: "fg loss %", align: "right" }},
            {{ key: "nginx_loss", header: "nginx loss %", align: "right" }},
            {{ key: "ratio", header: "nginx/fg pps", align: "right" }},
          ]}}
          rows={{rows}}
        />
      </Stack>
    </Stack>
  );
}}
"""
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--fg-csv", type=Path, required=True)
    parser.add_argument("--nginx-csv", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--measure-sec", type=int, default=15)
    parser.add_argument("--warmup-sec", type=int, default=3)
    args = parser.parse_args()

    fg = load_csv(args.fg_csv)
    nginx = load_csv(args.nginx_csv)
    if not fg or not nginx:
        print("Missing CSV data; run run_all_pktgen_bench.sh first.", file=sys.stderr)
        return 1

    args.out_dir.mkdir(parents=True, exist_ok=True)
    report = args.out_dir / "pktgen-bench-report.md"
    write_markdown(report, fg, nginx, args.measure_sec, args.warmup_sec)
    write_canvas(CANVAS_PATH, fg, nginx, args.measure_sec, args.warmup_sec)
    print(f"Wrote {report}")
    print(f"Wrote {CANVAS_PATH}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
