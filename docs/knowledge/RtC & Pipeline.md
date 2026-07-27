# DPDK 两种主流数据流模式：RtC 与 Pipeline

两种模式都建立在 **PMD 轮询 + burst 收发 + 尽量无中断** 之上。差别不在“怎么从网卡拿包”，而在于：**一个包从 RX 到 TX，是由同一个核跑完，还是拆成多段由不同核接力。**

## Run-to-Completion（RtC / 跑完为止）

一个 lcore 从 RX 队列取包，在同一核上完成解析、查表、改写、转发，再 `tx_burst` 发出去。

```
NIC RXQ ──► [同一 lcore: RX → 处理 → TX] ──► NIC TXQ
```

### 典型形态

- 每个 worker 绑定一个（或一组）RX/TX queue
- 常配合 **RSS**：同一连接的包落到同一队列，从而落到同一核
- 包不跨核传递，整条路径在一个核上闭环

### 优点

- 路径短，延迟通常更低
- Cache 友好：同一包的 mbuf、五元组、会话表往往落在同一核的 L1/L2
- 实现简单，没有核间 ring 背压问题

### 代价

- 某个处理阶段变重时，整条路径都被拖住
- 功能扩展时容易把一个核塞满
- 横向扩展主要靠「多队列 + 多份完整路径」，而不是拆阶段

### 适合场景

转发逻辑不重、阶段少、延迟敏感：简单 L2/L3 forward、轻量 LB 数据面。

---

## Pipeline（流水线）

把处理路径拆成多个阶段，不同核各干一段，核间用 `rte_ring`（或类似软队列）传递 **mbuf 指针**（通常不拷贝包体）。

```
NIC ──► RX lcore ──► ring ──► Worker(s) ──► ring ──► TX lcore ──► NIC
```

### 典型形态

- **I/O 核**只负责收发：`rte_eth_rx_burst` / `rte_eth_tx_burst`
- **Worker 核**只负责业务：会话、改包、选上游等
- 可按阶段水平扩展（例如多个 worker 分摊计算）

### 优点

- I/O 与计算解耦，重业务不会直接堵死收包
- 阶段边界清晰，方便插入 DPI、加密、复杂会话等模块
- 可按瓶颈阶段单独加核

### 代价

- 跨核传包有 ring 开销和 cache miss
- 流水线存在背压：下游慢时 ring 满，需要丢包或阻塞策略
- 端到端延迟通常高于 RtC

### 适合场景

处理重、阶段多、I/O 与业务压力差很大，需要独立扩缩某一段。

---

## 对比

| | RtC | Pipeline |
|---|---|---|
| 包路径 | 单核走完 | 多核接力 |
| 延迟 | 通常更低 | 通常更高 |
| 吞吐扩展 | 多队列 + 多 worker 复制整条路径 | 可按阶段扩 |
| 复杂度 | 简单 | 要管 ring、背压、核亲和 |
| Cache | 友好 | 跨核易 miss |

---

## 和 VioletGW 的关系

二进制在 init 时通过 **gflags** 选择数据面模式（`main` 里在 EAL 之前解析，`remove_flags=true` 后剩余 argv 交给 `rte_eal_init`）。定义见 `src/dpdk/datapath_flags.cpp`。

### Runtime modes

| Mode | Flags | 行为 |
|---|---|---|
| **Pipeline**（默认） | `--datapath_mode=pipeline` | RX lcore → soft ring → worker → soft ring → TX lcore |
| **Single-worker RTC**（P0） | `--datapath_mode=rtc --rtc_workers=1` | 同一 lcore 在 queue 0 上 RX → handle → TX；无 distributor |
| Multi-worker RTC | `--rtc_workers=N`（N>1）+ `--rtc_steer=auto\|hw_rss\|soft_rr` | 设计已定，**P1/P2 尚未实现**（init 会报错） |

```bash
# Pipeline（默认）
./build/bin/Src/vgw --datapath_mode=pipeline -l 0-2

# 单核 RTC
./build/bin/Src/vgw --datapath_mode=rtc --rtc_workers=1 -l 0
```

gflags 可写在命令行任意位置；EAL 选项（如 `-l`）在 gflags 被剥离后仍由 DPDK 正常解析。

### Pipeline 路径（默认）

1. **RX lcore**：`DpdkNetif::nic_recv()` 从 NIC 收包，`push_burst` 进 `state_.pipeline.rx_ring`；ring 满则本核直接 `free` 丢包
2. **Worker**：从 RX ring 取包，做 L4 转发 / 会话 / 选上游，再推进 `state_.pipeline.tx_ring`
3. **TX lcore**：`DpdkNetif::nic_send()` 从 TX ring 取包，`tx_burst` 发往网卡

相关配置在 `src/dpdk/config.hpp`（`IO_RX_BURST` / `IO_TX_BURST` / `IO_RING_SIZE` 等），lcore 启动在 `DpdkNetifManager::init()`。Pipeline 专用 gflags：`--io_ring_size`、`--io_rx_lcore`、`--io_tx_lcore`、`--io_tx_ring_full_sleep_us`。

### Direct RTC 路径（`rtc_workers=1`）

Worker 在同一 lcore 上直接 `rx_burst` / `tx_burst`（queue 0），不经 soft ring，也不启动 RX/TX I/O lcore。

注意分层：

- **架构级 Pipeline**：RX / Worker / TX 分核 + soft ring（上面说的）
- **`forward` 里的 pipeline**：单包在 worker 内的处理步骤（解析 → 会话 → 改写），仍是同一核上的顺序逻辑，不是多核数据流架构

---

## 选型直觉

- 路径短、逻辑轻 → 优先 **RtC**
- I/O 与业务压力差很大、或某阶段要独立扩缩 → 用 **Pipeline**
- 常见折中：RSS 多路 **RtC worker**，只把特别重的阶段拆成流水段
