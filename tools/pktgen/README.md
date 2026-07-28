# Pktgen stress — Pipeline vs RTC max throughput (physical NIC)

Measures end-to-end UDP throughput with pktgen at `PKTGEN_RATE=100` for:

- `--datapath_mode=pipeline` (`-l 0-2`)
- `--datapath_mode=rtc --rtc_workers=1` (`-l 0`)

No RSS / multi-worker RTC. Spec: `docs/superpowers/specs/2026-07-27-stress-pipeline-rtc-design.md`.

Pktgen CLI / Lua syntax: [docs/knowledge/Using Pktgen.md](../../docs/knowledge/Using%20Pktgen.md).

## Host NIC map

| NIC | PCI | Role |
|-----|-----|------|
| ens33 | `0000:03:00.0` | SSH — never bind |
| ens192 | `0000:0b:00.0` | vgw (DPDK; RX+TX) |
| ens224 | `0000:13:00.0` | unused (kernel) |
| ens256 | `0000:1b:00.0` | pktgen client + upstream backend (DPDK) |

Flow: `pktgen → vgw → NAT → pktgen` (client is upstream; no kernel echo).

VMware: put ens192/ens256 on the same LAN with promiscuous mode. Lab IPs/MACs match `src/config.hpp` and `env.sh`.

## Lab networking

Kernel tools (steady / iperf3) on ens33: one static neighbor for the VIP (vgw also answers ARP):

```bash
sudo ip neigh replace 192.168.1.100 lladdr 00:0c:29:e9:54:dc dev ens33
```

Pktgen uses DPDK on ens256; no kernel neigh needed — bind NICs and run.

## Layout

| Path | Purpose |
|------|---------|
| `env.sh` | NIC roles, IPs, MACs, modes, lcores, durations |
| `setup_nics.sh` | vfio bind/unbind ens192 + ens256 |
| `build.sh` | build pktgen |
| `run.sh` | bind → vgw + vgwcp → pipeline+rtc × M01–M06 (`PROFILE=1` for flame graph) |
| `run_profile_all.sh` | `PROFILE=1` sweep: each mode × M01–M06 (flame + CSV per case) |
| `perf_profile.sh` | wait warmup → `perf record` on vgw → FlameGraph SVG |
| `gen_lua.py` | generate pktgen Lua for one case |
| `test_gen_lua.py` | unit tests for `gen_lua.py` |
| `results/` | `PKTGEN_SUMMARY` → CSV parsing ([results/README.md](results/README.md)) |
| `out/` | run artifacts (`results.csv`, `logs/`, `lua/`) |

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
./tools/pktgen/build.sh
cmake --build build --target vgw

sudo ./tools/pktgen/setup_nics.sh bind
sudo -E ./tools/pktgen/run.sh
# results: tools/pktgen/out/results.csv

sudo ./tools/pktgen/setup_nics.sh unbind
```

Single mode: `sudo -E STRESS_DATAPATH_MODES=pipeline ./tools/pktgen/run.sh`

`STRESS_BIND=0` skips auto-bind if NICs are already vfio-bound.

Results CSV is named per run (does not overwrite). Counters/rates use **mega** units
with 2 decimal places (`client_sent_M`, `sent_Mpps`, `offered_Mbps`, …):

```text
out/results_pipeline+rtc_matrix_r100.csv
out/results_rtc_M01_r25.csv
out/results_rtc_M01_r25_20260728T134500Z.csv   # collision → timestamp

out/perf/rtc_M01_r25/flame.svg                 # PROFILE=1
out/perf/rtc_M01_r25_20260728T134500Z/         # collision → timestamp
```

A missing `PKTGEN_SUMMARY` for any case fails the run (non-zero exit).

## Offer rate (contention tests)

**Important:** pktgen `rate` is **% of theoretical line rate**, not % of what your NIC can
actually send. On 10G with 46-byte frames, **5% ≈ 0.89 Mpps** — still above the lab
vmxnet3 ceiling (~0.68 Mpps), so `PKTGEN_OFFER_SCALE=0.05` mapped to `rate=5` looked
identical to full blast (~20M packets / 30s).

Contention scaling is therefore **relative to measured baseline Mpps**, converted to a
(possibly fractional) line-rate % that pktgen can pace:

| Env | Default | Meaning |
|-----|---------|---------|
| `PKTGEN_OFFER_SCALE` | `1` | `≥1` → full blast (`PKTGEN_RATE`, usually 100). `<1` → target = `BASELINE_MPPS × SCALE` |
| `PKTGEN_BASELINE_MPPS` | `0.68` | Lab full-blast `sent_Mpps` (tune from your V1 CSV) |
| `PKTGEN_TARGET_MPPS` | (unset) | Absolute target Mpps; overrides scale × baseline |
| `PKTGEN_LINK_GBPS` | `10` | Link speed used for Mpps → rate% conversion |
| `PKTGEN_RATE` | `100` | Used only for full-blast (`SCALE≥1`, no `TARGET`) |

```bash
# Same as V1 baseline (rate=100, saturates vNIC)
sudo PROFILE=1 STRESS_DATAPATH_MODES=rtc PROFILE_CASE=M01 \
  PKTGEN_OFFER_SCALE=1 ./tools/pktgen/run.sh

# ~25% / 10% / 5% of baseline Mpps (e.g. 0.68 → 0.17 / 0.068 / 0.034 Mpps)
sudo PROFILE=1 STRESS_DATAPATH_MODES=rtc PROFILE_CASE=M01 \
  PKTGEN_OFFER_SCALE=0.25 ./tools/pktgen/run.sh
sudo PROFILE=1 STRESS_DATAPATH_MODES=rtc PROFILE_CASE=M01 \
  PKTGEN_OFFER_SCALE=0.1 ./tools/pktgen/run.sh
sudo PROFILE=1 STRESS_DATAPATH_MODES=rtc PROFILE_CASE=M01 \
  PKTGEN_OFFER_SCALE=0.05 ./tools/pktgen/run.sh

# Or set absolute offer directly:
sudo PROFILE=1 STRESS_DATAPATH_MODES=rtc PROFILE_CASE=M01 \
  PKTGEN_TARGET_MPPS=0.05 ./tools/pktgen/run.sh
```

Output files are tagged `s0.05` / `t0.05` (scale / target), not the old misleading `r5`.
CSV `pktgen_rate` is the **actual** % passed to pktgen (often fractional, e.g. `0.1904`).

Interpretation:

- If cutting offer drops `sent_pps` but `received_pps` stays ~flat until offer ≈ goodput → vgw TX cap, little client/gw TX contention benefit.
- If cutting offer **raises** `received_pps` → shared path (vSwitch / host) contention between pktgen TX and vgw TX.

## Profile (perf + flame graph)

Optional mode: run **one** datapath mode × **one** case, sample **vgw only** during the
Lua **measurement** window (after warmup), and write a FlameGraph SVG.

```bash
# deps
sudo apt install -y linux-tools-common "linux-tools-$(uname -r)"
git clone https://github.com/brendangregg/FlameGraph.git ~/FlameGraph

# prefer symbols (RelWithDebInfo or Debug)
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build --target vgw

sudo PROFILE=1 STRESS_DATAPATH_MODES=pipeline PROFILE_CASE=M01 ./tools/pktgen/run.sh
# → tools/pktgen/out/perf/pipeline_M01_s1/flame.svg  (tag depends on OFFER_SCALE)

# All cases in one go (one CSV + flame per case); default mode=pipeline:
sudo PROFILE=1 PROFILE_CASE=all STRESS_DATAPATH_MODES=pipeline \
  PKTGEN_OFFER_SCALE=0.1 ./tools/pktgen/run.sh
# Same via helper:
sudo PKTGEN_OFFER_SCALE=0.1 ./tools/pktgen/run_profile_all.sh
# pipeline + rtc × all cases:
sudo STRESS_DATAPATH_MODES="pipeline rtc" PKTGEN_OFFER_SCALE=0.1 \
  ./tools/pktgen/run_profile_all.sh
```

| Env | Default | Meaning |
|-----|---------|---------|
| `PROFILE` | `0` | `1` = profile mode (flame per case) |
| `PROFILE_CASE` | `M01` | `M01`–`M06`, or `all` for full sweep |
| `PROFILE_CASES` | (unset) | Space-separated subset; overrides `PROFILE_CASE` |
| `PERF_FREQ` | `99` | `perf record -F` |
| `PERF_CALL_GRAPH` | `dwarf` | `dwarf` or `fp` |
| `PERF_WARMUP_SLACK_SEC` | `1` | Extra wait after warmup before `perf` |
| `FLAMEGRAPH_DIR` | `$DEV_HOME/FlameGraph` | FlameGraph scripts |

Default matrix (`PROFILE=0`) is unchanged. `run_profile_all.sh` sets `PROFILE=1` and `PROFILE_CASE=all`.

## Env knobs

`STRESS_SECONDS`, `STRESS_WARMUP`, `PKTGEN_RATE`, `PKTGEN_OFFER_SCALE`,
`PKTGEN_BASELINE_MPPS`, `PKTGEN_TARGET_MPPS`, `PKTGEN_LINK_GBPS`, `STRESS_BIND`,
`STRESS_DATAPATH_MODES`, `VGW_LCORES_PIPELINE`, `VGW_LCORES_RTC`,
`PKTGEN_LCORES`, `PKTGEN_MAP` (default `[4:4].0` with lcores `3-4`; main cannot do port I/O),
`PKTGEN_TIMEOUT_SEC`, plus profile knobs above.

## Metrics

Observed on pktgen client NIC after warmup: `sent_pps`, `received_pps`, `loss_rate_pct`
(one-way through gateway back to client; no separate echo process).
