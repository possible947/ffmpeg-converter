# Comprehensive Statistical Analysis and Validation of Free Pascal Code

**Date:** 2026-04-26  
**Scope:** All `.pas` files in `fpc/`  
**Analysis performed by:** Copilot automated code review

---

## Repository Structure Overview

**35 Pascal source files** total:
- `fpc/cli/` — 4 files (args, menu, callbacks, progress)
- `fpc/common/` — 5 files (fs_utils, path_utils, process_utils, time_utils, tool_paths)
- `fpc/converter/` — 6 files (types, api, core, cmd_builder, analysis, runner) + apple_m4v_creator + mux_postprocess
- `fpc/platform/` — 5 files (linux_probe, windows_probe, windows_file_utils, windows_utf8, windows_mkvmerge)
- `fpc/gui/` — 4 files (form, form_windows, vulkan_device_selector, backup)
- `fpc/json/` — 1 file (loudnorm_json)
- `fpc/test/` — 9 test files
- Entry points: 2 CLI `.lpr` files, 1 library `.lpr`, 2 GUI `.lpr` files

---

## Analysis Task 1: Syntax & Compilation Verification

### CRITICAL-1 — `form_windows.pas`: Missing Function Signature for `ProbeEncoder`
**File:** `fpc/gui/form_windows.pas`, **Lines 52–64**

The function body that appears after line 51 (`begin...end` block with `Cmd`/`R` variables and Windows-specific logic) is an orphaned implementation — it has no `function` keyword or name. The unit calls `ProbeEncoder(Bin, 'h264_nvenc')` at lines 105–108, but no function `ProbeEncoder(const FfmpegBin, EncoderName: string): Boolean` is declared either in the `interface` or `implementation` section. The file contains:

```pascal
var
  Cmd: string;
  R: TRunResult;
begin
{$IFDEF Windows}
  Cmd := '"' + FfmpegBin + '"...
  R := RunCommandCapture(Cmd);
  Result := R.ExitCode = 0;
{$ELSE}
  Result := False;
{$ENDIF}
end;
```

...at lines 52–64, floating between the `interface` and the legitimate `ProbeVulkanDeviceCount` function. This is a **compilation-fatal syntax error** — the function heading `function ProbeEncoder(...)` is entirely absent. The unit cannot compile as written. All callsites (`DetectWindowsHardware`, lines 105–108) reference an undeclared identifier.

### CRITICAL-2 — `converter_runner.pas`: Hardcoded `/bin/sh` Makes Windows Builds Non-Functional
**File:** `fpc/converter/converter_runner.pas`, **Lines 192–196**

```pascal
P.Executable := '/bin/sh';
P.Parameters.Add('-c');
```

`RunEncode` creates its own `TProcess` with `/bin/sh` unconditionally, without any `{$IFDEF}` guard. On Windows `/bin/sh` does not exist. The Windows-compatible path (`cmd.exe /c`) used in `process_utils.pas::RunCommandCapture` (lines 171–172) is NOT replicated here. Since `RunEncode` is called from `converter_core.pas::converter_process_files`, every encoding operation on Windows would immediately fail with a process launch error. This affects both the Windows CLI and Windows GUI.

**Contrast:** `process_utils.pas::RunCommandCapture` correctly guards with `{$IFDEF Windows}` (line 171).

### HIGH-3 — `{$IFDEF}` Pairing: Nested Platform Guards
**File:** `fpc/cli/cli_args.pas`, **Lines 28–50, 113–131**

The nested `{$IFDEF Windows}` / `{$ELSE}` / `{$IFDEF Linux}` / `{$ELSE}` / `{$ENDIF}` / `{$ENDIF}` pattern is syntactically valid in FPC (the outer `{$ELSE}` corresponds to outer `{$IFNDEF Windows}`). However, the `{$ELSE}` at line 47 matches `{$IFDEF Linux}` inside the outer `{$ELSE}`, providing a macOS/other fallback. This triple-nesting is fragile but compiles correctly. Verified: all `{$IFDEF}` blocks are properly paired.

### INFO-4 — Duplicate Local Helper Functions
`ArrToStr` and `SetAnsiField` are each defined independently in at least 5 units (`converter_core.pas`, `converter_cmd_builder.pas`, `mux_postprocess.pas`, `cli_args.pas`, `cli_menu.pas`, `form.pas`, and both `.lpr` entry points). Pascal unit scoping makes these non-conflicting, but the duplication is a maintenance burden. No compilation error results.

---

## Analysis Task 2: Logic & Flow Analysis

### HIGH-5 — `loudnorm_json.pas`: Memory Leak on Early Exit
**File:** `fpc/json/loudnorm_json.pas`, **Lines 49–75**

```pascal
J := nil;
try
  J := GetJSON(Text);
  if not (J is TJSONObject) then
    Exit;           // ← J assigned, but J.Free at line 74 is bypassed
  ...
  Result := True;
except
  Result := False;
end;
J.Free;             // ← NOT in a finally block; skipped by Exit
```

`Exit` from within a `try..except` block (not `try..finally`) causes the post-block code (`J.Free`) to be skipped. Every call where `GetJSON(Text)` succeeds but the result is not a `TJSONObject` leaks the allocated `TJSONData` object. The correct pattern requires `try..finally`. Compare with `apple_m4v_creator.pas::BuildChapterText` (lines 257–315) which correctly uses `try..finally` with `J.Free` in the finally clause.

### MEDIUM-6 — `cli_menu.pas`: Incorrect Back-Navigation for Skip-Ahead Codecs
**File:** `fpc/cli/cli_menu.pas`, **Lines 553–558 (Step 4), 657–661 (Step 6)**

The menu flow:
- Codec 'copy' or VAAPI: Step 1 → Step 4 (audio norm), skipping Steps 2 (profile) and 3 (deblock)
- Codec 'prores'/'prores_ks': Step 1 → Step 2 → Step 3 → Step 4

At **Step 4**, pressing 'b' unconditionally goes to Step 3 (deblock):

```pascal
else if (Ch = 'b') or (Ch = 'B') then
  Step := 3    // ← always back to deblock
```

If the user selected 'copy' or a VAAPI codec, they jumped from Step 1 directly to Step 4, never visiting Step 3. Pressing 'b' would unexpectedly show the deblock menu. The correct back-target for skip-codecs should be Step 1. Same issue exists in **Step 6** (audio output): pressing 'b' always goes to Step 5 (genre), but genre is only shown when AudioNorm=5. If AudioNorm≠5, the user goes Step 4→Step 6 directly, and 'b' reveals an unvisited Step 5.

This doesn't break functionality (defaults remain valid) but confuses the user experience.

### MEDIUM-7 — `converter_core.pas`: `ERR_SKIP_FILE` Not Propagated as Batch Error
**File:** `fpc/converter/converter_core.pas`, **Lines 356–494**

When `converter_process_files` processes a batch, errors per-file (ERR_INPUT_NOT_FOUND, ERR_FFMPEG_FAILED, etc.) are reported via callbacks but the function always returns `ERR_OK` (line 499), regardless of how many files failed encoding. The main programs in both `.lpr` files check `if Err <> ERR_OK then Halt(1)` — but since individual file failures don't propagate to the top-level return, the process exits 0 even when all files failed encoding. This matches the C CLI behavior (each file error is logged but the batch proceeds), so it is by design — but worth documenting.

### HIGH-8 — `converter_core.pas`: `StopFlag` Lacks Memory Barrier
**File:** `fpc/converter/converter_core.pas`, **Lines 362–364, 383–384, 502–507**

`StopFlag: LongInt` is a field in `TConverterObj`, read from the background thread in `converter_process_files` and written from the GUI thread via `converter_stop`. There is no synchronization primitive (critical section, atomic, fence). On x86/x64, naturally aligned 32-bit reads/writes are atomic in practice, but on ARM targets (which FPC increasingly supports) and without a compiler barrier, the compiler might cache `StopFlag^` in a register, causing the stop signal to never be seen. This is technically undefined behavior under both Pascal and C memory models.

### MEDIUM-9 — `fs_utils.pas`: Windows `DirWritable` Does Not Check Write Access
**File:** `fpc/common/fs_utils.pas`, **Lines 58–60**

```pascal
{$IFDEF Windows}
  Result := (Path <> '') and DirectoryExists(Path);
```

Linux path correctly checks `fpAccess(PChar(Path), W_OK)`. Windows only checks existence. A read-only directory (e.g., `C:\Windows\System32`) would return `True`. The same weakness exists in `EnsureOutputDirWritable` (lines 124–147): the Windows branch verifies the directory exists and can be created, but never asserts `W_OK`-equivalent access. The Linux branch (lines 178–183) does check write access with `fpAccess`.

---

## Analysis Task 3: Cross-Platform Consistency

### HIGH-10 — Menu Step Count Discrepancy: Mux Skips 5 of 12 Steps
**Files:** `fpc/cli/cli_menu.pas`, **Lines 298–806**

Both Linux and Windows share the same `RunMenu` function. The loop condition is `while (Step <> 12) and (Step <> 0)`. For mux codec (Codec=4):
- Step 1 (codec) → Step **8** (output dir), skipping Steps 2, 3, 4, 5, 6, 7
- Step 8 → Step 9 (file list) → Step 10 (video track) → Step 11 (commit) → Step 12

Mux mode silently uses defaults for audio normalization, genre, audio output, and overwrite — these settings cannot be changed in mux mode. This may surprise users who expected to configure audio processing.

### MEDIUM-11 — Linux `--vk_device` Flag Parsed on Windows Without Documentation
**File:** `fpc/cli/cli_args.pas`, **Lines 129–131 (PrintUsage), 289–303 (ParseArgs)**

`PrintUsage` only shows `--vk_device` inside `{$IFDEF Linux}`. However, `ParseArgs` handles `--vk_device` without any `{$IFDEF}` guard (lines 289–303). On Windows, passing `--vk_device 1` is silently accepted and sets `Opts.vulkan_device := 1`, but it's not listed in the help output. This inconsistency is harmless but undocumented.

### INFO-12 — Clear Screen Escape Sequences Are Consistent
Both `cli_menu.pas::ClearScreen` (line 40: `Write(#27'[H'#27'[J')`) and `cli_args.pas::PrintSummary` (line 380: `Write(#27'[1;1H'#27'[2J')`) use VT100 escape codes. Both Linux and Windows CLIs share these — they work on Windows 10+ with VT100 support enabled, but may produce garbage on older Windows consoles.

---

## Analysis Task 4: Parity with C CLI Reference

### HIGH-13 — Pascal Linux Codec List Is a Strict Subset of C Linux Codec List
**Files:** `fpc/cli/cli_args.pas` (lines 44–46) vs `src/cli/platform/cli_linux.c` (lines 57–146)

C Linux codec support (dynamically probed):
```
copy, prores, prores_ks, mux, h264_vaapi*, hevc_vaapi*, h264_nvenc*, hevc_nvenc*,
h264_amf*, hevc_amf*, h264_qsv*, hevc_qsv*, prores_ks_vulkan*, m4v*
(* = if hardware detected)
```

Pascal Linux codec support (static):
```pascal
Result := (Codec = 'copy') or (Codec = 'prores') or (Codec = 'prores_ks') or
          (Codec = 'mux') or (Codec = 'h264_vaapi') or (Codec = 'hevc_vaapi');
```

Pascal does not support `h264_nvenc`, `hevc_nvenc`, `h264_amf`, `hevc_amf`, `h264_qsv`, `hevc_qsv`, `prores_ks_vulkan`, or `m4v` on Linux, even if hardware is present. The C CLI dynamically adds these based on runtime probing. The Pascal CLI has a hardcoded subset — a significant feature gap.

### MEDIUM-14 — `PrintUsage` Format Differs Between Pascal and C
**Files:** `fpc/cli/cli_args.pas` (lines 105–139) vs `src/cli/cli_common.c` (lines 74–137)

- C's `print_usage` dynamically generates the codec list from the platform's runtime detection.
- Pascal's `PrintUsage` has hardcoded, platform-guarded codec lists.
- C shows `Mux mode:` section with bullet points (lines 110–114); Pascal does not.
- C shows `Apple M4V options` section if supported (lines 116–129); Pascal does not.
- C shows `--vk_device <N>` with the default device index (lines 103–106); Pascal shows it unconditionally on Linux (line 129).

### MEDIUM-15 — `VerifyAndCompactFiles` Logic Is Pascal-Specific
**File:** `fpc/cli/cli_args.pas`, **Lines 440–561**

The C CLI (`cli_common.c`) has a `verify_compact_files` equivalent using `platform_file_is_regular_readable()` (one stat + one access). The Pascal implementation uses platform-specific code:

- Windows path (lines 458–490): `FileExists` → `FileRegular` → `FileReadable` — three separate calls
- Linux path (lines 492–525): single `fpStat` → `FPS_ISREG` → `fpAccess` — one stat + one access

Both produce equivalent semantics but the Linux path is more efficient. Behaviorally equivalent to C reference.

### HIGH-16 — `--audio-output` Silent Legacy Remapping Not in C
**File:** `fpc/cli/cli_args.pas`, **Lines 242–245**

```pascal
if S = 'fdk_aac_q2' then
  S := 'fdk_aac_q5'
else if S = 'fdk_aac_q2_ac3_640' then
  S := 'fdk_aac_q5_ac3_640';
```

The C CLI (`cli_common.c`) does not perform this alias mapping. Users who rely on `--audio-output fdk_aac_q2` on the Pascal CLI will get silent remapping to `fdk_aac_q5`, while on the C CLI this would be an unrecognized value. Behavioral divergence between Pascal and C CLI.

---

## Analysis Task 5: Windows-Specific Implementation

### HIGH-17 — `windows_probe.pas` Uses Text Grep vs `form_windows.pas` Uses Test Encoding
**Files:** `fpc/platform/windows_probe.pas` (lines 26–88) vs `fpc/gui/form_windows.pas` (lines 52–64)

`windows_probe.pas` (`IsNVENCAvailable`, `IsAMFAvailable`, `IsQSVAvailable`, `IsVulkanAvailable`): checks for codec availability using `ffmpeg -encoders 2>&1 | findstr /i <name>`. If ffmpeg lists a codec (even for an unrelated encoder), findstr would match. For example, if ffmpeg is built with h264_amf listed but no AMD GPU is present, `findstr /i amf` still returns exitcode 0. These functions are used from `cli_args.pas::IsCodecAllowedOnCurrentPlatform`.

`form_windows.pas::ProbeEncoder` (if it compiled): performs an actual test encoding with `-vframes 1 -f null NUL` to verify the encoder is genuinely usable.

This means the CLI and GUI use different detection strategies that can give inconsistent results: CLI might report a codec as available when it will fail at runtime.

### CRITICAL-18 — `form_windows.pas` Cannot Compile (Restatement of CRITICAL-1)
**File:** `fpc/gui/form_windows.pas`, **Lines 52–64**

As established: the `ProbeEncoder` function body exists without its function declaration. This is a compilation error for any build that includes `form_windows.pas`.

### HIGH-19 — `windows_utf8.pas::GetUTF8Arguments` Is Defined But Never Called
**Files:** `fpc/platform/windows_utf8.pas`, `fpc/cli/ffmpeg_converter_windows.lpr`

`ffmpeg_converter_windows.lpr` imports `windows_utf8` (line 13) but never calls `GetUTF8Arguments()`. All argument handling in `ParseArgs` uses `ParamStr(I)` which on Windows goes through the ANSI code page, not UTF-8. Even though the entry point sets `SetConsoleCP(CP_UTF8)` and `SetConsoleOutputCP(CP_UTF8)`, `ParamStr()` internally calls `GetCommandLineA()` which uses the system ANSI CP. Files with Unicode characters (e.g., Cyrillic or CJK) in their paths would fail to be found.

`GetUTF8Arguments()` was written to solve exactly this problem (using `CommandLineToArgvW` + `WideCharToMultiByte(CP_UTF8)`) but is dead code. This is a **functional regression** for Windows Unicode filenames.

### MEDIUM-20 — `windows_mkvmerge.pas` Non-Windows Path Is Redundant
**File:** `fpc/platform/windows_mkvmerge.pas`, **Lines 54–63**

The `{$ELSE}` branch (non-Windows) in `FindMkvmergeBin` uses `command -v mkvmerge 2>/dev/null`. This duplicates logic that already exists in `tool_paths.pas::ResolveMkvmergeBin`. Since `windows_mkvmerge.pas` is only imported on Windows, the non-Windows branch is never compiled for the intended target and represents unnecessary complexity.

### MEDIUM-21 — `windows_file_utils.pas` Functions Are Unused
**File:** `fpc/platform/windows_file_utils.pas`

Exports: `FileIsRegularReadable`, `DirIsWritable`, `EnsureOutputDirExists`. None of these appear to be called by any other Pascal unit — `fs_utils.pas` provides the equivalent functions. The unit is imported in `ffmpeg_converter_windows.lpr` (line 14) but contributes only dead code.

---

## Analysis Task 6: Mux Mode Implementation

### HIGH-22 — Windows Mux Lacks Output Stream Validation
**File:** `fpc/converter/mux_postprocess.pas`, **Linux: Lines 136–149, Windows: Lines 219–227**

Linux branch validates that the muxed output contains both video and audio:

```pascal
ProbeCmd := QuoteForShell(Tools.FfprobeBin) + ' -v error -show_entries stream=codec_type...';
CmdRes := RunCommandCapture(ProbeCmd);
if (CmdRes.ExitCode <> 0) or (Pos('video', CmdRes.OutputText) = 0) or
   (Pos('audio', CmdRes.OutputText) = 0) then
begin
  // cleanup and exit ERR_FFPROBE_FAILED
end;
```

Windows branch has no equivalent check. A partial mkvmerge output (missing one stream) would be silently accepted on Windows.

### HIGH-23 — Windows Mux Lacks Frame Rate Probing for Raw HEVC/H264
**File:** `fpc/converter/mux_postprocess.pas`, **Linux: Lines 88–110, Windows: n/a**

Linux branch probes `avg_frame_rate` from the intermediate file for raw `.hevc`/`.h265`/`.264`/`.h264` tracks and uses `--default-duration 0:FPSfps` in the mkvmerge command. This is essential for proper frame timing in the output MKV.

Windows branch omits this entirely. Raw bitstream tracks muxed on Windows will have incorrect or missing frame timing.

### MEDIUM-24 — Windows Mux Uses Unquoted `del` Command
**File:** `fpc/converter/mux_postprocess.pas`, **Lines 195, 213, 224**

```pascal
RunCommandCapture('del /f "' + TempOutputFile + '" 2>nul');
```

The filename is embedded with outer double-quotes but inner paths are not CMD-shell escaped. Windows paths with ampersands (`&`), parentheses, carets (`^`), or percent signs in the filename would cause `cmd.exe` to misparse the command. The Linux branch correctly uses `QuoteForShell` which escapes double-quotes within.

### INFO-25 — `mux_postprocess.pas` Fallback `'mkvmerge'` String Logic
**File:** `fpc/converter/mux_postprocess.pas`, **Lines 72–81** and `fpc/common/tool_paths.pas`, **Lines 247–253**

`ResolveMkvmergeBin` returns `'mkvmerge'` as a fallback when not found (tool_paths.pas line 252). Then `mux_postprocess.pas` checks:

```pascal
if (Tools.MkvmergeBin = '') or (Tools.MkvmergeBin = 'mkvmerge') then
  CmdRes := RunCommandCapture('command -v mkvmerge 2>/dev/null');
```

Since the fallback is `'mkvmerge'` (not `''`), the first condition `Tools.MkvmergeBin = ''` is always false. The logic correctly falls through to the `= 'mkvmerge'` check, which then does a secondary `command -v` probe. This is functionally correct but confusing — the fallback string was likely intended to trigger the secondary check.

---

## Analysis Task 7: Audio Processing

### INFO-26 — Audio Normalization Internal Names Are Consistent
**File:** `fpc/cli/cli_args.pas`, **Lines 226–231**

The CLI-to-internal mapping:

| CLI Arg | Internal Field |
|---------|---------------|
| `none` | `none` |
| `peak` | `peak_norm` |
| `peak2` | `peak_norm_2pass` |
| `loudnorm` | `loudness_norm` |
| `loudnorm2` | `loudness_norm_2pass` |

These match the internal string values used in `converter_core.pas` (lines 400, 425), `converter_cmd_builder.pas` (lines 113–124), and GUI `cmbAudioNorm` items (form.pas lines 844–848). Consistency is maintained across all three components.

### MEDIUM-27 — `converter_cmd_builder.pas` `fdk_aac_q5` Codec Mislabeled
**File:** `fpc/converter/converter_cmd_builder.pas`, **Lines 104–106**

```pascal
if AudioOut = 'fdk_aac_q5_ac3_640' then
  Result += '-c:a:0 aac -q:a:0 2 ...'
else if AudioOut = 'fdk_aac_q5' then
  Result += '-c:a aac -q:a 2 ...'
```

The option names reference `fdk_aac` (libfdk_aac encoder) but the actual FFmpeg filter used is `-c:a aac` (FFmpeg's native AAC encoder) with `-q:a 2`. The `_q5` suffix implies quality 5, but the actual quality parameter is `2`. The naming is misleading (fdk_aac in the name, native aac in the command). This is a documentation/naming inconsistency inherited from legacy naming.

### INFO-28 — Genre Targets Applied Only for `loudness_norm_2pass`
**File:** `fpc/converter/converter_core.pas`, **Lines 185–223**

`ApplyGenreTargets` is only called when `AudioNorm = 'loudness_norm_2pass'` (line 429). For `loudness_norm` (single-pass), hardcoded values (`-11 I / -1.5 TP / 7 LRA`) are used. The genre-based targets only affect the 2-pass workflow. This is by design and documented in the `PrintUsage` output.

---

## Analysis Task 8: GUI Integration

### HIGH-29 — `TConverterThread::Execute` TOCTOU Race with `StopClicked`
**File:** `fpc/gui/form.pas`, **Lines 644–687, 1196–1203**

```pascal
procedure TConverterThread.Execute;
begin
  FConverter := converter_create;
  ...
  converter_destroy(FConverter);
  FConverter := nil;          // ← (2) nil set AFTER destroy
end;

procedure TMainForm.StopClicked(Sender: TObject);
begin
  if Assigned(FWorker) and (FWorker.ConverterHandle <> nil) then
    converter_stop(FWorker.ConverterHandle);   // ← (1) race: read handle
end;
```

A concurrent `StopClicked` between `converter_destroy(FConverter)` and `FConverter := nil` would call `converter_stop` on an already-destroyed converter (freed memory). This is a time-of-check-time-of-use race. In practice the race window is very small, but it is technically undefined behavior.

### MEDIUM-30 — `UiComplete` Clears File List Unconditionally
**File:** `fpc/gui/form.pas`, **Lines 1423–1438**

`UiComplete` calls `lstFiles.Clear` (line 1437) on completion. This removes all file entries from the list even if some files failed processing. Users who want to retry failed files must re-add them manually.

### INFO-31 — GUI `SetRunningState` / `UpdateDependentWidgets` Interaction Is Correct
**File:** `fpc/gui/form.pas`, **Lines 1440–1470**

When `Running = False`, `SetRunningState` calls `UpdateDependentWidgets` (line 1469) which re-enables `cmbProfile`, `cmbDeblock`, `cmbGenre` based on current codec/audio selections. This is correct behavior. No inconsistency found.

### MEDIUM-32 — Apple M4V Thread Uses `/bin/rm` — Non-Portable to Windows
**File:** `fpc/converter/apple_m4v_creator.pas`, **Lines 169, 388, 415**

```pascal
procedure CleanupWorkDir(const WorkDir: string);
begin
  RunCommandCapture('/bin/rm -rf ' + QuoteForShell(WorkDir));
end;
```

Also at line 388: `RunCommandCapture('/bin/rm -f ' + QuoteForShell(M4VOut))`.

`apple_m4v_creator.pas` is imported unconditionally in `form.pas` (line 9), meaning it's compiled for all platforms. On Windows, `/bin/rm` doesn't exist. If the Apple M4V creator is triggered on Windows, the cleanup commands silently fail via `cmd.exe /c /bin/rm ...`, leaving temp directories and files behind. The `RunCommandCapture` return value is ignored in `CleanupWorkDir`.

---

## Analysis Task 9: Memory & Resource Management

### HIGH-33 — `loudnorm_json.pas`: `TJSONData` Leak (Detail)
**File:** `fpc/json/loudnorm_json.pas`, **Lines 56–75**

(See HIGH-5 above for full analysis.)

**Fix:** Replace `try..except` with `try..finally`:
```pascal
try
  J := GetJSON(Text);
  ...
finally
  J.Free;
end;
```

### MEDIUM-34 — `process_utils.pas::TryWriteTextFile`: Secondary `CloseFile` in Exception Handler
**File:** `fpc/common/process_utils.pas`, **Lines 75–98**

```pascal
try
  CloseFile(F);
except
end;
```

At line 93, if `Rewrite(F)` raised the exception (file open failed), the `TextFile` handle `F` is in an indeterminate state. `CloseFile(F)` on a file that was never successfully opened is implementation-defined in Pascal. FPC generally handles this gracefully (no crash), but the inner `except` silently swallows any secondary error. Low risk in practice.

### INFO-35 — Queue Pattern Handles `nil` GMainForm Correctly
**File:** `fpc/gui/form.pas`, **Lines 503–584**

All Queue* functions check `if Assigned(GMainForm)` before posting, and `Dispose(P)` in the else branch prevents leaks when called before the form is ready. Pattern is correct.

### MEDIUM-36 — `converter_runner.pas`: `FullOutput` Accumulates Unbounded
**File:** `fpc/converter/converter_runner.pas`, **Lines 186, 213–215**

```pascal
FullOutput := '';
...
FullOutput += Chunk;
```

For very long-running ffmpeg encodes, `FullOutput` grows indefinitely. It's only used for error logging on failure (line 237). For files that take hours to encode, `FullOutput` could consume many MB of memory. A reasonable mitigation would be to cap or truncate `FullOutput`. Low severity in typical usage.

### INFO-37 — `apple_m4v_creator.pas::BuildChapterText`: Object Pattern Is Safe
**File:** `fpc/converter/apple_m4v_creator.pas`, **Lines 263–270**

```pascal
with TStringList.Create do
try
  LoadFromFile(ChaptersJsonFile);
  JsonText := Text;
finally
  Free;
end;
```

The `with TStringList.Create do try...finally Free` pattern is safe — the object is freed in the `finally`. No leak here.

---

## Analysis Task 10: Error Reporting Consistency

### INFO-38 — `ERR_POPEN_FAILED` and `ERR_PCLOSE_FAILED` Are Dead Codes in Pascal
**File:** `fpc/converter/converter_types.pas`, **Lines 19–21**

`TConverterError` includes `ERR_POPEN_FAILED` and `ERR_PCLOSE_FAILED` (indices 10–11). These correspond to the C library's `popen()`/`pclose()` primitives. Pascal code uses `TProcess` (FPC's cross-platform process class) and never produces these error codes. The values appear in `ERR_STRINGS` (converter_core.pas lines 48–63) and the enum, but no Pascal code ever returns them. They are present for ABI compatibility with the C library's `ConverterError` enum.

### INFO-39 — Exit Codes Are Consistent (0=success, 1=error)
Both `.lpr` entry points use `Halt(0)` for success (help display) and `Halt(1)` for all error paths. The C CLI (`main.c`) similarly uses `exit(0)` and `exit(1)`. Consistent.

### MEDIUM-40 — `OnError` Callback Message Format Differs Between CLI and GUI
**Files:** `fpc/cli/cli_callbacks.pas` (line 59) vs `fpc/gui/form.pas` (line 621)

CLI:
```pascal
WriteLn('ERROR: ', string(text), ' (', string(converter_error_string(code)), ')');
```

GUI:
```pascal
QueueLog(Format('ERROR: %s (%s)', [string(text), string(converter_error_string(code))]));
QueueStatus('ERROR: ' + string(text));   // ← status omits error code
```

The GUI status label shows only the message text without the error code. The log list shows the full message. Minor inconsistency — harmless but notable.

---

## Summary Table

| ID | Severity | Location | Description |
|----|----------|----------|-------------|
| 1 | **CRITICAL** | `fpc/gui/form_windows.pas:52–64` | `ProbeEncoder` function body without declaration — compilation fatal |
| 2 | **CRITICAL** | `fpc/converter/converter_runner.pas:192–196` | Hardcoded `/bin/sh` in `RunEncode` — breaks all Windows encoding |
| 4 | INFO | Multiple units | Duplicate `ArrToStr`/`SetAnsiField` helpers — maintenance burden |
| 5 | **HIGH** | `fpc/json/loudnorm_json.pas:56–75` | `TJSONData` memory leak via `Exit` bypassing `J.Free` |
| 6 | MEDIUM | `fpc/cli/cli_menu.pas:553–558,657–661` | Back-navigation skips steps for skip-codec paths |
| 7 | MEDIUM | `fpc/converter/converter_core.pas:499` | Batch always returns `ERR_OK` despite individual file failures |
| 8 | **HIGH** | `fpc/converter/converter_core.pas:362–364` | `StopFlag` lacks memory barrier — unsafe on ARM |
| 9 | MEDIUM | `fpc/common/fs_utils.pas:58–60` | Windows `DirWritable` doesn't check write access |
| 10 | **HIGH** | `fpc/cli/cli_args.pas:44–46` | Pascal Linux codec list missing NVENC/AMF/QSV/Vulkan/M4V |
| 11 | MEDIUM | `fpc/cli/cli_args.pas:129,289–303` | `--vk_device` parsed silently on Windows |
| 12 | INFO | `fpc/cli/cli_menu.pas:40`, `fpc/cli/cli_args.pas:380` | VT100 escape codes: consistent, Windows 10+ only |
| 13 | **HIGH** | `fpc/cli/cli_args.pas` vs `src/cli/platform/cli_linux.c` | Pascal Linux missing 8 codecs vs C reference |
| 14 | MEDIUM | `fpc/cli/cli_args.pas:105–139` | `PrintUsage` lacks Mux/M4V sections present in C |
| 15 | MEDIUM | `fpc/cli/cli_args.pas:440–561` | `VerifyAndCompactFiles`: Windows uses 3 calls vs Linux/C 1 stat+access |
| 16 | **HIGH** | `fpc/cli/cli_args.pas:242–245` | Silent `fdk_aac_q2`→`fdk_aac_q5` alias not in C CLI |
| 17 | **HIGH** | `fpc/platform/windows_probe.pas` vs `fpc/gui/form_windows.pas` | CLI and GUI use different (inconsistent) codec detection strategies |
| 18 | **CRITICAL** | `fpc/gui/form_windows.pas:52–64` | Restatement of #1: compilation fatal |
| 19 | **HIGH** | `fpc/platform/windows_utf8.pas` + `fpc/cli/ffmpeg_converter_windows.lpr` | `GetUTF8Arguments` never called — Unicode filenames broken on Windows |
| 20 | MEDIUM | `fpc/platform/windows_mkvmerge.pas:54–63` | Non-Windows branch duplicates `tool_paths.pas` logic |
| 21 | MEDIUM | `fpc/platform/windows_file_utils.pas` | All exported functions are dead code |
| 22 | **HIGH** | `fpc/converter/mux_postprocess.pas:219–227` | Windows mux skips output stream validation |
| 23 | **HIGH** | `fpc/converter/mux_postprocess.pas` Windows branch | Windows mux skips frame rate probing for raw video |
| 24 | MEDIUM | `fpc/converter/mux_postprocess.pas:195,213,224` | Windows `del` command with unescaped paths |
| 25 | INFO | `fpc/converter/mux_postprocess.pas:72–81` | `MkvmergeBin = 'mkvmerge'` fallback logic correct but confusing |
| 26 | INFO | `fpc/cli/cli_args.pas:226–231` | Audio normalization internal names consistent across all components |
| 27 | MEDIUM | `fpc/converter/converter_cmd_builder.pas:104–106` | `fdk_aac_q5` name implies fdk encoder but uses native aac |
| 28 | INFO | `fpc/converter/converter_core.pas:185–223` | Genre targets applied only for loudness_norm_2pass — by design |
| 29 | **HIGH** | `fpc/gui/form.pas:1196–1203` | `StopClicked` TOCTOU race with `converter_destroy`/`FConverter := nil` |
| 30 | MEDIUM | `fpc/gui/form.pas:1437` | `UiComplete` clears file list including failed files |
| 31 | INFO | `fpc/gui/form.pas:1440–1470` | `SetRunningState`/`UpdateDependentWidgets` interaction is correct |
| 32 | MEDIUM | `fpc/converter/apple_m4v_creator.pas:169,388,415` | `/bin/rm` non-portable to Windows |
| 33 | **HIGH** | `fpc/json/loudnorm_json.pas:56–75` | Same as #5: `TJSONData` memory leak |
| 34 | MEDIUM | `fpc/common/process_utils.pas:75–98` | `CloseFile` in exception handler on unopened file |
| 35 | INFO | `fpc/gui/form.pas:503–584` | Queue pattern handles nil GMainForm correctly |
| 36 | MEDIUM | `fpc/converter/converter_runner.pas:213–215` | `FullOutput` grows unbounded for long encodes |
| 37 | INFO | `fpc/converter/apple_m4v_creator.pas:263–270` | `BuildChapterText` TStringList pattern is safe |
| 38 | INFO | `fpc/converter/converter_types.pas:19–21` | `ERR_POPEN_FAILED`/`ERR_PCLOSE_FAILED` unused in Pascal (ABI compat) |
| 39 | INFO | Both `.lpr` files | Exit codes consistent (0=success, 1=error) |
| 40 | MEDIUM | `fpc/cli/cli_callbacks.pas:59` vs `fpc/gui/form.pas:621` | `OnError` format differs between CLI and GUI status |

---

## Critical Findings Requiring Immediate Attention

1. **`fpc/gui/form_windows.pas`** cannot compile due to missing `ProbeEncoder` function declaration — the entire Windows GUI build is broken.

2. **`fpc/converter/converter_runner.pas::RunEncode`** hardcodes `/bin/sh` without platform guard — the Windows CLI and GUI encoding pipeline silently fails on all Windows targets.

3. **`fpc/platform/windows_utf8.pas::GetUTF8Arguments`** is imported but never called — Windows CLI cannot correctly handle Unicode filenames in paths.

4. **`fpc/json/loudnorm_json.pas`** leaks `TJSONData` objects on every non-object JSON parse result (needs `try..finally` instead of `try..except`).

5. **Pascal Linux CLI codec list** is static and misses hardware codecs (NVENC/AMF/QSV/Vulkan/M4V) that the C reference CLI detects and offers dynamically.
