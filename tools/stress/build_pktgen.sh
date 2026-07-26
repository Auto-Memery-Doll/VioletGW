#!/usr/bin/env bash
# Build Pktgen-DPDK with Lua into ~/pktgen/builddir/app/pktgen
#
# Ubuntu libdpdk-dev 23.11.x does not ship rte_ip6.h (split in DPDK 24.11).
# Pktgen 25.08+ requires that header, so default to pktgen-23.10.2 for apt DPDK 23.11.
set -euo pipefail

PKTGEN_SRC="${PKTGEN_SRC:-${HOME}/pktgen}"
BUILD_DIR="${PKTGEN_BUILD_DIR:-${PKTGEN_SRC}/builddir}"
BIN="${BUILD_DIR}/app/pktgen"
PKTGEN_TAG="${PKTGEN_TAG:-pktgen-23.10.2}"
STAMP="${BUILD_DIR}/.pktgen_tag"

if [[ ! -d "${PKTGEN_SRC}/.git" ]]; then
  echo "Clone first: git clone https://github.com/pktgen/Pktgen-DPDK.git ${PKTGEN_SRC}" >&2
  exit 1
fi

for cmd in meson ninja pkg-config git; do
  if ! command -v "${cmd}" >/dev/null; then
    echo "Missing ${cmd}. Install:" >&2
    echo "  sudo apt install meson ninja-build libbsd-dev liblua5.4-dev libpcap-dev git" >&2
    exit 1
  fi
done

dpdk_ver="$(pkg-config --modversion libdpdk 2>/dev/null || true)"
if [[ -n "${dpdk_ver}" && "${PKTGEN_TAG}" == "pktgen-23.10.2" ]]; then
  case "${dpdk_ver}" in
    24.11.*|25.*|26.*)
      echo "Note: DPDK ${dpdk_ver} detected; override PKTGEN_TAG (e.g. pktgen-26.03.0) if needed." >&2
      ;;
  esac
fi

cd "${PKTGEN_SRC}"
git fetch --tags origin 2>/dev/null || true
if ! git rev-parse --verify "${PKTGEN_TAG}^{commit}" >/dev/null 2>&1; then
  echo "Unknown PKTGEN_TAG=${PKTGEN_TAG}. Run: git -C ${PKTGEN_SRC} fetch --tags origin" >&2
  exit 1
fi
git checkout "${PKTGEN_TAG}" -f

need_setup=0
if [[ ! -f "${BUILD_DIR}/build.ninja" ]]; then
  need_setup=1
elif [[ ! -f "${STAMP}" ]] || [[ "$(cat "${STAMP}")" != "${PKTGEN_TAG}" ]]; then
  echo "Pktgen tag changed; reconfiguring ${BUILD_DIR}" >&2
  rm -rf "${BUILD_DIR}"
  need_setup=1
fi

if [[ "${need_setup}" -eq 1 ]]; then
  meson setup "${BUILD_DIR}" -Denable_lua=true -Dbuildtype=release
  echo "${PKTGEN_TAG}" >"${STAMP}"
fi

meson compile -C "${BUILD_DIR}"

if [[ ! -x "${BIN}" ]]; then
  echo "Build failed: ${BIN} not found" >&2
  exit 1
fi

echo "OK: ${BIN} (tag=${PKTGEN_TAG}, dpdk=${dpdk_ver:-unknown})"
