#!/usr/bin/env bash
# Stress environment — physical NIC lab (this VM).
#
#   ens33   SSH / management + kernel UDP client (steady / iperf3; never bind)
#   ens192  vgw (DPDK port 0; RX+TX on one NIC)
#   ens224  unused by stress (leave on kernel)
#   ens256  pktgen client (DPDK; stress runs only)
#
# Usage:  source "$(dirname "$0")/env.sh"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PKGEN_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STRESS_DIR="${PKGEN_DIR}"  # legacy alias for STRESS_OUT_DIR

# --- Host NIC roles ---
export MGMT_IFACE="${MGMT_IFACE:-ens33}"
export SUT_IFACE="${SUT_IFACE:-ens192}"
# Kernel UDP client (steady / iperf3) — reuse SSH NIC; never vfio-bind.
export KERNEL_CLIENT_IFACE="${KERNEL_CLIENT_IFACE:-${MGMT_IFACE}}"
# DPDK pktgen client (vfio-bound during stress runs).
export PKTGEN_CLIENT_IFACE="${PKTGEN_CLIENT_IFACE:-ens256}"

export MGMT_PCI="${MGMT_PCI:-0000:03:00.0}"
export SUT_PCI="${SUT_PCI:-0000:0b:00.0}"
export CLIENT_PCI="${CLIENT_PCI:-0000:1b:00.0}"

# Bind only DPDK roles (mgmt + ens224 stay on kernel).
export DPDK_BIND_ARGS="${DPDK_BIND_ARGS:-${SUT_PCI} ${CLIENT_PCI}}"
export DPDK_BIND_IFACES="${DPDK_BIND_IFACES:-${DPDK_BIND_ARGS}}"

# --- L3 / L2 (must match src/config.hpp lab defaults) ---
# Client is the upstream backend: NAT egress returns to pktgen on ens256.
export CLIENT_IP="${CLIENT_IP:-10.0.0.1}"
export CLIENT_MAC="${CLIENT_MAC:-02:00:00:00:00:03}"
export GW_IP="${GW_IP:-192.168.1.10}"
export GW_MAC="${GW_MAC:-02:00:00:00:00:01}"
export VIP_IP="${VIP_IP:-192.168.1.100}"
export VIP_PORT="${VIP_PORT:-53}"
export UPSTREAM_IP="${UPSTREAM_IP:-${CLIENT_IP}}"
export UPSTREAM_PORT="${UPSTREAM_PORT:-53}"
export UPSTREAM_MAC="${UPSTREAM_MAC:-${CLIENT_MAC}}"

# --- Developer home (sudo su / sudo -i leave HOME=/root) ---
if [[ -z "${DEV_HOME:-}" ]]; then
  if [[ -n "${SUDO_USER:-}" && "${SUDO_USER}" != "root" ]]; then
    DEV_HOME="$(getent passwd "${SUDO_USER}" | cut -d: -f6)"
  elif [[ "${EUID}" -eq 0 && -d /home/violet ]]; then
    DEV_HOME="/home/violet"
  else
    DEV_HOME="${HOME}"
  fi
fi
export DEV_HOME

# --- Binaries ---
export VGW_BIN="${VGW_BIN:-${ROOT}/build/bin/Src/vgw}"
export VGWCP_BIN="${VGWCP_BIN:-${ROOT}/tools/vgwcp/vgwcp}"
export PKTGEN_SRC="${PKTGEN_SRC:-${DEV_HOME}/pktgen}"
export PKTGEN_BIN="${PKTGEN_BIN:-${PKTGEN_SRC}/builddir/app/pktgen}"

if [[ -d "${DEV_HOME}/.local/lib/pkgconfig" ]]; then
  export PKG_CONFIG_PATH="${DEV_HOME}/.local/lib/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
fi

# --- Bench ---
export STRESS_SECONDS="${STRESS_SECONDS:-30}"
export STRESS_WARMUP="${STRESS_WARMUP:-5}"
export PKTGEN_RATE="${PKTGEN_RATE:-100}"
export STRESS_OUT_DIR="${STRESS_OUT_DIR:-${STRESS_DIR}/out}"
export STRESS_BIND="${STRESS_BIND:-1}"

export STRESS_DATAPATH_MODES="${STRESS_DATAPATH_MODES:-pipeline rtc}"
export VGW_LCORES_PIPELINE="${VGW_LCORES_PIPELINE:-0-2}"
export VGW_LCORES_RTC="${VGW_LCORES_RTC:-0}"
# pktgen: main lcore cannot do RX/TX — minimum is 2 cores (main + one RX/TX).
export PKTGEN_LCORES="${PKTGEN_LCORES:-3-4}"
export PKTGEN_MAP="${PKTGEN_MAP:-[4:4].0}"
export PKTGEN_TIMEOUT_SEC="${PKTGEN_TIMEOUT_SEC:-$((STRESS_SECONDS + STRESS_WARMUP + 90))}"
