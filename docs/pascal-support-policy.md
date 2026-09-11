# Free Pascal Support Policy

The Free Pascal implementation is supported and released for **Windows only**.

## Support boundary

- The Windows Pascal CLI and Lazarus GUI are maintained, debugged, and included
  in Windows builds.
- The C/CMake implementation is the supported implementation on Linux and
  macOS, including the Linux GTK4 GUI, macOS Cocoa GUI, and platform CLIs.
- Pascal Linux and macOS binaries, libraries, GUIs, and packages are not
  supported release targets.

## Removal policy

Pascal Linux and macOS support ends with the removal of their build entrypoints,
platform units, and packaging scripts from `fpc/`. Shared Pascal units are kept
only when they are required by the Windows build. Linux/macOS conditional code
that is no longer reachable from a supported Windows target is removed rather
than retained as an unsupported compatibility path.

The removal does not change the C implementation or the Linux/macOS product
support provided by C/CMake.

## Validation policy

Every Pascal change must be validated with the Windows CLI and GUI build on a
supported Windows environment. Linux hosts may inspect or edit the Pascal
sources, but are not considered Pascal build or release environments.

The Pascal Makefile must fail clearly when invoked on a non-Windows host and
must not advertise Linux, macOS, AppImage, or macOS application targets.