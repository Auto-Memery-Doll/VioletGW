# VGW

用户态 L4 UDP/IP 网关（Violet Gateway / VGW）：基于 DPDK mbuf 的 VIP 接入、会话 NAT、upstream 负载均衡与转发。

## Quick start

- 架构：[docs/architecture-upgrade-2026-07.md](docs/architecture-upgrade-2026-07.md)
- 环境：`./scripts/setup_env.sh`
- 网卡（ens33=SSH/内核客户端，ens192=vgw，ens256=pktgen）：steady 用 ens33；pktgen 压测 `sudo ./tools/pktgen/setup_nics.sh bind`
- 构建：`cmake -S . -B build -G Ninja && cmake --build build`
- I/O 调试日志（默认关）：`cmake -S . -B build -DVGW_IO_TRACE=ON && cmake --build build`（包住 forward/classify 等热路径日志；init / SHM / 端口初始化仍常开。在已有 build 目录下也可：`cd build && cmake -DVGW_IO_TRACE=ON ..`）
- 运行（Pipeline，默认）：`sudo ./build/bin/Src/vgw --datapath_mode=pipeline -l 0-2 --file-prefix=vgw -a 0000:0b:00.0`
- 运行（单核 RTC）：`sudo ./build/bin/Src/vgw --datapath_mode=rtc --rtc_workers=1 -l 0 --file-prefix=vgw -a 0000:0b:00.0`
- CLI: VGW consumes only `--datapath_*`, `--io_*`, `--rtc_*`, `--flagfile`, and gflags help/version flags; all other arguments, including DPDK `-l` and `--no-huge`, are passed to EAL unchanged. `--` may optionally separate VGW flags from EAL arguments.
- 数据面模式说明：[docs/knowledge/RtC & Pipeline.md](docs/knowledge/RtC%20%26%20Pipeline.md)
- Pktgen 用法：[docs/knowledge/Using Pktgen.md](docs/knowledge/Using%20Pktgen.md)
- 单测：`ctest --test-dir build -L unit`（[test/README.md](test/README.md)）
- 压测：`sudo ./tools/pktgen/run.sh`（[tools/pktgen/README.md](tools/pktgen/README.md)）
- 控制面：`cd tools/vgwcp && go build -o vgwcp .`

## V1 stress results (2026-07-28)

Lab: VMware vmxnet3；`PKTGEN_RATE=100`；测量 30s + warmup 5s；路径 `pktgen → vgw → NAT → pktgen`。
原始 CSV：`tools/pktgen/out/results.csv`（本地生成，已 gitignore）。

| Case | pattern | Pipeline recv_pps | RTC recv_pps | Pipeline loss% | RTC loss% |
|------|---------|------------------:|-------------:|---------------:|----------:|
| M01 | hot / 4B | 21812 | 19948 | 96.20 | 96.87 |
| M02 | hot / 1400B | 17645 | 17431 | 96.09 | 96.87 |
| M03 | flows×1000 / 4B | 20663 | 20656 | 96.17 | 96.87 |
| M04 | flows×1000 / 1400B | 16899 | 17997 | 95.98 | 96.87 |
| M05 | flows×10000 / 4B | 20530 | 21559 | 96.14 | 96.87 |
| M06 | hot / 64B | 20750 | 21564 | 96.15 | 96.87 |

Notes:

- ~96% loss means offered load (~0.4–0.7 Mpps) far exceeds gateway goodput (~17–22 Kpps) on this VM NIC; use `received_pps` for capacity, not `sent_pps`.
- Pipeline and RTC goodput are effectively tied here; RTC offers more TX but does not forward more end-to-end.
- Absolute PPS is VMware/vmxnet3-bound; relative mode comparison remains useful. Retest with lower `PKTGEN_RATE` to find the zero-loss knee.

## TODO

- **多 VIP / 多 upstream 集群**：当前控制面 SHM 与 `UpstreamTable` 只有单一扁平 endpoint 池，多服务集群会混用同一后端列表。后续按 VIP（或 cluster id）分区下发与选路。
