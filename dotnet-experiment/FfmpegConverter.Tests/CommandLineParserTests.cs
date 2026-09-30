using System.IO;
using System.Threading.Tasks;
using FfmpegConverter.Cli;
using FfmpegConverter.Core.Models;
using FfmpegConverter.Core.Presets;
using FfmpegConverter.Core.Probing;
using Xunit;

namespace FfmpegConverter.Tests;

public class CommandLineParserTests
{
    private readonly PresetDb _presetDb = PresetDb.Load();

    [Fact]
    public void Parse_DefaultOptions_ResolvesSoftwareProresKs()
    {
        string[] args = { "input.mov" };
        var result = CommandLineParser.Parse(args, _presetDb, "macos");

        Assert.True(result.Success);
        Assert.Single(result.Files);
        Assert.Equal("input.mov", result.Files[0]);
        Assert.Equal("software", result.Options.SelectionGroup);
        Assert.Equal("prores_ks", result.Options.SelectionEncoder);
        Assert.Equal("prores_ks", result.Options.Codec);
        Assert.Equal("standard", result.Options.Preset);
        Assert.Equal("none", result.Options.AudioNorm);
        Assert.Equal("pcm", result.Options.AudioOutputMode);
        Assert.Equal(1, result.Options.Deblock);
    }

    [Fact]
    public void Parse_HierarchicalSelection_ResolvesCorrectly()
    {
        string[] args = { "-c", "videotoolbox", "--encoder", "h264", "-p", "quality", "video.mp4" };
        var result = CommandLineParser.Parse(args, _presetDb, "macos");

        Assert.True(result.Success);
        Assert.Equal("videotoolbox", result.Options.SelectionGroup);
        Assert.Equal("h264", result.Options.SelectionEncoder);
        Assert.Equal("h264_videotoolbox", result.Options.Codec);
        Assert.Equal("quality", result.Options.Preset);
    }

    [Fact]
    public void Parse_DeblockModes_ParsedAccurately()
    {
        string[] argsWeak = { "-d", "weak", "input.mov" };
        var resWeak = CommandLineParser.Parse(argsWeak, _presetDb, "macos");
        Assert.True(resWeak.Success);
        Assert.Equal(2, resWeak.Options.Deblock);

        string[] argsStrong = { "--deblock", "strong", "input.mov" };
        var resStrong = CommandLineParser.Parse(argsStrong, _presetDb, "macos");
        Assert.True(resStrong.Success);
        Assert.Equal(3, resStrong.Options.Deblock);
    }

    [Fact]
    public void Parse_DeblockOnHardwareCodec_ReturnsError()
    {
        string[] args = { "-c", "videotoolbox", "--encoder", "hevc", "-d", "weak", "input.mov" };
        var result = CommandLineParser.Parse(args, _presetDb, "macos");

        Assert.False(result.Success);
        Assert.Contains("not available for hardware encoder", result.ErrorMessage);
    }

    [Fact]
    public void Parse_AudioNormAndGenre_SetsTargets()
    {
        string[] args = { "-a", "loudnorm2", "-g", "rock", "--audio-output", "fdk_aac_320", "input.mov" };
        var result = CommandLineParser.Parse(args, _presetDb, "macos");

        Assert.True(result.Success);
        Assert.Equal("loudness_norm_2pass", result.Options.AudioNorm);
        Assert.Equal(2, result.Options.Genre); // Rock
        Assert.Equal("fdk_aac_320", result.Options.AudioOutputMode);
        Assert.Equal(-11.0, result.Options.ITarget);
        Assert.Equal(-1.0, result.Options.TpTarget);
        Assert.Equal(7.0, result.Options.LraTarget);
    }

    [Fact]
    public void Parse_AppleM4vParameters_ParsedAccurately()
    {
        var tools = new ToolPaths { Mp4Box = "mock-mp4box" };
        string[] args =
        {
            "-c", "mux", "--encoder", "m4v",
            "--m4v-video-track", "1",
            "--m4v-audio-track", "2",
            "--m4v-ac3-bitrate", "448",
            "--m4v-lang", "eng",
            "--no-m4v-chapters",
            "--m4v-edit-before-mux",
            "feature.mov"
        };
        var result = CommandLineParser.Parse(args, _presetDb, "macos", tools);

        Assert.True(result.Success);
        Assert.Equal("m4v", result.Options.Codec);
        Assert.Equal(1, result.Options.M4vVideoTrackIndex);
        Assert.Equal(2, result.Options.M4vAudioTrackIndex);
        Assert.Equal(448, result.Options.M4vAc3BitrateKbps);
        Assert.Equal("eng", result.Options.M4vAudioLang);
        Assert.False(result.Options.M4vAddChapters);
        Assert.True(result.Options.M4vEditBeforeMux);
    }

    [Fact]
    public void Parse_MuxParameters_RequiresSingleSourceFile()
    {
        var tools = new ToolPaths { Mkvmerge = "mock-mkvmerge" };
        string[] args =
        {
            "-c", "mux", "--encoder", "mkv",
            "--video-track", "replacement.264",
            "source1.mkv", "source2.mkv"
        };
        var result = CommandLineParser.Parse(args, _presetDb, "linux", tools);

        Assert.False(result.Success);
        Assert.Contains("requires exactly one source file", result.ErrorMessage);
    }

    [Fact]
    public void Parse_VulkanAndHwDevice_ParsedAccurately()
    {
        string[] args = { "--vk_device", "2", "--hw_device", "/dev/dri/renderD128", "input.mov" };
        var result = CommandLineParser.Parse(args, _presetDb, "linux");

        Assert.True(result.Success);
        Assert.Equal(2, result.Options.VulkanDevice);
        Assert.Equal("/dev/dri/renderD128", result.Options.HwDevice);
    }

    [Fact]
    public void Parse_SummaryOutput_MatchesCFormat()
    {
        var opts = new ConvertOptions
        {
            SelectionGroup = "software",
            SelectionEncoder = "prores_ks",
            Codec = "prores_ks",
            Preset = "standard",
            Deblock = 1,
            AudioNorm = "none",
            AudioOutputMode = "pcm",
            Overwrite = false,
            DryRun = false
        };

        string summary = CliSummary.FormatSummary(opts, new[] { "test.mov" }, _presetDb, "macos");

        Assert.Contains("=== Summary ===", summary);
        Assert.Contains("Codec:        software", summary);
        Assert.Contains("Encoder:      prores_ks", summary);
        Assert.Contains("Preset:       standard", summary);
        Assert.Contains("Deblock:      none", summary);
        Assert.Contains("Audio norm:   none", summary);
        Assert.Contains("Audio out:    pcm", summary);
        Assert.Contains("Overwrite:    no", summary);
        Assert.Contains("Dry run:      no", summary);
        Assert.Contains("Files (1):", summary);
        Assert.Contains("test.mov", summary);
    }

    [Fact]
    public async Task InteractiveMenu_StepThrough_ProducesValidOptions()
    {
        // Create a dummy test file so File.Exists passes
        string tempFile = Path.GetFullPath("test_interactive_input.mov");
        File.WriteAllText(tempFile, "dummy");

        try
        {
            // Simulate interactive terminal inputs:
            // Group: software (Enter)
            // Encoder: prores_ks (Enter)
            // Preset: 2 (standard)
            // Deblock: 1 (none)
            // Audio norm: 1 (none)
            // Audio out: 1 (pcm)
            // Overwrite: n
            // Output dir: (Enter -> default)
            // File 1: input.mov
            // File 2: (empty line to finish)
            string simulatedInput = $"\n\n2\n1\n1\n1\nn\n\n{tempFile}\n\n";
            using var reader = new StringReader(simulatedInput);
            using var writer = new StringWriter();

            var tools = new ToolPaths { Ffmpeg = "mock-ffmpeg" };
            var menuResult = await InteractiveMenu.RunMenuAsync(_presetDb, "macos", tools, null, reader, writer);

            Assert.NotNull(menuResult);
            Assert.Equal("software", menuResult.Options.SelectionGroup);
            Assert.Equal("prores_ks", menuResult.Options.SelectionEncoder);
            Assert.Equal("prores_ks", menuResult.Options.Codec);
            Assert.Equal("standard", menuResult.Options.Preset);
            Assert.Equal(1, menuResult.Options.Deblock);
            Assert.Equal("none", menuResult.Options.AudioNorm);
            Assert.Equal("pcm", menuResult.Options.AudioOutputMode);
            Assert.False(menuResult.Options.Overwrite);
            Assert.Single(menuResult.Files);
            Assert.Equal(tempFile, menuResult.Files[0]);
        }
        finally
        {
            if (File.Exists(tempFile)) File.Delete(tempFile);
        }
    }
}
