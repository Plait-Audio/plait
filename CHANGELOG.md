# Changelog

All notable changes to ISO Drums.

## v0.3.1

**The big fix: ISO Drums now runs on macOS 11 and up.**

- **Fixed:** the plug-in and standalone app failed to load on macOS older than the machine they were built on — including Sonoma and Ventura — with a "version not supported" error, and the AU failed Logic's validation so it never appeared in the plug-in list. Builds now correctly target **macOS 11 (Big Sur)+ on Apple Silicon**.
- **Smaller & faster:** the inference backend moved from LibTorch to ONNX Runtime. Each plug-in is now **~65% smaller** (≈368 MB → ≈129 MB) and separation is noticeably faster, with identical output.
- **Privacy-first, opt-in analytics:** a new anonymous, off-by-default usage stat helps us improve the app. No audio, filenames, or personal data — ever. Toggle it any time in Settings.
- Hardening: safer cancellation when closing mid-separation, and general robustness fixes.

### Installation
1. Open the DMG and drag **ISO Drums.app** to Applications.
2. For DAWs, copy **ISO Drums.component** (AU) and **ISO Drums.vst3** to `~/Library/Audio/Plug-Ins/`.

Requires macOS 11+ on Apple Silicon (M1 or later).

## v0.3.0
- Hero background video, mobile layout refinements, GitHub Releases distribution.

## v0.2.0
- Earlier beta.
