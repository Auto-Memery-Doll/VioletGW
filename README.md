# VGW

用户态 L4 UDP/IP 网关（Violet Gateway / VGW）：基于 DPDK mbuf 的 VIP 接入、会话 NAT、upstream 负载均衡与转发。

- 架构：[docs/architecture-upgrade-2026-07.md](docs/architecture-upgrade-2026-07.md)
- 环境：`./scripts/setup_env.sh`
- 网卡（ens33=SSH，ens34=vgw，ens35=echo，ens36=pktgen）：`sudo ./tools/stress/setup_nics.sh bind`
- 构建：`cmake -S . -B build -G Ninja && cmake --build build`
- 运行（Pipeline，默认）：`sudo ./build/bin/Src/vgw --datapath_mode=pipeline -l 0-2`
- 运行（单核 RTC）：`sudo ./build/bin/Src/vgw --datapath_mode=rtc --rtc_workers=1 -l 0`
- CLI: VGW consumes only `--datapath_*`, `--io_*`, `--rtc_*`, `--flagfile`, and gflags help/version flags; all other arguments, including DPDK `-l` and `--no-huge`, are passed to EAL unchanged. `--` may optionally separate VGW flags from EAL arguments.
- 数据面模式说明：[docs/knowledge/RtC & Pipeline.md](docs/knowledge/RtC%20%26%20Pipeline.md)
- 单测：`ctest --test-dir build -L unit`（[test/README.md](test/README.md)）
- 压测：`sudo -E ./tools/stress/run.sh`（[tools/stress/README.md](tools/stress/README.md)）
- 控制面：`cd tools/vgwcp && go build -o vgwcp .`
