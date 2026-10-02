using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using FfmpegConverter.Core.Presets;

namespace FfmpegConverter.Core.Probing;

public class HardwareProbeResult
{
    public HashSet<string> SupportedEncoders { get; } = new(StringComparer.OrdinalIgnoreCase);
    public HashSet<string> SupportedDecoders { get; } = new(StringComparer.OrdinalIgnoreCase);
    public List<string> VulkanDevices { get; } = new();
    public int SelectedVulkanDeviceIndex { get; set; } = -1;
    public string? SelectedVaapiRenderNode { get; set; }
    public bool HasLibdav1dDec => SupportedDecoders.Contains("libdav1d");
    public bool HasAacAt => SupportedEncoders.Contains("aac_at");
    public bool HasLibfdkAac => SupportedEncoders.Contains("libfdk_aac");
    public bool HasAac => SupportedEncoders.Contains("aac");
}

public static class HardwareProbe
{
    private static readonly TimeSpan ProbeTimeout = TimeSpan.FromSeconds(8);

    private sealed class ProbeRunResult
    {
        public int ExitCode;
        public bool TimedOut;
        // True when the process terminated abnormally (crashed/killed-by-signal or timed
        // out), as opposed to a normal non-zero "unsupported" exit. On POSIX, .NET surfaces
        // a signal-terminated process via Process.ExitCode as 128 + signal number (mirrors
        // the shell $? convention), which is what we use to distinguish the two cases.
        public bool Crashed;
        public string Output = "";
    }

    // Crash-safe subprocess runner shared by all real-encode probes: enforces a timeout and
    // kills (SIGKILL on Linux) rather than leaving an orphaned ffmpeg process behind, and
    // flags abnormal termination distinctly from a normal "unsupported" non-zero exit.
    private static async Task<ProbeRunResult> RunProbeProcessAsync(string fileName, string args, TimeSpan timeout, bool captureStdout = false)
    {
        var result = new ProbeRunResult();
        using var process = new Process
        {
            StartInfo = new ProcessStartInfo
            {
                FileName = fileName,
                Arguments = args,
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardError = true,
                RedirectStandardOutput = true
            }
        };

        try
        {
            process.Start();
            Task<string>? stdoutTask = captureStdout ? process.StandardOutput.ReadToEndAsync() : null;

            using var cts = new CancellationTokenSource(timeout);
            try
            {
                await process.WaitForExitAsync(cts.Token);
            }
            catch (OperationCanceledException)
            {
                result.TimedOut = true;
                try { process.Kill(entireProcessTree: true); } catch { /* best effort */ }
                try { await process.WaitForExitAsync(); } catch { /* already gone */ }
            }

            if (stdoutTask != null)
            {
                try { result.Output = await stdoutTask; } catch { /* ignore */ }
            }

            result.ExitCode = result.TimedOut ? -1 : process.ExitCode;
            result.Crashed = result.TimedOut || process.ExitCode >= 128;
        }
        catch
        {
            result.ExitCode = -1;
            result.Crashed = true;
        }

        return result;
    }

    public static async Task<bool> ProbeEncoderAsync(string ffmpegPath, string encoderName, string extraArgs = "")
    {
        if (string.IsNullOrEmpty(ffmpegPath) || !File.Exists(ffmpegPath)) return false;

        string resolution = "1920x1080"; // standard 1080p frame as per repository memories
        if (encoderName.Contains("_10bit"))
        {
            // use 10bit probing argument format
            encoderName = encoderName.Replace("_10bit", "");
            extraArgs += " -vf format=p010le -profile:v main10";
        }

        string args = $"-v error -hide_banner -f lavfi -i color=size={resolution}:rate=1 -frames:v 1 {extraArgs} -c:v {encoderName} -f null -";
        var result = await RunProbeProcessAsync(ffmpegPath, args, ProbeTimeout);
        return !result.Crashed && result.ExitCode == 0;
    }

    public static async Task<bool> ProbeAudioEncoderAsync(string ffmpegPath, string encoderName)
    {
        if (string.IsNullOrEmpty(ffmpegPath) || !File.Exists(ffmpegPath)) return false;

        string args = $"-v error -hide_banner -f lavfi -i anullsrc=r=48000:cl=stereo -t 0.1 -c:a {encoderName} -f null -";
        var result = await RunProbeProcessAsync(ffmpegPath, args, ProbeTimeout);
        return !result.Crashed && result.ExitCode == 0;
    }

    public static async Task<bool> ProbeDecoderAsync(string ffmpegPath, string decoderName)
    {
        if (string.IsNullOrEmpty(ffmpegPath) || !File.Exists(ffmpegPath)) return false;

        string args = "-hide_banner -v error -decoders";
        var result = await RunProbeProcessAsync(ffmpegPath, args, ProbeTimeout, captureStdout: true);
        if (result.Crashed) return false;

        return result.Output.Contains($" {decoderName} ", StringComparison.OrdinalIgnoreCase) ||
               result.Output.Contains($" {decoderName}\n", StringComparison.OrdinalIgnoreCase) ||
               result.Output.Contains($" {decoderName}\r", StringComparison.OrdinalIgnoreCase);
    }

    // --------------------------------------------------------------------------
    // VAAPI safety policy: real one-frame encode probes are OPT-IN ONLY.
    //
    // Postmortem (C/CMake sibling implementation, same repo, src/platform/linux/
    // runtime_probe.c): a real VAAPI encode probe submits GPU-ring commands on the
    // same physical adapter the compositor is concurrently rendering on. On at
    // least one AMD (amdgpu) system this produced a genuine kernel-level
    // VM_L2_PROTECTION_FAULT / gfx-ring timeout, and the *compositor itself*
    // (GNOME Shell/Mutter), not the ffmpeg probe process, crashed and dumped
    // core. This is a GPU hardware/ring-level race, not something a process
    // boundary or timeout can fence off — so by default we never run a real
    // encode for VAAPI capability detection.
    //
    // Default (safe): rely solely on the read-only `vainfo` profile/entrypoint
    // listing for the exact render node (no surface/frame allocation, cannot
    // crash anything) as the final authority. Fails closed (reports
    // unsupported) if vainfo itself is unavailable.
    //
    // Opt-in (stricter, riskier): set FFMPEG_CONVERTER_VAAPI_REAL_PROBE=1 to
    // additionally confirm with the real one-frame encode for profiles vainfo
    // already reports as present. Never runs for profiles vainfo doesn't list.
    // --------------------------------------------------------------------------

    public const string VaapiH264Profile = "VAProfileH264Main/VAEntrypointEncSlice";
    public const string VaapiHevcProfile = "VAProfileHEVCMain/VAEntrypointEncSlice";
    public const string VaapiHevc10Profile = "VAProfileHEVCMain10/VAEntrypointEncSlice";
    public const string VaapiAv1Profile = "VAProfileAV1Main/VAEntrypointEncSlice";
    public const string VaapiAv110Profile = "VAProfileAV1Main10/VAEntrypointEncSlice";

    public static bool VaapiRealEncodeProbeEnabled()
    {
        string? v = Environment.GetEnvironmentVariable("FFMPEG_CONVERTER_VAAPI_REAL_PROBE");
        return !string.IsNullOrEmpty(v) && v != "0";
    }

    // Read-only capability pre-filter: runs `vainfo --display drm --device <node> -a`
    // once per render node and returns its captured text, or null if vainfo is
    // unavailable/failed/produced no output (never touches the GPU encode path).
    public static async Task<string?> GetVaapiNodeProfileTextAsync(string renderNode)
    {
        if (string.IsNullOrEmpty(renderNode)) return null;

        string args = $"--display drm --device \"{renderNode}\" -a";
        var result = await RunProbeProcessAsync("vainfo", args, TimeSpan.FromSeconds(5), captureStdout: true);
        if (result.Crashed || string.IsNullOrWhiteSpace(result.Output)) return null;
        return result.Output;
    }

    // Returns 1 if profileEntrypoint (e.g. VaapiHevc10Profile) appears in profileText,
    // 0 if profileText exists but doesn't mention it, or -1 if profileText is null
    // (vainfo unavailable/failed).
    public static int VaapiProfileListed(string? profileText, string profileEntrypoint)
    {
        if (string.IsNullOrEmpty(profileText)) return -1;
        return profileText.Contains(profileEntrypoint, StringComparison.Ordinal) ? 1 : 0;
    }

    private static async Task<(bool Supported, bool Crashed)> ProbeVaapiRealEncodeAsync(string ffmpegPath, string renderNode, string encoderName, bool is10Bit)
    {
        string format = is10Bit ? "p010le" : "nv12";
        string profileArg = is10Bit ? " -profile:v main10" : "";
        string args = $"-v error -hide_banner -init_hw_device vaapi=va:\"{renderNode}\" " +
                       $"-f lavfi -i color=size=1920x1080:rate=1 -frames:v 1 " +
                       $"-vf \"format={format},hwupload\"{profileArg} -c:v {encoderName} -f null -";
        var result = await RunProbeProcessAsync(ffmpegPath, args, ProbeTimeout);
        return (!result.Crashed && result.ExitCode == 0, result.Crashed);
    }

    // Resolves whether a VAAPI profile should be reported as supported, per the safety
    // policy above: the vainfo listing is authoritative by default; the real encode only
    // runs as an opt-in extra confirmation, and only when vainfo already says the profile
    // is present. A missing/unavailable vainfo fails CLOSED (unsupported) rather than
    // silently falling back to the risky real-encode path. Returns (supported, crashed) —
    // crashed is only ever true when the opt-in real-encode confirmation was attempted.
    public static async Task<(bool Supported, bool Crashed)> VaapiProfileSupportedAsync(
        string ffmpegPath, string renderNode, string encoderName, bool is10Bit,
        string? profileText, string profileEntrypoint)
    {
        if (VaapiProfileListed(profileText, profileEntrypoint) <= 0)
            return (false, false); // not listed, or vainfo unavailable — fail closed

        if (!VaapiRealEncodeProbeEnabled())
            return (true, false); // safe default: vainfo listing is authoritative

        return await ProbeVaapiRealEncodeAsync(ffmpegPath, renderNode, encoderName, is10Bit);
    }

    private static IEnumerable<string> GetVaapiRenderNodes()
    {
        try
        {
            if (!Directory.Exists("/dev/dri")) return Array.Empty<string>();
            var nodes = new List<string>(Directory.GetFiles("/dev/dri", "renderD*"));
            nodes.Sort(StringComparer.Ordinal);
            return nodes;
        }
        catch
        {
            return Array.Empty<string>();
        }
    }

    public static async Task<HardwareProbeResult> ProbeCapabilitiesAsync(string ffmpegPath, PresetDb presetDb)
    {
        var result = new HardwareProbeResult();
        if (string.IsNullOrEmpty(ffmpegPath) || !File.Exists(ffmpegPath)) return result;

        string platform = RuntimeInformation.IsOSPlatform(OSPlatform.Windows) ? "windows" :
                         RuntimeInformation.IsOSPlatform(OSPlatform.OSX) ? "macos" : "linux";

        var encodersToProbe = new List<string>();
        foreach (var codec in presetDb.GetCodecs(platform))
        {
            if (codec.Equals("copy", StringComparison.OrdinalIgnoreCase) || 
                codec.Equals("mux", StringComparison.OrdinalIgnoreCase) || 
                codec.Equals("m4v", StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }

            // VAAPI codecs are probed separately below with crash-safe, render-node-aware,
            // vainfo-gated logic (see the "VAAPI safety policy" comment above). Probing them
            // here would run an unguarded real encode missing -init_hw_device/hwupload.
            if (platform == "linux" && codec.Contains("_vaapi", StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }

            encodersToProbe.Add(codec);
        }

        // Also check common audio encoders
        string[] audioEncoders = { "aac_at", "libfdk_aac", "aac" };
        foreach (var aEnc in audioEncoders)
        {
            encodersToProbe.Add(aEnc);
        }

        // Run probes in parallel for insane performance compared to C/Pascal sequential probing!
        var tasks = new List<(string Name, Task<bool> Task)>();
        foreach (var encoder in encodersToProbe)
        {
            if (encoder == "aac_at" || encoder == "libfdk_aac" || encoder == "aac")
            {
                tasks.Add((encoder, ProbeAudioEncoderAsync(ffmpegPath, encoder)));
            }
            else
            {
                tasks.Add((encoder, ProbeEncoderAsync(ffmpegPath, encoder)));
            }
        }

        await Task.WhenAll(tasks.Select(t => t.Task));

        foreach (var item in tasks)
        {
            if (await item.Task)
            {
                result.SupportedEncoders.Add(item.Name);
            }
        }

        // Probe decoders (especially libdav1d)
        if (await ProbeDecoderAsync(ffmpegPath, "libdav1d"))
        {
            result.SupportedDecoders.Add("libdav1d");
        }

        // VAAPI — crash-safe, render-node-aware detection. Default mode never runs a real
        // encode (vainfo profile listing is authoritative); set
        // FFMPEG_CONVERTER_VAAPI_REAL_PROBE=1 to opt into real-encode confirmation. See the
        // "VAAPI safety policy" comment above for full rationale.
        if (platform == "linux")
        {
            var vaapiChecks = new (string FinalCodec, string Encoder, bool Is10Bit, string Profile)[]
            {
                ("h264_vaapi", "h264_vaapi", false, VaapiH264Profile),
                ("hevc_vaapi", "hevc_vaapi", false, VaapiHevcProfile),
                ("hevc_vaapi_10bit", "hevc_vaapi", true, VaapiHevc10Profile),
                ("av1_vaapi", "av1_vaapi", false, VaapiAv1Profile),
                ("av1_vaapi_10bit", "av1_vaapi", true, VaapiAv110Profile),
            };

            foreach (var renderNode in GetVaapiRenderNodes())
            {
                string? profileText = await GetVaapiNodeProfileTextAsync(renderNode);
                bool nodeUnstable = false;

                foreach (var check in vaapiChecks)
                {
                    if (nodeUnstable) break; // crash-abort-cascading: stop probing this node

                    var (supported, crashed) = await VaapiProfileSupportedAsync(
                        ffmpegPath, renderNode, check.Encoder, check.Is10Bit, profileText, check.Profile);

                    if (crashed)
                    {
                        nodeUnstable = true;
                        continue;
                    }

                    if (supported)
                    {
                        result.SupportedEncoders.Add(check.FinalCodec);
                        result.SelectedVaapiRenderNode ??= renderNode;
                    }
                }
            }
        }

        // Software Vulkan device filtering via vulkaninfo
        if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux) || RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
        {
            try
            {
                string cmd = RuntimeInformation.IsOSPlatform(OSPlatform.Windows) ? "vulkaninfo.exe" : "vulkaninfo";
                using var process = new Process
                {
                    StartInfo = new ProcessStartInfo
                    {
                        FileName = cmd,
                        Arguments = "--summary",
                        UseShellExecute = false,
                        CreateNoWindow = true,
                        RedirectStandardOutput = true,
                        RedirectStandardError = true
                    }
                };

                process.Start();
                string output = await process.StandardOutput.ReadToEndAsync();
                await process.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(3));

                // Regex search for GPU<N> or device Name in summary
                var matches = Regex.Matches(output, @"GPU\d+:\s*(.+)");
                int index = 0;
                foreach (Match m in matches)
                {
                    string gpuName = m.Groups[1].Value.Trim();
                    // exclude software devices like llvmpipe, lavapipe as per repository memories
                    if (!gpuName.Contains("llvmpipe", StringComparison.OrdinalIgnoreCase) && 
                        !gpuName.Contains("lavapipe", StringComparison.OrdinalIgnoreCase))
                    {
                        result.VulkanDevices.Add($"{index}: {gpuName}");
                        if (result.SelectedVulkanDeviceIndex == -1)
                        {
                            result.SelectedVulkanDeviceIndex = index;
                        }
                    }
                    index++;
                }
            }
            catch
            {
                // vulkaninfo not installed or failed, fallback to default
                result.VulkanDevices.Add("0: Default Vulkan Device");
                result.SelectedVulkanDeviceIndex = 0;
            }
        }

        return result;
    }

    // Helper for List extension select method
    private static IEnumerable<TResult> Select<T, TResult>(this IEnumerable<T> source, Func<T, TResult> selector)
    {
        foreach (var item in source)
        {
            yield return selector(item);
        }
    }
}
