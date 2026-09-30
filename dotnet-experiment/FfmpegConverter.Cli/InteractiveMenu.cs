using System;
using System.Collections.Generic;
using System.IO;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using FfmpegConverter.Core.Models;
using FfmpegConverter.Core.Presets;
using FfmpegConverter.Core.Probing;

namespace FfmpegConverter.Cli;

public class InteractiveMenuResult
{
    public ConvertOptions Options { get; set; } = new();
    public List<string> Files { get; set; } = new();
}

public static class InteractiveMenu
{
    public static async Task<InteractiveMenuResult?> RunMenuAsync(
        PresetDb presetDb,
        string platform,
        ToolPaths tools,
        HardwareProbeResult? probe = null,
        TextReader? inputReader = null,
        TextWriter? outputWriter = null)
    {
        var reader = inputReader ?? Console.In;
        var writer = outputWriter ?? Console.Out;

        string group = "software";
        string encoder = "prores_ks";
        string finalCodec = "prores_ks";
        string preset = "standard";
        int deblock = 1; // 1=none, 2=weak, 3=strong
        string audioNorm = "none";
        string audioOutput = "pcm";
        int genre = 1; // 1=edm
        bool overwrite = false;
        string outputDir = "";
        var files = new List<string>();
        string videoTrackPath = "";

        // M4V options
        int m4vAc3BitrateKbps = 640;
        string m4vAudioLang = "rus";
        bool m4vAddChapters = true;
        int m4vVideoTrack = 0;
        int m4vAudioTrack = 0;

        int step = 1;
        while (step != 12)
        {
            switch (step)
            {
                // ---- Step 1: Codec group & encoder ----
                case 1:
                {
                    writer.WriteLine("----ffmpeg_converter_simple_gui----\n");
                    writer.Write("Codec group (software, mux, or hardware group; default software): ");
                    string? gLine = await reader.ReadLineAsync();
                    if (gLine == null) return null;
                    gLine = gLine.Trim();
                    if (gLine.Equals("c", StringComparison.OrdinalIgnoreCase)) return null;
                    if (!string.IsNullOrEmpty(gLine)) group = gLine;

                    writer.Write("Encoder (default prores_ks): ");
                    string? eLine = await reader.ReadLineAsync();
                    if (eLine == null) return null;
                    eLine = eLine.Trim();
                    if (eLine.Equals("c", StringComparison.OrdinalIgnoreCase)) return null;
                    if (!string.IsNullOrEmpty(eLine)) encoder = eLine;

                    if (!presetDb.ResolveSelection(platform, group, encoder, out finalCodec))
                    {
                        if (presetDb.Platforms.TryGetValue(platform, out var direct) && direct.ContainsKey(group))
                        {
                            finalCodec = group;
                        }
                        else
                        {
                            writer.WriteLine($"Unavailable codec group/encoder: {group}/{encoder}");
                            continue;
                        }
                    }

                    if (finalCodec.Contains("prores", StringComparison.OrdinalIgnoreCase))
                    {
                        preset = "standard";
                    }
                    else if (finalCodec.Equals("mux", StringComparison.OrdinalIgnoreCase))
                    {
                        preset = encoder.Equals("mov", StringComparison.OrdinalIgnoreCase) || encoder.Equals("m4v", StringComparison.OrdinalIgnoreCase)
                            ? encoder.ToLowerInvariant()
                            : "mkv";
                    }
                    else
                    {
                        preset = "default";
                    }

                    if (finalCodec.Equals("m4v", StringComparison.OrdinalIgnoreCase))
                    {
                        step = 7; // Go to overwrite, m4v steps will be collected after files
                    }
                    else
                    {
                        var availablePresets = new List<string>(presetDb.GetPresets(platform, finalCodec));
                        bool needsProfile = availablePresets.Count > 1 || finalCodec.Contains("prores", StringComparison.OrdinalIgnoreCase);
                        step = needsProfile ? 2 : 4;
                    }
                    break;
                }

                // ---- Step 2: Profile / Preset ----
                case 2:
                {
                    var presets = new List<string>(presetDb.GetPresets(platform, finalCodec));
                    writer.WriteLine("\n----ffmpeg_converter_simple_gui----");
                    writer.WriteLine($"select profile for {finalCodec}");
                    writer.WriteLine("-----------------------");
                    if (presets.Count > 0)
                    {
                        for (int j = 0; j < presets.Count; j++)
                        {
                            string isDef = (presets[j].Equals(preset, StringComparison.OrdinalIgnoreCase) || (j == 0 && string.IsNullOrEmpty(preset))) ? " (default)" : "";
                            writer.WriteLine($"  {j + 1}. {presets[j]}{isDef}");
                        }
                    }
                    else
                    {
                        writer.WriteLine($"  1. {preset} (default)");
                    }
                    writer.WriteLine("-----------------------");
                    writer.Write("select: number->choice,Enter->(default),c->cancel,b->back\n> ");
                    string? ch = await reader.ReadLineAsync();
                    if (ch == null) return null;
                    ch = ch.Trim();

                    if (ch.Equals("c", StringComparison.OrdinalIgnoreCase)) return null;
                    if (ch.Equals("b", StringComparison.OrdinalIgnoreCase)) { step = 1; break; }

                    if (string.IsNullOrEmpty(ch))
                    {
                        // Use default
                    }
                    else if (int.TryParse(ch, out int idx) && idx >= 1 && idx <= presets.Count)
                    {
                        preset = presets[idx - 1];
                    }
                    else if (presets.Contains(ch, StringComparer.OrdinalIgnoreCase))
                    {
                        preset = ch;
                    }
                    else
                    {
                        writer.WriteLine("Invalid choice");
                        break;
                    }

                    bool needsDeblock = finalCodec.Equals("prores", StringComparison.OrdinalIgnoreCase) ||
                                       finalCodec.Equals("prores_ks", StringComparison.OrdinalIgnoreCase);
                    step = needsDeblock ? 3 : 4;
                    break;
                }

                // ---- Step 3: Deblock ----
                case 3:
                {
                    writer.WriteLine("\n----ffmpeg_converter_simple_gui----");
                    writer.WriteLine("select deblock");
                    writer.WriteLine("---------------------------");
                    writer.WriteLine("  1. none (default)");
                    writer.WriteLine("  2. weak (4K content)");
                    writer.WriteLine("  3. strong (1080p content)");
                    writer.WriteLine("---------------------------");
                    writer.Write("select: number->choice,Enter->(default),c->cancel,b->back\n> ");
                    string? ch = await reader.ReadLineAsync();
                    if (ch == null) return null;
                    ch = ch.Trim();

                    if (ch.Equals("c", StringComparison.OrdinalIgnoreCase)) return null;
                    if (ch.Equals("b", StringComparison.OrdinalIgnoreCase)) { step = 2; break; }

                    if (string.IsNullOrEmpty(ch) || ch == "1") { deblock = 1; step = 4; }
                    else if (ch == "2") { deblock = 2; step = 4; }
                    else if (ch == "3") { deblock = 3; step = 4; }
                    else { writer.WriteLine("Invalid choice"); }
                    break;
                }

                // ---- Step 4: Audio normalization ----
                case 4:
                {
                    writer.WriteLine("\n----ffmpeg_converter_simple_gui----");
                    writer.WriteLine("select audio normalization");
                    writer.WriteLine("---------------------------------");
                    writer.WriteLine("  1. none (default)");
                    writer.WriteLine("  2. peak");
                    writer.WriteLine("  3. peak 2-pass");
                    writer.WriteLine("  4. loudness normalization");
                    writer.WriteLine("  5. loudness normalization 2-pass");
                    writer.WriteLine("---------------------------------");
                    writer.Write("select: number->choice,Enter->(default),c->cancel,b->back\n> ");
                    string? ch = await reader.ReadLineAsync();
                    if (ch == null) return null;
                    ch = ch.Trim();

                    if (ch.Equals("c", StringComparison.OrdinalIgnoreCase)) return null;
                    if (ch.Equals("b", StringComparison.OrdinalIgnoreCase))
                    {
                        bool needsDeblock = finalCodec.Equals("prores", StringComparison.OrdinalIgnoreCase) ||
                                           finalCodec.Equals("prores_ks", StringComparison.OrdinalIgnoreCase);
                        step = needsDeblock ? 3 : 2;
                        break;
                    }

                    if (string.IsNullOrEmpty(ch) || ch == "1") { audioNorm = "none"; step = 6; }
                    else if (ch == "2") { audioNorm = "peak_norm"; step = 6; }
                    else if (ch == "3") { audioNorm = "peak_norm_2pass"; step = 6; }
                    else if (ch == "4") { audioNorm = "loudness_norm"; step = 6; }
                    else if (ch == "5") { audioNorm = "loudness_norm_2pass"; step = 5; }
                    else { writer.WriteLine("Invalid choice"); }
                    break;
                }

                // ---- Step 5: Genre (for loudness_norm_2pass) ----
                case 5:
                {
                    writer.WriteLine("\n----ffmpeg_converter_simple_gui----");
                    writer.WriteLine("select audio normalization genre");
                    writer.WriteLine("---------------------------------");
                    writer.WriteLine("  1. EDM (default)");
                    writer.WriteLine("  2. Rock");
                    writer.WriteLine("  3. HipHop");
                    writer.WriteLine("  4. Classical");
                    writer.WriteLine("  5. Podcast");
                    writer.WriteLine("---------------------------------");
                    writer.Write("select: number->choice,Enter->(default),c->cancel,b->back\n> ");
                    string? ch = await reader.ReadLineAsync();
                    if (ch == null) return null;
                    ch = ch.Trim();

                    if (ch.Equals("c", StringComparison.OrdinalIgnoreCase)) return null;
                    if (ch.Equals("b", StringComparison.OrdinalIgnoreCase)) { step = 4; break; }

                    if (string.IsNullOrEmpty(ch) || ch == "1") { genre = 1; step = 6; }
                    else if (ch == "2") { genre = 2; step = 6; }
                    else if (ch == "3") { genre = 3; step = 6; }
                    else if (ch == "4") { genre = 4; step = 6; }
                    else if (ch == "5") { genre = 5; step = 6; }
                    else { writer.WriteLine("Invalid choice"); }
                    break;
                }

                // ---- Step 6: Audio output ----
                case 6:
                {
                    writer.WriteLine("\n----ffmpeg_converter_simple_gui----");
                    writer.WriteLine("select audio output");
                    writer.WriteLine("----------------------------------");
                    writer.WriteLine("  1. pcm (default)");
                    writer.WriteLine("  2. fdk_aac_320");
                    writer.WriteLine("  3. fdk_aac_320_ac3_640");
                    writer.WriteLine("----------------------------------");
                    writer.Write("select: number->choice,Enter->(default),c->cancel,b->back\n> ");
                    string? ch = await reader.ReadLineAsync();
                    if (ch == null) return null;
                    ch = ch.Trim();

                    if (ch.Equals("c", StringComparison.OrdinalIgnoreCase)) return null;
                    if (ch.Equals("b", StringComparison.OrdinalIgnoreCase))
                    {
                        step = audioNorm.Equals("loudness_norm_2pass", StringComparison.OrdinalIgnoreCase) ? 5 : 4;
                        break;
                    }

                    if (string.IsNullOrEmpty(ch) || ch == "1") { audioOutput = "pcm"; step = 7; }
                    else if (ch == "2") { audioOutput = "fdk_aac_320"; step = 7; }
                    else if (ch == "3") { audioOutput = "fdk_aac_320_ac3_640"; step = 7; }
                    else { writer.WriteLine("Invalid choice"); }
                    break;
                }

                // ---- Step 7: Overwrite ----
                case 7:
                {
                    writer.WriteLine("\nchoice if overwrite files: yes/No");
                    writer.Write("select:y/n,Enter->(default),c->cancel,b->back\n> ");
                    string? ch = await reader.ReadLineAsync();
                    if (ch == null) return null;
                    ch = ch.Trim();

                    if (ch.Equals("c", StringComparison.OrdinalIgnoreCase)) return null;
                    if (ch.Equals("b", StringComparison.OrdinalIgnoreCase))
                    {
                        step = finalCodec.Equals("m4v", StringComparison.OrdinalIgnoreCase) ? 1 : 6;
                        break;
                    }

                    if (string.IsNullOrEmpty(ch) || ch.Equals("n", StringComparison.OrdinalIgnoreCase)) { overwrite = false; step = 8; }
                    else if (ch.Equals("y", StringComparison.OrdinalIgnoreCase)) { overwrite = true; step = 8; }
                    else { writer.WriteLine("Invalid choice"); }
                    break;
                }

                // ---- Step 8: Output directory ----
                case 8:
                {
                    writer.WriteLine("\n----ffmpeg_converter_simple_gui----\n");
                    writer.Write("output directory (default: HOME/ffmpeg_converter):\n> ");
                    string? line = await reader.ReadLineAsync();
                    if (line == null) return null;
                    line = CommandLineParser.CleanPath(line);

                    if (line.Equals("c", StringComparison.OrdinalIgnoreCase)) return null;
                    if (line.Equals("b", StringComparison.OrdinalIgnoreCase)) { step = 7; break; }

                    if (string.IsNullOrEmpty(line))
                    {
                        string home = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
                        outputDir = Path.Combine(home, "ffmpeg_converter");
                    }
                    else
                    {
                        outputDir = line;
                    }

                    try
                    {
                        Directory.CreateDirectory(outputDir);
                    }
                    catch (Exception ex)
                    {
                        writer.WriteLine($"Warning: cannot create output directory: {ex.Message}");
                    }

                    step = 9;
                    break;
                }

                // ---- Step 9: Input files ----
                case 9:
                {
                    writer.WriteLine("\n----ffmpeg_converter_simple_gui----\n");
                    writer.WriteLine("Enter file names (you can drag & drop files). Finish with empty line:");
                    files.Clear();

                    while (true)
                    {
                        writer.Write($"File {files.Count + 1}: ");
                        string? line = await reader.ReadLineAsync();
                        if (line == null) break;
                        line = line.Trim();

                        if (string.IsNullOrEmpty(line)) break;
                        if (line.Equals("c", StringComparison.OrdinalIgnoreCase)) return null;
                        if (line.Equals("b", StringComparison.OrdinalIgnoreCase) && files.Count == 0)
                        {
                            step = 8;
                            break;
                        }

                        // Check if multiple quoted files were pasted
                        var matches = Regex.Matches(line, @"[\""'].+?[\""']|[^ ]+");
                        if (matches.Count > 1)
                        {
                            foreach (Match m in matches)
                            {
                                string singlePath = CommandLineParser.CleanPath(m.Value);
                                if (!string.IsNullOrEmpty(singlePath))
                                {
                                    if (File.Exists(singlePath))
                                    {
                                        files.Add(singlePath);
                                        writer.WriteLine($"Added: {singlePath}");
                                    }
                                    else
                                    {
                                        writer.WriteLine($"File not found: {singlePath}");
                                    }
                                }
                            }
                        }
                        else
                        {
                            string singlePath = CommandLineParser.CleanPath(line);
                            if (File.Exists(singlePath))
                            {
                                files.Add(singlePath);
                                writer.WriteLine($"Added: {singlePath}");
                            }
                            else
                            {
                                writer.WriteLine($"File not found: {singlePath}");
                            }
                        }
                    }

                    if (step == 8) break; // Went back

                    if (files.Count == 0)
                    {
                        writer.WriteLine("No files added. Please add at least one file.");
                        continue;
                    }

                    if (finalCodec.Equals("mux", StringComparison.OrdinalIgnoreCase))
                    {
                        if (files.Count != 1)
                        {
                            writer.WriteLine("Mux mode requires exactly one source file.");
                            files.Clear();
                            continue;
                        }
                        step = 10;
                    }
                    else if (finalCodec.Equals("m4v", StringComparison.OrdinalIgnoreCase))
                    {
                        step = 14; // M4V options
                    }
                    else
                    {
                        step = 12; // Finalize
                    }
                    break;
                }

                // ---- Step 10: Mux video track ----
                case 10:
                {
                    writer.WriteLine("\n----ffmpeg_converter_simple_gui----\n");
                    writer.Write("video-track file for mux mode:\n> ");
                    string? line = await reader.ReadLineAsync();
                    if (line == null) return null;
                    line = CommandLineParser.CleanPath(line);

                    if (line.Equals("c", StringComparison.OrdinalIgnoreCase)) return null;
                    if (line.Equals("b", StringComparison.OrdinalIgnoreCase)) { step = 9; break; }

                    if (!File.Exists(line))
                    {
                        writer.WriteLine($"Invalid video-track file: {line}");
                        continue;
                    }

                    videoTrackPath = line;
                    step = 12; // Finalize
                    break;
                }

                // ---- Step 14: Apple M4V AC3 bitrate ----
                case 14:
                {
                    writer.WriteLine("\n----ffmpeg_converter_simple_gui----\n");
                    writer.WriteLine("Apple M4V: AC3 audio bitrate");
                    writer.WriteLine("------------------------------------------");
                    writer.WriteLine("  1. 384 kbps");
                    writer.WriteLine("  2. 448 kbps");
                    writer.WriteLine("  3. 640 kbps (default)");
                    writer.WriteLine("------------------------------------------");
                    writer.Write("select: number->choice,Enter->(default),c->cancel,b->back\n> ");
                    string? ch = await reader.ReadLineAsync();
                    if (ch == null) return null;
                    ch = ch.Trim();

                    if (ch.Equals("c", StringComparison.OrdinalIgnoreCase)) return null;
                    if (ch.Equals("b", StringComparison.OrdinalIgnoreCase)) { step = 9; break; }

                    if (string.IsNullOrEmpty(ch) || ch == "3") { m4vAc3BitrateKbps = 640; step = 15; }
                    else if (ch == "1") { m4vAc3BitrateKbps = 384; step = 15; }
                    else if (ch == "2") { m4vAc3BitrateKbps = 448; step = 15; }
                    else { writer.WriteLine("Invalid choice"); }
                    break;
                }

                // ---- Step 15: Apple M4V audio language ----
                case 15:
                {
                    writer.WriteLine("\n----ffmpeg_converter_simple_gui----\n");
                    writer.WriteLine("Apple M4V: audio language tag");
                    writer.WriteLine("  Examples: rus, eng, deu, fra, spa");
                    writer.Write("  Press Enter for default (rus), c to cancel, b to go back\n> ");
                    string? line = await reader.ReadLineAsync();
                    if (line == null) return null;
                    line = line.Trim();

                    if (line.Equals("c", StringComparison.OrdinalIgnoreCase)) return null;
                    if (line.Equals("b", StringComparison.OrdinalIgnoreCase)) { step = 14; break; }

                    if (!string.IsNullOrEmpty(line))
                    {
                        m4vAudioLang = line;
                    }
                    step = 16;
                    break;
                }

                // ---- Step 16: Apple M4V chapters ----
                case 16:
                {
                    writer.WriteLine("\n----ffmpeg_converter_simple_gui----\n");
                    writer.WriteLine("Apple M4V: embed chapter markers?");
                    writer.Write("  y/Enter=yes (default), n=no, c=cancel, b=back\n> ");
                    string? line = await reader.ReadLineAsync();
                    if (line == null) return null;
                    line = line.Trim();

                    if (line.Equals("c", StringComparison.OrdinalIgnoreCase)) return null;
                    if (line.Equals("b", StringComparison.OrdinalIgnoreCase)) { step = 15; break; }

                    m4vAddChapters = !line.Equals("n", StringComparison.OrdinalIgnoreCase);
                    step = 17;
                    break;
                }

                // ---- Step 17: Apple M4V stream indices ----
                case 17:
                {
                    writer.WriteLine("\n----ffmpeg_converter_simple_gui----\n");
                    writer.Write("Apple M4V: video stream index (0-based, default 0)\n> ");
                    string? vLine = await reader.ReadLineAsync();
                    if (vLine == null) return null;
                    vLine = vLine.Trim();

                    if (vLine.Equals("c", StringComparison.OrdinalIgnoreCase)) return null;
                    if (vLine.Equals("b", StringComparison.OrdinalIgnoreCase)) { step = 16; break; }

                    if (!string.IsNullOrEmpty(vLine) && int.TryParse(vLine, out int vi) && vi >= 0)
                    {
                        m4vVideoTrack = vi;
                    }

                    writer.Write("Apple M4V: audio stream index (0-based, default 0)\n> ");
                    string? aLine = await reader.ReadLineAsync();
                    if (aLine == null) return null;
                    aLine = aLine.Trim();

                    if (aLine.Equals("c", StringComparison.OrdinalIgnoreCase)) return null;
                    if (aLine.Equals("b", StringComparison.OrdinalIgnoreCase)) { step = 16; break; }

                    if (!string.IsNullOrEmpty(aLine) && int.TryParse(aLine, out int ai) && ai >= 0)
                    {
                        m4vAudioTrack = ai;
                    }

                    step = 12; // Finalize
                    break;
                }
            }
        }

        var opts = new ConvertOptions
        {
            SelectionGroup = group,
            SelectionEncoder = encoder,
            Codec = finalCodec,
            Preset = preset,
            Deblock = deblock,
            AudioNorm = audioNorm,
            AudioOutputMode = audioOutput,
            Genre = genre,
            Overwrite = overwrite,
            OutputDir = outputDir,
            VideoTrackPath = videoTrackPath,
            M4vAc3BitrateKbps = m4vAc3BitrateKbps,
            M4vAudioLang = m4vAudioLang,
            M4vAddChapters = m4vAddChapters,
            M4vVideoTrackIndex = m4vVideoTrack,
            M4vAudioTrackIndex = m4vAudioTrack
        };

        if (opts.AudioNorm.Equals("loudness_norm_2pass", StringComparison.OrdinalIgnoreCase))
        {
            opts.ApplyGenreTargets();
        }

        return new InteractiveMenuResult
        {
            Options = opts,
            Files = files
        };
    }
}
