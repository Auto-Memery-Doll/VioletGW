# Lab Finding: Offer Sweep, Virtual Soft-Switch Contention, and TX Rings

**Date:** 2026-07-29 (expanded review notes)  
**Chinese version:** [2026-07-29-offer-sweep-softswitch-contention.zh.md](./2026-07-29-offer-sweep-softswitch-contention.zh.md)  
**Related quantitative report:** [rtc_M01_offer_sweep/REPORT.md](../perf/rtc_M01_offer_sweep/REPORT.md)  
**Scope:** Background on soft switches / scheduling, plus what we learned on RTC M01 offer sweeps — so this note is useful for later review, not only as a lab diary.

---

## 0. How to use this note

| Section | Purpose |
|---------|---------|
| §§1–5 | **Background** — soft switch, scheduling, queues, virt NIC rings (study material) |
| §§6–10 | **This lab** — topology, causal chain, flame interpretation |
| §§11–12 | Next experiments + reading list |

---

## 1. What is a “soft switch”?

A **software switch** (soft switch / vSwitch) is a Layer-2 forwarding engine implemented in software. It plays the same *logical* role as a physical Ethernet switch:

1. Learn (or be told) MAC → port mappings.  
2. Receive a frame on an **ingress port**.  
3. Decide an **egress port** (unicast hit, flood, drop).  
4. Copy / move the frame to that port’s transmit path.

Differences from a hardware ASIC switch:

| | Hardware switch | Soft switch |
|--|-----------------|-------------|
| Forwarding engine | Dedicated silicon, often line-rate | Host CPUs (and maybe host NIC DMA) |
| Ports | PHY / ASIC ports | vNIC backends, tap, vhost, physical uplinks |
| Capacity | Designed for wire PPS/bps | Limited by **CPU cycles, cache, locks, copies** |
| Isolation | Per-port queues in hardware | Shared host resources unless carefully partitioned |

In VMware Workstation / Fusion (this lab), guest NICs attach to named virtual networks (**VMnet0, VMnet1, …**). Each VMnet is effectively a **virtual switch** that can also attach:

- a host virtual adapter (what Windows shows as “VMware Network Adapter VMnet8”),  
- a **NAT** device, and/or  
- a **bridge** onto a physical NIC (WLAN / Ethernet).

Official overview: [Understanding Virtual Networking Components (Workstation)](https://techdocs.broadcom.com/us/en/vmware-cis/desktop-hypervisors/workstation-pro/26H1/using-vmware-workstation-pro/configuring-network-connections/understanding-virtual-networking-components.html).

**Important for this lab:** when pktgen and vgw talk on the same VMnet, frames are forwarded **inside the soft switch**. They need not leave the host on Wi‑Fi. The bottleneck is still real: soft-switch **processing capacity**.

---

## 2. Packet path layers (guest → soft switch → peer)

Think in four layers:

```text
┌──────────────────────────────────────────────────────────┐
│ Guest userspace (DPDK pktgen / vgw)                      │
│   rte_eth_rx_burst / rte_eth_tx_burst                    │
├──────────────────────────────────────────────────────────┤
│ Guest virt NIC frontend (vmxnet3 PCI device)             │
│   RX ring + TX ring in guest memory (shared with backend)│
├──────────────────────────────────────────────────────────┤
│ Hypervisor / host backend (“soft switch” + device model) │
│   consume TX descriptors → L2 forward → fill peer RX     │
├──────────────────────────────────────────────────────────┤
│ Optional: host physical NIC (only if bridged / NATed out)│
└──────────────────────────────────────────────────────────┘
```

Guest **PCI exclusivity** (two BDF addresses) only means each vNIC has its own frontend.  
**Forwarding between them still shares the backend** in the middle box.

Classic paper on *hosted* VMware I/O (Workstation-style world switches, VMNet driver):  
Sugerman et al., *Virtualizing I/O Devices on VMware Workstation’s Hosted Virtual Machine Monitor* (USENIX 2001) — [PDF](https://pages.cs.wisc.edu/~remzi/Classes/838/Spring2013/Papers/usenix_io_devices.pdf).  
Even though modern paths use paravirtual vmxnet3 and fewer heavy world switches, the **idea remains**: guest TX/RX is mediated by host-side networking code with finite CPU budget.

---

## 3. Soft-switch “scheduling” — what actually gets scheduled

People ask: *“Is the soft switch serial on TX/RX?”*  
Answer: **not a half-duplex mutex**, but **shared-resource multiplexing**.

### 3.1 What must be scheduled

Any soft switch repeatedly does work units roughly like:

```text
while (work available and CPU budget remains):
    pull batch from some ingress source   // vNIC TX notify, host tap, phy RX
    classify / lookup MAC table
    enqueue toward egress port(s)
    push batch into egress vNIC RX rings / phy TX
```

That loop (or a set of worker threads running similar loops) is where **scheduling** shows up:

| Mechanism | Meaning |
|-----------|---------|
| **Time multiplexing** | One host core alternates among ports/flows (run-to-completion or short quanta) |
| **Multi-worker** | Several cores each own some RXQs/ports (common in OVS-DPDK PMD threads) |
| **Queueing** | When producers outrun consumers, packets wait in rings/FIFOs or are dropped |
| **Notification** | Kick / interrupt / eventfd to wake a sleeping backend (or poll forever) |

Workstation’s exact internal scheduler is proprietary; the **mental model** that matches measurements is:

> A finite set of host CPUs executes forwarding work for **all** ports on that VMnet.  
> More TX from pktgen ⇒ more work units of type “pktgen→vgw”.  
> Fewer CPU slices left for “vgw→pktgen” ⇒ that egress drains slower.

### 3.2 Run-to-completion vs pipeline (useful vocabulary)

Soft switches often use **run-to-completion** for the fast path: one thread takes a packet from ingress all the way to egress enqueue (Open vSwitch documents this for its fast path). That is still **not** “only one direction globally”; it means **one packet** is handled without softirq ping-pong — but many packets from many ports still **share the same workers**.

Contrast **pipeline** stages (RX thread → worker → TX thread) as in some DPDK apps: stages communicate via rings; backpressure appears as ring fullness — same *symptoms* as our TX-ring stall.

OVS-DPDK reference for PMD thread polling / assignment:  
[PMD Threads](https://docs.openvswitch.org/en/stable/topics/dpdk/pmd/).

### 3.3 Fairness is usually weak under overload

Under overload, soft switches rarely give perfect per-port fairness. Typical behaviors:

- **Aggressive producer wins** short-term (fills queues facing itself).  
- **Egress to a busy peer** slows (peer RX ring full → cannot complete TX).  
- **Drop tails** at the fullest queue (or silent drop in virt backend).

That matches “raise pktgen offer → vgw TX looks stuck”: pktgen is the aggressive producer on a shared fabric.

---

## 4. Queues, rings, and backpressure (review core)

### 4.1 Producer / consumer

```text
Producer (guest TX)          Consumer (soft switch / peer)
     │                              │
     ▼                              ▼
  fill TX descriptors  ──notify──►  read descriptors, forward frames
     │                              │
     ◄──── completion / reclaim ────┘
          (free slots + mbufs)
```

If the consumer is slower than the producer, the **TX ring fills**.  
Further `tx_burst` calls cannot post new packets until completions free slots.

### 4.2 Independent RX and TX rings

A modern virt NIC exposes **separate** RX and TX rings:

- RX: backend → guest (software polls with `rx_burst`).  
- TX: guest → backend (software posts with `tx_burst`).

The device can advance both; **congestion on TX does not mean “NIC is in TX-only mode and cannot RX.”**  
RX may still run; under fabric overload, RX *volume* into the guest can still fall because frames never arrive.

### 4.3 Head-of-line and shared memory fabrics (intuition)

Physical switches often use **shared-memory** or **crossbar + VOQs** to reduce head-of-line (HOL) blocking. Soft switches on a laptop typically have:

- shallow per-port rings,  
- shared CPU,  
- occasional coarse locks,

so **HOL-like effects** appear: one hot flow can delay unrelated work that needs the same worker or lock.

You do not need ASIC-level detail for this lab; remember: **shared CPU + shallow queues ⇒ cross-traffic interference.**

### 4.4 Backpressure vs drop

| Policy | Guest symptom |
|--------|----------------|
| Backpressure (TX ring full) | `tx_burst` returns 0 / partial; CPU spins reclaiming |
| Drop in switch | End-to-end loss; TX may still complete (frame already “sent” into switch then dropped) |
| Drop before guest RX | `rx_burst` sees fewer packets; RX CPU share shrinks |

Our flames (TX CPU ↑) emphasize **backpressure at vgw TX**. CSV loss emphasizes **end-to-end drops** somewhere on the path (switch, peer RX, or app). Both can coexist.

---

## 5. Virt NIC: vmxnet3 and DPDK TX semantics

### 5.1 Emulated vs paravirtual

| Type | Example | Cost |
|------|---------|------|
| Emulated | e1000 / e1000e | Many VM exits; host emulates register I/O |
| Paravirtual | **vmxnet3** | Shared rings + lighter notifications; still host CPU for forwarding |

vmxnet3 is better than e1000, but **advertised 10 Gbps ≠ achievable small-packet PPS** on Workstation. Host CPU still does soft-switch work.

### 5.2 `rte_eth_tx_burst` contract (study carefully)

From DPDK PMD docs:

- Burst API posts packets into the TX queue.  
- Return value = how many were **accepted into the driver/ring**, not “DMA finished / peer received”.  
- Drivers reclaim completed descriptors in bulk (e.g. after `tx_free_thresh`) to amortize cost.  
- Application may also call cleanup APIs to force reclaim.

Useful links:

- [Poll Mode Driver guide](https://doc.dpdk.org/guides/prog_guide/poll_mode_drv.html) (`tx_free_thresh`, RS bit, reclaim)  
- [Discussion: tx_burst return ≈ “consumed”, not “on wire”](https://inbox.dpdk.org/dev/20260219110049.60743444@phoenix.local/T/)

### 5.3 Why reclaim stalls under soft-switch overload

Completions arrive only after the **backend** has finished with the descriptor (copied/forwarded far enough to release ownership).  
If the soft switch is busy on other ports’ traffic, completions for *this* port slow down → ring stays full → guest `tx_burst` burns cycles.

---

## 6. This lab’s topology

```text
Windows host
  WLAN ~144 Mbps (up); Realtek Ethernet disconnected
  VMnet1 / VMnet8 = host-side VMware vNICs (advertised 100 Mbps ≠ guest PPS limit)
  VMware soft switch (VMnet connecting the two guest vNICs)
       │
       ├── Guest ens256 (0000:1b:00.0, vmxnet3, DPDK) = pktgen
       └── Guest ens192 (0000:0b:00.0, vmxnet3, DPDK) = vgw
```

Stress design (`tools/pktgen/env.sh`):

- **Client = upstream** (self-loop).  
- Minimum soft-switch crossings per successful forward:

```text
① pktgen TX → soft switch → vgw RX
② vgw TX    → soft switch → pktgen RX
```

Self-loop **doubles** fabric load relative to a one-way client→gateway→separate-upstream design.

---

## 7. Contention logic chain (canonical for this experiment)

```text
Raise PKTGEN_OFFER_SCALE
        ↓
pktgen posts more TX work into the soft switch
        ↓
Shared soft-switch CPU/queue budget is consumed by ①
        ↓
Work for ② (vgw→pktgen) is delayed (multiplexed scheduling, not half-duplex lock)
        ↓
vgw TX ring: completions scarce → free slots scarce
        ↓
tx_burst spends time reclaiming / returning 0
        ↓
Flame: send_burst ↑ ; CSV: recv ↓, loss ↑
```

**Capacity floor (A)** — vmxnet3 + Workstation PPS is modest even alone.  
**Contention (B)** — self-loop multi-TX sharing explains **goodput falling as offer rises** (collapse, not only plateau).  
See numbers/charts: [REPORT.md](../perf/rtc_M01_offer_sweep/REPORT.md).

---

## 8. Full duplex vs TX contention (concept check)

| Statement | Verdict |
|-----------|---------|
| Full duplex = A→B and B→A can proceed together on one link | True |
| Full duplex = many TX sources into one fabric never queue | **False** |
| Soft switch “serial TX/RX” like half-duplex PHY | **False** as a model |
| Soft switch = shared workers + queues ⇒ cross-traffic slowdown | **True** |

Road analogy: two-way street (duplex) vs many cars merging onto one on-ramp (shared TX/forwarding budget).

---

## 9. Flame asymmetry: `send_burst` ≫ `recv_burst`

| Path | Under overload |
|------|----------------|
| TX | App keeps trying to send; waits on ring reclaim → **high CPU in send_burst** |
| RX | Frames may never reach guest; empty poll is cheap → **low CPU share** |
| `handle` | Little time left / few packets fully processed → **&lt;2%** in this sweep |

Low-offer `now_ms` dominance was **idle busy-poll**, not the high-offer loss mechanism. Timers/`rte_timer` remain optional hygiene; they do not explain the loss cliff.

---

## 10. Reading the Windows host adapter list

- Guest vmxnet3 **do not** appear in host `Get-NetAdapter`.  
- Host VMnet adapters’ **100 Mbps** label is not the soft-switch PPS ceiling.  
- Guest **10G** link speed is nominal.  
- Wi-Fi-only host: bridged paths are weak; same-VMnet VM↔VM can stay on-host and still congest the soft switch.

---

## 11. Next experiments (to separate A vs B)

1. Move upstream off pktgen (`ens224` kernel echo, or second VM).  
2. Put client and gateway on **different VMnets** / hosts if possible.  
3. Re-run the same offer sweep; compare loss, recv Mpps, `send_burst` share to [REPORT.md](../perf/rtc_M01_offer_sweep/REPORT.md).  
4. Only then revisit datapath micro-opts (timers, burst sizes, etc.).

---

## 12. Further reading (deeper than the first pass)

### Soft switch / virtual networking

| Resource | Focus |
|----------|--------|
| [Workstation: virtual networking components](https://techdocs.broadcom.com/us/en/vmware-cis/desktop-hypervisors/workstation-pro/26H1/using-vmware-workstation-pro/configuring-network-connections/understanding-virtual-networking-components.html) | VMnet, DHCP, NAT, host adapters |
| Sugerman et al. USENIX 2001 ([PDF](https://pages.cs.wisc.edu/~remzi/Classes/838/Spring2013/Papers/usenix_io_devices.pdf)) | Hosted VMM I/O path, NIC virtualization overhead |
| [Open vSwitch FAQ / design](https://docs.openvswitch.org/en/latest/faq/design/) | Fast path vs slow path, run-to-completion |
| [OVS-DPDK PMD threads](https://docs.openvswitch.org/en/stable/topics/dpdk/pmd/) | How a *high-performance* soft switch schedules poll workers |
| Pfaff et al., *The Design and Implementation of Open vSwitch* (NSDI’15) | Production soft-switch architecture (contrast to Workstation) |

### DPDK rings / timers / vmxnet3

| Resource | Focus |
|----------|--------|
| [PMD / ethdev TX path](https://doc.dpdk.org/guides/prog_guide/poll_mode_drv.html) | `tx_burst`, reclaim thresholds |
| [Timer library](https://doc.dpdk.org/guides/prog_guide/timer_lib.html) | Userspace timers; `next_ticks` |
| [vmxnet3 PMD](https://doc.dpdk.org/guides/nics/vmxnet3.html) | Driver specifics |
| [VPP vmxnet3 usecase](https://fdio-vpp.readthedocs.io/en/latest/usecases/vmxnet3.html) | Workstation vs ESXi expectations |

### Performance measurement

| Resource | Focus |
|----------|--------|
| [CPU Flame Graphs (Gregg)](https://www.brendangregg.com/FlameGraphs/cpuflamegraphs.html) | Interpreting shares on a busy-polled core |
| In-repo [offer-sweep REPORT](../perf/rtc_M01_offer_sweep/REPORT.md) | Our numbers + SVG charts |

### In-repo artifacts

| Path | Content |
|------|---------|
| `tools/pktgen/out/results_rtc_M01_s*.csv` | Raw CSV |
| `tools/pktgen/out/perf/rtc_M01_s*/flame.svg` | Per-scale flames |
| `tools/pktgen/README.md`, `env.sh` | NIC roles, self-loop |

---

## 13. One-paragraph takeaway

A soft switch is a shared, CPU-backed L2 forwarder: it multiplexes work across ports with queues and finite workers—not a half-duplex “TX or RX exclusive” lock. In this lab, pktgen and vgw share one VMnet fabric with a self-looped upstream, so raising offer spends fabric budget on client→gw traffic, slows gw→client TX completions, fills vgw’s TX ring, and shows up as `send_burst`-dominated flames plus rising loss. Study soft-switch scheduling/queuing and DPDK TX reclaim to interpret the next topology experiments; do not start from idle-path `now_ms` optimization.
