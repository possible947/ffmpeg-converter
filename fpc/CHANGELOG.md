# Changelog — ffmpeg_converter (Free Pascal)

All notable changes to the Free Pascal implementation are documented here.
Format based on [Keep a Changelog](https://keepachangelog.com/).

---

## [Unreleased] — Phase 2 bug fixes (2026-08-21)

### Fixed
- **Lazarus GUI Vulkan-device selector ignored the hw-Vulkan probe fields**
  (mirrors the equivalent GTK4/C bug): the device combo and the options-save
  fallback always used `FLinuxSupport.VulkanDeviceIndex`/`VulkanDeviceCount`
  (ProRes-only), never `VulkanHwDeviceIndex`/`VulkanHwDeviceCount`, for
  `h264_vulkan`/`hevc_vulkan`/`av1_vulkan`. The Windows options-save path
  had **no smart-default branch at all** — "Auto" always resolved to device
  0. Added `CodecIsHwVulkan`, `RecommendedVulkanDeviceIndex`,
  `RefreshVulkanDeviceComboForCodec` to `form.pas`; the combo now
  repopulates on codec change and both Linux/Windows fallbacks pick the
  correct probe family.
- **Windows Pascal hw-Vulkan device stats were never tracked at all**:
  `windows_probe.pas`'s `ProbeVulkanEncoder`/`ProbeVulkanHwEncoder` were
  boolean-only (no device index/count), unlike the Linux probe. Extended
  both to return `out BestDevice, DeviceCount`; added
  `VulkanDeviceIndex`/`VulkanDeviceCount`/`VulkanHwDeviceIndex`/
  `VulkanHwDeviceCount` to `TWindowsCodecSupport` and `TWindowsHWInfo`
  (`form_windows.pas`); `DetectWindowsHardware` now merges per-codec-family
  best-device stats identically to `linux_probe.pas`, and the redundant
  local `ProbeVulkanDeviceCount` scan in `form_windows.pas` was removed.

See `docs/v3.0-Phase2.md` §15 for full root-cause analysis (found during a
post-implementation code review of the Phase 2 commits). The CLI-side
Vulkan-device-default and `--codecs-list` gating bugs identified in the
same review were C-only (Pascal's `cli_args.pas` was already correctly
gated) — see `CHANGELOG.md` `[3.0.0-Phase2.2]`.

## [Unreleased] — Phase 2 (2026-08-21)

### Added
- **New codec `av1_amf`** and **hardware Vulkan video encoders**
  (`h264_vulkan`, `hevc_vulkan`, `av1_vulkan`) mirroring the C implementation's
  Phase 2 additions, distinct from the existing `prores_ks_vulkan`
  compute-shader ProRes encoder.
- **`speed`/`balance`/`quality` preset tiers** for `h264_vaapi`, `hevc_vaapi`,
  `h264_nvenc`, `hevc_nvenc`, `h264_amf`, `hevc_amf`, `h264_qsv`, `hevc_qsv` —
  `GetHwCodecFlags()` in `converter_cmd_builder.pas` now branches on
  `Opts.preset` for GPU codecs (previously ignored for all GPU codecs,
  always emitting `default` behavior regardless of user selection).
- `linux_probe.pas`: `FfmpegHasEncoder()` pre-filter, `VulkanDeviceIsSoftware()`
  (parses `vulkaninfo --summary`, excludes llvmpipe/lavapipe — mirrors the C
  implementation's mandatory software-device fix), `ProbeVulkanHwEncoder()`
  generic hardware Vulkan encoder probe. New `TLinuxCodecSupport` fields:
  `HasAV1AMF`, `HasVulkanH264`, `HasVulkanHEVC`, `HasVulkanAV1`,
  `VulkanHwDeviceIndex`, `VulkanHwDeviceCount`.
- `windows_probe.pas`: `ProbeVulkanHwEncoder()`, new `TWindowsCodecSupport`
  fields `HasAV1AMF`, `HasVulkanH264`, `HasVulkanHEVC`, `HasVulkanAV1`.
- `gui/form_windows.pas`: `TWindowsHWInfo` extended with the same 4 new
  fields for the Lazarus GUI's independent Windows hardware-detection path.
- `cli_args.pas`/`cli_menu.pas`: new codec entries in `--codecs-list`,
  interactive menu, and codec validation on both Linux and Windows; preset
  tiers now listed for all GPU codecs (previously only `default` was shown
  in `--codecs-list` for GPU codecs, even though `presets.json` already
  defined more tiers via the shared `TPresetDb` loader used by the GUI).
  `--vk-device` help text now covers all four Vulkan codecs.
- `gui/form.pas`: codec combo entries for the 4 new codecs; `CodecIsAnyVulkan()`
  replaces the narrower `CodecIsVulkanProres()` check for vulkan-device-picker
  visibility and default device-index fallback.

### Fixed
- **`cmbProfile.Enabled` (Lazarus GUI preset combo) was hardcoded to
  `CodecUsesSoftwareProres(CodecText)`** — meaning the preset dropdown was
  disabled (though populated) for `prores_ks_vulkan`, and for all GPU codecs
  even after Phase 2 added real preset tiers to them. It is now data-driven:
  `cmbProfile.Enabled := cmbProfile.Items.Count > 1`, matching whatever
  `presets.json` actually defines for the selected codec.

### Known limitations (not yet validated — see `docs/v3.0-Phase2.md` §14.5)
- No real-hardware validation of `av1_amf`/hardware Vulkan encoders was
  performed (no AMD RDNA3+/NVIDIA Turing+/Intel Arc hardware available).
- Windows Pascal build (`fpc/cli/ffmpeg_converter_windows.lpr`,
  `windows_probe.pas`, `form_windows.pas`) was not compiled on a real
  Windows machine — only the Linux CLI, shared library, and Lazarus/Qt6
  GUI were build-verified (`make -C fpc/build cli lib gui-app`, all clean).

---

## [Unreleased] (Phase 1, superseded above by Phase 2 entries)

### Fixed
- Fixed the Lazarus GUI preset selector (`fpc/gui/form.pas`) submitting a
  hardcoded `lt`/`standard`/`hq`/`4444` value based on combo item index
  instead of the actual dynamically-populated combo text, which could
  silently submit the wrong preset if `presets.json` key ordering changes.
  The preset value is now always read from the selected combo item text,
  matching the GTK4 GUI's approach.

### Added
- **Phase 1 codec/preset architecture completed** for Pascal CLI/GUI with
  runtime preset loading from `presets.json`.
- Added CLI support for `--codecs-list` to print valid codec/preset pairs.
- Lazarus preset combo now updates dynamically per selected codec.

### Changed
- Preset loader now supports `PRESETS_PATH` environment override before
  executable-adjacent and config directory search.
- `fpc/build/Makefile` now copies `presets.json` to `fpc/bin/` as part of
  binary staging, aligning runtime behavior with C builds.

### Fixed
- Preset lookup behavior is now consistent between CLI and GUI flows.
- Bundled binary execution now loads the full preset database instead of
  falling back to minimal defaults when `presets.json` is missing nearby.

### Changed
- **Qt6 is now the default Linux GUI widgetset (P5)** — `make -C fpc/build
  gui` builds with `--ws=qt6`; GTK3 stays available via `GUI_WS=gtk3`.
  Qt6 is the recommended LCL widgetset for GNOME + Wayland (LCL GTK3 is
  still alpha in Lazarus). Requires `libQt6Pas.so.6`, built from the Qt6
  cbindings shipped with Lazarus (`qmake6 && make && sudo make install`);
  the Makefile now checks for it and prints build instructions when missing.
  Verified on Ubuntu 24.04 / Lazarus 4.8 / Qt 6.4: the Qt6 GUI builds
  (28 MB) and starts clean, without the Gtk-CRITICAL warnings of GTK3.
- **Interactive codec menu is now dynamic (P4.2)** — `fpc/cli/cli_menu.pas`
  builds the codec list at runtime from the platform probe
  (`ProbeLinuxCodecSupport` / `ProbeWindowsCodecSupport`) instead of
  hard-coding entries. Only codecs that are actually available are offered
  (Linux: copy/prores/prores_ks/mux + probed vaapi/nvenc/amf/qsv/
  prores_ks_vulkan/m4v; Windows likewise). Multi-digit choices are supported
  via `ReadChoiceNum`.
- **`--vk-device` / `--vk_device` unified (P4.3)** — argument parsing already
  accepted both spellings (`cli_args.pas`); the help now documents the
  canonical `--vk-device` form. No functional change.
- **VAAPI rate control reverted to `-rc_mode auto`** — the temporary
  `-rc_mode ICQ -global_quality 22|25` change in
  `fpc/converter/converter_cmd_builder.pas` was rolled back: the radeonsi
  VAAPI driver rejects ICQ ("Driver does not support ICQ RC mode
  (supported modes: CQP, CBR, VBR)"), breaking VAAPI encodes. `auto` works
  on all tested drivers and keeps parity with the C engine.

### Fixed
- **Stale drag-and-drop claim (P4.4)** — the historical [1.1.0] changelog
  entry "Drag-and-drop file loading in Lazarus GUI" does not match the
  current code: `fpc/gui/form.pas` / `form.lfm` have no `AcceptFilesAtRunTime`
  and no drag handlers. The current LCL GUI does **not** implement
  drag-and-drop; the C/GTK4 GUI does.
- **Linux GUI widgetset pinning (P3.1)** — `fpc/build/Makefile` now sets
  `--ws=gtk3` for Linux only; on Windows `--ws` is not passed (native
  win32/win64 widgetset). `form.lpi` is shared by both platforms, so the
  widgetset cannot be pinned inside the `.lpi` without breaking the Windows
  build.
- **Clear GUI build failure diagnostics (P3.4)** — `make gui` now saves the
  full lazbuild log to `fpc/build/.units/gui-build.log`, prints the compiler
  errors, and gives a correct hint per failure type: missing GTK3 dev libs →
  `sudo apt install libgtk-3-dev`; missing LCL GTK3 widgetset → install
  Lazarus from lazarus-ide.org (**Ubuntu/Debian apt has no `lcl-gtk3`
  package**, only `lcl-gtk2`/`lcl-qt5`); other errors → project code.
  Fixed a `/bin/sh` incompatibility (`set -o pipefail` → POSIX rewrite).
- **`gui-app` target fixed (P3.5)** — `make gui-app` is now an alias of
  `appimage` instead of printing a misleading "macOS not supported" message.
- **`form.lps` cleaned (P3.2)** — removed the stale
  `../../../../Temp/HQ_converter/...` module entry and updated the working
  directory to the current repo location; duplicate `form.lpr` removed
  (P3.3) in favour of `main.lpr`.
- **Windows mux failure with `mkvmerge` argument parsing (`codec=mux`).**
  Removed `--overwrite` from the Pascal post-mux command line in
  `fpc/converter/mux_postprocess.pas`. On current MKVToolNix builds this token
  is treated as an input filename, causing failures like `file "--overwrite"
  could not be opened for reading`.
- **Windows `mkvmerge` resolution for release/deployed builds.**
  `fpc/platform/windows_mkvmerge.pas` now checks `MKVMERGE` /
  `MKVMERGE_BIN` first and also searches `src\platform\windows\bin\mkvmerge.exe`
  relative to the executable path, improving tool discovery outside dev shells.

---

## [2.5.0] — 2026-06-27

### Fixed
- **Apple M4V HEVC playback**: the Pascal M4V creator now probes the source
  codec and passes `-tag:v hvc1` for HEVC input, so Apple hardware decoders
  on macOS/iOS recognize and play the file (was `hev1`).
- **Apple M4V color metadata**: `probe_video_color` calls `ffprobe` to extract
  `color_primaries`, `color_transfer`, `color_space` from the source and passes
  them through to the ffmpeg copy step, producing a proper `colr` box in output.
- **Apple M4V audio disposition**: new step 5/6 applies
  `-disposition:a:0 default -disposition:a:1 0` via an ffmpeg copy pass after
  MP4Box mux, making the AAC track the primary audio and the AC3 track secondary.

### Changed
- **AAC encoding standardized to CBR 320k** across all Pascal converter modes
  (`fdk_aac_320`, `fdk_aac_320_ac3_640`, `use_aac_for_h265`) in
  `converter_cmd_builder.pas`.
- **Apple M4V AAC step** now uses `libfdk_aac -b:a 320k` (CBR) instead of the
  old `-c:a aac -q:a N` VBR path.
- **M4V pipeline expanded to 6 steps** (was 5): new step 5/6 is the audio
  disposition fix applied after MP4Box mux, before chapter import.

### Removed
- `--m4v-aac-quality` CLI flag and `M4VAacQuality` variable from `cli_args.pas`.
- `TAppleM4VOptions.AacQuality` field from `apple_m4v_creator.pas`.
- AAC quality prompt dialog from `gui/form.pas` M4V options.
- `M4VOpts.AacQuality` parsing from `m4v_postprocess.pas` and
  `ffmpeg_converter_windows.lpr`.

---

## [2.4.0] — 2026-04-27

### Changed
- **macOS support removed** — Pascal implementation is now Linux and Windows only.
  macOS users should use the C/CMake native Cocoa GUI instead.
- CLI and GUI now feature-matched with C implementation on both Linux and Windows.
- Build system unified with C/CMake via updated CMakeLists.txt and CMake integration.

### Platform Status (v2.4)
- **Linux**: CLI + Lazarus GUI (feature parity with C)
- **Windows**: CLI + Lazarus GUI with Vulkan GPU support (feature parity with C CLI)
- **macOS**: Discontinued

### Removed
- All macOS-specific code paths and build scripts.
- `fpc/build/package_macos_app.sh` — no longer needed.
- macOS `.app` bundle support in CMakeLists.txt.

---

### Added
- Windows GUI runtime probing for Vulkan ProRes (`prores_ks_vulkan`) with
  per-device discovery (`vulkan:0..7`) and codec list exposure only when the
  encoder is actually usable.

### Changed
- Windows GUI layout: Vulkan device selector was moved to the action row (near
  `m4v edit`) and aligned with neighboring controls to avoid overlap with audio
  output widgets.

### Fixed
- Windows GUI subprocesses no longer flash console windows. All critical
  `TProcess` call sites now use `poNoConsole`.
- Vulkan capability detection in GUI: replaced generic encoder probe with
  Vulkan-aware probe path (`-init_hw_device vulkan=... -filter_hw_device vk ...
  -c:v prores_ks_vulkan`) to prevent false negatives.
- Vulkan auto-device behavior: `vk:-1` command generation is now normalized to
  `vk:0` for ffmpeg compatibility.
- Apple M4V Windows cleanup path: replaced `/bin/rm` assumptions with
  Windows-safe cleanup calls and platform-conditional stderr redirection.

### Fixed
- **Mux postprocess temporary output collisions (`codec=mux`, Linux/Windows Pascal).**
  `fpc/converter/mux_postprocess.pas` now allocates unique post-mux temp file names and passes
  `--overwrite` to `mkvmerge` when overwrite mode is enabled.
- **Apple M4V chapter import reliability (Pascal Linux/Windows).**
  Pascal Apple M4V workflow now transfers chapters with
  `ffmpeg -map 0 -map_chapters 1 -c copy` onto the muxed M4V output instead of relying on
  `MP4Box -chap chapters.txt`, avoiding text-parser import failures on complex chapter titles.
- **Critical: loudness normalization 2-pass producing wrong gain (~10–14 LUFS error).**
  FFmpeg `loudnorm` filter always outputs measurement values as JSON strings
  (e.g. `"input_i" : "-12.88"`), not JSON numbers. `JsonNumToFloat` in
  `fpc/json/loudnorm_json.pas` only handled `jtNumber`, silently returning 0.0
  for string-typed values. The 2nd pass therefore ran with `measured_I=0.00`
  etc., applying incorrect gain. Fixed by adding a `jtString` branch that
  parses the value via `TryStrToFloat` with invariant format settings.
- **ffmpeg/ffprobe resolution: system-wide binaries used instead of bundled ones.**
  CLI now looks for `ffmpeg`/`ffprobe` in the same directory as the executable
  first (`ResolveFromExeDir`). Fallback to Homebrew/PATH paths removed.
  Missing ffmpeg/ffprobe next to the binary now produces an error instead of
  silently using an unrelated system installation.
  GUI bundle behaviour unchanged — `ApplyBundledToolEnvironment` pre-seeds env
  vars from `Contents/Resources/bin/` before resolution.

### Changed
- macOS Pascal app packaging input path for bundled ffmpeg/ffprobe is
  documented and aligned with the current script behavior:
  `src/platform/macos/bin/ffmpeg` and `src/platform/macos/bin/ffprobe`.

### Fixed
- Pascal GUI conversion progress/status flow:
  - encode path now streams ffmpeg progress in real time instead of updating
    only at completion;
  - ffmpeg progress flags are injected before output argument to keep CLI syntax
    valid;
  - stage/status text delivery to GUI callbacks is stabilized for consistent
    bottom status line rendering.

---

## [2.0.0] — 2026-03-16

### Added
- macOS `.app` bundle packaging via `fpc/build/package_macos_app.sh`:
  - Bundles `ffmpeg` and `ffprobe` from `src/platform/macos/bin/` into
    `Contents/Resources/bin/`.
  - Bundles `MP4Box` (GPAC) and all its dylib dependencies (94 libs) into
    `Contents/Resources/bin/` + `Contents/Resources/lib/` with patched `@rpath`.
- `ApplyBundledToolEnvironment` in `tool_paths.pas`: auto-detects `Resources/bin`
  from within the running `.app` bundle and sets `FFMPEG`, `FFPROBE`, `FFMPEG_BIN`,
  `FFPROBE_BIN` env vars plus prepends `Resources/bin` to `PATH`.
- `ResolveMp4BoxBin` in `tool_paths.pas`: bundle-aware resolution of `MP4Box`
  binary before system PATH fallback.
- `Mp4BoxBin` field in `TToolPaths` record.
- `apple_m4v_creator.pas`: uses resolved `Mp4BoxBin` absolute path instead of
  bare `MP4Box` shell name — works from Finder launch without system PATH.
- GUI (`form.pas`): calls `ApplyBundledToolEnvironment` at startup so all
  subprocess calls find bundled tools regardless of launch method.

### Changed
- `apple_m4v_creator.pas`: MP4Box path validation uses `ResolveToolPaths` instead
  of `command -v MP4Box` shell check.
- `package_macos_app.sh`: recursive dylib bundler using `otool -L` +
  `install_name_tool` to make MP4Box self-contained inside the bundle.
- Audio filter pipeline updated to require `ffmpeg` built with `--enable-libsoxr`
  (soxr resampler is part of the defined normalization logic).
- All debug `on_message` calls removed from `converter_core.pas`
  (`job ffmpeg=`, `job ffprobe=`, `job PATH=`, `ffmpeg command built`).
- All debug `QueueLog` calls removed from `form.pas`
  (`Apple job ffmpeg=`, `Apple job ffprobe=`, `Apple job PATH=`).

---

## [1.3.0] — 2026-03-14

### Added
- Real-time progress reporting in `converter_runner.pas` via incremental stdout
  read loop during ffmpeg encode (replaces synthetic 100% callback).
- `ProbeDuration` call before encoding for accurate ETA computation.
- `RunEncodeWithProgress` replacing blocking `RunEncode`.

### Fixed
- `QuoteForShell` (PROC-1): replaced double-quote wrapping with single-quote
  strategy to prevent shell injection on filenames with `$`, backticks, etc.
- `RunCommandCapture` (PATH-1): replaced `poWaitOnExit` with incremental
  read loop to eliminate pipe deadlock on large output (>64 KB).

---

## [1.2.0] — 2026-03-12

### Added
- Apple M4V creator (`apple_m4v_creator.pas`): multi-step pipeline —
  video copy → AAC encode → AC3 encode → MP4Box mux → optional chapter import.
- Chapter extraction from source via `ffprobe -show_chapters` + JSON parse.
- Edit-before-mux mode in GUI for Apple M4V workflow.
- `fpc/build/package_macos_app.sh`: initial macOS `.app` packager.

### Fixed
- Audio normalization (workstream B audit):
  - All five modes (`none`, `peak_norm`, `peak_norm_2pass`, `loudness_norm`,
    `loudness_norm_2pass`) validated against C implementation as source of truth.
  - `loudnorm` 2-pass parameter mapping corrected.
- Chapter import (workstream A audit):
  - Canonical data path: `ffprobe` → `chapters.json` → parse → `chapters.txt`
    → `MP4Box -chap`.
  - Removed duplicate stdout/file data paths.

---

## [1.1.0] — 2026-03-10

### Added
- Lazarus/LCL GUI (`fpc/gui/form.pas`) with threaded conversion workers.
- Threaded background conversion with `TThread`-based workers.
- Progress display: encode percent, FPS, ETA.
- Apple M4V workflow UI (edit-before-mux option).
- Drag-and-drop file loading in Lazarus GUI.

### Changed
- `converter_core.pas` refactored to support per-file state reset in batch mode.
- Separated loudnorm JSON parsing into `fpc/json/loudnorm_json.pas`.

---

## [1.0.0] — 2026-02-01

### Added
- Initial Free Pascal port of `ffmpeg_converter`.
- CLI with full argument parsing (`-c`, `-p`, `-d`, `-a`, `-g`, `--overwrite`, `-o`)
  and interactive 9-step menu.
- Converter core: file validation, overwrite checks, peak/loudnorm 2-pass analysis,
  command builder, encode runner.
- Shared library with C ABI export (`converter_pas.lpr`, `converter_pas.h`).
- Reusable helpers: `fs_utils`, `path_utils`, `process_utils`, `time_utils`.
- Loudnorm JSON parser (`fpjson`/`jsonparser`).
- Unit tests: `test_cmd_builder`, `test_path_parse`, `test_cli_mode_matrix`.
- Integration test scripts: `test_cli_args_matrix.sh`, `check_gui_cli_issues.sh`.
