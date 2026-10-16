using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text.Json;
using FfmpegConverter.Cli;
using FfmpegConverter.Core.Engine;
using FfmpegConverter.Core.Models;
using FfmpegConverter.Core.Policy;
using FfmpegConverter.Core.Presets;
using FfmpegConverter.Core.Probing;
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
    public async Task HqAdapter_MissingOrchestratorConfigFailsPromptlyWithoutCreatingTaskFiles()
    {
        using var runtime = new HqRuntimeFixture();
        if (!runtime.Supported)
            return;

        string configPath = Path.Combine(runtime.Root, "modules", "encoder_orchestrator_v2",
            "config", "toolchain_runtime.ini");
        File.Delete(configPath);
        string workDir = Path.Combine(Path.GetTempPath(), $"hq-missing-config-{Guid.NewGuid():N}");
        var adapter = new HqAdapter();
        var timer = Stopwatch.StartNew();

        (int Result, string Error) result = await adapter.RunJobAsync(
            runtime.Root, "input.mkv", workDir, "x264", "FullHD");

        Assert.NotEqual(0, result.Result);
        Assert.Contains("toolchain_runtime.ini", result.Error);
        Assert.True(timer.Elapsed < TimeSpan.FromSeconds(5), $"HQ initialization took {timer.Elapsed}.");
        Assert.False(Directory.Exists(workDir));
    }

    [Fact]
    public async Task Converter_HqMissingConfigCleansTaskDirectoryAndReportsFailure()
    {
        if (!OperatingSystem.IsLinux())
            return;

        using var runtime = new HqRuntimeFixture();
        PresetDb db = PresetDb.Load(hqRuntimeRoot: runtime.Root);
        string root = Path.Combine(Path.GetTempPath(), $"hq-converter-config-{Guid.NewGuid():N}");
        Directory.CreateDirectory(root);
        string input = Path.Combine(root, "input.mkv");
        string outputDir = Path.Combine(root, "output");
        File.WriteAllText(input, "HQ preflight fixture");
        string configPath = Path.Combine(runtime.Root, "modules", "encoder_orchestrator_v2",
            "config", "toolchain_runtime.ini");
        File.Delete(configPath);
        var converter = new Converter(
            new ToolPaths { Ffmpeg = "/bin/true", Mkvmerge = "/bin/true" },
            db,
            new HardwareProbeResult());
        var options = new ConvertOptions
        {
            Codec = "hq_converter",
            SelectionGroup = "hq_converter",
            SelectionEncoder = "x264",
            Preset = "FullHD",
            AudioOutputMode = AudioOutputMode.Pcm,
            OutputDir = outputDir,
            Overwrite = true
        };
        var errors = new System.Collections.Generic.List<string>();
        ConverterError? fileStatus = null;
        converter.ErrorOccurred += (_, error) => errors.Add(error.Text);
        converter.FileEnd += (_, args) => fileStatus = args.Status;

        try
        {
            await converter.ProcessFilesAsync([input], options);

            Assert.Equal(ConverterError.FfmpegFailed, fileStatus);
            Assert.Contains(errors, error => error.Contains("toolchain_runtime.ini", StringComparison.Ordinal));
            Assert.False(Directory.Exists(Path.Combine(outputDir, "task_1")));
            Assert.Empty(Directory.GetFiles(outputDir));
            Assert.False(File.Exists(Path.Combine(outputDir, "input_converted.mkv")));
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
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
            File.WriteAllText(sessionPath, JsonSerializer.Serialize(new
            {
                artifacts = new { outputs = new { video_track = new { path = trackPath } } }
            }));
            Assert.Equal(trackPath, HqAdapter.ReadVideoTrackPath(workDir, "x264", "FullHD"));

            File.WriteAllText(sessionPath, JsonSerializer.Serialize(new
            {
                artifacts = new { outputs = new { video_track = new { path = "relative.h264" } } }
            }));
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
            string orchestratorConfigDir = Path.Combine(Root, "modules", "encoder_orchestrator_v2", "config");
            string toolchainDir = Path.Combine(Root, "modules", "segmentator", "config");
            Directory.CreateDirectory(libraryDir);
            Directory.CreateDirectory(catalogDir);
            Directory.CreateDirectory(orchestratorConfigDir);
            Directory.CreateDirectory(toolchainDir);
            File.WriteAllText(Path.Combine(libraryDir, libraryName), "");
            File.WriteAllText(Path.Combine(orchestratorConfigDir, "toolchain_runtime.ini"), "");
            File.WriteAllText(Path.Combine(toolchainDir, "toolchain_runtime.ini"), "");
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
