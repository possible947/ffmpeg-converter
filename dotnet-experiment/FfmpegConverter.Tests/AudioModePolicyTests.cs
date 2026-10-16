using FfmpegConverter.Core.Models;
using FfmpegConverter.Core.Policy;

namespace FfmpegConverter.Tests;

public class AudioModePolicyTests
{
    [Fact]
    public void SoftwareConversion_DoesNotOfferOrAcceptCopy()
    {
        var options = new ConvertOptions
        {
            SelectionGroup = "software",
            SelectionEncoder = "prores_ks",
            Codec = "prores_ks"
        };

        Assert.Equal(
            new[] { AudioOutputMode.Pcm, AudioOutputMode.Aac, AudioOutputMode.AacAc3 },
            AudioModePolicy.GetAvailableModes(options, "linux"));
        Assert.Contains("only in Mux and HQ Converter",
            AudioModePolicy.Validate(
                new ConvertOptions
                {
                    SelectionGroup = "software",
                    Codec = "prores_ks",
                    AudioOutputMode = AudioOutputMode.Copy
                },
                "linux"));
    }

    [Fact]
    public void MuxM4v_OffersOnlyAacModesWithDualAudioFirst()
    {
        var options = new ConvertOptions
        {
            SelectionGroup = "mux",
            SelectionEncoder = "m4v",
            Codec = "m4v"
        };

        AudioModePolicy.ApplyDefault(options);
        Assert.Equal(AudioOutputMode.AacAc3, options.AudioOutputMode);
        Assert.Equal(
            new[] { AudioOutputMode.AacAc3, AudioOutputMode.Aac },
            AudioModePolicy.GetAvailableModes(options, "linux"));
        Assert.NotNull(AudioModePolicy.Validate(
            new ConvertOptions
            {
                SelectionGroup = "mux",
                SelectionEncoder = "m4v",
                Codec = "m4v",
                AudioOutputMode = AudioOutputMode.Pcm
            },
            "linux"));
    }

    [Fact]
    public void ApplyDefault_MuxM4vUsesDualAudioUnlessExplicitlySelected()
    {
        var options = new ConvertOptions
        {
            SelectionGroup = "mux",
            SelectionEncoder = "m4v",
            Codec = "m4v"
        };

        AudioModePolicy.ApplyDefault(options);
        Assert.Equal(AudioOutputMode.AacAc3, options.AudioOutputMode);

        var explicitOptions = new ConvertOptions
        {
            SelectionGroup = "mux",
            SelectionEncoder = "m4v",
            Codec = "m4v",
            AudioOutputMode = AudioOutputMode.Aac
        };
        AudioModePolicy.ApplyDefault(explicitOptions);
        Assert.Equal(AudioOutputMode.Aac, explicitOptions.AudioOutputMode);
    }

    [Fact]
    public void HqProRes_OffersAndRequiresPcm()
    {
        var options = new ConvertOptions
        {
            SelectionGroup = "hq_converter",
            SelectionEncoder = "ffmpeg-prores",
            Codec = "hq_converter"
        };

        Assert.Equal(new[] { AudioOutputMode.Pcm },
            AudioModePolicy.GetAvailableModes(options, "linux"));
        Assert.Contains("require PCM",
            AudioModePolicy.Validate(
                new ConvertOptions
                {
                    SelectionGroup = "hq_converter",
                    SelectionEncoder = "ffmpeg-prores",
                    Codec = "hq_converter",
                    AudioOutputMode = AudioOutputMode.Copy
                },
                "linux"));
    }

    [Fact]
    public void DirectM4v_IsSeparateFromMuxM4vAudioPolicy()
    {
        var options = new ConvertOptions
        {
            SelectionGroup = "m4v",
            SelectionEncoder = "m4v",
            Codec = "m4v",
            AudioOutputMode = AudioOutputMode.Copy
        };

        Assert.Equal(AudioWorkflowContext.DirectM4v, AudioModePolicy.GetWorkflow(options));
        Assert.Empty(AudioModePolicy.GetAvailableModes(options, "macos"));
        Assert.Null(AudioModePolicy.Validate(options, "macos"));
    }
}
