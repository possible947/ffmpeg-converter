unit windows_mkvmerge;

{$mode objfpc}{$H+}

interface

function FindMkvmergeBin: string;

implementation

uses
  SysUtils,
  process_utils
  {$IFDEF Windows}
  , Windows, windows_file_utils
  {$ENDIF}
  ;

function FindMkvmergeBin: string;
var
  CmdRes: TRunResult;
{$IFDEF Windows}
  EnvPath: string;
  SearchPath: string;
  BaseDir: string;
  Candidate: string;
  I: Integer;
  ExePathW: array[0..MAX_PATH] of WideChar;
{$ENDIF}
begin
  Result := '';

{$IFDEF Windows}
  { Explicit env override }
  EnvPath := Trim(SysUtils.GetEnvironmentVariable('MKVMERGE'));
  if (EnvPath = '') then
    EnvPath := Trim(SysUtils.GetEnvironmentVariable('MKVMERGE_BIN'));
  if (EnvPath <> '') and FileExists(EnvPath) then
  begin
    Result := EnvPath;
    Exit;
  end;

  { Check if mkvmerge.exe is in PATH using where.exe }
  CmdRes := RunCommandCapture('where mkvmerge.exe 2>nul');
  if CmdRes.ExitCode = 0 then
  begin
    Result := Trim(CmdRes.OutputText);
    { where may return multiple lines; take the first }
    if Pos(#13, Result) > 0 then
      Result := Copy(Result, 1, Pos(#13, Result) - 1);
    if Pos(#10, Result) > 0 then
      Result := Copy(Result, 1, Pos(#10, Result) - 1);
    Result := Trim(Result);
    if Result <> '' then
      Exit;
  end;

  { Check in application directory }
  FillChar(ExePathW, SizeOf(ExePathW), 0);
  GetModuleFileNameW(0, ExePathW, MAX_PATH);
  SearchPath := ExtractFilePath(WideToAnsi(WideString(ExePathW))) + 'mkvmerge.exe';
  if FileExists(SearchPath) then
  begin
    Result := SearchPath;
    Exit;
  end;

  { Check repository bundled directory relative to executable path:
    ..\..\src\platform\windows\bin\mkvmerge.exe (up to several levels). }
  BaseDir := ExpandFileName(ExtractFilePath(WideToAnsi(WideString(ExePathW))));
  for I := 1 to 8 do
  begin
    Candidate := IncludeTrailingPathDelimiter(BaseDir) + 'src\platform\windows\bin\mkvmerge.exe';
    if FileExists(Candidate) then
    begin
      Result := Candidate;
      Exit;
    end;
    BaseDir := ExpandFileName(IncludeTrailingPathDelimiter(BaseDir) + '..');
  end;
{$ELSE}
  { On non-Windows, use command -v to find mkvmerge }
  CmdRes := RunCommandCapture('command -v mkvmerge 2>/dev/null');
  if CmdRes.ExitCode = 0 then
  begin
    Result := Trim(CmdRes.OutputText);
    if FileExists(Result) then
      Exit;
  end;
{$ENDIF}

  Result := '';
end;

end.
