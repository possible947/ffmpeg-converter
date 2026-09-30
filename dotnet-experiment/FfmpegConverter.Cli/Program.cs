using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using FfmpegConverter.Core.Models;
using FfmpegConverter.Core.Presets;
using FfmpegConverter.Core.Probing;
using FfmpegConverter.Core.Engine;

namespace FfmpegConverter.Cli;

class Program
{
    static async Task<int> Main(string[] args)
    {
        Console.OutputEncoding = System.Text.Encoding.UTF8;

        string platform = RuntimeInformation.IsOSPlatform(OSPlatform.Windows) ? "windows" :
                         RuntimeInformation.IsOSPlatform(OSPlatform.OSX) ? "macos" : "linux";

        var presetDb = PresetDb.Load();
        var tools = ToolDiscovery.ResolveAll();

        // 1. Check quick-exit options without heavy probing
        if (args.Length == 1)
        {
            string first = args[0];
            if (first is "-h" or "--help" or "/?" or "-?")
            {
                ShowUsage(tools, platform);
                return 0;
            }
            if (first is "-v" or "--version")
            {
                ShowVersion();
                return 0;
            }
            if (first == "--codecs-list")
            {
                await ShowCodecsListAsync(presetDb, tools, platform);
                return 0;
            }
        }

        // 2. Hardware components detection
        Console.WriteLine("Hardware components detection in progress...");
        HardwareProbeResult? probe = null;
        if (!string.IsNullOrEmpty(tools.Ffmpeg))
        {
            probe = await HardwareProbe.ProbeCapabilitiesAsync(tools.Ffmpeg, presetDb);
        }
        Console.WriteLine("Hardware components detection completed.");

        ConvertOptions opts;
        List<string> files;

        // 3. Interactive Menu vs Command-Line Arguments
        if (args.Length == 0)
        {
            var menuResult = await InteractiveMenu.RunMenuAsync(presetDb, platform, tools, probe);
            if (menuResult == null)
            {
                Console.WriteLine("Menu cancelled by user.");
                return 1;
            }
            if (menuResult.Files.Count == 0)
            {
                Console.WriteLine("No files selected.");
                return 1;
            }
            opts = menuResult.Options;
            files = menuResult.Files;
        }
        else
        {
            var parseResult = CommandLineParser.Parse(args, presetDb, platform, tools);
            if (!parseResult.Success)
            {
                Console.Error.WriteLine(parseResult.ErrorMessage);
                Console.WriteLine("Invalid options. Use -h for help.");
                return 1;
            }

            if (parseResult.ShowHelp)
            {
                ShowUsage(tools, platform);
                return 0;
            }
            if (parseResult.ShowVersion)
            {
                ShowVersion();
                return 0;
            }
            if (parseResult.ShowCodecsList)
            {
                await ShowCodecsListAsync(presetDb, tools, platform);
                return 0;
            }

            opts = parseResult.Options;
            files = parseResult.Files;
        }

        if (files.Count == 0)
        {
            ShowUsage(tools, platform);
            return 1;
        }

        // Apply hardware defaults if not specified
        ApplyHardwareDefaults(opts, probe, platform);

        // Print Summary (matching C implementation)
        CliSummary.PrintSummary(opts, files, presetDb, platform);

        // Verify readable files
        var validFiles = new List<string>();
        foreach (var file in files)
        {
            if (File.Exists(file))
            {
                validFiles.Add(file);
            }
            else
            {
                Console.Error.WriteLine($"File not found or unreadable: {file}");
            }
        }

        if (validFiles.Count == 0)
        {
            Console.Error.WriteLine("Error: None of the specified files could be found.");
            return 1;
        }
        if (validFiles.Count < files.Count)
        {
            Console.WriteLine($"Will process {validFiles.Count} valid file(s)");
        }

        // Validate pipeline-specific constraints
        if (opts.Codec.Equals("mux", StringComparison.OrdinalIgnoreCase))
        {
            if (string.IsNullOrEmpty(tools.Mkvmerge))
            {
                Console.Error.WriteLine("Mux mode is not supported on this platform (mkvmerge not found).");
                return 1;
            }
            if (validFiles.Count != 1)
            {
                Console.Error.WriteLine("Mux mode requires exactly one source file.");
                return 1;
            }
            if (string.IsNullOrEmpty(opts.VideoTrackPath) || !File.Exists(opts.VideoTrackPath))
            {
                Console.Error.WriteLine("Mux mode requires a readable --video-track file.");
                return 1;
            }
        }

        if (opts.Codec.Equals("m4v", StringComparison.OrdinalIgnoreCase))
        {
            if (string.IsNullOrEmpty(tools.Mp4Box))
            {
                Console.Error.WriteLine("Apple M4V mode is not supported on this platform (MP4Box not found).");
                return 1;
            }
        }

        if (string.IsNullOrEmpty(tools.Ffmpeg))
        {
            Console.Error.WriteLine("Error: ffmpeg could not be resolved! Place it in the path or set FFMPEG_BIN.");
            return 1;
        }

        // 4. Execution
        var converter = new Core.Engine.Converter();
        var cts = new CancellationTokenSource();

        Console.CancelKeyPress += (s, e) =>
        {
            Console.WriteLine("\n[C# Converter] Cancellation requested. Shutting down...");
            e.Cancel = true;
            cts.Cancel();
        };

        converter.FileBegin += (s, e) =>
        {
            Console.WriteLine($"\nFile [{e.Index}/{e.Total}]: {Path.GetFileName(e.FileName)}");
        };

        converter.FileEnd += (s, e) =>
        {
            Console.WriteLine($"\nFinished file: {Path.GetFileName(e.FileName)} with status: {e.Status}");
        };

        converter.StageChanged += (s, e) =>
        {
            Console.WriteLine($"  Stage: {e}");
        };

        converter.MessageLogged += (s, e) =>
        {
            Console.WriteLine($"  [Log] {e.Text}");
        };

        converter.ErrorOccurred += (s, e) =>
        {
            Console.Error.WriteLine($"  [Error] {e.Text} (Code: {e.Code})");
        };

        int barWidth = 40;
        converter.ProgressEncode += (s, e) =>
        {
            int filled = (int)(e.Percent * barWidth / 100f);
            if (filled < 0) filled = 0;
            if (filled > barWidth) filled = barWidth;

            string bar = new string('#', filled) + new string('-', barWidth - filled);
            string etaStr = e.EtaSeconds > 0 ? $"ETA: {TimeSpan.FromSeconds(e.EtaSeconds):hh\\:mm\\:ss}" : "ETA: --:--:--";
            Console.Write($"\r  Progress: [{bar}] {e.Percent:F1}% | fps={e.Fps:F0} | {etaStr}");
        };

        converter.ProgressAnalysis += (s, e) =>
        {
            string etaStr = e.EtaSeconds > 0 ? $"ETA: {TimeSpan.FromSeconds(e.EtaSeconds):hh\\:mm\\:ss}" : "ETA: --:--:--";
            Console.Write($"\r  Analysis: {e.Percent:F1}% | {etaStr}");
        };

        var result = await converter.ProcessFilesAsync(validFiles, opts, cts.Token);

        if (result == ConverterError.Ok)
        {
            Console.WriteLine("\n[C# Converter] All conversion tasks finished successfully.");
            return 0;
        }
        else
        {
            Console.WriteLine($"\n[C# Converter] Conversion finished with errors: {result}");
            return (int)result;
        }
    }

    static void ApplyHardwareDefaults(ConvertOptions opts, HardwareProbeResult? probe, string platform)
    {
        if (opts.VulkanDevice < 0)
        {
            if (probe != null && probe.SelectedVulkanDeviceIndex >= 0)
            {
                opts.VulkanDevice = probe.SelectedVulkanDeviceIndex;
            }
            else
            {
                opts.VulkanDevice = 0;
            }
        }

        if (platform == "linux" && string.IsNullOrEmpty(opts.HwDevice) &&
            opts.Codec.Contains("_vaapi", StringComparison.OrdinalIgnoreCase))
        {
            // Auto-detect VAAPI render node
            string[] candidateNodes = { "/dev/dri/renderD128", "/dev/dri/renderD129" };
            foreach (var node in candidateNodes)
            {
                if (File.Exists(node))
                {
                    opts.HwDevice = node;
                    break;
                }
            }
        }
    }

    static void ShowVersion()
    {
        Console.WriteLine($"ffmpeg_converter {CommandLineParser.VersionString}");
    }

    static void ShowUsage(ToolPaths tools, string platform)
    {
        bool hasMkvmerge = !string.IsNullOrEmpty(tools.Mkvmerge);
        bool hasMp4box = !string.IsNullOrEmpty(tools.Mp4Box);

        Console.WriteLine("Usage: ffmpeg_converter [options] file1 file2 ...\n");
        Console.WriteLine("Options:");
        Console.WriteLine("  -h, --help                Show this help message");
        Console.WriteLine("      --codecs-list         List all available codecs and presets");
        Console.WriteLine("  -v, --version             Show version information\n");

        Console.WriteLine("  -c, --codec <group>       Codec group (software, mux, or hardware group)");
        Console.WriteLine("      --encoder <name>      Encoder inside the selected group");
        Console.WriteLine("  -p, --preset <name>       Preset for the selected encoder");
        Console.WriteLine("  Available groups are filtered from presets.json after hardware detection.");
        Console.WriteLine("  -d, --deblock <none|weak|strong>");
        Console.WriteLine("  -a, --audio-norm <none|peak|peak2|loudnorm|loudnorm2>");
        Console.WriteLine("      --audio-output <pcm|fdk_aac_320|fdk_aac_320_ac3_640>");
        if (hasMkvmerge)
        {
            Console.WriteLine("      --video-track <file>  replacement video track for mux mode");
        }
        Console.WriteLine("  -g, --genre <edm|rock|hiphop|classical|podcast>");
        Console.WriteLine("      (genre is used only with loudnorm2)");
        Console.WriteLine("  --overwrite        overwrite output files");
        Console.WriteLine("  --dry-run          print the conversion plan without running ffmpeg");
        Console.WriteLine("      --vk_device <N>    Vulkan adapter index (default: auto)");
        if (platform == "linux")
        {
            Console.WriteLine("      --hw_device <path> VAAPI render node for h264_vaapi/hevc_vaapi (default: auto-detected)");
        }
        Console.WriteLine("  -o, --output <directory> set output directory\n");

        if (hasMkvmerge)
        {
            Console.WriteLine("Mux mode:");
            Console.WriteLine("  - requires exactly one source file");
            Console.WriteLine("  - requires --video-track <file>");
            Console.WriteLine("  - runs normal audio processing, then writes final .mkv\n");
        }

        if (hasMp4box)
        {
            Console.WriteLine("Apple M4V options (only used with -c mux --encoder m4v or -c m4v):");
            Console.WriteLine("      --m4v-video-track <N>   video stream index (default: 0)");
            Console.WriteLine("      --m4v-audio-track <N>   audio stream index (default: 0)");
            Console.WriteLine("      --m4v-ac3-bitrate <kbps> AC3 bitrate in kbps (default: 640)");
            Console.WriteLine("      --m4v-lang <tag>        audio language tag (default: rus)");
            Console.WriteLine("      --m4v-chapters          embed chapter markers (default: on)");
            Console.WriteLine("      --no-m4v-chapters       disable chapter markers\n");
            Console.WriteLine("Apple M4V mode:");
            Console.WriteLine("  - requires MP4Box (GPAC) on PATH");
            Console.WriteLine("  - uses libfdk_aac CBR 320k for AAC encoding (fixed)");
            Console.WriteLine("  - accepts input with h264, hevc, or prores video");
            Console.WriteLine("  - produces dual-audio .m4v (AAC + AC3) compatible with Apple TV\n");
        }

        Console.WriteLine("Examples:");
        Console.WriteLine("  ffmpeg_converter input.mov");
        Console.WriteLine("  ffmpeg_converter -c software --encoder prores_ks --preset hq input.mov");
        Console.WriteLine("  ffmpeg_converter -a loudnorm2 -g rock input1.mov input2.mov");
        if (hasMp4box)
        {
            Console.WriteLine("  ffmpeg_converter -c mux --encoder m4v --m4v-lang eng input.mov");
        }
        Console.WriteLine();
    }

    static async Task ShowCodecsListAsync(PresetDb presetDb, ToolPaths tools, string platform)
    {
        Console.WriteLine($"\nAvailable codec groups and encoders for {platform}:");
        Console.WriteLine("==============================================");

        HardwareProbeResult? probe = null;
        if (!string.IsNullOrEmpty(tools.Ffmpeg))
        {
            probe = await HardwareProbe.ProbeCapabilitiesAsync(tools.Ffmpeg, presetDb);
        }

        // Print common groups
        foreach (var (groupName, group) in presetDb.Selection.Common)
        {
            if (!group.Enabled) continue;
            Console.WriteLine($"{groupName}:");
            foreach (var (encName, enc) in group.Encoders)
            {
                if (!enc.Enabled) continue;
                string? final = enc.FinalCodec ?? enc.ExecutionCodec;
                bool isAvail = probe == null || final == "copy" || final == "mux" || final == "m4v" || probe.SupportedEncoders.Contains(final ?? "");
                if (isAvail)
                {
                    Console.WriteLine($"  {encName} ({final})");
                }
            }
        }

        // Print platform groups
        if (presetDb.Selection.Platforms.TryGetValue(platform, out var platSel) && platSel.HwaccelEnabled)
        {
            foreach (var (groupName, group) in platSel.Groups)
            {
                if (!group.Enabled) continue;
                Console.WriteLine($"{groupName}:");
                foreach (var (encName, enc) in group.Encoders)
                {
                    if (!enc.Enabled) continue;
                    string? final = enc.FinalCodec ?? enc.ExecutionCodec;
                    bool isAvail = probe == null || probe.SupportedEncoders.Contains(final ?? "");
                    if (isAvail)
                    {
                        Console.WriteLine($"  {encName} ({final})");
                    }
                }
            }
        }
    }
}

