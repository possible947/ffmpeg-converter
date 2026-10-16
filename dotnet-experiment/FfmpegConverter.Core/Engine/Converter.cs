using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using FfmpegConverter.Core.Models;
using FfmpegConverter.Core.Policy;
using FfmpegConverter.Core.Presets;
using FfmpegConverter.Core.Probing;

namespace FfmpegConverter.Core.Engine;

public class FileBeginEventArgs : EventArgs
{
    public string FileName { get; }
    public int Index { get; }
    public int Total { get; }
    public FileBeginEventArgs(string fileName, int index, int total) => (FileName, Index, Total) = (fileName, index, total);
}

public class FileEndEventArgs : EventArgs
{
    public string FileName { get; }
    public ConverterError Status { get; }
    public FileEndEventArgs(string fileName, ConverterError status) => (FileName, Status) = (fileName, status);
}

public class ProgressEncodeEventArgs : EventArgs
{
    public float Percent { get; }
    public float Fps { get; }
    public float EtaSeconds { get; }
    public ProgressEncodeEventArgs(float percent, float fps, float etaSeconds) => (Percent, Fps, EtaSeconds) = (percent, fps, etaSeconds);
}

public class ProgressAnalysisEventArgs : EventArgs
{
    public float Percent { get; }
    public float EtaSeconds { get; }
    public ProgressAnalysisEventArgs(float percent, float etaSeconds) => (Percent, EtaSeconds) = (percent, etaSeconds);
}

public class MessageEventArgs : EventArgs
{
    public string Text { get; }
    public MessageEventArgs(string text) => Text = text;
}

public class ErrorEventArgs : EventArgs
{
    public string Text { get; }
    public ConverterError Code { get; }
    public ErrorEventArgs(string text, ConverterError code) => (Text, Code) = (text, code);
}

public class Converter
{
    public event EventHandler<FileBeginEventArgs>? FileBegin;
    public event EventHandler<FileEndEventArgs>? FileEnd;
    public event EventHandler<string>? StageChanged;
    public event EventHandler<ProgressEncodeEventArgs>? ProgressEncode;
    public event EventHandler<ProgressAnalysisEventArgs>? ProgressAnalysis;
    public event EventHandler<MessageEventArgs>? MessageLogged;
    public event EventHandler<ErrorEventArgs>? ErrorOccurred;
    public event EventHandler? Completed;

    private readonly ToolPaths _tools;
    private readonly PresetDb _presetDb;
    private readonly IReplacementVideoProbe _replacementVideoProbe;
    private HardwareProbeResult? _probeResult;

    public Converter(
        ToolPaths? tools = null,
        PresetDb? presetDb = null,
        HardwareProbeResult? probeResult = null,
        IReplacementVideoProbe? replacementVideoProbe = null)
    {
        _tools = tools ?? ToolDiscovery.ResolveAll();
        _presetDb = presetDb ?? PresetDb.Load();
        _probeResult = probeResult;
        _replacementVideoProbe = replacementVideoProbe ?? new ReplacementVideoProbe();
    }

    public async Task<ConverterError> ProcessFilesAsync(IEnumerable<string> files, ConvertOptions opts, CancellationToken cancellationToken = default)
    {
        if (string.IsNullOrEmpty(_tools.Ffmpeg))
        {
            RaiseError("ffmpeg binary not found on the system.", ConverterError.PlatformInitFailed);
            return ConverterError.PlatformInitFailed;
        }

        if (opts.M4vEditBeforeMux &&
            AudioModePolicy.GetWorkflow(opts) == AudioWorkflowContext.DirectM4v)
        {
            RaiseError(
                "Direct M4V edit-before-mux requires separate source conversion options.",
                ConverterError.InvalidOptions);
            return ConverterError.InvalidOptions;
        }

        string effectiveOutputDir = string.IsNullOrEmpty(opts.OutputDir) 
            ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "ffmpeg_converter")
            : opts.OutputDir;

        try
        {
            Directory.CreateDirectory(effectiveOutputDir);
        }
        catch (Exception ex)
        {
            RaiseError($"Failed to create or access output directory {effectiveOutputDir}: {ex.Message}", ConverterError.HomeDirNotFound);
            return ConverterError.HomeDirNotFound;
        }

        // Lazy probe hardware capabilities once per converter run if not yet probed
        if (_probeResult == null)
        {
            try
            {
                _probeResult = await HardwareProbe.ProbeCapabilitiesAsync(_tools.Ffmpeg, _presetDb);
            }
            catch
            {
                _probeResult = new HardwareProbeResult();
            }
        }

        string platform = RuntimeInformation.IsOSPlatform(OSPlatform.Windows) ? "windows" :
                         RuntimeInformation.IsOSPlatform(OSPlatform.OSX) ? "macos" : "linux";

        // Resolve selection if group and encoder specified (and not default while Codec was explicitly set)
        if (!string.IsNullOrEmpty(opts.SelectionGroup) && !string.IsNullOrEmpty(opts.SelectionEncoder) &&
            (opts.SelectionGroup != "software" || opts.SelectionEncoder != "prores_ks" || opts.Codec == "prores_ks"))
        {
            if (_presetDb.ResolveSelection(platform, opts.SelectionGroup, opts.SelectionEncoder, out string resolvedCodec))
            {
                opts.Codec = resolvedCodec;
            }
        }
        AudioModePolicy.ApplyDefault(opts);

        int index = 0;
        var fileList = new List<string>(files);
        int total = fileList.Count;
        bool singleMuxWorkflow = total == 1 &&
                                 AudioModePolicy.GetWorkflow(opts) == AudioWorkflowContext.Mux;
        ConverterError singleMuxResult = ConverterError.Ok;

        foreach (var input in fileList)
        {
            index++;
            FileBegin?.Invoke(this, new FileBeginEventArgs(input, index, total));

            if (cancellationToken.IsCancellationRequested)
            {
                FileEnd?.Invoke(this, new FileEndEventArgs(input, ConverterError.SkipFile));
                return ConverterError.SkipFile;
            }

            if (!File.Exists(input))
            {
                RaiseError($"Input file not found: {input}", ConverterError.InputNotFound);
                FileEnd?.Invoke(this, new FileEndEventArgs(input, ConverterError.InputNotFound));
                if (singleMuxWorkflow)
                    singleMuxResult = ConverterError.InputNotFound;
                continue;
            }

            AudioWorkflowContext audioWorkflow = AudioModePolicy.GetWorkflow(opts);
            MuxVideoCodec sourceVideoCodec = MuxVideoCodec.Unknown;
            MuxVideoCodec replacementVideoCodec = MuxVideoCodec.Unknown;
            InputVideoProbeResult? replacementProbeResult = null;
            MuxPolicy? muxPolicy = null;
            MuxOperationalMode muxMode = default;
            if (audioWorkflow == AudioWorkflowContext.Mux)
            {
                if (!AudioModePolicy.TryGetMuxMode(opts, out muxMode))
                {
                    RaiseError("Selected Mux mode is invalid.", ConverterError.InvalidOptions);
                    FileEnd?.Invoke(this, new FileEndEventArgs(input, ConverterError.InvalidOptions));
                    continue;
                }

                if (muxMode == MuxOperationalMode.Copy && !opts.DryRun)
                {
                    var sourceInfo = await InputVideoInfo.ProbeAsync(
                        _tools.Ffprobe, input, cancellationToken);
                    if (sourceInfo == null)
                    {
                        RaiseError("Could not probe the source video codec for Copy mode.", ConverterError.FfprobeFailed);
                        FileEnd?.Invoke(this, new FileEndEventArgs(input, ConverterError.FfprobeFailed));
                        if (singleMuxWorkflow)
                            singleMuxResult = ConverterError.FfprobeFailed;
                        continue;
                    }
                    sourceVideoCodec = MuxPolicyResolver.ParseVideoCodec(sourceInfo.CodecName);
                }
                else if (muxMode != MuxOperationalMode.Copy)
                {
                    if (string.IsNullOrEmpty(opts.VideoTrackPath) || !File.Exists(opts.VideoTrackPath))
                    {
                        RaiseError("Mux mode requires a readable --video-track file.", ConverterError.InputNotFound);
                        FileEnd?.Invoke(this, new FileEndEventArgs(input, ConverterError.InputNotFound));
                        continue;
                    }

                    if (!opts.DryRun)
                    {
                        replacementProbeResult = await _replacementVideoProbe.ProbeAsync(
                            _tools.Ffprobe, opts.VideoTrackPath, cancellationToken);
                        if (!replacementProbeResult.Success)
                        {
                            (ConverterError probeError, string probeMessage) =
                                DescribeReplacementProbeFailure(replacementProbeResult);
                            RaiseError(probeMessage, probeError);
                            FileEnd?.Invoke(this, new FileEndEventArgs(input, probeError));
                            continue;
                        }
                        replacementVideoCodec = replacementProbeResult.Codec;
                    }
                }
            }

            string? audioPolicyError;
            if (audioWorkflow == AudioWorkflowContext.Mux)
            {
                muxPolicy = AudioModePolicy.ResolveMuxPolicy(
                    opts, platform, sourceVideoCodec, replacementVideoCodec, out string policyError);
                audioPolicyError = muxPolicy == null ? policyError : null;
            }
            else
            {
                audioPolicyError = AudioModePolicy.Validate(
                    opts, platform, sourceVideoCodec, replacementVideoCodec);
            }
            if (!string.IsNullOrEmpty(audioPolicyError))
            {
                RaiseError(audioPolicyError, ConverterError.InvalidOptions);
                FileEnd?.Invoke(this, new FileEndEventArgs(input, ConverterError.InvalidOptions));
                continue;
            }

            // Determine output file path
            string output = CommandBuilder.BuildOutputFilePath(
                input, opts, platform, _presetDb, sourceVideoCodec);

            if (File.Exists(output) && !opts.Overwrite)
            {
                RaiseError($"Output file already exists and overwrite is disabled: {output}", ConverterError.OutputExists);
                FileEnd?.Invoke(this, new FileEndEventArgs(input, ConverterError.SkipFile));
                continue;
            }

            // ====================================================
            // Apple M4V Pipeline
            // ====================================================
            if (AudioModePolicy.GetWorkflow(opts) == AudioWorkflowContext.DirectM4v)
            {
                if (opts.DryRun)
                {
                    RaiseMessage($"[DRY RUN] [m4v] {input} -> {output}");
                    FileEnd?.Invoke(this, new FileEndEventArgs(input, ConverterError.Ok));
                    continue;
                }

                if (string.IsNullOrEmpty(_tools.Mp4Box) || !File.Exists(_tools.Mp4Box))
                {
                    RaiseError("Apple M4V mode is not supported on this platform (MP4Box not found).", ConverterError.InvalidOptions);
                    FileEnd?.Invoke(this, new FileEndEventArgs(input, ConverterError.InvalidOptions));
                    continue;
                }

                var m4vPipe = new AppleM4vPipeline();
                m4vPipe.StageChanged += (s, ev) => StageChanged?.Invoke(this, ev);
                m4vPipe.MessageLogged += (s, ev) => RaiseMessage(ev);
                m4vPipe.ErrorOccurred += (s, ev) => RaiseError(ev.Text, ev.Code);

                var m4vErr = await m4vPipe.CreateM4vAsync(input, output, opts, _tools, _probeResult, cancellationToken);
                FileEnd?.Invoke(this, new FileEndEventArgs(input, m4vErr));
                continue;
            }

            // ====================================================
            // Mux Mode (Replacement Video Track via mkvmerge)
            // ====================================================
            if (audioWorkflow == AudioWorkflowContext.Mux &&
                muxMode != MuxOperationalMode.Copy)
            {
                if (string.IsNullOrEmpty(_tools.Mkvmerge) || !File.Exists(_tools.Mkvmerge))
                {
                    RaiseError("Mux mode is not supported on this platform (mkvmerge not found).", ConverterError.InvalidOptions);
                    FileEnd?.Invoke(this, new FileEndEventArgs(input, ConverterError.InvalidOptions));
                    continue;
                }

                if (string.IsNullOrEmpty(opts.VideoTrackPath) || !File.Exists(opts.VideoTrackPath))
                {
                    RaiseError("Mux mode requires a readable --video-track file.", ConverterError.InputNotFound);
                    FileEnd?.Invoke(this, new FileEndEventArgs(input, ConverterError.InputNotFound));
                    continue;
                }

                if (opts.DryRun)
                {
                    RaiseMessage($"[DRY RUN] [mux] {input} + {opts.VideoTrackPath} -> {output}");
                    FileEnd?.Invoke(this, new FileEndEventArgs(input, ConverterError.Ok));
                    continue;
                }

                if (muxPolicy == null || replacementProbeResult == null)
                {
                    RaiseError("Mux Add Track policy or replacement probe is unavailable.", ConverterError.InvalidOptions);
                    FileEnd?.Invoke(this, new FileEndEventArgs(input, ConverterError.InvalidOptions));
                    continue;
                }

                var addTrackRequest = new AddTrackRequest(
                    input, opts.VideoTrackPath, output, effectiveOutputDir,
                    opts, opts, platform, muxPolicy, replacementProbeResult);
                AddTrackResult addTrackResult = await ExecuteAddTrackAsync(addTrackRequest, cancellationToken);
                if (singleMuxWorkflow &&
                    addTrackResult.Stage == AddTrackStage.SourcePreparation &&
                    addTrackResult.Error is ConverterError.FfprobeFailed or ConverterError.InputNotFound or ConverterError.InputNotReadable)
                    singleMuxResult = addTrackResult.Error;
                FileEnd?.Invoke(this, new FileEndEventArgs(input, addTrackResult.Error));
                continue;
            }

            if (opts.Codec.Equals("hq_converter", StringComparison.OrdinalIgnoreCase))
            {
                HqPresetCatalog? catalog = _presetDb.HqCatalog;
                string encoder = opts.SelectionEncoder;
                if (catalog == null || !catalog.ContainsEncoder(encoder) ||
                    !catalog.ContainsPreset(encoder, opts.Preset))
                {
                    RaiseError("HQ Converter runtime or selected encoder/preset is unavailable.", ConverterError.InvalidOptions);
                    FileEnd?.Invoke(this, new FileEndEventArgs(input, ConverterError.InvalidOptions));
                    continue;
                }

                bool isProRes = HqPresetCatalog.IsProResEncoder(encoder);
                if (isProRes && opts.AudioOutputMode != AudioOutputMode.Pcm)
                {
                    RaiseError("HQ ProRes encoders require PCM audio output.", ConverterError.InvalidOptions);
                    FileEnd?.Invoke(this, new FileEndEventArgs(input, ConverterError.InvalidOptions));
                    continue;
                }

                if (string.IsNullOrEmpty(_tools.Mkvmerge) || !File.Exists(_tools.Mkvmerge))
                {
                    RaiseError("HQ Converter requires mkvmerge for final assembly.", ConverterError.InvalidOptions);
                    FileEnd?.Invoke(this, new FileEndEventArgs(input, ConverterError.InvalidOptions));
                    continue;
                }

                if (opts.DryRun)
                {
                    RaiseMessage($"[DRY RUN] [hq_converter] {input} -> {output} ({encoder}/{opts.Preset})");
                    FileEnd?.Invoke(this, new FileEndEventArgs(input, ConverterError.Ok));
                    continue;
                }

                string taskDir = Path.GetFullPath(Path.Combine(effectiveOutputDir, $"task_{index}"));
                if (Directory.Exists(taskDir) || File.Exists(taskDir))
                {
                    RaiseError($"HQ task directory already exists: {taskDir}", ConverterError.InvalidOptions);
                    FileEnd?.Invoke(this, new FileEndEventArgs(input, ConverterError.InvalidOptions));
                    continue;
                }

                ConverterError hqError = ConverterError.Ok;
                try
                {
                    Directory.CreateDirectory(taskDir);
                    string absoluteInput = Path.GetFullPath(input);
                    var hq = new HqAdapter();
                    hq.StageChanged += (s, stage) => StageChanged?.Invoke(this, stage);
                    hq.MessageLogged += (s, message) => RaiseMessage(message);
                    hq.ProgressEncode += (s, progress) => ProgressEncode?.Invoke(this, progress);
                    hq.ProgressAnalysis += (s, progress) => ProgressAnalysis?.Invoke(this, progress);
                    hq.ErrorOccurred += (s, error) => RaiseError(error.Text, error.Code);

                    StageChanged?.Invoke(this, "HQ encoding");
                    var hqResult = await hq.RunJobAsync(catalog.RuntimeRoot, absoluteInput,
                        taskDir, encoder, opts.Preset, cancellationToken);
                    if (hqResult.Result == 1 || cancellationToken.IsCancellationRequested)
                    {
                        hqError = ConverterError.SkipFile;
                    }
                    else if (hqResult.Result != 0)
                    {
                        RaiseError($"HQ failed: {hqResult.Error}", ConverterError.FfmpegFailed);
                        hqError = ConverterError.FfmpegFailed;
                    }
                    else
                    {
                        string? videoTrack = HqAdapter.ReadVideoTrackPath(taskDir, encoder, opts.Preset);
                        if (string.IsNullOrEmpty(videoTrack) || !File.Exists(videoTrack))
                        {
                            RaiseError("HQ succeeded but its result session has no readable video track.", ConverterError.InputNotFound);
                            hqError = ConverterError.InputNotFound;
                        }
                        else if (cancellationToken.IsCancellationRequested)
                        {
                            hqError = ConverterError.SkipFile;
                        }
                        else
                        {
                            InputVideoProbeResult replacementProbe = await _replacementVideoProbe.ProbeAsync(
                                _tools.Ffprobe, videoTrack, cancellationToken);
                            if (!replacementProbe.Success)
                            {
                                (ConverterError probeError, string probeMessage) =
                                    DescribeReplacementProbeFailure(replacementProbe);
                                RaiseError(probeMessage, probeError);
                                hqError = probeError;
                            }
                            else if (isProRes != (replacementProbe.Codec == MuxVideoCodec.ProRes))
                            {
                                RaiseError("HQ output video codec does not match the selected encoder.", ConverterError.InvalidOptions);
                                hqError = ConverterError.InvalidOptions;
                            }
                            else
                            {
                                PolicyResult hqPolicyResult = AudioModePolicy.ResolveHqPolicy(
                                    opts, platform, replacementProbe.Codec);
                                if (!hqPolicyResult.Success)
                                {
                                    RaiseError(hqPolicyResult.ErrorMessage, ConverterError.InvalidOptions);
                                    hqError = ConverterError.InvalidOptions;
                                }
                                else
                                {
                                    MuxPolicy hqPolicy = hqPolicyResult.Policy!;
                                    var muxOptions = new ConvertOptions
                                    {
                                        Codec = "mux",
                                        Preset = isProRes ? "mov" : "mkv",
                                        AudioOutputMode = hqPolicy.AudioMode,
                                        Overwrite = opts.Overwrite,
                                        OutputDir = effectiveOutputDir,
                                        VideoTrackPath = videoTrack
                                    };
                                    var addTrackRequest = new AddTrackRequest(
                                        input, videoTrack, output, effectiveOutputDir,
                                        opts, muxOptions, platform, hqPolicy, replacementProbe);
                                    AddTrackResult addTrackResult = await ExecuteAddTrackAsync(
                                        addTrackRequest, cancellationToken);
                                    hqError = addTrackResult.Error;
                                }
                            }
                        }
                    }
                }
                catch (OperationCanceledException)
                {
                    hqError = ConverterError.SkipFile;
                }
                catch (Exception ex)
                {
                    RaiseError($"HQ processing failed: {ex.Message}", ConverterError.FfmpegFailed);
                    hqError = ConverterError.FfmpegFailed;
                }
                finally
                {
                    try { RemoveTaskDirectory(taskDir); }
                    catch (Exception ex) { RaiseMessage($"HQ task cleanup failed: {ex.Message}"); }
                }

                FileEnd?.Invoke(this, new FileEndEventArgs(input, hqError));
                continue;
            }

            // ====================================================
            // Standard Conversion Pipeline (copy, prores, hwaccel)
            // ====================================================
            // Step 1. Probe input video properties
            var videoInfo = await InputVideoInfo.ProbeAsync(_tools.Ffprobe, input, cancellationToken);
            double duration = await GetDurationAsync(input);
            RaiseMessage($"Input duration: {duration:F2} seconds");

            // Step 2. Two-Pass Audio Analysis (Peak norm or Loudness norm)
            string audioNorm = opts.AudioNorm.ToLowerInvariant();
            if (opts.AudioOutputMode != AudioOutputMode.Copy &&
                (audioNorm == "peak2" || audioNorm == "peak_norm_2pass"))
            {
                StageChanged?.Invoke(this, "peak_analysis");
                double? gain = await RunPeakAnalysisAsync(input, duration, cancellationToken);
                if (gain == null)
                {
                    RaiseError("Peak normalization analysis failed.", ConverterError.PeakAnalysisFailed);
                    FileEnd?.Invoke(this, new FileEndEventArgs(input, ConverterError.PeakAnalysisFailed));
                    continue;
                }

                opts.Gain = gain.Value;
                RaiseMessage($"Peak analysis complete. Target gain: {opts.Gain:F2} dB");
            }
            else if (opts.AudioOutputMode != AudioOutputMode.Copy &&
                     (audioNorm == "loudnorm2" || audioNorm == "loudness_norm_2pass"))
            {
                StageChanged?.Invoke(this, "loudness_analysis_pass1");
                opts.ApplyGenreTargets();

                var analysisResult = await RunLoudnormAnalysisAsync(input, opts.ITarget, opts.TpTarget, opts.LraTarget, duration, cancellationToken);
                if (analysisResult == null)
                {
                    RaiseError("Loudness normalization analysis failed.", ConverterError.LoudnormAnalysisFailed);
                    FileEnd?.Invoke(this, new FileEndEventArgs(input, ConverterError.LoudnormAnalysisFailed));
                    continue;
                }

                opts.MeasuredI = analysisResult.InputI;
                opts.MeasuredTp = analysisResult.InputTp;
                opts.MeasuredLra = analysisResult.InputLra;
                opts.MeasuredThresh = analysisResult.InputThresh;
                opts.MeasuredOffset = analysisResult.TargetOffset;
                RaiseMessage($"Analysis: I={opts.MeasuredI:F2}, TP={opts.MeasuredTp:F2}, LRA={opts.MeasuredLra:F2}, Thresh={opts.MeasuredThresh:F2}, Offset={opts.MeasuredOffset:F2}");
            }

            // Step 3. Build command line
            string cmdArgs = CommandBuilder.BuildFfmpegArgs(input, output, opts, platform, _presetDb, videoInfo, _probeResult);

            if (opts.DryRun)
            {
                string dryCmd = $"\"{_tools.Ffmpeg}\" {cmdArgs}";
                RaiseMessage($"[DRY RUN] Command: {dryCmd}");
                FileEnd?.Invoke(this, new FileEndEventArgs(input, ConverterError.Ok));
                continue;
            }

            // Step 4. Encoding
            StageChanged?.Invoke(this, "encoding");
            var encodeError = await RunFfmpegEncodeAsync(cmdArgs, duration, cancellationToken);

            FileEnd?.Invoke(this, new FileEndEventArgs(input, encodeError));
        }

        Completed?.Invoke(this, EventArgs.Empty);
        return singleMuxWorkflow ? singleMuxResult : ConverterError.Ok;
    }

    public async Task<ConverterError> ProcessDirectM4vFilesWithEditBeforeMuxAsync(
        IEnumerable<string> files,
        ConvertOptions directOptions,
        ConvertOptions sourceConversionOptions,
        CancellationToken cancellationToken = default)
    {
        ConverterError Complete(ConverterError result)
        {
            Completed?.Invoke(this, EventArgs.Empty);
            return result;
        }

        if (AudioModePolicy.GetWorkflow(directOptions) != AudioWorkflowContext.DirectM4v ||
            !directOptions.M4vEditBeforeMux ||
            AudioModePolicy.GetWorkflow(sourceConversionOptions) == AudioWorkflowContext.DirectM4v)
        {
            RaiseError("Invalid Direct M4V edit-before-mux options.", ConverterError.InvalidOptions);
            return Complete(ConverterError.InvalidOptions);
        }

        var fileList = new List<string>(files);
        if (fileList.Count == 0)
        {
            RaiseError("No input files selected for Apple M4V creation.", ConverterError.InvalidOptions);
            return Complete(ConverterError.InvalidOptions);
        }

        string platform = RuntimeInformation.IsOSPlatform(OSPlatform.Windows) ? "windows" :
                          RuntimeInformation.IsOSPlatform(OSPlatform.OSX) ? "macos" : "linux";
        if (directOptions.DryRun)
        {
            foreach (string input in fileList)
            {
                string sourceOutput = CommandBuilder.BuildOutputFilePath(
                    input, sourceConversionOptions, platform, _presetDb);
                string m4vOutput = CommandBuilder.BuildOutputFilePath(
                    input, directOptions, platform, _presetDb);
                RaiseMessage($"[DRY RUN] [m4v edit-before-mux] {input} -> {sourceOutput} -> {m4vOutput}");
                FileEnd?.Invoke(this, new FileEndEventArgs(input, ConverterError.Ok));
            }
            return Complete(ConverterError.Ok);
        }

        if (string.IsNullOrEmpty(_tools.Ffmpeg) || !File.Exists(_tools.Ffmpeg) ||
            string.IsNullOrEmpty(_tools.Ffprobe) || !File.Exists(_tools.Ffprobe) ||
            string.IsNullOrEmpty(_tools.Mp4Box) || !File.Exists(_tools.Mp4Box))
        {
            RaiseError("Missing required tools (ffmpeg/ffprobe/MP4Box).", ConverterError.InvalidOptions);
            return Complete(ConverterError.InvalidOptions);
        }

        string outputDirectory = string.IsNullOrEmpty(directOptions.OutputDir)
            ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "ffmpeg_converter")
            : directOptions.OutputDir;
        try
        {
            Directory.CreateDirectory(outputDirectory);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or ArgumentException)
        {
            RaiseError(
                $"Failed to create or access output directory {outputDirectory}: {ex.Message}",
                ConverterError.HomeDirNotFound);
            return Complete(ConverterError.HomeDirNotFound);
        }

        sourceConversionOptions.OutputDir = outputDirectory;
        sourceConversionOptions.Overwrite = directOptions.Overwrite;
        sourceConversionOptions.DryRun = false;
        sourceConversionOptions.M4vEditBeforeMux = false;

        var intermediateFiles = new List<string>(fileList.Count);
        foreach (string input in fileList)
        {
            MuxVideoCodec sourceVideoCodec = MuxVideoCodec.Unknown;
            if (AudioModePolicy.GetWorkflow(sourceConversionOptions) == AudioWorkflowContext.Mux &&
                AudioModePolicy.TryGetMuxMode(sourceConversionOptions, out MuxOperationalMode sourceMuxMode) &&
                sourceMuxMode == MuxOperationalMode.Copy)
            {
                InputVideoInfo? info = await InputVideoInfo.ProbeAsync(
                    _tools.Ffprobe, input, cancellationToken);
                if (info == null)
                {
                    RaiseError(
                        $"Could not probe the source video codec for edit-before-mux: {input}",
                        ConverterError.FfprobeFailed);
                    return Complete(ConverterError.FfprobeFailed);
                }
                sourceVideoCodec = MuxPolicyResolver.ParseVideoCodec(info.CodecName);
            }

            try
            {
                intermediateFiles.Add(CommandBuilder.BuildOutputFilePath(
                    input, sourceConversionOptions, platform, _presetDb, sourceVideoCodec));
            }
            catch (Exception ex) when (ex is ArgumentException or IOException or NotSupportedException)
            {
                RaiseError($"Invalid edit-before-mux output path: {ex.Message}", ConverterError.InvalidOptions);
                return Complete(ConverterError.InvalidOptions);
            }
        }

        DateTime conversionStarted = DateTime.UtcNow;
        var sourceStatuses = new Dictionary<string, ConverterError>(StringComparer.Ordinal);
        var sourceConverter = new Converter(
            _tools, _presetDb, _probeResult, _replacementVideoProbe);
        sourceConverter.FileBegin += (_, args) => FileBegin?.Invoke(this, args);
        sourceConverter.FileEnd += (_, args) =>
        {
            sourceStatuses[args.FileName] = args.Status;
            FileEnd?.Invoke(this, args);
        };
        sourceConverter.StageChanged += (_, stage) => StageChanged?.Invoke(this, stage);
        sourceConverter.ProgressEncode += (_, args) => ProgressEncode?.Invoke(this, args);
        sourceConverter.ProgressAnalysis += (_, args) => ProgressAnalysis?.Invoke(this, args);
        sourceConverter.MessageLogged += (_, args) => MessageLogged?.Invoke(this, args);
        sourceConverter.ErrorOccurred += (_, args) => ErrorOccurred?.Invoke(this, args);

        ConverterError sourceResult = await sourceConverter.ProcessFilesAsync(
            fileList, sourceConversionOptions, cancellationToken);
        if (sourceResult != ConverterError.Ok)
            return Complete(sourceResult);
        if (cancellationToken.IsCancellationRequested)
            return Complete(ConverterError.SkipFile);

        for (int i = 0; i < fileList.Count; i++)
        {
            string input = fileList[i];
            string intermediate = intermediateFiles[i];
            if (!sourceStatuses.TryGetValue(input, out ConverterError sourceStatus) ||
                sourceStatus != ConverterError.Ok ||
                !File.Exists(intermediate) ||
                File.GetLastWriteTimeUtc(intermediate) < conversionStarted)
            {
                RaiseError(
                    $"Edit-before-mux source was not freshly produced: {intermediate}",
                    ConverterError.FfmpegFailed);
                return Complete(ConverterError.FfmpegFailed);
            }
        }

        int failureCount = 0;
        for (int i = 0; i < fileList.Count; i++)
        {
            string input = fileList[i];
            string intermediate = intermediateFiles[i];
            if (cancellationToken.IsCancellationRequested)
            {
                FileEnd?.Invoke(this, new FileEndEventArgs(input, ConverterError.SkipFile));
                return Complete(ConverterError.SkipFile);
            }

            string output = CommandBuilder.BuildOutputFilePath(
                input, directOptions, platform, _presetDb);
            if (File.Exists(output) && !directOptions.Overwrite)
            {
                RaiseError($"Output file already exists and overwrite is disabled: {output}",
                    ConverterError.OutputExists);
                FileEnd?.Invoke(this, new FileEndEventArgs(input, ConverterError.SkipFile));
                failureCount++;
                continue;
            }

            var pipeline = new AppleM4vPipeline();
            pipeline.StageChanged += (_, stage) => StageChanged?.Invoke(this, stage);
            pipeline.MessageLogged += (_, message) => RaiseMessage(message);
            pipeline.ErrorOccurred += (_, error) => RaiseError(error.Text, error.Code);
            ConverterError result = await pipeline.CreateM4vAsync(
                intermediate, output, directOptions, _tools, _probeResult, cancellationToken);
            FileEnd?.Invoke(this, new FileEndEventArgs(input, result));
            if (result != ConverterError.Ok)
            {
                failureCount++;
                if (result == ConverterError.SkipFile)
                    return Complete(result);
                continue;
            }

            try
            {
                File.Delete(intermediate);
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
            {
                RaiseMessage(
                    $"Apple M4V warning: failed to delete temporary converted file {intermediate}: {ex.Message}");
            }
        }

        return Complete(failureCount == 0 ? ConverterError.Ok : ConverterError.FfmpegFailed);
    }

    private async Task<AddTrackResult> ExecuteAddTrackAsync(
        AddTrackRequest request,
        CancellationToken cancellationToken)
    {
        AddTrackResult validation = AddTrackContract.Validate(request);
        if (!validation.Success)
        {
            RaiseError(validation.Message ?? "Add Track request validation failed.", validation.Error);
            return validation;
        }

        string input = request.SourcePath;
        string videoTrack = request.ReplacementVideoPath;
        string output = request.OutputPath;
        string effectiveOutputDir = request.OutputDirectory;
        ConvertOptions audioOptions = request.SourceOptions;
        ConvertOptions muxOptions = request.MuxOptions;
        string platform = request.Platform;
        var intermediateOptions = new ConvertOptions
        {
            Codec = "copy",
            Preset = "mkv",
            Deblock = 1,
            AudioNorm = audioOptions.AudioNorm,
            AudioOutputMode = request.Policy.AudioMode,
            Genre = audioOptions.Genre,
            Gain = audioOptions.Gain,
            ITarget = audioOptions.ITarget,
            TpTarget = audioOptions.TpTarget,
            LraTarget = audioOptions.LraTarget,
            MeasuredI = audioOptions.MeasuredI,
            MeasuredTp = audioOptions.MeasuredTp,
            MeasuredLra = audioOptions.MeasuredLra,
            MeasuredThresh = audioOptions.MeasuredThresh,
            MeasuredOffset = audioOptions.MeasuredOffset,
            OutputDir = effectiveOutputDir,
            Overwrite = true
        };
        string intermediateFile = Path.Combine(effectiveOutputDir,
            $"{Path.GetFileNameWithoutExtension(input)}_intermediate_{Guid.NewGuid():N}.mkv");

        try
        {
            cancellationToken.ThrowIfCancellationRequested();
            PolicyResult sourcePolicyResult = AudioModePolicy.ResolveSourcePreparationPolicy(
                platform, request.Policy.AudioMode);
            if (!sourcePolicyResult.Success)
            {
                RaiseError(sourcePolicyResult.ErrorMessage, ConverterError.InvalidOptions);
                return AddTrackResult.Failed(
                    ConverterError.InvalidOptions,
                    AddTrackStage.SourcePreparation,
                    sourcePolicyResult.ErrorMessage);
            }

            MuxSourcePreservationResult preservationResult =
                await MuxSourcePreservation.ProbeAsync(
                    _tools.Ffprobe, input, sourcePolicyResult.Policy!, cancellationToken);
            if (!preservationResult.Success)
            {
                string message = preservationResult.Error ?? "Could not resolve source stream preservation.";
                RaiseError(message, ConverterError.FfprobeFailed);
                return AddTrackResult.Failed(
                    ConverterError.FfprobeFailed,
                    AddTrackStage.SourcePreparation,
                    message);
            }
            foreach (string warning in preservationResult.Warnings)
                RaiseMessage(warning);

            double duration = await GetDurationAsync(input);
            string norm = intermediateOptions.AudioNorm.ToLowerInvariant();
            bool copyAudio = intermediateOptions.AudioOutputMode == AudioOutputMode.Copy;
            if (!copyAudio && (norm == "peak2" || norm == "peak_norm_2pass"))
            {
                StageChanged?.Invoke(this, "peak_analysis");
                double? gain = await RunPeakAnalysisAsync(input, duration, cancellationToken);
                if (gain.HasValue)
                    intermediateOptions.Gain = gain.Value;
            }
            else if (!copyAudio && (norm == "loudnorm2" || norm == "loudness_norm_2pass"))
            {
                StageChanged?.Invoke(this, "loudness_analysis_pass1");
                intermediateOptions.ApplyGenreTargets();
                LoudnormResult? analysis = await RunLoudnormAnalysisAsync(input,
                    intermediateOptions.ITarget, intermediateOptions.TpTarget,
                    intermediateOptions.LraTarget, duration, cancellationToken);
                if (analysis != null)
                {
                    intermediateOptions.MeasuredI = analysis.InputI;
                    intermediateOptions.MeasuredTp = analysis.InputTp;
                    intermediateOptions.MeasuredLra = analysis.InputLra;
                    intermediateOptions.MeasuredThresh = analysis.InputThresh;
                    intermediateOptions.MeasuredOffset = analysis.TargetOffset;
                }
            }

            StageChanged?.Invoke(this, "encoding intermediate");
            string arguments = CommandBuilder.BuildFfmpegArgs(input, intermediateFile,
                intermediateOptions, platform, _presetDb, null, _probeResult,
                preservationResult.Plan);
            ConverterError conversionError = await RunFfmpegEncodeAsync(arguments, duration, cancellationToken);
            if (conversionError != ConverterError.Ok)
                return AddTrackResult.Failed(
                    conversionError, AddTrackStage.SourcePreparation, "Add Track source preparation failed.");

            if (!await MuxPipeline.ValidatePreparedSourceAsync(
                    _tools.Ffprobe, intermediateFile, cancellationToken))
            {
                const string message = "Add Track source preparation produced no usable intermediate.";
                RaiseError(message, ConverterError.FfprobeFailed);
                return AddTrackResult.Failed(
                    ConverterError.FfprobeFailed, AddTrackStage.SourcePreparation, message);
            }

            var mux = new MuxPipeline();
            mux.StageChanged += (s, stage) => StageChanged?.Invoke(this, stage);
            mux.MessageLogged += (s, message) => RaiseMessage(message);
            mux.ErrorOccurred += (s, error) => RaiseError(error.Text, error.Code);
            ConverterError muxError = await mux.RunMuxPostprocessAsync(intermediateFile, videoTrack,
                output, muxOptions, _tools, _probeResult, cancellationToken,
                request.Policy, preservationResult.Plan);
            return muxError == ConverterError.Ok
                ? AddTrackResult.Completed()
                : AddTrackResult.Failed(
                    muxError, AddTrackStage.FinalAssembly, "Add Track final video assembly failed.");
        }
        catch (OperationCanceledException)
        {
            return AddTrackResult.Failed(
                ConverterError.SkipFile, AddTrackStage.SourcePreparation, "Add Track operation was cancelled.");
        }
        finally
        {
            try
            {
                if (File.Exists(intermediateFile))
                    File.Delete(intermediateFile);
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
            {
                RaiseMessage($"Add Track intermediate cleanup failed: {ex.Message}");
            }
        }
    }

    private static (ConverterError Error, string Message) DescribeReplacementProbeFailure(
        InputVideoProbeResult result)
    {
        ConverterError error = result.Failure switch
        {
            InputVideoProbeFailure.InputNotFound => ConverterError.InputNotFound,
            InputVideoProbeFailure.InputNotReadable => ConverterError.InputNotReadable,
            _ => ConverterError.FfprobeFailed
        };
        string message = result.Failure switch
        {
            InputVideoProbeFailure.ProbeToolMissing => "ffprobe binary not found.",
            InputVideoProbeFailure.InputNotFound => "Replacement video track was not found.",
            InputVideoProbeFailure.InputNotReadable => "Replacement video track is not readable.",
            InputVideoProbeFailure.NoVideoStream => "Replacement file contains no video stream.",
            InputVideoProbeFailure.MissingCodec => "Could not determine the replacement video codec.",
            InputVideoProbeFailure.ProcessStartFailed => "Could not start ffprobe for the replacement video track.",
            _ => "ffprobe could not read the replacement video track."
        };
        if (!string.IsNullOrEmpty(result.Detail))
            message += $" {result.Detail}";
        return (error, message);
    }

    private static void RemoveTaskDirectory(string path)
    {
        if (!Directory.Exists(path))
            return;

        foreach (string entry in Directory.EnumerateFileSystemEntries(path))
        {
            FileAttributes attributes = File.GetAttributes(entry);
            bool isDirectory = (attributes & FileAttributes.Directory) != 0;
            bool isLink = (attributes & FileAttributes.ReparsePoint) != 0;
            if (isDirectory && !isLink)
                RemoveTaskDirectory(entry);
            else if (isDirectory)
                Directory.Delete(entry);
            else
                File.Delete(entry);
        }

        Directory.Delete(path);
    }

    public async Task<double?> RunPeakAnalysisAsync(string inputPath, double duration, CancellationToken cancellationToken = default)
    {
        string args = $"-hwaccel none -vn -i \"{inputPath}\" -af volumedetect -f null -";
        try
        {
            using var process = new Process
            {
                StartInfo = new ProcessStartInfo
                {
                    FileName = _tools.Ffmpeg,
                    Arguments = args,
                    UseShellExecute = false,
                    CreateNoWindow = true,
                    RedirectStandardError = true,
                    RedirectStandardOutput = true
                }
            };

            double maxVolume = 0.0;
            bool foundMaxVolume = false;
            var startTimestamp = DateTime.UtcNow;

            process.Start();

            using var reader = process.StandardError;
            while (await reader.ReadLineAsync(cancellationToken) is { } line)
            {
                if (line.Contains("max_volume:"))
                {
                    int idx = line.IndexOf("max_volume:");
                    string valStr = line[(idx + "max_volume:".Length)..].Trim();
                    int spaceIdx = valStr.IndexOf(' ');
                    if (spaceIdx > 0) valStr = valStr[..spaceIdx];

                    if (double.TryParse(valStr, System.Globalization.CultureInfo.InvariantCulture, out double parsedVal))
                    {
                        maxVolume = parsedVal;
                        foundMaxVolume = true;
                    }
                }

                if (duration > 0 && line.Contains("time="))
                {
                    int tIdx = line.IndexOf("time=");
                    string timeStr = line[(tIdx + 5)..].Trim();
                    int spaceIdx = timeStr.IndexOf(' ');
                    if (spaceIdx > 0) timeStr = timeStr[..spaceIdx];

                    if (TimeSpan.TryParse(timeStr, out var ts))
                    {
                        double cur = ts.TotalSeconds;
                        double percent = (cur / duration) * 100.0;
                        if (percent > 100) percent = 100;

                        double elapsed = (DateTime.UtcNow - startTimestamp).TotalSeconds;
                        double eta = percent > 0 ? elapsed * (100.0 - percent) / percent : 0;
                        ProgressAnalysis?.Invoke(this, new ProgressAnalysisEventArgs((float)percent, (float)eta));
                    }
                }
            }

            await process.WaitForExitAsync(cancellationToken);

            if (process.ExitCode == 0 && foundMaxVolume)
            {
                // Target is -3.0 dB
                double target = -3.0;
                return target - maxVolume;
            }
        }
        catch (Exception ex)
        {
            RaiseError($"Peak analysis failed: {ex.Message}", ConverterError.PeakAnalysisFailed);
        }

        return null;
    }

    public async Task<double> GetDurationAsync(string inputPath)
    {
        if (string.IsNullOrEmpty(_tools.Ffprobe) || !File.Exists(_tools.Ffprobe)) return 0;

        string args = $"-v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 \"{inputPath}\"";
        try
        {
            using var process = new Process
            {
                StartInfo = new ProcessStartInfo
                {
                    FileName = _tools.Ffprobe,
                    Arguments = args,
                    UseShellExecute = false,
                    CreateNoWindow = true,
                    RedirectStandardOutput = true
                }
            };
            process.Start();
            string output = await process.StandardOutput.ReadToEndAsync();
            await process.WaitForExitAsync();
            if (double.TryParse(output.Trim(), System.Globalization.CultureInfo.InvariantCulture, out double res))
            {
                return res;
            }
        }
        catch { }
        return 0;
    }

    public class LoudnormResult
    {
        public double InputI { get; set; }
        public double InputTp { get; set; }
        public double InputLra { get; set; }
        public double InputThresh { get; set; }
        public double OutputI { get; set; }
        public double OutputTp { get; set; }
        public double OutputLra { get; set; }
        public double OutputThresh { get; set; }
        public double NormalizationGain { get; set; }
        public double TargetOffset { get; set; }
    }

    public async Task<LoudnormResult?> RunLoudnormAnalysisAsync(string inputPath, double iTarget, double tpTarget, double lraTarget, double duration = 0.0, CancellationToken cancellationToken = default)
    {
        string args = $"-i \"{inputPath}\" -af loudnorm=I={iTarget:F1}:TP={tpTarget:F1}:LRA={lraTarget:F1}:print_format=json -f null -";
        try
        {
            using var process = new Process
            {
                StartInfo = new ProcessStartInfo
                {
                    FileName = _tools.Ffmpeg,
                    Arguments = args,
                    UseShellExecute = false,
                    CreateNoWindow = true,
                    RedirectStandardError = true,
                    RedirectStandardOutput = true
                }
            };

            var stdErrBuffer = new StringBuilder();
            var startTimestamp = DateTime.UtcNow;

            process.Start();

            using var reader = process.StandardError;
            while (await reader.ReadLineAsync(cancellationToken) is { } line)
            {
                stdErrBuffer.AppendLine(line);

                if (duration > 0 && line.Contains("time="))
                {
                    int tIdx = line.IndexOf("time=");
                    string timeStr = line[(tIdx + 5)..].Trim();
                    int spaceIdx = timeStr.IndexOf(' ');
                    if (spaceIdx > 0) timeStr = timeStr[..spaceIdx];

                    if (TimeSpan.TryParse(timeStr, out var ts))
                    {
                        double cur = ts.TotalSeconds;
                        double percent = (cur / duration) * 100.0;
                        if (percent > 100) percent = 100;

                        double elapsed = (DateTime.UtcNow - startTimestamp).TotalSeconds;
                        double eta = percent > 0 ? elapsed * (100.0 - percent) / percent : 0;
                        ProgressAnalysis?.Invoke(this, new ProgressAnalysisEventArgs((float)percent, (float)eta));
                    }
                }
            }

            await process.WaitForExitAsync(cancellationToken);

            string errors = stdErrBuffer.ToString();

            // Find JSON block in stderr output
            int jsonStart = errors.IndexOf('{');
            int jsonEnd = errors.LastIndexOf('}');
            if (jsonStart >= 0 && jsonEnd > jsonStart)
            {
                string jsonText = errors.Substring(jsonStart, jsonEnd - jsonStart + 1);
                using var doc = JsonDocument.Parse(jsonText);
                var root = doc.RootElement;

                var res = new LoudnormResult();
                if (root.TryGetProperty("input_i", out var p)) res.InputI = double.Parse(p.GetString()!, System.Globalization.CultureInfo.InvariantCulture);
                if (root.TryGetProperty("input_tp", out p)) res.InputTp = double.Parse(p.GetString()!, System.Globalization.CultureInfo.InvariantCulture);
                if (root.TryGetProperty("input_lra", out p)) res.InputLra = double.Parse(p.GetString()!, System.Globalization.CultureInfo.InvariantCulture);
                if (root.TryGetProperty("input_thresh", out p)) res.InputThresh = double.Parse(p.GetString()!, System.Globalization.CultureInfo.InvariantCulture);
                if (root.TryGetProperty("target_offset", out p)) res.TargetOffset = double.Parse(p.GetString()!, System.Globalization.CultureInfo.InvariantCulture);

                return res;
            }
        }
        catch (Exception ex)
        {
            RaiseError($"Loudnorm analysis failed with exception: {ex.Message}", ConverterError.LoudnormAnalysisFailed);
        }

        return null;
    }

    private async Task<ConverterError> RunFfmpegEncodeAsync(string cmdArgs, double duration, CancellationToken cancellationToken)
    {
        try
        {
            using var process = new Process
            {
                StartInfo = new ProcessStartInfo
                {
                    FileName = _tools.Ffmpeg,
                    Arguments = cmdArgs,
                    UseShellExecute = false,
                    CreateNoWindow = true,
                    RedirectStandardOutput = true,
                    RedirectStandardError = true
                }
            };

            var startTimestamp = DateTime.UtcNow;
            double outTimeMs = 0;
            double fps = 0;

            process.Start();

            // Asynchronously read the standard output (which has progress statements)
            var readOutputTask = Task.Run(async () =>
            {
                using var reader = process.StandardOutput;
                while (await reader.ReadLineAsync(cancellationToken) is { } line)
                {
                    if (line.StartsWith("out_time_ms="))
                    {
                        if (double.TryParse(line.Substring(12), System.Globalization.CultureInfo.InvariantCulture, out double val))
                        {
                            outTimeMs = val;
                        }
                    }
                    else if (line.StartsWith("fps="))
                    {
                        if (double.TryParse(line.Substring(4), System.Globalization.CultureInfo.InvariantCulture, out double val))
                        {
                            fps = val;
                        }
                    }
                    else if (line.StartsWith("progress="))
                    {
                        if (line.Contains("end"))
                        {
                            ProgressEncode?.Invoke(this, new ProgressEncodeEventArgs(100.0f, (float)fps, 0));
                            break;
                        }
                    }

                    if (duration > 0 && outTimeMs > 0)
                    {
                        double currentSeconds = outTimeMs / 1000000.0;
                        double percent = (currentSeconds / duration) * 100.0;
                        if (percent > 100) percent = 100;

                        double elapsed = (DateTime.UtcNow - startTimestamp).TotalSeconds;
                        double eta = percent > 0 ? elapsed * (100.0 - percent) / percent : 0;

                        ProgressEncode?.Invoke(this, new ProgressEncodeEventArgs((float)percent, (float)fps, (float)eta));
                    }
                }
            });

            await process.WaitForExitAsync(cancellationToken);
            await readOutputTask;

            if (process.ExitCode != 0)
            {
                RaiseError($"ffmpeg exited with non-zero code: {process.ExitCode}", ConverterError.FfmpegFailed);
                return ConverterError.FfmpegFailed;
            }

            return ConverterError.Ok;
        }
        catch (OperationCanceledException)
        {
            RaiseMessage("Conversion was cancelled by user.");
            return ConverterError.SkipFile;
        }
        catch (Exception ex)
        {
            RaiseError($"Encoding execution exception: {ex.Message}", ConverterError.FfmpegFailed);
            return ConverterError.FfmpegFailed;
        }
    }

    private void RaiseMessage(string text) => MessageLogged?.Invoke(this, new MessageEventArgs(text));
    private void RaiseError(string text, ConverterError code) => ErrorOccurred?.Invoke(this, new ErrorEventArgs(text, code));
}
