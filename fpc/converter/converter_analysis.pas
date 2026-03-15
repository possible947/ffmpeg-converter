unit converter_analysis;

{$mode objfpc}{$H+}

interface

uses converter_types;

function RunPeakTwoPass(const InputFile: string; out Gain: Double): TConverterError;
function RunLoudnormTwoPass(const InputFile: string; var Opts: TConvertOptions): TConverterError;
function ExtractNumberAfterToken(const Text, Token: string; out V: Double): Boolean;
function ExtractLoudnormJson(const OutputText: string; out JsonText: string): Boolean;

implementation

uses
  SysUtils,
  Math,
  StrUtils,
  process_utils,
  path_utils,
  tool_paths,
  loudnorm_json;

procedure WriteDebugDump(const FileName, Content: string);
var
  Dump: TextFile;
begin
  AssignFile(Dump, FileName);
  Rewrite(Dump);
  try
    Write(Dump, Content);
  finally
    CloseFile(Dump);
  end;
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

function RunPeakTwoPass(const InputFile: string; out Gain: Double): TConverterError;
var
  FfmpegBin: string;
  R: TRunResult;
  V: Double;
begin
  Gain := 0.0;
  FfmpegBin := ResolveFfmpegBin;
  R := RunCommandCapture(QuoteForShell(FfmpegBin) + ' -nostdin -vn -i ' + QuoteForShell(InputFile) + ' -af volumedetect -f null - 2>&1');

  if R.ExitCode <> 0 then
  begin
    WriteDebugDump('/tmp/ffc_peak_fail.log', R.OutputText);
    Exit(ERR_PEAK_ANALYSIS_FAILED);
  end;

  if not ExtractNumberAfterToken(R.OutputText, 'max_volume:', V) then
  begin
    WriteDebugDump('/tmp/ffc_peak_fail.log', R.OutputText);
    Exit(ERR_PEAK_ANALYSIS_FAILED);
  end;

  if IsNan(V) or IsInfinite(V) then
  begin
    WriteDebugDump('/tmp/ffc_peak_fail.log', R.OutputText);
    Exit(ERR_PEAK_ANALYSIS_FAILED);
  end;

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

function RunLoudnormTwoPass(const InputFile: string; var Opts: TConvertOptions): TConverterError;
var
  Fmt: TFormatSettings;
  FfmpegBin: string;
  R: TRunResult;
  Metrics: TLoudnormMetrics;
  JsonText: string;
begin
  Fmt := DefaultFormatSettings;
  Fmt.DecimalSeparator := '.';
  FfmpegBin := ResolveFfmpegBin;

  R := RunCommandCapture(
    Format('%s -nostdin -vn -i %s -af "loudnorm=I=%.1f:TP=%.1f:LRA=%.1f:print_format=json" -f null - 2>&1',
      [QuoteForShell(FfmpegBin), QuoteForShell(InputFile), Opts.I_target, Opts.TP_target, Opts.LRA_target], Fmt));

  if R.ExitCode <> 0 then
  begin
    WriteDebugDump('/tmp/ffc_loud_fail.log', R.OutputText);
    Exit(ERR_LOUDNORM_ANALYSIS_FAILED);
  end;

  if not ExtractLoudnormJson(R.OutputText, JsonText) then
  begin
    WriteDebugDump('/tmp/ffc_loud_fail.log', R.OutputText);
    Exit(ERR_LOUDNORM_ANALYSIS_FAILED);
  end;

  if not TryParseLoudnormJson(JsonText, Metrics) then
  begin
    WriteDebugDump('/tmp/ffc_loud_fail.log', R.OutputText);
    Exit(ERR_LOUDNORM_ANALYSIS_FAILED);
  end;

  if IsNan(Metrics.InputI) or IsInfinite(Metrics.InputI) or
     IsNan(Metrics.InputTP) or IsInfinite(Metrics.InputTP) or
     IsNan(Metrics.InputLRA) or IsInfinite(Metrics.InputLRA) or
     IsNan(Metrics.InputThresh) or IsInfinite(Metrics.InputThresh) or
     IsNan(Metrics.TargetOffset) or IsInfinite(Metrics.TargetOffset) then
  begin
    WriteDebugDump('/tmp/ffc_loud_fail.log', R.OutputText);
    Exit(ERR_LOUDNORM_ANALYSIS_FAILED);
  end;

  Opts.measured_I := Metrics.InputI;
  Opts.measured_TP := Metrics.InputTP;
  Opts.measured_LRA := Metrics.InputLRA;
  Opts.measured_thresh := Metrics.InputThresh;
  Opts.measured_offset := Metrics.TargetOffset;

  Result := ERR_OK;
end;

end.
