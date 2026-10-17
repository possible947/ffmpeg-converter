using System.IO;

namespace FfmpegConverter.Core;

internal static class PreparedReleaseLayout
{
    public static string? FindRoot(string executableDirectory)
    {
        if (string.IsNullOrEmpty(executableDirectory))
            return null;

        string? parent = Directory.GetParent(Path.TrimEndingDirectorySeparator(executableDirectory))?.FullName;
        return parent != null && File.Exists(Path.Combine(parent, "ffmpeg-converter.release"))
            ? parent
            : null;
    }
}
