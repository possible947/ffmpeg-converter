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
| G — MOV finalization and subtitle compatibility | PARTIAL | MOV remux explicitly maps video/audio, applies per-subtitle compatibility conversions, preserves chapters/metadata, uses MOV metadata tags, and warns when attachments are dropped. Real-media MOV PCM now succeeds and matches the C fixture's stream codecs, chapters, subtitle dispositions, and generated `bin_data` stream; broader platform coverage remains. Subtitle codec overrides now follow the global `-c copy` option so FFmpeg does not copy SRT into MOV. |
| H — Mux M4V finalization | PARTIAL | Mux M4V distinguishes AAC-only from AAC+AC3/default, requires libfdk_aac, preserves chapters/subtitles/metadata, assigns AAC-only default disposition, uses argument lists, and selects a plus-free MP4Box import directory. Real AAC, AAC+AC3, and default cases now pass on the fixture; AAC+AC3/default match C structurally. The AAC-only C output has an additional generated `GoPro MET` data stream not present in .NET, while both preserve the same chapter count. |
| I — Shared preservation plan | PARTIAL | MKV, MOV, and M4V source/finalization paths now consume one typed preservation plan for stream ownership, audio selection, subtitles, chapters, metadata, language, dispositions, attachments, and replacement video. M4V attachment dropping now matches native policy; real-media verification remains. |
| J — Output verification, errors, and cancellation | PARTIAL | Typed ffprobe snapshots verify openable containers, replacement video codec, audio/subtitle codecs and dispositions, chapters, attachments, and auxiliary-stream isolation. MOV/M4V finalizers stage output and promote only after verification. Real fixtures exposed native muxer behavior that output verification now reflects: M4V language tags may be `und`, first MOV/M4V subtitles may become default, and text remuxes may produce `bin_data`; injected failure/cancellation tests remain. |
| K — HQ handoff parity | PARTIAL | HQ output continues through the shared Add Track and typed policy path; cancellation now escalates from stop to worker kill and process-tree termination. Runtime encoder and native HQ integration tests remain unavailable. |
| L — CLI and Avalonia integration | PARTIAL | CLI probes and policy-validates replacement video before conversion; GUI audio choices refresh from current sources/replacement and policy selection, invalid Mux Add Track states disable Start, and Mux M4V checks MP4Box. GUI interaction automation remains. |
| M — Direct M4V workflow | PARTIAL | Direct M4V keeps its own dialog, language/defaults, uses fixed libfdk_aac, and now applies direct M4V metadata/subtitle preservation. A real .NET Direct M4V run preserved dual audio, subtitles, and chapters. Edit-before-mux runs the selected source conversion, verifies fresh intermediates, creates original-name M4V outputs, and cleans successful intermediates; edit-before-mux and native C cross-comparison remain unverified. |

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

Policy decisions and output extensions are covered. Mux M4V is routed through replacement-track workflow rather than the Direct M4V branch. MKV, MOV, and M4V production paths consume explicit source/replacement ownership and preservation decisions, with typed final verification. The tested MKV Copy/PCM, MOV PCM, and M4V AAC/AAC+AC3/default paths completed on a real fixture; other combinations and toolchain versions remain.

## 7. Preservation parity status

**Status: PARTIAL**

The typed preservation plan now carries audio selection, video replacement ownership, per-container subtitle compatibility, chapters, global and stream metadata, language, default/forced dispositions, and attachment actions to preparation/finalization commands. MKV preparation and MOV/M4V finalizers map only policy-selected streams; mkvmerge restricts the replacement input to video, keeps source auxiliaries isolated, and applies source metadata explicitly. The real fixture confirms MKV Copy/PCM preservation, M4V attachment dropping, and MOV/M4V subtitle conversions/dispositions. Mux M4V AAC-only still differs from native C by one generated chapter metadata data stream.

## 8. HQ parity status

**Status: PASS for the HQ → Mux handoff contract; actual HQ runtime is OUT OF SCOPE**

HQ audio availability uses the shared policy and rejects non-PCM HQ ProRes before execution. The production path reads the session video-track path, probes the generated replacement, resolves HQ policy, creates an `AddTrackRequest`, and calls `ExecuteAddTrackAsync`; that shared method validates the request and continues through ordinary source preparation and `MuxPipeline`. Contract tests cover x264 → MKV/PCM, x265 → MKV/AAC, and ProRes → MOV/PCM. No separate HQ mux implementation or final-container policy is applied after handoff. Actual HQ encoding/runtime parity was not rerun and is out of scope for this closure.

## 9. Direct M4V parity status

**Status: OUT OF SCOPE / NOT TESTED for Mux parity closure**

Direct M4V is an independent UI/settings and creation workflow, not Mux M4V. It was not tested in this closure and does not affect the Mux parity verdict. The earlier implementation-phase .NET Direct M4V run is historical only; no native C Direct M4V comparison was made.

## 10. CLI and Avalonia parity status

**Status: PARTIAL**

CLI parsing and Core validation use the shared availability resolver. Before execution, CLI probes the replacement video and validates its codec against the selected Mux policy; the parser also rejects Mux M4V when MP4Box is unavailable. The interactive menu and Avalonia choices use policy order/defaults and filter Copy from one-pass contexts; Avalonia probes queued sources and the replacement track to refresh choices, preserves a still-valid audio selection, and disables Start for invalid replacement and missing-tool states. GUI interaction automation and all invalid replacement-state combinations remain to be verified.

## 11. Test results

**Status: PASS**

The final closure build `dotnet build dotnet-experiment/FfmpegConverter.slnx --no-restore` succeeded with zero warnings and zero errors. The full suite `dotnet test dotnet-experiment/FfmpegConverter.slnx --no-build --no-restore` passed 282/282. This total includes the added HQ handoff cases and MOV audio auto-default regression coverage; earlier phase totals are historical.

## 12. Real-media results

**Status: PASS for the focused pre-Windows Mux matrix**

Generated a shared Matroska fixture with H.264 video, AAC audio, SRT subtitles, two chapters, global title/artist metadata, and a font attachment, plus a shared H.264 replacement. The focused C/.NET matrix used FFmpeg/ffprobe 9.0, `mkvmerge` 102.0, and GPAC/MP4Box 26.07. The three Mux M4V modes were also compared with the locally available FFmpeg/ffprobe 8.1 release binaries at `/Users/viktor/Git/ffmpeg_builder/workspace_81/release/`.

- One-pass ProRes with PCM, AAC, and AAC+AC3 completed in both; ffprobe audio/video codec lists matched. Invalid one-pass Copy was rejected by both.
- Mux MKV Copy and PCM completed in both; stream codecs, selected audio tracks, subtitle, attachment, and two chapters matched.
- Mux MOV PCM completed in both with matching video/audio/subtitle/data stream semantics, dispositions, and chapters. The shared fixture exposed FFmpeg's first-audio auto-default behavior; .NET output already matched C, and its verifier was corrected to accept that output.
- Mux M4V AAC-only completed in both with replacement video, exactly one AAC track, no AC3, compatible subtitle, two chapters, and matching expected metadata/language behavior. AAC+AC3 and implicit default each produced AAC default plus non-default AC3, with the compatible subtitle, chapters, and metadata preserved.
- Invalid Mux MOV Copy and M4V PCM/Copy were rejected by both. ffprobe JSON was saved independently for each valid output during the run.

The GoPro MET classification and exact C pipeline stage are recorded in the final closure section below. These results cover the exercised macOS toolchain and fixture, not every platform or media type.

## 13. Cross-implementation comparison

**Status: PASS for the exercised Mux cases**

The comparisons used independent ffprobe semantic summaries, not byte-for-byte output. The required audio structure, replacement-video isolation, supported subtitle behavior, chapter count, metadata, language/dispositions, and container validity matched for the exercised cases. The C-only chapter-remux data track is classified below as incidental tool output and is not a functional Mux contract requirement.

## 14. WSL validation status

**Status: NOT AVAILABLE**

The current development environment is macOS; target Windows hardware through Linux/WSL is not available in this session.

## 15. Windows validation status

**Status: NOT TESTED**

Native Windows release validation is intentionally deferred until the non-Windows implementation and parity matrix are functionally complete.

## 16. Remaining known gaps

- End-to-end Add Track failure/cancellation injection remains.
- Failure cleanup, cancellation/process termination, and queue behavior need coverage.
- Direct M4V and actual HQ encoder/runtime parity are out of scope for this Mux closure. Avalonia interaction, broader media/platform fixtures, WSL hardware preparation, native Windows packaged release, and successful HQ runtime media remain untested.

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

## 19. Final Mux parity closure

**Tested HEAD:** `5677714e84bc785a37a75289b785f92af3041e77`

### GoPro MET investigation

The same chapter-bearing source and replacement were used for C/.NET M4V AAC-only, AAC+AC3, and default runs. Independent ffprobe JSON was captured for each output. The native C AAC-only output contains an additional `data/bin_data` stream tagged `gpmd` with handler `GoPro MET`; it is absent from the original source and the Add Track prepared MKV.

The C AAC-only stage trace used FFmpeg 9.0, mkvmerge 102.0, and GPAC/MP4Box 26.07. The original source, prepared Add Track MKV, and replacement-video assembly MKV contain no GoPro MET stream. The video-only and AAC extraction files contain generic `data/bin_data` streams tagged `text` with handler `SubtitleHandler`, but not `gpmd`. The captured raw MP4Box output contains only replacement H.264 and AAC, with no data stream. The chapter-stage file contains the generic text data; the final FFmpeg metadata/subtitle remux maps that data and emits `data/bin_data`, tag `gpmd`, handler `GoPro MET`. Therefore the first actual GoPro MET stream is created at the final metadata/subtitle remux, while the generic text-data precursor is already present in extraction and chapter-stage intermediates.

The final C output contains one `gpmd`/GoPro MET stream in AAC-only, AAC+AC3, and default modes under both FFmpeg 8.1 and 9.0. It disappears when the source has no chapters, while removing source subtitles or global tags does not remove it. The behavior is therefore chapter/tool-stage dependent, not audio-mode or source-GoPro-data dependent.

**Classification: B. INCIDENTAL TOOL OUTPUT.** The application contract preserves chapter metadata and compatible source subtitles; it does not require FFmpeg's extra chapter-associated data stream or its GoPro handler/tag. .NET intentionally does not reproduce it. No .NET change was made for the MET difference.

### Remaining verification fix

The focused MOV PCM matrix initially showed that .NET produced the same MOV stream/disposition structure as C, but rejected its output: the MOV muxer auto-sets the first audio stream as default when the source has no default audio. The verifier had compared the final output directly to the source's unset flag. The .NET verifier now accounts for this MOV muxer behavior and has a regression test. This was a real conversion-success/verifier defect; no output-generation behavior was changed for it.

### Final statuses

| Area | Status | Evidence |
|---|---|---|
| Mux Policy parity | PASS | Shared resolver and current policy test suite. |
| Add Track parity | PASS | Shared validation and focused MKV/MOV/M4V runtime matrix. |
| MKV parity | PASS | Copy and PCM real-media results match. |
| MOV parity | PASS | PCM real-media result matches; auto-default verification is covered. |
| Mux M4V parity | PASS | AAC-only, AAC+AC3, and default have required audio structure and preservation behavior; incidental MET difference is excluded. |
| HQ → Mux handoff parity | PASS | Production HQ path probes replacement, resolves HQ policy, creates `AddTrackRequest`, and calls shared validation/preparation/Mux; x264/MKV, x265/MKV, and ProRes/MOV contracts are tested. |
| Actual HQ encoder/runtime parity | OUT OF SCOPE | No HQ encoding rerun required for handoff verification. |
| Direct M4V parity | OUT OF SCOPE / NOT TESTED | Independent workflow, not part of Mux closure. |
| Windows release validation | NOT TESTED | No Windows release environment used. |

The full focused matrix passed in C and .NET: one-pass PCM/AAC/AAC+AC3; invalid one-pass Copy; Mux MKV Copy/PCM; Mux MOV PCM and invalid Copy; Mux M4V AAC/AAC+AC3/default and invalid PCM/Copy. The final `dotnet build` completed with zero warnings/errors and the full suite passed **282/282**. C and Pascal remain unchanged. Remaining general gaps are failure/cancellation injection, broader media/platform coverage, Avalonia interaction automation, and Windows release validation.

**Final decision:** .NET Mux functional parity with native C is **COMPLETE for the pre-Windows scope**.
