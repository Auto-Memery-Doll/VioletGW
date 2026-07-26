# Client-side comparison: flow_gateway vs katran

Date: 2026-07-25  
Status: draft — pending user approval

## Problem with current comparison

The previous matrix compared:

- **flow_gateway Layer B** — sustained load with `client_sent` / `loss_rate_pct` (client-like)
- **katran `katran_tester -perf_testing`** — in-kernel microbench (`Mpps`, no loss, ~1s/run)

These are **not the same observability plane**. Katran numbers look 2–3× higher because they measure BPF syscall microbench, not what a client would see on the wire.

## Goal

Re-define cases and metrics from the **client (traffic generator) perspective**:

| Primary metric | Meaning |
|----------------|---------|
| **loss_rate_pct** | `(client_sent − client_received) / client_sent × 100` |
| **received_bps** | `client_received_pps × frame_bytes × 8` (goodput at client) |
| **offered_bps** | `client_sent_pps × frame_bytes × 8` |
| **received_pps** | Packets client considers successfully forwarded |
| **offered_pps** | Packets client attempted to inject |

### Secondary (diagnostics)

| Metric | flow_gateway | katran (wire client) |
|--------|--------------|----------------------|
| `ring_full` / backpressure | yes | N/A |
| `worker_drop` | yes | N/A (XDP drop stats if exposed) |
| `alloc_fail` | yes | N/A |
| `frame_bytes` | eth+IP+UDP+payload | same |
| `payload_bytes` | CLI `--pkt-size` | scapy payload len |
| `p99_us` (optional) | future: timestamp in mbuf | scapy send/sniff delta |

---

## What “client” means in each stack

### flow_gateway (WSL — available now)

`ring_stress` already implements a **synthetic client**:

```text
[Gen lcore]  client_sent++  ──► rx_ring ──► Forwarder ──► tx_ring
             ◄── drain tx_ring counts as client_received (forwarded_ok)
```

- **Client sent** = gen inject attempts (including ring_full drops)
- **Client received** = packets that completed forward path and reached tx_ring
- **30s wall clock** — sustained load
- Loss = ring backpressure + worker drop (today: essentially all `ring_full`)

This is the **canonical client model** for WSL.

### katran (not available as client-side perf today)

| Tool | Client? | Loss? | Bandwidth? | WSL |
|------|---------|-------|------------|-----|
| `katran_tester -perf_testing` | No (kernel test harness) | No | No (Mpps only) | sudo |
| `fplane_testing.py` | Yes (scapy send/sniff) | pass/fail only | No | needs L2 NIC |
| pktgen / MoonGen | Yes | Yes | Yes | bare metal |

For **fair client-side comparison**, katran needs a **wire or L2 client** (scapy rate generator on veth, or pktgen on bare metal). DSR semantics: client typically sees **one direction** unless a loopback backend reflects packets.

---

## Proposed case matrix (client-centric)

Cases are named by **what the client does**, not SUT-internal knobs.

### Dimensions

| Dimension | Values | Notes |
|-----------|--------|-------|
| **mode** | hot, multi, newflow, bidir | Same semantics as today |
| **payload** | 4, 512, 4096 bytes | UDP payload |
| **duration** | 30s measure + 5s warmup | Both sides when wire client exists |

### Drop ring-size as cross-project case axis

Ring size is **flow_gateway internal tuning**. From the client view:

- Client observes **loss_rate** and **received_bps** — ring size is a hidden cause, not a case label.
- Optional fg-only sweep: `STRESS_RING_SIZE=32|128|256` recorded as column, not separate case name.

### Standard case set (8 runs per SUT)

| Case ID | mode | payload | Client behavior |
|---------|------|---------|-----------------|
| C01 | hot | 4 | Single flow, max rate |
| C02 | multi | 4 | Rotate 1000 flows |
| C03 | newflow | 4 | New sport every packet |
| C04 | bidir | 4 | Fwd/rev alternate |
| C05 | hot | 512 | Single flow, larger frames |
| C06 | hot | 4096 | Single flow, jumbo-ish |
| C07 | newflow | 512 | New flow + medium payload |
| C08 | newflow | 4096 | New flow + large payload |

**Optional fg-only extension:** C01–C04 × ring `{32,128,256}` → diagnose backpressure.

### Unified CSV schema (client view)

```csv
timestamp,sut,case_id,mode,payload_bytes,seconds,warmup,flows,
client_sent,client_received,lost,loss_rate_pct,
offered_pps,received_pps,offered_bps,received_bps,frame_bytes,
ring_full,worker_drop,alloc_fail,ring_size
```

- `sut` = `flow_gateway` | `katran`
- Katran rows: `ring_*` columns empty; `ring_full` etc. empty or katran-specific drops if added later.

---

## Three approaches

### Approach A — WSL synthetic client (recommended first)

| SUT | Client implementation |
|-----|----------------------|
| flow_gateway | **Keep `ring_stress`** — extend CSV with bps columns |
| katran | **Pause cross-project PPS compare** until Layer C; document gap |

**Pros:** Honest metrics, no false comparison, minimal work.  
**Cons:** No katran numbers in short term.

### Approach B — WSL veth + scapy client for katran

Setup:

```text
veth0 (client scapy) ←→ veth1 (katran XDP + loopback real or reflector)
```

- New `tools/stress/client_scapy.py`: rate-limited send, sniff/count, same 8 cases.
- flow_gateway: still `ring_stress` OR later TAP attach.

**Pros:** True client metrics for katran on WSL.  
**Cons:** Katran install/attach complexity; DSR may need userspace reflector for `client_received`.

### Approach C — Bare metal wire client (production-like)

- pktgen-DPDK or MoonGen → NIC → SUT → loopback backend
- Both fg and katran in driver XDP / DPDK bind mode
- Add latency histogram (p50/p99)

**Pros:** Industry-standard LB benchmarking.  
**Cons:** Not WSL; large setup.

**Recommendation:** **A now** (fix fg client CSV + deprecate katran microbench compare), **B next** if katran on veth is acceptable, **C** for production sign-off.

---

## flow_gateway implementation (Approach A)

1. Extend `bench_common.hpp` summary line with `offered_bps`, `received_bps`, `frame_bytes`.
2. Update `run_ab.sh` CSV header and parsing.
3. Replace `run_katran_bench.sh` / `compare_fg_katran.py` with `run_client_matrix.sh` + optional katran stub.
4. Rename output: `docs/client-results_flow_gateway.csv`.
5. Canvas: loss_rate + received_bps by case_id (no katran until B/C).

## katran implementation (Approach B sketch)

1. Script: `tools/stress/katran_veth_setup.sh` — create veth, attach katran BPF, add VIP/reals.
2. `tools/stress/client_scapy_bench.py` — mirror 8 cases, output same CSV schema.
3. `tools/stress/compare_client.py` — merge fg + katran on `case_id`.

---

## What we stop doing

- **Do not** compare `katran_tester Mpps` to `ring_stress fwd_pps` as “performance”.
- **Do not** use ring size as katran case label (meaningless for BPF microbench).
- **Do not** use mismatched fixture indices as “hot/multi”.

---

## Open decision

Which approach do you want to implement first?

1. **A** — fg client CSV only, katran comparison deferred (fast, honest)
2. **B** — add katran scapy client on veth (more work, both on WSL)
3. **A + optional fg ring sweep** — 8 cases × 3 ring sizes for fg only
