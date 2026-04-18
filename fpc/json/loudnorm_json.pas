unit loudnorm_json;

{$mode objfpc}{$H+}

interface

type
  TLoudnormMetrics = record
    InputI: Double;
    InputTP: Double;
    InputLRA: Double;
    InputThresh: Double;
    TargetOffset: Double;
  end;

function TryParseLoudnormJson(const Text: string; out Metrics: TLoudnormMetrics): Boolean;

implementation

uses
  SysUtils,
  fpjson,
  jsonparser;

function JsonNumToFloat(O: TJSONObject; const Key: string; out Value: Double): Boolean;
var
  D: TJSONData;
  Fmt: TFormatSettings;
begin
  D := O.Find(Key);
  if D = nil then
    Exit(False);

  if D.JSONType = jtNumber then
    Value := TJSONNumber(D).AsFloat
  else if D.JSONType = jtString then
  begin
    Fmt := DefaultFormatSettings;
    Fmt.DecimalSeparator := '.';
    if not TryStrToFloat(D.AsString, Value, Fmt) then
      Exit(False);
  end
  else
    Exit(False);

  Result := True;
end;

function TryParseLoudnormJson(const Text: string; out Metrics: TLoudnormMetrics): Boolean;
var
  J: TJSONData;
  O: TJSONObject;
begin
  FillChar(Metrics, SizeOf(Metrics), 0);
  Result := False;

  J := nil;
  try
    J := GetJSON(Text);
    if not (J is TJSONObject) then
      Exit;

    O := TJSONObject(J);
    if not JsonNumToFloat(O, 'input_i', Metrics.InputI) then Exit;
    if not JsonNumToFloat(O, 'input_tp', Metrics.InputTP) then Exit;
    if not JsonNumToFloat(O, 'input_lra', Metrics.InputLRA) then Exit;
    if not JsonNumToFloat(O, 'input_thresh', Metrics.InputThresh) then Exit;
    if not JsonNumToFloat(O, 'target_offset', Metrics.TargetOffset) then Exit;

    Result := True;
  except
    Result := False;
  end;
  J.Free;
end;

end.
