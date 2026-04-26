unit windows_utf8;

{$mode objfpc}{$H+}

interface

uses SysUtils;

function GetUTF8Arguments: TStringArray;

implementation

{$IFDEF Windows}
uses Windows;

{ CommandLineToArgvW is in shell32.dll and is not declared in FPC's Windows unit }
function CommandLineToArgvW(lpCmdLine: LPCWSTR; pNumArgs: PLongInt): PPWideChar;
  stdcall; external 'shell32.dll' name 'CommandLineToArgvW';
{$ENDIF}

function GetUTF8Arguments: TStringArray;
{$IFDEF Windows}
var
  WideCmd: WideString;
  WideArgv: PPWideChar;
  ArgCount, I, Len: Integer;
  ArgW: WideString;
{$ELSE}
var
  I: Integer;
{$ENDIF}
begin
  Result := nil;
{$IFDEF Windows}
  WideCmd := GetCommandLineW;
  WideArgv := CommandLineToArgvW(PWideChar(WideCmd), @ArgCount);
  if WideArgv = nil then
    Exit;

  try
    SetLength(Result, ArgCount);
    for I := 0 to ArgCount - 1 do
    begin
      ArgW := WideArgv[I];
      Len := WideCharToMultiByte(CP_UTF8, 0, PWideChar(ArgW), Length(ArgW), nil, 0, nil, nil);
      SetLength(Result[I], Len);
      if Len > 0 then
        WideCharToMultiByte(CP_UTF8, 0, PWideChar(ArgW), Length(ArgW),
                            PAnsiChar(Result[I]), Len, nil, nil);
    end;
  finally
    LocalFree(HLOCAL(WideArgv));
  end;
{$ELSE}
  SetLength(Result, ParamCount + 1);
  for I := 0 to ParamCount do
    Result[I] := ParamStr(I);
{$ENDIF}
end;

end.
