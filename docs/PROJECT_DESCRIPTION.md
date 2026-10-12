# ffmpeg_converter — Developer Description (Version 3.0, Phase 3 complete)

This document describes the current, factual architecture of the repository
after Phase 3.0 (pre-integration refactor) and Phase 3.1 (HQ Converter Library
integration). It is aligned with the executable sources, CMake files,
Makefiles, and validated Linux/macOS behavior. Phase 4 (filter catalog) has
not started.

## Version 3.0 Status

Phase 3 (3.0 refactor + 3.1 HQ Converter integration) is complete and has been
reviewed and tested end-to-end. The Linux C application (CLI + GTK4 GUI) and
the macOS C application (CLI + native Cocoa GUI) are the sole supported
implementations on those platforms; both have been functionally tested,
including the new `hq_converter` codec group and mux workflows. **Windows C
support (CLI, GUI, platform sources) has been removed entirely** — Windows is
now served only by the Free Pascal port, which has in turn become a
**Windows-only** target (its Linux and macOS build paths were removed; the
Pascal Makefile hard-blocks non-Windows hosts). See "2.2 Free Pascal" below.

## Phase 3 Status

- The single generated `presets.json` is the source of truth for GUI/CLI
  selection metadata and execution presets, for both the `src/` C
  implementation and the Windows-only Pascal implementation.
- Linux C and macOS C GUI paths use the shared semantic model:
  **Group -> Encoder -> Preset**, now joined by a fourth, feature-gated
  `hq_converter` group (see "8. HQ Converter Integration (Phase 3.1)" below).
- Runtime probes filter hardware groups/encoders after static catalog loading.
  Linux hardware groups now include VAAPI, NVENC, AMF, QSV, and Vulkan
  (`prores_ks_vulkan`, `h264_vulkan`, `hevc_vulkan`, `av1_vulkan`); macOS has
  the VideoToolbox group (`prores_videotoolbox`, `h264_videotoolbox`,
  `hevc_videotoolbox`).
- Linux C and macOS C GUIs share the approved three-column layout: Video,
  Audio, and Filters. The visual reference is `docs/maket_example.png`.
- Linux C has been tested across the available codec, audio, device, mux,
  M4V, HQ Converter, theme, and repeated-selection combinations and is
  accepted for use. macOS C CLI + native Cocoa GUI have likewise been built
  and functionally validated, including HQ Converter + mux workflows.
- C AppImage packaging (`package_appimage` target, `ENABLE_APPIMAGE=ON`) is
  configured and includes the bundled `third_party/hq_converter` runtime tree
  when HQ Converter is enabled.
- Phase 4 (structured filter catalog) has not started; the Filter/Filter
  Preset controls remain compatibility controls mapped to the legacy
  `deblock` ABI field.

## Preset and Selection Architecture

### One generated catalog

The repository stores the catalog template in `presets.json.in`. During CMake
configuration, CMake writes the platform-specific generated catalog to
`build/generated/presets.json`. The same generated file is copied beside C
CLI/GUI binaries and into the Linux AppImage. The Windows Pascal build locates
`presets.json` beside the executable, through `PRESETS_PATH`, or via the
repository-development fallback path.

The catalog has two distinct layers in one JSON document:

1. `selection` describes user-facing groups, encoders, static enablement,
   capability requirements, bit-depth variants, and references to execution
   presets.
2. `linux`, `macos`, and `windows` contain execution preset records: FFmpeg
   arguments, containers, pixel formats, input arguments, filters, pipelines,
   and runtime requirements.

The CMake-generated template (`presets.json.in`, consumed by the C build via
`build/generated/presets.json`) now only emits the `linux` and `macos`
execution blocks, since C Windows support was removed. The committed root
`presets.json` consumed directly by the Windows-only Pascal build still
carries a `windows` execution block for Pascal's own codec/preset set; the two
files are generated/maintained independently and are not required to be
identical outside the shared `selection` layer's intent.

Keeping these layers together prevents the GUI from inventing a second catalog
while allowing the build system to gate platform-specific groups.

### Selection layer

Platform-independent groups live under `selection.common`:

- `software` contains software encoders such as `prores` and `prores_ks`;
- `mux` is the single pipeline group with Encoder modes `copy`, `mkv`, `mov`,
  and `m4v`;
- `hq_converter` is feature-gated and references the prepared external runtime
  catalog when enabled.

Platform hardware groups live under
`selection.platforms.<platform>.hwaccel.groups`. Each group contains encoder
entries with fields such as `enabled`, `final_codec`, `requires`, optional
`presets`, and 10-bit metadata (`bit_depths`, `pixel_format`, `profile_args`).

The GUI model filters disabled groups and entries first, then applies runtime
capability results. Empty groups are hidden. The C model is exposed through
`src/platform/selection_catalog.h/.c`; the Pascal mirror is
`fpc/converter/selection_catalog.pas`.

### Resolution boundary

Widgets never construct FFmpeg arguments. They hold the semantic tuple and
resolve it once at the options boundary:

```text
(group, encoder, preset) -> existing converter codec + preset
```

The converter ABI remains stable and continues to receive the existing final
codec string. Special mux modes are resolved as follows:

- `mux/copy` -> `codec=copy`, `preset=default`, no replacement track;
- `mux/mkv` or `mux/mov` -> `codec=mux`, preset equal to the selected mode,
  replacement track required;
- `mux/m4v` -> `codec=mux`, `preset=m4v`, replacement track required;
- standalone Apple M4V uses its dedicated workflow and a `copy/default`
  intermediate rather than passing `m4v` to ordinary conversion.

For normal software/hardware entries, `final_codec` is copied to the legacy
ABI field. A `_10bit` encoder variant resolves to the corresponding synthetic
internal codec and retains its catalog pixel/profile metadata.

### Execution preset layer

When an encoder is selected, its optional `presets` list is used if present;
otherwise the model resolves the execution codec key in the current platform
section. The GUI preserves a preset only if it remains valid after a parent
change. If no explicit selection exists, the established fallback order is
`standard`, then `default`, then the first available preset.

Mux has no meaningful third-level preset selection in the GUI. Its Preset
control is disabled and displays `default`; the Encoder mode supplies the
container/pipeline choice.

### Runtime bundling

The C Linux AppImage contains the GUI binary, FFmpeg, FFprobe, MKVToolNix,
MP4Box, generated `presets.json`, shared runtime libraries, and the complete
enabled `third_party/hq_converter` tree. The AppImage sets `PRESETS_PATH` and
tool environment variables through `AppRun`. HQ_converter is treated as an
opaque immutable runtime tree and is never rebuilt or updated by the app.

Pascal (Windows-only) packaging is a native Windows concern and does not use
an AppImage.

## 1. Project Scope

ffmpeg_converter is a cross-platform media conversion project with two
independent implementations:

- **C/CMake** in `src/` — primary implementation, Linux and macOS only
  (Windows C support was removed in Phase 3.0)
- **Free Pascal** in `fpc/` — Windows-only release target (Linux and macOS
  Pascal build paths were removed; the Makefile hard-blocks non-Windows hosts)

Both paths provide conversion workflows around external `ffmpeg`/`ffprobe`.

## 2. Implementations (Phase 3 complete)

### 2.1 C/CMake (`src/`)

**Platform coverage:**
- **macOS**: CLI + native Cocoa GUI (sole macOS implementation)
- **Linux**: CLI + GTK4 GUI (C release path; fully tested)
- **Windows**: removed in Phase 3.0 (no C CLI, no C GUI); see Free Pascal below

**Key modules:**
- Core engine: `src/converter/converter.c`, `src/converter/converter.h`
- Shared CLI entry point: `src/cli/main.c`, `src/cli/cli_common.c/.h`,
  platform specifics in `src/cli/platform/cli_linux.c` and `cli_macos.c`
- Linux GUI (GTK4): `src/gui/`
- macOS native GUI (Cocoa/AppKit): `src/gui_macos_native/`
- Audio orchestrator: `src/audio/audio_orchestrator.c/.h` (isolated in
  Phase 3.0; shared by every workflow that needs audio processing)
- Add Track convergence point: `src/add_track/add_track.c/.h` (isolated in
  Phase 3.0; the single shared pipeline for manual mux **and** HQ Converter
  output — see "8. HQ Converter Integration" below)
- Mux module: `src/mux/`
- Apple M4V module: `src/m4v/`
- HQ Converter adapter: `src/hq/` (`hq_catalog.c` for runtime catalog
  discovery, `hq_adapter.c` for job execution, `hq_worker_main.c` for the
  isolated per-job `hq_worker` helper binary)

**Codecs:**
- Cross-platform: `copy`, `prores`, `prores_ks`, `hq_converter` (feature-gated,
  Linux/macOS only — see section 8)
- Linux (runtime-detected): VAAPI (`h264_vaapi`, `hevc_vaapi`, `av1_vaapi`,
  plus 10-bit HEVC/AV1 variants), NVIDIA NVENC (`h264_nvenc`, `hevc_nvenc`,
  `av1_nvenc`), AMD AMF (`h264_amf`, `hevc_amf`, `av1_amf`), Intel QSV
  (`h264_qsv`, `hevc_qsv`, `av1_qsv`, plus 10-bit variants), and Vulkan
  (`prores_ks_vulkan`, `h264_vulkan`, `hevc_vulkan`, `av1_vulkan`)
- macOS (VideoToolbox): `prores_videotoolbox`, `h264_videotoolbox`,
  `hevc_videotoolbox`
- All platforms: AV1 input decoding (auto-selected decoder)

**Audio normalization:**
- `none`, `peak`, `peak_2pass`, `loudness`, `loudness_2pass`
- 2-pass uses `-filter_threads N` for parallel processing
- Implemented once in the shared audio orchestrator (`src/audio/`), used
  identically by ordinary conversion, mux mode, M4V, and HQ Converter jobs

**Special workflows:**
- Mux mode: one source file + replacement video track → `.mkv`/`.mov` via
  `mkvmerge`, or `.m4v` via the Apple M4V pipeline
- Apple M4V creator: multi-step pipeline (video copy → AAC → AC3 → MP4Box mux → optional chapters)
- HQ Converter: external HQ Converter Library produces the replacement video
  track; `add_track_execute()` then performs the same audio/mux work as
  manual mux (see section 8)

### 2.2 Free Pascal (`fpc/`) — Windows-Only

**Platform coverage:**
- **Windows**: CLI + Lazarus/LCL GUI with Vulkan GPU support (sole Pascal
  target and release target)
- **Linux**: removed — the Pascal Makefile (`fpc/build/Makefile`)
  intentionally errors out ("Pascal/FPC builds are supported on Windows
  only.") on any non-Windows host; use the C/CMake implementation
- **macOS**: unsupported Pascal target (discontinued v2.4+); use the C/Cocoa
  implementation

**Key modules:**
- Engine: `fpc/converter/converter_core.pas`
- CLI: `fpc/cli/ffmpeg_converter_windows.lpr`
- GUI: `fpc/gui/form.pas` (Lazarus/LCL)
- Apple M4V: `fpc/converter/apple_m4v_creator.pas`
- JSON parsing: `fpc/json/loudnorm_json.pas`
- Tests: `fpc/test/test_*.pas` (currently no tracked test sources; the
  Makefile `tests` target skips cleanly when the directory is empty)
- No HQ Converter counterpart — HQ Converter integration is C/Linux/macOS
  only (see section 8); Pascal/Windows does not have an `hq_converter` group

**Codec support:**
- Feature-parity with the C CLI's original Windows codec set (NVENC, AMF,
  QSV, Vulkan ProRes) — maintained independently now that C Windows support
  has been removed
- Runtime probing for Vulkan device selection in the Windows GUI

**C ABI export (for library usage):**
- `fpc/converter/converter_pas.lpr` exports C-compatible shared library
- 8 exported functions: `converter_create`, `converter_destroy`, `converter_set_callbacks`,
  `converter_set_options`, `converter_process_files`, `converter_make_output_name`,
  `converter_stop`, `converter_error_string`
- Any change to `src/converter/converter.h` must be mirrored here to keep the
  two implementations' ABIs in sync

## 3. Apple M4V Workflow

Implemented in both C and Pascal (where available):

- **C macOS**: `src/gui_macos_native/apple_m4v_creator.m` and bridge in `converter_bridge.m`
- **C Linux**: GTK GUI action; shared backend in `src/m4v/`
- **Pascal (Windows)**: `fpc/converter/apple_m4v_creator.pas`

**Pipeline (all platforms):**
1. Extract video track to temporary `.mp4` (stream copy)
2. Encode AAC audio (`libfdk_aac -b:a 320k`, fixed CBR)
3. Encode AC3 audio (configurable bitrate: 384/448/640 kbps)
4. Mux tracks into `.m4v` via `MP4Box`
5. Embed chapter markers (optional) by transferring chapter metadata from source
   with `ffmpeg -map_chapters 1 -c copy`

**Supported video codecs (preflight check):**
- `h264`, `hevc`, `prores` (others rejected with clear error)

## 4. Build Targets

### 4.1 C/CMake targets

**Linux:**
- `linux_cli` — CLI binary
- `linux_gui` — GTK4 GUI binary
- `package_appimage` — AppImage (optional, requires `ENABLE_APPIMAGE=ON`;
  bundles the enabled `third_party/hq_converter` tree when HQ Converter is
  enabled)

**macOS (C only):**
- `macos_cli` — CLI binary
- `macos_gui_native` — native Cocoa GUI
- `MACOS_BUNDLE_INFO_PLIST` — Info.plist for `.app` bundle

**Shared (both Linux and macOS, when `third_party/hq_converter` is prepared):**
- `hq_worker` — minimal helper binary used for per-job process isolation by
  the HQ Converter adapter (see section 8); built as a dependency of
  `linux_cli`/`linux_gui` and copied next to `macos_cli`/`macos_gui_native`

**Windows:** removed in Phase 3.0 — there is no `windows_cli`/C GUI target
anymore. Use the Free Pascal build for Windows.

**CMake feature switches:**
- `ENABLE_LINUX_GUI` (default ON on Linux)
- `ENABLE_MACOS_NATIVE_GUI` (default ON on macOS)
- `HQ_CONVERTER_ENABLED` — computed automatically from the presence of a
  valid prepared release under `third_party/hq_converter/` (not a
  user-facing cache option); see section 8

### 4.2 Pascal/Make targets (Windows only)

In `fpc/build/Makefile`:
- `cli` — CLI binary
- `lib` — shared library (C ABI export)
- `gui` — Lazarus/LCL GUI (needs `lazbuild`)
- `tests` — all unit tests (currently a no-op: `fpc/test/` has no tracked
  sources, so the target prints a "no tests found" message and exits cleanly)

The Makefile errors out immediately on any non-Windows host.

**Platform-specific:**
- Windows: produces `fpc/cli/ffmpeg_converter_windows.exe`

## 5. Runtime Dependencies

### C Path (Linux, macOS)
- `ffmpeg`, `ffprobe` (bundled, or discovered via env/PATH)
- `jansson` (system library for loudnorm JSON parsing)
- `mkvmerge` (optional, for mux mode)
- `MP4Box` (optional, for Apple M4V creator)
- **Linux GUI**: GTK4
- **macOS GUI**: AppKit (native framework, no GTK)
- **HQ Converter (optional)**: prepared release tree under
  `third_party/hq_converter/` — Linux and macOS only; not used/rebuilt at
  runtime (treated as an immutable, opaque external dependency)

### Pascal Path (Windows only)
- `ffmpeg`, `ffprobe` (bundled or discovered)
- `mkvmerge` (optional, for mux mode)
- `MP4Box` (optional, for Apple M4V creator)
- **For builds**: FPC compiler + Lazarus IDE (for GUI)
- No HQ Converter counterpart

### Tool Discovery Priority (both C and Pascal implementations)
1. Executable-adjacent directory (next to binary)
2. Environment variables: `FFMPEG_BIN`, `FFPROBE_BIN`, `MKVMERGE_BIN`, `MP4BOX_BIN`
3. System `PATH`

**macOS specifics (C only):**
- Checks MacPorts paths first: `/opt/local/bin/ffmpeg8` → `/opt/local/bin/ffmpeg`
- Falls back to bundled or system PATH

## 6. Known Boundaries

- There is no Windows C implementation (CLI or GUI) — Windows is Pascal-only.
- C CLI supports `--dry-run` and `--version`.
- Linux C and macOS C GUI paths consume the structured selection catalog,
  including the feature-gated `hq_converter` group.
- The converter ABI and command builders receive resolved legacy codec/preset
  values; GUI code does not assemble FFmpeg arguments.
- Phase 4 (structured filter catalog) has not started; the GUI Filter/Filter
  Preset controls remain compatibility controls mapped to the existing
  `deblock` ABI.
- HQ Converter is Linux/macOS-only and feature-gated at CMake configure time
  on the presence of a valid prepared release under
  `third_party/hq_converter/`; it is absent entirely from builds where that
  release is not present, and from the Windows Pascal build regardless.

## 7. Canonical References

- User-facing overview: `README.md`
- C changelog: `CHANGELOG.md`
- Pascal changelog: `fpc/CHANGELOG.md`
- Install guides: `docs/install-linux.md`, `docs/install-macos.md`,
  `docs/install-windows.md` (Windows guide now covers the Pascal build only)
- Apple M4V design/status: `docs/macos-native-apple-m4v-design.md`
- Preset catalog rules: `docs/preset-file-rules.md`
- HQ Converter integration spec, invariants, and review findings:
  `docs/v3.0-Phase3.1.md`
- HQ Converter packaging/immutability policy:
  `docs/hq-converter-integration-policy.md`
- Unified selection catalog and execution rules: `docs/preset-file-rules.md`
- Catalog template: `presets.json.in`; generated runtime catalog: `presets.json`

## 8. HQ Converter Integration (Phase 3.1)

The external **HQ Converter Library** is integrated as a feature-gated
`hq_converter` codec group, Linux and macOS C only (no Pascal/Windows
counterpart). Summary (full spec: `docs/v3.0-Phase3.1.md`):

- **Feature gate**: `HQ_CONVERTER_ENABLED` is computed by the root
  `CMakeLists.txt` from the presence of a valid prepared release under
  `third_party/hq_converter/` (binaries, orchestrator runner, preset/config
  files all present and parseable). If absent or invalid, the group and all
  `src/hq/` integration code are excluded from the build.
- **Architectural model**: `hq_converter` is *only* an automatic Add Track
  video source. HQ produces an external video track; `add_track_execute()`
  (`src/add_track/add_track.c`, isolated in Phase 3.0) then performs the
  exact same audio processing and muxing used by manual Add Track/mux mode —
  no HQ-specific audio or mux code exists.
- **Runtime catalog**: `src/hq/hq_catalog.c` reads the packaged HQ release's
  own preset/config files at runtime; encoder/preset lists are never
  hardcoded in `presets.json`/`presets.json.in`.
- **Process isolation**: each HQ job runs in a dedicated `fork()` + immediate
  `execve()`'d helper process, `hq_worker` (`src/hq/hq_worker_main.c`), with
  its own fresh HQ library handle created and destroyed per job. This design
  was hardened during the Phase 3.1 code review after a GUI-specific
  multi-file hang was root-caused to a `fork()`-in-multi-threaded-process
  malloc-arena deadlock risk; re-execing before any `malloc()`/`dlopen()`/
  `pthread_create()` in the child eliminates that risk (see
  `docs/v3.0-Phase3.1.md` Section 28 for the full root-cause analysis and
  verification).
- **Packaging**: the enabled `third_party/hq_converter/` tree is copied
  verbatim into `bin/hq_converter/` and into the AppImage/`.app` bundle; it is
  treated as monolithic and immutable and is never modified, rebuilt, or
  updated by the application at runtime (`docs/hq-converter-integration-policy.md`).
- **CLI/GUI parity**: multi-file queues, Stop/Cancel, `task_N` working
  directory lifecycle/cleanup, and ProRes HQ container handling are
  implemented consistently across the CLI, Linux GTK GUI, and macOS Cocoa
  GUI, verified by the Phase 3.1 review (see `CHANGELOG.md` and
  `docs/v3.0-Phase3.1.md` Section 28).
