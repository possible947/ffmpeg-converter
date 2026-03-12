unit converter_analysis;

{$mode objfpc}{$H+}

interface

uses converter_types;

function RunPeakTwoPass(const InputFile: string; out Gain: Double): TConverterError;
function RunLoudnormTwoPass(const InputFile: string; var Opts: TConvertOptions): TConverterError;

implementation

uses
  SysUtils,
  StrUtils,
  process_utils,
  path_utils,
  loudnorm_json;

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
  R: TRunResult;
  P: SizeInt;
  V: Double;
  Dump: TextFile;
begin
  Gain := 0.0;
  R := RunCommandCapture('ffmpeg -nostdin -vn -i ' + QuoteForShell(InputFile) + ' -af volumedetect -f null - 2>&1');

  P := Pos('max_volume:', R.OutputText);
  if P <= 0 then
  begin
    AssignFile(Dump, '/tmp/ffc_peak_fail.log');
    Rewrite(Dump);
    Write(Dump, R.OutputText);
    CloseFile(Dump);
    Exit(ERR_PEAK_ANALYSIS_FAILED);
  end;

  if not ExtractNumberAfterToken(R.OutputText, 'max_volume:', V) then
  begin
    AssignFile(Dump, '/tmp/ffc_peak_fail.log');
    Rewrite(Dump);
    Write(Dump, R.OutputText);
    CloseFile(Dump);
    Exit(ERR_PEAK_ANALYSIS_FAILED);
  end;

  Gain := -3.0 - V;
  Result := ERR_OK;
end;

function RunLoudnormTwoPass(const InputFile: string; var Opts: TConvertOptions): TConverterError;
var
  R: TRunResult;
  Metrics: TLoudnormMetrics;
  StartPos: SizeInt;
  EndPos: SizeInt;
  JsonText: string;
  Dump: TextFile;
begin
  R := RunCommandCapture(
    Format('ffmpeg -nostdin -vn -i %s -af "loudnorm=I=%.1f:TP=%.1f:LRA=%.1f:print_format=json" -f null - 2>&1',
      [QuoteForShell(InputFile), Opts.I_target, Opts.TP_target, Opts.LRA_target]));

  if R.ExitCode <> 0 then
  begin
    AssignFile(Dump, '/tmp/ffc_loud_fail.log');
    Rewrite(Dump);
    Write(Dump, R.OutputText);
    CloseFile(Dump);
    Exit(ERR_LOUDNORM_ANALYSIS_FAILED);
  end;

  StartPos := RPos('{', R.OutputText);
  EndPos := RPos('}', R.OutputText);
  if (StartPos <= 0) or (EndPos < StartPos) then
  begin
    AssignFile(Dump, '/tmp/ffc_loud_fail.log');
    Rewrite(Dump);
    Write(Dump, R.OutputText);
    CloseFile(Dump);
    Exit(ERR_LOUDNORM_ANALYSIS_FAILED);
  end;

  JsonText := Copy(R.OutputText, StartPos, EndPos - StartPos + 1);
  if not TryParseLoudnormJson(JsonText, Metrics) then
  begin
    AssignFile(Dump, '/tmp/ffc_loud_fail.log');
    Rewrite(Dump);
    Write(Dump, R.OutputText);
    CloseFile(Dump);
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
