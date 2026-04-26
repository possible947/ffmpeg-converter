unit converter_analysis;

{$mode objfpc}{$H+}

interface

uses converter_types;

function RunPeakTwoPass(const InputFile, FallbackLogDir: string;
  out ErrorLogPath, ErrorLogNotice: string; out Gain: Double;
  OnProgress: TOnProgressAnalysis = nil): TConverterError;
function RunLoudnormTwoPass(const InputFile, FallbackLogDir: string;
  out ErrorLogPath, ErrorLogNotice: string; var Opts: TConvertOptions;
  OnProgress: TOnProgressAnalysis = nil): TConverterError;
function ExtractNumberAfterToken(const Text, Token: string; out V: Double): Boolean;
function ExtractLoudnormJson(const OutputText: string; out JsonText: string): Boolean;

implementation

uses
  Classes,
  SysUtils,
  Math,
  StrUtils,
  Process,
  process_utils,
  path_utils,
  tool_paths,
  loudnorm_json;

function InvariantFmt: TFormatSettings;
begin
  Result := DefaultFormatSettings;
  Result.DecimalSeparator := '.';
end;

function ProbeAnalysisDuration(const InputFile: string): Double;
var
  Tools: TToolPaths;
  Cmd: string;
  R: TRunResult;
  Fmt: TFormatSettings;
begin
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
    Exit(0.0);

  Fmt := InvariantFmt;
  if not TryStrToFloat(Trim(R.OutputText), Result, Fmt) then
    Result := 0.0;
end;

procedure ParseAnalysisProgressChunk(
  const Chunk: string;
  var Pending: string;
  var OutTimeMs: Double
);
var
  P: SizeInt;
  Line: string;
  Fmt: TFormatSettings;
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

    if Pos('out_time_ms=', Line) = 1 then
    begin
      Fmt := InvariantFmt;
      OutTimeMs := StrToFloatDef(
        Trim(Copy(Line, Length('out_time_ms=') + 1, MaxInt)),
        OutTimeMs,
        Fmt
      );
    end;
  until False;
end;

function RunAnalysisWithProgress(const CommandLine, InputFile: string;
  OnProgress: TOnProgressAnalysis): TRunResult;
var
  P: TProcess;
  EffectiveCmd: string;
  ReadBuf: array[0..4095] of Byte;
  ReadCount: LongInt;
  Chunk: string;
  Pending: string;
  DurationSec: Double;
  OutTimeMs: Double;
  StartTickMs: QWord;
  LastEmitTickMs: QWord;
  NowTickMs: QWord;
  Percent: Double;
  CurSec: Double;
  ElapsedSec: Double;
  EtaSec: Double;
begin
  Result.ExitCode := -1;
  Result.OutputText := '';

  DurationSec := ProbeAnalysisDuration(InputFile);
  OutTimeMs := 0.0;
  Pending := '';
  StartTickMs := GetTickCount64;
  LastEmitTickMs := 0;

  P := TProcess.Create(nil);
  try
{$IFDEF Windows}
    EffectiveCmd := Trim(CommandLine);
    if (EffectiveCmd <> '') and (EffectiveCmd[1] = '"') then
      EffectiveCmd := '"' + EffectiveCmd + '"';

    P.Executable := 'cmd.exe';
    P.Parameters.Add('/c');
{$ELSE}
    EffectiveCmd := CommandLine;
    P.Executable := '/bin/sh';
    P.Parameters.Add('-c');
{$ENDIF}
    P.Parameters.Add(EffectiveCmd);
    P.Options := [poUsePipes, poStderrToOutput, poNoConsole];
    P.Execute;

    while P.Running or (P.Output.NumBytesAvailable > 0) do
    begin
      if P.Output.NumBytesAvailable > 0 then
      begin
        ReadCount := P.Output.Read(ReadBuf{%H-}, SizeOf(ReadBuf));
        if ReadCount > 0 then
        begin
          SetString(Chunk, PAnsiChar(@ReadBuf[0]), ReadCount);
          Result.OutputText += Chunk;
          ParseAnalysisProgressChunk(Chunk, Pending, OutTimeMs);
        end;
      end
      else
        Sleep(20);

      if Assigned(OnProgress) and (DurationSec > 0.0) and (OutTimeMs > 0.0) then
      begin
        NowTickMs := GetTickCount64;
        if (LastEmitTickMs = 0) or ((NowTickMs - LastEmitTickMs) >= 500) then
        begin
          CurSec := OutTimeMs / 1000000.0;
          Percent := (CurSec / DurationSec) * 100.0;
          if Percent < 0.0 then
            Percent := 0.0
          else if Percent > 100.0 then
            Percent := 100.0;

          ElapsedSec := (NowTickMs - StartTickMs) / 1000.0;
          if Percent > 0.0 then
            EtaSec := ElapsedSec * (100.0 - Percent) / Percent
          else
            EtaSec := 0.0;

          OnProgress(Percent, EtaSec);
          LastEmitTickMs := NowTickMs;
        end;
      end;
    end;

    if Pending <> '' then
      Result.OutputText += Pending;

    P.WaitOnExit;
    Result.ExitCode := P.ExitStatus;
  finally
    P.Free;
  end;

  if Assigned(OnProgress) then
    OnProgress(100.0, 0.0);
end;

function ParseFloatInvariant(const S: string; out V: Double): Boolean;
var
  Fmt: TFormatSettings;
begin
  Fmt := DefaultFormatSettings;
  Fmt.DecimalSeparator := '.';
  Result := TryStrToFloat(S, V, Fmt);
end;

function ExtractNumberAfterToken(const Text, Token: string; out V: Double): Boolean;
var
  P, LastP, I, StartP: SizeInt;
  Num: string;
begin
  Result := False;
  LastP := 0;
  P := 1;
  repeat
    P := PosEx(Token, Text, P);
    if P > 0 then
    begin
      LastP := P;
      Inc(P, Length(Token));
    end;
  until P = 0;

  if LastP <= 0 then
    Exit(False);

  StartP := LastP + Length(Token);
  while (StartP <= Length(Text)) and (Text[StartP] in [' ', #9, '"', ':', '=']) do
    Inc(StartP);

  Num := '';
  for I := StartP to Length(Text) do
  begin
    if Text[I] in ['0'..'9', '-', '+', '.'] then
      Num += Text[I]
    else if Num <> '' then
      Break;
  end;

  if Num = '' then
    Exit(False);

  Result := ParseFloatInvariant(Num, V);
end;

function RunPeakTwoPass(const InputFile, FallbackLogDir: string;
  out ErrorLogPath, ErrorLogNotice: string; out Gain: Double;
  OnProgress: TOnProgressAnalysis = nil): TConverterError;
var
  Tools: TToolPaths;
  Cmd: string;
  R: TRunResult;
  LogInfo: TCommandErrorLog;
  V: Double;
  NullOutput: string;
begin
  ErrorLogPath := '';
  ErrorLogNotice := '';
  Gain := 0.0;
  Tools := ResolveToolPaths;
{$IFDEF Windows}
  NullOutput := 'nul';
{$ELSE}
  NullOutput := '-';
{$ENDIF}

  if Assigned(OnProgress) then
    Cmd :=
      QuoteForShell(Tools.FfmpegBin) +
      ' -nostdin -progress pipe:2 -nostats -vn -i ' + QuoteForShell(InputFile) +
      ' -af volumedetect -f null ' + NullOutput
  else
    Cmd :=
      QuoteForShell(Tools.FfmpegBin) +
      ' -nostdin -vn -i ' + QuoteForShell(InputFile) +
      ' -af volumedetect -f null ' + NullOutput + ' 2>&1';

  if Assigned(OnProgress) then
    R := RunAnalysisWithProgress(Cmd, InputFile, OnProgress)
  else
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
    LogInfo.ContextNote := 'peak analysis failed';
    if not WriteCommandErrorLog(LogInfo, ProgramDirectory, FallbackLogDir, ErrorLogPath, ErrorLogNotice) then
      ErrorLogPath := '';
    Exit(ERR_PEAK_ANALYSIS_FAILED);
  end;

  if not ExtractNumberAfterToken(R.OutputText, 'max_volume:', V) then
    Exit(ERR_PEAK_ANALYSIS_FAILED);

  if IsNan(V) or IsInfinite(V) then
    Exit(ERR_PEAK_ANALYSIS_FAILED);

  Gain := -3.0 - V;
  Result := ERR_OK;
end;

function ExtractJsonObjectAt(const Text: string; StartPos: SizeInt; out JsonText: string): Boolean;
var
  I: SizeInt;
  Depth: Integer;
begin
  Result := False;
  JsonText := '';

  if (StartPos <= 0) or (StartPos > Length(Text)) or (Text[StartPos] <> '{') then
    Exit(False);

  Depth := 0;
  for I := StartPos to Length(Text) do
  begin
    if Text[I] = '{' then
      Inc(Depth)
    else if Text[I] = '}' then
    begin
      Dec(Depth);
      if Depth = 0 then
      begin
        JsonText := Copy(Text, StartPos, I - StartPos + 1);
        Exit(True);
      end;
      if Depth < 0 then
        Exit(False);
    end;
  end;
end;

function ExtractLoudnormJson(const OutputText: string; out JsonText: string): Boolean;
var
  MarkerPos: SizeInt;
  SearchFrom: SizeInt;
  OpenPos: SizeInt;
  Candidate: string;
  M: TLoudnormMetrics;
begin
  Result := False;
  JsonText := '';

  SearchFrom := 1;
  repeat
    MarkerPos := PosEx('Parsed_loudnorm', OutputText, SearchFrom);
    if MarkerPos <= 0 then
      Break;

    OpenPos := PosEx('{', OutputText, MarkerPos);
    if (OpenPos > 0) and ExtractJsonObjectAt(OutputText, OpenPos, Candidate) and
       TryParseLoudnormJson(Candidate, M) then
    begin
      JsonText := Candidate;
      Exit(True);
    end;

    SearchFrom := MarkerPos + 1;
  until False;

  SearchFrom := 1;
  repeat
    OpenPos := PosEx('{', OutputText, SearchFrom);
    if OpenPos <= 0 then
      Break;

    if ExtractJsonObjectAt(OutputText, OpenPos, Candidate) and TryParseLoudnormJson(Candidate, M) then
    begin
      JsonText := Candidate;
      Exit(True);
    end;

    SearchFrom := OpenPos + 1;
  until False;
end;

function RunLoudnormTwoPass(const InputFile, FallbackLogDir: string;
  out ErrorLogPath, ErrorLogNotice: string; var Opts: TConvertOptions;
  OnProgress: TOnProgressAnalysis = nil): TConverterError;
var
  Fmt: TFormatSettings;
  Tools: TToolPaths;
  Cmd: string;
  FilterStr: string;
  R: TRunResult;
  LogInfo: TCommandErrorLog;
  Metrics: TLoudnormMetrics;
  JsonText: string;
  NullOutput: string;
begin
  ErrorLogPath := '';
  ErrorLogNotice := '';

  Fmt := DefaultFormatSettings;
  Fmt.DecimalSeparator := '.';
  Tools := ResolveToolPaths;

{$IFDEF Windows}
  NullOutput := 'nul';
  FilterStr := Format('loudnorm=I=%.1f:TP=%.1f:LRA=%.1f:print_format=json', [Opts.I_target, Opts.TP_target, Opts.LRA_target], Fmt);
  if Assigned(OnProgress) then
    Cmd := Format('%s -nostdin -progress pipe:2 -nostats -vn -i %s -af "%s" -f null %s',
      [QuoteForShell(Tools.FfmpegBin), QuoteForShell(InputFile), FilterStr, NullOutput])
  else
    Cmd := Format('%s -nostdin -vn -i %s -af "%s" -f null %s 2>&1',
      [QuoteForShell(Tools.FfmpegBin), QuoteForShell(InputFile), FilterStr, NullOutput]);
{$ELSE}
  NullOutput := '-';
  FilterStr := Format('loudnorm=I=%.1f:TP=%.1f:LRA=%.1f:print_format=json', [Opts.I_target, Opts.TP_target, Opts.LRA_target], Fmt);
  if Assigned(OnProgress) then
    Cmd := Format('%s -nostdin -progress pipe:2 -nostats -vn -i %s -af "%s" -f null %s',
      [QuoteForShell(Tools.FfmpegBin), QuoteForShell(InputFile), FilterStr, NullOutput])
  else
    Cmd := Format('%s -nostdin -vn -i %s -af "%s" -f null %s 2>&1',
      [QuoteForShell(Tools.FfmpegBin), QuoteForShell(InputFile), FilterStr, NullOutput]);
{$ENDIF}

  if Assigned(OnProgress) then
    R := RunAnalysisWithProgress(Cmd, InputFile, OnProgress)
  else
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
    LogInfo.ContextNote := 'loudnorm analysis failed';
    if not WriteCommandErrorLog(LogInfo, ProgramDirectory, FallbackLogDir, ErrorLogPath, ErrorLogNotice) then
      ErrorLogPath := '';
    Exit(ERR_LOUDNORM_ANALYSIS_FAILED);
  end;

  if not ExtractLoudnormJson(R.OutputText, JsonText) then
    Exit(ERR_LOUDNORM_ANALYSIS_FAILED);

  if not TryParseLoudnormJson(JsonText, Metrics) then
    Exit(ERR_LOUDNORM_ANALYSIS_FAILED);

  if IsNan(Metrics.InputI) or IsInfinite(Metrics.InputI) or
     IsNan(Metrics.InputTP) or IsInfinite(Metrics.InputTP) or
     IsNan(Metrics.InputLRA) or IsInfinite(Metrics.InputLRA) or
     IsNan(Metrics.InputThresh) or IsInfinite(Metrics.InputThresh) or
     IsNan(Metrics.TargetOffset) or IsInfinite(Metrics.TargetOffset) then
    Exit(ERR_LOUDNORM_ANALYSIS_FAILED);

  Opts.measured_I := Metrics.InputI;
  Opts.measured_TP := Metrics.InputTP;
  Opts.measured_LRA := Metrics.InputLRA;
  Opts.measured_thresh := Metrics.InputThresh;
  Opts.measured_offset := Metrics.TargetOffset;

  Result := ERR_OK;
end;

end.