# Stress test design (Layer A + B, WSL)

Date: 2026-07-24  
Status: approved (conversation) — WSL scope; Layer C (NIC/pktgen) out of scope

## Goal

Establish a **repeatable baseline** for the single-threaded forward worker on WSL2:

1. **Layer A — `fwd_bench`**: microbenchmark `Forwarder::handle` only (no `DpdkNetif`, no rings).
2. **Layer B — `ring_stress`**: full worker loop through software rings + RX/TX lcores, using a **null vdev** (no physical NIC).

Together they answer:

- How fast can one core do parse → session → NAT?
- How much overhead do rings + I/O lcores add at the same logical load?

## Non-goals

- pktgen / MoonGen / production NIC (Layer C)
- TAP injection, multi-process secondary EAL
- SHM / control-plane churn during stress (use fixed upstream picker)
- CI gating on absolute PPS numbers (WSL numbers are relative baselines only)
- Refactoring `main.cpp` into a shared library (duplicate minimal worker loop in test binary for now)

## Environment

- WSL2 Ubuntu, DPDK via pkg-config, EAL `--no-huge`
- Layer A: EAL + mempool only (same as `fwd_loop_smoke`)
- Layer B: EAL + `--vdev net_null0` so `dpdk::init()` / link check succeeds; **3 lcores minimum** (`-l 0-2` or wider)

Shared lab constants (align with `fwd_loop_smoke` / `config.hpp`):

| Field | Value |
|-------|-------|
| Client | 10.0.0.1:4000+ |
| VIP | 192.168.1.100:53 |
| Gateway | 192.168.1.10 |
| Upstream | 10.1.0.2:53 |

---

## Layer 0 — Metrics (embedded in A/B binaries)

Each benchmark prints a **one-line summary** plus optional CSV-friendly fields.

| Counter | Layer A | Layer B |
|---------|---------|---------|
| `pkts_in` | handled calls | pushed to rx_ring |
| `pkts_fwd` / `pkts_rev` | by `HandleResult` | drained from tx_ring by direction |
| `drop` | `HandleResult::drop` | worker drop + gen ring full |
| `new_sessions` | on create path | same |
| `elapsed_ns` | wall clock | wall clock |
| `pps` | pkts_in / elapsed | pkts_in / elapsed |

Timing: `std::chrono::steady_clock` for wall time; optional `rte_rdtsc` delta per packet in Layer A debug mode (`--verbose`).

No changes to production `main.cpp` in the first iteration.

---

## Layer A — `fwd_bench`

**Path:** `test/dpdk/fwd_bench.cpp`  
**Link:** `forward` (+ transitive session/packet/upstream)

### Modes (`--mode`)

| Mode | Behavior | Maps to load |
|------|----------|--------------|
| `hot` | Single pre-created session; repeat forward handle | L1 single-flow max |
| `multi` | Pre-create N sessions; round-robin or random sport lookup | L2 steady multi-flow |
| `newflow` | Monotonic new client sport every packet (create storm) | L3 new session / SNAT |
| `bidir` | Alternate forward / reverse on established sessions | L4 symmetric |

### CLI

```
fwd_bench -l 0 --no-huge --mode hot|multi|newflow|bidir \
  [--seconds 60] [--warmup 5] [--flows 10000] [--pkt-size 64]
```

Defaults: `--seconds 30`, `--warmup 5`, `--flows 10000` (ignored for `hot`), `--pkt-size 64` (UDP payload padding only).

### Loop

1. EAL init, private mempool (≥4096 mbufs for `newflow`).
2. Build `SessionTable`, `ForwardConfig`, static upstream picker (single backend).
3. Warmup: run mode loop without counting.
4. Measure: run until `--seconds` elapsed; count results.
5. Print summary; exit 0.

### Correctness guard

- Sample 1 in 10 000 packets: light parse + assert dst IP/port after forward (or VIP/client after reverse).
- Any assert failure → exit 1 (bench invalid).

### Success (Layer A)

- Exits 0 on WSL with all four modes.
- `hot` PPS ≥ `newflow` PPS (sanity: create path slower).
- No unexpected `drop` in `hot` / `multi` / `bidir`.

---

## Layer B — `ring_stress`

**Path:** `test/dpdk/ring_stress.cpp`  
**Link:** `forward`, `control` (optional, can skip SHM), `dpdk` netif path via existing targets

### Architecture

Single process; traffic generator on a **dedicated lcore**; worker on **main lcore** (mirrors `flow_gw`).

```text
gen lcore ──push──► vgm_rx_ring_0 ──► worker (main): recv_burst → Forwarder → send_burst
              ◄──pop── vgm_tx_ring_0 ◄──
```

RX/TX lcores run as today (`DpdkNetifManager::init` + `net_null` vdev).

### Modes

Same four modes as Layer A; generator creates client→VIP mbufs (and reverse mbufs for `bidir` by reading SNAT from drained forward packets or pre-built session table on gen side).

For `bidir` / `multi`, gen maintains a small side table (client sport → snat port) synced from first forward drain.

### CLI

```
ring_stress -l 0-3 --no-huge --vdev net_null0 --mode hot|multi|newflow|bidir \
  [--seconds 60] [--warmup 5] [--flows 10000]
```

Lcore map (fixed for v1):

| lcore | Role |
|-------|------|
| 0 | EAL master + **worker** (forward loop) |
| 1 | RX lcore (`nic_recv`) |
| 2 | TX lcore (`nic_send`) |
| 3 | **Generator** (push rx / pop tx) |

### Worker loop (duplicate of `main.cpp`, trimmed)

- No SHM poll (fixed `UpstreamTable` seed).
- No session expire during short runs (<60s default); optional `--expire-ms 0` to disable.
- Burst size 32 (same as `main.cpp`).
- Counters: `worker_rx`, `worker_tx`, `worker_drop`.

### Generator backpressure

- If `rx_ring` push returns 0: increment `gen_rx_ring_full`, optionally usleep(1).
- If `tx_ring` pop empty: spin briefly (gen should not outrun worker indefinitely in `hot`).

### Ring size note

Default `VDEV_*_ring_num = 32`. Bench reports `gen_rx_ring_full` / implicit RX drops so we can distinguish **ring-limited** vs **CPU-limited**. Optional follow-up: `--ring-size 1024` via EAL `--vdev` does not help; would need config override or test-only flag (out of v1 scope).

### Success (Layer B)

- Exits 0 on WSL with `--vdev net_null0`.
- `hot` mode: `worker_drop == 0`, `pkts_fwd` ≈ `pkts_in` within 1%.
- Layer A `hot` PPS ≥ Layer B `hot` PPS (rings add overhead; if B > A, bug).

---

## Load matrix (manual runs)

| ID | Command | Primary signal |
|----|---------|----------------|
| L1 | `fwd_bench --mode hot` | Forwarder ceiling |
| L2 | `fwd_bench --mode multi --flows 10000` | Session lookup |
| L3 | `fwd_bench --mode newflow` | create + SNAT |
| L4 | `fwd_bench --mode bidir --flows 1000` | Reverse path |
| L1′ | `ring_stress --mode hot` | End-to-end ceiling |
| L2′ | `ring_stress --mode multi --flows 10000` | Ring + lookup |

Suggested script (optional v1): `tools/stress/run_ab.sh` runs L1–L4 + L1′–L2′, appends CSV row with date/git-sha.

---

## CMake

Under `test/dpdk/CMakeLists.txt`:

```cmake
add_executable(fwd_bench fwd_bench.cpp)
target_link_libraries(fwd_bench PRIVATE forward)

add_executable(ring_stress ring_stress.cpp)
target_link_libraries(ring_stress PRIVATE forward upstream dpdk_netif ...)
```

Not labeled `unit` (require EAL runtime). Document run commands in architecture doc §7.

---

## Implementation order

1. **`fwd_bench`** + shared mbuf fill helper (extract from `fwd_loop_smoke` into `test/dpdk/mbuf_fixture.hpp` if duplication > ~80 lines).
2. **`ring_stress`** gen lcore + worker loop; verify `hot` on WSL.
3. **`run_ab.sh`** (optional) + one paragraph in `architecture-upgrade-2026-07.md`.

---

## Risks

| Risk | Mitigation |
|------|------------|
| WSL PPS noisy | Treat as regression baseline, not SLA |
| Ring size 32 dominates B | Report ring-full counters; document |
| `dpdk::init` blocks on link | Use `net_null` vdev |
| 3+ lcores on small WSL CPU | Document minimum `-l 0-3`; reduce if needed |
| SNAT exhaustion in long `newflow` | Cap `--seconds` or use port range aware limits |

---

## Success criteria (overall)

| Check | Pass |
|-------|------|
| Build | `fwd_bench`, `ring_stress` link in `build/bin/Test/dpdk/` |
| Layer A | All modes exit 0 on `--no-huge` |
| Layer B | `hot` + `multi` exit 0 on `--no-huge --vdev net_null0` |
| Regression | Record L1/L1′ PPS in first baseline run (human note or CSV) |
| Unit tests | `ctest -L unit` still 21/21 green |
