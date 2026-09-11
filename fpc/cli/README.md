# FPC CLI Module

This folder contains the Windows Pascal CLI implementation.

## Files

- `ffmpeg_converter_windows.lpr`: application entry point
- `cli_args.pas`: command line parsing skeleton
- `cli_menu.pas`: interactive flow skeleton
- `cli_progress.pas`: progress rendering helpers
- `cli_callbacks.pas`: callback bindings for converter events

## Notes

Current implementation is a scaffold and intentionally minimal.
Port detailed menu/validation logic from C incrementally.
