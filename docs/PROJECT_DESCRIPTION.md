# ffmpeg_converter — Developer Description (Version 3.0b)

This document describes the current, factual architecture of the repository at
the end of the V3 preparation stage. It is aligned with the executable sources,
CMake files, Makefiles, and validated Linux behavior.

## Version 3.0b Status

V3 preparation for Phase 3 and Phase 4 is complete. The Linux C application has
been functionally tested and accepted for use. The Linux Pascal application is
fully functional for the available runtime components and is maintained as a
parallel development/debug baseline for Windows. macOS and Windows source
paths and GUI prototypes are prepared, but native build, runtime, and hardware
validation remain open.

## V3 Preparation Status

- The single generated `presets.json` is the source of truth for GUI/CLI
  selection metadata and execution presets.
- Linux C and Linux Pascal GUI paths use the shared semantic model:
  **Group -> Encoder -> Preset**.
- Runtime probes filter hardware groups/encoders after static catalog loading.
- Linux C and Pascal GUI prototypes share the approved three-column layout:
  Video, Audio, and Filters. The visual reference is
  `docs/maket_example.png`.
- Linux C has been tested across the available codec, audio, device, mux, M4V,
  theme, and repeated-selection combinations and is accepted for use.
- Linux Pascal has been tested for the available FFmpeg and LCL components and
  is ready as a Windows development/debug baseline. Its Linux GUI remains a
  development target with known interface polish limitations.
- Native macOS and Windows validation remains required. C AppImage packaging
  is configured and built; Pascal AppImage packaging is not required.

## Preset and Selection Architecture

### One generated catalog

The repository stores the catalog template in `presets.json.in`. During CMake
configuration, CMake writes the platform-specific generated catalog to
`build/generated/presets.json`. The same generated file is copied beside C
CLI/GUI binaries and into the Linux AppImage. Direct Linux Pascal development
builds locate `presets.json` beside the executable, through `PRESETS_PATH`, or
via the repository-development fallback path.

The catalog has two distinct layers in one JSON document:

1. `selection` describes user-facing groups, encoders, static enablement,
   capability requirements, bit-depth variants, and references to execution
   presets.
2. `linux`, `macos`, and `windows` contain execution preset records: FFmpeg
   arguments, containers, pixel formats, input arguments, filters, pipelines,
   and runtime requirements.

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

Pascal Linux is a direct Lazarus development build and does not require an
AppImage package. Windows Pascal packaging remains a native Windows concern.

## 1. Project Scope

ffmpeg_converter is a cross-platform media conversion project with two
independent implementations:

- **C/CMake** in `src/` (primary on all platforms)
- **Free Pascal** in `fpc/` (Windows release target; Linux development/debug target)

Both paths provide conversion workflows around external `ffmpeg`/`ffprobe`.

## 2. Implementations (v3.0b)

### 2.1 C/CMake (`src/`)

**Platform coverage:**
- **macOS**: CLI + native Cocoa GUI (sole macOS implementation)
- **Linux**: CLI + GTK4 GUI (C release path; fully tested)
- **Windows**: CLI (most complete version, MSVC build only)

**Key modules:**
- Core engine: `src/converter/converter.c`, `src/converter/converter.h`
- CLI entry points: `src/cli/linux/main.c`, `src/cli/macos/main.c`, `src/cli/windows/main.c`
- Linux GUI (GTK4): `src/gui/`
- macOS native GUI (Cocoa/AppKit): `src/gui_macos_native/`
- Mux module: `src/mux/`
- Apple M4V module: `src/m4v/`

**Codecs:**
- Cross-platform: `copy`, `prores`, `prores_ks`
- Linux (VAAPI, runtime-detected): `h264_vaapi`, `hevc_vaapi`
- macOS (VideoToolbox): `prores_videotoolbox`, `hevc_videotoolbox`
- Windows (GPU runtime-detected): NVIDIA NVENC, AMD AMF, Intel QSV, Vulkan ProRes
- All platforms: AV1 input decoding (auto-selected decoder)

**Audio normalization:**
- `none`, `peak`, `peak_2pass`, `loudness`, `loudness_2pass`
- 2-pass uses `-filter_threads N` for parallel processing

**Special workflows:**
- Mux mode: one source file + replacement video track → `.mkv` via `mkvmerge`
- Apple M4V creator: multi-step pipeline (video copy → AAC → AC3 → MP4Box mux → optional chapters)

### 2.2 Free Pascal (`fpc/`) — Windows Release / Linux Development

**Platform coverage:**
- **Windows**: CLI + Lazarus/LCL GUI with Vulkan GPU support (native release target)
- **Linux**: Lazarus/LCL GUI and converter path for development/debug and
   preliminary Windows validation; GTK3 and Qt6 builds are validated on Fedora 44
- **macOS**: unsupported Pascal target; use the C/Cocoa implementation

**Key modules:**
- Engine: `fpc/converter/converter_core.pas`
- CLI: `fpc/cli/ffmpeg_converter_windows.lpr`
- GUI: `fpc/gui/form.pas` (Lazarus/LCL)
- Apple M4V: `fpc/converter/apple_m4v_creator.pas`
- JSON parsing: `fpc/json/loudnorm_json.pas`
- Tests: `fpc/test/test_*.pas`

**Codec support:**
- Feature-parity with the C CLI on Windows
- Runtime probing for Vulkan device selection in the Windows GUI

**C ABI export (for library usage):**
- `fpc/converter/converter_pas.lpr` exports C-compatible shared library
- 8 exported functions: `converter_create`, `converter_destroy`, `converter_set_callbacks`,
  `converter_set_options`, `converter_process_files`, `converter_make_output_name`,
  `converter_stop`, `converter_error_string`

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

## 4. Build Targets (v2.5)

### 4.1 C/CMake targets

**Linux:**
- `linux_cli` — CLI binary
- `linux_gui` — GTK4 GUI binary
- `package_appimage` — AppImage (optional, requires `ENABLE_APPIMAGE=ON`)

**macOS (C only):**
- `macos_cli` — CLI binary
- `macos_gui_native` — native Cocoa GUI
- `MACOS_BUNDLE_INFO_PLIST` — Info.plist for `.app` bundle

**Windows (MSVC):**
- `windows_cli` — CLI binary (most complete)
- Use `./scripts/windows_build.ps1` or manual CMake

**CMake feature switches:**
- `ENABLE_LINUX_GUI` (default ON on Linux)
- `ENABLE_MACOS_NATIVE_GUI` (default ON on macOS)

### 4.2 Pascal/Make targets

In `fpc/build/Makefile`:
- `cli` — CLI binary
- `lib` — shared library (C ABI export)
- `tests` — all unit tests

**Platform-specific:**
- Windows: produces `fpc/cli/ffmpeg_converter_windows.exe`

## 5. Runtime Dependencies

### C Path
- `ffmpeg`, `ffprobe` (bundled on all platforms, or discovered via env/PATH)
- `jansson` (system library for loudnorm JSON parsing)
- `mkvmerge` (optional, for mux mode)
- `MP4Box` (optional, for Apple M4V creator)
- **Linux GUI**: GTK4
- **macOS GUI**: AppKit (native framework, no GTK)

### Pascal Path
- `ffmpeg`, `ffprobe` (bundled or discovered)
- `mkvmerge` (optional, for mux mode)
- `MP4Box` (optional, for Apple M4V creator)
- **For builds**: FPC compiler + Lazarus IDE (for GUI)

### Tool Discovery Priority (all platforms, C and Pascal)
1. Executable-adjacent directory (next to binary)
2. Environment variables: `FFMPEG_BIN`, `FFPROBE_BIN`, `MKVMERGE_BIN`, `MP4BOX_BIN`
3. System `PATH`

**macOS specifics (C only):**
- Checks MacPorts paths first: `/opt/local/bin/ffmpeg8` → `/opt/local/bin/ffmpeg`
- Falls back to bundled or system PATH

## 6. Known Boundaries

- Windows C GUI is not implemented.
- C CLI supports `--dry-run` and `--version`.
- Linux C and Pascal GUI paths consume the structured selection catalog. Native
  macOS and Windows builds remain the outstanding validation boundary.
- The converter ABI and command builders receive resolved legacy codec/preset
  values; GUI code does not assemble FFmpeg arguments.
- Phase 4 filter-catalog implementation is not part of this preparation stage;
  the GUI Filter/Filter Preset controls remain compatibility controls mapped to
  the existing `deblock` ABI.

## 7. Canonical References

- User-facing overview: `README.md`
- C changelog: `CHANGELOG.md`
- Pascal changelog: `fpc/CHANGELOG.md`
- Install guides: `docs/install-linux.md`, `docs/install-macos.md`,
  `docs/install-windows.md`
- Apple M4V design/status: `docs/macos-native-apple-m4v-design.md`
- Preset catalog rules: `docs/preset-file-rules.md`
- Unified selection catalog and execution rules: `docs/preset-file-rules.md`
- Catalog template: `presets.json.in`; generated runtime catalog: `presets.json`
