# Displayed Avalonia progress checks on GNOME Wayland

This is an opt-in, displayed application smoke test, not a headless replacement
for the normal xUnit suite. It reuses the startup-hook approach that previously
controlled the real Avalonia window on Fedora. No application startup code,
backend selection, dependencies, Core events, or media commands are replaced.

## Session and window preflight

Run as the logged-in desktop user from that user's graphical terminal or VS Code
terminal. Preserve the existing `XDG_SESSION_TYPE`, `XDG_CURRENT_DESKTOP`,
`DESKTOP_SESSION`, `WAYLAND_DISPLAY`, `DISPLAY`, `XAUTHORITY`,
`DBUS_SESSION_BUS_ADDRESS`, and `XDG_RUNTIME_DIR`. Do not copy another session's
authorization file, hardcode its filename, or switch the desktop/backend.

The verified desktop was GNOME/Wayland, but this application used **X11 through
XWayland**. Evidence: its native handle was an enumerable X window with
`_NET_WM_PID` matching the GUI process. The shell could connect to both the user
D-Bus and `org.a11y.Bus`; AT-SPI exposed `Avalonia Application` with the same PID
and a `FFmpeg Converter` frame.

Check session access before conversion:

```sh
loginctl list-sessions
busctl --user --no-pager list
gdbus call --session --dest org.a11y.Bus --object-path /org/a11y/bus \
  --method org.a11y.Bus.GetAddress
```

Do not identify the client by title alone. Mutter also creates an X frame named
`FFmpeg Converter`. Recursively enumerate X windows, match `_NET_WM_PID` to the
launched process, and use that client ID for `import -window CLIENT_ID file.png`.
The startup-hook driver obtains this ID from the actual `MainWindow` platform
handle, so it does not need global mouse coordinates.

For an independent accessibility interaction, using the existing `pyatspi`
installation and a freshly launched app's PID:

```python
import pyatspi

pid = ...  # PID of the GUI just launched, not a worker or a previous run
app = next(a for a in pyatspi.Registry.getDesktop(0) if a.get_process_id() == pid)

def descendants(node):
    yield node
    for child in node:
        yield from descendants(child)

control = next(a for a in descendants(app)
               if a.name == "Overwrite existing files"
               and "Action" in pyatspi.listInterfaces(a))
before = control.getState().contains(pyatspi.STATE_CHECKED)
assert control.queryComponent().grabFocus()
assert control.queryAction().doAction(0)
# Wait/poll until STATE_CHECKED differs, capture the client window, then toggle
# back and verify the original state. Close via WM_DELETE_WINDOW.
```

Poll for readiness instead of using a fixed sleep. The driver waits up to
60 seconds for the displayed main window, tool probing, and audio choices.
It activates the window, focuses/toggles/restores the harmless overwrite control,
saves `preflight.json` and a screenshot, and exits cleanly in preflight mode.

## Build and launch the existing test driver

From the repository root, with the ordinary Debug GUI already built:

```sh
dotnet build dotnet-experiment/tests/ui/UiSmoke.csproj
export DOTNET_STARTUP_HOOKS="$PWD/dotnet-experiment/tests/ui/bin/Debug/net10.0/UiSmoke.dll"
export FFC_UI_EVIDENCE="$(mktemp -d /tmp/ffc-ui-evidence.XXXXXX)"
FFC_UI_SCENARIO=preflight \
  dotnet-experiment/FfmpegConverter.Gui/bin/Debug/net10.0/FfmpegConverter.Gui
cat "$FFC_UI_EVIDENCE/preflight.json"
unset DOTNET_STARTUP_HOOKS
```

The driver references the already-built GUI assemblies, with no NuGet packages
or production project changes. Override `GuiBuildDir` when building the driver
against a different existing build. It runs inside the **visible, normal GUI**
on its UI dispatcher, changes actual selections, adds fixture paths to the
queue's existing collection, sets the replacement path as the picker does,
refreshes normal audio choices, and awaits the normal conversion handler.
It does not inject progress, mock tools, or invoke Core in place of the GUI.
It skips HQ worker processes. File-picker interaction itself is out of scope.

Required tools: .NET 10, the prepared application/HQ runtime and supported
FFmpeg/ffprobe, mkvmerge, MP4Box, and ImageMagick `import` for X client captures.
AT-SPI/`pyatspi` is optional for independent accessibility preflight.

Use software-only fixtures and leave `FFMPEG_CONVERTER_VAAPI_REAL_PROBE=0`.
For an entirely software-only smoke session, the earlier working recipe used a
test-local `FFMPEG_BIN` wrapper that refuses the hardware-probe input
`color=size=1920x1080:rate=1` and otherwise `exec`s the real bundled FFmpeg with
unchanged arguments. This disables capability confirmation for that invocation
only; do not change production probes or enable real GPU probes for UI testing.

## Focused suite

Prepare these generated files outside the repository:

| File | Deterministic fixture |
|---|---|
| `valid-1.mkv`, `valid-2.mkv`, `valid-3.mkv` | Separate names/hardlinks for 20 seconds of 1280x720/24fps `testsrc2`, H.264 ultrafast, PCM sine audio |
| `corrupt.mkv` | 64 zero bytes, intentionally invalid |
| `mux-source.mkv` | 61 seconds of 640x360/24fps `testsrc2`, H.264 ultrafast, PCM sine audio |
| `replacement.mp4` | Video-only stream copy of `mux-source.mkv` |
| `hq-source.mkv` | 61 seconds of 160x90/24fps `testsrc2`, H.264 ultrafast, PCM sine audio; explicit BT.709 color metadata |

Example fixture creation (set `fixtures` to a new external directory and
`ffmpeg` to the supported bundled executable):

```sh
mkdir -p "$fixtures"
"$ffmpeg" -v error -f lavfi -i testsrc2=size=1280x720:rate=24 \
  -f lavfi -i sine=frequency=440:sample_rate=48000 -t 20 \
  -c:v libx264 -preset ultrafast -crf 20 -pix_fmt yuv420p \
  -color_primaries bt709 -color_trc bt709 -colorspace bt709 \
  -c:a pcm_s16le "$fixtures/valid-1.mkv"
ln "$fixtures/valid-1.mkv" "$fixtures/valid-2.mkv"
ln "$fixtures/valid-1.mkv" "$fixtures/valid-3.mkv"
# Repeat the first command with the dimensions/durations above for mux-source
# and hq-source, retaining the color metadata and PCM audio.
"$ffmpeg" -v error -i "$fixtures/mux-source.mkv" -map 0:v -c copy \
  "$fixtures/replacement.mp4"
head -c 64 /dev/zero > "$fixtures/corrupt.mkv"
```

After preflight passes, launch with:

```sh
env DOTNET_STARTUP_HOOKS="$PWD/dotnet-experiment/tests/ui/bin/Debug/net10.0/UiSmoke.dll" \
  FFC_UI_FIXTURES="$fixtures" \
  FFC_UI_OUTPUT="$(mktemp -d /tmp/fcu.XXXXXX)" \
  FFC_UI_EVIDENCE="$(mktemp -d /tmp/ffc-ui-evidence.XXXXXX)" \
  FFMPEG_BIN="$ffmpeg" FFPROBE_BIN="$ffprobe" \
  FFMPEG_CONVERTER_VAAPI_REAL_PROBE=0 \
  dotnet-experiment/FfmpegConverter.Gui/bin/Debug/net10.0/FfmpegConverter.Gui
```

Use a **fresh output root** each time. Keep it short: HQ uses Unix-domain sockets
under its working directory and fails if the resulting socket path is too long.
`FFC_UI_SCENARIO` optionally selects one or comma-separated scenarios:
`legacy-single`, `legacy-queue`, `legacy-error-queue`, `mux-mkv`, `mux-mov`,
`mux-m4v`, `hq-mux`. Unset it to run all seven. MOV uses PCM; M4V uses
`fdk_aac_320` and its GUI preset is `default`, not `m4v`.

Each scenario writes actual StatusLabel/DetailsLabel/ProgressBar property-change
samples, the GUI console, ffprobe results, and client-window PNGs. JSON samples
are the precise evidence for short-lived tool percentages; asynchronous PNGs
can capture a later frame than the stage that requested them. Samples include
individual property assignments, so adjacent samples may contain a new label
with the preceding bar value before the snapshot's final assignment.
`FFC_UI_SCREENSHOTS=terminal` disables intermediate PNG requests when diagnosing
capture interference. `results.json` lists successful scenarios; `error.txt`
and a nonzero exit identify a failed assertion. No media/evidence is committed.

## Confirmed pitfalls and presentation regression

The earlier failed attempt used ad-hoc XTest clicks rather than the previously
successful startup hook. Its first ctypes call omitted XTest argument types
(and the display-pointer type for close), and the Python process segfaulted.
Subsequent attempts used hardcoded coordinates and a root-level title match
without client/PID validation; the reported frame extended beyond the X screen.
This is a **TEST_HARNESS_DEFECT**, not evidence that XWayland is generally
unusable. Prefer AT-SPI actions or the displayed startup hook. Declare all
ctypes signatures if low-level Xlib is ever necessary.

Focused testing also exposed a separate real presenter race: latest-generated
revision filtering discarded queued assembly percentages and short verification
stages when Core advanced before the dispatcher applied them. Screenshots on the
UI thread made it easier to reproduce, but it also occurred with intermediate
screenshots disabled. Display acceptance now preserves ordered current-file
snapshots, rejects revisions older than an already displayed snapshot, and
drains pending UI updates before marking the run terminal. Two deterministic
presenter regressions cover ordered delivery and out-of-order rejection.

If display authorization, D-Bus, window ownership, focus, or the harmless
interaction fails, stop before media tests. Report **ENVIRONMENT FAILURE** and
**APPLICATION UI RESULT: NOT TESTED**, rather than changing application settings.
Native Wayland, portal/file-picker automation, hardware encoders, and exhaustive
HQ encoder coverage are outside this recipe.
