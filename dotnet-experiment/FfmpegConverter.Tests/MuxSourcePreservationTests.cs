using System.Collections.Generic;
using System.Linq;
using FfmpegConverter.Core.Engine;
using FfmpegConverter.Core.Policy;
using Xunit;

namespace FfmpegConverter.Tests;

public class MuxSourcePreservationTests
{
    [Fact]
    public void ParseProbeJson_PreservesSupportedMkvSubtitlesAndReportsUnsupported()
    {
        PolicyResult policyResult = AudioModePolicy.ResolveSourcePreparationPolicy(
            "linux", AudioOutputMode.Copy);
        Assert.True(policyResult.Success);

        MuxSourcePreservationResult result = MuxSourcePreservation.ParseProbeJson(
            """
            {
              "streams": [
                { "index": 2, "codec_type": "subtitle", "codec_name": "subrip" },
                { "index": 4, "codec_type": "subtitle", "codec_name": "mov_text" },
                { "index": 5, "codec_type": "subtitle", "codec_name": "hdmv_pgs_subtitle" },
                { "index": 6, "codec_type": "subtitle", "codec_name": "unknown_subtitle" },
                { "index": 7, "codec_type": "attachment", "codec_name": "ttf" }
              ]
            }
            """,
            policyResult.Policy!);

        Assert.True(result.Success);
        Assert.Equal(
            new[]
            {
                new SubtitleStreamMapping(2, 0, "copy"),
                new SubtitleStreamMapping(4, 1, "srt"),
                new SubtitleStreamMapping(5, 2, "copy")
            },
            result.Plan!.SubtitleStreams);
        Assert.True(result.Plan.PreserveChapters);
        Assert.True(result.Plan.PreserveMetadata);
        Assert.True(result.Plan.PreserveLanguage);
        Assert.True(result.Plan.PreserveDisposition);
        Assert.True(result.Plan.PreserveAttachments);
        Assert.True(result.HasAttachments);
        Assert.Equal(2, result.Warnings.Count);
        Assert.Contains("-map 0:2 -c:s:0 copy", result.Plan.BuildFfmpegArguments());
        Assert.Contains("-map 0:4 -c:s:1 srt", result.Plan.BuildFfmpegArguments());
        Assert.Contains("-map_chapters 0", result.Plan.BuildFfmpegArguments());
        Assert.Contains("-map 0:t?", result.Plan.BuildFfmpegArguments());
    }

    [Fact]
    public void ParseProbeJson_NoSubtitlesStillPreservesChaptersAndAttachments()
    {
        PolicyResult policyResult = AudioModePolicy.ResolveSourcePreparationPolicy(
            "linux", AudioOutputMode.Aac);
        Assert.True(policyResult.Success);

        MuxSourcePreservationResult result = MuxSourcePreservation.ParseProbeJson(
            """{ "streams": [] }""",
            policyResult.Policy!);

        Assert.True(result.Success);
        Assert.Empty(result.Plan!.SubtitleStreams);
        Assert.Equal("-map_chapters 0 -map 0:t? ", result.Plan.BuildFfmpegArguments());
    }

    [Fact]
    public void ParseProbeJson_FailsClosedForMalformedStreamIndices()
    {
        PolicyResult policyResult = AudioModePolicy.ResolveSourcePreparationPolicy(
            "linux", AudioOutputMode.Pcm);
        Assert.True(policyResult.Success);

        MuxSourcePreservationResult result = MuxSourcePreservation.ParseProbeJson(
            """{ "streams": [{ "codec_name": "subrip" }] }""",
            policyResult.Policy!);

        Assert.False(result.Success);
        Assert.Contains("valid index", result.Error);
    }

    [Fact]
    public void ParseProbeJson_RejectsMalformedJson()
    {
        PolicyResult policyResult = AudioModePolicy.ResolveSourcePreparationPolicy(
            "linux", AudioOutputMode.Pcm);
        Assert.True(policyResult.Success);

        MuxSourcePreservationResult result = MuxSourcePreservation.ParseProbeJson(
            "{", policyResult.Policy!);

        Assert.False(result.Success);
        Assert.Contains("invalid source-stream JSON", result.Error);
    }

    [Fact]
    public void MovRemuxArguments_ConvertTextSubtitlesAndDropAttachments()
    {
        var profile = new MuxBuildProfile(MuxPlatform.Linux, MuxFfmpegVersion.V8_1);
        PolicyResult policyResult = MuxPolicyResolver.Resolve(
            profile, MuxOperationalMode.Mov, AudioOutputMode.Pcm,
            MuxVideoCodec.Unknown, MuxVideoCodec.H264);
        Assert.True(policyResult.Success);
        MuxSourcePreservationResult preservation = MuxSourcePreservation.ParseProbeJson(
            """
            {
              "streams": [
                { "index": 2, "codec_type": "subtitle", "codec_name": "subrip" },
                { "index": 3, "codec_type": "subtitle", "codec_name": "mov_text" },
                { "index": 4, "codec_type": "subtitle", "codec_name": "hdmv_pgs_subtitle" },
                { "index": 5, "codec_type": "attachment", "codec_name": "ttf" }
              ]
            }
            """,
            policyResult.Policy!);

        IReadOnlyList<string> arguments = MuxPipeline.BuildMovRemuxArguments(
            "/input with spaces/source.mkv",
            "/output with spaces/final.mov",
            preservation.Plan!);

        Assert.Equal(new[]
        {
            new SubtitleStreamMapping(2, 0, "mov_text"),
            new SubtitleStreamMapping(3, 1, "copy")
        }, preservation.Plan!.SubtitleStreams);
        Assert.True(preservation.HasAttachments);
        Assert.False(preservation.Plan.PreserveAttachments);
        Assert.Contains("-map", arguments);
        Assert.Contains("0:2", arguments);
        Assert.Contains("0:3", arguments);
        Assert.Contains("mov_text", arguments);
        Assert.Contains("copy", arguments);
        Assert.Contains("-map_metadata", arguments);
        Assert.Contains("-map_chapters", arguments);
        Assert.Contains("-movflags", arguments);
        Assert.DoesNotContain("-map 0:t?", string.Join(" ", arguments));
        Assert.Equal("/output with spaces/final.mov", arguments[^1]);
    }

    [Theory]
    [InlineData(MuxOperationalMode.Mkv, AudioOutputMode.Pcm, AudioTrackSelectionPolicy.First, StreamAction.Preserve)]
    [InlineData(MuxOperationalMode.Mov, AudioOutputMode.Pcm, AudioTrackSelectionPolicy.First, StreamAction.Drop)]
    [InlineData(MuxOperationalMode.M4v, AudioOutputMode.Aac, AudioTrackSelectionPolicy.First, StreamAction.Drop)]
    [InlineData(MuxOperationalMode.M4v, AudioOutputMode.AacAc3, AudioTrackSelectionPolicy.First, StreamAction.Drop)]
    [InlineData(MuxOperationalMode.Mkv, AudioOutputMode.Copy, AudioTrackSelectionPolicy.All, StreamAction.Preserve)]
    public void PreservationPlan_RecordsContainerStreamOwnership(
        MuxOperationalMode mode,
        AudioOutputMode audioMode,
        AudioTrackSelectionPolicy trackPolicy,
        StreamAction attachmentAction)
    {
        PolicyResult policy = MuxPolicyResolver.Resolve(
            new MuxBuildProfile(MuxPlatform.Linux, MuxFfmpegVersion.V8_1),
            mode,
            audioMode,
            MuxVideoCodec.H264,
            MuxVideoCodec.H264);
        Assert.True(policy.Success);

        MuxSourcePreservationResult result = MuxSourcePreservation.ParseProbeJson(
            """{ "streams": [] }""", policy.Policy!);

        Assert.True(result.Success);
        Assert.Equal(policy.Policy!.Container, result.Plan!.Container);
        Assert.Equal(trackPolicy, result.Plan.AudioTrackPolicy);
        Assert.True(result.Plan.RequiresVideoReplacement);
        Assert.Equal(StreamAction.Preserve, result.Plan.VideoAction);
        Assert.Equal(StreamAction.Preserve, result.Plan.SubtitleAction);
        Assert.Equal(StreamAction.Preserve, result.Plan.ChapterAction);
        Assert.Equal(StreamAction.Preserve, result.Plan.MetadataAction);
        Assert.Equal(StreamAction.Preserve, result.Plan.StreamMetadataAction);
        Assert.Equal(StreamAction.Preserve, result.Plan.LanguageAction);
        Assert.Equal(StreamAction.Preserve, result.Plan.DispositionAction);
        Assert.Equal(attachmentAction, result.Plan.AttachmentAction);
    }

    [Fact]
    public void PreservationPlan_RejectsUnsupportedPolicyBeforeExecution()
    {
        PolicyResult resolved = MuxPolicyResolver.Resolve(
            new MuxBuildProfile(MuxPlatform.Linux, MuxFfmpegVersion.V8_1),
            MuxOperationalMode.Mkv,
            AudioOutputMode.Pcm,
            MuxVideoCodec.H264,
            MuxVideoCodec.H264);
        MuxPolicy rejectPolicy = resolved.Policy! with { ChapterAction = StreamAction.Reject };

        MuxSourcePreservationResult result = MuxSourcePreservation.ParseProbeJson(
            """{ "streams": [] }""", rejectPolicy);

        Assert.False(result.Success);
        Assert.Contains("rejects", result.Error);
    }

    [Fact]
    public void MkvmergeArguments_UseOnlyReplacementVideoAndExplicitSourceMetadata()
    {
        PolicyResult sourcePolicy = AudioModePolicy.ResolveSourcePreparationPolicy(
            "linux", AudioOutputMode.Pcm);
        MuxSourcePreservationResult preservation = MuxSourcePreservation.ParseProbeJson(
            """{ "streams": [] }""", sourcePolicy.Policy!);
        var metadata = new MuxSourceMetadata(
            "; replacement metadata must not be parsed as options",
            "Source video title",
            "und");

        var arguments = MuxPipeline.BuildMkvmergeArguments(
            "/output with spaces/final.mkv",
            "/replacement with spaces/track.mkv",
            "/source with spaces/prepared.mkv",
            metadata,
            "24000/1001",
            preservation.Plan);

        Assert.Equal("--title", arguments[2]);
        Assert.Equal(metadata.SegmentTitle, arguments[3]);
        Assert.Contains("--no-audio", arguments);
        Assert.Contains("--no-subtitles", arguments);
        Assert.Contains("--no-attachments", arguments);
        Assert.Contains("--no-chapters", arguments);
        Assert.Contains("--no-global-tags", arguments);
        Assert.Contains("--no-track-tags", arguments);
        Assert.Contains("--no-buttons", arguments);
        Assert.Contains("0:24000/1001fps", arguments);
        Assert.Contains("0:und", arguments);
        Assert.Contains("0:Source video title", arguments);
        Assert.Contains("--video-tracks", arguments);
        Assert.Contains("/replacement with spaces/track.mkv", arguments);
        int replacementIndex = arguments.ToList().IndexOf("/replacement with spaces/track.mkv");
        int sourceNoVideoIndex = arguments.ToList().IndexOf("--no-video");
        Assert.True(replacementIndex > 0);
        Assert.True(sourceNoVideoIndex > replacementIndex);
        Assert.DoesNotContain(
            arguments.Skip(sourceNoVideoIndex),
            argument => argument is "--no-audio" or "--no-subtitles" or
                        "--no-attachments" or "--no-chapters" or
                        "--no-global-tags" or "--no-track-tags");
        Assert.Equal("/source with spaces/prepared.mkv", arguments[^1]);
    }

    [Fact]
    public void MuxSourceMetadataProbe_UsesSourceValuesAndDefaultsMissingLanguage()
    {
        MuxSourceMetadataResult result = MuxSourceMetadataProbe.ParseProbeJson(
            """
            {
              "format": { "tags": { "title": "Source segment" } },
              "streams": [{ "tags": { "title": "Source video" } }]
            }
            """);

        Assert.True(result.Success);
        Assert.Equal("Source segment", result.Metadata!.SegmentTitle);
        Assert.Equal("Source video", result.Metadata.VideoTitle);
        Assert.Equal("und", result.Metadata.VideoLanguage);
    }

    [Fact]
    public void MuxSourceMetadataProbe_FailsClosedWithoutSourceVideo()
    {
        MuxSourceMetadataResult result = MuxSourceMetadataProbe.ParseProbeJson(
            """{ "streams": [] }""");

        Assert.False(result.Success);
        Assert.Contains("source video stream", result.Error);
    }
}
