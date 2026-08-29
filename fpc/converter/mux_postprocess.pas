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
  apple_m4v_creator,
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

procedure SafeWriteLn(const S: string);
begin
  try
    WriteLn(S);
  except
  end;
end;

procedure SafeWriteErr(const S: string);
begin
  try
    WriteLn(StdErr, S);
  except
  end;
end;

function BuildUniqueTempOutputPath(const OutputFile: string): string;
var
  I: Integer;
  Candidate: string;
begin
  Result := '';
  if OutputFile = '' then
    Exit;

  Randomize;
  for I := 0 to 31 do
  begin
    Candidate := OutputFile + '.postmux.' + IntToStr(GetProcessID) + '.' +
      IntToStr(Random(1000000)) + '.' + IntToStr(I) + '.tmp.mkv';
    if not FileExists(Candidate) then
      Exit(Candidate);
  end;
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
  Preset: string;
  FinalOutputFile: string;
  RemuxCmd: string;
  M4VOpts: TAppleM4VOptions;
  M4VErrorText: string;
{$IFDEF Linux}
  MuxRate: string;
  MuxLang: string;
  ProbeCmd: string;
  Tools: TToolPaths;
{$ENDIF}
{$IFDEF Windows}
  MkvmergePath: string;
  MuxRate: string;
  MuxLang: string;
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
    SafeWriteErr('Error: intermediate file not found: ' + IntermediateFile);
    Exit(ERR_INPUT_NOT_FOUND);
  end;

  { Video track file (provides the replacement video stream) }
  VideoTrack := ArrToStr(Opts.video_track_path);
  if not FileExists(VideoTrack) then
  begin
    SafeWriteErr('Error: video track file not found: ' + VideoTrack);
    Exit(ERR_INVALID_OPTIONS);
  end;

  { Verify mkvmerge is available }
  Tools := ResolveToolPaths;
  if (Tools.MkvmergeBin = '') or (Tools.MkvmergeBin = 'mkvmerge') then
  begin
    CmdRes := RunCommandCapture('command -v mkvmerge 2>/dev/null');
    if CmdRes.ExitCode <> 0 then
    begin
      SafeWriteErr('Error: mkvmerge not found');
      Exit(ERR_INVALID_OPTIONS);
    end;
  end;

  { Use a temp output file to avoid modifying the intermediate file in-place }
  TempOutputFile := BuildUniqueTempOutputPath(IntermediateFile);
  if TempOutputFile = '' then
  begin
    SafeWriteErr('Error: could not allocate temporary mux output path');
    Exit(ERR_INVALID_OPTIONS);
  end;
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
      SafeWriteErr('Error: could not probe source FPS from intermediate file');
      Exit(ERR_FFPROBE_FAILED);
    end;
  end;

  { The replacement video track is usually a raw elementary stream with no
    language tag of its own, so inherit it from the original source. }
  MuxLang := '';
  ProbeCmd := QuoteForShell(Tools.FfprobeBin) +
    ' -v error -select_streams v:0 -show_entries stream_tags=language' +
    ' -of default=noprint_wrappers=1:nokey=1 ' +
    QuoteForShell(IntermediateFile) + ' 2>/dev/null';
  CmdRes := RunCommandCapture(ProbeCmd);
  if CmdRes.ExitCode = 0 then
    MuxLang := Trim(CmdRes.OutputText);

  { Build mkvmerge command:
      video track 0 from VideoTrack, non-video tracks from IntermediateFile }
  MuxCmd := QuoteForShell(Tools.MkvmergeBin) + ' -o ' + QuoteForShell(TempOutputFile) +
            ' --no-audio --no-subtitles --no-buttons --no-attachments' +
            ' --no-chapters --no-global-tags --no-track-tags ';
  if MuxRate <> '' then
    MuxCmd += '--default-duration 0:' + MuxRate + 'fps ';
  if MuxLang <> '' then
    MuxCmd += '--language 0:' + MuxLang + ' ';
  MuxCmd += '--video-tracks 0 ' + QuoteForShell(VideoTrack) +
            ' --no-video ' + QuoteForShell(IntermediateFile) + ' 2>&1';

  SafeWriteLn('Running mux postprocess...');
  SafeWriteLn('Command: ' + MuxCmd);

  { Execute mkvmerge }
  CmdRes := RunCommandCapture(MuxCmd);
  if CmdRes.ExitCode <> 0 then
  begin
    RunCommandCapture('rm -f ' + QuoteForShell(TempOutputFile));
    SafeWriteErr('Error: mkvmerge failed with exit code: ' + IntToStr(CmdRes.ExitCode));
    if CmdRes.OutputText <> '' then
      SafeWriteErr('mkvmerge output: ' + CmdRes.OutputText);
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
    SafeWriteErr('Error: mux output validation failed');
    Exit(ERR_FFPROBE_FAILED);
  end;

  { Replace the intermediate file with the validated muxed output }
  CmdRes := RunCommandCapture('mv -f ' + QuoteForShell(TempOutputFile) +
                              ' ' + QuoteForShell(IntermediateFile));
  if CmdRes.ExitCode <> 0 then
  begin
    RunCommandCapture('rm -f ' + QuoteForShell(TempOutputFile));
    SafeWriteErr('Error: could not move mux output into place');
    Exit(ERR_UNKNOWN);
  end;

  SafeWriteLn('Mux successful: ' + IntermediateFile);

  { The mkvmerge step above always produces an .mkv. For 'mov'/'m4v' presets,
    convert that merged file into the correct final container; the default
    'mkv' preset keeps IntermediateFile as the final output unchanged. }
  Preset := ArrToStr(Opts.preset);
  if Preset = 'mov' then
  begin
    FinalOutputFile := MakeOutputName(InputFile, 'mux', EffectiveOutputDir, Preset);
    RemuxCmd := QuoteForShell(Tools.FfmpegBin) + ' -y -nostdin -i ' + QuoteForShell(IntermediateFile) +
                ' -c copy -f mov ' + QuoteForShell(FinalOutputFile) + ' 2>&1';
    SafeWriteLn('Running mux container remux (mov)...');
    CmdRes := RunCommandCapture(RemuxCmd);
    if CmdRes.ExitCode <> 0 then
    begin
      SafeWriteErr('Error: mov remux failed: ' + CmdRes.OutputText);
      Exit(ERR_FFMPEG_FAILED);
    end;
    SysUtils.DeleteFile(IntermediateFile);
  end
  else if Preset = 'm4v' then
  begin
    FinalOutputFile := MakeOutputName(InputFile, 'mux', EffectiveOutputDir, Preset);
    M4VOpts := DefaultAppleM4VOptions;
    if not CreateAppleM4V(IntermediateFile, FinalOutputFile, M4VOpts, M4VErrorText) then
    begin
      SafeWriteErr('Error: Apple M4V pipeline failed: ' + M4VErrorText);
      Exit(ERR_FFMPEG_FAILED);
    end;
    SysUtils.DeleteFile(IntermediateFile);
  end;

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
    SafeWriteErr('Error: intermediate file not found: ' + IntermediateFile);
    Exit(ERR_INPUT_NOT_FOUND);
  end;

  { Video track file (provides the replacement video stream) }
  VideoTrack := ArrToStr(Opts.video_track_path);
  if not FileExists(VideoTrack) then
  begin
    SafeWriteErr('Error: video track file not found: ' + VideoTrack);
    Exit(ERR_INVALID_OPTIONS);
  end;

  { Locate tools }
  Tools := ResolveToolPaths;
  MkvmergePath := Tools.MkvmergeBin;
  if (MkvmergePath = '') or (LowerCase(MkvmergePath) = 'mkvmerge') then
    MkvmergePath := FindMkvmergeBin;
  if MkvmergePath = '' then
  begin
    SafeWriteErr('Error: mkvmerge not found');
    Exit(ERR_INVALID_OPTIONS);
  end;

  TempOutputFile := BuildUniqueTempOutputPath(IntermediateFile);
  if TempOutputFile = '' then
  begin
    SafeWriteErr('Error: could not allocate temporary mux output path');
    Exit(ERR_INVALID_OPTIONS);
  end;
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
      SafeWriteErr('Error: could not probe source FPS from intermediate file');
      Exit(ERR_FFPROBE_FAILED);
    end;
  end;

  { The replacement video track is usually a raw elementary stream with no
    language tag of its own, so inherit it from the original source. }
  MuxLang := '';
  if Tools.FfprobeBin <> '' then
  begin
    ProbeCmd := '"' + Tools.FfprobeBin + '"' +
      ' -v error -select_streams v:0 -show_entries stream_tags=language' +
      ' -of default=noprint_wrappers=1:nokey=1 "' +
      IntermediateFile + '" 2>nul';
    CmdRes := RunCommandCapture(ProbeCmd);
    if CmdRes.ExitCode = 0 then
      MuxLang := Trim(CmdRes.OutputText);
  end;

  { Build mkvmerge command for Windows:
      video track 0 from VideoTrack, non-video tracks from IntermediateFile }
  MuxCmd := '"' + MkvmergePath + '" -o "' + TempOutputFile + '"' +
            ' --no-audio --no-subtitles --no-buttons --no-attachments' +
            ' --no-chapters --no-global-tags --no-track-tags';
  if MuxRate <> '' then
    MuxCmd := MuxCmd + ' --default-duration 0:' + MuxRate + 'fps';
  if MuxLang <> '' then
    MuxCmd := MuxCmd + ' --language 0:' + MuxLang;
  MuxCmd := MuxCmd + ' --video-tracks 0 "' + VideoTrack + '"' +
            ' --no-video "' + IntermediateFile + '"';

  SafeWriteLn('Running mux postprocess...');
  SafeWriteLn('Command: ' + MuxCmd);

  { Execute mkvmerge }
  CmdRes := RunCommandCapture(MuxCmd);
  if CmdRes.ExitCode <> 0 then
  begin
    if FileExists(TempOutputFile) then
      SysUtils.DeleteFile(TempOutputFile);
    SafeWriteErr('Error: mkvmerge failed with exit code: ' + IntToStr(CmdRes.ExitCode));
    if CmdRes.OutputText <> '' then
      SafeWriteErr('mkvmerge output: ' + CmdRes.OutputText);
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
      SafeWriteErr('Error: mux output validation failed');
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
    SafeWriteErr('Error: could not move mux output into place');
    Exit(ERR_UNKNOWN);
  end;

  SafeWriteLn('Mux successful: ' + IntermediateFile);

  { The mkvmerge step above always produces an .mkv. For 'mov'/'m4v' presets,
    convert that merged file into the correct final container; the default
    'mkv' preset keeps IntermediateFile as the final output unchanged. }
  Preset := ArrToStr(Opts.preset);
  if Preset = 'mov' then
  begin
    FinalOutputFile := MakeOutputName(InputFile, 'mux', EffectiveOutputDir, Preset);
    RemuxCmd := QuoteForShell(Tools.FfmpegBin) + ' -y -nostdin -i ' + QuoteForShell(IntermediateFile) +
                ' -c copy -f mov ' + QuoteForShell(FinalOutputFile) + ' 2>&1';
    SafeWriteLn('Running mux container remux (mov)...');
    CmdRes := RunCommandCapture(RemuxCmd);
    if CmdRes.ExitCode <> 0 then
    begin
      SafeWriteErr('Error: mov remux failed: ' + CmdRes.OutputText);
      Exit(ERR_FFMPEG_FAILED);
    end;
    SysUtils.DeleteFile(IntermediateFile);
  end
  else if Preset = 'm4v' then
  begin
    FinalOutputFile := MakeOutputName(InputFile, 'mux', EffectiveOutputDir, Preset);
    M4VOpts := DefaultAppleM4VOptions;
    if not CreateAppleM4V(IntermediateFile, FinalOutputFile, M4VOpts, M4VErrorText) then
    begin
      SafeWriteErr('Error: Apple M4V pipeline failed: ' + M4VErrorText);
      Exit(ERR_FFMPEG_FAILED);
    end;
    SysUtils.DeleteFile(IntermediateFile);
  end;

  Result := ERR_OK;
{$ELSE}
  SafeWriteErr('Error: mux postprocess is only supported on Linux and Windows');
  Result := ERR_INVALID_OPTIONS;
{$ENDIF}
{$ENDIF}
end;

end.
