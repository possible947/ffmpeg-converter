using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using FfmpegConverter.Core.Models;
using FfmpegConverter.Core.Policy;
using FfmpegConverter.Core.Probing;

namespace FfmpegConverter.Core.Engine;

public class AppleM4vPipeline
{
    public event EventHandler<string>? StageChanged;
    public event EventHandler<MuxProgressEventArgs>? MuxProgressChanged;
    public event EventHandler<string>? MessageLogged;
    public event EventHandler<(string Text, ConverterError Code)>? ErrorOccurred;
    private long _progressRevision;

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
        string vOutput;
        try
        {
            vOutput = await RunCaptureOutputAsync(ffprobePath, new[]
            {
                "-v", "error", "-select_streams", "v:0", "-show_entries",
                "stream=codec_name", "-of", "default=noprint_wrappers=1:nokey=1",
                inputFile
            }, cancellationToken);
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        catch (Exception ex) when (ex is Win32Exception or InvalidOperationException or IOException or UnauthorizedAccessException)
        {
            return (false, "", $"Failed to probe video codec: {ex.Message}");
        }

        string videoCodec = vOutput.Trim().ToLowerInvariant();
        if (!IsSupportedVideoCodec(videoCodec))
        {
            return (false, videoCodec, $"Unsupported video codec for M4V: {(string.IsNullOrEmpty(videoCodec) ? "unknown" : videoCodec)}");
        }

        // 2. Check for usable audio stream
        string aOutput;
        try
        {
            aOutput = await RunCaptureOutputAsync(ffprobePath, new[]
            {
                "-v", "error", "-select_streams", "a:0", "-show_entries",
                "stream=codec_type", "-of", "default=noprint_wrappers=1:nokey=1",
                inputFile
            }, cancellationToken);
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        catch (Exception ex) when (ex is Win32Exception or InvalidOperationException or IOException or UnauthorizedAccessException)
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
        try
        {
            string outStr = await RunCaptureOutputAsync(ffprobePath, new[]
            {
                "-v", "error", "-select_streams", "v:0", "-show_entries",
                "stream=avg_frame_rate", "-of", "default=noprint_wrappers=1:nokey=1",
                inputFile
            }, cancellationToken);
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
            string rOut = await RunCaptureOutputAsync(ffprobePath, new[]
            {
                "-v", "error", "-select_streams", "v:0", "-show_entries",
                "stream=r_frame_rate", "-of", "default=noprint_wrappers=1:nokey=1",
                inputFile
            }, cancellationToken);
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
        catch (OperationCanceledException)
        {
            throw;
        }
        catch (Exception ex) when (ex is Win32Exception or InvalidOperationException or IOException or UnauthorizedAccessException)
        {
            return 25.0;
        }

        return 25.0;
    }

    public Task<ConverterError> CreateM4vAsync(
        string inputFile,
        string outputFile,
        ConvertOptions opts,
        ToolPaths tools,
        HardwareProbeResult? probeResult,
        CancellationToken cancellationToken = default) =>
        CreateM4vCoreAsync(inputFile, outputFile, opts, tools, null, cancellationToken, inputFile);

    public Task<ConverterError> CreateMuxM4vAsync(
        string inputFile,
        string outputFile,
        ConvertOptions opts,
        ToolPaths tools,
        HardwareProbeResult? probeResult,
        MuxPolicy policy,
        CancellationToken cancellationToken = default,
        string? progressFilePath = null) =>
        CreateM4vCoreAsync(
            inputFile, outputFile, opts, tools, policy, cancellationToken, progressFilePath ?? inputFile);

    private async Task<ConverterError> CreateM4vCoreAsync(
        string inputFile,
        string outputFile,
        ConvertOptions opts,
        ToolPaths tools,
        MuxPolicy? muxPolicy,
        CancellationToken cancellationToken,
        string progressFilePath)
    {
        if (muxPolicy != null &&
            (muxPolicy.Container != MuxContainer.M4v ||
             muxPolicy.AudioMode is not (AudioOutputMode.Aac or AudioOutputMode.AacAc3) ||
             opts.AudioOutputMode != muxPolicy.AudioMode))
        {
            EmitError("Mux M4V requires AAC or AAC+AC3 audio output.", ConverterError.InvalidOptions);
            return ConverterError.InvalidOptions;
        }

        if (File.Exists(outputFile) && !opts.Overwrite)
        {
            EmitError("Output exists (enable overwrite)", ConverterError.OutputExists);
            return ConverterError.OutputExists;
        }

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
        bool isMuxWorkflow = muxPolicy != null;
        if (isMuxWorkflow &&
            videoCodec is not ("h264" or "hevc"))
        {
            EmitError("Mux M4V requires an H.264 or HEVC replacement video track.", ConverterError.InvalidOptions);
            return ConverterError.InvalidOptions;
        }
        bool addAc3 = muxPolicy == null || muxPolicy.AudioMode == AudioOutputMode.AacAc3;
        int videoTrackIndex = isMuxWorkflow ? 0 : opts.M4vVideoTrackIndex;
        int audioTrackIndex = isMuxWorkflow ? 0 : opts.M4vAudioTrackIndex;
        string lang = isMuxWorkflow
            ? "rus"
            : !string.IsNullOrEmpty(opts.M4vAudioLang) ? opts.M4vAudioLang : "rus";
        int ac3Bitrate = isMuxWorkflow
            ? 640
            : opts.M4vAc3BitrateKbps > 0 ? opts.M4vAc3BitrateKbps : 640;
        MuxPolicy preservationPolicy =
            muxPolicy ?? MuxPolicyResolver.CreateDirectM4vPreservationPolicy();
        MuxSourcePreservationResult preservationResult =
            await MuxSourcePreservation.ProbeAsync(
                tools.Ffprobe, inputFile, preservationPolicy, cancellationToken);
        if (!preservationResult.Success)
        {
            string message = preservationResult.Error ??
                             "Apple M4V stream policy could not be resolved.";
            EmitError(message, ConverterError.FfprobeFailed);
            return ConverterError.FfprobeFailed;
        }
        MuxSourcePreservationPlan sourcePreservationPlan = preservationResult.Plan!;
        foreach (string warning in preservationResult.Warnings)
            EmitMessage(warning);
        if (preservationResult.HasAttachments &&
            !sourcePreservationPlan.PreserveAttachments)
            EmitMessage("Warning: input attachments are dropped by the selected output container.");

        bool addChapters = muxPolicy == null
            ? opts.M4vAddChapters
            : sourcePreservationPlan.PreserveChapters;

        string outputDir;
        try
        {
            outputDir = Path.GetDirectoryName(Path.GetFullPath(outputFile)) ?? ".";
            if (string.IsNullOrEmpty(outputDir))
                outputDir = ".";
        }
        catch (Exception ex) when (ex is ArgumentException or IOException or NotSupportedException)
        {
            EmitError($"Invalid M4V output path: {ex.Message}", ConverterError.InvalidOptions);
            return ConverterError.InvalidOptions;
        }

        string workRoot;
        try
        {
            workRoot = SelectWorkRoot(outputFile, outputDir, Path.GetTempPath());
        }
        catch (ArgumentException ex)
        {
            EmitError(ex.Message, ConverterError.InvalidOptions);
            return ConverterError.InvalidOptions;
        }
        string workDir = Path.Combine(workRoot, $"m4v_tmp_{Guid.NewGuid():N}");
        try
        {
            Directory.CreateDirectory(workDir);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or ArgumentException)
        {
            EmitError($"Failed to create temp dir: {ex.Message}", ConverterError.Unknown);
            return ConverterError.Unknown;
        }

        string videoMp4 = Path.Combine(workDir, "video_only.mp4");
        string aacM4a = Path.Combine(workDir, "audio_aac.m4a");
        string ac3Mp4 = Path.Combine(workDir, "audio_ac3.mp4");
        string assembledM4v = Path.Combine(workDir, "assembled.m4v");
        string dispositionM4v = Path.Combine(workDir, "with_disposition.m4v");
        string chaptersM4v = Path.Combine(workDir, "with_chapters.m4v");
        string subtitlesM4v = Path.Combine(workDir, "with_subtitles.m4v");

        try
        {
            double fps = await ProbeFpsAsync(tools.Ffprobe, inputFile, cancellationToken);

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
            IReadOnlyList<string> step1Args = BuildVideoCopyArguments(
                inputFile, videoTrackIndex, videoMp4, isHevc,
                colorPrimaries, colorTrc, colorSpace, colorRange);

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
            IReadOnlyList<string> step2Args = BuildFixedAacArguments(
                inputFile, audioTrackIndex, aacM4a);
            rc = await RunProcessAsync(tools.Ffmpeg, step2Args, cancellationToken);
            if (rc != 0)
            {
                EmitError("Apple M4V AAC encode failed", ConverterError.FfmpegFailed);
                return ConverterError.FfmpegFailed;
            }

            // ----------------------------------------------------
            // Step 3/6: AC3 encode
            // ----------------------------------------------------
            if (addAc3)
            {
                EmitStage("Apple M4V step 3/6: AC3 encode");
                IReadOnlyList<string> step3Args = BuildAudioEncodeArguments(
                    inputFile, audioTrackIndex, "ac3", $"{ac3Bitrate}k", ac3Mp4, "mp4",
                    sampleRate: null);
                rc = await RunProcessAsync(tools.Ffmpeg, step3Args, cancellationToken);
                if (rc != 0)
                {
                    EmitError("Apple M4V AC3 encode failed", ConverterError.FfmpegFailed);
                    return ConverterError.FfmpegFailed;
                }
            }

            // ----------------------------------------------------
            // Step 4/6: MP4Box mux
            // ----------------------------------------------------
            EmitMuxProgress(
                muxPolicy,
                MuxProgressStage.PackagingM4v,
                "Creating M4V with MP4Box...",
                progressFilePath);
            EmitStage("Apple M4V step 4/6: MP4Box mux");
            string videoAdd = $"{videoMp4}#video:fps={fps.ToString("F6", CultureInfo.InvariantCulture)}:name=Video";
            string aacAdd = $"{aacM4a}#audio:name=AAC:lang={lang}";
            string ac3Add = $"{ac3Mp4}#audio:name=AC3 {ac3Bitrate}k:lang={lang}";
            IReadOnlyList<string> step4Args = BuildMp4BoxArguments(
                videoAdd, aacAdd, ac3Add, assembledM4v, addAc3);

            rc = await RunProcessAsync(
                tools.Mp4Box,
                step4Args,
                cancellationToken,
                percent => EmitMuxProgress(
                    muxPolicy,
                    MuxProgressStage.PackagingM4v,
                    "Creating M4V with MP4Box...",
                    progressFilePath,
                    percent),
                line => MuxToolProgressParser.TryParseMp4BoxProgress(line, out float percent)
                    ? percent
                    : null);
            if (rc != 0)
            {
                EmitError("Apple M4V MP4Box mux failed", ConverterError.FfmpegFailed);
                return ConverterError.FfmpegFailed;
            }

            // ----------------------------------------------------
            // Step 5/6: Set audio disposition
            // ----------------------------------------------------
            EmitMuxProgress(
                muxPolicy,
                MuxProgressStage.PreservingMetadata,
                "Finalizing M4V metadata and track dispositions...",
                progressFilePath);
            EmitStage("Apple M4V step 5/6: set audio disposition");
            IReadOnlyList<string> step5Args = BuildDispositionArguments(
                assembledM4v, dispositionM4v, addAc3);

            rc = await RunProcessAsync(tools.Ffmpeg, step5Args, cancellationToken);
            if (rc != 0)
            {
                EmitError("Apple M4V audio disposition failed", ConverterError.FfmpegFailed);
                return ConverterError.FfmpegFailed;
            }

            MoveOrCopyFile(dispositionM4v, assembledM4v);

            // ----------------------------------------------------
            // Step 6/6: Chapters (optional)
            // ----------------------------------------------------
            if (addChapters)
            {
                EmitMuxProgress(
                    muxPolicy,
                    MuxProgressStage.PreservingMetadata,
                    "Preserving M4V chapters...",
                    progressFilePath);
                EmitStage("Apple M4V step 6/6: chapters");
                IReadOnlyList<string> step6Args = BuildChapterArguments(
                    assembledM4v, inputFile, chaptersM4v);
                rc = await RunProcessAsync(tools.Ffmpeg, step6Args, cancellationToken);
                if (rc == 0 && File.Exists(chaptersM4v))
                {
                    MoveOrCopyFile(chaptersM4v, assembledM4v);
                }
                else
                {
                    EmitMessage("Apple M4V chapters warning: chapter import skipped or failed");
                }
            }

            EmitMuxProgress(
                muxPolicy,
                MuxProgressStage.PreservingMetadata,
                "Preserving M4V metadata and subtitles...",
                progressFilePath);
            EmitStage("Apple M4V metadata and subtitle preservation");
            IReadOnlyList<string> metadataArgs = BuildMuxMetadataSubtitleArguments(
                assembledM4v, inputFile, subtitlesM4v,
                sourcePreservationPlan, videoTrackIndex);
            rc = await RunProcessAsync(tools.Ffmpeg, metadataArgs, cancellationToken);
            if (rc != 0)
            {
                EmitError("Apple M4V metadata remux failed", ConverterError.FfmpegFailed);
                return ConverterError.FfmpegFailed;
            }
            MoveOrCopyFile(subtitlesM4v, assembledM4v);

            MoveOrCopyFile(assembledM4v, outputFile);
            return ConverterError.Ok;
        }
        catch (OperationCanceledException)
        {
            EmitMuxProgress(muxPolicy, MuxProgressStage.Canceled, "M4V processing canceled", progressFilePath);
            return ConverterError.SkipFile;
        }
        catch (Exception ex) when (ex is Win32Exception or InvalidOperationException or IOException or UnauthorizedAccessException or ArgumentException)
        {
            EmitError($"Apple M4V processing failed: {ex.Message}", ConverterError.FfmpegFailed);
            return ConverterError.FfmpegFailed;
        }
        finally
        {
            try
            {
                if (Directory.Exists(workDir))
                    Directory.Delete(workDir, recursive: true);
            }
            catch (IOException ex) { EmitMessage($"Apple M4V temporary cleanup failed: {ex.Message}"); }
            catch (UnauthorizedAccessException ex) { EmitMessage($"Apple M4V temporary cleanup failed: {ex.Message}"); }
        }
    }

    public static IReadOnlyList<string> BuildVideoCopyArguments(
        string inputFile,
        int videoTrackIndex,
        string outputFile,
        bool isHevc,
        string colorPrimaries,
        string colorTransfer,
        string colorSpace,
        string colorRange)
    {
        var arguments = new List<string>
        {
            "-y", "-nostdin", "-i", inputFile, "-map", $"0:v:{videoTrackIndex}",
            "-c:v", "copy"
        };
        if (isHevc)
            arguments.AddRange(new[] { "-tag:v", "hvc1" });
        arguments.AddRange(new[]
        {
            "-color_primaries", colorPrimaries,
            "-color_trc", colorTransfer,
            "-colorspace", colorSpace,
            "-color_range", colorRange,
            "-an", "-sn", "-dn", "-f", "mp4", outputFile
        });
        return arguments;
    }

    public static string SelectWorkRoot(
        string outputFile,
        string outputDirectory,
        string systemTempDirectory)
    {
        if (!outputFile.Contains('+'))
            return outputDirectory;
        if (systemTempDirectory.Contains('+'))
            throw new ArgumentException(
                "Could not select a temporary directory safe for the MP4Box import path.");
        return systemTempDirectory;
    }

    public static IReadOnlyList<string> BuildAudioEncodeArguments(
        string inputFile,
        int audioTrackIndex,
        string encoder,
        string bitrate,
        string outputFile,
        string format,
        string? sampleRate = "48000")
    {
        var arguments = new List<string>
        {
            "-y", "-nostdin", "-i", inputFile, "-map", $"0:a:{audioTrackIndex}",
            "-c:a", encoder, "-b:a", bitrate
        };
        if (sampleRate != null)
            arguments.AddRange(new[] { "-ar", sampleRate });
        arguments.AddRange(new[] { "-f", format, outputFile });
        return arguments;
    }

    public static IReadOnlyList<string> BuildFixedAacArguments(
        string inputFile,
        int audioTrackIndex,
        string outputFile) =>
        BuildAudioEncodeArguments(
            inputFile, audioTrackIndex, "libfdk_aac", "320k", outputFile, "mp4");

    public static IReadOnlyList<string> BuildMp4BoxArguments(
        string videoAdd,
        string aacAdd,
        string ac3Add,
        string outputFile,
        bool addAc3)
    {
        var arguments = new List<string>
        {
            "-new", "-brand", "M4V :0", "-ab", "mp42", "-ab", "isom",
            "-add", videoAdd, "-add", aacAdd
        };
        if (addAc3)
            arguments.AddRange(new[] { "-add", ac3Add });
        arguments.Add(outputFile);
        return arguments;
    }

    public static IReadOnlyList<string> BuildDispositionArguments(
        string inputFile,
        string outputFile,
        bool hasAc3)
    {
        var arguments = new List<string>
        {
            "-y", "-nostdin", "-i", inputFile,
            "-map", "0:v:0", "-map", "0:a:0"
        };
        if (hasAc3)
            arguments.AddRange(new[] { "-map", "0:a:1" });
        arguments.AddRange(new[]
        {
            "-c:v", "copy", "-c:a", "copy",
            "-disposition:a:0", "default"
        });
        if (hasAc3)
            arguments.AddRange(new[] { "-disposition:a:1", "0" });
        arguments.AddRange(new[] { "-f", "mp4", outputFile });
        return arguments;
    }

    public static IReadOnlyList<string> BuildChapterArguments(
        string inputFile,
        string sourceFile,
        string outputFile) =>
        new[]
        {
            "-y", "-nostdin", "-i", inputFile, "-i", sourceFile,
            "-map", "0", "-map_chapters", "1", "-c", "copy", "-f", "mp4",
            outputFile
        };

    public static IReadOnlyList<string> BuildMuxMetadataSubtitleArguments(
        string inputFile,
        string sourceFile,
        string outputFile,
        MuxSourcePreservationPlan sourcePlan,
        int sourceVideoTrackIndex)
    {
        if (sourcePlan.Container != MuxContainer.M4v)
            throw new ArgumentException("M4V remux requires an M4V preservation plan.", nameof(sourcePlan));

        var arguments = new List<string>
        {
            "-y", "-nostdin", "-i", inputFile, "-i", sourceFile
        };
        if (sourcePlan.VideoAction != StreamAction.Drop)
            arguments.AddRange(new[] { "-map", "0:v:0" });
        if (sourcePlan.AudioAction != StreamAction.Drop)
            arguments.AddRange(new[] { "-map", "0:a?" });
        if (sourcePlan.PreserveMetadata)
        {
            arguments.AddRange(new[]
            {
                "-map_metadata", "1",
                "-map_metadata:s:v:0", $"1:s:v:{sourceVideoTrackIndex}"
            });
        }
        if (sourcePlan.PreserveChapters)
            arguments.AddRange(new[] { "-map_chapters", "0" });
        arguments.AddRange(new[] { "-c", "copy" });
        if (sourcePlan.SubtitleAction != StreamAction.Drop)
        {
            foreach (SubtitleStreamMapping subtitle in sourcePlan.SubtitleStreams)
            {
                arguments.Add("-map");
                arguments.Add($"1:{subtitle.StreamIndex}");
                arguments.Add($"-c:s:{subtitle.OutputIndex}");
                arguments.Add(subtitle.Encoder);
            }
        }
        arguments.AddRange(new[] { "-f", "mp4", outputFile });
        return arguments;
    }

    private static void MoveOrCopyFile(string source, string destination)
    {
        try
        {
            File.Move(source, destination, overwrite: true);
        }
        catch (IOException)
        {
            File.Copy(source, destination, overwrite: true);
            File.Delete(source);
        }
    }

    private async Task<int> RunProcessAsync(
        string executable,
        IReadOnlyList<string> arguments,
        CancellationToken cancellationToken,
        Action<float>? progress = null,
        Func<string, float?>? parseProgress = null)
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

        using var process = new Process { StartInfo = startInfo };

        void HandleLine(string? line)
        {
            if (string.IsNullOrEmpty(line))
                return;
            if (parseProgress?.Invoke(line) is float percent)
            {
                progress?.Invoke(percent);
                return;
            }
            EmitMessage(line);
        }

        process.OutputDataReceived += (s, e) => HandleLine(e.Data);
        process.ErrorDataReceived += (s, e) => HandleLine(e.Data);

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
                try { process.Kill(entireProcessTree: true); }
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

        using var process = new Process { StartInfo = startInfo };
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
                try { process.Kill(entireProcessTree: true); }
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
    private void EmitMuxProgress(
        MuxPolicy? muxPolicy,
        MuxProgressStage stage,
        string status,
        string filePath,
        float? percent = null)
    {
        if (muxPolicy == null)
            return;
        MuxProgressChanged?.Invoke(
            this,
            new MuxProgressEventArgs(
                stage,
                System.Threading.Interlocked.Increment(ref _progressRevision),
                filePath,
                status,
                percent));
    }
    private void EmitMessage(string text) => MessageLogged?.Invoke(this, text);
    private void EmitError(string text, ConverterError code) => ErrorOccurred?.Invoke(this, (text, code));
}
