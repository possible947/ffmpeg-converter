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
    bool PreserveChapters,
    bool PreserveAttachments,
    IReadOnlyList<SubtitleStreamMapping> SubtitleStreams)
{
    public string BuildFfmpegArguments()
    {
        var arguments = new System.Text.StringBuilder();
        foreach (SubtitleStreamMapping subtitle in SubtitleStreams)
            arguments.Append($"-map 0:{subtitle.StreamIndex} -c:s:{subtitle.OutputIndex} {subtitle.Encoder} ");
        if (PreserveChapters)
            arguments.Append("-map_chapters 0 ");
        if (PreserveAttachments)
            arguments.Append("-map 0:t? ");
        return arguments.ToString();
    }
}

public sealed record MuxSourcePreservationResult(
    MuxSourcePreservationPlan? Plan,
    IReadOnlyList<string> Warnings,
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
        try
        {
            using JsonDocument document = JsonDocument.Parse(json);
            if (!document.RootElement.TryGetProperty("streams", out JsonElement streams) ||
                streams.ValueKind != JsonValueKind.Array)
                return Failed("ffprobe subtitle output did not contain a streams array.");

            var mappings = new List<SubtitleStreamMapping>();
            var warnings = new List<string>();
            foreach (JsonElement stream in streams.EnumerateArray())
            {
                if (!stream.TryGetProperty("index", out JsonElement indexElement) ||
                    !indexElement.TryGetInt32(out int index) || index < 0)
                    return Failed("ffprobe returned a subtitle stream without a valid index.");

                string? codec = stream.TryGetProperty("codec_name", out JsonElement codecElement) &&
                                codecElement.ValueKind == JsonValueKind.String
                    ? codecElement.GetString()?.ToLowerInvariant()
                    : null;
                SubtitlePolicy subtitlePolicy = MuxPolicyResolver.ResolveSubtitle(
                    codec, policy.Container);

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
                    policy.ChapterAction == StreamAction.Preserve,
                    policy.AttachmentAction == StreamAction.Preserve,
                    mappings),
                warnings,
                null);
        }
        catch (JsonException ex)
        {
            return Failed($"ffprobe returned invalid subtitle JSON: {ex.Message}");
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
            return Failed("Add Track source file was not found while probing subtitles.");

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
            "-v", "error", "-select_streams", "s", "-show_entries",
            "stream=index,codec_name", "-of", "json", inputPath
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
                    ? "ffprobe failed while probing source subtitles."
                    : $"ffprobe failed while probing source subtitles: {error.Trim()}");

            return ParseProbeJson(output, policy);
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        catch (Exception ex) when (ex is Win32Exception or InvalidOperationException or IOException or UnauthorizedAccessException)
        {
            return Failed($"Could not probe source subtitles: {ex.Message}");
        }
    }

    private static MuxSourcePreservationResult Failed(string error) =>
        new(null, Array.Empty<string>(), error);
}
