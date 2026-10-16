using System;
using System.Collections.Generic;

namespace FfmpegConverter.Core.Engine;

public static class MediaLanguageCodes
{
    private static readonly IReadOnlyDictionary<string, string> CanonicalCodes =
        new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
        {
            ["fra"] = "fra",
            ["fre"] = "fra"
        };

    public static bool AreEquivalent(string? expected, string? actual)
    {
        if (string.IsNullOrWhiteSpace(expected) || string.IsNullOrWhiteSpace(actual))
            return false;

        string expectedCode = CanonicalCodes.TryGetValue(expected, out string? expectedCanonical)
            ? expectedCanonical
            : expected;
        string actualCode = CanonicalCodes.TryGetValue(actual, out string? actualCanonical)
            ? actualCanonical
            : actual;
        return string.Equals(expectedCode, actualCode, StringComparison.OrdinalIgnoreCase);
    }

    public static bool IsFrenchAlias(string? code) =>
        string.Equals(code, "fra", StringComparison.OrdinalIgnoreCase) ||
        string.Equals(code, "fre", StringComparison.OrdinalIgnoreCase);
}
