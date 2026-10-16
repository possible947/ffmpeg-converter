using System.IO;

namespace FfmpegConverter.Core.Presets;

public sealed class HqRuntimeLayout
{
    public HqRuntimeLayout(string runtimeRoot)
    {
        Root = Path.GetFullPath(runtimeRoot);
        ModulePath = Path.Combine(Root, "modules", "encoder_orchestrator_v2");
        ConfigurationPath = Path.Combine(ModulePath, "config", "toolchain_runtime.ini");
        PresetCatalogPath = Path.Combine(ModulePath, "config", "presets", "encoder_presets_v2.json");
        SegmentatorConfigurationPath = Path.Combine(
            Root, "modules", "segmentator", "config", "toolchain_runtime.ini");
        string libraryName = OperatingSystem.IsMacOS()
            ? "libhq_converter.dylib"
            : "libhq_converter.so";
        LibraryPath = Path.Combine(Root, "modules", "library", "bin", libraryName);
    }

    public string Root { get; }
    public string ModulePath { get; }
    public string ConfigurationPath { get; }
    public string PresetCatalogPath { get; }
    public string SegmentatorConfigurationPath { get; }
    public string LibraryPath { get; }

    public string? GetValidationError()
    {
        if (!Directory.Exists(Root))
            return $"HQ runtime root was not found: {Root}";
        if (!File.Exists(LibraryPath))
            return $"HQ library was not found: {LibraryPath}";
        if (!File.Exists(PresetCatalogPath))
            return $"HQ encoder preset catalog was not found: {PresetCatalogPath}";
        if (!File.Exists(ConfigurationPath))
            return $"HQ encoder toolchain configuration was not found: {ConfigurationPath}";
        if (!File.Exists(SegmentatorConfigurationPath))
            return $"HQ segmentator toolchain configuration was not found: {SegmentatorConfigurationPath}";
        return null;
    }
}
