# Technical Design: Apple M4V Creator for C macOS Native GUI

Date: 2026-03-19
Status: Implemented (Phases 1-3 completed)
Owner: ffmpeg-converter maintainers

## Implementation Status (2026-03-19)

Completed:

- Phase 1:
  - Apple M4V backend module (`apple_m4v_creator.h/.m`)
  - Bridge API and run-state integration
  - Basic UI action in native macOS GUI
- Phase 2:
  - User options prompt in native GUI
  - Edit-before-mux flow
  - Option forwarding from UI -> bridge -> backend
- Phase 3:
  - MP4Box + dylib bundling via post-build script
  - Runtime MP4Box resolution and environment propagation

Reference implementation files:

- `src/gui_macos_native/main.m`
- `src/gui_macos_native/converter_bridge.h`
- `src/gui_macos_native/converter_bridge.m`
- `src/gui_macos_native/apple_m4v_creator.h`
- `src/gui_macos_native/apple_m4v_creator.m`
- `src/gui_macos_native/bundle_mp4box_deps.sh`
- `src/gui_macos_native/CMakeLists.txt`

## 1. Objective

Add Apple-compatible M4V creation workflow to the C macOS native GUI implementation, aligned with:

- Shell reference pipeline in `mux_apple_m4v.sh`
- Existing Pascal implementation in `fpc/converter/apple_m4v_creator.pas`

The new feature must support:

- Direct mode: source file -> `.m4v`
- Edit-before-mux mode: main converter output -> `.m4v` -> optional cleanup of intermediate

## 2. Scope and Non-goals

### In scope

- C macOS native GUI integration (`src/gui_macos_native`)
- Apple M4V backend pipeline execution
- UI controls and run-state management
- Tool resolution (`ffmpeg`, `ffprobe`, `MP4Box`)
- App bundle packaging for `MP4Box` and dependent dylibs
- Validation and regression tests for workflow

### Out of scope

- Changes to core conversion algorithms in `src/converter` unrelated to Apple M4V
- Linux GTK GUI changes
- Windows-specific implementation
- Rework of existing ProRes/H265 conversion logic

## 3. Current State

### 3.1 References already implemented

- Shell reference pipeline:
  - `mux_apple_m4v.sh`
- Pascal creator backend:
  - `fpc/converter/apple_m4v_creator.pas`
- Pascal GUI workflow (button, thread, edit mode):
  - `fpc/gui/form.pas`
- Pascal app packaging with MP4Box + dylibs:
  - `fpc/build/package_macos_app.sh`

### 3.2 C macOS GUI baseline

- Native app skeleton exists:
  - `src/gui_macos_native/main.m`
  - `src/gui_macos_native/converter_bridge.h`
  - `src/gui_macos_native/converter_bridge.m`
- Current bundle step copies only `ffmpeg` and `ffprobe`:
  - `src/gui_macos_native/CMakeLists.txt`

## 4. Functional Requirements

1. Process selected files in batch.
2. Build Apple-compatible `.m4v` output with:
   - Video copied from selected video track
   - AAC stereo track (q:a configurable)
   - AC3 track (bitrate configurable)
   - MP4Box mux with Apple branding (`M4V :0`, `mp42`, `isom`)
3. Optional chapter import from source.
4. Support configurable:
   - Video track index
   - Audio track index
   - AAC quality
   - AC3 bitrate (kbps)
   - Audio language code
   - Add chapters (on/off)
5. Support two flows:
   - Direct mode
   - Edit-before-mux mode
6. Respect overwrite option from GUI.
7. Provide user-visible progress and stage logging.
8. Keep UI responsive and update controls on main thread.

## 5. Non-functional Requirements

1. Maintain behavior parity with `mux_apple_m4v.sh` timing strategy.
2. Avoid regressions in existing standard conversion flow.
3. Ensure temporary artifacts are cleaned up on success/failure/stop.
4. Work correctly in packaged `.app` without system-wide ffmpeg when bundled tools are present.
5. Degrade gracefully with actionable errors when MP4Box is unavailable.

## 6. High-level Architecture

## 6.1 New backend module

Add dedicated Apple M4V backend in native GUI layer:

- `src/gui_macos_native/apple_m4v_creator.h`
- `src/gui_macos_native/apple_m4v_creator.m`

Responsibilities:

- Resolve binaries for `ffmpeg`, `ffprobe`, `MP4Box`
- Create temp working directory
- Execute 5-stage pipeline
- Report structured stage/progress/log callbacks
- Cleanup temp files

## 6.2 Bridge integration

Extend bridge API in:

- `src/gui_macos_native/converter_bridge.h`
- `src/gui_macos_native/converter_bridge.m`

Responsibilities:

- Start/stop Apple M4V jobs in background queue
- Keep independent run-state from standard conversion
- Expose completion status and counters (`success`, `fail`)
- Prevent concurrent start of incompatible workflows

## 6.3 UI integration

Update:

- `src/gui_macos_native/main.m`

New controls:

- Button: `Apple m4v creator`
- Checkbox: `m4v edit` (edit-before-mux mode)

Behavior:

- Disable conflicting actions while Apple M4V job is running
- Prompt for Apple M4V options before start
- Show stage and log lines in existing log/status widgets

## 7. Pipeline Design (Implementation Contract)

The backend pipeline should mirror `mux_apple_m4v.sh` and Pascal behavior.

### Step 0: preflight

- Verify input exists and is readable.
- Resolve `ffmpeg`, `ffprobe`, `MP4Box`.
- Validate output directory writable.
- Resolve output file path (`<basename>.m4v`).
- Handle overwrite policy.

### Step 1: probe FPS

- First: `ffprobe avg_frame_rate`
- Fallback: `ffprobe r_frame_rate`
- Fallback default: `25.0`

### Step 2: video scratch

- Command template:
  - `ffmpeg -y -nostdin -i <in> -map 0:v:<idx> -c:v copy -an -sn -dn -f mp4 <video.mp4>`

### Step 3: AAC scratch

- Command template:
  - `ffmpeg -y -nostdin -i <in> -map 0:a:<idx> -c:a aac -profile:a aac_low -q:a <q> -f mp4 <aac.m4a>`

### Step 4: AC3 scratch

- Command template:
  - `ffmpeg -y -nostdin -i <in> -map 0:a:<idx> -c:a ac3 -b:a <kbps>k -f mp4 <ac3.mp4>`

### Step 5: MP4Box mux

- Command template:
  - `MP4Box -new -brand "M4V :0" -ab mp42 -ab isom ...`

Track naming/language:

- Video: `name=Video`
- AAC: `name=AAC:lang=<lang>`
- AC3: `name=AC3 <kbps>k:lang=<lang>`

### Step 6: chapters (optional)

- Extract: `ffprobe -show_chapters -print_format json`
- Convert to MP4Box chapter text format
- Import via: `MP4Box -chap <chapters.txt> <out.m4v>`

## 8. API Design

## 8.1 New bridge types

Add Objective-C structures/classes:

- `AppleM4VOptions` with fields:
  - `videoTrackIndex`
  - `audioTrackIndex`
  - `aacQuality`
  - `ac3BitrateKbps`
  - `audioLang`
  - `addChapters`

- `AppleM4VResult` with fields:
  - `successCount`
  - `failCount`
  - `errors[]`

## 8.2 New bridge methods

Proposed methods in `ConverterBridge`:

- `- (BOOL)isAppleM4VRunning;`
- `- (void)startAppleM4VWithOptions:(AppleM4VOptions)options files:(NSArray<NSString *> *)files editBeforeMux:(BOOL)editBeforeMux convertOptions:(ConvertOptions)convertOptions log:(BridgeLogHandler)log stage:(BridgeStageHandler)stage status:(BridgeStatusHandler)status completion:(BridgeCompletionHandler)completion;`
- `- (void)stopAppleM4V;`

Notes:

- `stopAppleM4V` is cooperative between stages; subprocess hard-kill is optional and can be phase-2.
- Keep existing standard conversion API unchanged.

## 9. UI/UX Flow

1. User selects files and output directory.
2. User clicks `Apple m4v creator`.
3. App prompts Apple options (default values prefilled).
4. App validates run preconditions and starts background processing.
5. UI shows:
   - Current file index (`[i/n]`)
   - Current stage (`video copy`, `aac`, `ac3`, `mux`, `chapters`)
   - Final summary (`ok=X, failed=Y`)
6. If `m4v edit` is enabled:
   - Run standard converter first using current GUI options
   - Use converted file as M4V source
   - Remove intermediate converted file on success

## 10. Tool Resolution and Bundle Strategy

## 10.1 Runtime resolution order

For each tool:

1. Explicit env vars (`FFMPEG`, `FFMPEG_BIN`, `FFPROBE`, `FFPROBE_BIN`, optional `MP4BOX_BIN`)
2. App bundle path:
   - `Contents/Resources/bin/<tool>`
3. Common system paths:
   - `/opt/local/bin`
   - `/opt/homebrew/bin`
   - `/usr/local/bin`
4. `PATH` lookup fallback

## 10.2 Packaging changes for C native app

Update `src/gui_macos_native/CMakeLists.txt`:

1. Bundle `MP4Box` when available.
2. Bundle non-system dependent dylibs into `Contents/Resources/lib`.
3. Patch install names/rpath to resolve bundled libs.
4. Emit warning if `MP4Box` absent at build time.

This should align with proven Pascal packaging strategy in `fpc/build/package_macos_app.sh`.

## 11. Error Handling and Logging

1. Every pipeline step reports:
   - Step name
   - Exit code
   - Compact stderr tail
2. User-facing errors must be actionable:
   - Missing MP4Box with installation hint
   - Invalid track index
   - Output exists with overwrite disabled
3. Preserve diagnostic context in logs for post-mortem.

## 12. Concurrency and State Management

1. Disallow simultaneous standard conversion and Apple M4V run.
2. Disable relevant controls during active Apple job.
3. Keep UI updates on main thread only.
4. Ensure clean state reset after completion/failure/stop.

## 13. Test Plan

## 13.1 Automated/smoke

1. Direct mode, single file.
2. Direct mode, multiple files.
3. Edit-before-mux mode, single file.
4. Overwrite off with existing target -> expected fail/skip.
5. Overwrite on with existing target -> expected replace.
6. Chapters on with chaptered input -> chapters imported.
7. Chapters on without chapters -> no hard failure.
8. Missing MP4Box -> clear error.

## 13.2 Output validation

Use `ffprobe` checks:

1. Container is m4v/mp4 compatible.
2. Track presence:
   - Video
   - AAC audio
   - AC3 audio
3. Language tags applied.
4. Chapter stream present when expected.

## 13.3 Regression

1. Existing Start/Stop conversion flow remains intact.
2. Existing conversion option mapping unchanged.
3. No app crash on close during idle and during active run.

## 14. Delivery Plan

### Phase 1

- Backend pipeline (`apple_m4v_creator`)
- Bridge API for direct mode
- Basic UI button + defaults
- Manual smoke test

### Phase 2

- Edit-before-mux mode
- Full options prompt UI
- Improved error reporting

### Phase 3

- MP4Box bundle + dylib packaging
- Expanded tests and docs

## 15. Acceptance Criteria

1. C macOS GUI can create Apple-compatible M4V using MP4Box pipeline.
2. Direct mode and edit-before-mux mode both work.
3. Behavior matches shell/Pascal references for core steps.
4. Packaged app supports bundled tool workflow.
5. No regression in standard conversion path.

## 16. Open Questions

1. Should `stopAppleM4V` terminate currently running subprocess immediately (SIGTERM) or remain cooperative-only in v1?
2. Should Apple options be persisted between runs (NSUserDefaults) in v1 or later?
3. Should language validation be strict ISO-639-2 or allow free-form (current Pascal behavior is permissive)?
