# HQ_converter Integration Policy

`HQ_converter` is an external project. It is prepared, built, tested, and
released independently from `ffmpeg-converter`.

## Release placement

Before configuring or building `ffmpeg-converter`, the prepared HQ_converter
release must be placed at:

```text
third_party/hq_converter/
```

The release directory is an opaque, relocatable runtime tree. Its internal
layout, utilities, shared libraries, configuration, and launcher files must be
preserved. `ffmpeg-converter` must not rebuild, patch, or selectively replace
HQ_converter files.

The release is intentionally ignored by Git. It is supplied separately by the
developer, build environment, or release/dependency preparation process.

## Build and packaging boundary

The `ffmpeg-converter` build consumes the prepared release as an input:

1. validate that the expected release is present;
2. generate or validate the HQ codec and preset catalog from the release preset
   file;
3. copy the complete release to the executable-adjacent
   `bin/hq_converter/` directory;
4. include that same complete directory in AppImage and application bundles.

The application must use the copied runtime tree and must not depend on the
original source checkout after installation or packaging.

The generated program preset file contains the runtime capability flag
`features.hq_converter`. Its default value is `false`. During Linux/macOS
CMake configuration it becomes `true` only when the prepared release passes the
build-time presence checks. Windows builds always generate `false`.

This flag controls whether the application may inspect the HQ preset file and
expose HQ_converter in its runtime catalog. Phase 3 implements the actual HQ
execution path; this build step only prepares the capability value.

## Application immutability

After `ffmpeg-converter` is built, the result is treated as a monolithic,
self-contained application. The installed or packaged application must not
modify, update, regenerate, or download the HQ_converter release at runtime.

Updating HQ_converter requires preparing a new external release, placing it in
`third_party/hq_converter/`, and rebuilding `ffmpeg-converter`.

## Platform scope

HQ_converter is supported only by the Linux and macOS C/CMake implementations.
Windows builds do not discover, parse, copy, package, or expose HQ_converter.
The Windows Pascal implementation has no HQ_converter dependency.