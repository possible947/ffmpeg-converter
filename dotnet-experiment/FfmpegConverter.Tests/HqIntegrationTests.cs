using System;
using System.IO;
using System.Runtime.InteropServices;
using FfmpegConverter.Cli;
using FfmpegConverter.Core.Engine;
using FfmpegConverter.Core.Presets;
using Xunit;

namespace FfmpegConverter.Tests;

public class HqIntegrationTests
{
    [Fact]
    public void PresetDb_LoadsExactRuntimeCatalogAndEnablesHqSelection()
    {
        using var runtime = new HqRuntimeFixture();
        if (!runtime.Supported)
            return;

        PresetDb db = PresetDb.Load(hqRuntimeRoot: runtime.Root);

        Assert.True(db.Features["hq_converter"]);
        Assert.True(db.ResolveSelection("linux", "hq_converter", "x264", out string finalCodec));
        Assert.Equal("hq_converter", finalCodec);
        Assert.Equal("FullHD", db.HqCatalog!.GetDefaultPreset("x264"));
        Assert.Equal(new[] { "FullHD", "UltraHD" }, db.GetSelectionPresets("linux", "hq_converter", "x264"));
    }

    [Fact]
    public void CommandLineParser_UsesAndValidatesHqCatalogIdentifiers()
    {
        using var runtime = new HqRuntimeFixture();
        if (!runtime.Supported)
            return;
        PresetDb db = PresetDb.Load(hqRuntimeRoot: runtime.Root);

        var defaults = CommandLineParser.Parse(
            new[] { "-c", "hq_converter", "--encoder", "x264", "input.mkv" }, db, "linux");
        Assert.True(defaults.Success, defaults.ErrorMessage);
        Assert.Equal("FullHD", defaults.Options.Preset);

        var invalid = CommandLineParser.Parse(
            new[] { "-c", "hq_converter", "--encoder", "x264", "--preset", "fullhd", "input.mkv" }, db, "linux");
        Assert.False(invalid.Success);

        var proresAudio = CommandLineParser.Parse(
            new[] { "-c", "hq_converter", "--encoder", "ffmpeg-prores", "--audio-output", "fdk_aac_320", "input.mov" }, db, "linux");
        Assert.False(proresAudio.Success);
        Assert.Contains("require PCM", proresAudio.ErrorMessage);
    }

    [Fact]
    public void HqProResOutputUsesMovAndOtherHqOutputUsesMkv()
    {
        using var runtime = new HqRuntimeFixture();
        if (!runtime.Supported)
            return;
        PresetDb db = PresetDb.Load(hqRuntimeRoot: runtime.Root);

        var prores = new FfmpegConverter.Core.Models.ConvertOptions
        {
            Codec = "hq_converter",
            SelectionEncoder = "ffmpeg-prores"
        };
        var h264 = new FfmpegConverter.Core.Models.ConvertOptions
        {
            Codec = "hq_converter",
            SelectionEncoder = "x264"
        };

        Assert.EndsWith(".mov", CommandBuilder.BuildOutputFilePath("source.mkv", prores, "linux", db));
        Assert.EndsWith(".mkv", CommandBuilder.BuildOutputFilePath("source.mkv", h264, "linux", db));
    }

    [Fact]
    public void HqSessionReaderAcceptsOnlyAbsoluteVideoTrackPaths()
    {
        string workDir = Path.Combine(Path.GetTempPath(), $"hq-session-{Guid.NewGuid():N}");
        Directory.CreateDirectory(workDir);
        string trackPath = Path.Combine(workDir, "video.h264");
        File.WriteAllText(trackPath, "");
        string sessionPath = Path.Combine(workDir, "job.x264.FullHD.eov2.session.json");
        try
        {
            File.WriteAllText(sessionPath,
                $$"""{"artifacts":{"outputs":{"video_track":{"path":"{{trackPath.Replace("\\", "\\\\")}}"}}}}""");
            Assert.Equal(trackPath, HqAdapter.ReadVideoTrackPath(workDir, "x264", "FullHD"));

            File.WriteAllText(sessionPath,
                """{"artifacts":{"outputs":{"video_track":{"path":"relative.h264"}}}}""");
            Assert.Null(HqAdapter.ReadVideoTrackPath(workDir, "x264", "FullHD"));
        }
        finally
        {
            Directory.Delete(workDir, recursive: true);
        }
    }

    private sealed class HqRuntimeFixture : IDisposable
    {
        public HqRuntimeFixture()
        {
            Supported = RuntimeInformation.IsOSPlatform(OSPlatform.Linux) ||
                        RuntimeInformation.IsOSPlatform(OSPlatform.OSX);
            if (!Supported)
                return;

            Root = Path.Combine(Path.GetTempPath(), $"hq-runtime-{Guid.NewGuid():N}");
            string libraryName = RuntimeInformation.IsOSPlatform(OSPlatform.OSX)
                ? "libhq_converter.dylib"
                : "libhq_converter.so";
            string libraryDir = Path.Combine(Root, "modules", "library", "bin");
            string catalogDir = Path.Combine(Root, "modules", "encoder_orchestrator_v2", "config", "presets");
            Directory.CreateDirectory(libraryDir);
            Directory.CreateDirectory(catalogDir);
            File.WriteAllText(Path.Combine(libraryDir, libraryName), "");
            File.WriteAllText(Path.Combine(catalogDir, "encoder_presets_v2.json"),
                """
                {
                  "encoders": {
                    "x264": {
                      "default_preset": "FullHD",
                      "presets": { "FullHD": {}, "UltraHD": {} }
                    },
                    "ffmpeg-prores": {
                      "default_preset": "ProResHQ",
                      "presets": { "ProResHQ": {} }
                    }
                  }
                }
                """);
        }

        public bool Supported { get; }
        public string Root { get; } = "";

        public void Dispose()
        {
            if (Supported && Directory.Exists(Root))
                Directory.Delete(Root, recursive: true);
        }
    }
}
