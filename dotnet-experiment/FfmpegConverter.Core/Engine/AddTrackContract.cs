using System.IO;
using FfmpegConverter.Core.Models;
using FfmpegConverter.Core.Policy;

namespace FfmpegConverter.Core.Engine;

public sealed record AddTrackRequest(
    string SourcePath,
    string ReplacementVideoPath,
    string OutputPath,
    string OutputDirectory,
    ConvertOptions SourceOptions,
    ConvertOptions MuxOptions,
    string Platform,
    MuxPolicy Policy,
    InputVideoProbeResult ReplacementProbe);

public enum AddTrackStage
{
    RequestValidation,
    SourcePreparation,
    FinalAssembly,
    Complete
}

public sealed record AddTrackResult(
    ConverterError Error,
    AddTrackStage Stage,
    string? Message)
{
    public bool Success => Error == ConverterError.Ok;

    public static AddTrackResult Failed(ConverterError error, AddTrackStage stage, string message) =>
        new(error, stage, message);

    public static AddTrackResult Completed() =>
        new(ConverterError.Ok, AddTrackStage.Complete, null);
}

public static class AddTrackContract
{
    public static AddTrackResult Validate(AddTrackRequest? request)
    {
        if (request == null)
            return AddTrackResult.Failed(ConverterError.InvalidOptions, AddTrackStage.RequestValidation, "Add Track request is missing.");
        if (string.IsNullOrWhiteSpace(request.SourcePath) ||
            string.IsNullOrWhiteSpace(request.ReplacementVideoPath) ||
            string.IsNullOrWhiteSpace(request.OutputPath) ||
            string.IsNullOrWhiteSpace(request.OutputDirectory))
            return AddTrackResult.Failed(ConverterError.InvalidOptions, AddTrackStage.RequestValidation, "Add Track request contains an empty path.");
        if (!Directory.Exists(request.OutputDirectory))
            return AddTrackResult.Failed(ConverterError.HomeDirNotFound, AddTrackStage.RequestValidation, "Add Track output directory is unavailable.");
        if (!File.Exists(request.SourcePath))
            return AddTrackResult.Failed(ConverterError.InputNotFound, AddTrackStage.RequestValidation, "Add Track source file was not found.");
        if (!File.Exists(request.ReplacementVideoPath))
            return AddTrackResult.Failed(ConverterError.InputNotFound, AddTrackStage.RequestValidation, "Replacement video file was not found.");
        if (!request.ReplacementProbe.Success)
        {
            ConverterError probeError = request.ReplacementProbe.Failure switch
            {
                InputVideoProbeFailure.InputNotFound => ConverterError.InputNotFound,
                InputVideoProbeFailure.InputNotReadable => ConverterError.InputNotReadable,
                _ => ConverterError.FfprobeFailed
            };
            return AddTrackResult.Failed(
                probeError,
                AddTrackStage.RequestValidation,
                request.ReplacementProbe.Detail ?? "Replacement video probe failed.");
        }
        if (request.ReplacementProbe.VideoInfo == null ||
            MuxPolicyResolver.ParseVideoCodec(request.ReplacementProbe.VideoInfo.CodecName) != request.ReplacementProbe.Codec)
            return AddTrackResult.Failed(ConverterError.FfprobeFailed, AddTrackStage.RequestValidation, "Replacement video probe result is inconsistent.");

        if (request.Policy is null || !request.Policy.RequiresAddTrack ||
            !request.Policy.RequiresVideoReplacement ||
            request.Policy.Workflow is not (AudioWorkflowContext.Mux or AudioWorkflowContext.Hq))
            return AddTrackResult.Failed(ConverterError.InvalidOptions, AddTrackStage.RequestValidation, "Add Track policy does not require replacement assembly.");

        PolicyResult expectedPolicy;
        if (request.Policy.Workflow == AudioWorkflowContext.Hq)
        {
            if (AudioModePolicy.GetWorkflow(request.SourceOptions) != AudioWorkflowContext.Hq)
                return AddTrackResult.Failed(ConverterError.InvalidOptions, AddTrackStage.RequestValidation, "HQ Add Track request does not contain HQ source options.");
            string? audioError = AudioModePolicy.Validate(
                request.SourceOptions, request.Platform, replacementVideoCodec: request.ReplacementProbe.Codec);
            if (!string.IsNullOrEmpty(audioError))
                return AddTrackResult.Failed(ConverterError.InvalidOptions, AddTrackStage.RequestValidation, audioError);
            expectedPolicy = AudioModePolicy.ResolveHqPolicy(
                request.SourceOptions, request.Platform, request.ReplacementProbe.Codec);
        }
        else
        {
            if (AudioModePolicy.GetWorkflow(request.MuxOptions) != AudioWorkflowContext.Mux)
                return AddTrackResult.Failed(ConverterError.InvalidOptions, AddTrackStage.RequestValidation, "Mux Add Track request does not contain Mux options.");
            MuxPolicy? muxPolicy = AudioModePolicy.ResolveMuxPolicy(
                request.MuxOptions, request.Platform, MuxVideoCodec.Unknown,
                request.ReplacementProbe.Codec, out string policyError);
            expectedPolicy = muxPolicy == null
                ? PolicyResult.Failed(PolicyError.UnsupportedReplacementVideo, policyError)
                : PolicyResult.Succeeded(muxPolicy);
        }

        if (!expectedPolicy.Success || expectedPolicy.Policy != request.Policy)
        {
            string message = !expectedPolicy.Success
                ? expectedPolicy.ErrorMessage
                : "Add Track policy does not match the request options and replacement codec.";
            return AddTrackResult.Failed(ConverterError.InvalidOptions, AddTrackStage.RequestValidation, message);
        }

        return AddTrackResult.Completed();
    }
}
