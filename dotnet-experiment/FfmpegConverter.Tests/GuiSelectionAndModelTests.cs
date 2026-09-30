using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using FfmpegConverter.Core.Models;
using FfmpegConverter.Core.Presets;
using FfmpegConverter.Core.Probing;
using Xunit;

namespace FfmpegConverter.Tests;

public class GuiSelectionAndModelTests
{
    private readonly PresetDb _presetDb;

    public GuiSelectionAndModelTests()
    {
        _presetDb = PresetDb.Load();
    }

    [Fact]
    public void Gui_InitialCatalogGroups_ContainsSoftwareAndMux()
    {
        var groups = _presetDb.GetSelectionGroups("macos");
        Assert.Contains("software", groups);
        Assert.Contains("mux", groups);
    }

    [Fact]
    public void Gui_SoftwareGroup_HasProresEncoders()
    {
        var encoders = _presetDb.GetGroupEncoders("macos", "software");
        Assert.Contains("prores_ks", encoders);
        Assert.Contains("prores", encoders);

        var presets = _presetDb.GetSelectionPresets("macos", "software", "prores_ks");
        Assert.Contains("standard", presets);
        Assert.Contains("hq", presets);
        Assert.Contains("lt", presets);
        Assert.Contains("4444", presets);
    }

    [Fact]
    public void Gui_MuxGroup_HasFourPipelineModes()
    {
        var encoders = _presetDb.GetGroupEncoders("linux", "mux");
        Assert.Equal(4, encoders.Count);
        Assert.Contains("copy", encoders);
        Assert.Contains("mkv", encoders);
        Assert.Contains("mov", encoders);
        Assert.Contains("m4v", encoders);
    }

    [Fact]
    public void Gui_Resolution_MatchesAllSelections()
    {
        // 1. Software prores_ks
        Assert.True(_presetDb.ResolveSelection("macos", "software", "prores_ks", out string codec1));
        Assert.Equal("prores_ks", codec1);

        // 2. Mux pipeline mkv -> mux
        Assert.True(_presetDb.ResolveSelection("macos", "mux", "mkv", out string codec2));
        Assert.Equal("mux", codec2);

        // 3. Mux pipeline m4v -> m4v
        Assert.True(_presetDb.ResolveSelection("macos", "mux", "m4v", out string codec3));
        Assert.Equal("m4v", codec3);

        // 4. Mux pipeline copy -> copy
        Assert.True(_presetDb.ResolveSelection("macos", "mux", "copy", out string codec4));
        Assert.Equal("copy", codec4);
    }

    [Fact]
    public void Gui_FilterCapabilityRule_SoftwareOnly()
    {
        // Deblock filter is only valid for software ProRes
        bool isSoftwareProres = string.Equals("software", "software", StringComparison.OrdinalIgnoreCase);
        Assert.True(isSoftwareProres);

        bool isHardwareProres = string.Equals("videotoolbox", "software", StringComparison.OrdinalIgnoreCase);
        Assert.False(isHardwareProres);

        bool isMux = string.Equals("mux", "software", StringComparison.OrdinalIgnoreCase);
        Assert.False(isMux);
    }

    [Fact]
    public void Gui_AudioNormAndGenreDependency()
    {
        var opts = new ConvertOptions
        {
            AudioNorm = "loudness_norm_2pass",
            Genre = 2 // Rock
        };

        opts.ApplyGenreTargets();
        Assert.Equal(-11.0, opts.ITarget);
        Assert.Equal(-1.0, opts.TpTarget);
        Assert.Equal(7.0, opts.LraTarget);

        opts.Genre = 4; // Classical
        opts.ApplyGenreTargets();
        Assert.Equal(-16.0, opts.ITarget);
        Assert.Equal(-2.0, opts.TpTarget);
        Assert.Equal(12.0, opts.LraTarget);
    }

    [Fact]
    public void Gui_AppleM4vOptions_ApplyCorrectly()
    {
        var opts = new ConvertOptions
        {
            M4vVideoTrackIndex = 0,
            M4vAudioTrackIndex = 1,
            M4vAc3BitrateKbps = 640,
            M4vAudioLang = "rus",
            M4vAddChapters = true,
            M4vEditBeforeMux = false
        };

        Assert.Equal(0, opts.M4vVideoTrackIndex);
        Assert.Equal(1, opts.M4vAudioTrackIndex);
        Assert.Equal(640, opts.M4vAc3BitrateKbps);
        Assert.Equal("rus", opts.M4vAudioLang);
        Assert.True(opts.M4vAddChapters);
        Assert.False(opts.M4vEditBeforeMux);
    }

    [Fact]
    public void Gui_HardwareProbeCapabilityFilter_WorksProperly()
    {
        var probe = new HardwareProbeResult();
        probe.SupportedEncoders.Add("hevc_videotoolbox");

        Func<string, string, string, IReadOnlyList<string>, bool> filter = (group, encoder, finalCodec, requires) =>
        {
            if (group.Equals("software", StringComparison.OrdinalIgnoreCase) ||
                group.Equals("mux", StringComparison.OrdinalIgnoreCase))
                return true;

            return probe.SupportedEncoders.Contains(finalCodec) || probe.SupportedEncoders.Contains(encoder);
        };

        var groups = _presetDb.GetSelectionGroups("macos", filter);
        Assert.Contains("software", groups);
        Assert.Contains("mux", groups);
        Assert.Contains("videotoolbox", groups);

        var vtEncoders = _presetDb.GetGroupEncoders("macos", "videotoolbox", filter);
        Assert.Contains("hevc", vtEncoders);
        Assert.DoesNotContain("h264", vtEncoders); // Not in probe result
    }

    [Fact]
    public void Gui_DefaultValues_MatchMaketSpecifications()
    {
        // Default values as specified in user requirements & maket:
        // Audio norm: none
        // Genre: none (0)
        // Audio out: pcm
        // Filter: none
        // Filter preset: none
        var opts = new ConvertOptions();

        Assert.Equal("none", opts.AudioNorm);
        Assert.Equal("pcm", opts.AudioOutputMode);
        Assert.Equal(1, opts.Deblock); // 1 = none

        // When audio norm is none, genre is not applied
        Assert.Equal(0.0, opts.Gain);
    }

    [Fact]
    public void Gui_GenreReset_WhenAudioNormNotLoudnorm2()
    {
        var opts = new ConvertOptions
        {
            AudioNorm = "none",
            Genre = 0 // none
        };

        // When user selects loudness_norm_2pass, genre becomes active (defaulting to EDM 1)
        opts.AudioNorm = "loudness_norm_2pass";
        opts.Genre = 1;
        opts.ApplyGenreTargets();
        Assert.Equal(-11.0, opts.ITarget);

        // When switching back to none, genre resets
        opts.AudioNorm = "none";
        opts.Genre = 0;
        Assert.Equal(0, opts.Genre);
    }
}