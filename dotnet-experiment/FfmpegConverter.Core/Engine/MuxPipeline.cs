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
        MuxPolicy? policy = null,
        MuxSourcePreservationPlan? sourcePreservationPlan = null)
    {
        string requestedPreset = opts.Preset.ToLowerInvariant();
        if (requestedPreset == "m4v" &&
            (policy == null ||
             policy.Container != MuxContainer.M4v ||
             policy.AudioMode is not (AudioOutputMode.Aac or AudioOutputMode.AacAc3) ||
             opts.AudioOutputMode != policy.AudioMode))
        {
            EmitError("post-mux failed: M4V stream policy is unavailable", ConverterError.InvalidOptions);
            return ConverterError.InvalidOptions;
        }
        if (requestedPreset == "mov" &&
            (policy == null || policy.Container != MuxContainer.Mov))
        {
            EmitError("post-mux failed: MOV stream policy is unavailable", ConverterError.InvalidOptions);
            return ConverterError.InvalidOptions;
        }

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

        if (policy == null ||
            (requestedPreset == "mkv" && policy.Container != MuxContainer.Mkv))
        {
            EmitError("post-mux failed: typed output stream policy is unavailable or inconsistent", ConverterError.InvalidOptions);
            return ConverterError.InvalidOptions;
        }

        if (File.Exists(finalOutputFile) && !opts.Overwrite)
        {
            EmitError("Output exists (enable overwrite)", ConverterError.OutputExists);
            return ConverterError.OutputExists;
        }

        string tempOutput = $"{finalOutputFile}.postmux.{Environment.ProcessId}.{Guid.NewGuid():N}.tmp.mkv";
        string? stagedFinalOutput = null;
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
                rate,
                sourcePreservationPlan);

            EmitStage("Post-mux (mkvmerge)");
            EmitMessage($"Executing: \"{tools.Mkvmerge}\" {string.Join(" ", mkvmergeArgs)}");

            int rc = await RunProcessAsync(tools.Mkvmerge, mkvmergeArgs, cancellationToken);
            if (rc != 0)
            {
                EmitError("post-mux failed: mkvmerge returned error", ConverterError.FfmpegFailed);
                return ConverterError.FfmpegFailed;
            }

            MuxOutputVerificationResult assemblyResult = await VerifyAssemblyAsync(
                tools.Ffprobe, intermediateFile, videoTrackFile, tempOutput, cancellationToken);
            if (!assemblyResult.Success)
            {
                EmitError($"post-mux failed: {assemblyResult.Error}", ConverterError.FfprobeFailed);
                return ConverterError.FfprobeFailed;
            }

            if (policy.Container == MuxContainer.Mkv)
            {
                MuxOutputVerificationResult finalResult = await VerifyFinalOutputAsync(
                    tools.Ffprobe, tempOutput, videoTrackFile, tempOutput, policy, cancellationToken);
                if (!finalResult.Success)
                {
                    EmitError($"post-mux failed: {finalResult.Error}", ConverterError.FfprobeFailed);
                    return ConverterError.FfprobeFailed;
                }
                PromoteStagedOutput(tempOutput, finalOutputFile, opts.Overwrite);
            }
            else if (policy.Container == MuxContainer.Mov)
            {
                stagedFinalOutput = CreateStagedOutputPath(finalOutputFile, "mov");
                ConverterError remuxError = await RunMovRemuxAsync(
                    tempOutput, stagedFinalOutput, tools, policy, cancellationToken);
                if (remuxError != ConverterError.Ok)
                    return remuxError;
                MuxOutputVerificationResult finalResult = await VerifyFinalOutputAsync(
                    tools.Ffprobe, tempOutput, videoTrackFile, stagedFinalOutput, policy, cancellationToken);
                if (!finalResult.Success)
                {
                    EmitError($"post-mux failed: {finalResult.Error}", ConverterError.FfprobeFailed);
                    return ConverterError.FfprobeFailed;
                }
                PromoteStagedOutput(stagedFinalOutput, finalOutputFile, opts.Overwrite);
            }
            else if (policy.Container == MuxContainer.M4v)
            {
                stagedFinalOutput = CreateStagedOutputPath(finalOutputFile, "m4v");
                EmitStage("Post-mux: Apple M4V pipeline");
                var m4vPipe = new AppleM4vPipeline();
                m4vPipe.StageChanged += (s, ev) => EmitStage(ev);
                m4vPipe.MessageLogged += (s, ev) => EmitMessage(ev);
                m4vPipe.ErrorOccurred += (s, ev) => EmitError(ev.Text, ev.Code);

                var err = await m4vPipe.CreateMuxM4vAsync(
                    tempOutput, stagedFinalOutput, opts, tools, probeResult, policy,
                    cancellationToken);
                if (err != ConverterError.Ok)
                    return err;
                MuxOutputVerificationResult finalResult = await VerifyFinalOutputAsync(
                    tools.Ffprobe, tempOutput, videoTrackFile, stagedFinalOutput, policy, cancellationToken);
                if (!finalResult.Success)
                {
                    EmitError($"post-mux failed: {finalResult.Error}", ConverterError.FfprobeFailed);
                    return ConverterError.FfprobeFailed;
                }
                PromoteStagedOutput(stagedFinalOutput, finalOutputFile, opts.Overwrite);
            }
            else
            {
                EmitError($"post-mux failed: unsupported output container {policy.Container}", ConverterError.InvalidOptions);
                return ConverterError.InvalidOptions;
            }

            EmitMessage("Post-mux completed");
            return ConverterError.Ok;
        }
        catch (OperationCanceledException)
        {
            return ConverterError.SkipFile;
        }
        catch (Exception ex) when (ex is Win32Exception or InvalidOperationException or IOException or UnauthorizedAccessException or ArgumentException)
        {
            EmitError($"post-mux failed: {ex.Message}", ConverterError.Unknown);
            return ConverterError.Unknown;
        }
        finally
        {
            DeleteTemporaryFile(tempOutput);
            if (stagedFinalOutput != null)
                DeleteTemporaryFile(stagedFinalOutput);
        }
    }

    private async Task<MuxOutputVerificationResult> VerifyAssemblyAsync(
        string ffprobePath,
        string sourceFile,
        string replacementFile,
        string assembledFile,
        CancellationToken cancellationToken)
    {
        MuxOutputProbeResult source = await MuxOutputVerification.ProbeAsync(
            ffprobePath, sourceFile, cancellationToken);
        if (!source.Success)
            return MuxOutputVerificationResult.Failed($"Could not probe prepared source: {source.Error}");
        MuxOutputProbeResult replacement = await MuxOutputVerification.ProbeAsync(
            ffprobePath, replacementFile, cancellationToken);
        if (!replacement.Success)
            return MuxOutputVerificationResult.Failed($"Could not probe replacement video: {replacement.Error}");
        MuxOutputProbeResult assembled = await MuxOutputVerification.ProbeAsync(
            ffprobePath, assembledFile, cancellationToken);
        if (!assembled.Success)
            return MuxOutputVerificationResult.Failed($"Could not probe assembled MKV: {assembled.Error}");

        return MuxOutputVerification.VerifyAssembly(
            source.Snapshot!, replacement.Snapshot!, assembled.Snapshot!);
    }

    private async Task<MuxOutputVerificationResult> VerifyFinalOutputAsync(
        string ffprobePath,
        string assembledFile,
        string replacementFile,
        string outputFile,
        MuxPolicy policy,
        CancellationToken cancellationToken)
    {
        MuxOutputProbeResult assembled = await MuxOutputVerification.ProbeAsync(
            ffprobePath, assembledFile, cancellationToken);
        if (!assembled.Success)
            return MuxOutputVerificationResult.Failed($"Could not probe assembled source: {assembled.Error}");
        MuxOutputProbeResult replacement = await MuxOutputVerification.ProbeAsync(
            ffprobePath, replacementFile, cancellationToken);
        if (!replacement.Success)
            return MuxOutputVerificationResult.Failed($"Could not probe replacement video: {replacement.Error}");
        MuxOutputProbeResult output = await MuxOutputVerification.ProbeAsync(
            ffprobePath, outputFile, cancellationToken);
        if (!output.Success)
            return MuxOutputVerificationResult.Failed($"Could not probe final output: {output.Error}");

        return MuxOutputVerification.VerifyFinalOutput(
            assembled.Snapshot!, replacement.Snapshot!, output.Snapshot!, policy);
    }

    private static string CreateStagedOutputPath(string finalOutputFile, string extension)
    {
        string fullPath = Path.GetFullPath(finalOutputFile);
        string outputDirectory = Path.GetDirectoryName(fullPath) ?? ".";
        string fileName = Path.GetFileNameWithoutExtension(fullPath);
        return Path.Combine(
            outputDirectory,
            $"{fileName}.postmux.{Environment.ProcessId}.{Guid.NewGuid():N}.tmp.{extension}");
    }

    private static void PromoteStagedOutput(string stagedFile, string finalOutputFile, bool overwrite)
    {
        if (File.Exists(finalOutputFile) && !overwrite)
            throw new IOException("Output exists (enable overwrite).");
        File.Move(stagedFile, finalOutputFile, overwrite);
    }

    private void DeleteTemporaryFile(string path)
    {
        try
        {
            if (File.Exists(path))
                File.Delete(path);
        }
        catch (IOException ex)
        {
            EmitMessage($"Temporary output cleanup failed for '{path}': {ex.Message}");
        }
        catch (UnauthorizedAccessException ex)
        {
            EmitMessage($"Temporary output cleanup failed for '{path}': {ex.Message}");
        }
    }

    public static IReadOnlyList<string> BuildMkvmergeArguments(
        string tempOutput,
        string videoTrackFile,
        string intermediateFile,
        MuxSourceMetadata sourceMetadata,
        string rate,
        MuxSourcePreservationPlan? sourcePreservationPlan = null)
    {
        if (sourcePreservationPlan != null &&
            !sourcePreservationPlan.RequiresVideoReplacement)
            throw new ArgumentException(
                "Mux assembly requires a replacement-video preservation plan.",
                nameof(sourcePreservationPlan));

        var arguments = new List<string> { "-o", tempOutput };
        if (sourcePreservationPlan?.PreserveMetadata != false)
            arguments.AddRange(new[] { "--title", sourceMetadata.SegmentTitle });
        arguments.AddRange(new[]
        {
            "--no-audio", "--no-subtitles", "--no-buttons", "--no-attachments",
            "--no-chapters", "--no-global-tags", "--no-track-tags"
        });
        if (!string.IsNullOrEmpty(rate))
        {
            arguments.Add("--default-duration");
            arguments.Add($"0:{rate}fps");
        }
        if (sourcePreservationPlan?.PreserveLanguage != false)
        {
            arguments.Add("--language");
            arguments.Add($"0:{sourceMetadata.VideoLanguage}");
        }
        if (sourcePreservationPlan?.PreserveStreamMetadata != false)
        {
            arguments.Add("--track-name");
            arguments.Add($"0:{sourceMetadata.VideoTitle}");
        }
        arguments.Add("--video-tracks");
        arguments.Add("0");
        arguments.Add(videoTrackFile);
        arguments.Add("--no-video");
        if (sourcePreservationPlan?.AudioAction == StreamAction.Drop)
            arguments.Add("--no-audio");
        if (sourcePreservationPlan?.SubtitleAction == StreamAction.Drop)
            arguments.Add("--no-subtitles");
        if (sourcePreservationPlan?.AttachmentAction == StreamAction.Drop)
            arguments.Add("--no-attachments");
        if (sourcePreservationPlan?.ChapterAction == StreamAction.Drop)
            arguments.Add("--no-chapters");
        if (sourcePreservationPlan?.MetadataAction == StreamAction.Drop)
            arguments.Add("--no-global-tags");
        if (sourcePreservationPlan?.StreamMetadataAction == StreamAction.Drop)
            arguments.Add("--no-track-tags");
        arguments.Add(intermediateFile);
        return arguments;
    }

    public static IReadOnlyList<string> BuildMovRemuxArguments(
        string inputFile,
        string outputFile,
        MuxSourcePreservationPlan sourcePlan)
    {
        if (sourcePlan.Container != MuxContainer.Mov)
            throw new ArgumentException("MOV remux requires a MOV preservation plan.", nameof(sourcePlan));

        var arguments = new List<string> { "-y", "-nostdin", "-i", inputFile };
        if (sourcePlan.VideoAction != StreamAction.Drop)
            arguments.AddRange(new[] { "-map", "0:v:0" });
        if (sourcePlan.AudioAction != StreamAction.Drop)
            arguments.AddRange(new[] { "-map", "0:a?" });
        if (sourcePlan.SubtitleAction != StreamAction.Drop)
        {
            foreach (SubtitleStreamMapping subtitle in sourcePlan.SubtitleStreams)
            {
                arguments.Add("-map");
                arguments.Add($"0:{subtitle.StreamIndex}");
                arguments.Add($"-c:s:{subtitle.OutputIndex}");
                arguments.Add(subtitle.Encoder);
            }
        }
        if (sourcePlan.PreserveAttachments)
            arguments.AddRange(new[] { "-map", "0:t?" });
        if (sourcePlan.PreserveMetadata)
            arguments.AddRange(new[] { "-map_metadata", "0" });
        if (sourcePlan.PreserveChapters)
            arguments.AddRange(new[] { "-map_chapters", "0" });
        arguments.AddRange(new[] { "-c", "copy" });
        if (sourcePlan.PreserveMetadata)
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
            !preservationResult.Plan!.PreserveAttachments)
            EmitMessage("Warning: input attachments are dropped by the selected output container.");

        EmitStage("Post-mux: remux to .mov");
        IReadOnlyList<string> arguments = BuildMovRemuxArguments(
            inputFile, outputFile, preservationResult.Plan!);
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
