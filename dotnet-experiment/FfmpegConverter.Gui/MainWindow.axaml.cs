using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Platform.Storage;
using Avalonia.Threading;
using FfmpegConverter.Core.Engine;
using FfmpegConverter.Core.Models;
using FfmpegConverter.Core.Presets;
using FfmpegConverter.Core.Probing;

namespace FfmpegConverter.Gui;

public partial class MainWindow : Window
{
    private readonly ToolPaths _tools;
    private readonly PresetDb _presetDb;
    private readonly string _platform;
    private readonly ObservableCollection<string> _inputFiles = new();
    private CancellationTokenSource? _cts;

    public MainWindow()
    {
        InitializeComponent();

        _tools = ToolDiscovery.ResolveAll();
        _presetDb = PresetDb.Load();

        _platform = RuntimeInformation.IsOSPlatform(OSPlatform.Windows) ? "windows" :
                    RuntimeInformation.IsOSPlatform(OSPlatform.OSX) ? "macos" : "linux";

        PlatformLabel.Text = $"Platform: {_platform.ToUpper()}";
        PresetDbLabel.Text = $"Preset DB: v{_presetDb.Version} ({_presetDb.LoadedPath})";
        FfmpegLabel.Text = $"FFmpeg: {(!string.IsNullOrEmpty(_tools.Ffmpeg) ? Path.GetFileName(_tools.Ffmpeg) : "NOT FOUND")}";

        InputFilesListBox.ItemsSource = _inputFiles;

        // Populate Codecs
        var codecs = _presetDb.GetCodecs(_platform).ToList();
        CodecComboBox.ItemsSource = codecs;
        if (codecs.Count > 0)
        {
            CodecComboBox.SelectedIndex = 0;
        }

        CodecComboBox.SelectionChanged += OnCodecSelectionChanged;
        UpdatePresets();

        // Trigger Hardware Probe
        _ = Task.Run(async () =>
        {
            try
            {
                var probeResult = await HardwareProbe.ProbeCapabilitiesAsync(_tools.Ffmpeg, _presetDb);
                Dispatcher.UIThread.Post(() =>
                {
                    string hwText = "Supported Encoders:\n" + (probeResult.SupportedEncoders.Count > 0 ? string.Join(", ", probeResult.SupportedEncoders) : "None");
                    if (probeResult.VulkanDevices.Count > 0)
                    {
                        hwText += "\n\nVulkan Devices:\n" + string.Join("\n", probeResult.VulkanDevices);
                    }
                    HwSupportText.Text = hwText;
                });
            }
            catch (Exception ex)
            {
                Dispatcher.UIThread.Post(() =>
                {
                    HwSupportText.Text = $"Error probing hardware: {ex.Message}";
                });
            }
        });
    }

    private void UpdatePresets()
    {
        if (CodecComboBox.SelectedItem is string selectedCodec)
        {
            var presets = _presetDb.GetPresets(_platform, selectedCodec).ToList();
            PresetComboBox.ItemsSource = presets;
            if (presets.Count > 0)
            {
                PresetComboBox.SelectedIndex = 0;
            }
        }
    }

    private void OnCodecSelectionChanged(object? sender, SelectionChangedEventArgs e)
    {
        UpdatePresets();
    }

    private void AppendLog(string message)
    {
        Dispatcher.UIThread.Post(() =>
        {
            LogTextBox.Text += $"[{DateTime.Now:HH:mm:ss}] {message}\n";
            LogTextBox.CaretIndex = LogTextBox.Text?.Length ?? 0;
        });
    }

    private void SetUiState(bool isConverting)
    {
        StartButton.IsEnabled = !isConverting;
        CancelButton.IsEnabled = isConverting;
        
        AddFilesButton.IsEnabled = !isConverting;
        RemoveFilesButton.IsEnabled = !isConverting;
        ClearFilesButton.IsEnabled = !isConverting;
        BrowseOutputDirButton.IsEnabled = !isConverting;
        OutputDirTextBox.IsEnabled = !isConverting;
        
        CodecComboBox.IsEnabled = !isConverting;
        PresetComboBox.IsEnabled = !isConverting;
        AudioNormComboBox.IsEnabled = !isConverting;
        AudioModeComboBox.IsEnabled = !isConverting;
        OverwriteCheckBox.IsEnabled = !isConverting;
        DryRunCheckBox.IsEnabled = !isConverting;
    }

    public async void OnAddFilesClick(object? sender, RoutedEventArgs e)
    {
        var topLevel = TopLevel.GetTopLevel(this);
        if (topLevel == null) return;

        try
        {
            var files = await topLevel.StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
            {
                Title = "Select Input Files",
                AllowMultiple = true
            });

            if (files != null)
            {
                foreach (var file in files)
                {
                    string path = file.Path.LocalPath;
                    if (!string.IsNullOrEmpty(path) && !_inputFiles.Contains(path))
                    {
                        _inputFiles.Add(path);
                        AppendLog($"Added input file: {path}");
                    }
                }
            }
        }
        catch (Exception ex)
        {
            AppendLog($"Error selecting files: {ex.Message}");
        }
    }

    public void OnRemoveFilesClick(object? sender, RoutedEventArgs e)
    {
        var selected = InputFilesListBox.SelectedItems?.Cast<string>().ToList();
        if (selected != null && selected.Count > 0)
        {
            foreach (var file in selected)
            {
                _inputFiles.Remove(file);
                AppendLog($"Removed file: {file}");
            }
        }
    }

    public void OnClearFilesClick(object? sender, RoutedEventArgs e)
    {
        _inputFiles.Clear();
        AppendLog("Cleared all input files.");
    }

    public async void OnBrowseOutputDirClick(object? sender, RoutedEventArgs e)
    {
        var topLevel = TopLevel.GetTopLevel(this);
        if (topLevel == null) return;

        try
        {
            var folders = await topLevel.StorageProvider.OpenFolderPickerAsync(new FolderPickerOpenOptions
            {
                Title = "Select Output Directory",
                AllowMultiple = false
            });

            if (folders != null && folders.Count > 0)
            {
                string path = folders[0].Path.LocalPath;
                OutputDirTextBox.Text = path;
                AppendLog($"Output directory changed to: {path}");
            }
        }
        catch (Exception ex)
        {
            AppendLog($"Error selecting output directory: {ex.Message}");
        }
    }

    public async void OnStartConversionClick(object? sender, RoutedEventArgs e)
    {
        if (_inputFiles.Count == 0)
        {
            AppendLog("Error: No input files specified.");
            StatusLabel.Text = "Error: No input files specified.";
            return;
        }

        string selectedCodec = CodecComboBox.SelectedItem as string ?? "copy";
        string selectedPreset = PresetComboBox.SelectedItem as string ?? "default";
        string selectedNorm = (AudioNormComboBox.SelectedItem as ComboBoxItem)?.Tag as string ?? "none";
        string selectedAudioMode = (AudioModeComboBox.SelectedItem as ComboBoxItem)?.Tag as string ?? "copy";
        bool overwrite = OverwriteCheckBox.IsChecked ?? false;
        bool dryRun = DryRunCheckBox.IsChecked ?? false;
        string outputDir = OutputDirTextBox.Text ?? "";

        var opts = new ConvertOptions
        {
            Codec = selectedCodec,
            Preset = selectedPreset,
            AudioNorm = selectedNorm,
            AudioOutputMode = selectedAudioMode,
            Overwrite = overwrite,
            DryRun = dryRun,
            OutputDir = outputDir
        };

        SetUiState(true);
        _cts = new CancellationTokenSource();
        LogTextBox.Text = ""; // Clear log
        AppendLog("Starting conversion session...");

        try
        {
            var converter = new Core.Engine.Converter();

            converter.FileBegin += (s, ev) =>
            {
                Dispatcher.UIThread.Post(() =>
                {
                    StatusLabel.Text = $"Processing [{ev.Index}/{ev.Total}]: {Path.GetFileName(ev.FileName)}";
                    FileProgressBar.Value = 0;
                });
                AppendLog($"Processing file [{ev.Index}/{ev.Total}]: {ev.FileName}");
            };

            converter.FileEnd += (s, ev) =>
            {
                AppendLog($"Finished file: {Path.GetFileName(ev.FileName)} - Status: {ev.Status}");
            };

            converter.StageChanged += (s, ev) =>
            {
                Dispatcher.UIThread.Post(() =>
                {
                    DetailsLabel.Text = $"Stage: {ev}";
                });
                AppendLog($"Stage changed to: {ev}");
            };

            converter.MessageLogged += (s, ev) =>
            {
                AppendLog($"[Log] {ev.Text}");
            };

            converter.ErrorOccurred += (s, ev) =>
            {
                AppendLog($"[Error] {ev.Text} (Code: {ev.Code})");
            };

            converter.ProgressEncode += (s, ev) =>
            {
                Dispatcher.UIThread.Post(() =>
                {
                    FileProgressBar.Value = ev.Percent;
                    string etaStr = ev.EtaSeconds > 0 ? $"ETA: {TimeSpan.FromSeconds(ev.EtaSeconds):hh\\:mm\\:ss}" : "ETA: --:--:--";
                    DetailsLabel.Text = $"Encoding: {ev.Percent:F1}% | FPS: {ev.Fps:F0} | {etaStr}";
                });
            };

            var result = await Task.Run(async () =>
            {
                return await converter.ProcessFilesAsync(_inputFiles, opts, _cts.Token);
            });

            if (result == ConverterError.Ok)
            {
                StatusLabel.Text = "Conversion completed successfully!";
                AppendLog("All conversion tasks finished successfully.");
            }
            else
            {
                StatusLabel.Text = $"Conversion finished with errors: {result}";
                AppendLog($"Conversion finished with status/errors: {result}");
            }
        }
        catch (OperationCanceledException)
        {
            StatusLabel.Text = "Conversion cancelled by user.";
            AppendLog("Conversion session cancelled by user.");
        }
        catch (Exception ex)
        {
            StatusLabel.Text = "An error occurred during conversion.";
            AppendLog($"Exception: {ex.Message}");
        }
        finally
        {
            if (_cts != null)
            {
                _cts.Dispose();
                _cts = null;
            }
            SetUiState(false);
        }
    }

    public void OnCancelClick(object? sender, RoutedEventArgs e)
    {
        if (_cts != null && !_cts.IsCancellationRequested)
        {
            AppendLog("Cancelling conversion...");
            _cts.Cancel();
        }
    }
}