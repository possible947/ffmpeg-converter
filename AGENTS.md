# AGENTS.md

High-signal guidance for AI agents in `ffmpeg-converter`. Prefer executable
sources (CMake/Makefile/scripts) over docs when they conflict.

## Two implementations — keep in sync

- **C/CMake** (`src/`) — primary engine for all platforms: macOS (Cocoa GUI + CLI), Linux (GTK4 GUI + CLI), Windows (CLI only, MSVC).
- **Free Pascal** (`fpc/`) — complete port, **Linux + Windows only**. macOS Pascal is discontinued since v2.4 and hard-blocked in `fpc/build/Makefile` (errors out on Darwin) — do not re-add it.

Both share the same CLI and conversion model. Cross-cutting concepts (Apple M4V pipeline, codec strings, audio modes) are implemented independently in each — change one, update the other.

## Build commands

C/CMake (out-of-source `build/`):

```bash
cmake -B build
cmake --build build --target linux_cli        # → build/bin/ffmpeg_converter
cmake --build build --target linux_gui        # needs libgtk-4-dev; gated by ENABLE_LINUX_GUI
cmake --build build --target macos_cli
cmake --build build --target macos_gui_native && cmake --install build   # .app bundle
cmake --build build --target windows_cli --config Release   # VS x64 prompt, or scripts/windows_build.ps1
```

Free Pascal (`fpc/build/Makefile`, default `FPCFLAGS=-Mobjfpc -Sh -O2`):

```bash
make -C fpc/build cli        # → fpc/bin/ffmpeg_converter
make -C fpc/build lib        # → fpc/converter/libconverter_pas.so
make -C fpc/build gui        # needs lazbuild; LCL via --ws=gtk3
make -C fpc/build appimage   # optional packaging
```

`scripts/linux_build.sh` is a convenience wrapper over the FPC Makefile (defaults to `--cli`; `--gui`, `--clean` flags). Windows FPC builds use `scripts/windows_build_fpc.ps1`/`.bat` (distinct from the MSVC `windows_build.ps1`).

### Hard prerequisites that abort the build

- **macOS**: static `ffmpeg` + `ffprobe` in `src/platform/macos/bin/` — CMake `FATAL_ERROR` if missing. Pass `-DCMAKE_PREFIX_PATH=/opt/local` so MacPorts `jansson` is found (CI uses this).
- **Windows**: `src/platform/windows/bin/` must contain `ffmpeg.exe`, `ffprobe.exe` + DLLs — `FATAL_ERROR` if missing; copied next to the `.exe`.
- **Linux**: missing bundled binaries only emit a CMake WARNING; runtime falls back to env vars / PATH.
- `jansson` is vendored under `third_party/` (added via `add_subdirectory`); don't assume a system jansson except macOS/MacPorts.

## Verification / tests

- **No C test suite, no `ctest`, no lint/typecheck target.** Verify C changes by building the relevant target and running `ffmpeg_converter --help`.
- Pascal entrypoint is `make -C fpc/build tests`, but **`fpc/test/` currently has no tracked sources** — the Makefile prints "No Pascal tests found… skipping" when empty. Check the directory exists before expecting tests to run.
- The regression script (`fpc/test/run_all_regression_and_capture.sh`, referenced in docs) likewise only exists when `fpc/test/` is populated.

## C architecture notes

- `src/converter/converter.h` is the public C ABI with **8 functions**: `converter_create`, `converter_destroy`, `converter_set_callbacks`, `converter_set_options`, `converter_process_files`, `converter_make_output_name`, `converter_stop`, `converter_error_string`. The Pascal export (`fpc/converter/converter_pas.lpr`) mirrors all 8 — keep in sync.
- `Converter` is opaque: always `converter_create()`/`converter_destroy()`; never touch `struct Converter` fields outside `converter.c`.
- All output surfaces via `ConverterCallbacks` (file begin/end, stage, progress encode/analysis, message, error, complete). CLI and GUI register separate callback structs.
- `ConvertOptions.codec` / `.audio_norm` / `.audio_output_mode` are fixed `char[32]` — use `snprintf`/`strncpy`, never `strcpy`.
- In `src/CMakeLists.txt`, `core`, `utils`, `progress`, `audio`, `video`, `ffmpeg_cmd` are **INTERFACE (header-only)** libs. Don't add `.c` files there; put implementation in `converter/`, `mux/`, `m4v/`, or `platform/`.
- Platform code is split by OS dir `src/platform/{linux,macos,windows}/`. Guard with `CMAKE_SYSTEM_NAME` / `#if defined(_WIN32)` / `#if defined(__APPLE__)`. Never put Linux code in macos/windows subdirs.
- **No Windows C GUI** — Windows is CLI only; the Windows GUI lives in the Pascal port.

## Pascal notes

- On Windows/MSYS2, bash mangles `-Fu` paths; the Makefile uses `cygpath -w` + double-quoted args — follow that pattern for any new `-Fu`.
- Compiled units → `fpc/build/.units/`, binaries → `fpc/bin/`. Never commit these.

## Tool discovery (both implementations)

Order: executable-adjacent dir → env vars (`FFMPEG_BIN`, `FFPROBE_BIN`, `MKVMERGE_BIN`, `MP4BOX_BIN`, legacy `MKVMERGE`) → system `PATH`. macOS C also probes `/opt/local/bin/ffmpeg8`, `/opt/local/bin/ffmpeg`, `/opt/homebrew/bin`. `mux` mode is silently disabled when `mkvmerge` is absent.

## Apple M4V pipeline

Implemented separately in C (`src/m4v/`, `src/gui_macos_native/apple_m4v_creator.m`) and Pascal (`fpc/converter/apple_m4v_creator.pas`). Order: (1) stream-copy video to temp `.mp4`, (2) AAC via `libfdk_aac -b:a 320k` CBR — **fixed, not configurable** (`aac_quality` removed in v2.5), (3) AC3 (384/448/640), (4) `MP4Box` mux, (5) optional `ffmpeg -map_chapters 1 -c copy`.

## CI / releases

`.github/workflows/` is **tag-driven**: pushing tag `macos` → macOS build/release; tag `windows` → MSVC build/release. Both also support `workflow_dispatch`. Each builds only `macos_cli` / `windows_cli` and creates a GitHub Release with an auto-generated changelog.

## Gotchas

- Codec strings vary by platform (`*_vaapi` Linux, `*_videotoolbox` macOS, `prores_ks_vulkan` Windows) and are runtime-probed — check `README.md` Features and `converter.h` comments rather than assuming a fixed list.
- `-o/--output` sets an output **directory**, not a filename; default is `$HOME/ffmpeg_converter` (created if missing).
- `AGENTS.md` itself is gitignored; the committed sibling instruction file is `.github/copilot-instructions.md`.
