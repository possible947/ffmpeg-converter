unit tool_paths;

{$mode objfpc}{$H+}

interface

function ResolveFfmpegBin: string;
function ResolveFfprobeBin: string;

implementation

uses
  BaseUnix,
  SysUtils,
  process_utils,
  path_utils;

function IsExecutableFile(const FilePath: string): Boolean;
begin
  Result := (FilePath <> '') and FileExists(FilePath) and (fpAccess(PChar(FilePath), X_OK) = 0);
end;

function ResolveFromPath(const Name: string): string;
var
  R: TRunResult;
  P: string;
begin
  Result := '';
  if Name = '' then
    Exit;

  R := RunCommandCapture('command -v ' + QuoteForShell(Name) + ' 2>/dev/null');
  if R.ExitCode <> 0 then
    Exit;

  P := Trim(R.OutputText);
  if IsExecutableFile(P) then
    Result := P;
end;

function ResolveBinary(const PrimaryName: string; const MacCandidates: array of string): string;
{$IFDEF DARWIN}
var
  I: Integer;
  P: string;
{$ENDIF}
begin
  Result := ResolveFromPath(PrimaryName);
  if Result <> '' then
    Exit;

  {$IFDEF DARWIN}
  for I := Low(MacCandidates) to High(MacCandidates) do
  begin
    P := MacCandidates[I];
    if IsExecutableFile(P) then
      Exit(P);
  end;
  {$ENDIF}
end;

function ResolveFfmpegBin: string;
begin
  Result := ResolveBinary('ffmpeg',
    ['/opt/homebrew/bin/ffmpeg', '/usr/local/bin/ffmpeg', '/usr/bin/ffmpeg']);
  if Result = '' then
    Result := 'ffmpeg';
end;

function ResolveFfprobeBin: string;
begin
  Result := ResolveBinary('ffprobe',
    ['/opt/homebrew/bin/ffprobe', '/usr/local/bin/ffprobe', '/usr/bin/ffprobe']);
  if Result = '' then
    Result := 'ffprobe';
end;

end.
