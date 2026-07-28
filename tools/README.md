# tools/

| Path | Purpose |
|------|---------|
| `vgwcp/` | Go control-plane CLI (SHM publish) |
| `verify_vgwcp_apply.cpp` | Manual SHM checker |
| `pktgen/` | pktgen PCI stress — run scripts + `results/` CSV parsing ([pktgen/README.md](pktgen/README.md)) |
| `steady/` | low-rate UDP loss check ([steady/README.md](steady/README.md)) |
| `iperf3/` | iperf3 UDP lab ([iperf3/README.md](iperf3/README.md)) |
| `ebpf/` | libbpf NIC RX/TX observe for RTC/Pipeline ([ebpf/README.md](ebpf/README.md)) |

## Lab networking

| NIC | Role |
|-----|------|
| ens33 | SSH + kernel UDP client (steady / iperf3) |
| ens192 | vgw (DPDK) |
| ens256 | pktgen client (DPDK, stress only) |

vgw proxy-ARPs the VIP. Kernel clients on ens33 only need one static neighbor:

```bash
sudo ip neigh replace 192.168.1.100 lladdr 00:0c:29:e9:54:dc dev ens33
```

```bash
# steady (kernel client on ens33; run from repo root)
sudo python3 tools/steady/loss_check.py --echo --publish

# pktgen stress (vfio-bind ens192 + ens256)
sudo ./tools/pktgen/setup_nics.sh bind
sudo -E ./tools/pktgen/run.sh
```

Legacy path `tools/stress/` redirects to `tools/pktgen/`.
