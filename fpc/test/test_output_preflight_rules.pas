program test_output_preflight_rules;

{$mode objfpc}{$H+}

uses
  SysUtils,
  fs_utils;

var
  TmpBase: string;
  GoodDir: string;
  BadPath: string;
  OutDir: string;
  ErrText: string;
  F: TextFile;
begin
  TmpBase := IncludeTrailingPathDelimiter(GetTempDir(False)) + 'ffc_preflight_test';
  if DirectoryExists(TmpBase) then
    DeleteFile(TmpBase + '/not_a_dir_marker');
  ForceDirectories(TmpBase);

  GoodDir := IncludeTrailingPathDelimiter(TmpBase) + 'nested/output';
  if not EnsureOutputDirWritable(GoodDir, OutDir, ErrText) then
  begin
    WriteLn('FAIL: create+writable preflight failed: ', ErrText);
    Halt(1);
  end;

  if OutDir <> GoodDir then
  begin
    WriteLn('FAIL: resolved output mismatch: ', OutDir, ' <> ', GoodDir);
    Halt(1);
  end;

  BadPath := IncludeTrailingPathDelimiter(TmpBase) + 'not_a_dir_marker';
  AssignFile(F, BadPath);
  Rewrite(F);
  WriteLn(F, 'marker');
  CloseFile(F);

  if EnsureOutputDirWritable(BadPath, OutDir, ErrText) then
  begin
    WriteLn('FAIL: preflight accepted file path as directory: ', BadPath);
    Halt(1);
  end;

  WriteLn('OK: output preflight rules');
end.
