unit fs_utils;

{$mode objfpc}{$H+}

interface

function FileReadable(const Path: string): Boolean;
function FileRegular(const Path: string): Boolean;
function DirWritable(const Path: string): Boolean;

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

end.
