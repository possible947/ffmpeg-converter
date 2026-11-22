# Free Pascal Support Policy

The Free Pascal implementation is supported for Windows release builds and
maintenance. Linux and macOS Pascal builds are unsupported; use the C/CMake
implementation on those platforms.

## Windows build

The Windows application is built natively with FPC, Lazarus, and
`scripts/windows_build_fpc.ps1`. The release consumes the prepared FFmpeg
bundle in `src/platform/windows/bin/`, plus MKVToolNix and GPAC. MSYS2 is not
required.

The release is staged in `fpc/bin/` and must contain the CLI and GUI,
`ffmpeg.exe`, `ffprobe.exe`, `mkvmerge.exe`, `MP4Box.exe`, required runtime
files, and `presets.json`. The build fails when a required input is missing or
fails its startup check. The release script also verifies that the application
resolves the staged tools ahead of environment overrides.

## Validation

Before release, run `scripts/windows_build_fpc.ps1 -Clean` on Windows. The
script builds the CLI and GUI, validates the staged tool executables, and runs
the focused bundled-tool resolution test. Hardware encoder behavior still
requires validation on the corresponding Windows hardware.

`-LibraryOnly` builds `converter_pas.dll` without creating an application
release bundle. There is no broader tracked Pascal test suite.
