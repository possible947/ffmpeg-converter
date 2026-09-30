using FfmpegConverter.Core.Models;
using Xunit;

namespace FfmpegConverter.Tests;

public class ConvertOptionsTests
{
    [Fact]
    public void DefaultValues_MatchCImplementation()
    {
        var opts = new ConvertOptions();

        Assert.Equal("software", opts.SelectionGroup);
        Assert.Equal("prores_ks", opts.SelectionEncoder);
        Assert.Equal("prores_ks", opts.Codec);
        Assert.Equal("standard", opts.Preset);
        Assert.Equal(1, opts.Deblock);
        Assert.Equal("none", opts.AudioNorm);
        Assert.Equal("pcm", opts.AudioOutputMode);
        Assert.Equal(1, opts.Genre);
        Assert.False(opts.UseAacForH265);
        Assert.Equal(-1, opts.VulkanDevice);
        Assert.Equal("", opts.HwDevice);
        Assert.Equal("", opts.VideoTrackPath);

        // Apple M4V defaults
        Assert.Equal(0, opts.M4vVideoTrackIndex);
        Assert.Equal(0, opts.M4vAudioTrackIndex);
        Assert.Equal(640, opts.M4vAc3BitrateKbps);
        Assert.Equal("rus", opts.M4vAudioLang);
        Assert.True(opts.M4vAddChapters);
        Assert.False(opts.M4vEditBeforeMux);

        // Output defaults
        Assert.False(opts.Overwrite);
        Assert.False(opts.DryRun);
        Assert.Equal("", opts.OutputDir);
        Assert.Equal(0, opts.OutputDirStatus);
    }

    [Theory]
    [InlineData(1, -11.0, -1.5, 6.0)]  // EDM
    [InlineData(2, -11.0, -1.0, 7.0)]  // Rock
    [InlineData(3, -12.0, -1.0, 6.0)]  // Hip-Hop
    [InlineData(4, -16.0, -2.0, 12.0)] // Classical
    [InlineData(5, -16.0, -1.5, 7.0)]  // Podcast
    [InlineData(0, -11.0, -1.5, 7.0)]  // Default
    [InlineData(99, -11.0, -1.5, 7.0)] // Out of range -> Default
    public void ApplyGenreTargets_SetsExpectedTargetLevels(int genre, double expectedI, double expectedTp, double expectedLra)
    {
        var opts = new ConvertOptions { Genre = genre };
        opts.ApplyGenreTargets();

        Assert.Equal(expectedI, opts.ITarget);
        Assert.Equal(expectedTp, opts.TpTarget);
        Assert.Equal(expectedLra, opts.LraTarget);
    }
}
