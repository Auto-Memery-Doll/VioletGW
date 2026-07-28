# eBPF NIC I/O observe (libbpf)

libbpf tools that count **NIC-level** RX/TX packets for VioletGW datapath
modes. Counts use uretprobe **return values** (packets per burst), not call
counts.

## Requirements

- root (`sudo`) to load/attach
- `clang`, `llvm-strip`, `bpftool`, `libbpf-dev`
- `build/bin/Src/vgw` built **with symbols** (not stripped)

Install build deps (Ubuntu/Debian):

```bash
sudo apt install -y clang llvm libbpf-dev linux-tools-common
```

## Build

```bash
# Prefer system clang (recommended):
sudo apt install -y clang llvm libbpf-dev

make -C tools/ebpf
```

`vmlinux.h` is generated from `/sys/kernel/btf/vmlinux` on first build (gitignored).

Produces `tools/ebpf/rtc_io` and `tools/ebpf/pipeline_io`.

## Layout

| Mode | BPF C | Binary | Probes |
|------|-------|--------|--------|
| RTC (`--datapath_mode=rtc`) | `rtc_io.bpf.c` | `rtc_io` | `DpdkNetif::recv_burst` / `send_burst` |
| Pipeline (`--datapath_mode=pipeline`) | `pipeline_io.bpf.c` | `pipeline_io` | `dpdk::rx_burst` / `tx_burst` |

Mangled symbols (used for attach):

| Demangled | Mangled |
|-----------|---------|
| `vgw::DpdkNetif::recv_burst` | `_ZN3vgw9DpdkNetif10recv_burstEPP8rte_mbufj` |
| `vgw::DpdkNetif::send_burst` | `_ZN3vgw9DpdkNetif10send_burstEPP8rte_mbufj` |
| `vgw::dpdk::rx_burst` | `_ZN3vgw4dpdk8rx_burstEttPP8rte_mbuft` |
| `vgw::dpdk::tx_burst` | `_ZN3vgw4dpdk8tx_burstEttPP8rte_mbuft` |

## Usage

From repo root (so default `-b build/bin/Src/vgw` resolves):

```bash
# Terminal A
sudo ./build/bin/Src/vgw --datapath_mode=rtc --rtc_workers=1 -l 0
# or
sudo ./build/bin/Src/vgw --datapath_mode=pipeline -l 0-2

# Terminal B
sudo ./tools/ebpf/rtc_io -p $(pgrep -n vgw)
sudo ./tools/ebpf/pipeline_io -p $(pgrep -n vgw)
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

- Match the binary to `--datapath_mode`. Wrong pairing may under-count.
- Pipeline worker ring path is intentionally not probed.
- Uprobes add overhead; treat pps as approximate observability.
- After rebuild/strip: `nm -C build/bin/Src/vgw | grep burst`.
