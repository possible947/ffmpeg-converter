# Converter API Map

This file maps C API symbols to Free Pascal units.

## C Header Source

- `src/converter/converter.h`

## Function Mapping

- `converter_create` -> `fpc/converter/converter_core.pas`
- `converter_destroy` -> `fpc/converter/converter_core.pas`
- `converter_set_callbacks` -> `fpc/converter/converter_core.pas`
- `converter_set_options` -> `fpc/converter/converter_core.pas`
- `converter_process_files` -> `fpc/converter/converter_core.pas`
- `converter_stop` -> `fpc/converter/converter_core.pas`
- `converter_error_string` -> `fpc/converter/converter_core.pas`

## Adapter Layer

- `fpc/converter/converter_api_c.pas` re-exports the same API signatures for C-like integration boundaries.
