# 实验结论：Offer 扫描、虚拟软交换争用与 TX 环

**日期：** 2026-07-29（扩写复习版）  
**英文版：** [2026-07-29-offer-sweep-softswitch-contention.md](./2026-07-29-offer-sweep-softswitch-contention.md)  
**定量报告：** [rtc_M01_offer_sweep/REPORT.md](../perf/rtc_M01_offer_sweep/REPORT.md)  
**范围：** 软交换 / 调度背景知识 + RTC M01 offer 扫描结论，便于后续复习（不只是实验流水账）。

---

## 0. 怎么用这份笔记

| 章节 | 用途 |
|------|------|
| §§1–5 | **背景** — 软交换、调度、队列、虚拟网卡环（学习材料） |
| §§6–10 | **本次实验** — 拓扑、因果链、火焰图解读 |
| §§11–12 | 后续实验 + 阅读清单 |

---

## 1. 什么是「软交换」（soft switch）？

**软件交换机**（soft switch / vSwitch）是用软件实现的二层转发引擎。逻辑角色与物理以太网交换机相同：

1. 学习（或配置）MAC → 端口映射。  
2. 在**入端口**收到帧。  
3. 决定**出端口**（单播命中、泛洪、丢弃）。  
4. 把帧拷贝/搬到该端口的发送路径。

与硬件 ASIC 交换机的差异：

| | 硬件交换机 | 软交换 |
|--|------------|--------|
| 转发引擎 | 专用硅片，常按线速设计 | 宿主机 CPU（也可能用物理网卡 DMA） |
| 端口 | PHY / ASIC 口 | vNIC 后端、tap、vhost、物理上行 |
| 容量 | 面向线速 PPS/bps | 受限于 **CPU 周期、缓存、锁、拷贝** |
| 隔离 | 硬件侧常有 per-port 队列 | 默认共享宿主机资源（除非刻意分区） |

在 VMware Workstation / Fusion（本实验环境）里，客户机网卡挂在名为 **VMnet0、VMnet1…** 的虚拟网络上。每个 VMnet 本质上就是一台**虚拟交换机**，还可以挂上：

- 宿主机虚拟网卡（Windows 里显示为 “VMware Network Adapter VMnet8”），  
- **NAT** 设备，和/或  
- **桥接**到物理网卡（WLAN / 有线）。

官方概述：[Understanding Virtual Networking Components (Workstation)](https://techdocs.broadcom.com/us/en/vmware-cis/desktop-hypervisors/workstation-pro/26H1/using-vmware-workstation-pro/configuring-network-connections/understanding-virtual-networking-components.html)。

**对本实验很关键：** pktgen 与 vgw 在同一 VMnet 上通信时，帧在**软交换内部**转发，不一定出主机 Wi‑Fi。瓶颈仍然真实存在：软交换的**处理能力**。

---

## 2. 报文路径分层（客户机 → 软交换 → 对端）

分四层理解：

```text
┌──────────────────────────────────────────────────────────┐
│ 客户机用户态（DPDK pktgen / vgw）                          │
│   rte_eth_rx_burst / rte_eth_tx_burst                    │
├──────────────────────────────────────────────────────────┤
│ 客户机虚拟网卡前端（vmxnet3 PCI 设备）                     │
│   RX 环 + TX 环（客户机内存，与后端共享）                   │
├──────────────────────────────────────────────────────────┤
│ Hypervisor / 宿主机后端（「软交换」+ 设备模型）             │
│   消费 TX 描述符 → L2 转发 → 填入对端 RX                   │
├──────────────────────────────────────────────────────────┤
│ 可选：宿主机物理网卡（仅当桥接 / NAT 出网时）               │
└──────────────────────────────────────────────────────────┘
```

客户机里 **PCI「独占」**（两个不同 BDF）只说明每张 vNIC 有自己的前端。  
**它们之间的转发仍然共享中间的后端。**

经典论文（*hosted* VMware I/O，Workstation 式 world switch、VMNet 驱动）：  
Sugerman 等，*Virtualizing I/O Devices on VMware Workstation’s Hosted Virtual Machine Monitor*（USENIX 2001）— [PDF](https://pages.cs.wisc.edu/~remzi/Classes/838/Spring2013/Papers/usenix_io_devices.pdf)。  
现代路径用半虚拟化 vmxnet3、更少的重型 world switch，但**核心不变**：客户机 TX/RX 由宿主机侧网络代码中介，CPU 预算有限。

---

## 3. 软交换「调度」——到底在调度什么

常有人问：*「软交换是不是串行处理 TX/RX？」*  
答案：**不是半双工那种互斥锁**，而是 **共享资源上的多路复用**。

### 3.1 必须被调度的工作

软交换大致反复执行这类工作单元：

```text
while (有活干 且 CPU 预算还在):
    从某个入向源拉取一批   // vNIC TX 通知、host tap、物理 RX
    分类 / 查 MAC 表
    入队到出端口
    推入对端 vNIC RX 环 / 物理 TX
```

这个循环（或一组跑类似循环的 worker 线程）就是 **调度** 发生的地方：

| 机制 | 含义 |
|------|------|
| **时间复用** | 一个宿主机核在多个端口/流之间轮转（跑完一个包，或短时间片） |
| **多 worker** | 多个核各自负责部分 RXQ/端口（OVS-DPDK 的 PMD 线程常见） |
| **排队** | 生产快过消费时，包在环/FIFO 里等，或被丢弃 |
| **通知** | kick / 中断 / eventfd 唤醒睡眠中的后端（或一直轮询） |

Workstation 内部精确调度器是专有实现；与测量相符的**心智模型**是：

> 有限个宿主机 CPU 为该 VMnet 上**所有**端口做转发。  
> pktgen 的 TX 越多 ⇒ 「pktgen→vgw」类工作单元越多。  
> 留给 「vgw→pktgen」的 CPU 片越少 ⇒ 该出方向消费越慢。

### 3.2 跑完再收下一个 vs 流水线（词汇）

软交换快路径常用 **run-to-completion（跑完再收下一个）**：一个线程把包从入向一路做到出向入队（Open vSwitch 文档如此描述快路径）。这仍**不是**「全局同一时刻只能有一个方向」；而是**单个包**不做 softirq 来回甩 —— 但多端口的大量包仍**共享同一批 worker**。

对比 DPDK 应用常见的 **pipeline（流水线）**（RX 线程 → worker → TX 线程）：级间用 ring 通信；反压表现为 ring 满 —— 与我们 TX 环卡住的*表象*类似。

OVS-DPDK PMD 轮询 / 绑定参考：  
[PMD Threads](https://docs.openvswitch.org/en/stable/topics/dpdk/pmd/)。

### 3.3 过载时公平性通常很弱

过载时软交换很少做到完美的 per-port 公平。常见现象：

- **激进生产者短期占优**（把自己面对的队列灌满）。  
- **通向忙碌对端的出向变慢**（对端 RX 环满 → 本端 TX 无法完成）。  
- **在最满的队列尾丢包**（或在虚拟后端静默丢）。

这与「提高 pktgen offer → vgw TX 看起来卡住」一致：pktgen 是共享织物上的激进生产者。

---

## 4. 队列、环与反压（核心复习）

### 4.1 生产者 / 消费者

```text
生产者（客户机 TX）              消费者（软交换 / 对端）
     │                              │
     ▼                              ▼
  填写 TX 描述符  ──通知──►  读描述符、转发帧
     │                              │
     ◄──── 完成 / 回收 ─────────────┘
          （腾出槽位 + 释放 mbuf）
```

消费者慢于生产者时，**TX 环被填满**。  
后续 `tx_burst` 在完成回收腾出槽位之前，无法再挂新包。

### 4.2 RX 与 TX 环相互独立

现代虚拟网卡通常提供**独立的** RX 环与 TX 环：

- RX：后端 → 客户机（软件用 `rx_burst` 轮询）。  
- TX：客户机 → 后端（软件用 `tx_burst` 挂包）。

设备可以两边同时推进；**TX 拥塞 ≠「网卡进入只能 TX、不能 RX 的模式」**。  
RX 仍可运行；织物过载时，进入客户机的 RX *数量*仍可能下降，因为帧根本到不了。

### 4.3 队头阻塞与共享存储交换（直觉）

物理交换机常用 **共享内存** 或 **crossbar + VOQ** 减轻队头阻塞（HOL）。笔记本上的软交换通常是：

- 每端口环很浅，  
- CPU 共享，  
- 偶有粗粒度锁，

于是会出现 **类似 HOL 的效应**：一条热流可以拖慢需要同一 worker/锁的其他工作。

本实验不必抠 ASIC 细节；记住：**共享 CPU + 浅队列 ⇒ 交叉流量互相干扰。**

### 4.4 反压 vs 丢包

| 策略 | 客户机侧症状 |
|------|----------------|
| 反压（TX 环满） | `tx_burst` 返回 0 / 部分发送；CPU 忙于回收 |
| 交换内丢包 | 端到端丢失；TX 仍可能「完成」（已进入交换后再丢） |
| 到达客户机 RX 之前丢 | `rx_burst` 看到的包变少；RX CPU 占比下降 |

火焰图（TX CPU ↑）强调 **vgw TX 侧反压**。CSV 丢包强调路径某处的 **端到端丢失**（交换、对端 RX 或应用）。两者可以同时存在。

---

## 5. 虚拟网卡：vmxnet3 与 DPDK TX 语义

### 5.1 全仿真 vs 半虚拟化

| 类型 | 例子 | 代价 |
|------|------|------|
| 全仿真 | e1000 / e1000e | 大量 VM exit；宿主机仿真寄存器 I/O |
| 半虚拟化 | **vmxnet3** | 共享环 + 更轻通知；转发仍耗宿主机 CPU |

vmxnet3 好于 e1000，但 Workstation 上 **标称 10 Gbps ≠ 可达到的小包 PPS**。宿主机 CPU 仍在做软交换。

### 5.2 `rte_eth_tx_burst` 契约（建议精读）

根据 DPDK PMD 文档：

- Burst API 把包挂进 TX 队列。  
- 返回值 = **被驱动/环接受了多少个**，不是「DMA 做完 / 对端已收到」。  
- 驱动批量回收已完成描述符（例如达到 `tx_free_thresh`）以摊销开销。  
- 应用也可调用 cleanup API 强制回收。

链接：

- [Poll Mode Driver guide](https://doc.dpdk.org/guides/prog_guide/poll_mode_drv.html)（`tx_free_thresh`、RS 位、回收）  
- [讨论：tx_burst 返回 ≈「已接管」，不是「已上线」](https://inbox.dpdk.org/dev/20260219110049.60743444@phoenix.local/T/)

### 5.3 软交换过载时为何回收变慢

只有在**后端**处理完描述符（拷贝/转发到足以释放所有权）之后，完成通知才会到来。  
若软交换忙于其他端口流量，*本端口*的完成变慢 → 环保持满 → 客户机 `tx_burst` 烧 CPU。

---

## 6. 本实验拓扑

```text
Windows 宿主机
  WLAN ~144 Mbps（在线）；Realtek 有线断开
  VMnet1 / VMnet8 = 宿主机侧 VMware 虚拟网卡（标称 100 Mbps ≠ 客户机 PPS 上限）
  VMware 软交换（连接两张客户机 vNIC 的 VMnet）
       │
       ├── 客户机 ens256（0000:1b:00.0，vmxnet3，DPDK）= pktgen
       └── 客户机 ens192（0000:0b:00.0，vmxnet3，DPDK）= vgw
```

压测设计（`tools/pktgen/env.sh`）：

- **Client = upstream**（自环）。  
- 每次成功转发至少穿过软交换两次：

```text
① pktgen TX → 软交换 → vgw RX
② vgw TX    → 软交换 → pktgen RX
```

相对「客户端→网关→独立 upstream」的单向设计，自环使织物负载大约 **翻倍**。

---

## 7. 争用因果链（本实验标准表述）

```text
提高 PKTGEN_OFFER_SCALE
        ↓
pktgen 向软交换提交更多 TX 工作
        ↓
共享软交换的 CPU/队列预算被 ① 吃掉
        ↓
②（vgw→pktgen）的工作被推迟（多路复用调度，不是半双工互斥）
        ↓
vgw TX 环：完成稀少 → 空槽稀少
        ↓
tx_burst 忙于回收 / 返回 0
        ↓
火焰图：send_burst ↑ ；CSV：recv ↓、loss ↑
```

**能力地板（A）** — 即便单独跑，vmxnet3 + Workstation 的小包 PPS 也不高。  
**争用（B）** — 自环多路 TX 共享，解释了 **offer 升高时 goodput 反而下降**（崩塌，而不只是平台期）。  
数据与图：[REPORT.md](../perf/rtc_M01_offer_sweep/REPORT.md)。

---

## 8. 全双工 vs TX 争用（概念自检）

| 说法 | 判定 |
|------|------|
| 全双工 = 同一链路上 A→B 与 B→A 可同时进行 | 对 |
| 全双工 = 多个 TX 源进入同一织物永远不排队 | **错** |
| 软交换像半双工 PHY 那样「串行 TX/RX」 | **作为模型是错的** |
| 软交换 = 共享 worker + 队列 ⇒ 交叉流量变慢 | **对** |

马路类比：双向马路（双工）vs 多辆车挤同一条匝道（共享 TX/转发预算）。

---

## 9. 火焰图不对称：`send_burst` ≫ `recv_burst`

| 路径 | 过载时 |
|------|--------|
| TX | 应用仍在尝试发送；等环回收 → **send_burst CPU 很高** |
| RX | 帧可能到不了客户机；空 poll 很便宜 → **CPU 占比低** |
| `handle` | 没多少时间 / 完整处理的包很少 → 本扫描中 **&lt;2%** |

低 offer 时 `now_ms` 占主导是 **空转 busy-poll**，不是高压丢包机制。定时器/`rte_timer` 仍可作为工程整洁项；解释不了丢包悬崖。

---

## 10. 怎么读 Windows 宿主机网卡列表

- 客户机 vmxnet3 **不会**出现在宿主机 `Get-NetAdapter` 里。  
- 宿主机 VMnet 适配器上的 **100 Mbps** 标签不是软交换 PPS 上限。  
- 客户机 **10G** 链路速率是标称值。  
- 仅有 Wi‑Fi：桥接路径很弱；同一 VMnet 的 VM↔VM 可留在主机内，仍会把软交换打满。

---

## 11. 后续实验（区分 A 与 B）

1. 把 upstream 从 pktgen 拆开（`ens224` 内核 echo，或第二台 VM）。  
2. 尽量把客户端与网关放到 **不同 VMnet** / 主机。  
3. 重跑同一 offer 扫描；对比 loss、recv Mpps、`send_burst` 占比与 [REPORT.md](../perf/rtc_M01_offer_sweep/REPORT.md)。  
4. 拓扑干净后再做数据面微优化（定时器、burst 大小等）。

---

## 12. 延伸阅读（比第一版更深）

### 软交换 / 虚拟网络

| 资源 | 重点 |
|------|------|
| [Workstation: virtual networking components](https://techdocs.broadcom.com/us/en/vmware-cis/desktop-hypervisors/workstation-pro/26H1/using-vmware-workstation-pro/configuring-network-connections/understanding-virtual-networking-components.html) | VMnet、DHCP、NAT、宿主机适配器 |
| Sugerman 等 USENIX 2001（[PDF](https://pages.cs.wisc.edu/~remzi/Classes/838/Spring2013/Papers/usenix_io_devices.pdf)） | Hosted VMM I/O 路径、网卡虚拟化开销 |
| [Open vSwitch FAQ / design](https://docs.openvswitch.org/en/latest/faq/design/) | 快路径 vs 慢路径、run-to-completion |
| [OVS-DPDK PMD threads](https://docs.openvswitch.org/en/stable/topics/dpdk/pmd/) | 高性能软交换如何调度 poll worker |
| Pfaff 等，*The Design and Implementation of Open vSwitch*（NSDI’15） | 生产级软交换架构（对照 Workstation） |

### DPDK 环 / 定时器 / vmxnet3

| 资源 | 重点 |
|------|------|
| [PMD / ethdev TX path](https://doc.dpdk.org/guides/prog_guide/poll_mode_drv.html) | `tx_burst`、回收阈值 |
| [Timer library](https://doc.dpdk.org/guides/prog_guide/timer_lib.html) | 用户态定时器；`next_ticks` |
| [vmxnet3 PMD](https://doc.dpdk.org/guides/nics/vmxnet3.html) | 驱动细节 |
| [VPP vmxnet3 usecase](https://fdio-vpp.readthedocs.io/en/latest/usecases/vmxnet3.html) | Workstation vs ESXi 预期 |

### 性能测量

| 资源 | 重点 |
|------|------|
| [CPU Flame Graphs（Gregg）](https://www.brendangregg.com/FlameGraphs/cpuflamegraphs.html) | 解读 busy-poll 核上的占比 |
| 仓库内 [offer-sweep REPORT](../perf/rtc_M01_offer_sweep/REPORT.md) | 本次数值 + SVG 图 |

### 仓库内产物

| 路径 | 内容 |
|------|------|
| `tools/pktgen/out/results_rtc_M01_s*.csv` | 原始 CSV |
| `tools/pktgen/out/perf/rtc_M01_s*/flame.svg` | 各档火焰图 |
| `tools/pktgen/README.md`、`env.sh` | 网卡角色、自环设计 |

---

## 13. 一段话收束

软交换是共享、靠 CPU 支撑的二层转发器：用队列和有限 worker 在多端口间复用工作——不是半双工「同一时刻只能 TX 或 RX」的互斥锁。本实验中 pktgen 与 vgw 共享一条带自环 upstream 的 VMnet 织物；提高 offer 把织物预算花在 client→gw 上，拖慢 gw→client 的 TX 完成，填满 vgw 的 TX 环，表现为 `send_burst` 主导的火焰图与上升的丢包。下一步拓扑实验应结合软交换调度/排队与 DPDK TX 回收来解读；不要从空闲路径的 `now_ms` 优化起步。
