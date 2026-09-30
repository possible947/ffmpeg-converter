using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using FfmpegConverter.Core.Models;
using FfmpegConverter.Core.Presets;
using FfmpegConverter.Core.Probing;

namespace FfmpegConverter.Cli;

public class CommandLineParseResult
{
    public bool Success { get; set; } = true;
    public string? ErrorMessage { get; set; }
    public bool ShowHelp { get; set; }
    public bool ShowVersion { get; set; }
    public bool ShowCodecsList { get; set; }
    public ConvertOptions Options { get; set; } = new();
    public List<string> Files { get; set; } = new();

    public static CommandLineParseResult Error(string message) =>
        new() { Success = false, ErrorMessage = message };

    public static CommandLineParseResult Help() =>
        new() { Success = true, ShowHelp = true };

    public static CommandLineParseResult Version() =>
        new() { Success = true, ShowVersion = true };

    public static CommandLineParseResult CodecsList() =>
        new() { Success = true, ShowCodecsList = true };
}

public static class CommandLineParser
{
    public const string VersionString = "3.0b";

    public static CommandLineParseResult Parse(
        string[] args,
        PresetDb presetDb,
        string platform,
        ToolPaths? tools = null)
    {
        var opts = new ConvertOptions();
        var files = new List<string>();

        string selectionGroup = "software";
        string selectionEncoder = "prores_ks";
        string preset = "standard";
        bool presetExplicit = false;
        bool deblockExplicit = false;
        bool audioNormExplicit = false;

        for (int i = 0; i < args.Length; i++)
        {
            string arg = args[i];

            if (arg == "-h" || arg == "--help" || arg == "/?" || arg == "-?")
            {
                return CommandLineParseResult.Help();
            }
            if (arg == "-v" || arg == "--version")
            {
                return CommandLineParseResult.Version();
            }
            if (arg == "--codecs-list")
            {
                return CommandLineParseResult.CodecsList();
            }

            if (arg == "-c" || arg == "--codec")
            {
                if (i + 1 >= args.Length)
                    return CommandLineParseResult.Error("Error: --codec requires an argument");
                selectionGroup = args[++i];
                continue;
            }

            if (arg == "--encoder")
            {
                if (i + 1 >= args.Length)
                    return CommandLineParseResult.Error("Error: --encoder requires an argument");
                selectionEncoder = args[++i];
                continue;
            }

            if (arg == "-p" || arg == "--preset" || arg == "--profile")
            {
                if (i + 1 >= args.Length)
                    return CommandLineParseResult.Error("Error: --preset requires an argument");
                preset = args[++i];
                presetExplicit = true;
                continue;
            }

            if (arg == "-d" || arg == "--deblock")
            {
                if (i + 1 >= args.Length)
                    return CommandLineParseResult.Error("Error: --deblock requires an argument");
                string val = args[++i].ToLowerInvariant();
                if (val == "none") opts.Deblock = 1;
                else if (val == "weak") opts.Deblock = 2;
                else if (val == "strong") opts.Deblock = 3;
                else return CommandLineParseResult.Error($"Error: Invalid deblock mode: '{val}'. Expected: none, weak, strong");
                deblockExplicit = true;
                continue;
            }

            if (arg == "-a" || arg == "--audio-norm")
            {
                if (i + 1 >= args.Length)
                    return CommandLineParseResult.Error("Error: --audio-norm requires an argument");
                string val = args[++i].ToLowerInvariant();
                if (val == "none") opts.AudioNorm = "none";
                else if (val == "peak" || val == "peak_norm") opts.AudioNorm = "peak_norm";
                else if (val == "peak2" || val == "peak_norm_2pass") opts.AudioNorm = "peak_norm_2pass";
                else if (val == "loudnorm" || val == "loudness_norm") opts.AudioNorm = "loudness_norm";
                else if (val == "loudnorm2" || val == "loudness_norm_2pass") opts.AudioNorm = "loudness_norm_2pass";
                else return CommandLineParseResult.Error($"Error: Invalid audio-norm mode: '{val}'. Expected: none, peak, peak2, loudnorm, loudnorm2");
                audioNormExplicit = true;
                continue;
            }

            if (arg == "--audio-output" || arg == "--audio-mode")
            {
                if (i + 1 >= args.Length)
                    return CommandLineParseResult.Error("Error: --audio-output requires an argument");
                string val = args[++i].ToLowerInvariant();
                if (val == "pcm" || val == "fdk_aac_320" || val == "fdk_aac_320_ac3_640")
                {
                    opts.AudioOutputMode = val;
                }
                else
                {
                    return CommandLineParseResult.Error($"Error: Invalid audio-output mode: '{val}'. Expected: pcm, fdk_aac_320, fdk_aac_320_ac3_640");
                }
                continue;
            }

            if (arg == "-g" || arg == "--genre")
            {
                if (i + 1 >= args.Length)
                    return CommandLineParseResult.Error("Error: --genre requires an argument");
                string val = args[++i].ToLowerInvariant();
                if (val == "edm") opts.Genre = 1;
                else if (val == "rock") opts.Genre = 2;
                else if (val == "hiphop" || val == "hip-hop") opts.Genre = 3;
                else if (val == "classical") opts.Genre = 4;
                else if (val == "podcast") opts.Genre = 5;
                else return CommandLineParseResult.Error($"Error: Invalid genre: '{val}'. Expected: edm, rock, hiphop, classical, podcast");
                opts.ApplyGenreTargets();
                continue;
            }

            if (arg == "--video-track")
            {
                if (i + 1 >= args.Length)
                    return CommandLineParseResult.Error("Error: --video-track requires an argument");
                opts.VideoTrackPath = CleanPath(args[++i]);
                continue;
            }

            if (arg == "--overwrite")
            {
                opts.Overwrite = true;
                continue;
            }

            if (arg == "--dry-run")
            {
                opts.DryRun = true;
                continue;
            }

            if (arg == "--vk_device")
            {
                if (i + 1 >= args.Length)
                    return CommandLineParseResult.Error("Error: --vk_device requires an argument");
                if (int.TryParse(args[++i], out int vk) && vk >= 0 && vk <= 7)
                {
                    opts.VulkanDevice = vk;
                }
                else
                {
                    return CommandLineParseResult.Error($"Error: Invalid Vulkan device index: '{args[i]}'. Expected integer between 0 and 7");
                }
                continue;
            }

            if (arg == "--hw_device")
            {
                if (i + 1 >= args.Length)
                    return CommandLineParseResult.Error("Error: --hw_device requires an argument");
                opts.HwDevice = args[++i];
                continue;
            }

            if (arg == "-o" || arg == "--output")
            {
                if (i + 1 >= args.Length)
                    return CommandLineParseResult.Error("Error: --output requires an argument");
                opts.OutputDir = CleanPath(args[++i]);
                continue;
            }

            // Apple M4V parameters
            if (arg == "--m4v-video-track")
            {
                if (i + 1 >= args.Length || !int.TryParse(args[++i], out int vTrack) || vTrack < 0)
                    return CommandLineParseResult.Error("Error: --m4v-video-track requires a non-negative integer");
                opts.M4vVideoTrackIndex = vTrack;
                continue;
            }

            if (arg == "--m4v-audio-track")
            {
                if (i + 1 >= args.Length || !int.TryParse(args[++i], out int aTrack) || aTrack < 0)
                    return CommandLineParseResult.Error("Error: --m4v-audio-track requires a non-negative integer");
                opts.M4vAudioTrackIndex = aTrack;
                continue;
            }

            if (arg == "--m4v-ac3-bitrate")
            {
                if (i + 1 >= args.Length || !int.TryParse(args[++i], out int br) || br <= 0)
                    return CommandLineParseResult.Error("Error: --m4v-ac3-bitrate requires a positive integer kbps value");
                opts.M4vAc3BitrateKbps = br;
                continue;
            }

            if (arg == "--m4v-lang")
            {
                if (i + 1 >= args.Length)
                    return CommandLineParseResult.Error("Error: --m4v-lang requires a language code argument");
                opts.M4vAudioLang = args[++i];
                continue;
            }

            if (arg == "--m4v-chapters")
            {
                opts.M4vAddChapters = true;
                continue;
            }

            if (arg == "--no-m4v-chapters")
            {
                opts.M4vAddChapters = false;
                continue;
            }

            if (arg == "--m4v-edit-before-mux")
            {
                opts.M4vEditBeforeMux = true;
                continue;
            }

            if (arg == "--no-m4v-edit-before-mux" || arg == "--m4v-direct-mux")
            {
                opts.M4vEditBeforeMux = false;
                continue;
            }

            if (arg.StartsWith("-"))
            {
                return CommandLineParseResult.Error($"Unknown option: {arg}");
            }

            files.Add(CleanPath(arg));
        }

        // Post-parsing selection resolution
        opts.SelectionGroup = selectionGroup;
        opts.SelectionEncoder = selectionEncoder;

        if (!presetDb.ResolveSelection(platform, selectionGroup, selectionEncoder, out string resolvedCodec))
        {
            if (presetDb.Platforms.TryGetValue(platform, out var direct) && direct.ContainsKey(selectionGroup))
            {
                resolvedCodec = selectionGroup;
            }
            else
            {
                return CommandLineParseResult.Error($"Error: unavailable codec group/encoder: {selectionGroup}/{selectionEncoder}");
            }
        }
        opts.Codec = resolvedCodec;

        // Preset defaults & resolution
        if (!presetExplicit)
        {
            if (opts.Codec.Equals("copy", StringComparison.OrdinalIgnoreCase))
            {
                opts.Preset = "default";
            }
            else if (opts.Codec.Equals("mux", StringComparison.OrdinalIgnoreCase) &&
                     (selectionEncoder.Equals("mkv", StringComparison.OrdinalIgnoreCase) ||
                      selectionEncoder.Equals("mov", StringComparison.OrdinalIgnoreCase) ||
                      selectionEncoder.Equals("m4v", StringComparison.OrdinalIgnoreCase)))
            {
                opts.Preset = selectionEncoder.ToLowerInvariant();
            }
            else if (opts.Codec.Contains("prores", StringComparison.OrdinalIgnoreCase))
            {
                opts.Preset = "standard";
            }
            else
            {
                opts.Preset = "default";
            }
        }
        else
        {
            opts.Preset = preset;
        }

        // Hardware constraints and validation
        bool isHw = opts.Codec.Contains("_vaapi", StringComparison.OrdinalIgnoreCase) ||
                    opts.Codec.Contains("_nvenc", StringComparison.OrdinalIgnoreCase) ||
                    opts.Codec.Contains("_amf", StringComparison.OrdinalIgnoreCase) ||
                    opts.Codec.Contains("_qsv", StringComparison.OrdinalIgnoreCase) ||
                    opts.Codec.Contains("_videotoolbox", StringComparison.OrdinalIgnoreCase) ||
                    opts.Codec.Contains("_vulkan", StringComparison.OrdinalIgnoreCase);

        if (isHw && deblockExplicit)
        {
            return CommandLineParseResult.Error($"Error: --deblock is not available for hardware encoder '{opts.Codec}'");
        }

        if (isHw && !audioNormExplicit)
        {
            opts.AudioNorm = "none";
        }

        if (presetExplicit)
        {
            bool validPreset = false;
            if (opts.Codec.Equals("prores_ks_vulkan", StringComparison.OrdinalIgnoreCase))
            {
                validPreset = opts.Preset is "lt" or "standard" or "hq" or "4444";
            }
            else if (isHw)
            {
                validPreset = opts.Preset is "default" or "speed" or "balance" or "quality";
            }
            else if (opts.Codec.Equals("copy", StringComparison.OrdinalIgnoreCase) ||
                     opts.Codec.Equals("mux", StringComparison.OrdinalIgnoreCase) ||
                     opts.Codec.Equals("m4v", StringComparison.OrdinalIgnoreCase))
            {
                validPreset = true;
            }
            else
            {
                var available = new List<string>(presetDb.GetPresets(platform, opts.Codec));
                validPreset = available.Count == 0 || available.Contains(opts.Preset, StringComparer.OrdinalIgnoreCase);
            }

            if (!validPreset)
            {
                return CommandLineParseResult.Error($"Error: preset '{opts.Preset}' is not available for encoder '{opts.Codec}'");
            }
        }

        // Output directory validation
        if (!string.IsNullOrEmpty(opts.OutputDir))
        {
            try
            {
                Directory.CreateDirectory(opts.OutputDir);
                opts.OutputDirStatus = 1;
            }
            catch
            {
                opts.OutputDirStatus = 0;
            }
        }

        // Pipeline validation
        if (opts.Codec.Equals("mux", StringComparison.OrdinalIgnoreCase))
        {
            if (tools != null && string.IsNullOrEmpty(tools.Mkvmerge))
            {
                return CommandLineParseResult.Error("Mux mode is not supported on this platform (mkvmerge not found).");
            }
            if (files.Count > 0 && files.Count != 1)
            {
                return CommandLineParseResult.Error("Mux mode requires exactly one source file.");
            }
            if (files.Count > 0 && (string.IsNullOrEmpty(opts.VideoTrackPath) || !File.Exists(opts.VideoTrackPath)))
            {
                return CommandLineParseResult.Error("Mux mode requires a readable --video-track file.");
            }
        }

        if (opts.Codec.Equals("m4v", StringComparison.OrdinalIgnoreCase))
        {
            if (tools != null && string.IsNullOrEmpty(tools.Mp4Box))
            {
                return CommandLineParseResult.Error("Apple M4V mode is not supported on this platform (MP4Box not found).");
            }
        }

        return new CommandLineParseResult
        {
            Success = true,
            Options = opts,
            Files = files
        };
    }

    public static string CleanPath(string input)
    {
        if (string.IsNullOrWhiteSpace(input)) return "";
        string s = input.Trim();
        if ((s.StartsWith("\"") && s.EndsWith("\"")) || (s.StartsWith("'") && s.EndsWith("'")))
        {
            s = s.Substring(1, s.Length - 2);
        }
        s = s.Replace("\\ ", " ").Trim();
        return s;
    }
}
