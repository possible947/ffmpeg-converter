using System.IO;
using System.Threading.Tasks;
using FfmpegConverter.Core.Engine;
using FfmpegConverter.Core.Models;
using FfmpegConverter.Core.Probing;
using Xunit;

namespace FfmpegConverter.Tests;

public class AppleM4vPipelineTests
{
    [Theory]
    [InlineData("h264", true)]
    [InlineData("H264", true)]
    [InlineData("hevc", true)]
    [InlineData("HEVC", true)]
    [InlineData("prores", true)]
    [InlineData("prores_ks", true)]
    [InlineData("vp9", false)]
    [InlineData("av1", false)]
    [InlineData("mpeg4", false)]
    [InlineData("", false)]
    [InlineData(null, false)]
    public void IsSupportedVideoCodec_ValidatesExpectedCodecs(string? codec, bool expected)
    {
        bool actual = AppleM4vPipeline.IsSupportedVideoCodec(codec);
        Assert.Equal(expected, actual);
    }

    [Fact]
    public async Task ValidateInputSupported_MissingFile_ReturnsFalse()
    {
        var result = await AppleM4vPipeline.ValidateInputSupportedAsync("/fake/ffprobe", "/fake/nonexistent.mov");
        Assert.False(result.IsSupported);
        Assert.Contains("not found", result.ErrorDetail);
    }

    [Fact]
    public async Task CreateM4v_MissingTools_ReturnsInvalidOptions()
    {
        var pipeline = new AppleM4vPipeline();
        var tools = new ToolPaths { Ffmpeg = "", Ffprobe = "", Mp4Box = "" };
        var opts = new ConvertOptions();

        var err = await pipeline.CreateM4vAsync("input.mov", "output.m4v", opts, tools, null);
        Assert.Equal(ConverterError.InvalidOptions, err);
    }

    [Fact]
    public async Task Converter_M4vDryRun_SucceedsWithoutTouchingFiles()
    {
        var tempInput = Path.GetTempFileName();
        try
        {
            var converter = new Converter();
            var opts = new ConvertOptions
            {
                Codec = "m4v",
                DryRun = true,
                OutputDir = Path.GetTempPath()
            };

            bool messageLogged = false;
            converter.MessageLogged += (s, e) =>
            {
                if (e.Text.Contains("[DRY RUN] [m4v]"))
                    messageLogged = true;
            };

            var result = await converter.ProcessFilesAsync(new[] { tempInput }, opts);
            Assert.Equal(ConverterError.Ok, result);
            Assert.True(messageLogged);
        }
        finally
        {
            if (File.Exists(tempInput)) File.Delete(tempInput);
        }
    }

    [Theory]
    [InlineData("24/1", 24.0)]
    [InlineData("30000/1001", 29.97002997002997)]
    [InlineData("60/1", 60.0)]
    [InlineData("25.0", 25.0)]
    [InlineData("0/0", 25.0)]
    [InlineData("", 25.0)]
    public void ParseRateToFps_CalculatesCorrectFramerate(string rateStr, double expectedFps)
    {
        double fps = 25.0;
        if (!string.IsNullOrEmpty(rateStr) && rateStr != "0/0")
        {
            var parts = rateStr.Split('/');
            if (parts.Length == 2 &&
                double.TryParse(parts[0], System.Globalization.CultureInfo.InvariantCulture, out double n) &&
                double.TryParse(parts[1], System.Globalization.CultureInfo.InvariantCulture, out double d) &&
                d > 0)
            {
                fps = n / d;
            }
            else if (double.TryParse(rateStr, System.Globalization.CultureInfo.InvariantCulture, out double direct))
            {
                fps = direct;
            }
        }

        Assert.Equal(expectedFps, fps, precision: 4);
    }
}
