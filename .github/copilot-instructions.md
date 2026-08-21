# Copilot Instructions

## Build Commands

### C/CMake — Linux
```bash
mkdir build && cd build && cmake ..
cmake --build . --target linux_cli
cmake --build . --target linux_gui
```

### C/CMake — macOS
```bash
mkdir build && cd build && cmake -DCMAKE_PREFIX_PATH=/opt/local ..
cmake --build . --target macos_cli
cmake --build . --target macos_gui_native
cmake --install .   # produces build/install/ffmpeg_converter_gui_macos.app
```

### C/CMake — Windows (MSVC, from x64 Native Tools Command Prompt)
```powershell
.\scripts\windows_build.ps1          # incremental
.\scripts\windows_build.ps1 -Clean   # clean rebuild
cmake --build build-msvc --target windows_cli --config Release
```

### Free Pascal — Linux
```bash
make -C fpc/build cli          # → fpc/bin/ffmpeg_converter
make -C fpc/build lib          # → fpc/converter/libconverter_pas.so
make -C fpc/build gui-app      # → fpc/bin/ffmpeg_converter_gui (Lazarus required)
make -C fpc/build tests        # all unit tests
```

### Running a single Pascal test
```bash
make -C fpc/build tests TEST_PROGRAMS="test_cmd_builder"
./fpc/test/test_cmd_builder
```

### Pascal integration tests
```bash
bash fpc/test/test_cli_args_matrix.sh
./fpc/test/run_all_regression_and_capture.sh   # full regression; writes report to /tmp/ffc_regression_<timestamp>/
```

## Architecture

Two **independent, feature-matched** implementations share the same CLI interface and conversion model:

- **C/CMake** (`src/`) — primary engine for all platforms: macOS (Cocoa GUI + CLI), Linux (GTK4 GUI + CLI), Windows (CLI only, MSVC).
- **Free Pascal** (`fpc/`) — complete port for Linux and Windows only; macOS Pascal support is **discontinued** since v2.4.

### C Module Layout (`src/`)
- `converter/converter.c` + `converter.h` — core public API (`Converter*` opaque object, `ConvertOptions`, `ConverterCallbacks`, `ConverterError` enum).
- `cli/{linux,macos,windows}/main.c` — platform CLI entry points.
- `gui/` — Linux GTK4 GUI.
- `gui_macos_native/` — macOS Cocoa/AppKit GUI (`.m` files, ObjC).
- `m4v/` — Apple M4V creator pipeline (shared backend).
- `mux/` — MKV mux mode via `mkvmerge`.
- `platform/{linux,macos,windows}/` — platform-specific runtime probing (VAAPI, VideoToolbox, GPU).
- `platform/runtime_probe_common.{c,h}` — shared probe logic.
- Modules `core`, `utils`, `progress`, `audio`, `video`, `ffmpeg_cmd` are **INTERFACE** (header-only) CMake libraries — they expose include paths but contain no `.c` sources themselves.

### Pascal Module Layout (`fpc/`)
- `converter/converter_core.pas` — engine; `converter_pas.lpr` exports C ABI shared library.
- `cli/ffmpeg_converter.lpr` (Linux) / `ffmpeg_converter_windows.lpr` (Windows) — CLI.
- `gui/form.pas` — Lazarus/LCL GUI.
- `converter/apple_m4v_creator.pas` — M4V pipeline.
- `common/` — reusable FS, path, process, time helpers.
- `json/loudnorm_json.pas` — loudnorm JSON parsing (uses `fpjson`/`jsonparser`; C uses `jansson`).
- `platform/` — platform-specific units.

### Tool Discovery (both implementations)
1. Executable-adjacent directory
2. Env vars: `FFMPEG_BIN`, `FFPROBE_BIN`, `MKVMERGE_BIN`, `MP4BOX_BIN`  (also `MKVMERGE`)
3. System `PATH`
- macOS C additionally checks `/opt/local/bin/ffmpeg8`, `/opt/local/bin/ffmpeg`, `/opt/homebrew/bin`.

## Key Conventions

### C Implementation
- The converter is an opaque object — always use `converter_create()` / `converter_destroy()`. Never access `struct Converter` fields directly from outside `converter.c`.
- All results surface through `ConverterCallbacks` (on_file_begin, on_file_end, on_stage, on_progress_encode, on_progress_analysis, on_message, on_error, on_complete). GUI and CLI each register their own callback struct.
- `ConvertOptions.codec` and `ConvertOptions.audio_norm` are fixed-length `char[32]` string fields — use `snprintf`/`strncpy`, not `strcpy`.
- Platform guards use `CMAKE_SYSTEM_NAME` / `#if defined(_WIN32)` / `#if defined(__APPLE__)` — never add Linux-specific code to `macos` or `windows` platform subdirs.
- `src/CMakeLists.txt` declares `core`, `utils`, etc. as `INTERFACE` libraries (header-only). Don't add `.c` files there — put implementation into the correct module directory.
- Windows C GUI is **not implemented**; Windows uses the CLI binary only.

### Free Pascal Implementation
- Pascal C ABI export (`converter_pas.lpr`) exports exactly 7 symbols matching `converter.h`: `converter_create`, `converter_destroy`, `converter_set_callbacks`, `converter_set_options`, `converter_process_files`, `converter_stop`, `converter_error_string`. Keep these in sync with the C header.
- FPC compiler flags: `-Mobjfpc -Sh -O2`. Always compile with `-Cg` (PIC) when building the shared library.
- Build output goes to `fpc/bin/` (binaries) and `fpc/build/.units/` (compiled units) — never commit these.
- On Windows, MSYS2 bash path mangling for FPC requires double-quoting `-Fu` arguments (see `fpc/build/Makefile` for the pattern).
- Pascal macOS builds are explicitly blocked in the Makefile with an error message — do not attempt to add them back.

### Apple M4V Pipeline
Implemented independently in both C (`src/m4v/`, `src/gui_macos_native/apple_m4v_creator.m`) and Pascal (`fpc/converter/apple_m4v_creator.pas`). Both must stay in sync. Pipeline order:
1. Extract video to temp `.mp4` (stream copy)
2. Encode AAC with `libfdk_aac -b:a 320k` (CBR, fixed — not user-configurable)
3. Encode AC3 (configurable: 384/448/640 kbps)
4. Mux via `MP4Box`
5. Optional chapter import via `ffmpeg -map_chapters 1 -c copy`

### Audio Output Modes
String values used in `ConvertOptions.audio_output_mode` (C) and equivalent Pascal fields:
- `"pcm"` — PCM audio passthrough
- `"fdk_aac_320"` — CBR 320k AAC via `libfdk_aac`
- `"fdk_aac_320_ac3_640"` — AAC + AC3 dual-track

### Codec String Values
`ConvertOptions.codec` accepted values: `copy`, `prores`, `prores_ks`, `prores_videotoolbox` (macOS), `hevc_videotoolbox` (macOS), `h264_vaapi` (Linux), `hevc_vaapi` (Linux), `prores_ks_vulkan` (Windows GPU).

### Bundled Binaries
- **Windows**: place `ffmpeg.exe`, `ffprobe.exe`, and all DLLs in `src/platform/windows/bin/` before building — the CMake target copies them next to the `.exe`.
- **Linux**: stage binaries in `src/platform/linux/bin/`; build copies them to `build/bin/`.
- **macOS**: place in `src/platform/macos/bin/`; bundled inside `.app` at install time.

### CI / Releases
- Workflows in `.github/workflows/` trigger on tags: `macos` tag → macOS build, `windows` tag → Windows MSVC build.
- Both workflows upload artifacts and create GitHub Releases when triggered by a tag push.

## Verification & Testing

### C Implementation
- **No C test suite or `ctest`**. Verify C changes by building the relevant target and running `ffmpeg_converter --help`.
- No lint or typecheck target in the build system.

### Free Pascal Implementation
- Unit tests entry point: `make -C fpc/build tests`
- Run a single test: `make -C fpc/build tests TEST_PROGRAMS="test_cmd_builder"`
- Integration tests: `bash fpc/test/test_cli_args_matrix.sh`
- Full regression suite: `./fpc/test/run_all_regression_and_capture.sh` (writes report to `/tmp/ffc_regression_<timestamp>/`)

### Build Prerequisites (Hard Blocks)
- **macOS**: static `ffmpeg` + `ffprobe` in `src/platform/macos/bin/` — CMake `FATAL_ERROR` if missing. Pass `-DCMAKE_PREFIX_PATH=/opt/local` so MacPorts `jansson` is found.
- **Windows**: `src/platform/windows/bin/` must contain `ffmpeg.exe`, `ffprobe.exe` + DLLs — `FATAL_ERROR` if missing; copied next to the `.exe`.
- **Linux**: missing bundled binaries only emit a CMake WARNING; runtime falls back to env vars / PATH.
- `jansson` is vendored under `third_party/`; don't assume a system jansson except macOS/MacPorts.

## Important Edge Cases

- Codec strings vary by platform (`*_vaapi` Linux, `*_videotoolbox` macOS, `prores_ks_vulkan` Windows) and are runtime-probed — check `README.md` Features and `converter.h` comments.
- `-o/--output` sets an output **directory**, not a filename; default is `$HOME/ffmpeg_converter` (created if missing).
- `mkvmerge` must be present for mux mode; it is silently disabled if absent.
- Apple M4V pipeline's AAC encoding is **fixed at CBR 320k** via `libfdk_aac` — not user-configurable.
- Changes to the Apple M4V pipeline must be made independently in C (`src/m4v/`) and Pascal (`fpc/converter/apple_m4v_creator.pas`) to keep both implementations in sync.
- When changing the public C ABI (`src/converter/converter.h`), the Pascal export (`fpc/converter/converter_pas.lpr`) must mirror all function signatures exactly.
- Both C and Pascal converters use the same converter model and CLI interface — breaking changes must be coordinated across both implementations.
