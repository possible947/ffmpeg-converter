using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Input;
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
    private readonly ConvertOptions _m4vOptions = new();
    private HardwareProbeResult? _probeResult;
    private string _videoTrackPath = "";
    private CancellationTokenSource? _cts;
    private bool _updatingSelection = false;

    public MainWindow()
    {
        InitializeComponent();

        _tools = ToolDiscovery.ResolveAll();
        _presetDb = PresetDb.Load();

        _platform = RuntimeInformation.IsOSPlatform(OSPlatform.Windows) ? "windows" :
                    RuntimeInformation.IsOSPlatform(OSPlatform.OSX) ? "macos" : "linux";

        PlatformLabel.Text = $"Platform: {_platform.ToUpper()} (v3.0b)";
        PresetDbLabel.Text = $"Preset DB: v{_presetDb.Version} ({Path.GetFileName(_presetDb.LoadedPath)})";

        string ffStr = !string.IsNullOrEmpty(_tools.Ffmpeg) ? Path.GetFileName(_tools.Ffmpeg) : "NOT FOUND";
        string mp4bStr = !string.IsNullOrEmpty(_tools.Mp4Box) && File.Exists(_tools.Mp4Box) ? "MP4Box OK" : "no MP4Box";
        string mkvmStr = !string.IsNullOrEmpty(_tools.Mkvmerge) && File.Exists(_tools.Mkvmerge) ? "mkvmerge OK" : "no mkvmerge";
        ToolsStatusLabel.Text = $"FFmpeg: {ffStr} | {mp4bStr} | {mkvmStr}";

        InputFilesListBox.ItemsSource = _inputFiles;

        // Setup Drag & Drop handlers
        AddHandler(DragDrop.DropEvent, OnDrop);
        AddHandler(DragDrop.DragOverEvent, OnDragOver);

        // Populate initial groups and cascades
        PopulateCodecGroups();

        // Default audio selections
        AudioNormComboBox.SelectedIndex = 0; // none
        GenreComboBox.SelectedIndex = 0;     // none
        AudioModeComboBox.SelectedIndex = 0; // pcm

        // Default filter selections
        FilterComboBox.SelectedIndex = 0;       // none
        FilterPresetComboBox.SelectedIndex = 0; // none

        // Connect cascade listeners
        CodecComboBox.SelectionChanged += OnCodecSelectionChanged;
        EncoderComboBox.SelectionChanged += OnEncoderSelectionChanged;
        AudioNormComboBox.SelectionChanged += OnAudioNormSelectionChanged;
        FilterComboBox.SelectionChanged += OnFilterSelectionChanged;
        FilterPresetComboBox.SelectionChanged += OnFilterPresetSelectionChanged;

        // Trigger asynchronous hardware probe
        _ = Task.Run(async () =>
        {
            try
            {
                var probe = await HardwareProbe.ProbeCapabilitiesAsync(_tools.Ffmpeg, _presetDb);
                Dispatcher.UIThread.Post(() =>
                {
                    _probeResult = probe;
                    HwEncodersSummary.Text = probe.SupportedEncoders.Count > 0
                        ? $"HW encoders: {string.Join(", ", probe.SupportedEncoders)}"
                        : "No hardware encoders detected.";

                    PopulateVulkanDevices(probe);
                    PopulateVaapiDefault();
                    PopulateCodecGroups();
                });
            }
            catch (Exception ex)
            {
                Dispatcher.UIThread.Post(() =>
                {
                    HwEncodersSummary.Text = $"HW probe error: {ex.Message}";
                });
            }
        });
    }

    // =========================================================================
    // Cascade Selection Model (Group -> Encoder -> Preset)
    // =========================================================================

    private bool CapabilityFilter(string group, string encoder, string finalCodec, IReadOnlyList<string> requires)
    {
        if (group.Equals("software", StringComparison.OrdinalIgnoreCase) ||
            group.Equals("mux", StringComparison.OrdinalIgnoreCase) ||
            group.Equals("copy", StringComparison.OrdinalIgnoreCase))
        {
            return true;
        }

        if (_probeResult == null) return true;

        return _probeResult.SupportedEncoders.Contains(finalCodec) ||
               _probeResult.SupportedEncoders.Contains(encoder);
    }

    private void PopulateCodecGroups()
    {
        _updatingSelection = true;
        try
        {
            string? prevGroup = CodecComboBox.SelectedItem?.ToString();
            var groups = _presetDb.GetSelectionGroups(_platform, CapabilityFilter).ToList();

            CodecComboBox.ItemsSource = groups;

            if (groups.Count > 0)
            {
                if (!string.IsNullOrEmpty(prevGroup) && groups.Contains(prevGroup))
                {
                    CodecComboBox.SelectedItem = prevGroup;
                }
                else if (groups.Contains("software"))
                {
                    CodecComboBox.SelectedItem = "software";
                }
                else
                {
                    CodecComboBox.SelectedIndex = 0;
                }
            }
        }
        finally
        {
            _updatingSelection = false;
        }

        UpdateEncoders();
    }

    private void OnCodecSelectionChanged(object? sender, SelectionChangedEventArgs e)
    {
        if (_updatingSelection) return;
        UpdateEncoders();
    }

    private void UpdateEncoders()
    {
        _updatingSelection = true;
        string group = CodecComboBox.SelectedItem?.ToString() ?? "software";
        try
        {
            string? prevEncoder = EncoderComboBox.SelectedItem?.ToString();
            var encoders = _presetDb.GetGroupEncoders(_platform, group, CapabilityFilter).ToList();

            EncoderComboBox.ItemsSource = encoders;

            if (encoders.Count > 0)
            {
                if (!string.IsNullOrEmpty(prevEncoder) && encoders.Contains(prevEncoder))
                {
                    EncoderComboBox.SelectedItem = prevEncoder;
                }
                else if (group.Equals("software", StringComparison.OrdinalIgnoreCase) && encoders.Contains("prores_ks"))
                {
                    EncoderComboBox.SelectedItem = "prores_ks";
                }
                else if (group.Equals("mux", StringComparison.OrdinalIgnoreCase) && encoders.Contains("mkv"))
                {
                    EncoderComboBox.SelectedItem = "mkv";
                }
                else
                {
                    EncoderComboBox.SelectedIndex = 0;
                }
            }

            // Adjust hardware device selectors visibility
            bool isVulkan = group.Contains("vulkan", StringComparison.OrdinalIgnoreCase);
            VulkanDevRow.IsVisible = isVulkan;

            bool isVaapi = group.Contains("vaapi", StringComparison.OrdinalIgnoreCase);
            VaapiDevRow.IsVisible = isVaapi;
        }
        finally
        {
            _updatingSelection = false;
        }

        UpdatePresets();
    }

    private void OnEncoderSelectionChanged(object? sender, SelectionChangedEventArgs e)
    {
        if (_updatingSelection) return;
        UpdatePresets();
    }

    private void UpdatePresets()
    {
        _updatingSelection = true;
        string group = CodecComboBox.SelectedItem?.ToString() ?? "software";
        string encoder = EncoderComboBox.SelectedItem?.ToString() ?? "prores_ks";

        try
        {
            string? prevPreset = PresetComboBox.SelectedItem?.ToString();
            var presets = _presetDb.GetSelectionPresets(_platform, group, encoder).ToList();

            PresetComboBox.ItemsSource = presets;

            if (presets.Count > 0)
            {
                if (!string.IsNullOrEmpty(prevPreset) && presets.Contains(prevPreset))
                {
                    PresetComboBox.SelectedItem = prevPreset;
                }
                else if (presets.Contains("standard"))
                {
                    PresetComboBox.SelectedItem = "standard";
                }
                else if (presets.Contains("default"))
                {
                    PresetComboBox.SelectedItem = "default";
                }
                else
                {
                    PresetComboBox.SelectedIndex = 0;
                }
            }

            // Adjust Mux Video Track controls visibility
            bool isMuxTrack = group.Equals("mux", StringComparison.OrdinalIgnoreCase) &&
                             (encoder.Equals("mkv", StringComparison.OrdinalIgnoreCase) || encoder.Equals("mov", StringComparison.OrdinalIgnoreCase));
            VideoTrackRow.IsVisible = isMuxTrack;
            AddTrackButton.IsEnabled = isMuxTrack;

            // Adjust Deblock / Filter controls enablement
            bool isSoftwareProres = group.Equals("software", StringComparison.OrdinalIgnoreCase) &&
                                   (encoder.Equals("prores", StringComparison.OrdinalIgnoreCase) || encoder.Equals("prores_ks", StringComparison.OrdinalIgnoreCase));

            FilterComboBox.IsEnabled = isSoftwareProres;
            FilterPresetComboBox.IsEnabled = isSoftwareProres;

            if (!isSoftwareProres)
            {
                FilterComboBox.SelectedIndex = 0; // none
                FilterPresetComboBox.SelectedIndex = 0; // none
                FilterStatusLabel.Text = "Deblock filters disabled for hardware/mux encoders.";
            }
            else
            {
                if (FilterComboBox.SelectedIndex < 0) FilterComboBox.SelectedIndex = 0;
                if (FilterPresetComboBox.SelectedIndex < 0) FilterPresetComboBox.SelectedIndex = 0;
                FilterStatusLabel.Text = "Deblock filter: active for ProRes software.";
            }
        }
        finally
        {
            _updatingSelection = false;
        }
    }

    // =========================================================================
    // Device & Filter Helpers
    // =========================================================================

    private void PopulateVulkanDevices(HardwareProbeResult probe)
    {
        VulkanDevComboBox.Items.Clear();

        var autoItem = new ComboBoxItem { Content = "Auto (-1)", Tag = -1 };
        VulkanDevComboBox.Items.Add(autoItem);

        int idx = 0;
        foreach (var dev in probe.VulkanDevices)
        {
            var item = new ComboBoxItem { Content = dev, Tag = idx };
            VulkanDevComboBox.Items.Add(item);
            idx++;
        }

        VulkanDevComboBox.SelectedIndex = 0;
    }

    private void PopulateVaapiDefault()
    {
        if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
        {
            try
            {
                if (File.Exists("/dev/dri/renderD128"))
                {
                    VaapiDevTextBox.Text = "/dev/dri/renderD128";
                }
            }
            catch { }
        }
    }

    private void OnAudioNormSelectionChanged(object? sender, SelectionChangedEventArgs e)
    {
        string norm = (AudioNormComboBox.SelectedItem as ComboBoxItem)?.Tag?.ToString() ?? "none";
        bool isLoudnorm = (norm == "loudness_norm_2pass");
        GenreComboBox.IsEnabled = isLoudnorm;
        if (isLoudnorm)
        {
            if (GenreComboBox.SelectedIndex <= 0)
            {
                GenreComboBox.SelectedIndex = 1; // edm
            }
        }
        else
        {
            GenreComboBox.SelectedIndex = 0; // none
        }
    }

    private void OnFilterSelectionChanged(object? sender, SelectionChangedEventArgs e)
    {
        if (_updatingSelection) return;
        _updatingSelection = true;
        try
        {
            string f = (FilterComboBox.SelectedItem as ComboBoxItem)?.Tag?.ToString() ?? "none";
            foreach (var item in FilterPresetComboBox.Items)
            {
                if (item is ComboBoxItem cbi && cbi.Tag?.ToString() == f)
                {
                    FilterPresetComboBox.SelectedItem = cbi;
                    break;
                }
            }
        }
        finally
        {
            _updatingSelection = false;
        }
    }

    private void OnFilterPresetSelectionChanged(object? sender, SelectionChangedEventArgs e)
    {
        if (_updatingSelection) return;
        _updatingSelection = true;
        try
        {
            string fp = (FilterPresetComboBox.SelectedItem as ComboBoxItem)?.Tag?.ToString() ?? "none";
            foreach (var item in FilterComboBox.Items)
            {
                if (item is ComboBoxItem cbi && cbi.Tag?.ToString() == fp)
                {
                    FilterComboBox.SelectedItem = cbi;
                    break;
                }
            }
        }
        finally
        {
            _updatingSelection = false;
        }
    }

    // =========================================================================
    // Drag and Drop & File Management
    // =========================================================================

    private void OnDragOver(object? sender, DragEventArgs e)
    {
        var files = e.DataTransfer.TryGetFiles();
        if (files != null && files.Any())
        {
            e.DragEffects = DragDropEffects.Copy;
        }
        else
        {
            e.DragEffects = DragDropEffects.None;
        }
    }

    private void OnDrop(object? sender, DragEventArgs e)
    {
        var files = e.DataTransfer.TryGetFiles();
        if (files != null)
        {
            foreach (var item in files)
            {
                string path = item.Path.LocalPath;
                if (!string.IsNullOrEmpty(path) && !_inputFiles.Contains(path))
                {
                    _inputFiles.Add(path);
                    AppendLog($"Added input file: {path}");
                }
            }
        }
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
                AppendLog($"Output directory set to: {path}");
            }
        }
        catch (Exception ex)
        {
            AppendLog($"Error selecting output directory: {ex.Message}");
        }
    }

    // =========================================================================
    // Mux Mode Video Track
    // =========================================================================

    public async void OnBrowseVideoTrackClick(object? sender, RoutedEventArgs e)
    {
        var topLevel = TopLevel.GetTopLevel(this);
        if (topLevel == null) return;

        try
        {
            var files = await topLevel.StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
            {
                Title = "Select Replacement Video Track",
                AllowMultiple = false
            });

            if (files != null && files.Count > 0)
            {
                string path = files[0].Path.LocalPath;
                _videoTrackPath = path;
                VideoTrackTextBox.Text = path;
                AppendLog($"Video track set to: {path}");
            }
        }
        catch (Exception ex)
        {
            AppendLog($"Error selecting video track: {ex.Message}");
        }
    }

    public void OnClearVideoTrackClick(object? sender, RoutedEventArgs e)
    {
        _videoTrackPath = "";
        VideoTrackTextBox.Text = "";
        AppendLog("Video track cleared.");
    }

    // =========================================================================
    // Apple M4V Dedicated Action
    // =========================================================================

    public async void OnAppleM4vClick(object? sender, RoutedEventArgs e)
    {
        if (_inputFiles.Count == 0)
        {
            AppendLog("Error: No input files selected for Apple m4v creation.");
            StatusLabel.Text = "Error: No input files selected.";
            return;
        }

        var dialog = new AppleM4vDialog(_m4vOptions);
        var result = await dialog.ShowDialog<bool>(this);

        if (result)
        {
            dialog.ApplyToOptions(_m4vOptions);
            AppendLog($"Configured Apple M4V: AudioLang={_m4vOptions.M4vAudioLang}, AC3={_m4vOptions.M4vAc3BitrateKbps}k, Chapters={_m4vOptions.M4vAddChapters}, VTrack={_m4vOptions.M4vVideoTrackIndex}, ATrack={_m4vOptions.M4vAudioTrackIndex}");

            // Run conversion directly in Apple M4V mode
            await StartConversionInternalAsync(forceM4v: true);
        }
        else
        {
            AppendLog("Apple M4V creation cancelled by user.");
        }
    }

    public void OnClearLogClick(object? sender, RoutedEventArgs e)
    {
        LogTextBox.Text = "";
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
        AppleM4vButton.IsEnabled = !isConverting;

        AddFilesButton.IsEnabled = !isConverting;
        AddTrackButton.IsEnabled = !isConverting;
        RemoveFilesButton.IsEnabled = !isConverting;
        ClearFilesButton.IsEnabled = !isConverting;
        BrowseOutputDirButton.IsEnabled = !isConverting;
        OutputDirTextBox.IsEnabled = !isConverting;

        CodecComboBox.IsEnabled = !isConverting;
        EncoderComboBox.IsEnabled = !isConverting;
        PresetComboBox.IsEnabled = !isConverting;
        AudioNormComboBox.IsEnabled = !isConverting;
        GenreComboBox.IsEnabled = !isConverting && ((AudioNormComboBox.SelectedItem as ComboBoxItem)?.Tag?.ToString() == "loudness_norm_2pass");
        AudioModeComboBox.IsEnabled = !isConverting;
        FilterComboBox.IsEnabled = !isConverting && (CodecComboBox.SelectedItem?.ToString() == "software");
        FilterPresetComboBox.IsEnabled = !isConverting && (CodecComboBox.SelectedItem?.ToString() == "software");

        OverwriteCheckBox.IsEnabled = !isConverting;
        DryRunCheckBox.IsEnabled = !isConverting;
    }

    // =========================================================================
    // Conversion Execution Pipeline
    // =========================================================================

    public async void OnStartConversionClick(object? sender, RoutedEventArgs e)
    {
        await StartConversionInternalAsync(forceM4v: false);
    }

    private async Task StartConversionInternalAsync(bool forceM4v)
    {
        if (_inputFiles.Count == 0)
        {
            AppendLog("Error: No input files specified.");
            StatusLabel.Text = "Error: No input files specified.";
            return;
        }

        string selectedGroup = CodecComboBox.SelectedItem?.ToString() ?? "software";
        string selectedEncoder = EncoderComboBox.SelectedItem?.ToString() ?? "prores_ks";
        string selectedPreset = PresetComboBox.SelectedItem?.ToString() ?? "standard";

        string finalCodec;
        if (forceM4v)
        {
            finalCodec = "m4v";
        }
        else
        {
            if (!_presetDb.ResolveSelection(_platform, selectedGroup, selectedEncoder, out finalCodec))
            {
                AppendLog($"Error: Unable to resolve selection for group '{selectedGroup}' and encoder '{selectedEncoder}'.");
                StatusLabel.Text = "Error: Invalid codec selection.";
                return;
            }
        }

        // Validate Mux Mode constraints
        if (finalCodec.Equals("mux", StringComparison.OrdinalIgnoreCase))
        {
            if (_inputFiles.Count != 1)
            {
                AppendLog("Error: Mux mode requires exactly one source file.");
                StatusLabel.Text = "Error: Mux mode requires exactly 1 file.";
                return;
            }

            if (string.IsNullOrEmpty(_videoTrackPath) || !File.Exists(_videoTrackPath))
            {
                AppendLog("Error: Mux mode requires a valid replacement video track.");
                StatusLabel.Text = "Error: Missing video track.";
                return;
            }

            if (string.IsNullOrEmpty(_tools.Mkvmerge) || !File.Exists(_tools.Mkvmerge))
            {
                AppendLog("Error: mkvmerge tool is required for Mux mode but was not found.");
                StatusLabel.Text = "Error: mkvmerge not found.";
                return;
            }
        }

        // Validate Apple M4V constraints
        if (finalCodec.Equals("m4v", StringComparison.OrdinalIgnoreCase))
        {
            if (string.IsNullOrEmpty(_tools.Mp4Box) || !File.Exists(_tools.Mp4Box))
            {
                AppendLog("Error: MP4Box tool is required for Apple M4V mode but was not found.");
                StatusLabel.Text = "Error: MP4Box not found.";
                return;
            }
        }

        string selectedNorm = (AudioNormComboBox.SelectedItem as ComboBoxItem)?.Tag?.ToString() ?? "none";
        string selectedAudioMode = (AudioModeComboBox.SelectedItem as ComboBoxItem)?.Tag?.ToString() ?? "pcm";
        string filterTag = (FilterComboBox.SelectedItem as ComboBoxItem)?.Tag?.ToString() ?? "none";
        int deblockMode = filterTag == "weak" ? 2 : filterTag == "strong" ? 3 : 1;

        int genreIndex = 0;
        if (selectedNorm == "loudness_norm_2pass")
        {
            if (GenreComboBox.SelectedItem is ComboBoxItem genreItem &&
                int.TryParse(genreItem.Tag?.ToString(), out int gVal) && gVal > 0)
            {
                genreIndex = gVal;
            }
            else
            {
                genreIndex = 1; // default to EDM if none selected
            }
        }

        bool overwrite = OverwriteCheckBox.IsChecked ?? false;
        bool dryRun = DryRunCheckBox.IsChecked ?? false;
        string outputDir = OutputDirTextBox.Text?.Trim() ?? "";

        var opts = new ConvertOptions
        {
            SelectionGroup = selectedGroup,
            SelectionEncoder = selectedEncoder,
            Codec = finalCodec,
            Preset = selectedPreset,
            Deblock = deblockMode,
            AudioNorm = selectedNorm,
            AudioOutputMode = selectedAudioMode,
            Genre = genreIndex,
            Overwrite = overwrite,
            DryRun = dryRun,
            OutputDir = outputDir,
            VideoTrackPath = _videoTrackPath
        };

        // Vulkan Device
        if (VulkanDevRow.IsVisible && VulkanDevComboBox.SelectedItem is ComboBoxItem vkItem &&
            int.TryParse(vkItem.Tag?.ToString(), out int vkDev))
        {
            opts.VulkanDevice = vkDev;
        }

        // VAAPI Device
        if (VaapiDevRow.IsVisible && !string.IsNullOrWhiteSpace(VaapiDevTextBox.Text))
        {
            opts.HwDevice = VaapiDevTextBox.Text.Trim();
        }

        // Copy M4V parameters
        opts.M4vVideoTrackIndex = _m4vOptions.M4vVideoTrackIndex;
        opts.M4vAudioTrackIndex = _m4vOptions.M4vAudioTrackIndex;
        opts.M4vAc3BitrateKbps = _m4vOptions.M4vAc3BitrateKbps;
        opts.M4vAudioLang = _m4vOptions.M4vAudioLang;
        opts.M4vAddChapters = _m4vOptions.M4vAddChapters;
        opts.M4vEditBeforeMux = _m4vOptions.M4vEditBeforeMux;

        if (selectedNorm == "loudness_norm_2pass")
        {
            opts.ApplyGenreTargets();
        }

        SetUiState(true);
        _cts = new CancellationTokenSource();
        LogTextBox.Text = "";
        AppendLog($"Starting conversion session ({_inputFiles.Count} files)...");
        AppendLog($"Selection: Group={selectedGroup}, Encoder={selectedEncoder}, ResolvedCodec={finalCodec}, Preset={selectedPreset}");
        AppendLog($"Audio: Norm={selectedNorm}, Mode={selectedAudioMode}, Genre={genreIndex}");
        if (deblockMode > 1) AppendLog($"Filter: Deblock mode {deblockMode} ({filterTag})");

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
                StatusLabel.Text = $"Conversion finished with status: {result}";
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