using System;
using System.Collections.Generic;

namespace FfmpegConverter.Core.Presets;

public class SelectionEncoder
{
    public string Name { get; set; } = "";
    public bool Enabled { get; set; } = true;
    public string? Kind { get; set; }
    public string? FinalCodec { get; set; }
    public string? ExecutionCodec { get; set; }
    public List<string> Requires { get; set; } = new();
    public List<int> BitDepths { get; set; } = new();
    public string? PixelFormat { get; set; }
    public string? ProfileArgs { get; set; }
    public List<string> Presets { get; set; } = new();
    public List<string> Containers { get; set; } = new();
}

public class SelectionGroup
{
    public string Name { get; set; } = "";
    public bool Enabled { get; set; } = true;
    public string? Kind { get; set; }
    public string? FeatureGate { get; set; }
    public Dictionary<string, SelectionEncoder> Encoders { get; set; } = new(StringComparer.OrdinalIgnoreCase);
}

public class PlatformSelection
{
    public bool HwaccelEnabled { get; set; } = true;
    public Dictionary<string, SelectionGroup> Groups { get; set; } = new(StringComparer.OrdinalIgnoreCase);
}

public class SelectionCatalog
{
    public Dictionary<string, SelectionGroup> Common { get; } = new(StringComparer.OrdinalIgnoreCase);
    public Dictionary<string, PlatformSelection> Platforms { get; } = new(StringComparer.OrdinalIgnoreCase);
}
