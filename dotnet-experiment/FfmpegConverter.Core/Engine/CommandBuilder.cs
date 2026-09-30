using System;
using System.IO;
using System.Text;
using FfmpegConverter.Core.Models;
using FfmpegConverter.Core.Presets;
using FfmpegConverter.Core.Probing;

namespace FfmpegConverter.Core.Engine;

public static class CommandBuilder
{
    public static int CalcVideoToolboxBitrateKbps(int width, int height, double fps, string preset, bool isHevc)
    {
        if (width <= 0 || height <= 0)
        {
            width = 1920;
            height = 1080;
        }
        if (fps <= 0.0) fps = 30.0;

        double bpp;
        if (isHevc)
        {
            bpp = preset.ToLowerInvariant() switch
            {
                "low" => 0.030,
                "high" => 0.075,
                _ => 0.050 // medium or default
            };
        }
        else // h264
        {
            bpp = preset.ToLowerInvariant() switch
            {
                "low" => 0.050,
                "high" => 0.120,
                _ => 0.080 // medium or default
            };
        }

        double bps = (double)width * (double)height * fps * bpp;
        int kbps = (int)(bps / 1000.0);
        return kbps > 0 ? kbps : 1;
    }

    public static string BuildAudioFilter(ConvertOptions opts)
    {
        string soxr = "aresample=resampler=soxr:precision=28:cheby=1";
        string norm = opts.AudioNorm.ToLowerInvariant();

        return norm switch
        {
            "peak" or "peak_norm" =>
                $"{soxr},volume=-3dB",
            "peak2" or "peak_norm_2pass" =>
                $"{soxr},volume={opts.Gain:F2}dB",
            "loudnorm" or "loudness_norm" =>
                $"{soxr},loudnorm=I=-11:TP=-1.5:LRA=7",
            "loudnorm2" or "loudness_norm_2pass" =>
                $"{soxr},loudnorm=I={opts.ITarget:F1}:TP={opts.TpTarget:F1}:LRA={opts.LraTarget:F1}:" +
                $"measured_I={opts.MeasuredI:F2}:measured_TP={opts.MeasuredTp:F2}:measured_LRA={opts.MeasuredLra:F2}:" +
                $"measured_thresh={opts.MeasuredThresh:F2}:offset={opts.MeasuredOffset:F2}:linear=true",
            _ => soxr
        };
    }

    public static string BuildOutputFilePath(string inputPath, ConvertOptions opts, string platform, PresetDb presetDb)
    {
        string baseDir = string.IsNullOrEmpty(opts.OutputDir)
            ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "ffmpeg_converter")
            : opts.OutputDir;

        string baseName = Path.GetFileNameWithoutExtension(inputPath);
        string ext;

        string codec = opts.Codec.ToLowerInvariant();
        string preset = opts.Preset.ToLowerInvariant();

        if (codec == "copy")
        {
            ext = "mkv";
        }
        else if (codec == "mux")
        {
            ext = preset switch
            {
                "mov" => "mov",
                "m4v" => "m4v",
                _ => "mkv"
            };
        }
        else if (codec == "m4v")
        {
            ext = "m4v";
        }
        else if (codec == "hevc_videotoolbox" || codec == "h264_videotoolbox")
        {
            ext = "mp4";
        }
        else if (codec == "prores" || codec == "prores_ks" || codec == "prores_videotoolbox" || codec == "prores_ks_vulkan")
        {
            ext = "mov";
        }
        else
        {
            var pInfo = presetDb.GetPresetInfo(platform, opts.Codec, opts.Preset);
            ext = !string.IsNullOrEmpty(pInfo?.Container) ? pInfo.Container : "mkv";
        }

        return Path.Combine(baseDir, $"{baseName}_converted.{ext}");
    }

    public static string BuildFfmpegArgs(
        string inputPath,
        string outputPath,
        ConvertOptions opts,
        string platform,
        PresetDb presetDb,
        InputVideoInfo? videoInfo,
        HardwareProbeResult? probeResult)
    {
        var sb = new StringBuilder();
        sb.Append("-hide_banner ");

        // Overwrite flag
        sb.Append(opts.Overwrite ? "-y " : "-n ");

        string codec = opts.Codec.ToLowerInvariant();
        bool is10Bit = codec.EndsWith("_10bit") || (videoInfo != null && videoInfo.BitDepth == 10);
        bool isVaapi = codec.Contains("vaapi");
        bool isVulkan = codec.Contains("vulkan");
        bool isQsv = codec.Contains("qsv");
        bool isNvenc = codec.Contains("nvenc");
        bool isAmf = codec.Contains("amf");
        bool isHwOutput = isVaapi || isVulkan || isQsv || isNvenc || isAmf;

        // VideoToolbox bitrate calculation if applicable
        int vtBitrate = 0;
        if (codec == "hevc_videotoolbox" || codec == "h264_videotoolbox")
        {
            int w = videoInfo?.Width ?? 1920;
            int h = videoInfo?.Height ?? 1080;
            double fps = (videoInfo != null && videoInfo.Fps > 0) ? videoInfo.Fps : 30.0;
            bool isHevc = codec == "hevc_videotoolbox";
            vtBitrate = CalcVideoToolboxBitrateKbps(w, h, fps, opts.Preset, isHevc);
            opts.HevcVtBitrateKbps = vtBitrate;
        }

        // Look up preset info
        var presetInfo = presetDb.GetPresetInfo(platform, opts.Codec, opts.Preset);

        // 1. Pre-input hardware device args
        if (presetInfo != null && !string.IsNullOrEmpty(presetInfo.PreInputArgs))
        {
            string preInput = PresetDb.SubstitutePlaceholders(
                presetInfo.PreInputArgs,
                vaapiDevice: !string.IsNullOrEmpty(opts.HwDevice) ? opts.HwDevice : null,
                vkDevice: opts.VulkanDevice,
                vtBitrate: vtBitrate
            );
            sb.Append($"{preInput} ");
        }
        else if (isVaapi && !string.IsNullOrEmpty(opts.HwDevice))
        {
            sb.Append($"-vaapi_device \"{opts.HwDevice}\" ");
        }

        // 2. Input decoder configuration (including AV1 / libdav1d bypass)
        bool inputIsAv1 = videoInfo != null && videoInfo.CodecName.Equals("av1", StringComparison.OrdinalIgnoreCase);
        bool hasLibdav1d = probeResult?.HasLibdav1dDec ?? false;

        if (inputIsAv1 && isHwOutput && hasLibdav1d)
        {
            sb.Append("-hwaccel none -c:v libdav1d ");
        }
        else if (inputIsAv1 && hasLibdav1d)
        {
            sb.Append("-hwaccel none -c:v libdav1d ");
        }
        else
        {
            sb.Append("-hwaccel none ");
        }

        // 3. Input file
        sb.Append($"-i \"{inputPath}\" ");

        // 4. Stream mappings
        sb.Append("-map 0:v:0 ");

        bool isDualAudio = opts.AudioOutputMode.Equals("fdk_aac_320_ac3_640", StringComparison.OrdinalIgnoreCase);
        string audioFilter = BuildAudioFilter(opts);

        if (isDualAudio)
        {
            sb.Append($"-filter_complex \"[0:a:0]{audioFilter},asplit=2[aout0][aout1]\" -map [aout0] -map [aout1] ");
        }
        else
        {
            sb.Append("-map 0:a:0 ");
        }

        sb.Append("-map_metadata 0 ");

        // 5. Color metadata
        if (videoInfo != null)
        {
            if (!string.IsNullOrEmpty(videoInfo.ColorRange))
                sb.Append($"-color_range {videoInfo.ColorRange} ");
            if (!string.IsNullOrEmpty(videoInfo.ColorPrimaries))
                sb.Append($"-color_primaries {videoInfo.ColorPrimaries} ");
            if (!string.IsNullOrEmpty(videoInfo.ColorTransfer))
                sb.Append($"-color_trc {videoInfo.ColorTransfer} ");
            if (!string.IsNullOrEmpty(videoInfo.ColorSpace))
                sb.Append($"-colorspace {videoInfo.ColorSpace} ");
        }

        // 6. Video codec & preset flags
        if (platform == "macos" && (codec == "hevc_videotoolbox" || codec == "h264_videotoolbox"))
        {
            string vtPreset = opts.Preset.ToLowerInvariant();
            if (codec == "hevc_videotoolbox")
            {
                if (vtPreset == "default")
                {
                    sb.Append("-c:v hevc_videotoolbox -tag:v hvc1 -spatial_aq 1 ");
                }
                else
                {
                    sb.Append($"-c:v hevc_videotoolbox -b:v {vtBitrate}k -tag:v hvc1 -spatial_aq 1 ");
                }
            }
            else // h264_videotoolbox
            {
                if (vtPreset == "default")
                {
                    sb.Append("-c:v h264_videotoolbox -spatial_aq 1 ");
                }
                else
                {
                    sb.Append($"-c:v h264_videotoolbox -b:v {vtBitrate}k -spatial_aq 1 ");
                }
            }
        }
        else if (presetInfo != null && !string.IsNullOrEmpty(presetInfo.FfmpegArgs))
        {
            string codecArgs = PresetDb.SubstitutePlaceholders(
                presetInfo.FfmpegArgs,
                vaapiDevice: !string.IsNullOrEmpty(opts.HwDevice) ? opts.HwDevice : null,
                vkDevice: opts.VulkanDevice,
                vtBitrate: vtBitrate
            );
            sb.Append($"{codecArgs} ");

            if (is10Bit && codec.Contains("hevc"))
            {
                sb.Append("-profile:v main10 ");
            }
        }
        else if (codec == "copy")
        {
            sb.Append("-c:v copy ");
        }
        else if (codec == "prores" || codec == "prores_ks")
        {
            string prof = opts.Preset.ToLowerInvariant() switch
            {
                "lt" => "lt",
                "hq" => "hq",
                "4444" => "4444",
                _ => "standard"
            };

            if (codec == "prores_ks")
            {
                sb.Append($"-c:v prores_ks -profile:v {prof} ");
            }
            else
            {
                int pNum = prof switch { "lt" => 1, "hq" => 3, "4444" => 4, _ => 2 };
                sb.Append($"-c:v prores -profile:v {pNum} ");
            }
        }
        else
        {
            sb.Append($"-c:v {opts.Codec} ");
            if (!string.IsNullOrEmpty(opts.Preset) && !opts.Preset.Equals("default", StringComparison.OrdinalIgnoreCase))
            {
                sb.Append($"-preset {opts.Preset} ");
            }
        }

        // 7. Video filters (Deblock vs HW Upload filters)
        if (!isHwOutput && platform != "macos")
        {
            if (opts.Deblock == 2)
            {
                sb.Append("-vf \"deblock=filter=weak:block=4:planes=1\" ");
            }
            else if (opts.Deblock == 3)
            {
                sb.Append("-vf \"deblock=filter=strong:block=4:alpha=0.12:beta=0.07:gamma=0.06:delta=0.05:planes=1\" ");
            }
        }
        else if (isHwOutput)
        {
            string hwVf = presetInfo?.VideoFilter ?? "";
            if (!string.IsNullOrEmpty(hwVf))
            {
                sb.Append($"-vf \"{hwVf}\" ");
            }
            else if (isVaapi || isVulkan)
            {
                string format = is10Bit ? "p010le,hwupload" : "nv12,hwupload";
                sb.Append($"-vf \"format={format}\" ");
            }
            else if ((isQsv || isNvenc) && is10Bit)
            {
                sb.Append("-vf \"format=p010le\" ");
            }
        }

        // 8. Audio encoding selection
        bool hasAacAt = probeResult?.HasAacAt ?? false;
        bool hasLibfdk = probeResult?.HasLibfdkAac ?? true; // base repo expects libfdk_aac

        string aacEncoder = hasAacAt ? "aac_at" : (hasLibfdk ? "libfdk_aac" : "aac");

        if (isDualAudio)
        {
            sb.Append($"-c:a:0 {aacEncoder} -b:a:0 320k -ar:a:0 48000 -c:a:1 ac3 -b:a:1 640k -ar:a:1 48000 ");
        }
        else if (opts.AudioOutputMode.Equals("fdk_aac_320", StringComparison.OrdinalIgnoreCase) ||
                 opts.AudioOutputMode.Equals("fdk_aac_320", StringComparison.OrdinalIgnoreCase))
        {
            sb.Append($"-c:a {aacEncoder} -b:a 320k -ar 48000 ");
        }
        else if (opts.AudioOutputMode.Equals("pcm", StringComparison.OrdinalIgnoreCase))
        {
            sb.Append("-c:a pcm_s16le -ar 48000 ");
        }
        else if (opts.UseAacForH265 || codec == "hevc_videotoolbox")
        {
            sb.Append($"-c:a {aacEncoder} -b:a 320k -ar 48000 ");
        }
        else
        {
            sb.Append("-c:a pcm_s16le -ar 48000 ");
        }

        // Audio filter for single-stream audio
        if (!isDualAudio)
        {
            sb.Append($"-af \"{audioFilter}\" ");
        }

        // 9. Progress & output
        sb.Append("-progress pipe:1 -nostats -nostdin ");
        sb.Append($"\"{outputPath}\"");

        return sb.ToString();
    }
}
