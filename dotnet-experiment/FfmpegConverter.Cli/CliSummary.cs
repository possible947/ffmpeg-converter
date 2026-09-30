using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using FfmpegConverter.Core.Models;
using FfmpegConverter.Core.Presets;

namespace FfmpegConverter.Cli;

public static class CliSummary
{
    public static string FormatSummary(
        ConvertOptions opts,
        IReadOnlyList<string> files,
        PresetDb presetDb,
        string platform)
    {
        var sb = new StringBuilder();

        string codecGroup = opts.SelectionGroup;
        string encoder = opts.SelectionEncoder;

        if (presetDb.FindSelectionForCodec(platform, opts.Codec, out string foundGroup, out string foundEncoder))
        {
            codecGroup = foundGroup;
            encoder = foundEncoder;
        }
        else if (opts.Codec.Equals("mux", StringComparison.OrdinalIgnoreCase))
        {
            codecGroup = "mux";
            encoder = !string.IsNullOrEmpty(opts.Preset) ? opts.Preset : "mkv";
        }
        else if (opts.Codec.Equals("copy", StringComparison.OrdinalIgnoreCase))
        {
            codecGroup = "copy";
            encoder = "copy";
        }
        else if (opts.Codec.Equals("m4v", StringComparison.OrdinalIgnoreCase))
        {
            codecGroup = "mux";
            encoder = "m4v";
        }

        string preset = !string.IsNullOrEmpty(opts.Preset) ? opts.Preset : "default";

        sb.AppendLine("=== Summary ===");
        sb.AppendLine($"Codec:        {codecGroup}");
        sb.AppendLine($"Encoder:      {encoder}");
        sb.AppendLine($"Preset:       {preset}");

        if (opts.Codec.Equals("m4v", StringComparison.OrdinalIgnoreCase))
        {
            sb.AppendLine("Deblock:      (m4v)");
        }
        else if (opts.Codec.Equals("mux", StringComparison.OrdinalIgnoreCase))
        {
            sb.AppendLine("Deblock:      (mux)");
        }
        else if (opts.Codec.Equals("prores", StringComparison.OrdinalIgnoreCase) ||
                 opts.Codec.Equals("prores_ks", StringComparison.OrdinalIgnoreCase))
        {
            string deblockStr = opts.Deblock switch
            {
                1 => "none",
                2 => "weak",
                3 => "strong",
                _ => "none"
            };
            sb.AppendLine($"Deblock:      {deblockStr}");
        }
        else
        {
            sb.AppendLine("Deblock:      (n/a)");
            if (opts.Codec.Contains("_vaapi", StringComparison.OrdinalIgnoreCase))
            {
                sb.AppendLine($"HW device:    {(!string.IsNullOrEmpty(opts.HwDevice) ? opts.HwDevice : "(auto)")}");
            }
        }

        sb.AppendLine($"Audio norm:   {opts.AudioNorm}");
        sb.AppendLine($"Audio out:    {(!string.IsNullOrEmpty(opts.AudioOutputMode) ? opts.AudioOutputMode : "pcm")}");

        if (opts.Codec.Equals("mux", StringComparison.OrdinalIgnoreCase))
        {
            sb.AppendLine($"Video track:  {(!string.IsNullOrEmpty(opts.VideoTrackPath) ? opts.VideoTrackPath : "(missing)")}");
        }

        if (opts.Codec.Equals("m4v", StringComparison.OrdinalIgnoreCase))
        {
            sb.AppendLine($"M4V video idx:{opts.M4vVideoTrackIndex}");
            sb.AppendLine($"M4V audio idx:{opts.M4vAudioTrackIndex}");
            sb.AppendLine("M4V AAC:      CBR 320k (libfdk_aac)");
            sb.AppendLine($"M4V AC3 kbps: {opts.M4vAc3BitrateKbps}");
            sb.AppendLine($"M4V lang:     {(!string.IsNullOrEmpty(opts.M4vAudioLang) ? opts.M4vAudioLang : "rus")}");
            sb.AppendLine($"M4V chapters: {(opts.M4vAddChapters ? "yes" : "no")}");
        }

        if (opts.AudioNorm.Equals("loudness_norm_2pass", StringComparison.OrdinalIgnoreCase))
        {
            string genreStr = opts.Genre switch
            {
                1 => "edm",
                2 => "rock",
                3 => "hiphop",
                4 => "classical",
                5 => "podcast",
                _ => "none"
            };
            sb.AppendLine($"Genre:        {genreStr}");
        }

        sb.AppendLine($"Overwrite:    {(opts.Overwrite ? "yes" : "no")}");
        sb.AppendLine($"Dry run:      {(opts.DryRun ? "yes" : "no")}");

        if (!string.IsNullOrEmpty(opts.OutputDir))
        {
            sb.AppendLine($"Output dir:   {opts.OutputDir}");
            if (opts.OutputDirStatus != 0)
            {
                sb.AppendLine("Dir status:   OK");
            }
            else
            {
                sb.AppendLine("Dir status:   ERROR (directory missing or not writable)");
            }
        }
        else
        {
            sb.AppendLine("Output dir:   (same as input)");
        }

        sb.AppendLine();
        sb.AppendLine($"Files ({files.Count}):");
        foreach (var file in files)
        {
            if (file.Contains(' '))
            {
                sb.AppendLine($"  \"{file}\"");
            }
            else
            {
                sb.AppendLine($"  {file}");
            }
        }
        sb.AppendLine("===============");

        return sb.ToString();
    }

    public static void PrintSummary(
        ConvertOptions opts,
        IReadOnlyList<string> files,
        PresetDb presetDb,
        string platform)
    {
        Console.Write(FormatSummary(opts, files, presetDb, platform));
    }
}
