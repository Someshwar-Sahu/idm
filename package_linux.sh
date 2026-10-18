#!/usr/bin/env bash
# ==============================================================================
# package_linux.sh - Builds IDM and packages .deb and .tar.gz on Linux
# ==============================================================================
set -e

echo "==> [1/3] Configuring CMake for Linux..."
cmake -B build -DCMAKE_BUILD_TYPE=Release

echo "==> [2/3] Building IDMan..."
cmake --build build --target idm_app -j$(nproc)

echo "==> [3/3] Packaging Debian (.deb) package..."
cd build
cpack -G "DEB;TGZ"

echo ""
echo "[SUCCESS] Linux packages generated in build/:"
ls -lh idman-*.*
