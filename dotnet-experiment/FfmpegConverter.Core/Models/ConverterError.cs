namespace FfmpegConverter.Core.Models;

public enum ConverterError
{
    Ok = 0,
    InputNotFound,
    InputNotRegular,
    InputNotReadable,
    OutputExists,
    SkipFile,
    PeakAnalysisFailed,
    LoudnormAnalysisFailed,
    FfmpegFailed,
    FfprobeFailed,
    PopenFailed,
    PcloseFailed,
    InvalidOptions,
    Unknown,
    PlatformInitFailed,
    AudioFilterValidationFailed,
    GpuNotSupported,
    PathTooLong,
    HomeDirNotFound
}
