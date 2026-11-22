program test_windows_bundle_resolution;

{$mode objfpc}{$H+}

uses
  SysUtils,
  Windows,
  tool_paths,
  windows_mkvmerge;

procedure SetOverride(const Name, Value: string);
var
  NameA: AnsiString;
  ValueA: AnsiString;
begin
  NameA := AnsiString(Name);
  ValueA := AnsiString(Value);
  if not SetEnvironmentVariableA(PAnsiChar(NameA), PAnsiChar(ValueA)) then
    raise Exception.CreateFmt('Could not set test override %s.', [Name]);
end;

procedure CheckBundledPath(const ToolName, ActualPath: string);
var
  ExpectedPath: string;
begin
  ExpectedPath := IncludeTrailingPathDelimiter(
    ExtractFilePath(ExpandFileName(ParamStr(0)))) + ToolName;
  if not SameText(ExpandFileName(ActualPath), ExpandFileName(ExpectedPath)) then
    raise Exception.CreateFmt(
      '%s resolved to "%s"; expected bundled file "%s".',
      [ToolName, ActualPath, ExpectedPath]);
end;

var
  DecoyPath: string;
begin
  if ParamCount <> 1 then
    raise Exception.Create('Pass an existing executable path as the override decoy.');

  DecoyPath := ExpandFileName(ParamStr(1));
  if not FileExists(DecoyPath) then
    raise Exception.CreateFmt('Override decoy does not exist: %s', [DecoyPath]);

  SetOverride('FFMPEG', DecoyPath);
  SetOverride('FFMPEG_BIN', DecoyPath);
  SetOverride('FFPROBE', DecoyPath);
  SetOverride('FFPROBE_BIN', DecoyPath);
  SetOverride('MKVMERGE', DecoyPath);
  SetOverride('MKVMERGE_BIN', DecoyPath);
  SetOverride('MP4BOX', DecoyPath);
  SetOverride('MP4BOX_BIN', DecoyPath);

  CheckBundledPath('ffmpeg.exe', ResolveFfmpegBin);
  CheckBundledPath('ffprobe.exe', ResolveFfprobeBin);
  CheckBundledPath('mkvmerge.exe', ResolveMkvmergeBin);
  CheckBundledPath('mkvmerge.exe', FindMkvmergeBin);
  CheckBundledPath('MP4Box.exe', ResolveMp4BoxBin);

  WriteLn('PASS: bundled tools take priority over environment overrides.');
end.
