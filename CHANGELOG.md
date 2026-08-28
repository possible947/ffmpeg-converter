# Changelog — ffmpeg_converter (C/CMake)

All notable changes to the C implementation are documented here.
Format based on [Keep a Changelog](https://keepachangelog.com/).

---

## [Unreleased] — macOS h264_videotoolbox Support (2026-08-28)

### Added
- **`h264_videotoolbox` codec on macOS**: the bundled ffmpeg (v8.1, built
  with `--enable-videotoolbox`) already supported hardware H.264 encoding
  via VideoToolbox, and `runtime_probe.c` already probed for it
  (`has_h264_videotoolbox`), but the codec was never wired into the
  preset database, CLI codec table, or GUI codec popup — only
  `hevc_videotoolbox` and `prores_videotoolbox` were exposed. Added:
  - `presets.json`: new `macos.h264_videotoolbox.default` preset
    (`-c:v h264_videotoolbox -b:v {vt_bitrate} -spatial_aq 1`).
  - `src/converter/platform/converter_macos.c`: `platform_supports_codec()`
    now accepts `h264_videotoolbox`; new `macos_calc_h264_vt_bitrate_kbps()`
    reuses the existing HEVC sub-linear bits-per-pixel formula with a
    higher base bitrate (50000 vs 35000 kbps @ 4K/24fps, since H.264 needs
    roughly 1.4x the bitrate of HEVC for comparable quality);
    `platform_get_video_codec_flags()` emits the encoder flags.
  - `src/cli/platform/cli_macos.c`: added to the CLI codec table and
    `platform_codec_is_available()`.
  - `src/gui_macos_native/main.m`: `macGuiSupportsCodec()` now checks
    `support->has_h264_videotoolbox` so the codec appears in the GUI
    popup only when actually probed as available.

### Verified (macOS Sequoia, bundled ffmpeg 8.1 with videotoolbox/vulkan/opencl)
- Confirmed via `-hwaccels`/`-encoders`/`-hide_banner` that the bundled
  ffmpeg supports `videotoolbox`, `opencl`, and `vulkan` hwaccels, but that
  Vulkan video **encoders** (`h264_vulkan`, `hevc_vulkan`, `av1_vulkan`,
  `prores_ks_vulkan`) fail at runtime with `Invalid argument` even with a
  correct `hwupload` pipeline, because Apple's Vulkan implementation
  (MoltenVK) does not implement the Vulkan Video encode extensions on this
  hardware. OpenCL provides only filters (denoise, tonemap, etc.), not
  encoders, so there is no OpenCL codec to add. Only VideoToolbox is a
  functional hardware encode path on macOS today.
- Rebuilt `macos_cli`/`macos_gui_native`; `--help`/`--codecs-list` now list
  `h264_videotoolbox`.
- Ran a real conversion (`--dry-run` then full encode) of a generated test
  clip with `-c h264_videotoolbox`; `ffprobe` confirmed a valid `h264`
  output stream, produced via real VideoToolbox hardware encoding (not a
  software fallback).

## [Unreleased] — WSL2 Codec Detection Fix (2026-08-24)

### Fixed
- **`prores_ks_vulkan` could be falsely reported as available on
  software-only Vulkan systems**: `probe_vulkan_prores()` in
  `src/platform/linux/runtime_probe.c` ran its one-frame encode probe
  against every `vk:N` device without filtering out CPU-only Mesa
  llvmpipe/lavapipe implementations, unlike the equivalent
  `probe_vulkan_encoder()` used for `h264_vulkan`/`hevc_vulkan`/`av1_vulkan`,
  which already skipped software devices via `vulkan_device_is_software()`.
  On a WSL2 Ubuntu 24.04 host with an NVIDIA Titan V + Intel Arc A750 but
  only a software Vulkan ICD registered, this caused `prores_ks_vulkan` to
  incorrectly show up in `--codecs-list`. `probe_vulkan_prores()` now shares
  the same `vulkan_device_is_software()` filter as `probe_vulkan_encoder()`.

### Verified (WSL2 Ubuntu 24.04, NVIDIA Titan V + Intel Arc A750, "linux" profile)
- Clean `linux_cli` build from scratch succeeds (warnings only, no errors).
- `h264_vaapi`/`hevc_vaapi` are **correctly absent**: this WSL2 instance has
  no `/dev/dri` render nodes and no Intel Arc driver entry under
  `/usr/lib/wsl/drivers` (only the NVIDIA driver is passed through), so VAAPI
  hardware is genuinely unavailable at the OS level — not a program bug.
  Resolving this requires enabling Intel Arc GPU passthrough for WSL2 on the
  Windows host (updated Intel driver + `wsl --shutdown`/restart).
- `h264_nvenc`/`hevc_nvenc` correctly detected and functionally verified via
  a real encode (`ffprobe` confirmed `h264` output codec in the produced
  `.mkv`); NVIDIA GPU is reachable via `/dev/dxg` without needing `/dev/dri`.
- After the fix, `--codecs-list` on this host correctly reports: `copy`,
  `prores`, `prores_ks`, `h264_nvenc`, `hevc_nvenc`, `mux`, `m4v` — with no
  VAAPI or Vulkan hardware codecs, matching actual available hardware.

## [Unreleased] — Windows AV1 Preset Tuning (2026-08-24)

### Changed
- **`av1_nvenc` `default` preset now uses `-preset p6`** instead of `p7`,
  making it distinct from the `quality` tier — mirrors `hevc_nvenc`, whose
  `default` (`-preset hq`) already differs from its `quality` tier
  (`-preset p7`). `av1_nvenc` has no legacy `hq` preset alias, so `p6`
  ("slower, better quality") is the closest equivalent one step below the
  slowest/best `p7` used by `quality`.
- **`av1_qsv` `default`/`quality` presets now include `-g 240 -bf 4`**
  (explicit GOP length + B-frame depth), matching `hevc_qsv`'s tuned
  default, which already sets these. Previously `av1_qsv` left them at
  encoder defaults while `hevc_qsv` did not.
- Verified on real hardware (Intel Arc A750): `av1_qsv` `default` and
  `quality` now produce byte-identical output (as intended, matching
  `hevc_qsv`'s default==quality pattern), both differing from `balance`.

## [Unreleased] — Windows AV1 QSV/NVENC Encoder Support (2026-08-24)

### Added
- **New codec `av1_qsv`** (Windows) — Intel QSV AV1 hardware encoder, for
  Xe-HPG (Arc A-series) and 12th-gen+ Iris Xe iGPUs. Runtime-probed via
  a cheap `-encoders` text pre-filter followed by a real one-frame encode
  test, mirroring the existing `av1_amf` gating pattern. Adds
  `PLAT_CAP_QSV_AV1` to `converter_platform.h` and 4 quality presets
  (`default`/`speed`/`balance`/`quality`) to `presets.json`.
- **New codec `av1_nvenc`** (Windows) — NVIDIA NVENC AV1 hardware encoder,
  requires Ada Lovelace (RTX 40-series+); older GPUs (Turing/Volta/Ampere)
  correctly report unavailable. Same probe pattern as `av1_qsv`/`av1_amf`.
  Adds `PLAT_CAP_NVENC_AV1` and 4 quality presets to `presets.json`.
- Both codecs plug into the existing dynamic codec catalog
  (`platform_get_codec_entries()`/`platform_codec_is_available()`), so
  `--help`/`--codecs-list`/interactive menu automatically include them
  when detected — no static codec-list text needed updating.

### Fixed
- **Windows encoder catalog was missing all AV1 QSV/NVENC support** — only
  `av1_amf` (AMD) and `av1_vulkan` (Vulkan hardware encode) existed; systems
  with Intel Arc / Xe iGPUs (QSV AV1-capable) or RTX 40-series GPUs had no
  way to select AV1 hardware encoding even though the installed ffmpeg
  build supported it. Verified on Intel Arc A750 + NVIDIA Titan V: `av1_qsv`
  now correctly appears in `--codecs-list`, while `av1_nvenc` correctly
  stays hidden (Titan V is pre-Ada Volta, no AV1 NVENC support).

## [Unreleased] — Mux/M4V Pipeline Fixes (2026-08-22)

### Fixed
- **Apple M4V audio disposition step regressed to running MP4Box instead of
  ffmpeg**: `m4v.c` reused the `quoted_tool` scratch buffer from the
  preceding MP4Box mux step without resetting it to the ffmpeg binary before
  building the disposition command, so step 5/6 always failed with
  `Apple M4V audio disposition failed` even though the muxed file itself was
  already complete and valid.
- **`codec=mux` ignored the `preset` (mkv/mov/m4v) and always produced an
  `.mkv`** via the legacy mkvmerge-only path: added
  `mux_run_postprocess_for_preset()` (`src/mux/mux.c`/`mux.h`), which keeps
  the `mkv` preset unchanged and adds `mov` (ffmpeg stream-copy remux) and
  `m4v` (Apple M4V pipeline on the merged intermediate) as real final
  containers. Wired into the Linux/Windows CLI mux post-process
  (`cli_linux.c`, `cli_windows.c`), the GTK4 GUI (`gui_callbacks.c`), and the
  macOS Cocoa bridge (`converter_bridge.m`); `converter_make_output_name()`
  now derives the correct extension for `codec=mux` from `preset`.
- Restored the `mux` codec entry to the macOS Cocoa codec picker
  (`main.m`), which had been dropped when the popup became data-driven,
  before its preset dispatch was fully wired.

## [Unreleased] — Preset UX and Platform Validation Follow-up (2026-08-21)

### Fixed
- Linux GTK4 now labels the selector **Preset**, enables it for every
  multi-preset codec, preserves the selected preset in conversion options, and
  uses a friendly GPU name for the VAAPI automatic recommendation.
- The macOS Cocoa bridge now uses `ConvertOptions.preset` instead of the
  removed integer profile field. Cocoa codec/preset menus load from bundled
  `presets.json`; `prores_videotoolbox` and `hevc_videotoolbox` are runtime-gated.
- The macOS app bundle includes `presets.json` in `Contents/Resources` and
  exports `PRESETS_PATH` before conversion/probing.
- Linux C and Pascal AppImage bundles now carry presets, export
  `PRESETS_PATH`, and use consistent `ffmpeg-converter` desktop/icon assets.

### Verification Required Before Release
- **Windows:** build C/MSVC and Pascal CLI/GUI, verify staged presets for both
  Pascal PowerShell build paths, and run codec/preset, Vulkan-device, and
  real-hardware conversion smoke tests.
- **macOS:** build CLI and native Cocoa GUI with Xcode, verify bundled presets,
  dynamic preset selection, VideoToolbox probe gating, and Apple M4V workflow.

## [3.0.0-Phase2.2] — 2026-08-21 (Development)

### Fixed
- **`--codecs-list` was not hardware-gated** (user-confirmed bug):
  `cli_print_codecs_list()` read only from `presets.json`, listing every
  codec the build knows about regardless of actual hardware/driver support
  — unlike `--help`, which correctly gates on `platform_codec_is_available()`.
  On a box with no AMF/Vulkan-encode support this incorrectly showed
  `av1_amf`, `h264_vulkan`, `hevc_vulkan`, `av1_vulkan`, NVENC, and QSV.
  Now filters through `platform_codec_is_available()`, matching `--help`.
- **`--vk_device` default ignored the selected codec**:
  `platform_get_default_vulkan_device()` was resolved once at options-init,
  before `--codec` was parsed, and always prioritized the ProRes-Vulkan
  probe recommendation over hardware-Vulkan — risking the wrong GPU on
  multi-GPU systems. Now resolved after the codec is finalized (via a `-1`
  "unresolved" sentinel on `opts->vulkan_device`, resolved inside
  `platform_apply_hw_device()`), and codec-aware.
- **GTK4 GUI Vulkan-device picker ignored the new hw-Vulkan probe fields**:
  `populate_vulkan_device_combo()` and the options-collection fallback
  always used the ProRes-only `vulkan_device_index`/`vulkan_working_mask`,
  making the new `vulkan_hw_device_index`/`vulkan_hw_working_mask` fields
  dead code for `h264_vulkan`/`hevc_vulkan`/`av1_vulkan`. Now selects the
  matching probe family via a new `get_vulkan_probe_for_codec()` helper,
  and repopulates the combo on every codec change.

See `docs/v3.0-Phase2.md` §15 for full root-cause analysis and verification
details of all three fixes (found during a post-implementation code review).

## [3.0.0-Phase2.1] — 2026-08-21 (Development)

### Added
- **New codec `av1_amf`** — AMD AMF AV1 encoder, alongside existing
  `h264_amf`/`hevc_amf`. Requires ffmpeg built with `--enable-amf` and the
  AMF runtime library (`libamfrt64.so.1` on Linux, `amfrt64.dll` on Windows)
  installed on the target system.
- **New hardware Vulkan video encoders**: `h264_vulkan`, `hevc_vulkan`,
  `av1_vulkan` — distinct from the existing `prores_ks_vulkan` compute-shader
  ProRes encoder. Require ffmpeg built with `--enable-vulkan` plus a shader
  compiler (`--enable-libshaderc`/`--enable-libglslang`) and a GPU/driver
  implementing `VK_KHR_video_encode_queue`.
- **`speed`/`balance`/`quality` preset tiers** for `h264_vaapi`, `hevc_vaapi`,
  `h264_nvenc`, `hevc_nvenc`, `h264_amf`, `hevc_amf`, `h264_qsv`, `hevc_qsv` —
  selectable via `-p/--profile` (CLI) or the preset combo box (GUI). The
  `default` tier is byte-for-byte unchanged from Phase 1 for full backward
  compatibility.
- 4 new `PLAT_CAP_*` capability flags in `converter_platform.h`:
  `PLAT_CAP_AMF_AV1`, `PLAT_CAP_VULKAN_H264`, `PLAT_CAP_VULKAN_HEVC`,
  `PLAT_CAP_VULKAN_AV1`.
- Linux runtime probing (`runtime_probe.c`): `ffmpeg_has_encoder()` cheap
  `-encoders` text pre-filter; `vulkan_device_is_software()` — parses
  `vulkaninfo --summary` to exclude `llvmpipe`/`lavapipe` software renderers
  from being reported as usable Vulkan hardware encoders (mandatory fix
  identified in `docs/v3.0-Phase2.md` §8); `probe_vulkan_encoder()` — generic
  one-frame hardware Vulkan encoder probe (vk:0..7).
- Windows runtime probing mirrors: `windows_ffmpeg_has_encoder()`,
  `windows_probe_vulkan_encoder()` (no software-device exclusion — Linux/
  llvmpipe-specific concern).
- `HwPreset` lookup table in `converter_linux.c`/`converter_windows.c`
  (`platform_get_video_codec_flags()`) — GPU codecs now actually honor
  `opts->preset` when building the ffmpeg command line; previously all GPU
  codecs silently ignored the user's preset selection and always used
  `default` behavior (a latent pre-Phase-2 gap, closed here).
- `--vk_device` CLI help text (`cli_common.c`) now covers all four Vulkan
  codecs (`prores_ks_vulkan`, `h264_vulkan`, `hevc_vulkan`, `av1_vulkan`),
  not just `prores_ks_vulkan`.
- GTK4 GUI: new codec entries in the codec combo, and vulkan-device-picker
  visibility now generalized to any Vulkan codec via
  `codec_uses_any_vulkan()` (`gui_codec_utils.h`).

### Changed
- `presets.json`: added speed/balance/quality tiers for 8 existing GPU
  codecs and 4 new codec entries, applied to both `linux` and `windows` OS
  blocks (`macos` untouched — Phase 2 is Linux/Windows only per plan scope).
- `codec_is_vulkan()` (`converter.c`) generalized from `prores_ks_vulkan`-only
  to also match the 3 new hardware Vulkan codecs (affects deblock-skip and
  `-vf format=...` filter-chain selection).
- `platform_get_preinput_hw_flags()` / `platform_get_hw_vfilter()`
  (`converter_linux.c`/`converter_windows.c`) generalized to cover the new
  hardware Vulkan codecs (shared `-init_hw_device vulkan=vk:N
  -filter_hw_device vk` / `format=nv12,hwupload` pattern).

### Known limitations (not yet validated — see `docs/v3.0-Phase2.md` §14.5)
- **No real-hardware validation performed.** `av1_amf`,
  `h264_vulkan`/`hevc_vulkan`/`av1_vulkan` preset values are transcribed
  from the design document's Section 5 tables, not empirically tuned or
  tested against AMD RDNA3+, NVIDIA Turing+, or Intel Arc hardware.
- **Windows build never compiled.** `converter_windows.c`, `cli_windows.c`,
  and `platform/windows/runtime_probe.{c,h}` changes were reviewed manually
  and brace/paren-balance checked, but no MSVC or mingw-w64 toolchain was
  available to actually build `windows_cli`. A build + smoke test on real
  Windows/MSVC is required before release.
- **No macOS build performed.** Phase 2 intentionally does not touch any
  macOS file, but this has not been build-verified on a macOS machine.
- **Bundled-ffmpeg build config** (re-enabling `prores_ks_vulkan`, verifying
  `--enable-amf`/`--enable-vulkan`+shaderc on all three bundle variants) is
  a build-infrastructure task outside this repo's C/Pascal source — ffmpeg
  binaries are vendored/staged externally per `AGENTS.md`. Documented as a
  requirement in `README.md`; actual ffmpeg rebuild was not performed.

---

## [3.0.0-Phase1.11] — 2026-08-21 (Development)

### Fixed
- Fixed a stack buffer overflow in the interactive CLI preset menu
  (`cli_common.c`): preset names loaded from `presets.json` were copied into
  a fixed `char preset[32]` via unchecked `strcpy`; now uses bounded
  `strncpy` with explicit NUL-termination.
- Fixed memory leaks where the preset-name array returned by
  `preset_db_list_presets()` was never freed: `cli_choose_preset_for_codec`,
  the interactive CLI preset step, and the GTK4 `populate_preset_combo`
  (leaked on every codec-combo change).
- Restored the `prores_ks_vulkan` fallback preset default to `"hq"` on Linux
  and Windows (it had regressed to `"standard"` during the profile → preset
  migration), preserving pre-refactor, behavior-identical output.

## [3.0.0-Phase1.10] — 2026-08-21 (Development)

### Added
- **Phase 1 codec/preset architecture completed** across C and Free Pascal
  implementations with dynamic preset loading from `presets.json`.
- New CLI command `--codecs-list` to print valid codec/preset combinations
  available on the current system.
- GUI preset combo updates in GTK4 and Lazarus: presets now repopulate
  dynamically when codec selection changes.
- Linux VAAPI device picker now shows friendly GPU names (for example
  `Intel UHD Graphics (renderD128)`) instead of raw `/dev/dri/*` paths.

### Changed
- `presets.json` is now bundled to runtime binary folders:
  - C/CMake targets copy to `build/bin/`
  - Free Pascal targets copy to `fpc/bin/`
- AppImage packaging includes `presets.json` and exports `PRESETS_PATH`
  so runtime preset discovery works out of the box.
- Preset search order now supports explicit `PRESETS_PATH` override before
  executable-adjacent and config-directory lookup.

### Fixed
- Resolved fallback-only preset behavior when `presets.json` was not located
  next to built binaries.
- Synchronized codec/preset visibility behavior across CLI and GUI flows.

### Phase 1 Status
- **Complete:** 10/10 planned tasks and approved enhancements finished.
- **Validated on Linux:** C CLI/GUI and Pascal CLI/GUI build and runtime checks.

## [3.0.0-Phase1.5] — 2026-08-21 (Development)

### Phase 1: Data-Driven Preset System Implementation

#### Task 1.5: Additional Testing ✅ COMPLETE
- **Extended Test Suite (C):** 21 comprehensive unit tests for edge cases
  - Malformed JSON graceful fallback
  - Missing file graceful fallback
  - Placeholder substitution with defaults
  - Performance benchmark: 0.432ms for 10,000 lookups (23M lookups/sec)
  - Error message clarity and consistency
  - List operations (platforms, codecs, presets)
- **Extended Test Suite (Pascal):** 21 unit tests with 100% feature parity
  - Identical test categories and assertions
  - Performance benchmark: ~1ms for 10,000 lookups (10M lookups/sec)
- **Path Discovery Tests:** 18 cross-platform integration tests
  - Linux XDG_CONFIG_HOME and ~/.config fallback
  - macOS ~/Library/Preferences path simulation
  - Windows %APPDATA% path simulation
  - Executable-adjacent file search
  - File precedence order verification
- **Cumulative Testing:** 134 total tests across all 5 completed Phase 1 tasks
  - C Core: 43 tests | Pascal Core: 31 tests
  - C Extended: 21 tests | Pascal Extended: 21 tests
  - Path Discovery: 18 tests
  - **Pass Rate:** 100% (134/134 tests passing)

#### Previous Tasks Summary:
- ✅ **Task 1.1:** JSON Schema Design
- ✅ **Task 1.2:** Initial presets.json (63 presets extracted from C/Pascal sources)
- ✅ **Task 1.3:** C Preset Loader (43 unit tests, O(1) lookup, jansson JSON parsing)
- ✅ **Task 1.4:** Pascal Preset Loader (31 unit tests, fpjson integration, TPresetDb class)

#### Key Features:
- **File Discovery:** 4-level fallback chain (explicit → executable-adjacent → platform config → built-in)
- **JSON Parsing:** Both C and Pascal handle malformed JSON gracefully
- **Placeholder Substitution:** Runtime variable injection ({vaapi_device}, {vk_device}, {vt_bitrate})
- **Performance:** Both implementations exceed 10M lookups/sec, vastly exceeding requirements
- **Cross-Platform:** Verified Linux, macOS, Windows path handling
- **Robustness:** No crashes on edge cases, proper nil/NULL pointer handling

#### Build Status:
- ✓ C CLI binary: Fully functional, all tests passing
- ✓ Pascal CLI binary: Fully functional, all tests passing
- ✓ Both implementations: Byte-for-byte compatible output

#### Phase 1 Progress:
- **Completion:** 62.5% (5 of 8 tasks complete)
- **Remaining:** Task 2–8 (ConvertOptions refactor, hardcoded branch removal, CLI, GUI, M4V, integration)
- **Timeline:** On track for 4-week Phase 1 completion

---

## [2.5.0] — 2026-06-27

### Fixed
- **Apple M4V HEVC playback on macOS/iOS**: the video copy step now passes
  `-tag:v hvc1` when the source codec is HEVC, so Apple hardware decoders
  recognize the stream (was `hev1`, which Apple devices reject).
- **Apple M4V color metadata**: video color space, transfer, and primaries are
  now probed from the source via `ffprobe` and passed through to the output,
  producing a `colr` (nclx) box matching HandBrake/Apple Compressor etalons.
- **Apple M4V audio track disposition**: the new step 5/6 runs an ffmpeg copy
  that sets `-disposition:a:0 default -disposition:a:1 0`, making the AAC track
  the primary audio and the AC3 track secondary — matching etalon files.

### Changed
- **AAC encoding standardized to CBR 320k** across all converter modes
  (`fdk_aac_320`, `fdk_aac_320_ac3_640`, `use_aac_for_h265`, Apple M4V creator)
  in both C and Pascal implementations. The `aac_at` and native `aac` fallback
  encoders also use CBR 320k.
- **Apple M4V AAC step** now uses `libfdk_aac -b:a 320k` (CBR) instead of
  VBR quality 5, matching the bitrate of HandBrake/Apple etalon files (~320 kbps).
- **M4V pipeline expanded to 6 steps** (was 5): new step 5/6 is the audio
  disposition fix applied after MP4Box mux and before the chapter import step.
- All converter encoder messages now include `(CBR 320k)` so users can see
  the active bitrate in log output.

### Removed
- `--m4v-aac-quality` CLI option: AAC bitrate is now a fixed 320k CBR and no
  longer user-configurable. Interactive menu step 13 (AAC quality selector)
  removed from CLI.
- `M4VOptions.aac_quality`, `CliM4VOptions.aac_quality`,
  `AppleM4VOptions.aacQuality`, `TAppleM4VOptions.AacQuality` fields removed
  from all M4V option structs (C and Pascal).
- `FDK AAC VBR` spin button removed from Linux GTK4 M4V options dialog.
- `AAC quality (1..9)` text field removed from macOS Cocoa M4V options dialog.
- `AAC quality` dialog step removed from Pascal GUI M4V options.
- `--m4v-aac-quality` argument parsing removed from Pascal CLI.
- `opts.aac_quality` assignments removed from all bridge/option-copy code paths.

### Documentation
- CLI `--help` updated: removed `--m4v-aac-quality`, added note that AAC
  encoding uses `libfdk_aac CBR 320k (fixed)`.
- CLI summary output shows `M4V AAC: CBR 320k (libfdk_aac)` instead of the
  old `M4V AAC qual: N` line.
- All README and doc version references bumped from 2.4 to 2.5.

---

## [2.4.0] — 2026-04-27

### Added
- **macOS is now C-only** — native Cocoa GUI is the sole macOS implementation.
  Pascal macOS support discontinued.
- **Windows C CLI expanded** — now the primary and most complete Windows implementation.
- **Linux and Windows feature parity** — both C and Pascal implementations support
  identical codec sets, audio modes, and mux workflows.
- New build system configuration for unified platform detection (CMake + FPC).
- Enhanced tool discovery for bundled binaries:
  - Linux: searches `build/bin/` first, then falls back to `src/platform/linux/bin/`,
    then PATH.
  - Windows: searches `build-msvc/src/cli/Release/`, then bundled `src/platform/windows/bin/`,
    then PATH.
  - macOS: searches bundled `.app` resources, then MacPorts paths, then system PATH.

### Changed
- macOS no longer includes Pascal implementation or packaging scripts.
- Documentation updated to reflect platform feature coverage (C primary on all,
  Pascal available on Linux/Windows).
- Windows CLI build scripts (PowerShell/CMD) updated to reflect new target structure.
- CMakeLists.txt reorganized for clearer platform-specific configurations.

### Removed
- Pascal macOS `.app` packaging and build support (`fpc/build/package_macos_app.sh`).
- Pascal macOS CLI and GUI targets from repository.
- macOS-specific Pascal converter implementations.

### Platform Status (v2.4)
- **macOS**: C CLI + native Cocoa GUI (stable, no new functions).
- **Linux**: C CLI + GTK4 GUI, Pascal CLI + LCL GUI (feature-matched).
- **Windows**: C CLI (most complete), Pascal CLI + GUI (feature-matched).

---

## [2.2.0] — 2026-04-11 (archived)

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
- **AppImage icon and desktop name** — the C GUI AppImage now ships
  `src/gui/icon.png` as `ffmpeg-converter` (matching
  `gtk_window_set_icon_name`) in the AppDir root, `.DirIcon`, and the
  hicolor 256/1024 trees, plus `usr/share/applications/ffmpeg-converter.desktop`.
  Display name is **FFMpeg-Converter**; `StartupWMClass` is the GtkApplication
  id so GNOME dock / Ubuntu panel and the applications menu show the icon.
  Output file: `FFMpeg-Converter-<arch>.AppImage`. Packaging fails if
  `icon.png` is missing.

### Changed
- Linux GTK4 window title and `g_set_application_name` are now
  `FFMpeg-Converter` (was `ffmpeg-converter GUI`).
- **Pascal Linux GUI switched to the Qt6 widgetset (P5)** — `make -C
  fpc/build gui` now builds with `--ws=qt6` (default) for GNOME + Wayland;
  GTK3 remains via `GUI_WS=gtk3`. Requires `libQt6Pas.so.6` built from the
  Qt6 cbindings shipped with Lazarus; the Makefile verifies it and prints
  the build steps when missing. Verified: Qt6 GUI builds (28 MB) and starts
  clean (no Gtk-CRITICAL warnings).
- **Pascal GUI build diagnostics** — `make -C fpc/build gui` saves the
  lazbuild log to `fpc/build/.units/gui-build.log`, prints the compiler
  errors, and gives a correct hint per failure (GTK3 dev libs vs LCL GTK3
  widgetset). The old hint suggested `apt install lcl-gtk3`, which does not
  exist on Ubuntu/Debian — the correct fix is Lazarus from lazarus-ide.org.

### Stage log — P2 implementation
- [P2.1 — done] M4V «edit-before-mux» в Linux C GUI: `M4VOptions.edit_before_mux`,
  чекбокс в диалоге Apple m4v, edit flow в `run_gui_m4v_job`
  (main worker → m4v из конвертированных файлов → cleanup).
- [P2.2 — done] `--version` в C CLI (`FFMPEG_CONVERTER_VERSION "2.6.0"`,
  quick-exit в `main.c`, строка в `print_usage`); `CMakeLists.txt` VERSION
  синхронизирован 2.5.0 → 2.6.0.
- [P2.3 — done] `--dry-run` в C CLI (`ConvertOptions.dry_run`, парсинг,
  строка в summary, план операций в `main.c` без запуска ffmpeg).
- [verify — done] `linux_cli`/`linux_gui` собраны; `--version`,
  `--dry-run` (prores_ks и m4v), обычное кодирование и старт GUI проверены.

### Stage log — P3/P4 implementation
- [P3.1 — done] Linux GUI widgetset pinned to gtk3 via Makefile (Linux only;
  Windows keeps native widgetset — `form.lpi` is shared, so the widgetset
  cannot be pinned inside the `.lpi`).
- [P3.2 — done] `fpc/gui/form.lps` cleaned: removed stale
  `Temp/HQ_converter/...` module entry, fixed working directory path.
- [P3.3 — done] Duplicate `fpc/gui/form.lpr` removed (project uses `main.lpr`).
- [P3.4 — done] `make gui` prints a clear hint when the LCL GTK3 widgetset
  is missing (`lcl-gtk3`).
- [P3.5 — done] `fpc/README.md` rewritten for v2.6 + strategy; `gui-app`
  target is now an alias of `appimage` (no more misleading macOS message).
- [P4.1 — done] Strategy recorded in `fpc/README.md`: Linux GUI = C/GTK4;
  Pascal on Linux = CLI + library; Pascal GUI = Windows-only.
- [P4.2 — done] Pascal interactive codec menu is dynamic — built from the
  runtime probe (vaapi/nvenc/amf/qsv/prores_ks_vulkan/m4v) instead of a
  hard-coded list; multi-digit choices via `ReadChoiceNum`.
- [P4.3 — done] `--vk-device`/`--vk_device` unified (parsing already accepted
  both; help documents the canonical form).
- [P4.4 — done] `fpc/CHANGELOG.md` synced with reality: stale drag-and-drop
  claim in [1.1.0] flagged; current LCL GUI has no drag-and-drop.
- [verify — done] Lazarus 4.8 + FPC 3.2.2 (official site): LCL GTK3 widgetset
  present; Pascal GUI builds via `make -C fpc/build gui` (first lazbuild link
  failed with "cannot find -lgtk-3" until the LCL units were rebuilt by a
  direct fpc invocation — now clean). Pascal CLI dynamic menu shows 7 codecs
  on this machine (vaapi + m4v; vulkan hidden — not in the bundled ffmpeg).
  C `linux_cli`/`linux_gui` build; C GUI starts clean.

### Known issues — Linux hardware probe (diagnosed 2026-08-20, fix deferred)
- **`prores_ks_vulkan` missing from the new bundled ffmpeg (user's 8.1 build)**
  — the rebuild added VAAPI/AMF/Vulkan/OpenCL but lost the ProRes Vulkan
  encoder: `ffmpeg -h encoder=prores_ks_vulkan` → "Codec ... is not
  recognized". `h264_vulkan`/`hevc_vulkan`/`av1_vulkan` are present, only
  `prores_ks_vulkan` is gone (the previous bundle had it). Needs to be
  re-enabled in the ffmpeg build config; no program code change required.
- **Vulkan probe counts software devices as working GPUs** — ffmpeg's Vulkan
  GPU listing includes `llvmpipe (software)` at `vk:2`; the `vk:0..vk:7`
  probe treats it as a working device, so a 2-GPU system reports
  `vulkan_device_index=2` and the GUI recommends `vk:2` (llvmpipe). Needs a
  fix when work resumes: parse the `GPU listing:` block and exclude entries
  tagged `(software)` before probing.

### Stage log — P1 implementation
- [P1.1+P1.2 — done] `converter_linux.c` now emits full hardware encoder
  flags (NVENC/QSV quality, `prores_ks_vulkan` profile + 4444 pixel format).
- [P1.2 GUI — done] GTK4 profile combo enabled for `prores_ks_vulkan`.
- [P1.3 — reverted] VAAPI `-rc_mode ICQ -global_quality 22|25` was rolled
  back to `-rc_mode auto` in both `converter_linux.c` and
  `converter_cmd_builder.pas`: the radeonsi driver rejects ICQ
  ("Driver does not support ICQ RC mode (supported modes: CQP, CBR, VBR)").
- [P1.4 — done] `--hw_device` CLI flag (parse, help, summary, no-override
  auto-detection, zeroed option structs).
- [VAAPI device selector — done] GTK4 GUI now shows a "VAAPI dev:" dropdown
  (auto + each usable `/dev/dri/renderD*` node) for `h264_vaapi`/`hevc_vaapi`.
- [verify — done] `linux_cli`/`linux_gui` build; end-to-end `h264_vaapi` and
  `hevc_vaapi` encodes succeed with the new bundled ffmpeg; GUI starts clean.

### Added
- **C CLI: `--hw_device <path>` option** — overrides the VAAPI render node
  used by `h264_vaapi` / `hevc_vaapi` (Linux only). Without it, the engine
  auto-selects the first working render node from the startup probe, matching
  the existing Pascal CLI behavior.
- **C CLI: `--version`** — prints `ffmpeg_converter <version>` and exits.
  Version source: `FFMPEG_CONVERTER_VERSION` in `src/cli/cli_common.h`,
  synced with `CMakeLists.txt` (2.6.0) and the changelog.
- **C CLI: `--dry-run`** — prints the planned operations
  (`[codec] input -> output` for every file, incl. m4v output names) and
  exits without running ffmpeg. Summary shows "Dry run: yes".
- **Linux GTK4 GUI: M4V «edit-before-mux» mode** — new checkbox in the
  Apple m4v options dialog: first runs the main conversion with the GUI
  options, then creates .m4v from each converted file, then deletes the
  intermediate files (parity with the Pascal GUI `chkM4VEditBeforeMux` and
  the macOS GUI `m4vEditCheck`).
- **Linux GTK4 GUI: VAAPI render-node selector** — a "VAAPI dev:" dropdown
  (auto / each usable `/dev/dri/renderD*` node) is shown when a VAAPI codec
  is selected; the chosen node is passed as `-vaapi_device`. Auto mode lets
  the converter pick the first working node (same as before).

### Changed
- **Linux C engine: full hardware encoder quality flags** — NVENC
  (`-preset p7 -qp 22 -spatial_aq 1 -temporal_aq 1` for H.264,
  `-preset hq -cq 25 -lookahead_level auto` for HEVC), QSV
  (`-global_quality 22 ...` for H.264, `-global_quality 25 ...` for HEVC),
  and `prores_ks_vulkan` (`-profile:v lt|standard|hq|4444` + `yuv444p10le`
  upload for the 4444 profile) now match the Windows C engine and the Pascal
  command builder.
- **Linux VAAPI rate control kept at `-rc_mode auto`** — the temporary
  `-rc_mode ICQ -global_quality 22|25` change (P1.3) was reverted after
  testing: the radeonsi VAAPI driver reports "Driver does not support ICQ RC
  mode (supported modes: CQP, CBR, VBR)" and the encode fails. `auto` works
  on all tested drivers.

### Fixed
- **CLI `ConvertOptions` uninitialized fields** — `parse_args()` and
  `run_menu()` now zero the options struct first; `opts.hw_device` and
  `opts.vulkan_device` were previously read uninitialized in menu mode
  (could pass a garbage Vulkan device index to `prores_ks_vulkan`).

## [2.6.0] — 2026-07-30

### Fixed — Linux GTK4 GUI
- **Critical: startup freeze** — `linux_probe_codec_support()` (10–25 serial
  ffmpeg probes for VAAPI/NVENC/AMF/QSV/Vulkan) was called synchronously on
  the main thread, blocking the GTK event loop for several seconds before the
  window appeared. Moved to a `GThread`; codec combo is repopulated from the
  main thread via `g_idle_add` when the probe finishes. Window now opens
  instantly.
- **Critical: GPU renderer freeze** — the NGL/GL/Vulkan GSK renderers freeze
  on some Mesa and Nvidia driver combinations. `main()` now sets
  `GSK_RENDERER=cairo` before GTK init when the variable is not already set.
  Users who want GPU acceleration can override with `GSK_RENDERER=ngl`.
- **M4V dialog nested event loop** — the Apple M4V options dialog used a
  nested `g_main_loop_run()` inside a signal handler, which is forbidden in
  GTK4 and caused re-entrancy hangs. Replaced with a fully asynchronous
  `GtkWindow + GtkHeaderBar` dialog; state is passed via
  `g_object_set_data_full`.
- **UAF on file removal** — `on_remove_file_clicked()` called
  `g_object_get_data()` to get the path pointer, then passed it to
  `g_ptr_array_remove()` which freed it via the array's destroy function,
  leaving the widget holding a dangling pointer. Fixed with
  `g_object_steal_data()` to atomically clear the widget association before
  the free.
- **g_widgets race on shutdown** — `shutdown_conversion()` cleared the global
  `g_widgets` pointer while the worker thread was still inside the
  `ConverterCallbacks` functions that read it. The pointer is now cleared by
  the worker thread itself at the end of `run_converter()` cleanup; the main
  thread clears it again after `g_thread_join` as a safety net.

### Added — Linux GTK4 GUI
- **Drag-and-drop** — `GtkDropTarget(GDK_TYPE_FILE_LIST)` installed on the
  file listbox; accepts file drops from any file manager. A blue dashed border
  + tinted background CSS highlight shows while dragging. Duplicates are
  silently ignored.
- **Light/dark theme adaptation** — subscribes to
  `notify::gtk-application-prefer-dark-theme` on `GtkSettings`; all custom
  CSS uses `rgba()` values that render correctly in both themes.
- **Application icon** — `icon.png` embedded as a `GResource` bundle (compiled
  at build time via `glib-compile-resources`); registered with
  `gtk_icon_theme_add_resource_path` so `gtk_window_set_icon_name` resolves it.
- **Keyboard shortcuts** — five `GSimpleAction`s registered on the
  `GtkApplication` with system-level accelerators:
  | Action | Shortcut |
  |--------|----------|
  | Add files | Ctrl+O |
  | Remove selected | Delete |
  | Clear list | Ctrl+L |
  | Start conversion | Ctrl+Return |
  | Stop conversion | Escape |
- **Resizable file list / log pane** — replaced two competing `vexpand=TRUE`
  scrolled windows with a `GtkPaned(VERTICAL)` widget; the user can drag the
  divider between the file queue and the log area.
- **Tooltips** — 15 `gtk_widget_set_tooltip_text()` calls covering every
  interactive control (codec combo, profile, deblock, audio norm, genre,
  audio output, overwrite checkbox, output dir, all file buttons, start/stop).
- **File deduplication** — adding the same file twice silently skips it.
- **Persistent log autoscroll mark** — replaced per-message
  `create_mark/delete_mark` calls with a single persistent `GtkTextMark`
  (`log_end_mark`), eliminating a heap allocation per log line.

### Changed — Linux GTK4 GUI
- Migrated all deprecated GTK APIs to their GTK 4.6+ equivalents:
  - `GtkFileChooserDialog` → `GtkFileDialog` (async open-multiple / open /
    select-folder).
  - All 7 `GtkComboBoxText` widgets → `GtkDropDown + GtkStringList`.
  - `GtkDialog` (M4V options) → `GtkWindow + GtkHeaderBar`.
  - `"changed"` signal on combos → `"notify::selected"`.
  - `G_APPLICATION_FLAGS_NONE` → `G_APPLICATION_DEFAULT_FLAGS`.
- App ID corrected from `com.example.*` to
  `io.github.possible947.ffmpeg_converter` (required for portal and taskbar
  integration; also updated in `.desktop` file `StartupWMClass`).
- All 7 combo boxes set `hexpand=TRUE` so they fill their grid cells.
- Path labels (`output_dir_label`, `video_track_label`, `status_label`) use
  `PANGO_ELLIPSIZE_MIDDLE` — long paths show `…` in the middle, preserving
  both root and filename.
- Section separators added between Video/Audio, Output, and Files control
  groups.
- Log view CSS class `"log"` with monospace font (`9pt`).

### Internal — Linux GTK4 GUI
- Extracted `src/gui/gui_codec_utils.h` with 4 `static inline` codec
  predicates shared between `gui_window.c` and `gui_callbacks.c`, removing
  duplicate definitions and a return-type mismatch (`gboolean` vs `int`).
- Removed `-Wno-deprecated-declarations` from CMake compile options (no longer
  needed after API migration).

### Build — Linux GTK4 GUI
- `src/gui/CMakeLists.txt` now requires `glib-compile-resources` and generates
  `resources.c` from `resources.gresource.xml` at configure time.

### Added
- **AV1 input decoding support (Windows and Linux).**
  The native `av1` decoder in ffmpeg triggers NVIDIA NVDEC pixel-format negotiation even
  on GPUs that do not support AV1 hardware decoding, producing a fatal
  `"Failed to get pixel format"` error. The converter now detects the input video codec
  via `ffprobe` before encoding and selects the appropriate decoder automatically:
  - `av1_qsv` — used when Intel QSV encoders are detected (Intel Arc / UHD via libvpl).
  - `libdav1d` — pure-software fallback, used when QSV is unavailable.
  - Native `av1` with `-hwaccel none` — only when neither decoder is detected.
  Detection is runtime-based via `ffmpeg -decoders` at startup. **Requires ffmpeg
  built with `--enable-libdav1d`.** Windows bundled binary includes `libdav1d-7.dll`.
- `-hwaccel none` added to `peak_two_pass` and `loudnorm_two_pass` analysis passes
  to prevent NVDEC negotiation failure on AV1 input during audio analysis.

### Fixed
- **Mux postprocess temporary output collisions (`codec=mux`).**
  `src/mux/mux.c` now generates unique temp output names for post-mux files instead of
  a single fixed `<output>.postmux.tmp.mkv` name, preventing false "file exists" failures
  on repeated runs and concurrent jobs.
- **macOS native GUI locale-sensitive mux failures with Unicode filenames.**
  Finder-launched GUI sessions could inherit non-UTF-8 locale settings (`LC_ALL=C`), causing
  `mkvmerge` to truncate Unicode paths and mis-detect output/source file equality. GUI startup
  now forces UTF-8 locale variables when needed.
- **Apple M4V chapter import reliability (C path).**
  M4V chapter import no longer relies on `MP4Box -chap chapters.txt` text parsing in C paths.
  Chapter transfer now uses `ffmpeg` metadata mapping (`-map 0 -map_chapters 1 -c copy`) on the
  produced M4V, eliminating failures on long chapter lists and chapter-title parsing edge cases.
- **Critical: loudness normalization 2-pass producing wrong gain (~10–14 LUFS error).**
  FFmpeg `loudnorm` filter always outputs measurement values as JSON strings
  (e.g. `"input_i" : "-12.88"`), not JSON numbers. `json_number_value()` from
  jansson returns 0.0 for non-number types. All five fields (`input_i`,
  `input_tp`, `input_lra`, `input_thresh`, `target_offset`) parsed as 0.0,
  causing 2nd pass to run with `measured_I=0.00` etc. Fixed via new helper
  `json_number_or_string_value()` that falls back to `atof(json_string_value())`
  when the JSON node is a string.
- **ffmpeg/ffprobe resolution on macOS: bundled binary not found.**
  `get_exe_dir()` used `readlink /proc/self/exe` which only works on Linux.
  On macOS it silently returned an empty string, causing `resolve_bundled_bin()`
  to always fail and falling through to hardcoded MacPorts paths or bare
  `"ffmpeg"` string. Fixed by using `_NSGetExecutablePath` + `realpath` on
  `#if defined(__APPLE__)`. MacPorts hardcoded candidates removed.
  `resolve_bundled_bin()` now also checks `<exe_dir>/../Resources/bin/<name>`
  to cover the `.app` bundle layout. Missing binary returns `""` (error) instead
  of a bare tool name that might resolve to an unrelated system binary.

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
- Native macOS GUI mux workflow support (`codec = mux`):
  - one-source-file mux session model
  - replacement video track selection in GUI (`Add track...`)
  - post-mux stage routed through shared `src/mux/` module
  - final output written as `.mkv` via `mkvmerge`
- Native macOS GUI audio output mode selector aligned with Linux GUI:
  - `pcm`
  - `fdk_aac_q5`
  - `fdk_aac_q5_ac3_640`
- macOS app bundling for `mkvmerge` and dependent dylibs:
  - helper script `src/gui_macos_native/bundle_mkvmerge_deps.sh`
  - staged into app `Contents/Resources/bin/` when found
- Native macOS Apple M4V video codec preflight in C path:
  - probes selected video stream codec via `ffprobe` before M4V steps
  - allows only `h264`, `hevc`, `prores`
  - rejects unsupported codecs early with clear error message
- Native macOS Apple M4V AAC encoder runtime preference chain:
  - `aac_at` -> `libfdk_aac` -> native `aac`
  - automatic fallback based on local ffmpeg encoder availability

### Changed
- macOS codec popup width adjusted to 160 px to prevent overlap with adjacent
  column in the native GUI.
- Native macOS GUI `onAddFilesClicked:` now uses `[NSOpenPanel runModal]`
  instead of `beginSheetModalForWindow:`.
- `src/gui_macos_native/CMakeLists.txt`: added `MACOSX_BUNDLE_INFO_PLIST`
  property pointing to the new `Info.plist.in` template.
- macOS codec popup items updated to: `copy`, `prores`, `prores_ks`,
  `prores_videotoolbox`, `hevc_videotoolbox`, `mux`.
- macOS `updateDependentControls`: profile control enabled for `prores`,
  `prores_ks`, and `prores_videotoolbox`; deblock enabled for `prores` and
  `prores_ks` only (hardware encoders excluded).
- Native macOS bridge output-name prediction now delegates to shared
  converter output naming (`converter_make_output_name`) to keep extension
  rules aligned with core converter behavior.
- Shared C ProRes profile handling hardened:
  - invalid/unset profile now defaults to `standard`
  - `prores_ks` now uses explicit profile names (`lt|standard|hq|4444`)
    instead of relying on ambiguous auto behavior
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

## [2.1.0] — 2026-03-19 (archived)

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

## [2.0.0] — 2026-03-16 (archived)

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

## [1.1.0] — 2026-01-23 (archived)

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

## [1.0.0] — 2025-12-01 (archived)

### Added
- Initial project version.
- Basic ffmpeg command generation logic.
- Progress bar with FPS and ETA display.
- Audio normalization support (peak, loudnorm).
- ProRes codec support with profiles.
- Initial CLI implementation.
