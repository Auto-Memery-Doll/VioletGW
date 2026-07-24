# UpstreamTable + dataplane worker

Date: 2026-07-24  
Status: approved (conversation); revised lock-free pick

## Goal

Wire the UDP L4 forward path end-to-end on the data plane:

`NIC RX → Forwarder::handle → TX / free`

Upstream membership is a **control-plane-owned table** living in the C++ process for now (constants seed it). Health checks / heartbeat stay **out of the data plane**; a future Go control plane will call into (or replace) the setter API.

## Non-goals

- Data-plane heartbeat / `HeartbeatMonitor`
- `shared_mutex` (or any lock) on the pick / balance hot path
- Wiring or deleting legacy `balance::Engine` (left unused this step)
- Go control-plane process or IPC protocol
- Multi-queue RSS / multi-port load split
- Dynamic VIP / MAC from runtime config files

## Architecture

```
config.hpp (seed) / future Go CP
        │  publish (rare)
        ▼
 UpstreamTable
   policy: atomic<BalancePolicy>     ← CP writes; workers read
   nodes:  atomic shared_ptr to      ← CP swaps whole immutable snapshot
           immutable Endpoint[]
        │
        │  pick(FlowKey): load policy + load snapshot, no lock
        ▼
 Forwarder(UpstreamPicker) ← SessionTable
        ▲
 main worker: recv_burst → handle → send_burst | free_burst
```

### Lock-free control vs data plane

| Role | Behavior |
|------|----------|
| Control plane | Rarely `set_policy(p)` and/or `set(endpoints)`; publishes new values |
| Data plane worker | `pick` only: load policy + load node snapshot; run chosen algorithm; **no mutex** |

**This step:** use `std::atomic` for policy and for the snapshot pointer (`memory_order_relaxed` on the hot-path loads is enough). Relaxed atomics are cheap on x86; they are not expected to be a bottleneck vs parse/NAT/checksum.

**If atomics ever show up in profiles:** policy may be demoted to a plain `BalancePolicy` / `uint8_t` “constant” written by the single control-plane publisher. Per-packet / per-worker seeing a slightly stale algorithm or a slightly different pick is **acceptable** — short-lived load skew across workers does not threaten cluster stability. Node-list publish should still avoid tearing the pointer itself (keep an atomic/RCU pointer swap, or accept a documented single-writer plain pointer only if measured necessary).

**Brief inconsistency is acceptable:** one worker may still see old policy or old node list for a few packets while another sees the new publish. All algorithms share the **same** node snapshot; only the selection function changes. Existing `Session` entries are untouched, so established flows stay sticky.

No `EngineState::switching`, no rwlock, no per-algorithm object swap like old `balance::Engine`.

### Module: `src/upstream/`

| Type | Role |
|------|------|
| `UpstreamEndpoint` | `uint32_t ip_be`, `uint16_t port` (host-order port; same shape as `forward::Upstream`) |
| `BalancePolicy` | `mod` (default), `rr` — stored as `std::atomic<BalancePolicy>` (or `uint8_t`) |
| `UpstreamTable` | Publish API for CP; lock-free `pick` for workers |

**API**

- `void set(std::vector<UpstreamEndpoint> endpoints)` — build immutable snapshot, `atomic_store` / `shared_ptr` exchange (release); old snapshot freed when last reader drops ref
- `void set_policy(BalancePolicy p)` — `policy_.store(p, memory_order_release)`
- `bool pick(const session::FlowKey& key, forward::Upstream* out) const` — `load` policy + snapshot (`acquire`); empty → false; **no lock**
- `size_t size() const` — load snapshot and return length (non-hot helpers OK)

Optional later: `set(endpoints, policy)` as one CP convenience; not required for correctness given accepted inconsistency.

**Pick rules (same node list)**

- Load `n = snapshot->size()`; if `n == 0` → false
- `mod`: `hash(client_ip, client_port) % n`
- `rr`: per-worker (or process-local atomic) counter `% n` — **not** protected by the removed table mutex; a process-wide `atomic<uint64_t>` RR index is fine and stays lock-free

Hash: fold client IP + port into `uint32_t` (simple mix / small-buffer crc). Do **not** call string-IP `balance` code.

**Forwarder integration**

`UpstreamPicker` closes over `UpstreamTable*` and calls `pick`. NAT / session semantics unchanged.

### Config seed (`config.hpp`)

Dev defaults (placeholders OK):

- VIP IP/port
- Gateway IP + MAC
- Client-side next-hop MAC, upstream next-hop MAC
- Initial upstream list
- Default `BalancePolicy`
- Session idle timeout / expire interval (ms)

Compile-time seed for lab; Go CP later replaces `set` / `set_policy`.

### `main` worker

1. `init_logging` → DPDK EAL → `dpdk_netif_mg()->init()`
2. Construct `SessionTable`, `UpstreamTable` (seed from config), `ForwardConfig`, `Forwarder`
3. Loop on port 0 (or first netif):
   - `recv_burst`
   - for each mbuf: `handle(m, now_ms)`; `drop` → `free_burst`; `tx_*` → `send_burst`
   - periodically `sessions.expire(now_ms)`
4. `now_ms`: `std::chrono` monotonic ms for this step

### Tests

`test/upstream/upstream_test.cpp` (gtest):

- empty → `pick` false
- `set` then `pick` returns a member
- `mod` same key → same endpoint for fixed list
- `set_policy` changes algorithm without requiring a lock; same list used
- `set` replace updates membership for subsequent picks

### CMake

- `add_subdirectory(upstream)`; `flow_gw` links `upstream`
- `test/` registers upstream gtests

## Error handling

| Case | Behavior |
|------|----------|
| Empty snapshot | new VIP flows drop |
| Existing session | session map (independent of table / policy) |
| Parse / non-VIP / unknown reverse | drop + free |
| Mid-update tear | at most stale policy or stale list for a short window; OK by design |

## Future (out of scope)

- Go CP publishing policy + node list into these atomics
- Remove legacy `src/balance/`
- Weights / consistent hash as additional `BalancePolicy` values on the same snapshot

## Success criteria

- Unit tests for `UpstreamTable` pass under `ctest -L unit`
- `flow_gw` builds with worker loop linked
- No mutex on `pick`; no heartbeat threads on data path
