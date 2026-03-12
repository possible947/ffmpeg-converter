# Free Pascal Port — ffmpeg_converter

This folder contains the Free Pascal (FPC) implementation of the `ffmpeg-converter` project, including CLI, shared library, and Lazarus/LCL GUI.

## Features

- Full C API parity — exports all 7 converter symbols with ABI-compatible types
- CLI with argument parsing and interactive multi-step menu
- Lazarus/LCL GUI with threaded conversion and progress display
- Apple M4V creator with multi-step mux pipeline (video copy + AAC + AC3 + MP4Box)
- 2-pass peak and loudnorm (EBU R128) audio analysis
- Codecs: copy, prores, prores_ks, h265_mi50 (VAAPI)

## Folder Layout

- `converter/`: core engine, C ABI export, command builder, analysis, runner, Apple M4V creator
- `common/`: reusable file, process, path, and time helpers
- `json/`: loudnorm JSON parsing (using `fpjson`/`jsonparser`)
- `cli/`: CLI binary — argument parsing, interactive menu, progress display
- `gui/`: Lazarus/LCL GUI application with threaded workers
- `test/`: unit tests and integration test scripts

## Build

### CLI binary

```bash
fpc -Fu./fpc/converter -Fu./fpc/common -Fu./fpc/json -Fu./fpc/cli ./fpc/cli/ffmpeg_converter.lpr
```

### Shared library

```bash
fpc -Fu./fpc/converter -Fu./fpc/common -Fu./fpc/json ./fpc/converter/converter_pas.lpr
```

### GUI (requires Lazarus IDE or lazbuild)

```bash
lazbuild ./fpc/gui/form.lpi
```

### Tests

```bash
fpc -Fu./fpc/converter -Fu./fpc/common -Fu./fpc/json ./fpc/test/test_cmd_builder.pas
fpc -Fu./fpc/converter -Fu./fpc/common -Fu./fpc/json ./fpc/test/test_path_parse.pas
fpc -Fu./fpc/converter -Fu./fpc/common -Fu./fpc/json -Fu./fpc/cli ./fpc/test/test_cli_mode_matrix.pas
```

Shell-based integration tests:

```bash
bash fpc/test/test_cli_args_matrix.sh
bash fpc/test/check_gui_cli_issues.sh
```

### Generated artifacts

- CLI binary: `fpc/cli/ffmpeg_converter`
- Shared library: `fpc/converter/libconverter_pas.so`
- GUI binary: `fpc/gui/form`

## C/C++ Integration

Use header: `fpc/converter/converter_pas.h`

Link against:

```bash
-L fpc/converter -lconverter_pas
```

Runtime loader path example:

```bash
LD_LIBRARY_PATH=fpc/converter ./your_app
```

## Documentation

- Converter library API detail: `fpc/converter/CONVERTER_LIBRARY_DETAIL.md`
- Code review report: `fpc/REVIEW_REPORT.md`
- Optimization audit: `fpc/OPTIMIZATION_AUDIT.md`
- Cross-platform install guides: `docs/install-linux.md`, `docs/install-macos.md`, `docs/install-windows.md`
