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
}
