#!/bin/bash
# Bundle libonnxruntime into a JUCE .component / .vst3 / .app so the plug-in
# runs without a fixed path to third_party/onnxruntime, then seal the bundle.
# Run this AFTER all other bundle contents (e.g. the icon) are in place.
set -eu

BUNDLE="${1:?bundle path}"
ORT_LIB="${2:?onnxruntime lib directory}"

if [[ ! -d "${BUNDLE}/Contents" ]]; then
  echo "bundle_onnxruntime_macos: not a bundle: ${BUNDLE}" >&2
  exit 1
fi

FW="${BUNDLE}/Contents/Frameworks"
MACOS_DIR="${BUNDLE}/Contents/MacOS"
mkdir -p "${FW}"

# Work out which onnxruntime dylib the executable actually links against, so this
# is independent of the ORT version (e.g. libonnxruntime.1.19.2.dylib).
DYLIB_NAME=""
shopt -s nullglob
for exe in "${MACOS_DIR}"/*; do
  [[ -f "${exe}" ]] || continue
  n="$(otool -L "${exe}" 2>/dev/null | grep -oE 'libonnxruntime[^[:space:]]*\.dylib' | head -1 || true)"
  if [[ -n "${n}" ]]; then DYLIB_NAME="$(basename "${n}")"; break; fi
done

if [[ -z "${DYLIB_NAME}" ]]; then
  echo "bundle_onnxruntime_macos: ${BUNDLE} does not link onnxruntime; nothing to do."
  exit 0
fi

cp -fL "${ORT_LIB}/${DYLIB_NAME}" "${FW}/${DYLIB_NAME}"
chmod u+w "${FW}/${DYLIB_NAME}"
install_name_tool -id "@loader_path/${DYLIB_NAME}" "${FW}/${DYLIB_NAME}"

# Remove stale onnxruntime dylibs left by a previous build / version (self-cleaning).
for old in "${FW}"/libonnxruntime*.dylib; do
  [[ -e "${old}" ]] || continue
  [[ "$(basename "${old}")" == "${DYLIB_NAME}" ]] || rm -f "${old}"
done

for exe in "${MACOS_DIR}"/*; do
  [[ -f "${exe}" ]] || continue
  otool -L "${exe}" 2>/dev/null | grep -q libonnxruntime || continue
  # Drop any RPATH pointing into third_party/onnxruntime (paths may contain spaces).
  otool -l "${exe}" 2>/dev/null | awk '/cmd LC_RPATH$/ { getline; getline; if ($1=="path") { p=$0; sub(/^[[:space:]]*path /,"",p); sub(/ \(offset.*/,"",p); print p } }' | while IFS= read -r rp; do
    case "${rp}" in
      *onnxruntime*) install_name_tool -delete_rpath "${rp}" "${exe}" 2>/dev/null || true ;;
    esac
  done
  install_name_tool -delete_rpath "${ORT_LIB}" "${exe}" 2>/dev/null || true
  install_name_tool -add_rpath '@loader_path/../Frameworks' "${exe}" 2>/dev/null || true
done
shopt -u nullglob

# ---- Re-sign (install_name_tool invalidates signatures) ----
# Ad-hoc only when no identity is provided; with a real identity, fail loudly
# rather than silently downgrading to an unsigned/Gatekeeper-rejected bundle.
IDENTITY="${CODESIGN_IDENTITY:--}"
ENTITLEMENTS_FILE="${ENTITLEMENTS:-}"

sign_path() {  # $1 = path to sign
  if [[ "${IDENTITY}" == "-" ]]; then
    codesign --force --sign - "$1"
  else
    local args=(--force --sign "${IDENTITY}" --options runtime --timestamp)
    if [[ -n "${ENTITLEMENTS_FILE}" && -f "${ENTITLEMENTS_FILE}" ]]; then
      args+=(--entitlements "${ENTITLEMENTS_FILE}")
    fi
    codesign "${args[@]}" "$1"
  fi
}

sign_path "${FW}/${DYLIB_NAME}"
sign_path "${BUNDLE}"           # seal the whole bundle last (icon etc. already in place)
