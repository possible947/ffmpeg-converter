# Copilot instructions for ffmpeg-converter

Prefer the executable sources in this repo (CMake, Makefiles, scripts, and platform code) over prose docs when they disagree. This project intentionally keeps two feature-matched implementations in sync: a C/CMake engine in `src/` and a Free Pascal port in `fpc/`.

## ⚠️ Critical Prerequisites (read before building)

- **FFmpeg version 8.1** (not system FFmpeg) compiled with `--enable-libfdk_aac` and `--enable-soxr` is **mandatory**
  - macOS: must be in `src/platform/macos/bin/ffmpeg` and `src/platform/macos/bin/ffprobe` → build fails with `FATAL_ERROR` if missing
  - Windows: must be in `src/platform/windows/bin/` with all runtime DLLs → build fails with `FATAL_ERROR` if missing
  - Linux: optional for build, but binary won't work at runtime without it
- **Windows GUI is Pascal-only** (`fpc/gui/`) — there is NO Windows C GUI
- **macOS Pascal is discontinued** (v2.4+) — use C/Cocoa GUI only; FPC Makefile hard-blocks Darwin with exit error
- `presets.json` is copied next to each binary automatically by the build system

## Build, test, and validation commands

### C/CMake

Linux:
```bash
cmake -B build
cmake --build build --target linux_cli
cmake --build build --target linux_gui
```

macOS:
```bash
cmake -B build -DCMAKE_PREFIX_PATH=/opt/local
cmake --build build --target macos_cli
cmake --build build --target macos_gui_native
cmake --install build   # produces build/install/ffmpeg_converter_gui_macos.app
```

Windows (MSVC, from x64 Native Tools Command Prompt):
```powershell
.\scripts\windows_build.ps1
.\scripts\windows_build.ps1 -Clean
cmake --build build-msvc --target windows_cli --config Release
```

C validation: there is no C test suite or `ctest` target. The usual validation is to build the relevant target and then run the built binary with `--help` (for example `./build/bin/ffmpeg_converter --help`).

### Free Pascal

CLI/library build:
```bash
make -C fpc/build cli
make -C fpc/build lib
```

GUI build:
```bash
make -C fpc/build gui
```

Test entry point:
```bash
make -C fpc/build tests
```

Single test target:
```bash
make -C fpc/build tests TEST_PROGRAMS="test_cmd_builder"
./fpc/test/test_cmd_builder
```

Integration/regression scripts, when present:
```bash
bash fpc/test/test_cli_args_matrix.sh
./fpc/test/run_all_regression_and_capture.sh
```

There is no dedicated lint target in the repo; validation is build-oriented and script-driven.

## Architecture at a glance

This repo has two independent implementations that share the same conversion model and CLI behavior:

- `src/` is the primary C/CMake implementation. It contains the converter core, platform-specific code, CLI entry points, and native macOS/Linux GUIs.
- `fpc/` is the Free Pascal port; it mirrors the C implementation for Linux and Windows and is deliberately kept feature-matched with the C version.

Key structure:

- `src/converter/` contains the public C ABI (`converter.h` / `converter.c`) and the conversion engine. Treat `Converter` as opaque.
- `src/cli/{linux,macos,windows}/` contains the CLI entry points.
- `src/gui/` and `src/gui_macos_native/` contain the native GUI code.
- `src/m4v/` and `fpc/converter/apple_m4v_creator.pas` contain the Apple M4V pipeline; both must be kept in sync.
- `src/platform/{linux,macos,windows}/` and `fpc/platform/` contain runtime probing and OS-specific logic.
- `presets.json` is the runtime source of truth for codec/preset definitions used by both implementations; doc updates live in `docs/`.

Important repo-level behavior:

- Tool discovery order is executable-adjacent dir, environment variables (`FFMPEG_BIN`, `FFPROBE_BIN`, `MKVMERGE_BIN`, `MP4BOX_BIN`, legacy `MKVMERGE`), then `PATH`.
- Runtime probing chooses supported codecs and GPU encoders dynamically; codec strings are platform-specific and must be validated against the runtime, not hardcoded from memory.
- `-o/--output` sets an output directory, not a filename; the default output directory is `$HOME/ffmpeg_converter`.

## Key conventions specific to this repo

- Keep the C and Pascal implementations aligned when changing public interfaces or data-driven codec logic. In particular, any change to `src/converter/converter.h` should be mirrored in `fpc/converter/converter_pas.lpr`.
- `ConvertOptions.*` strings such as `codec`, `audio_norm`, and `audio_output_mode` are fixed-length `char[32]` arrays; use `snprintf`/`strncpy`, not `strcpy`.
- The converter object is opaque: create/destroy via `converter_create()` / `converter_destroy()` and do not reach into `struct Converter` from outside `converter.c`.
- `src/CMakeLists.txt` treats `core`, `utils`, `progress`, `audio`, `video`, and `ffmpeg_cmd` as interface libraries. Put implementations into the correct module directory rather than adding `.c` files there.
- OS-specific code belongs in the matching platform directory; do not add Linux-only code into macOS or Windows platform subdirs, or vice versa.
- `mkvmerge` is optional; mux mode is silently disabled when it is absent.
- Apple M4V pipeline details are intentionally duplicated in C and Pascal and must stay synchronized: stream-copy video, fixed AAC CBR 320k via `libfdk_aac`, AC3 track, `MP4Box` mux, optional chapter import.
- The repo’s executable sources are the source of truth. If README text and scripts disagree, prefer the actual build or runtime behavior in CMake/Makefile/scripts.

## Repo docs worth checking before major changes

- `README.md` for the user-facing feature overview and build requirements
- `AGENTS.md` for repo-specific AI guidance and gotchas
- `docs/` and `presets.json` for codec/preset behavior introduced in the v3.0 phases
- platform build scripts under `scripts/` for Windows/Linux packaging and build flows

## Notes for future Copilot work

- Prefer small, targeted changes that honor the dual C/Pascal architecture.
- When editing codec/preset behavior, update the matching runtime presets and the encoding tables used by the C and Pascal command builders together.
- Validate the changed behavior with the smallest build or target-level check available.

## Troubleshooting Common Agent Errors

| Problem | Likely Cause | Solution |
|---------|--------------|----------|
| macOS CMake fails: "src/platform/macos/bin/ffmpeg not found" | FFmpeg not bundled | Place static FFmpeg 8.1 binary in `src/platform/macos/bin/` |
| Windows CMake fails: "src/platform/windows/bin not found" | FFmpeg not bundled | Place `ffmpeg.exe`, `ffprobe.exe` + all DLLs in `src/platform/windows/bin/` |
| Linux CLI builds but fails at runtime: "ffmpeg not found" | No bundled binary, missing env var | Set `FFMPEG_BIN=/path/to/ffmpeg` or place binary next to `ffmpeg_converter` |
| Windows: GUI doesn't exist | Tried to build C GUI on Windows | Windows GUI is Pascal-only (`fpc/gui/`). Use `fpc/` build or use macOS C GUI + CLI. |
| macOS: Error when building FPC | Pascal support discontinued | Use C implementation (`cmake --build build --target macos_cli`). FPC Makefile exits with error on Darwin. |
| Both C & Pascal sync issue | Changed `converter.h` but not Pascal export | Any change to `src/converter/converter.h` → mirror in `fpc/converter/converter_pas.lpr` (8 exported functions) |
| M4V pipeline broken after edit | C and Pascal M4V diverged | Keep `src/m4v/` + `src/gui_macos_native/apple_m4v_creator.m` synchronized with `fpc/converter/apple_m4v_creator.pas` |
