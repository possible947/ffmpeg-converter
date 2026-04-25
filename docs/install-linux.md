# Linux Install and Build

This document covers Linux install/build for both project paths:
- C/CMake (`src/`)
- Free Pascal (`fpc/`)

## 1. C/CMake Path

### 1.1 Install dependencies
Debian/Ubuntu:
```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config ffmpeg libjansson-dev libgtk-4-dev
```

Fedora:
```bash
sudo dnf install -y gcc gcc-c++ make cmake pkgconf-pkg-config ffmpeg jansson-devel gtk4-devel
```

> **AV1 input decoding note:** The `ffmpeg` package from the default Debian/Ubuntu APT
> repositories may not include `libdav1d` support. To decode AV1 source files reliably,
> use an ffmpeg build that includes `--enable-libdav1d`. Options:
> - Ubuntu 22.04+: `sudo apt install ffmpeg` usually includes libdav1d.
> - Debian stable: consider [deb-multimedia.org](https://deb-multimedia.org/) or a
>   custom build.
> - Fedora: `ffmpeg` from RPM Fusion includes libdav1d.
> - Or place a custom-built `ffmpeg`/`ffprobe` in `src/platform/linux/bin/` — the
>   converter prefers the bundled binary over the system one.
> If libdav1d is unavailable at runtime, the converter falls back to the native `av1`
> decoder with `-hwaccel none` (may fail on systems with NVIDIA GPUs that lack AV1
> NVDEC support).

### 1.2 Build targets
From repository root:
```bash
mkdir -p build
cd build
cmake ..
cmake --build . --target linux_cli
cmake --build . --target linux_gui
```

Artifacts in the flat Linux layout:
- `build/bin/ffmpeg_converter`
- `build/bin/ffmpeg_converter_gui`
- `build/bin/ffmpeg`
- `build/bin/ffprobe`
- `build/bin/mkvmerge` when found in common Linux system locations
- `build/bin/MP4Box` when found in common Linux system locations

### 1.3 AppImage packaging (optional)
AppImage produces a single-file portable executable that bundles the GUI, ffmpeg/ffprobe, and required libraries.

```bash
# From the build directory (with ENABLE_APPIMAGE=ON)
cmake -DENABLE_APPIMAGE=ON ..
cmake --build . --target package_appimage
```

The AppImage is created at `src/gui/ffmpeg_converter_gui-x86_64.AppImage` (≈71 MB). Requires `appimagetool` in PATH.

Alternatively, run the packaging script directly:
```bash
cd src/gui
./package_appimage.sh ../build
```

The script:
- Copies the GUI binary and bundled tools (ffmpeg, ffprobe, mkvmerge, MP4Box) into an AppDir
- Resolves shared library dependencies via `ldd`, excluding system libraries (`/lib`, `/usr/lib` paths)
- Generates `AppRun` wrapper that sets `PATH` and `LD_LIBRARY_PATH`
- Creates a desktop entry with icon
- Invokes `appimagetool` to produce the final `.AppImage`

## 2. Free Pascal Path

### 2.1 Install dependencies
```bash
sudo apt install -y fpc lazarus
```

### 2.2 Build targets
From repository root:
```bash
make -C fpc/build cli
make -C fpc/build lib
make -C fpc/build tests
```

Artifacts:
- `fpc/cli/ffmpeg_converter`
- `fpc/converter/libconverter_pas.so`

## 3. Runtime Notes
- Linux C path first prefers `ffmpeg` and `ffprobe` located in the same directory as
	the executable. The Linux build now stages them together in `build/bin`.
- During development, runtime still falls back to `src/platform/linux/bin` if needed.
- If bundled tools are unavailable, runtime falls back to `FFMPEG`/`FFMPEG_BIN`,
	`FFPROBE`/`FFPROBE_BIN`, and then `PATH`.
- Linux mux mode also checks `MKVMERGE_BIN`, then executable-adjacent `mkvmerge`,
	and then `PATH`.
- Linux GTK Apple M4V creator checks `MP4BOX_BIN`, then executable-adjacent `MP4Box`,
	and then `PATH`.
- Linux hardware codec exposure is runtime-detected. Current C path exposes
	`h264_vaapi` and `hevc_vaapi` only when the active system/driver actually supports them.
- AV1 input decoding is runtime-detected. The converter probes `ffmpeg -decoders` at
	startup and selects `av1_qsv` (when Intel QSV encoders are present), then `libdav1d`
	(when available), then falls back to the native `av1` decoder with `-hwaccel none`.
	Requires ffmpeg compiled with `--enable-libdav1d` for the software-decoder path.
- C CLI inputs are positional (`ffmpeg_converter [options] file1 file2 ...`).
- `-o/--output` sets output directory; if omitted, default is
	`$HOME/ffmpeg_converter` (created automatically).
- Linux `codec=mux` is a one-source-file session. It requires
	`--video-track <file>` and always writes final `.mkv` via `mkvmerge`.
- Linux GTK also provides a separate Apple M4V creator button.
- Linux Apple M4V creator is GUI-only and follows the macOS direct M4V steps
	through `ffmpeg`, `ffprobe`, and `MP4Box`.
- Linux Apple M4V creator input preflight currently allows only `h264`, `hevc`,
	or `prores` video streams.
- To run the toolset from another directory on the same Linux machine, copy or symlink
	the staged files from `build/bin` into one folder such as `~/.local/bin`.
- `MP4Box` is staged as a single binary only. No Linux shared-library bundle is created,
	so it works where the target system already has compatible GPAC/runtime libraries.

## 4. Validation
```bash
make -C fpc/build all
nm -D fpc/converter/libconverter_pas.so | grep -E 'converter_(create|destroy|set_callbacks|set_options|process_files|stop|error_string)'
```
