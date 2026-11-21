#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 /path/to/your-authorized-device.wvd" >&2
    exit 2
fi

WVD_PATH=$(realpath "$1")
if [ ! -s "$WVD_PATH" ]; then
    echo "Error: WVD file does not exist or is empty: $WVD_PATH" >&2
    exit 1
fi

for tool in git cargo clang++; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Error: required tool not found: $tool" >&2
        exit 1
    fi
done

# OpenWV uses Rust Edition 2024, which requires Cargo/Rust 1.85 or newer.
# Prefer an installed rustup stable toolchain when the system Cargo is too old.
CARGO_RUN="cargo"
CARGO_VERSION=$(cargo --version | sed -E 's/^cargo ([0-9]+)\\.([0-9]+).*/\\1 \\2/')
CARGO_MAJOR=$(printf '%s\\n' "$CARGO_VERSION" | awk '{print $1}')
CARGO_MINOR=$(printf '%s\\n' "$CARGO_VERSION" | awk '{print $2}')
if [ "$CARGO_MAJOR" -lt 1 ] || { [ "$CARGO_MAJOR" -eq 1 ] && [ "$CARGO_MINOR" -lt 85 ]; }; then
    if command -v rustup >/dev/null 2>&1 && rustup toolchain list | grep -q '^stable'; then
        echo "[OpenWV] System Cargo is too old; using the installed rustup stable toolchain."
        CARGO_RUN="rustup run stable cargo"
    else
        echo "Error: OpenWV requires Rust/Cargo 1.85 or newer (Rust Edition 2024)." >&2
        echo "Your version: $(cargo --version)" >&2
        echo "Install/update stable Rust, then retry:" >&2
        echo "  rustup toolchain install stable" >&2
        echo "  rustup default stable" >&2
        exit 1
    fi
fi

BUILD_ROOT=$(mktemp -d /tmp/kodigui-openwv.XXXXXX)
cleanup() {
    rm -rf "$BUILD_ROOT"
}
trap cleanup EXIT HUP INT TERM

SRC="$BUILD_ROOT/openwv"
echo "[OpenWV] Cloning source and submodules..."
git clone --recurse-submodules -c submodule.recurse=true --depth 1 \
    https://github.com/tchebb/openwv.git "$SRC"

# OpenWV embeds the device identity at compile time. Keep the input out of the
# KodiGui repository and remove the temporary source tree when this script exits.
install -m 600 "$WVD_PATH" "$SRC/embedded.wvd"

echo "[OpenWV] Building release CDM..."
(
    cd "$SRC"
    $CARGO_RUN build --release --locked
)

LIB="$SRC/target/release/libwidevinecdm.so"
if [ ! -s "$LIB" ]; then
    echo "Error: OpenWV build did not produce $LIB" >&2
    exit 1
fi

DEST="$HOME/.local/share/KodiGui/widevine"
install -d -m 700 "$DEST"
install -m 755 "$LIB" "$DEST/libwidevinecdm.so"

echo
echo "[OpenWV] Installed: $DEST/libwidevinecdm.so"
echo "[OpenWV] The device identity is embedded in this library."
echo "[OpenWV] Keep this binary private; do not commit or publish it."
echo "[OpenWV] Restart KodiGui to make Qt WebEngine load the CDM."
