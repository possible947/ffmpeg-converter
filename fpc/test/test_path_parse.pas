program test_path_parse;

{$mode objfpc}{$H+}

uses
  SysUtils,
  path_utils;

begin
  if MakeOutputName('/tmp/a b/input.mov', 'copy', '/tmp/out') <> '/tmp/out/input_converted.mkv' then
  begin
    WriteLn('FAIL: output name mismatch');
    Halt(1);
  end;

  WriteLn('OK');
end.
