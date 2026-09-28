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

        if (args.Length == 0)
        {
            ShowHelp();
            return 0;
        }

        var opts = new ConvertOptions();
        var files = new List<string>();

        // Custom, simple, high-signal command-line parser
        for (int i = 0; i < args.Length; i++)
        {
            string arg = args[i];

            if (arg == "-h" || arg == "--help" || arg == "/?")
            {
                ShowHelp();
                return 0;
            }
            else if (arg == "--codecs-list")
            {
                await ShowCodecsListAsync();
                return 0;
            }
            else if (arg == "-c" || arg == "--codec")
            {
                if (i + 1 < args.Length) opts.Codec = args[++i];
            }
            else if (arg == "-p" || arg == "--preset" || arg == "--profile") // profile is legacy alias
            {
                if (i + 1 < args.Length) opts.Preset = args[++i];
            }
            else if (arg == "-o" || arg == "--output")
            {
                if (i + 1 < args.Length) opts.OutputDir = args[++i];
            }
            else if (arg == "--audio-norm")
            {
                if (i + 1 < args.Length) opts.AudioNorm = args[++i];
            }
            else if (arg == "--audio-mode")
            {
                if (i + 1 < args.Length) opts.AudioOutputMode = args[++i];
            }
            else if (arg == "--overwrite")
            {
                opts.Overwrite = true;
            }
            else if (arg == "--dry-run")
            {
                opts.DryRun = true;
            }
            else if (arg.StartsWith("-"))
            {
                Console.Error.WriteLine($"Unknown option: {arg}");
                return 1;
            }
            else
            {
                files.Add(arg);
            }
        }

        if (files.Count == 0)
        {
            Console.Error.WriteLine("Error: No input files specified.");
            return 1;
        }

        var tools = ToolDiscovery.ResolveAll();
        if (string.IsNullOrEmpty(tools.Ffmpeg))
        {
            Console.Error.WriteLine("Error: ffmpeg could not be resolved! Place it in the path or set FFMPEG_BIN.");
            return 1;
        }

        Console.WriteLine($"[C# Converter] Starting conversion session using ffmpeg at: {tools.Ffmpeg}");

        var converter = new Core.Engine.Converter();
        var cts = new CancellationTokenSource();

        // Handle Ctrl+C gracefully
        Console.CancelKeyPress += (s, e) =>
        {
            Console.WriteLine("\n[C# Converter] Cancellation requested. Shutting down...");
            e.Cancel = true;
            cts.Cancel();
        };

        // Hook up progress events for live feedback
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

        var result = await converter.ProcessFilesAsync(files, opts, cts.Token);

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

    static void ShowHelp()
    {
        Console.WriteLine("=========================================================================");
        Console.WriteLine(" FFMpeg Converter Port (.NET Experiment CLI)");
        Console.WriteLine("=========================================================================");
        Console.WriteLine("Usage: dotnet run --project FfmpegConverter.Cli.csproj [options] [files...]");
        Console.WriteLine();
        Console.WriteLine("Options:");
        Console.WriteLine("  -h, --help           Show this help information");
        Console.WriteLine("  --codecs-list        List detected and available hardware encoders & presets");
        Console.WriteLine("  -c, --codec <codec>  Target video codec/encoder (e.g., copy, prores, h264_vaapi)");
        Console.WriteLine("  -p, --preset <pres>  Target encoder preset (e.g., lt, standard, fast, quality)");
        Console.WriteLine("  -o, --output <dir>   Custom output directory");
        Console.WriteLine("  --audio-norm <mode>  Audio normalisation: none, peak_norm, peak_norm_2pass, loudness_norm_2pass");
        Console.WriteLine("  --audio-mode <mode>  Audio output: pcm, fdk_aac_320, fdk_aac_320_ac3_640");
        Console.WriteLine("  --overwrite          Overwrite existing destination files");
        Console.WriteLine("  --dry-run            Construct command lines and print without running ffmpeg");
    }

    static async Task ShowCodecsListAsync()
    {
        var tools = ToolDiscovery.ResolveAll();
        var presetDb = PresetDb.Load();
        
        string platform = RuntimeInformation.IsOSPlatform(OSPlatform.Windows) ? "windows" :
                         RuntimeInformation.IsOSPlatform(OSPlatform.OSX) ? "macos" : "linux";

        Console.WriteLine($"Platform detected: {platform}");
        Console.WriteLine($"Presets database path: {presetDb.LoadedPath} (v{presetDb.Version})");
        Console.WriteLine();
        Console.WriteLine("Dynamic Codecs Catalog:");
        Console.WriteLine("-----------------------------------------------------------------");
        
        foreach (var codec in presetDb.GetCodecs(platform))
        {
            Console.WriteLine($"Codec: {codec}");
            Console.Write("  Presets: ");
            Console.WriteLine(string.Join(", ", presetDb.GetPresets(platform, codec)));
        }

        if (!string.IsNullOrEmpty(tools.Ffmpeg))
        {
            Console.WriteLine();
            Console.WriteLine("Running hardware capabilities probe...");
            var probe = await HardwareProbe.ProbeCapabilitiesAsync(tools.Ffmpeg, presetDb);
            
            Console.WriteLine("\nProbed Hardware Encoders Support Status:");
            Console.WriteLine("-----------------------------------------------------------------");
            foreach (var codec in presetDb.GetCodecs(platform))
            {
                if (codec == "copy" || codec == "mux" || codec == "m4v") continue;
                bool isSupported = probe.SupportedEncoders.Contains(codec);
                string status = isSupported ? "AVAILABLE" : "NOT_SUPPORTED";
                Console.WriteLine($"  {codec,-25} : {status}");
            }

            Console.WriteLine("\nDetected Vulkan GPUs / Adapters (Excluding Soft-Vulkan):");
            Console.WriteLine("-----------------------------------------------------------------");
            if (probe.VulkanDevices.Count > 0)
            {
                foreach (var d in probe.VulkanDevices)
                {
                    Console.WriteLine($"  * {d}");
                }
            }
            else
            {
                Console.WriteLine("  No Vulkan GPUs found.");
            }
        }
        else
        {
            Console.WriteLine("\n[Warning] ffmpeg not found. Hardware probing skipped.");
        }
    }
}

