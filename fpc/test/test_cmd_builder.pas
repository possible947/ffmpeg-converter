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

  if (Pos('"ffmpeg" -n -i ', Cmd) <> 1) and
     (Pos('"ffmpeg" -y -i ', Cmd) <> 1) and
     (Pos('"/usr/bin/ffmpeg" -n -i ', Cmd) <> 1) and
     (Pos('"/usr/bin/ffmpeg" -y -i ', Cmd) <> 1) and
     (Pos('"/usr/local/bin/ffmpeg" -n -i ', Cmd) <> 1) and
     (Pos('"/usr/local/bin/ffmpeg" -y -i ', Cmd) <> 1) and
     (Pos('"/opt/homebrew/bin/ffmpeg" -n -i ', Cmd) <> 1) and
     (Pos('"/opt/homebrew/bin/ffmpeg" -y -i ', Cmd) <> 1) then
  begin
    WriteLn('FAIL: command prefix');
    Halt(1);
  end;

  WriteLn('OK: ', Cmd);
end.
