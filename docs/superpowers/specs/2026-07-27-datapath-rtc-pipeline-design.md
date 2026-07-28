# Design: Datapath modes — RTC (RSS / soft RR) + Pipeline

**Date:** 2026-07-27  
**Status:** approved — **P0 shipped** (Pipeline + single-worker RTC via gflags); **P1/P2 pending** (multi-worker hw RSS / soft RR)  
**Related:** `docs/knowledge/RtC & Pipeline.md`, `src/dpdk/netif.*`

### Implementation status

| Milestone | Scope | Status |
|-----------|-------|--------|
| **P0** | `--datapath_mode=pipeline` (default) and `--datapath_mode=rtc --rtc_workers=1` | **Shipped** |
| **P1** | RTC + hardware RSS / multi-queue (`rtc_workers > 1`, `hw_rss` / `auto` on capable NIC) | Pending |
| **P2** | RTC + software steer (`soft_rr`, distributor + RR rings) | Pending |

P0 rejects `rtc_workers > 1` at init with `P1/P2 not implemented; use --rtc_workers=1`.

## Goal

Support **init-time** selection between datapath models in one binary, without virtual functions:

1. **Pipeline** — existing RX lcore → soft ring → worker → soft ring → TX lcore  
2. **RTC + hardware RSS / multi-queue** — one worker per RX queue; worker does RX → handle → TX on that queue  
3. **RTC + software steer (fallback)** — when NIC lacks usable multi-queue/RSS but user wants **multi-worker** RTC: one **RX distributor** lcore **round-robins** packets onto per-worker rings; each **worker** does handle → TX  
4. **RTC single worker** — if `rtc_workers == 1`, always the simplified path: **same thread** RX → handle → TX on queue 0; **no** distributor lcore, **no** steer rings

Configuration via **gflags**. Hot path uses `enum` + `switch` over a `union` of mode-local state/params (static polymorphism).

## Non-goals

- Hot-swap mode or worker count at runtime  
- Dynamic worker membership / work stealing  
- Per-flow soft sticky hashing (soft multi-worker uses **round-robin**, not consistent hash)  
- YAML config (may come later)  
- Changing L4 forward / session semantics  
- Multi-port load split beyond today’s port mask (worker still picks one primary port unless extended later)

## Decisions (from review)

| Topic | Choice |
|-------|--------|
| Mode select | Init-time only |
| Config | gflags |
| Polymorphism | `enum DatapathMode` + `union` params/state; no vtable |
| Soft multi-thread RTC I/O | **RX distributor only**; workers **handle + TX** (not one thread doing both RX and TX) |
| Soft dispatch | **Round-robin** across fixed `workers` (no 5-tuple hash) |
| `rtc_workers == 1` | **Direct RTC only** — never launch distributor |

---

## Architecture

```
                    ┌─────────────────────────────┐
  gflags ──────────►│ DatapathConfig (enum+union) │
                    └─────────────┬───────────────┘
                                  │ init once
                                  ▼
                         DpdkNetifManager
                                  │
          ┌───────────────────────┼───────────────────────┐
          ▼                       ▼                       ▼
     Pipeline            RTC (workers==1)         RTC hw RSS           RTC soft RR
  RX/TX lcores           same-core NIC            Worker↔RXQ/TXQ       Distributor + Workers
  + soft rings           RX→handle→TX             (classic RTC)        RR rings + worker TX
```

`VioletGW::run` (or N worker loops) keeps the shape **recv → handle → send**; only the meaning of recv/send changes per mode.

**Simplify rule:** `mode == Rtc && rtc_workers == 1` → path (2) single-core direct NIC, regardless of `--rtc_steer`. Steer / distributor apply only when `rtc_workers > 1`.

---

## Mode taxonomy

### `DatapathMode`

```cpp
enum class DatapathMode : uint8_t {
    Pipeline = 0,   // default (preserves current lab behavior)
    Rtc      = 1,   // run-to-completion family
};
```

RTC further selects **steer** (not a separate top-level mode, nested under RTC params):

```cpp
enum class RtcSteer : uint8_t {
    Auto   = 0,  // workers>1: prefer hw RSS if usable; else soft_rr
    HwRss  = 1,  // require multi-queue + RSS; fail init if unavailable
    SoftRr = 2,  // force distributor + round-robin (only meaningful if workers>1)
};
```

### Capability probe (init)

On each enabled port, after `rte_eth_dev_info_get` / configure:

- `max_rx_queues`, `max_tx_queues`, RSS offload flags  
- **Usable hw RSS path:** requested `rtc_workers <= max_rx_queues` (and TX queues policy below) and device can enable RSS  

Resolve order:

1. If `rtc_workers == 1` → **DirectRtc** (single queue, no distributor). Ignore steer.  
2. Else if `Auto` and hw OK → `HwRss`  
3. Else if `Auto` → `SoftRr`  
4. Else honor explicit `HwRss` / `SoftRr` (fail hard if `HwRss` requested but caps insufficient)

---

## Config: enum + union

```cpp
struct PipelineParams {
    uint16_t ring_size;            // 0 → config::IO_RING_SIZE
    bool launch_rx_lcore;
    bool launch_tx_lcore;
    int tx_ring_full_sleep_us;
};

struct RtcParams {
    RtcSteer steer;
    uint16_t workers;              // >= 1
    uint16_t rx_queues;            // 0 → same as workers when HwRss
    uint16_t tx_queues;            // 0 → same as workers when possible
    uint16_t dist_ring_size;       // SoftRr per-worker ring; 0 → default
    // SoftRr only when workers>1: distributor lcore optional (pick next free)
};

struct DatapathConfig {
    DatapathMode mode;
    uint32_t port_mask;
    uint16_t worker_port;          // primary port for VioletGW (default 0)

    union {
        PipelineParams pipeline;
        RtcParams rtc;
    } u;
};
```

Filled once from gflags (+ defaults from `dpdk/config.hpp`). Invalid combos rejected at parse/init (e.g. `Pipeline` ignores RTC-only flags with warning).

---

## Per-mode data path

### 1. Pipeline (unchanged semantics)

```
NIC ─► RX lcore ─► rx_ring ─► Worker ─► tx_ring ─► TX lcore ─► NIC
```

`recv_burst` / `send_burst` = soft ring pop/push (current behavior).  
State in union: `rx_ring`, `tx_ring`, `stop`.

### 2. RTC single worker (`rtc_workers == 1`) — simplified

```
NIC RXQ 0 ─► same lcore: rx_burst → handle → tx_burst
```

- **No** distributor thread, **no** per-worker rings, **no** RR/RSS steer logic.  
- This is the default RTC lab path and the P0 deliverable.  
- Equivalent to classic single-core run-to-completion.

### 3. RTC + hardware RSS (`workers > 1`)

```
NIC RXQ i ─► Worker i: rx_burst(q=i) → handle → tx_burst(q=i)
```

- Configure `rx_queues = tx_queues = workers` (or explicit flags).  
- Enable RSS on IPv4/UDP (match gateway traffic).  
- **No** distributor; **no** soft rings between NIC and worker.  
- Each worker pinned to an lcore; owns queue `i`.

`recv_burst` / `send_burst` on a worker-bound context call `rte_eth_rx/tx_burst` with that queue id.

### 4. RTC + soft round-robin (`workers > 1`, fallback)

```
NIC RXQ 0
    │
    ▼
Distributor lcore     rx_burst → RR index → worker ring[i]
    │
    ├── ring[0] → Worker 0: pop → handle → tx_burst(...)
    ├── ring[1] → Worker 1: …
    └── ring[N] → Worker N: …
```

**Why not one thread for RX+TX:** a single I/O core doing both becomes the bottleneck and leaves workers idle (“starvation” = empty rings). Distributor **only RX + enqueue**; workers **handle + TX**.

#### Dispatch: round-robin (not hash)

- Maintain `next_worker` (or burst-local cursor); assign packet `k` to `worker = (next++) % workers`.  
- **No** 5-tuple parse on the distributor hot path.  
- Worker set fixed for process lifetime; no dynamic rebalance.  
- **Affinity note:** RR does **not** keep a flow on one worker. Multi-worker soft path therefore **requires** a shared, concurrency-safe `SessionTable` (see Open issues). Do not assume per-worker sharded sessions under SoftRr.

#### TX under soft RR

| NIC TX queues | Policy |
|---------------|--------|
| `>= workers` | Worker `i` → `tx_queue i` (lock-free) |
| `== 1` | Worker TX via **spinlock** around `tx_burst` **or** optional shared TX drain (document; v1 prefer spinlock for less machinery) |

Init must log which TX policy applied.

#### Backpressure

- Distributor: if `worker_ring[i]` full → **drop** and free mbuf (same spirit as today’s RX ring full drop), counter++.  
- Do not block forever in distributor (avoids stalling all workers).  
- Optional v1 refinement: on full ring, try next worker once (still RR-flavored); default = drop on chosen ring full.

---

## `DpdkNetif` / Manager shape (no vtable)

Prefer **one public I/O façade** used by workers:

```cpp
// Conceptual hot path
unsigned recv_burst(...) {
  switch (mode_) {
  case DatapathMode::Pipeline: /* ring pop */ break;
  case DatapathMode::Rtc:      /* eth rx or soft ring pop */ break;
  }
}
```

Internal `union` holds only the active mode’s state (pipeline rings vs RTC queue id / soft rx ring ptr).

`DpdkNetifManager::init(DatapathConfig)`:

1. Probe device caps  
2. Resolve `RtcSteer::Auto`  
3. Configure ports (queue counts, RSS)  
4. Launch lcores: pipeline RX/TX; or single RTC worker (no extra I/O thread); or multi RTC workers + optional SoftRr distributor  

`VioletGW` today is single-threaded `run()`. Multi-worker RTC needs either:

- **v1:** Manager launches `workers` remote lcores each running a clone of the worker loop (callback / `VioletGW` shared tables — session concurrency is a **follow-up risk**), or  
- **v1 constrained:** netif layer first; `VioletGW::run` remains 1 worker until session/forward is multi-thread safe  

**Explicit phased delivery:**

| Phase | Deliverable |
|-------|-------------|
| **P0** | enum+union + gflags; Pipeline vs **single-worker RTC** (direct NIC, no distributor) |
| **P1** | RTC **HwRss** multi-queue + multi worker loops (session locking or shard — see Open issues) |
| **P2** | RTC **SoftRr** distributor + RR + per-worker rings + TX policy |

P0 unblocks structure; P1/P2 need a decision on `SessionTable` thread safety before claiming multi-worker production readiness. SoftRr especially needs **shared** sessions (RR breaks flow stickiness).

---

## gflags

| Flag | Default | Notes |
|------|---------|-------|
| `--datapath_mode` | `pipeline` | `pipeline` \| `rtc` |
| `--datapath_port_mask` | from `DPDK_vaild_port_marks` | |
| `--datapath_worker_port` | `0` | |
| `--io_ring_size` | `IO_RING_SIZE` | Pipeline |
| `--io_rx_lcore` / `--io_tx_lcore` | `true` | Pipeline |
| `--io_tx_ring_full_sleep_us` | `10` | Pipeline |
| `--rtc_steer` | `auto` | `auto` \| `hw_rss` \| `soft_rr` (ignored when `rtc_workers==1`) |
| `--rtc_workers` | `1` | `1` ⇒ direct RTC, no distributor |
| `--rtc_dist_ring_size` | `IO_RING_SIZE` or dedicated default | SoftRr only |
| `--rtc_tx_lock` | `auto` | **P2 planned** — SoftRr single-TXQ spinlock; not implemented in P0 |

### argv vs EAL

1. Parse gflags first (VGW flags).  
2. Pass remaining argv to `rte_eal_init`.  

Document in README; conflict with DPDK’s own `-` flags must be avoided (prefer long `--datapath_*` / `--rtc_*` / `--io_*` names).

CMake: FetchContent **gflags**, link into `vgw` / `vgw_dpdk_io` as appropriate.

---

## File layout (target)

```
src/dpdk/
  config.hpp
  datapath_config.hpp   # enums, DatapathConfig, flags→config
  netif.hpp / netif.cpp # façade + manager
  pipeline_io.cpp       # optional split: nic_recv/send, rings
  rtc_soft_steer.cpp    # optional: distributor loop + RR
```

EAL helpers may stay in `netif.cpp` or move to `eal.cpp` opportunistically.

---

## Testing

- Unit: flag parsing → `DatapathConfig`; `workers==1` resolves to DirectRtc.  
- Existing gtests / `init_services` unchanged.  
- Lab: `--datapath_mode=pipeline` (current); `--datapath_mode=rtc --rtc_workers=1`; later RSS / soft_rr on real NIC.  
- Soft RR: force `--rtc_steer=soft_rr --rtc_workers=2` even on multi-queue NIC to validate distributor path.

---

## Open issues (must resolve before P1/P2 coding)

1. **`SessionTable` / `UpstreamTable` concurrency** for multi-worker RTC — especially **SoftRr** (no flow affinity) needs a **shared** table + lock/RCU; HwRss may still prefer shard-by-queue if RSS sticky. Spec does not pick yet; **P1/P2 blocked** until chosen.  
2. SoftRr ring-full: drop vs try-next-worker — default **drop**.  
3. Single TXQ soft path: spinlock vs TX drain lcore — default **spinlock** for P2.

---

## Success criteria

- One binary; gflags select Pipeline vs RTC at init  
- No virtual functions on datapath hot path  
- `rtc_workers == 1` ⇒ direct single-core RTC (no distributor)  
- Soft multi-worker RTC uses **RX-only distributor + RR + worker TX**  
- Hw RSS used when `Auto`/`HwRss` and hardware allows; otherwise SoftRr  
- Default remains **Pipeline** so existing labs keep working  

---

## Spec self-review

- Single-worker simplification and RR (not hash) recorded in Decisions  
- Soft multi-worker I/O split still avoids single RX+TX bottleneck  
- Session affinity risk under RR called out in Open issues  
