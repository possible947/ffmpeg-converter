using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text.Json;

namespace FfmpegConverter.Core.Presets;

public sealed class HqPresetCatalog
{
    private readonly Dictionary<string, HqEncoder> _encoders;

    private HqPresetCatalog(string runtimeRoot, Dictionary<string, HqEncoder> encoders)
        => (RuntimeRoot, _encoders) = (runtimeRoot, encoders);

    public string RuntimeRoot { get; }
    public IReadOnlyList<string> Encoders => _encoders.Keys.ToArray();

    public static HqPresetCatalog? LoadAvailable(string? runtimeRoot = null)
    {
        if (!RuntimeInformation.IsOSPlatform(OSPlatform.Linux) &&
            !RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
            return null;

        string? root = runtimeRoot ?? ResolveRuntimeRoot();
        if (string.IsNullOrWhiteSpace(root))
            return null;

        string libraryName = RuntimeInformation.IsOSPlatform(OSPlatform.OSX)
            ? "libhq_converter.dylib"
            : "libhq_converter.so";
        string libraryPath = Path.Combine(root, "modules", "library", "bin", libraryName);
        string catalogPath = Path.Combine(root, "modules", "encoder_orchestrator_v2",
            "config", "presets", "encoder_presets_v2.json");
        if (!File.Exists(libraryPath) || !File.Exists(catalogPath))
            return null;

        try
        {
            using JsonDocument document = JsonDocument.Parse(File.ReadAllText(catalogPath));
            if (!document.RootElement.TryGetProperty("encoders", out JsonElement encodersJson) ||
                encodersJson.ValueKind != JsonValueKind.Object)
                return null;

            var encoders = new Dictionary<string, HqEncoder>(StringComparer.Ordinal);
            foreach (JsonProperty encoderJson in encodersJson.EnumerateObject())
            {
                if (encoderJson.Value.ValueKind != JsonValueKind.Object ||
                    !encoderJson.Value.TryGetProperty("presets", out JsonElement presetsJson) ||
                    presetsJson.ValueKind != JsonValueKind.Object)
                    continue;

                string[] presets = presetsJson.EnumerateObject()
                    .Select(preset => preset.Name)
                    .ToArray();
                if (presets.Length == 0)
                    continue;

                string? defaultPreset = encoderJson.Value.TryGetProperty("default_preset", out JsonElement defaultJson) &&
                    defaultJson.ValueKind == JsonValueKind.String
                    ? defaultJson.GetString()
                    : null;
                if (string.IsNullOrEmpty(defaultPreset) ||
                    !presets.Contains(defaultPreset, StringComparer.Ordinal))
                    defaultPreset = presets[0];

                encoders[encoderJson.Name] = new HqEncoder(defaultPreset!, presets);
            }

            return encoders.Count > 0 ? new HqPresetCatalog(Path.GetFullPath(root), encoders) : null;
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

    public bool ContainsEncoder(string encoder) => _encoders.ContainsKey(encoder);

    public IReadOnlyList<string> GetPresets(string encoder)
        => _encoders.TryGetValue(encoder, out HqEncoder? entry) ? entry.Presets : Array.Empty<string>();

    public string? GetDefaultPreset(string encoder)
        => _encoders.TryGetValue(encoder, out HqEncoder? entry) ? entry.DefaultPreset : null;

    public bool ContainsPreset(string encoder, string preset)
        => _encoders.TryGetValue(encoder, out HqEncoder? entry) &&
           entry.Presets.Contains(preset, StringComparer.Ordinal);

    public static bool IsProResEncoder(string encoder)
        => string.Equals(encoder, "ffmpeg-prores", StringComparison.Ordinal) ||
           string.Equals(encoder, "ffmpeg-prores_ks", StringComparison.Ordinal);

    private static string? ResolveRuntimeRoot()
    {
        string? overrideRoot = Environment.GetEnvironmentVariable("FFC_HQ_ROOT");
        if (!string.IsNullOrWhiteSpace(overrideRoot))
            return overrideRoot;

        string executableDirectory = AppContext.BaseDirectory;
        string[] candidates =
        {
            Path.Combine(executableDirectory, "hq_converter"),
            Path.GetFullPath(Path.Combine(executableDirectory, "..", "Resources", "hq_converter"))
        };
        return candidates.FirstOrDefault(Directory.Exists);
    }

    private sealed record HqEncoder(string DefaultPreset, string[] Presets);
}
