using System;
using System.Collections.Generic;
using System.IO;
using FfmpegConverter.Core.Probing;

namespace FfmpegConverter.Tests;

public class ToolDiscoveryTests
{
    [Fact]
    public void Resolve_EnvironmentOverrideTakesPrecedenceOverExecutableAndPath()
    {
        using var layout = new TemporaryLayout();
        string environmentTool = layout.CreateFile(Path.Combine("environment", "ffmpeg"));
        string executableTool = layout.CreateFile(Path.Combine("executable", "ffmpeg"));
        string pathTool = layout.CreateFile(Path.Combine("path", "ffmpeg"));
        var context = Context(Path.Combine(layout.Root, "executable"), "linux",
            new Dictionary<string, string> { ["FFMPEG_BIN"] = environmentTool },
            Path.GetDirectoryName(pathTool)!);

        Assert.Equal(environmentTool, ToolDiscovery.Resolve("ffmpeg", context));
    }

    [Fact]
    public void Resolve_ExecutableAdjacentToolTakesPrecedenceOverRepositoryAndPath()
    {
        using var layout = new TemporaryLayout();
        string executableDirectory = Path.Combine(layout.Root, "repo", "build", "bin");
        string adjacent = layout.CreateFile(Path.Combine("repo", "build", "bin", "ffmpeg"));
        layout.CreateFile(Path.Combine("repo", "presets.json"));
        layout.CreateFile(Path.Combine("repo", "src", "platform", "linux", "bin", "ffmpeg"));
        string pathTool = layout.CreateFile(Path.Combine("path", "ffmpeg"));
        var context = Context(executableDirectory, "linux", new Dictionary<string, string>(),
            Path.GetDirectoryName(pathTool)!);

        Assert.Equal(adjacent, ToolDiscovery.Resolve("ffmpeg", context));
    }

    [Fact]
    public void Resolve_RepositoryPlatformBundleTakesPrecedenceOverPath()
    {
        using var layout = new TemporaryLayout();
        string executableDirectory = Path.Combine(layout.Root, "repo", "dotnet", "bin");
        layout.CreateFile(Path.Combine("repo", "presets.json"));
        string bundledTool = layout.CreateFile(Path.Combine("repo", "src", "platform", "macos", "bin", "ffprobe"));
        string pathTool = layout.CreateFile(Path.Combine("path", "ffprobe"));
        var context = Context(executableDirectory, "macos", new Dictionary<string, string>(),
            Path.GetDirectoryName(pathTool)!);

        Assert.Equal(bundledTool, ToolDiscovery.Resolve("ffprobe", context));
    }

    [Fact]
    public void Resolve_RepositoryGenericBinIsUsedOnlyForRecognizedRepository()
    {
        using var layout = new TemporaryLayout();
        string executableDirectory = Path.Combine(layout.Root, "repo", "publish");
        layout.CreateFile(Path.Combine("repo", "presets.json"));
        string repoTool = layout.CreateFile(Path.Combine("repo", "bin", "mkvmerge"));
        string pathTool = layout.CreateFile(Path.Combine("path", "mkvmerge"));
        var context = Context(executableDirectory, "linux", new Dictionary<string, string>(),
            Path.GetDirectoryName(pathTool)!);

        Assert.Equal(repoTool, ToolDiscovery.Resolve("mkvmerge", context));
    }

    [Fact]
    public void Resolve_MacPortsVersionIsPreferredBeforeOtherMacAndPathLocations()
    {
        using var layout = new TemporaryLayout();
        string macPortsTool = layout.CreateFile(Path.Combine("macports", "ffmpeg8"));
        string macPortsRegularTool = layout.CreateFile(Path.Combine("macports", "ffmpeg"));
        string homebrewTool = layout.CreateFile(Path.Combine("homebrew", "ffmpeg"));
        string pathTool = layout.CreateFile(Path.Combine("path", "ffmpeg"));
        var context = Context(Path.Combine(layout.Root, "executable"), "macos",
            new Dictionary<string, string>(), Path.GetDirectoryName(pathTool)!) with
        {
            MacToolDirectories = new[]
            {
                Path.Combine(layout.Root, "macports"),
                Path.Combine(layout.Root, "homebrew"),
                Path.Combine(layout.Root, "usr-local")
            }
        };

        Assert.Equal(macPortsTool, ToolDiscovery.Resolve("ffmpeg", context));
        Assert.True(File.Exists(macPortsRegularTool));
        Assert.True(File.Exists(homebrewTool));
    }

    [Fact]
    public void Resolve_SystemPathIsUsedWhenNoHigherPriorityToolExists()
    {
        using var layout = new TemporaryLayout();
        string pathTool = layout.CreateFile(Path.Combine("path", "ffprobe"));
        var context = Context(Path.Combine(layout.Root, "executable"), "linux",
            new Dictionary<string, string>(), Path.GetDirectoryName(pathTool)!);

        Assert.Equal(pathTool, ToolDiscovery.Resolve("ffprobe", context));
    }

    [Fact]
    public void Resolve_WindowsPlatformUsesExecutableExtension()
    {
        using var layout = new TemporaryLayout();
        string pathTool = layout.CreateFile(Path.Combine("path", "ffmpeg.exe"));
        var context = Context(Path.Combine(layout.Root, "executable"), "windows",
            new Dictionary<string, string>(), Path.GetDirectoryName(pathTool)!);

        Assert.Equal(pathTool, ToolDiscovery.Resolve("ffmpeg", context));
    }

    [Theory]
    [InlineData("mkvmerge", "MKVMERGE_BIN")]
    [InlineData("MP4Box", "MP4BOX_BIN")]
    public void Resolve_UsesToolSpecificEnvironmentNames(string toolName, string environmentName)
    {
        using var layout = new TemporaryLayout();
        string environmentTool = layout.CreateFile("tools/custom-tool");
        var context = Context(Path.Combine(layout.Root, "executable"), "linux",
            new Dictionary<string, string> { [environmentName] = environmentTool }, "");

        Assert.Equal(environmentTool, ToolDiscovery.Resolve(toolName, context));
    }

    [Fact]
    public void Resolve_UsesLegacyEnvironmentNameWhenPrimaryOverrideIsAbsent()
    {
        using var layout = new TemporaryLayout();
        string environmentTool = layout.CreateFile("tools/ffprobe");
        var context = Context(Path.Combine(layout.Root, "executable"), "linux",
            new Dictionary<string, string> { ["FFPROBE"] = environmentTool }, "");

        Assert.Equal(environmentTool, ToolDiscovery.Resolve("ffprobe", context));
    }

    [Fact]
    public void Resolve_ReturnsEmptyWhenToolIsUnavailable()
    {
        using var layout = new TemporaryLayout();
        var context = Context(Path.Combine(layout.Root, "executable"), "linux",
            new Dictionary<string, string>(), "");

        Assert.Equal("", ToolDiscovery.Resolve("mkvmerge", context));
    }

    private static ToolDiscoveryContext Context(
        string executableDirectory,
        string platform,
        IReadOnlyDictionary<string, string> environmentVariables,
        string pathValue) =>
        new(executableDirectory, environmentVariables, pathValue, platform, Array.Empty<string>());

    private sealed class TemporaryLayout : IDisposable
    {
        public string Root { get; } = Path.Combine(Path.GetTempPath(), $"ffmpeg-tools-{Guid.NewGuid():N}");

        public string CreateFile(string relativePath)
        {
            string path = Path.Combine(Root, relativePath);
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllText(path, "");
            return path;
        }

        public void Dispose()
        {
            if (Directory.Exists(Root))
                Directory.Delete(Root, recursive: true);
        }
    }
}
