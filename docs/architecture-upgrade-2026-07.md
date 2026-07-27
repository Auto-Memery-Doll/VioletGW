# VGW 架构升级总结（2026-07）

本文档沉淀本次架构升级的目标、流程、模块划分与当前状态，便于后续开发与 onboarding。更细的设计决策见 `docs/superpowers/specs/` 下的分阶段 spec。

---

## 1. 背景与目标

**原形态：** 基于 lwIP 的用户态协议栈网关，夹杂 HTTP cache、协程、DMA、旧 balance 引擎（string IP + 心跳线程）等模块，数据路径依赖 pbuf/IOBuf 适配层。

**目标形态：** **DPDK mbuf 原生 L4 网关** — 仅处理 **UDP/IPv4**，做 VIP 接入、会话 NAT、upstream 负载均衡与转发；控制面与数据面分离，健康检查由未来 Go 控制面负责。

**核心数据路径：**

```text
NIC RX → parse → session 查/建 → DNAT/SNAT + L2 改写 → TX
                ↑
         upstream pick（新流）
```

---

## 2. 升级流程（按实施顺序）

| 阶段 | 内容 | 状态 |
|------|------|------|
| **P0 拆除** | 删除 lwIP、cache、throttle、coroutine、dma、stack、netif_driver、socket_api 等 | 已完成 |
| **P1 I/O 层** | `dpdk_netif` 改为纯 `rte_mbuf`；RX/TX 软件 ring + 双 lcore 收发包 | 已完成 |
| **P2 base** | 精简 `base/`；日志改为 spdlog；保留 ring/util/crc 等 | 已完成 |
| **P3 数据面模块** | `packet` → `session` → `forward` 逐步落地 + gtest | 已完成 |
| **P4 负载** | `UpstreamTable`（mod/rr，无锁 pick）；删除旧 `balance/` | 已完成 |
| **P5 控制面** | POSIX SHM 版本化配置块 + `CpShm::poll_apply`；Go CLI `tools/vgwcp` | 已完成 |
| **P6 验证** | `fwd_loop_smoke`（真 mbuf 正/回程）；21 个 gtest | 已完成 |
| **P7 工程整理** | 命名空间 `fg` → `vgw`；`src/`/`test/` 目录扁平化；`#pragma once` | 已完成 |

---

## 3. 架构对比

### 3.1 删除或不再使用

| 模块 | 原因 |
|------|------|
| `src/lwip/` | 不做 TCP/完整 IP 栈，L4 直接 parse mbuf |
| `cache/`、`throttle/`、`coroutine/` | 与 L4 网关无关 |
| `balance/` + `HeartbeatMonitor` | string IP + 热路径外心跳；由 `UpstreamTable` + 控制面替代 |
| `IOBuf` / `Closure` / 旧 `netif` 抽象 | 统一 `rte_mbuf` |
| HTTP / DMA / POSIX socket shim | 非当前产品方向 |

### 3.2 保留或新建

| 模块 | 职责 |
|------|------|
| `base/` | log（spdlog）、util（crc/fnv/mac/ring 名）、ring、singleton、type |
| `packet` | 解析 UDP/IPv4 mbuf，checksum 刷新 |
| `session` | 双向会话表，SNAT 端口池，idle expire |
| `forward` | VIP/回程识别，DNAT+SNAT，L2 改写 |
| `upstream` | 无锁 upstream 表与 mod/rr 选路 |
| `control` | SHM 附着与 `poll_apply` → `UpstreamTable` |
| `dpdk_netif` | DPDK port + 软件 ring + worker API |

---

## 4. 目录结构（当前）

### 4.1 `src/`

```text
src/
  base/              # 基础工具（单独子目录）
  packet.hpp/cpp
  session.hpp/cpp
  forward.hpp/cpp
  upstream.hpp/cpp
  cp_shm.hpp/cpp
  vgw_cp_shm.h        # 控制面 C ABI（Go cgo 可复用）
  config.hpp           # DPDK / lab 默认 VIP、upstream 等
  dpdk_netif.hpp/cpp
  main.cpp
```

- 命名空间：**`vgw`**（`vgw::packet`、`vgw::session`、`vgw::config` 等）
- 头文件：**`#pragma once`**

### 4.2 `test/`

```text
test/
  base/              # log、crc 等基础层单测
  packet_test.cpp    # 网关业务单测（与 src 平铺对应）
  session_test.cpp
  forward_test.cpp
  upstream_test.cpp
  cp_shm_test.cpp
  dpdk/              # eal_smoke、fwd_loop_smoke（非 gtest）
```

- 单元测试：**GoogleTest**，`ctest -L unit`（21 项）
- CMake 辅助：`vgw_add_gtest`

---

## 5. 数据面详解

### 5.1 报文处理（`packet`）

- `parse_udp_ipv4(mbuf)` → `PacketView`（Ether / IPv4 / UDP）
- 支持可选 checksum 校验；改写后 `refresh_ipv4_checksum` / UDP cksum 清零或重算

### 5.2 会话（`session`）

- **Forward map：** client→VIP 五元组 → `Session`
- **Reverse map：** upstream→gateway(SNAT port) → 同一会话
- SNAT 端口区间分配；`expire(now_ms)` 清理 idle 会话

### 5.3 转发与 NAT（`forward`）

**正向（client → VIP）：**

```text
client:sport → VIP:vport
  →  pick upstream（新流）
  →  gw:snat_port → upstream:uport   （SNAT + DNAT）
  →  改 L2 发往 upstream 侧
```

**回程（upstream → gw:snat）：**

```text
upstream:uport → gw:snat_port
  →  VIP:vport → client:sport         （reverse NAT）
  →  改 L2 发往 client 侧
```

`Forwarder::handle(mbuf, now_ms)` 返回 `drop` / `tx_forward` / `tx_reverse`；drop 由 caller `free_burst`。

### 5.4 负载均衡（`upstream`）

- **控制面写入：** `UpstreamTable::set(endpoints)`、`set_policy(mod|rr)`
- **数据面读取：** `pick(FlowKey)` — 仅 `atomic` load，**无 mutex**
- 实现：策略枚举 + `shared_ptr` 不可变节点快照交换
- 短暂不一致（worker 间看到不同 policy/列表）可接受；**已建 session 不受 pick 变化影响**

### 5.5 I/O 线程模型（`dpdk_netif`）

```text
NIC ──RX lcore──► fg_rx_ring_* ──► main worker ──► fg_tx_ring_* ──TX lcore──► NIC
                      recv_burst              send_burst
```

Worker 循环（`main`）：`recv_burst` → `Forwarder::handle` → `send_burst` / `free_burst`，并周期性 `session.expire` 与 `CpShm::poll_apply`。

---

## 6. 控制面

### 6.1 设计原则

- **数据面不做心跳**；upstream 健康由未来 Go 控制面维护后写入 SHM
- **热路径不读 SHM**；仅后台轮询 `poll_apply` 写入 `UpstreamTable`

### 6.2 SHM 布局（`vgw_cp_shm.h`）

- POSIX 名默认：`/vgw_cp`
- 字段：`magic`、`version`（最后 bump）、`policy`、`count`、`endpoints[]`（最多 64）
- 写端：填完 endpoints + policy 后 **递增 version**
- 读端：`version` 变化则拷贝并 `UpstreamTable::set` / `set_policy`

### 6.3 Go 写端

```bash
cd tools/vgwcp && go build -o vgwcp .
./vgwcp -policy mod -upstream 10.1.0.2:53 -upstream 10.1.0.3:53
```

运行中的 `vgw` 约每秒 `poll_apply` 一次（见 `config.hpp` 中 `CP_POLL_INTERVAL_MS`）。

---

## 7. 构建与测试

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build -L unit          # 21 个单元测试
./build/bin/Test/dpdk/fwd_loop_smoke -l 0 --no-huge --no-pci
```

依赖：DPDK（pkg-config）、spdlog、GTest（系统或 FetchContent）、Boost（base/util）。

### 7.1 压测（pktgen 统一客户端）

**DPDK pktgen** 打物理 NIC（ens34=vgw，ens35=kernel echo，ens36=pktgen）：

```bash
./tools/stress/build.sh
cmake --build build --target vgw
sudo -E ./tools/stress/run.sh
```

说明：[tools/stress/README.md](../tools/stress/README.md)

---

## 8. 尚未完成 / 后续方向

| 项 | 说明 |
|----|------|
| 全链路 DPDK I/O 生产化 | `vgw` + 物理 NIC / 多队列 RSS（WSL 上已用 pktgen + net_tap 联调） |
| VIP/MAC 进 SHM | 仍在 `config.hpp` 常量 |
| Go 控制面服务化 | 目前为 `vgwcp` CLI，无 RPC/常驻进程 |
| 健康检查 | 由 Go CP 实现，结果反映到 SHM upstream 列表 |
| 多队列 RSS / 多 VIP | 未做 |
| 一致性哈希 / 权重 | 可扩展 `BalancePolicy`，共用同一节点快照 |

---

## 9. 相关文档索引

| 文档 | 主题 |
|------|------|
| [upstream-table-worker-design.md](superpowers/specs/2026-07-24-upstream-table-worker-design.md) | UpstreamTable + worker |
| [shm-control-plane-design.md](superpowers/specs/2026-07-24-shm-control-plane-design.md) | 删除 balance + SHM |
| [fwd-loop-and-vgwcp-design.md](superpowers/specs/2026-07-24-fwd-loop-and-vgwcp-design.md) | smoke + Go 写端 |
| [wsl-dpdk-env-design.md](superpowers/specs/2026-07-24-wsl-dpdk-env-design.md) | WSL2 DPDK 环境 |
| [pktgen-unified-bench-design.md](superpowers/specs/2026-07-25-pktgen-unified-bench-design.md) | pktgen 压测（fg / nginx） |

---

## 10. 术语速查

| 术语 | 含义 |
|------|------|
| **VIP** | 对外虚拟 IP:port，客户端访问入口 |
| **DNAT** | 改目的 IP/port（VIP → upstream） |
| **SNAT** | 改源 IP/port（client → gateway，分配 snat_port） |
| **FlowKey** | UDP/IPv4 五元组（IP 网络序，port 主机序） |
| **UpstreamTable** | 控制面下发的 upstream 列表 + 选路策略 |
| **CpShm** | POSIX 共享内存配置块读写 |
