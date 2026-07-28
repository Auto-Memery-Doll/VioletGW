# Tests

Unit / smoke: see [test/README.md](../../test/README.md).

Physical-NIC max-throughput stress (Pipeline + RTC): see [tools/pktgen/README.md](../../tools/pktgen/README.md).

```bash
./tools/pktgen/build.sh
cmake --build build --target vgw
sudo ./tools/pktgen/setup_nics.sh bind
sudo -E ./tools/pktgen/run.sh
```

Results: `tools/pktgen/out/results.csv` (local; gitignored). V1 summary table: [README.md](../../README.md#v1-stress-results-2026-07-28).

