#!/usr/bin/env bash
# VGW (VioletGW) environment setup
#
# Usage:
#   ./scripts/setup_env.sh              # install deps + hugepages + verify
#   ./scripts/setup_env.sh deps         # apt packages + libdpdk-dev only
#   ./scripts/setup_env.sh hugepages    # reserve 2MB hugepages
#   ./scripts/setup_env.sh verify       # check toolchain / DPDK / hugepages
#   ./scripts/setup_env.sh build        # cmake configure + build
#   ./scripts/setup_env.sh all          # deps + hugepages + verify + build
#
# Go (for tools/vgwcp): installed under ~/sdk/go; see ~/.bashrc "VGW Go env".
#   export PATH="$HOME/sdk/go/bin:$HOME/go/bin:$PATH"
#
# Optional env:
#   HUGEPAGES=1024          # number of 2MB pages (default: 1024 => ~2GB)
#   BUILD_DIR=build         # cmake out-of-source dir
#   SKIP_APT=1              # skip apt install
#   BIND_NICS="ens35 ens36" # optional: bind listed NICs via setup_dpdk_nics.sh (vfio-pci)
#                           # NEVER bind the SSH/management NIC

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build}"
HUGEPAGES="${HUGEPAGES:-1024}"
ACTION="${1:-default}"

log()  { printf '[setup] %s\n' "$*"; }
warn() { printf '[setup][WARN] %s\n' "$*" >&2; }
die()  { printf '[setup][ERROR] %s\n' "$*" >&2; exit 1; }

need_root() {
  if [[ "${EUID}" -ne 0 ]]; then
    die "this step needs root (try: sudo $0 $ACTION)"
  fi
}

run_sudo() {
  if [[ "${EUID}" -eq 0 ]]; then
    "$@"
    return
  fi
  if sudo -n true 2>/dev/null; then
    sudo "$@"
    return
  fi
  die "need root privileges. re-run as:
  sudo $0 ${ACTION}
or configure passwordless sudo for apt/sysctl."
}

install_deps() {
  if [[ "${SKIP_APT:-0}" == "1" ]]; then
    log "SKIP_APT=1, skip apt packages"
    return 0
  fi

  log "installing apt packages (build tools, Boost, DPDK)..."
  run_sudo apt-get update -y
  # Use env=... so sudo does not treat the assignment as a command name.
  run_sudo env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    pkgconf \
    pkg-config \
    python3 \
    python3-pyelftools \
    meson \
    git \
    pciutils \
    ethtool \
    libnuma-dev \
    libpcap-dev \
    libssl-dev \
    libelf-dev \
    zlib1g-dev \
    libboost-filesystem-dev \
    libjitterentropy3-dev \
    libdpdk-dev \
    dpdk \
    dpdk-dev

  # Optional: kernel UIO helpers for NIC bind experiments
  run_sudo env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    linux-headers-"$(uname -r)" || warn "linux-headers install failed (ok for compile-only)"

  log "deps installed"
}

setup_hugepages() {
  log "configuring ${HUGEPAGES} x 2MB hugepages..."
  local hp_path="/sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages"
  [[ -f "${hp_path}" ]] || die "2MB hugepage sysfs not found: ${hp_path}"

  run_sudo bash -c "echo ${HUGEPAGES} > '${hp_path}'"
  local cur
  cur="$(cat "${hp_path}")"
  log "nr_hugepages=${cur}"

  if ! mount | grep -q 'hugetlbfs'; then
    run_sudo mkdir -p /dev/hugepages
    run_sudo mount -t hugetlbfs nodev /dev/hugepages || warn "hugetlbfs mount failed"
  fi

  # Persist for next boot (best-effort)
  local conf="/etc/sysctl.d/99-violetgw-hugepages.conf"
  run_sudo bash -c "cat > '${conf}' <<EOF
# VioletGW / DPDK 2MB hugepages
vm.nr_hugepages = ${HUGEPAGES}
EOF"
  run_sudo sysctl --system >/dev/null 2>&1 || true
  log "hugepages ready (also wrote ${conf})"
}

maybe_bind_nics() {
  if [[ -z "${BIND_NICS:-}" ]]; then
    log "BIND_NICS not set; skip NIC bind"
    log "  physical: sudo ./scripts/setup_dpdk_nics.sh setup ens35 ens36"
    return 0
  fi

  local nic_script="${ROOT_DIR}/scripts/setup_dpdk_nics.sh"
  [[ -x "${nic_script}" ]] || die "missing ${nic_script}"
  # shellcheck disable=SC2086
  run_sudo "${nic_script}" setup ${BIND_NICS}
}

verify_env() {
  log "verifying environment..."
  local ok=1

  for cmd in cmake g++ pkg-config; do
    if command -v "${cmd}" >/dev/null 2>&1; then
      log "  OK  ${cmd}: $(command -v "${cmd}")"
    else
      warn "  MISSING ${cmd}"
      ok=0
    fi
  done

  if pkg-config --exists libdpdk; then
    log "  OK  libdpdk: $(pkg-config --modversion libdpdk)"
    log "  OK  cflags: $(pkg-config --cflags libdpdk | tr '\n' ' ' | cut -c1-120)..."
  else
    warn "  MISSING libdpdk (pkg-config)"
    ok=0
  fi

  if pkg-config --exists libdpdk; then
    local static_libs
    static_libs="$(pkg-config --static --libs libdpdk 2>/dev/null || true)"
    if [[ "${static_libs}" == *"--whole-archive"* ]]; then
      log "  OK  static libs contain --whole-archive"
    else
      warn "  static libs may miss --whole-archive (CMake may fail)"
      ok=0
    fi
  fi

  if [[ -f /usr/include/boost/filesystem.hpp ]] || \
     ls /usr/include/boost*/boost/filesystem.hpp >/dev/null 2>&1; then
    log "  OK  Boost filesystem headers present"
  else
    warn "  MISSING Boost filesystem headers"
    ok=0
  fi

  if [[ -f /usr/lib/x86_64-linux-gnu/libjitterentropy.a ]] || \
     [[ -f /usr/lib/x86_64-linux-gnu/libjitterentropy.so ]]; then
    log "  OK  libjitterentropy present"
  else
    warn "  MISSING libjitterentropy (apt: libjitterentropy3-dev)"
    ok=0
  fi

  if command -v git >/dev/null 2>&1; then
    log "  OK  git: $(command -v git) (needed for FetchContent spdlog)"
  else
    warn "  MISSING git (spdlog FetchContent may fail)"
    ok=0
  fi

  local hp
  hp="$(cat /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages 2>/dev/null || echo 0)"
  if [[ "${hp}" -gt 0 ]]; then
    log "  OK  hugepages(2MB)=${hp}"
  else
    warn "  hugepages(2MB)=0 (DPDK runtime may fail; run: $0 hugepages)"
  fi

  log "  branch: $(git -C "${ROOT_DIR}" branch --show-current 2>/dev/null || echo unknown)"
  log "  NICs:"
  ip -br addr show | sed 's/^/    /' || true

  if [[ "${ok}" -eq 1 ]]; then
    log "verify PASSED"
  else
    die "verify FAILED (fix missing items above)"
  fi
}

build_project() {
  command -v cmake >/dev/null 2>&1 || die "cmake not found; run: $0 deps"
  pkg-config --exists libdpdk || die "libdpdk not found; run: $0 deps"

  log "configuring CMake in ${BUILD_DIR}..."
  # Drop stale cache from a different machine/path (common after repo copy).
  if [[ -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    local cached_home
    cached_home="$(grep -E '^CMAKE_HOME_DIRECTORY:' "${BUILD_DIR}/CMakeCache.txt" | cut -d= -f2- || true)"
    if [[ -n "${cached_home}" && "${cached_home}" != "${ROOT_DIR}" ]]; then
      warn "stale CMakeCache (${cached_home}); cleaning ${BUILD_DIR}"
      rm -rf "${BUILD_DIR}"
    fi
  fi
  mkdir -p "${BUILD_DIR}"
  cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo

  log "building..."
  cmake --build "${BUILD_DIR}" -j"$(nproc)"
  log "build finished: ${BUILD_DIR}"
}

print_usage() {
  sed -n '2,20p' "$0"
}

case "${ACTION}" in
  default)
    install_deps
    setup_hugepages
    maybe_bind_nics
    verify_env
    log "done. next: $0 build"
    ;;
  deps)
    install_deps
    ;;
  hugepages)
    setup_hugepages
    ;;
  bind)
    maybe_bind_nics
    ;;
  verify)
    verify_env
    ;;
  build)
    build_project
    ;;
  all)
    install_deps
    setup_hugepages
    maybe_bind_nics
    verify_env
    build_project
    ;;
  -h|--help|help)
    print_usage
    ;;
  *)
    die "unknown action: ${ACTION} (try --help)"
    ;;
esac
