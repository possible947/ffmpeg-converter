# V3 Final GUI Migration Plan: Codec, Encoder, Preset

## 1. Purpose and baseline

This document is the implementation plan for the final GUI migration before
the V3 Phase 4 filter engine work. It replaces the old flat-codec GUI plan.

The following work is treated as complete and is not repeated here:

- the unified `presets.json` catalog and runtime loading of FFmpeg arguments;
- the `codec -> encoder -> preset` selection model in the Linux C core and CLI;
- Linux CLI verification of the catalog, hardware probing, and conversion paths;
- source-level macOS and Windows support, pending native-platform validation.

The GUI must consume the same catalog and selection semantics as the CLI. The
GUI migration changes selection and layout only; conversion behavior, output
naming, audio processing, device handling, and callback flow remain unchanged.

### Current implementation anchors

- Linux GTK4 GUI: `src/gui/gui_window.c`, `src/gui/gui_window.h`, and
  `src/gui/gui_callbacks.c` currently expose a flat `codec` combo and a
  dynamic preset combo. Linux hardware probing is asynchronous and updates the
  codec list after startup.
- macOS native GUI: `src/gui_macos_native/main.m` currently loads the catalog,
  filters final codec ids with `macos_probe_codec_support()`, and exposes flat
  codec and preset popups.
- Windows GUI: there is no C Windows GUI. The supported GUI is the Lazarus /
  Free Pascal implementation in `fpc/gui/form.pas`; Windows availability is
  probed in `fpc/gui/form_windows.pas`.
- Existing special controls must remain functional: Vulkan and VAAPI device
  selectors, mux track selection, M4V creation, audio normalization and genre
  dependency, overwrite/output controls, and conversion callbacks.

## 2. Target GUI contract

### 2.1 Three upper zones

The top of every GUI window will contain three visually distinct zones.

**Video zone, left:**

1. `Codec` — catalog group or mode, for example `software`, `nvenc`,
   `qsv`, `vaapi`, `vulkan`, `videotoolbox`, `amf`, or `mux` when available.
2. `Encoder` — an encoder valid for the selected group.
3. `Preset` — a preset valid for the selected group and encoder.

**Audio zone, center:**

1. `Audio norm`
2. `Genre`
3. `Audio output`

The existing audio values and enablement rules remain unchanged. In
particular, `Genre` is enabled only for the normalization mode that needs it.

**Filter zone, right:**

1. `Filter`
2. `Preset`

This is a Phase 4-compatible placeholder, not the Phase 4 filter engine. The
`Filter` control must display the current deblock choice (`none`, `weak`, or
`strong`) and the filter preset control must remain compatible with that
selection. The internal ABI and command path continue to use `deblock` until
the Phase 4 filter engine replaces it. No new GPU filter probing or filter
catalog is part of this GUI phase.

The remaining file queue, output, device, progress, and log controls may stay
below these zones. The layout may use platform-native containers, but the
semantic order and dependencies must be identical on all platforms.

### Approved layout reference

The finalized Linux GTK layout is captured in
[`docs/maket_example.png`](maket_example.png). This image is the visual
reference for the GUI migration and should be treated as the correct layout
when implementing or reviewing the macOS Cocoa and Windows Lazarus surfaces.

The reference fixes these geometry rules:

- the upper controls are three equal semantic columns: `Video`, `Audio`, and
  `Filters`;
- each column stacks its controls vertically in the documented order;
- every field label occupies a fixed label area, is right-aligned, and sits
  immediately before its corresponding control;
- the separator, output row, file actions, queue, progress, and status areas
  begin below the three-column selection area;
- platform-native styling may differ, but the column structure, control order,
  relative alignment, and spacing must remain equivalent to the reference.

### 2.2 Population and selection rules

At startup, each GUI loads the bundled `presets.json` and obtains the catalog
entries for its platform. It then applies the platform's runtime hardware
probe before exposing hardware groups or encoders. The result must be the same
set of valid `group -> encoder -> preset` combinations that the CLI can use.

Changing a parent selection rebuilds only its dependent list:

- Codec change rebuilds Encoder, then Preset, then dependent device controls.
- Encoder change rebuilds Preset and preserves it only if still valid.
- Filter change rebuilds the filter preset placeholder and updates `deblock`.
- Invalid or unavailable selections fall back to the catalog default, or the
  first valid item when no default is defined.

The UI must pass the selected group, encoder, and preset through the existing
options-building path. It must not reconstruct FFmpeg arguments or duplicate
catalog tables in a platform GUI.

## 3. Implementation stages

Status markers are intentionally kept in this document and mirrored in the
changelog:

- `[ ]` not started
- `[~]` in progress
- `[x]` implemented and validated on the stated target

### Stage 0 — Freeze the cross-platform GUI contract

**Status:** `[x]` for the catalog and cross-platform contract; GUI model
implementation remains Stage 1 work.

**Work:**

- Confirm the catalog fields used by GUI population: platform, group, encoder,
  final codec id, default preset, available presets, and capability gate.
- Define the shared mapping contract from `group + encoder` to the existing
  final codec id or special mode. Include `copy` and `mux` explicitly.
- Define how the temporary Filter / Filter Preset controls map to the existing
  `deblock` integer and when they are enabled.
- Define selection reset behavior and fallback behavior for an unavailable
  hardware device or removed catalog entry.

### Stage 0 contract decision

The unified catalog is the source of truth. Its current fields used by the GUI
model are:

- `selection.common` for platform-independent groups (`software`, `mux`, and
  the feature-gated `hq_converter` group);
- `selection.platforms.<platform>.hwaccel.groups` for platform-specific
  hardware groups;
- group `kind` and `enabled` for selection behavior and static availability;
- child `enabled`, `final_codec` (or `execution_codec` for pipeline modes),
  `requires`, `bit_depths`, `pixel_format`, `profile_args`, and optional
  `presets` for component availability and selection metadata;
- the matching execution section (`linux`, `macos`, or `windows`) for the
  preset names and FFmpeg arguments referenced by `presets_source`.

The catalog currently uses the top-level `version` field. A future schema
version field may be added, but Stage 0 does not rename or duplicate the
existing field. GUI code must not parse execution FFmpeg arguments or carry a
second catalog.

The GUI model exposes the following canonical selection tuple:

```text
(group, encoder, preset) -> (final_codec, preset)
```

For ordinary software and hardware entries, `group` comes from the catalog
group, `encoder` is the child key, and `final_codec` comes from
`final_codec`. A child whose name ends in `_10bit` keeps that selection
variant in the resolved internal codec (`<final_codec>_10bit`) and also carries
its catalog pixel/profile metadata. Runtime `requires` checks filter children
before they reach the GUI. Static `enabled: false` and the `features` gate
filter groups before probing; empty groups are hidden.

Special modes are normalized without changing the converter ABI:

| GUI selection | Catalog source | Resolved internal codec | Encoder/preset meaning |
| --- | --- | --- | --- |
| `mux` + `copy` | `selection.common.mux.modes.copy` | `copy` | terminal stream-copy mode; no replacement track |
| `mux` + `mkv`/`mov` | `selection.common.mux.modes.mux` | `mux` | replacement track and selected output container |
| `mux` + `m4v` | `selection.common.mux.modes.m4v` | `m4v` | Apple M4V pipeline; existing M4V controls remain authoritative |

`mux` is the single user-facing pipeline group. Its Encoder values are
`copy`, `mkv`, `mov`, and `m4v`; standalone `copy` and `m4v` groups must not
be exposed because they duplicate pipeline modes and create ambiguous parent
selection behavior.
The Apple M4V button may continue to enter its existing dedicated workflow,
but any structured selection it creates must resolve through the same catalog
entry rather than a GUI-local table.

Preset selection uses the explicit child `presets` list when present;
otherwise it uses the referenced execution codec's preset list. A selected
preset is preserved only when it remains in the rebuilt list. The fallback
order is: catalog default (`standard` for the existing ProRes family,
otherwise `default`), then the first valid catalog preset. If no valid preset
exists, the selection is invalid and conversion must not start. A missing or
unavailable hardware child falls back to the first valid enabled group and
child, with `software`/`prores_ks` as the portable baseline when available.

The temporary filter controls are compatibility controls, not a new catalog:
`Filter` displays `none`, `weak`, or `strong`; `Filter Preset` is rebuilt from
that choice and currently has no effect beyond retaining a valid placeholder
selection. The selected filter maps to the existing `deblock` ABI value
`none -> 1`, `weak -> 2`, and `strong -> 3`. The controls retain the old
availability rule: they are enabled for the software ProRes paths and remain
disabled or constrained for copy, mux, M4V, and hardware paths unless the
existing GUI already permits them.

**Validation:** the current catalog was checked for the required common and
platform group fields, special-mode mappings, capability gates, and execution
preset references. The existing CLI resolver and summary path are the
reference for the tuple mapping. A complete GUI combination matrix is deferred
to Stage 1/3 because no shared GUI model exists yet; Stage 1 must add fixtures
for copy, software, one hardware group, 10-bit variants, mux, and disabled or
missing entries before claiming the matrix validated.

**Documentation at stage end:** completed in this section. The implementation
status and validation limitation are recorded in `CHANGELOG.md`.

### Stage 1 — Implement one shared catalog-to-GUI model

**Status:** `[x]` for the shared catalog/mapping APIs; widget migration and
options wiring remain Stage 2/3 work.

**Work:**

- Add or extend the C catalog/mapping API used by Linux GTK and macOS Cocoa;
  expose group enumeration, encoder enumeration, final codec resolution, and
  preset enumeration without exposing JSON internals to widgets.
- Add the matching Pascal loader/mapping API used by the Lazarus GUI. It must
  read the same generated/bundled catalog and resolve the same names as C.
- Keep runtime capability filtering outside the static catalog: the model
  accepts platform probe results and removes unsupported groups/encoders.
- Remove GUI-local flat codec tables once the new model is available. Do not
  change the converter ABI or command builder in this stage.

### Stage 1 implementation

The C model is implemented by `src/platform/selection_catalog.h` and
`src/platform/selection_catalog.c`, linked through the shared `converter`
library. The Pascal mirror is `fpc/converter/selection_catalog.pas`. Both
models expose group enumeration, dependent encoder enumeration, preset
enumeration, and final-codec resolution without exposing JSON objects to GUI
widgets.

The model keeps the catalog's `mux.modes` entries under one visible `mux`
group. Hardware
availability is supplied through a capability callback, so static catalog
metadata and runtime probing remain separate. The execution sections remain
the only source of preset names and FFmpeg arguments.

The converter ABI and command builders were not changed. Existing GUI-local
codec lists remain temporarily because their replacement is the Stage 2/3
control and options-collection work.

**Validation:** build the Linux GUI sources and Pascal GUI sources with the
new model; run catalog fixtures covering copy, software, one hardware group,
10-bit variants, mux, and missing/disabled entries.

The focused C fixture `test_selection_catalog` covers those cases, including a
capability callback that hides `nvenc`, and passes on Linux. The Pascal model
passes a syntax-only FPC compile on the Linux host. Native Windows GUI build
and runtime validation remain deferred to the required Windows environment.

**Documentation at stage end:** document the shared model API, source of
truth, fallback rules, and C/Pascal synchronization requirement in the
relevant architecture documentation; update `CHANGELOG.md` with files and
test results.

### Stage 2 — Migrate all GUI controls and layout together

**Status:** `[~]` corrected three-column controls and layouts implemented in all
three source paths; native macOS/Windows validation and model-driven
population remain pending.

Implement the same semantic change in all supported GUI implementations before
platform-specific polish:

- **Linux GTK4:** add `encoder_combo` and the filter/filter-preset controls to
  `AppWidgets`; replace the flat codec row with the three-zone top layout;
  preserve the existing asynchronous probe callback and Vulkan/VAAPI rows.
- **macOS Cocoa:** add Encoder and Filter/Filter Preset popups; replace the
  fixed coordinate arrangement with three top zones that fit the native window;
  preserve native VideoToolbox probing and existing popup actions.
- **Windows Lazarus:** add the encoder and filter controls to the form and
  wire the same model; update the `.lfm` layout and `form.pas` together so the
  Windows GUI is not dependent on dynamically guessed control positions.

Each implementation must keep the audio zone centered, including the current
audio norm, genre, and audio output controls. The filter zone initially uses
`none`, `weak`, and `strong` as its visible compatibility values.

### Stage 2 implementation

- Linux GTK4 now declares and lays out Encoder, Filter, and Filter Preset
  controls in `AppWidgets`, with Video, Audio, and Filters as separate vertical
  zones, deferred dependency updates, and running-state locking preserved.
- macOS Cocoa now owns native Encoder and Filter Preset popups and uses a
  three-column upper layout with each zone stacked vertically. The existing
  deblock popup is presented as the temporary Filter control.
- Windows Lazarus now declares the equivalent controls in `form.pas` and
  `form.lfm`; Video, Audio, and Filters are separate vertical columns, and the
  form height and lower workflow controls were adjusted so the columns do not
  overlap output or queue controls.

The controls are intentionally transitional: Stage 3 will replace the current
flat codec population and resolve group plus encoder at the options boundary.
The existing codec values and deblock behavior remain intact until that wiring
is completed.

**Validation:** perform a compile/build check for all three GUI source paths;
exercise parent-selection changes and verify that no control is duplicated,
overlapped, or left without a model.

Linux `linux_gui` compilation and the selection catalog test pass. The Cocoa
and Lazarus source/layout edits could not receive native target builds on this
Linux host, so this stage remains in progress until macOS and Windows
validation is performed.

**Documentation at stage end:** update the GUI layout description and add
platform file mappings, screenshots or manual test notes where available, and
the stage status to `CHANGELOG.md`.

### Stage 3 — Wire startup population, dependencies, and option collection

**Status:** `[x]` structured population and option resolution are implemented
and functionally tested in the C and Linux Pascal GUI paths. Native Windows
validation remains a separate final-release step.

**Work:**

- Populate Codec from catalog groups at startup, then populate Encoder and
  Preset from the selected parents. Hardware entries appear only after the
  existing target-platform probe completes.
- Ensure Linux probe completion refreshes all three video selectors rather than
  only the old codec list. Preserve asynchronous UI safety and signal blocking.
- Ensure macOS and Windows perform the equivalent post-probe refresh before a
  conversion can start.
- Resolve the selected `group + encoder` to the final internal codec at the
  single options-collection boundary. Keep preset text from the selected
  catalog entry, not from a hardcoded index.
- Map Filter / Filter Preset to current `deblock` values without changing
  converter behavior. Keep filter controls disabled or constrained exactly
  where the old deblock control was unavailable.
- Preserve mux, copy, M4V, audio, device, output, overwrite, and running-state
  behavior. A conversion must never start with a stale encoder or preset after
  a parent selection changes.

### Stage 3 implementation

- Linux GTK now loads `selection_catalog` from the bundled/generated
  `presets.json`, populates groups, encoders, and presets from the shared model,
  filters hardware entries through `LinuxCodecSupport`, and resolves the
  selected group plus encoder to the legacy converter codec at option
  collection.
- macOS Cocoa now performs the equivalent catalog loading and VideoToolbox
  capability filtering, including structured resolution before calling the
  existing converter bridge.
- Windows Lazarus now loads the Pascal selection model, uses the existing
  Windows hardware probe as its capability filter, populates groups and
  encoders from the catalog, and resolves options through the same boundary.
- Dependent mux, deblock, Vulkan, and VAAPI behavior now uses the resolved
  final codec rather than a group display name. The converter ABI and command
  builders remain unchanged.

**Validation:** run a GUI model/option-collection matrix for every available
platform group and verify the generated options against the Linux CLI's
resolved summary. Test startup with no hardware, one hardware backend, and
multiple hardware backends.

The Linux GUI, Linux Pascal GUI, and `SelectionCatalogTests` pass after the
wiring changes. The Pascal selection unit and full Lazarus form compile on
Fedora Linux 44 with GTK3 and Qt6. Native Windows validation remains required
for final release approval, but the tested Pascal Linux implementation is ready
to serve as the Windows development/debug baseline.

The Linux Pascal GUI status is now explicitly **development/debug** rather than
removed: the historical `fpc/platform/linux_probe.pas` backend was restored
from Git, and the full Lazarus GUI builds successfully with both
`lazbuild --ws=gtk3 fpc/gui/form.lpi` and
`lazbuild --ws=qt6 fpc/gui/form.lpi`. The implementation is functionally usable
and suitable for parallel debugging before Windows testing, but known interface
issues remain and native Windows validation is still authoritative.

The Pascal Linux GUI now uses the same structured population contract as the C
GUI: Group comes from `selection_catalog`, Encoder is rebuilt for the selected
Group, and Preset is rebuilt for the selected Group plus Encoder. The previous
flat `cmbCodec.Items.Add(...)` population path was removed. Direct GTK3 and
Qt6 builds and a startup smoke run of the rebuilt GUI passed on Fedora Linux 44.

Encoder selection now has its own change handler. Selecting an Encoder rebuilds
only Preset and dependent enablement; it no longer invokes the parent Codec
handler and immediately resets the Encoder list. Rebuilding after a Codec change
preserves the previous Encoder when that value remains valid.

This result is distribution- and installation-source dependent. On Ubuntu
Linux 24.04.4, with Lazarus installed from external sources, the required LCL
units were unavailable in the tested environment. On Fedora Linux 44, the LCL
packages are available and the GTK3 and Qt6 builds above succeed. The Ubuntu
result must therefore not be generalized to Linux Pascal support as a whole.

### Linux GUI test feedback and correction

Manual Linux testing found two Stage 3 interaction defects. Encoder selection
was immediately reset because the generic dependency refresh rebuilt the Encoder
list in response to the Encoder's own change signal. Vulkan and VAAPI device
lists were rebuilt through the same path, so their selections were also not
stable. The refresh order now rebuilds Encoder and device lists only when their
parent Codec or Encoder changes; ordinary audio, filter, and file-list updates
no longer replace those models. The selected Encoder is preserved when a
parent list is rebuilt if it remains valid.

VAAPI fallback labels also used a process-global incrementing counter, causing
repeated refreshes to display ever-growing GPU numbers. Labels now derive from
the stable `renderD<N>` device number.

The Linux correction was validated with the `linux_gui` build and
`SelectionCatalogTests`. Native platform validation remains pending.

### Linux GUI crash correction after repeated video selections

A subsequent manual test exposed a crash after several Codec/Encoder changes,
most often when changing entries within the `mux` group. The cause was nested
GTK `notify::selected` delivery while dependent models were being rebuilt:
rebuilding Encoder could synchronously trigger another Encoder/device refresh.
The Linux GTK model now has an `updating_selection` reentrancy guard around
Codec and Encoder dependent refreshes. This keeps the `mux` cascade
single-pass while preserving the normal deferred preset and sensitivity update.

The fix was rebuilt and `SelectionCatalogTests` passed. The manual repeated
selection scenario should be rerun against the rebuilt `linux_gui` binary;
native macOS and Windows validation remains pending.

### Final mux and Apple M4V correction

Manual C and Pascal testing confirmed that mux modes must use only two active
selection levels: Group `mux` and Encoder mode. The third Preset control is now
disabled for mux and displays only a `default` placeholder.

The mode contract is:

- `mux/copy`: stream copy, no replacement track, no mux postprocess;
- `mux/mkv` and `mux/mov`: replacement track required, with the Encoder mode
  supplying the final container preset;
- `mux/m4v`: replacement track required, with the Encoder mode selecting the
  Apple M4V post-mux pipeline;
- the standalone Apple M4V button uses its own dedicated workflow and a safe
  copy intermediate, independent of the current mux selection.

The C and Pascal GUIs now share this mode behavior. This fixes the previous
double population of Encoder/Preset, restores mux M4V track activation, and
prevents the standalone Apple M4V workflow from passing `mux`/`m4v` as a normal
converter codec.

### Standalone Apple M4V temporary storage correction

The standalone C Apple M4V crash was traced to `m4v_create_from_input()` always
creating its work directory under `/tmp`, while the final output was written to
the selected output directory. Finalization used `rename()` across those
locations; when they were different filesystems or mounts, the operation could
fail and leave the workflow in an invalid state. The M4V platform helper now
creates its unique work directory beside the requested output file on POSIX and
Windows. This keeps intermediate files and finalization on the same filesystem
while retaining the existing verified copy fallback.

The same cross-filesystem finalization defect was present in the Pascal creator:
its `CreateAppleM4V()` used `GetTempDir(False)` and later called `RenameFile()`
into the selected output directory. Pascal M4V work directories now use the
output directory as their base as well. GTK3 and Qt6 Pascal GUI builds pass
after this correction; a real M4V conversion should be rerun with an input
file and MP4Box available.

The C standalone Apple M4V post-completion crash had a separate lifecycle bug:
the dedicated M4V worker enters the shared cleanup path without creating a
normal `Converter`, but cleanup still called `converter_destroy()` on an
uninitialized pointer. The worker now initializes that handle to `NULL` and
destroys it only when it was created, allowing the completed M4V output to
reach the normal GUI Completed state.

### C AppImage packaging validation

The Linux C AppImage packaging path was enabled and completed successfully.
The staging script now requires the tested mux/M4V runtime set (`ffmpeg`,
`ffprobe`, `mkvmerge`, and `MP4Box`), copies the generated `presets.json`,
includes the complete enabled `hq_converter` tree, and preserves the project
Vulkan runtime metadata/library files when present. The resulting artifact is
`build/bin/FFMpeg-Converter-x86_64.AppImage` (about 187 MB). Its contents were
extracted and checked, and a startup smoke run remained alive until an external
8-second timeout. AppImage packaging is built and smoke-tested and is ready for
user acceptance.

**Documentation at stage end:** update the user manual and `docs/` references
from codec/preset to codec/encoder/preset, describe the temporary filter
compatibility behavior, and record the matrix results in `CHANGELOG.md`.

### Stage 4 — Linux target validation and regression pass

**Status:** `[x]` C Linux GUI and Linux Pascal GUI are functionally tested with
the available runtime components. C AppImage packaging is built and
smoke-tested; Pascal AppImage packaging is not required.

**Target:** Linux GTK4 GUI on the validated Linux host.

**Checks:**

- Build `linux_gui` and run the GUI with the bundled/generated `presets.json`.
- Confirm startup population and post-probe population for software, VAAPI,
  NVENC, QSV, Vulkan, copy, and mux entries available on the host.
- Verify each parent change updates only valid dependent entries and that the
  selected options match the CLI summary.
- Exercise deblock compatibility through the new Filter controls, audio norm
  and genre enablement, audio output, VAAPI/Vulkan device selectors, mux track
  selection, and M4V entry point.
- Run representative conversions, including a hardware encoder and a software
  encoder, then verify output with the existing FFmpeg/ffprobe validation.

**Documentation at stage end:** record build commands, host capabilities,
manual scenarios, conversion results, and known limitations in the project
docs; update the Linux status in `CHANGELOG.md`.

### Stage 5 — macOS native target validation and regression pass

**Status:** `[~]` V3 preparation is complete for the tested Linux C/Pascal
surfaces. Native macOS and Windows validation remains open.

**Target:** macOS Cocoa GUI with the bundled FFmpeg 8.1 and native runtime
dependencies.

**Checks:**

- Build `macos_gui_native` and install/run the `.app` bundle with its generated
  `presets.json`.
- Confirm that catalog groups and encoders are filtered by
  `macos_probe_codec_support()` and that VideoToolbox entries appear only when
  the native probe succeeds.
- Verify the three-zone layout at the minimum window size and with long preset
  names; no popup or label may be clipped.
- Exercise software, VideoToolbox, copy, and mux paths that are available on
  the target, including audio and temporary deblock mapping.
- Compare GUI option resolution with the macOS CLI for identical selections;
  record unsupported hardware as a target capability result, not a source bug.

**Documentation at stage end:** add macOS build/install commands, OS version,
FFmpeg bundle version, probe results, and manual regression results to the
project docs and update `CHANGELOG.md`.

### Stage 6 — Windows Lazarus target validation and regression pass

**Status:** `[~]` Windows remains the native release target; Linux Pascal is
also used as a development/debug target for preliminary validation.

**Target:** Windows GUI from the Pascal implementation, using the same staged
`presets.json`, FFmpeg/FFprobe, and runtime DLL set as the Windows build.

**Checks:**

- Build the Windows Pascal GUI with the supported build script and confirm the
  `.lfm` form and Pascal sources are synchronized.
- Confirm startup catalog loading and post-probe population for NVENC, AMF,
  QSV, Vulkan, software, copy, and mux entries supported by the host.
- Test selection dependency resets, options collection, audio controls,
  Vulkan device selection, mux track selection, and Filter/deblock mapping.
- Run representative software and available hardware conversions and compare
  the GUI-resolved options with the Windows CLI.
- Verify behavior with missing optional `mkvmerge` and with unavailable GPU
  encoders: invalid entries must be hidden or rejected before conversion.

**Documentation at stage end:** record the exact Windows build command,
compiler/Lazarus version, driver and FFmpeg details, hardware matrix, manual
test results, and known limitations in `fpc/README.md` or the applicable
Windows documentation; update `CHANGELOG.md`.

Linux Pascal is intentionally not a substitute for this stage. Its role is to
catch shared implementation issues before the final native Windows build and
runtime test.

### Stage 7 — Cross-platform closeout before Phase 4

**Status:** `[ ]`

**Work and exit criteria:**

- Confirm C and Pascal catalogs expose equivalent group/encoder/preset names
  where the platform supports them.
- Confirm no GUI contains a second hardcoded codec or preset catalog.
- Confirm all three GUIs use the same visible three-zone semantics and the same
  temporary Filter/deblock compatibility rule.
- Confirm CLI behavior and converter ABI are unchanged except for GUI-produced
  structured options.
- Update `CHANGELOG.md` from planned to verified status for each platform.
- Update `README.md`, user manual, Phase 3 completion notes, and Phase 4
  prerequisites to state that GUI migration is complete and that full filter
  catalog implementation remains a separate Phase 4 task.

Linux preparation and validation are complete. The overall cross-platform phase
remains open only for native macOS and Windows validation; source compilation
alone does not mark those platforms as validated.

## 4. Explicit non-goals

- Do not implement `filter_presets.json`, GPU filter probing, filter-chain
  construction, or replace the `deblock` ABI in this phase.
- Do not change FFmpeg command generation, audio semantics, output naming, mux
  behavior, M4V behavior, or converter callbacks.
- Do not reintroduce a Windows C GUI or Pascal macOS target. Linux Pascal may
  remain a development/debug target without becoming a release packaging target.
- Do not mark macOS or Windows as tested based only on Linux compilation.

## 5. Change tracking rule

Every stage must update both this plan and `CHANGELOG.md` in the same change.
The changelog entry must identify:

1. implementation status (`planned`, `in progress`, or `verified`);
2. affected C and Pascal GUI files or model APIs;
3. target platform and exact build/test command;
4. hardware and runtime limitations discovered during validation.

This keeps the plan actionable while preserving a reliable distinction between
source synchronization and native-platform verification.
