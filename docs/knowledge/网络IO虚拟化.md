# 网络 I/O 虚拟化：全虚拟化、半虚拟化、IO 透传与 SR-IOV

四种技术代表网络 I/O 虚拟化从「纯软件模拟」到「硬件切分」的演进。本文对比原理、数据路径、性能与适用场景，并对应 VioletGW lab 里的 vmxnet3 / DPDK 压测现状。

---

## 总览

| 维度 | 全虚拟化（QEMU 模拟） | 半虚拟化（Virtio / vmxnet3） | IO 透传（PCI Passthrough） | SR-IOV |
|------|----------------------|------------------------------|---------------------------|--------|
| 核心思想 | 纯软件模拟一张假网卡 | Guest 感知虚拟化，前后端协作 | 整张物理网卡直通给一台 VM | 物理网卡硬件切出多个 VF |
| Guest 是否感知虚拟化 | 否 | 是 | 否（看到真实硬件） | 否（看到真实 VF） |
| 数据路径 | Guest → VM Exit → QEMU（用户态）→ 物理网卡 | Guest → 共享环 / notify → 后端（vhost / VMkernel）→ 物理或 vSwitch | Guest → 物理网卡（绕过 Hypervisor 数据面） | Guest → VF → 物理网卡（绕过 Hypervisor 数据面） |
| 性能 | 最差 | 中等（vhost-user/DPDK 可很高） | 接近物理网卡 | 接近物理网卡 |
| 硬件要求 | 无 | 无 | IOMMU（VT-d / AMD-Vi） | 网卡支持 SR-IOV + IOMMU |
| 设备共享 | 多 VM 共享 | 多 VM 共享 | 一卡一 VM | 多 VF 共享一张物理卡 |
| 热迁移 | 支持 | 支持 | 通常不支持 | 通常不支持（需特殊方案） |

---

## 1. 全虚拟化（QEMU 纯软件模拟）

### 原理

Hypervisor（如 QEMU）在用户态用软件模拟标准网卡（e1000、RTL8139 等）的寄存器与行为。Guest OS 无需改动，以为自己驱动的是真实物理网卡。

### 发包路径（示意）

```text
Guest App → Guest 协议栈 → e1000 驱动 → 写虚拟寄存器
  → VM Exit
  → KVM 捕获 I/O，通知 QEMU
  → QEMU 用户态模拟网卡、处理包
  → TAP → 宿主机协议栈 → 物理网卡
```

### 瓶颈与取舍

每次 I/O 往往经历 Guest → VM Exit → KVM → QEMU（用户态）→ 宿主机内核，多次上下文切换与拷贝，吞吐通常远低于物理网卡。

**优点**：兼容性最好；支持热迁移 / 快照；无特殊硬件。  
**用途**：兼容性测试或极低端场景；生产网络几乎不再用纯模拟网卡。

---

## 2. 半虚拟化（Virtio；VMware 对应 vmxnet3）

### 原理

Guest **知道**自己在虚拟机里，加载专用前端驱动（Linux `virtio-net`，VMware `vmxnet3`），与宿主机后端通过**共享内存环形队列**协作，减少「模拟完整硬件寄存器」的开销。

Virtio 常见分层：

1. **前端**（Guest）：`virtio-net`，把包描述符放进 Virtqueue  
2. **Virtqueue（vring）**：Guest / Host 共享环，传 I/O 描述符  
3. **后端**（Host）：早期在 QEMU 用户态；可演进为 `vhost-net`（内核）或 `vhost-user`（用户态 DPDK）

### 发包路径（vhost-net 示意）

```text
Guest App → Guest 协议栈 → virtio-net
  → 描述符写入 Virtqueue（共享内存）
  → notify → VM Exit
  → vhost-net（内核）从环取包
  → TAP → 宿主机协议栈 → 物理网卡
```

VMware 路径概念上类似：Guest vmxnet3 ↔ 共享环 ↔ VMkernel / vSwitch ↔（可选）物理上行。本 lab 的 ens192 / ens256 即此类半虚拟化网卡。

### 性能提升点

- **批量**：多个 I/O 进环后一次性处理，降低每包 VM Exit  
- **vhost-net**：数据面下沉到宿主机内核，少一次用户态往返  
- **vhost-user + DPDK**：数据面到用户态轮询（PMD、大页、绑核），可接近直通  
- **vDPA**：进一步硬件卸载，逼近物理网卡

### Virtio 后端演进

```text
QEMU 用户态后端（最慢）
  → vhost-net（内核，中等）
    → vhost-user + DPDK（用户态轮询，接近直通）
      → vDPA（硬件卸载）
```

**优点**：远优于全虚拟化；公有云主流；热迁移友好；无需特殊网卡硬件。  
**局限**：数据面仍常经 Hypervisor / vSwitch；同机 hairpin、高 PPS 时易顶在虚拟 TX（见本仓库 pktgen 火焰图）。

---

## 3. IO 透传（PCI Passthrough / VFIO）

### 原理

借助 **IOMMU（VT-d）** 与 **VFIO**，把宿主机上一张**完整物理网卡**直接交给一台 VM。Guest 加载原生驱动（ixgbe、mlx5 等），数据面 DMA 直达设备，不经 Hypervisor 软件转发。

### 路径

```text
Guest App → Guest 协议栈 → 物理网卡原生驱动
  → IOMMU 保护下 DMA 访问物理网卡
  → 线缆发出（数据面无 VM Exit / 无 vSwitch）
```

### 取舍

| | |
|--|--|
| 性能 | 吞吐 / 延迟接近裸机（通常差距很小） |
| 共享 | **一卡一 VM**，无法多 VM 分一张卡 |
| 迁移 | 与硬件绑定，热迁移困难 |
| 硬件 | 需 BIOS 开 VT-d，平台支持 IOMMU |

适合：HPC、强隔离、可接受独占物理口的场景。

---

## 4. SR-IOV（Single Root I/O Virtualization）

### 原理

PCI-SIG 硬件虚拟化标准。支持 SR-IOV 的物理网卡在硬件内切出多个轻量 **VF（Virtual Function）**。每个 VF 有独立配置空间、中断与 DMA 能力；对 Guest 像一张真网卡。

- **PF（Physical Function）**：完整控制面，管理 / 配置 VF  
- **VF**：裁剪后的数据面接口，交给各 VM（再经 VFIO 可给 DPDK）

### 路径

```text
Guest App → Guest 协议栈 → VF 驱动（如 iavf）
  → IOMMU 下 DMA 访问 VF
  → 物理网卡硬件发出（数据面绕过 Hypervisor）
```

### 与 IO 透传对比

| 维度 | IO 透传 | SR-IOV |
|------|---------|--------|
| 分配粒度 | 整卡 → 1 VM | 1 卡 → N 个 VF → N VM |
| 共享 | 不可共享 | 多 VM 共享一张物理卡 |
| 硬件成本 | 高（每 VM 一张卡） | 低（一卡多 VF） |
| 性能 | 略优或持平（无 VF 切分） | 接近透传 |
| 要求 | IOMMU | IOMMU + 网卡 SR-IOV |

**局限**：需网卡支持；VF 能力可能裁剪；热迁移难；MAC/VLAN 等常由 PF / 管理面配置。

---

## 如何选择

| 场景 | 倾向 |
|------|------|
| 兼容性 / 实验模拟网卡 | 全虚拟化（e1000 等） |
| 通用云、要迁移与密度 | 半虚拟化（Virtio / vmxnet3） |
| 单 VM 独占、极致性能 | PCI 透传 |
| 多 VM 共享有限物理口、要接近线速 PPS | **SR-IOV** |
| DPDK pktgen / NFV 压测要冲 PPS | SR-IOV VF + `vfio-pci`（或透传） |

---

## 与 VioletGW lab 的关系

当前拓扑：

```text
pktgen (ens256 / vmxnet3 / DPDK) ──► vSwitch ──► vgw (ens192 / vmxnet3 / DPDK)
```

- ens192 / ens256 属于 **半虚拟化**（VMware vmxnet3），不是透传 / SR-IOV。  
- 两张「不同」Guest 网卡仍常共用 **宿主机 vSwitch + 虚拟 TX 路径**；满速压测火焰图中大量时间在 `librte_net_vmxnet3` TX，goodput 远低于 10G 标称，是半虚拟化路径上限，而非 NAT 逻辑本身。  
- **拆成两台同宿主机 vmxnet3 VM**：可能略减单 Guest 抢核，大头争抢往往仍在。  
- **要明显缓解**：SR-IOV / PCI 透传，或两台物理机 / 跨宿主机真网络。

相关压测入口：[Using Pktgen.md](./Using%20Pktgen.md)、[tools/pktgen/README.md](../../tools/pktgen/README.md)。

---

## 参考要点（整理自公开资料）

- Virtio / vhost / vhost-user 演进降低 VM Exit 与用户态往返  
- 透传与 SR-IOV 依赖 IOMMU；SR-IOV 用 VF 解决「一卡一机」成本  
- 云上 Virtio 仍是默认；NFV / 极致 PPS 场景优先 SR-IOV 或透传  
- 延迟量级：软件半虚拟化 VM–VM 往往数十 μs 级；SR-IOV / 物理路径可显著更低（具体数字依赖 nic / 拓扑，以实测为准）
