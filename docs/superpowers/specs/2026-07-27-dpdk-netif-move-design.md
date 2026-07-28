# Design: Move DPDK netif into `src/dpdk/`

**Date:** 2026-07-27  
**Status:** approved  
**Approach:** A (thin `dpdk/` directory + split config)

## Goal

Keep gateway business logic under `src/` root. Move DPDK EAL/port encapsulation and soft-ring netif I/O into `src/dpdk/`. Split DPDK/IO constants out of `src/config.hpp`.

## Non-goals

- No behavior change (same rings, lcores, burst sizes, drop/usleep policy).
- Do not move `base/ring.hpp` or `base/util` MAC helpers.
- Do not split EAL vs netif into separate translation units (can be a later cleanup).
- Do not rename `DpdkNetif` / `dpdk_netif_mg` APIs in this change (include path only).

## Target layout

```
src/
  config.hpp              # VIP / gateway / MAC / upstream / session / CP only
  vgw.h / vgw.cpp         # #include "dpdk/netif.hpp"
  forward / session / …   # unchanged
  base/                   # unchanged (ring, util, …)
  dpdk/
    CMakeLists.txt
    config.hpp            # mempool, queues, port mask, IO ring/burst
    netif.hpp             # was dpdk_netif.hpp
    netif.cpp             # was dpdk_netif.cpp
```

## Config split

| Stays in `src/config.hpp` (`vgw::config`) | Moves to `src/dpdk/config.hpp` (`vgw::config` or `vgw::dpdk::config`) |
|---|---|
| VIP / gateway IP+MAC | `DPDK_*` mempool / queue / port mask |
| Upstream seed / balance policy | `IO_RX_BURST` / `IO_TX_BURST` / `IO_RING_*` |
| Session timeouts | `IO_TX_RING_FULL_SLEEP_US` |
| CP SHM name / poll interval | `IO_PORTS_PER_RXTX_LCORE_PAIR` |

**Namespace choice:** keep symbols under `vgw::config` in both headers so call sites only change the include path (`"config.hpp"` vs `"dpdk/config.hpp"`), not every `config::DPDK_*` qualifier. Both headers may define members in the same `vgw::config` namespace (C++ allows this).

## CMake

- `src/dpdk/CMakeLists.txt` builds library `vgw_dpdk_io` from `netif.cpp`, public include = `src/` (so `"dpdk/netif.hpp"` works).
- `src/CMakeLists.txt`: `add_subdirectory(dpdk)`; `vgw_core` = `vgw.cpp` only, links `vgw_dpdk_io` (+ existing business libs).
- Root `vgw_dpdk` INTERFACE (pkg-config libdpdk) remains project-wide; `vgw_dpdk_io` is the C++ I/O wrapper.

## Call-site updates

- `vgw.h`: `#include "dpdk/netif.hpp"`
- `netif.cpp`: `#include "dpdk/config.hpp"` (not business `config.hpp`)
- Tests / fixtures that need mempool size: `#include "dpdk/config.hpp"`; VIP/lab L3 still from `config.hpp`
- Knowledge note `docs/knowledge/RtC & Pipeline.md`: point IO constants at `src/dpdk/config.hpp`

## Compatibility

- Delete `src/dpdk_netif.hpp` / `src/dpdk_netif.cpp` after move (no shim unless something external still includes the old path; in-tree only).
- No git commit in this task unless explicitly requested.

## Success criteria

- `src/` root has no `dpdk_netif.*`
- Business `config.hpp` has no `DPDK_*` / `IO_*` constants
- Unit / dpdk smoke targets still build and pass with updated includes
