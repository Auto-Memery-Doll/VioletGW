#!/usr/bin/env bash
# Run pktgen matrix for nginx + flow_gateway, merge results, write report + canvas.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
STRESS="${ROOT}/tools/stress"
DOCS="${ROOT}/docs/test"
SECONDS="${STRESS_SECONDS:-15}"
WARMUP="${STRESS_WARMUP:-3}"

run_bench() {
  local sut="$1"
  echo "========== PKTGEN_SUT=${sut} ==========" >&2
  env PKTGEN_SUT="${sut}" STRESS_SECONDS="${SECONDS}" STRESS_WARMUP="${WARMUP}" \
    "${STRESS}/run_pktgen_bench.sh"
}

if [[ "${EUID}" -ne 0 ]]; then
  echo "Re-exec with sudo (required for veth + DPDK tap on WSL)..." >&2
  exec sudo -E "$0" "$@"
fi

mkdir -p "${DOCS}"

run_bench nginx
run_bench fg

python3 "${STRESS}/generate_stress_report.py" \
  --fg-csv "${ROOT}/docs/pktgen-client-results_fg.csv" \
  --nginx-csv "${ROOT}/docs/pktgen-client-results_nginx.csv" \
  --out-dir "${DOCS}" \
  --measure-sec "${SECONDS}" \
  --warmup-sec "${WARMUP}"

echo "Done. Report: ${DOCS}/pktgen-bench-report.md" >&2
echo "Canvas: ${ROOT}/../.cursor/projects/home-violet-flow-gateway/canvases/pktgen-bench-results.canvas.tsx" >&2
