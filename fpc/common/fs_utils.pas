unit fs_utils;

{$mode objfpc}{$H+}
{$WARN 5057 OFF}

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
  {$IFDEF Windows}
  Windows,
  {$ENDIF}
  SysUtils;

{$IFDEF Windows}
{ Convert UTF-8 string to UTF-16 (WideString) }
function UTF8ToUTF16(const UTF8Str: string): WideString;
var
  Len: LongInt;
  UCS2Str: PWideChar;
begin
  Result := '';
  if UTF8Str = '' then
    Exit;

  Len := MultiByteToWideChar(CP_UTF8, 0, @UTF8Str[1], Length(UTF8Str), nil, 0);
  if Len = 0 then
    Exit;

  GetMem(UCS2Str, (Len + 1) * SizeOf(WideChar));
  try
    MultiByteToWideChar(CP_UTF8, 0, @UTF8Str[1], Length(UTF8Str), UCS2Str, Len);
    UCS2Str[Len] := #0;
    Result := UCS2Str;
  finally
    FreeMem(UCS2Str);
  end;
end;

{ Check if UTF-8 file exists using Windows Unicode API }
function FileExistsUTF8(const Path: string): Boolean;
var
  WidePath: WideString;
  Attr: DWORD;
begin
  Result := False;
  if Path = '' then
    Exit;

  WidePath := UTF8ToUTF16(Path);
  if WidePath = '' then
    Exit;

  Attr := GetFileAttributesW(PWideChar(WidePath));
  Result := Attr <> INVALID_FILE_ATTRIBUTES;
end;

{ Get file attributes for UTF-8 filename using Windows Unicode API }
function FileGetAttrUTF8(const Path: string): LongInt;
var
  WidePath: WideString;
  Attr: DWORD;
begin
  Result := -1;
  if Path = '' then
    Exit;

  WidePath := UTF8ToUTF16(Path);
  if WidePath = '' then
    Exit;

  Attr := GetFileAttributesW(PWideChar(WidePath));
  if Attr <> INVALID_FILE_ATTRIBUTES then
    Result := LongInt(Attr);
end;
{$ENDIF}

function FileReadable(const Path: string): Boolean;
begin
{$IFDEF Windows}
  Result := (Path <> '') and FileExistsUTF8(Path);
{$ELSE}
  Result := (fpAccess(PChar(Path), R_OK) = 0);
{$ENDIF}
end;

function FileRegular(const Path: string): Boolean;
{$IFNDEF Windows}
var
  Info{%H-}: Stat;
{$ELSE}
var
  Attr: LongInt;
{$ENDIF}
begin
{$IFDEF Windows}
  if Path = '' then
    Exit(False);
  Attr := FileGetAttrUTF8(Path);
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
  Info{%H-}: Stat;
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
  Info{%H-}: Stat;
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
