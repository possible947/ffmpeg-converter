# Planned GUI and CLI update for codec selection

## Purpose

This document describes the planned update of codec selection in
`ffmpeg-converter` GUI and CLI.

The goal is to replace the current flat codec list with a structured selection
model that:

- keeps the UI manageable when the number of available encoders grows;
- matches the planned `HQ_converter` integration described in
  `/home/runner/work/ffmpeg-converter/ffmpeg-converter/docs/v3.0-Phase3.md`;
- stays compatible with the current preset-based workflow;
- does not expose low-level override fields from `HQ_converter`.

## Problem in the current design

At the moment, GUI and CLI mostly operate on a flat final codec id such as:

- `prores`
- `prores_ks`
- `h264_nvenc`
- `hevc_qsv`
- `av1_vulkan`
- `hq_converter` (planned integration mode)

This works while the codec catalog is small, but it becomes inconvenient when:

- multiple hardware backends are available on one machine;
- several variants exist inside one backend;
- `HQ_converter` adds its own encoder catalog and preset hierarchy.

The current flat list mixes three different concepts in one field:

1. conversion mode or family;
2. concrete encoder variant;
3. preset of the selected encoder.

## Planned GUI change

### New selection model

GUI will use three logical levels:

1. **Codec** — top-level mode or encoder family
2. **Encoder** — concrete variant inside the selected codec family
3. **Preset** — preset for the selected encoder

The new field added to the GUI is the **Encoder** field.

### GUI behavior

The GUI should be updated consistently in:

- Linux GTK4 GUI
- macOS native Cocoa GUI
- Pascal GUI

The recommended interaction model is explicit selection with separate controls,
not hover-based submenus.

### Top-level codec field

The first field should contain only high-level groups or modes. Example groups:

- `copy`
- `mux`
- `software`
- `videotoolbox`
- `vaapi`
- `nvenc`
- `amf`
- `qsv`
- `vulkan`
- `hq_converter`

Exact visible labels may be made more user-friendly, but the structure should
stay the same: the first field must no longer be a flat list of final codec ids.

### Encoder field

The second field should show only variants valid for the chosen codec group.

Examples:

- for `software`:
  - `prores`
  - `prores_ks`
- for `vaapi`:
  - `h264`
  - `hevc`
- for `nvenc`:
  - `h264`
  - `hevc`
  - `av1`
- for `qsv`:
  - `h264`
  - `hevc`
  - `av1`
- for `vulkan`:
  - `prores_ks`
  - `h264`
  - `hevc`
  - `av1`

The GUI internally maps `codec group + encoder variant` to the existing final
codec id used by the converter core and preset loader.

Examples:

- `software + prores` → `prores`
- `software + prores_ks` → `prores_ks`
- `nvenc + h264` → `h264_nvenc`
- `qsv + av1` → `av1_qsv`
- `vulkan + prores_ks` → `prores_ks_vulkan`

### Preset field

The existing preset field remains in place.

Its behavior does not change in principle:

- it is populated from the selected final encoder id;
- it stays disabled when only one preset exists;
- it uses the current preset-loading rules for built-in codecs;
- it uses the `HQ_converter` preset catalog when the selected mode is
  `hq_converter`.

## Planned CLI change

CLI should be aligned with the same three-level model:

- `-c` / `--codec` — top-level codec group or mode
- `--encoder` — concrete encoder variant inside the selected codec group
- `--preset` — preset for the selected encoder

### Parameter semantics

#### `-c`, `--codec`

Selects the top-level mode or family.

Examples:

- `copy`
- `mux`
- `software`
- `videotoolbox`
- `vaapi`
- `nvenc`
- `amf`
- `qsv`
- `vulkan`
- `hq_converter`

#### `--encoder`

Selects the encoder variant valid for the chosen codec group.

Examples:

- `prores`
- `prores_ks`
- `h264`
- `hevc`
- `av1`
- `x264`
- `x265`
- `svt-hevc`
- `svt-av1`
- `ffmpeg-prores`
- `ffmpeg-prores_ks`

#### `--preset`

Selects the preset for the chosen encoder.

Examples:

- `standard`
- `hq`
- `quality`
- `balance`
- `Medium`
- `balanced_4k`

### Examples

Built-in encoders:

```bash
ffmpeg_converter -c software --encoder prores_ks --preset hq input.mov
ffmpeg_converter -c nvenc --encoder hevc --preset quality input.mkv
ffmpeg_converter -c qsv --encoder av1 --preset default input.mkv
```

`HQ_converter`:

```bash
ffmpeg_converter -c hq_converter --encoder x265 --preset Medium input.mkv
ffmpeg_converter -c hq_converter --encoder ffmpeg-prores_ks --preset hq input.mkv
```

### Backward compatibility

For migration safety, the current low-level flat codec form should remain
supported for some time.

Examples of legacy syntax:

```bash
ffmpeg_converter -c h264_nvenc --preset quality input.mkv
ffmpeg_converter -c prores_ks --preset hq input.mov
```

Planned compatibility rules:

1. If `-c/--codec` receives a legacy final codec id, it is accepted.
2. In that mode, `--encoder` is optional and normally omitted.
3. New help text and examples should prefer the structured syntax.
4. `--codecs-list` should evolve to show grouped output instead of a flat dump.

## `HQ_converter` integration rules

The `HQ_converter` behavior is constrained by
`/home/runner/work/ffmpeg-converter/ffmpeg-converter/docs/v3.0-Phase3.md`
and by the provided `encoder_presets_v2.json`.

### What `ffmpeg-converter` must expose

For `HQ_converter`, the user may choose only:

1. `codec = hq_converter`
2. one encoder from `encoders.*`
3. one preset from `encoders.<selected>.presets`

### What `ffmpeg-converter` must not expose

`ffmpeg-converter` must not expose HQ internal override mechanics, including:

- `allowed_override_fields`
- `field_allowed_values`
- task/session override parameters
- manual editing of assembled HQ command flags

In other words, `ffmpeg-converter` uses only predefined HQ presets and does not
act as an HQ configuration editor.

### Why this is the correct model

The HQ preset file is already structured as:

- top-level `encoders`
- encoder-level `default_preset`
- encoder-level `presets`

This structure naturally matches:

- GUI field 1: `Codec` → `hq_converter`
- GUI field 2: `Encoder` → one key from `encoders`
- GUI field 3: `Preset` → one key from `encoders.<encoder>.presets`

This is a better fit than forcing HQ into the existing flat codec-id model.

## Required implementation tasks

### 1. Shared selection model

Introduce a shared mapping layer that converts:

- top-level codec group
- encoder variant

into:

- final built-in codec id for the converter core; or
- `hq_converter` plus an HQ encoder id.

This mapping should become the single source of truth for:

- Linux GTK4 GUI
- macOS native GUI
- Pascal GUI
- C CLI
- Pascal CLI

### 2. GUI updates

- Add the new **Encoder** control.
- Rebuild dependent widgets when the top-level codec changes.
- Keep current preset behavior.
- Preserve existing special controls such as Vulkan and VAAPI device selectors.
- Keep `copy` and `mux` behavior simple and explicit.

### 3. CLI updates

- Add `--encoder`.
- Redefine `-c/--codec` as the high-level selection input in the new syntax.
- Keep legacy flat codec ids accepted during transition.
- Update help, interactive menu flow, and `--codecs-list`.

### 4. HQ catalog reader usage

- Read HQ encoder names from `encoders.*`.
- Read HQ presets from `encoders.<name>.presets`.
- Use `default_preset` when preset is not specified.
- Ignore all HQ override metadata in the user-facing interface.

## Expected result

After the update:

- the GUI no longer presents one long flat codec list;
- codec choice becomes easier on systems with many available encoders;
- `HQ_converter` fits naturally into the same UI structure;
- CLI and GUI use the same conceptual model;
- `ffmpeg-converter` stays preset-driven and does not expose unsupported HQ
  low-level controls.
