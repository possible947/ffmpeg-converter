program test_cmd_builder;

{$mode objfpc}{$H+}

uses
  SysUtils,
  converter_types,
  converter_cmd_builder;

var
  Opts: TConvertOptions;
  Cmd: string;
  PrefixOk: Boolean;

begin
  InitDefaultOptions(Opts);
  Cmd := BuildFfmpegCommand(Opts, 'input.mov', 'output.mov');

  PrefixOk := ((Pos('"', Cmd) = 1) and ((Pos('" -n -i ', Cmd) > 1) or (Pos('" -y -i ', Cmd) > 1))) or
              (Pos('ffmpeg -n -i ', Cmd) = 1) or
              (Pos('ffmpeg -y -i ', Cmd) = 1);

  if not PrefixOk then
  begin
    WriteLn('FAIL: command prefix');
    WriteLn('CMD: ', Cmd);
    Halt(1);
  end;

  WriteLn('OK: ', Cmd);
end.
