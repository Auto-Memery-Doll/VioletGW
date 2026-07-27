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
| `csv_parse.sh` | PKTGEN_SUMMARY → CSV row parser |
| `test_gen_lua.py` | unit tests for `gen_lua.py` |
| `test_csv_parse.sh` | unit test for CSV parsing |

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

A missing `PKTGEN_SUMMARY` for any case fails the run (non-zero exit).

## Env knobs

`STRESS_SECONDS`, `STRESS_WARMUP`, `PKTGEN_RATE`, `STRESS_BIND`,
`STRESS_DATAPATH_MODES`, `VGW_LCORES_PIPELINE`, `VGW_LCORES_RTC`, `PKTGEN_LCORES`.

## Metrics

Observed on pktgen client NIC after warmup: `sent_pps`, `received_pps`, `loss_rate_pct`
(end-to-end RTT view — includes echo/return path).
