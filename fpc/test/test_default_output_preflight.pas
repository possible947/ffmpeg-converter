program test_default_output_preflight;

{$mode objfpc}{$H+}

uses
  SysUtils,
  fs_utils;

var
  OutDir: string;
  ErrText: string;
begin
  if not EnsureOutputDirWritable('', OutDir, ErrText) then
  begin
    WriteLn('FAIL: default output preflight failed: ', ErrText);
    Halt(1);
  end;

  if Pos('ffmpeg_converter', OutDir) = 0 then
  begin
    WriteLn('FAIL: default output directory name mismatch: ', OutDir);
    Halt(1);
  end;

  if not DirectoryExists(OutDir) then
  begin
    WriteLn('FAIL: default output directory does not exist: ', OutDir);
    Halt(1);
  end;

  WriteLn('OK: default output dir preflight -> ', OutDir);
end.
