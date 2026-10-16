using System.Threading;
using System.Threading.Tasks;

namespace FfmpegConverter.Core.Engine;

public interface IReplacementVideoProbe
{
    Task<InputVideoProbeResult> ProbeAsync(
        string ffprobePath,
        string replacementPath,
        CancellationToken cancellationToken);
}

public sealed class ReplacementVideoProbe : IReplacementVideoProbe
{
    public Task<InputVideoProbeResult> ProbeAsync(
        string ffprobePath,
        string replacementPath,
        CancellationToken cancellationToken) =>
        InputVideoInfo.ProbeDetailedAsync(ffprobePath, replacementPath, cancellationToken);
}
