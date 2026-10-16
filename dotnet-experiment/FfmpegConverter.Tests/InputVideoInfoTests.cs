using FfmpegConverter.Core.Engine;
using FfmpegConverter.Core.Policy;
using Xunit;

namespace FfmpegConverter.Tests;

public class InputVideoInfoTests
{
    [Fact]
    public void ParseText_ParsesStandardProbedStream()
    {
        string ffprobeOutput = @"codec_name=hevc
pix_fmt=yuv420p10le
bits_per_raw_sample=10
profile=Main 10
width=3840
height=2160
avg_frame_rate=60/1
color_range=tv
color_primaries=bt2020
color_transfer=smpte2084
color_space=bt2020nc";

        var info = InputVideoInfo.ParseText(ffprobeOutput);

        Assert.Equal("hevc", info.CodecName);
        Assert.Equal("yuv420p10le", info.PixelFormat);
        Assert.Equal(10, info.BitDepth);
        Assert.Equal("Main 10", info.Profile);
        Assert.Equal(3840, info.Width);
        Assert.Equal(2160, info.Height);
        Assert.Equal(60.0, info.Fps);
        Assert.Equal("tv", info.ColorRange);
        Assert.Equal("bt2020", info.ColorPrimaries);
        Assert.Equal("smpte2084", info.ColorTransfer);
        Assert.Equal("bt2020nc", info.ColorSpace);
    }

    [Theory]
    [InlineData("h264", MuxVideoCodec.H264)]
    [InlineData("hevc", MuxVideoCodec.Hevc)]
    [InlineData("prores_ks", MuxVideoCodec.ProRes)]
    [InlineData("av1", MuxVideoCodec.Av1)]
    [InlineData("mpeg2video", MuxVideoCodec.Unsupported)]
    public void ProbeResult_MapsCodecNameToTypedCodec(string codecName, MuxVideoCodec expectedCodec)
    {
        InputVideoProbeResult result = InputVideoProbeResult.FromProcessResult(
            0, $"codec_name={codecName}\nwidth=1920\nheight=1080\n", "");

        Assert.True(result.Success);
        Assert.Equal(expectedCodec, result.Codec);
        Assert.Equal(codecName, result.VideoInfo!.CodecName);
    }

    [Fact]
    public void ProbeResult_ClassifiesCorruptAndNoVideoInputs()
    {
        InputVideoProbeResult corrupt = InputVideoProbeResult.FromProcessResult(1, "", "Invalid data found");
        InputVideoProbeResult noVideo = InputVideoProbeResult.FromProcessResult(0, "", "");

        Assert.Equal(InputVideoProbeFailure.ProbeFailed, corrupt.Failure);
        Assert.Equal("Invalid data found", corrupt.Detail);
        Assert.Equal(InputVideoProbeFailure.NoVideoStream, noVideo.Failure);
    }

    [Fact]
    public void ProbeResult_RejectsOutputWithoutCodecName()
    {
        InputVideoProbeResult result = InputVideoProbeResult.FromProcessResult(0, "width=1920\nheight=1080\n", "");

        Assert.Equal(InputVideoProbeFailure.MissingCodec, result.Failure);
        Assert.False(result.Success);
    }

    [Fact]
    public async Task ProbeDetailedAsync_ClassifiesMissingAndNonFileInputs()
    {
        string root = Path.Combine(Path.GetTempPath(), $"ffprobe-input-{Guid.NewGuid():N}");
        Directory.CreateDirectory(root);
        string toolPath = Path.Combine(root, "ffprobe");
        string missingPath = Path.Combine(root, "missing.mkv");
        File.WriteAllText(toolPath, "");
        string directoryInput = Path.Combine(root, "media-directory");
        Directory.CreateDirectory(directoryInput);
        try
        {
            InputVideoProbeResult missing = await InputVideoInfo.ProbeDetailedAsync(toolPath, missingPath);
            InputVideoProbeResult directory = await InputVideoInfo.ProbeDetailedAsync(toolPath, directoryInput);

            Assert.Equal(InputVideoProbeFailure.InputNotFound, missing.Failure);
            Assert.Equal(InputVideoProbeFailure.InputNotReadable, directory.Failure);
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }

    [Fact]
    public async Task ProbeDetailedAsync_ClassifiesMissingToolAndFailedProcess()
    {
        string root = Path.Combine(Path.GetTempPath(), $"ffprobe-failure-{Guid.NewGuid():N}");
        Directory.CreateDirectory(root);
        string input = Path.Combine(root, "input.mkv");
        string invalidTool = Path.Combine(root, "invalid-ffprobe");
        File.WriteAllText(input, "");
        File.WriteAllText(invalidTool, "");
        try
        {
            InputVideoProbeResult missingTool = await InputVideoInfo.ProbeDetailedAsync(
                Path.Combine(root, "missing-ffprobe"), input);
            InputVideoProbeResult failedProcess = await InputVideoInfo.ProbeDetailedAsync(invalidTool, input);

            Assert.Equal(InputVideoProbeFailure.ProbeToolMissing, missingTool.Failure);
            Assert.Equal(InputVideoProbeFailure.ProcessStartFailed, failedProcess.Failure);
            Assert.False(failedProcess.Success);
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }

    [Theory]
    [InlineData("yuv420p", 8)]
    [InlineData("yuv420p10le", 10)]
    [InlineData("yuv422p10be", 10)]
    [InlineData("yuv444p12le", 12)]
    [InlineData("yuv420p16le", 16)]
    [InlineData("p010le", 10)]
    [InlineData("nv12", 8)]
    [InlineData("N/A", 8)]
    public void BitDepthFromPixelFormat_ResolvesCorrectly(string pixFmt, int expectedDepth)
    {
        int depth = InputVideoInfo.BitDepthFromPixelFormat(pixFmt);
        Assert.Equal(expectedDepth, depth);
    }
}
