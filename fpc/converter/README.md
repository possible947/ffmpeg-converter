# FPC Converter Units

Core conversion logic and API-compatible surface.

## Units

- `converter_types.pas`: ABI-sensitive data types and callbacks
- `converter_core.pas`: converter lifecycle and main processing loop
- `converter_api_c.pas`: C-style wrapper exports for API parity
- `converter_cmd_builder.pas`: ffmpeg command generation
- `converter_analysis.pas`: 2-pass analysis helpers
- `converter_runner.pas`: process execution/probe helpers

## Compatibility Goal

Keep behavior and naming close to `src/converter/converter.h` and `src/converter/converter.c`.
