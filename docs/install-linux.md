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

### 1.2 Build targets
From repository root:
```bash
mkdir -p build
cd build
cmake ..
cmake --build . --target linux_cli
cmake --build . --target linux_gui
```

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
- Ensure `ffmpeg` and `ffprobe` are available in `PATH`.
- `h265_mi50` uses VAAPI by default (`/dev/dri/renderD128`).

## 4. Validation
```bash
make -C fpc/build all
nm -D fpc/converter/libconverter_pas.so | grep -E 'converter_(create|destroy|set_callbacks|set_options|process_files|stop|error_string)'
```
