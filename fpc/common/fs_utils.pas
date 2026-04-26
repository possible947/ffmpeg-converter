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
  {$IFNDEF Windows}
  BaseUnix,
  {$ENDIF}
  SysUtils;

function FileReadable(const Path: string): Boolean;
begin
{$IFDEF Windows}
  Result := (Path <> '') and FileExists(Path);
{$ELSE}
  Result := (fpAccess(PChar(Path), R_OK) = 0);
{$ENDIF}
end;

function FileRegular(const Path: string): Boolean;
{$IFNDEF Windows}
var
  Info: Stat;
{$ELSE}
var
  Attr: Integer;
{$ENDIF}
begin
{$IFDEF Windows}
  if Path = '' then
    Exit(False);
  Attr := FileGetAttr(Path);
  Result := (Attr <> -1) and ((Attr and faDirectory) = 0);
{$ELSE}
  if fpStat(PChar(Path), Info) <> 0 then
    Exit(False);
  Result := FPS_ISREG(Info.st_mode);
{$ENDIF}
end;

function DirWritable(const Path: string): Boolean;
var
{$IFDEF Windows}
  TmpFile: string;
  F: Integer;
{$ELSE}
  Info: Stat;
{$ENDIF}
begin
{$IFDEF Windows}
  if (Path = '') or not DirectoryExists(Path) then
    Exit(False);
  TmpFile := IncludeTrailingPathDelimiter(Path) + '.ffc_write_test';
  F := FileCreate(TmpFile);
  if F < 0 then
    Exit(False);
  FileClose(F);
  SysUtils.DeleteFile(TmpFile);
  Result := True;
{$ELSE}
  if fpStat(PChar(Path), Info) <> 0 then
    Exit(False);
  if not FPS_ISDIR(Info.st_mode) then
    Exit(False);
  Result := fpAccess(PChar(Path), W_OK) = 0;
{$ENDIF}
end;

function ResolveUserHomeDir: string;
var
  AppCfgDir: string;
begin
{$IFDEF Windows}
  Result := Trim(GetEnvironmentVariable('USERPROFILE'));
  if Result <> '' then
    Exit;
  Result := Trim(GetEnvironmentVariable('HOMEDRIVE')) + Trim(GetEnvironmentVariable('HOMEPATH'));
  if Result <> '' then
    Exit;
{$ELSE}
  Result := Trim(GetEnvironmentVariable('HOME'));
  if Result <> '' then
    Exit;
{$ENDIF}
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
{$IFNDEF Windows}
  Info: Stat;
{$ELSE}
  Attr: Integer;
{$ENDIF}
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

{$IFDEF Windows}
  Attr := FileGetAttr(TargetDir);
  if Attr = -1 then
  begin
    if not ForceDirectories(TargetDir) then
    begin
      ErrorText := 'Failed to create output directory: ' + TargetDir;
      Exit(False);
    end;
    Attr := FileGetAttr(TargetDir);
    if Attr = -1 then
    begin
      ErrorText := 'Output directory is not accessible after creation: ' + TargetDir;
      Exit(False);
    end;
  end;

  if (Attr and faDirectory) = 0 then
  begin
    ErrorText := 'Output path is not a directory: ' + TargetDir;
    Exit(False);
  end;

  ResolvedDir := TargetDir;
  Result := True;
{$ELSE}
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
{$ENDIF}
end;

end.
