using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using FfmpegConverter.Core.Engine;
using FfmpegConverter.Core.Models;
using FfmpegConverter.Core.Policy;
using FfmpegConverter.Core.Probing;
using Xunit;

namespace FfmpegConverter.Tests;

public class AppleM4vPipelineTests
{
    [Theory]
    [InlineData("h264", true)]
    [InlineData("H264", true)]
    [InlineData("hevc", true)]
    [InlineData("HEVC", true)]
    [InlineData("prores", true)]
    [InlineData("prores_ks", true)]
    [InlineData("vp9", false)]
    [InlineData("av1", false)]
    [InlineData("mpeg4", false)]
    [InlineData("", false)]
    [InlineData(null, false)]
    public void IsSupportedVideoCodec_ValidatesExpectedCodecs(string? codec, bool expected)
    {
        bool actual = AppleM4vPipeline.IsSupportedVideoCodec(codec);
        Assert.Equal(expected, actual);
    }

    [Fact]
    public async Task ValidateInputSupported_MissingFile_ReturnsFalse()
    {
        var result = await AppleM4vPipeline.ValidateInputSupportedAsync("/fake/ffprobe", "/fake/nonexistent.mov");
        Assert.False(result.IsSupported);
        Assert.Contains("not found", result.ErrorDetail);
    }

    [Fact]
    public async Task CreateM4v_MissingTools_ReturnsInvalidOptions()
    {
        var pipeline = new AppleM4vPipeline();
        var tools = new ToolPaths { Ffmpeg = "", Ffprobe = "", Mp4Box = "" };
        var opts = new ConvertOptions();

        var err = await pipeline.CreateM4vAsync("input.mov", "output.m4v", opts, tools, null);
        Assert.Equal(ConverterError.InvalidOptions, err);
    }

    [Fact]
    public async Task CreateMuxM4v_RejectsInvalidAudioBeforeCheckingTools()
    {
        PolicyResult validPolicy = MuxPolicyResolver.Resolve(
            new MuxBuildProfile(MuxPlatform.Linux, MuxFfmpegVersion.V8_1),
            MuxOperationalMode.M4v,
            AudioOutputMode.Aac,
            MuxVideoCodec.Unknown,
            MuxVideoCodec.H264);
        Assert.True(validPolicy.Success);
        MuxPolicy invalidPolicy = validPolicy.Policy! with
        {
            AudioMode = AudioOutputMode.Pcm
        };

        var pipeline = new AppleM4vPipeline();
        string? error = null;
        pipeline.ErrorOccurred += (_, args) => error = args.Text;
        ConverterError result = await pipeline.CreateMuxM4vAsync(
            "input.mkv", "output.m4v", new ConvertOptions(),
            new ToolPaths(), null, invalidPolicy);

        Assert.Equal(ConverterError.InvalidOptions, result);
        Assert.Contains("requires AAC or AAC+AC3", error);
    }

    [Fact]
    public void MuxM4vAudioOnly_ProducesSingleAacStreamAndDefaultDisposition()
    {
        const string specialInput = "/tmp/Video + (日本語).mkv";
        const string specialOutput = "/tmp/Video + (日本語).m4v";
        IReadOnlyList<string> aacArgs = AppleM4vPipeline.BuildFixedAacArguments(
            specialInput, 0, "/tmp/audio (日本語).m4a");
        IReadOnlyList<string> mp4BoxArgs = AppleM4vPipeline.BuildMp4BoxArguments(
            "/tmp/video + (日本語).mp4#video:fps=24.000000:name=Video",
            "/tmp/audio + (日本語).m4a#audio:name=AAC:lang=rus",
            "/tmp/unused.m4a#audio:name=AC3 640k:lang=rus",
            specialOutput,
            addAc3: false);
        IReadOnlyList<string> dispositionArgs = AppleM4vPipeline.BuildDispositionArguments(
            specialOutput, "/tmp/disposition + (日本語).m4v", hasAc3: false);

        Assert.Contains("libfdk_aac", aacArgs);
        Assert.Contains("320k", aacArgs);
        Assert.Contains("48000", aacArgs);
        Assert.Equal(2, mp4BoxArgs.Count(arg => arg == "-add"));
        Assert.DoesNotContain("/tmp/unused.m4a#audio:name=AC3 640k:lang=rus", mp4BoxArgs);
        Assert.Contains("0:a:0", dispositionArgs);
        Assert.DoesNotContain("0:a:1", dispositionArgs);
        Assert.DoesNotContain("-disposition:a:1", dispositionArgs);
        Assert.Equal(specialOutput, mp4BoxArgs[^1]);
    }

    [Fact]
    public void DirectM4vUsesFixedLibfdkAacAndPreservesItsOwnM4vPolicy()
    {
        IReadOnlyList<string> aacArgs =
            AppleM4vPipeline.BuildFixedAacArguments("input.mov", 1, "audio.m4a");
        MuxPolicy policy = MuxPolicyResolver.CreateDirectM4vPreservationPolicy();
        MuxSourcePreservationResult preservation = MuxSourcePreservation.ParseProbeJson(
            """
            {
              "streams": [
                { "index": 0, "codec_type": "video", "codec_name": "h264" },
                { "index": 1, "codec_type": "audio", "codec_name": "aac" },
                { "index": 3, "codec_type": "subtitle", "codec_name": "subrip" },
                { "index": 4, "codec_type": "attachment", "codec_name": "ttf" }
              ]
            }
            """,
            policy);
        IReadOnlyList<string> arguments = AppleM4vPipeline.BuildMuxMetadataSubtitleArguments(
            "assembled.m4v",
            "source.mov",
            "preserved.m4v",
            preservation.Plan!,
            sourceVideoTrackIndex: 0);

        Assert.Contains("libfdk_aac", aacArgs);
        Assert.Contains("320k", aacArgs);
        Assert.Equal(AudioWorkflowContext.DirectM4v, policy.Workflow);
        Assert.False(policy.RequiresAddTrack);
        Assert.True(preservation.Success);
        Assert.False(preservation.Plan!.PreserveAttachments);
        Assert.Contains("1:3", arguments);
        Assert.Contains("-map_metadata", arguments);
        Assert.Contains("-map_chapters", arguments);
        Assert.Equal("preserved.m4v", arguments[^1]);
    }

    [Fact]
    public void MuxM4vDualAudio_MapsAc3AndMakesOnlyAacDefault()
    {
        IReadOnlyList<string> ac3Args = AppleM4vPipeline.BuildAudioEncodeArguments(
            "input.mkv", 0, "ac3", "640k", "audio_ac3.mp4", "mp4",
            sampleRate: null);
        IReadOnlyList<string> mp4BoxArgs = AppleM4vPipeline.BuildMp4BoxArguments(
            "video.mp4#video:fps=24.000000:name=Video",
            "audio_aac.m4a#audio:name=AAC:lang=rus",
            "audio_ac3.mp4#audio:name=AC3 640k:lang=rus",
            "output.m4v",
            addAc3: true);
        IReadOnlyList<string> dispositionArgs = AppleM4vPipeline.BuildDispositionArguments(
            "output.m4v", "with_disposition.m4v", hasAc3: true);

        Assert.DoesNotContain("-ar", ac3Args);
        Assert.Equal(3, mp4BoxArgs.Count(arg => arg == "-add"));
        Assert.Contains("0:a:1", dispositionArgs);
        Assert.Contains("-disposition:a:0", dispositionArgs);
        Assert.Contains("default", dispositionArgs);
        Assert.Contains("-disposition:a:1", dispositionArgs);
        Assert.Contains("0", dispositionArgs);
    }

    [Fact]
    public void MuxM4vMetadataRemux_MapsSubtitlesOnlyFromSourceAndKeepsChapterOutput()
    {
        PolicyResult policy = MuxPolicyResolver.Resolve(
            new MuxBuildProfile(MuxPlatform.Linux, MuxFfmpegVersion.V8_1),
            MuxOperationalMode.M4v,
            AudioOutputMode.Aac,
            MuxVideoCodec.H264,
            MuxVideoCodec.H264);
        MuxSourcePreservationResult preservation = MuxSourcePreservation.ParseProbeJson(
            """{ "streams": [{ "index": 4, "codec_type": "subtitle", "codec_name": "subrip" }] }""",
            policy.Policy!);

        IReadOnlyList<string> arguments = AppleM4vPipeline.BuildMuxMetadataSubtitleArguments(
            "assembled.m4v",
            "/source + (日本語).mkv",
            "with_subtitles.m4v",
            preservation.Plan!,
            sourceVideoTrackIndex: 0);

        Assert.Contains("1:4", arguments);
        Assert.Contains("1:s:v:0", arguments);
        Assert.Contains("-map_metadata", arguments);
        Assert.Contains("-map_chapters", arguments);
        Assert.Contains("0:v:0", arguments);
        Assert.Contains("0:a?", arguments);
        Assert.DoesNotContain("0", arguments.Where((_, index) =>
            index > 0 && arguments[index - 1] == "-map"));
        Assert.DoesNotContain("1:t?", arguments);
        Assert.Equal("with_subtitles.m4v", arguments[^1]);
    }

    [Fact]
    public void M4vWorkRoot_UsesPlusFreeTemporaryDirectoryForMp4BoxImports()
    {
        Assert.Equal(
            "/output dir",
            AppleM4vPipeline.SelectWorkRoot(
                "/output dir/Film (日本語).m4v", "/output dir", "/temporary"));
        Assert.Equal(
            "/temporary",
            AppleM4vPipeline.SelectWorkRoot(
                "/output + dir/Film.m4v", "/output + dir", "/temporary"));
        Assert.Throws<System.ArgumentException>(() =>
            AppleM4vPipeline.SelectWorkRoot(
                "/output + dir/Film.m4v", "/output + dir", "/temporary + dir"));
    }

    [Fact]
    public async Task Converter_M4vDryRun_SucceedsWithoutTouchingFiles()
    {
        var tempInput = Path.GetTempFileName();
        try
        {
            var converter = new Converter();
            var opts = new ConvertOptions
            {
                Codec = "m4v",
                DryRun = true,
                OutputDir = Path.GetTempPath()
            };

            bool messageLogged = false;
            converter.MessageLogged += (s, e) =>
            {
                if (e.Text.Contains("[DRY RUN] [m4v]"))
                    messageLogged = true;
            };

            var result = await converter.ProcessFilesAsync(new[] { tempInput }, opts);
            Assert.Equal(ConverterError.Ok, result);
            Assert.True(messageLogged);
        }
        finally
        {
            if (File.Exists(tempInput)) File.Delete(tempInput);
        }
    }

    [Fact]
    public async Task Converter_DirectM4vEditBeforeMuxDryRunShowsBothStages()
    {
        string input = Path.GetTempFileName();
        string outputDir = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N"));
        try
        {
            var converter = new Converter();
            var directOptions = new ConvertOptions
            {
                SelectionGroup = "m4v",
                SelectionEncoder = "m4v",
                Codec = "m4v",
                M4vEditBeforeMux = true,
                DryRun = true,
                OutputDir = outputDir
            };
            var sourceOptions = new ConvertOptions
            {
                SelectionGroup = "software",
                SelectionEncoder = "prores_ks",
                Codec = "prores_ks",
                Preset = "standard"
            };
            string? message = null;
            converter.MessageLogged += (_, args) => message = args.Text;

            ConverterError result =
                await converter.ProcessDirectM4vFilesWithEditBeforeMuxAsync(
                    new[] { input }, directOptions, sourceOptions);

            Assert.Equal(ConverterError.Ok, result);
            Assert.Contains("[m4v edit-before-mux]", message);
            Assert.Contains("_converted.mov", message);
            Assert.EndsWith(".m4v", message);
            Assert.False(Directory.Exists(outputDir));
        }
        finally
        {
            File.Delete(input);
            if (Directory.Exists(outputDir))
                Directory.Delete(outputDir, recursive: true);
        }
    }

    [Fact]
    public async Task Converter_DirectM4vEditBeforeMuxRequiresSourceConversionOptions()
    {
        string input = Path.GetTempFileName();
        try
        {
            var converter = new Converter(
                new ToolPaths { Ffmpeg = "mock-ffmpeg" },
                probeResult: new HardwareProbeResult());
            var options = new ConvertOptions
            {
                SelectionGroup = "m4v",
                SelectionEncoder = "m4v",
                Codec = "m4v",
                M4vEditBeforeMux = true,
                OutputDir = Path.GetTempPath()
            };

            ConverterError result = await converter.ProcessFilesAsync(new[] { input }, options);

            Assert.Equal(ConverterError.InvalidOptions, result);
        }
        finally
        {
            File.Delete(input);
        }
    }

    [Theory]
    [InlineData("24/1", 24.0)]
    [InlineData("30000/1001", 29.97002997002997)]
    [InlineData("60/1", 60.0)]
    [InlineData("25.0", 25.0)]
    [InlineData("0/0", 25.0)]
    [InlineData("", 25.0)]
    public void ParseRateToFps_CalculatesCorrectFramerate(string rateStr, double expectedFps)
    {
        double fps = 25.0;
        if (!string.IsNullOrEmpty(rateStr) && rateStr != "0/0")
        {
            var parts = rateStr.Split('/');
            if (parts.Length == 2 &&
                double.TryParse(parts[0], System.Globalization.CultureInfo.InvariantCulture, out double n) &&
                double.TryParse(parts[1], System.Globalization.CultureInfo.InvariantCulture, out double d) &&
                d > 0)
            {
                fps = n / d;
            }
            else if (double.TryParse(rateStr, System.Globalization.CultureInfo.InvariantCulture, out double direct))
            {
                fps = direct;
            }
        }

        Assert.Equal(expectedFps, fps, precision: 4);
    }
}
