# C core and .NET experiment: functional-parity assessment

> **Historical assessment:** this report predates the Linux behavioral-parity
> remediation for `fra`/`fre` output verification, corrupt single-source Mux
> results, and HQ runtime discovery/worker lifecycle. Those three cases were
> revalidated and closed; see [parity-fix-report.md](./parity-fix-report.md).
> Other findings below were not revisited by that scoped follow-up.

## Executive summary

**Functional parity is feasible.** The .NET experiment already implements most of the
Phase 1/2 application surface: preset-driven codec selection, FFmpeg conversion,
hardware discovery, audio analysis, muxing, Apple M4V, CLI, GUI, and unit tests. The
main work is to close a small number of concrete behavior gaps, align the C
refactoring's pipeline contracts, and validate both supported C platforms. This is
not a ground-up port.

Estimate for **user-visible parity with the current C CLI and GUI on Linux and
macOS**, excluding HQ Converter and excluding matching the C GUI toolkits: **about
7–12 person-days**. If HQ Converter is brought into scope, budget a separate
**8–15 person-days** and first resolve the explicit scope restriction in its
specification. These estimates assume the prepared dependencies and test media are
available; GPU/OS-specific failures could extend validation.

## Scope and interpretation

This review compares the current checkout, not an untouched historical snapshot:
the .NET tree includes later fixes, notably the safe VAAPI probing policy, while
the current C tree has advanced beyond Phase 2. The Phase 3.1 specification is
explicitly for the native C implementation and excludes non-C implementations.
The current CMake targets are Linux and macOS; the Windows catalog and Avalonia
support in .NET are additional platform coverage, not C parity requirements.

Phase 4 describes a future filter-engine direction. The current C and C# command
paths still expose the deblock filter rather than that complete filter catalog, so
Phase 4 is not counted as a C-to-C# gap here.

## What is already substantially aligned

- Both implementations use the shared `presets.json` selection/preset model, map
  group/encoder choices to an FFmpeg codec, and support dynamic codec availability.
- Both have the main conversion flow, FFprobe metadata reading, color metadata
  propagation, progress reporting, cancellation, peak/loudness analysis, ProRes
  deblock, mux container choices, and an Apple M4V pipeline.
- The .NET implementation already has separate `MuxPipeline` and
  `AppleM4vPipeline` classes. Mux and M4V are not missing features; the main
  architectural delta is how their orchestration and contracts are shared.
- The C# VAAPI probe uses read-only `vainfo` capability discovery by default and
  gates risky real-encode confirmation behind
  `FFMPEG_CONVERTER_VAAPI_REAL_PROBE=1`, matching the C safety policy.
- Avalonia is a viable functional GUI parity target. Reimplementing GTK4 or Cocoa
  in C# is unnecessary unless native-toolkit parity is itself a requirement.

## Discrepancies and required work

### 1. Audio copy mode and track policy — functional gap

The C audio orchestrator supports `copy`, maps all input audio tracks in that mode,
and uses the first audio track for its other output modes
(`src/audio/audio_orchestrator.c`, `audio_get_track_policy()` and
`audio_resolve_configuration()`; `src/cli/cli_common.c`, `--audio-output` parsing).
The C# parser accepts only `pcm`, `fdk_aac_320`, and
`fdk_aac_320_ac3_640`; `CommandBuilder` always maps `0:a:0` and falls through to
PCM for unrecognized internal modes. Add the `copy` mode to CLI/GUI and command
construction, preserve all audio tracks for copy, and test no-audio/multi-audio
inputs.

### 2. Apple M4V AAC encoder policy — functional discrepancy

The C M4V pipeline explicitly uses `libfdk_aac` at 320 kbps
(`src/m4v/m4v.c`, AAC encode step). The C# pipeline chooses `aac_at`, then
`libfdk_aac`, then native `aac` (`FfmpegConverter.Core/Engine/AppleM4vPipeline.cs`,
AAC encode step). This can produce different output and can succeed where the
core C implementation would fail because its required encoder is unavailable.
Match the C policy and its failure behavior, and validate the permitted AC3
bitrates and chapter behavior against `src/m4v/m4v.c`.

### 3. Interactive CLI — extra behavior to remove or explicitly retain

The C entry point parses command-line arguments and has no interactive setup
wizard (`src/cli/main.c`, `parse_args()` in `src/cli/cli_common.c`). C# launches a
multi-step wizard when invoked without arguments
(`FfmpegConverter.Cli/Program.cs`, `InteractiveMenu.RunMenuAsync()`). For
behavioral parity, remove the implicit wizard (or make it a separately requested
extension) and align the no-argument/help/error outcomes with C. This concerns the
wizard; the C CLI still has a limited confirmation prompt when some, but not all,
input paths fail validation.

### 4. Add Track orchestration — align the reusable contract

The C refactor introduced `add_track_execute()` as a shared, caller-owned
conversion/mux convergence point with a defined single-file, callback, cancellation,
and result contract (`src/add_track/add_track.h/.c`). .NET already performs the
replacement-video workflow through `Converter.ProcessFilesAsync()` and
`MuxPipeline.RunMuxPostprocessAsync()`, so this is not a missing end-user feature.
It does not expose an equivalent reusable Add Track entry point or clearly mirror
the C lifecycle/error contract. Extract or formalize that boundary in the .NET
core, then compare intermediate/final output naming, cleanup, overwrite, and stop
behavior with C.

### 5. Audio-core separation — architecture and testability

The C refactor extracted audio mode policy, encoder capability selection,
normalization expression generation, and analysis into `src/audio/audio_orchestrator.c`.
The corresponding .NET responsibilities remain spread across
`CommandBuilder.cs` and `Converter.cs`. This is not by itself a functional defect,
but it makes synchronization harder. Centralize the shared audio policy behind a
focused .NET service (or document an equivalent tested boundary) so new C changes
have one obvious counterpart.

### 6. Runtime catalog and CLI parity — verify remaining edges

Both versions consume the JSON catalog for normal codecs, and the current C# probe
already mirrors the important safe VAAPI default. C now also adds runtime HQ
catalog entries to `--codecs-list`; those entries cannot be derived from the
ordinary `presets.json` catalog. Keep the JSON-backed codec paths data-driven, and
add regression checks for catalog availability, encoder capability filtering,
10-bit variants, default hardware-device selection, and `--codecs-list` output.
Do not replace the safe VAAPI default with real encoding probes.

### 7. HQ Converter — explicit scope decision

The current C implementation has an HQ adapter/catalog and isolated worker path
(`src/hq/`, called from `src/cli/main.c` and routed through
`add_track_execute()`). There is no corresponding .NET runtime/catalog/worker
implementation. However, the Phase 3.1 specification explicitly excludes
non-C implementations, so this should **not** block parity with the documented
scope. If the product requirement is changed to include HQ, the .NET port needs a
separately designed worker/process integration, catalog and preset validation,
status/progress/cancellation forwarding, packaging, and convergence with Add Track.
Prefer a process boundary over loading the native library in-process; the C
implementation deliberately isolates HQ work.

### 8. Platform GUI and packaging

The current C GUI surfaces are GTK4 on Linux and Cocoa on macOS; .NET uses one
Avalonia GUI. Compare supported user actions and their results—selection cascades,
device choices, M4V configuration, Add Track, output handling, logs, and stopping—
rather than requiring matching widgets or native frameworks. Keep Windows-specific
.NET behavior as an explicit extension; it is outside the current C target
matrix. Validate tool/preset discovery and packaged runtime behavior in built
applications, not only from the repository checkout.

## Suggested work sequence and effort

| Work package | Main outcome | Estimate |
|---|---|---:|
| CLI contract | Remove/gate implicit wizard; align defaults, validation, help, and list behavior | 0.5–1 day |
| Audio and M4V | Add audio-copy/all-tracks semantics; align encoder requirements and M4V edge behavior | 1–2 days |
| Pipeline boundaries | Match Add Track lifecycle, cancellation, error, cleanup, and output contracts | 1–2 days |
| Catalog and probing | Exercise current preset/capability paths, platform defaults, and codec listing against C | 1–2 days |
| GUI and packaging | Close functional UI differences; verify installed/bundled discovery on Linux and macOS | 1–2 days |
| Regression validation | Add focused tests and run CLI/pipeline checks on supported platforms and representative hardware | 2–3 days |
| **Baseline total** | **Functional Linux/macOS parity; HQ excluded** | **about 7–12 person-days** |

The estimates overlap slightly: pipeline and GUI testing can reuse the same
fixtures. HQ Converter, if re-scoped into the target, is an additional **8–15
person-days**, with low confidence until the distributable runtime and supported
integration interface are confirmed.

## Validation snapshot

`dotnet test FfmpegConverter.slnx` built the .NET projects and ran 125 tests:
**123 passed, 2 failed**. The M4V and mux dry-run tests expected `Ok` but got
`PlatformInitFailed`; this checkout environment has no `ffmpeg` on `PATH`, and
`Converter.ProcessFilesAsync()` checks for FFmpeg before reaching its dry-run
branches. Make those tests independent of a real installation (for example, by
injecting tool paths or a controlled test executable) before using them as a
parity gate. This is an environment-sensitive testability issue, not evidence that
the actual M4V/mux pipelines are absent.

## Conclusion

The C# implementation is a strong base and parity is practical without rewriting
its engine or replacing Avalonia. Prioritize the audio-copy behavior, M4V encoder
policy, command-line mode, and Add Track contract; then verify the GUI and
platform-specific runtime behavior with focused tests. Treat HQ Converter as a
separate scope decision because its current specification explicitly reserves it
for native C.
