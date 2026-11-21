#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

if command -v qmake6 >/dev/null 2>&1; then
    QMAKE=qmake6
elif command -v qmake >/dev/null 2>&1; then
    QMAKE=qmake
else
    echo "Qt 6 qmake was not found (install qt6-base-dev)." >&2
    exit 1
fi

if ! pkg-config --exists libssh2; then
    echo "libssh2 development files were not found (install libssh2-1-dev)." >&2
    exit 1
fi

"${QMAKE}" kodigui.pro
make -j"$(getconf _NPROCESSORS_ONLN)"

if [[ ! -x "${ROOT_DIR}/kodigui" ]]; then
    echo "Build finished but ./kodigui was not produced." >&2
    exit 1
fi

echo "Build successful: ${ROOT_DIR}/kodigui"
