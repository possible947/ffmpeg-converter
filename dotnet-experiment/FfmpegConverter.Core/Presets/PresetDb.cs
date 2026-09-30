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

    [JsonPropertyName("pre_input_args")]
    public string PreInputArgs { get; set; } = "";

    [JsonPropertyName("video_filter")]
    public string VideoFilter { get; set; } = "";

    [JsonPropertyName("pipeline")]
    public string Pipeline { get; set; } = "";

    [JsonPropertyName("requires")]
    public List<string> Requires { get; set; } = new();

    [JsonPropertyName("profile_args")]
    public string ProfileArgs { get; set; } = "";

    [JsonPropertyName("rc_mode")]
    public string RcMode { get; set; } = "";
}

public class PresetDb
{
    private static readonly string BuiltinPresetsJson = @"
    {
        ""version"": ""1.0"",
        ""description"": ""Builtin fallback presets"",
        ""features"": { ""hq_converter"": false },
        ""linux"": {
            ""copy"": { ""default"": { ""ffmpeg_args"": ""-c:v copy"", ""container"": ""mkv"" } },
            ""prores"": {
                ""lt"": { ""ffmpeg_args"": ""-c:v prores -profile:v 1"", ""container"": ""mov"" },
                ""standard"": { ""ffmpeg_args"": ""-c:v prores -profile:v 2"", ""container"": ""mov"" },
                ""hq"": { ""ffmpeg_args"": ""-c:v prores -profile:v 3"", ""container"": ""mov"" },
                ""4444"": { ""ffmpeg_args"": ""-c:v prores -profile:v 4"", ""pix_fmt"": ""yuv444p10le"", ""container"": ""mov"" }
            },
            ""prores_ks"": {
                ""lt"": { ""ffmpeg_args"": ""-c:v prores_ks -profile:v lt"", ""container"": ""mov"" },
                ""standard"": { ""ffmpeg_args"": ""-c:v prores_ks -profile:v standard"", ""container"": ""mov"" },
                ""hq"": { ""ffmpeg_args"": ""-c:v prores_ks -profile:v hq"", ""container"": ""mov"" },
                ""4444"": { ""ffmpeg_args"": ""-c:v prores_ks -profile:v 4444"", ""pix_fmt"": ""yuv444p10le"", ""container"": ""mov"" }
            }
        },
        ""macos"": {
            ""copy"": { ""default"": { ""ffmpeg_args"": ""-c:v copy"", ""container"": ""mkv"" } },
            ""prores"": {
                ""lt"": { ""ffmpeg_args"": ""-c:v prores -profile:v 1"", ""container"": ""mov"" },
                ""standard"": { ""ffmpeg_args"": ""-c:v prores -profile:v 2"", ""container"": ""mov"" },
                ""hq"": { ""ffmpeg_args"": ""-c:v prores -profile:v 3"", ""container"": ""mov"" },
                ""4444"": { ""ffmpeg_args"": ""-c:v prores -profile:v 4"", ""pix_fmt"": ""yuv444p10le"", ""container"": ""mov"" }
            },
            ""prores_ks"": {
                ""lt"": { ""ffmpeg_args"": ""-c:v prores_ks -profile:v lt"", ""container"": ""mov"" },
                ""standard"": { ""ffmpeg_args"": ""-c:v prores_ks -profile:v standard"", ""container"": ""mov"" },
                ""hq"": { ""ffmpeg_args"": ""-c:v prores_ks -profile:v hq"", ""container"": ""mov"" },
                ""4444"": { ""ffmpeg_args"": ""-c:v prores_ks -profile:v 4444"", ""pix_fmt"": ""yuv444p10le"", ""container"": ""mov"" }
            }
        },
        ""windows"": {
            ""copy"": { ""default"": { ""ffmpeg_args"": ""-c:v copy"", ""container"": ""mkv"" } },
            ""prores"": {
                ""lt"": { ""ffmpeg_args"": ""-c:v prores -profile:v 1"", ""container"": ""mov"" },
                ""standard"": { ""ffmpeg_args"": ""-c:v prores -profile:v 2"", ""container"": ""mov"" },
                ""hq"": { ""ffmpeg_args"": ""-c:v prores -profile:v 3"", ""container"": ""mov"" },
                ""4444"": { ""ffmpeg_args"": ""-c:v prores -profile:v 4"", ""pix_fmt"": ""yuv444p10le"", ""container"": ""mov"" }
            },
            ""prores_ks"": {
                ""lt"": { ""ffmpeg_args"": ""-c:v prores_ks -profile:v lt"", ""container"": ""mov"" },
                ""standard"": { ""ffmpeg_args"": ""-c:v prores_ks -profile:v standard"", ""container"": ""mov"" },
                ""hq"": { ""ffmpeg_args"": ""-c:v prores_ks -profile:v hq"", ""container"": ""mov"" },
                ""4444"": { ""ffmpeg_args"": ""-c:v prores_ks -profile:v 4444"", ""pix_fmt"": ""yuv444p10le"", ""container"": ""mov"" }
            }
        },
        ""selection"": {
            ""common"": {
                ""software"": {
                    ""enabled"": true,
                    ""kind"": ""software_encoder_group"",
                    ""encoders"": {
                        ""prores"": { ""enabled"": true, ""final_codec"": ""prores"" },
                        ""prores_ks"": { ""enabled"": true, ""final_codec"": ""prores_ks"" }
                    }
                },
                ""mux"": {
                    ""enabled"": true,
                    ""kind"": ""video_pipeline_group"",
                    ""modes"": {
                        ""copy"": { ""enabled"": true, ""execution_codec"": ""copy"" },
                        ""mux"": { ""enabled"": true, ""execution_codec"": ""mux"", ""containers"": [ ""mkv"", ""mov"", ""m4v"" ] },
                        ""m4v"": { ""enabled"": true, ""execution_codec"": ""m4v"" }
                    }
                }
            },
            ""platforms"": {
                ""linux"": { ""hwaccel"": { ""enabled"": false, ""groups"": {} } },
                ""macos"": { ""hwaccel"": { ""enabled"": false, ""groups"": {} } },
                ""windows"": { ""hwaccel"": { ""enabled"": false, ""groups"": {} } }
            }
        }
    }";

    // Dict<Platform, Dict<Codec, Dict<Preset, PresetInfo>>>
    public Dictionary<string, Dictionary<string, Dictionary<string, PresetInfo>>> Platforms { get; private set; } = new(StringComparer.OrdinalIgnoreCase);
    public SelectionCatalog Selection { get; private set; } = new();
    public Dictionary<string, bool> Features { get; private set; } = new(StringComparer.OrdinalIgnoreCase);
    public string Version { get; private set; } = "1.0";
    public string Description { get; private set; } = "";
    public string LoadedPath { get; private set; } = "builtin";

    public static PresetDb Load(string? explicitPath = null)
    {
        var db = new PresetDb();
        string path = db.ResolvePresetsPath(explicitPath);
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

            // Parse features
            if (root.TryGetProperty("features", out var featProp) && featProp.ValueKind == JsonValueKind.Object)
            {
                foreach (var f in featProp.EnumerateObject())
                {
                    if (f.Value.ValueKind == JsonValueKind.True || f.Value.ValueKind == JsonValueKind.False)
                    {
                        db.Features[f.Name] = f.Value.GetBoolean();
                    }
                }
            }

            // Parse selection catalog
            if (root.TryGetProperty("selection", out var selProp) && selProp.ValueKind == JsonValueKind.Object)
            {
                db.ParseSelectionCatalog(selProp);
            }

            // Parse execution platform sections
            foreach (var prop in root.EnumerateObject())
            {
                string key = prop.Name.ToLowerInvariant();
                if (key == "version" || key == "description" || key == "features" || key == "selection")
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
            db.Selection = new SelectionCatalog();
        }

        return db;
    }

    private void ParseSelectionCatalog(JsonElement selProp)
    {
        // 1. common
        if (selProp.TryGetProperty("common", out var commonProp) && commonProp.ValueKind == JsonValueKind.Object)
        {
            foreach (var groupProp in commonProp.EnumerateObject())
            {
                var group = ParseSelectionGroup(groupProp.Name, groupProp.Value);
                Selection.Common[groupProp.Name] = group;
            }
        }

        // 2. platforms
        if (selProp.TryGetProperty("platforms", out var platformsProp) && platformsProp.ValueKind == JsonValueKind.Object)
        {
            foreach (var platProp in platformsProp.EnumerateObject())
            {
                var platSelection = new PlatformSelection();
                if (platProp.Value.TryGetProperty("hwaccel", out var hwProp) && hwProp.ValueKind == JsonValueKind.Object)
                {
                    platSelection.HwaccelEnabled = !hwProp.TryGetProperty("enabled", out var en) || en.GetBoolean();
                    if (hwProp.TryGetProperty("groups", out var groupsProp) && groupsProp.ValueKind == JsonValueKind.Object)
                    {
                        foreach (var grpProp in groupsProp.EnumerateObject())
                        {
                            var group = ParseSelectionGroup(grpProp.Name, grpProp.Value);
                            platSelection.Groups[grpProp.Name] = group;
                        }
                    }
                }
                Selection.Platforms[platProp.Name] = platSelection;
            }
        }
    }

    private static SelectionGroup ParseSelectionGroup(string groupName, JsonElement groupElem)
    {
        var group = new SelectionGroup
        {
            Name = groupName,
            Enabled = !groupElem.TryGetProperty("enabled", out var en) || en.GetBoolean(),
            Kind = groupElem.TryGetProperty("kind", out var k) ? k.GetString() : null,
            FeatureGate = groupElem.TryGetProperty("feature_gate", out var fg) ? fg.GetString() : null
        };

        JsonElement itemsElem;
        bool hasModes = groupElem.TryGetProperty("modes", out itemsElem);
        if (!hasModes)
        {
            groupElem.TryGetProperty("encoders", out itemsElem);
        }

        if (itemsElem.ValueKind == JsonValueKind.Object)
        {
            foreach (var encProp in itemsElem.EnumerateObject())
            {
                var enc = new SelectionEncoder
                {
                    Name = encProp.Name,
                    Enabled = !encProp.Value.TryGetProperty("enabled", out var ee) || ee.GetBoolean(),
                    Kind = encProp.Value.TryGetProperty("kind", out var ek) ? ek.GetString() : null,
                    FinalCodec = encProp.Value.TryGetProperty("final_codec", out var fc) ? fc.GetString() : null,
                    ExecutionCodec = encProp.Value.TryGetProperty("execution_codec", out var ec) ? ec.GetString() : null,
                    PixelFormat = encProp.Value.TryGetProperty("pixel_format", out var pf) ? pf.GetString() : null,
                    ProfileArgs = encProp.Value.TryGetProperty("profile_args", out var pa) ? pa.GetString() : null
                };

                if (encProp.Value.TryGetProperty("requires", out var reqProp) && reqProp.ValueKind == JsonValueKind.Array)
                {
                    foreach (var item in reqProp.EnumerateArray())
                    {
                        if (item.GetString() is { } s) enc.Requires.Add(s);
                    }
                }

                if (encProp.Value.TryGetProperty("bit_depths", out var bdProp) && bdProp.ValueKind == JsonValueKind.Array)
                {
                    foreach (var item in bdProp.EnumerateArray())
                    {
                        if (item.TryGetInt32(out int d)) enc.BitDepths.Add(d);
                    }
                }

                if (encProp.Value.TryGetProperty("presets", out var prProp) && prProp.ValueKind == JsonValueKind.Array)
                {
                    foreach (var item in prProp.EnumerateArray())
                    {
                        if (item.GetString() is { } s) enc.Presets.Add(s);
                    }
                }

                if (encProp.Value.TryGetProperty("containers", out var conProp) && conProp.ValueKind == JsonValueKind.Array)
                {
                    foreach (var item in conProp.EnumerateArray())
                    {
                        if (item.GetString() is { } s) enc.Containers.Add(s);
                    }
                }

                group.Encoders[encProp.Name] = enc;
            }
        }

        return group;
    }

    private string ResolvePresetsPath(string? explicitPath = null)
    {
        // 0. Explicit path argument
        if (!string.IsNullOrEmpty(explicitPath))
        {
            if (File.Exists(explicitPath)) return explicitPath;
            string subP = Path.Combine(explicitPath, "presets.json");
            if (File.Exists(subP)) return subP;
        }

        // 1. Env Var PRESETS_V2_PATH
        string? v2Path = Environment.GetEnvironmentVariable("PRESETS_V2_PATH");
        if (!string.IsNullOrEmpty(v2Path))
        {
            if (File.Exists(v2Path)) return v2Path;
            string p = Path.Combine(v2Path, "presets.json");
            if (File.Exists(p)) return p;
        }

        // 2. Env Var PRESETS_PATH
        string? envPath = Environment.GetEnvironmentVariable("PRESETS_PATH");
        if (!string.IsNullOrEmpty(envPath))
        {
            if (File.Exists(envPath)) return envPath;
            string p = Path.Combine(envPath, "presets.json");
            if (File.Exists(p)) return p;
        }

        // 3. Process executable directory
        string exePath = AppContext.BaseDirectory;
        if (!string.IsNullOrEmpty(exePath))
        {
            string p = Path.Combine(exePath, "presets.json");
            if (File.Exists(p)) return p;
        }

        // 4. User config directory
        try
        {
            string home = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
            if (!string.IsNullOrEmpty(home))
            {
                string linuxConfig = Path.Combine(home, ".config", "ffmpeg_converter", "presets.json");
                if (File.Exists(linuxConfig)) return linuxConfig;

                string macConfig = Path.Combine(home, "Library", "Preferences", "ffmpeg_converter", "presets.json");
                if (File.Exists(macConfig)) return macConfig;
            }

            string appData = Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);
            if (!string.IsNullOrEmpty(appData))
            {
                string winConfig = Path.Combine(appData, "ffmpeg_converter", "presets.json");
                if (File.Exists(winConfig)) return winConfig;
            }
        }
        catch { }

        // 5. Parent / Ancestor directories (for dev repository checkouts)
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

    // =========================================================================
    // Catalog Queries & Selection Resolution
    // =========================================================================

    public bool ResolveSelection(string platform, string group, string encoder, out string finalCodec)
    {
        finalCodec = "";

        if (string.IsNullOrEmpty(group))
            return false;

        // Terminal stream-copy mode
        if (string.Equals(group, "copy", StringComparison.OrdinalIgnoreCase))
        {
            finalCodec = "copy";
            return true;
        }

        // Apple M4V mode
        if (string.Equals(group, "m4v", StringComparison.OrdinalIgnoreCase))
        {
            finalCodec = "m4v";
            return true;
        }

        // Video pipeline group: mux
        if (string.Equals(group, "mux", StringComparison.OrdinalIgnoreCase))
        {
            if (string.Equals(encoder, "copy", StringComparison.OrdinalIgnoreCase))
            {
                finalCodec = "copy";
                return true;
            }
            if (string.Equals(encoder, "m4v", StringComparison.OrdinalIgnoreCase))
            {
                finalCodec = "m4v";
                return true;
            }
            if (string.Equals(encoder, "mkv", StringComparison.OrdinalIgnoreCase) ||
                string.Equals(encoder, "mov", StringComparison.OrdinalIgnoreCase))
            {
                finalCodec = "mux";
                return true;
            }
            if (string.IsNullOrEmpty(encoder) || string.Equals(encoder, "prores_ks", StringComparison.OrdinalIgnoreCase))
            {
                finalCodec = "mux";
                return true;
            }
        }

        // Direct codec match fallback (backward compatibility)
        if (string.IsNullOrEmpty(encoder) || string.Equals(encoder, "prores_ks", StringComparison.OrdinalIgnoreCase))
        {
            if (Platforms.TryGetValue(platform, out var directCodecs) && directCodecs.ContainsKey(group))
            {
                finalCodec = group;
                return true;
            }
        }

        // Find group
        SelectionGroup? groupObj = null;
        if (Selection.Common.TryGetValue(group, out var commonGroup) && commonGroup.Enabled)
        {
            groupObj = commonGroup;
        }
        else if (Selection.Platforms.TryGetValue(platform, out var platSel) && platSel.HwaccelEnabled)
        {
            if (platSel.Groups.TryGetValue(group, out var platGroup) && platGroup.Enabled)
            {
                groupObj = platGroup;
            }
        }

        if (groupObj == null || !groupObj.Enabled)
            return false;

        if (!groupObj.Encoders.TryGetValue(encoder, out var encObj) || !encObj.Enabled)
            return false;

        string? resolved = encObj.FinalCodec ?? encObj.ExecutionCodec;
        if (string.IsNullOrEmpty(resolved))
            return false;

        if (encoder.Contains("_10bit", StringComparison.OrdinalIgnoreCase) &&
            !resolved.EndsWith("_10bit", StringComparison.OrdinalIgnoreCase))
        {
            finalCodec = resolved + "_10bit";
        }
        else
        {
            finalCodec = resolved;
        }

        return true;
    }

    public bool FindSelectionForCodec(string platform, string finalCodec, out string groupName, out string encoderName)
    {
        groupName = finalCodec;
        encoderName = finalCodec;

        if (string.Equals(finalCodec, "copy", StringComparison.OrdinalIgnoreCase))
        {
            groupName = "copy";
            encoderName = "copy";
            return true;
        }

        if (string.Equals(finalCodec, "mux", StringComparison.OrdinalIgnoreCase))
        {
            groupName = "mux";
            encoderName = "mux";
            return true;
        }

        if (string.Equals(finalCodec, "m4v", StringComparison.OrdinalIgnoreCase))
        {
            groupName = "mux";
            encoderName = "m4v";
            return true;
        }

        // Search common groups
        foreach (var (gName, group) in Selection.Common)
        {
            if (!group.Enabled) continue;
            foreach (var (eName, enc) in group.Encoders)
            {
                if (!enc.Enabled) continue;
                string? resolved = enc.FinalCodec ?? enc.ExecutionCodec;
                if (string.Equals(resolved, finalCodec, StringComparison.OrdinalIgnoreCase) ||
                    (eName.Contains("_10bit", StringComparison.OrdinalIgnoreCase) && string.Equals(resolved + "_10bit", finalCodec, StringComparison.OrdinalIgnoreCase)))
                {
                    groupName = gName;
                    encoderName = eName;
                    return true;
                }
            }
        }

        // Search platform groups
        if (Selection.Platforms.TryGetValue(platform, out var platSel) && platSel.HwaccelEnabled)
        {
            foreach (var (gName, group) in platSel.Groups)
            {
                if (!group.Enabled) continue;
                foreach (var (eName, enc) in group.Encoders)
                {
                    if (!enc.Enabled) continue;
                    string? resolved = enc.FinalCodec ?? enc.ExecutionCodec;
                    if (string.Equals(resolved, finalCodec, StringComparison.OrdinalIgnoreCase) ||
                        (eName.Contains("_10bit", StringComparison.OrdinalIgnoreCase) && string.Equals(resolved + "_10bit", finalCodec, StringComparison.OrdinalIgnoreCase)))
                    {
                        groupName = gName;
                        encoderName = eName;
                        return true;
                    }
                }
            }
        }

        return false;
    }

    public IReadOnlyList<string> GetSelectionGroups(
        string platform,
        Func<string, string, string, IReadOnlyList<string>, bool>? capabilityFilter = null)
    {
        var groups = new List<string>();

        // Check common groups
        foreach (var (name, group) in Selection.Common)
        {
            if (!group.Enabled) continue;
            var encoders = GetGroupEncoders(platform, name, capabilityFilter);
            if (encoders.Count > 0)
            {
                groups.Add(name);
            }
        }

        // Check platform hwaccel groups
        if (Selection.Platforms.TryGetValue(platform, out var platSel) && platSel.HwaccelEnabled)
        {
            foreach (var (name, group) in platSel.Groups)
            {
                if (!group.Enabled) continue;
                var encoders = GetGroupEncoders(platform, name, capabilityFilter);
                if (encoders.Count > 0)
                {
                    groups.Add(name);
                }
            }
        }

        return groups;
    }

    public IReadOnlyList<string> GetGroupEncoders(
        string platform,
        string group,
        Func<string, string, string, IReadOnlyList<string>, bool>? capabilityFilter = null)
    {
        var result = new List<string>();

        if (string.Equals(group, "mux", StringComparison.OrdinalIgnoreCase))
        {
            result.Add("copy");
            result.Add("mkv");
            result.Add("mov");
            result.Add("m4v");
            return result;
        }

        SelectionGroup? groupObj = null;
        if (Selection.Common.TryGetValue(group, out var commonGroup) && commonGroup.Enabled)
        {
            groupObj = commonGroup;
        }
        else if (Selection.Platforms.TryGetValue(platform, out var platSel) && platSel.HwaccelEnabled)
        {
            if (platSel.Groups.TryGetValue(group, out var platGroup) && platGroup.Enabled)
            {
                groupObj = platGroup;
            }
        }

        if (groupObj == null || !groupObj.Enabled)
            return result;

        foreach (var (encName, enc) in groupObj.Encoders)
        {
            if (!enc.Enabled) continue;
            string finalCodec = enc.FinalCodec ?? enc.ExecutionCodec ?? "";
            if (encName.Contains("_10bit", StringComparison.OrdinalIgnoreCase) &&
                !finalCodec.EndsWith("_10bit", StringComparison.OrdinalIgnoreCase))
            {
                finalCodec += "_10bit";
            }

            if (capabilityFilter == null || capabilityFilter(group, encName, finalCodec, enc.Requires))
            {
                result.Add(encName);
            }
        }

        return result;
    }

    public IReadOnlyList<string> GetSelectionPresets(string platform, string group, string encoder)
    {
        if (!ResolveSelection(platform, group, encoder, out string finalCodec))
            return Array.Empty<string>();

        // 1. Check if the selection encoder explicitly specifies presets
        SelectionGroup? groupObj = null;
        if (Selection.Common.TryGetValue(group, out var commonGroup)) groupObj = commonGroup;
        else if (Selection.Platforms.TryGetValue(platform, out var platSel)) platSel.Groups.TryGetValue(group, out groupObj);

        if (groupObj != null && groupObj.Encoders.TryGetValue(encoder, out var encObj) && encObj.Presets.Count > 0)
        {
            return encObj.Presets;
        }

        // 2. Fall back to platform execution codec presets
        string executionCodec = group switch
        {
            "mux" when encoder.Equals("copy", StringComparison.OrdinalIgnoreCase) => "copy",
            "mux" when encoder.Equals("m4v", StringComparison.OrdinalIgnoreCase) => "m4v",
            "mux" => "mux",
            _ => finalCodec
        };

        if (Platforms.TryGetValue(platform, out var platCodecs))
        {
            if (platCodecs.TryGetValue(executionCodec, out var codecDict))
            {
                return codecDict.Keys.ToList();
            }

            // If 10bit, check base codec
            if (executionCodec.EndsWith("_10bit", StringComparison.OrdinalIgnoreCase))
            {
                string baseCodec = executionCodec[..^6];
                if (platCodecs.TryGetValue(baseCodec, out var baseDict))
                {
                    return baseDict.Keys.ToList();
                }
            }
        }

        return new[] { "default" };
    }

    public bool IsValidPreset(string platform, string codec, string preset)
    {
        if (string.IsNullOrEmpty(codec) || string.IsNullOrEmpty(preset))
            return false;

        if (GetPresetInfo(platform, codec, preset) != null)
            return true;

        if (codec.EndsWith("_10bit", StringComparison.OrdinalIgnoreCase))
        {
            string baseCodec = codec[..^6];
            return GetPresetInfo(platform, baseCodec, preset) != null;
        }

        return false;
    }

    // =========================================================================
    // Placeholder Substitution
    // =========================================================================

    public static string SubstitutePlaceholders(
        string template,
        string? vaapiDevice = null,
        int vkDevice = -1,
        int vtBitrate = 0)
    {
        if (string.IsNullOrEmpty(template))
            return "";

        string vaapi = !string.IsNullOrEmpty(vaapiDevice) ? vaapiDevice : "/dev/dri/renderD128";
        int vk = vkDevice >= 0 ? vkDevice : 0;

        return template
            .Replace("{vaapi_device}", vaapi)
            .Replace("{vk_device}", vk.ToString())
            .Replace("{vt_bitrate}", vtBitrate.ToString());
    }

    // =========================================================================
    // Legacy / Direct Codec Accessors
    // =========================================================================

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
        if (Platforms.TryGetValue(platform, out var codecs))
        {
            if (codecs.TryGetValue(codec, out var presets))
            {
                return presets.Keys;
            }

            if (codec.EndsWith("_10bit", StringComparison.OrdinalIgnoreCase))
            {
                string baseCodec = codec[..^6];
                if (codecs.TryGetValue(baseCodec, out var basePresets))
                {
                    return basePresets.Keys;
                }
            }
        }
        return Array.Empty<string>();
    }

    public PresetInfo? GetPresetInfo(string platform, string codec, string preset)
    {
        if (Platforms.TryGetValue(platform, out var codecs))
        {
            if (codecs.TryGetValue(codec, out var presets) &&
                presets.TryGetValue(preset, out var info))
            {
                return info;
            }

            if (codec.EndsWith("_10bit", StringComparison.OrdinalIgnoreCase))
            {
                string baseCodec = codec[..^6];
                if (codecs.TryGetValue(baseCodec, out var basePresets) &&
                    basePresets.TryGetValue(preset, out var baseInfo))
                {
                    return baseInfo;
                }
            }
        }
        return null;
    }
}
