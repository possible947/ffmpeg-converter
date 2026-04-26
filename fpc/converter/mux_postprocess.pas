unit mux_postprocess;

{$mode objfpc}{$H+}

interface

uses converter_types;

function RunMuxPostprocess(const Opts: TConvertOptions;
                           const InputFile: string): TConverterError;

implementation

uses
  SysUtils,
  process_utils,
  path_utils,
  {$IFDEF Linux}
  tool_paths,
  {$ENDIF}
  {$IFDEF Windows}
  windows_mkvmerge,
  tool_paths,
  {$ENDIF}
  fs_utils;

function ArrToStr(const A: array of AnsiChar): string;
begin
  Result := StrPas(@A[0]);
end;

function RunMuxPostprocess(const Opts: TConvertOptions;
                           const InputFile: string): TConverterError;
var
  IntermediateFile: string;
  TempOutputFile: string;
  VideoTrack: string;
  MuxCmd: string;
  CmdRes: TRunResult;
  EffectiveOutputDir: string;
{$IFDEF Linux}
  MuxRate: string;
  ProbeCmd: string;
  Tools: TToolPaths;
{$ENDIF}
{$IFDEF Windows}
  MkvmergePath: string;
  MuxRate: string;
  ProbeCmd: string;
  Tools: TToolPaths;
{$ENDIF}
begin
{$IFDEF Linux}
  { Resolve effective output directory }
  EffectiveOutputDir := ArrToStr(Opts.output_dir);
  if EffectiveOutputDir = '' then
    EffectiveOutputDir := DefaultOutputDir;

  { Intermediate file is the result of the ffmpeg copy step }
  IntermediateFile := MakeOutputName(InputFile, 'copy', EffectiveOutputDir);
  if not FileExists(IntermediateFile) then
  begin
    WriteLn(StdErr, 'Error: intermediate file not found: ', IntermediateFile);
    Exit(ERR_INPUT_NOT_FOUND);
  end;

  { Video track file (provides the replacement video stream) }
  VideoTrack := ArrToStr(Opts.video_track_path);
  if not FileExists(VideoTrack) then
  begin
    WriteLn(StdErr, 'Error: video track file not found: ', VideoTrack);
    Exit(ERR_INVALID_OPTIONS);
  end;

  { Verify mkvmerge is available }
  Tools := ResolveToolPaths;
  if (Tools.MkvmergeBin = '') or (Tools.MkvmergeBin = 'mkvmerge') then
  begin
    CmdRes := RunCommandCapture('command -v mkvmerge 2>/dev/null');
    if CmdRes.ExitCode <> 0 then
    begin
      WriteLn(StdErr, 'Error: mkvmerge not found');
      Exit(ERR_INVALID_OPTIONS);
    end;
  end;

  { Use a temp output file to avoid modifying the intermediate file in-place }
  TempOutputFile := IntermediateFile + '.postmux.tmp.mkv';
  RunCommandCapture('rm -f ' + QuoteForShell(TempOutputFile));

  { Probe source frame rate for raw HEVC/H264 tracks (needed for --default-duration) }
  MuxRate := '';
  if (Pos('.hevc', LowerCase(VideoTrack)) > 0) or
     (Pos('.h265', LowerCase(VideoTrack)) > 0) or
     (Pos('.264', LowerCase(VideoTrack)) > 0) or
     (Pos('.h264', LowerCase(VideoTrack)) > 0) then
  begin
    ProbeCmd := QuoteForShell(Tools.FfprobeBin) +
      ' -v error -select_streams v:0 -show_entries stream=avg_frame_rate' +
      ' -of default=noprint_wrappers=1:nokey=1 ' +
      QuoteForShell(IntermediateFile) + ' 2>/dev/null';
    CmdRes := RunCommandCapture(ProbeCmd);
    if CmdRes.ExitCode = 0 then
    begin
      MuxRate := Trim(CmdRes.OutputText);
      if (MuxRate = '') or (MuxRate = '0/0') then
        MuxRate := '';
    end;
    if MuxRate = '' then
    begin
      WriteLn(StdErr, 'Error: could not probe source FPS from intermediate file');
      Exit(ERR_FFPROBE_FAILED);
    end;
  end;

  { Build mkvmerge command:
      video from VideoTrack, audio from IntermediateFile }
  MuxCmd := QuoteForShell(Tools.MkvmergeBin) + ' -o ' + QuoteForShell(TempOutputFile) +
            ' --no-audio --no-subtitles --no-buttons --no-attachments' +
            ' --no-chapters --no-global-tags --no-track-tags ';
  if MuxRate <> '' then
    MuxCmd += '--default-duration 0:' + MuxRate + 'fps ';
  MuxCmd += QuoteForShell(VideoTrack) +
            ' --no-video ' + QuoteForShell(IntermediateFile) + ' 2>&1';

  WriteLn('Running mux postprocess...');
  WriteLn('Command: ', MuxCmd);

  { Execute mkvmerge }
  CmdRes := RunCommandCapture(MuxCmd);
  if CmdRes.ExitCode <> 0 then
  begin
    RunCommandCapture('rm -f ' + QuoteForShell(TempOutputFile));
    WriteLn(StdErr, 'Error: mkvmerge failed with exit code: ', CmdRes.ExitCode);
    if CmdRes.OutputText <> '' then
      WriteLn(StdErr, 'mkvmerge output: ', CmdRes.OutputText);
    Exit(ERR_FFMPEG_FAILED);
  end;

  { Validate that muxed output contains both video and audio streams }
  ProbeCmd := QuoteForShell(Tools.FfprobeBin) +
    ' -v error -show_entries stream=codec_type' +
    ' -of default=noprint_wrappers=1:nokey=1 ' +
    QuoteForShell(TempOutputFile) + ' 2>/dev/null';
  CmdRes := RunCommandCapture(ProbeCmd);
  if (CmdRes.ExitCode <> 0) or
     (Pos('video', CmdRes.OutputText) = 0) or
     (Pos('audio', CmdRes.OutputText) = 0) then
  begin
    RunCommandCapture('rm -f ' + QuoteForShell(TempOutputFile));
    WriteLn(StdErr, 'Error: mux output validation failed');
    Exit(ERR_FFPROBE_FAILED);
  end;

  { Replace the intermediate file with the validated muxed output }
  CmdRes := RunCommandCapture('mv -f ' + QuoteForShell(TempOutputFile) +
                              ' ' + QuoteForShell(IntermediateFile));
  if CmdRes.ExitCode <> 0 then
  begin
    RunCommandCapture('rm -f ' + QuoteForShell(TempOutputFile));
    WriteLn(StdErr, 'Error: could not move mux output into place');
    Exit(ERR_UNKNOWN);
  end;

  WriteLn('Mux successful: ', IntermediateFile);
  Result := ERR_OK;
{$ELSE}
{$IFDEF Windows}
  { Resolve effective output directory }
  EffectiveOutputDir := ArrToStr(Opts.output_dir);
  if EffectiveOutputDir = '' then
    EffectiveOutputDir := DefaultOutputDir;

  { Intermediate file is the result of the ffmpeg copy step }
  IntermediateFile := MakeOutputName(InputFile, 'copy', EffectiveOutputDir);
  if not FileExists(IntermediateFile) then
  begin
    WriteLn(StdErr, 'Error: intermediate file not found: ', IntermediateFile);
    Exit(ERR_INPUT_NOT_FOUND);
  end;

  { Video track file (provides the replacement video stream) }
  VideoTrack := ArrToStr(Opts.video_track_path);
  if not FileExists(VideoTrack) then
  begin
    WriteLn(StdErr, 'Error: video track file not found: ', VideoTrack);
    Exit(ERR_INVALID_OPTIONS);
  end;

  { Locate tools }
  Tools := ResolveToolPaths;
  MkvmergePath := FindMkvmergeBin;
  if MkvmergePath = '' then
  begin
    WriteLn(StdErr, 'Error: mkvmerge not found');
    Exit(ERR_INVALID_OPTIONS);
  end;

  TempOutputFile := IntermediateFile + '.postmux.tmp.mkv';
  if FileExists(TempOutputFile) then
    SysUtils.DeleteFile(TempOutputFile);

  { Probe source frame rate for raw HEVC/H264 tracks (needed for --default-duration) }
  MuxRate := '';
  if (Tools.FfprobeBin <> '') and
     ((Pos('.hevc', LowerCase(VideoTrack)) > 0) or
      (Pos('.h265', LowerCase(VideoTrack)) > 0) or
      (Pos('.264',  LowerCase(VideoTrack)) > 0) or
      (Pos('.h264', LowerCase(VideoTrack)) > 0)) then
  begin
    ProbeCmd := '"' + Tools.FfprobeBin + '"' +
      ' -v error -select_streams v:0 -show_entries stream=avg_frame_rate' +
      ' -of default=noprint_wrappers=1:nokey=1 "' +
      IntermediateFile + '" 2>nul';
    CmdRes := RunCommandCapture(ProbeCmd);
    if CmdRes.ExitCode = 0 then
    begin
      MuxRate := Trim(CmdRes.OutputText);
      if (MuxRate = '') or (MuxRate = '0/0') then
        MuxRate := '';
    end;
    if MuxRate = '' then
    begin
      WriteLn(StdErr, 'Error: could not probe source FPS from intermediate file');
      Exit(ERR_FFPROBE_FAILED);
    end;
  end;

  { Build mkvmerge command for Windows:
      video from VideoTrack, audio from IntermediateFile }
  MuxCmd := '"' + MkvmergePath + '" -o "' + TempOutputFile + '"' +
            ' --no-audio --no-subtitles --no-buttons --no-attachments' +
            ' --no-chapters --no-global-tags --no-track-tags';
  if MuxRate <> '' then
    MuxCmd := MuxCmd + ' --default-duration 0:' + MuxRate + 'fps';
  MuxCmd := MuxCmd + ' "' + VideoTrack + '"' +
            ' --no-video "' + IntermediateFile + '"';

  WriteLn('Running mux postprocess...');
  WriteLn('Command: ', MuxCmd);

  { Execute mkvmerge }
  CmdRes := RunCommandCapture(MuxCmd);
  if CmdRes.ExitCode <> 0 then
  begin
    if FileExists(TempOutputFile) then
      SysUtils.DeleteFile(TempOutputFile);
    WriteLn(StdErr, 'Error: mkvmerge failed with exit code: ', CmdRes.ExitCode);
    if CmdRes.OutputText <> '' then
      WriteLn(StdErr, 'mkvmerge output: ', CmdRes.OutputText);
    Exit(ERR_FFMPEG_FAILED);
  end;

  { Validate that muxed output contains both video and audio streams }
  if Tools.FfprobeBin <> '' then
  begin
    ProbeCmd := '"' + Tools.FfprobeBin + '"' +
      ' -v error -show_entries stream=codec_type' +
      ' -of default=noprint_wrappers=1:nokey=1 "' +
      TempOutputFile + '" 2>nul';
    CmdRes := RunCommandCapture(ProbeCmd);
    if (CmdRes.ExitCode <> 0) or
       (Pos('video', CmdRes.OutputText) = 0) or
       (Pos('audio', CmdRes.OutputText) = 0) then
    begin
      if FileExists(TempOutputFile) then
        SysUtils.DeleteFile(TempOutputFile);
      WriteLn(StdErr, 'Error: mux output validation failed');
      Exit(ERR_FFPROBE_FAILED);
    end;
  end;

  { Replace the intermediate file with the validated muxed output }
  if FileExists(IntermediateFile) then
    SysUtils.DeleteFile(IntermediateFile);
  if not RenameFile(TempOutputFile, IntermediateFile) then
  begin
    if FileExists(TempOutputFile) then
      SysUtils.DeleteFile(TempOutputFile);
    WriteLn(StdErr, 'Error: could not move mux output into place');
    Exit(ERR_UNKNOWN);
  end;

  WriteLn('Mux successful: ', IntermediateFile);
  Result := ERR_OK;
{$ELSE}
  WriteLn(StdErr, 'Error: mux postprocess is only supported on Linux and Windows');
  Result := ERR_INVALID_OPTIONS;
{$ENDIF}
{$ENDIF}
end;

end.
