using System.IO;
using System.Threading.Tasks;
using FfmpegConverter.Core.Engine;
using FfmpegConverter.Core.Models;
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
}
