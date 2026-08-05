#!/usr/bin/env bash
# =============================================================================
# make_dmg.sh — Build the branded ISO Drums installer DMG from signed bundles.
#
# Usage:
#   bash scripts/make_dmg.sh <src_dir> <version> <dmg_out>
#
#   <src_dir>  folder containing "ISO Drums.app", "ISO Drums.component",
#              "ISO Drums.vst3" (already code-signed).
#   <version>  e.g. 0.3.1  (used for the volume name)
#   <dmg_out>  path of the final compressed .dmg to write.
#
# Uses dmgbuild (https://dmgbuild.readthedocs.io) for a deterministic styled
# window — background art, icon layout, and the Applications drop-link — with
# no fragile Finder/AppleScript automation. The retina background lives at
# Resources/dmg/background.tiff (regenerate with scripts/make_dmg_background.py
# + tiffutil if you edit the layout). Layout/positions are in dmg_settings.py.
#
# Does NOT sign or notarize: the caller signs the bundles beforehand and
# notarizes the resulting DMG.
# =============================================================================
set -euo pipefail

SRC_DIR="$1"
VERSION="$2"
DMG_OUT="$3"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BG_TIFF="${ROOT_DIR}/Resources/dmg/background.tiff"
SETTINGS="${ROOT_DIR}/scripts/dmg_settings.py"
VOL="ISO Drums ${VERSION}"

[[ -f "${BG_TIFF}"  ]] || { echo "Error: missing ${BG_TIFF} (run scripts/make_dmg_background.py then tiffutil)"; exit 1; }
[[ -f "${SETTINGS}" ]] || { echo "Error: missing ${SETTINGS}"; exit 1; }

# ── Bootstrap dmgbuild into a local venv (build/ is gitignored) ───────────
VENV="${ROOT_DIR}/build/dmg-venv"
if [[ ! -x "${VENV}/bin/dmgbuild" ]]; then
  echo "▸ Bootstrapping dmgbuild into ${VENV}..."
  python3 -m venv "${VENV}"
  "${VENV}/bin/pip" install --quiet --upgrade pip >/dev/null
  "${VENV}/bin/pip" install --quiet dmgbuild >/dev/null
fi

echo "▸ Building branded DMG with dmgbuild..."
rm -f "${DMG_OUT}"
"${VENV}/bin/dmgbuild" \
  -s "${SETTINGS}" \
  -D srcdir="${SRC_DIR}" \
  -D bg="${BG_TIFF}" \
  "${VOL}" "${DMG_OUT}"

echo "  ✓ Branded DMG: ${DMG_OUT}"
