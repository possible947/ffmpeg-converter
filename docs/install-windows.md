# Windows Install and Build

This document covers Windows install/build for both project paths:
- C/CMake (`src/`) using MSYS2/MinGW
- Free Pascal (`fpc/`)

## 1. C/CMake Path (MSYS2/MinGW, recommended)

Install MSYS2, then open `MSYS2 MinGW x64` shell.

### 1.1 Install dependencies
```bash
pacman -Syu
pacman -S --needed mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake mingw-w64-x86_64-pkgconf mingw-w64-x86_64-ffmpeg mingw-w64-x86_64-jansson
```

### 1.2 Build target
From repository root in MinGW shell:
```bash
mkdir -p build
cd build
cmake -G "MSYS Makefiles" -DCMAKE_PREFIX_PATH=/mingw64 ..
cmake --build . --target windows_cli
```

## 2. Free Pascal Path

### 2.1 Install dependencies
Install FPC. Install Lazarus too if GUI work is needed.

### 2.2 Build targets
From repository root:
```bash
make -C fpc/build cli
make -C fpc/build tests
```

Notes:
- `fpc/build/Makefile` includes a Windows shared-library output path (`fpc/converter/converter_pas.dll`).

## 3. GUI Notes
If Lazarus GUI build reports missing `Interfaces` or `Forms`, install full Lazarus/LCL widgetset packages for the active compiler/toolchain.

## 4. CI/Release Notes
See `WINDOWS_BRANCH.md` for Windows release/tag workflow details in GitHub Actions.
