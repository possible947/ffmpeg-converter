using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using FfmpegConverter.Core.Presets;

namespace FfmpegConverter.Core.Probing;

public class HardwareProbeResult
{
    public HashSet<string> SupportedEncoders { get; } = new(StringComparer.OrdinalIgnoreCase);
    public List<string> VulkanDevices { get; } = new();
    public int SelectedVulkanDeviceIndex { get; set; } = -1;
}

public static class HardwareProbe
{
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

        try
        {
            using var process = new Process
            {
                StartInfo = new ProcessStartInfo
                {
                    FileName = ffmpegPath,
                    Arguments = args,
                    UseShellExecute = false,
                    CreateNoWindow = true,
                    RedirectStandardError = true,
                    RedirectStandardOutput = true
                }
            };

            process.Start();
            await process.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(5)); // timeout safety
            return process.ExitCode == 0;
        }
        catch
        {
            return false;
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
            encodersToProbe.Add(codec);
        }

        // Run probes in parallel for insane performance compared to C/Pascal sequential probing!
        var tasks = new List<(string Name, Task<bool> Task)>();
        foreach (var encoder in encodersToProbe)
        {
            tasks.Add((encoder, ProbeEncoderAsync(ffmpegPath, encoder)));
        }

        await Task.WhenAll(tasks.Select(t => t.Task));

        foreach (var item in tasks)
        {
            if (await item.Task)
            {
                result.SupportedEncoders.Add(item.Name);
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
