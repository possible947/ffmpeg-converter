using System;
using System.IO;
using System.Linq;
using FfmpegConverter.Core.Presets;
using Xunit;

namespace FfmpegConverter.Tests;

public class PresetDbTests
{
    private readonly PresetDb _presetDb;

    public PresetDbTests()
    {
        _presetDb = PresetDb.Load();
    }

    [Fact]
    public void Load_LoadsPresetsJsonSuccessfully()
    {
        Assert.NotNull(_presetDb);
        Assert.Equal("1.0", _presetDb.Version);
        Assert.Contains("linux", _presetDb.Platforms.Keys, StringComparer.OrdinalIgnoreCase);
        Assert.Contains("macos", _presetDb.Platforms.Keys, StringComparer.OrdinalIgnoreCase);
        Assert.Contains("windows", _presetDb.Platforms.Keys, StringComparer.OrdinalIgnoreCase);
    }

    [Fact]
    public void SelectionCatalog_CommonGroupsParsed()
    {
        Assert.True(_presetDb.Selection.Common.ContainsKey("software"));
        Assert.True(_presetDb.Selection.Common.ContainsKey("mux"));

        var softwareGroup = _presetDb.Selection.Common["software"];
        Assert.True(softwareGroup.Enabled);
        Assert.Contains("prores", softwareGroup.Encoders.Keys, StringComparer.OrdinalIgnoreCase);
        Assert.Contains("prores_ks", softwareGroup.Encoders.Keys, StringComparer.OrdinalIgnoreCase);

        var muxGroup = _presetDb.Selection.Common["mux"];
        Assert.True(muxGroup.Enabled);
        Assert.Contains("copy", muxGroup.Encoders.Keys, StringComparer.OrdinalIgnoreCase);
        Assert.Contains("mux", muxGroup.Encoders.Keys, StringComparer.OrdinalIgnoreCase);
        Assert.Contains("m4v", muxGroup.Encoders.Keys, StringComparer.OrdinalIgnoreCase);
    }

    [Fact]
    public void SelectionCatalog_PlatformHwaccelGroupsParsed()
    {
        Assert.True(_presetDb.Selection.Platforms.ContainsKey("linux"));
        var linuxPlat = _presetDb.Selection.Platforms["linux"];
        Assert.True(linuxPlat.HwaccelEnabled);
        Assert.Contains("vaapi", linuxPlat.Groups.Keys, StringComparer.OrdinalIgnoreCase);

        Assert.True(_presetDb.Selection.Platforms.ContainsKey("macos"));
        var macosPlat = _presetDb.Selection.Platforms["macos"];
        Assert.True(macosPlat.HwaccelEnabled);
        Assert.Contains("videotoolbox", macosPlat.Groups.Keys, StringComparer.OrdinalIgnoreCase);

        Assert.True(_presetDb.Selection.Platforms.ContainsKey("windows"));
        var winPlat = _presetDb.Selection.Platforms["windows"];
        Assert.True(winPlat.HwaccelEnabled);
        Assert.Contains("nvenc", winPlat.Groups.Keys, StringComparer.OrdinalIgnoreCase);
    }

    [Theory]
    // Common / Software
    [InlineData("macos", "software", "prores", "prores")]
    [InlineData("linux", "software", "prores_ks", "prores_ks")]
    // Common / Mux
    [InlineData("linux", "mux", "copy", "copy")]
    [InlineData("linux", "mux", "mkv", "mux")]
    [InlineData("linux", "mux", "mov", "mux")]
    [InlineData("linux", "mux", "m4v", "m4v")]
    // Direct alias
    [InlineData("linux", "copy", "", "copy")]
    // Linux VAAPI
    [InlineData("linux", "vaapi", "h264", "h264_vaapi")]
    [InlineData("linux", "vaapi", "hevc", "hevc_vaapi")]
    [InlineData("linux", "vaapi", "hevc_10bit", "hevc_vaapi_10bit")]
    // macOS VideoToolbox
    [InlineData("macos", "videotoolbox", "prores", "prores_videotoolbox")]
    [InlineData("macos", "videotoolbox", "h264", "h264_videotoolbox")]
    [InlineData("macos", "videotoolbox", "hevc", "hevc_videotoolbox")]
    // Windows NVENC
    [InlineData("windows", "nvenc", "h264", "h264_nvenc")]
    [InlineData("windows", "nvenc", "hevc", "hevc_nvenc")]
    [InlineData("windows", "nvenc", "hevc_10bit", "hevc_nvenc_10bit")]
    // Windows Vulkan
    [InlineData("windows", "vulkan", "prores_ks", "prores_ks_vulkan")]
    public void ResolveSelection_ResolvesCorrectFinalCodec(string platform, string group, string encoder, string expectedCodec)
    {
        bool success = _presetDb.ResolveSelection(platform, group, encoder, out string finalCodec);
        Assert.True(success, $"Failed to resolve {group}/{encoder} on {platform}");
        Assert.Equal(expectedCodec, finalCodec);
    }

    [Fact]
    public void GetSelectionGroups_ReturnsAvailableGroups()
    {
        var linuxGroups = _presetDb.GetSelectionGroups("linux");
        Assert.Contains("software", linuxGroups);
        Assert.Contains("mux", linuxGroups);
        Assert.Contains("vaapi", linuxGroups);

        var macosGroups = _presetDb.GetSelectionGroups("macos");
        Assert.Contains("software", macosGroups);
        Assert.Contains("mux", macosGroups);
        Assert.Contains("videotoolbox", macosGroups);
    }

    [Fact]
    public void GetSelectionGroups_WithFilter_FiltersCorrectly()
    {
        // Hide NVENC group
        var filteredGroups = _presetDb.GetSelectionGroups("windows", (grp, enc, finalCodec, reqs) => grp != "nvenc");
        Assert.DoesNotContain("nvenc", filteredGroups);
        Assert.Contains("software", filteredGroups);
    }

    [Fact]
    public void GetGroupEncoders_MuxReturnsAllModes()
    {
        var muxEncoders = _presetDb.GetGroupEncoders("linux", "mux");
        Assert.Equal(new[] { "copy", "mkv", "mov", "m4v" }, muxEncoders);
    }

    [Fact]
    public void GetSelectionPresets_ReturnsCorrectPresets()
    {
        // Software ProRes
        var proresPresets = _presetDb.GetSelectionPresets("macos", "software", "prores");
        Assert.Contains("lt", proresPresets);
        Assert.Contains("standard", proresPresets);
        Assert.Contains("hq", proresPresets);
        Assert.Contains("4444", proresPresets);

        // macOS VideoToolbox HEVC
        var vtHevcPresets = _presetDb.GetSelectionPresets("macos", "videotoolbox", "hevc");
        Assert.Contains("default", vtHevcPresets);
        Assert.Contains("low", vtHevcPresets);
        Assert.Contains("medium", vtHevcPresets);
        Assert.Contains("high", vtHevcPresets);

        // Linux VAAPI 10-bit HEVC
        var vaapi10bitPresets = _presetDb.GetSelectionPresets("linux", "vaapi", "hevc_10bit");
        Assert.Contains("default", vaapi10bitPresets);
        Assert.Contains("speed", vaapi10bitPresets);
        Assert.Contains("balance", vaapi10bitPresets);
        Assert.Contains("quality", vaapi10bitPresets);
    }

    [Theory]
    [InlineData("macos", "prores", "standard", true)]
    [InlineData("macos", "prores", "non_existent_preset", false)]
    [InlineData("linux", "hevc_vaapi_10bit", "quality", true)]
    [InlineData("linux", "h264_vaapi", "speed", true)]
    public void IsValidPreset_ValidatesCorrectly(string platform, string codec, string preset, bool expected)
    {
        bool actual = _presetDb.IsValidPreset(platform, codec, preset);
        Assert.Equal(expected, actual);
    }

    [Fact]
    public void SubstitutePlaceholders_ReplacesVaapiDevicePlaceholder()
    {
        // Custom device
        string result1 = PresetDb.SubstitutePlaceholders("-vaapi_device {vaapi_device}", vaapiDevice: "/dev/dri/renderD129");
        Assert.Equal("-vaapi_device /dev/dri/renderD129", result1);

        // Default device
        string result2 = PresetDb.SubstitutePlaceholders("-vaapi_device {vaapi_device}", vaapiDevice: null);
        Assert.Equal("-vaapi_device /dev/dri/renderD128", result2);
    }

    [Fact]
    public void SubstitutePlaceholders_ReplacesVulkanDevicePlaceholder()
    {
        // Custom device index
        string result1 = PresetDb.SubstitutePlaceholders("-init_hw_device vulkan=vk:{vk_device}", vkDevice: 1);
        Assert.Equal("-init_hw_device vulkan=vk:1", result1);

        // Default device index
        string result2 = PresetDb.SubstitutePlaceholders("-init_hw_device vulkan=vk:{vk_device}", vkDevice: -1);
        Assert.Equal("-init_hw_device vulkan=vk:0", result2);
    }

    [Fact]
    public void SubstitutePlaceholders_ReplacesVideoToolboxBitrate()
    {
        string result = PresetDb.SubstitutePlaceholders("-b:v {vt_bitrate}k", vtBitrate: 5000);
        Assert.Equal("-b:v 5000k", result);
    }

    [Fact]
    public void SubstitutePlaceholders_MultiplePlaceholders()
    {
        string result = PresetDb.SubstitutePlaceholders(
            "-vaapi_device {vaapi_device} -init_hw_device vulkan=vk:{vk_device}",
            vaapiDevice: "/dev/dri/renderD130",
            vkDevice: 2
        );
        Assert.Equal("-vaapi_device /dev/dri/renderD130 -init_hw_device vulkan=vk:2", result);
    }

    [Fact]
    public void PresetInfo_ExtendedFieldsParsed()
    {
        var info = _presetDb.GetPresetInfo("linux", "h264_vaapi", "default");
        Assert.NotNull(info);
        Assert.Equal("-c:v h264_vaapi -rc_mode auto", info.FfmpegArgs);
        Assert.Equal("-vaapi_device {vaapi_device}", info.PreInputArgs);
        Assert.Equal("format=nv12,hwupload", info.VideoFilter);
        Assert.Equal("mkv", info.Container);
        Assert.Contains("h264_vaapi", info.Requires);
    }
}
