#!/usr/bin/env bash
# userinstall.sh - Install PixelCity locally for the current user

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Ensure project is built
if [ ! -f "${ROOT_DIR}/pixelcity" ]; then
    echo "PixelCity is not built yet. Building now..."
    make -C "${ROOT_DIR}" -j4
fi

echo "Installing PixelCity for the current user (local directory: ~/.local)..."
make -C "${ROOT_DIR}" install-user

echo "Updating desktop database and reloading panel..."
update-desktop-database ~/.local/share/applications || true
if [ -d ~/.local/share/icons/hicolor ]; then
    gtk-update-icon-cache -f -t ~/.local/share/icons/hicolor || true
fi
if command -v lxpanelctl >/dev/null 2>&1; then
    lxpanelctl restart || true
fi

echo "User installation complete!"
