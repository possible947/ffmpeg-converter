# MSVC Build Problem Report

Date: 2026-04-24
Project: `ffmpeg-converter`
Target: `windows_cli` (MSVC)

## Scope
This report documents the current MSVC build blockers discovered while building the Windows CLI target.

Build command used:

```powershell
cmake --build build-msvc --target windows_cli --config Release
```

## Blocking Problems

### 1) Unresolved `popen` symbol at link time
- Error: `LNK2019: unresolved external symbol popen`
- Affected target: `windows_cli`
- Reference location from linker output: function `ffmpeg_encoder_available` in `converter.obj`
- Additional unresolved reference from `converter_windows.obj`

### 2) Unresolved `pclose` symbol at link time
- Error: `LNK2019: unresolved external symbol pclose`
- Affected target: `windows_cli`
- Reference location from linker output: function `ffmpeg_encoder_available` in `converter.obj`
- Additional unresolved reference from `converter_windows.obj`

### 3) Unresolved `S_ISDIR` symbol at link time
- Error: `LNK2019: unresolved external symbol S_ISDIR`
- Affected target: `windows_cli`
- Reference location from linker output: function `converter_process_files` in `converter.obj`

### 4) Unresolved `S_ISREG` symbol at link time
- Error: `LNK2019: unresolved external symbol S_ISREG`
- Affected target: `windows_cli`
- Reference location from linker output: function `check_file` in `converter.obj`

### 5) Final link failure
- Error: `LNK1120: 4 unresolved externals`
- Result: `ffmpeg_converter.exe` is not produced for `windows_cli`

## Non-blocking Warning Observed

### `converter_windows.c` warning
- Warning: `C4047: 'initializing': 'FILE *' differs in levels of indirection from 'int'`
- File/line from output: `src/converter/platform/converter_windows.c(374,11)`

## Why MSVC Fails Here
The current code path includes POSIX identifiers (`popen`, `pclose`, `S_ISDIR`, `S_ISREG`) in shared converter sources. Under MSVC, these must resolve to Windows CRT equivalents/macros (`_popen`, `_pclose`, `_S_IF*`-based checks), otherwise linker resolution fails.

## Fix Direction (for implementation phase)
1. Ensure shared converter compilation under `WIN32` uses MSVC-compatible wrappers/macros for process and stat checks.
2. Keep behavior unchanged while replacing unresolved POSIX identifiers in the Windows/MSVC compile path only.
3. Rebuild and verify that `windows_cli` links successfully in `Release`.

## Current Status
- MSYS2 dependency flow has been removed from the Windows CI/build path.
- MSVC build configuration is now active.
- Build remains blocked by the unresolved symbol set listed above.
