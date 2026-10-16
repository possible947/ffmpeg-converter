# .NET/C Parity Implementation Report

## 1. Baseline repository state

**Status: PASS**

Baseline branch was `main`, HEAD `ba42f4e0cd3a3c67f89951b86470d9084cdc90d5`. The worktree already contained the untracked audit report `dotnet-c-parity-audit-and-port-plan.md`; it has been left untouched. A second pre-existing untracked file, `dotnet-experiment/NET:Avalonia Converter Parity Audit and Direct-Port Plan.md`, has also been preserved.

Baseline .NET solution build succeeded with zero warnings and errors; the test suite passed 131/131. The existing native `build/` directory had CTest registrations but lacked test executables, so the baseline CTest run was unavailable. A 232-file SHA-256 inventory of tracked files outside `dotnet-experiment/` was captured before implementation.

## 2. C reference HEAD

**Status: PASS**

The immutable native reference is HEAD `ba42f4e0cd3a3c67f89951b86470d9084cdc90d5`. All tracked paths outside `dotnet-experiment/` continue to match the saved SHA-256 inventory.

## 3. .NET implementation phases completed

| Phase | Status | Result |
|---|---|---|
| A — Contract fixtures and typed policy model | PASS | Added typed policy enums, profile/result records, pure resolver, policy and subtitle tests. |
| B — Shared audio availability and Copy isolation | PASS | Wired policy-based choices and validation into Core, CLI, interactive menu, and Avalonia; separated Mux M4V selection from Direct M4V. |
| C — Catalog and tool-resolution parity | PASS | Made catalog/tool resolution inputs deterministic and covered precedence, fallback, malformed catalogs, platform catalogs, and environment aliases. |
| D — Replacement codec probe and early validation | PASS | Added a typed one-probe result, explicit missing/unreadable/corrupt/no-video failures, C-derived container codec acceptance, and rejection before Add Track preparation. |
| E — Shared Add Track contract | PARTIAL | Added a reusable validated request/result contract shared by ordinary Mux and HQ, verifies the prepared MKV has video and audio, and retains unique intermediate cleanup. Final-container verification remains for Phase J. |
| F — MKV preservation and replacement isolation | PARTIAL | Source preparation now maps supported subtitles, chapters, attachments, and metadata into the MKV intermediate; mkvmerge explicitly takes only replacement video and assigns source segment/video metadata. Real-media ownership verification remains. |
| G — MOV finalization and subtitle compatibility | PARTIAL | MOV remux now explicitly maps video/audio, applies per-subtitle compatibility conversions, preserves chapters/metadata, uses MOV metadata tags, and warns when attachments are dropped. Real-media output verification remains. |
| H — Mux M4V finalization | PARTIAL | Mux M4V now distinguishes AAC-only from AAC+AC3/default, requires libfdk_aac, preserves chapters/subtitles/metadata, assigns AAC-only default disposition, uses argument lists, and selects a plus-free MP4Box import directory. Real-media output verification remains. |
| I — Shared preservation plan | PARTIAL | MKV, MOV, and M4V source/finalization paths now consume one typed preservation plan for stream ownership, audio selection, subtitles, chapters, metadata, language, dispositions, attachments, and replacement video. M4V attachment dropping now matches native policy; real-media verification remains. |
| J — Output verification, errors, and cancellation | PARTIAL | Typed ffprobe snapshots verify openable containers, replacement video codec, audio/subtitle codecs and dispositions, chapters, attachments, and auxiliary-stream isolation. MOV/M4V finalizers stage output and promote only after verification; real-media and injected failure/cancellation tests remain. |
| K — HQ handoff parity | PARTIAL | HQ output continues through the shared Add Track and typed policy path; cancellation now escalates from stop to worker kill and process-tree termination. Runtime encoder and native HQ integration tests remain unavailable. |
| L — CLI and Avalonia integration | PARTIAL | CLI probes and policy-validates replacement video before conversion; GUI audio choices refresh from current sources/replacement and policy selection, invalid Mux Add Track states disable Start, and Mux M4V checks MP4Box. GUI interaction automation remains. |
| M — Direct M4V workflow | PARTIAL | Direct M4V keeps its own dialog, language/defaults, uses fixed libfdk_aac, and now applies direct M4V metadata/subtitle preservation. Edit-before-mux runs the selected source conversion, verifies fresh intermediates, creates original-name M4V outputs, and cleans successful intermediates; real-media validation remains. |

## 4. Files changed by phase

**Phase A:** `FfmpegConverter.Core/Policy/MuxPolicy.cs`; `FfmpegConverter.Tests/MuxPolicyTests.cs`.

**Phase B:** `FfmpegConverter.Core/Policy/AudioModePolicy.cs`; `FfmpegConverter.Core/Models/ConvertOptions.cs`; `FfmpegConverter.Core/Engine/CommandBuilder.cs`; `FfmpegConverter.Core/Engine/Converter.cs`; CLI `CliSummary.cs`, `CommandLineParser.cs`, `InteractiveMenu.cs`, `Program.cs`; GUI `MainWindow.axaml`, `MainWindow.axaml.cs`; related Core, CLI, and GUI tests.

**Phase C:** `FfmpegConverter.Core/Presets/PresetDb.cs`; `FfmpegConverter.Core/Probing/ToolDiscovery.cs`; `FfmpegConverter.Tests/PresetDbTests.cs`; `FfmpegConverter.Tests/ToolDiscoveryTests.cs`.

**Phase D:** `FfmpegConverter.Core/Engine/InputVideoInfo.cs`; `FfmpegConverter.Core/Engine/ReplacementVideoProbe.cs`; `FfmpegConverter.Core/Engine/Converter.cs`; `FfmpegConverter.Core/Policy/AudioModePolicy.cs`; `FfmpegConverter.Tests/InputVideoInfoTests.cs`; `FfmpegConverter.Tests/MuxPolicyTests.cs`; `FfmpegConverter.Tests/AudioModePolicyTests.cs`; `FfmpegConverter.Tests/MuxPipelineTests.cs`.

**Phase E:** `FfmpegConverter.Core/Engine/AddTrackContract.cs`; `FfmpegConverter.Core/Engine/Converter.cs`; `FfmpegConverter.Core/Engine/MuxPipeline.cs`; `FfmpegConverter.Core/Policy/AudioModePolicy.cs`; `FfmpegConverter.Tests/AddTrackContractTests.cs`; `FfmpegConverter.Tests/MuxPipelineTests.cs`.

**Phase F:** `FfmpegConverter.Core/Engine/MuxSourcePreservation.cs`; `FfmpegConverter.Core/Engine/MuxSourceMetadata.cs`; `FfmpegConverter.Core/Engine/CommandBuilder.cs`; `FfmpegConverter.Core/Engine/Converter.cs`; `FfmpegConverter.Core/Engine/MuxPipeline.cs`; `FfmpegConverter.Core/Policy/AudioModePolicy.cs`; `FfmpegConverter.Tests/MuxSourcePreservationTests.cs`; `FfmpegConverter.Tests/CommandBuilderTests.cs`.

**Phase G:** `FfmpegConverter.Core/Engine/MuxSourcePreservation.cs`; `FfmpegConverter.Core/Engine/Converter.cs`; `FfmpegConverter.Core/Engine/MuxPipeline.cs`; `FfmpegConverter.Tests/MuxSourcePreservationTests.cs`.

**Phase H:** `FfmpegConverter.Core/Engine/AppleM4vPipeline.cs`; `FfmpegConverter.Core/Engine/MuxPipeline.cs`; `FfmpegConverter.Tests/AppleM4vPipelineTests.cs`; `FfmpegConverter.Tests/MuxPipelineTests.cs`.

**Phase I:** `FfmpegConverter.Core/Engine/MuxSourcePreservation.cs`; `FfmpegConverter.Core/Engine/CommandBuilder.cs`; `FfmpegConverter.Core/Engine/Converter.cs`; `FfmpegConverter.Core/Engine/MuxPipeline.cs`; `FfmpegConverter.Core/Engine/AppleM4vPipeline.cs`; `FfmpegConverter.Core/Policy/MuxPolicy.cs`; related Mux policy, source-preservation, and M4V tests.

**Phase J:** `FfmpegConverter.Core/Engine/MuxOutputVerification.cs`; `FfmpegConverter.Core/Engine/MuxPipeline.cs`; `FfmpegConverter.Core/Engine/AppleM4vPipeline.cs`; `FfmpegConverter.Tests/MuxOutputVerificationTests.cs`.

**Phase K:** `FfmpegConverter.Core/Engine/HqAdapter.cs`; `FfmpegConverter.Core/Engine/HqWorker.cs`.

**Phase L:** `FfmpegConverter.Core/Policy/AudioModePolicy.cs`; `FfmpegConverter.Cli/CommandLineParser.cs`; `FfmpegConverter.Cli/Program.cs`; `FfmpegConverter.Gui/MainWindow.axaml.cs`; `FfmpegConverter.Tests/AudioModePolicyTests.cs`; `FfmpegConverter.Tests/CommandLineParserTests.cs`.

**Phase M:** `FfmpegConverter.Cli/CommandLineParser.cs`; `FfmpegConverter.Core/Engine/AppleM4vPipeline.cs`; `FfmpegConverter.Core/Engine/Converter.cs`; `FfmpegConverter.Core/Engine/MuxSourcePreservation.cs`; `FfmpegConverter.Core/Policy/MuxPolicy.cs`; `FfmpegConverter.Gui/MainWindow.axaml.cs`; `FfmpegConverter.Tests/AppleM4vPipelineTests.cs`; `FfmpegConverter.Tests/CommandLineParserTests.cs`.

## 5. Policy parity status

**Status: PASS**

Pure tests cover mode parsing, defaults, container/extension selection, audio constraints, replacement codec classification, audio track selection, preservation actions, subtitle compatibility, FFmpeg 8.1/9.0 profiles, and mixed-source Copy availability. General one-pass modes are PCM, AAC, and AAC+AC3; Copy is excluded. Mux M4V defaults to AAC+AC3 and accepts AAC-only; HQ ProRes is PCM-only.

## 6. Container parity status

**Status: PARTIAL**

Policy decisions and output extensions are covered. Mux M4V is routed through replacement-track workflow rather than the Direct M4V branch. MKV, MOV, and M4V production paths consume explicit source/replacement ownership and preservation decisions, with typed final verification; real-media output verification remains.

## 7. Preservation parity status

**Status: PARTIAL**

The typed preservation plan now carries audio selection, video replacement ownership, per-container subtitle compatibility, chapters, global and stream metadata, language, default/forced dispositions, and attachment actions to preparation/finalization commands. MKV preparation and MOV/M4V finalizers map only policy-selected streams; mkvmerge restricts the replacement input to video, keeps source auxiliaries isolated, and applies source metadata explicitly. M4V attachment dropping now matches the native policy. Real-media stream ownership and dispositions remain unverified.

## 8. HQ parity status

**Status: PARTIAL**

HQ audio availability uses the shared policy and rejects non-PCM HQ ProRes before execution. HQ-generated tracks use the same validated Add Track request, policy, output verification, and assembly path as ordinary Mux. Cancellation requests a graceful stop, then native kill, and finally kills the worker process tree if necessary; native HQ runtime and encoder-output tests remain unavailable.

## 9. Direct M4V parity status

**Status: PARTIAL**

Direct M4V remains a separate workflow selected outside the Mux group. Its dedicated dialog, explicit language, track indices, AC3 bitrate, chapter option, and defaults remain separate from Mux M4V, which uses the native autonomous `rus` language default. Direct output now uses the required fixed `libfdk_aac`, preserves supported subtitles, chapters, and metadata, and applies edit-before-mux through a separate source-conversion option set with fresh-output checks and success-only intermediate cleanup. The CLI rejects edit-before-mux because its command line does not provide the separate source-conversion settings. Real-media output, disposition, and edit-before-mux cleanup validation remain.

## 10. CLI and Avalonia parity status

**Status: PARTIAL**

CLI parsing and Core validation use the shared availability resolver. Before execution, CLI probes the replacement video and validates its codec against the selected Mux policy; the parser also rejects Mux M4V when MP4Box is unavailable. The interactive menu and Avalonia choices use policy order/defaults and filter Copy from one-pass contexts; Avalonia probes queued sources and the replacement track to refresh choices, preserves a still-valid audio selection, and disables Start for invalid replacement and missing-tool states. GUI interaction automation and all invalid replacement-state combinations remain to be verified.

## 11. Test results

**Status: PASS**

After phase M, `dotnet build FfmpegConverter.slnx --no-restore` succeeded with zero warnings and errors; the full test suite passed 278/278.

## 12. Real-media results

**Status: NOT TESTED**

The audit baseline contains real-media observations, but no post-implementation real-media matrix has been run yet.

## 13. Cross-implementation comparison

**Status: NOT TESTED**

No post-implementation semantic comparison of matching C and .NET fixture outputs has been run yet. Byte-for-byte output comparisons are not planned.

## 14. WSL validation status

**Status: NOT AVAILABLE**

The current development environment is macOS; target Windows hardware through Linux/WSL is not available in this session.

## 15. Windows validation status

**Status: NOT TESTED**

Native Windows release validation is intentionally deferred until the non-Windows implementation and parity matrix are functionally complete.

## 16. Remaining known gaps

- Real-media output verification and end-to-end Add Track failure/cancellation injection remain.
- MKV source preservation and replacement isolation need real-media verification; M4V AAC-only, dual-audio, default/forced dispositions, and dropped attachments need real-media stream-count tests.
- Failure cleanup, cancellation/process termination, and queue behavior need coverage.
- Direct M4V real-media output, chapter-disabled behavior, and edit-before-mux end-to-end cleanup need verification; Avalonia interaction, cross-implementation fixtures, WSL hardware preparation, and Windows packaged release validation remain.

## 17. Commit hashes and messages

| Commit | Message |
|---|---|
| `f9eec6f` | Add typed .NET mux policy |
| `31daad5` | Enforce shared .NET audio mode policy |
| `6bbbcb9` | Make catalog and tool discovery deterministic |
| `ffaad1b` | Validate mux replacement video before preparation |
| `b26cf42` | Add shared Add Track contract |
| `294c747` | Preserve MKV Add Track source streams |
| `2b1b14f` | Preserve MOV mux streams and metadata |
| `84ad44e` | Match Mux M4V audio and preservation policy |
| `e5f67ca` | Centralize mux stream preservation decisions |
| `53c62fc` | Verify mux outputs and stage final files |
| `0505ff0` | Harden HQ worker cancellation handoff |
| `e682846` | Integrate .NET CLI and GUI mux policy |
| `72d95b3` | Align .NET Direct M4V workflow with C |

## 18. Confirmation that C and Pascal remain unchanged

**Status: PASS**

No C, Objective-C, CMake, native resource, or Pascal file was modified. The tracked-file hash inventory outside `dotnet-experiment/` has been verified after each implementation phase commit.
