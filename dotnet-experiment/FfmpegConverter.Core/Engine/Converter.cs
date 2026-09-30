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
    private HardwareProbeResult? _probeResult;

    public Converter()
    {
        _tools = ToolDiscovery.ResolveAll();
        _presetDb = PresetDb.Load();
    }

    public async Task<ConverterError> ProcessFilesAsync(IEnumerable<string> files, ConvertOptions opts, CancellationToken cancellationToken = default)
    {
        if (string.IsNullOrEmpty(_tools.Ffmpeg))
        {
            RaiseError("ffmpeg binary not found on the system.", ConverterError.PlatformInitFailed);
            return ConverterError.PlatformInitFailed;
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

        int index = 0;
        var fileList = new List<string>(files);
        int total = fileList.Count;

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
                continue;
            }

            // Determine output file path
            string output = CommandBuilder.BuildOutputFilePath(input, opts, platform, _presetDb);

            if (File.Exists(output) && !opts.Overwrite)
            {
                RaiseError($"Output file already exists and overwrite is disabled: {output}", ConverterError.OutputExists);
                FileEnd?.Invoke(this, new FileEndEventArgs(input, ConverterError.SkipFile));
                continue;
            }

            // ====================================================
            // Apple M4V Pipeline
            // ====================================================
            if (opts.Codec.Equals("m4v", StringComparison.OrdinalIgnoreCase))
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
            if (opts.Codec.Equals("mux", StringComparison.OrdinalIgnoreCase))
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

                // Intermediate conversion with video codec = copy
                var intermediateOpts = new ConvertOptions
                {
                    Codec = "copy",
                    Preset = "default",
                    Deblock = 1,
                    AudioNorm = opts.AudioNorm,
                    AudioOutputMode = opts.AudioOutputMode,
                    Genre = opts.Genre,
                    Gain = opts.Gain,
                    ITarget = opts.ITarget,
                    TpTarget = opts.TpTarget,
                    LraTarget = opts.LraTarget,
                    OutputDir = effectiveOutputDir,
                    Overwrite = true
                };

                string intermediateFile = Path.Combine(effectiveOutputDir, $"{Path.GetFileNameWithoutExtension(input)}_intermediate_{Guid.NewGuid():N}.mkv");

                // Step 1. Two-Pass Audio Analysis if needed for intermediate
                string aNorm = intermediateOpts.AudioNorm.ToLowerInvariant();
                double inDuration = await GetDurationAsync(input);
                if (aNorm == "peak2" || aNorm == "peak_norm_2pass")
                {
                    StageChanged?.Invoke(this, "peak_analysis");
                    double? gain = await RunPeakAnalysisAsync(input, inDuration, cancellationToken);
                    if (gain != null) intermediateOpts.Gain = gain.Value;
                }
                else if (aNorm == "loudnorm2" || aNorm == "loudness_norm_2pass")
                {
                    StageChanged?.Invoke(this, "loudness_analysis_pass1");
                    intermediateOpts.ApplyGenreTargets();
                    var aRes = await RunLoudnormAnalysisAsync(input, intermediateOpts.ITarget, intermediateOpts.TpTarget, intermediateOpts.LraTarget, inDuration, cancellationToken);
                    if (aRes != null)
                    {
                        intermediateOpts.MeasuredI = aRes.InputI;
                        intermediateOpts.MeasuredTp = aRes.InputTp;
                        intermediateOpts.MeasuredLra = aRes.InputLra;
                        intermediateOpts.MeasuredThresh = aRes.InputThresh;
                        intermediateOpts.MeasuredOffset = aRes.TargetOffset;
                    }
                }

                // Encode intermediate file
                StageChanged?.Invoke(this, "encoding intermediate");
                string intermediateArgs = CommandBuilder.BuildFfmpegArgs(input, intermediateFile, intermediateOpts, platform, _presetDb, null, _probeResult);
                var interErr = await RunFfmpegEncodeAsync(intermediateArgs, inDuration, cancellationToken);
                if (interErr != ConverterError.Ok)
                {
                    try { if (File.Exists(intermediateFile)) File.Delete(intermediateFile); } catch { }
                    FileEnd?.Invoke(this, new FileEndEventArgs(input, interErr));
                    continue;
                }

                // Step 2. Mux post-process
                var muxPipe = new MuxPipeline();
                muxPipe.StageChanged += (s, ev) => StageChanged?.Invoke(this, ev);
                muxPipe.MessageLogged += (s, ev) => RaiseMessage(ev);
                muxPipe.ErrorOccurred += (s, ev) => RaiseError(ev.Text, ev.Code);

                var muxErr = await muxPipe.RunMuxPostprocessAsync(intermediateFile, opts.VideoTrackPath, output, opts, _tools, _probeResult, cancellationToken);

                try { if (File.Exists(intermediateFile)) File.Delete(intermediateFile); } catch { }

                FileEnd?.Invoke(this, new FileEndEventArgs(input, muxErr));
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
            if (audioNorm == "peak2" || audioNorm == "peak_norm_2pass")
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
            else if (audioNorm == "loudnorm2" || audioNorm == "loudness_norm_2pass")
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
        return ConverterError.Ok;
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
