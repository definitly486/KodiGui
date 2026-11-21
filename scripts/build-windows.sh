#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

if ! command -v qmake >/dev/null 2>&1; then
    echo "Qt 6 qmake was not found. Use an MSYS2 MinGW 64-bit shell and install Qt 6." >&2
    exit 1
fi

if ! command -v pkg-config >/dev/null 2>&1 || ! pkg-config --exists libssh2; then
    echo "libssh2 development files were not found (install mingw-w64-x86_64-libssh2)." >&2
    exit 1
fi

MAKE_BIN=""
for candidate in mingw32-make make; do
    if command -v "${candidate}" >/dev/null 2>&1; then
        MAKE_BIN="${candidate}"
        break
    fi
done
if [[ -z "${MAKE_BIN}" ]]; then
    echo "GNU make for MinGW was not found." >&2
    exit 1
fi

qmake kodigui.pro "CONFIG+=release"
"${MAKE_BIN}" -j"$(nproc)"

if [[ ! -f "${ROOT_DIR}/kodigui.exe" ]]; then
    echo "Build finished but ./kodigui.exe was not produced." >&2
    exit 1
fi

echo "Build successful: ${ROOT_DIR}/kodigui.exe"
