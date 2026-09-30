using System;
using System.Diagnostics;
using System.IO;
using System.Threading;
using System.Threading.Tasks;

namespace FfmpegConverter.Core.Engine;

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
        if (string.IsNullOrEmpty(ffprobePath) || !File.Exists(ffprobePath) || !File.Exists(inputPath))
            return null;

        string args = $"-v error -select_streams v:0 " +
                      $"-show_entries stream=codec_name,pix_fmt,bits_per_raw_sample,profile,width,height,avg_frame_rate,color_range,color_primaries,color_transfer,color_space " +
                      $"-of default=noprint_wrappers=1 \"{inputPath}\"";

        try
        {
            using var process = new Process
            {
                StartInfo = new ProcessStartInfo
                {
                    FileName = ffprobePath,
                    Arguments = args,
                    UseShellExecute = false,
                    CreateNoWindow = true,
                    RedirectStandardOutput = true,
                    RedirectStandardError = true
                }
            };

            process.Start();
            string output = await process.StandardOutput.ReadToEndAsync(cancellationToken);
            await process.WaitForExitAsync(cancellationToken);

            if (process.ExitCode == 0 && !string.IsNullOrWhiteSpace(output))
            {
                return ParseText(output);
            }
        }
        catch
        {
            // ignore probing failures, return null
        }

        return null;
    }
}
