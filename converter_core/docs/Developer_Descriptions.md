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
   platform `hq-converter` and store the folder in `Tools.HqConverterPath`
   (the registry reads the HQ preset file from it). Its absence is not an error.
7. Set `PlatformAllowed := True`.

On the first failure in steps 1–3 detection stops: `PlatformAllowed` stays
`False`, `ErrorMessage` names the failed tool or component and the folder that
was searched, and no platform is added. The resolved tool paths are stored in
`TPlatformlist.Tools`.

### GPU platforms (Windows and Linux)

`WindowsPlatformList` and `LinuxPlatformList` only identify the GPU
architecture and add the matching registry platform name. They never start
FFmpeg or run test encodes; which encoders and presets a platform offers is
decided by the registry (`src/reestr.pas`). An unknown or missing GPU adds
nothing and is not an error.

Display adapters are read by PCI vendor/device ID: on Windows through
`EnumDisplayDevicesW` (`PCI\VEN_xxxx&DEV_xxxx`), on Linux from
`/sys/bus/pci/devices/*/{class,vendor,device}` (display class `0x03`).

| Condition | Windows | Linux |
|---|---|---|
| NVIDIA `10DE`, device `1D80–1DFF`, `1E00–1FFF`, `2180–21FF`, `2200–25FF` except `2300–23FF` (GV100, Turing, Ampere; GH100 excluded) | `windows_nvenc7` (ID 9) | `linux_nvenc7` (ID 6) |
| Intel `8086`, device `5690–56CF` (Arc DG2) | `windows_qsv_dg2` (ID 10) | `linux_qsv_dg2` (ID 7) |
| any NVIDIA adapter and the Vulkan loader loads (`vulkan-1.dll` / `libvulkan.so.1`) | `windows_vulkan_nvidia` (ID 11) | `linux_vulkan_nvidia` (ID 8) |

Each name is added once, in ID order. `linux_vaapi` is not detected yet.
The ID ranges are in `NVENC7_DEVICE_RANGES`, `NVENC7_EXCLUDED_RANGES` and
`DG2_DEVICE_RANGES`.

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

Registry (`GetReestr`) tests, no tools needed:

```bash
converter_core/tests/run_reestr_tests.sh
```

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File converter_core/tests/run_reestr_tests.ps1
```

Builds `tests/reestr_tests.pas` with heap (`-gh`) and range/overflow/stack
checks and fails on any `FAIL` line or unfreed memory. It checks the base
groups, ignored unknown/duplicate names, the fixed codec order, encoder
availability of the nvenc7/qsv_dg2/Vulkan platforms (no AV1 on nvenc7, no
10-bit on Windows), the tree rules for all platforms at once and the registry
for the hardware platforms detected on the current machine. HQ checks
(`HQPresetLoader`) write temporary preset files: missing folder/file, broken
JSON and empty `encoders` give no HQ group; a sample file checks encoder IDs
(known keys 18–23, unknown keys from 24), file order, `default_preset` as the
current preset, Preset ID 40 with tool 5 and the HQ rules. The runners pass
`third_party/hq_converter` (`--hq <dir>`); when the release is there its
preset file is loaded too, otherwise `NOT AVAILABLE` is printed.
`--dump <names...>` (`-Dump` in PowerShell, `hq-converter` included) and
`--detected` (`-Detected`) print the registry tree.

## 8. Planned milestones

1. Platform detection — tool and component checks, macOS model detection
   (current).
2. `linux_prepare.sh` (`patchelf --set-rpath '$ORIGIN/lib'`) and Linux
   hardware platform detection (GPU platforms by PCI ID done; `linux_vaapi`
   pending).
3. Windows tool resolution and hardware platform detection (GPU platforms by
   PCI ID done).
4. Library build and packaging that copies `platform/tools` and
   `platform/hq_converter` next to the library.
