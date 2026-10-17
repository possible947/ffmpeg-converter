using FfmpegConverter.Core.Engine;
using FfmpegConverter.Core.Models;
using FfmpegConverter.Gui;

namespace FfmpegConverter.Tests;

public class ProgressDisplayStateTests
{
    [Fact]
    public void Legacy_UsesAnalysisAndEncodingProgressAndCompletesPerFile()
    {
        var display = NewRun(1, ProgressWorkflow.Legacy);
        display.BeginFile(1, 1, 1, "/input/legacy.mp4");

        display.UpdateStage(1, 1, "peak_analysis");
        ProgressDisplaySnapshot analysis =
            display.UpdateAnalysis(1, 1, new ProgressAnalysisEventArgs(28, 14))!;
        Assert.Equal(28, analysis.Percent);
        Assert.Contains("Stage: peak_analysis", analysis.Details);
        Assert.Contains("Analysis: 28.0%", analysis.Details);
        Assert.Contains("ETA:", analysis.Details);

        display.UpdateStage(1, 1, "encoding");
        ProgressDisplaySnapshot encoding =
            display.UpdateEncoding(1, 1, new ProgressEncodeEventArgs(51.5f, 24, 38))!;
        Assert.Equal(51.5, encoding.Percent);
        Assert.Contains("Stage: encoding", encoding.Details);
        Assert.Contains("Encoding: 51.5%", encoding.Details);
        Assert.Contains("FPS: 24", encoding.Details);
        Assert.Contains("ETA:", encoding.Details);

        display.RecordFileEnd(1, 1, ConverterError.Ok);
        Assert.Equal(ConverterError.Ok, display.RunResult);
        Assert.Equal(ConverterError.Ok, display.CurrentFileResult);
        display.FinishRun(1);
        Assert.Null(display.UpdateEncoding(1, 1, new ProgressEncodeEventArgs(100, 24, 0)));
    }

    [Fact]
    public void Hq_UsesStageCheckpointAndNeverShowsLegacySpeedOrEta()
    {
        var display = NewRun(1, ProgressWorkflow.Hq);
        display.BeginFile(1, 1, 1, "/input/hq.mp4");
        display.UpdateStage(1, 1, "analyzing");

        ProgressDisplaySnapshot checkpoint =
            display.UpdateAnalysis(1, 1, new ProgressAnalysisEventArgs(37.5f, 99))!;
        Assert.Equal("Stage: analyzing", checkpoint.Status);
        Assert.Contains("Checkpoint: 37.5%", checkpoint.Details);
        Assert.DoesNotContain("ETA", checkpoint.Details);
        Assert.DoesNotContain("FPS", checkpoint.Details);

        display.UpdateStage(1, 1, "encoding");
        ProgressDisplaySnapshot encoding =
            display.UpdateEncoding(1, 1, new ProgressEncodeEventArgs(52, 300, 18))!;
        Assert.Contains("Stage: encoding", encoding.Details);
        Assert.DoesNotContain("FPS", encoding.Details);
        Assert.DoesNotContain("ETA", encoding.Details);

        ProgressDisplaySnapshot completed = display.UpdateStage(1, 1, "completed")!;
        Assert.Equal(100, completed.Percent);
        Assert.Equal("Stage: completed", completed.Status);
    }

    [Fact]
    public void HqThenLegacy_UsesFreshLegacyContract()
    {
        var hq = NewRun(1, ProgressWorkflow.Hq);
        hq.BeginFile(1, 1, 1, "hq.mp4");
        hq.UpdateStage(1, 1, "completed");
        hq.RecordFileEnd(1, 1, ConverterError.Ok);
        hq.FinishRun(1);

        var legacy = NewRun(2, ProgressWorkflow.Legacy);
        ProgressDisplaySnapshot start = legacy.BeginFile(2, 1, 1, "legacy.mp4")!;
        Assert.Equal("Processing [1/1]: legacy.mp4", start.Status);
        Assert.Equal("", start.Details);
        Assert.Equal(0, start.Percent);

        ProgressDisplaySnapshot progress =
            legacy.UpdateEncoding(2, 1, new ProgressEncodeEventArgs(22, 30, 44))!;
        Assert.Contains("FPS: 30", progress.Details);
        Assert.Contains("ETA:", progress.Details);
        Assert.DoesNotContain("Stage: completed", progress.Details);
    }

    [Fact]
    public void LegacyThenHq_UsesFreshHqContract()
    {
        var legacy = NewRun(1, ProgressWorkflow.Legacy);
        legacy.BeginFile(1, 1, 1, "legacy.mp4");
        legacy.UpdateEncoding(1, 1, new ProgressEncodeEventArgs(100, 30, 0));
        legacy.RecordFileEnd(1, 1, ConverterError.Ok);
        legacy.FinishRun(1);

        var hq = NewRun(2, ProgressWorkflow.Hq);
        hq.BeginFile(2, 1, 1, "hq.mp4");
        ProgressDisplaySnapshot stage = hq.UpdateStage(2, 1, "analyzing")!;
        Assert.Equal("Stage: analyzing", stage.Status);
        Assert.Equal(0, stage.Percent);

        ProgressDisplaySnapshot progress =
            hq.UpdateAnalysis(2, 1, new ProgressAnalysisEventArgs(8, 50))!;
        Assert.Contains("Checkpoint: 8.0%", progress.Details);
        Assert.DoesNotContain("FPS", progress.Details);
        Assert.DoesNotContain("ETA", progress.Details);
    }

    [Fact]
    public void HqMuxSourcePreparation_KeepsItsRealFfmpegSpeedAndEta()
    {
        var display = NewRun(1, ProgressWorkflow.Hq);
        display.BeginFile(1, 1, 1, "hq.mp4");
        display.UpdateStage(1, 1, "completed");
        display.UpdateStage(1, 1, "encoding intermediate");

        ProgressDisplaySnapshot sourceProgress =
            display.UpdateEncoding(1, 1, new ProgressEncodeEventArgs(64, 18, 27))!;
        Assert.Contains("Encoding: 64.0%", sourceProgress.Details);
        Assert.Contains("FPS: 18", sourceProgress.Details);
        Assert.Contains("ETA:", sourceProgress.Details);
    }

    [Fact]
    public void NextLegacyFile_ResetsStatusPercentSpeedEtaAndResult()
    {
        var display = NewRun(1, ProgressWorkflow.Legacy);
        display.BeginFile(1, 1, 2, "valid-1.mp4");
        display.UpdateEncoding(1, 1, new ProgressEncodeEventArgs(73, 25, 39));
        display.RecordFileEnd(1, 1, ConverterError.Ok);

        ProgressDisplaySnapshot next = display.BeginFile(1, 2, 2, "valid-2.mp4")!;
        Assert.Equal("Processing [2/2]: valid-2.mp4", next.Status);
        Assert.Equal("", next.Details);
        Assert.Equal(0, next.Percent);
        Assert.Null(next.FileResult);
        Assert.Null(display.UpdateEncoding(1, 1, new ProgressEncodeEventArgs(100, 25, 0)));
    }

    [Fact]
    public void ErrorThenSuccess_KeepsFailureInRunResultAndStartsNextFileClean()
    {
        var display = NewRun(1, ProgressWorkflow.Legacy);
        display.BeginFile(1, 1, 2, "corrupt.mp4");
        display.UpdateError(1, 1, "Decode failed");
        display.RecordFileEnd(1, 1, ConverterError.FfmpegFailed);

        ProgressDisplaySnapshot next = display.BeginFile(1, 2, 2, "valid.mp4")!;
        Assert.Equal(ConverterError.FfmpegFailed, display.RunResult);
        Assert.Null(next.FileResult);
        Assert.Equal("Processing [2/2]: valid.mp4", next.Status);
        Assert.Equal("", next.Details);
        Assert.Equal(0, next.Percent);

        display.RecordFileEnd(1, 2, ConverterError.Ok);
        Assert.Equal(ConverterError.Ok, display.CurrentFileResult);
        Assert.Equal(ConverterError.FfmpegFailed, display.RunResult);
    }

    [Fact]
    public void SuccessThenError_RecordsActualLaterFailure()
    {
        var display = NewRun(1, ProgressWorkflow.Legacy);
        display.BeginFile(1, 1, 2, "valid.mp4");
        display.RecordFileEnd(1, 1, ConverterError.Ok);
        display.BeginFile(1, 2, 2, "corrupt.mp4");
        ProgressDisplaySnapshot error = display.UpdateError(1, 2, "Invalid data found")!;
        display.RecordFileEnd(1, 2, ConverterError.FfmpegFailed);

        Assert.Equal("Invalid data found", error.Status);
        Assert.Equal(ConverterError.FfmpegFailed, display.RunResult);
        Assert.Equal(ConverterError.FfmpegFailed, display.CurrentFileResult);
    }

    [Fact]
    public void DelayedUpdates_AreRejectedAcrossFilesAndRuns()
    {
        var firstRun = NewRun(1, ProgressWorkflow.Hq);
        firstRun.BeginFile(1, 1, 2, "hq.mp4");
        ProgressDisplaySnapshot delayedHq =
            firstRun.UpdateAnalysis(1, 1, new ProgressAnalysisEventArgs(50, 0))!;
        firstRun.BeginFile(1, 2, 2, "next-hq.mp4");
        Assert.False(firstRun.IsLatest(delayedHq));

        firstRun.FinishRun(1);
        var nextRun = NewRun(2, ProgressWorkflow.Legacy);
        ProgressDisplaySnapshot nextLegacy = nextRun.BeginFile(2, 1, 1, "legacy.mp4")!;
        Assert.Null(firstRun.UpdateAnalysis(1, 2, new ProgressAnalysisEventArgs(99, 0)));
        Assert.Equal(0, nextLegacy.Percent);
    }

    private static ProgressDisplayState NewRun(long id, ProgressWorkflow workflow)
        => new(id, workflow);
}
