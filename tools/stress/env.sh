#!/usr/bin/env bash
# Stress environment — physical NIC lab (this VM).
#
#   ens33  SSH / management (kernel only, never bind)
#   ens34  vgw (DPDK port 0)
#   ens35  upstream UDP echo (kernel)
#   ens36  pktgen client (DPDK)
#
# Usage:  source "$(dirname "$0")/env.sh"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
STRESS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# --- Host NIC roles ---
export MGMT_IFACE="${MGMT_IFACE:-ens33}"
export SUT_IFACE="${SUT_IFACE:-ens34}"
export UPSTREAM_IFACE="${UPSTREAM_IFACE:-ens35}"
export CLIENT_IFACE="${CLIENT_IFACE:-ens36}"

export MGMT_PCI="${MGMT_PCI:-0000:02:01.0}"
export SUT_PCI="${SUT_PCI:-0000:02:02.0}"
export UPSTREAM_PCI="${UPSTREAM_PCI:-0000:02:03.0}"
export CLIENT_PCI="${CLIENT_PCI:-0000:02:04.0}"

# Bind only DPDK roles (upstream echo stays on kernel)
export DPDK_BIND_IFACES="${DPDK_BIND_IFACES:-${SUT_IFACE} ${CLIENT_IFACE}}"

# --- L3 / L2 (must match src/config.hpp lab defaults) ---
export CLIENT_IP="${CLIENT_IP:-10.0.0.1}"
export CLIENT_MAC="${CLIENT_MAC:-02:00:00:00:00:03}"
export GW_IP="${GW_IP:-192.168.1.10}"
export GW_MAC="${GW_MAC:-02:00:00:00:00:01}"
export VIP_IP="${VIP_IP:-192.168.1.100}"
export VIP_PORT="${VIP_PORT:-53}"
export UPSTREAM_IP="${UPSTREAM_IP:-10.1.0.2}"
export UPSTREAM_PORT="${UPSTREAM_PORT:-53}"
export UPSTREAM_MAC="${UPSTREAM_MAC:-02:00:00:00:00:02}"

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
export PKTGEN_SRC="${PKTGEN_SRC:-${DEV_HOME}/pktgen}"
export PKTGEN_BIN="${PKTGEN_BIN:-${PKTGEN_SRC}/builddir/app/pktgen}"

if [[ -d "${DEV_HOME}/.local/lib/pkgconfig" ]]; then
  export PKG_CONFIG_PATH="${DEV_HOME}/.local/lib/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
fi

# --- Bench ---
export STRESS_SECONDS="${STRESS_SECONDS:-30}"
export STRESS_WARMUP="${STRESS_WARMUP:-5}"
export STRESS_FLOWS="${STRESS_FLOWS:-1000}"
export PKTGEN_RATE="${PKTGEN_RATE:-100}"
export STRESS_OUT_DIR="${STRESS_OUT_DIR:-${STRESS_DIR}/out}"
# Auto vfio-bind SUT+client NICs at start of run.sh (1=yes)
export STRESS_BIND="${STRESS_BIND:-1}"
