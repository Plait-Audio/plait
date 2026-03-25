#!/bin/bash
# Bundle LibTorch dylibs into a JUCE .component / .vst3 / .app so the plug-in
# runs without a fixed path to third_party/libtorch.
set -eu

BUNDLE="${1:?bundle path}"
TORCH_LIB="${2:?libtorch lib directory}"

if [[ ! -d "${BUNDLE}/Contents" ]]; then
  echo "bundle_libtorch_macos: not a bundle: ${BUNDLE}" >&2
  exit 1
fi

FW="${BUNDLE}/Contents/Frameworks"
mkdir -p "${FW}"

for lib in libc10.dylib libtorch.dylib libtorch_cpu.dylib libomp.dylib; do
  cp -f "${TORCH_LIB}/${lib}" "${FW}/"
done

# Sibling lookups inside Frameworks/
for name in libc10 libtorch libtorch_cpu libomp; do
  install_name_tool -id "@loader_path/${name}.dylib" "${FW}/${name}.dylib"
done

install_name_tool -change @rpath/libtorch_cpu.dylib @loader_path/libtorch_cpu.dylib "${FW}/libtorch.dylib"
install_name_tool -change @rpath/libc10.dylib @loader_path/libc10.dylib "${FW}/libtorch.dylib"

install_name_tool -change @rpath/libc10.dylib @loader_path/libc10.dylib "${FW}/libtorch_cpu.dylib"
install_name_tool -change @rpath/libomp.dylib @loader_path/libomp.dylib "${FW}/libtorch_cpu.dylib"

MACOS_DIR="${BUNDLE}/Contents/MacOS"
shopt -s nullglob
for exe in "${MACOS_DIR}"/*; do
  [[ -f "${exe}" ]] || continue
  if ! otool -L "${exe}" 2>/dev/null | grep -q libtorch; then
    continue
  fi
  # Remove any RPATH pointing into LibTorch (handles spaces in paths)
  otool -l "${exe}" 2>/dev/null | awk '/cmd LC_RPATH$/ { getline; getline; if ($1=="path") { p=$0; sub(/^.*path /,"",p); sub(/ \(offset.*/,"",p); print p } }' | while IFS= read -r rp; do
    case "${rp}" in
      *libtorch*) install_name_tool -delete_rpath "${rp}" "${exe}" 2>/dev/null || true ;;
    esac
  done
  install_name_tool -delete_rpath "${TORCH_LIB}" "${exe}" 2>/dev/null || true
  install_name_tool -add_rpath '@loader_path/../Frameworks' "${exe}" 2>/dev/null || true
done
shopt -u nullglob

# Re-sign after install_name_tool (invalidates prior signature)
# Use Developer ID if CODESIGN_IDENTITY is set, otherwise ad-hoc
IDENTITY="${CODESIGN_IDENTITY:--}"
ENTITLEMENTS_FILE="${ENTITLEMENTS:-}"

SIGN_ARGS=(--force --sign "${IDENTITY}" --deep)
if [[ "${IDENTITY}" != "-" ]]; then
  SIGN_ARGS+=(--options runtime --timestamp)
  if [[ -n "${ENTITLEMENTS_FILE}" && -f "${ENTITLEMENTS_FILE}" ]]; then
    SIGN_ARGS+=(--entitlements "${ENTITLEMENTS_FILE}")
  fi
fi

# Sign individual frameworks first, then the bundle
for lib in "${FW}"/*.dylib; do
  [[ -f "${lib}" ]] || continue
  codesign --force --sign "${IDENTITY}" ${IDENTITY:+--options runtime --timestamp} "${lib}" 2>/dev/null || true
done

codesign "${SIGN_ARGS[@]}" "${BUNDLE}" 2>/dev/null || \
  codesign --force --sign - --deep "${BUNDLE}"
