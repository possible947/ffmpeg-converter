unit time_utils;

{$mode objfpc}{$H+}

interface

function ParseFfmpegTime(const S: string): Double;
function FormatEta(Seconds: Double): string;

implementation

uses
  SysUtils;

function ParseFfmpegTime(const S: string): Double;
var
  H, M: Integer;
  Sec: Double;
  Code: Integer;
  Work: string;
begin
  Result := 0.0;
  Work := Trim(S);
  H := 0;
  M := 0;
  Sec := 0.0;

  Val(Copy(Work, 1, 2), H, Code);
  if Code <> 0 then
    Exit;
  Val(Copy(Work, 4, 2), M, Code);
  if Code <> 0 then
    Exit;
  Val(Copy(Work, 7, MaxInt), Sec, Code);
  if Code <> 0 then
    Exit;

  Result := (H * 3600.0) + (M * 60.0) + Sec;
end;

function FormatEta(Seconds: Double): string;
var
  T, H, M, S: Integer;
begin
  if Seconds <= 0 then
    Exit('ETA --:--:--');

  T := Trunc(Seconds);
  H := T div 3600;
  M := (T mod 3600) div 60;
  S := T mod 60;

  Result := Format('ETA %.2d:%.2d:%.2d', [H, M, S]);
end;

end.
