#!/usr/bin/env bash
# PCI stress: vgw (ens192) + pktgen client (ens256). Upstream = client (no Python echo).
#
#   sudo -E ./run.sh
#
# SSH must stay on ens33. VMware: put ens192/ens256 on the same (promiscuous) LAN.
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=env.sh
source "${DIR}/env.sh"
# shellcheck source=results/csv_parse.sh
source "${DIR}/results/csv_parse.sh"

OUT_DIR="${STRESS_OUT_DIR}"
LUA_DIR="${OUT_DIR}/lua"
VGW_PID=""
BOUND_BY_US=0

log() { printf '[pktgen] %s\n' "$*" >&2; }
die() { printf '[pktgen][ERROR] %s\n' "$*" >&2; exit 1; }

ensure_dpdk_idle() {
  local pids
  pids="$(pgrep -x vgw 2>/dev/null || true)"
  pids="${pids} $(pgrep -f '/app/pktgen' 2>/dev/null || true)"
  pids="$(echo "${pids}" | xargs)"
  [[ -z "${pids}" ]] && return 0
  die "DPDK already in use (pids: ${pids}). Stop manual vgw/pktgen first, e.g. sudo pkill vgw; sudo pkill -f app/pktgen"
}

[[ "${EUID}" -eq 0 ]] || die "need root, e.g. sudo -E $0"

stop_vgw() {
  if [[ -n "${VGW_PID}" ]]; then
    kill "${VGW_PID}" 2>/dev/null || true
    wait "${VGW_PID}" 2>/dev/null || true
    VGW_PID=""
  fi
}

on_exit() {
  stop_vgw
}
trap on_exit EXIT

maybe_bind() {
  if [[ "${STRESS_BIND}" != "1" ]]; then
    log "STRESS_BIND=0 — assuming ${DPDK_BIND_ARGS} already vfio-bound"
    return
  fi
  log "bind DPDK NICs: ${DPDK_BIND_ARGS} (mgmt=${MGMT_IFACE})"
  # shellcheck disable=SC2086
  MGMT_IFACE="${MGMT_IFACE}" "${DIR}/setup_nics.sh" bind
  BOUND_BY_US=1
}

publish_upstream() {
  [[ -x "${VGWCP_BIN}" ]] || die "vgwcp missing at ${VGWCP_BIN}"
  log "upstream=${UPSTREAM_IP}:${UPSTREAM_PORT} (client backend)"
  "${VGWCP_BIN}" -upstream "${UPSTREAM_IP}:${UPSTREAM_PORT}"
}

start_vgw() {
  local mode="$1"
  local lcores
  case "${mode}" in
    pipeline) lcores="${VGW_LCORES_PIPELINE}" ;;
    rtc) lcores="${VGW_LCORES_RTC}" ;;
    *) die "unknown datapath mode: ${mode}" ;;
  esac
  [[ -x "${VGW_BIN}" ]] || die "vgw missing; cmake --build build --target vgw"
  log "SUT=vgw datapath_mode=${mode} pci=${SUT_PCI} lcores=${lcores}"
  local -a cmd=(
    "${VGW_BIN}"
    --datapath_mode="${mode}"
    -l "${lcores}"
    --file-prefix=vgw
    -a "${SUT_PCI}"
  )
  if [[ "${mode}" == "rtc" ]]; then
    cmd+=(--rtc_workers=1)
  fi
  "${cmd[@]}" &
  VGW_PID=$!
  sleep 2
  kill -0 "${VGW_PID}" 2>/dev/null || die "vgw exited early (mode=${mode})"
  publish_upstream
}

run_one_case() {
  local datapath_mode="$1"
  local case_id="$2"
  local pattern="$3"
  local flows="$4"
  local payload="$5"
  local out="$6"
  local lcores="${PKTGEN_LCORES}"
  local map="${PKTGEN_MAP}"
  local prefix="pg_vgw_pci"
  local lua="${LUA_DIR}/${datapath_mode}_${case_id}.lua"
  local logf line pktgen_cmd rc timeout_sec

  log "case ${case_id} datapath=${datapath_mode} pattern=${pattern} flows=${flows} payload=${payload}"
  python3 "${DIR}/gen_lua.py" --case "${case_id}" --pattern "${pattern}" \
    --payload "${payload}" --seconds "${STRESS_SECONDS}" --warmup "${STRESS_WARMUP}" \
    --flows "${flows}" --rate "${PKTGEN_RATE}" \
    --src-ip "${CLIENT_IP}" --dst-ip "${VIP_IP}" --dst-port "${VIP_PORT}" \
    --src-mac "${CLIENT_MAC}" --dst-mac "${GW_MAC}" \
    -o "${lua}"

  mkdir -p "${OUT_DIR}/logs"
  logf="${OUT_DIR}/logs/${datapath_mode}_${case_id}.log"
  timeout_sec="${PKTGEN_TIMEOUT_SEC}"
  pktgen_cmd=$(printf '%q ' timeout --foreground "${timeout_sec}" \
    "${PKTGEN_BIN}" -l "${lcores}" --file-prefix="${prefix}" \
    -a "${CLIENT_PCI}" \
    -- -P -m "${map}" -f "${lua}")
  log "pktgen -l ${lcores} -m ${map} (timeout ${timeout_sec}s)"
  set +e
  script -qefc "${pktgen_cmd}" /dev/null >"${logf}" 2>&1
  rc=$?
  set -e
  if [[ "${rc}" -eq 124 ]]; then
    log "pktgen timed out after ${timeout_sec}s for ${datapath_mode}/${case_id} (see ${logf})"
    return 1
  fi
  line="$(grep -o 'PKTGEN_SUMMARY.*' "${logf}" | tail -1 || true)"
  [[ -n "${line}" ]] || {
    log "missing PKTGEN_SUMMARY for ${datapath_mode}/${case_id} (see ${logf})"
    return 1
  }
  line="$(printf '%s' "${line}" | tr -d '\r' | sed 's/frame_bytes=\([0-9][0-9]*\).*/frame_bytes=\1/')"
  append_csv_row "${out}" "${datapath_mode}" "${line}"
}

run_matrix() {
  local out="${OUT_DIR}/results.csv"
  local failures=0
  local modes=(${STRESS_DATAPATH_MODES})
  local cases=(
    "M01:hot:1:4"
    "M02:hot:1:1400"
    "M03:flows:1000:4"
    "M04:flows:1000:1400"
    "M05:flows:10000:4"
    "M06:hot:1:64"
  )
  local mode entry case_id pattern flows payload

  [[ -x "${PKTGEN_BIN}" ]] || die "pktgen missing at ${PKTGEN_BIN} (DEV_HOME=${DEV_HOME}); as violet: ${DIR}/build.sh"

  mkdir -p "${OUT_DIR}" "${LUA_DIR}"
  ensure_dpdk_idle
  maybe_bind

  echo "timestamp,datapath_mode,case_id,pattern,flows,payload_bytes,seconds,warmup,client_sent,client_received,lost,loss_rate_pct,sent_pps,received_pps,offered_bps,received_bps,frame_bytes" >"${out}"

  for mode in "${modes[@]}"; do
    start_vgw "${mode}"
    for entry in "${cases[@]}"; do
      IFS=: read -r case_id pattern flows payload <<<"${entry}"
      run_one_case "${mode}" "${case_id}" "${pattern}" "${flows}" "${payload}" "${out}" || failures=$((failures + 1))
    done
    stop_vgw
  done

  log "results: ${out}"
  if [[ "${failures}" -gt 0 ]]; then
    die "${failures} case(s) missing PKTGEN_SUMMARY"
  fi
  if [[ "${BOUND_BY_US}" -eq 1 ]]; then
    log "NICs still vfio-bound; restore with: ${DIR}/setup_nics.sh unbind"
  fi
}

run_matrix
