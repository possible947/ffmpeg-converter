using System;
using System.Collections.Generic;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using FfmpegConverter.Core.Engine;
using FfmpegConverter.Core.Models;
using FfmpegConverter.Core.Policy;
using FfmpegConverter.Core.Presets;
using FfmpegConverter.Core.Probing;
using Xunit;

namespace FfmpegConverter.Tests;

public class MuxPipelineTests
{
    [Theory]
    [InlineData("video.hevc", true)]
    [InlineData("video.HEVC", true)]
    [InlineData("video.h265", true)]
    [InlineData("video.H265", true)]
    [InlineData("video.264", true)]
    [InlineData("video.h264", true)]
    [InlineData("video.H264", true)]
    [InlineData("video.mkv", false)]
    [InlineData("video.mov", false)]
    [InlineData("video.mp4", false)]
    [InlineData("video.avi", false)]
    [InlineData("", false)]
    [InlineData(null, false)]
    public void VideoTrackNeedsForcedFps_IdentifiesRawElementaryStreams(string? path, bool expected)
    {
        bool actual = MuxPipeline.VideoTrackNeedsForcedFps(path!);
        Assert.Equal(expected, actual);
    }

    [Fact]
    public async Task RunMuxPostprocess_MissingIntermediateFile_ReturnsInputNotFound()
    {
        var pipeline = new MuxPipeline();
        var tools = new ToolPaths { Mkvmerge = "/fake/mkvmerge" };
        var opts = new ConvertOptions();

        var err = await pipeline.RunMuxPostprocessAsync("/nonexistent/intermediate.mkv", "/some/video.hevc", "output.mkv", opts, tools, null);
        Assert.Equal(ConverterError.InputNotFound, err);
    }

    [Fact]
    public async Task RunMuxPostprocess_MissingVideoTrackFile_ReturnsInputNotFound()
    {
        var tempIntermediate = Path.GetTempFileName();
        try
        {
            var pipeline = new MuxPipeline();
            var tools = new ToolPaths { Mkvmerge = "/fake/mkvmerge" };
            var opts = new ConvertOptions();

            var err = await pipeline.RunMuxPostprocessAsync(tempIntermediate, "/nonexistent/video.hevc", "output.mkv", opts, tools, null);
            Assert.Equal(ConverterError.InputNotFound, err);
        }
        finally
        {
            if (File.Exists(tempIntermediate)) File.Delete(tempIntermediate);
        }
    }

    [Fact]
    public async Task RunMuxPostprocess_MissingMkvmergeTool_ReturnsInvalidOptions()
    {
        var tempIntermediate = Path.GetTempFileName();
        var tempTrack = Path.GetTempFileName();
        try
        {
            var pipeline = new MuxPipeline();
            var tools = new ToolPaths { Mkvmerge = "/fake/nonexistent/mkvmerge" };
            var opts = new ConvertOptions();

            var err = await pipeline.RunMuxPostprocessAsync(tempIntermediate, tempTrack, "output.mkv", opts, tools, null);
            Assert.Equal(ConverterError.InvalidOptions, err);
        }
        finally
        {
            if (File.Exists(tempIntermediate)) File.Delete(tempIntermediate);
            if (File.Exists(tempTrack)) File.Delete(tempTrack);
        }
    }

    [Fact]
    public async Task RunMuxPostprocess_InvalidM4vAudioPolicyIsRejectedBeforeToolsOrInputs()
    {
        PolicyResult validPolicy = MuxPolicyResolver.Resolve(
            new MuxBuildProfile(MuxPlatform.Linux, MuxFfmpegVersion.V8_1),
            MuxOperationalMode.M4v,
            AudioOutputMode.Aac,
            MuxVideoCodec.Unknown,
            MuxVideoCodec.H264);
        Assert.True(validPolicy.Success);
        MuxPolicy invalidPolicy = validPolicy.Policy! with
        {
            AudioMode = AudioOutputMode.Copy
        };
        var pipeline = new MuxPipeline();
        var options = new ConvertOptions
        {
            Preset = "m4v",
            AudioOutputMode = AudioOutputMode.Copy
        };
        string? error = null;
        pipeline.ErrorOccurred += (_, args) => error = args.Text;

        ConverterError result = await pipeline.RunMuxPostprocessAsync(
            "/missing/intermediate.mkv",
            "/missing/video.mkv",
            "/missing/output.m4v",
            options,
            new ToolPaths(),
            null,
            policy: invalidPolicy);

        Assert.Equal(ConverterError.InvalidOptions, result);
        Assert.Contains("M4V stream policy is unavailable", error);
    }

    [Fact]
    public async Task ValidatePreparedSource_MissingProbeToolRejectsIntermediate()
    {
        string intermediate = Path.GetTempFileName();
        try
        {
            bool usable = await MuxPipeline.ValidatePreparedSourceAsync(
                "/missing/ffprobe", intermediate);

            Assert.False(usable);
        }
        finally
        {
            File.Delete(intermediate);
        }
    }

    [Fact]
    public async Task Converter_MuxDryRun_SucceedsWithoutTouchingFiles()
    {
        var tempInput = Path.GetTempFileName();
        var tempTrack = Path.GetTempFileName();
        try
        {
            var converter = new Converter();
            var opts = new ConvertOptions
            {
                Codec = "mux",
                VideoTrackPath = tempTrack,
                DryRun = true,
                OutputDir = Path.GetTempPath()
            };

            bool messageLogged = false;
            converter.MessageLogged += (s, e) =>
            {
                if (e.Text.Contains("[DRY RUN] [mux]"))
                    messageLogged = true;
            };

            var result = await converter.ProcessFilesAsync(new[] { tempInput }, opts);
            Assert.Equal(ConverterError.Ok, result);
            Assert.True(messageLogged);
        }
        finally
        {
            if (File.Exists(tempInput)) File.Delete(tempInput);
            if (File.Exists(tempTrack)) File.Delete(tempTrack);
        }
    }

    [Fact]
    public async Task Converter_SingleCorruptMuxSource_ReturnsFailureWithoutStartingTools()
    {
        if (!OperatingSystem.IsLinux())
            return;

        string root = Path.Combine(Path.GetTempPath(), $"mux-preflight-{Guid.NewGuid():N}");
        Directory.CreateDirectory(root);
        string input = Path.Combine(root, "corrupt.mkv");
        string replacement = Path.Combine(root, "replacement.mkv");
        string outputDir = Path.Combine(root, "output");
        string ffprobe = CreateExecutable(root, "ffprobe", "#!/bin/sh\nexit 1\n");
        string ffmpegMarker = Path.Combine(root, "ffmpeg-started");
        string mkvmergeMarker = Path.Combine(root, "mkvmerge-started");
        string ffmpeg = CreateExecutable(root, "ffmpeg",
            $"#!/bin/sh\nprintf started > '{ffmpegMarker}'\n");
        string mkvmerge = CreateExecutable(root, "mkvmerge",
            $"#!/bin/sh\nprintf started > '{mkvmergeMarker}'\n");
        File.WriteAllText(input, "not a media file");
        File.WriteAllText(replacement, "replacement video fixture");
        var replacementProbe = new StubReplacementVideoProbe(
            InputVideoProbeResult.FromProcessResult(0, "codec_name=h264\n", ""));
        var converter = new Converter(
            new ToolPaths { Ffmpeg = ffmpeg, Ffprobe = ffprobe, Mkvmerge = mkvmerge },
            PresetDb.Load(),
            new HardwareProbeResult(),
            replacementProbe);
        var options = new ConvertOptions
        {
            Codec = "mux",
            SelectionGroup = "mux",
            SelectionEncoder = "mkv",
            Preset = "mkv",
            AudioOutputMode = AudioOutputMode.Copy,
            VideoTrackPath = replacement,
            OutputDir = outputDir,
            Overwrite = true
        };
        ConverterError? fileStatus = null;
        converter.FileEnd += (_, args) => fileStatus = args.Status;

        try
        {
            ConverterError result = await converter.ProcessFilesAsync([input], options);

            Assert.Equal(ConverterError.FfprobeFailed, fileStatus);
            Assert.NotEqual(ConverterError.Ok, result);
            Assert.False(File.Exists(ffmpegMarker));
            Assert.False(File.Exists(mkvmergeMarker));
            Assert.Empty(Directory.GetFiles(outputDir));
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }

    [Fact]
    public async Task Converter_SingleMissingMuxSource_ReturnsFailure()
    {
        string root = Path.Combine(Path.GetTempPath(), $"mux-missing-{Guid.NewGuid():N}");
        Directory.CreateDirectory(root);
        try
        {
            var converter = new Converter(
                new ToolPaths { Ffmpeg = "/bin/true" },
                PresetDb.Load(),
                new HardwareProbeResult());
            var options = new ConvertOptions
            {
                Codec = "mux",
                Preset = "mkv",
                AudioOutputMode = AudioOutputMode.Copy,
                OutputDir = Path.Combine(root, "output")
            };

            ConverterError result = await converter.ProcessFilesAsync(
                [Path.Combine(root, "missing.mkv")], options);

            Assert.Equal(ConverterError.InputNotFound, result);
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public async Task Converter_OnePassQueueContinuesAfterFfmpegFailure(bool corruptFirst)
    {
        if (!OperatingSystem.IsLinux())
            return;

        string root = Path.Combine(Path.GetTempPath(), $"conversion-queue-{Guid.NewGuid():N}");
        Directory.CreateDirectory(root);
        string valid = Path.Combine(root, "valid.mkv");
        string corrupt = Path.Combine(root, "corrupt.mkv");
        string outputDir = Path.Combine(root, "output");
        string ffprobe = CreateExecutable(root, "ffprobe",
            "#!/bin/sh\ncase \"$*\" in *format=duration*) echo 1;; *) printf 'codec_name=h264\\npix_fmt=yuv420p\\nwidth=16\\nheight=16\\navg_frame_rate=25/1\\n';; esac\n");
        string ffmpeg = CreateExecutable(root, "ffmpeg",
            "#!/bin/sh\ncase \"$*\" in *corrupt.mkv*) exit 1;; esac\nfor output do :; done\n: > \"$output\"\nexit 0\n");
        File.WriteAllText(valid, "valid source fixture");
        File.WriteAllText(corrupt, "corrupt source fixture");
        var converter = new Converter(
            new ToolPaths { Ffmpeg = ffmpeg, Ffprobe = ffprobe },
            PresetDb.Load(),
            new HardwareProbeResult());
        var options = new ConvertOptions
        {
            Codec = "prores_ks",
            SelectionGroup = "software",
            SelectionEncoder = "prores_ks",
            Preset = "standard",
            AudioOutputMode = AudioOutputMode.Pcm,
            OutputDir = outputDir,
            Overwrite = true
        };
        var statuses = new List<ConverterError>();
        converter.FileEnd += (_, args) => statuses.Add(args.Status);
        string[] inputs = corruptFirst ? [corrupt, valid] : [valid, corrupt];
        string successfulOutput = CommandBuilder.BuildOutputFilePath(
            valid, options, "linux", PresetDb.Load());

        try
        {
            ConverterError result = await converter.ProcessFilesAsync(inputs, options);

            Assert.Equal(ConverterError.Ok, result);
            Assert.Equal(corruptFirst
                ? new[] { ConverterError.FfmpegFailed, ConverterError.Ok }
                : new[] { ConverterError.Ok, ConverterError.FfmpegFailed }, statuses);
            Assert.True(File.Exists(successfulOutput));
            string failedOutput = CommandBuilder.BuildOutputFilePath(
                corrupt, options, "linux", PresetDb.Load());
            Assert.False(File.Exists(failedOutput));
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }

    [Fact]
    public async Task Converter_MuxM4vDryRun_UsesReplacementTrackWorkflow()
    {
        string tempInput = Path.GetTempFileName();
        string tempTrack = Path.GetTempFileName();
        try
        {
            var converter = new Converter();
            var opts = new ConvertOptions
            {
                SelectionGroup = "mux",
                SelectionEncoder = "m4v",
                Codec = "m4v",
                Preset = "m4v",
                AudioOutputMode = AudioOutputMode.AacAc3,
                VideoTrackPath = tempTrack,
                DryRun = true,
                OutputDir = Path.GetTempPath()
            };

            string? dryRunMessage = null;
            converter.MessageLogged += (s, e) =>
            {
                if (e.Text.Contains("[DRY RUN]"))
                    dryRunMessage = e.Text;
            };

            var result = await converter.ProcessFilesAsync(new[] { tempInput }, opts);

            Assert.Equal(ConverterError.Ok, result);
            Assert.Contains("[DRY RUN] [mux]", dryRunMessage);
            Assert.Contains(tempTrack, dryRunMessage);
        }
        finally
        {
            File.Delete(tempInput);
            File.Delete(tempTrack);
        }
    }

    [Fact]
    public async Task Converter_RejectsUnsupportedReplacementBeforePreparationAfterOneProbe()
    {
        string root = Path.Combine(Path.GetTempPath(), $"mux-replacement-{Guid.NewGuid():N}");
        Directory.CreateDirectory(root);
        string source = Path.Combine(root, "source.mkv");
        string replacement = Path.Combine(root, "replacement.mkv");
        string ffmpeg = Path.Combine(root, "ffmpeg");
        string outputDir = Path.Combine(root, "output");
        File.WriteAllText(source, "");
        File.WriteAllText(replacement, "");
        File.WriteAllText(ffmpeg, "");
        var probe = new StubReplacementVideoProbe(
            InputVideoProbeResult.FromProcessResult(0, "codec_name=mpeg2video\n", ""));
        var converter = new Converter(
            new ToolPaths { Ffmpeg = ffmpeg, Ffprobe = "ffprobe", Mkvmerge = "mkvmerge" },
            PresetDb.Load(),
            new HardwareProbeResult(),
            probe);
        ConverterError? fileStatus = null;
        string? errorMessage = null;
        var stages = new List<string>();
        converter.FileEnd += (_, args) => fileStatus = args.Status;
        converter.ErrorOccurred += (_, args) => errorMessage = args.Text;
        converter.StageChanged += (_, stage) => stages.Add(stage);
        var options = new ConvertOptions
        {
            Codec = "mux",
            SelectionGroup = "mux",
            SelectionEncoder = "mkv",
            Preset = "mkv",
            AudioOutputMode = AudioOutputMode.Pcm,
            VideoTrackPath = replacement,
            OutputDir = outputDir,
            Overwrite = true
        };

        try
        {
            ConverterError result = await converter.ProcessFilesAsync(new[] { source }, options);

            Assert.Equal(ConverterError.Ok, result);
            Assert.Equal(ConverterError.InvalidOptions, fileStatus);
            Assert.Contains("Unsupported video is not supported in Mkv output.", errorMessage);
            Assert.Equal(1, probe.CallCount);
            Assert.DoesNotContain("encoding intermediate", stages);
            Assert.Empty(Directory.GetFiles(outputDir));
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }

    [Theory]
    [InlineData("mkv", "_converted.mkv")]
    [InlineData("mov", "_converted.mov")]
    [InlineData("m4v", "_converted.m4v")]
    [InlineData("default", "_converted.mkv")]
    public void BuildOutputFilePath_MuxPresets_SelectsCorrectExtension(string preset, string expectedEnd)
    {
        var presetDb = PresetDb.Load();
        var opts = new ConvertOptions
        {
            Codec = "mux",
            Preset = preset,
            OutputDir = "/tmp"
        };

        string outPath = CommandBuilder.BuildOutputFilePath("sample.mov", opts, "linux", presetDb);
        Assert.EndsWith(expectedEnd, outPath);
    }

    private sealed class StubReplacementVideoProbe(InputVideoProbeResult result) : IReplacementVideoProbe
    {
        public int CallCount { get; private set; }

        public Task<InputVideoProbeResult> ProbeAsync(
            string ffprobePath,
            string replacementPath,
            CancellationToken cancellationToken)
        {
            CallCount++;
            return Task.FromResult(result);
        }
    }

    private static string CreateExecutable(string directory, string name, string contents)
    {
        if (!OperatingSystem.IsLinux())
            throw new PlatformNotSupportedException("Executable fixtures require Linux.");

        string path = Path.Combine(directory, name);
        File.WriteAllText(path, contents);
        File.SetUnixFileMode(path,
            UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.UserExecute);
        return path;
    }
}
