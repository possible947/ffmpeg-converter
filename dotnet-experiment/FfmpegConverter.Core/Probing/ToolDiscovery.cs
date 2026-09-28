using System;
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

public static class ToolDiscovery
{
    public static ToolPaths ResolveAll()
    {
        return new ToolPaths
        {
            Ffmpeg = Resolve("ffmpeg"),
            Ffprobe = Resolve("ffprobe"),
            Mkvmerge = Resolve("mkvmerge"),
            Mp4Box = Resolve("MP4Box")
        };
    }

    public static string Resolve(string toolName)
    {
        string exeName = RuntimeInformation.IsOSPlatform(OSPlatform.Windows) ? $"{toolName}.exe" : toolName;

        // 1. Resolve from Env Vars
        string? envVar = toolName.ToUpperInvariant();
        if (toolName.Equals("mkvmerge", StringComparison.OrdinalIgnoreCase)) envVar = "MKVMERGE_BIN";
        else if (toolName.Equals("MP4Box", StringComparison.OrdinalIgnoreCase)) envVar = "MP4BOX_BIN";
        else envVar = $"{envVar}_BIN";

        string? envPath = Environment.GetEnvironmentVariable(envVar);
        if (string.IsNullOrEmpty(envPath))
        {
            // fallback to legacy/secondary env names
            envPath = Environment.GetEnvironmentVariable(toolName.ToUpperInvariant());
        }

        if (!string.IsNullOrEmpty(envPath) && File.Exists(envPath))
        {
            return envPath;
        }

        // 2. Resolve from Exe Dir
        string exeDir = AppContext.BaseDirectory;
        if (!string.IsNullOrEmpty(exeDir))
        {
            string localPath = Path.Combine(exeDir, exeName);
            if (File.Exists(localPath)) return localPath;

            // Also check special platform subdirectory in repo structure
            string platformFolder = RuntimeInformation.IsOSPlatform(OSPlatform.Windows) ? "windows" :
                                    RuntimeInformation.IsOSPlatform(OSPlatform.OSX) ? "macos" : "linux";
            
            // Try to find in typical repo structures: bin/ or src/platform/{os}/bin/
            string parent = exeDir;
            for (int i = 0; i < 5; i++)
            {
                if (string.IsNullOrEmpty(parent)) break;
                
                string candidate = Path.Combine(parent, "src", "platform", platformFolder, "bin", exeName);
                if (File.Exists(candidate)) return candidate;

                candidate = Path.Combine(parent, "bin", exeName);
                if (File.Exists(candidate)) return candidate;

                parent = Path.GetDirectoryName(parent) ?? "";
            }
        }

        // 3. MacOS specific locations
        if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
        {
            string[] macPaths = {
                $"/opt/local/bin/{toolName}8",
                $"/opt/local/bin/{toolName}",
                $"/opt/homebrew/bin/{toolName}",
                $"/usr/local/bin/{toolName}"
            };

            foreach (var p in macPaths)
            {
                if (File.Exists(p)) return p;
            }
        }

        // 4. Resolve from system PATH
        string? pathEnv = Environment.GetEnvironmentVariable("PATH");
        if (!string.IsNullOrEmpty(pathEnv))
        {
            char separator = RuntimeInformation.IsOSPlatform(OSPlatform.Windows) ? ';' : ':';
            string[] paths = pathEnv.Split(separator, StringSplitOptions.RemoveEmptyEntries);

            foreach (var p in paths)
            {
                string fullPath = Path.Combine(p, exeName);
                if (File.Exists(fullPath))
                {
                    return fullPath;
                }
            }
        }

        return "";
    }
}
