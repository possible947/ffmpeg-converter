# macOS Install and Build

This document covers macOS install/build for both project paths:
- C/CMake (`src/`)
- Free Pascal (`fpc/`)

## 1. C/CMake Path

### 1.1 Install dependencies
Homebrew:
```bash
brew install cmake ffmpeg jansson gtk4 pkg-config
```

MacPorts:
```bash
sudo port install cmake ffmpeg jansson gtk4 +quartz -x11 pkgconfig
```

### 1.2 Build targets
From repository root:
```bash
mkdir -p build
cd build
cmake ..
cmake --build . --target macos_cli
cmake --build . --target macos_gui
```

## 2. Free Pascal Path

### 2.1 Install dependencies
Install Lazarus + FPC for macOS (package manager or official installer).

### 2.2 Build targets
From repository root:
```bash
make -C fpc/build cli
make -C fpc/build lib
make -C fpc/build tests
```

Note:
- macOS shared libraries commonly use `.dylib` extension.
- The recreated `fpc/build/Makefile` emits `.dylib` on macOS.

## 3. Runtime Notes
- Ensure `ffmpeg` and `ffprobe` are installed. The Pascal path resolves binaries
	via `PATH` and also checks common macOS locations:
	- `/opt/homebrew/bin`
	- `/usr/local/bin`
	- `/usr/bin`
- GUI builds require GTK4 properly installed for the selected package manager.
- CLI `-o/--output` creates missing output directories and reports a clear
	error when the path is invalid or not writable.

## 4. Validation
```bash
make -C fpc/build all
```

Full regression (including Pascal/C parity checks):

```bash
./fpc/test/run_all_regression_and_capture.sh
```
