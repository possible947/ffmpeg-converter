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
    // public event EventHandler<ProgressAnalysisEventArgs>? ProgressAnalysis;
    public event EventHandler<MessageEventArgs>? MessageLogged;
    public event EventHandler<ErrorEventArgs>? ErrorOccurred;
    public event EventHandler? Completed;

    private readonly ToolPaths _tools;
    private readonly PresetDb _presetDb;

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
            string platform = RuntimeInformation.IsOSPlatform(OSPlatform.Windows) ? "windows" :
                             RuntimeInformation.IsOSPlatform(OSPlatform.OSX) ? "macos" : "linux";

            var presetInfo = _presetDb.GetPresetInfo(platform, opts.Codec, opts.Preset);
            string extension = presetInfo?.Container ?? "mkv";
            
            // Handle specific pipelines (Apple M4V / Mux)
            if (opts.Codec.Equals("m4v", StringComparison.OrdinalIgnoreCase)) extension = "m4v";
            else if (opts.Codec.Equals("mux", StringComparison.OrdinalIgnoreCase)) extension = "mkv";

            string baseName = Path.GetFileNameWithoutExtension(input);
            string output = Path.Combine(effectiveOutputDir, $"{baseName}_converted.{extension}");

            if (File.Exists(output) && !opts.Overwrite)
            {
                RaiseError($"Output file already exists and overwrite is disabled: {output}", ConverterError.OutputExists);
                FileEnd?.Invoke(this, new FileEndEventArgs(input, ConverterError.SkipFile));
                continue;
            }

            // Step 1. Get Duration and bit depth info for logs/estimation
            double duration = await GetDurationAsync(input);
            RaiseMessage($"Input duration: {duration:F2} seconds");

            // Step 2. Two-Pass loudness normalization analysis if requested
            if (opts.AudioNorm.Equals("loudness_norm_2pass", StringComparison.OrdinalIgnoreCase))
            {
                StageChanged?.Invoke(this, "loudness_analysis_pass1");
                var analysisResult = await RunLoudnormAnalysisAsync(input, opts.ITarget, opts.TpTarget, opts.LraTarget, cancellationToken);
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
                RaiseMessage($"Analysis: I={opts.MeasuredI}, TP={opts.MeasuredTp}, LRA={opts.MeasuredLra}");
            }

            // Step 3. Build command line
            if (opts.DryRun)
            {
                string dryCmd = BuildFfmpegCommand(input, output, opts, presetInfo);
                RaiseMessage($"[DRY RUN] Command: {dryCmd}");
                FileEnd?.Invoke(this, new FileEndEventArgs(input, ConverterError.Ok));
                continue;
            }

            string cmdArgs = BuildFfmpegCommandArgs(input, output, opts, presetInfo);
            
            // Step 4. Encoding
            StageChanged?.Invoke(this, "encoding");
            var encodeError = await RunFfmpegEncodeAsync(cmdArgs, duration, cancellationToken);

            FileEnd?.Invoke(this, new FileEndEventArgs(input, encodeError));
        }

        Completed?.Invoke(this, EventArgs.Empty);
        return ConverterError.Ok;
    }

    private string BuildFfmpegCommand(string input, string output, ConvertOptions opts, PresetInfo? presetInfo)
    {
        return $"\"{_tools.Ffmpeg}\" {BuildFfmpegCommandArgs(input, output, opts, presetInfo)}";
    }

    private string BuildFfmpegCommandArgs(string input, string output, ConvertOptions opts, PresetInfo? presetInfo)
    {
        var sb = new StringBuilder();
        sb.Append("-hide_banner ");

        if (opts.Overwrite) sb.Append("-y ");
        else sb.Append("-n ");

        // Hardware device setups
        if (!string.IsNullOrEmpty(opts.HwDevice))
        {
            if (opts.Codec.Contains("vaapi", StringComparison.OrdinalIgnoreCase))
            {
                sb.Append($"-vaapi_device \"{opts.HwDevice}\" ");
            }
        }

        // Input file
        sb.Append($"-i \"{input}\" ");

        // Video codec & preset arguments from DB or options
        if (presetInfo != null && !string.IsNullOrEmpty(presetInfo.FfmpegArgs))
        {
            sb.Append($"{presetInfo.FfmpegArgs} ");
        }
        else if (opts.Codec.Equals("copy", StringComparison.OrdinalIgnoreCase))
        {
            sb.Append("-c:v copy ");
        }
        else
        {
            sb.Append($"-c:v {opts.Codec} ");
            if (!string.IsNullOrEmpty(opts.Preset) && !opts.Preset.Equals("default", StringComparison.OrdinalIgnoreCase))
            {
                sb.Append($"-preset {opts.Preset} ");
            }
        }

        // Deblock filter
        if (opts.Deblock == 2) sb.Append("-vf deblock=filter=weak ");
        else if (opts.Deblock == 3) sb.Append("-vf deblock=filter=strong ");

        // Audio normalization & encoding
        BuildAudioArgs(sb, opts);

        // Progress & output
        sb.Append("-progress - -nostats ");
        sb.Append($"\"{output}\"");

        return sb.ToString();
    }

    private void BuildAudioArgs(StringBuilder sb, ConvertOptions opts)
    {
        // Output mode
        if (opts.AudioOutputMode.Equals("pcm", StringComparison.OrdinalIgnoreCase))
        {
            sb.Append("-c:a pcm_s16le ");
        }
        else if (opts.AudioOutputMode.Equals("fdk_aac_320", StringComparison.OrdinalIgnoreCase))
        {
            sb.Append("-c:a libfdk_aac -b:a 320k ");
        }
        else if (opts.AudioOutputMode.Equals("fdk_aac_320_ac3_640", StringComparison.OrdinalIgnoreCase))
        {
            sb.Append("-map 0:v -map 0:a -map 0:a -c:a:0 libfdk_aac -b:a:0 320k -c:a:1 ac3 -b:a:1 640k ");
        }
        else
        {
            sb.Append("-c:a copy ");
        }

        // Audio Filter
        if (opts.AudioNorm.Equals("peak_norm", StringComparison.OrdinalIgnoreCase))
        {
            sb.Append("-af volume=0dB "); // simple peak normalisation placeholder
        }
        else if (opts.AudioNorm.Equals("peak_norm_2pass", StringComparison.OrdinalIgnoreCase) && opts.Gain != 0)
        {
            sb.Append($"-af volume={opts.Gain:F2}dB ");
        }
        else if (opts.AudioNorm.Equals("loudness_norm", StringComparison.OrdinalIgnoreCase))
        {
            sb.Append($"-af loudnorm=I={opts.ITarget:F1}:TP={opts.TpTarget:F1}:LRA={opts.LraTarget:F1} ");
        }
        else if (opts.AudioNorm.Equals("loudness_norm_2pass", StringComparison.OrdinalIgnoreCase))
        {
            sb.Append($"-af loudnorm=I={opts.ITarget:F1}:TP={opts.TpTarget:F1}:LRA={opts.LraTarget:F1}:measured_I={opts.MeasuredI:F1}:measured_TP={opts.MeasuredTp:F1}:measured_LRA={opts.MeasuredLra:F1}:measured_thresh={opts.MeasuredThresh:F1}:linear=true:offset={opts.MeasuredOffset:F1} ");
        }
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

    public async Task<LoudnormResult?> RunLoudnormAnalysisAsync(string inputPath, double iTarget, double tpTarget, double lraTarget, CancellationToken cancellationToken = default)
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
            process.ErrorDataReceived += (s, e) => { if (e.Data != null) stdErrBuffer.AppendLine(e.Data); };

            process.Start();
            process.BeginErrorReadLine();
            
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
