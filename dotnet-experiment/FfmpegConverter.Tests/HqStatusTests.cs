using System.Text;
using FfmpegConverter.Core.Engine;
using FfmpegConverter.Core.Models;
using ErrorEventArgs = FfmpegConverter.Core.Engine.ErrorEventArgs;

namespace FfmpegConverter.Tests;

public class HqStatusTests
{
    [Fact]
    public void Preparing_PresentsStructuredMessageOnceWithoutInventingProgress()
    {
        var adapter = new HqAdapter();
        var stages = new List<string>();
        var messages = new List<string>();
        adapter.StageChanged += (_, stage) => stages.Add(stage);
        adapter.MessageLogged += (_, message) => messages.Add(message);
        adapter.ProgressAnalysis += (_, _) => Assert.Fail("Preparing has no analysis progress.");
        adapter.ProgressEncode += (_, _) => Assert.Fail("Preparing has no encode progress.");

        adapter.ForwardStatus("""{"stage":"preparing","message":"Preparing HQ session"}""");
        adapter.ForwardStatus("""{"stage":"preparing","message":"Preparing HQ session"}""");

        Assert.Equal(["preparing"], stages);
        Assert.Equal(["Preparing HQ session"], messages);
    }

    [Fact]
    public void Preparing_PreservesMeaningfulStatusChangesWithinTheSameStage()
    {
        var adapter = new HqAdapter();
        var messages = new List<string>();
        adapter.MessageLogged += (_, message) => messages.Add(message);

        adapter.ForwardStatus("""{"stage":"preparing","message":"Preparing HQ session"}""");
        adapter.ForwardStatus("""{"stage":"preparing","message":"Session ready"}""");

        Assert.Equal(["Preparing HQ session", "Session ready"], messages);
    }

    [Fact]
    public void Segmentator_AdvancesAnalysisProgressWithoutLoggingEachUpdate()
    {
        var adapter = new HqAdapter();
        var stages = new List<string>();
        var progress = new List<ProgressAnalysisEventArgs>();
        adapter.StageChanged += (_, stage) => stages.Add(stage);
        adapter.ProgressAnalysis += (_, args) => progress.Add(args);
        adapter.MessageLogged += (_, _) => Assert.Fail("Analysis updates are not console messages.");

        adapter.ForwardStatus("""{"stage":"analyzing","progress_percent":0,"fps":null,"remaining_time_sec":null,"message":"segmentator"}""");
        adapter.ForwardStatus("""{"stage":"analyzing","progress_percent":37.5,"fps":null,"remaining_time_sec":null,"message":"segmentator"}""");
        adapter.ForwardStatus("""{"stage":"analyzing","progress_percent":100,"fps":null,"remaining_time_sec":null,"message":"segmentator"}""");

        Assert.Equal(["analyzing"], stages);
        Assert.Equal([0f, 37.5f, 100f], progress.Select(args => args.Percent));
        Assert.All(progress, args => Assert.Equal(0, args.EtaSeconds));
    }

    [Fact]
    public void AnalysisToEncoding_ChangesStageBeforeForwardingStageLocalProgress()
    {
        var adapter = new HqAdapter();
        var events = new List<string>();
        adapter.StageChanged += (_, stage) => events.Add(stage);
        adapter.ProgressAnalysis += (_, args) =>
        {
            Assert.Equal(100, args.Percent);
            events.Add("analysis progress");
        };
        adapter.ProgressEncode += (_, args) =>
        {
            Assert.Equal(12.5f, args.Percent);
            Assert.Equal(42, args.Fps);
            Assert.Equal(9, args.EtaSeconds);
            events.Add("encode progress");
        };
        adapter.MessageLogged += (_, _) => Assert.Fail("Numeric progress is not console output.");

        adapter.ForwardStatus("""{"stage":"analyzing","progress_percent":100}""");
        adapter.ForwardStatus("""{"stage":"encoding","progress_percent":12.5,"fps":42,"remaining_time_sec":9,"message":"encoding"}""");

        Assert.Equal(["analyzing", "analysis progress", "encoding", "encode progress"], events);
    }

    [Fact]
    public async Task RawOutput_IsDrainedWithoutEnteringTheConsoleChannel()
    {
        var adapter = new HqAdapter();
        adapter.MessageLogged += (_, _) => Assert.Fail("Raw worker output must not be logged.");
        byte[] raw = Encoding.UTF8.GetBytes(new string('x', 256_000) + "\rframe=23\nsegmenter chatter\n");
        using var stream = new MemoryStream(raw);
        using var reader = new StreamReader(stream);

        await HqAdapter.DrainProcessOutputAsync(reader);

        Assert.Equal(stream.Length, stream.Position);
    }

    [Fact]
    public void Completed_PresentsCompletionWithoutAnEncodeProgressReset()
    {
        var adapter = new HqAdapter();
        var stages = new List<string>();
        var messages = new List<string>();
        adapter.StageChanged += (_, stage) => stages.Add(stage);
        adapter.MessageLogged += (_, message) => messages.Add(message);
        adapter.ProgressEncode += (_, _) => Assert.Fail("Completion is a stage, not another encoding tick.");

        adapter.ForwardStatus("""{"stage":"completed","progress_percent":100,"message":"HQ encoding complete"}""");

        Assert.Equal(["completed"], stages);
        Assert.Equal(["HQ encoding complete"], messages);
    }

    [Theory]
    [InlineData("failed", ConverterError.FfmpegFailed)]
    [InlineData("cancelled", ConverterError.SkipFile)]
    public void TerminalError_PreservesStructuredErrorAndDoesNotEmitProgress(
        string stage, ConverterError expectedCode)
    {
        var adapter = new HqAdapter();
        var stages = new List<string>();
        var errors = new List<ErrorEventArgs>();
        adapter.StageChanged += (_, value) => stages.Add(value);
        adapter.ErrorOccurred += (_, error) => errors.Add(error);
        adapter.ProgressAnalysis += (_, _) => Assert.Fail("Terminal status must not advance analysis.");
        adapter.ProgressEncode += (_, _) => Assert.Fail("Terminal status must not advance encoding.");
        adapter.MessageLogged += (_, _) => Assert.Fail("Errors use the error channel, not duplicate console output.");

        adapter.ForwardStatus(System.Text.Json.JsonSerializer.Serialize(new
        {
            stage, message = "Actual HQ reason", progress_percent = 40
        }));

        Assert.Equal([stage], stages);
        ErrorEventArgs error = Assert.Single(errors);
        Assert.Equal("Actual HQ reason", error.Text);
        Assert.Equal(expectedCode, error.Code);
    }

    [Fact]
    public void InvalidStructuredStatus_ReportsAnIntegrationDiagnostic()
    {
        var adapter = new HqAdapter();
        var messages = new List<string>();
        adapter.MessageLogged += (_, message) => messages.Add(message);

        adapter.ForwardStatus("{");

        Assert.Equal(["HQ worker returned an invalid status payload."], messages);
    }

    [Fact]
    public async Task AlreadyCancelledJob_DoesNotStartWorkerOrAdvanceProgress()
    {
        var adapter = new HqAdapter();
        adapter.ProgressAnalysis += (_, _) => Assert.Fail("Cancelled jobs must not advance analysis.");
        adapter.ProgressEncode += (_, _) => Assert.Fail("Cancelled jobs must not advance encoding.");
        adapter.MessageLogged += (_, _) => Assert.Fail("No worker should start.");

        var result = await adapter.RunJobAsync(
            "unused-runtime", "unused-input", "unused-work", "x264", "fullhd",
            new CancellationToken(canceled: true));

        Assert.Equal(1, result.Result);
        Assert.Equal("HQ job cancelled.", result.Error);
    }
}
