#!/usr/bin/env bash
# Sample vgw with perf during the pktgen measurement window, then build a flame graph.
#
# Usage (normally invoked from run.sh):
#   ./perf_profile.sh <vgw_pid> <out_dir> <title>
#
# Timing: sleep STRESS_WARMUP + PERF_WARMUP_SLACK_SEC, then
#   perf record -p <pid> for STRESS_SECONDS (aligned with Lua measure segment).
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=env.sh
source "${DIR}/env.sh"

log() { printf '[perf] %s\n' "$*" >&2; }
die() { printf '[perf][ERROR] %s\n' "$*" >&2; exit 1; }

VGW_PID="${1:-}"
OUT_DIR="${2:-}"
TITLE="${3:-vgw}"

[[ -n "${VGW_PID}" && -n "${OUT_DIR}" ]] || die "usage: $0 <vgw_pid> <out_dir> <title>"
kill -0 "${VGW_PID}" 2>/dev/null || die "vgw pid ${VGW_PID} not running"

command -v perf >/dev/null || die "perf not found (apt install linux-tools-common linux-tools-\$(uname -r))"

SC="${FLAMEGRAPH_DIR}/stackcollapse-perf.pl"
FG="${FLAMEGRAPH_DIR}/flamegraph.pl"
[[ -x "${SC}" || -f "${SC}" ]] || die "missing ${SC} — clone https://github.com/brendangregg/FlameGraph to ${FLAMEGRAPH_DIR}"
[[ -x "${FG}" || -f "${FG}" ]] || die "missing ${FG} — clone https://github.com/brendangregg/FlameGraph to ${FLAMEGRAPH_DIR}"

mkdir -p "${OUT_DIR}"
PERF_DATA="${OUT_DIR}/perf.data"
PERF_SCRIPT="${OUT_DIR}/perf.script"
FLAME_SVG="${OUT_DIR}/flame.svg"
PERF_REPORT="${OUT_DIR}/perf-report.txt"

wait_sec=$((STRESS_WARMUP + PERF_WARMUP_SLACK_SEC))
log "wait ${wait_sec}s (warmup=${STRESS_WARMUP}+slack=${PERF_WARMUP_SLACK_SEC}) then record ${STRESS_SECONDS}s pid=${VGW_PID}"
sleep "${wait_sec}"

kill -0 "${VGW_PID}" 2>/dev/null || die "vgw pid ${VGW_PID} exited before perf record"

log "perf record -F ${PERF_FREQ} --call-graph ${PERF_CALL_GRAPH} -p ${VGW_PID} (${STRESS_SECONDS}s)"
# -- sleep N limits duration; -g implies call graphs with --call-graph
perf record -F "${PERF_FREQ}" -g --call-graph "${PERF_CALL_GRAPH}" \
  -p "${VGW_PID}" -o "${PERF_DATA}" -- sleep "${STRESS_SECONDS}"

log "perf script → ${PERF_SCRIPT}"
perf script -i "${PERF_DATA}" >"${PERF_SCRIPT}"

log "flamegraph → ${FLAME_SVG}"
perl "${SC}" "${PERF_SCRIPT}" | perl "${FG}" --title "${TITLE}" >"${FLAME_SVG}"

log "perf report → ${PERF_REPORT}"
perf report -i "${PERF_DATA}" --stdio --no-children >"${PERF_REPORT}" || true

log "done: ${FLAME_SVG}"
