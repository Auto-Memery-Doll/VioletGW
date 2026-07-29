# VGW

用户态 L4 UDP/IP 网关（Violet Gateway / VGW）：基于 DPDK `rte_mbuf` 的 VIP 接入、会话 NAT、upstream 负载均衡与转发。数据面支持 **Pipeline**（RX/worker/TX 分核）与 **RTC**（单核 run-to-completion）。

```text
NIC RX → parse → session 查/建 → DNAT/SNAT + L2 改写 → TX
                ↑
         upstream pick（新流）
```

## Docs

| Doc | 内容 |
|-----|------|
| [architecture-upgrade-2026-07.md](docs/architecture-upgrade-2026-07.md) | 架构升级总结与模块划分 |
| [RtC & Pipeline](docs/knowledge/RtC%20%26%20Pipeline.md) | 两种数据面模式 |
| [Using Pktgen](docs/knowledge/Using%20Pktgen.md) | pktgen 压测用法 |
| [RTC M01 offer-sweep](docs/perf/rtc_M01_offer_sweep/REPORT.md) | 吞吐 / 丢包 / CPU 定量报告 |
| [softswitch contention](docs/knowledge/2026-07-29-offer-sweep-softswitch-contention.md) | 软交换争用结论与背景 |
| [test/README.md](test/README.md) · [tools/README.md](tools/README.md) | 单测与工具入口 |

## Build & run

```bash
./scripts/setup_env.sh
# Lab NICs: ens33=SSH/kernel client, ens192=vgw, ens256=pktgen
# steady (kernel): use ens33; pktgen stress:
sudo ./tools/pktgen/setup_nics.sh bind

cmake -S . -B build -G Ninja && cmake --build build
# Optional hot-path I/O debug logs (off by default):
# cmake -S . -B build -DVGW_IO_TRACE=ON && cmake --build build

# Pipeline (default)
sudo ./build/bin/Src/vgw --datapath_mode=pipeline -l 0-2 --file-prefix=vgw -a 0000:0b:00.0

# Single-core RTC
sudo ./build/bin/Src/vgw --datapath_mode=rtc --rtc_workers=1 -l 0 --file-prefix=vgw -a 0000:0b:00.0
```

**CLI:** VGW only consumes `--datapath_*`, `--io_*`, `--rtc_*`, `--flagfile`, and gflags help/version. Everything else (including DPDK `-l`, `--no-huge`) is passed to EAL. Optional `--` separates VGW flags from EAL args.

| Tool | Command / doc |
|------|----------------|
| Control plane | `cd tools/vgwcp && go build -o vgwcp .` |
| eBPF NIC I/O observe | [tools/ebpf/README.md](tools/ebpf/README.md) |
| Steady loss check | [tools/steady/README.md](tools/steady/README.md) |

## Bench

```bash
ctest --test-dir build -L unit
sudo ./tools/pktgen/run.sh   # see tools/pktgen/README.md
```

## Results (lab)

**Lab:** VMware vmxnet3；拓扑 `pktgen(ens256) ↔ vgw(ens192)`，upstream = client（self-loop）；测量 30s + warmup 5s。

**不要用满载 `PKTGEN_RATE=100` 的丢包率当容量。** 那一档 offered load 远超 VM 路径 goodput（旧表 ~96% loss）；看 `received_Mpps` / offer-sweep。详细数字与火焰图见 [REPORT](docs/perf/rtc_M01_offer_sweep/REPORT.md)。

### RTC M01 offer sweep (2026-07-29)

Case `M01`：hot / 1 flow / 4 B payload。`handle` 在所有 scale 上 &lt;2%；高 offer 时 CPU 集中在 `send_burst` / vmxnet3 TX — 瓶颈是 **self-loop 下共享软交换带宽争用**，不是会话/转发逻辑。

| scale | rate % | sent Mpps | recv Mpps | loss % | CPU send % |
|------:|-------:|----------:|----------:|-------:|-----------:|
| 0.1 | 0.38 | 0.06 | **0.06** | **0.0** | 11 |
| 0.5 | 1.90 | 0.27 | **0.17** | 37 | 45 |
| 0.75 | 2.86 | 0.40 | 0.12 | 70 | 57 |
| 1 | 100 | 0.53 | 0.04 | 93 | 90 |

- 本扫描中仅 `scale=0.1` 零丢包；峰值 goodput 约在 `0.5`（已有丢包）。
- Pipeline / RTC 在满载下端到端 goodput 接近；绝对 PPS 受 VMware/vmxnet3 约束。
- 下一步应用 **独立 upstream**（如 `ens224` kernel echo 或分离 vSwitch）再扫一遍，而不是微优化 timer。

原始 CSV / flame：`tools/pktgen/out/`（本地生成，已 gitignore）。

## Layout

```text
src/                 # packet / session / forward / upstream / cp_shm / vgw
  base/              # log, util, ring
  dpdk/              # netif, datapath flags/config
tools/
  pktgen/            # DPDK stress + perf/flame helpers
  ebpf/              # libbpf RX/TX observe (RTC / Pipeline)
  vgwcp/             # Go SHM control-plane CLI
  steady/            # low-rate kernel UDP loss check
docs/                # architecture, knowledge, perf reports
test/                # gtest + DPDK smoke
scripts/             # setup_env, setup_dpdk_nics
```

## TODO

- **多 VIP / 多 upstream 集群**：SHM 与 `UpstreamTable` 仍是单一扁平 endpoint 池；按 VIP（或 cluster id）分区下发与选路。
- **独立 upstream 拓扑再测**：验证 offer-sweep 在非 self-loop 下的零丢包膝点。
- **eBPF tool** 集成到常规压测流水线
- **go client**
- **saas platform**
