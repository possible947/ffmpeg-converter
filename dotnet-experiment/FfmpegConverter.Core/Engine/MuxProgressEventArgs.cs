using System;

namespace FfmpegConverter.Core.Engine;

public enum MuxProgressStage
{
    PreparingSource,
    AssemblingContainer,
    FinalizingContainer,
    PreparingM4vComponents,
    PackagingM4v,
    PreservingMetadata,
    VerifyingOutput,
    Completed,
    Canceled
}

public sealed class MuxProgressEventArgs : EventArgs
{
    public MuxProgressStage Stage { get; }
    public long Revision { get; }
    public string FilePath { get; }
    public string Status { get; }
    public float? Percent { get; }
    public bool UsesFfmpegProgress { get; }

    public MuxProgressEventArgs(
        MuxProgressStage stage,
        long revision,
        string filePath,
        string status,
        float? percent = null,
        bool usesFfmpegProgress = false)
    {
        Stage = stage;
        Revision = revision;
        FilePath = filePath;
        Status = status;
        Percent = percent;
        UsesFfmpegProgress = usesFfmpegProgress;
    }
}
