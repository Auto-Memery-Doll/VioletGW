# UpstreamTable + Worker Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add lock-free `UpstreamTable` (atomic policy + snapshot) and wire `main` worker: recv → Forwarder → send/free.

**Architecture:** Control plane publishes `atomic<BalancePolicy>` and an immutable node snapshot via `shared_ptr` exchange. Workers `pick` with relaxed loads only (no mutex). `main` seeds from `config.hpp` and runs the I/O loop.

**Tech Stack:** C++17, DPDK mbuf I/O, gtest, existing `forward`/`session`.

## Global Constraints

- English-only in code
- No mutex on `pick`
- No heartbeat / no `balance::Engine` wiring
- IPs network-order; ports host-order (match session/forward)
- Do not commit unless user asks

---

### Task 1: UpstreamTable (TDD)

**Files:**
- Create: `src/upstream/upstream.hpp`, `src/upstream/upstream.cpp`, `src/upstream/CMakeLists.txt`
- Create: `test/upstream/upstream_test.cpp`, `test/upstream/CMakeLists.txt`
- Modify: `src/CMakeLists.txt`, `test/CMakeLists.txt`

**Interfaces:**
- Produces: `fg::upstream::UpstreamEndpoint`, `BalancePolicy::{mod,rr}`, `UpstreamTable::{set,set_policy,pick,size}`

- [x] **Step 1:** Add gtest for empty/mod sticky/set/policy; wire CMake stubs so test can compile against headers
- [x] **Step 2:** Run test — expect fail (missing lib)
- [x] **Step 3:** Implement `UpstreamTable` (atomic policy + `shared_ptr<const vector<Endpoint>>`)
- [x] **Step 4:** Run `ctest -L unit` — upstream tests pass

### Task 2: Config seed + main worker

**Files:**
- Modify: `src/config.hpp`, `src/main.cpp`, `src/CMakeLists.txt` (link `upstream`)

**Interfaces:**
- Consumes: `UpstreamTable`, `Forwarder`, `SessionTable`, `DpdkNetif`

- [x] **Step 1:** Add VIP/gw/MAC/upstream seed + idle timeout to `config.hpp`
- [x] **Step 2:** Wire `main`: seed table, picker lambda, recv/handle/send/free + periodic expire
- [x] **Step 3:** Build `flow_gw` + full `ctest -L unit`

---

## Spec coverage

| Spec item | Task |
|-----------|------|
| Lock-free pick / atomic publish | 1 |
| mod + rr same node list | 1 |
| Unit tests | 1 |
| config seed | 2 |
| main worker loop | 2 |
| No heartbeat | both (omitted) |
