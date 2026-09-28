namespace FfmpegConverter.Core.Models;

public class ConvertOptions
{
    // VIDEO
    public string Codec { get; set; } = "copy";
    public string Preset { get; set; } = "default";
    public int Deblock { get; set; } = 1; // 1=none, 2=weak, 3=strong

    // AUDIO NORMALIZATION
    public string AudioNorm { get; set; } = "none";
    public string AudioOutputMode { get; set; } = ""; // "pcm", "fdk_aac_320", "fdk_aac_320_ac3_640"
    public int Genre { get; set; } = 0; // 0=none, 1=EDM, 2=Rock, 3=Hip-Hop, 4=Classical, 5=Podcast

    // INTERNAL PARAMETERS FOR 2-PASS
    public double Gain { get; set; }
    public double ITarget { get; set; } = -11;
    public double TpTarget { get; set; } = -1.5;
    public double LraTarget { get; set; } = 7;
    public double MeasuredI { get; set; }
    public double MeasuredTp { get; set; }
    public double MeasuredLra { get; set; }
    public double MeasuredThresh { get; set; }
    public double MeasuredOffset { get; set; }

    // OUTPUT
    public bool Overwrite { get; set; } = false;
    public bool DryRun { get; set; } = false;
    public string OutputDir { get; set; } = "";
    public string VideoTrackPath { get; set; } = "";
    public string HwDevice { get; set; } = "";
    public int VideoQuality { get; set; } = -1;
    public bool UseAacForH265 { get; set; } = false;
    public int HevcVtBitrateKbps { get; set; }
    public int VulkanDevice { get; set; } = 1;
}
