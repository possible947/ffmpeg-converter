# Linux C → .NET parity-fix follow-up

**Status: PASS for the three confirmed gaps in the original behavioral audit.**
This follow-up supplements `parity-report.md` and the C-first audit at
`/home/viktor/.copilot/session-state/0057b5ab-6268-4366-b6e3-e180687f6ca4/files/c-to-dotnet-behavioral-parity.md`.
It does not revise other audit findings or broaden the tested scope.

## Tested revision and environment

| Item | Result |
|---|---|
| Source baseline | `e8599686d76445f0dcd82f9f9f0914649784a77e` |
| Tested .NET HEAD | `f61e07f7ee036ca5a92870c9af341f98502da95d` |
| Branch | `main` |
| Platform | Fedora 44, Linux x86_64 |
| .NET SDK | `10.0.112` |
| Release CLI | `dotnet-experiment/FfmpegConverter.Cli/bin/Release/net10.0/FfmpegConverter.Cli.dll`; SHA-256 `98dd0c0d38d6e29b5e9f378cb9ba5c4b668f4435c302fae43c546a87e861b076` |
| Conversion tools | Shared C Release FFmpeg, FFprobe, MKVToolNix, and MP4Box under `/tmp/ffmpeg-parity-audit/c-release/bin/` |
| HQ runtime root | `/home/viktor/LLM/Projects/ffmpeg-converter/dotnet-experiment/FfmpegConverter.Cli/bin/Release/net10.0/hq_converter` |
| C/Pascal scope guard | Tracked-file hash outside `dotnet-experiment/` remained `bc4df002bfe6bf39c051e1934c582dcdc89029d021761b06027963543032b2ea`; no C or Pascal file changed. |

The real-media inputs and per-run evidence are in `/tmp/ffmpeg-parity-audit/`
and `/tmp/ffmpeg-parity-fix/`. Outputs were independently probed with the same
FFprobe executable; files were not compared byte-for-byte.

## Findings and fixes

| Gap | Status | Implemented behavior and evidence |
|---|---|---|
| `fra` / `fre` language verification | **PASS** | Added a controlled, case-insensitive alias equivalence (`fra` ≡ `fre`) used by output verification. It does not equate either code with `eng`, accept a missing expected tag, or rewrite output metadata. French MKV cases M01–M04 and M4V cases A01–A03 completed and retained valid outputs. V01 MOV completed as well. |
| French subtitle tags in final MOV | **PASS** | Native C V01 output was re-probed and has no language tag on the converted French subtitle. The .NET verifier now requires that observed MOV-specific absence for a French-alias subtitle; it still rejects a present `eng` or `fre` tag. It does not alter M4V `und` output. |
| Corrupt sole-source Mux result | **PASS** | A single-source Mux preflight/probe failure is returned to the caller; N02 now exits nonzero (`9`, `FfprobeFailed`), does not print the successful-completion message, launches no FFmpeg/mkvmerge/post-mux tool, and leaves no intermediate or final artifact. This is deliberately scoped to a one-source Mux workflow. |
| Ordinary one-pass queue continuation | **PASS** | Q01–Q03 retain their prior behavior: valid+valid creates both files and exits 0; valid+corrupt retains the valid output and exits 0; corrupt+valid continues to the later valid output and exits 0. Focused tests cover both orderings. |
| HQ runtime discovery and worker lifecycle | **PASS** | A shared runtime-layout resolver validates the library, catalog, orchestrator config, and segmentator config. Parent and worker agree on absolute runtime/module/config/task paths; the worker receives the root and configuration environment before loading HQ. Initialization failures return a configuration error promptly, and the worker no longer blocks completion waiting for its console stop-reader. Successful jobs continue through the existing shared Add Track and Mux path. |

### HQ runtime validation

The runtime root above was found from the executable-adjacent Release layout
with `FFC_HQ_ROOT` unset. An additional Debug `dotnet run` used the declared
`FFC_HQ_ROOT` override to the same prepared runtime, validating the
repository/development-run path. Both runs completed x264/fullhd + PCM to a
60-second H.264/PCM MKV. The ProRes/hq + PCM run completed to a 60-second
ProRes/PCM MOV. FFprobe confirmed the expected container, video codec, PCM
audio, and duration for both. Successful runs removed their `task_1`
directories, and no HQ worker or runner remained afterward.

The missing-config regression deletes the orchestrator
`toolchain_runtime.ini` after catalog loading. The adapter returns a clear
configuration error in under five seconds without starting a worker; the
end-to-end Converter test verifies the error is reported, `task_1` is removed,
and no final output is created. No HQ runtime content was modified.

## Regression results

| Scenario group | Status |
|---|---|
| Language fixtures M01–M04, V01, A01–A03 | **PASS**; expected files exist and probe successfully. M4V output retains observed `und` language tags. |
| English controls E01, E05, E06, E07 | **PASS** |
| N02, Q02, Q03 | **PASS** |
| Policy controls S04, V02, A04, A05 | **PASS** |
| Subtitle controls SUB01–SUB04 | **PASS** |
| Special-path controls P01–P03 | **PASS** |
| HQ60-1 x264/fullhd + PCM and HQ60-2 ProRes/hq + PCM | **PASS** |
| Focused regression tests | **PASS**, 50 passed |
| Full Release build | **PASS**, 0 warnings and 0 errors |
| Full Release test suite | **PASS**, 295 passed, 0 failed, 0 skipped |
| Windows package, hardware encoding, distinct macOS `Resources` bundle layout | **NOT TESTED / OUT OF SCOPE** |

The selected policy, subtitle, path, language, queue, and HQ media checks used
the original audit's deterministic fixtures. Other behaviors from the original
audit were not reopened.

## Commits

| Change | Commit |
|---|---|
| Equivalent language-code verification and tests | `e410f255cbfa44a161363cbffbcb682d94f89562` |
| Single-source Mux failure propagation and queue regression tests | `d2fb78f2a37ce17de89ce133bac2b7f513075bd4` |
| HQ runtime discovery, worker lifecycle, and failure-cleanup tests | `f61e07f7ee036ca5a92870c9af341f98502da95d` |

Remaining audit items, platform packaging, GUI automation, hardware backends,
and native M4V language behavior remain outside this follow-up.
