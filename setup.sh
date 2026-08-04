#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
THIRD_PARTY_DIR="${ROOT_DIR}/third_party"
ORT_DIR="${THIRD_PARTY_DIR}/onnxruntime"
JUCE_DIR="${THIRD_PARTY_DIR}/JUCE"

# onnxruntime 1.19.2 is the newest release built for macOS 11.0 (minos 11.0).
# Do NOT bump without also raising CMAKE_OSX_DEPLOYMENT_TARGET to match its minos.
ORT_VERSION="1.19.2"
ORT_URL="https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-osx-arm64-${ORT_VERSION}.tgz"
JUCE_REPO_URL="https://github.com/juce-framework/JUCE.git"
JUCE_TAG="8.0.4"
JUCE_TARBALL_URL="https://codeload.github.com/juce-framework/JUCE/tar.gz/refs/tags/${JUCE_TAG}"

mkdir -p "${THIRD_PARTY_DIR}"

if [[ ! -d "${ORT_DIR}/lib" ]]; then
  echo "Downloading ONNX Runtime ${ORT_VERSION} (osx-arm64)..."
  TMP_TGZ="$(mktemp -t onnxruntime-osx-arm64.XXXXXX.tgz)"
  curl -L "${ORT_URL}" -o "${TMP_TGZ}"
  rm -rf "${ORT_DIR}"
  tar -xzf "${TMP_TGZ}" -C "${THIRD_PARTY_DIR}"
  mv "${THIRD_PARTY_DIR}/onnxruntime-osx-arm64-${ORT_VERSION}" "${ORT_DIR}"
  rm -f "${TMP_TGZ}"
else
  echo "ONNX Runtime already present at ${ORT_DIR}"
fi

if [[ ! -f "${JUCE_DIR}/CMakeLists.txt" ]]; then
  echo "Installing JUCE ${JUCE_TAG}..."
  rm -rf "${JUCE_DIR}"
  if git clone --depth 1 --branch "${JUCE_TAG}" "${JUCE_REPO_URL}" "${JUCE_DIR}"; then
    :
  else
    echo "Git clone failed, falling back to JUCE tarball download..."
    rm -rf "${JUCE_DIR}"
    TMP_TAR="$(mktemp -t juce-8.0.4.XXXXXX.tar.gz)"
    curl -L "${JUCE_TARBALL_URL}" -o "${TMP_TAR}"
    tar -xzf "${TMP_TAR}" -C "${THIRD_PARTY_DIR}"
    rm -f "${TMP_TAR}"
    mv "${THIRD_PARTY_DIR}/JUCE-${JUCE_TAG}" "${JUCE_DIR}"
  fi
else
  echo "JUCE already present at ${JUCE_DIR}"
fi

echo "Setup complete."
