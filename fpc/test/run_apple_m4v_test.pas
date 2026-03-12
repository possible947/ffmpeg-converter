program run_apple_m4v_test;

{$mode objfpc}{$H+}

uses
  SysUtils,
  apple_m4v_creator;

var
  Opts: TAppleM4VOptions;
  Err: string;
begin
  if ParamCount < 2 then
  begin
    WriteLn('usage: run_apple_m4v_test <in> <out>');
    Halt(2);
  end;

  Opts := DefaultAppleM4VOptions;
  if not CreateAppleM4V(ParamStr(1), ParamStr(2), Opts, Err) then
  begin
    WriteLn('FAIL: ', Err);
    Halt(1);
  end;

  WriteLn('OK: ', ParamStr(2));
end.
