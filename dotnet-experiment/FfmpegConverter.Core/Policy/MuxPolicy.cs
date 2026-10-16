namespace FfmpegConverter.Core.Policy;

public enum AudioWorkflowContext
{
    OnePass,
    Mux,
    Hq,
    DirectM4v
}

public enum MuxOperationalMode
{
    Copy,
    Mkv,
    Mov,
    M4v
}

public enum MuxContainer
{
    Mkv,
    Mov,
    M4v
}

public enum AudioOutputMode
{
    Copy,
    Pcm,
    Aac,
    AacAc3
}

public enum MuxVideoCodec
{
    Unknown,
    ProRes,
    H264,
    Hevc,
    Av1,
    Unsupported
}

public enum MuxPlatform
{
    Linux,
    MacOS,
    Windows
}

public enum MuxFfmpegVersion
{
    V8_1,
    V9_0
}

public enum AudioTrackSelectionPolicy
{
    First,
    All
}

public enum StreamAction
{
    Preserve,
    Convert,
    Drop,
    Reject
}

public enum SubtitleAction
{
    Copy,
    Convert,
    Drop
}

public enum PolicyError
{
    None,
    InvalidBuildProfile,
    UnsupportedReplacementVideo,
    AudioModeUnavailable,
    InvalidOperationalMode
}

public sealed record MuxBuildProfile(MuxPlatform Platform, MuxFfmpegVersion FfmpegVersion);

public sealed record SubtitlePolicy(SubtitleAction Action, string? Encoder);

public sealed record MuxPolicy(
    AudioWorkflowContext Workflow,
    MuxOperationalMode OperationalMode,
    MuxContainer Container,
    string OutputExtension,
    AudioOutputMode AudioMode,
    bool AudioModeWasDefaulted,
    bool RequiresAddTrack,
    bool RequiresVideoReplacement,
    bool PreserveVideo,
    bool PreserveChapters,
    bool PreserveMetadata,
    bool PreserveSubtitlesWhenSupported,
    AudioTrackSelectionPolicy AudioTrackPolicy,
    StreamAction VideoAction,
    StreamAction AudioAction,
    StreamAction SubtitleAction,
    StreamAction AttachmentAction,
    StreamAction ChapterAction,
    StreamAction MetadataAction,
    StreamAction LanguageAction,
    StreamAction DispositionAction);

public sealed record PolicyResult(MuxPolicy? Policy, PolicyError Error, string ErrorMessage)
{
    public bool Success => Error == PolicyError.None && Policy != null;

    public static PolicyResult Succeeded(MuxPolicy policy) =>
        new(policy, PolicyError.None, string.Empty);

    public static PolicyResult Failed(PolicyError error, string message) =>
        new(null, error, message);
}

public static class MuxPolicyResolver
{
    private static readonly AudioOutputMode[] OnePassAudioModes =
        [AudioOutputMode.Pcm, AudioOutputMode.Aac, AudioOutputMode.AacAc3];

    public static bool TryParseMode(string? value, out MuxOperationalMode mode)
    {
        mode = value switch
        {
            "copy" => MuxOperationalMode.Copy,
            "mkv" => MuxOperationalMode.Mkv,
            "mov" => MuxOperationalMode.Mov,
            "m4v" => MuxOperationalMode.M4v,
            _ => (MuxOperationalMode)(-1)
        };
        return Enum.IsDefined(mode);
    }

    public static bool TryParseAudioMode(string? value, out AudioOutputMode mode)
    {
        mode = value switch
        {
            "copy" => AudioOutputMode.Copy,
            "pcm" => AudioOutputMode.Pcm,
            "fdk_aac_320" or "aac" => AudioOutputMode.Aac,
            "fdk_aac_320_ac3_640" or "aac+ac3" =>
                AudioOutputMode.AacAc3,
            _ => (AudioOutputMode)(-1)
        };
        return Enum.IsDefined(mode);
    }

    public static string GetAudioModeName(AudioOutputMode mode) => mode switch
    {
        AudioOutputMode.Copy => "copy",
        AudioOutputMode.Pcm => "pcm",
        AudioOutputMode.Aac => "fdk_aac_320",
        AudioOutputMode.AacAc3 => "fdk_aac_320_ac3_640",
        _ => throw new ArgumentOutOfRangeException(nameof(mode), mode, null)
    };

    public static string GetAudioModeDisplayName(AudioOutputMode mode) => mode switch
    {
        AudioOutputMode.Copy => "copy",
        AudioOutputMode.Pcm => "pcm",
        AudioOutputMode.Aac => "fdk_aac_320",
        AudioOutputMode.AacAc3 => "fdk_aac_320_ac3_640",
        _ => throw new ArgumentOutOfRangeException(nameof(mode), mode, null)
    };

    public static MuxVideoCodec ParseVideoCodec(string? codec)
    {
        if (string.IsNullOrWhiteSpace(codec))
            return MuxVideoCodec.Unknown;

        string value = codec;
        if (value.Contains("prores", StringComparison.Ordinal))
            return MuxVideoCodec.ProRes;
        if (value.Contains("h264", StringComparison.Ordinal) ||
            value.Contains("avc", StringComparison.Ordinal) ||
            value.Contains("x264", StringComparison.Ordinal))
            return MuxVideoCodec.H264;
        if (value.Contains("hevc", StringComparison.Ordinal) ||
            value.Contains("h265", StringComparison.Ordinal) ||
            value.Contains("x265", StringComparison.Ordinal))
            return MuxVideoCodec.Hevc;
        if (value.Contains("av1", StringComparison.Ordinal))
            return MuxVideoCodec.Av1;
        return MuxVideoCodec.Unsupported;
    }

    public static PolicyResult Resolve(
        MuxBuildProfile profile,
        MuxOperationalMode mode,
        AudioOutputMode? requestedAudioMode,
        MuxVideoCodec sourceVideoCodec,
        MuxVideoCodec replacementVideoCodec)
    {
        if (!IsSupported(profile))
        {
            return PolicyResult.Failed(
                PolicyError.InvalidBuildProfile,
                "Mux policy requires a supported FFmpeg build profile.");
        }

        MuxContainer container = mode switch
        {
            MuxOperationalMode.Copy when sourceVideoCodec == MuxVideoCodec.ProRes => MuxContainer.Mov,
            MuxOperationalMode.Copy => MuxContainer.Mkv,
            MuxOperationalMode.Mkv => MuxContainer.Mkv,
            MuxOperationalMode.Mov => MuxContainer.Mov,
            MuxOperationalMode.M4v => MuxContainer.M4v,
            _ => (MuxContainer)(-1)
        };
        if (!Enum.IsDefined(container))
        {
            return PolicyResult.Failed(
                PolicyError.InvalidOperationalMode,
                $"Unsupported mux mode: {mode}.");
        }

        if (IsUnsupportedReplacement(replacementVideoCodec, mode))
        {
            return PolicyResult.Failed(
                PolicyError.UnsupportedReplacementVideo,
                $"{replacementVideoCodec} video is not supported in {container} output.");
        }

        AudioOutputMode defaultAudio = GetDefaultAudioMode(container);
        AudioOutputMode audioMode = requestedAudioMode ?? defaultAudio;
        if (!GetAvailableAudioModes(
                profile,
                AudioWorkflowContext.Mux,
                mode,
                [sourceVideoCodec],
                replacementVideoCodec).Contains(audioMode))
        {
            return PolicyResult.Failed(
                PolicyError.AudioModeUnavailable,
                $"Audio mode {GetAudioModeName(audioMode)} is unavailable for {container} output.");
        }

        return PolicyResult.Succeeded(CreatePolicy(
            AudioWorkflowContext.Mux,
            mode,
            container,
            audioMode,
            requestedAudioMode == null,
            mode != MuxOperationalMode.Copy));
    }

    public static PolicyResult ResolveHq(
        MuxBuildProfile profile,
        MuxVideoCodec replacementVideoCodec,
        AudioOutputMode? requestedAudioMode)
    {
        if (!IsSupported(profile))
        {
            return PolicyResult.Failed(
                PolicyError.InvalidBuildProfile,
                "HQ Converter policy requires a supported FFmpeg build profile.");
        }

        if (replacementVideoCodec == MuxVideoCodec.Unsupported ||
            replacementVideoCodec == MuxVideoCodec.Unknown)
        {
            return PolicyResult.Failed(
                PolicyError.UnsupportedReplacementVideo,
                "HQ Converter requires a supported replacement video codec.");
        }

        bool isProRes = replacementVideoCodec == MuxVideoCodec.ProRes;
        MuxOperationalMode mode = isProRes ? MuxOperationalMode.Mov : MuxOperationalMode.Mkv;
        AudioOutputMode audioMode = isProRes
            ? AudioOutputMode.Pcm
            : requestedAudioMode ?? AudioOutputMode.Pcm;

        if (!GetAvailableAudioModes(
                profile, AudioWorkflowContext.Hq, mode, [], replacementVideoCodec)
            .Contains(audioMode))
        {
            return PolicyResult.Failed(
                PolicyError.AudioModeUnavailable,
                $"Audio mode {GetAudioModeName(audioMode)} is unavailable for this HQ Converter encoder.");
        }

        return PolicyResult.Succeeded(CreatePolicy(
            AudioWorkflowContext.Hq,
            mode,
            isProRes ? MuxContainer.Mov : MuxContainer.Mkv,
            audioMode,
            isProRes || requestedAudioMode == null,
            true));
    }

    public static IReadOnlyList<AudioOutputMode> GetAvailableAudioModes(
        MuxBuildProfile profile,
        AudioWorkflowContext workflow,
        MuxOperationalMode mode,
        IReadOnlyList<MuxVideoCodec> sourceVideoCodecs,
        MuxVideoCodec replacementVideoCodec)
    {
        if (workflow == AudioWorkflowContext.DirectM4v)
            return Array.Empty<AudioOutputMode>();

        if (workflow == AudioWorkflowContext.OnePass)
            return OnePassAudioModes;

        if (!IsSupported(profile))
            return Array.Empty<AudioOutputMode>();

        if (workflow == AudioWorkflowContext.Hq &&
            replacementVideoCodec is MuxVideoCodec.Unknown or MuxVideoCodec.Unsupported)
            return Array.Empty<AudioOutputMode>();

        AudioOutputMode[] candidates = workflow == AudioWorkflowContext.Hq
            ? [AudioOutputMode.Pcm, AudioOutputMode.Copy, AudioOutputMode.Aac, AudioOutputMode.AacAc3]
            : [
                GetDefaultAudioMode(GetContainer(mode, sourceVideoCodecs)),
                AudioOutputMode.Copy,
                AudioOutputMode.Pcm,
                AudioOutputMode.Aac,
                AudioOutputMode.AacAc3
            ];

        var uniqueCandidates = new List<AudioOutputMode>(candidates.Length);
        foreach (AudioOutputMode candidate in candidates)
        {
            if (!uniqueCandidates.Contains(candidate) &&
                IsAvailableAcrossSources(
                    workflow, mode, sourceVideoCodecs, candidate, replacementVideoCodec))
            {
                uniqueCandidates.Add(candidate);
            }
        }

        return uniqueCandidates;
    }

    public static SubtitlePolicy ResolveSubtitle(string? codec, MuxContainer container)
    {
        if (codec == null)
            return new SubtitlePolicy(SubtitleAction.Drop, null);

        string? encoder = container == MuxContainer.Mkv
            ? codec switch
            {
                "mov_text" => "srt",
                "subrip" or "ass" or "ssa" or "webvtt" or "hdmv_pgs_subtitle" or
                    "dvd_subtitle" or "dvb_subtitle" => "copy",
                _ => null
            }
            : codec switch
            {
                "mov_text" => "copy",
                "subrip" or "ass" or "ssa" or "webvtt" => "mov_text",
                _ => null
            };

        return encoder == null
            ? new SubtitlePolicy(SubtitleAction.Drop, null)
            : new SubtitlePolicy(
                encoder == "copy" ? SubtitleAction.Copy : SubtitleAction.Convert,
                encoder);
    }

    private static bool IsSupported(MuxBuildProfile? profile) =>
        profile != null && Enum.IsDefined(profile.Platform) &&
        Enum.IsDefined(profile.FfmpegVersion);

    private static bool IsUnsupportedReplacement(MuxVideoCodec codec, MuxOperationalMode mode)
    {
        if (mode == MuxOperationalMode.Copy)
            return false;
        if (codec == MuxVideoCodec.Unsupported)
            return true;
        if (mode == MuxOperationalMode.M4v)
            return codec is MuxVideoCodec.ProRes;
        if (mode == MuxOperationalMode.Mov)
            return codec is MuxVideoCodec.Av1;
        return false;
    }

    private static MuxContainer GetContainer(
        MuxOperationalMode mode,
        IReadOnlyList<MuxVideoCodec> sourceVideoCodecs) =>
        mode switch
        {
            MuxOperationalMode.Copy when sourceVideoCodecs.Contains(MuxVideoCodec.ProRes) =>
                MuxContainer.Mov,
            MuxOperationalMode.Copy => MuxContainer.Mkv,
            MuxOperationalMode.Mkv => MuxContainer.Mkv,
            MuxOperationalMode.Mov => MuxContainer.Mov,
            MuxOperationalMode.M4v => MuxContainer.M4v,
            _ => MuxContainer.Mkv
        };

    private static AudioOutputMode GetDefaultAudioMode(MuxContainer container) =>
        container == MuxContainer.M4v ? AudioOutputMode.AacAc3 : AudioOutputMode.Pcm;

    private static bool IsAvailableAcrossSources(
        AudioWorkflowContext workflow,
        MuxOperationalMode mode,
        IReadOnlyList<MuxVideoCodec> sourceVideoCodecs,
        AudioOutputMode audioMode,
        MuxVideoCodec replacementVideoCodec)
    {
        if (workflow == AudioWorkflowContext.OnePass)
            return OnePassAudioModes.Contains(audioMode);
        if (workflow == AudioWorkflowContext.Hq && replacementVideoCodec == MuxVideoCodec.ProRes)
            return audioMode == AudioOutputMode.Pcm;

        if (workflow == AudioWorkflowContext.Hq)
            return IsAudioModeAvailable(MuxContainer.Mkv, audioMode);

        IReadOnlyList<MuxVideoCodec> sources = sourceVideoCodecs.Count == 0
            ? [MuxVideoCodec.Unknown]
            : sourceVideoCodecs;
        foreach (MuxVideoCodec source in sources)
        {
            MuxContainer container = GetContainer(mode, [source]);
            if (IsUnsupportedReplacement(replacementVideoCodec, mode) ||
                !IsAudioModeAvailable(container, audioMode))
                return false;
        }
        return true;
    }

    private static bool IsAudioModeAvailable(MuxContainer container, AudioOutputMode audioMode) =>
        container switch
        {
            MuxContainer.Mkv => true,
            MuxContainer.Mov => audioMode == AudioOutputMode.Pcm,
            MuxContainer.M4v => audioMode is AudioOutputMode.Aac or AudioOutputMode.AacAc3,
            _ => false
        };
    private static MuxPolicy CreatePolicy(
        AudioWorkflowContext workflow,
        MuxOperationalMode mode,
        MuxContainer container,
        AudioOutputMode audioMode,
        bool defaulted,
        bool requiresVideoReplacement)
    {
        bool copyAudio = audioMode == AudioOutputMode.Copy;
        return new MuxPolicy(
            workflow,
            mode,
            container,
            container switch
            {
                MuxContainer.Mkv => ".mkv",
                MuxContainer.Mov => ".mov",
                MuxContainer.M4v => ".m4v",
                _ => throw new ArgumentOutOfRangeException(nameof(container), container, null)
            },
            audioMode,
            defaulted,
            mode != MuxOperationalMode.Copy,
            requiresVideoReplacement,
            true,
            true,
            true,
            true,
            copyAudio ? AudioTrackSelectionPolicy.All : AudioTrackSelectionPolicy.First,
            StreamAction.Preserve,
            copyAudio ? StreamAction.Preserve : StreamAction.Convert,
            StreamAction.Preserve,
            container == MuxContainer.Mov ? StreamAction.Drop : StreamAction.Preserve,
            StreamAction.Preserve,
            StreamAction.Preserve,
            StreamAction.Preserve,
            StreamAction.Preserve);
    }
}
