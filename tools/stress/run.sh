#!/usr/bin/env bash
# PCI stress: vgw (ens34) + pktgen (ens36) + kernel echo (ens35).
#
#   sudo -E ./run.sh
#
# SSH must stay on ens33. VMware: put ens34/35/36 on the same (promiscuous) LAN.
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=env.sh
source "${DIR}/env.sh"

OUT_DIR="${STRESS_OUT_DIR}"
LUA_DIR="${OUT_DIR}/lua"
ECHO_PID=""
VGW_PID=""
BOUND_BY_US=0

log() { printf '[stress] %s\n' "$*" >&2; }
die() { printf '[stress][ERROR] %s\n' "$*" >&2; exit 1; }

[[ "${EUID}" -eq 0 ]] || die "need root, e.g. sudo -E $0"

stop_services() {
  if [[ -n "${ECHO_PID}" ]]; then
    kill "${ECHO_PID}" 2>/dev/null || true
    ECHO_PID=""
  fi
  if [[ -n "${VGW_PID}" ]]; then
    kill "${VGW_PID}" 2>/dev/null || true
    wait "${VGW_PID}" 2>/dev/null || true
    VGW_PID=""
  fi
}

on_exit() {
  stop_services
}
trap on_exit EXIT

prepare_upstream() {
  local iface="${UPSTREAM_IFACE}"
  [[ -d "/sys/class/net/${iface}" ]] || die "upstream iface ${iface} missing (keep it on kernel)"

  ip link set "${iface}" down || true
  ip link set "${iface}" address "${UPSTREAM_MAC}"
  ip addr flush dev "${iface}" 2>/dev/null || true
  ip addr add "${UPSTREAM_IP}/24" dev "${iface}"
  ip link set "${iface}" up
  log "upstream ${iface} ${UPSTREAM_IP} mac=${UPSTREAM_MAC}"

  python3 "${DIR}/udp_echo.py" --bind "${UPSTREAM_IP}" --port "${UPSTREAM_PORT}" &
  ECHO_PID=$!
  sleep 0.3
  kill -0 "${ECHO_PID}" 2>/dev/null || die "udp_echo failed to start"
}

maybe_bind() {
  if [[ "${STRESS_BIND}" != "1" ]]; then
    log "STRESS_BIND=0 — assuming ${DPDK_BIND_IFACES} already vfio-bound"
    return
  fi
  log "bind DPDK NICs: ${DPDK_BIND_IFACES} (mgmt=${MGMT_IFACE})"
  # shellcheck disable=SC2086
  MGMT_IFACE="${MGMT_IFACE}" "${DIR}/setup_nics.sh" bind
  BOUND_BY_US=1
}

start_vgw() {
  [[ -x "${VGW_BIN}" ]] || die "vgw missing; cmake --build build --target vgw"
  log "SUT=vgw pci=${SUT_PCI} lcores=${VGW_LCORES:-0-2}"
  "${VGW_BIN}" -l "${VGW_LCORES:-0-2}" --file-prefix=vgw -a "${SUT_PCI}" &
  VGW_PID=$!
  sleep 2
  kill -0 "${VGW_PID}" 2>/dev/null || die "vgw exited early"
}

run_matrix() {
  local out="${OUT_DIR}/results_vgw.csv"
  local lcores="${PKTGEN_LCORES:-3-4}"
  local prefix="pg_vgw_pci"

  [[ -x "${PKTGEN_BIN}" ]] || die "pktgen missing at ${PKTGEN_BIN} (DEV_HOME=${DEV_HOME}); as violet: ${DIR}/build.sh"

  mkdir -p "${OUT_DIR}" "${LUA_DIR}"
  maybe_bind
  prepare_upstream
  start_vgw

  echo "timestamp,sut,case_id,mode,seconds,warmup,flows,payload_bytes,client_sent,client_received,lost,loss_rate_pct,sent_pps,received_pps,offered_bps,received_bps,frame_bytes" >"${out}"

  local cases=(
    "C01:hot:4" "C02:multi:4" "C03:newflow:4" "C04:bidir:4"
    "C05:hot:512" "C06:hot:4096" "C07:newflow:512" "C08:newflow:4096"
  )
  local entry case_id mode payload lua logf line ts pktgen_cmd
  for entry in "${cases[@]}"; do
    IFS=: read -r case_id mode payload <<<"${entry}"
    lua="${LUA_DIR}/vgw_${case_id}.lua"
    log "case vgw ${case_id} mode=${mode} payload=${payload}"
    python3 "${DIR}/gen_lua.py" --sut vgw --case "${case_id}" --mode "${mode}" \
      --payload "${payload}" --seconds "${STRESS_SECONDS}" --warmup "${STRESS_WARMUP}" \
      --flows "${STRESS_FLOWS}" --rate "${PKTGEN_RATE}" \
      --src-ip "${CLIENT_IP}" --dst-ip "${VIP_IP}" --dst-port "${VIP_PORT}" \
      --src-mac "${CLIENT_MAC}" --dst-mac "${GW_MAC}" \
      -o "${lua}"

    logf="$(mktemp)"
    pktgen_cmd=$(printf '%q ' "${PKTGEN_BIN}" -l "${lcores}" --file-prefix="${prefix}" \
      -a "${CLIENT_PCI}" \
      -- -P -m "[1:1].0" -f "${lua}")
    script -qefc "${pktgen_cmd}" /dev/null >"${logf}" 2>&1 || true
    line="$(grep '^PKTGEN_SUMMARY' "${logf}" | tail -1 || true)"
    rm -f "${logf}"
    [[ -n "${line}" ]] || {
      log "missing PKTGEN_SUMMARY for ${case_id}"
      continue
    }
    ts="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    # shellcheck disable=SC2001
    eval "$(echo "${line}" | sed -n \
      's/PKTGEN_SUMMARY sut=\([^ ]*\) case=\([^ ]*\) mode=\([^ ]*\) payload=\([0-9]*\) seconds=\([0-9]*\) warmup=\([0-9]*\) flows=\([0-9]*\) client_sent=\([0-9]*\) client_received=\([0-9]*\) lost=\([0-9]*\) loss_rate_pct=\([0-9.]*\) sent_pps=\([0-9.]*\) received_pps=\([0-9.]*\) offered_bps=\([0-9.]*\) received_bps=\([0-9.]*\) frame_bytes=\([0-9]*\).*/\
sut=\1 case_id=\2 mode=\3 payload=\4 seconds=\5 warmup=\6 flows=\7 client_sent=\8 client_received=\9 lost=\10 loss_rate_pct=\11 sent_pps=\12 received_pps=\13 offered_bps=\14 received_bps=\15 frame_bytes=\16/p')"
    echo "${ts},vgw,${case_id},${mode},${seconds},${warmup},${flows},${payload},${client_sent},${client_received},${lost},${loss_rate_pct},${sent_pps},${received_pps},${offered_bps},${received_bps},${frame_bytes}" >>"${out}"
  done

  stop_services
  log "results: ${out}"
  if [[ "${BOUND_BY_US}" -eq 1 ]]; then
    log "NICs still vfio-bound; restore with: ${DIR}/setup_nics.sh unbind"
  fi
}

run_matrix
