# Changelog — ffmpeg_converter (C/CMake)

All notable changes to the C implementation are documented here.
Format based on [Keep a Changelog](https://keepachangelog.com/).

---

## [2.0.0] — 2026-03-16

### Added
- Native macOS GUI (Cocoa/AppKit, Objective-C) replacing GTK4 on macOS.
- App bundle packaging for macOS (`.app` with `Contents/Resources/bin/`).
- Bundled `ffmpeg` and `ffprobe` inside the native macOS `.app` — no dependency
  on system PATH when launched from Finder.
- `converter_bridge.m`: bridge between native Cocoa UI and the C converter engine;
  resolves bundled tool paths via env vars (`FFMPEG`, `FFPROBE`, `FFMPEG_BIN`,
  `FFPROBE_BIN`) and prepends `Resources/bin` to `PATH`.
- Drag-and-drop support in native macOS GUI.
- Fixed-size compact window layout for HiDPI / 4K displays.
- CMake option `ENABLE_MACOS_NATIVE_GUI` (default ON) for the native GUI target.
- CMake option `ENABLE_LINUX_GUI` (default ON) for the Linux GTK4 GUI target.
- Source binaries for macOS bundling stored at project root (`ffmpeg`, `ffprobe`).

### Changed
- Build split: macOS builds `macos_gui_native` target; Linux builds `linux_gui`.
- GTK4 dependency eliminated on macOS; macOS GUI no longer requires GTK.
- Module libraries converted to CMake INTERFACE targets.

### Migration
- Completed: migration plan documented in
  `docs/macos-native-gui-migration-plan.md`.

---

## [1.1.0] — 2026-01-23

### Changed
- Full project architecture refactoring.
- Modules (`audio`, `video`, `utils`, `progress`, `ffmpeg_cmd`, `core`) converted
  to INTERFACE libraries.
- Platform implementations (`.c`) moved to `src/platform/<platform>/`.
- Full Linux and macOS build support added.
- `src/cli/CMakeLists.txt` updated for platform-specific builds.
- Removed FFmpeg API dependency — project uses external binaries only.
- Improved CLI directory structure.
- Added `src/README.md` and CLI parameter table.

---

## [1.0.0] — 2025-12-01

### Added
- Initial project version.
- Basic ffmpeg command generation logic.
- Progress bar with FPS and ETA display.
- Audio normalization support (peak, loudnorm).
- ProRes codec support with profiles.
- Initial CLI implementation.
