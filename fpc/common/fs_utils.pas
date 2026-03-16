unit fs_utils;

{$mode objfpc}{$H+}

interface

function FileReadable(const Path: string): Boolean;
function FileRegular(const Path: string): Boolean;
function DirWritable(const Path: string): Boolean;
function ResolveUserHomeDir: string;
function DefaultOutputDir: string;
function EnsureOutputDirWritable(const RequestedDir: string; out ResolvedDir: string; out ErrorText: string): Boolean;

implementation

uses
  BaseUnix,
  SysUtils;

function FileReadable(const Path: string): Boolean;
begin
  Result := (fpAccess(PChar(Path), R_OK) = 0);
end;

function FileRegular(const Path: string): Boolean;
var
  Info: Stat;
begin
  if fpStat(PChar(Path), Info) <> 0 then
    Exit(False);
  Result := FPS_ISREG(Info.st_mode);
end;

function DirWritable(const Path: string): Boolean;
var
  Info: Stat;
begin
  if fpStat(PChar(Path), Info) <> 0 then
    Exit(False);
  if not FPS_ISDIR(Info.st_mode) then
    Exit(False);
  Result := fpAccess(PChar(Path), W_OK) = 0;
end;

function ResolveUserHomeDir: string;
var
  AppCfgDir: string;
begin
  Result := Trim(GetEnvironmentVariable('HOME'));
  if Result <> '' then
    Exit;

  AppCfgDir := ExcludeTrailingPathDelimiter(GetAppConfigDir(False));
  if AppCfgDir <> '' then
    Result := ExtractFileDir(AppCfgDir);
end;

function DefaultOutputDir: string;
var
  HomeDir: string;
begin
  HomeDir := ResolveUserHomeDir;
  if HomeDir = '' then
    Exit('');
  Result := IncludeTrailingPathDelimiter(HomeDir) + 'ffmpeg_converter';
end;

function EnsureOutputDirWritable(const RequestedDir: string; out ResolvedDir: string; out ErrorText: string): Boolean;
var
  TargetDir: string;
  Info: Stat;
begin
  Result := False;
  ErrorText := '';
  ResolvedDir := '';

  TargetDir := Trim(RequestedDir);
  if TargetDir = '' then
    TargetDir := DefaultOutputDir;

  if TargetDir = '' then
  begin
    ErrorText := 'Unable to resolve user home directory for default output folder.';
    Exit(False);
  end;

  if fpStat(PChar(TargetDir), Info) <> 0 then
  begin
    if fpgeterrno = ESysENOENT then
    begin
      if not ForceDirectories(TargetDir) then
      begin
        ErrorText := 'Failed to create output directory: ' + TargetDir;
        Exit(False);
      end;

      if fpStat(PChar(TargetDir), Info) <> 0 then
      begin
        ErrorText := 'Output directory is not accessible after creation: ' + TargetDir;
        Exit(False);
      end;
    end
    else
    begin
      ErrorText := 'Cannot stat output directory: ' + TargetDir;
      Exit(False);
    end;
  end;

  if not FPS_ISDIR(Info.st_mode) then
  begin
    ErrorText := 'Output path is not a directory: ' + TargetDir;
    Exit(False);
  end;

  if fpAccess(PChar(TargetDir), W_OK) <> 0 then
  begin
    ErrorText := 'Output directory is not writable: ' + TargetDir;
    Exit(False);
  end;

  ResolvedDir := TargetDir;
  Result := True;
end;

end.
