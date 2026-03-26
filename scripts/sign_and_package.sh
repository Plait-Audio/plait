#!/usr/bin/env bash
# =============================================================================
# sign_and_package.sh — Build, sign, notarize, and create a .dmg for ISO Drums
#
# Usage:
#   bash scripts/sign_and_package.sh [version]
#
# Environment variables (required for signed builds):
#   CODESIGN_IDENTITY    — Developer ID cert name OR SHA-1 hash
#                          e.g. "Developer ID Application: Your Name (TEAMID)"
#                          or   "FD57D2490702A70FA0B6BDA0400851733930FEC4"
#                          Using SHA-1 avoids "ambiguous" errors when the cert
#                          exists in multiple keychains.
#   NOTARIZE_PROFILE     — notarytool credentials profile name
#                          (created via: xcrun notarytool store-credentials)
#
# If these are unset, the script still builds and packages (ad-hoc signed).
# =============================================================================
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${1:-0.1.0}"
DIST_NAME="ISO-Drums-macOS-arm64-${VERSION}"
DIST_DIR="${ROOT_DIR}/dist/${DIST_NAME}"
BUILD_DIR="${ROOT_DIR}/build"
ARTEFACTS="${BUILD_DIR}/ISODrums_artefacts/Release"
ENTITLEMENTS="${ROOT_DIR}/Resources/ISODrums.entitlements"
DMG_PATH="${ROOT_DIR}/dist/${DIST_NAME}.dmg"

IDENTITY="${CODESIGN_IDENTITY:-}"
PROFILE="${NOTARIZE_PROFILE:-}"

# If CODESIGN_IDENTITY looks like a name and is ambiguous, resolve to SHA-1
if [[ -n "${IDENTITY}" && ! "${IDENTITY}" =~ ^[0-9A-Fa-f]{40}$ ]]; then
  HASH=$(security find-identity -v -p codesigning | grep "${IDENTITY}" | head -1 | awk '{print $2}')
  if [[ -n "${HASH}" ]]; then
    echo "▸ Resolved certificate to SHA-1: ${HASH}"
    IDENTITY="${HASH}"
  fi
fi

# ── Build ────────────────────────────────────────────────────────────────────
echo "▸ Building ISO Drums ${VERSION}..."
export CODESIGN_IDENTITY="${IDENTITY}"
export ENTITLEMENTS="${ENTITLEMENTS}"
cmake -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release "${ROOT_DIR}"
cmake --build "${BUILD_DIR}" --config Release -j "$(sysctl -n hw.logicalcpu)"

# ── Assemble dist folder ─────────────────────────────────────────────────────
echo "▸ Assembling dist/${DIST_NAME}..."
rm -rf "${DIST_DIR}"
mkdir -p "${DIST_DIR}"

cp -R "${ARTEFACTS}/Standalone/ISO Drums.app"   "${DIST_DIR}/"
cp -R "${ARTEFACTS}/AU/ISO Drums.component"      "${DIST_DIR}/"
cp -R "${ARTEFACTS}/VST3/ISO Drums.vst3"         "${DIST_DIR}/"

# ── Code sign ─────────────────────────────────────────────────────────────────
if [[ -n "${IDENTITY}" ]]; then
  echo "▸ Signing with: ${IDENTITY}"

  sign_bundle() {
    local bundle="$1"
    # Sign frameworks/dylibs first
    if [[ -d "${bundle}/Contents/Frameworks" ]]; then
      for lib in "${bundle}/Contents/Frameworks"/*.dylib; do
        [[ -f "${lib}" ]] || continue
        codesign --force --sign "${IDENTITY}" --options runtime --timestamp "${lib}"
      done
    fi
    # Sign the bundle itself
    codesign --force --sign "${IDENTITY}" --options runtime --timestamp \
             --entitlements "${ENTITLEMENTS}" --deep "${bundle}"
  }

  sign_bundle "${DIST_DIR}/ISO Drums.app"
  sign_bundle "${DIST_DIR}/ISO Drums.component"
  sign_bundle "${DIST_DIR}/ISO Drums.vst3"

  echo "▸ Verifying signatures..."
  codesign --verify --deep --strict "${DIST_DIR}/ISO Drums.app"
  codesign --verify --deep --strict "${DIST_DIR}/ISO Drums.component"
  codesign --verify --deep --strict "${DIST_DIR}/ISO Drums.vst3"
  echo "  ✓ All signatures valid"
else
  echo "▸ CODESIGN_IDENTITY not set — skipping signing (ad-hoc only)"
fi

# ── Create DMG ────────────────────────────────────────────────────────────────
echo "▸ Creating DMG..."
rm -f "${DMG_PATH}"

# Create a temporary DMG with a nice layout
TEMP_DMG="${ROOT_DIR}/dist/_temp_${DIST_NAME}.dmg"
DMG_VOLUME="ISO Drums ${VERSION}"

hdiutil create -volname "${DMG_VOLUME}" \
  -srcfolder "${DIST_DIR}" \
  -ov -format UDRW "${TEMP_DMG}"

# Mount, add Applications symlink, unmount
MOUNT_DIR=$(hdiutil attach -readwrite -noverify "${TEMP_DMG}" | tail -1 | awk '{print $3}')
ln -sf /Applications "${MOUNT_DIR}/Applications"
hdiutil detach "${MOUNT_DIR}" -quiet

# Convert to compressed read-only DMG
hdiutil convert "${TEMP_DMG}" -format UDZO -imagekey zlib-level=9 -o "${DMG_PATH}"
rm -f "${TEMP_DMG}"

echo "  ✓ DMG: dist/${DIST_NAME}.dmg"

# ── Notarize ──────────────────────────────────────────────────────────────────
if [[ -n "${IDENTITY}" && -n "${PROFILE}" ]]; then
  echo "▸ Submitting for notarization..."
  xcrun notarytool submit "${DMG_PATH}" \
    --keychain-profile "${PROFILE}" \
    --wait

  echo "▸ Stapling notarization ticket..."
  xcrun stapler staple "${DMG_PATH}"
  echo "  ✓ Notarization complete"
elif [[ -n "${IDENTITY}" ]]; then
  echo "▸ NOTARIZE_PROFILE not set — skipping notarization"
  echo "  To notarize later:"
  echo "    xcrun notarytool submit \"${DMG_PATH}\" --keychain-profile YOUR_PROFILE --wait"
  echo "    xcrun stapler staple \"${DMG_PATH}\""
fi

# ── Also create a zip (useful for Sparkle / direct downloads) ─────────────────
ZIP_PATH="${ROOT_DIR}/dist/${DIST_NAME}.zip"
rm -f "${ZIP_PATH}"
cd "${ROOT_DIR}/dist"
zip -r --symlinks "${DIST_NAME}.zip" "${DIST_NAME}"

# ── Summary ───────────────────────────────────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════════"
echo "  ISO Drums ${VERSION} — Package Ready"
echo "═══════════════════════════════════════════════════"
echo "  DMG: dist/${DIST_NAME}.dmg"
echo "  ZIP: dist/${DIST_NAME}.zip"
ls -lh "${DMG_PATH}" "${ZIP_PATH}"
echo ""
if [[ -z "${IDENTITY}" ]]; then
  echo "  ⚠ Not signed. Recipients must run:"
  echo "    xattr -cr \"/path/to/ISO Drums.app\""
fi
