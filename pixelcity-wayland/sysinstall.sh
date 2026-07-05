#!/usr/bin/env bash
# sysinstall.sh - Install PixelCity system-wide (requires root/sudo)

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Ensure project is built
if [ ! -f "${ROOT_DIR}/pixelcity" ]; then
    echo "PixelCity is not built yet. Building now..."
    make -C "${ROOT_DIR}" -j4
fi

# Determine if sudo is needed
SUDO=""
if [ "$(id -u)" -ne 0 ]; then
    if command -v sudo >/dev/null 2>&1; then
        SUDO="sudo"
    else
        echo "WARNING: Not running as root and 'sudo' not found. Trying to proceed without sudo..." >&2
    fi
fi

echo "Installing PixelCity system-wide..."
$SUDO make -C "${ROOT_DIR}" install

# Restore ownership of any files generated as root during installation
if [ "$(id -u)" -ne 0 ] && command -v sudo >/dev/null 2>&1; then
    sudo chown -R "$(id -u):$(id -g)" "${ROOT_DIR}"
fi

echo "System installation complete!"
