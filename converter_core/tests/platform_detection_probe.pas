program platform_detection_probe;
// Prints the result of PlatformDetection as KEY=VALUE lines for
// converter_core/tests/run_platform_detection_tests.sh.

{$mode objfpc}{$H+}

uses
  converter_core_types, platform_detection;

var
  List: TPlatformlist;
  Platforms: string;
  I: integer;
begin
  List := TPlatformlist.Create;
  try
    PlatformDetection(List);
    Platforms := '';
    for I := 0 to List.Count - 1 do
    begin
      if I > 0 then
        Platforms := Platforms + ',';
      Platforms := Platforms + List.GetItem(I);
    end;
    WriteLn('PLATFORM=', DetectPlatform);
    WriteLn('MODULE=', GetCoreModulePath);
    WriteLn('TOOLS_DIR=', ResolveToolsDir);
    WriteLn('ALLOWED=', Ord(List.PlatformAllowed));
    WriteLn('ERROR=', List.ErrorMessage);
    WriteLn('PLATFORMS=', Platforms);
    WriteLn('FFMPEG=', List.Tools.FFmpegPath);
    WriteLn('FFPROBE=', List.Tools.FFprobePath);
    WriteLn('MKVMERGE=', List.Tools.MkvmergePath);
    WriteLn('MP4BOX=', List.Tools.mp4boxPath);
  finally
    List.Free;
  end;
end.
