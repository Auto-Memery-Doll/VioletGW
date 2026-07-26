# 一些关于dma的内容

## 如何理解由dma引入的cpu缓存一致性的问题

缓存一致性问题是由多个处理器共享内存系统而产生的。当一个处理器修改了共享内存中的数据后，其他处理器的缓存必须同步更新，以保证所有处理器看到的数据是一致的。在涉及DMA操作的情况下，**这个问题变得更加复杂，因为DMA设备可以直接访问内存，而不经过CPU的缓存管理机制。**

解决方案：

1. **总线锁定**：当一个处理器需要修改内存中的数据时，它可以通过锁定总线来阻止其他处理器访问内存，直到完成自己的操作为止。这种方法简单但效率地下，因为他可能直接导致其他处理器阻塞等待

2. **写回策略**：处理器在修改数据后并步理解写回内存，而是将新值存储在其缓存行中。当缓存被替换出去时，才会将修改后的数据协会到内存。这种策略可以减少不必要的内存访问，但也可能导致数据的一致性问题

3. **MESI协议**：多级缓存系统中广泛使用的协议，用于维护缓存一致性。每个缓存行都有四种状态：*Modified*, *Exclusive*, *Share* 和 *Invaild*。通过交换状态信息，处理器之间可以协调对共享数据的访问，从而解决一致性问题。

## 各种"CPU"之间的差别

1. physical CPU
2. logical CPU
3. Core
4. Thread
5. Socket

一些可能提到的名词：
**microprocessor** 微处理器
**chip** 芯片
**system** bus 系统总线
**bottlenecks** 瓶颈

### 单核cpu核和超线程

chip communicated with other motherboard elemets through a **connector** or **socket**.
connectors or sockets had a board,

超线程(hyperthreading)的cpu架构
[img]<https://cdn.daniloaz.com/wp-content/uploads/2015/02/single-core-hyperthreading-cpu-diagram.png>
超线程是为了解决在多处理器线，进程（处理器）之间的通信需要跨越系统总线（system bus），从而导致处理器的计算性能下降，而导致的性能瓶颈

**HT** 超线程是在同一个芯片内，复制一些cpu的内部组件，例如寄存器(register)或者一级缓存(first level cache)所以信息就能够在两个不同的执行线程之间被共享，而不需要跨越系统总线

**LCPU** 逻辑CPU

### 多核架构CPU

架构图
[img]<https://cdn.daniloaz.com/wp-content/uploads/2017/05/quad-core-hyperthreading-cpu-diagram.png>

**core**：将多个上面所说的超线程处理器中的LCPU进行进一步封装，成为一个核心core，这些这些核心允许实现更高速的通信，因为他们使用的是共享同一个硅芯片的内部总线而不是系统总线

多核就是将多个core封装在同一个cpu上，用使用内部总线进行更加高速的交互
**1 LCPU = 1 thread**

### logical cpu and virtual cpu

virtal cpu: it's more framed in terms of computing virtualization.
virtual cpu通过mapping将底层的硬件映射成一个vcpu，这些硬件可以是physical cpu, logical cpu, HT, 

## 多队列网卡

网卡的多队列功能：一个网卡有多个队列，接收到的包根据TCP四元组信息hash后放入其中一个队列，后面该连接的所有包豆放入该队列，每个队列对应不同的中断，使用**irqbanlance**将不同的中断绑定到不同的核。充分利用了多核并行处理特性，提高效率
通过这种设置网卡多队列的方式，将不同队列绑定到不同cpu上，从而实现了cpu利用率的负载分担，实现该技术的前提是网课必须支持RSS(Receive Side Scaling)。RSS是网卡的硬件特性，实现了多队列，可以将不同的流分发到不同的cpu上。

### RSS

RSS(**Receive Side Scaling, 接受侧拓展**)，可以在多核系统中高效的分配网络接收处理任务，。RSS的主要目的是通过将网络数据包在多个CPU核心之间进行负载均衡，来提高系统的整体性能并减少单一核心的负载。

#### 基本概念

- **多队列**：RSS通过将接收的数据包分配到多个硬件队列中来实现
- **负载均衡**：通过将数据包分配给不同的CPU核心处理，RSS实现了负载的均衡
- **散列算法**：RSS使用一种散列算法，来确定数据包应分配到那个队列中，常用的散列算法基于源IP，目的IP，源端口，目的端口等信息的组合

#### RSS的工作流程

1. **数据包到达**：当数据包到达网卡时，网卡的硬件根据预设的散列算法计算出一个哈希值
2. **队列选择**：基于计算出的哈希值，网卡将数据包放入对应的接收队列
3. **CPU核心分配**：操作系统或驱动程序将接收队列绑定到特定的CPU核心，这意味着队列中的数据包会被核心处理
4. **数据包的处理**：每个CPU核心处理其分配队列中的数据包，从而实现了并行处理

#### RSS的好处

- **提高吞吐量**：通过利用多核处理器的能力，RSS可以显著提高网络吞吐量
- **减少延迟**：负载均衡可以减少单一核心的处理时间，从而降低延迟
- **减少中断开销**：RSS可以减少因频繁中断引起的CPU开销
