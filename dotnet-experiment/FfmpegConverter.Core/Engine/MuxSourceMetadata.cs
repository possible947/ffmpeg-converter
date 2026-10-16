using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace FfmpegConverter.Core.Engine;

public sealed record MuxSourceMetadata(string SegmentTitle, string VideoTitle, string VideoLanguage);

public sealed record MuxSourceMetadataResult(MuxSourceMetadata? Metadata, string? Error)
{
    public bool Success => Metadata != null && Error == null;
}

public static class MuxSourceMetadataProbe
{
    public static MuxSourceMetadataResult ParseProbeJson(string json)
    {
        try
        {
            using JsonDocument document = JsonDocument.Parse(json);
            if (!document.RootElement.TryGetProperty("streams", out JsonElement streams) ||
                streams.ValueKind != JsonValueKind.Array ||
                streams.GetArrayLength() == 0)
                return Failed("ffprobe source metadata did not contain the source video stream.");

            string segmentTitle = GetFormatTag(document.RootElement, "title") ?? string.Empty;
            JsonElement video = streams[0];
            string videoTitle = GetStreamTag(video, "title") ?? string.Empty;
            string videoLanguage = GetStreamTag(video, "language") ?? string.Empty;
            if (string.IsNullOrWhiteSpace(videoLanguage))
                videoLanguage = "und";

            return new MuxSourceMetadataResult(
                new MuxSourceMetadata(segmentTitle, videoTitle, videoLanguage),
                null);
        }
        catch (JsonException ex)
        {
            return Failed($"ffprobe returned invalid source metadata JSON: {ex.Message}");
        }
    }

    public static async Task<MuxSourceMetadataResult> ProbeAsync(
        string ffprobePath,
        string inputPath,
        CancellationToken cancellationToken = default)
    {
        if (string.IsNullOrEmpty(ffprobePath) || !File.Exists(ffprobePath))
            return Failed("ffprobe binary not found.");
        if (string.IsNullOrEmpty(inputPath) || !File.Exists(inputPath))
            return Failed("Source preparation intermediate was not found while probing metadata.");

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
            "-v", "error", "-select_streams", "v:0", "-show_entries",
            "format_tags=title:stream_tags=title,language", "-of", "json", inputPath
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
                    ? "ffprobe failed while probing source metadata."
                    : $"ffprobe failed while probing source metadata: {error.Trim()}");

            return ParseProbeJson(output);
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        catch (Exception ex) when (ex is Win32Exception or InvalidOperationException or IOException or UnauthorizedAccessException)
        {
            return Failed($"Could not probe source metadata: {ex.Message}");
        }
    }

    private static string? GetFormatTag(JsonElement root, string key)
    {
        if (!root.TryGetProperty("format", out JsonElement format))
            return null;
        if (!format.TryGetProperty("tags", out JsonElement tags) ||
            tags.ValueKind != JsonValueKind.Object ||
            !tags.TryGetProperty(key, out JsonElement value) ||
            value.ValueKind != JsonValueKind.String)
            return null;
        return value.GetString();
    }

    private static string? GetStreamTag(JsonElement stream, string key)
    {
        if (!stream.TryGetProperty("tags", out JsonElement tags) ||
            tags.ValueKind != JsonValueKind.Object ||
            !tags.TryGetProperty(key, out JsonElement value) ||
            value.ValueKind != JsonValueKind.String)
            return null;
        return value.GetString();
    }

    private static MuxSourceMetadataResult Failed(string error) => new(null, error);
}
