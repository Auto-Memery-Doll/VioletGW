# WSL2 DPDK Environment Setup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (inline) or superpowers:subagent-driven-development. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Install apt DPDK on Ubuntu 24.04 WSL2, build `flow_gateway`, and run `eal_smoke` with `--no-huge`.

**Architecture:** System packages provide `libdpdk` for pkg-config; project CMake already links via `src/base/CMakeLists.txt`. WSL runs EAL without hugepages.

**Tech Stack:** Ubuntu 24.04 apt DPDK 23.11.x, cmake, ninja/make, existing `test/dpdk/eal_smoke.c`.

## Global Constraints

- Install via apt only (`libdpdk-dev`), expected version ~23.11
- Do not modify application source or CMake for this setup
- No physical NIC binding / vfio / igb_uio
- No hugepage persistence required; use `--no-huge`
- No git commits unless the user asks

## File map

- No repo source files created or modified for success path
- Machine-local: apt packages under `/usr`
- Build outputs under `/home/violet/flow_gateway/build` (or recreate)

---

### Task 1: Install and verify DPDK packages

**Files:**
- Create: (none in repo)
- Modify: (none in repo)
- Test: shell `pkg-config`

**Interfaces:**
- Consumes: Ubuntu apt, `sudo`
- Produces: `pkg-config libdpdk` usable; version string ~23.11

- [x] **Step 1: Install packages**

```bash
sudo apt-get update
sudo apt-get install -y libdpdk-dev
```

Expected: packages install without error.

- [x] **Step 2: Verify pkg-config**

```bash
pkg-config --modversion libdpdk
pkg-config --exists libdpdk && echo OK
```

Expected: version like `23.11.4` (or similar 23.11.x), and `OK`.

---

### Task 2: Build the project including eal_smoke

**Files:**
- Create: build artifacts under `/home/violet/flow_gateway/build`
- Modify: (none in source)
- Test: binary exists at build output path for `eal_smoke`

**Interfaces:**
- Consumes: Task 1 `libdpdk` via pkg-config; existing CMakeLists
- Produces: executable `eal_smoke` (target name from `test/dpdk/CMakeLists.txt`)

- [ ] **Step 1: Configure CMake**

```bash
cd /home/violet/flow_gateway
cmake -S . -B build -G Ninja
```

If Ninja generator fails, use default Makefiles:
```bash
cmake -S . -B build
```

Expected: configure succeeds; no `no installation of DPDK found`.

- [ ] **Step 2: Build**

```bash
cmake --build build -j"$(nproc)"
```

Expected: build succeeds; `eal_smoke` binary present (search under `build/`).

- [ ] **Step 3: Locate binary**

```bash
find /home/violet/flow_gateway/build -name eal_smoke -type f
```

Expected: one path printed.

---

### Task 3: Run helloworld under WSL flags

**Files:**
- Create: (none)
- Modify: (none)
- Test: process exit 0 and stdout contains `hello from core`

**Interfaces:**
- Consumes: Task 2 `eal_smoke` binary
- Produces: verified EAL run on WSL

- [ ] **Step 1: Run with --no-huge**

```bash
sudo <path-to-eal_smoke> -l 0-1 --no-huge --log-level=8
```

Expected: lines like `hello from core 0` / `hello from core 1`, exit code 0.

- [ ] **Step 2: Record result**

If EAL fails on hugepage/permissions, retry same flags; do not configure hugepages unless `--no-huge` is insufficient. Report final command and outcome to the user.

---

## Spec coverage

| Spec item | Task |
|-----------|------|
| apt install libdpdk-dev | Task 1 |
| pkg-config verify | Task 1 |
| cmake build | Task 2 |
| run eal_smoke --no-huge | Task 3 |
| no source/CMake changes | Global Constraints |
| no NIC binding / hugepage persistence | Global Constraints / Task 3 |
