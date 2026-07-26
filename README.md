# flow_gateway

用户态 L4 UDP/IP 网关：基于 DPDK mbuf 的 VIP 接入、会话 NAT、upstream 负载均衡与转发。

- 架构升级说明：[docs/architecture-upgrade-2026-07.md](docs/architecture-upgrade-2026-07.md)
- 构建：`cmake -S . -B build -G Ninja && cmake --build build`
- 单测：`ctest --test-dir build -L unit`
- 压测（pktgen，fg / nginx）：见 [architecture-upgrade §7.1](docs/architecture-upgrade-2026-07.md#71-压测pktgen-统一客户端)
