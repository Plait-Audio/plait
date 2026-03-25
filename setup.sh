#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
THIRD_PARTY_DIR="${ROOT_DIR}/third_party"
LIBTORCH_DIR="${THIRD_PARTY_DIR}/libtorch"
JUCE_DIR="${THIRD_PARTY_DIR}/JUCE"

LIBTORCH_URL="https://download.pytorch.org/libtorch/cpu/libtorch-macos-arm64-2.5.1.zip"
JUCE_REPO_URL="https://github.com/juce-framework/JUCE.git"
JUCE_TAG="8.0.4"
JUCE_TARBALL_URL="https://codeload.github.com/juce-framework/JUCE/tar.gz/refs/tags/${JUCE_TAG}"

mkdir -p "${THIRD_PARTY_DIR}"

if [[ ! -d "${LIBTORCH_DIR}/lib" ]]; then
  echo "Downloading LibTorch 2.5.1 (CPU arm64)..."
  TMP_ZIP="$(mktemp -t libtorch-macos-arm64-2.5.1.XXXXXX.zip)"
  curl -L "${LIBTORCH_URL}" -o "${TMP_ZIP}"
  rm -rf "${LIBTORCH_DIR}"
  unzip -q "${TMP_ZIP}" -d "${THIRD_PARTY_DIR}"
  rm -f "${TMP_ZIP}"
else
  echo "LibTorch already present at ${LIBTORCH_DIR}"
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
