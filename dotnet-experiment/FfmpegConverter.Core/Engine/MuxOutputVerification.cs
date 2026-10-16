using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using FfmpegConverter.Core.Policy;

namespace FfmpegConverter.Core.Engine;

public sealed record MuxOutputStream(
    string CodecType,
    string CodecName,
    string? Language,
    bool IsDefault,
    bool IsForced);

public sealed record MuxOutputSnapshot(
    string FormatName,
    IReadOnlyList<MuxOutputStream> Streams,
    int ChapterCount);

public sealed record MuxOutputProbeResult(MuxOutputSnapshot? Snapshot, string? Error)
{
    public bool Success => Snapshot != null && Error == null;
}

public sealed record MuxOutputVerificationResult(string? Error)
{
    public bool Success => Error == null;

    public static MuxOutputVerificationResult Verified() => new((string?)null);

    public static MuxOutputVerificationResult Failed(string error) => new(error);
}

public static class MuxOutputVerification
{
    public static async Task<MuxOutputProbeResult> ProbeAsync(
        string ffprobePath,
        string inputPath,
        CancellationToken cancellationToken = default)
    {
        if (string.IsNullOrWhiteSpace(ffprobePath) || !File.Exists(ffprobePath))
            return new(null, "ffprobe binary not found.");
        if (string.IsNullOrWhiteSpace(inputPath) || !File.Exists(inputPath))
            return new(null, "Output file was not found for verification.");

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
            "-v", "error", "-show_streams", "-show_chapters", "-show_format",
            "-of", "json", inputPath
        })
            startInfo.ArgumentList.Add(argument);

        using var process = new Process { StartInfo = startInfo };
        try
        {
            if (!process.Start())
                return new(null, "ffprobe process did not start.");

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
                return new(null, string.IsNullOrWhiteSpace(error)
                    ? "ffprobe failed to open the output."
                    : $"ffprobe failed to open the output: {error.Trim()}");
            return ParseJson(output);
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        catch (Exception ex) when (ex is Win32Exception or InvalidOperationException or IOException or UnauthorizedAccessException)
        {
            return new(null, $"Could not verify output with ffprobe: {ex.Message}");
        }
    }

    public static MuxOutputProbeResult ParseJson(string json)
    {
        try
        {
            using JsonDocument document = JsonDocument.Parse(json);
            JsonElement root = document.RootElement;
            if (!root.TryGetProperty("streams", out JsonElement streamsElement) ||
                streamsElement.ValueKind != JsonValueKind.Array)
                return new(null, "ffprobe output did not contain a streams array.");
            if (!root.TryGetProperty("format", out JsonElement formatElement) ||
                formatElement.ValueKind != JsonValueKind.Object ||
                !formatElement.TryGetProperty("format_name", out JsonElement formatNameElement) ||
                formatNameElement.ValueKind != JsonValueKind.String ||
                string.IsNullOrWhiteSpace(formatNameElement.GetString()))
                return new(null, "ffprobe output did not identify an openable container.");
            if (!root.TryGetProperty("chapters", out JsonElement chaptersElement) ||
                chaptersElement.ValueKind != JsonValueKind.Array)
                return new(null, "ffprobe output did not contain a chapters array.");

            var streams = new List<MuxOutputStream>();
            foreach (JsonElement stream in streamsElement.EnumerateArray())
            {
                if (!stream.TryGetProperty("codec_type", out JsonElement typeElement) ||
                    typeElement.ValueKind != JsonValueKind.String)
                    return new(null, "ffprobe returned a stream without a codec type.");

                string codecType = typeElement.GetString()!.ToLowerInvariant();
                string codecName = stream.TryGetProperty("codec_name", out JsonElement codecElement) &&
                                   codecElement.ValueKind == JsonValueKind.String
                    ? codecElement.GetString()!.ToLowerInvariant()
                    : "";
                if (codecName.Length == 0 && codecType != "attachment")
                    return new(null, "ffprobe returned a stream without a codec name.");

                string? language = null;
                if (stream.TryGetProperty("tags", out JsonElement tagsElement) &&
                    tagsElement.ValueKind == JsonValueKind.Object &&
                    tagsElement.TryGetProperty("language", out JsonElement languageElement) &&
                    languageElement.ValueKind == JsonValueKind.String)
                    language = languageElement.GetString();

                bool isDefault = ReadDisposition(stream, "default");
                bool isForced = ReadDisposition(stream, "forced");
                streams.Add(new MuxOutputStream(
                    codecType,
                    codecName,
                    language,
                    isDefault,
                    isForced));
            }

            return new(
                new MuxOutputSnapshot(
                    formatNameElement.GetString()!.ToLowerInvariant(),
                    streams,
                    chaptersElement.GetArrayLength()),
                null);
        }
        catch (JsonException ex)
        {
            return new(null, $"ffprobe returned invalid output JSON: {ex.Message}");
        }
    }

    public static MuxOutputVerificationResult VerifyAssembly(
        MuxOutputSnapshot source,
        MuxOutputSnapshot replacement,
        MuxOutputSnapshot assembled)
    {
        MuxOutputVerificationResult formatResult = VerifyFormat(assembled, MuxContainer.Mkv);
        if (!formatResult.Success)
            return formatResult;
        MuxOutputVerificationResult videoResult = VerifyReplacementVideo(replacement, assembled);
        if (!videoResult.Success)
            return videoResult;

        foreach (string streamType in source.Streams
                     .Where(stream => stream.CodecType != "video")
                     .Select(stream => stream.CodecType)
                     .Distinct(StringComparer.Ordinal))
        {
            MuxOutputVerificationResult streamResult = CompareStreams(
                source.Streams.Where(stream => stream.CodecType == streamType).ToArray(),
                assembled.Streams.Where(stream => stream.CodecType == streamType).ToArray(),
                $"Add Track assembly {streamType} streams");
            if (!streamResult.Success)
                return streamResult;
        }

        if (assembled.Streams.Any(stream =>
                stream.CodecType != "video" &&
                !source.Streams.Any(sourceStream => sourceStream.CodecType == stream.CodecType)))
            return MuxOutputVerificationResult.Failed(
                "Add Track assembly contains auxiliary streams that were not present in the source.");
        if (assembled.ChapterCount != source.ChapterCount)
            return MuxOutputVerificationResult.Failed(
                $"Add Track assembly chapter count changed from {source.ChapterCount} to {assembled.ChapterCount}.");
        return MuxOutputVerificationResult.Verified();
    }

    public static MuxOutputVerificationResult VerifyFinalOutput(
        MuxOutputSnapshot assembled,
        MuxOutputSnapshot replacement,
        MuxOutputSnapshot output,
        MuxPolicy policy)
    {
        MuxOutputVerificationResult formatResult = VerifyFormat(output, policy.Container);
        if (!formatResult.Success)
            return formatResult;
        MuxOutputVerificationResult videoResult = VerifyReplacementVideo(replacement, output);
        if (!videoResult.Success)
            return videoResult;

        // The native Mux M4V final remux reports MP4Box language tags as "und".
        MuxOutputStream[] expectedAudio = policy.Container == MuxContainer.M4v
            ? policy.AudioMode == AudioOutputMode.AacAc3
                ? [new("audio", "aac", null, true, false), new("audio", "ac3", null, false, false)]
                : [new("audio", "aac", null, true, false)]
            : assembled.Streams.Where(stream => stream.CodecType == "audio").ToArray();
        MuxOutputVerificationResult audioResult = CompareStreams(
            expectedAudio,
            output.Streams.Where(stream => stream.CodecType == "audio").ToArray(),
            "Final audio streams");
        if (!audioResult.Success)
            return audioResult;

        MuxOutputStream[] expectedSubtitles = GetExpectedSubtitles(
            assembled.Streams.Where(stream => stream.CodecType == "subtitle"),
            policy);
        MuxOutputVerificationResult subtitleResult = CompareStreams(
            expectedSubtitles,
            output.Streams.Where(stream => stream.CodecType == "subtitle").ToArray(),
            "Final subtitle streams");
        if (!subtitleResult.Success)
            return subtitleResult;

        int expectedAttachments = policy.AttachmentAction == StreamAction.Preserve
            ? assembled.Streams.Count(stream => stream.CodecType == "attachment")
            : 0;
        MuxOutputStream[] expectedAttachmentStreams = policy.AttachmentAction == StreamAction.Preserve
            ? assembled.Streams.Where(stream => stream.CodecType == "attachment").ToArray()
            : [];
        MuxOutputVerificationResult attachmentResult = CompareStreams(
            expectedAttachmentStreams,
            output.Streams.Where(stream => stream.CodecType == "attachment").ToArray(),
            "Final attachment streams");
        if (!attachmentResult.Success || expectedAttachments != output.Streams.Count(stream => stream.CodecType == "attachment"))
            return attachmentResult.Success
                ? MuxOutputVerificationResult.Failed("Final attachment count does not match the container policy.")
                : attachmentResult;

        int expectedChapters = policy.ChapterAction == StreamAction.Preserve
            ? assembled.ChapterCount
            : 0;
        if (output.ChapterCount != expectedChapters)
            return MuxOutputVerificationResult.Failed(
                $"Final chapter count is {output.ChapterCount}; expected {expectedChapters}.");

        if (policy.Container == MuxContainer.Mkv)
        {
            foreach (string streamType in assembled.Streams
                         .Where(stream => stream.CodecType is not ("video" or "audio" or "subtitle" or "attachment"))
                         .Select(stream => stream.CodecType)
                         .Distinct(StringComparer.Ordinal))
            {
                MuxOutputVerificationResult streamResult = CompareStreams(
                    assembled.Streams.Where(stream => stream.CodecType == streamType).ToArray(),
                    output.Streams.Where(stream => stream.CodecType == streamType).ToArray(),
                    $"Final {streamType} streams");
                if (!streamResult.Success)
                    return streamResult;
            }
        }
        else
        {
            // The native MOV/M4V remux can expose preserved text tracks as bin_data.
            MuxOutputStream[] unsupportedAuxiliary = output.Streams
                .Where(stream =>
                    stream.CodecType is not ("video" or "audio" or "subtitle") &&
                    !((policy.Container is MuxContainer.Mov or MuxContainer.M4v) &&
                      stream.CodecType == "data" &&
                      stream.CodecName == "bin_data"))
                .ToArray();
            if (unsupportedAuxiliary.Length > 0)
            {
                string streamNames = string.Join(", ", unsupportedAuxiliary.Select(
                    stream => $"{stream.CodecType}/{stream.CodecName}"));
                return MuxOutputVerificationResult.Failed(
                    $"Final {policy.Container} contains unsupported auxiliary streams: {streamNames}.");
            }
        }

        return MuxOutputVerificationResult.Verified();
    }

    private static MuxOutputStream[] GetExpectedSubtitles(
        IEnumerable<MuxOutputStream> sourceSubtitles,
        MuxPolicy policy)
    {
        if (policy.SubtitleAction == StreamAction.Drop ||
            !policy.PreserveSubtitlesWhenSupported)
            return [];

        var expected = new List<MuxOutputStream>();
        foreach (MuxOutputStream subtitle in sourceSubtitles)
        {
            SubtitlePolicy subtitlePolicy = MuxPolicyResolver.ResolveSubtitle(
                subtitle.CodecName, policy.Container);
            if (subtitlePolicy.Action == SubtitleAction.Drop)
                continue;

            expected.Add(subtitle with
            {
                CodecName = subtitlePolicy.Action == SubtitleAction.Copy
                    ? subtitle.CodecName
                    : subtitlePolicy.Encoder!.ToLowerInvariant(),
                Language = policy.Container == MuxContainer.M4v
                    ? null
                    : subtitle.Language
            });
        }
        if ((policy.Container is MuxContainer.Mov or MuxContainer.M4v) &&
            expected.Count > 0 &&
            !expected.Exists(subtitle => subtitle.IsDefault))
        {
            expected[0] = expected[0] with { IsDefault = true };
        }
        return expected.ToArray();
    }

    private static MuxOutputVerificationResult VerifyReplacementVideo(
        MuxOutputSnapshot replacement,
        MuxOutputSnapshot output)
    {
        MuxOutputStream? expectedVideo = replacement.Streams
            .FirstOrDefault(stream => stream.CodecType == "video");
        MuxOutputStream[] outputVideos = output.Streams
            .Where(stream => stream.CodecType == "video")
            .ToArray();
        if (expectedVideo == null || outputVideos.Length != 1)
            return MuxOutputVerificationResult.Failed(
                $"Output has {outputVideos.Length} video streams; expected exactly one replacement video.");
        if (!string.Equals(expectedVideo.CodecName, outputVideos[0].CodecName, StringComparison.OrdinalIgnoreCase))
            return MuxOutputVerificationResult.Failed(
                $"Output video codec is {outputVideos[0].CodecName}; expected replacement codec {expectedVideo.CodecName}.");
        return MuxOutputVerificationResult.Verified();
    }

    private static MuxOutputVerificationResult VerifyFormat(
        MuxOutputSnapshot snapshot,
        MuxContainer container)
    {
        bool valid = container switch
        {
            MuxContainer.Mkv => snapshot.FormatName.Split(',').Contains("matroska", StringComparer.Ordinal),
            MuxContainer.Mov or MuxContainer.M4v =>
                snapshot.FormatName.Split(',').Any(name =>
                    name is "mov" or "mp4" or "m4a" or "3gp" or "3g2" or "mj2"),
            _ => false
        };
        return valid
            ? MuxOutputVerificationResult.Verified()
            : MuxOutputVerificationResult.Failed(
                $"Output container is {snapshot.FormatName}; expected {container}.");
    }

    private static MuxOutputVerificationResult CompareStreams(
        IReadOnlyList<MuxOutputStream> expected,
        IReadOnlyList<MuxOutputStream> actual,
        string description)
    {
        if (expected.Count != actual.Count)
            return MuxOutputVerificationResult.Failed(
                $"{description} count is {actual.Count}; expected {expected.Count}.");

        for (int index = 0; index < expected.Count; index++)
        {
            MuxOutputStream wanted = expected[index];
            MuxOutputStream found = actual[index];
            bool attachmentCodecKnown =
                wanted.CodecType != "attachment" ||
                (!string.IsNullOrEmpty(wanted.CodecName) &&
                 !string.IsNullOrEmpty(found.CodecName));
            if (attachmentCodecKnown &&
                !string.Equals(wanted.CodecName, found.CodecName, StringComparison.OrdinalIgnoreCase))
                return MuxOutputVerificationResult.Failed(
                    $"{description} codec {index} is {found.CodecName}; expected {wanted.CodecName}.");
            if (!string.IsNullOrEmpty(wanted.Language) &&
                !string.Equals(wanted.Language, found.Language, StringComparison.OrdinalIgnoreCase))
                return MuxOutputVerificationResult.Failed(
                    $"{description} language {index} is {found.Language ?? "unset"}; expected {wanted.Language}.");
            if (wanted.IsDefault != found.IsDefault || wanted.IsForced != found.IsForced)
                return MuxOutputVerificationResult.Failed(
                    $"{description} default/forced disposition {index} does not match the preservation policy.");
        }
        return MuxOutputVerificationResult.Verified();
    }

    private static bool ReadDisposition(JsonElement stream, string name) =>
        stream.TryGetProperty("disposition", out JsonElement disposition) &&
        disposition.ValueKind == JsonValueKind.Object &&
        disposition.TryGetProperty(name, out JsonElement value) &&
        value.TryGetInt32(out int flag) &&
        flag != 0;
}
