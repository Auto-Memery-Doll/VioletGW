# Design: Pipeline vs RTC max-throughput stress (physical NIC)

**Date:** 2026-07-27  
**Status:** approved (pending implementation plan)  
**Related:** `tools/stress/`, `docs/knowledge/RtC & Pipeline.md`, `docs/superpowers/specs/2026-07-27-datapath-rtc-pipeline-design.md`

## Goal

Measure **maximum sustained throughput** for the two shipped datapath modes under a **fixed full blast** client load (`PKTGEN_RATE=100`):

| Mode | Flags | Lcores (default) |
|------|-------|------------------|
| Pipeline | `--datapath_mode=pipeline` | `-l 0-2` |
| RTC (P0) | `--datapath_mode=rtc --rtc_workers=1` | `-l 0` |

Primary metrics: **`received_pps`** and **`loss_rate`**. **`sent_pps`** only confirms the client saturated. Side-by-side comparison is a byproduct of running the same matrix on both modes into one CSV.

## Non-goals

- Hardware RSS, multi-queue, `rtc_workers > 1`, soft_rr
- Rate sweep / binary search for a loss-threshold knee
- Latency histograms (p50/p99), CPU flame graphs
- CI pass/fail gates on absolute PPS
- Changing forward / session / control-plane semantics
- Keeping legacy C01–C08 / multi / bidir case compatibility

## Decisions

| Topic | Choice |
|-------|--------|
| Approach | Clean rewrite of `tools/stress` (single `run.sh`, no split runners) |
| Load | Fixed `PKTGEN_RATE=100` |
| Matrix | Curated M01–M06 (session pattern × payload × flow count) |
| Modes under test | `pipeline` and single-worker `rtc` only |
| Observation point | pktgen client NIC (ens36) port stats after warmup |
| Result file | `tools/stress/out/results.csv` with `datapath_mode` column |

## Topology

Unchanged physical-NIC lab:

```text
pktgen (ens36 / DPDK) ──► vgw (ens34 / DPDK) ──► udp_echo (ens35 / kernel)
         ▲                                              │
         └────────────── echo return path ──────────────┘
ens33 = SSH / management — never vfio-bind
```

L2/L3 defaults match `src/config.hpp` (VIP `192.168.1.100:53`, upstream `10.1.0.2:53`, lab MACs).

## Metric definitions

All counts are taken on the **pktgen client port** after warmup baseline reset, over `STRESS_SECONDS`:

| Metric | Definition |
|--------|------------|
| `client_sent` / `client_received` | `opackets` / `ipackets` deltas in the measure window |
| `sent_pps` | `client_sent / seconds` |
| `received_pps` | `client_received / seconds` |
| `lost` | `max(client_sent - client_received, 0)` |
| `loss_rate` | `lost / client_sent` (0 if sent == 0) |
| `offered_bps` / `received_bps` | `pps × frame_bytes × 8`, where `frame_bytes = 14+20+8+payload` |

This is an **end-to-end RTT view**: loss may occur in vgw, upstream echo, or return path — not vgw TX drops alone.

## Traffic patterns

pktgen uses **range** mode. Sport ranges **wrap**, so after the first pass through N ports, subsequent packets mostly **hit** existing sessions.

| Pattern | Behavior | What it stresses |
|---------|----------|------------------|
| `hot` | Fixed sport (4000) | Single session, hit path, PPS/bandwidth ceiling |
| `flows` | Sport range of size `N` | ~N sessions after warmup (table occupancy); not unbounded create storm |

Legacy names `multi` / `bidir` / `newflow` are **removed** from generators and docs.

SNAT port pool is `10000–60000` (~50k); **M05 `flows=10000` fits**. Sport base for `flows` starts at 4000 so `4000+10000-1` stays in uint16 range.

## Case matrix (M01–M06)

| Case | pattern | flows | payload | Purpose |
|------|---------|------:|--------:|---------|
| M01 | hot | 1 | 4 | Single-flow small-packet PPS ceiling |
| M02 | hot | 1 | 1400 | Single-flow large-packet bandwidth ceiling |
| M03 | flows | 1000 | 4 | 1k sessions + small packets |
| M04 | flows | 1000 | 1400 | 1k sessions + large packets |
| M05 | flows | 10000 | 4 | Cross: heavier session table |
| M06 | hot | 1 | 64 | Cross: mid packet size |

Each `datapath_mode` runs all six → **12** measure windows per full run.

Defaults: `STRESS_WARMUP=5`, `STRESS_SECONDS=30` (overridable via env).

## `tools/stress` file plan

| File | Action | Role |
|------|--------|------|
| `env.sh` | Update | NIC/PCI/IP/MAC, durations, paths; add `STRESS_DATAPATH_MODES`, per-mode lcore vars, matrix defaults |
| `setup_nics.sh` | Keep | vfio bind/unbind ens34+ens36 |
| `build.sh` | Keep | Build pktgen |
| `udp_echo.py` | Keep | Kernel upstream echo |
| `gen_lua.py` | Rewrite | Emit only M01–M06 (`hot` / `flows`); drop C01–C08 / multi / bidir |
| `run.sh` | Rewrite | Bind → echo → for each mode start vgw → matrix → CSV |
| `README.md` | Rewrite | Dual-mode max-throughput stress only |
| `out/` | Runtime | Not committed; supersedes old `results_vgw.csv` |

No new split runners. Every retained file must map to a step of this stress path.

## Run flow

```text
./tools/stress/build.sh
cmake --build build --target vgw
sudo -E ./tools/stress/run.sh
```

Inside `run.sh`:

1. Optionally bind DPDK NICs (`STRESS_BIND=1` default).
2. Configure ens35 + start `udp_echo.py`.
3. For each mode in `STRESS_DATAPATH_MODES` (default `pipeline rtc`):
   - Start `vgw` with mode-specific flags and lcores.
   - For each M01–M06: generate Lua, run pktgen at rate 100, parse `PKTGEN_SUMMARY`, append CSV row.
   - Stop `vgw`.
4. Stop echo; leave NICs bound with unbind hint if this run bound them.

Single-mode debug: `STRESS_DATAPATH_MODES=pipeline` or `rtc`.

### Lcore defaults

| Role | Env | Default |
|------|-----|---------|
| vgw Pipeline | `VGW_LCORES_PIPELINE` | `0-2` |
| vgw RTC | `VGW_LCORES_RTC` | `0` |
| pktgen | `PKTGEN_LCORES` | `3-4` |

### CSV schema

```text
timestamp,datapath_mode,case_id,pattern,flows,payload_bytes,seconds,warmup,
client_sent,client_received,lost,loss_rate_pct,sent_pps,received_pps,
offered_bps,received_bps,frame_bytes
```

Path: `tools/stress/out/results.csv`.

## Success criteria (manual)

- `sent_pps` near client full-blast for that frame size → load is valid.
- Record per-mode `received_pps` and `loss_rate` for M01–M06; use those as the max-throughput readout.
- No automated absolute thresholds in this design.

## Risks

- E2E loss includes echo and return path, not only vgw.
- VMware vNIC absolute PPS is below bare metal; **relative** Pipeline vs RTC comparison remains the useful signal.
- If lab ever shrinks SNAT range below 10k, lower M05 `flows` and document in README.

## Supersedes

Operational stress entrypoint remains `tools/stress/`. This design **replaces** the C01–C08 matrix from `docs/superpowers/specs/2026-07-25-pktgen-unified-bench-design.md` for VioletGW physical-NIC runs. Older WSL Layer A/B (`fwd_bench` / `ring_stress`) stays historical / deleted.
