# converter_core — Developer Descriptions

Practical rules for building, testing and packaging `converter_core`.
The functional architecture is described in
[converter_core_archtecture.md](./converter_core_archtecture.md); this document
covers how the library finds and validates its external tools, and how those
tools are prepared.

## 1. Principle: build on the platform, for the platform

FFmpeg 9.0, HQ_converter (with its own FFmpeg 8.1) and the helper tools are
built with optimizations for the host they are prepared on. Prepared sets are
not portable and are never shared between operating systems or machines.
For that reason there are no per-OS subfolders: the paths below are the same on
every host.

## 2. Folder layout

### Source tree (development, tests, packaging input)

```text
converter_core/
  docs/                     documentation
  src/                      Pascal sources
  scripts/                  <os>_prepare.sh scripts that create platform/
  tests/                    test programs and runners
  platform/                 prepared on the host, git-ignored
    tools/                  ffmpeg, ffprobe, mkvmerge, MP4Box (+ libraries)
    hq_converter/           complete HQ_converter release (FFmpeg 8.1)
```

### Runtime (next to the built library)

```text
<library dir>/
  libconverter_core.dylib | libconverter_core.so | converter_core.dll
  tools/                    copy of converter_core/platform/tools
  hq_converter/             copy of converter_core/platform/hq_converter (macOS/Linux)
```

`tools/` and `hq_converter/` never share libraries: HQ_converter uses
FFmpeg 8.1 and the tools use FFmpeg 9.0.

### Inside `tools/` (macOS)

- `ffmpeg`, `ffprobe` and their own libraries stay flat in `tools/`, exactly as
  delivered by the FFmpeg builder (`src/platform/macos/bin`). They resolve
  libraries through `@loader_path`.
- `mkvmerge`, `MP4Box` are in `tools/`; every non-system library they need is in
  `tools/lib/` and is resolved through `@loader_path/lib`.
  Keeping them in `lib/` prevents name clashes with the FFmpeg 9.0 libraries
  (for example both sets contain `libiconv.2.dylib`).

## 3. Tool resolution rules

1. The library determines **its own** file location, never the host program's
   location, the current directory or `PATH`:
   - macOS/Linux: `dladdr()` on the address of a unit-level variable.
     Do not pass `@FunctionName` from inside that function: in `{$mode objfpc}`
     it is the address of `Result`.
   - Windows: `GetModuleFileNameW(HInstance)`.
2. Default tools folder: `<library dir>/tools`.
   HQ_converter folder: the `hq_converter` folder next to the tools folder.
3. Override: environment variable `CONVERTER_CORE_TOOLS_DIR`.
   - It replaces the tools folder only (the HQ folder follows it as its sibling).
   - It is honoured in every build.
   - There is no fallback: a tool missing from the selected folder is an error.
     System tools and `PATH` are never used.
   - The selected folder gets exactly the same checks as the default one.
   - Tests use `CONVERTER_CORE_TOOLS_DIR=<repo>/converter_core/platform/tools`.
4. Tool file names get the `.exe` suffix on Windows.

## 4. Required tools and components

| Tool | Check |
|---|---|
| `ffmpeg` | exists, regular file, executable, `ffmpeg -version` exits with 0 |
| `ffprobe` | same, `ffprobe -version` |
| `mkvmerge` | same, `mkvmerge --version` |
| `MP4Box` | same, `MP4Box -version` |

Required FFmpeg components (checked with the resolved `ffmpeg`):

| Kind | Name | Source of truth |
|---|---|---|
| encoder | `prores_ks` | `ffmpeg -encoders` |
| encoder | `libx264` | `ffmpeg -encoders` |
| encoder | `libfdk_aac` | `ffmpeg -encoders` |
| encoder | `ac3` | `ffmpeg -encoders` |
| decoder | `libdav1d` | `ffmpeg -decoders` (rav1d is reported under the same name) |
| build option | `--enable-libsoxr` | `ffmpeg -buildconf` |

The FFmpeg version is **not** checked (converter_core uses FFmpeg 9.0;
HQ_converter keeps its own FFmpeg 8.1).

## 5. Platform detection order

`PlatformDetection(List)` in `src/platform_detection.pas`:

1. Resolve the tools folder (section 3).
2. Check the tools (section 4).
3. Check the FFmpeg components (section 4).
4. Add platform `all`.
5. Run the OS-specific detection (`DarwinPlatformDetection`,
   `LinuxPlatformList`, `WindowsPlatformList`).
6. macOS/Linux: if the HQ_converter folder contains a complete release, add
   platform `hq-converter`. Its absence is not an error.
7. Set `PlatformAllowed := True`.

On the first failure in steps 1–3 detection stops: `PlatformAllowed` stays
`False`, `ErrorMessage` names the failed tool or component and the folder that
was searched, and no platform is added. The resolved tool paths are stored in
`TPlatformlist.Tools`.

## 6. Preparing `converter_core/platform`

macOS (requires MacPorts `mkvtoolnix` and `gpac`, and Xcode command-line tools):

```bash
converter_core/scripts/macos_prepare.sh
```

Sources:

| Target | Source |
|---|---|
| `tools/ffmpeg`, `tools/ffprobe` + libraries | `src/platform/macos/bin` |
| `tools/mkvmerge`, `tools/MP4Box` | system (`/opt/local/bin`) |
| `hq_converter/` | `third_party/hq_converter` |

The script makes every Mach-O file self-contained: each non-system library
(`/opt/local/...`) is copied, its references are rewritten to `@rpath/<name>`,
the matching `LC_RPATH` is added, `/opt/local` rpaths are removed and changed
files are ad-hoc re-signed. Inside `hq_converter/`, a library that already
exists next to the referencing file is reused (for example the bundled
`libomp.dylib`); other libraries go to `hq_converter/lib/`.
The script fails if any `/opt/local` reference remains or any tool does not
start. HQ_converter is changed only at preparation time, never at runtime.
A full run takes about 10 minutes and produces roughly 375 MB in `tools/`
(about 200 libraries, mostly MP4Box dependencies) and 355 MB in `hq_converter/`.

Linux (requires system `mkvtoolnix`, `gpac`, `patchelf`):

```bash
converter_core/scripts/linux_prepare.sh
```

Sources: `tools/ffmpeg`, `tools/ffprobe` from `src/platform/linux/bin`;
`tools/mkvmerge`, `tools/MP4Box` from `PATH` (override with `MKVMERGE_SRC` /
`MP4BOX_SRC`); `hq_converter/` from `third_party/hq_converter`. Non-system
shared libraries are copied to `<root>/lib` and referenced through an
`$ORIGIN`-relative RUNPATH set with `patchelf`. glibc, libgcc_s, libstdc++ and
GPU/driver libraries (libva, libvulkan, libdrm, libGL/EGL, CUDA) stay
host-provided. The script fails if any other external library reference
remains or any tool does not start without `LD_LIBRARY_PATH`.

## 7. Tests

```bash
converter_core/tests/run_platform_detection_tests.sh
```

Builds `tests/platform_detection_probe.pas` into a temporary folder and checks:
detection with `platform/tools`, default resolution through
`<binary dir>/tools`, a missing tools folder and a missing tool.

## 8. Planned milestones

1. Platform detection — tool and component checks, macOS model detection
   (current).
2. `linux_prepare.sh` (`patchelf --set-rpath '$ORIGIN/lib'`) and Linux
   hardware platform detection.
3. Windows tool resolution and hardware platform detection.
4. Library build and packaging that copies `platform/tools` and
   `platform/hq_converter` next to the library.
