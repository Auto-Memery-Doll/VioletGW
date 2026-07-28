# Steady UDP loss check (low rate)

Kernel client binds to **ens33** IPv4 (SSH NIC). Lab IPs: `tools/pktgen/env.sh` / `src/config.hpp`.

| Script | Purpose |
|--------|---------|
| `loss_check.py` | send/count UDP through vgw (optional echo + vgwcp publish) |

## Prerequisites

```bash
# vgw only (ens192); ens33 stays on kernel for the client
sudo ./tools/pktgen/setup_nics.sh bind-sut
sudo ./build/bin/Src/vgw --datapath_mode=pipeline -l 0-2
```

## Lab networking

vgw answers ARP for the VIP. On ens33, add one static neighbor entry
(use the MAC from vgw startup log, e.g. `Port 0, MAC address: ...`):

```bash
sudo ip neigh replace 192.168.1.100 lladdr 00:0c:29:e9:54:dc dev ens33
```

## Run

From the repo root:

```bash
sudo python3 tools/steady/loss_check.py --echo --publish
```

Or from this directory (`tools/steady/`):

```bash
sudo python3 loss_check.py --echo --publish
```

See also [../pktgen/README.md](../pktgen/README.md) for max-throughput stress (DPDK client on ens256).
