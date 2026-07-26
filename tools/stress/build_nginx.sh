#!/usr/bin/env bash
# Build stream-only nginx from ~/nginx into ~/nginx/install (no root required).
set -euo pipefail

SRC="${NGINX_SRC:-${HOME}/nginx}"
PREFIX="${NGINX_PREFIX:-${HOME}/nginx/install}"

if [[ ! -d "${SRC}" ]]; then
  echo "nginx source not found at ${SRC}" >&2
  exit 1
fi

cd "${SRC}"
make distclean 2>/dev/null || rm -rf objs
./auto/configure --prefix="${PREFIX}" --without-http --with-stream
make -j"$(nproc)"
make install
echo "Installed: ${PREFIX}/sbin/nginx"
"${PREFIX}/sbin/nginx" -v
