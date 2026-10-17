# Plan: adapting the .NET port for Windows

Status: **plan only, not started.** Implementation waits for a Windows host with
real NVIDIA/AMD/Intel/Vulkan hardware. Nothing below can be validated on Linux.

## Premises (settled)

- There has never been a working Windows C implementation (the attempt was
  removed in `b369127`). Windows has only ever shipped the Free Pascal port
  (`fpc/`), and the whole hwaccel kernel (NVENC/AMF/QSV/Vulkan flags, probing,
  device selection) was developed and tested there.
- Reference split:
  - **C (`src/`)** is the reference for Phase 3 mux / M4V / policy behavior.
  - **Pascal (`fpc/`)** is the reference for Windows hwaccel, probing and
    Phase 4 (filter/hardware work).
- Functional parity with C is **not** a goal. The goal is a Windows application
  that reuses the proven Pascal hwaccel solution, debuggable on Linux/macOS for
  everything that is not hardware-specific.
- HQ_converter does not exist on Windows: drop it from the Windows target
  (`HqAdapter`, `HqWorker`, `HqRuntimeLayout`, `HqPresetCatalog`, HQ UI, about
  700 lines). Keep it behind the platform gate or remove it.
- Phase 4 (`docs/v3.0-Phase4.md`) is primarily for Windows; it still reads
  C-first with a Pascal twin and must be rewritten Windows-first.

## Pascal sources to reuse

| Topic | Pascal source |
|---|---|
| Encoder probing (1080p null-encode, Vulkan `vk:0..7`) | `fpc/platform/windows_probe.pas` |
| Hardware flag table, `-init_hw_device`, `-vf` assembly | `fpc/converter/converter_cmd_builder.pas` (`GetHwCodecFlags`) |
| Catalog (identical to Pascal flags) | `presets.json`, `windows` block |
| Mux / MKV tooling on Windows | `fpc/platform/windows_mkvmerge.pas`, `fpc/converter/mux_postprocess.pas` |
| Apple M4V (must match C) | `fpc/converter/apple_m4v_creator.pas` |
| UTF-8 / file helpers | `fpc/platform/windows_utf8.pas`, `windows_file_utils.pas` |

## Known gaps in `FfmpegConverter.Core/Probing/HardwareProbe.cs`

Found by comparing with `windows_probe.pas` (done on Linux, not on Windows):

1. **Vulkan device-name parsing bug.** `GPU\d+:\s*(.+)` matches the line after
   `GPU0:` in real `vulkaninfo --summary` output, so the "name" is
   `apiVersion = ...`. The llvmpipe/lavapipe filter never matches and the GUI
   device list is junk. Fix: parse `deviceName\s*=\s*(.+)` (and `deviceType`)
   per `GPU<N>:` block.
2. **No per-device Vulkan probing.** Pascal tries `vk:0..7` for
   `prores_ks_vulkan` and for h264/hevc/av1_vulkan and keeps separate
   `VulkanDeviceIndex` / `VulkanHwDeviceIndex`. .NET probes Vulkan encoders
   without `-init_hw_device`, so it tests the default device regardless of the
   selected index. Port the loop, skipping software devices.
3. **No cache.** Every CLI/GUI launch re-runs real encodes. Add a disk cache
   keyed on the ffmpeg binary, `presets.json` and a GPU/driver signature (model
   on the Linux cache in `src/platform/linux/runtime_probe.c`).
4. **All probes run in parallel** (`Task.WhenAll`). Serialize per vendor or cap
   concurrency; verify on real hardware that parallel NVENC/AMF/QSV sessions
   do not produce false negatives.
5. **Repo safety rule:** a default real hardware probe needs process isolation
   and a non-invasive fallback. Keep the timeout and tree-kill; add a
   cache-first / opt-out path (e.g. an environment variable mirroring
   `FFMPEG_CONVERTER_VAAPI_REAL_PROBE`).
6. No AV1 decoder selection (`GetBestAV1Decoder`, `-hwaccel none`) is needed on
   Windows (it is `{$IFDEF Linux}` in Pascal); leave it out.

Advantages of the .NET probe to keep: catalog-driven encoder list, timeout with
process-tree kill and crash flag, software-Vulkan filtering, 10-bit variants.

## Work items

1. Fix Vulkan parsing and add unit tests with captured `vulkaninfo` samples
   (Linux-testable).
2. Port the per-device Vulkan probe loop and device-index plumbing
   (`VulkanDevice` in `CommandBuilder`, CLI, GUI).
3. Add the probe disk cache and concurrency limit.
4. Compare the .NET flag/command output with `GetHwCodecFlags` using golden
   command-line tests derived from the Pascal builder (Linux-testable).
5. Remove HQ from the Windows build (platform gate or deletion).
6. Windows packaging: bundled `ffmpeg.exe`/`ffprobe.exe` + DLLs (currently
   copied from `src/platform/windows/bin/` by the Pascal Makefile), mkvmerge /
   MP4Box discovery, and a release workflow replacing the removed
   `windows-msys2.yml`.
7. Rewrite `docs/v3.0-Phase4.md` Windows-first.

## Validation on the Windows host (not yet done)

- NVENC, AMF, QSV: one real encode each for h264/hevc/av1 on the matching GPU.
- Vulkan: multi-GPU index selection, `prores_ks_vulkan`, h264/hevc/av1_vulkan.
- Probe behavior: first run vs cached run, and no false negatives.
- Mux MKV, M4V and Add Track smoke tests with ffprobe verification.
- Avalonia GUI preflight and representative acceptance per
  `tests/ui/README-linux-ui-automation.md` (adapted for Windows).

Report anything not run as NOT TESTED rather than passing.

## Restoring the compact .NET port

An earlier (uncommitted, now deleted) audit recommended restoring the compact
post-Phase-3.3 baseline `31daad5` (188 tests; Core about 4,800 lines vs about
7,500 now). Decision for now: **do not restore**; the current tree is the
starting point. If restoration is revisited, stop if the restored Core exceeds
about 2,000 net lines over that baseline.
