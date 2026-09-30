using System;
using System.Diagnostics;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using FfmpegConverter.Core.Models;
using FfmpegConverter.Core.Probing;

namespace FfmpegConverter.Core.Engine;

public class MuxPipeline
{
    public event EventHandler<string>? StageChanged;
    public event EventHandler<string>? MessageLogged;
    public event EventHandler<(string Text, ConverterError Code)>? ErrorOccurred;

    public static bool VideoTrackNeedsForcedFps(string path)
    {
        if (string.IsNullOrEmpty(path)) return false;
        string ext = Path.GetExtension(path).ToLowerInvariant();
        return ext == ".hevc" || ext == ".h265" || ext == ".264" || ext == ".h264";
    }

    public static async Task<string> ProbeVideoRateStringAsync(string ffprobePath, string inputFile, CancellationToken cancellationToken = default)
    {
        if (string.IsNullOrEmpty(ffprobePath) || !File.Exists(ffprobePath) || !File.Exists(inputFile))
            return "";

        string args = $"-v error -select_streams v:0 -show_entries stream=avg_frame_rate -of default=noprint_wrappers=1:nokey=1 \"{inputFile}\"";
        try
        {
            string output = await RunCaptureOutputAsync(ffprobePath, args, cancellationToken);
            output = output.Trim();
            if (!string.IsNullOrEmpty(output) && output != "0/0")
            {
                return output;
            }

            string rArgs = $"-v error -select_streams v:0 -show_entries stream=r_frame_rate -of default=noprint_wrappers=1:nokey=1 \"{inputFile}\"";
            string rOut = await RunCaptureOutputAsync(ffprobePath, rArgs, cancellationToken);
            rOut = rOut.Trim();
            if (!string.IsNullOrEmpty(rOut) && rOut != "0/0")
            {
                return rOut;
            }
        }
        catch { }

        return "";
    }

    public static async Task<string> ProbeVideoTrackLanguageAsync(string ffprobePath, string inputFile, CancellationToken cancellationToken = default)
    {
        if (string.IsNullOrEmpty(ffprobePath) || !File.Exists(ffprobePath) || !File.Exists(inputFile))
            return "";

        string args = $"-v error -select_streams v:0 -show_entries stream_tags=language -of default=noprint_wrappers=1:nokey=1 \"{inputFile}\"";
        try
        {
            string output = await RunCaptureOutputAsync(ffprobePath, args, cancellationToken);
            output = output.Trim();
            if (!string.IsNullOrEmpty(output))
            {
                return output;
            }
        }
        catch { }

        return "";
    }

    public static async Task<bool> ValidateMuxOutputAsync(string ffprobePath, string outputFile, CancellationToken cancellationToken = default)
    {
        if (!File.Exists(outputFile))
            return false;

        string args = $"-v error -show_entries stream=codec_type -of default=noprint_wrappers=1:nokey=1 \"{outputFile}\"";
        try
        {
            string output = await RunCaptureOutputAsync(ffprobePath, args, cancellationToken);
            bool hasVideo = output.Contains("video", StringComparison.OrdinalIgnoreCase);
            bool hasAudio = output.Contains("audio", StringComparison.OrdinalIgnoreCase);
            return hasVideo && hasAudio;
        }
        catch
        {
            return false;
        }
    }

    public async Task<ConverterError> RunMuxPostprocessAsync(
        string intermediateFile,
        string videoTrackFile,
        string finalOutputFile,
        ConvertOptions opts,
        ToolPaths tools,
        HardwareProbeResult? probeResult,
        CancellationToken cancellationToken = default)
    {
        if (!File.Exists(intermediateFile))
        {
            EmitError("post-mux failed: intermediate file not found", ConverterError.InputNotFound);
            return ConverterError.InputNotFound;
        }

        if (!File.Exists(videoTrackFile))
        {
            EmitError("post-mux failed: video-track file not found", ConverterError.InputNotFound);
            return ConverterError.InputNotFound;
        }

        if (string.IsNullOrEmpty(tools.Mkvmerge) || !File.Exists(tools.Mkvmerge))
        {
            EmitError("post-mux failed: mkvmerge not found", ConverterError.InvalidOptions);
            return ConverterError.InvalidOptions;
        }

        string tempOutput = $"{finalOutputFile}.postmux.{Environment.ProcessId}.{Guid.NewGuid():N}.tmp.mkv";
        try
        {
            string timingArg = "";
            if (VideoTrackNeedsForcedFps(videoTrackFile))
            {
                string rate = await ProbeVideoRateStringAsync(tools.Ffprobe, intermediateFile, cancellationToken);
                if (string.IsNullOrEmpty(rate))
                {
                    EmitError("post-mux failed: could not probe source FPS", ConverterError.FfprobeFailed);
                    return ConverterError.FfprobeFailed;
                }
                timingArg = $"--default-duration 0:{rate}fps ";
            }

            string langArg = "";
            string lang = await ProbeVideoTrackLanguageAsync(tools.Ffprobe, intermediateFile, cancellationToken);
            if (!string.IsNullOrEmpty(lang))
            {
                langArg = $"--language 0:{lang} ";
            }

            string mkvmergeArgs = $"-o \"{tempOutput}\" --no-audio --no-subtitles --no-buttons --no-attachments " +
                                  $"--no-chapters --no-global-tags --no-track-tags {timingArg}{langArg}--video-tracks 0 \"{videoTrackFile}\" --no-video \"{intermediateFile}\"";

            EmitStage("Post-mux (mkvmerge)");
            EmitMessage($"Executing: \"{tools.Mkvmerge}\" {mkvmergeArgs}");

            int rc = await RunProcessAsync(tools.Mkvmerge, mkvmergeArgs, cancellationToken);
            if (rc != 0)
            {
                EmitError("post-mux failed: mkvmerge returned error", ConverterError.FfmpegFailed);
                return ConverterError.FfmpegFailed;
            }

            bool valid = await ValidateMuxOutputAsync(tools.Ffprobe, tempOutput, cancellationToken);
            if (!valid)
            {
                EmitError("post-mux failed: output validation failed", ConverterError.FfprobeFailed);
                return ConverterError.FfprobeFailed;
            }

            string preset = !string.IsNullOrEmpty(opts.Preset) ? opts.Preset.ToLowerInvariant() : "mkv";

            if (preset == "mkv")
            {
                if (File.Exists(finalOutputFile))
                {
                    if (!opts.Overwrite)
                    {
                        EmitError("Output exists (enable overwrite)", ConverterError.OutputExists);
                        return ConverterError.OutputExists;
                    }
                    File.Delete(finalOutputFile);
                }
                File.Move(tempOutput, finalOutputFile, overwrite: true);
            }
            else if (preset == "mov")
            {
                EmitStage("Post-mux: remux to .mov");
                string remuxArgs = $"-y -nostdin -i \"{tempOutput}\" -c copy -f mov \"{finalOutputFile}\"";
                int remuxRc = await RunProcessAsync(tools.Ffmpeg, remuxArgs, cancellationToken);
                if (remuxRc != 0)
                {
                    EmitError("post-mux failed: container remux returned error", ConverterError.FfmpegFailed);
                    return ConverterError.FfmpegFailed;
                }
            }
            else if (preset == "m4v")
            {
                EmitStage("Post-mux: Apple M4V pipeline");
                var m4vPipe = new AppleM4vPipeline();
                m4vPipe.StageChanged += (s, ev) => EmitStage(ev);
                m4vPipe.MessageLogged += (s, ev) => EmitMessage(ev);
                m4vPipe.ErrorOccurred += (s, ev) => EmitError(ev.Text, ev.Code);

                var err = await m4vPipe.CreateM4vAsync(tempOutput, finalOutputFile, opts, tools, probeResult, cancellationToken);
                if (err != ConverterError.Ok)
                {
                    return err;
                }
            }
            else
            {
                File.Move(tempOutput, finalOutputFile, overwrite: true);
            }

            EmitMessage("Post-mux completed");
            return ConverterError.Ok;
        }
        catch (OperationCanceledException)
        {
            return ConverterError.SkipFile;
        }
        finally
        {
            try
            {
                if (File.Exists(tempOutput))
                    File.Delete(tempOutput);
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
