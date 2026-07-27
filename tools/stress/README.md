# Stress tools (physical NIC)

## Host NIC map

| NIC | PCI | Role |
|-----|-----|------|
| ens33 | `0000:02:01.0` | SSH — never bind |
| ens34 | `0000:02:02.0` | vgw (DPDK) |
| ens35 | `0000:02:03.0` | upstream UDP echo (kernel) |
| ens36 | `0000:02:04.0` | pktgen client (DPDK) |

VMware: put ens34/35/36 on the same LAN with promiscuous mode (hairpin: SUT TX → echo on ens35 → back).

L2/L3 defaults match `src/config.hpp` (`GW_MAC` / `UPSTREAM_MAC` / `CLIENT_MAC`, VIP `192.168.1.100:53`, upstream `10.1.0.2:53`).

## Scripts

| Script | Purpose |
|--------|---------|
| `env.sh` | NIC roles, IPs, MACs, paths |
| `setup_nics.sh` | vfio bind/unbind ens34 + ens36 |
| `build.sh` | build pktgen |
| `run.sh` | bind → echo → vgw → C01–C08 |
| `gen_lua.py` | generate pktgen Lua |
| `udp_echo.py` | kernel upstream echo |

## Run

```bash
# SSH on ens33 first (e.g. 192.168.56.137)
./tools/stress/build.sh
cmake --build build --target vgw

sudo -E ./tools/stress/run.sh
# results: tools/stress/out/results_vgw.csv

sudo ./tools/stress/setup_nics.sh unbind   # restore NICs when done
```

`STRESS_BIND=0` skips auto-bind if you already ran `setup_nics.sh bind`.

## Env knobs

`STRESS_SECONDS`, `STRESS_WARMUP`, `STRESS_FLOWS`, `PKTGEN_RATE`, `STRESS_BIND`, `VGW_LCORES`, `PKTGEN_LCORES`.
