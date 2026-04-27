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
  SearchPath: string;
  ExePathW: array[0..MAX_PATH] of WideChar;
{$ENDIF}
begin
  Result := '';

{$IFDEF Windows}
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
