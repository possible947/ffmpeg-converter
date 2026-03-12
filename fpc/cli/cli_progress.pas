unit cli_progress;

{$mode objfpc}{$H+}

interface

procedure ProgressUpdate(const Percent, Fps, Eta: Single);
procedure ProgressEnd;

implementation

uses
  SysUtils,
  time_utils;

procedure ProgressUpdate(const Percent, Fps, Eta: Single);
begin
  if Fps > 0 then
    Write(Format(#13'fps=%.0f %3.0f%% %s', [Fps, Percent, FormatEta(Eta)]))
  else
    Write(Format(#13'%3.0f%% %s', [Percent, FormatEta(Eta)]));
end;

procedure ProgressEnd;
begin
  WriteLn;
end;

end.
