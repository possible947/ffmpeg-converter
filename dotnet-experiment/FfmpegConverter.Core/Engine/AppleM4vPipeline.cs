using System;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using FfmpegConverter.Core.Models;
using FfmpegConverter.Core.Probing;

namespace FfmpegConverter.Core.Engine;

public class AppleM4vPipeline
{
    public event EventHandler<string>? StageChanged;
    public event EventHandler<string>? MessageLogged;
    public event EventHandler<(string Text, ConverterError Code)>? ErrorOccurred;

    public static bool IsSupportedVideoCodec(string? codecName)
    {
        if (string.IsNullOrEmpty(codecName)) return false;
        string c = codecName.ToLowerInvariant();
        return c == "h264" || c == "hevc" || c == "prores" || c == "prores_ks";
    }

    public static async Task<(bool IsSupported, string VideoCodec, string? ErrorDetail)> ValidateInputSupportedAsync(
        string ffprobePath,
        string inputFile,
        CancellationToken cancellationToken = default)
    {
        if (!File.Exists(inputFile))
            return (false, "", "Input file not found or unreadable");

        if (string.IsNullOrEmpty(ffprobePath) || !File.Exists(ffprobePath))
            return (false, "", "ffprobe binary not found");

        // 1. Probe video codec
        string vArgs = $"-v error -select_streams v:0 -show_entries stream=codec_name -of default=noprint_wrappers=1:nokey=1 \"{inputFile}\"";
        string vOutput;
        try
        {
            vOutput = await RunCaptureOutputAsync(ffprobePath, vArgs, cancellationToken);
        }
        catch (Exception ex)
        {
            return (false, "", $"Failed to probe video codec: {ex.Message}");
        }

        string videoCodec = vOutput.Trim().ToLowerInvariant();
        if (!IsSupportedVideoCodec(videoCodec))
        {
            return (false, videoCodec, $"Unsupported video codec for M4V: {(string.IsNullOrEmpty(videoCodec) ? "unknown" : videoCodec)}");
        }

        // 2. Check for usable audio stream
        string aArgs = $"-v error -select_streams a:0 -show_entries stream=codec_type -of default=noprint_wrappers=1:nokey=1 \"{inputFile}\"";
        string aOutput;
        try
        {
            aOutput = await RunCaptureOutputAsync(ffprobePath, aArgs, cancellationToken);
        }
        catch (Exception ex)
        {
            return (false, videoCodec, $"Failed to probe audio stream: {ex.Message}");
        }

        if (!aOutput.Contains("audio", StringComparison.OrdinalIgnoreCase))
        {
            return (false, videoCodec, "Input file has no usable audio stream");
        }

        return (true, videoCodec, null);
    }

    public static async Task<double> ProbeFpsAsync(string ffprobePath, string inputFile, CancellationToken cancellationToken = default)
    {
        string args = $"-v error -select_streams v:0 -show_entries stream=avg_frame_rate -of default=noprint_wrappers=1:nokey=1 \"{inputFile}\"";
        try
        {
            string outStr = await RunCaptureOutputAsync(ffprobePath, args, cancellationToken);
            outStr = outStr.Trim();
            if (!string.IsNullOrEmpty(outStr) && outStr != "0/0")
            {
                var parts = outStr.Split('/');
                if (parts.Length == 2 &&
                    double.TryParse(parts[0], CultureInfo.InvariantCulture, out double n) &&
                    double.TryParse(parts[1], CultureInfo.InvariantCulture, out double d) &&
                    d > 0)
                {
                    return n / d;
                }
                if (double.TryParse(outStr, CultureInfo.InvariantCulture, out double direct))
                {
                    return direct;
                }
            }

            // Fallback to r_frame_rate
            string rArgs = $"-v error -select_streams v:0 -show_entries stream=r_frame_rate -of default=noprint_wrappers=1:nokey=1 \"{inputFile}\"";
            string rOut = await RunCaptureOutputAsync(ffprobePath, rArgs, cancellationToken);
            rOut = rOut.Trim();
            if (!string.IsNullOrEmpty(rOut) && rOut != "0/0")
            {
                var parts = rOut.Split('/');
                if (parts.Length == 2 &&
                    double.TryParse(parts[0], CultureInfo.InvariantCulture, out double n) &&
                    double.TryParse(parts[1], CultureInfo.InvariantCulture, out double d) &&
                    d > 0)
                {
                    return n / d;
                }
            }
        }
        catch { }

        return 25.0;
    }

    public async Task<ConverterError> CreateM4vAsync(
        string inputFile,
        string outputFile,
        ConvertOptions opts,
        ToolPaths tools,
        HardwareProbeResult? probeResult,
        CancellationToken cancellationToken = default)
    {
        if (string.IsNullOrEmpty(tools.Ffmpeg) || !File.Exists(tools.Ffmpeg) ||
            string.IsNullOrEmpty(tools.Ffprobe) || !File.Exists(tools.Ffprobe) ||
            string.IsNullOrEmpty(tools.Mp4Box) || !File.Exists(tools.Mp4Box))
        {
            EmitError("Missing required tools (ffmpeg/ffprobe/MP4Box)", ConverterError.InvalidOptions);
            return ConverterError.InvalidOptions;
        }

        var validation = await ValidateInputSupportedAsync(tools.Ffprobe, inputFile, cancellationToken);
        if (!validation.IsSupported)
        {
            EmitError(validation.ErrorDetail ?? "Validation failed", ConverterError.InvalidOptions);
            return ConverterError.InvalidOptions;
        }

        string videoCodec = validation.VideoCodec;
        string? outputDir = Path.GetDirectoryName(Path.GetFullPath(outputFile));
        if (string.IsNullOrEmpty(outputDir)) outputDir = ".";

        string workDir = Path.Combine(outputDir, $"m4v_tmp_{Guid.NewGuid():N}");
        try
        {
            Directory.CreateDirectory(workDir);
        }
        catch (Exception ex)
        {
            EmitError($"Failed to create temp dir: {ex.Message}", ConverterError.Unknown);
            return ConverterError.Unknown;
        }

        string videoMp4 = Path.Combine(workDir, "video_only.mp4");
        string aacM4a = Path.Combine(workDir, "audio_aac.m4a");
        string ac3Mp4 = Path.Combine(workDir, "audio_ac3.mp4");
        string dispositionM4v = Path.Combine(workDir, "with_disposition.m4v");
        string chaptersM4v = Path.Combine(workDir, "with_chapters.m4v");

        try
        {
            double fps = await ProbeFpsAsync(tools.Ffprobe, inputFile, cancellationToken);
            string lang = !string.IsNullOrEmpty(opts.M4vAudioLang) ? opts.M4vAudioLang : "rus";

            var videoInfo = await InputVideoInfo.ProbeAsync(tools.Ffprobe, inputFile, cancellationToken);
            string colorPrimaries = !string.IsNullOrEmpty(videoInfo?.ColorPrimaries) ? videoInfo.ColorPrimaries : "bt709";
            string colorTrc = !string.IsNullOrEmpty(videoInfo?.ColorTransfer) ? videoInfo.ColorTransfer : "bt709";
            string colorSpace = !string.IsNullOrEmpty(videoInfo?.ColorSpace) ? videoInfo.ColorSpace : "bt709";
            string colorRange = !string.IsNullOrEmpty(videoInfo?.ColorRange) ? videoInfo.ColorRange : "tv";

            // ----------------------------------------------------
            // Step 1/6: Video copy
            // ----------------------------------------------------
            EmitStage("Apple M4V step 1/6: video copy");
            bool isHevc = videoCodec.Equals("hevc", StringComparison.OrdinalIgnoreCase);
            string tagArg = isHevc ? "-tag:v hvc1 " : "";

            string step1Args = $"-y -nostdin -i \"{inputFile}\" -map 0:v:{opts.M4vVideoTrackIndex} -c:v copy {tagArg}" +
                               $"-color_primaries {colorPrimaries} -color_trc {colorTrc} -colorspace {colorSpace} -color_range {colorRange} " +
                               $"-an -sn -dn -f mp4 \"{videoMp4}\"";

            int rc = await RunProcessAsync(tools.Ffmpeg, step1Args, cancellationToken);
            if (rc != 0)
            {
                EmitError("Apple M4V video copy failed", ConverterError.FfmpegFailed);
                return ConverterError.FfmpegFailed;
            }

            // ----------------------------------------------------
            // Step 2/6: AAC encode
            // ----------------------------------------------------
            EmitStage("Apple M4V step 2/6: AAC encode");
            string aacEncoder = (probeResult?.HasAacAt == true) ? "aac_at" :
                                (probeResult?.HasLibfdkAac == true) ? "libfdk_aac" : "aac";

            string step2Args = $"-y -nostdin -i \"{inputFile}\" -map 0:a:{opts.M4vAudioTrackIndex} -c:a {aacEncoder} -b:a 320k -ar 48000 -f mp4 \"{aacM4a}\"";
            rc = await RunProcessAsync(tools.Ffmpeg, step2Args, cancellationToken);
            if (rc != 0)
            {
                EmitError("Apple M4V AAC encode failed", ConverterError.FfmpegFailed);
                return ConverterError.FfmpegFailed;
            }

            // ----------------------------------------------------
            // Step 3/6: AC3 encode
            // ----------------------------------------------------
            EmitStage("Apple M4V step 3/6: AC3 encode");
            int ac3Bitrate = opts.M4vAc3BitrateKbps > 0 ? opts.M4vAc3BitrateKbps : 640;
            string step3Args = $"-y -nostdin -i \"{inputFile}\" -map 0:a:{opts.M4vAudioTrackIndex} -c:a ac3 -b:a {ac3Bitrate}k -f mp4 \"{ac3Mp4}\"";
            rc = await RunProcessAsync(tools.Ffmpeg, step3Args, cancellationToken);
            if (rc != 0)
            {
                EmitError("Apple M4V AC3 encode failed", ConverterError.FfmpegFailed);
                return ConverterError.FfmpegFailed;
            }

            // ----------------------------------------------------
            // Step 4/6: MP4Box mux
            // ----------------------------------------------------
            EmitStage("Apple M4V step 4/6: MP4Box mux");
            string videoAdd = $"{videoMp4}#video:fps={fps.ToString("F6", CultureInfo.InvariantCulture)}:name=Video";
            string aacAdd = $"{aacM4a}#audio:name=AAC:lang={lang}";
            string ac3Add = $"{ac3Mp4}#audio:name=AC3 {ac3Bitrate}k:lang={lang}";

            string step4Args = $"-new -brand \"M4V :0\" -ab mp42 -ab isom -add \"{videoAdd}\" -add \"{aacAdd}\" -add \"{ac3Add}\" \"{outputFile}\"";

            if (File.Exists(outputFile))
            {
                if (!opts.Overwrite)
                {
                    EmitError("Output exists (enable overwrite)", ConverterError.OutputExists);
                    return ConverterError.OutputExists;
                }
                File.Delete(outputFile);
            }

            rc = await RunProcessAsync(tools.Mp4Box, step4Args, cancellationToken);
            if (rc != 0)
            {
                EmitError("Apple M4V MP4Box mux failed", ConverterError.FfmpegFailed);
                return ConverterError.FfmpegFailed;
            }

            // ----------------------------------------------------
            // Step 5/6: Set audio disposition
            // ----------------------------------------------------
            EmitStage("Apple M4V step 5/6: set audio disposition");
            string step5Args = $"-y -nostdin -i \"{outputFile}\" -map 0:v:0 -map 0:a:0 -map 0:a:1 -c:v copy -c:a copy " +
                               $"-disposition:a:0 default -disposition:a:1 0 -f mp4 \"{dispositionM4v}\"";

            rc = await RunProcessAsync(tools.Ffmpeg, step5Args, cancellationToken);
            if (rc != 0)
            {
                EmitError("Apple M4V audio disposition failed", ConverterError.FfmpegFailed);
                return ConverterError.FfmpegFailed;
            }

            File.Move(dispositionM4v, outputFile, overwrite: true);

            // ----------------------------------------------------
            // Step 6/6: Chapters (optional)
            // ----------------------------------------------------
            if (opts.M4vAddChapters)
            {
                EmitStage("Apple M4V step 6/6: chapters");
                string step6Args = $"-y -nostdin -i \"{outputFile}\" -i \"{inputFile}\" -map 0 -map_chapters 1 -c copy -f mp4 \"{chaptersM4v}\"";
                rc = await RunProcessAsync(tools.Ffmpeg, step6Args, cancellationToken);
                if (rc == 0 && File.Exists(chaptersM4v))
                {
                    File.Move(chaptersM4v, outputFile, overwrite: true);
                }
                else
                {
                    EmitMessage("Apple M4V chapters warning: chapter import skipped or failed");
                }
            }

            return ConverterError.Ok;
        }
        catch (OperationCanceledException)
        {
            if (File.Exists(outputFile))
            {
                try { File.Delete(outputFile); } catch { }
            }
            return ConverterError.SkipFile;
        }
        finally
        {
            try
            {
                if (Directory.Exists(workDir))
                    Directory.Delete(workDir, recursive: true);
            }
            catch { }
        }
    }

    private async Task<int> RunProcessAsync(string executable, string arguments, CancellationToken cancellationToken)
    {
        using var process = new Process
        {
            StartInfo = new ProcessStartInfo
            {
                FileName = executable,
                Arguments = arguments,
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true
            }
        };

        process.OutputDataReceived += (s, e) => { if (!string.IsNullOrEmpty(e.Data)) EmitMessage(e.Data); };
        process.ErrorDataReceived += (s, e) => { if (!string.IsNullOrEmpty(e.Data)) EmitMessage(e.Data); };

        process.Start();
        process.BeginOutputReadLine();
        process.BeginErrorReadLine();

        await process.WaitForExitAsync(cancellationToken);
        return process.ExitCode;
    }

    private static async Task<string> RunCaptureOutputAsync(string executable, string arguments, CancellationToken cancellationToken)
    {
        using var process = new Process
        {
            StartInfo = new ProcessStartInfo
            {
                FileName = executable,
                Arguments = arguments,
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true
            }
        };

        process.Start();
        string output = await process.StandardOutput.ReadToEndAsync(cancellationToken);
        await process.WaitForExitAsync(cancellationToken);
        return output;
    }

    private void EmitStage(string text) => StageChanged?.Invoke(this, text);
    private void EmitMessage(string text) => MessageLogged?.Invoke(this, text);
    private void EmitError(string text, ConverterError code) => ErrorOccurred?.Invoke(this, (text, code));
}
