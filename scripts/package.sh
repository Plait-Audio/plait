#!/usr/bin/env bash
# Build and package ISO Drums for distribution.
# Usage: bash scripts/package.sh [version]
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${1:-0.1.0}"
DIST_NAME="ISO-Drums-macOS-arm64-${VERSION}"
DIST_DIR="${ROOT_DIR}/dist/${DIST_NAME}"
BUILD_DIR="${ROOT_DIR}/build"
ARTEFACTS="${BUILD_DIR}/ISODrums_artefacts/Release"

# ---- Ensure a release build is up to date ----
echo "Building ISO Drums ${VERSION}..."
cmake -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release "${ROOT_DIR}" -DISO_DRUMS_VERSION="${VERSION}"
cmake --build "${BUILD_DIR}" --config Release -j "$(sysctl -n hw.logicalcpu)"

# ---- Assemble dist folder ----
echo "Assembling dist/${DIST_NAME}..."
rm -rf "${DIST_DIR}"
mkdir -p "${DIST_DIR}"

cp -R "${ARTEFACTS}/Standalone/ISO Drums.app"         "${DIST_DIR}/"
cp -R "${ARTEFACTS}/AU/ISO Drums.component"            "${DIST_DIR}/"
cp -R "${ARTEFACTS}/VST3/ISO Drums.vst3"               "${DIST_DIR}/"
cp    "${ROOT_DIR}/INSTALL.txt"                        "${DIST_DIR}/"

# ---- Zip ----
ZIP_PATH="${ROOT_DIR}/dist/${DIST_NAME}.zip"
rm -f "${ZIP_PATH}"
cd "${ROOT_DIR}/dist"
zip -r --symlinks "${DIST_NAME}.zip" "${DIST_NAME}"

echo ""
echo "Package ready: dist/${DIST_NAME}.zip"
echo "Contents:"
zipinfo -1 "${DIST_NAME}.zip" | sed -n '1,20p'
