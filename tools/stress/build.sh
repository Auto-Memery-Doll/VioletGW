#!/usr/bin/env bash
# Build DPDK pktgen for stress.
#
#   ./build.sh
#   ./build.sh pktgen
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=env.sh
source "${DIR}/env.sh"

ensure_lua_pkgconfig() {
  if pkg-config --exists lua5.4 || pkg-config --exists lua; then
    return 0
  fi
  local pc="${DEV_HOME}/.local/lib/pkgconfig"
  if [[ -f "${pc}/lua5.4.pc" ]] || [[ -f "${pc}/lua.pc" ]]; then
    export PKG_CONFIG_PATH="${pc}${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
    return 0
  fi
  echo "Missing Lua (liblua5.4-dev or ${DEV_HOME}/.local with lua5.4.pc)" >&2
  echo "  sudo apt install -y liblua5.4-dev" >&2
  exit 1
}

build_pktgen() {
  local tag="${PKTGEN_TAG:-}"
  local dpdk_ver
  dpdk_ver="$(pkg-config --modversion libdpdk 2>/dev/null || true)"
  if [[ -z "${tag}" ]]; then
    case "${dpdk_ver}" in
      25.*|26.*) tag="pktgen-26.03.0" ;;
      24.11.*) tag="pktgen-25.07.0" ;;
      *) tag="pktgen-23.10.2" ;;
    esac
  fi

  [[ -d "${PKTGEN_SRC}/.git" ]] || {
    echo "Clone: git clone https://github.com/pktgen/Pktgen-DPDK.git ${PKTGEN_SRC}" >&2
    exit 1
  }

  for cmd in meson ninja pkg-config git; do
    command -v "${cmd}" >/dev/null || {
      echo "Missing ${cmd}" >&2
      exit 1
    }
  done
  ensure_lua_pkgconfig

  local build_dir="${PKTGEN_SRC}/builddir"
  local stamp="${build_dir}/.pktgen_tag"
  cd "${PKTGEN_SRC}"
  git fetch --tags origin 2>/dev/null || true
  git checkout "${tag}" -f

  # DPDK 25.11 dropped RTE_ETHDEV_QUEUE_STAT_CNTRS from public headers.
  local meson_args=(-Denable_lua=true -Dbuildtype=release)
  if ! echo '#include <rte_ethdev.h>' | \
      cc $(pkg-config --cflags libdpdk) -E - 2>/dev/null | \
      grep -q 'RTE_ETHDEV_QUEUE_STAT_CNTRS'; then
    meson_args+=(-Dc_args=-DRTE_ETHDEV_QUEUE_STAT_CNTRS=16)
  fi

  if [[ ! -f "${build_dir}/build.ninja" ]] || \
     [[ ! -f "${stamp}" ]] || [[ "$(cat "${stamp}")" != "${tag}" ]]; then
    rm -rf "${build_dir}"
    meson setup "${build_dir}" "${meson_args[@]}"
    echo "${tag}" >"${stamp}"
  fi
  meson compile -C "${build_dir}"
  [[ -x "${PKTGEN_BIN}" ]] || { echo "pktgen binary missing" >&2; exit 1; }
  echo "OK pktgen: ${PKTGEN_BIN} (tag=${tag}, dpdk=${dpdk_ver:-unknown})"
}

case "${1:-pktgen}" in
  pktgen) build_pktgen ;;
  *)
    echo "Usage: $0 [pktgen]" >&2
    exit 1
    ;;
esac
