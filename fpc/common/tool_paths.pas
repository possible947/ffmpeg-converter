unit tool_paths;

{$mode objfpc}{$H+}

interface

type
  TToolPaths = record
    FfmpegBin: string;
    FfprobeBin: string;
    Mp4BoxBin: string;
    MkvmergeBin: string;
    PathValue: string;
  end;

function ResolveFfmpegBin: string;
function ResolveFfprobeBin: string;
function ResolveMp4BoxBin: string;
function ResolveMkvmergeBin: string;
function ResolveToolPaths: TToolPaths;
function ApplyBundledToolEnvironment: Boolean;

implementation

uses
  {$IFDEF Windows}
  Windows,
  {$ELSE}
  BaseUnix,
  ctypes,
  {$ENDIF}
  SysUtils,
  process_utils,
  path_utils;

{$IFNDEF Windows}
function libc_setenv(name, value: PChar; overwrite: cint): cint; cdecl; external 'c' name 'setenv';
{$ENDIF}

function IsExecutableFile(const FilePath: string): Boolean;
begin
{$IFDEF Windows}
  Result := (FilePath <> '') and FileExists(FilePath);
{$ELSE}
  Result := (FilePath <> '') and FileExists(FilePath) and (fpAccess(PChar(FilePath), X_OK) = 0);
{$ENDIF}
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
{$IFDEF Windows}
var
  NameA: AnsiString;
  ValueA: AnsiString;
{$ENDIF}
begin
  Result := False;
  if (Name = '') or (Value = '') then
    Exit;
{$IFDEF Windows}
  NameA := AnsiString(Name);
  ValueA := AnsiString(Value);
  Result := SetEnvironmentVariableA(PAnsiChar(NameA), PAnsiChar(ValueA));
{$ELSE}
  Result := libc_setenv(PChar(Name), PChar(Value), 1) = 0;
{$ENDIF}
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

{$IFDEF Windows}
  R := RunCommandCapture('where ' + Name + ' 2>nul');
{$ELSE}
  R := RunCommandCapture('command -v ' + QuoteForShell(Name) + ' 2>/dev/null');
{$ENDIF}
  if R.ExitCode <> 0 then
    Exit;

  P := Trim(R.OutputText);
{$IFDEF Windows}
  { 'where' may return multiple lines; take the first one }
  if Pos(#13, P) > 0 then
    P := Copy(P, 1, Pos(#13, P) - 1);
  if Pos(#10, P) > 0 then
    P := Copy(P, 1, Pos(#10, P) - 1);
  P := Trim(P);
{$ENDIF}
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
  end
  else if PrimaryName = 'mkvmerge' then
    Result := ResolveFromEnv('MKVMERGE_BIN');

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

function ResolveFromExeDir(const Name: string): string;
var
  ExeDir: string;
  Candidate: string;
begin
  Result := '';
  ExeDir := ExtractFilePath(ExpandFileName(ParamStr(0)));
  if ExeDir = '' then Exit;
{$IFDEF Windows}
  Candidate := IncludeTrailingPathDelimiter(ExeDir) + Name + '.exe';
  if IsExecutableFile(Candidate) then
    Exit(Candidate);
{$ENDIF}
  Candidate := IncludeTrailingPathDelimiter(ExeDir) + Name;
  if IsExecutableFile(Candidate) then
    Result := Candidate;
end;

function ResolveFromRepoWindowsBin(const Name: string): string;
{$IFDEF Windows}
var
  BaseDir: string;
  Candidate: string;
  I: Integer;
{$ENDIF}
begin
  Result := '';
{$IFDEF Windows}
  BaseDir := ExpandFileName(ExtractFilePath(ParamStr(0)));
  for I := 1 to 8 do
  begin
    Candidate := IncludeTrailingPathDelimiter(BaseDir) + 'src\platform\windows\bin\' + Name + '.exe';
    if IsExecutableFile(Candidate) then
      Exit(Candidate);
    BaseDir := ExpandFileName(IncludeTrailingPathDelimiter(BaseDir) + '..');
  end;
{$ENDIF}
end;

function ResolveFfmpegBin: string;
begin
  Result := ResolveFromEnv('FFMPEG');
  if Result = '' then Result := ResolveFromEnv('FFMPEG_BIN');
  if Result = '' then Result := ResolveFromExeDir('ffmpeg');
  if Result = '' then Result := ResolveFromRepoWindowsBin('ffmpeg');
  if Result = '' then
  Result := ResolveBinary('ffmpeg',
    ['/opt/local/bin/ffmpeg8', '/opt/local/bin/ffmpeg', '/opt/homebrew/bin/ffmpeg', '/usr/local/bin/ffmpeg',
     '/usr/bin/ffmpeg', '/snap/bin/ffmpeg']);
end;

function ResolveFfprobeBin: string;
begin
  Result := ResolveFromEnv('FFPROBE');
  if Result = '' then Result := ResolveFromEnv('FFPROBE_BIN');
  if Result = '' then Result := ResolveFromExeDir('ffprobe');
  if Result = '' then Result := ResolveFromRepoWindowsBin('ffprobe');
  if Result = '' then
  Result := ResolveBinary('ffprobe',
    ['/opt/local/bin/ffprobe8', '/opt/local/bin/ffprobe', '/opt/homebrew/bin/ffprobe', '/usr/local/bin/ffprobe',
     '/usr/bin/ffprobe', '/snap/bin/ffprobe']);
end;

function ResolveMp4BoxBin: string;
{$IFDEF Windows}
var
  Candidates: array of string;
  I: Integer;
{$ENDIF}
begin
  Result := ResolveFromEnv('MP4BOX');
  if Result = '' then Result := ResolveFromEnv('MP4BOX_BIN');
  if Result = '' then Result := ResolveFromExeDir('MP4Box');
  if Result = '' then Result := ResolveFromRepoWindowsBin('MP4Box');
  if Result <> '' then
    Exit;

{$IFDEF Windows}
  { Search in common Windows installation paths }
  Candidates := [
    'C:\Program Files\GPAC\mp4box.exe',
    'C:\Program Files (x86)\GPAC\mp4box.exe',
    'C:\Program Files\Hybrid\64bit\MP4Box.exe',
    'C:\Program Files (x86)\Hybrid\MP4Box.exe'
  ];
  
  for I := Low(Candidates) to High(Candidates) do
  begin
    if IsExecutableFile(Candidates[I]) then
    begin
      Result := Candidates[I];
      Exit;
    end;
  end;
{$ENDIF}

  Result := ResolveBinary('MP4Box',
    ['/opt/local/bin/MP4Box', '/opt/homebrew/bin/MP4Box', '/usr/local/bin/MP4Box',
     '/usr/bin/MP4Box', '/snap/bin/mp4box']);
end;

function ResolveMkvmergeBin: string;
begin
  Result := ResolveFromEnv('MKVMERGE');
  if Result = '' then
    Result := ResolveFromEnv('MKVMERGE_BIN');
  if Result = '' then
    Result := ResolveFromExeDir('mkvmerge');
  if Result = '' then
    Result := ResolveFromRepoWindowsBin('mkvmerge');
  if Result = '' then
  Result := ResolveBinary('mkvmerge',
    ['/opt/local/bin/mkvmerge', '/opt/homebrew/bin/mkvmerge', '/usr/local/bin/mkvmerge',
     '/usr/bin/mkvmerge']);
  if Result = '' then
    Result := 'mkvmerge';
end;

function ResolveToolPaths: TToolPaths;
begin
  Result.FfmpegBin := ResolveFfmpegBin;
  Result.FfprobeBin := ResolveFfprobeBin;
  Result.Mp4BoxBin := ResolveMp4BoxBin;
  Result.MkvmergeBin := ResolveMkvmergeBin;
  Result.PathValue := GetEnvironmentVariable('PATH');
end;

function ApplyBundledToolEnvironment: Boolean;
var
  BinDir: string;
  FfmpegPath: string;
  FfprobePath: string;
  MkvmergePath: string;
  PathValue: string;
begin
  Result := False;

  BinDir := ResolveBundledMacBinDir;
  if BinDir = '' then
    Exit(False);

  FfmpegPath := IncludeTrailingPathDelimiter(BinDir) + 'ffmpeg';
  FfprobePath := IncludeTrailingPathDelimiter(BinDir) + 'ffprobe';
  MkvmergePath := IncludeTrailingPathDelimiter(BinDir) + 'mkvmerge';

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

  if IsExecutableFile(MkvmergePath) then
  begin
    SetEnvValue('MKVMERGE_BIN', MkvmergePath);
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
