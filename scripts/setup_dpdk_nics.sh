#!/usr/bin/env bash
# Bind / unbind host NICs for DPDK using vfio-pci.
#
# Usage:
#   sudo ./scripts/setup_dpdk_nics.sh status
#   sudo ./scripts/setup_dpdk_nics.sh bind ens35 ens36
#   sudo ./scripts/setup_dpdk_nics.sh bind 0000:02:03.0 0000:02:04.0
#   sudo ./scripts/setup_dpdk_nics.sh unbind              # restore previously bound
#   sudo ./scripts/setup_dpdk_nics.sh unbind ens35        # restore listed
#   sudo ./scripts/setup_dpdk_nics.sh setup ens35 ens36   # hugepages hint + vfio + bind
#
# Env:
#   DPDK_DRIVER=vfio-pci          # default; also supports uio_pci_generic
#   VFIO_NOIOMMU=1                # force no-IOMMU mode (auto if 0 iommu groups)
#   MGMT_IFACE=ens33              # protected management/SSH NIC (preferred over default-route)
#   ALLOW_MGMT_BIND=1             # allow binding the management NIC (dangerous)
#   STATE_FILE=...                # bind state path (default: /var/tmp/violetgw-dpdk-nics.state)
#
# Safety: refuses to bind the interface that owns the preferred default route
# unless ALLOW_MGMT_BIND=1.

set -euo pipefail

DRIVER="${DPDK_DRIVER:-vfio-pci}"
STATE_FILE="${STATE_FILE:-/var/tmp/violetgw-dpdk-nics.state}"
ACTION="${1:-status}"
shift || true

log()  { printf '[dpdk-nic] %s\n' "$*"; }
warn() { printf '[dpdk-nic][WARN] %s\n' "$*" >&2; }
die()  { printf '[dpdk-nic][ERROR] %s\n' "$*" >&2; exit 1; }

need_root() {
  [[ "${EUID}" -eq 0 ]] || die "run as root: sudo $0 ${ACTION} $*"
}

find_bind_tool() {
  if command -v dpdk-devbind.py >/dev/null 2>&1; then
    command -v dpdk-devbind.py
  elif [[ -x /usr/share/dpdk/usertools/dpdk-devbind.py ]]; then
    echo /usr/share/dpdk/usertools/dpdk-devbind.py
  else
    die "dpdk-devbind.py not found (apt install dpdk-dev)"
  fi
}

normalize_pci() {
  local x="$1"
  if [[ "${x}" =~ ^[0-9a-fA-F]+:[0-9a-fA-F]+:[0-9a-fA-F]+\.[0-9a-fA-F]+$ ]]; then
    echo "${x}"
  elif [[ "${x}" =~ ^[0-9a-fA-F]+:[0-9a-fA-F]+\.[0-9a-fA-F]+$ ]]; then
    echo "0000:${x}"
  else
    echo ""
  fi
}

iface_to_pci() {
  local iface="$1"
  local pci
  pci="$(ethtool -i "${iface}" 2>/dev/null | awk '/bus-info:/ {print $2}')"
  [[ -n "${pci}" ]] || return 1
  normalize_pci "${pci}"
}

resolve_to_pci() {
  local arg="$1"
  local pci
  pci="$(normalize_pci "${arg}")"
  if [[ -n "${pci}" ]]; then
    echo "${pci}"
    return 0
  fi
  if [[ -d "/sys/class/net/${arg}" ]]; then
    iface_to_pci "${arg}"
    return
  fi
  return 1
}

pci_to_iface() {
  local pci="$1"
  local d
  for d in /sys/class/net/*; do
    local bus
    bus="$(ethtool -i "$(basename "${d}")" 2>/dev/null | awk '/bus-info:/ {print $2}')"
    if [[ "$(normalize_pci "${bus:-}")" == "$(normalize_pci "${pci}")" ]]; then
      basename "${d}"
      return 0
    fi
  done
  return 1
}

mgmt_iface() {
  # Explicit management/SSH NIC wins (this machine: ens33).
  if [[ -n "${MGMT_IFACE:-}" ]]; then
    echo "${MGMT_IFACE}"
    return 0
  fi
  # Fallback: preferred default route device (lowest metric).
  ip -4 route show default 2>/dev/null | awk '
    {
      metric=0
      for (i=1;i<=NF;i++) if ($i=="metric") metric=$(i+1)
      for (i=1;i<=NF;i++) if ($i=="dev") { print metric, $(i+1); break }
    }' | sort -n | awk 'NR==1 {print $2; exit}'
}

iommu_group_count() {
  local n=0
  if [[ -d /sys/kernel/iommu_groups ]]; then
    n="$(find /sys/kernel/iommu_groups -mindepth 1 -maxdepth 1 -type d 2>/dev/null | wc -l)"
  fi
  echo "${n}"
}

load_vfio() {
  local groups
  groups="$(iommu_group_count)"
  local no_iommu="${VFIO_NOIOMMU:-}"
  if [[ -z "${no_iommu}" ]]; then
    if [[ "${groups}" -eq 0 ]]; then
      no_iommu=1
    else
      no_iommu=0
    fi
  fi

  if [[ "${DRIVER}" == "vfio-pci" ]]; then
    if [[ "${no_iommu}" == "1" ]]; then
      warn "IOMMU groups=${groups}; loading vfio with enable_unsafe_noiommu_mode=1"
      modprobe vfio enable_unsafe_noiommu_mode=1
    else
      log "IOMMU groups=${groups}; loading vfio (IOMMU mode)"
      modprobe vfio
    fi
    modprobe vfio_pci
  elif [[ "${DRIVER}" == "uio_pci_generic" ]]; then
    modprobe uio_pci_generic
  else
    die "unsupported DPDK_DRIVER=${DRIVER}"
  fi
}

down_iface_for_pci() {
  local pci="$1"
  local iface
  if iface="$(pci_to_iface "${pci}" 2>/dev/null)"; then
    log "ip link set ${iface} down (${pci})"
    ip link set "${iface}" down || true
    # Drop addresses so DHCP does not fight bind restore later.
    ip -4 addr flush dev "${iface}" 2>/dev/null || true
  else
    log "no kernel iface for ${pci} (already unbound?)"
  fi
}

save_state_line() {
  local pci="$1"
  local old_driver="$2"
  local iface="$3"
  mkdir -p "$(dirname "${STATE_FILE}")"
  # pci|old_driver|iface
  grep -v "^${pci}|" "${STATE_FILE}" 2>/dev/null >"${STATE_FILE}.tmp" || true
  mv "${STATE_FILE}.tmp" "${STATE_FILE}" 2>/dev/null || true
  echo "${pci}|${old_driver}|${iface}" >>"${STATE_FILE}"
}

current_driver() {
  local pci="$1"
  local link="/sys/bus/pci/devices/${pci}/driver"
  if [[ -L "${link}" ]]; then
    basename "$(readlink -f "${link}")"
  else
    echo ""
  fi
}

bind_one() {
  local arg="$1"
  local pci iface old_drv mgmt
  pci="$(resolve_to_pci "${arg}")" || die "cannot resolve PCI/iface: ${arg}"
  iface="$(pci_to_iface "${pci}" 2>/dev/null || true)"
  mgmt="$(mgmt_iface || true)"

  if [[ -n "${iface}" && -n "${mgmt}" && "${iface}" == "${mgmt}" && "${ALLOW_MGMT_BIND:-0}" != "1" ]]; then
    die "refusing to bind management NIC ${iface} (${pci}); set ALLOW_MGMT_BIND=1 to override"
  fi

  old_drv="$(current_driver "${pci}")"
  if [[ "${old_drv}" == "${DRIVER}" ]]; then
    log "already on ${DRIVER}: ${pci}"
    return 0
  fi

  down_iface_for_pci "${pci}"
  save_state_line "${pci}" "${old_drv:-unknown}" "${iface:-}"

  local tool
  tool="$(find_bind_tool)"
  log "bind ${pci} -> ${DRIVER} (was ${old_drv:-none}, iface=${iface:-n/a})"
  "${tool}" -b "${DRIVER}" "${pci}"
}

unbind_one() {
  local arg="$1"
  local pci old_drv iface tool
  pci="$(resolve_to_pci "${arg}")" || {
    # Allow unbind by PCI even if already unbound from netdev.
    pci="$(normalize_pci "${arg}")"
    [[ -n "${pci}" ]] || die "cannot resolve: ${arg}"
  }

  old_drv=""
  iface=""
  if [[ -f "${STATE_FILE}" ]]; then
    local line
    line="$(grep "^${pci}|" "${STATE_FILE}" || true)"
    if [[ -n "${line}" ]]; then
      old_drv="$(echo "${line}" | cut -d'|' -f2)"
      iface="$(echo "${line}" | cut -d'|' -f3)"
    fi
  fi
  [[ -n "${old_drv}" && "${old_drv}" != "unknown" ]] || old_drv="e1000"

  tool="$(find_bind_tool)"
  log "unbind ${pci} -> ${old_drv}"
  "${tool}" -b "${old_drv}" "${pci}" || warn "bind back to ${old_drv} failed for ${pci}"

  if [[ -f "${STATE_FILE}" ]]; then
    grep -v "^${pci}|" "${STATE_FILE}" >"${STATE_FILE}.tmp" || true
    mv "${STATE_FILE}.tmp" "${STATE_FILE}"
  fi

  if [[ -n "${iface}" ]]; then
    ip link set "${iface}" up 2>/dev/null || true
  fi
}

show_status() {
  local tool groups
  tool="$(find_bind_tool)"
  groups="$(iommu_group_count)"
  log "driver=${DRIVER} iommu_groups=${groups} state=${STATE_FILE}"
  log "mgmt_iface=$(mgmt_iface || echo none)"
  echo
  ip -br addr || true
  echo
  "${tool}" --status || true
  if [[ -f "${STATE_FILE}" ]]; then
    echo
    log "saved bind state:"
    sed 's/^/  /' "${STATE_FILE}" || true
  fi
}

cmd_bind() {
  need_root
  [[ "$#" -ge 1 ]] || die "usage: $0 bind <iface|pci> [...]"
  load_vfio
  local a
  for a in "$@"; do
    bind_one "${a}"
  done
  show_status
}

cmd_unbind() {
  need_root
  local targets=("$@")
  if [[ "${#targets[@]}" -eq 0 ]]; then
    [[ -f "${STATE_FILE}" ]] || die "no state file; pass iface/pci to unbind"
    mapfile -t targets < <(cut -d'|' -f1 "${STATE_FILE}")
  fi
  local a
  for a in "${targets[@]}"; do
    [[ -n "${a}" ]] || continue
    unbind_one "${a}"
  done
  show_status
}

cmd_setup() {
  need_root
  [[ "$#" -ge 1 ]] || die "usage: $0 setup <iface|pci> [...]"
  local hp
  hp="$(cat /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages 2>/dev/null || echo 0)"
  if [[ "${hp}" -eq 0 ]]; then
    warn "hugepages=0; run: sudo ./scripts/setup_env.sh hugepages"
  else
    log "hugepages(2MB)=${hp}"
  fi
  cmd_bind "$@"
}

print_help() {
  sed -n '2,22p' "$0"
}

case "${ACTION}" in
  status) show_status ;;
  bind) cmd_bind "$@" ;;
  unbind) cmd_unbind "$@" ;;
  setup) cmd_setup "$@" ;;
  -h|--help|help) print_help ;;
  *) die "unknown action: ${ACTION} (status|bind|unbind|setup)" ;;
esac
