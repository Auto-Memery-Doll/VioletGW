# SHM Control Plane + Remove Balance Implementation Plan

> **For agentic workers:** Execute task-by-task. Commits only if the user asks.

**Goal:** Delete legacy `balance/`; add versioned POSIX SHM publisher apply path into `UpstreamTable`.

**Architecture:** Pure-C `fg_cp_shm` layout; C++ `CpShm` create/attach + `poll_apply`; main seeds and polls. Hot path untouched.

**Tech Stack:** POSIX `shm_open`/`mmap`, gtest, existing `upstream`.

## Global Constraints

- English-only code
- No mutex on datapath pick; SHM only on poll path
- Do not commit unless asked

---

### Task 1: Remove `src/balance/`

- Delete balance tree; unlink from CMake; rebuild

### Task 2: SHM header + CpShm (TDD)

- Create `src/control/fg_cp_shm.h`, `cp_shm.hpp|cpp`, tests, CMake
- Wire `main` seed + poll

### Task 3: Verify

- `ctest -L unit` all green; `flow_gw` builds
