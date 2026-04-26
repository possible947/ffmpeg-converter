unit converter_runner;

{$mode objfpc}{$H+}

interface

uses converter_types;

function ProbeDuration(const InputFile, FallbackLogDir: string; out ErrorLogPath, ErrorLogNotice: string): Double;
function RunEncode(const CommandBase, InputFile, OutputFile, FallbackLogDir: string;
  OnProgressEncode: TOnProgressEncode; OnMessage: TOnMessage; StopFlag: PLongInt;
  out ErrorLogPath, ErrorLogNotice: string): TConverterError;

implementation

uses
  Classes,
  SysUtils,
  Process,
  DateUtils,
  process_utils,
  tool_paths,
  path_utils;

function InvariantFmt: TFormatSettings;
begin
  Result := DefaultFormatSettings;
  Result.DecimalSeparator := '.';
end;

procedure ParseProgressLine(
  const Line: string;
  DurationSec: Double;
  StartTickMs: QWord;
  var OutTimeMs: Double;
  var LastFps: Double;
  OnProgressEncode: TOnProgressEncode
);
var
  CurSec: Double;
  Percent: Double;
  ElapsedSec: Double;
  EtaSec: Double;
  Value: string;
  Fmt: TFormatSettings;
begin
  if Pos('out_time_ms=', Line) = 1 then
  begin
    Fmt := InvariantFmt;
    Value := Copy(Line, Length('out_time_ms=') + 1, MaxInt);
    OutTimeMs := StrToFloatDef(Trim(Value), OutTimeMs, Fmt);
  end
  else if Pos('fps=', Line) = 1 then
  begin
    Fmt := InvariantFmt;
    Value := Copy(Line, Length('fps=') + 1, MaxInt);
    LastFps := StrToFloatDef(Trim(Value), LastFps, Fmt);
  end
  else if (Pos('progress=', Line) = 1) and (Pos('end', Line) > 0) then
  begin
    if Assigned(OnProgressEncode) then
      OnProgressEncode(100.0, LastFps, 0.0);
    Exit;
  end;

  if (DurationSec > 0.0) and (OutTimeMs > 0.0) and Assigned(OnProgressEncode) then
  begin
    CurSec := OutTimeMs / 1000000.0;
    Percent := (CurSec / DurationSec) * 100.0;
    if Percent < 0.0 then
      Percent := 0.0
    else if Percent > 100.0 then
      Percent := 100.0;

    ElapsedSec := (GetTickCount64 - StartTickMs) / 1000.0;
    if Percent > 0.0 then
      EtaSec := ElapsedSec * (100.0 - Percent) / Percent
    else
      EtaSec := 0.0;

    OnProgressEncode(Percent, LastFps, EtaSec);
  end;
end;

procedure ParseProgressChunk(
  const Chunk: string;
  var Pending: string;
  var OutTimeMs: Double;
  var LastFps: Double;
  DurationSec: Double;
  StartTickMs: QWord;
  OnProgressEncode: TOnProgressEncode
);
var
  P: SizeInt;
  Line: string;
begin
  Pending += Chunk;
  repeat
    P := Pos(#10, Pending);
    if P <= 0 then
      Break;

    Line := Copy(Pending, 1, P - 1);
    if (Length(Line) > 0) and (Line[Length(Line)] = #13) then
      Delete(Line, Length(Line), 1);

    Delete(Pending, 1, P);
    ParseProgressLine(Line, DurationSec, StartTickMs, OutTimeMs, LastFps, OnProgressEncode);
  until False;
end;

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
    QuoteForShell(InputFile) +
{$IFDEF Windows}
    ' 2>NUL';
{$ELSE}
    ' 2>/dev/null';
{$ENDIF}
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
  OnProgressEncode: TOnProgressEncode; OnMessage: TOnMessage; StopFlag: PLongInt;
  out ErrorLogPath, ErrorLogNotice: string): TConverterError;
var
  Tools: TToolPaths;
  Cmd: string;
  EffectiveCmd: string;
  P: TProcess;
  ReadBuf: array[0..4095] of Byte;
  ReadCount: LongInt;
  Chunk: string;
  Pending: string;
  FullOutput: string;
  DurationErrLogPath: string;
  DurationErrNotice: string;
  DurationSec: Double;
  OutTimeMs: Double;
  LastFps: Double;
  StartTickMs: QWord;
  TerminatedByStop: Boolean;
  ExitCode: LongInt;
  LogInfo: TCommandErrorLog;
begin
  ErrorLogPath := '';
  ErrorLogNotice := '';

  Tools := ResolveToolPaths;
  DurationErrLogPath := '';
  DurationErrNotice := '';
  DurationSec := ProbeDuration(InputFile, FallbackLogDir, DurationErrLogPath, DurationErrNotice);

  Cmd := CommandBase;
{$IFDEF Windows}
  EffectiveCmd := Trim(Cmd);
  if (EffectiveCmd <> '') and (EffectiveCmd[1] = '"') then
    EffectiveCmd := '"' + EffectiveCmd + '"';
{$ELSE}
  EffectiveCmd := Cmd;
{$ENDIF}
  P := TProcess.Create(nil);
  Pending := '';
  FullOutput := '';
  OutTimeMs := 0.0;
  LastFps := 0.0;
  StartTickMs := GetTickCount64;
  TerminatedByStop := False;

  try
{$IFDEF Windows}
    P.Executable := 'cmd.exe';
    P.Parameters.Add('/c');
{$ELSE}
    P.Executable := '/bin/sh';
    P.Parameters.Add('-c');
{$ENDIF}
    P.Parameters.Add(EffectiveCmd);
    P.Options := [poUsePipes, poStderrToOutput];
    P.Execute;

    while P.Running or (P.Output.NumBytesAvailable > 0) do
    begin
      if Assigned(StopFlag) and (StopFlag^ <> 0) then
      begin
        TerminatedByStop := True;
        if P.Running then
          P.Terminate(15);
      end;

      if P.Output.NumBytesAvailable > 0 then
      begin
        ReadCount := P.Output.Read(ReadBuf{%H-}, SizeOf(ReadBuf));
        if ReadCount > 0 then
        begin
          SetString(Chunk, PAnsiChar(@ReadBuf[0]), ReadCount);
          FullOutput += Chunk;
          ParseProgressChunk(Chunk, Pending, OutTimeMs, LastFps, DurationSec, StartTickMs, OnProgressEncode);
        end;
      end
      else
        Sleep(20);
    end;

    if Pending <> '' then
      ParseProgressLine(Pending, DurationSec, StartTickMs, OutTimeMs, LastFps, OnProgressEncode);

    ExitCode := P.ExitStatus;
  finally
    P.Free;
  end;

  if TerminatedByStop then
    Exit(ERR_SKIP_FILE);

  if ExitCode <> 0 then
  begin
    FillChar(LogInfo, SizeOf(LogInfo), 0);
    LogInfo.CommandLine := Cmd;
    LogInfo.StdOutErr := FullOutput;
    LogInfo.ExitCode := ExitCode;
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

  if Assigned(OnMessage) then
    OnMessage('encoding finished');

  if Assigned(OnProgressEncode) then
    OnProgressEncode(100.0, LastFps, 0.0);

  if (DurationErrNotice <> '') and Assigned(OnMessage) then
  begin
    OnMessage(PAnsiChar(AnsiString('duration probe warning: ' + DurationErrNotice)));
  end;

  Result := ERR_OK;
end;

end.
