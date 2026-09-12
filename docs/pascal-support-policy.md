# Free Pascal Support Policy

The Free Pascal implementation has two support levels:

- **Windows** is the native release and final-validation target.
- **Linux** is a supported development/debug target used for parallel
  implementation work and preliminary Windows validation.

Linux Pascal is not a separate release target yet. Its binaries are useful for
functional development and cross-platform debugging, but final Windows
behavior must still be verified in the native Windows environment.

Linux build availability depends on the distribution and Lazarus installation
source. In the tested Ubuntu Linux 24.04.4 environment, Lazarus installed from
external sources did not provide the required LCL units. Fedora Linux 44
provides the required LCL packages, including GTK3 and Qt6 widgetsets, and the
Linux Pascal GUI builds successfully there.

## Support boundary

- The Windows Pascal CLI and Lazarus GUI are maintained, debugged, and included
  in Windows builds.
- The Linux Pascal CLI and Lazarus GUI are maintained as development/debug
  targets. The Linux GUI is functionally usable, but known interface issues
  remain under active development.
- The C/CMake implementation remains the primary released implementation on
  Linux and macOS, including the Linux GTK4 GUI, macOS Cocoa GUI, and platform
  CLIs.
- Pascal macOS binaries, libraries, GUIs, and packages are not supported.

## Removal policy

Pascal macOS support ends with the removal of its build entrypoints and
packaging scripts. Linux Pascal platform units and the Linux Lazarus GUI are
retained for the development/debug target and may be built directly with
Lazarus using GTK3 or Qt6. The Makefile may still reserve its portable build
targets for Windows until Linux Pascal packaging is formalized.

The removal does not change the C implementation or the Linux/macOS product
support provided by C/CMake.

## Validation policy

Every Pascal change must be validated with the Windows CLI and GUI build on a
supported Windows environment before release. Linux hosts may additionally
build and exercise the Pascal development target; those results are preliminary
and do not replace native Windows validation.

The Pascal Makefile may keep the current Windows-only release/build entrypoint
while Linux development builds use direct `lazbuild --ws=gtk3` or
`lazbuild --ws=qt6` commands. It must not advertise Pascal macOS targets.