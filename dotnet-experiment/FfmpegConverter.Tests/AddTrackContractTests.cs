using System;
using System.IO;
using FfmpegConverter.Core.Engine;
using FfmpegConverter.Core.Models;
using FfmpegConverter.Core.Policy;
using Xunit;

namespace FfmpegConverter.Tests;

public class AddTrackContractTests
{
    [Fact]
    public void Validate_AcceptsRequestWhosePolicyMatchesTheProbedReplacement()
    {
        using var layout = new TemporaryLayout();
        AddTrackRequest request = CreateMuxRequest(layout, MuxOperationalMode.Mkv, MuxVideoCodec.H264);

        AddTrackResult result = AddTrackContract.Validate(request);

        Assert.True(result.Success, result.Message);
        Assert.Equal(AddTrackStage.Complete, result.Stage);
    }

    [Theory]
    [InlineData("x264", MuxVideoCodec.H264, AudioOutputMode.Pcm, MuxContainer.Mkv)]
    [InlineData("x265", MuxVideoCodec.Hevc, AudioOutputMode.Aac, MuxContainer.Mkv)]
    [InlineData("ffmpeg-prores", MuxVideoCodec.ProRes, AudioOutputMode.Pcm, MuxContainer.Mov)]
    public void Validate_AcceptsHqReplacementThroughSharedMuxContract(
        string encoder,
        MuxVideoCodec replacementCodec,
        AudioOutputMode audioMode,
        MuxContainer container)
    {
        using var layout = new TemporaryLayout();
        string source = layout.CreateFile("hq-source.mkv");
        string replacement = layout.CreateFile("hq-video.mkv");
        var sourceOptions = new ConvertOptions
        {
            Codec = "hq_converter",
            SelectionGroup = "hq_converter",
            SelectionEncoder = encoder,
            AudioOutputMode = audioMode
        };
        PolicyResult policyResult = AudioModePolicy.ResolveHqPolicy(
            sourceOptions, "macos", replacementCodec);
        Assert.True(policyResult.Success, policyResult.ErrorMessage);
        MuxPolicy hqPolicy = policyResult.Policy!;
        var muxOptions = new ConvertOptions
        {
            Codec = "mux",
            Preset = container == MuxContainer.Mov ? "mov" : "mkv",
            AudioOutputMode = hqPolicy.AudioMode,
            VideoTrackPath = replacement
        };
        var request = new AddTrackRequest(
            source, replacement,
            Path.Combine(layout.Root, $"hq-output{hqPolicy.OutputExtension}"), layout.Root,
            sourceOptions, muxOptions, "macos", hqPolicy,
            InputVideoProbeResult.FromProcessResult(
                0, $"codec_name={CodecName(replacementCodec)}\n", ""));

        AddTrackResult result = AddTrackContract.Validate(request);

        Assert.True(result.Success, result.Message);
        Assert.Equal(AudioWorkflowContext.Hq, hqPolicy.Workflow);
        Assert.Equal(container, hqPolicy.Container);
        Assert.Equal(audioMode, hqPolicy.AudioMode);
        Assert.Equal(audioMode, muxOptions.AudioOutputMode);
        Assert.True(hqPolicy.RequiresAddTrack);
        Assert.Equal(audioMode, AudioModePolicy.ResolveSourcePreparationPolicy(
            "macos", hqPolicy.AudioMode).Policy!.AudioMode);
    }

    [Fact]
    public void Validate_RejectsPolicyMismatchBeforePreparation()
    {
        using var layout = new TemporaryLayout();
        AddTrackRequest request = CreateMuxRequest(layout, MuxOperationalMode.Mkv, MuxVideoCodec.H264);
        PolicyResult movPolicy = MuxPolicyResolver.Resolve(
            new MuxBuildProfile(MuxPlatform.MacOS, MuxFfmpegVersion.V8_1),
            MuxOperationalMode.Mov, AudioOutputMode.Pcm,
            MuxVideoCodec.Unknown, MuxVideoCodec.H264);

        AddTrackResult result = AddTrackContract.Validate(request with { Policy = movPolicy.Policy! });

        Assert.False(result.Success);
        Assert.Equal(ConverterError.InvalidOptions, result.Error);
        Assert.Equal(AddTrackStage.RequestValidation, result.Stage);
    }

    [Fact]
    public void Validate_RejectsFailedReplacementProbeBeforePreparation()
    {
        using var layout = new TemporaryLayout();
        AddTrackRequest request = CreateMuxRequest(layout, MuxOperationalMode.Mkv, MuxVideoCodec.H264);

        AddTrackResult result = AddTrackContract.Validate(request with
        {
            ReplacementProbe = InputVideoProbeResult.Failed(InputVideoProbeFailure.NoVideoStream, null)
        });

        Assert.False(result.Success);
        Assert.Equal(ConverterError.FfprobeFailed, result.Error);
        Assert.Equal(AddTrackStage.RequestValidation, result.Stage);
    }

    [Fact]
    public void Validate_RequiresBothSourceAndReplacementFiles()
    {
        using var layout = new TemporaryLayout();
        AddTrackRequest request = CreateMuxRequest(layout, MuxOperationalMode.Mkv, MuxVideoCodec.H264);
        File.Delete(request.SourcePath);

        AddTrackResult result = AddTrackContract.Validate(request);

        Assert.False(result.Success);
        Assert.Equal(ConverterError.InputNotFound, result.Error);
        Assert.Equal(AddTrackStage.RequestValidation, result.Stage);
    }

    [Theory]
    [InlineData("video\naudio\n", true)]
    [InlineData("audio\nvideo\n", true)]
    [InlineData("video\n", false)]
    [InlineData("audio\n", false)]
    [InlineData("", false)]
    public void PreparedSourceOutputIsUsable_RequiresVideoAndAudio(string output, bool expected)
    {
        Assert.Equal(expected, MuxPipeline.PreparedSourceOutputIsUsable(output));
    }

    private static AddTrackRequest CreateMuxRequest(
        TemporaryLayout layout,
        MuxOperationalMode mode,
        MuxVideoCodec replacementCodec)
    {
        string source = layout.CreateFile("source.mkv");
        string replacement = layout.CreateFile("replacement.mkv");
        var options = new ConvertOptions
        {
            Codec = "mux",
            SelectionGroup = "mux",
            SelectionEncoder = mode.ToString().ToLowerInvariant(),
            Preset = mode.ToString().ToLowerInvariant(),
            AudioOutputMode = AudioOutputMode.Pcm,
            VideoTrackPath = replacement
        };
        MuxPolicy policy = AudioModePolicy.ResolveMuxPolicy(
            options, "macos", MuxVideoCodec.Unknown, replacementCodec, out string error)
            ?? throw new InvalidOperationException(error);

        return new AddTrackRequest(
            source, replacement, Path.Combine(layout.Root, "output.mkv"), layout.Root,
            options, options, "macos", policy,
            InputVideoProbeResult.FromProcessResult(0, $"codec_name={CodecName(replacementCodec)}\n", ""));
    }

    private static string CodecName(MuxVideoCodec codec) => codec switch
    {
        MuxVideoCodec.H264 => "h264",
        MuxVideoCodec.Hevc => "hevc",
        MuxVideoCodec.ProRes => "prores",
        MuxVideoCodec.Av1 => "av1",
        _ => "mpeg2video"
    };

    private sealed class TemporaryLayout : IDisposable
    {
        public string Root { get; } = Path.Combine(Path.GetTempPath(), $"add-track-{Guid.NewGuid():N}");

        public string CreateFile(string relativePath)
        {
            string path = Path.Combine(Root, relativePath);
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllText(path, "");
            return path;
        }

        public void Dispose()
        {
            if (Directory.Exists(Root))
                Directory.Delete(Root, recursive: true);
        }
    }
}
