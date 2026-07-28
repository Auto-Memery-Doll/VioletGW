# Result processing

Parses pktgen run output into CSV. Used by `run.sh`; can be tested standalone.

| Script | Purpose |
|--------|---------|
| `csv_parse.sh` | `PKTGEN_SUMMARY` key=value → `out/results.csv` row |
| `test_csv_parse.sh` | unit test for `csv_parse.sh` |

```bash
./tools/pktgen/results/test_csv_parse.sh
```
