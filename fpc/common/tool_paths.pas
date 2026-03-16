unit tool_paths;

{$mode objfpc}{$H+}

interface

type
  TToolPaths = record
    FfmpegBin: string;
    FfprobeBin: string;
    Mp4BoxBin: string;
    PathValue: string;
  end;

function ResolveFfmpegBin: string;
function ResolveFfprobeBin: string;
function ResolveMp4BoxBin: string;
function ResolveToolPaths: TToolPaths;
function ApplyBundledToolEnvironment: Boolean;

implementation

uses
  BaseUnix,
  ctypes,
  SysUtils,
  process_utils,
  path_utils;

function libc_setenv(name, value: PChar; overwrite: cint): cint; cdecl; external 'c' name 'setenv';

function IsExecutableFile(const FilePath: string): Boolean;
begin
  Result := (FilePath <> '') and FileExists(FilePath) and (fpAccess(PChar(FilePath), X_OK) = 0);
end;

function ResolveBundledMacTool(const PrimaryName: string): string;
{$IFDEF DARWIN}
var
  BundleMarkerPos: SizeInt;
  ExePath: string;
  ProgramDir: string;
  Candidate: string;
{$ENDIF}
begin
  Result := '';

  {$IFDEF DARWIN}
  ProgramDir := ExcludeTrailingPathDelimiter(ProgramDirectory);
  if ProgramDir <> '' then
  begin
    Candidate := ExpandFileName(ProgramDir + '/../Resources/bin/' + PrimaryName);
    if IsExecutableFile(Candidate) then
      Exit(Candidate);
  end;

  ExePath := ExpandFileName(ParamStr(0));
  if ExePath <> '' then
  begin
    BundleMarkerPos := Pos('.app/Contents/MacOS/', ExePath);
    if BundleMarkerPos > 0 then
    begin
      Candidate := Copy(ExePath, 1, BundleMarkerPos + Length('.app/Contents')) + '/Resources/bin/' + PrimaryName;
      Candidate := ExpandFileName(Candidate);
      if IsExecutableFile(Candidate) then
        Exit(Candidate);
    end;
  end;
  {$ENDIF}
end;

function ResolveBundledMacBinDir: string;
{$IFDEF DARWIN}
var
  ProgramDir: string;
  Candidate: string;
{$ENDIF}
begin
  Result := '';

  {$IFDEF DARWIN}
  ProgramDir := ExcludeTrailingPathDelimiter(ProgramDirectory);
  if ProgramDir <> '' then
  begin
    Candidate := ExpandFileName(ProgramDir + '/../Resources/bin');
    if DirectoryExists(Candidate) then
      Exit(Candidate);
  end;
  {$ENDIF}
end;

function SetEnvValue(const Name, Value: string): Boolean;
begin
  Result := False;
  if (Name = '') or (Value = '') then
    Exit;
  Result := libc_setenv(PChar(Name), PChar(Value), 1) = 0;
end;

function ResolveFromEnv(const EnvVarName: string): string;
var
  EnvPath: string;
begin
  Result := '';
  EnvPath := Trim(GetEnvironmentVariable(EnvVarName));
  if EnvPath = '' then
    Exit;

  if IsExecutableFile(EnvPath) then
    Result := EnvPath;
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
  Result := '';

  if PrimaryName = 'ffmpeg' then
  begin
    Result := ResolveFromEnv('FFMPEG');
    if Result = '' then
      Result := ResolveFromEnv('FFMPEG_BIN');
  end
  else if PrimaryName = 'ffprobe' then
  begin
    Result := ResolveFromEnv('FFPROBE');
    if Result = '' then
      Result := ResolveFromEnv('FFPROBE_BIN');
  end;

  if Result <> '' then
    Exit;

  {$IFDEF DARWIN}
  Result := ResolveBundledMacTool(PrimaryName);
  if Result <> '' then
    Exit;

  for I := Low(MacCandidates) to High(MacCandidates) do
  begin
    P := MacCandidates[I];
    if IsExecutableFile(P) then
      Exit(P);
  end;
  {$ENDIF}

  Result := ResolveFromPath(PrimaryName);
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

function ResolveMp4BoxBin: string;
begin
  Result := ResolveBinary('MP4Box',
    ['/opt/local/bin/MP4Box', '/opt/homebrew/bin/MP4Box', '/usr/local/bin/MP4Box']);
end;

function ResolveToolPaths: TToolPaths;
begin
  Result.FfmpegBin := ResolveFfmpegBin;
  Result.FfprobeBin := ResolveFfprobeBin;
  Result.Mp4BoxBin := ResolveMp4BoxBin;
  Result.PathValue := GetEnvironmentVariable('PATH');
end;

function ApplyBundledToolEnvironment: Boolean;
var
  BinDir: string;
  FfmpegPath: string;
  FfprobePath: string;
  PathValue: string;
begin
  Result := False;

  BinDir := ResolveBundledMacBinDir;
  if BinDir = '' then
    Exit(False);

  FfmpegPath := IncludeTrailingPathDelimiter(BinDir) + 'ffmpeg';
  FfprobePath := IncludeTrailingPathDelimiter(BinDir) + 'ffprobe';

  if IsExecutableFile(FfmpegPath) then
  begin
    SetEnvValue('FFMPEG', FfmpegPath);
    SetEnvValue('FFMPEG_BIN', FfmpegPath);
    Result := True;
  end;

  if IsExecutableFile(FfprobePath) then
  begin
    SetEnvValue('FFPROBE', FfprobePath);
    SetEnvValue('FFPROBE_BIN', FfprobePath);
    Result := True;
  end;

  if Result then
  begin
    PathValue := GetEnvironmentVariable('PATH');
    if Pos(BinDir, PathValue) = 0 then
    begin
      if PathValue <> '' then
        PathValue := BinDir + ':' + PathValue
      else
        PathValue := BinDir;
      SetEnvValue('PATH', PathValue);
    end;
  end;
end;

end.
