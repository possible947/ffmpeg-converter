using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using FfmpegConverter.Core.Policy;

namespace FfmpegConverter.Core.Engine;

public sealed record SubtitleStreamMapping(int StreamIndex, int OutputIndex, string Encoder);

public sealed record MuxSourcePreservationPlan(
    MuxContainer Container,
    AudioTrackSelectionPolicy AudioTrackPolicy,
    bool RequiresVideoReplacement,
    StreamAction VideoAction,
    StreamAction AudioAction,
    StreamAction SubtitleAction,
    StreamAction ChapterAction,
    StreamAction MetadataAction,
    StreamAction StreamMetadataAction,
    StreamAction LanguageAction,
    StreamAction DispositionAction,
    StreamAction AttachmentAction,
    IReadOnlyList<SubtitleStreamMapping> SubtitleStreams)
{
    public bool PreserveVideo => VideoAction == StreamAction.Preserve;
    public bool PreserveAudio => AudioAction == StreamAction.Preserve;
    public bool PreserveChapters => ChapterAction == StreamAction.Preserve;
    public bool PreserveMetadata => MetadataAction == StreamAction.Preserve;
    public bool PreserveStreamMetadata => StreamMetadataAction == StreamAction.Preserve;
    public bool PreserveLanguage => LanguageAction == StreamAction.Preserve;
    public bool PreserveDisposition => DispositionAction == StreamAction.Preserve;
    public bool PreserveAttachments => AttachmentAction == StreamAction.Preserve;

    public string BuildFfmpegArguments(int inputIndex = 0)
    {
        var arguments = new System.Text.StringBuilder();
        if (SubtitleAction != StreamAction.Drop)
        {
            foreach (SubtitleStreamMapping subtitle in SubtitleStreams)
                arguments.Append($"-map {inputIndex}:{subtitle.StreamIndex} -c:s:{subtitle.OutputIndex} {subtitle.Encoder} ");
        }
        if (PreserveChapters)
            arguments.Append($"-map_chapters {inputIndex} ");
        if (PreserveAttachments)
            arguments.Append($"-map {inputIndex}:t? ");
        return arguments.ToString();
    }
}

public sealed record MuxSourcePreservationResult(
    MuxSourcePreservationPlan? Plan,
    IReadOnlyList<string> Warnings,
    bool HasAttachments,
    string? Error)
{
    public bool Success => Plan != null && Error == null;
}

public static class MuxSourcePreservation
{
    public static MuxSourcePreservationResult ParseProbeJson(
        string json,
        MuxPolicy policy)
    {
        if (policy.VideoAction == StreamAction.Reject ||
            policy.AudioAction == StreamAction.Reject ||
            policy.SubtitleAction == StreamAction.Reject ||
            policy.ChapterAction == StreamAction.Reject ||
            policy.MetadataAction == StreamAction.Reject ||
            policy.LanguageAction == StreamAction.Reject ||
            policy.DispositionAction == StreamAction.Reject ||
            policy.AttachmentAction == StreamAction.Reject)
            return Failed("The mux preservation policy rejects one or more source stream types.");

        try
        {
            using JsonDocument document = JsonDocument.Parse(json);
            if (!document.RootElement.TryGetProperty("streams", out JsonElement streams) ||
                streams.ValueKind != JsonValueKind.Array)
                return Failed("ffprobe source-stream output did not contain a streams array.");

            var mappings = new List<SubtitleStreamMapping>();
            var warnings = new List<string>();
            bool hasAttachments = false;
            foreach (JsonElement stream in streams.EnumerateArray())
            {
                string? streamType = stream.TryGetProperty("codec_type", out JsonElement typeElement) &&
                                     typeElement.ValueKind == JsonValueKind.String
                    ? typeElement.GetString()
                    : null;
                if (string.Equals(streamType, "attachment", StringComparison.OrdinalIgnoreCase))
                    hasAttachments = true;
                if (streamType != null &&
                    !string.Equals(streamType, "subtitle", StringComparison.OrdinalIgnoreCase))
                    continue;

                if (!stream.TryGetProperty("index", out JsonElement indexElement) ||
                    !indexElement.TryGetInt32(out int index) || index < 0)
                    return Failed("ffprobe returned a stream without a valid index.");

                string? codec = stream.TryGetProperty("codec_name", out JsonElement codecElement) &&
                                codecElement.ValueKind == JsonValueKind.String
                    ? codecElement.GetString()?.ToLowerInvariant()
                    : null;
                SubtitlePolicy subtitlePolicy = policy.SubtitleAction != StreamAction.Drop &&
                                                policy.PreserveSubtitlesWhenSupported
                    ? MuxPolicyResolver.ResolveSubtitle(codec, policy.Container)
                    : new SubtitlePolicy(SubtitleAction.Drop, null);

                if (subtitlePolicy.Action != SubtitleAction.Copy)
                {
                    string action = subtitlePolicy.Action == SubtitleAction.Drop
                        ? "dropped (unsupported)"
                        : "converted";
                    warnings.Add(
                        $"Warning: subtitle stream {index} ({codec ?? "unknown"}): {action} for {policy.Container}" +
                        (subtitlePolicy.Encoder == null ? "." : $" using {subtitlePolicy.Encoder}."));
                }

                if (subtitlePolicy.Action == SubtitleAction.Drop)
                    continue;

                mappings.Add(new SubtitleStreamMapping(
                    index, mappings.Count, subtitlePolicy.Encoder!));
            }

            return new MuxSourcePreservationResult(
                new MuxSourcePreservationPlan(
                    policy.Container,
                    policy.AudioTrackPolicy,
                    policy.RequiresVideoReplacement,
                    policy.VideoAction,
                    policy.AudioAction,
                    policy.SubtitleAction,
                    policy.ChapterAction,
                    policy.MetadataAction,
                    policy.MetadataAction,
                    policy.LanguageAction,
                    policy.DispositionAction,
                    policy.AttachmentAction,
                    mappings),
                warnings,
                hasAttachments,
                null);
        }
        catch (JsonException ex)
        {
            return Failed($"ffprobe returned invalid source-stream JSON: {ex.Message}");
        }
    }

    public static async Task<MuxSourcePreservationResult> ProbeAsync(
        string ffprobePath,
        string inputPath,
        MuxPolicy policy,
        CancellationToken cancellationToken = default)
    {
        if (string.IsNullOrEmpty(ffprobePath) || !File.Exists(ffprobePath))
            return Failed("ffprobe binary not found.");
        if (string.IsNullOrEmpty(inputPath) || !File.Exists(inputPath))
            return Failed("Source file was not found while probing source streams.");

        var startInfo = new ProcessStartInfo
        {
            FileName = ffprobePath,
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true
        };
        foreach (string argument in new[]
        {
            "-v", "error", "-show_entries",
            "stream=index,codec_name,codec_type", "-of", "json", inputPath
        })
            startInfo.ArgumentList.Add(argument);

        using var process = new Process { StartInfo = startInfo };
        try
        {
            if (!process.Start())
                return Failed("ffprobe process did not start.");

            Task<string> outputTask = process.StandardOutput.ReadToEndAsync();
            Task<string> errorTask = process.StandardError.ReadToEndAsync();
            try
            {
                await process.WaitForExitAsync(cancellationToken);
            }
            catch (OperationCanceledException)
            {
                if (!process.HasExited)
                {
                    try
                    {
                        process.Kill(entireProcessTree: true);
                    }
                    catch (InvalidOperationException) { }
                    catch (Win32Exception) { }
                }
                await process.WaitForExitAsync(CancellationToken.None);
                throw;
            }

            string output = await outputTask;
            string error = await errorTask;
            if (process.ExitCode != 0)
                return Failed(string.IsNullOrWhiteSpace(error)
                    ? "ffprobe failed while probing source streams."
                    : $"ffprobe failed while probing source streams: {error.Trim()}");

            return ParseProbeJson(output, policy);
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        catch (Exception ex) when (ex is Win32Exception or InvalidOperationException or IOException or UnauthorizedAccessException)
        {
            return Failed($"Could not probe source streams: {ex.Message}");
        }
    }

    private static MuxSourcePreservationResult Failed(string error) =>
        new(null, Array.Empty<string>(), false, error);
}
