#!/usr/bin/env bash
# Unified pktgen bench for flow_gateway and nginx (same client, same lab topology).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
STRESS_DIR="${ROOT}/tools/stress"
PKTGEN_DIR="${STRESS_DIR}/pktgen"
NGINX_DIR="${STRESS_DIR}/nginx"
TOPO="${PKTGEN_DIR}/lab_topology.sh"
GEN_LUA="${PKTGEN_DIR}/gen_case_lua.py"
ECHO_PY="${NGINX_DIR}/udp_echo_server.py"
OUT="${PKTGEN_OUT:-${ROOT}/docs/pktgen-client-results_${PKTGEN_SUT:-nginx}.csv}"
LUA_DIR="${PKTGEN_DIR}/generated"

PKTGEN_SRC="${PKTGEN_SRC:-${HOME}/pktgen}"
PKTGEN_BIN="${PKTGEN_BIN:-${PKTGEN_SRC}/builddir/app/pktgen}"
NGINX_BIN="${NGINX_BIN:-${HOME}/nginx/install/sbin/nginx}"
FG_BIN="${FG_BIN:-${ROOT}/build/bin/Src/flow_gw}"

MEASURE_SEC="${STRESS_SECONDS:-30}"
WARMUP_SEC="${STRESS_WARMUP:-5}"
FLOW_COUNT="${STRESS_FLOWS:-1000}"
RATE_PCT="${PKTGEN_RATE:-100}"
SUT="${PKTGEN_SUT:-nginx}"  # nginx | fg

VETH_PG="${VETH_PG:-fg-pktgen0}"
FILE_PREFIX="${PKTGEN_FILE_PREFIX:-pgbench_${SUT}}"
PKTGEN_LCORES="${PKTGEN_LCORES:-}"
if [[ -z "${PKTGEN_LCORES}" ]]; then
  if [[ "${SUT}" == "fg" ]]; then
    PKTGEN_LCORES="3-4"
  else
    PKTGEN_LCORES="0-1"
  fi
fi
FG_LCORES="${FG_LCORES:-0-2}"

ECHO_PID=""
NGINX_STARTED=0
FG_PID=""

resolve_nginx() {
  if [[ -x "${NGINX_BIN}" ]]; then echo "${NGINX_BIN}"; return; fi
  command -v nginx 2>/dev/null || { echo "nginx not found" >&2; exit 1; }
}

cleanup() {
  if [[ "${NGINX_STARTED}" -eq 1 ]]; then
    "${NGINX_BIN}" -s stop -c "${NGINX_DIR}/stream-udp-lab.conf" 2>/dev/null || true
    rm -f /tmp/fg-nginx-lab.pid
  fi
  if [[ -n "${ECHO_PID}" ]] && kill -0 "${ECHO_PID}" 2>/dev/null; then
    kill "${ECHO_PID}" 2>/dev/null || true
  fi
  if [[ -n "${FG_PID}" ]] && kill -0 "${FG_PID}" 2>/dev/null; then
    kill "${FG_PID}" 2>/dev/null || true
  fi
  if [[ "${EUID}" -eq 0 ]]; then
    "${TOPO}" down 2>/dev/null || true
  else
    sudo "${TOPO}" down 2>/dev/null || "${TOPO}" down 2>/dev/null || true
  fi
}
trap cleanup EXIT

if [[ ! -x "${PKTGEN_BIN}" ]]; then
  echo "Build pktgen: ${STRESS_DIR}/build_pktgen.sh" >&2
  exit 1
fi

CASES=(
  "C01:hot:4"
  "C02:multi:4"
  "C03:newflow:4"
  "C04:bidir:4"
  "C05:hot:512"
  "C06:hot:4096"
  "C07:newflow:512"
  "C08:newflow:4096"
)

mkdir -p "$(dirname "${OUT}")" "${LUA_DIR}"

if [[ "${EUID}" -ne 0 ]]; then
  echo "Run with sudo (veth + DPDK tap on WSL):" >&2
  echo "  sudo -E PKTGEN_SUT=nginx ${STRESS_DIR}/run_pktgen_bench.sh" >&2
  echo "  sudo -E ./tools/stress/run_all_pktgen_bench.sh   # nginx + fg + report" >&2
  exit 1
fi

"${TOPO}" up

python3 "${ECHO_PY}" --bind 10.1.0.2 --port 53 &
ECHO_PID=$!
sleep 0.3

NGINX_BIN="$(resolve_nginx)"

if [[ "${SUT}" == "nginx" ]]; then
  echo "SUT=nginx ${NGINX_BIN}" >&2
  "${NGINX_BIN}" -c "${NGINX_DIR}/stream-udp-lab.conf"
  NGINX_STARTED=1
elif [[ "${SUT}" == "fg" ]]; then
  if [[ ! -x "${FG_BIN}" ]]; then
    echo "Build flow_gw: cmake --build build --target flow_gw" >&2
    exit 1
  fi
  echo "SUT=flow_gateway ${FG_BIN}" >&2
  "${FG_BIN}" -l "${FG_LCORES}" --no-huge --no-shconf --file-prefix=fgw \
    --vdev net_tap0,iface=fg-sut0 &
  FG_PID=$!
  sleep 2
else
  echo "PKTGEN_SUT must be nginx or fg" >&2
  exit 1
fi

echo "timestamp,sut,case_id,mode,seconds,warmup,flows,payload_bytes,client_sent,client_received,lost,loss_rate_pct,sent_pps,received_pps,offered_bps,received_bps,frame_bytes" >"${OUT}"

for entry in "${CASES[@]}"; do
  IFS=: read -r case_id mode payload <<<"${entry}"
  lua="${LUA_DIR}/${SUT}_${case_id}.lua"
  echo "=== pktgen ${SUT} ${case_id} mode=${mode} payload=${payload} ===" >&2
  python3 "${GEN_LUA}" --sut "${SUT}" --case "${case_id}" --mode "${mode}" \
    --payload "${payload}" --seconds "${MEASURE_SEC}" --warmup "${WARMUP_SEC}" \
    --flows "${FLOW_COUNT}" --rate "${RATE_PCT}" -o "${lua}"

  log="$(mktemp)"
  pktgen_cmd=$(printf '%q ' "${PKTGEN_BIN}" -l "${PKTGEN_LCORES}" --no-huge --no-shconf \
    --file-prefix="${FILE_PREFIX}" --vdev "net_tap0,iface=${VETH_PG}" \
    -- -P -m "[1:1].0" -f "${lua}")
  script -qefc "${pktgen_cmd}" /dev/null >"${log}" 2>&1 || true

  line="$(grep '^PKTGEN_SUMMARY' "${log}" | tail -1 || true)"
  rm -f "${log}"
  if [[ -z "${line}" ]]; then
    echo "missing PKTGEN_SUMMARY for ${case_id}" >&2
    continue
  fi
  ts="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  # shellcheck disable=SC2001
  eval "$(echo "${line}" | sed -n \
    's/PKTGEN_SUMMARY sut=\([^ ]*\) case=\([^ ]*\) mode=\([^ ]*\) payload=\([0-9]*\) seconds=\([0-9]*\) warmup=\([0-9]*\) flows=\([0-9]*\) client_sent=\([0-9]*\) client_received=\([0-9]*\) lost=\([0-9]*\) loss_rate_pct=\([0-9.]*\) sent_pps=\([0-9.]*\) received_pps=\([0-9.]*\) offered_bps=\([0-9.]*\) received_bps=\([0-9.]*\) frame_bytes=\([0-9]*\).*/\
sut=\1 case_id=\2 mode=\3 payload=\4 seconds=\5 warmup=\6 flows=\7 client_sent=\8 client_received=\9 lost=\10 loss_rate_pct=\11 sent_pps=\12 received_pps=\13 offered_bps=\14 received_bps=\15 frame_bytes=\16/p')"
  echo "${ts},${sut},${case_id},${mode},${seconds},${warmup},${flows},${payload},${client_sent},${client_received},${lost},${loss_rate_pct},${sent_pps},${received_pps},${offered_bps},${received_bps},${frame_bytes}" >>"${OUT}"
done

echo "Results: ${OUT}" >&2
