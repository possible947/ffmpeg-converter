using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using FfmpegConverter.Core.Policy;

namespace FfmpegConverter.Core.Engine;

public enum InputVideoProbeFailure
{
    None,
    ProbeToolMissing,
    InputNotFound,
    InputNotReadable,
    ProcessStartFailed,
    ProbeFailed,
    NoVideoStream,
    MissingCodec
}

public sealed record InputVideoProbeResult(
    InputVideoInfo? VideoInfo,
    MuxVideoCodec Codec,
    InputVideoProbeFailure Failure,
    string? Detail)
{
    public bool Success => Failure == InputVideoProbeFailure.None && VideoInfo != null;

    public static InputVideoProbeResult FromProcessResult(int exitCode, string output, string error)
    {
        if (exitCode != 0)
            return Failed(InputVideoProbeFailure.ProbeFailed, string.IsNullOrWhiteSpace(error) ? null : error.Trim());

        if (string.IsNullOrWhiteSpace(output))
            return Failed(InputVideoProbeFailure.NoVideoStream, null);

        InputVideoInfo info = InputVideoInfo.ParseText(output);
        if (string.IsNullOrWhiteSpace(info.CodecName))
            return Failed(InputVideoProbeFailure.MissingCodec, null);

        return new InputVideoProbeResult(
            info,
            MuxPolicyResolver.ParseVideoCodec(info.CodecName),
            InputVideoProbeFailure.None,
            null);
    }

    public static InputVideoProbeResult Failed(InputVideoProbeFailure failure, string? detail) =>
        new(null, MuxVideoCodec.Unknown, failure, detail);
}

public class InputVideoInfo
{
    public string CodecName { get; set; } = "";
    public string PixelFormat { get; set; } = "";
    public string Profile { get; set; } = "";
    public string ColorRange { get; set; } = "";
    public string ColorPrimaries { get; set; } = "";
    public string ColorTransfer { get; set; } = "";
    public string ColorSpace { get; set; } = "";
    public int BitDepth { get; set; } = 8;
    public int Width { get; set; } = 0;
    public int Height { get; set; } = 0;
    public double Fps { get; set; } = 0.0;

    public static int BitDepthFromPixelFormat(string pixelFormat)
    {
        if (string.IsNullOrEmpty(pixelFormat) || pixelFormat.Equals("N/A", StringComparison.OrdinalIgnoreCase))
            return 8;

        if (pixelFormat.Contains("12le") || pixelFormat.Contains("12be") || pixelFormat.Contains("p012"))
            return 12;
        if (pixelFormat.Contains("10le") || pixelFormat.Contains("10be") || pixelFormat.Contains("p010") || pixelFormat.Contains("x2rgb10"))
            return 10;
        if (pixelFormat.Contains("14le") || pixelFormat.Contains("14be"))
            return 14;
        if (pixelFormat.Contains("16le") || pixelFormat.Contains("16be") || pixelFormat.Contains("p016"))
            return 16;
        if (pixelFormat.Contains("9le") || pixelFormat.Contains("9be"))
            return 9;

        return 8;
    }

    public static InputVideoInfo ParseText(string text)
    {
        var info = new InputVideoInfo();
        if (string.IsNullOrEmpty(text))
            return info;

        using var reader = new StringReader(text);
        string? line;
        while ((line = reader.ReadLine()) != null)
        {
            int eqIdx = line.IndexOf('=');
            if (eqIdx <= 0) continue;

            string key = line[..eqIdx].Trim();
            string val = line[(eqIdx + 1)..].Trim();

            if (val.Equals("N/A", StringComparison.OrdinalIgnoreCase))
                val = "";

            switch (key)
            {
                case "codec_name":
                    info.CodecName = val;
                    break;
                case "pix_fmt":
                    info.PixelFormat = val;
                    break;
                case "profile":
                    info.Profile = val;
                    break;
                case "color_range":
                    info.ColorRange = val;
                    break;
                case "color_primaries":
                    info.ColorPrimaries = val;
                    break;
                case "color_transfer":
                    info.ColorTransfer = val;
                    break;
                case "color_space":
                    info.ColorSpace = val;
                    break;
                case "bits_per_raw_sample":
                    if (int.TryParse(val, out int b) && b > 0)
                        info.BitDepth = b;
                    break;
                case "width":
                    if (int.TryParse(val, out int w))
                        info.Width = w;
                    break;
                case "height":
                    if (int.TryParse(val, out int h))
                        info.Height = h;
                    break;
                case "avg_frame_rate":
                case "r_frame_rate":
                    if (!string.IsNullOrEmpty(val))
                    {
                        var parts = val.Split('/');
                        if (parts.Length == 2 &&
                            double.TryParse(parts[0], System.Globalization.CultureInfo.InvariantCulture, out double num) &&
                            double.TryParse(parts[1], System.Globalization.CultureInfo.InvariantCulture, out double den) &&
                            den > 0)
                        {
                            info.Fps = num / den;
                        }
                        else if (double.TryParse(val, System.Globalization.CultureInfo.InvariantCulture, out double directFps))
                        {
                            info.Fps = directFps;
                        }
                    }
                    break;
            }
        }

        if (!string.IsNullOrEmpty(info.PixelFormat))
        {
            int depth = BitDepthFromPixelFormat(info.PixelFormat);
            if (depth > 0)
                info.BitDepth = depth;
        }

        return info;
    }

    public static async Task<InputVideoInfo?> ProbeAsync(string ffprobePath, string inputPath, CancellationToken cancellationToken = default)
    {
        InputVideoProbeResult result = await ProbeDetailedAsync(ffprobePath, inputPath, cancellationToken);
        return result.Success ? result.VideoInfo : null;
    }

    public static async Task<InputVideoProbeResult> ProbeDetailedAsync(
        string ffprobePath,
        string inputPath,
        CancellationToken cancellationToken = default)
    {
        if (string.IsNullOrEmpty(ffprobePath) || !File.Exists(ffprobePath))
            return InputVideoProbeResult.Failed(InputVideoProbeFailure.ProbeToolMissing, null);
        if (string.IsNullOrEmpty(inputPath) || !File.Exists(inputPath))
        {
            InputVideoProbeFailure failure = !string.IsNullOrEmpty(inputPath) && Directory.Exists(inputPath)
                ? InputVideoProbeFailure.InputNotReadable
                : InputVideoProbeFailure.InputNotFound;
            return InputVideoProbeResult.Failed(failure, null);
        }

        try
        {
            using var input = new FileStream(inputPath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
        }
        catch (IOException ex)
        {
            return InputVideoProbeResult.Failed(InputVideoProbeFailure.InputNotReadable, ex.Message);
        }
        catch (UnauthorizedAccessException ex)
        {
            return InputVideoProbeResult.Failed(InputVideoProbeFailure.InputNotReadable, ex.Message);
        }

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
            "-v", "error", "-select_streams", "v:0", "-show_entries",
            "stream=codec_name,pix_fmt,bits_per_raw_sample,profile,width,height,avg_frame_rate,color_range,color_primaries,color_transfer,color_space",
            "-of", "default=noprint_wrappers=1", inputPath
        })
            startInfo.ArgumentList.Add(argument);

        using var process = new Process { StartInfo = startInfo };
        try
        {
            if (!process.Start())
                return InputVideoProbeResult.Failed(InputVideoProbeFailure.ProcessStartFailed, "ffprobe process did not start.");
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
            string error = await errorTask;
            return InputVideoProbeResult.FromProcessResult(process.ExitCode, output, error);
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        catch (Exception ex) when (ex is Win32Exception or InvalidOperationException or IOException or UnauthorizedAccessException)
        {
            return InputVideoProbeResult.Failed(InputVideoProbeFailure.ProcessStartFailed, ex.Message);
        }
    }
}
