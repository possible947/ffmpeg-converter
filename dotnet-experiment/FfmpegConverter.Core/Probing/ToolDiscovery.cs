using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;

namespace FfmpegConverter.Core.Probing;

public class ToolPaths
{
    public string Ffmpeg { get; set; } = "";
    public string Ffprobe { get; set; } = "";
    public string Mkvmerge { get; set; } = "";
    public string Mp4Box { get; set; } = "";
}

public sealed record ToolDiscoveryContext(
    string ExecutableDirectory,
    IReadOnlyDictionary<string, string> EnvironmentVariables,
    string PathValue,
    string Platform,
    IReadOnlyList<string> MacToolDirectories)
{
    public static ToolDiscoveryContext FromCurrentEnvironment()
    {
        var environmentVariables = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        foreach (string name in new[] { "FFMPEG_BIN", "FFMPEG", "FFPROBE_BIN", "FFPROBE", "MKVMERGE_BIN", "MKVMERGE", "MP4BOX_BIN", "MP4BOX", "PATH" })
        {
            string? value = Environment.GetEnvironmentVariable(name);
            if (value != null)
                environmentVariables[name] = value;
        }

        string platform = RuntimeInformation.IsOSPlatform(OSPlatform.Windows) ? "windows" :
            RuntimeInformation.IsOSPlatform(OSPlatform.OSX) ? "macos" : "linux";
        return new ToolDiscoveryContext(
            AppContext.BaseDirectory,
            environmentVariables,
            Environment.GetEnvironmentVariable("PATH") ?? "",
            platform,
            new[] { "/opt/local/bin", "/opt/homebrew/bin", "/usr/local/bin" });
    }
}

public static class ToolDiscovery
{
    public static ToolPaths ResolveAll() => ResolveAll(ToolDiscoveryContext.FromCurrentEnvironment());

    public static ToolPaths ResolveAll(ToolDiscoveryContext context)
    {
        return new ToolPaths
        {
            Ffmpeg = Resolve("ffmpeg", context),
            Ffprobe = Resolve("ffprobe", context),
            Mkvmerge = Resolve("mkvmerge", context),
            Mp4Box = Resolve("MP4Box", context)
        };
    }

    public static string Resolve(string toolName) => Resolve(toolName, ToolDiscoveryContext.FromCurrentEnvironment());

    public static string Resolve(string toolName, ToolDiscoveryContext context)
    {
        string exeName = IsWindows(context) ? $"{toolName}.exe" : toolName;

        string? envPath = GetEnvironmentValue(context, GetPrimaryEnvironmentVariable(toolName));
        if (string.IsNullOrEmpty(envPath))
            envPath = GetEnvironmentValue(context, toolName.ToUpperInvariant());
        if (!string.IsNullOrEmpty(envPath) && File.Exists(envPath))
            return envPath;

        if (!string.IsNullOrEmpty(context.ExecutableDirectory))
        {
            string localPath = Path.Combine(context.ExecutableDirectory, exeName);
            if (File.Exists(localPath))
                return localPath;

            string platformFolder = context.Platform;
            string? parent = context.ExecutableDirectory;
            while (!string.IsNullOrEmpty(parent))
            {
                string candidate = Path.Combine(parent, "src", "platform", platformFolder, "bin", exeName);
                if (File.Exists(candidate))
                    return candidate;

                if (File.Exists(Path.Combine(parent, "presets.json")))
                {
                    candidate = Path.Combine(parent, "bin", exeName);
                    if (File.Exists(candidate))
                        return candidate;

                    // Stop at the recognized repository root rather than searching unrelated ancestors.
                    break;
                }

                parent = Directory.GetParent(parent)?.FullName;
            }
        }

        if (context.Platform.Equals("macos", StringComparison.OrdinalIgnoreCase))
        {
            for (int index = 0; index < context.MacToolDirectories.Count; index++)
            {
                string directory = context.MacToolDirectories[index];
                string candidate;
                if (index == 0)
                {
                    candidate = Path.Combine(directory, $"{toolName}8");
                    if (File.Exists(candidate))
                        return candidate;
                }

                candidate = Path.Combine(directory, toolName);
                if (File.Exists(candidate))
                    return candidate;
            }
        }

        foreach (string directory in context.PathValue.Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries))
        {
            string candidate = Path.Combine(directory, exeName);
            if (File.Exists(candidate))
                return candidate;
        }

        return "";
    }

    private static bool IsWindows(ToolDiscoveryContext context) =>
        context.Platform.Equals("windows", StringComparison.OrdinalIgnoreCase);

    private static string GetPrimaryEnvironmentVariable(string toolName)
    {
        if (toolName.Equals("mkvmerge", StringComparison.OrdinalIgnoreCase))
            return "MKVMERGE_BIN";
        if (toolName.Equals("MP4Box", StringComparison.OrdinalIgnoreCase))
            return "MP4BOX_BIN";
        return $"{toolName.ToUpperInvariant()}_BIN";
    }

    private static string? GetEnvironmentValue(ToolDiscoveryContext context, string name) =>
        context.EnvironmentVariables.TryGetValue(name, out string? value) ? value : null;
}
