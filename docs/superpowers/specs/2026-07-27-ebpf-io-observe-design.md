# Design: eBPF NIC RX/TX observe for RTC and Pipeline

**Date:** 2026-07-27  
**Status:** approved  
**Related:** `docs/knowledge/RtC & Pipeline.md`, `src/dpdk/netif.cpp`

## Goal

Ship tools under `tools/ebpf/` that observe **NIC-level packet counts** for the two datapath modes. Counts come from uretprobe **return values** (packets per burst), not call counts.

| Mode | BPF C | Userspace | RX probe | TX probe |
|------|-------|-----------|----------|----------|
| RTC | `rtc_io.bpf.c` | `rtc_io` | `vgw::DpdkNetif::recv_burst` | `…::send_burst` |
| Pipeline | `pipeline_io.bpf.c` | `pipeline_io` | `vgw::dpdk::rx_burst` | `…::tx_burst` |

## Non-goals

- Observing Pipeline worker / soft-ring path (`recv_burst` / `send_burst` on rings)
- Changing `vgw` source to add counters
- bpftrace / BCC Python loaders
- Latency, drop reason, or per-queue breakdown
- Auto-detecting datapath mode (user picks the matching binary)

## Why probes differ

- **RTC:** worker calls `recv_burst` / `send_burst`, which call `dpdk::rx_burst` / `tx_burst` on the NIC. uretprobe on `recv_burst` / `send_burst` is the NIC I/O boundary and returns packet counts.
- **Pipeline:** NIC I/O is in `nic_recv` / `nic_send` (`void`). Packet counts live in nested `dpdk::rx_burst` / `tx_burst`. In Pipeline mode only I/O lcores call those helpers (worker uses rings), so uretprobe on them is NIC-level without worker noise.

## Approach

**libbpf + BPF CO-RE style C:**

- Kernel programs: `*.bpf.c` compiled with clang (`-target bpf`), skeleton via `bpftool gen skeleton`.
- Userspace: C loaders (`rtc_io.c` / `pipeline_io.c`) + shared `observe_loop.c` for 1 Hz print / Ctrl-C summary.
- Attach uretprobes by **mangled** symbol name with `bpf_program__attach_uprobe_opts(..., .retprobe = true)`.
- Build: `make -C tools/ebpf` (needs `clang`, `llvm-strip`, `bpftool`, `libbpf-dev`).

## Files

```text
tools/ebpf/
├── Makefile
├── counts.h
├── observe_loop.h
├── observe_loop.c
├── rtc_io.bpf.c
├── rtc_io.c
├── pipeline_io.bpf.c
├── pipeline_io.c
└── README.md
```

Generated (gitignored): `*.bpf.o`, `*.skel.h`, `rtc_io`, `pipeline_io`.

## BPF program behavior

Shared pattern in both `.bpf.c` files:

- `BPF_MAP_TYPE_PERCPU_ARRAY` `counts` with 4 entries: `RX`, `TX`, `RX_CALLS`, `TX_CALLS`
- `SEC("uretprobe")` + `BPF_URETPROBE`: if return value `> 0`, add to RX/TX and increment call counter
- License `GPL`

## Userspace behavior

- Args: `-b <binary>` (default `build/bin/Src/vgw`), `-p <pid>` (optional, `-1` = all)
- Attach two uretprobes; every 1s print cumulative `rx`/`tx` and `rx_pps`/`tx_pps`
- Ctrl-C: print elapsed, totals, average pps, call counts

```bash
make -C tools/ebpf
sudo ./tools/ebpf/rtc_io -p $(pgrep -n vgw)
sudo ./tools/ebpf/pipeline_io -p $(pgrep -n vgw)
```

## Output format (stdout)

```text
[rtc] rx=<cum> tx=<cum> rx_pps=<n> tx_pps=<n>
```

On exit:

```text
[rtc] done elapsed_s=<n> rx=<cum> tx=<cum> avg_rx_pps=<n> avg_tx_pps=<n> rx_calls=<n> tx_calls=<n>
```

Pipeline uses `[pipeline]` prefix.

## Constraints / ops notes

- Requires root (`sudo`) and a running `vgw` with matching symbols.
- Rebuild / strip of `vgw` can break probes; README documents mangled names.
- High PPS uprobe cost can perturb measurement; approximate observability only.

## Success criteria

- RTC binary against `--datapath_mode=rtc` shows non-zero RX/TX under traffic.
- Pipeline binary against `--datapath_mode=pipeline` shows non-zero RX/TX under traffic.
- Wrong tool on wrong mode may under-count (documented; no auto-guard).
