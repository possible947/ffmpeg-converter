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
        Assert.False(firstRun.TryAcceptForDisplay(delayedHq));

        firstRun.FinishRun(1);
        var nextRun = NewRun(2, ProgressWorkflow.Legacy);
        ProgressDisplaySnapshot nextLegacy = nextRun.BeginFile(2, 1, 1, "legacy.mp4")!;
        Assert.Null(firstRun.UpdateAnalysis(1, 2, new ProgressAnalysisEventArgs(99, 0)));
        Assert.Equal(0, nextLegacy.Percent);
    }

    [Fact]
    public void QueuedMuxStages_RemainDisplayableInOrderAfterFileEnd()
    {
        var display = NewRun(1, ProgressWorkflow.Mux);
        display.BeginFile(1, 1, 1, "input.mkv");
        ProgressDisplaySnapshot assembly = display.UpdateMuxProgress(
            1, 1, Mux(1, MuxProgressStage.AssemblingContainer, "Assembling intermediate MKV...", 4))!;
        ProgressDisplaySnapshot verification = display.UpdateMuxProgress(
            1, 1, Mux(2, MuxProgressStage.VerifyingOutput, "Verifying final output..."))!;
        ProgressDisplaySnapshot complete = display.UpdateMuxProgress(
            1, 1, Mux(3, MuxProgressStage.Completed, "Complete", 100))!;
        display.RecordFileEnd(1, 1, ConverterError.Ok);

        Assert.True(display.TryAcceptForDisplay(assembly));
        Assert.True(display.TryAcceptForDisplay(verification));
        Assert.True(display.TryAcceptForDisplay(complete));
        Assert.False(display.TryAcceptForDisplay(assembly));
    }

    [Fact]
    public void DisplayedNewerMuxStage_RejectsOlderQueuedSnapshot()
    {
        var display = NewRun(1, ProgressWorkflow.Mux);
        display.BeginFile(1, 1, 1, "input.mkv");
        ProgressDisplaySnapshot assembly = display.UpdateMuxProgress(
            1, 1, Mux(1, MuxProgressStage.AssemblingContainer, "Assembling intermediate MKV...", 4))!;
        ProgressDisplaySnapshot verification = display.UpdateMuxProgress(
            1, 1, Mux(2, MuxProgressStage.VerifyingOutput, "Verifying final output..."))!;

        Assert.True(display.TryAcceptForDisplay(verification));
        Assert.False(display.TryAcceptForDisplay(assembly));
        display.FinishRun(1);
        Assert.False(display.TryAcceptForDisplay(verification));
    }

    [Fact]
    public void MuxMkv_TransitionsFromFfmpegPreparationThroughVerificationToComplete()
    {
        var display = NewRun(1, ProgressWorkflow.Mux);
        display.BeginFile(1, 1, 1, "input.mkv");
        display.UpdateMuxProgress(1, 1, Mux(1, MuxProgressStage.PreparingSource, "Preparing source...", ffmpeg: true));
        display.UpdateEncoding(1, 1, new ProgressEncodeEventArgs(72, 25, 14));

        ProgressDisplaySnapshot assembly = display.UpdateMuxProgress(
            1, 1, Mux(2, MuxProgressStage.AssemblingContainer, "Assembling intermediate MKV..."))!;
        Assert.Equal("Assembling intermediate MKV...", assembly.Status);
        Assert.Equal(0, assembly.Percent);
        Assert.DoesNotContain("FPS", assembly.Details);
        Assert.DoesNotContain("ETA", assembly.Details);

        Assert.Equal(
            "Verifying assembled MKV...",
            display.UpdateMuxProgress(1, 1, Mux(3, MuxProgressStage.VerifyingOutput, "Verifying assembled MKV..."))!.Status);
        ProgressDisplaySnapshot complete =
            display.UpdateMuxProgress(1, 1, Mux(4, MuxProgressStage.Completed, "Complete", 100))!;
        Assert.Equal("Complete", complete.Status);
        Assert.Equal(100, complete.Percent);
    }

    [Fact]
    public void MuxMov_DistinguishesIntermediateAssemblyAndMovFinalization()
    {
        var display = NewRun(1, ProgressWorkflow.Mux);
        display.BeginFile(1, 1, 1, "input.mov");
        display.UpdateMuxProgress(1, 1, Mux(1, MuxProgressStage.PreparingSource, "Preparing source...", ffmpeg: true, filePath: "input.mov"));
        display.UpdateEncoding(1, 1, new ProgressEncodeEventArgs(100, 30, 0));

        Assert.Equal(
            "Assembling intermediate MKV...",
            display.UpdateMuxProgress(1, 1, Mux(2, MuxProgressStage.AssemblingContainer, "Assembling intermediate MKV...", filePath: "input.mov"))!.Status);
        Assert.Equal(
            "Finalizing MOV...",
            display.UpdateMuxProgress(1, 1, Mux(3, MuxProgressStage.FinalizingContainer, "Finalizing MOV...", filePath: "input.mov"))!.Status);
        Assert.Equal(
            "Verifying final output...",
            display.UpdateMuxProgress(1, 1, Mux(4, MuxProgressStage.VerifyingOutput, "Verifying final output...", filePath: "input.mov"))!.Status);
        Assert.Equal(
            "Complete",
            display.UpdateMuxProgress(1, 1, Mux(5, MuxProgressStage.Completed, "Complete", 100, filePath: "input.mov"))!.Status);
    }

    [Fact]
    public void MuxM4v_SeparatesComponentsMp4BoxPreservationAndFinalVerification()
    {
        var display = NewRun(1, ProgressWorkflow.Mux);
        display.BeginFile(1, 1, 1, "input.mkv");

        string[] expected =
        [
            "Assembling intermediate MKV...",
            "Preparing M4V components...",
            "Creating M4V with MP4Box...",
            "Preserving M4V metadata and subtitles...",
            "Verifying final M4V...",
            "Complete"
        ];
        MuxProgressStage[] stages =
        [
            MuxProgressStage.AssemblingContainer,
            MuxProgressStage.PreparingM4vComponents,
            MuxProgressStage.PackagingM4v,
            MuxProgressStage.PreservingMetadata,
            MuxProgressStage.VerifyingOutput,
            MuxProgressStage.Completed
        ];

        for (int i = 0; i < expected.Length; i++)
        {
            ProgressDisplaySnapshot snapshot = display.UpdateMuxProgress(
                1, 1, Mux(i + 1, stages[i], expected[i], i == expected.Length - 1 ? 100 : null, filePath: "input.mkv"))!;
            Assert.Equal(expected[i], snapshot.Status);
        }
    }

    [Fact]
    public void MuxStageSwitch_ClearsFfmpegSpeedEtaAndPreviousPercentage()
    {
        var display = NewRun(1, ProgressWorkflow.Mux);
        display.BeginFile(1, 1, 1, "input.mkv");
        display.UpdateMuxProgress(1, 1, Mux(1, MuxProgressStage.PreparingSource, "Preparing source...", ffmpeg: true));
        display.UpdateEncoding(1, 1, new ProgressEncodeEventArgs(68, 32, 18));

        ProgressDisplaySnapshot assembly = display.UpdateMuxProgress(
            1, 1, Mux(2, MuxProgressStage.AssemblingContainer, "Assembling MKV..."))!;
        Assert.Equal("Assembling MKV...", assembly.Status);
        Assert.Equal(0, assembly.Percent);
        Assert.DoesNotContain("FPS", assembly.Details);
        Assert.DoesNotContain("ETA", assembly.Details);
    }

    [Fact]
    public void HqToMux_DoesNotLeakHqCheckpointFieldsIntoMuxStage()
    {
        var display = NewRun(1, ProgressWorkflow.Hq);
        display.BeginFile(1, 1, 1, "input.mkv");
        display.UpdateStage(1, 1, "analyzing");
        display.UpdateAnalysis(1, 1, new ProgressAnalysisEventArgs(45, 0));

        ProgressDisplaySnapshot muxStage = display.UpdateMuxProgress(
            1, 1, Mux(1, MuxProgressStage.AssemblingContainer, "Assembling intermediate MKV..."))!;
        Assert.Equal("Assembling intermediate MKV...", muxStage.Status);
        Assert.DoesNotContain("Checkpoint", muxStage.Details);
        Assert.DoesNotContain("FPS", muxStage.Details);
        Assert.DoesNotContain("ETA", muxStage.Details);
    }

    [Fact]
    public void MuxStageLocalPercentageMayResetWhenStageChanges()
    {
        var display = NewRun(1, ProgressWorkflow.Mux);
        display.BeginFile(1, 1, 1, "input.m4v");
        display.UpdateMuxProgress(1, 1, Mux(1, MuxProgressStage.AssemblingContainer, "Assembling MKV...", 100, filePath: "input.m4v"));

        ProgressDisplaySnapshot nextStage = display.UpdateMuxProgress(
            1, 1, Mux(2, MuxProgressStage.PackagingM4v, "Creating M4V with MP4Box...", 10, filePath: "input.m4v"))!;
        Assert.Equal("Creating M4V with MP4Box...", nextStage.Status);
        Assert.Equal(10, nextStage.Percent);
    }

    [Fact]
    public void MuxFailure_RetainsActiveStageAndRealErrorWithoutCompleting()
    {
        var display = NewRun(1, ProgressWorkflow.Mux);
        display.BeginFile(1, 1, 1, "input.m4v");
        display.UpdateMuxProgress(
            1, 1, Mux(1, MuxProgressStage.PackagingM4v, "Creating M4V with MP4Box...", filePath: "input.m4v"));

        ProgressDisplaySnapshot failure = display.UpdateError(1, 1, "MP4Box returned error")!;
        Assert.Equal("MP4Box returned error", failure.Status);
        Assert.Contains("Creating M4V with MP4Box", failure.Details);
        Assert.Contains("MP4Box returned error", failure.Details);
        Assert.NotEqual(100, failure.Percent);
        Assert.Null(display.UpdateMuxProgress(
            1, 1, Mux(2, MuxProgressStage.Completed, "Complete", 100, filePath: "input.m4v")));
    }

    [Fact]
    public void MuxCancellation_IsTerminalAndRejectsLateCompletion()
    {
        var display = NewRun(1, ProgressWorkflow.Mux);
        display.BeginFile(1, 1, 1, "input.m4v");
        display.UpdateMuxProgress(
            1, 1, Mux(1, MuxProgressStage.PackagingM4v, "Creating M4V with MP4Box...", 45, filePath: "input.m4v"));

        ProgressDisplaySnapshot canceled = display.UpdateMuxProgress(
            1, 1, Mux(2, MuxProgressStage.Canceled, "Mux canceled", filePath: "input.m4v"))!;
        Assert.Equal("Mux canceled", canceled.Status);
        Assert.NotEqual(100, canceled.Percent);
        Assert.Null(display.UpdateMuxProgress(
            1, 1, Mux(3, MuxProgressStage.Completed, "Complete", 100, filePath: "input.m4v")));
    }

    [Fact]
    public void StaleMuxStageProgress_IsRejectedAfterLaterStageBegins()
    {
        var display = NewRun(1, ProgressWorkflow.Mux);
        display.BeginFile(1, 1, 1, "input.m4v");
        display.UpdateMuxProgress(
            1, 1, Mux(1, MuxProgressStage.AssemblingContainer, "Assembling MKV...", 100, filePath: "input.m4v"));
        ProgressDisplaySnapshot current = display.UpdateMuxProgress(
            1, 1, Mux(2, MuxProgressStage.PackagingM4v, "Creating M4V with MP4Box...", 10, filePath: "input.m4v"))!;

        Assert.Null(display.UpdateMuxProgress(
            1, 1, Mux(1, MuxProgressStage.AssemblingContainer, "Assembling MKV...", 99, filePath: "input.m4v")));
        Assert.Equal("Creating M4V with MP4Box...", current.Status);
    }

    private static MuxProgressEventArgs Mux(
        long revision,
        MuxProgressStage stage,
        string status,
        float? percent = null,
        bool ffmpeg = false,
        string filePath = "input.mkv") =>
        new(stage, revision, filePath, status, percent, ffmpeg);

    private static ProgressDisplayState NewRun(long id, ProgressWorkflow workflow)
        => new(id, workflow);
}
