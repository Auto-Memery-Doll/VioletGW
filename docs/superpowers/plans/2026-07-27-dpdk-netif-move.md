# Move DPDK netif into `src/dpdk/` Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (or subagent-driven-development). Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Relocate DPDK EAL/netif + IO config under `src/dpdk/`; leave gateway business under `src/` root.

**Architecture:** Thin `vgw_dpdk_io` library (`netif.cpp` + `dpdk/config.hpp`); business `config.hpp` keeps VIP/upstream/session/CP only; symbols stay in `vgw::config`.

**Tech Stack:** CMake, DPDK, existing `vgw_core` / gtest

**Spec:** `docs/superpowers/specs/2026-07-27-dpdk-netif-move-design.md`

## Global Constraints

- No behavior change
- Do not move `base/ring.hpp` or util MAC helpers
- Keep `DpdkNetif` / `dpdk_netif_mg` API names
- English only in code
- No git commit unless user asks

---

### Task 1: Create `src/dpdk/` files + CMake

**Files:**
- Create: `src/dpdk/config.hpp`, `src/dpdk/netif.hpp`, `src/dpdk/netif.cpp`, `src/dpdk/CMakeLists.txt`
- Modify: `src/config.hpp`, `src/CMakeLists.txt`
- Delete: `src/dpdk_netif.hpp`, `src/dpdk_netif.cpp`

- [x] Split DPDK/IO constants into `src/dpdk/config.hpp` (`namespace vgw::config`)
- [x] Slim `src/config.hpp` to business constants only
- [x] Move netif sources to `dpdk/netif.*` with includes `"dpdk/netif.hpp"` / `"dpdk/config.hpp"`
- [x] Add `vgw_dpdk_io` library; wire `vgw_core` to link it without compiling `dpdk_netif.cpp`

### Task 2: Update call sites + docs

**Files:**
- Modify: `src/vgw.h`, `test/dpdk/mbuf_fixture.hpp`, `docs/knowledge/RtC & Pipeline.md` (and any other `dpdk_netif` includes)

- [x] Point includes at `dpdk/netif.hpp` / `dpdk/config.hpp` as needed
- [x] Update knowledge note path for IO constants

### Task 3: Verify build + unit tests

- [x] Reconfigure/build
- [x] Run unit tests that link `vgw_core` / do not need PCI
