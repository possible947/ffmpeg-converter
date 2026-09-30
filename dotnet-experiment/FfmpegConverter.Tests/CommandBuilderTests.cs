using System.IO;
using FfmpegConverter.Core.Engine;
using FfmpegConverter.Core.Models;
using FfmpegConverter.Core.Presets;
using FfmpegConverter.Core.Probing;
using Xunit;

namespace FfmpegConverter.Tests;

public class CommandBuilderTests
{
    private readonly PresetDb _presetDb;

    public CommandBuilderTests()
    {
        _presetDb = PresetDb.Load();
    }

    [Theory]
    // HEVC BPP: low=0.030, medium=0.050, high=0.075
    // 1920 * 1080 * 30 * 0.050 / 1000 = 3110 kbps
    [InlineData(1920, 1080, 30.0, "medium", true, 3110)]
    // 1920 * 1080 * 30 * 0.030 / 1000 = 1866 kbps
    [InlineData(1920, 1080, 30.0, "low", true, 1866)]
    // 1920 * 1080 * 30 * 0.075 / 1000 = 4665 kbps
    [InlineData(1920, 1080, 30.0, "high", true, 4665)]
    // H264 BPP: low=0.050, medium=0.080, high=0.120
    // 1920 * 1080 * 30 * 0.080 / 1000 = 4976 kbps
    [InlineData(1920, 1080, 30.0, "medium", false, 4976)]
    public void CalcVideoToolboxBitrateKbps_MatchesFormula(int width, int height, double fps, string preset, bool isHevc, int expectedKbps)
    {
        int actual = CommandBuilder.CalcVideoToolboxBitrateKbps(width, height, fps, preset, isHevc);
        Assert.Equal(expectedKbps, actual);
    }

    [Fact]
    public void BuildAudioFilter_IncludesSoxrResampler()
    {
        var opts = new ConvertOptions { AudioNorm = "none" };
        string filter = CommandBuilder.BuildAudioFilter(opts);
        Assert.Equal("aresample=resampler=soxr:precision=28:cheby=1", filter);
    }

    [Fact]
    public void BuildAudioFilter_PeakNorm_AddsMinus3dB()
    {
        var opts = new ConvertOptions { AudioNorm = "peak_norm" };
        string filter = CommandBuilder.BuildAudioFilter(opts);
        Assert.Equal("aresample=resampler=soxr:precision=28:cheby=1,volume=-3dB", filter);
    }

    [Fact]
    public void BuildAudioFilter_PeakNorm2Pass_AddsCalculatedGain()
    {
        var opts = new ConvertOptions { AudioNorm = "peak_norm_2pass", Gain = 4.25 };
        string filter = CommandBuilder.BuildAudioFilter(opts);
        Assert.Equal("aresample=resampler=soxr:precision=28:cheby=1,volume=4.25dB", filter);
    }

    [Fact]
    public void BuildAudioFilter_LoudnessNorm2Pass_IncludesAllMeasuredTargets()
    {
        var opts = new ConvertOptions
        {
            AudioNorm = "loudness_norm_2pass",
            ITarget = -11.0,
            TpTarget = -1.5,
            LraTarget = 6.0,
            MeasuredI = -15.2,
            MeasuredTp = -2.1,
            MeasuredLra = 5.4,
            MeasuredThresh = -25.8,
            MeasuredOffset = 0.3
        };

        string filter = CommandBuilder.BuildAudioFilter(opts);
        Assert.Contains("aresample=resampler=soxr:precision=28:cheby=1,loudnorm=", filter);
        Assert.Contains("I=-11.0:TP=-1.5:LRA=6.0", filter);
        Assert.Contains("measured_I=-15.20:measured_TP=-2.10:measured_LRA=5.40:measured_thresh=-25.80:offset=0.30:linear=true", filter);
    }

    [Fact]
    public void BuildOutputFilePath_ProResUsesMovExtension()
    {
        var opts = new ConvertOptions { Codec = "prores_ks", Preset = "standard", OutputDir = "/tmp/out" };
        string path = CommandBuilder.BuildOutputFilePath("/path/to/movie.mkv", opts, "linux", _presetDb);
        Assert.Equal("/tmp/out/movie_converted.mov", path);
    }

    [Fact]
    public void BuildOutputFilePath_VideoToolboxUsesMp4Extension()
    {
        var opts = new ConvertOptions { Codec = "hevc_videotoolbox", Preset = "default", OutputDir = "/tmp/out" };
        string path = CommandBuilder.BuildOutputFilePath("/path/to/movie.mkv", opts, "macos", _presetDb);
        Assert.Equal("/tmp/out/movie_converted.mp4", path);
    }

    [Fact]
    public void BuildFfmpegArgs_DualAudio_UsesComplexFilterWithAsplit()
    {
        var opts = new ConvertOptions
        {
            Codec = "prores_ks",
            Preset = "standard",
            AudioNorm = "peak_norm",
            AudioOutputMode = "fdk_aac_320_ac3_640"
        };

        var probe = new HardwareProbeResult();
        probe.SupportedEncoders.Add("libfdk_aac");

        string args = CommandBuilder.BuildFfmpegArgs("input.mov", "output.mov", opts, "linux", _presetDb, null, probe);

        Assert.Contains("-filter_complex \"[0:a:0]aresample=resampler=soxr:precision=28:cheby=1,volume=-3dB,asplit=2[aout0][aout1]\"", args);
        Assert.Contains("-map [aout0] -map [aout1]", args);
        Assert.Contains("-c:a:0 libfdk_aac -b:a:0 320k -ar:a:0 48000", args);
        Assert.Contains("-c:a:1 ac3 -b:a:1 640k -ar:a:1 48000", args);
    }

    [Fact]
    public void BuildFfmpegArgs_ColorMetadata_AppendedWhenAvailable()
    {
        var opts = new ConvertOptions { Codec = "prores_ks", Preset = "standard" };
        var vInfo = new InputVideoInfo
        {
            ColorRange = "tv",
            ColorPrimaries = "bt709",
            ColorTransfer = "bt709",
            ColorSpace = "bt709"
        };

        string args = CommandBuilder.BuildFfmpegArgs("input.mov", "output.mov", opts, "linux", _presetDb, vInfo, null);

        Assert.Contains("-color_range tv", args);
        Assert.Contains("-color_primaries bt709", args);
        Assert.Contains("-color_trc bt709", args);
        Assert.Contains("-colorspace bt709", args);
    }

    [Fact]
    public void BuildFfmpegArgs_MacOsVideoToolbox_BitrateCalculationAndTags()
    {
        var opts = new ConvertOptions
        {
            Codec = "hevc_videotoolbox",
            Preset = "medium",
            AudioNorm = "none",
            AudioOutputMode = "pcm"
        };

        var vInfo = new InputVideoInfo { Width = 1920, Height = 1080, Fps = 30.0 };

        string args = CommandBuilder.BuildFfmpegArgs("input.mov", "output.mp4", opts, "macos", _presetDb, vInfo, null);

        Assert.Contains("-c:v hevc_videotoolbox -b:v 3110k -tag:v hvc1 -spatial_aq 1", args);
    }

    [Fact]
    public void BuildFfmpegArgs_Av1Input_UsesLibdav1dDecoderWhenAvailable()
    {
        var opts = new ConvertOptions { Codec = "prores_ks", Preset = "standard" };
        var vInfo = new InputVideoInfo { CodecName = "av1" };
        var probe = new HardwareProbeResult();
        probe.SupportedDecoders.Add("libdav1d");

        string args = CommandBuilder.BuildFfmpegArgs("input.mkv", "output.mov", opts, "linux", _presetDb, vInfo, probe);

        Assert.Contains("-hwaccel none -c:v libdav1d -i \"input.mkv\"", args);
    }
}
