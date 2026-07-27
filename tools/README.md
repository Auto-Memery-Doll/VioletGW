# tools/

| Path | Purpose |
|------|---------|
| `vgwcp/` | Go control-plane CLI (SHM publish) |
| `verify_vgwcp_apply.cpp` | Manual SHM checker |
| `stress/` | pktgen PCI stress (see [stress/README.md](stress/README.md)) |

```bash
# ens33=SSH  ens34=vgw  ens35=echo  ens36=pktgen
sudo -E ./tools/stress/run.sh
```
