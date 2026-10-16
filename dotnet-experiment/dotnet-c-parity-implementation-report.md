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

## 4. Files changed by phase

**Phase A:** `FfmpegConverter.Core/Policy/MuxPolicy.cs`; `FfmpegConverter.Tests/MuxPolicyTests.cs`.

**Phase B:** `FfmpegConverter.Core/Policy/AudioModePolicy.cs`; `FfmpegConverter.Core/Models/ConvertOptions.cs`; `FfmpegConverter.Core/Engine/CommandBuilder.cs`; `FfmpegConverter.Core/Engine/Converter.cs`; CLI `CliSummary.cs`, `CommandLineParser.cs`, `InteractiveMenu.cs`, `Program.cs`; GUI `MainWindow.axaml`, `MainWindow.axaml.cs`; related Core, CLI, and GUI tests.

**Phase C:** `FfmpegConverter.Core/Presets/PresetDb.cs`; `FfmpegConverter.Core/Probing/ToolDiscovery.cs`; `FfmpegConverter.Tests/PresetDbTests.cs`; `FfmpegConverter.Tests/ToolDiscoveryTests.cs`.

**Phase D:** `FfmpegConverter.Core/Engine/InputVideoInfo.cs`; `FfmpegConverter.Core/Engine/ReplacementVideoProbe.cs`; `FfmpegConverter.Core/Engine/Converter.cs`; `FfmpegConverter.Core/Policy/AudioModePolicy.cs`; `FfmpegConverter.Tests/InputVideoInfoTests.cs`; `FfmpegConverter.Tests/MuxPolicyTests.cs`; `FfmpegConverter.Tests/AudioModePolicyTests.cs`; `FfmpegConverter.Tests/MuxPipelineTests.cs`.

**Phase E:** `FfmpegConverter.Core/Engine/AddTrackContract.cs`; `FfmpegConverter.Core/Engine/Converter.cs`; `FfmpegConverter.Core/Engine/MuxPipeline.cs`; `FfmpegConverter.Core/Policy/AudioModePolicy.cs`; `FfmpegConverter.Tests/AddTrackContractTests.cs`; `FfmpegConverter.Tests/MuxPipelineTests.cs`.

**Phase F:** `FfmpegConverter.Core/Engine/MuxSourcePreservation.cs`; `FfmpegConverter.Core/Engine/MuxSourceMetadata.cs`; `FfmpegConverter.Core/Engine/CommandBuilder.cs`; `FfmpegConverter.Core/Engine/Converter.cs`; `FfmpegConverter.Core/Engine/MuxPipeline.cs`; `FfmpegConverter.Core/Policy/AudioModePolicy.cs`; `FfmpegConverter.Tests/MuxSourcePreservationTests.cs`; `FfmpegConverter.Tests/CommandBuilderTests.cs`.

**Phase G:** `FfmpegConverter.Core/Engine/MuxSourcePreservation.cs`; `FfmpegConverter.Core/Engine/Converter.cs`; `FfmpegConverter.Core/Engine/MuxPipeline.cs`; `FfmpegConverter.Tests/MuxSourcePreservationTests.cs`.

## 5. Policy parity status

**Status: PASS**

Pure tests cover mode parsing, defaults, container/extension selection, audio constraints, replacement codec classification, audio track selection, preservation actions, subtitle compatibility, FFmpeg 8.1/9.0 profiles, and mixed-source Copy availability. General one-pass modes are PCM, AAC, and AAC+AC3; Copy is excluded. Mux M4V defaults to AAC+AC3 and accepts AAC-only; HQ ProRes is PCM-only.

## 6. Container parity status

**Status: PARTIAL**

Policy decisions and output extensions are covered. Mux M4V is routed through replacement-track workflow rather than the Direct M4V branch. MKV and MOV production paths now implement their source/replacement ownership and preservation rules; M4V finalization and real-media output verification remain.

## 7. Preservation parity status

**Status: PARTIAL**

The pure subtitle decision table matches the C policy, including MOV text subtitle conversion to SRT for MKV and text-to-`mov_text` conversion for MOV. MKV Add Track source preparation probes and maps supported source subtitles, chapters, and attachments; MOV finalization probes the assembled MKV, applies per-subtitle compatibility policy, preserves chapters/metadata, and warns on dropped attachments. mkvmerge restricts the replacement input to video and applies source segment/video metadata explicitly. Real-media stream ownership and dispositions remain unverified.

## 8. HQ parity status

**Status: PARTIAL**

HQ audio availability uses the shared policy and rejects non-PCM HQ ProRes before execution. HQ-generated tracks now use the same validated Add Track request, policy, and assembly path as ordinary Mux; real HQ output verification remains.

## 9. Direct M4V parity status

**Status: PARTIAL**

Direct M4V remains a separate workflow selected outside the Mux group. Its dedicated dialog and language handling are unchanged. Mux M4V no longer shares Direct M4V selection routing; execution parity remains incomplete.

## 10. CLI and Avalonia parity status

**Status: PARTIAL**

CLI parsing and Core validation use the shared availability resolver. The interactive menu and Avalonia choices use policy order/defaults and filter Copy from one-pass contexts; Avalonia probes queued sources and the replacement track to refresh choices. GUI interaction automation and all invalid replacement-state combinations remain to be verified.

## 11. Test results

**Status: PASS**

After phase G, `dotnet build FfmpegConverter.slnx --no-restore` succeeded with zero warnings and errors; the full test suite passed 251/251. Focused MOV finalizer and stream-preservation tests passed 32/32.

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

- Final MOV/M4V output verification and end-to-end Add Track failure injection remain.
- MKV source preservation and replacement isolation need real-media verification; M4V still needs production preservation, subtitle, chapter, metadata, disposition, attachment, and replacement-isolation parity.
- Mux M4V AAC-only, dual-audio, and default behavior need a dedicated finalizer path and real-media tests.
- Failure cleanup, cancellation/process termination, and queue behavior need coverage.
- Direct M4V regression behavior, Avalonia interaction, cross-implementation fixtures, WSL hardware preparation, and Windows packaged release validation remain.

## 17. Commit hashes and messages

| Commit | Message |
|---|---|
| `f9eec6f` | Add typed .NET mux policy |
| `31daad5` | Enforce shared .NET audio mode policy |
| `6bbbcb9` | Make catalog and tool discovery deterministic |
| `ffaad1b` | Validate mux replacement video before preparation |
| `b26cf42` | Add shared Add Track contract |
| `294c747` | Preserve MKV Add Track source streams |

## 18. Confirmation that C and Pascal remain unchanged

**Status: PASS**

No C, Objective-C, CMake, native resource, or Pascal file was modified. The tracked-file hash inventory outside `dotnet-experiment/` has been verified after each implementation phase commit.
