using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace FfmpegConverter.Core.Presets;

public class PresetInfo
{
    [JsonPropertyName("ffmpeg_args")]
    public string FfmpegArgs { get; set; } = "";

    [JsonPropertyName("container")]
    public string Container { get; set; } = "";

    [JsonPropertyName("pix_fmt")]
    public string PixFmt { get; set; } = "";
}

public class PresetDb
{
    private static readonly string BuiltinPresetsJson = @"
    {
        ""version"": ""1.0"",
        ""linux"": {
            ""copy"": { ""default"": { ""ffmpeg_args"": ""-c:v copy"", ""container"": ""mkv"" } },
            ""prores"": { ""default"": { ""ffmpeg_args"": ""-c:v prores -profile:v 2"", ""container"": ""mov"" } }
        },
        ""macos"": {
            ""copy"": { ""default"": { ""ffmpeg_args"": ""-c:v copy"", ""container"": ""mkv"" } },
            ""prores"": { ""default"": { ""ffmpeg_args"": ""-c:v prores -profile:v 2"", ""container"": ""mov"" } }
        },
        ""windows"": {
            ""copy"": { ""default"": { ""ffmpeg_args"": ""-c:v copy"", ""container"": ""mkv"" } },
            ""prores"": { ""default"": { ""ffmpeg_args"": ""-c:v prores -profile:v 2"", ""container"": ""mov"" } }
        }
    }";

    // Dict<Platform, Dict<Codec, Dict<Preset, PresetInfo>>>
    public Dictionary<string, Dictionary<string, Dictionary<string, PresetInfo>>> Platforms { get; private set; } = new(StringComparer.OrdinalIgnoreCase);
    public string Version { get; private set; } = "1.0";
    public string Description { get; private set; } = "";
    public string LoadedPath { get; private set; } = "builtin";

    public static PresetDb Load()
    {
        var db = new PresetDb();
        string path = db.ResolvePresetsPath();
        string jsonText = BuiltinPresetsJson;

        if (!string.IsNullOrEmpty(path) && File.Exists(path))
        {
            try
            {
                jsonText = File.ReadAllText(path);
                db.LoadedPath = path;
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine($"[PresetDb] Error reading presets file at {path}: {ex.Message}. Falling back to builtin presets.");
            }
        }

        try
        {
            using var doc = JsonDocument.Parse(jsonText);
            var root = doc.RootElement;

            if (root.TryGetProperty("version", out var vProp))
                db.Version = vProp.GetString() ?? "1.0";
            if (root.TryGetProperty("description", out var dProp))
                db.Description = dProp.GetString() ?? "";

            // Parse platform sections (excluding version, description, features)
            foreach (var prop in root.EnumerateObject())
            {
                string key = prop.Name.ToLower();
                if (key == "version" || key == "description" || key == "features")
                    continue;

                var platformDict = new Dictionary<string, Dictionary<string, PresetInfo>>(StringComparer.OrdinalIgnoreCase);
                foreach (var codecProp in prop.Value.EnumerateObject())
                {
                    string codecName = codecProp.Name;
                    var codecDict = new Dictionary<string, PresetInfo>(StringComparer.OrdinalIgnoreCase);
                    
                    foreach (var presetProp in codecProp.Value.EnumerateObject())
                    {
                        string presetName = presetProp.Name;
                        var info = JsonSerializer.Deserialize<PresetInfo>(presetProp.Value.GetRawText());
                        if (info != null)
                        {
                            codecDict[presetName] = info;
                        }
                    }
                    platformDict[codecName] = codecDict;
                }
                db.Platforms[prop.Name] = platformDict;
            }
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"[PresetDb] Error parsing presets JSON: {ex.Message}. Re-initializing with empty database.");
            db.Platforms = new(StringComparer.OrdinalIgnoreCase);
        }

        return db;
    }

    private string ResolvePresetsPath()
    {
        // 1. Env Var PRESETS_PATH
        string? envPath = Environment.GetEnvironmentVariable("PRESETS_PATH");
        if (!string.IsNullOrEmpty(envPath))
        {
            string p = Path.Combine(envPath, "presets.json");
            if (File.Exists(p)) return p;
            if (File.Exists(envPath)) return envPath;
        }

        // 2. Process directory
        string exePath = AppContext.BaseDirectory;
        if (!string.IsNullOrEmpty(exePath))
        {
            string p = Path.Combine(exePath, "presets.json");
            if (File.Exists(p)) return p;
        }

        // 3. Parent / Ancestor directories (for development checkout)
        string? current = Directory.GetCurrentDirectory();
        while (!string.IsNullOrEmpty(current))
        {
            string p = Path.Combine(current, "presets.json");
            if (File.Exists(p)) return p;

            string subP = Path.Combine(current, "src", "presets.json");
            if (File.Exists(subP)) return subP;

            current = Directory.GetParent(current)?.FullName;
        }

        return "";
    }

    public IEnumerable<string> GetCodecs(string platform)
    {
        if (Platforms.TryGetValue(platform, out var codecs))
        {
            return codecs.Keys;
        }
        return Array.Empty<string>();
    }

    public IEnumerable<string> GetPresets(string platform, string codec)
    {
        if (Platforms.TryGetValue(platform, out var codecs) && codecs.TryGetValue(codec, out var presets))
        {
            return presets.Keys;
        }
        return Array.Empty<string>();
    }

    public PresetInfo? GetPresetInfo(string platform, string codec, string preset)
    {
        if (Platforms.TryGetValue(platform, out var codecs) && 
            codecs.TryGetValue(codec, out var presets) && 
            presets.TryGetValue(preset, out var info))
        {
            return info;
        }
        return null;
    }
}
