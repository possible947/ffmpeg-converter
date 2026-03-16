unit converter_runner;

{$mode objfpc}{$H+}

interface

uses converter_types;

function ProbeDuration(const InputFile, FallbackLogDir: string; out ErrorLogPath, ErrorLogNotice: string): Double;
function RunEncode(const CommandBase, InputFile, OutputFile, FallbackLogDir: string;
  out ErrorLogPath, ErrorLogNotice: string): TConverterError;

implementation

uses
  SysUtils,
  process_utils,
  tool_paths,
  path_utils;

function ProbeDuration(const InputFile, FallbackLogDir: string; out ErrorLogPath, ErrorLogNotice: string): Double;
var
  Tools: TToolPaths;
  R: TRunResult;
  Cmd: string;
  LogInfo: TCommandErrorLog;
  Code: Integer;
begin
  ErrorLogPath := '';
  ErrorLogNotice := '';

  Tools := ResolveToolPaths;
  Cmd :=
    QuoteForShell(Tools.FfprobeBin) +
    ' -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 ' +
    QuoteForShell(InputFile) + ' 2>/dev/null';
  R := RunCommandCapture(Cmd);

  if R.ExitCode <> 0 then
  begin
    FillChar(LogInfo, SizeOf(LogInfo), 0);
    LogInfo.CommandLine := Cmd;
    LogInfo.StdOutErr := R.OutputText;
    LogInfo.ExitCode := R.ExitCode;
    LogInfo.FfmpegBin := Tools.FfmpegBin;
    LogInfo.FfprobeBin := Tools.FfprobeBin;
    LogInfo.InputFile := InputFile;
    LogInfo.WorkingDir := GetCurrentDir;
    LogInfo.PathValue := Tools.PathValue;
    LogInfo.ContextNote := 'ffprobe duration probe failed';
    if not WriteCommandErrorLog(LogInfo, ProgramDirectory, FallbackLogDir, ErrorLogPath, ErrorLogNotice) then
      ErrorLogPath := '';
    Exit(0.0);
  end;

  Val(Trim(R.OutputText), Result, Code);
  if Code <> 0 then
    Result := 0.0;
end;

function RunEncode(const CommandBase, InputFile, OutputFile, FallbackLogDir: string;
  out ErrorLogPath, ErrorLogNotice: string): TConverterError;
var
  Tools: TToolPaths;
  Cmd: string;
  R: TRunResult;
  LogInfo: TCommandErrorLog;
begin
  ErrorLogPath := '';
  ErrorLogNotice := '';

  Tools := ResolveToolPaths;
  Cmd := CommandBase + ' -progress pipe:1 -nostats -nostdin 2>&1';
  R := RunCommandCapture(Cmd);

  if R.ExitCode <> 0 then
  begin
    FillChar(LogInfo, SizeOf(LogInfo), 0);
    LogInfo.CommandLine := Cmd;
    LogInfo.StdOutErr := R.OutputText;
    LogInfo.ExitCode := R.ExitCode;
    LogInfo.FfmpegBin := Tools.FfmpegBin;
    LogInfo.FfprobeBin := Tools.FfprobeBin;
    LogInfo.InputFile := InputFile;
    LogInfo.OutputFile := OutputFile;
    LogInfo.WorkingDir := GetCurrentDir;
    LogInfo.PathValue := Tools.PathValue;
    LogInfo.ContextNote := 'ffmpeg encoding failed';
    if not WriteCommandErrorLog(LogInfo, ProgramDirectory, FallbackLogDir, ErrorLogPath, ErrorLogNotice) then
      ErrorLogPath := '';
    Exit(ERR_FFMPEG_FAILED);
  end;

  Result := ERR_OK;
end;

end.
