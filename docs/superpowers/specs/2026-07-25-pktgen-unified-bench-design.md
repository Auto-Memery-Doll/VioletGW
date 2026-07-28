# Unified DPDK pktgen benchmark (fg + nginx)

Date: 2026-07-25  
Status: **current** — sole stress / cross-SUT benchmark path (fg + nginx)

## Problem

Previous fg vs nginx comparison used **different clients**:

- fg: in-process DPDK gen lcore (`ring_stress`)
- nginx: Python UDP sockets (`client_udp_bench.py`)

Metrics names matched but load and observation path did not.

## Goal

One **DPDK pktgen** instance generates traffic for both SUTs over the same L2/L3 lab topology.

```text
pktgen (net_tap, fg-pktgen0)
    │ 10.0.0.1 → 192.168.1.100:53
    ▼
veth fg-pktgen0 ↔ fg-sut0
    │ VIP 192.168.1.100:53
    ▼
SUT: flow_gateway (net_tap) OR nginx (stream UDP)
    ▼
echo 10.1.0.2:53 (on fg-sut0)
    ▼
replies → pktgen rx (ipackets)
```

## Lab addressing (matches `mbuf_fixture.hpp`)

| Role | IPv4 | Port |
|------|------|------|
| Client (pktgen src) | 10.0.0.1 | 4000+ |
| VIP | 192.168.1.100 | 53 |
| Gateway (SUT leg) | 192.168.1.10 | — |
| Upstream echo | 10.1.0.2 | 53 |

## Case matrix (unchanged C01–C08)

| Case | mode | payload |
|------|------|---------|
| C01 | hot | 4 |
| C02 | multi | 4 |
| C03 | newflow | 4 |
| C04 | bidir | 4 |
| C05 | hot | 512 |
| C06 | hot | 4096 |
| C07 | newflow | 512 |
| C08 | newflow | 4096 |

pktgen **range** mode implements flow patterns:

- **hot**: fixed sport 4000
- **multi**: sport 4000–4999 increment
- **newflow**: sport 50000–65530 increment
- **bidir**: same as multi (client sees echo replies; internal fg bidir inject is N/A on wire)

## Metrics (pktgen port 0)

| CSV field | Source |
|-----------|--------|
| `client_sent` | `opackets` after measure window |
| `client_received` | `ipackets` |
| `loss_rate_pct` | `(sent − recv) / sent × 100` |
| `sent_pps` / `received_pps` | delta / seconds |
| `offered_bps` / `received_bps` | pps × frame_bytes × 8 |

## Prerequisites

```bash
# pktgen source: ~/pktgen (clone Pktgen-DPDK)
sudo apt install meson ninja-build libbsd-dev liblua5.4-dev libpcap-dev git
./tools/stress/build_pktgen.sh   # pins pktgen-23.10.2 for apt DPDK 23.11
```

**Version pin:** Ubuntu `libdpdk-dev` 23.11.x has no `rte_ip6.h` (added in DPDK 24.11).  
Upstream pktgen `main` / 26.x fails to compile against 23.11; `build_pktgen.sh` checks out **`pktgen-23.10.2`** by default. Override with `PKTGEN_TAG=pktgen-26.03.0` when using DPDK ≥ 24.11.

## Scripts

| Script | Role |
|--------|------|
| `build_pktgen.sh` | Build pktgen with Lua |
| `pktgen/lab_topology.sh` | veth, IPs, static neigh |
| `pktgen/gen_case_lua.py` | Emit per-case Lua |
| `run_pktgen_bench.sh` | Matrix runner (fg / nginx) |
| `compare_pktgen_results.py` | Merge CSV |

## SUT-specific notes

### nginx

- `tools/stress/nginx/stream-udp-lab.conf` binds VIP `192.168.1.100:53`
- Kernel stack on `fg-sut0`

### flow_gateway

- Run `flow_gw` with `--no-huge --vdev net_tap0,iface=fg-sut0`
- Requires link-up on tap (interface must be `up` before EAL init)

## Removed (2026-07-25 cleanup)

The following are **deleted**; do not reintroduce parallel clients:

- `client_udp_bench.py`, `run_nginx_bench.sh`, `compare_fg_nginx.py`
- `run_ab.sh`, `ring_stress`, `fwd_bench`, `bench_common.hpp`
- `run_katran_bench.sh`, `compare_fg_katran.py`

Wire-level stress and fg vs nginx comparison use **only** `run_pktgen_bench.sh` + `compare_pktgen_results.py`.
In-process Forwarder smoke remains `test/dpdk/fwd_loop_smoke` (unit path, not a load generator).
