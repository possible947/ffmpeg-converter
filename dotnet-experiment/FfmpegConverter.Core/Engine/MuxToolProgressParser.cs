using System.Globalization;

namespace FfmpegConverter.Core.Engine;

public static class MuxToolProgressParser
{
    public static bool TryParseMkvmergeProgress(string line, out float percent)
    {
        percent = 0;
        const string prefix = "Progress: ";
        if (!line.StartsWith(prefix, System.StringComparison.Ordinal) ||
            !line.EndsWith('%'))
            return false;

        string value = line[prefix.Length..^1];
        return float.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out percent) &&
               float.IsFinite(percent) &&
               percent is >= 0 and <= 100;
    }

    public static bool TryParseMp4BoxProgress(string line, out float percent)
    {
        percent = 0;
        const string prefix = "ISO File Writing:";
        if (!line.StartsWith(prefix, System.StringComparison.Ordinal))
            return false;

        string trimmed = line.TrimEnd();
        int openingParen = trimmed.LastIndexOf('(');
        if (openingParen < prefix.Length || !trimmed.EndsWith(')'))
            return false;

        string[] fraction = trimmed[(openingParen + 1)..^1].Split('/');
        if (fraction.Length != 2 ||
            !int.TryParse(fraction[0], NumberStyles.None, CultureInfo.InvariantCulture, out int current) ||
            !int.TryParse(fraction[1], NumberStyles.None, CultureInfo.InvariantCulture, out int total) ||
            total <= 0 ||
            current < 0 ||
            current > total)
            return false;

        percent = current * 100f / total;
        return true;
    }
}
