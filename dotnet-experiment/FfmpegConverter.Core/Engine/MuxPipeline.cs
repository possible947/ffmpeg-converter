using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using FfmpegConverter.Core.Models;
using FfmpegConverter.Core.Policy;
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

        try
        {
            string output = await RunCaptureOutputAsync(ffprobePath, new[]
            {
                "-v", "error", "-select_streams", "v:0", "-show_entries",
                "stream=avg_frame_rate", "-of", "default=noprint_wrappers=1:nokey=1",
                inputFile
            }, cancellationToken);
            output = output.Trim();
            if (!string.IsNullOrEmpty(output) && output != "0/0")
            {
                return output;
            }

            string rOut = await RunCaptureOutputAsync(ffprobePath, new[]
            {
                "-v", "error", "-select_streams", "v:0", "-show_entries",
                "stream=r_frame_rate", "-of", "default=noprint_wrappers=1:nokey=1",
                inputFile
            }, cancellationToken);
            rOut = rOut.Trim();
            if (!string.IsNullOrEmpty(rOut) && rOut != "0/0")
            {
                return rOut;
            }
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        catch (Exception ex) when (ex is Win32Exception or InvalidOperationException or IOException or UnauthorizedAccessException)
        {
            return "";
        }

        return "";
    }

    public static async Task<string> ProbeVideoTrackLanguageAsync(string ffprobePath, string inputFile, CancellationToken cancellationToken = default)
    {
        if (string.IsNullOrEmpty(ffprobePath) || !File.Exists(ffprobePath) || !File.Exists(inputFile))
            return "";

        try
        {
            string output = await RunCaptureOutputAsync(ffprobePath, new[]
            {
                "-v", "error", "-select_streams", "v:0", "-show_entries",
                "stream_tags=language", "-of", "default=noprint_wrappers=1:nokey=1",
                inputFile
            }, cancellationToken);
            output = output.Trim();
            if (!string.IsNullOrEmpty(output))
            {
                return output;
            }
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        catch (Exception ex) when (ex is Win32Exception or InvalidOperationException or IOException or UnauthorizedAccessException)
        {
            return "";
        }

        return "";
    }

    public static async Task<bool> ValidateMuxOutputAsync(string ffprobePath, string outputFile, CancellationToken cancellationToken = default)
    {
        if (!File.Exists(outputFile))
            return false;

        try
        {
            string output = await RunCaptureOutputAsync(ffprobePath, new[]
            {
                "-v", "error", "-show_entries", "stream=codec_type",
                "-of", "default=noprint_wrappers=1:nokey=1", outputFile
            }, cancellationToken);
            bool hasVideo = output.Contains("video", StringComparison.OrdinalIgnoreCase);
            bool hasAudio = output.Contains("audio", StringComparison.OrdinalIgnoreCase);
            return hasVideo && hasAudio;
        }
        catch
        {
            return false;
        }
    }

    public static bool PreparedSourceOutputIsUsable(string output)
    {
        bool hasVideo = false;
        bool hasAudio = false;
        using var reader = new StringReader(output);
        string? line;
        while ((line = reader.ReadLine()) != null)
        {
            if (line.Equals("video", StringComparison.OrdinalIgnoreCase))
                hasVideo = true;
            else if (line.Equals("audio", StringComparison.OrdinalIgnoreCase))
                hasAudio = true;
        }
        return hasVideo && hasAudio;
    }

    public static async Task<bool> ValidatePreparedSourceAsync(
        string ffprobePath,
        string intermediateFile,
        CancellationToken cancellationToken = default)
    {
        if (string.IsNullOrEmpty(ffprobePath) || !File.Exists(ffprobePath) || !File.Exists(intermediateFile))
            return false;

        var startInfo = new ProcessStartInfo
        {
            FileName = ffprobePath,
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true
        };
        foreach (string argument in new[]
        {
            "-v", "error", "-show_entries", "stream=codec_type",
            "-of", "default=noprint_wrappers=1:nokey=1", intermediateFile
        })
            startInfo.ArgumentList.Add(argument);

        using var process = new Process { StartInfo = startInfo };
        try
        {
            if (!process.Start())
                return false;

            Task<string> outputTask = process.StandardOutput.ReadToEndAsync();
            Task<string> errorTask = process.StandardError.ReadToEndAsync();
            try
            {
                await process.WaitForExitAsync(cancellationToken);
            }
            catch (OperationCanceledException)
            {
                if (!process.HasExited)
                {
                    try
                    {
                        process.Kill(entireProcessTree: true);
                    }
                    catch (InvalidOperationException) { }
                    catch (Win32Exception) { }
                }
                await process.WaitForExitAsync(CancellationToken.None);
                throw;
            }

            _ = await errorTask;
            return process.ExitCode == 0 && PreparedSourceOutputIsUsable(await outputTask);
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        catch (Exception ex) when (ex is Win32Exception or InvalidOperationException or IOException or UnauthorizedAccessException)
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
        CancellationToken cancellationToken = default,
        MuxPolicy? policy = null)
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
            string rate = "";
            if (VideoTrackNeedsForcedFps(videoTrackFile))
            {
                rate = await ProbeVideoRateStringAsync(tools.Ffprobe, intermediateFile, cancellationToken);
                if (string.IsNullOrEmpty(rate))
                {
                    EmitError("post-mux failed: could not probe source FPS", ConverterError.FfprobeFailed);
                    return ConverterError.FfprobeFailed;
                }
            }

            MuxSourceMetadataResult sourceMetadataResult =
                await MuxSourceMetadataProbe.ProbeAsync(
                    tools.Ffprobe, intermediateFile, cancellationToken);
            if (!sourceMetadataResult.Success)
            {
                string message = sourceMetadataResult.Error ??
                                 "post-mux failed: could not probe source metadata";
                EmitError(message, ConverterError.FfprobeFailed);
                return ConverterError.FfprobeFailed;
            }

            IReadOnlyList<string> mkvmergeArgs = BuildMkvmergeArguments(
                tempOutput,
                videoTrackFile,
                intermediateFile,
                sourceMetadataResult.Metadata!,
                rate);

            EmitStage("Post-mux (mkvmerge)");
            EmitMessage($"Executing: \"{tools.Mkvmerge}\" {string.Join(" ", mkvmergeArgs)}");

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
                if (policy == null || policy.Container != MuxContainer.Mov)
                {
                    EmitError("post-mux failed: MOV stream policy is unavailable", ConverterError.InvalidOptions);
                    return ConverterError.InvalidOptions;
                }
                ConverterError remuxError = await RunMovRemuxAsync(
                    tempOutput, finalOutputFile, tools, policy, cancellationToken);
                if (remuxError != ConverterError.Ok)
                    return remuxError;
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

    public static IReadOnlyList<string> BuildMkvmergeArguments(
        string tempOutput,
        string videoTrackFile,
        string intermediateFile,
        MuxSourceMetadata sourceMetadata,
        string rate)
    {
        var arguments = new List<string>
        {
            "-o", tempOutput,
            "--title", sourceMetadata.SegmentTitle,
            "--no-audio", "--no-subtitles", "--no-buttons", "--no-attachments",
            "--no-chapters", "--no-global-tags", "--no-track-tags"
        };
        if (!string.IsNullOrEmpty(rate))
        {
            arguments.Add("--default-duration");
            arguments.Add($"0:{rate}fps");
        }
        arguments.Add("--language");
        arguments.Add($"0:{sourceMetadata.VideoLanguage}");
        arguments.Add("--track-name");
        arguments.Add($"0:{sourceMetadata.VideoTitle}");
        arguments.Add("--video-tracks");
        arguments.Add("0");
        arguments.Add(videoTrackFile);
        arguments.Add("--no-video");
        arguments.Add(intermediateFile);
        return arguments;
    }

    public static IReadOnlyList<string> BuildMovRemuxArguments(
        string inputFile,
        string outputFile,
        MuxSourcePreservationPlan sourcePlan,
        MuxPolicy policy)
    {
        var arguments = new List<string>
        {
            "-y", "-nostdin", "-i", inputFile,
            "-map", "0:v:0", "-map", "0:a?"
        };
        foreach (SubtitleStreamMapping subtitle in sourcePlan.SubtitleStreams)
        {
            arguments.Add("-map");
            arguments.Add($"0:{subtitle.StreamIndex}");
            arguments.Add($"-c:s:{subtitle.OutputIndex}");
            arguments.Add(subtitle.Encoder);
        }
        if (policy.MetadataAction == StreamAction.Preserve)
            arguments.AddRange(new[] { "-map_metadata", "0" });
        if (sourcePlan.PreserveChapters)
            arguments.AddRange(new[] { "-map_chapters", "0" });
        arguments.AddRange(new[] { "-c", "copy" });
        if (policy.MetadataAction == StreamAction.Preserve)
            arguments.AddRange(new[] { "-movflags", "use_metadata_tags" });
        arguments.AddRange(new[] { "-f", "mov", outputFile });
        return arguments;
    }

    private async Task<ConverterError> RunMovRemuxAsync(
        string inputFile,
        string outputFile,
        ToolPaths tools,
        MuxPolicy policy,
        CancellationToken cancellationToken)
    {
        MuxSourcePreservationResult preservationResult =
            await MuxSourcePreservation.ProbeAsync(
                tools.Ffprobe, inputFile, policy, cancellationToken);
        if (!preservationResult.Success)
        {
            string message = preservationResult.Error ??
                             "post-mux failed: could not probe source streams";
            EmitError(message, ConverterError.FfprobeFailed);
            return ConverterError.FfprobeFailed;
        }
        foreach (string warning in preservationResult.Warnings)
            EmitMessage(warning);
        if (preservationResult.HasAttachments &&
            policy.AttachmentAction == StreamAction.Drop)
            EmitMessage("Warning: input attachments are dropped by the selected output container.");

        EmitStage("Post-mux: remux to .mov");
        IReadOnlyList<string> arguments = BuildMovRemuxArguments(
            inputFile, outputFile, preservationResult.Plan!, policy);
        int result = await RunProcessAsync(tools.Ffmpeg, arguments, cancellationToken);
        if (result == 0)
            return ConverterError.Ok;

        EmitError("post-mux failed: container remux returned error", ConverterError.FfmpegFailed);
        return ConverterError.FfmpegFailed;
    }

    private async Task<int> RunProcessAsync(
        string executable,
        IReadOnlyList<string> arguments,
        CancellationToken cancellationToken)
    {
        var startInfo = new ProcessStartInfo
        {
            FileName = executable,
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true
        };
        foreach (string argument in arguments)
            startInfo.ArgumentList.Add(argument);

        using var process = new Process
        {
            StartInfo = startInfo
        };

        process.OutputDataReceived += (s, e) => { if (!string.IsNullOrEmpty(e.Data)) EmitMessage(e.Data); };
        process.ErrorDataReceived += (s, e) => { if (!string.IsNullOrEmpty(e.Data)) EmitMessage(e.Data); };

        process.Start();
        process.BeginOutputReadLine();
        process.BeginErrorReadLine();

        try
        {
            await process.WaitForExitAsync(cancellationToken);
        }
        catch (OperationCanceledException)
        {
            if (!process.HasExited)
            {
                try
                {
                    process.Kill(entireProcessTree: true);
                }
                catch (InvalidOperationException) { }
                catch (Win32Exception) { }
            }
            await process.WaitForExitAsync(CancellationToken.None);
            throw;
        }
        return process.ExitCode;
    }

    private static async Task<string> RunCaptureOutputAsync(
        string executable,
        IReadOnlyList<string> arguments,
        CancellationToken cancellationToken)
    {
        var startInfo = new ProcessStartInfo
        {
            FileName = executable,
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true
        };
        foreach (string argument in arguments)
            startInfo.ArgumentList.Add(argument);

        using var process = new Process
        {
            StartInfo = startInfo
        };

        process.Start();
        Task<string> outputTask = process.StandardOutput.ReadToEndAsync();
        Task<string> errorTask = process.StandardError.ReadToEndAsync();
        try
        {
            await process.WaitForExitAsync(cancellationToken);
        }
        catch (OperationCanceledException)
        {
            if (!process.HasExited)
            {
                try
                {
                    process.Kill(entireProcessTree: true);
                }
                catch (InvalidOperationException) { }
                catch (Win32Exception) { }
            }
            await process.WaitForExitAsync(CancellationToken.None);
            throw;
        }
        string output = await outputTask;
        _ = await errorTask;
        return output;
    }

    private void EmitStage(string text) => StageChanged?.Invoke(this, text);
    private void EmitMessage(string text) => MessageLogged?.Invoke(this, text);
    private void EmitError(string text, ConverterError code) => ErrorOccurred?.Invoke(this, (text, code));
}
