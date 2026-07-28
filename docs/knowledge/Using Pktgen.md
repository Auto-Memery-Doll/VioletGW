# Using Pktgen（VioletGW 压测客户端）

Pktgen-DPDK 是 DPDK 上的流量发生器。在 VioletGW 里它扮演 **DPDK 客户端**：从 `ens256` 打 UDP 到 VIP，经 vgw NAT 后再回到同一 port，用于测 Pipeline / RTC 端到端吞吐与丢包。

操作入口与脚本见 [tools/pktgen/README.md](../../tools/pktgen/README.md)。本文讲 **启动参数、交互 CLI、Lua 脚本** 三类语法。

---

## Lab 拓扑（本机）

```text
pktgen (ens256 / DPDK port 0) ──► vgw (ens192) ──NAT──► pktgen
ens33 = SSH / 内核客户端（steady，不参与 pktgen）
```

| 角色 | 网卡 | PCI | 说明 |
|------|------|-----|------|
| SSH | ens33 | `0000:03:00.0` | 永不 vfio-bind |
| vgw | ens192 | `0000:0b:00.0` | `run.sh` 里 `-a ${SUT_PCI}` |
| pktgen | ens256 | `0000:1b:00.0` | `-a ${CLIENT_PCI}` |

Lab L3/L2 默认值在 `tools/pktgen/env.sh` 与 `src/config.hpp`：

| 字段 | 默认值 |
|------|--------|
| VIP | 192.168.1.100:53 |
| 客户端 IP | 10.0.0.1 |
| 客户端 MAC | 02:00:00:00:00:03 |
| vgw MAC（lab） | 02:00:00:00:00:01 |

---

## 构建与启动

### 构建

```bash
git clone https://github.com/pktgen/Pktgen-DPDK.git ~/pktgen   # 首次
./tools/pktgen/build.sh
```

二进制默认路径：`~/pktgen/builddir/app/pktgen`（`PKTGEN_BIN`）。

### 命令行结构

```text
pktgen <EAL 参数> -- <应用参数>
```

**EAL（DPDK）常用：**

| 参数 | VioletGW 默认 | 含义 |
|------|---------------|------|
| `-l 3-4` | `PKTGEN_LCORES` | 可用 lcore；**main 核不能收发包** |
| `-a 0000:1b:00.0` | `CLIENT_PCI` | 绑定 pktgen 用的 PCI |
| `--file-prefix=pg_vgw_pci` | `run.sh` | 与 vgw 的 `vgw` prefix 区分，避免多进程冲突 |
| `-n 4` | （可选） | 内存 channel 数 |

**应用参数常用：**

| 参数 | 示例 | 含义 |
|------|------|------|
| `-P` | 必开 | Promiscuous |
| `-m "[4:4].0"` | `PKTGEN_MAP` | lcore 与 port 映射（见下节） |
| `-f script.lua` | `out/lua/pipeline_M01.lua` | 启动后执行 Lua |

**本 lab 推荐启动示例：**

```bash
sudo ./tools/pktgen/setup_nics.sh bind    # ens192 + ens256 → vfio

sudo ~/pktgen/builddir/app/pktgen \
  -l 3-4 \
  --file-prefix=pg_manual \
  -a 0000:1b:00.0 \
  -- -P -m "[4:4].0" \
  -f ~/VioletGW/tools/pktgen/out/lua/pipeline_M01.lua
```

一键压测（含 vgw + 全部用例）：

```bash
sudo ./tools/pktgen/run.sh
```

**注意：** 跑 `run.sh` 前不要留手动起的 vgw/pktgen，否则 VFIO 会 `Device or resource busy`。

---

## Lcore 映射 `-m`

格式：`[rx_lcore:tx_lcore].port`

| 示例 | 含义 |
|------|------|
| `[4:4].0` | lcore 4 对 port 0 既 RX 又 TX（本项目默认） |
| `[3:4].0` | lcore 3 RX，lcore 4 TX |

`-l 3-4` 表示 EAL 可见核为 3、4；其中 **第一个核（3）通常给 main/CLI**，映射里应把 **收发包核放在 4**。

错误示例（Pktgen 26 不支持）：`[0.2,0.3],[1.4,1.5]` — 这是旧式/多 port 写法，会报 `Invalid mapping format`。

---

## 两种发包模式

| 模式 | 配置方式 | VioletGW 是否使用 |
|------|----------|-------------------|
| **Sequence / Main** | `set 0 size 64`、`set 0 proto udp` | 否（仅交互调试） |
| **Range** | `range 0 ...` + `enable 0 range` | **是**（`gen_lua.py` 全部用 range） |

Main 页的 `set 0 size` **不会**作用于 range 流量。压测必须进 range 模式。

---

## 交互 CLI（`Pktgen:/>` 提示符）

在 TUI 底部输入；`help`、`help range` 可看完整帮助。

### 页面切换

| 命令 | 作用 |
|------|------|
| `page 0` / `page main` | 主统计页 |
| `page range` | Range 配置页 |
| `page stats` | 详细统计 |

### 端口基本控制

| 命令 | 作用 |
|------|------|
| `start 0` / `start all` | 开始发包 |
| `stop 0` / `stop all` | 停止 |
| `restart 0` | 重启 port |
| `clear 0 all` | 清计数 |

### `set`（速率、计数等）

```text
set 0 count 0          # 0 = 无限包（直到 stop）
set 0 rate 100         # 线速百分比 1–100
set 0 size 64          # 仅 sequence 模式；range 下用 range size
set 0 src ip 10.0.0.1/24
set 0 dst ip 192.168.1.100/32
```

### `range`（本项目压测核心）

语法：`range <port> <field> <start|min|max|inc> <value>`

**对应 M01（payload=4，帧长 46，CLI size 含 CRC 为 50）：**

```text
page range
set 0 count 0
set 0 rate 100
range 0 proto udp
range 0 size start 50
range 0 size min 50
range 0 size max 50
range 0 size inc 0
range 0 dst ip start 192.168.1.100
range 0 dst ip min 192.168.1.100
range 0 dst ip max 192.168.1.100
range 0 dst ip inc 0.0.0.0
range 0 src ip start 10.0.0.1
range 0 src ip min 10.0.0.1
range 0 src ip max 10.0.0.1
range 0 src ip inc 0.0.0.0
range 0 dst port start 53
range 0 src port start 4000
range 0 dst mac start 02:00:00:00:00:01
range 0 src mac start 02:00:00:00:00:03
enable 0 range
start 0
```

`flows` 模式（M03–M05）把 `src port` 设为区间并 `inc 1`，例如 4000–4999（1000 流）。

### `enable` / `disable`

```text
enable 0 range      # 开 range 发包
disable 0 range
enable 0 pcap       # 播 pcap
enable 0 process    # 处理入向包（echo 等）
```

### 脚本与状态

| 命令 | 作用 |
|------|------|
| `script foo.lua` | 在运行中的 pktgen 里执行 Lua |
| `load cmdfile.txt` | 加载 CLI 命令文件 |
| `save config.txt` | 导出当前配置为 CLI 命令 |
| `quit` | 退出 |

---

## Lua API（本项目常用）

Pktgen 启动后注册全局表 `pktgen`。VioletGW **不** `require("Pktgen.lua")`（依赖源码目录 cwd），直接调 C 绑定。

模板见 `tools/pktgen/gen_lua.py`，生成物在 `tools/pktgen/out/lua/`。

### 环境与控制

```lua
pktgen.screen("off")           -- 关 TUI，适合自动化
pktgen.page("range")             -- 等价 CLI: page range
pktgen.set("0", "count", 0)      -- 无限发包
pktgen.set("0", "rate", 100)     -- 100% 线速
pktgen.set_type("0", "ipv4")
pktgen.set_proto("0", "udp")     -- 与 range 配合；真正生效靠 set_range
pktgen.set_range("0", "on")      -- enable range
pktgen.start("0")
pktgen.stop("0")
pktgen.delay(5000)               -- 毫秒
pktgen.quit()
```

### Range 字段

```lua
pktgen.range.ip_proto(port, "udp")
pktgen.range.pkt_size(port, "start", 46)   -- 不含 CRC；与 gen_lua frame_size 一致
pktgen.range.dst_ip(port, "start", "192.168.1.100")
pktgen.range.src_ip(port, "start", "10.0.0.1")
pktgen.range.dst_port(port, "start", 53)
pktgen.range.src_port(port, "start", 4000)
pktgen.range.dst_mac(port, "start", "02:00:00:00:00:01")
pktgen.range.src_mac(port, "start", "02:00:00:00:00:03")
-- inc/min/max 同理；hot 流 inc=0，flows 流 src_port inc=1
```

`start|min|max|inc` 四元组定义字段恒定或递增；hot 用同一值，flows 用 `src_port` 扫描模拟多连接。

### 统计（Pktgen 26.x）

```lua
local s = pktgen.portStats("0")
local row = s[0] or s[0]  -- 按 port id 索引
local tx = row.curr.opackets
local rx = row.curr.ipackets
```

本项目脚本在 **warmup 前后各读一次计数**，用差值算测量窗口内的 sent/recv/lost。

### 输出约定

脚本末尾 `print` 一行 **`PKTGEN_SUMMARY`**，`run.sh` 用 grep 写入 `out/results.csv`：

```text
PKTGEN_SUMMARY case=M01 pattern=hot payload=4 seconds=30 warmup=5 flows=1 \
  client_sent=... client_received=... lost=... loss_rate_pct=... \
  sent_pps=... received_pps=... offered_bps=... received_bps=... frame_bytes=46
```

---

## 项目脚本怎么串起来

```text
gen_lua.py  →  out/lua/{pipeline|rtc}_Mxx.lua
run.sh      →  bind NICs → start vgw → vgwcp -upstream → pktgen -f lua → CSV
```

生成单个用例：

```bash
python3 tools/pktgen/gen_lua.py --case M01 --pattern hot --payload 4 \
  --seconds 30 --warmup 5 --flows 1 --rate 100 \
  -o tools/pktgen/out/lua/pipeline_M01.lua
```

`frame_size = 14 + 20 + 8 + payload`（以太 + IPv4 + UDP + payload）。

| Case | pattern | flows | payload | 帧长 |
|------|---------|------:|--------:|-----:|
| M01 | hot | 1 | 4 | 46 |
| M02 | hot | 1 | 1400 | 1442 |
| M03 | flows | 1000 | 4 | 46 |
| M04 | flows | 1000 | 1400 | 1442 |
| M05 | flows | 10000 | 4 | 46 |
| M06 | hot | 1 | 64 | 106 |

---

## 与 vgw 联调检查清单

1. `sudo ./tools/pktgen/setup_nics.sh bind`（或 `run.sh` 自动 bind）
2. vgw 已起：`--file-prefix=vgw -a 0000:0b:00.0`
3. upstream 已发布：`vgwcp -upstream 10.0.0.1:53`（`run.sh` 自动做）
4. 无残留进程：`pkill vgw; pkill -f app/pktgen`
5. 若无回包：检查 `dst_mac` 是否与实际 vgw port MAC 一致（lab 用 02:00:…，物理机可能是 `00:0c:29:…`）

---

## 常见问题

| 现象 | 原因 | 处理 |
|------|------|------|
| `Invalid mapping format` | `-m` 语法错误 | 用 `[4:4].0` |
| `Invalid port ID 0` | 未 `-a` PCI 或 bind 失败 | `setup_nics.sh bind` + `-a 0000:1b:00.0` |
| `vfio ... busy` | 已有 vgw/pktgen 占设备 | 先 pkill |
| `set 0 size` 不生效 | 未开 range | `enable 0 range` 或 Lua `set_range(on)` |
| 100% 丢包 | vgw 未起 / upstream 未 publish / MAC 不对 | 见联调清单 |
| `command not found: pktgen` | 未装系统包 | 用 `~/pktgen/builddir/app/pktgen` 或 `run.sh` |

---

## 延伸阅读

- 压测 runbook：[tools/pktgen/README.md](../../tools/pktgen/README.md)
- 设计说明：`docs/superpowers/specs/2026-07-27-stress-pipeline-rtc-design.md`
- 上游文档：[Pktgen-DPDK](https://github.com/pktgen/Pktgen-DPDK)
