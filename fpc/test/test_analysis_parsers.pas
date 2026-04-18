program test_analysis_parsers;

{$mode objfpc}{$H+}

uses
  SysUtils,
  converter_analysis,
  loudnorm_json;

procedure AssertTrue(const LabelName: string; Cond: Boolean);
begin
  if not Cond then
  begin
    WriteLn('FAIL [', LabelName, ']');
    Halt(1);
  end;
end;

procedure AssertNear(const LabelName: string; Actual, Expected, Eps: Double);
begin
  if Abs(Actual - Expected) > Eps then
  begin
    WriteLn('FAIL [', LabelName, ']: expected=', Expected:0:6, ' actual=', Actual:0:6);
    Halt(1);
  end;
end;

var
  V: Double;
  JsonText: string;
  Metrics: TLoudnormMetrics;
  OutText: string;
begin
  AssertTrue('peak token parse basic', ExtractNumberAfterToken('max_volume: -7.2 dB', 'max_volume:', V));
  AssertNear('peak token value basic', V, -7.2, 0.0001);

  AssertTrue('peak token parse last occurrence',
    ExtractNumberAfterToken('max_volume: -9.1 dB ... max_volume: -4.5 dB', 'max_volume:', V));
  AssertNear('peak token value last occurrence', V, -4.5, 0.0001);

  AssertTrue('peak token parse spaced separator',
    ExtractNumberAfterToken('foo max_volume:    "-1.25" bar', 'max_volume:', V));
  AssertNear('peak token value spaced separator', V, -1.25, 0.0001);

  AssertTrue('peak token missing fails', not ExtractNumberAfterToken('no peak here', 'max_volume:', V));
  AssertTrue('peak token malformed fails', not ExtractNumberAfterToken('max_volume: NaN dB', 'max_volume:', V));

  OutText :=
    'ffmpeg preamble' + LineEnding +
    '[Parsed_loudnorm_0 @ 0xaaa] {' + LineEnding +
    '  "input_i" : "-18.91",' + LineEnding +
    '  "input_tp" : "-3.20",' + LineEnding +
    '  "input_lra" : "8.10",' + LineEnding +
    '  "input_thresh" : "-29.50",' + LineEnding +
    '  "target_offset" : "0.40"' + LineEnding +
    '}' + LineEnding +
    'trailer';

  AssertTrue('extract loudnorm json with marker', ExtractLoudnormJson(OutText, JsonText));
  AssertTrue('parse extracted loudnorm json', TryParseLoudnormJson(JsonText, Metrics));
  AssertNear('loudnorm quoted input_i', Metrics.InputI, -18.91, 0.0001);
  AssertNear('loudnorm quoted input_tp', Metrics.InputTP, -3.20, 0.0001);
  AssertNear('loudnorm quoted input_lra', Metrics.InputLRA, 8.10, 0.0001);
  AssertNear('loudnorm quoted input_thresh', Metrics.InputThresh, -29.50, 0.0001);
  AssertNear('loudnorm quoted target_offset', Metrics.TargetOffset, 0.40, 0.0001);

  OutText :=
    '{' +
    '"input_i":-18.91,' +
    '"input_tp":-3.20,' +
    '"input_lra":8.10,' +
    '"input_thresh":-29.50,' +
    '"target_offset":0.40' +
    '}';
  AssertTrue('parse numeric loudnorm json', TryParseLoudnormJson(OutText, Metrics));
  AssertNear('loudnorm numeric input_i', Metrics.InputI, -18.91, 0.0001);
  AssertNear('loudnorm numeric input_tp', Metrics.InputTP, -3.20, 0.0001);
  AssertNear('loudnorm numeric input_lra', Metrics.InputLRA, 8.10, 0.0001);
  AssertNear('loudnorm numeric input_thresh', Metrics.InputThresh, -29.50, 0.0001);
  AssertNear('loudnorm numeric target_offset', Metrics.TargetOffset, 0.40, 0.0001);

  OutText := 'log with unrelated {"foo":1} and no loudnorm fields';
  AssertTrue('extract loudnorm json fails without metrics', not ExtractLoudnormJson(OutText, JsonText));

  WriteLn('OK: analysis parser tests');
end.
