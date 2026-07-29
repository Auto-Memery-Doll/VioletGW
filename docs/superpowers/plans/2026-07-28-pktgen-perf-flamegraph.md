# pktgen perf + FlameGraph Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add `PROFILE=1` mode to `tools/pktgen/run.sh` that samples `vgw` with `perf` during the measurement window and writes a FlameGraph SVG.

**Architecture:** Keep default matrix unchanged. When `PROFILE=1`, run one mode × one case; a helper starts `perf record -p $VGW_PID` after warmup+slack, then collapses stacks to SVG.

**Tech Stack:** bash, `perf`, FlameGraph (`stackcollapse-perf.pl`, `flamegraph.pl`).

---

### Task 1: Env knobs + helper script

**Files:**
- Modify: `tools/pktgen/env.sh`
- Create: `tools/pktgen/perf_profile.sh`

**Steps:**
1. Add `PROFILE`, `PROFILE_CASE`, `PERF_*`, `FLAMEGRAPH_DIR` defaults to `env.sh`.
2. Implement `perf_profile.sh` that:
   - Args: `vgw_pid`, `out_dir`, `title`
   - Checks `perf` and FlameGraph scripts exist
   - `sleep $((STRESS_WARMUP + PERF_WARMUP_SLACK_SEC))`
   - `perf record ... -p $vgw_pid -- sleep $STRESS_SECONDS`
   - Builds `perf.script`, `flame.svg`, `perf-report.txt`
3. Commit.

### Task 2: Wire into `run.sh`

**Files:**
- Modify: `tools/pktgen/run.sh`

**Steps:**
1. If `PROFILE=1`: require single mode; resolve `PROFILE_CASE` to pattern/flows/payload; run only that case.
2. Before/with pktgen: background-invoke `perf_profile.sh` with `VGW_PID`.
3. Wait for perf helper after pktgen returns (or run helper in background and `wait`).
4. Commit.

### Task 3: Docs

**Files:**
- Modify: `tools/pktgen/README.md`

**Steps:**
1. Document profile usage, deps, symbol build tip.
2. Commit.
