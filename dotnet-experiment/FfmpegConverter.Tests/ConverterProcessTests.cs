using System.Diagnostics;
using FfmpegConverter.Core.Engine;
using FfmpegConverter.Core.Models;
using FfmpegConverter.Core.Policy;
using FfmpegConverter.Core.Presets;
using FfmpegConverter.Core.Probing;

namespace FfmpegConverter.Tests;

public class ConverterProcessTests
{
    [Theory]
    [InlineData(0)]
    [InlineData(7)]
    public async Task Encoding_DrainsDiagnosticsAndReportsFailures(int exitCode)
    {
        using var fixture = new ProcessFixture();
        string arguments = fixture.Script(
            $"for ($i = 0; $i -lt 20000; $i++) {{ [Console]::Error.WriteLine(('x' * 128)) }}\n" +
            $"[Console]::Error.WriteLine('diagnostic sentinel')\n" +
            $"[Console]::Out.WriteLine('out_time_ms=1000000')\n" +
            $"[Console]::Out.WriteLine('progress=end')\nexit {exitCode}\n",
            $"i=0\nwhile [ \"$i\" -lt 20000 ]; do printf '%0128d\\n' 0 >&2; i=$((i+1)); done\n" +
            $"echo 'diagnostic sentinel' >&2\nprintf 'out_time_ms=1000000\\nprogress=end\\n'\nexit {exitCode}\n");
        var converter = fixture.Converter();
        string? error = null;
        bool completed = false;
        converter.ErrorOccurred += (_, e) => error = e.Text;
        converter.ProgressEncode += (_, e) => completed |= e.Percent == 100;
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(30));

        ConverterError result = await converter.RunFfmpegEncodeAsync(arguments, 2, timeout.Token);

        Assert.False(timeout.IsCancellationRequested);
        Assert.True(completed);
        Assert.Equal(exitCode == 0 ? ConverterError.Ok : ConverterError.FfmpegFailed, result);
        if (exitCode == 0)
            Assert.Null(error);
        else
        {
            Assert.Contains("diagnostic sentinel", error);
            Assert.Contains("code: 7", error);
            Assert.True(error!.Length < 17000);
        }
    }

    [Fact]
    public async Task Encoding_CancellationTerminatesProcessTree()
    {
        using var fixture = new ProcessFixture();
        string pidPath = Path.Combine(fixture.Root, "pids.txt");
        string psPath = pidPath.Replace("'", "''");
        string shPath = pidPath.Replace("'", "'\\''");
        string arguments = fixture.Script(
            "$child = Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') " +
            "-ArgumentList @('-NoProfile','-NonInteractive','-Command','Start-Sleep -Seconds 60') -PassThru -WindowStyle Hidden\n" +
            $"[IO.File]::WriteAllText('{psPath}', \"$PID $($child.Id)\")\n" +
            "[Console]::Out.WriteLine('out_time_ms=1000000'); [Console]::Out.Flush()\n" +
            "while ($true) { [Console]::Error.WriteLine(('x' * 1024)) }\n",
            $"sleep 60 &\nchild=$!\necho \"$$ $child\" > '{shPath}'\n" +
            "printf 'out_time_ms=1000000\\n'\nwhile :; do printf '%01024d\\n' 0 >&2; done\n");
        var converter = fixture.Converter();
        var started = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        converter.ProgressEncode += (_, _) => started.TrySetResult();
        using var cancellation = new CancellationTokenSource(TimeSpan.FromSeconds(30));
        Task<ConverterError> run = converter.RunFfmpegEncodeAsync(arguments, 2, cancellation.Token);
        Process? parent = null;
        Process? child = null;
        try
        {
            await started.Task.WaitAsync(TimeSpan.FromSeconds(20));
            int[] pids = (await File.ReadAllTextAsync(pidPath)).Split(' ', StringSplitOptions.RemoveEmptyEntries)
                .Select(int.Parse).ToArray();
            Assert.Equal(2, pids.Length);
            parent = Process.GetProcessById(pids[0]);
            child = Process.GetProcessById(pids[1]);
            cancellation.Cancel();

            Assert.Equal(ConverterError.SkipFile, await run.WaitAsync(TimeSpan.FromSeconds(20)));
            Assert.True(parent.WaitForExit(5000));
            Assert.True(child.WaitForExit(5000));
        }
        finally
        {
            cancellation.Cancel();
            foreach (Process? process in new[] { parent, child })
            {
                if (process == null) continue;
                try
                {
                    if (!process.HasExited)
                    {
                        process.Kill(entireProcessTree: true);
                        await process.WaitForExitAsync();
                    }
                }
                finally { process.Dispose(); }
            }
            await run.WaitAsync(TimeSpan.FromSeconds(20));
        }
    }

    [Fact]
    public async Task Queue_CancelBeforeEncoding_DoesNotStartNextFile()
    {
        using var fixture = new ProcessFixture();
        string first = Path.Combine(fixture.Root, "first.mkv");
        string second = Path.Combine(fixture.Root, "second.mkv");
        File.WriteAllText(first, "fixture");
        File.WriteAllText(second, "fixture");
        var converter = fixture.Converter();
        var options = new ConvertOptions
        {
            Codec = "prores_ks",
            SelectionGroup = "software",
            SelectionEncoder = "prores_ks",
            Preset = "standard",
            AudioOutputMode = AudioOutputMode.Pcm,
            OutputDir = fixture.Root,
            Overwrite = true
        };
        var started = new List<string>();
        var results = new List<ConverterError>();
        using var cancellation = new CancellationTokenSource();
        converter.FileBegin += (_, e) => started.Add(e.FileName);
        converter.FileEnd += (_, e) => results.Add(e.Status);
        converter.StageChanged += (_, stage) =>
        {
            if (stage == "encoding") cancellation.Cancel();
        };

        ConverterError result = await converter.ProcessFilesAsync([first, second], options, cancellation.Token);

        Assert.Equal(ConverterError.SkipFile, result);
        Assert.Equal(new[] { first }, started);
        Assert.Equal(new[] { ConverterError.SkipFile }, results);
    }

    private sealed class ProcessFixture : IDisposable
    {
        public string Root { get; } = Path.Combine(Path.GetTempPath(), $"ffc-process-{Guid.NewGuid():N}");
        private readonly string _executable;

        public ProcessFixture()
        {
            Directory.CreateDirectory(Root);
            _executable = OperatingSystem.IsWindows()
                ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),
                    "WindowsPowerShell", "v1.0", "powershell.exe")
                : "/bin/sh";
        }

        public string Script(string windows, string unix)
        {
            string path = Path.Combine(Root, OperatingSystem.IsWindows() ? "worker.ps1" : "worker.sh");
            File.WriteAllText(path, OperatingSystem.IsWindows() ? windows : unix);
            return OperatingSystem.IsWindows()
                ? $"-NoProfile -NonInteractive -File \"{path}\""
                : $"\"{path}\"";
        }

        public Converter Converter() => new(
            new ToolPaths { Ffmpeg = _executable },
            PresetDb.Load(),
            new HardwareProbeResult());

        public void Dispose() => Directory.Delete(Root, recursive: true);
    }
}
