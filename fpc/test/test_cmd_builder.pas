program test_cmd_builder;

{$mode objfpc}{$H+}

uses
  SysUtils,
  converter_types,
  converter_cmd_builder;

var
  Opts: TConvertOptions;
  Cmd: string;

begin
  InitDefaultOptions(Opts);
  Cmd := BuildFfmpegCommand(Opts, 'input.mov', 'output.mov');

  if Pos('ffmpeg -i', Cmd) <> 1 then
  begin
    WriteLn('FAIL: command prefix');
    Halt(1);
  end;

  WriteLn('OK: ', Cmd);
end.
