# Deep Audit Report: FPC Codebase Optimization & Feature Usage

**Date:** 2026-03-12  
**Scope:** All `.pas` / `.lpr` files under `fpc/`  
**Focus:** FPC language features, data structures, object model, FFmpeg option usage

---

## 1. Sets for Parameter Values — Major Opportunity

### Current Problem: Integer-encoded enums matched by magic numbers

Throughout the codebase, codec, profile, deblock, audio normalization, and genre are stored either as **raw integers** or **fixed-size C char arrays** inside `TConvertOptions`, then compared via string equality or `case` on magic numbers.

**Examples of what's wrong:**

In `converter_types.pas`: `codec` and `audio_norm` are `array[0..31] of AnsiChar` — C-style strings. Every consumer calls `ArrToStr()` (duplicated 3 times across files) and then does chains of `if Codec = 'prores' then ... else if Codec = 'h265_mi50' then ...`.

In `cli_args.pas`: `profile`, `deblock`, `genre` are `LongInt` values (1..N). Every usage requires a mapping function (`ProfileToText`, `DeblockToText`, `GenreToText`) to go back to strings, and `case` statements to go from strings to integers.

In `cli_menu.pas`: Step 9 maps local integer variables back to string fields via `case` + `SetAnsiField`.

### Recommended Fix: Native FPC enumerated types + sets

```pascal
type
  TCodecType = (ctCopy, ctProRes, ctProResKS, ctH265MI50);
  TProfileType = (ptLT, ptStandard, ptHQ, pt4444);
  TDeblockType = (dbNone, dbWeak, dbStrong);
  TAudioNormType = (anNone, anPeakNorm, anPeakNorm2Pass,
                    anLoudnessNorm, anLoudnessNorm2Pass);
  TGenreType = (gtEDM, gtRock, gtHipHop, gtClassical, gtPodcast);
```

Then `TConvertOptions` becomes:
```pascal
TConvertOptions = record
  Codec: TCodecType;
  Profile: TProfileType;
  Deblock: TDeblockType;
  AudioNorm: TAudioNormType;
  Genre: TGenreType;
  // ... numeric fields stay the same
end;
```

**Benefits:**
- Eliminates all `ArrToStr()` calls and string comparisons in `converter_cmd_builder.pas`, `converter_core.pas`, `cli_args.pas`, `cli_menu.pas`
- `case` on enums gets compile-time exhaustiveness checking (FPC warns on missing branches)
- Eliminates 4 copies of `SetAnsiField` and 3 copies of `ArrToStr`
- Sets become possible for validation: `if Opts.Codec in [ctProRes, ctProResKS] then` instead of string matching

**Where sets pay off specifically:**

| Current code | Replacement |
|---|---|
| `if (Codec = 'prores') or (Codec = 'prores_ks') then` in `converter_cmd_builder.pas` | `if Opts.Codec in [ctProRes, ctProResKS] then` |
| `if (CodecText <> 'copy') and (CodecText <> 'h265_mi50')` in `form.pas` | `if Opts.Codec in [ctProRes, ctProResKS] then` |
| `if AudioNorm = 'loudness_norm_2pass'` repeated in `converter_core.pas`, `cli_args.pas` | `if Opts.AudioNorm = anLoudnessNorm2Pass then` (type-safe) |
| `if (Codec = 'copy') or (Codec = 'h265_mi50')` in `path_utils.pas` | `if Codec in [ctCopy, ctH265MI50] then` |

**Impact on C ABI:** The `converter_pas.lpr` shared library exports a C-compatible API. You'd keep the packed record with char arrays at the C boundary (`converter_api_c.pas`), and convert to native enums internally. This is already how `converter_api_c.pas` works — it's a thin adapter.

---

## 2. Replace JSON with Native Records/TList — `loudnorm_json.pas`

### Current state

`loudnorm_json.pas` uses `fpjson`/`jsonparser` to parse a small fixed-schema JSON blob from ffmpeg's loudnorm output. The only place JSON is used is to extract 5 float values from a known structure.

`apple_m4v_creator.pas` also uses `fpjson` + `jsonparser` to parse ffprobe chapter output.

### Recommendation

**For loudnorm:** The JSON is only 5 fields with known keys. You can replace `fpjson` with a simple line-by-line key-value parser (the JSON from ffmpeg loudnorm is always pretty-printed, one field per line):

```pascal
function TryParseLoudnormText(const Text: string; out Metrics: TLoudnormMetrics): Boolean;
// Parse lines like:   "input_i" : "-14.52",
// No fpjson dependency needed
```

This eliminates the `fpjson`/`jsonparser` unit dependency for the CLI and library targets (only the GUI truly benefits from `fpjson` for chapter parsing).

**Alternative: Use `TStringList` with `NameValueSeparator`** for the loudnorm case:
```pascal
var SL: TStringList;
SL := TStringList.Create;
SL.NameValueSeparator := ':';
SL.Text := CleanedJson;
Value := SL.Values['"input_i"'];
```

**Impact:** Reduces compile-time dependencies for non-GUI builds. The `fpjson` unit pulls in several other units.

**For chapters in apple_m4v_creator.pas:** The `fpjson` dependency is justified here because ffprobe JSON chapter output is a proper nested structure with arrays. Keep it.

---

## 3. Object Model Opportunities — Simplify with Classes

### 3.1 `TConverterObj` record → `TConverter` class

Current: `converter_core.pas` defines `TConverterObj` as a plain record allocated with `New()`/`Dispose()`, accessed via an opaque `Pointer`. Seven standalone procedures operate on this pointer with manual nil checks.

```pascal
type
  TConverterObj = record
    Opts: TConvertOptions;
    Cb: TConverterCallbacks;
    StopFlag: LongInt;
  end;
```

**Recommended:** Convert to a proper class:
```pascal
type
  TConverter = class
  private
    FOpts: TConvertOptions;
    FCb: TConverterCallbacks;
    FStopFlag: LongInt;
  public
    constructor Create;
    destructor Destroy; override;
    procedure SetCallbacks(const Cb: TConverterCallbacks);
    function SetOptions(const Opts: TConvertOptions): TConverterError;
    function ProcessFiles(Files: PPAnsiChar; Count: LongInt): TConverterError;
    procedure Stop;
  end;
```

**Benefits:**
- Eliminates 7 nil checks at function entry (`if c = nil then Exit`)
- Methods have direct access to fields — no `PConverterObj(c)^.` casting
- Constructor/destructor handle init/cleanup automatically
- `converter_api_c.pas` adapter layer stays unchanged — it just casts `Pointer` to `TConverter`

### 3.2 `TRunResult` record → could stay as-is

The `TRunResult` in `process_utils.pas` is simple enough as a record. No class benefit here.

### 3.3 `TAppleM4VOptions` record → could stay as-is

Simple value type, no behavior. Record is appropriate.

### 3.4 CLI Menu — State Machine Pattern

`cli_menu.pas` implements a 9-step menu as a single 400-line `while` loop with a `case Step of` dispatcher. Each step duplicates boilerplate: screen clear, header, options display, input reading, navigation.

**Recommended:** Extract a base class or record for menu steps:
```pascal
type
  TMenuStep = record
    Title: string;
    Options: array of string;
    DefaultIndex: Integer;
    Next: Integer;
    Prev: Integer;
  end;
```

This would reduce `RunMenu` from ~400 lines to ~80 lines with a data-driven loop processing an array of `TMenuStep` records.

### 3.5 Callback Marshalling in GUI — Could Use Variant Record

`form.pas` defines 6 nearly identical heap-allocated record types (`PLogData`, `PStatusData`, `PStageData`, `PProgressData`, `PFileBeginData`, `PFileEndData`) and 6 matching `Queue*` procedures and 6 matching `Async*` handlers. This is ~200 lines of boilerplate.

**Recommended:** Use a variant record:
```pascal
type
  TAsyncKind = (akLog, akStatus, akStage, akProgress, akFileBegin, akFileEnd);
  PAsyncData = ^TAsyncData;
  TAsyncData = record
    case Kind: TAsyncKind of
      akLog: (Msg: string);
      akStatus: (StatusText: string);
      // etc.
  end;
```

Or a single `QueueAsync(Kind, Data)` dispatcher. This would cut ~150 lines.

---

## 4. FFmpeg Option Usage — `-progress` Flag Audit

### 4.1 Current `-progress` Usage (BROKEN)

`converter_runner.pas` appends `-progress pipe:1 -nostats -nostdin 2>&1` to the encode command:

```pascal
function RunEncode(const CommandBase: string): TConverterError;
var R: TRunResult;
begin
  R := RunCommandCapture(CommandBase + ' -progress pipe:1 -nostats -nostdin 2>&1');
  if R.ExitCode <> 0 then Exit(ERR_FFMPEG_FAILED);
  Result := ERR_OK;
end;
```

**Problem:** `RunCommandCapture` uses `TProcess` with `poWaitOnExit` — it blocks until ffmpeg finishes, then returns all output at once. The `-progress pipe:1` output is **never parsed**. The progress callbacks in `converter_core.pas` only emit a synthetic 100% at the end.

### 4.2 How `-progress` Should Be Used

FFmpeg's `-progress` flag writes key=value pairs to the specified file/pipe:
```
frame=1234
fps=29.97
stream_0_0_q=2.0
bitrate=10234.5kbits/s
total_size=12345678
out_time_us=5000000
out_time_ms=5000000
out_time=00:00:05.000000
dup_frames=0
drop_frames=0
speed=1.5x
progress=continue
```

Followed by `progress=end` when done.

### 4.3 Recommended Implementation

Replace `RunCommandCapture` in `RunEncode` with a streaming reader:

```pascal
function RunEncodeWithProgress(const Cmd: string; Duration: Double;
  OnProgress: TOnProgressEncode): TConverterError;
var
  P: TProcess;
  Line: string;
  OutTime: Double;
  Fps: Single;
  BytesRead: LongInt;
  Buf: array[0..4095] of Byte;
  Accumulated: string;
begin
  P := TProcess.Create(nil);
  try
    P.Executable := '/bin/sh';
    P.Parameters.Add('-c');
    P.Parameters.Add(Cmd + ' -progress pipe:1 -nostats -nostdin 2>&1');
    P.Options := [poUsePipes, poStderrToOutput];
    P.Execute;
    
    Accumulated := '';
    repeat
      BytesRead := P.Output.Read(Buf, SizeOf(Buf));
      if BytesRead > 0 then
      begin
        Accumulated += Copy(PAnsiChar(@Buf[0]), 1, BytesRead);
        // Parse lines, extract out_time_us, fps
        // Calculate percent = out_time / duration * 100
        // Call OnProgress(percent, fps, eta)
      end;
    until BytesRead = 0;
    
    P.WaitOnExit;
    if P.ExitStatus <> 0 then Exit(ERR_FFMPEG_FAILED);
    Result := ERR_OK;
  finally
    P.Free;
  end;
end;
```

**Key points:**
- Remove `poWaitOnExit` from Options — read the pipe in a loop instead
- Parse `out_time_us=` lines to get current position
- Divide by total duration (from `ProbeDuration`, currently dead code!) to get percentage
- Parse `fps=` for display
- This also **fixes PROC-1** (the deadlock risk with large output)

### 4.4 Other FFmpeg Options Not Used Optimally

| FFmpeg feature | Current state | Recommendation |
|---|---|---|
| `-progress pipe:1` | Appended but output never parsed | Implement streaming reader (above) |
| `-vstats_file` | Not used | Alternative to `-progress` for video stats. `-progress` is better. |
| `-filter_complex` | Not used | Not needed for current pipeline |
| `-disposition` | Not used | Could set default audio track in M4V output |
| `-movflags +faststart` | Not used | **Should add** for MP4/M4V outputs — relocates moov atom for streaming |
| `-max_muxing_queue_size` | Not used | Consider adding (default 256) to prevent muxing failures on complex files |
| `-threads` | Not used | Could allow user control of encoding threads |
| `-loglevel` | Not used | Should add `-loglevel warning` or `-loglevel error` instead of combining stderr with `-progress` output |
| `-stats_period` | Not used | Controls how often stats are printed (default 0.5s). Useful with `-progress` |

---

## 5. Code Duplication Removable with Proper Units

| Duplicated function | Occurrences | Fix |
|---|---|---|
| `SetAnsiField` | `cli_args.pas`, `cli_menu.pas`, `form.pas`, `test_cli_mode_matrix.pas` — **4 copies** | Move to `converter_types.pas` (or eliminate entirely with enum refactor) |
| `ArrToStr` | `converter_core.pas`, `converter_cmd_builder.pas`, `cli_args.pas` — **3 copies** | Move to `converter_types.pas` (or eliminate with enum refactor) |
| `FormatEta` | `time_utils.pas`, `form.pas` — **2 copies** | Use the one from `time_utils.pas` in the GUI |

With the enum refactor from Section 1, both `SetAnsiField` and `ArrToStr` become **completely unnecessary**.

---

## 6. `TList` / `TStringList` Opportunities

### 6.1 File list: `array of PAnsiChar` → `TStringList`

The main program `ffmpeg_converter.lpr` allocates a fixed `array[0..4095] of PAnsiChar` and manually manages memory with `StrNew`/`StrDispose`. The menu and args modules also work with this fixed array.

**Recommended:** Use `TStringList`:
```pascal
var Files: TStringList;
Files := TStringList.Create;
// ... populate from args or menu
// Pass to converter as TStringList
// No manual StrNew/StrDispose needed
```

This eliminates:
- The fixed 4096-element array limit
- Manual `StrNew`/`StrDispose` calls in `ffmpeg_converter.lpr`
- `ClearAllocated` helper in `cli_menu.pas`
- The `VerifyAndCompactFiles` compaction loop in `cli_args.pas` — `TStringList.Delete` handles this

For the C ABI boundary, convert to `PPAnsiChar` only at the call site in `converter_process_files`.

### 6.2 GUI file list already uses `TStringList` (via `TListBox.Items`)

`form.pas` already uses `lstFiles.Items` (a `TStrings`). The thread then converts to `array of AnsiString`. This is fine.

### 6.3 Apple M4V chapter lines: Already uses `TStringList`

`apple_m4v_creator.pas` uses `TStringList.Create` for chapter text. Good.

---

## 7. Additional FPC Features Underutilized

### 7.1 `specialize` / Generics

FPC supports generics. The `TFPGList<T>` and `TFPGMap<TKey,TValue>` from `fgl` unit could replace some patterns, but the codebase is small enough that raw arrays/records are fine. **Low priority.**

### 7.2 `resourcestring` for user-visible text

All user-facing strings are hardcoded. Using `resourcestring` would facilitate future localization and avoid string duplication:
```pascal
resourcestring
  SFileNotFound = 'input file not found';
  SFileNotRegular = 'input file is not a regular file';
```

### 7.3 `const` parameters

Several functions pass `string` by value where `const` would avoid reference counting overhead:

| File | Function | Parameter |
|---|---|---|
| `converter_runner.pas` | `ProbeDuration` | `InputFile: string` → `const InputFile: string` |
| `converter_runner.pas` | `RunEncode` | `CommandBase: string` → `const CommandBase: string` |
| `process_utils.pas` | `RunCommandCapture` | `CommandLine: string` → `const CommandLine: string` |

All read-only string parameters should use `const`.

### 7.4 `InterlockedExchange` for StopFlag

Replace the plain `LongInt` StopFlag with:
```pascal
uses SyncObjs;
InterlockedExchange(Ctx^.StopFlag, 1);  // writer
if InterlockedCompareExchange(Ctx^.StopFlag, 0, 0) <> 0 then ...  // reader
```

### 7.5 `with` statement (use sparingly)

Some deeply nested record access like `Ctx^.Opts.I_target` in `converter_core.pas` could use `with Opts do` to reduce repetition. However, `with` is generally discouraged in modern Pascal style for maintenance reasons.

### 7.6 Default parameter values

FPC supports default parameter values. Useful for simple types but does not work for record parameters.

---

## 8. Summary of Recommended Changes — Priority Order

| Priority | Change | Impact | Effort |
|---|---|---|---|
| **HIGH** | Replace char-array fields with FPC enums + sets | Eliminates ~40 string comparisons, 4×`SetAnsiField`, 3×`ArrToStr`; adds compile-time safety | Medium |
| **HIGH** | Implement streaming `-progress` parsing in `RunEncode` | Fixes broken progress bar; fixes deadlock risk (PROC-1); activates dead `ProbeDuration` code | Medium |
| **HIGH** | Add `-movflags +faststart` for MP4/M4V outputs | Enables progressive playback | Trivial |
| **MEDIUM** | Convert `TConverterObj` to `TConverter` class | Cleaner API, eliminates nil checks and pointer casts | Medium |
| **MEDIUM** | Replace `array of PAnsiChar` with `TStringList` for file lists | Eliminates manual memory management, removes 4096 limit | Medium |
| **MEDIUM** | Add `-loglevel warning` to ffmpeg commands | Prevents log noise from mixing with `-progress` output | Trivial |
| **LOW** | Simplify loudnorm JSON parsing (drop fpjson for CLI/lib) | Reduces dependencies for non-GUI builds | Small |
| **LOW** | Extract menu steps into data-driven structure | Cuts `cli_menu.pas` from ~400 to ~80 lines | Medium |
| **LOW** | Add `const` to all read-only string parameters | Minor perf improvement, idiomatic FPC | Trivial |
| **LOW** | Use `InterlockedExchange` for StopFlag | Correctness on non-x86 platforms | Trivial |
| **LOW** | Consolidate `FormatEta` duplicate | Remove copy in form.pas, use time_utils version | Trivial |
