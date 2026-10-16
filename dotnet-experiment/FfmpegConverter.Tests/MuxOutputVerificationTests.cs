using System.Linq;
using FfmpegConverter.Core.Engine;
using FfmpegConverter.Core.Policy;
using Xunit;

namespace FfmpegConverter.Tests;

public class MuxOutputVerificationTests
{
    private static readonly MuxBuildProfile Profile =
        new(MuxPlatform.Linux, MuxFfmpegVersion.V8_1);

    [Fact]
    public void ParseJson_ReadsContainerStreamsChaptersAndDispositions()
    {
        MuxOutputProbeResult result = MuxOutputVerification.ParseJson(
            """
            {
              "streams": [
                {
                  "codec_type": "subtitle",
                  "codec_name": "mov_text",
                  "tags": { "language": "eng" },
                  "disposition": { "default": 1, "forced": 1 }
                }
              ],
              "chapters": [{}, {}],
              "format": { "format_name": "mov,mp4,m4a" }
            }
            """);

        Assert.True(result.Success, result.Error);
        Assert.Equal("mov,mp4,m4a", result.Snapshot!.FormatName);
        Assert.Equal(2, result.Snapshot.ChapterCount);
        Assert.Equal("eng", result.Snapshot.Streams[0].Language);
        Assert.True(result.Snapshot.Streams[0].IsDefault);
        Assert.True(result.Snapshot.Streams[0].IsForced);
    }

    [Fact]
    public void ParseJson_AllowsMissingCodecNameForMatroskaAttachments()
    {
        MuxOutputProbeResult result = MuxOutputVerification.ParseJson(
            """
            {
              "streams": [
                { "codec_type": "attachment", "tags": { "filename": "font.ttf" } }
              ],
              "chapters": [],
              "format": { "format_name": "matroska,webm" }
            }
            """);

        Assert.True(result.Success, result.Error);
        Assert.Equal("attachment", result.Snapshot!.Streams[0].CodecType);
        Assert.Empty(result.Snapshot.Streams[0].CodecName);
    }

    [Fact]
    public void VerifyAssembly_PreservesSourceStreamsAndExcludesReplacementAuxiliaries()
    {
        MuxOutputSnapshot source = new(
            "matroska,webm",
            [
                new("video", "hevc", null, true, false),
                new("audio", "pcm_s24le", "eng", true, false),
                new("subtitle", "ass", "jpn", false, true),
                new("attachment", "ttf", null, false, false)
            ],
            2);
        MuxOutputSnapshot replacement = new(
            "matroska,webm",
            [
                new("video", "h264", null, true, false),
                new("audio", "aac", null, true, false),
                new("subtitle", "subrip", "fra", false, false)
            ],
            0);
        MuxOutputSnapshot assembled = new(
            "matroska,webm",
            [
                new("video", "h264", null, true, false),
                new("audio", "pcm_s24le", "eng", true, false),
                new("subtitle", "ass", "jpn", false, true),
                new("attachment", "ttf", null, false, false)
            ],
            2);

        Assert.True(MuxOutputVerification.VerifyAssembly(source, replacement, assembled).Success);

        MuxOutputSnapshot leakedReplacementSubtitle = assembled with
        {
            Streams = assembled.Streams.Append(
                new MuxOutputStream("subtitle", "subrip", "fra", false, false)).ToArray()
        };
        MuxOutputVerificationResult result = MuxOutputVerification.VerifyAssembly(
            source, replacement, leakedReplacementSubtitle);
        Assert.False(result.Success);
        Assert.Contains("subtitle streams count", result.Error);
    }

    [Fact]
    public void VerifyFinalM4v_RequiresExpectedAudioSubtitlesChaptersAndDropsAttachments()
    {
        PolicyResult policyResult = MuxPolicyResolver.Resolve(
            Profile, MuxOperationalMode.M4v, AudioOutputMode.Aac,
            MuxVideoCodec.Unknown, MuxVideoCodec.H264);
        Assert.True(policyResult.Success);
        MuxOutputSnapshot assembled = new(
            "matroska,webm",
            [
                new("video", "h264", null, true, false),
                new("audio", "pcm_s24le", "eng", true, false),
                new("subtitle", "subrip", "eng", false, true),
                new("attachment", "ttf", null, false, false)
            ],
            1);
        MuxOutputSnapshot replacement = new(
            "matroska,webm", [new("video", "h264", null, true, false)], 0);
        MuxOutputSnapshot final = new(
            "mov,mp4,m4a,3gp,3g2,mj2",
            [
                new("video", "h264", null, true, false),
                new("audio", "aac", "und", true, false),
                new("subtitle", "mov_text", "und", true, true),
                new("data", "bin_data", "eng", false, false)
            ],
            1);

        Assert.True(MuxOutputVerification.VerifyFinalOutput(
            assembled, replacement, final, policyResult.Policy!).Success);

        MuxOutputSnapshot wrongDisposition = final with
        {
            Streams = final.Streams.Select(stream => stream.CodecType == "audio"
                ? stream with { IsDefault = false }
                : stream).ToArray()
        };
        MuxOutputVerificationResult result = MuxOutputVerification.VerifyFinalOutput(
            assembled, replacement, wrongDisposition, policyResult.Policy!);
        Assert.False(result.Success);
        Assert.Contains("default/forced", result.Error);

        MuxOutputSnapshot unsupportedData = final with
        {
            Streams = [.. final.Streams, new("data", "scte_35", null, false, false)]
        };
        result = MuxOutputVerification.VerifyFinalOutput(
            assembled, replacement, unsupportedData, policyResult.Policy!);
        Assert.False(result.Success);
        Assert.Contains("data/scte_35", result.Error);
    }

    [Fact]
    public void VerifyFinalM4v_DualAudioRequiresDefaultAacAndNonDefaultAc3()
    {
        PolicyResult policyResult = MuxPolicyResolver.Resolve(
            Profile, MuxOperationalMode.M4v, AudioOutputMode.AacAc3,
            MuxVideoCodec.Unknown, MuxVideoCodec.Hevc);
        Assert.True(policyResult.Success);
        MuxOutputSnapshot assembled = new(
            "matroska,webm",
            [
                new("video", "hevc", null, true, false),
                new("audio", "pcm_s24le", "eng", true, false)
            ],
            0);
        MuxOutputSnapshot replacement = new(
            "matroska,webm", [new("video", "hevc", null, true, false)], 0);
        MuxOutputSnapshot final = new(
            "mov,mp4,m4a,3gp,3g2,mj2",
            [
                new("video", "hevc", null, true, false),
                new("audio", "aac", "und", true, false),
                new("audio", "ac3", "und", false, false)
            ],
            0);

        Assert.True(MuxOutputVerification.VerifyFinalOutput(
            assembled, replacement, final, policyResult.Policy!).Success);

        MuxOutputSnapshot bothDefault = final with
        {
            Streams = final.Streams.Select(stream => stream.CodecType == "audio"
                ? stream with { IsDefault = true }
                : stream).ToArray()
        };
        MuxOutputVerificationResult result = MuxOutputVerification.VerifyFinalOutput(
            assembled, replacement, bothDefault, policyResult.Policy!);
        Assert.False(result.Success);
        Assert.Contains("default/forced", result.Error);
    }

    [Fact]
    public void VerifyFinalMov_ConvertsTextAndRejectsUnexpectedAttachmentOrReplacementStream()
    {
        PolicyResult policyResult = MuxPolicyResolver.Resolve(
            Profile, MuxOperationalMode.Mov, AudioOutputMode.Pcm,
            MuxVideoCodec.Unknown, MuxVideoCodec.H264);
        Assert.True(policyResult.Success);
        MuxOutputSnapshot assembled = new(
            "matroska,webm",
            [
                new("video", "h264", null, true, false),
                new("audio", "pcm_s24le", "eng", true, false),
                new("subtitle", "ass", "eng", false, true),
                new("attachment", "ttf", null, false, false)
            ],
            0);
        MuxOutputSnapshot replacement = new(
            "matroska,webm", [new("video", "h264", null, true, false)], 0);
        MuxOutputSnapshot final = new(
            "mov,mp4,m4a,3gp,3g2,mj2",
            [
                new("video", "h264", null, true, false),
                new("audio", "pcm_s24le", "eng", true, false),
                new("subtitle", "mov_text", "eng", true, true),
                new("data", "bin_data", "eng", false, false)
            ],
            0);

        Assert.True(MuxOutputVerification.VerifyFinalOutput(
            assembled, replacement, final, policyResult.Policy!).Success);

        MuxOutputSnapshot unexpectedAttachment = final with
        {
            Streams = final.Streams.Append(
                new MuxOutputStream("attachment", "ttf", null, false, false)).ToArray()
        };
        MuxOutputVerificationResult result = MuxOutputVerification.VerifyFinalOutput(
            assembled, replacement, unexpectedAttachment, policyResult.Policy!);
        Assert.False(result.Success);
        Assert.Contains("attachment", result.Error);
    }

    [Fact]
    public void VerifyFinalMov_AcceptsMuxerDefaultForFirstAudioWhenSourceHasNoDefault()
    {
        PolicyResult policyResult = MuxPolicyResolver.Resolve(
            Profile, MuxOperationalMode.Mov, AudioOutputMode.Pcm,
            MuxVideoCodec.Unknown, MuxVideoCodec.H264);
        Assert.True(policyResult.Success);
        MuxOutputSnapshot assembled = new(
            "matroska,webm",
            [
                new("video", "h264", null, true, false),
                new("audio", "pcm_s16le", "eng", false, false)
            ],
            0);
        MuxOutputSnapshot replacement = new(
            "matroska,webm", [new("video", "h264", null, true, false)], 0);
        MuxOutputSnapshot final = new(
            "mov,mp4,m4a,3gp,3g2,mj2",
            [
                new("video", "h264", null, true, false),
                new("audio", "pcm_s16le", "eng", true, false)
            ],
            0);

        MuxOutputVerificationResult result = MuxOutputVerification.VerifyFinalOutput(
            assembled, replacement, final, policyResult.Policy!);

        Assert.True(result.Success, result.Error);

        MuxOutputSnapshot missingMuxerDefault = final with
        {
            Streams = final.Streams.Select(stream => stream.CodecType == "audio"
                ? stream with { IsDefault = false }
                : stream).ToArray()
        };
        result = MuxOutputVerification.VerifyFinalOutput(
            assembled, replacement, missingMuxerDefault, policyResult.Policy!);

        Assert.False(result.Success);
        Assert.Contains("default/forced", result.Error);
    }

    [Fact]
    public void ParseJson_FailsClosedWhenContainerMetadataIsMissing()
    {
        MuxOutputProbeResult result = MuxOutputVerification.ParseJson(
            """{ "streams": [], "chapters": [] }""");

        Assert.False(result.Success);
        Assert.Contains("openable container", result.Error);
    }
}
