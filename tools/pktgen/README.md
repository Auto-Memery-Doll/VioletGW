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
| `run.sh` | bind → vgw + vgwcp → pipeline+rtc × M01–M06 |
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

A missing `PKTGEN_SUMMARY` for any case fails the run (non-zero exit).

## Env knobs

`STRESS_SECONDS`, `STRESS_WARMUP`, `PKTGEN_RATE`, `STRESS_BIND`,
`STRESS_DATAPATH_MODES`, `VGW_LCORES_PIPELINE`, `VGW_LCORES_RTC`,
`PKTGEN_LCORES`, `PKTGEN_MAP` (default `[4:4].0` with lcores `3-4`; main cannot do port I/O),
`PKTGEN_TIMEOUT_SEC`.

## Metrics

Observed on pktgen client NIC after warmup: `sent_pps`, `received_pps`, `loss_rate_pct`
(one-way through gateway back to client; no separate echo process).
