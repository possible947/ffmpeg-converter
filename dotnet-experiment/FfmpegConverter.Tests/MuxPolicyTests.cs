using FfmpegConverter.Core.Policy;

namespace FfmpegConverter.Tests;

public class MuxPolicyTests
{
    private static readonly MuxBuildProfile Profile = new(
        MuxPlatform.MacOS,
        MuxFfmpegVersion.V8_1);

    [Theory]
    [InlineData("copy", MuxOperationalMode.Copy)]
    [InlineData("mkv", MuxOperationalMode.Mkv)]
    [InlineData("mov", MuxOperationalMode.Mov)]
    [InlineData("m4v", MuxOperationalMode.M4v)]
    public void ParseMode_RecognizesCanonicalValues(string value, MuxOperationalMode expected)
    {
        Assert.True(MuxPolicyResolver.TryParseMode(value, out var actual));
        Assert.Equal(expected, actual);
    }

    [Theory]
    [InlineData("bad")]
    [InlineData("")]
    public void ParseMode_RejectsUnknownValues(string value)
    {
        Assert.False(MuxPolicyResolver.TryParseMode(value, out _));
    }

    [Theory]
    [InlineData(MuxOperationalMode.Copy, MuxVideoCodec.ProRes, MuxContainer.Mov, ".mov")]
    [InlineData(MuxOperationalMode.Copy, MuxVideoCodec.H264, MuxContainer.Mkv, ".mkv")]
    [InlineData(MuxOperationalMode.Mkv, MuxVideoCodec.Unknown, MuxContainer.Mkv, ".mkv")]
    [InlineData(MuxOperationalMode.Mov, MuxVideoCodec.Unknown, MuxContainer.Mov, ".mov")]
    [InlineData(MuxOperationalMode.M4v, MuxVideoCodec.Unknown, MuxContainer.M4v, ".m4v")]
    public void Resolve_MapsContainerAndExtension(
        MuxOperationalMode mode,
        MuxVideoCodec sourceCodec,
        MuxContainer container,
        string extension)
    {
        var result = MuxPolicyResolver.Resolve(
            Profile, mode, null, sourceCodec, MuxVideoCodec.Unknown);

        Assert.True(result.Success, result.ErrorMessage);
        Assert.Equal(container, result.Policy!.Container);
        Assert.Equal(extension, result.Policy.OutputExtension);
    }

    [Theory]
    [InlineData(MuxOperationalMode.Mkv, AudioOutputMode.Pcm)]
    [InlineData(MuxOperationalMode.Mov, AudioOutputMode.Pcm)]
    [InlineData(MuxOperationalMode.M4v, AudioOutputMode.AacAc3)]
    public void Resolve_UsesReferenceDefaultAudio(
        MuxOperationalMode mode,
        AudioOutputMode expected)
    {
        var result = MuxPolicyResolver.Resolve(
            Profile, mode, null, MuxVideoCodec.Unknown, MuxVideoCodec.Unknown);

        Assert.True(result.Success, result.ErrorMessage);
        Assert.True(result.Policy!.AudioModeWasDefaulted);
        Assert.Equal(expected, result.Policy.AudioMode);
    }

    [Theory]
    [InlineData(MuxOperationalMode.Mov, AudioOutputMode.Copy)]
    [InlineData(MuxOperationalMode.Mov, AudioOutputMode.Aac)]
    [InlineData(MuxOperationalMode.M4v, AudioOutputMode.Pcm)]
    [InlineData(MuxOperationalMode.M4v, AudioOutputMode.Copy)]
    public void Resolve_RejectsUnavailableContainerAudio(
        MuxOperationalMode mode,
        AudioOutputMode audio)
    {
        var result = MuxPolicyResolver.Resolve(
            Profile, mode, audio, MuxVideoCodec.Unknown, MuxVideoCodec.Unknown);

        Assert.False(result.Success);
        Assert.Equal(PolicyError.AudioModeUnavailable, result.Error);
    }

    [Theory]
    [InlineData(MuxOperationalMode.Mkv, MuxVideoCodec.H264, true)]
    [InlineData(MuxOperationalMode.Mkv, MuxVideoCodec.Hevc, true)]
    [InlineData(MuxOperationalMode.Mkv, MuxVideoCodec.ProRes, true)]
    [InlineData(MuxOperationalMode.Mkv, MuxVideoCodec.Av1, true)]
    [InlineData(MuxOperationalMode.Mov, MuxVideoCodec.H264, true)]
    [InlineData(MuxOperationalMode.Mov, MuxVideoCodec.Hevc, true)]
    [InlineData(MuxOperationalMode.Mov, MuxVideoCodec.ProRes, true)]
    [InlineData(MuxOperationalMode.Mov, MuxVideoCodec.Av1, false)]
    [InlineData(MuxOperationalMode.M4v, MuxVideoCodec.H264, true)]
    [InlineData(MuxOperationalMode.M4v, MuxVideoCodec.Hevc, true)]
    [InlineData(MuxOperationalMode.M4v, MuxVideoCodec.ProRes, false)]
    [InlineData(MuxOperationalMode.Mkv, MuxVideoCodec.Unsupported, false)]
    [InlineData(MuxOperationalMode.Mov, MuxVideoCodec.Unsupported, false)]
    [InlineData(MuxOperationalMode.M4v, MuxVideoCodec.Unsupported, false)]
    public void Resolve_ValidatesReplacementCodec(
        MuxOperationalMode mode,
        MuxVideoCodec replacement,
        bool expectedSuccess)
    {
        var result = MuxPolicyResolver.Resolve(
            Profile, mode, null, MuxVideoCodec.Unknown, replacement);

        Assert.Equal(expectedSuccess, result.Success);
        if (!expectedSuccess)
            Assert.Equal(PolicyError.UnsupportedReplacementVideo, result.Error);
    }

    [Theory]
    [InlineData(MuxVideoCodec.ProRes, AudioOutputMode.Pcm)]
    [InlineData(MuxVideoCodec.H264, AudioOutputMode.Copy)]
    public void ResolveCopyMode_SelectsContainerAndKeepsValidAudio(
        MuxVideoCodec source,
        AudioOutputMode audio)
    {
        var result = MuxPolicyResolver.Resolve(
            Profile, MuxOperationalMode.Copy, audio, source, MuxVideoCodec.Unknown);

        Assert.True(result.Success, result.ErrorMessage);
        Assert.False(result.Policy!.RequiresAddTrack);
        Assert.Equal(audio == AudioOutputMode.Copy
            ? AudioTrackSelectionPolicy.All
            : AudioTrackSelectionPolicy.First, result.Policy.AudioTrackPolicy);
    }

    [Fact]
    public void Resolve_RecordsCopyTrackAndAttachmentPolicy()
    {
        var result = MuxPolicyResolver.Resolve(
            Profile, MuxOperationalMode.Mkv, AudioOutputMode.Copy,
            MuxVideoCodec.Unknown, MuxVideoCodec.H264);

        Assert.True(result.Success, result.ErrorMessage);
        Assert.Equal(AudioTrackSelectionPolicy.All, result.Policy!.AudioTrackPolicy);
        Assert.Equal(StreamAction.Preserve, result.Policy.AttachmentAction);
        Assert.True(result.Policy.RequiresVideoReplacement);
    }

    [Fact]
    public void ResolveMov_DropsAttachmentsAndKeepsOtherSourceStreams()
    {
        var result = MuxPolicyResolver.Resolve(
            Profile, MuxOperationalMode.Mov, AudioOutputMode.Pcm,
            MuxVideoCodec.Unknown, MuxVideoCodec.H264);

        Assert.True(result.Success, result.ErrorMessage);
        Assert.Equal(StreamAction.Drop, result.Policy!.AttachmentAction);
        Assert.Equal(StreamAction.Preserve, result.Policy.ChapterAction);
        Assert.Equal(StreamAction.Preserve, result.Policy.MetadataAction);
        Assert.Equal(StreamAction.Preserve, result.Policy.SubtitleAction);
    }

    [Fact]
    public void ResolveM4v_DropsAttachments()
    {
        PolicyResult result = MuxPolicyResolver.Resolve(
            Profile, MuxOperationalMode.M4v, AudioOutputMode.Aac,
            MuxVideoCodec.Unknown, MuxVideoCodec.H264);

        Assert.True(result.Success, result.ErrorMessage);
        Assert.Equal(StreamAction.Drop, result.Policy!.AttachmentAction);
    }

    [Theory]
    [InlineData("mov_text", MuxContainer.Mkv, SubtitleAction.Convert, "srt")]
    [InlineData("subrip", MuxContainer.Mkv, SubtitleAction.Copy, "copy")]
    [InlineData("subrip", MuxContainer.Mov, SubtitleAction.Convert, "mov_text")]
    [InlineData("ass", MuxContainer.M4v, SubtitleAction.Convert, "mov_text")]
    [InlineData("mov_text", MuxContainer.M4v, SubtitleAction.Copy, "copy")]
    [InlineData("hdmv_pgs_subtitle", MuxContainer.Mov, SubtitleAction.Drop, null)]
    [InlineData("unknown", MuxContainer.Mkv, SubtitleAction.Drop, null)]
    public void ResolveSubtitlePolicy_MatchesReference(
        string codec,
        MuxContainer container,
        SubtitleAction expectedAction,
        string? expectedEncoder)
    {
        var policy = MuxPolicyResolver.ResolveSubtitle(codec, container);

        Assert.Equal(expectedAction, policy.Action);
        Assert.Equal(expectedEncoder, policy.Encoder);
    }

    [Theory]
    [InlineData(AudioWorkflowContext.OnePass, MuxOperationalMode.Mkv, MuxVideoCodec.Unknown, false)]
    [InlineData(AudioWorkflowContext.Mux, MuxOperationalMode.Mkv, MuxVideoCodec.Unknown, true)]
    [InlineData(AudioWorkflowContext.Mux, MuxOperationalMode.Mov, MuxVideoCodec.Unknown, false)]
    [InlineData(AudioWorkflowContext.Mux, MuxOperationalMode.M4v, MuxVideoCodec.Unknown, false)]
    [InlineData(AudioWorkflowContext.Hq, MuxOperationalMode.Mkv, MuxVideoCodec.H264, true)]
    [InlineData(AudioWorkflowContext.Hq, MuxOperationalMode.Mkv, MuxVideoCodec.ProRes, false)]
    public void GetAvailableAudioModes_IsContextAware(
        AudioWorkflowContext context,
        MuxOperationalMode mode,
        MuxVideoCodec replacement,
        bool expectedCopy)
    {
        var modes = MuxPolicyResolver.GetAvailableAudioModes(
            Profile, context, mode, Array.Empty<MuxVideoCodec>(), replacement);

        Assert.Equal(expectedCopy, modes.Contains(AudioOutputMode.Copy));
        if (context == AudioWorkflowContext.OnePass)
            Assert.Equal(
                new[] { AudioOutputMode.Pcm, AudioOutputMode.Aac, AudioOutputMode.AacAc3 },
                modes);
        if (context == AudioWorkflowContext.Hq && replacement == MuxVideoCodec.ProRes)
            Assert.Equal(new[] { AudioOutputMode.Pcm }, modes);
    }

    [Fact]
    public void GetAvailableAudioModes_UsesOnlyModesValidAcrossQueuedCopySources()
    {
        var modes = MuxPolicyResolver.GetAvailableAudioModes(
            Profile,
            AudioWorkflowContext.Mux,
            MuxOperationalMode.Copy,
            [MuxVideoCodec.H264, MuxVideoCodec.ProRes],
            MuxVideoCodec.Unknown);

        Assert.Equal(new[] { AudioOutputMode.Pcm }, modes);
    }

    [Theory]
    [InlineData(MuxPlatform.Linux)]
    [InlineData(MuxPlatform.MacOS)]
    [InlineData(MuxPlatform.Windows)]
    public void PolicySupportsBothReferenceFfmpegVersions(MuxPlatform platform)
    {
        foreach (MuxFfmpegVersion version in Enum.GetValues<MuxFfmpegVersion>())
        {
            var result = MuxPolicyResolver.Resolve(
                new MuxBuildProfile(platform, version),
                MuxOperationalMode.M4v,
                AudioOutputMode.Aac,
                MuxVideoCodec.Unknown,
                MuxVideoCodec.H264);

            Assert.True(result.Success, result.ErrorMessage);
        }
    }

    [Fact]
    public void ParseVideoCodec_RecognizesReferenceAliases()
    {
        Assert.Equal(MuxVideoCodec.ProRes, MuxPolicyResolver.ParseVideoCodec("prores_ks"));
        Assert.Equal(MuxVideoCodec.H264, MuxPolicyResolver.ParseVideoCodec("libx264"));
        Assert.Equal(MuxVideoCodec.Hevc, MuxPolicyResolver.ParseVideoCodec("hevc_videotoolbox"));
        Assert.Equal(MuxVideoCodec.Av1, MuxPolicyResolver.ParseVideoCodec("av1_nvenc"));
        Assert.Equal(MuxVideoCodec.Unsupported, MuxPolicyResolver.ParseVideoCodec("mpeg2video"));
        Assert.Equal(MuxVideoCodec.Unknown, MuxPolicyResolver.ParseVideoCodec(null));
    }

    [Fact]
    public void ResolveHqProRes_ForcesMovAndPcm()
    {
        var result = MuxPolicyResolver.ResolveHq(
            Profile, MuxVideoCodec.ProRes, AudioOutputMode.Aac);

        Assert.True(result.Success, result.ErrorMessage);
        Assert.Equal(MuxContainer.Mov, result.Policy!.Container);
        Assert.Equal(AudioOutputMode.Pcm, result.Policy.AudioMode);
        Assert.Equal(StreamAction.Drop, result.Policy.AttachmentAction);
    }
}
