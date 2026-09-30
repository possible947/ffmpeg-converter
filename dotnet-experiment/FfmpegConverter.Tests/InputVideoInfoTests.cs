using FfmpegConverter.Core.Engine;
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
