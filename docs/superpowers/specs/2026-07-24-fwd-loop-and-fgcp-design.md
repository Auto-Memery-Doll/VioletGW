# fwd_loop_smoke + Go fgcp SHM writer

Date: 2026-07-24  
Status: approved (conversation)

## Goal

1. **fwd_loop_smoke**: DPDK mbuf + `Forwarder` forward/reverse NAT check (no `DpdkNetif` software rings).
2. **tools/fgcp**: minimal Go CLI that publishes upstream/policy into `/flow_gateway_cp`.

## Non-goals

- Full RX/TX lcore path through `fg_rx_ring_*` / `fg_tx_ring_*`
- TAP / external packet injection
- Expanding SHM to VIP/MAC
- Go health checks

## fwd_loop_smoke

- Path: `test/dpdk/fwd_loop_smoke.cpp`
- EAL: `-l 0 --no-huge` (no eth port required; mempool only)
- Steps: alloc mbuf → fill client→VIP → `handle` → assert gw/snat/upstream → reverse mbuf → assert VIP→client → exit 0/1
- CMake: binary next to `eal_smoke`; not in `unit` label (needs EAL runtime)

## tools/fgcp

- `tools/fgcp/main.go`: mmap SHM layout matching `fg_cp_shm.h`, bump `version` last
- Flags: `-shm`, `-policy mod|rr`, repeated `-upstream ip:port`
- Creates segment if missing

## Success

- `./fwd_loop_smoke -l 0 --no-huge` exits 0
- `go run ./tools/fgcp ...` then C++ `poll_apply` / running `flow_gw` sees new version
- Existing `ctest -L unit` still green
