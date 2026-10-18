#!/usr/bin/env bash
# ==============================================================================
# package_mac.sh - Builds IDM and creates a standalone .dmg on macOS
# ==============================================================================
set -e

QT_PREFIX=$(brew --prefix qt@6 2>/dev/null || echo "/usr/local/opt/qt@6")

echo "==> [1/3] Configuring CMake for macOS using Qt at $QT_PREFIX..."
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT_PREFIX"

echo "==> [2/3] Building IDMan.app bundle..."
cmake --build build --target idm_app -j$(sysctl -n hw.ncpu)

echo "==> [3/3] Running macdeployqt to bundle frameworks and create .dmg..."
"$QT_PREFIX/bin/macdeployqt" build/IDMan.app -dmg
mv build/IDMan.dmg build/IDMan_macOS_v6.42.dmg

echo ""
echo "[SUCCESS] macOS Disk Image created:"
ls -lh build/IDMan_macOS_v6.42.dmg
