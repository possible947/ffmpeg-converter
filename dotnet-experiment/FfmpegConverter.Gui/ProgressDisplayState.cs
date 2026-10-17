using System;
using System.IO;
using FfmpegConverter.Core.Engine;
using FfmpegConverter.Core.Models;

namespace FfmpegConverter.Gui;

internal enum ProgressWorkflow
{
    Legacy,
    Hq
}

internal sealed record ProgressDisplaySnapshot(
    long RunId,
    int FileIndex,
    int Revision,
    string Status,
    string Details,
    double Percent,
    ConverterError? FileResult);

internal sealed class ProgressDisplayState
{
    private readonly object _gate = new();
    private readonly long _runId;
    private readonly ProgressWorkflow _workflow;
    private bool _active = true;
    private int _fileIndex;
    private int _revision;
    private string _stage = "";
    private string _status = "";
    private string _details = "";
    private double _percent;
    private ConverterError? _fileResult;
    private ConverterError _runResult = ConverterError.Ok;

    public ProgressDisplayState(long runId, ProgressWorkflow workflow)
    {
        _runId = runId;
        _workflow = workflow;
    }

    public ConverterError RunResult
    {
        get
        {
            lock (_gate)
                return _runResult;
        }
    }

    public ConverterError? CurrentFileResult
    {
        get
        {
            lock (_gate)
                return _fileResult;
        }
    }

    public ProgressDisplaySnapshot? BeginFile(
        long runId, int fileIndex, int total, string fileName)
    {
        lock (_gate)
        {
            if (!IsActiveRun(runId))
                return null;

            _fileIndex = fileIndex;
            _stage = "";
            _status = $"Processing [{fileIndex}/{total}]: {Path.GetFileName(fileName)}";
            _details = "";
            _percent = 0;
            _fileResult = null;
            _revision++;
            return Snapshot();
        }
    }

    public ProgressDisplaySnapshot? UpdateStage(long runId, int fileIndex, string stage)
    {
        lock (_gate)
        {
            if (!IsCurrentFile(runId, fileIndex))
                return null;

            _stage = stage;
            _details = $"Stage: {stage}";
            if (_workflow == ProgressWorkflow.Hq)
            {
                _status = $"Stage: {stage}";
                if (stage is "preparing" or "analyzing" or "encoding")
                    _percent = 0;
                else if (stage == "completed")
                    _percent = 100;
            }
            _revision++;
            return Snapshot();
        }
    }

    public ProgressDisplaySnapshot? UpdateAnalysis(
        long runId, int fileIndex, ProgressAnalysisEventArgs progress)
    {
        lock (_gate)
        {
            if (!IsCurrentFile(runId, fileIndex))
                return null;

            _percent = progress.Percent;
            if (_workflow == ProgressWorkflow.Hq && _stage == "analyzing")
            {
                _details = $"Stage: analyzing | Checkpoint: {progress.Percent:F1}%";
            }
            else
            {
                string eta = FormatEta(progress.EtaSeconds);
                string stage = string.IsNullOrEmpty(_stage) ? "analysis" : _stage;
                _details = $"Stage: {stage} | Analysis: {progress.Percent:F1}% | {eta}";
            }
            _revision++;
            return Snapshot();
        }
    }

    public ProgressDisplaySnapshot? UpdateEncoding(
        long runId, int fileIndex, ProgressEncodeEventArgs progress)
    {
        lock (_gate)
        {
            if (!IsCurrentFile(runId, fileIndex))
                return null;

            _percent = progress.Percent;
            if (_workflow == ProgressWorkflow.Hq && _stage == "encoding")
            {
                _details = $"Stage: encoding | {progress.Percent:F1}%";
            }
            else
            {
                string eta = FormatEta(progress.EtaSeconds);
                string stage = string.IsNullOrEmpty(_stage) ? "encoding" : _stage;
                _details = $"Stage: {stage} | Encoding: {progress.Percent:F1}% | FPS: {progress.Fps:F0} | {eta}";
            }
            _revision++;
            return Snapshot();
        }
    }

    public ProgressDisplaySnapshot? UpdateError(
        long runId, int fileIndex, string message)
    {
        lock (_gate)
        {
            if (!IsCurrentFile(runId, fileIndex))
                return null;

            _status = message;
            _details = "Finished with errors";
            _revision++;
            return Snapshot();
        }
    }

    public void RecordFileEnd(long runId, int fileIndex, ConverterError result)
    {
        lock (_gate)
        {
            if (!IsCurrentFile(runId, fileIndex))
                return;

            _fileResult = result;
            if (result != ConverterError.Ok && _runResult == ConverterError.Ok)
                _runResult = result;
            _revision++;
        }
    }

    public bool IsLatest(ProgressDisplaySnapshot snapshot)
    {
        lock (_gate)
        {
            return _active &&
                   snapshot.RunId == _runId &&
                   snapshot.FileIndex == _fileIndex &&
                   snapshot.Revision == _revision;
        }
    }

    public void FinishRun(long runId)
    {
        lock (_gate)
        {
            if (runId == _runId)
                _active = false;
        }
    }

    private bool IsActiveRun(long runId) => _active && runId == _runId;

    private bool IsCurrentFile(long runId, int fileIndex)
        => IsActiveRun(runId) && fileIndex == _fileIndex;

    private ProgressDisplaySnapshot Snapshot()
        => new(_runId, _fileIndex, _revision, _status, _details, _percent, _fileResult);

    private static string FormatEta(float etaSeconds)
        => etaSeconds > 0
            ? $"ETA: {TimeSpan.FromSeconds(etaSeconds):hh\\:mm\\:ss}"
            : "ETA: --:--:--";
}
