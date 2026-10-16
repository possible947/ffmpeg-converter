using FfmpegConverter.Core.Models;

namespace FfmpegConverter.Core.Policy;

public static class AudioModePolicy
{
    public static void ApplyDefault(ConvertOptions options)
    {
        if (options.AudioOutputModeSpecified ||
            GetWorkflow(options) != AudioWorkflowContext.Mux ||
            !TryGetMuxMode(options, out MuxOperationalMode mode) ||
            mode != MuxOperationalMode.M4v)
            return;

        options.AudioOutputMode = AudioOutputMode.AacAc3;
    }

    public static AudioWorkflowContext GetWorkflow(ConvertOptions options)
    {
        if (IsCodec(options.Codec, "m4v") &&
            !IsGroup(options.SelectionGroup, "mux"))
            return AudioWorkflowContext.DirectM4v;

        if (IsCodec(options.Codec, "hq_converter") ||
            IsGroup(options.SelectionGroup, "hq_converter"))
            return AudioWorkflowContext.Hq;

        if (IsGroup(options.SelectionGroup, "mux") ||
            IsGroup(options.SelectionGroup, "copy") ||
            IsCodec(options.Codec, "mux") ||
            IsCodec(options.Codec, "copy"))
            return AudioWorkflowContext.Mux;

        return AudioWorkflowContext.OnePass;
    }

    public static bool TryGetMuxMode(ConvertOptions options, out MuxOperationalMode mode)
    {
        if (IsGroup(options.SelectionGroup, "copy"))
        {
            mode = MuxOperationalMode.Copy;
            return true;
        }

        if (IsGroup(options.SelectionGroup, "mux"))
        {
            if (MuxPolicyResolver.TryParseMode(options.SelectionEncoder, out mode))
                return true;
            string fallbackMode = string.IsNullOrEmpty(options.Preset) ||
                                  options.Preset.Equals("default", StringComparison.OrdinalIgnoreCase)
                ? "mkv"
                : options.Preset;
            if (MuxPolicyResolver.TryParseMode(fallbackMode, out mode))
                return true;
        }
        else if (IsCodec(options.Codec, "copy"))
        {
            if (string.IsNullOrEmpty(options.Preset) ||
                options.Preset.Equals("default", StringComparison.Ordinal))
            {
                mode = MuxOperationalMode.Copy;
                return true;
            }
            if (MuxPolicyResolver.TryParseMode(options.Preset, out mode))
                return true;
        }
        else if (IsCodec(options.Codec, "mux") || IsCodec(options.Codec, "m4v"))
        {
            string selectedMode = string.IsNullOrEmpty(options.Preset)
                ? "mkv"
                : options.Preset;
            if (MuxPolicyResolver.TryParseMode(selectedMode, out mode))
                return true;
            if (IsCodec(options.Codec, "mux"))
            {
                mode = MuxOperationalMode.Mkv;
                return true;
            }
        }

        mode = default;
        return false;
    }

    public static IReadOnlyList<AudioOutputMode> GetAvailableModes(
        ConvertOptions options,
        string platform,
        IReadOnlyList<MuxVideoCodec>? sourceVideoCodecs = null,
        MuxVideoCodec replacementVideoCodec = MuxVideoCodec.Unknown)
    {
        AudioWorkflowContext workflow = GetWorkflow(options);
        if (workflow == AudioWorkflowContext.DirectM4v)
            return Array.Empty<AudioOutputMode>();
        if (workflow == AudioWorkflowContext.OnePass)
        {
            return MuxPolicyResolver.GetAvailableAudioModes(
                CreateProfile(platform), workflow, default,
                Array.Empty<MuxVideoCodec>(), MuxVideoCodec.Unknown);
        }

        if (workflow == AudioWorkflowContext.Hq)
        {
            MuxVideoCodec hqCodec = MuxPolicyResolver.ParseVideoCodec(options.SelectionEncoder);
            return MuxPolicyResolver.GetAvailableAudioModes(
                CreateProfile(platform), workflow, default,
                Array.Empty<MuxVideoCodec>(), hqCodec);
        }

        if (!TryGetMuxMode(options, out MuxOperationalMode mode))
            return Array.Empty<AudioOutputMode>();

        return MuxPolicyResolver.GetAvailableAudioModes(
            CreateProfile(platform), workflow, mode,
            sourceVideoCodecs ?? Array.Empty<MuxVideoCodec>(),
            replacementVideoCodec);
    }

    public static string? Validate(
        ConvertOptions options,
        string platform,
        MuxVideoCodec sourceVideoCodec = MuxVideoCodec.Unknown,
        MuxVideoCodec replacementVideoCodec = MuxVideoCodec.Unknown)
    {
        AudioWorkflowContext workflow = GetWorkflow(options);
        if (workflow == AudioWorkflowContext.DirectM4v)
            return null;

        IReadOnlyList<AudioOutputMode> available = GetAvailableModes(
            options, platform, [sourceVideoCodec], replacementVideoCodec);
        if (!available.Contains(options.AudioOutputMode))
        {
            if (workflow == AudioWorkflowContext.Hq &&
                MuxPolicyResolver.ParseVideoCodec(options.SelectionEncoder) == MuxVideoCodec.ProRes)
                return "HQ ProRes encoders require PCM audio output.";
            if (workflow == AudioWorkflowContext.OnePass &&
                options.AudioOutputMode == AudioOutputMode.Copy)
                return "Copy audio is available only in Mux and HQ Converter workflows.";
            return $"Audio mode {MuxPolicyResolver.GetAudioModeName(options.AudioOutputMode)} is unavailable for this workflow.";
        }

        if (workflow == AudioWorkflowContext.Hq)
        {
            PolicyResult hqResult = MuxPolicyResolver.ResolveHq(
                CreateProfile(platform),
                MuxPolicyResolver.ParseVideoCodec(options.SelectionEncoder),
                options.AudioOutputMode);
            return hqResult.Success ? null : hqResult.ErrorMessage;
        }

        if (workflow == AudioWorkflowContext.Mux &&
            TryGetMuxMode(options, out MuxOperationalMode mode))
        {
            PolicyResult result = MuxPolicyResolver.Resolve(
                CreateProfile(platform), mode, options.AudioOutputMode,
                sourceVideoCodec, replacementVideoCodec);
            return result.Success ? null : result.ErrorMessage;
        }

        return null;
    }

    public static MuxPolicy? ResolveMuxPolicy(
        ConvertOptions options,
        string platform,
        MuxVideoCodec sourceVideoCodec,
        MuxVideoCodec replacementVideoCodec,
        out string error)
    {
        if (!TryGetMuxMode(options, out MuxOperationalMode mode))
        {
            error = "Mux mode could not be resolved from the selected catalog mode.";
            return null;
        }

        PolicyResult result = MuxPolicyResolver.Resolve(
            CreateProfile(platform), mode, options.AudioOutputMode,
            sourceVideoCodec, replacementVideoCodec);
        error = result.ErrorMessage;
        return result.Policy;
    }

    private static MuxBuildProfile CreateProfile(string platform)
    {
        MuxPlatform resolvedPlatform = platform.ToLowerInvariant() switch
        {
            "linux" => MuxPlatform.Linux,
            "macos" => MuxPlatform.MacOS,
            "windows" => MuxPlatform.Windows,
            _ => throw new ArgumentOutOfRangeException(nameof(platform), platform, "Unknown platform.")
        };
        return new MuxBuildProfile(resolvedPlatform, MuxFfmpegVersion.V8_1);
    }

    private static bool IsGroup(string value, string expected) =>
        string.Equals(value, expected, StringComparison.OrdinalIgnoreCase);

    private static bool IsCodec(string value, string expected) =>
        string.Equals(value, expected, StringComparison.OrdinalIgnoreCase);
}
