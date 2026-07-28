# WSL2 DPDK Environment Setup Design

Date: 2026-07-24  
Status: approved (conversation) — pending implementation

## Goal

On Ubuntu 24.04 WSL2, install DPDK via apt so `flow_gateway` can build and the existing DPDK helloworld sample can run (user-space EAL only; no physical NIC binding).

## Constraints

- Host: WSL2 (`microsoft-standard-WSL2`), no reliable PCI NIC / igb_uio / vfio for production packet I/O.
- Project discovers DPDK via `pkg-config libdpdk` and links statically (`src/base/CMakeLists.txt`).
- Boost, cmake, g++, ninja are already present.
- Do not change application source or CMake logic for this setup.

## Approach

**Apt install of `libdpdk-dev` (Ubuntu 23.11.x) + run samples with `--no-huge`.**

Rejected alternatives:

- Source-build DPDK: slower, unnecessary for this project’s pkg-config usage.
- Hugepage setup: optional on WSL; `--no-huge` is sufficient for functional verification.

## Steps

1. Install packages: `libdpdk-dev` and apt-pulled dependencies (e.g. numa/pcap/elf as required by the package).
2. Verify: `pkg-config --modversion libdpdk` reports a version (expected ~23.11).
3. Configure and build the project (existing `build/` or a clean cmake out-of-tree build).
4. Run `eal_smoke` (from `test/dpdk/eal_smoke.c`) as:
   ```bash
   sudo ./eal_smoke -l 0-1 --no-huge --log-level=8
   ```
5. Success = EAL init OK, per-lcore `hello from core N` printed, clean exit.

## Out of scope

- Physical NIC binding / vfio / igb_uio
- Persistent hugepage configuration
- Repo setup scripts / README changes (unless needed to unblock)
- Committing environment changes (machine-local packages)

## Success criteria

| Check | Pass condition |
|-------|----------------|
| pkg-config | `libdpdk` found |
| Build | `eal_smoke` (and project) links successfully |
| Run | helloworld completes under `--no-huge` |
