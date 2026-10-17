using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using System.Reflection;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using FfmpegConverter.Core.Models;
using FfmpegConverter.Core.Presets;

namespace FfmpegConverter.Core.Engine;

public sealed class HqAdapter
{
    private string _lastStage = "";
    private string _lastMessage = "";

    public event EventHandler<string>? StageChanged;
    public event EventHandler<string>? MessageLogged;
    public event EventHandler<ProgressEncodeEventArgs>? ProgressEncode;
    public event EventHandler<ProgressAnalysisEventArgs>? ProgressAnalysis;
    public event EventHandler<ErrorEventArgs>? ErrorOccurred;

    public async Task<(int Result, string Error)> RunJobAsync(
        string runtimeRoot,
        string inputPath,
        string workDir,
        string codec,
        string preset,
        CancellationToken cancellationToken = default)
    {
        _lastStage = "";
        _lastMessage = "";
        if (cancellationToken.IsCancellationRequested)
            return (1, "HQ job cancelled.");

        HqRuntimeLayout layout;
        try
        {
            layout = new HqRuntimeLayout(runtimeRoot);
        }
        catch (Exception ex) when (ex is ArgumentException or IOException or NotSupportedException)
        {
            return (2, $"HQ runtime root is invalid: {ex.Message}");
        }
        string? layoutError = layout.GetValidationError();
        if (layoutError != null)
            return (2, layoutError);

        string absoluteInputPath = Path.GetFullPath(inputPath);
        string absoluteWorkDir = Path.GetFullPath(workDir);
        string pipeName = $"ffmpeg-converter-hq-{Environment.ProcessId}-{Guid.NewGuid():N}";
        await using var pipe = new NamedPipeServerStream(
            pipeName, PipeDirection.In, 1, PipeTransmissionMode.Byte, PipeOptions.Asynchronous);

        ProcessStartInfo startInfo = CreateWorkerStartInfo();
        startInfo.WorkingDirectory = layout.Root;
        startInfo.Environment["HQ_PROJECT_ROOT"] = layout.Root;
        startInfo.Environment["HQ_ENCODER_V2_TOOLCHAIN_INI"] = layout.ConfigurationPath;
        startInfo.Environment["HQ_ENCODER_TOOLCHAIN_INI"] = layout.ConfigurationPath;
        startInfo.Environment["HQ_SEGMENTATOR_TOOLCHAIN_INI"] = layout.SegmentatorConfigurationPath;
        startInfo.RedirectStandardInput = true;
        startInfo.RedirectStandardOutput = true;
        startInfo.RedirectStandardError = true;
        startInfo.UseShellExecute = false;
        startInfo.ArgumentList.Add("--hq-worker");
        startInfo.ArgumentList.Add(layout.Root);
        startInfo.ArgumentList.Add(pipeName);
        startInfo.ArgumentList.Add(absoluteInputPath);
        startInfo.ArgumentList.Add(absoluteWorkDir);
        startInfo.ArgumentList.Add(codec);
        startInfo.ArgumentList.Add(preset);

        using var process = new Process { StartInfo = startInfo };
        try
        {
            if (!process.Start())
                return (2, "Could not start the HQ worker process.");
        }
        catch (Exception ex) when (ex is Win32Exception or InvalidOperationException or IOException or UnauthorizedAccessException)
        {
            return (2, $"Could not start the HQ worker process: {ex.Message}");
        }

        using CancellationTokenRegistration stopRegistration = cancellationToken.Register(() =>
        {
            TrySendWorkerCommand(process, "stop");
        });

        Task stdoutPump = DrainProcessOutputAsync(process.StandardOutput);
        Task stderrPump = DrainProcessOutputAsync(process.StandardError);
        int result = 2;
        string error = "HQ worker exited without reporting a result.";

        try
        {
            Task connectionTask = pipe.WaitForConnectionAsync(cancellationToken);
            Task workerExitTask = process.WaitForExitAsync(cancellationToken);
            Task firstTask = await Task.WhenAny(connectionTask, workerExitTask);
            if (firstTask == workerExitTask && !pipe.IsConnected)
            {
                await workerExitTask;
                await Task.WhenAll(stdoutPump, stderrPump);
                return (2, $"HQ worker exited with code {process.ExitCode} before connecting.");
            }
            await connectionTask;
            using var reader = new StreamReader(pipe);
            while (await reader.ReadLineAsync(cancellationToken) is { } line)
            {
                using JsonDocument document = JsonDocument.Parse(line);
                JsonElement message = document.RootElement;
                if (!message.TryGetProperty("type", out JsonElement typeJson))
                    continue;

                string? type = typeJson.GetString();
                if (type == "status" &&
                    message.TryGetProperty("payload", out JsonElement payloadJson) &&
                    payloadJson.ValueKind == JsonValueKind.String)
                {
                    ForwardStatus(payloadJson.GetString() ?? "");
                }
                else if (type == "result")
                {
                    result = message.TryGetProperty("result", out JsonElement resultJson)
                        ? resultJson.GetInt32()
                        : 2;
                    error = message.TryGetProperty("error", out JsonElement errorJson)
                        ? errorJson.GetString() ?? ""
                        : "";
                }
            }

            await process.WaitForExitAsync();
            await Task.WhenAll(stdoutPump, stderrPump);
            if (cancellationToken.IsCancellationRequested && result == 0)
                result = 1;
            return (result, error);
        }
        catch (OperationCanceledException)
        {
            await StopWorkerAsync(process);
            await Task.WhenAll(stdoutPump, stderrPump);
            return (1, "HQ job cancelled.");
        }
        catch (Exception ex) when (ex is JsonException or InvalidOperationException or IOException or FormatException)
        {
            if (!process.HasExited)
            {
                try { process.Kill(entireProcessTree: true); }
                catch (InvalidOperationException) { }
                catch (Win32Exception) { }
            }
            await process.WaitForExitAsync();
            await Task.WhenAll(stdoutPump, stderrPump);
            return (2, $"HQ worker failed: {ex.Message}");
        }
    }

    private async Task StopWorkerAsync(Process process)
    {
        if (process.HasExited)
            return;

        TrySendWorkerCommand(process, "stop");
        if (await WaitForExitAsync(process, TimeSpan.FromSeconds(5)))
            return;

        TrySendWorkerCommand(process, "kill");
        if (await WaitForExitAsync(process, TimeSpan.FromSeconds(5)))
            return;

        try
        {
            if (!process.HasExited)
                process.Kill(entireProcessTree: true);
        }
        catch (InvalidOperationException) { }
        catch (Win32Exception) { }
        await process.WaitForExitAsync();
    }

    private static async Task<bool> WaitForExitAsync(Process process, TimeSpan timeout)
    {
        using var timeoutSource = new CancellationTokenSource(timeout);
        try
        {
            await process.WaitForExitAsync(timeoutSource.Token);
            return true;
        }
        catch (OperationCanceledException) when (timeoutSource.IsCancellationRequested)
        {
            return process.HasExited;
        }
    }

    private void TrySendWorkerCommand(Process process, string command)
    {
        try
        {
            if (!process.HasExited)
            {
                process.StandardInput.WriteLine(command);
                process.StandardInput.Flush();
            }
        }
        catch (IOException ex)
        {
            MessageLogged?.Invoke(this, $"Could not send '{command}' to HQ worker: {ex.Message}");
        }
        catch (InvalidOperationException ex)
        {
            MessageLogged?.Invoke(this, $"Could not send '{command}' to HQ worker: {ex.Message}");
        }
    }

    public static string? ReadVideoTrackPath(string workDir, string codec, string preset)
    {
        string sessionPath = Path.Combine(workDir, $"job.{codec}.{preset}.eov2.session.json");
        try
        {
            using JsonDocument document = JsonDocument.Parse(File.ReadAllText(sessionPath));
            if (!document.RootElement.TryGetProperty("artifacts", out JsonElement artifacts) ||
                !artifacts.TryGetProperty("outputs", out JsonElement outputs) ||
                !outputs.TryGetProperty("video_track", out JsonElement videoTrack) ||
                !videoTrack.TryGetProperty("path", out JsonElement pathJson) ||
                pathJson.ValueKind != JsonValueKind.String)
                return null;

            string? path = pathJson.GetString();
            return !string.IsNullOrWhiteSpace(path) && Path.IsPathFullyQualified(path) ? path : null;
        }
        catch (IOException)
        {
            return null;
        }
        catch (UnauthorizedAccessException)
        {
            return null;
        }
        catch (JsonException)
        {
            return null;
        }
    }

    private static ProcessStartInfo CreateWorkerStartInfo()
    {
        string processPath = Environment.ProcessPath
            ?? throw new InvalidOperationException("Unable to resolve the current application process.");
        var info = new ProcessStartInfo { FileName = processPath };
        if (Path.GetFileNameWithoutExtension(processPath).Equals("dotnet", StringComparison.OrdinalIgnoreCase))
        {
            string? entryAssembly = Assembly.GetEntryAssembly()?.Location;
            if (string.IsNullOrEmpty(entryAssembly))
                throw new InvalidOperationException("Unable to resolve the application assembly for HQ worker startup.");
            info.ArgumentList.Add(entryAssembly);
        }
        return info;
    }

    internal static Task DrainProcessOutputAsync(StreamReader reader)
        => reader.BaseStream.CopyToAsync(Stream.Null);

    internal void ForwardStatus(string json)
    {
        try
        {
            using JsonDocument document = JsonDocument.Parse(json);
            JsonElement root = document.RootElement;
            string stage = GetString(root, "stage");
            string message = GetString(root, "message");
            float percent = GetFloat(root, "progress_percent");
            float fps = GetFloat(root, "fps");
            float eta = GetFloat(root, "remaining_time_sec");

            bool stageChanged = stage.Length > 0 && stage != _lastStage;
            bool messageChanged = stageChanged || message != _lastMessage;
            _lastMessage = message;
            if (stageChanged)
            {
                _lastStage = stage;
                StageChanged?.Invoke(this, stage);
            }

            switch (stage)
            {
                case "preparing" when messageChanged:
                    MessageLogged?.Invoke(this, message.Length > 0 ? message : "Preparing HQ session...");
                    break;
                case "analyzing":
                    ProgressAnalysis?.Invoke(this, new ProgressAnalysisEventArgs(percent, eta));
                    break;
                case "encoding":
                    ProgressEncode?.Invoke(this, new ProgressEncodeEventArgs(percent, fps, eta));
                    break;
                case "completed" when messageChanged:
                    MessageLogged?.Invoke(this, message.Length > 0 ? message : "HQ encoding complete");
                    break;
                case "failed":
                    ErrorOccurred?.Invoke(this, new ErrorEventArgs(
                        message.Length > 0 ? message : "HQ processing failed.", ConverterError.FfmpegFailed));
                    break;
                case "cancelled":
                    ErrorOccurred?.Invoke(this, new ErrorEventArgs(
                        message.Length > 0 ? message : "HQ job cancelled.", ConverterError.SkipFile));
                    break;
            }
        }
        catch (JsonException)
        {
            MessageLogged?.Invoke(this, "HQ worker returned an invalid status payload.");
        }
    }

    private static string GetString(JsonElement root, string name)
        => root.TryGetProperty(name, out JsonElement value) && value.ValueKind == JsonValueKind.String
            ? value.GetString() ?? ""
            : "";

    private static float GetFloat(JsonElement root, string name)
        => root.TryGetProperty(name, out JsonElement value) && value.ValueKind == JsonValueKind.Number &&
           value.TryGetSingle(out float result)
            ? result
            : 0;
}
