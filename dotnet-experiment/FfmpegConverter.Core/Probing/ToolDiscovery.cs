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

            // Try to find in typical repo structures: bin/ or src/platform/{os}/bin/.
            // Walk all the way up to the filesystem root (not a fixed depth) — build/publish
            // output directories vary in nesting (Debug/Release, RID-specific publish dirs,
            // self-contained single-file, etc.), and a fixed-count loop previously stopped
            // one level short of the actual repo root in the standard `dotnet build` layout,
            // silently falling through to system PATH instead of the bundled binary. This
            // mirrors the unbounded ancestor walk already used by PresetDb.ResolvePresetsPath().
            string? parent = exeDir;
            while (!string.IsNullOrEmpty(parent))
            {
                string candidate = Path.Combine(parent, "src", "platform", platformFolder, "bin", exeName);
                if (File.Exists(candidate)) return candidate;

                // Only trust a generic "bin/<exe>" match once `parent` is recognizably the
                // ffmpeg-converter repo root (marked by presets.json living there). Without
                // this guard, walking unbounded can "find" unrelated system directories once
                // it reaches the filesystem root (e.g. "/bin/ffmpeg" on distros where /bin
                // exists as a real top-level dir), silently bypassing the intended PATH
                // fallback for a plain system install outside the repo checkout.
                if (File.Exists(Path.Combine(parent, "presets.json")))
                {
                    candidate = Path.Combine(parent, "bin", exeName);
                    if (File.Exists(candidate)) return candidate;

                    // Found the repo root but no bundled binary there — stop climbing;
                    // anything further up is outside the project tree.
                    break;
                }

                parent = Directory.GetParent(parent)?.FullName;
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
