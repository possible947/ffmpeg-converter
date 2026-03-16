program test_unified_tool_resolver;

{$mode objfpc}{$H+}

uses
  SysUtils,
  tool_paths,
  apple_m4v_creator;

var
  MainTools: TToolPaths;
  AppleFfmpeg: string;
  AppleFfprobe: string;
begin
  MainTools := ResolveToolPaths;
  if not ResolveAppleM4VTools(AppleFfmpeg, AppleFfprobe) then
  begin
    WriteLn('FAIL: Apple M4V tool resolver returned false.');
    Halt(1);
  end;

  if MainTools.FfmpegBin <> AppleFfmpeg then
  begin
    WriteLn('FAIL: ffmpeg resolver mismatch');
    WriteLn('main=', MainTools.FfmpegBin);
    WriteLn('apple=', AppleFfmpeg);
    Halt(1);
  end;

  if MainTools.FfprobeBin <> AppleFfprobe then
  begin
    WriteLn('FAIL: ffprobe resolver mismatch');
    WriteLn('main=', MainTools.FfprobeBin);
    WriteLn('apple=', AppleFfprobe);
    Halt(1);
  end;

  WriteLn('OK: unified resolver main/apple');
end.
