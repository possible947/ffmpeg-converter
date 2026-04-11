# Changelog — ffmpeg_converter (C/CMake)

All notable changes to the C implementation are documented here.
Format based on [Keep a Changelog](https://keepachangelog.com/).

---

## [2.2.0] — 2026-04-11

### Added
- Linux runtime probe for bundled/system tool resolution and VAAPI capability detection.
- Linux codec set extended with runtime-detected `h264_vaapi` and `hevc_vaapi`.
- Linux audio output modes: PCM, FDK AAC q5, and FDK AAC q5 + AC3 640.
- Linux MKV post-mux mode:
  - new CLI codec `mux`
  - `--video-track <file>` input
  - final MKV written through `mkvmerge`
- Shared C mux module under `src/mux/`.
- Linux GTK Apple M4V creator workflow with direct M4V pipeline:
  - video copy
  - FDK AAC VBR encode
  - AC3 encode
  - MP4Box mux
  - optional chapter import
- Shared C M4V module under `src/m4v/`.

### Changed
- Linux build now stages `ffmpeg`, `ffprobe`, `mkvmerge`, and `MP4Box` next to
  `build/bin/ffmpeg_converter` and `build/bin/ffmpeg_converter_gui` when available.
- Linux CLI help now documents mux mode and replacement video track usage.
- Linux GTK GUI now includes a dedicated Apple M4V action in addition to normal
  conversion and MKV mux workflows.
- Linux Apple M4V AAC step now uses `libfdk_aac -vbr 5` by default.

### Removed
- Legacy Linux `h265_mi50` path from active C/Linux workflows.

## [Unreleased]

### Added
- App icon for native macOS GUI (`src/gui_macos_native/icon.icns`):
  - Source PNG (`icon.png`) converted to multi-resolution `.icns` via `iconutil`.
  - Rounded corners (22.5% radius) and 10% transparent padding applied to match
    Apple HIG proportions for macOS Sequoia.
  - Bundled into `Contents/Resources/` via CMake `MACOSX_PACKAGE_LOCATION`.
  - `CFBundleIconFile` key added to `Info.plist.in`.
- macOS VideoToolbox codec support (`prores_videotoolbox`, `hevc_videotoolbox`):
  - `prores_videotoolbox`: uses Apple's proprietary ProRes encoder; passes
    `-allow_sw 1` so software fallback is available on Intel Macs (hardware on
    Apple Silicon).
  - `hevc_videotoolbox`: uses Apple VideoToolbox H.265 encoder with automatic
    per-file bitrate calculation (sub-linear formula: base 35 Mbps at 4K/24 fps,
    exponent 0.75 on fps, clamped [2000, 80000] kbps). Output tagged `hvc1` for
    maximum compatibility. Spatial AQ enabled.
  - `get_video_info()`: new helper in `converter.c` (macOS-guarded) — calls
    `ffprobe` to extract width, height, and frame rate for bitrate calculation.
  - `calc_hevc_vt_bitrate_kbps()`: implements the sub-linear bitrate formula.
- `src/gui_macos_native/Info.plist.in`: CMake-processed `Info.plist` template
  with `CFBundleIdentifier = com.ffmpeg-converter.macos-gui`, fixing silent
  NSOpenPanel failure caused by missing bundle identifier (macOS TCC).

### Changed
- macOS codec popup width adjusted to 160 px to prevent overlap with adjacent
  column in the native GUI.
- Native macOS GUI `onAddFilesClicked:` now uses `[NSOpenPanel runModal]`
  instead of `beginSheetModalForWindow:`.
- `src/gui_macos_native/CMakeLists.txt`: added `MACOSX_BUNDLE_INFO_PLIST`
  property pointing to the new `Info.plist.in` template.
- macOS codec popup items updated to: `copy`, `prores`, `prores_ks`,
  `prores_videotoolbox`, `hevc_videotoolbox`.
- macOS `updateDependentControls`: profile control enabled for `prores`,
  `prores_ks`, and `prores_videotoolbox`; deblock enabled for `prores` and
  `prores_ks` only (hardware encoders excluded).
- macOS CLI usage and interactive menu updated for the new codec set.
- Output extension mapping: `hevc_videotoolbox` → `.mp4`; `copy` → `.mkv`;
  all other codecs → `.mov`.

### Removed
- `h265_mi50` (H.265 VAAPI) and all VAAPI-related command-line preamble removed
  from `converter.c` macOS path. VAAPI was Linux-only and unused on macOS.

### Added
- Audio filter multithreading for 2-pass analysis:
  - Added `-filter_threads N` flag to `peak_two_pass` and `loudnorm_two_pass`.
  - CPU thread detection via `sysconf(_SC_NPROCESSORS_ONLN)` (Linux) or `sysctlbyname("hw.ncpu")` (macOS).
  - Thread count = CPU cores / 2 (minimum 1).
- macOS FFmpeg priority detection:
  - Checks `/opt/local/bin/ffmpeg8` (macports FFmpeg8) first
  - Falls back to `/opt/local/bin/ffmpeg` (macports)
  - Falls back to bundled or system ffmpeg
- Same priority order for ffprobe on macOS.

### Fixed
- Converter output directory preflight in C core (`src/converter/converter.c`):
  - default output directory fallback to `$HOME/ffmpeg_converter` when `-o` is
    not specified;
  - missing output directory is created before encoding;
  - output path writability is validated early.
- ffmpeg command construction in C core:
  - progress flags (`-progress pipe:1 -nostats -nostdin`) are now injected
    before output argument to keep ffmpeg CLI syntax valid.

### Documentation
- Synchronized project docs with current behavior (CLI syntax, output directory
  semantics, C vs Pascal feature boundaries).
- Clarified canonical bundled ffmpeg/ffprobe source path for macOS packaging:
  `src/platform/macos/bin/`.

---

## [2.1.0] — 2026-03-19

### Added
- Apple M4V creator workflow in native macOS C GUI (`main.m`, `converter_bridge.m`,
  `apple_m4v_creator.m`):
  - Direct mode (`source -> .m4v`)
  - Edit-before-mux mode (`main converter -> .m4v -> intermediate cleanup`)
- Apple M4V options dialog in native macOS GUI:
  - Video track index
  - Audio track index
  - AAC quality
  - AC3 bitrate
  - Audio language
  - Chapter import toggle
- New native backend module for Apple M4V pipeline:
  - `src/gui_macos_native/apple_m4v_creator.h`
  - `src/gui_macos_native/apple_m4v_creator.m`
- New packaging helper for MP4Box and dylib dependencies:
  - `src/gui_macos_native/bundle_mp4box_deps.sh`

### Changed
- `src/gui_macos_native/CMakeLists.txt` now runs post-build MP4Box bundling into
  app `Contents/Resources`.
- Native macOS GUI run-state management now blocks concurrent standard conversion
  and Apple M4V workflows.
- Native macOS tool environment setup now resolves and exports `MP4BOX_BIN` in
  addition to ffmpeg/ffprobe variables.

### Documentation
- Added technical design and implementation status document for native macOS
  Apple M4V workflow:
  - `docs/macos-native-apple-m4v-design.md`

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
- Source binaries for macOS bundling stored in
  `src/platform/macos/bin/` (`ffmpeg`, `ffprobe`).

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
