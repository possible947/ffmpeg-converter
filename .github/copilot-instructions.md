# Copilot instructions for ffmpeg-converter

Prefer executable sources in this repo (CMake, Makefiles, scripts, and platform code) over prose docs when they disagree. The C/CMake implementation in `src/` is the primary implementation for Linux and macOS and the reference for Phase 3 mux/M4V/policy behavior. **There is no Windows C implementation: it was never functional and its platform sources were removed (`b369127`, Phase 3.0).** Windows has only ever shipped the Free Pascal port, which is also where the hardware-acceleration (NVENC/AMF/QSV/Vulkan) logic was developed and validated; Linux hardware/Vulkan encoding is compatibility-only. Phase 4 (filter engine) targets Windows first. The Windows Free Pascal port in `fpc/` is deprecated for new development and retained as a legacy implementation; avoid adding new Pascal features unless explicitly requested. The experimental .NET 10 port in `dotnet-experiment/` is planned to replace Pascal as the native Windows implementation, but it is not yet integrated into the release/build pipeline. Older documents may still describe Pascal as actively maintained.

## ⚠️ Critical Prerequisites (read before building)

- **FFmpeg version 8.1** (not system FFmpeg) compiled with `--enable-libfdk_aac` and `--enable-soxr` is **mandatory**
  - macOS: must be in `src/platform/macos/bin/ffmpeg` and `src/platform/macos/bin/ffprobe` → build fails with `FATAL_ERROR` if missing
  - Linux: optional for build, but binary won't work at runtime without it
- **Windows has no C build at all** (no CLI, no GUI, no CMake target) — Windows is Pascal (`fpc/`) today, with the .NET port as its planned replacement
- **macOS Pascal is discontinued** (v2.4+) — use C/Cocoa GUI only; FPC Makefile hard-blocks Darwin with exit error
- `presets.json` is copied next to each binary automatically by the build system
- HQ_converter is an external Linux/macOS dependency. Its prepared release must
  be placed in `third_party/hq_converter/` before configuring/building; the build
  copies the complete tree to `bin/hq_converter/` and bundles it with AppImage/
  application packages. The built application is monolithic and must not modify
  or update HQ_converter at runtime. Windows builds do not include HQ_converter.

## Build, test, and validation commands

### C/CMake

Linux:
```bash
cmake -B build
cmake --build build --target linux_cli
cmake --build build --target linux_gui
```

macOS:
```bash
cmake -B build -DCMAKE_PREFIX_PATH=/opt/local
cmake --build build --target macos_cli
cmake --build build --target macos_gui_native
cmake --install build   # produces build/install/ffmpeg_converter_gui_macos.app
```

Windows has no C/CMake target. Use the Free Pascal or .NET builds below.

C/CMake tests are registered with CTest and vary by platform. Build before running them:
```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Run one test by building its target and filtering the CTest name, for example:
```bash
cmake --build build --target test_preset_loader
ctest --test-dir build -R '^PresetLoaderTests$' --output-on-failure
```

For a CLI smoke check, run the platform binary with `--help` (for example `./build/bin/ffmpeg_converter --help`).

### Deprecated Free Pascal port (Windows)

The Pascal port is deprecated for new feature work, but its existing Windows
CLI, library, and GUI remain in the repository for maintenance. Its Makefile
currently permits builds only on Windows.

CLI/library build:
```bash
make -C fpc/build cli
make -C fpc/build lib
```

GUI build:
```bash
make -C fpc/build gui
```

Test entry point (currently skips because `fpc/test/` has no Pascal test sources):
```bash
make -C fpc/build tests
```

Run Pascal targets from Windows; the Makefile rejects Linux and macOS builds.

Pascal build notes: default `FPCFLAGS=-Mobjfpc -Sh -O2`; compiled units land in `fpc/build/.units/`, binaries in `fpc/bin/` (never commit these). On Windows/MSYS2, bash mangles `-Fu` include paths — the Makefile wraps them with `cygpath -w` + double quotes; follow that pattern for any new `-Fu` flag. Windows FPC builds use `scripts/windows_build_fpc.ps1`/`.bat`.

### Experimental .NET port (planned Windows Pascal replacement)

From `dotnet-experiment/`:
```bash
dotnet build FfmpegConverter.slnx
dotnet test FfmpegConverter.Tests/FfmpegConverter.Tests.csproj
dotnet test FfmpegConverter.Tests/FfmpegConverter.Tests.csproj --filter FullyQualifiedName~CommandBuilderTests
dotnet run --project FfmpegConverter.Cli/FfmpegConverter.Cli.csproj -- --help
```

The last `dotnet test` command runs one test class; change the filter to the desired class or test name. There is no repository-defined lint target.

## Validation Strategy

**Core validation proves WHAT the application does. UI acceptance proves HOW
validated behavior is presented.** Core coverage must be comprehensive for the
affected behavior; UI coverage is representative, focused, and non-combinatorial.

### Level 1: Core and Behavioral Validation (default)

Implementation work requires appropriate Level 1 checks. This covers policies,
input validation, command construction, audio modes, queues, FFmpeg processing,
Mux, Add Track, MKV/MOV/M4V, HQ, stream preservation, output verification, errors,
cancellation, runtime discovery, hardware backends, and non-UI platform behavior.

For Core-only work: implement -> focused automated tests -> full applicable
automated suite -> targeted CLI/Core runtime or media validation when required
-> commit. Use deterministic fixtures, representative real-media smoke tests,
and ffprobe verification where the changed behavior warrants them. Select the
applicable suite for the changed implementation/platform, not unrelated suites
merely to increase test volume. Report unavailable checks explicitly; do not
treat missing tools or skipped checks as passing validation.

Interactive UI automation is **not required merely because a GUI consumes a
changed Core API or event**. Passing appropriate Level 1 checks verifies that
Core implementation and permits its commit. Do not block it for lack of
interactive testing or use expensive GUI runs to re-prove Core/CLI media behavior.

### Level 2: UI Acceptance

Run UI acceptance separately when a change affects controls, availability or
enabled state, selections or persistence, dialogs, progress/status, speed/ETA,
queue presentation, workflow transitions, errors, or completion presentation.
This validates presentation, not a duplicate Core behavioral matrix.

Choose the minimum scenarios covering distinct presentation contracts. A
representative Converter matrix may include one Legacy single-file conversion,
one Legacy queue containing success and failure, one HQ workflow, one Mux MKV
workflow, one Mux MOV workflow if its visible stages differ, and one Mux M4V
workflow because its MKV and MP4Box stages are visually distinct. Run only the
scenarios relevant to the change. Do not exhaustively cross every encoder,
preset, audio mode, container, and source codec through the GUI when Core tests
already establish those combinations.

Core validation establishes codecs, audio track counts, Copy policy, AAC+AC3,
subtitles, chapters, attachments, replacement isolation, and hardware encoder
output. UI checks primarily establish correct controls/selections, operation
start, current filename, progress/status, stage transitions, queue items matching
actual results, and correct completion/failure. File existence or a successful
probe is an acceptable UI smoke check; do not repeat the complete semantic
ffprobe matrix for every GUI workflow.

Keep progress contracts distinct, even when sharing presentation infrastructure:

| Source | Presentation contract |
|--------|-----------------------|
| Legacy | FFmpeg-derived stage, percentage, speed, and ETA |
| HQ | HQ/segmentator stage and checkpoint percentage; no fabricated Legacy speed/ETA |
| Mux | Source-preparation FFmpeg telemetry where applicable; structured Mux stages; mkvmerge/MP4Box stage-local percentages where available; status-only stages when percentage is unreliable |

Preserve the semantic source/workflow; do not impose one universal progress
interpretation or fabricate speed/ETA for non-FFmpeg stages.

### Reuse UI automation and preflight first

Before writing GUI automation, locate existing repository scripts, read their
documentation, and reuse them. For Linux Avalonia, consult
[`dotnet-experiment/tests/ui/README-linux-ui-automation.md`](../dotnet-experiment/tests/ui/README-linux-ui-automation.md)
and its `UiSmoke.csproj` / `StartupHook.cs` driver before interactive testing.
Extend the driver only for a confirmed harness defect or genuinely new UI
workflow; keep generally useful scenarios/scripts in the repository. Prefer
extending and retaining this infrastructure over recreating temporary,
coordinate-based or session-specific scripts. Screenshots, videos, logs, and
media fixtures normally stay outside tracked source unless explicitly required.

Run a cheap preflight before expensive acceptance: launch the app, identify the
correct process/window, confirm graphical-session access and relevant UI
inspection, and verify one harmless interaction. Only proceed after preflight
PASS. A failed connection/preflight is **UI AUTOMATION ENVIRONMENT FAILURE**,
with application UI **NOT TESTED**, not application failure. Do not modify
production code to compensate for broken automation.

GNOME can use a Wayland desktop while Avalonia uses X11/XWayland. Do not infer
the application backend solely from `XDG_SESSION_TYPE`; use the documented
preflight/runtime evidence. Do not force native Wayland or X11 behavior, or
change the desktop session, merely for testing.

### Proportionality, task separation, and commits

Select validation by the changed component, not an automatic maximal matrix:

| Change | Appropriate validation |
|--------|------------------------|
| Policy only | Focused policy tests and full applicable automated suite |
| Media execution | Focused tests, full applicable suite, representative real-media validation |
| UI presentation | Focused presenter tests, full applicable suite, separate UI acceptance |
| Packaging only | Package structural validation and targeted packaged smoke |
| Hardware backend | Backend-specific runtime matrix, preserving hardware-probe safety rules |
| Instructions/documentation only | Review the documentation/diff; no application, media, or UI tests unless documentation tests apply |

Do not automatically run full real-media, HQ encoder, hardware backend, UI,
packaging, or cross-platform matrices after every change. Repeat a proven matrix
only when relevant production code changes, dependencies/toolchains can affect
its behavior, a regression implicates that subsystem, or release qualification
requires it. A different agent is not a reason to repeat unaffected passing work.

Prefer two-stage agent work: Stage 1 implements, runs focused tests, the full
applicable automated suite and targeted runtime checks, then commits. Stage 2,
only when needed, runs UI preflight and representative acceptance, fixes only
demonstrated presentation defects, and confirms final UI acceptance. Do not add
Stage 2 to every implementation task; this separation reduces runtime, cost,
and risk.

Interactive UI acceptance is not a universal commit gate. If appropriate Level 1
checks pass but required UI acceptance is intentionally deferred, report
**IMPLEMENTATION VERIFIED** and **UI ACCEPTANCE PENDING**. This does not invalidate
passing Core tests. Do not modify, revert, or reimplement a passing change merely
because GUI automation is unavailable. After later acceptance, update its status
and fix demonstrated UI defects without reopening validated Core behavior absent
evidence.

Use precise evidence/statuses: **PASS**, **FAIL**, **NOT TESTED**,
**NOT AVAILABLE**, **ENVIRONMENT LIMITATION**, **UI ACCEPTANCE PENDING**, and
**OUT OF SCOPE**. Do not claim UI PASS from ViewModel/presenter tests alone, Core
failure from an automation connection failure, or behavioral parity from
screenshots alone. Match each claim to evidence from the appropriate layer.

## Architecture at a glance

The C/CMake implementation is the primary product implementation:

- `src/` is the primary C/CMake implementation. It contains the converter core, platform-specific code, CLI entry points, and native macOS/Linux GUIs. It has no Windows code.
- `fpc/` is the deprecated legacy Pascal port, with existing Windows CLI/GUI/library targets. When maintaining shared behavior or ABI, keep it aligned with C; do not assume it is the preferred place for new features.
- `dotnet-experiment/` is an exploratory .NET 10 implementation with Core, CLI, Avalonia GUI, and xUnit projects. It is the planned native Windows replacement for the Pascal port, but remains experimental and is not yet wired into the release/build pipeline.

### Version 3.0 Phase 3

Phase 3 is documented in three stages:

- **3.0 — Pre-integration refactor:** isolate shared audio, Add Track, and mux execution paths.
- **3.1 — HQ Converter integration:** integrate HQ Converter into the C/Linux and C/macOS paths; Windows and Pascal are outside this integration.
- **3.2 — Mux policy cleanup:** a corrective-refactor specification in `docs/v3.0-Phase3.2.md`, not a new feature phase. It is marked as a draft; check its status before treating requirements as implemented.

Key structure:

- `src/converter/` contains the public C ABI (`converter.h` / `converter.c`) and the conversion engine. Treat `Converter` as opaque.
- `src/cli/{linux,macos,windows}/` contains the CLI entry points.
- `src/gui/` and `src/gui_macos_native/` contain the native GUI code.
- `src/m4v/` and `fpc/converter/apple_m4v_creator.pas` contain the Apple M4V pipeline; both must be kept in sync.
- `src/platform/{linux,macos,windows}/` and `fpc/platform/` contain runtime probing and OS-specific logic.
- The preset catalog is shared runtime data: `presets.json` is consumed by the runtimes, while CMake generates a platform-gated build copy from `presets.json.in`. Keep catalog changes aligned with the C and Pascal command-builder tables; the .NET port has its own preset loader and builder.

Important repo-level behavior:

- Tool discovery order is executable-adjacent dir, environment variables (`FFMPEG_BIN`, `FFPROBE_BIN`, `MKVMERGE_BIN`, `MP4BOX_BIN`, legacy `MKVMERGE`), then `PATH`.
- Runtime probing chooses supported codecs and GPU encoders dynamically; codec strings are platform-specific and must be validated against the runtime, not hardcoded from memory.
- `-o/--output` sets an output directory, not a filename; the default output directory is `$HOME/ffmpeg_converter`.

## Key conventions specific to this repo

- Keep the C and Pascal implementations aligned when changing public interfaces or data-driven codec logic. In particular, any change to `src/converter/converter.h` should be mirrored in `fpc/converter/converter_pas.lpr`.
- `ConvertOptions.*` strings such as `codec`, `audio_norm`, and `audio_output_mode` are fixed-length `char[32]` arrays; use `snprintf`/`strncpy`, not `strcpy`.
- The converter object is opaque: create/destroy via `converter_create()` / `converter_destroy()` and do not reach into `struct Converter` from outside `converter.c`.
- `src/CMakeLists.txt` treats `core`, `utils`, `progress`, `audio`, `video`, and `ffmpeg_cmd` as interface libraries. Put implementations into the correct module directory rather than adding `.c` files there.
- OS-specific code belongs in the matching platform directory; do not add Linux-only code into macOS or Windows platform subdirs, or vice versa.
- `mkvmerge` is optional; mux mode is silently disabled when it is absent.
- Apple M4V pipeline details are intentionally duplicated in C and Pascal and must stay synchronized: stream-copy video, fixed AAC CBR 320k via `libfdk_aac`, AC3 track, `MP4Box` mux, optional chapter import.
- The repo’s executable sources are the source of truth. If README text and scripts disagree, prefer the actual build or runtime behavior in CMake/Makefile/scripts.
- **Never add a real hardware-encode probe (VAAPI/Vulkan/NVENC/QSV/AMF) that runs by default without process isolation and a non-invasive fallback.** A real one-frame VAAPI encode in `src/platform/linux/runtime_probe.c` was found to crash the desktop compositor (GNOME Shell/Mutter) via a kernel-level GPU-ring fault on some `amdgpu` systems — not the probe process itself, so `fork`/timeout/signal handling alone cannot prevent it. VAAPI capability detection now defaults to a read-only `vainfo -a` profile/entrypoint listing (`vaapi_profile_supported()`/`VaapiProfileSupported`), with real-encode confirmation gated behind `FFMPEG_CONVERTER_VAAPI_REAL_PROBE=1` (opt-in only). Follow this same read-only-by-default pattern for any new hardware-encoder probe; see `CHANGELOG.md` "Linux VAAPI hardware-probe crash fix" for full details.

## Key env vars for Linux hardware probing (`src/platform/linux/runtime_probe.c`)

- `FFMPEG_CONVERTER_VAAPI_REAL_PROBE=1` — opt-in: additionally confirm VAAPI profiles already listed by `vainfo` with a real one-frame encode (accepts crash risk on affected drivers). Default off.
- `FFMPEG_CONVERTER_PROBE_DEBUG=1` — redirect probe subprocess stderr to a debug log instead of discarding it.
- Probe results are cached at `~/.cache/ffmpeg_converter/hw_probe_cache.bin` (or `$XDG_CACHE_HOME/ffmpeg_converter/...`), auto-invalidated by a signature over the bundled ffmpeg binary, `presets.json`, and the `/dev/dri/renderD*` node set. `linux_invalidate_codec_support_cache()` exists for a future "rescan hardware" action.
- The same safety policy (vainfo pre-filter + opt-in real probe) is mirrored in `fpc/platform/linux_probe.pas` (`VaapiProfileSupported`), even though that file's `{$IFDEF Linux}` branches are currently dead code in the shipped Windows-only Pascal build.

## Apple M4V pipeline

Implemented separately in C (`src/m4v/`, `src/gui_macos_native/apple_m4v_creator.m`) and Pascal (`fpc/converter/apple_m4v_creator.pas`). Order: (1) stream-copy video to temp `.mp4`, (2) AAC via `libfdk_aac -b:a 320k` CBR — **fixed, not configurable** (`aac_quality` removed in v2.5), (3) AC3 (384/448/640), (4) `MP4Box` mux, (5) optional `ffmpeg -map_chapters 1 -c copy`.

## CI / releases

`.github/workflows/` contains only the tag-driven macOS workflow (tag `macos`, also `workflow_dispatch`): it builds `macos_cli` and creates a GitHub Release with an auto-generated changelog. There is no Windows workflow (the former one built a nonexistent C `windows_cli` and was removed); Windows is built manually with `scripts/windows_build_fpc.ps1`.

## Further reading

- `README.md` for the user-facing feature overview and build requirements
- `AGENTS.md` for repo-specific AI guidance and gotchas
- `docs/` and `presets.json` for codec/preset behavior introduced in the v3.0 phases
- platform build scripts under `scripts/` for Windows/Linux packaging and build flows

## Notes for future Copilot work

- Prefer small, targeted changes that honor the dual C/Pascal architecture.
- When editing codec/preset behavior, update the matching runtime presets and the encoding tables used by the C and Pascal command builders together.
- Start validation with the smallest build or target-level check available, then complete the applicable checks in Validation Strategy.

## Troubleshooting Common Agent Errors

| Problem | Likely Cause | Solution |
|---------|--------------|----------|
| macOS CMake fails: "src/platform/macos/bin/ffmpeg not found" | FFmpeg not bundled | Place static FFmpeg 8.1 binary in `src/platform/macos/bin/` |
| Windows: no `windows_cli` target | C Windows port never worked and was removed | Use the Pascal build (`make -C fpc/build cli`/`gui`, `scripts/windows_build_fpc.ps1`) or the .NET port |
| Linux CLI builds but fails at runtime: "ffmpeg not found" | No bundled binary, missing env var | Set `FFMPEG_BIN=/path/to/ffmpeg` or place binary next to `ffmpeg_converter` |
| Windows: GUI doesn't exist in C | Tried to build C on Windows | Windows GUI is Pascal-only (`fpc/gui/`) until the .NET/Avalonia port replaces it. |
| macOS: Error when building FPC | Pascal support discontinued | Use C implementation (`cmake --build build --target macos_cli`). FPC Makefile exits with error on Darwin. |
| Both C & Pascal sync issue | Changed `converter.h` but not Pascal export | Any change to `src/converter/converter.h` → mirror in `fpc/converter/converter_pas.lpr` (8 exported functions) |
| M4V pipeline broken after edit | C and Pascal M4V diverged | Keep `src/m4v/` + `src/gui_macos_native/apple_m4v_creator.m` synchronized with `fpc/converter/apple_m4v_creator.pas` |

## Misc gotchas

- `AGENTS.md` itself is gitignored; this file (`.github/copilot-instructions.md`) is its committed sibling — keep them in sync when either is updated.
- Codec strings vary by platform (`*_vaapi` Linux, `*_videotoolbox` macOS, `prores_ks_vulkan` Windows) and are runtime-probed — check `README.md` Features and `converter.h` comments rather than assuming a fixed list.
