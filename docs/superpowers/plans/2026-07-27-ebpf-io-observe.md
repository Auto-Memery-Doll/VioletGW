# eBPF NIC I/O Observe Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add BCC + C BPF tools under `tools/ebpf/` that count NIC RX/TX packets for RTC and Pipeline modes via uretprobe return values.

**Architecture:** BPF programs in C (`.bpf.c`); short Python BCC loaders attach uretprobes. RTC attaches to `DpdkNetif::recv_burst` / `send_burst`. Pipeline attaches to `dpdk::rx_burst` / `tx_burst` (I/O lcores only). Both print 1 Hz cumulative + pps and a Ctrl-C summary.

**Tech Stack:** BCC (`python3-bpfcc`), ELF symbols on `build/bin/Src/vgw` (must not be stripped).

**Status:** Implemented in-tree (BCC+C). bpftrace approach superseded.

## Global Constraints

- English only in project files (no Chinese in scripts/README).
- Do not modify `src/` or gateway behavior.
- Probe symbols use **mangled** names for reliability (demangled aliases documented in README).
- Default binary path: `build/bin/Src/vgw` (run bpftrace from repo root, or pass `-p <pid>` with cwd at repo root so the relative path resolves).
- Counts: add `retval` only when `retval > 0`.
- Commits: only when the user explicitly asks (do not auto-commit).

**Spec:** `docs/superpowers/specs/2026-07-27-ebpf-io-observe-design.md`

---

## File Structure

| File | Responsibility |
|------|----------------|
| `tools/ebpf/rtc_io.bt` | RTC NIC RX/TX uretprobes + 1s print + END summary |
| `tools/ebpf/pipeline_io.bt` | Pipeline NIC RX/TX uretprobes + 1s print + END summary |
| `tools/ebpf/README.md` | Usage, symbols, mode mapping, caveats |

---

### Task 1: RTC bpftrace script

**Files:**
- Create: `tools/ebpf/rtc_io.bt`

**Interfaces:**
- Consumes: symbols `_ZN3vgw9DpdkNetif10recv_burstEPP8rte_mbufj`, `_ZN3vgw9DpdkNetif10send_burstEPP8rte_mbufj` in `build/bin/Src/vgw`
- Produces: stdout lines prefixed `[rtc]`

- [ ] **Step 1: Confirm symbols exist**

```bash
nm /home/violet/VioletGW/build/bin/Src/vgw | \
  grep -E '_ZN3vgw9DpdkNetif10(recv|send)_burst'
```

Expected:

```text
... T _ZN3vgw9DpdkNetif10recv_burstEPP8rte_mbufj
... T _ZN3vgw9DpdkNetif10send_burstEPP8rte_mbufj
```

- [ ] **Step 2: Write `tools/ebpf/rtc_io.bt`**

```bpftrace
#!/usr/bin/env bpftrace
/*
 * RTC NIC I/O observe: uretprobe DpdkNetif::recv_burst / send_burst.
 * Run from repo root:
 *   sudo bpftrace tools/ebpf/rtc_io.bt -p $(pgrep -n vgw)
 */

BEGIN
{
  @rx = 0;
  @tx = 0;
  @rx_calls = 0;
  @tx_calls = 0;
  @prev_rx = 0;
  @prev_tx = 0;
  @start_ns = nsecs;
  printf("[rtc] attached (recv_burst/send_burst); Ctrl-C to stop\n");
}

uretprobe:build/bin/Src/vgw:_ZN3vgw9DpdkNetif10recv_burstEPP8rte_mbufj
{
  if (retval > 0) {
    @rx += retval;
    @rx_calls += 1;
  }
}

uretprobe:build/bin/Src/vgw:_ZN3vgw9DpdkNetif10send_burstEPP8rte_mbufj
{
  if (retval > 0) {
    @tx += retval;
    @tx_calls += 1;
  }
}

interval:s:1
{
  $rx = @rx;
  $tx = @tx;
  $rx_pps = $rx - @prev_rx;
  $tx_pps = $tx - @prev_tx;
  printf("[rtc] rx=%lu tx=%lu rx_pps=%lu tx_pps=%lu\n",
         $rx, $tx, $rx_pps, $tx_pps);
  @prev_rx = $rx;
  @prev_tx = $tx;
}

END
{
  $elapsed_s = (nsecs - @start_ns) / 1000000000;
  if ($elapsed_s == 0) {
    $elapsed_s = 1;
  }
  printf("[rtc] done elapsed_s=%llu rx=%lu tx=%lu avg_rx_pps=%llu avg_tx_pps=%llu rx_calls=%lu tx_calls=%lu\n",
         $elapsed_s, @rx, @tx, @rx / $elapsed_s, @tx / $elapsed_s,
         @rx_calls, @tx_calls);
  clear(@rx);
  clear(@tx);
  clear(@rx_calls);
  clear(@tx_calls);
  clear(@prev_rx);
  clear(@prev_tx);
  clear(@start_ns);
}
```

- [ ] **Step 3: Syntax / attach dry-run (needs root)**

From repo root, with `vgw` running in RTC mode (or at least the binary present):

```bash
cd /home/violet/VioletGW
sudo bpftrace --dry-run tools/ebpf/rtc_io.bt
```

Expected: attaches probes then exits with no parse/attach errors. If no process is required for dry-run attach to the ELF path, success is enough. If attach fails because relative path is wrong, re-run from repo root.

Optional live check (RTC vgw + traffic):

```bash
sudo bpftrace tools/ebpf/rtc_io.bt -p $(pgrep -n vgw)
```

Expected within a few seconds under load: non-zero `rx_pps` / `tx_pps`.

---

### Task 2: Pipeline bpftrace script

**Files:**
- Create: `tools/ebpf/pipeline_io.bt`

**Interfaces:**
- Consumes: symbols `_ZN3vgw4dpdk8rx_burstEttPP8rte_mbuft`, `_ZN3vgw4dpdk8tx_burstEttPP8rte_mbuft` in `build/bin/Src/vgw`
- Produces: stdout lines prefixed `[pipeline]`

- [ ] **Step 1: Confirm symbols exist**

```bash
nm /home/violet/VioletGW/build/bin/Src/vgw | \
  grep -E '_ZN3vgw4dpdk8(rx|tx)_burst'
```

Expected:

```text
... T _ZN3vgw4dpdk8rx_burstEttPP8rte_mbuft
... T _ZN3vgw4dpdk8tx_burstEttPP8rte_mbuft
```

- [ ] **Step 2: Write `tools/ebpf/pipeline_io.bt`**

```bpftrace
#!/usr/bin/env bpftrace
/*
 * Pipeline NIC I/O observe: uretprobe dpdk::rx_burst / tx_burst
 * (called from nic_recv / nic_send I/O lcores only).
 * Run from repo root:
 *   sudo bpftrace tools/ebpf/pipeline_io.bt -p $(pgrep -n vgw)
 */

BEGIN
{
  @rx = 0;
  @tx = 0;
  @rx_calls = 0;
  @tx_calls = 0;
  @prev_rx = 0;
  @prev_tx = 0;
  @start_ns = nsecs;
  printf("[pipeline] attached (dpdk::rx_burst/tx_burst); Ctrl-C to stop\n");
}

uretprobe:build/bin/Src/vgw:_ZN3vgw4dpdk8rx_burstEttPP8rte_mbuft
{
  if (retval > 0) {
    @rx += retval;
    @rx_calls += 1;
  }
}

uretprobe:build/bin/Src/vgw:_ZN3vgw4dpdk8tx_burstEttPP8rte_mbuft
{
  if (retval > 0) {
    @tx += retval;
    @tx_calls += 1;
  }
}

interval:s:1
{
  $rx = @rx;
  $tx = @tx;
  $rx_pps = $rx - @prev_rx;
  $tx_pps = $tx - @prev_tx;
  printf("[pipeline] rx=%lu tx=%lu rx_pps=%lu tx_pps=%lu\n",
         $rx, $tx, $rx_pps, $tx_pps);
  @prev_rx = $rx;
  @prev_tx = $tx;
}

END
{
  $elapsed_s = (nsecs - @start_ns) / 1000000000;
  if ($elapsed_s == 0) {
    $elapsed_s = 1;
  }
  printf("[pipeline] done elapsed_s=%llu rx=%lu tx=%lu avg_rx_pps=%llu avg_tx_pps=%llu rx_calls=%lu tx_calls=%lu\n",
         $elapsed_s, @rx, @tx, @rx / $elapsed_s, @tx / $elapsed_s,
         @rx_calls, @tx_calls);
  clear(@rx);
  clear(@tx);
  clear(@rx_calls);
  clear(@tx_calls);
  clear(@prev_rx);
  clear(@prev_tx);
  clear(@start_ns);
}
```

- [ ] **Step 3: Syntax / attach dry-run (needs root)**

```bash
cd /home/violet/VioletGW
sudo bpftrace --dry-run tools/ebpf/pipeline_io.bt
```

Expected: no parse/attach errors.

Optional live check (Pipeline vgw + traffic):

```bash
sudo bpftrace tools/ebpf/pipeline_io.bt -p $(pgrep -n vgw)
```

Expected under load: non-zero `rx_pps` / `tx_pps`.

---

### Task 3: README

**Files:**
- Create: `tools/ebpf/README.md`

**Interfaces:**
- Documents Task 1–2 scripts and symbol table from the spec

- [ ] **Step 1: Write `tools/ebpf/README.md`**

```markdown
# eBPF NIC I/O observe

bpftrace scripts that count **NIC-level** RX/TX packets for VioletGW datapath modes.
Counts use uretprobe **return values** (packets per burst), not call counts.

## Requirements

- root (`sudo`)
- `bpftrace` installed
- `build/bin/Src/vgw` built **with symbols** (not stripped)
- run `bpftrace` from the **repo root** so `build/bin/Src/vgw` resolves

## Scripts

| Mode | Script | Probes |
|------|--------|--------|
| RTC (`--datapath_mode=rtc`) | `rtc_io.bt` | `DpdkNetif::recv_burst` / `send_burst` |
| Pipeline (`--datapath_mode=pipeline`) | `pipeline_io.bt` | `dpdk::rx_burst` / `tx_burst` (from `nic_recv` / `nic_send`) |

Mangled symbols (stable for attach):

| Demangled | Mangled |
|-----------|---------|
| `vgw::DpdkNetif::recv_burst` | `_ZN3vgw9DpdkNetif10recv_burstEPP8rte_mbufj` |
| `vgw::DpdkNetif::send_burst` | `_ZN3vgw9DpdkNetif10send_burstEPP8rte_mbufj` |
| `vgw::dpdk::rx_burst` | `_ZN3vgw4dpdk8rx_burstEttPP8rte_mbuft` |
| `vgw::dpdk::tx_burst` | `_ZN3vgw4dpdk8tx_burstEttPP8rte_mbuft` |

## Usage

```bash
# Terminal A: start vgw in the matching mode
sudo ./build/bin/Src/vgw --datapath_mode=rtc --rtc_workers=1 -l 0
# or
sudo ./build/bin/Src/vgw --datapath_mode=pipeline -l 0-2

# Terminal B: from repo root
sudo bpftrace tools/ebpf/rtc_io.bt -p $(pgrep -n vgw)
sudo bpftrace tools/ebpf/pipeline_io.bt -p $(pgrep -n vgw)
```

Dry-run attach check:

```bash
sudo bpftrace --dry-run tools/ebpf/rtc_io.bt
sudo bpftrace --dry-run tools/ebpf/pipeline_io.bt
```

## Output

Every second:

```text
[rtc] rx=<cum> tx=<cum> rx_pps=<n> tx_pps=<n>
```

On Ctrl-C:

```text
[rtc] done elapsed_s=<n> rx=<cum> tx=<cum> avg_rx_pps=<n> avg_tx_pps=<n> rx_calls=<n> tx_calls=<n>
```

## Notes

- Pick the script that matches `--datapath_mode`. Wrong pairing may under-count or miss NIC I/O.
- Pipeline worker ring path is intentionally not probed.
- Uprobes add overhead; treat pps as approximate observability, not a substitute for DPDK/NIC port stats at max load.
- After rebuild/strip, re-check symbols with `nm -C build/bin/Src/vgw | grep burst`.
```

- [ ] **Step 2: Sanity-check files exist**

```bash
ls -la /home/violet/VioletGW/tools/ebpf/
```

Expected: `rtc_io.bt`, `pipeline_io.bt`, `README.md`.

---

## Spec coverage (self-review)

| Spec requirement | Task |
|------------------|------|
| `rtc_io.bt` with recv/send_burst | Task 1 |
| `pipeline_io.bt` with dpdk rx/tx_burst | Task 2 |
| README usage/symbols/caveats | Task 3 |
| 1s cumulative + pps | Tasks 1–2 |
| END summary | Tasks 1–2 |
| No src changes / no BCC | Global Constraints |
| retval > 0 counting | Tasks 1–2 |

No placeholders remaining. Types/names consistent (`@rx`, `@tx`, `[rtc]` / `[pipeline]`).
