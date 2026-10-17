using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Reflection;
using System.Text.Json;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Interactivity;
using Avalonia.Threading;
using FfmpegConverter.Gui;

public static class StartupHook
{
    private static readonly string Evidence = Environment.GetEnvironmentVariable("FFC_UI_EVIDENCE")
        ?? throw new InvalidOperationException("Set FFC_UI_EVIDENCE to an external evidence directory.");
    private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };
    private static readonly BindingFlags PrivateInstance = BindingFlags.Instance | BindingFlags.NonPublic;
    private static readonly List<Task> ScreenshotTasks = new();

    public static void Initialize()
    {
        if (Environment.GetCommandLineArgs().Contains("--hq-worker"))
            return;
        _ = Task.Run(async () =>
        {
            try
            {
                Directory.CreateDirectory(Evidence);
                for (int i = 0; i < 600; i++)
                {
                    if (Application.Current?.ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop &&
                        desktop.MainWindow is MainWindow window)
                    {
                        Dispatcher.UIThread.Post(async () => await Run(window, desktop));
                        return;
                    }
                    await Task.Delay(100);
                }
                throw new TimeoutException("Displayed main window did not appear.");
            }
            catch (Exception ex)
            {
                File.WriteAllText(Path.Combine(Evidence, "error.txt"), ex.ToString());
                Environment.Exit(1);
            }
        });
    }

    private static async Task Run(MainWindow window, IClassicDesktopStyleApplicationLifetime desktop)
    {
        try
        {
            T Control<T>(string name) where T : Control =>
                window.FindControl<T>(name) ?? throw new InvalidOperationException($"Missing {name}");
            await WaitUntil(() => window.IsVisible &&
                !(Control<TextBlock>("HwEncodersSummary").Text ?? "").StartsWith("Probing") &&
                (bool)Field(window, "_audioChoicesReady")!, "main-window/tool/audio readiness");
            window.Activate();
            var overwrite = Control<CheckBox>("OverwriteCheckBox");
            bool? initial = overwrite.IsChecked;
            overwrite.Focus();
            overwrite.IsChecked = !(initial ?? false);
            Check(overwrite.IsChecked != initial, "Harmless checkbox interaction did not change the actual control.");
            Screenshot(window, "preflight");
            Save("preflight", new
            {
                result = "PASS", pid = Environment.ProcessId, executable = Environment.ProcessPath,
                title = window.Title, visible = window.IsVisible, active = window.IsActive,
                platformHandle = window.TryGetPlatformHandle()?.Handle.ToString("x"),
                before = initial, after = overwrite.IsChecked
            });
            overwrite.IsChecked = initial;
            if (Environment.GetEnvironmentVariable("FFC_UI_SCENARIO") == "preflight")
            {
                await Task.WhenAll(ScreenshotTasks);
                desktop.Shutdown(0);
                return;
            }

            string fixtures = Environment.GetEnvironmentVariable("FFC_UI_FIXTURES")
                ?? throw new InvalidOperationException("Set FFC_UI_FIXTURES.");
            string outputRoot = Environment.GetEnvironmentVariable("FFC_UI_OUTPUT")
                ?? throw new InvalidOperationException("Set FFC_UI_OUTPUT to a short external path.");
            var scenarios = new[]
            {
                new Scenario("legacy-single", "software", "prores_ks", "standard", ["valid-1.mkv"], "pcm"),
                new Scenario("legacy-queue", "software", "prores_ks", "standard",
                    ["valid-1.mkv", "valid-2.mkv", "valid-3.mkv"], "pcm"),
                new Scenario("legacy-error-queue", "software", "prores_ks", "standard",
                    ["valid-1.mkv", "corrupt.mkv", "valid-3.mkv"], "pcm"),
                new Scenario("mux-mkv", "mux", "mkv", "mkv", ["mux-source.mkv"], "fdk_aac_320"),
                new Scenario("mux-mov", "mux", "mov", "mov", ["mux-source.mkv"], "pcm"),
                new Scenario("mux-m4v", "mux", "m4v", "default", ["mux-source.mkv"], "fdk_aac_320"),
                new Scenario("hq-mux", "hq_converter", "x264", "fullhd", ["hq-source.mkv"], "pcm")
            };
            string? selected = Environment.GetEnvironmentVariable("FFC_UI_SCENARIO");
            var results = new List<object>();
            foreach (Scenario scenario in scenarios.Where(s => selected is null || selected.Split(',').Contains(s.Name)))
            {
                var inputs = (ObservableCollection<string>)Control<ListBox>("InputFilesListBox").ItemsSource!;
                inputs.Clear();
                Control<ComboBox>("CodecComboBox").SelectedItem = scenario.Group;
                Select(Control<ComboBox>("EncoderComboBox"), scenario.Encoder);
                Select(Control<ComboBox>("PresetComboBox"), scenario.Preset);
                string replacement = scenario.Group == "mux" ? Path.Combine(fixtures, "replacement.mp4") : "";
                typeof(MainWindow).GetField("_videoTrackPath", PrivateInstance)!.SetValue(window, replacement);
                Control<TextBox>("VideoTrackTextBox").Text = replacement;
                foreach (string input in scenario.Inputs)
                    inputs.Add(Path.Combine(fixtures, input));
                await (Task)typeof(MainWindow).GetMethod("RefreshAudioModeChoicesAsync", PrivateInstance)!
                    .Invoke(window, null)!;
                await WaitUntil(() => (bool)Field(window, "_audioChoicesReady")!, "audio choices");
                var audio = Control<ComboBox>("AudioModeComboBox");
                ComboBoxItem? audioItem = audio.Items.OfType<ComboBoxItem>()
                    .FirstOrDefault(item => item.Tag?.ToString() == scenario.Audio);
                Check(audioItem is not null, $"Unavailable audio {scenario.Audio}: " +
                    string.Join(", ", audio.Items.OfType<ComboBoxItem>().Select(item => item.Tag)));
                audio.SelectedItem = audioItem;
                string output = Path.Combine(outputRoot, scenario.Name);
                Directory.CreateDirectory(output);
                Check(Directory.GetFiles(output).Length == 0,
                    $"Use a fresh output root; {output} already contains files.");
                Control<TextBox>("OutputDirTextBox").Text = output;
                overwrite.IsChecked = true;
                Control<CheckBox>("DryRunCheckBox").IsChecked = false;
                Check(Control<Button>("StartButton").IsEnabled, $"{scenario.Name}: Start is disabled.");
                Check(window.IsVisible, "Application window is not displayed.");

                var status = Control<TextBlock>("StatusLabel");
                var details = Control<TextBlock>("DetailsLabel");
                var bar = Control<ProgressBar>("FileProgressBar");
                var console = Control<TextBox>("LogTextBox");
                var samples = new List<Sample>();
                var captured = new HashSet<string>();
                bool recording = true;
                void Observe(object? sender, AvaloniaPropertyChangedEventArgs args)
                {
                    if (!recording || args.Property.Name is not ("Text" or "Value"))
                        return;
                    samples.Add(new Sample(DateTime.UtcNow, status.Text ?? "", details.Text ?? "", bar.Value,
                        Directory.GetFiles(output).Any(path => !path.Contains(".tmp.") && !path.Contains("intermediate"))));
                    if (sender != details)
                        return;
                    string text = details.Text ?? "";
                    string key = text.Split('|')[0].Trim();
                    if (text.Contains("Encoding:"))
                        key += bar.Value < 50 ? "-early" : "-late";
                    else if (text.Contains('%'))
                        key += "-percent";
                    if (Environment.GetEnvironmentVariable("FFC_UI_SCREENSHOTS") != "terminal" && captured.Add(key))
                    {
                        string captureName = $"{scenario.Name}-{captured.Count}-{SafeName(key)}";
                        Dispatcher.UIThread.Post(() => Screenshot(window,
                            captureName), DispatcherPriority.Background);
                    }
                }
                status.PropertyChanged += Observe;
                details.PropertyChanged += Observe;
                bar.PropertyChanged += Observe;
                try
                {
                    await (Task)typeof(MainWindow).GetMethod("StartConversionInternalAsync", PrivateInstance)!
                        .Invoke(window, [false, null])!;
                    await Dispatcher.UIThread.InvokeAsync(() => { }, DispatcherPriority.Background);
                    recording = false;
                    string log = console.Text ?? "";
                    Save(scenario.Name, new
                    {
                        scenario, visible = window.IsVisible, status = status.Text, details = details.Text,
                        value = bar.Value, samples, console = log, outputs = Directory.GetFiles(output)
                    });
                    File.WriteAllText(Path.Combine(Evidence, scenario.Name + ".log"), log);
                    Screenshot(window, scenario.Name + "-final");
                    await Task.WhenAll(ScreenshotTasks);
                    Validate(scenario, samples, log, status.Text ?? "", details.Text ?? "");
                    string[] outputFiles = Directory.GetFiles(output);
                    Check(outputFiles.Length == scenario.Inputs.Count(input => input != "corrupt.mkv"),
                        $"{scenario.Name}: output count does not match successful inputs.");
                    Check(!outputFiles.Any(file => Path.GetFileName(file).StartsWith("corrupt")),
                        "Corrupt input produced an output.");
                    Check(samples.Where(s => s.Status is "Complete" or "Conversion completed successfully!")
                        .All(s => s.OutputExists), "Success was presented before the final output existed.");
                    foreach (string file in outputFiles)
                        await Probe(file, scenario.Name);
                    string terminalStatus = status.Text ?? "";
                    string terminalDetails = details.Text ?? "";
                    double terminalPercent = bar.Value;
                    await Task.Delay(350);
                    Check(status.Text == terminalStatus && details.Text == terminalDetails && bar.Value == terminalPercent,
                        "Terminal presentation changed after completion.");
                    results.Add(new { scenario = scenario.Name, result = "PASS", samples = samples.Count });
                    Save("results", results);
                }
                finally
                {
                    recording = false;
                    status.PropertyChanged -= Observe;
                    details.PropertyChanged -= Observe;
                    bar.PropertyChanged -= Observe;
                }
            }
            Check(results.Count > 0, $"No scenario matches {selected}.");
            desktop.Shutdown(0);
        }
        catch (Exception ex)
        {
            File.WriteAllText(Path.Combine(Evidence, "error.txt"), ex.ToString());
            desktop.Shutdown(1);
        }
    }

    private static void Validate(Scenario scenario, List<Sample> samples, string log, string status, string details)
    {
        bool errorQueue = scenario.Name == "legacy-error-queue";
        Check(errorQueue ? details == "Finished with errors" : status == "Conversion completed successfully!",
            $"{scenario.Name}: incorrect final result: {status} / {details}");
        foreach (string input in scenario.Inputs)
        {
            Check(log.Split('\n').Count(line => line.Contains("Processing file [") && line.Contains(input)) == 1,
                $"{input} was not begun exactly once.");
            string expected = input == "corrupt.mkv" ? "FfmpegFailed" : "Ok";
            Check(log.Split('\n').Count(line => line.Contains($"Finished file: {input} - Status: {expected}")) == 1,
                $"{input}: unexpected or repeated file result (expected {expected}).");
        }
        if (scenario.Group == "software")
        {
            foreach (string input in scenario.Inputs.Where(name => name != "corrupt.mkv"))
            {
                var progress = samples.Where(s => s.Status.Contains(input) && s.Details.Contains("Encoding:")).ToList();
                Check(progress.Select(s => s.Details).Distinct().Count() >= 2,
                    $"{input}: no multiple visible encoding progress points.");
                Check(progress.Any(s => s.Details.Contains("FPS:") && s.Details.Contains("ETA:")),
                    $"{input}: missing Legacy speed/ETA.");
            }
            Check(!samples.Any(s => s.Details.Contains("Checkpoint:")), "HQ state leaked into Legacy.");
            return;
        }
        if (scenario.Group == "hq_converter")
        {
            var checkpoints = samples.Where(s => s.Details.Contains("Checkpoint:")).ToList();
            Check(checkpoints.Count > 1, "No advancing HQ checkpoints.");
            Check(checkpoints.All(s => !s.Details.Contains("FPS:") && !s.Details.Contains("ETA:")),
                "HQ checkpoints displayed Legacy speed/ETA.");
        }
        string[] sequence = scenario.Name switch
        {
            "mux-mov" => ["Preparing source...", "Assembling intermediate MKV...",
                "Finalizing MOV...", "Verifying final output..."],
            "mux-m4v" => ["Preparing source...", "Assembling intermediate MKV...",
                "Preparing M4V components...", "Creating M4V with MP4Box...",
                "Preserving M4V metadata and subtitles...", "Verifying final M4V..."],
            _ => ["Preparing source...", "Assembling intermediate MKV...",
                "Verifying final output..."]
        };
        int position = -1;
        foreach (string stage in sequence)
        {
            position = samples.FindIndex(position + 1, s => s.Details.StartsWith($"Stage: {stage}"));
            Check(position >= 0, $"{scenario.Name}: stage missing or out of order: {stage}");
        }
        Check(samples.Any(s => s.Details.Contains("Preparing source...") && s.Details.Contains("Encoding:") &&
            s.Details.Contains("FPS:") && s.Details.Contains("ETA:")), "Source preparation telemetry missing.");
        Check(samples.Any(s => s.Details.Contains("Assembling intermediate MKV... |") && s.Details.Contains('%')),
            "No mkvmerge stage-local percentage displayed.");
        if (scenario.Name == "mux-m4v")
            Check(samples.Any(s => s.Details.Contains("Creating M4V with MP4Box... |") && s.Details.Contains('%')),
                "No MP4Box stage-local percentage displayed.");
        foreach (Sample sample in samples.Where(s => s.Details.StartsWith("Stage:") &&
            !s.Details.Contains("Preparing source...") && !s.Details.Contains("Checkpoint:") &&
            !s.Details.StartsWith("Stage: encoding")))
        {
            Check(!sample.Details.Contains("FPS:") && !sample.Details.Contains("ETA:"),
                $"Stale speed/ETA in {sample.Details}");
        }
        Check(log.Contains("Post-mux completed"), "Final mux did not complete.");
    }

    private static async Task Probe(string file, string scenario)
    {
        var info = new ProcessStartInfo(Environment.GetEnvironmentVariable("FFPROBE_BIN") ?? "ffprobe")
        {
            RedirectStandardOutput = true, RedirectStandardError = true, UseShellExecute = false
        };
        foreach (string arg in new[] { "-v", "error", "-show_entries", "format=duration:stream=codec_name,codec_type",
                     "-of", "json", file })
            info.ArgumentList.Add(arg);
        using var process = Process.Start(info)!;
        Task<string> stdout = process.StandardOutput.ReadToEndAsync();
        Task<string> stderr = process.StandardError.ReadToEndAsync();
        await process.WaitForExitAsync();
        Check(process.ExitCode == 0, $"Output probe failed: {file}: {await stderr}");
        string json = await stdout;
        using var document = JsonDocument.Parse(json);
        Check(document.RootElement.GetProperty("streams").GetArrayLength() >= 2, "Missing output streams.");
        File.WriteAllText(Path.Combine(Evidence, scenario + "-" + Path.GetFileName(file) + ".ffprobe.json"), json);
    }

    private static object? Field(MainWindow window, string name) =>
        typeof(MainWindow).GetField(name, PrivateInstance)!.GetValue(window);

    private static void Select(ComboBox combo, string value)
    {
        Check(combo.Items.Cast<object>().Any(item => item.ToString() == value),
            $"Unavailable selection {value}: {string.Join(", ", combo.Items.Cast<object>())}");
        combo.SelectedItem = value;
    }

    private static async Task WaitUntil(Func<bool> ready, string description)
    {
        for (int i = 0; i < 600; i++)
        {
            if (ready()) return;
            await Task.Delay(100);
        }
        throw new TimeoutException($"Timed out waiting for {description}.");
    }

    private static void Screenshot(MainWindow window, string name)
    {
        string handle = "0x" + (window.TryGetPlatformHandle()?.Handle.ToString("x")
            ?? throw new InvalidOperationException("No displayed platform window."));
        ScreenshotTasks.Add(Task.Run(async () =>
        {
            var info = new ProcessStartInfo("import") { RedirectStandardError = true, UseShellExecute = false };
            foreach (string arg in new[] { "-window", handle, Path.Combine(Evidence, SafeName(name) + ".png") })
                info.ArgumentList.Add(arg);
            using var process = Process.Start(info)!;
            Task<string> error = process.StandardError.ReadToEndAsync();
            await process.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(30));
            Check(process.ExitCode == 0, $"Screenshot failed: {await error}");
        }));
    }

    private static string SafeName(string text) =>
        string.Concat(text.Select(c => char.IsAsciiLetterOrDigit(c) ? c : '-'));

    private static void Save(string name, object value) =>
        File.WriteAllText(Path.Combine(Evidence, name + ".json"), JsonSerializer.Serialize(value, JsonOptions));

    private static void Check(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }

    private sealed record Scenario(string Name, string Group, string Encoder, string Preset, string[] Inputs, string Audio);
    private sealed record Sample(DateTime At, string Status, string Details, double Percent, bool OutputExists);
}
