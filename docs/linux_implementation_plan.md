# Linux Implementation Plan for Pascal Codebase

## Overview
This document outlines the plan to improve the Linux implementation of the Pascal codebase to achieve feature parity with the Windows version. The goal is to prepare the Linux version for testing and use.

## Comparison Report: Linux vs. Windows Pascal Implementation

### 1. **CLI Entry Points**
- **Windows**: Includes UTF-8 console handling, Apple M4V support, and Windows-specific mux postprocessing.
- **Linux**: Lacks UTF-8 console handling and Apple M4V support. Only includes basic mux postprocessing.

### 2. **Hardware Probing**
- **Windows**: Provides comprehensive hardware detection for NVENC, AMF, QSV, Vulkan, and mkvmerge.
- **Linux**: Only checks for VAAPI devices (`/dev/dri/renderD128` or `/dev/dri/card0`). Lacks probing for other hardware encoders or mkvmerge.

### 3. **Apple M4V Support**
- **Windows**: Fully supported in `ffmpeg_converter_windows.lpr` and `cli_args.pas`. Includes parsing for M4V options and integration with the GUI.
- **Linux**: No support for Apple M4V. The `m4v` codec is not included in the allowed codecs for Linux.

### 4. **Mux Postprocessing**
- **Windows**: Implemented in `windows_mkvmerge.pas` and integrated into `mux_postprocess.pas` with Windows-specific commands.
- **Linux**: Implemented in `mux_postprocess.pas` but lacks Linux-specific optimizations or additional checks.

### 5. **Tool Path Resolution**
- **Windows**: Includes Windows-specific paths and bundled tool detection.
- **Linux**: Uses generic paths and lacks Linux-specific optimizations for tool detection.

### 6. **GUI Support**
- **Windows**: Provides Windows-specific hardware detection and GUI integration.
- **Linux**: No equivalent Linux-specific GUI support.

### 7. **Codec Support**
- **Windows**: Supports a wide range of codecs, including hardware-accelerated options (NVENC, AMF, QSV, Vulkan).
- **Linux**: Limited to VAAPI, copy, prores, prores_ks, and mux. Lacks hardware-accelerated options like NVENC or AMF.

### 8. **Command-Line Argument Parsing**
- **Windows**: Includes Windows-specific arguments like `--vk_device` and M4V options.
- **Linux**: Lacks these options and does not validate VAAPI devices during argument parsing.

## Plan for Improving the Linux Implementation

### 1. **Add Apple M4V Support**
- Extend `ffmpeg_converter.lpr` to include Apple M4V support.
- Add M4V parsing logic to `cli_args.pas` for Linux.

### 2. **Enhance Hardware Probing**
- Extend `linux_probe.pas` to include probing for VAAPI, NVENC (if applicable), and mkvmerge.
- Add Linux-specific hardware detection logic.

### 3. **Improve Tool Path Resolution**
- Update `tool_paths.pas` to include Linux-specific paths and optimizations.

### 4. **Add GUI Support for Linux**
- Create a Linux-specific GUI unit similar to `form_windows.pas`.

### 5. **Extend Codec Support**
- Add support for additional codecs in `cli_args.pas` for Linux, including hardware-accelerated options if available.

### 6. **Enhance Mux Postprocessing**
- Optimize `mux_postprocess.pas` for Linux, including additional checks and Linux-specific commands.

### 7. **Add UTF-8 Console Handling**
- Include UTF-8 console handling in `ffmpeg_converter.lpr` for better compatibility.

### 8. **Update Command-Line Arguments**
- Add Linux-specific arguments like `--vk_device` and M4V options to `cli_args.pas`.

## Implementation Steps

### 1. **Update `linux_probe.pas`**
- Add functions to probe for VAAPI, NVENC (if applicable), and mkvmerge.
- Include logic to detect available hardware encoders.

### 2. **Extend `ffmpeg_converter.lpr`**
- Add Apple M4V support and UTF-8 console handling.
- Include Linux-specific mux postprocessing logic.

### 3. **Update `cli_args.pas`**
- Add Linux-specific codec support and command-line arguments.
- Include M4V parsing logic for Linux.

### 4. **Enhance `tool_paths.pas`**
- Add Linux-specific paths and optimizations for tool detection.

### 5. **Create Linux-Specific GUI Unit**
- Develop a Linux-specific GUI unit similar to `form_windows.pas`.

### 6. **Test and Validate**
- Ensure all changes are compatible with the existing codebase.
- Test the Linux implementation for functionality and performance.

## Conclusion
By addressing these discrepancies and adding the missing features, the Linux implementation can achieve parity with the Windows version, making it more robust and feature-rich for users. This plan serves as a roadmap for future development and testing efforts.