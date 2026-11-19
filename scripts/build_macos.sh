#!/usr/bin/env bash
# Build AEROSOUND on macOS (Universal 2: arm64 + x86_64) with Xcode.
# Usage: scripts/build_macos.sh [Release|Debug] [path/to/AAX_SDK]
set -euo pipefail
CONFIG="${1:-Release}"
AAX_SDK="${2:-}"
cd "$(dirname "$0")/.."

ARGS=(-S . -B build-mac -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0)
[[ -n "$AAX_SDK" ]] && ARGS+=(-DAEROGLOW_AAX_SDK_PATH="$AAX_SDK")
[[ -n "${JUCE_DIR:-}" ]] && ARGS+=(-DJUCE_DIR="$JUCE_DIR")

cmake "${ARGS[@]}"
cmake --build build-mac --config "$CONFIG" --parallel
ctest --test-dir build-mac -C "$CONFIG" --output-on-failure

echo
echo "Artefacts:"
find build-mac/AeroSound_artefacts/"$CONFIG" -maxdepth 2 -name "*.vst3" -o -maxdepth 2 -name "*.component" -o -maxdepth 2 -name "*.aaxplugin" -o -maxdepth 2 -name "*.app"
