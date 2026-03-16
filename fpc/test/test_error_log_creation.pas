program test_error_log_creation;

{$mode objfpc}{$H+}

uses
  SysUtils,
  process_utils;

var
  Info: TCommandErrorLog;
  LogPath: string;
  SaveErr: string;
  TmpDir: string;
begin
  TmpDir := IncludeTrailingPathDelimiter(GetTempDir(False)) + 'ffc_error_log_test';
  ForceDirectories(TmpDir);

  FillChar(Info, SizeOf(Info), 0);
  Info.CommandLine := '/usr/bin/false -arg value';
  Info.StdOutErr := 'simulated stderr/stdout';
  Info.ExitCode := 1;
  Info.FfmpegBin := '/usr/bin/ffmpeg';
  Info.FfprobeBin := '/usr/bin/ffprobe';
  Info.InputFile := '/tmp/in.mov';
  Info.OutputFile := '/tmp/out.mov';
  Info.WorkingDir := TmpDir;
  Info.PathValue := GetEnvironmentVariable('PATH');
  Info.ContextNote := 'synthetic ffmpeg failure';

  if not WriteCommandErrorLog(Info, TmpDir, TmpDir, LogPath, SaveErr) then
  begin
    WriteLn('FAIL: cannot write error log: ', SaveErr);
    Halt(1);
  end;

  if (LogPath = '') or (not FileExists(LogPath)) then
  begin
    WriteLn('FAIL: expected log file missing: ', LogPath);
    Halt(1);
  end;

  if Pos('ffc_error_', ExtractFileName(LogPath)) <> 1 then
  begin
    WriteLn('FAIL: unexpected log filename: ', ExtractFileName(LogPath));
    Halt(1);
  end;

  WriteLn('OK: error log created -> ', LogPath);
end.
