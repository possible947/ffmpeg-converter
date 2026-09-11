# Preset File Rules

This document defines the preset data model for the GUI and CLI codec-selection
update.

## Files and responsibilities

The preset data has two layers:

- `presets_v2.json` is the unified selection catalog. It describes groups,
  encoders, availability gates, runtime requirements, and mappings to execution
  codec IDs.
- `presets.json` is the current built-in execution catalog. It contains the
  FFmpeg command parameters and remains the execution source until the loader
  migration is completed.

The selection layer must not duplicate FFmpeg command construction logic. GUI
and CLI code select a group, encoder, and preset; the execution layer resolves
that selection to the existing FFmpeg parameters.

## Schema rules

The selection catalog must contain `schema_version` and a boolean
`features.hq_converter` field. Boolean values are JSON booleans, not strings:

```json
"enabled": true
```

or:

```json
"enabled": false
```

An omitted `enabled` field is interpreted as `false`. A disabled parent disables
all children and prevents runtime probing of those children.

Each selectable group or component has these applicable fields:

- `enabled`: static catalog/build availability;
- `kind`: selection behavior;
- `final_codec`: existing built-in codec ID, when applicable;
- `requires`: runtime capability names for probing;
- `presets_source`: link to built-in execution presets or an external source.

## Selection groups

### Software

The predefined `software` group contains:

- `prores` → `prores`;
- `prores_ks` → `prores_ks`.

Their preset names and FFmpeg parameters are resolved from the corresponding
entries in the built-in execution catalog.

### Hardware acceleration

Hardware encoder groups are platform-specific and are stored under
`selection.platforms.<platform>.hwaccel.groups`.

Build-time availability determines whether a platform group is enabled. Runtime
probing then determines which components inside the enabled group are usable on
the current machine. An empty group after probing is hidden from the GUI and
CLI.

A component maps its user-facing encoder name to the existing final FFmpeg
codec ID, for example:

```json
{
  "enabled": true,
  "final_codec": "hevc_nvenc",
  "requires": ["hevc_nvenc"]
}
```

### Pipeline group

The predefined `mux` group is a video-pipeline group. It contains:

- `copy`: stream-copy mode using the source video track;
- `mux`: replacement-video-track mode with container choices;
- `m4v`: Apple M4V pipeline mode.

`copy` and replacement-track `mux` share a pipeline family but remain separate
execution modes. `copy` does not require a replacement track; `mux` does.

### HQ_converter

`hq_converter` is an external encoder group controlled by the
`features.hq_converter` build/runtime capability flag.

The selection catalog does not hardcode HQ encoder or preset names. When the
feature is enabled, the runtime reads the prepared HQ release preset file and
loads `encoders.*`, `default_preset`, and `encoders.<name>.presets`. HQ override
metadata and assembled command flags are not user-facing preset data.

Windows builds keep this feature disabled and do not inspect or expose HQ data.

## Runtime filtering

The common filtering algorithm is:

1. Load the selection catalog.
2. Select the current platform.
3. Remove disabled groups.
4. Apply build/platform gates.
5. Probe enabled hardware components using `requires`.
6. Remove unavailable components and hide empty groups.
7. Resolve presets for the selected built-in execution codec.
8. If HQ is enabled, load its external encoder/preset catalog.

The GUI and CLI must use the same filtering and resolution semantics.

## Execution parameters

FFmpeg parameters belong to execution preset records, not GUI or CLI source
code. Supported execution fields include:

- `ffmpeg_args`;
- `pre_input_args`;
- `video_filter`;
- `container`;
- `pix_fmt`;
- `pipeline`;
- `requires`.

The selection catalog references these records through the final codec ID and
must not silently invent a second set of command parameters.

## Legacy compatibility

During migration, flat final codec IDs remain accepted by the CLI and existing
internal APIs. Examples include `h264_nvenc`, `hevc_qsv`, and `prores_ks`.

The resolver maps legacy IDs back to the structured selection where possible,
while new help text and GUI controls use the group/encoder/preset model.
