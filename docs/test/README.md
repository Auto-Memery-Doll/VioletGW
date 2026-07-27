# Pktgen 压测

物理 NIC：`vgw`(ens34) + `pktgen`(ens36) + 内核 UDP echo(ens35)。

详见 [tools/stress/README.md](../../tools/stress/README.md)。

```bash
./tools/stress/build.sh
cmake --build build --target vgw
# SSH 使用 ens33
sudo -E ./tools/stress/run.sh
```

结果：`tools/stress/out/results_vgw.csv`。
