#!/usr/bin/env bash
# =============================================================================
# release.sh — Full release pipeline for ISO Drums
#
# Usage:
#   bash scripts/release.sh <version>
#   e.g.  bash scripts/release.sh 0.3.0
#
# What this does:
#   1. Bumps version in CMakeLists.txt
#   2. Commits + tags the version bump on main
#   3. Builds, signs, notarizes, and packages the DMG
#   4. Creates a GitHub Release and uploads the DMG
#   5. Updates the website download links
#   6. Commits + pushes the website update
#
# Requirements:
#   - gh CLI authenticated (gh auth login)
#   - CODESIGN_IDENTITY set (SHA-1 or full cert name)
#   - NOTARIZE_PROFILE set (xcrun notarytool credentials profile)
# =============================================================================
set -euo pipefail

# ── Args & env ────────────────────────────────────────────────────────────────
VERSION="${1:-}"
if [[ -z "${VERSION}" ]]; then
  echo "Usage: bash scripts/release.sh <version>  (e.g. 0.3.0)"
  exit 1
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CMAKELIST="${ROOT_DIR}/CMakeLists.txt"
WEB_INDEX="${ROOT_DIR}/web/index.html"
DMG_NAME="ISO-Drums-macOS-arm64-${VERSION}.dmg"
DMG_PATH="${ROOT_DIR}/dist/${DMG_NAME}"
TAG="v${VERSION}"

if [[ -z "${CODESIGN_IDENTITY:-}" || -z "${NOTARIZE_PROFILE:-}" ]]; then
  echo "Error: CODESIGN_IDENTITY and NOTARIZE_PROFILE must be set."
  echo "  export CODESIGN_IDENTITY=\"<SHA-1 or cert name>\""
  echo "  export NOTARIZE_PROFILE=\"<notarytool profile name>\""
  exit 1
fi

# ── 1. Bump version in CMakeLists.txt ─────────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════"
echo "  Step 1 — Bump version to ${VERSION}"
echo "═══════════════════════════════════════════════"

CURRENT=$(grep -E '^project\(ISODrums VERSION' "${CMAKELIST}" | grep -oE '[0-9]+\.[0-9]+\.[0-9]+')
if [[ "${CURRENT}" == "${VERSION}" ]]; then
  echo "  ▸ Version already at ${VERSION}, skipping bump."
else
  sed -i '' "s/project(ISODrums VERSION ${CURRENT})/project(ISODrums VERSION ${VERSION})/" "${CMAKELIST}"
  echo "  ✓ CMakeLists.txt: ${CURRENT} → ${VERSION}"
fi

# ── 2. Commit version bump + create tag ───────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════"
echo "  Step 2 — Commit version bump and tag ${TAG}"
echo "═══════════════════════════════════════════════"

cd "${ROOT_DIR}"

# Ensure we're on main
BRANCH=$(git branch --show-current)
if [[ "${BRANCH}" != "main" ]]; then
  echo "  ⚠ Not on main (currently on '${BRANCH}'). Switching to main..."
  git checkout main
fi

git add "${CMAKELIST}"
if git diff --cached --quiet; then
  echo "  ▸ Nothing to commit for version bump."
else
  git commit -m "Bump version to v${VERSION}"
fi

# Create tag (skip if already exists)
if git rev-parse "${TAG}" >/dev/null 2>&1; then
  echo "  ▸ Tag ${TAG} already exists, skipping."
else
  git tag -a "${TAG}" -m "ISO Drums ${TAG}"
  echo "  ✓ Created tag ${TAG}"
fi

git push origin main
git push origin "${TAG}"
echo "  ✓ Pushed main and tag ${TAG}"

# ── 3. Build, sign, notarize ──────────────────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════"
echo "  Step 3 — Build, sign, and notarize"
echo "═══════════════════════════════════════════════"

bash "${ROOT_DIR}/scripts/sign_and_package.sh" "${VERSION}"

if [[ ! -f "${DMG_PATH}" ]]; then
  echo "Error: DMG not found at ${DMG_PATH}"
  exit 1
fi
echo "  ✓ DMG ready: dist/${DMG_NAME}"

# ── 4. Create GitHub Release and upload DMG ───────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════"
echo "  Step 4 — Create GitHub Release ${TAG}"
echo "═══════════════════════════════════════════════"

# Delete existing release if it exists (re-run safety)
if gh release view "${TAG}" >/dev/null 2>&1; then
  echo "  ▸ Release ${TAG} already exists, deleting and re-creating..."
  gh release delete "${TAG}" --yes
fi

NOTES_FILE=$(mktemp)
cat > "${NOTES_FILE}" << NOTES
## ISO Drums ${TAG}

macOS Apple Silicon (M1+) — Standalone App, AU & VST3

### What's new
- See commit history for full details.

### Installation
1. Download the DMG below
2. Open the DMG and drag **ISO Drums.app** to Applications
3. Copy **ISO Drums.component** to ~/Library/Audio/Plug-Ins/Components/
4. Copy **ISO Drums.vst3** to ~/Library/Audio/Plug-Ins/VST3/
NOTES

gh release create "${TAG}" \
  --title "ISO Drums ${TAG}" \
  --notes-file "${NOTES_FILE}" \
  "${DMG_PATH}"

rm -f "${NOTES_FILE}"

# Get the published asset URL
DOWNLOAD_URL=$(gh release view "${TAG}" --json assets -q '.assets[] | select(.name | test("'"${DMG_NAME}"'")) | .browserDownloadUrl')

if [[ -z "${DOWNLOAD_URL}" ]]; then
  echo "Error: Could not retrieve download URL from GitHub Release."
  exit 1
fi
echo "  ✓ Download URL: ${DOWNLOAD_URL}"

# ── 5. Update website download links ─────────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════"
echo "  Step 5 — Update website"
echo "═══════════════════════════════════════════════"

# Replace all href="..." that point to a previous download (Dropbox or GitHub)
# Handles both dropbox.com links and previous github releases
sed -i '' -E "s|href=\"https://(www\.dropbox\.com|github\.com/[^\"]+/releases/download)[^\"]*\.(dmg\|zip)[^\"]*\"|href=\"${DOWNLOAD_URL}\"|g" "${WEB_INDEX}"

# Update version string in the pricing tagline and meta line
OLD_VER=$(grep -oE 'v[0-9]+\.[0-9]+\.[0-9]+' "${WEB_INDEX}" | head -1)
if [[ -n "${OLD_VER}" ]]; then
  sed -i '' "s/${OLD_VER}/v${VERSION}/g" "${WEB_INDEX}"
  echo "  ✓ Version string updated: ${OLD_VER} → v${VERSION}"
fi

echo "  ✓ website download links updated"

# ── 6. Commit + push website update ──────────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════"
echo "  Step 6 — Commit and push website update"
echo "═══════════════════════════════════════════════"

git add "${WEB_INDEX}"
if git diff --cached --quiet; then
  echo "  ▸ No website changes to commit."
else
  git commit -m "Update website download link to ${TAG}"
  git push origin main
  echo "  ✓ Website update pushed"
fi

# ── Summary ───────────────────────────────────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════"
echo "  ISO Drums ${TAG} — Release Complete"
echo "═══════════════════════════════════════════════"
echo ""
echo "  GitHub Release : https://github.com/Plait-Audio/plait/releases/tag/${TAG}"
echo "  Download URL   : ${DOWNLOAD_URL}"
echo "  DMG size       : $(du -sh "${DMG_PATH}" | cut -f1)"
echo ""
