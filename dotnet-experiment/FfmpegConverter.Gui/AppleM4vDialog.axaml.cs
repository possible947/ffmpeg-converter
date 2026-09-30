using System;
using Avalonia.Controls;
using Avalonia.Interactivity;
using FfmpegConverter.Core.Models;

namespace FfmpegConverter.Gui;

public partial class AppleM4vDialog : Window
{
    public bool Confirmed { get; private set; }

    public int VideoTrackIndex => (int)(VideoTrackSpin.Value ?? 0);
    public int AudioTrackIndex => (int)(AudioTrackSpin.Value ?? 0);
    public int Ac3BitrateKbps
    {
        get
        {
            if (Ac3BitrateCombo.SelectedItem is ComboBoxItem item &&
                int.TryParse(item.Tag?.ToString(), out int val))
            {
                return val;
            }
            return 640;
        }
    }
    public string AudioLanguage => string.IsNullOrWhiteSpace(AudioLangTextBox.Text) ? "rus" : AudioLangTextBox.Text.Trim();
    public bool AddChapters => AddChaptersCheckBox.IsChecked ?? true;
    public bool EditBeforeMux => EditBeforeMuxCheckBox.IsChecked ?? false;

    public AppleM4vDialog()
    {
        InitializeComponent();
    }

    public AppleM4vDialog(ConvertOptions opts) : this()
    {
        VideoTrackSpin.Value = opts.M4vVideoTrackIndex;
        AudioTrackSpin.Value = opts.M4vAudioTrackIndex;

        foreach (var item in Ac3BitrateCombo.Items)
        {
            if (item is ComboBoxItem cbi && cbi.Tag?.ToString() == opts.M4vAc3BitrateKbps.ToString())
            {
                Ac3BitrateCombo.SelectedItem = cbi;
                break;
            }
        }

        AudioLangTextBox.Text = !string.IsNullOrEmpty(opts.M4vAudioLang) ? opts.M4vAudioLang : "rus";
        AddChaptersCheckBox.IsChecked = opts.M4vAddChapters;
        EditBeforeMuxCheckBox.IsChecked = opts.M4vEditBeforeMux;
    }

    public void ApplyToOptions(ConvertOptions opts)
    {
        opts.M4vVideoTrackIndex = VideoTrackIndex;
        opts.M4vAudioTrackIndex = AudioTrackIndex;
        opts.M4vAc3BitrateKbps = Ac3BitrateKbps;
        opts.M4vAudioLang = AudioLanguage;
        opts.M4vAddChapters = AddChapters;
        opts.M4vEditBeforeMux = EditBeforeMux;
    }

    private void OnStartClick(object? sender, RoutedEventArgs e)
    {
        Confirmed = true;
        Close(true);
    }

    private void OnCancelClick(object? sender, RoutedEventArgs e)
    {
        Confirmed = false;
        Close(false);
    }
}