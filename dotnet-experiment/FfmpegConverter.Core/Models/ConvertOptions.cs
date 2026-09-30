namespace FfmpegConverter.Core.Models;

public class ConvertOptions
{
    // ==========================================
    // SELECTION (v3.0 catalog: group/encoder)
    // ==========================================
    public string SelectionGroup { get; set; } = "software";
    public string SelectionEncoder { get; set; } = "prores_ks";

    // ==========================================
    // VIDEO
    // ==========================================
    public string Codec { get; set; } = "prores_ks";
    public string Preset { get; set; } = "standard";
    public int Deblock { get; set; } = 1; // 1=none, 2=weak, 3=strong
    public int VideoQuality { get; set; } = -1;
    public int HevcVtBitrateKbps { get; set; } = 0;
    public int VulkanDevice { get; set; } = -1; // -1 = unresolved/auto
    public string HwDevice { get; set; } = ""; // VAAPI render node path

    // ==========================================
    // AUDIO NORMALIZATION & OUTPUT
    // ==========================================
    public string AudioNorm { get; set; } = "none"; // "none", "peak_norm", "peak_norm_2pass", "loudness_norm", "loudness_norm_2pass"
    public string AudioOutputMode { get; set; } = "pcm"; // "pcm", "fdk_aac_320", "fdk_aac_320_ac3_640"
    public int Genre { get; set; } = 1; // 0=none, 1=EDM, 2=Rock, 3=Hip-Hop, 4=Classical, 5=Podcast
    public bool UseAacForH265 { get; set; } = false;

    // INTERNAL PARAMETERS FOR 2-PASS
    public double Gain { get; set; } = 0.0;
    public double ITarget { get; set; } = -11.0;
    public double TpTarget { get; set; } = -1.5;
    public double LraTarget { get; set; } = 7.0;
    public double MeasuredI { get; set; } = 0.0;
    public double MeasuredTp { get; set; } = 0.0;
    public double MeasuredLra { get; set; } = 0.0;
    public double MeasuredThresh { get; set; } = 0.0;
    public double MeasuredOffset { get; set; } = 0.0;

    // ==========================================
    // MUX PIPELINE
    // ==========================================
    public string VideoTrackPath { get; set; } = "";

    // ==========================================
    // APPLE M4V PIPELINE
    // ==========================================
    public int M4vVideoTrackIndex { get; set; } = 0;
    public int M4vAudioTrackIndex { get; set; } = 0;
    public int M4vAc3BitrateKbps { get; set; } = 640;
    public string M4vAudioLang { get; set; } = "rus";
    public bool M4vAddChapters { get; set; } = true;
    public bool M4vEditBeforeMux { get; set; } = false;

    // ==========================================
    // OUTPUT & EXECUTION
    // ==========================================
    public bool Overwrite { get; set; } = false;
    public bool DryRun { get; set; } = false;
    public string OutputDir { get; set; } = "";
    public int OutputDirStatus { get; set; } = 0;

    /// <summary>
    /// Applies recommended EBU R128 targets based on genre (1=EDM, 2=Rock, 3=Hip-Hop, 4=Classical, 5=Podcast).
    /// </summary>
    public void ApplyGenreTargets()
    {
        switch (Genre)
        {
            case 1: ITarget = -11.0; TpTarget = -1.5; LraTarget = 6.0; break;  // EDM
            case 2: ITarget = -11.0; TpTarget = -1.0; LraTarget = 7.0; break;  // Rock
            case 3: ITarget = -12.0; TpTarget = -1.0; LraTarget = 6.0; break;  // Hip-Hop
            case 4: ITarget = -16.0; TpTarget = -2.0; LraTarget = 12.0; break; // Classical
            case 5: ITarget = -16.0; TpTarget = -1.5; LraTarget = 7.0; break;  // Podcast
            default: ITarget = -11.0; TpTarget = -1.5; LraTarget = 7.0; break;
        }
    }
}
