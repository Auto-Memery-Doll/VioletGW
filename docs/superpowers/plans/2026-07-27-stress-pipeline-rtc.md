# Pipeline vs RTC Stress Tools Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rewrite `tools/stress` so a single `run.sh` measures max throughput for Pipeline and single-worker RTC under a fixed full-blast M01–M06 matrix.

**Architecture:** Keep physical-NIC topology (pktgen → vgw → kernel UDP echo). `gen_lua.py` emits only `hot`/`flows` Lua cases; `run.sh` loops `STRESS_DATAPATH_MODES`, restarts `vgw` per mode with correct gflags/lcores, appends one CSV with a `datapath_mode` column. Drop legacy C01–C08 / multi / bidir / newflow.

**Tech Stack:** bash, Python 3, DPDK pktgen Lua, existing `vgw` binary + gflags

**Spec:** `docs/superpowers/specs/2026-07-27-stress-pipeline-rtc-design.md`

## Global Constraints

- English only in code/comments/scripts under `tools/stress`
- No RSS / `rtc_workers > 1` / soft_rr in this stress path
- Fixed `PKTGEN_RATE=100` by default (no rate sweep)
- Every retained file under `tools/stress` must serve this stress path
- No git commit unless the user explicitly asks (skip Commit steps or stage only)
- Do not change `vgw` datapath / session semantics — scripts only

## File map

| File | Responsibility |
|------|----------------|
| `tools/stress/gen_lua.py` | Generate one pktgen Lua script for a case (`hot` or `flows`) |
| `tools/stress/test_gen_lua.py` | Unit tests for frame size, sport blocks, M01–M06 emission |
| `tools/stress/env.sh` | Lab NIC/IP/MAC + stress knobs (modes, lcores, durations) |
| `tools/stress/run.sh` | Orchestrate bind → echo → per-mode vgw → matrix → CSV |
| `tools/stress/README.md` | How to run dual-mode max-throughput stress |
| `docs/test/README.md` | Point to new results path / modes |
| `tools/stress/setup_nics.sh` | Unchanged (vfio bind/unbind) |
| `tools/stress/build.sh` | Unchanged (pktgen build) |
| `tools/stress/udp_echo.py` | Unchanged (kernel echo) |

---

### Task 1: Rewrite `gen_lua.py` + unit tests

**Files:**
- Modify: `tools/stress/gen_lua.py` (full rewrite of CLI + patterns)
- Create: `tools/stress/test_gen_lua.py`
- Keep: Lua measure/summary logic (warmup → baseline → measure → `PKTGEN_SUMMARY`)

**Interfaces:**
- Produces CLI:
  - `python3 gen_lua.py --case M01 --pattern hot --flows 1 --payload 4 --seconds 30 --warmup 5 --rate 100 --src-ip ... --dst-ip ... --dst-port ... --src-mac ... --dst-mac ... -o path.lua`
- Produces Lua line:
  - `PKTGEN_SUMMARY case=<id> pattern=<hot|flows> payload=<n> seconds=<n> warmup=<n> flows=<n> client_sent=... client_received=... lost=... loss_rate_pct=... sent_pps=... received_pps=... offered_bps=... received_bps=... frame_bytes=...`
- Produces helpers (importable by tests):
  - `frame_size(payload: int) -> int`
  - `src_port_block(pattern: str, flows: int) -> str`
- Consumes: nothing from later tasks

- [ ] **Step 1: Write failing unit tests**

Create `tools/stress/test_gen_lua.py`:

```python
#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(DIR))

import gen_lua  # noqa: E402


class GenLuaTest(unittest.TestCase):
    def test_frame_size(self) -> None:
        self.assertEqual(gen_lua.frame_size(4), 46)
        self.assertEqual(gen_lua.frame_size(1400), 1442)

    def test_hot_fixed_sport(self) -> None:
        block = gen_lua.src_port_block("hot", 1)
        self.assertIn('"start", 4000', block)
        self.assertIn('"inc", 0', block)

    def test_flows_range(self) -> None:
        block = gen_lua.src_port_block("flows", 1000)
        self.assertIn('"start", 4000', block)
        self.assertIn('"inc", 1', block)
        self.assertIn('"max", 4999', block)

    def test_flows_10000_in_range(self) -> None:
        block = gen_lua.src_port_block("flows", 10000)
        self.assertIn('"max", 13999', block)

    def test_unknown_pattern_raises(self) -> None:
        with self.assertRaises(ValueError):
            gen_lua.src_port_block("newflow", 1000)

    def test_cli_writes_summary_fields(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            out = Path(td) / "m01.lua"
            subprocess.check_call(
                [
                    sys.executable,
                    str(DIR / "gen_lua.py"),
                    "--case",
                    "M01",
                    "--pattern",
                    "hot",
                    "--flows",
                    "1",
                    "--payload",
                    "4",
                    "-o",
                    str(out),
                ],
                cwd=str(DIR),
            )
            text = out.read_text()
            self.assertIn("PKTGEN_SUMMARY case=M01 pattern=hot", text)
            self.assertIn("payload=4", text)
            self.assertIn("flows=1", text)
            self.assertNotIn("mode=", text)
            self.assertNotIn("sut=", text)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run tests — expect fail**

Run: `python3 tools/stress/test_gen_lua.py -v`

Expected: FAIL (`--pattern` / `flows` pattern missing, or import errors / old `mode` CLI).

- [ ] **Step 3: Implement `gen_lua.py`**

Replace `tools/stress/gen_lua.py` with:

```python
#!/usr/bin/env python3
"""Generate one pktgen Lua case (M01–M06: hot | flows)."""
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
    p.add_argument("--rate", type=int, default=100)
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
        rate_pct=args.rate,
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
```

- [ ] **Step 4: Re-run unit tests — expect pass**

Run: `python3 tools/stress/test_gen_lua.py -v`

Expected: all tests PASS.

- [ ] **Step 5: Commit (skip unless user asked)**

If user requested a commit:

```bash
git add tools/stress/gen_lua.py tools/stress/test_gen_lua.py
git commit -m "$(cat <<'EOF'
Rewrite stress Lua generator for M01–M06 hot/flows patterns.

EOF
)"
```

Otherwise: skip.

---

### Task 2: Update `env.sh` knobs

**Files:**
- Modify: `tools/stress/env.sh`

**Interfaces:**
- Produces env vars (defaults):
  - `STRESS_DATAPATH_MODES=pipeline rtc`
  - `VGW_LCORES_PIPELINE=0-2`
  - `VGW_LCORES_RTC=0`
  - `PKTGEN_LCORES=3-4`
  - `PKTGEN_RATE=100`
  - `STRESS_SECONDS=30`, `STRESS_WARMUP=5`
  - Removes reliance on single `STRESS_FLOWS` / `VGW_LCORES` as the matrix driver (matrix carries per-case flows in `run.sh`)
- Consumes: existing NIC/PCI/IP/MAC/bin paths (keep)

- [ ] **Step 1: Replace bench section in `env.sh`**

Keep NIC / L2/L3 / DEV_HOME / binary blocks unchanged. Replace the `# --- Bench ---` section (from `STRESS_SECONDS` through `STRESS_BIND`) with:

```bash
# --- Bench ---
export STRESS_SECONDS="${STRESS_SECONDS:-30}"
export STRESS_WARMUP="${STRESS_WARMUP:-5}"
export PKTGEN_RATE="${PKTGEN_RATE:-100}"
export STRESS_OUT_DIR="${STRESS_OUT_DIR:-${STRESS_DIR}/out}"
# Auto vfio-bind SUT+client NICs at start of run.sh (1=yes)
export STRESS_BIND="${STRESS_BIND:-1}"

# Space-separated: pipeline and/or rtc
export STRESS_DATAPATH_MODES="${STRESS_DATAPATH_MODES:-pipeline rtc}"
export VGW_LCORES_PIPELINE="${VGW_LCORES_PIPELINE:-0-2}"
export VGW_LCORES_RTC="${VGW_LCORES_RTC:-0}"
export PKTGEN_LCORES="${PKTGEN_LCORES:-3-4}"
```

Also delete unused `export STRESS_FLOWS=...` and any `VGW_LCORES=` default if present (per-mode vars replace it).

- [ ] **Step 2: Sanity-source**

Run:

```bash
bash -c 'source tools/stress/env.sh && echo "$STRESS_DATAPATH_MODES|$VGW_LCORES_PIPELINE|$VGW_LCORES_RTC|$PKTGEN_RATE"'
```

Expected: `pipeline rtc|0-2|0|100`

- [ ] **Step 3: Commit (skip unless user asked)**

---

### Task 3: Rewrite `run.sh` for dual datapath modes + M01–M06

**Files:**
- Modify: `tools/stress/run.sh` (full orchestration rewrite)

**Interfaces:**
- Consumes: `env.sh` vars from Task 2; `gen_lua.py` CLI from Task 1; `setup_nics.sh`; `udp_echo.py`; `VGW_BIN`; `PKTGEN_BIN`
- Produces: `tools/stress/out/results.csv` with header:

```text
timestamp,datapath_mode,case_id,pattern,flows,payload_bytes,seconds,warmup,client_sent,client_received,lost,loss_rate_pct,sent_pps,received_pps,offered_bps,received_bps,frame_bytes
```

- [ ] **Step 1: Replace `run.sh` body**

Keep `set -euo pipefail`, `source env.sh`, `log`/`die`, `stop_services`, `prepare_upstream`, `maybe_bind`, and root check. Replace `start_vgw` and `run_matrix` with:

```bash
start_vgw() {
  local mode="$1"
  local lcores
  case "${mode}" in
    pipeline) lcores="${VGW_LCORES_PIPELINE}" ;;
    rtc) lcores="${VGW_LCORES_RTC}" ;;
    *) die "unknown datapath mode: ${mode}" ;;
  esac
  [[ -x "${VGW_BIN}" ]] || die "vgw missing; cmake --build build --target vgw"
  log "SUT=vgw datapath_mode=${mode} pci=${SUT_PCI} lcores=${lcores}"
  local -a cmd=("${VGW_BIN}" --datapath_mode="${mode}" -l "${lcores}" --file-prefix=vgw -a "${SUT_PCI}")
  if [[ "${mode}" == "rtc" ]]; then
    cmd+=(--rtc_workers=1)
  fi
  "${cmd[@]}" &
  VGW_PID=$!
  sleep 2
  kill -0 "${VGW_PID}" 2>/dev/null || die "vgw exited early (mode=${mode})"
}

stop_vgw() {
  if [[ -n "${VGW_PID}" ]]; then
    kill "${VGW_PID}" 2>/dev/null || true
    wait "${VGW_PID}" 2>/dev/null || true
    VGW_PID=""
  fi
}

append_csv_row() {
  local out="$1"
  local datapath_mode="$2"
  local line="$3"
  local ts case_id pattern payload seconds warmup flows
  local client_sent client_received lost loss_rate_pct
  local sent_pps received_pps offered_bps received_bps frame_bytes

  # shellcheck disable=SC2001
  eval "$(echo "${line}" | sed -n \
    's/PKTGEN_SUMMARY case=\([^ ]*\) pattern=\([^ ]*\) payload=\([0-9]*\) seconds=\([0-9]*\) warmup=\([0-9]*\) flows=\([0-9]*\) client_sent=\([0-9]*\) client_received=\([0-9]*\) lost=\([0-9]*\) loss_rate_pct=\([0-9.]*\) sent_pps=\([0-9.]*\) received_pps=\([0-9.]*\) offered_bps=\([0-9.]*\) received_bps=\([0-9.]*\) frame_bytes=\([0-9]*\).*/\
case_id=\1 pattern=\2 payload=\3 seconds=\4 warmup=\5 flows=\6 client_sent=\7 client_received=\8 lost=\9 loss_rate_pct=\10 sent_pps=\11 received_pps=\12 offered_bps=\13 received_bps=\14 frame_bytes=\15/p')"

  ts="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "${ts},${datapath_mode},${case_id},${pattern},${flows},${payload},${seconds},${warmup},${client_sent},${client_received},${lost},${loss_rate_pct},${sent_pps},${received_pps},${offered_bps},${received_bps},${frame_bytes}" >>"${out}"
}

run_one_case() {
  local datapath_mode="$1"
  local case_id="$2"
  local pattern="$3"
  local flows="$4"
  local payload="$5"
  local out="$6"
  local lcores="${PKTGEN_LCORES}"
  local prefix="pg_vgw_pci"
  local lua="${LUA_DIR}/${datapath_mode}_${case_id}.lua"
  local logf line pktgen_cmd

  log "case ${case_id} datapath=${datapath_mode} pattern=${pattern} flows=${flows} payload=${payload}"
  python3 "${DIR}/gen_lua.py" --case "${case_id}" --pattern "${pattern}" \
    --payload "${payload}" --seconds "${STRESS_SECONDS}" --warmup "${STRESS_WARMUP}" \
    --flows "${flows}" --rate "${PKTGEN_RATE}" \
    --src-ip "${CLIENT_IP}" --dst-ip "${VIP_IP}" --dst-port "${VIP_PORT}" \
    --src-mac "${CLIENT_MAC}" --dst-mac "${GW_MAC}" \
    -o "${lua}"

  logf="$(mktemp)"
  pktgen_cmd=$(printf '%q ' "${PKTGEN_BIN}" -l "${lcores}" --file-prefix="${prefix}" \
    -a "${CLIENT_PCI}" \
    -- -P -m "[1:1].0" -f "${lua}")
  script -qefc "${pktgen_cmd}" /dev/null >"${logf}" 2>&1 || true
  line="$(grep '^PKTGEN_SUMMARY' "${logf}" | tail -1 || true)"
  rm -f "${logf}"
  [[ -n "${line}" ]] || {
    log "missing PKTGEN_SUMMARY for ${datapath_mode}/${case_id}"
    return 0
  }
  append_csv_row "${out}" "${datapath_mode}" "${line}"
}

run_matrix() {
  local out="${OUT_DIR}/results.csv"
  local modes=(${STRESS_DATAPATH_MODES})
  # case_id:pattern:flows:payload
  local cases=(
    "M01:hot:1:4"
    "M02:hot:1:1400"
    "M03:flows:1000:4"
    "M04:flows:1000:1400"
    "M05:flows:10000:4"
    "M06:hot:1:64"
  )
  local mode entry case_id pattern flows payload

  [[ -x "${PKTGEN_BIN}" ]] || die "pktgen missing at ${PKTGEN_BIN} (DEV_HOME=${DEV_HOME}); as violet: ${DIR}/build.sh"

  mkdir -p "${OUT_DIR}" "${LUA_DIR}"
  maybe_bind
  prepare_upstream

  echo "timestamp,datapath_mode,case_id,pattern,flows,payload_bytes,seconds,warmup,client_sent,client_received,lost,loss_rate_pct,sent_pps,received_pps,offered_bps,received_bps,frame_bytes" >"${out}"

  for mode in "${modes[@]}"; do
    start_vgw "${mode}"
    for entry in "${cases[@]}"; do
      IFS=: read -r case_id pattern flows payload <<<"${entry}"
      run_one_case "${mode}" "${case_id}" "${pattern}" "${flows}" "${payload}" "${out}"
    done
    stop_vgw
  done

  stop_services
  log "results: ${out}"
  if [[ "${BOUND_BY_US}" -eq 1 ]]; then
    log "NICs still vfio-bound; restore with: ${DIR}/setup_nics.sh unbind"
  fi
}

run_matrix
```

Update `stop_services` so it still kills echo + vgw (call `stop_vgw` logic or keep killing `VGW_PID`).

Important: `local modes=(${STRESS_DATAPATH_MODES})` intentionally word-splits the space-separated list.

- [ ] **Step 2: Syntax check**

Run: `bash -n tools/stress/run.sh`

Expected: no output, exit 0.

- [ ] **Step 3: Dry-run Lua generation for all cases (no root / no NIC)**

```bash
source tools/stress/env.sh
mkdir -p /tmp/stress_lua
for e in M01:hot:1:4 M02:hot:1:1400 M03:flows:1000:4 M04:flows:1000:1400 M05:flows:10000:4 M06:hot:1:64; do
  IFS=: read -r c p f pl <<<"$e"
  python3 tools/stress/gen_lua.py --case "$c" --pattern "$p" --flows "$f" --payload "$pl" -o "/tmp/stress_lua/${c}.lua"
done
grep -h 'PKTGEN_SUMMARY' /tmp/stress_lua/*.lua
```

Expected: six summary template lines with `pattern=hot` or `pattern=flows` and correct payloads/flows.

- [ ] **Step 4: Commit (skip unless user asked)**

---

### Task 4: Docs — `tools/stress/README.md` + pointer docs

**Files:**
- Modify: `tools/stress/README.md` (full rewrite)
- Modify: `docs/test/README.md` (results path + dual mode note)
- Optional touch if still wrong: `tools/README.md` (one-liner OK as-is if it only points at `run.sh`)

**Interfaces:**
- Docs must match Task 2/3 env names and `out/results.csv`

- [ ] **Step 1: Rewrite `tools/stress/README.md`**

```markdown
# Stress tools — Pipeline vs RTC max throughput (physical NIC)

Measures end-to-end UDP throughput with pktgen at `PKTGEN_RATE=100` for:

- `--datapath_mode=pipeline` (`-l 0-2`)
- `--datapath_mode=rtc --rtc_workers=1` (`-l 0`)

No RSS / multi-worker RTC. Spec: `docs/superpowers/specs/2026-07-27-stress-pipeline-rtc-design.md`.

## Host NIC map

| NIC | PCI | Role |
|-----|-----|------|
| ens33 | `0000:02:01.0` | SSH — never bind |
| ens34 | `0000:02:02.0` | vgw (DPDK) |
| ens35 | `0000:02:03.0` | upstream UDP echo (kernel) |
| ens36 | `0000:02:04.0` | pktgen client (DPDK) |

VMware: put ens34/35/36 on the same LAN with promiscuous mode.

## Scripts

| Script | Purpose |
|--------|---------|
| `env.sh` | NIC roles, IPs, MACs, modes, lcores, durations |
| `setup_nics.sh` | vfio bind/unbind ens34 + ens36 |
| `build.sh` | build pktgen |
| `run.sh` | bind → echo → pipeline+rtc × M01–M06 |
| `gen_lua.py` | generate pktgen Lua for one case |
| `udp_echo.py` | kernel upstream echo |
| `test_gen_lua.py` | unit tests for `gen_lua.py` |

## Case matrix

| Case | pattern | flows | payload |
|------|---------|------:|--------:|
| M01 | hot | 1 | 4 |
| M02 | hot | 1 | 1400 |
| M03 | flows | 1000 | 4 |
| M04 | flows | 1000 | 1400 |
| M05 | flows | 10000 | 4 |
| M06 | hot | 1 | 64 |

Full default run ≈ 9–12 minutes (2 modes × 6 cases × ~35s traffic + overhead).

## Run

```bash
# SSH on ens33 first
./tools/stress/build.sh
cmake --build build --target vgw

sudo -E ./tools/stress/run.sh
# results: tools/stress/out/results.csv

sudo ./tools/stress/setup_nics.sh unbind
```

Single mode: `sudo -E STRESS_DATAPATH_MODES=pipeline ./tools/stress/run.sh`

`STRESS_BIND=0` skips auto-bind if NICs are already vfio-bound.

## Env knobs

`STRESS_SECONDS`, `STRESS_WARMUP`, `PKTGEN_RATE`, `STRESS_BIND`,
`STRESS_DATAPATH_MODES`, `VGW_LCORES_PIPELINE`, `VGW_LCORES_RTC`, `PKTGEN_LCORES`.

## Metrics

Observed on pktgen client NIC after warmup: `sent_pps`, `received_pps`, `loss_rate_pct`
(end-to-end RTT view — includes echo/return path).
```

- [ ] **Step 2: Update `docs/test/README.md`**

Replace the stress result line that mentions `results_vgw.csv` / C01–C08 with:

```markdown
# Tests

Unit / smoke: see [test/README.md](../../test/README.md).

Physical-NIC max-throughput stress (Pipeline + RTC): see [tools/stress/README.md](../../tools/stress/README.md).

```bash
./tools/stress/build.sh
cmake --build build --target vgw
sudo -E ./tools/stress/run.sh
```

Results: `tools/stress/out/results.csv` (includes `datapath_mode` column).
```

Keep any other accurate content already in that file if present; only fix outdated stress pointers.

- [ ] **Step 3: Commit (skip unless user asked)**

---

### Task 5: Verification gate

**Files:**
- None required (run existing artifacts)

**Interfaces:**
- Consumes: Tasks 1–4 outputs

- [ ] **Step 1: Unit tests**

Run: `python3 tools/stress/test_gen_lua.py -v`  
Expected: PASS

- [ ] **Step 2: Shell syntax**

Run: `bash -n tools/stress/run.sh && bash -n tools/stress/env.sh`  
Expected: exit 0

- [ ] **Step 3: Optional short live smoke (needs root + NICs)**

Only if the lab is free:

```bash
sudo -E STRESS_SECONDS=5 STRESS_WARMUP=2 STRESS_DATAPATH_MODES=rtc ./tools/stress/run.sh
head -3 tools/stress/out/results.csv
```

Expected: CSV header + at least one `rtc,M01,...` data row with numeric PPS fields. Full matrix is not required for this gate.

- [ ] **Step 4: Confirm no legacy case strings remain under `tools/stress/`**

Run:

```bash
rg -n 'C0[1-8]|newflow|bidir|results_vgw|STRESS_FLOWS|--mode' tools/stress --glob '!out/**'
```

Expected: no matches (except possibly comments explicitly saying they were removed — prefer zero matches).

- [ ] **Step 5: Commit (skip unless user asked)**

---

## Spec coverage checklist

| Spec requirement | Task |
|------------------|------|
| Dual modes pipeline + rtc_workers=1 | Task 3 `start_vgw` |
| Fixed rate 100 | Task 2 `PKTGEN_RATE` + Task 3 |
| M01–M06 matrix | Task 3 `cases=()` |
| hot / flows patterns only | Task 1 |
| Metric definitions / PKTGEN_SUMMARY | Task 1 (same formulas) |
| CSV + `datapath_mode` | Task 3 |
| `out/results.csv` | Task 3 |
| File keep/rewrite plan | Tasks 1–4 |
| Lcore defaults 0-2 / 0 / 3-4 | Task 2 |
| README rewrite | Task 4 |
| No RSS | Global + Task 3 (never passes steer flags) |
| Drop C01–C08 legacy | Tasks 1, 3, 5 |

## Plan self-review

- No TBD/placeholder steps; full script bodies included.
- `pattern` naming consistent across gen_lua, SUMMARY, CSV, README.
- `STRESS_FLOWS` removed; per-case flows live in `run.sh` matrix.
- Commit steps gated on user request per project rule.
