# Pktgen 压测

统一客户端：**DPDK pktgen** + veth 实验室拓扑。

## 一键跑数 + 报告 + 图表

需要 **sudo**（创建 veth、DPDK tap/af_packet）：

```bash
cd ~/flow_gateway
# 可选：缩短单次时长 STRESS_SECONDS=12 STRESS_WARMUP=2
sudo -E ./tools/stress/run_all_pktgen_bench.sh
```

产出：

| 文件 | 说明 |
|------|------|
| `docs/pktgen-client-results_nginx.csv` | nginx 8 case 原始数据 |
| `docs/pktgen-client-results_fg.csv` | flow_gateway 8 case 原始数据 |
| `docs/test/pktgen-bench-report.md` | 汇总表格 |
| `~/.cursor/projects/.../canvases/pktgen-bench-results.canvas.tsx` | 可视化图表（在 IDE 中打开 Canvas） |

## 单独跑某一 SUT

```bash
sudo -E PKTGEN_SUT=nginx ./tools/stress/run_pktgen_bench.sh
sudo -E PKTGEN_SUT=fg FG_BIN=./build/bin/Src/flow_gw ./tools/stress/run_pktgen_bench.sh
python3 tools/stress/generate_stress_report.py \
  --fg-csv docs/pktgen-client-results_fg.csv \
  --nginx-csv docs/pktgen-client-results_nginx.csv \
  --out-dir docs/test
```

## 前置

```bash
./tools/stress/build_pktgen.sh   # pktgen-23.10.2 @ ~/pktgen
./tools/stress/build_nginx.sh    # nginx 对比时需要
cmake --build build --target flow_gw
```

设计说明：[pktgen-unified-bench-design.md](../superpowers/specs/2026-07-25-pktgen-unified-bench-design.md)
