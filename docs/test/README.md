# Tests

Unit / smoke: see [test/README.md](../../test/README.md).

Physical-NIC max-throughput stress (Pipeline + RTC): see [tools/stress/README.md](../../tools/stress/README.md).

```bash
./tools/stress/build.sh
cmake --build build --target vgw
sudo -E ./tools/stress/run.sh
```

Results: `tools/stress/out/results.csv` (includes `datapath_mode` column).
