unit apple_m4v_creator;

{$mode objfpc}{$H+}
{$HINTS OFF}
{$WARN 5091 OFF}

interface

uses
  SysUtils;

type
  TAppleM4VOptions = record
    VideoTrackIndex: Integer;
    AudioTrackIndex: Integer;
    AacQuality: Integer;
    Ac3BitrateKbps: Integer;
    AudioLang: string;
    AddChapters: Boolean;
  end;

function DefaultAppleM4VOptions: TAppleM4VOptions;
function CreateAppleM4V(const InputFile, OutputFile: string; const Opts: TAppleM4VOptions; out ErrorText: string): Boolean;
function ResolveAppleM4VTools(out FfmpegBin, FfprobeBin: string): Boolean;

implementation

uses
  Classes,
  fs_utils,
  path_utils,
  process_utils,
  tool_paths;

function DefaultAppleM4VOptions: TAppleM4VOptions;
begin
  Result.VideoTrackIndex := 0;
  Result.AudioTrackIndex := 0;
  Result.AacQuality := 2;
  Result.Ac3BitrateKbps := 640;
  Result.AudioLang := 'rus';
  Result.AddChapters := True;
end;

function ResolveAppleM4VTools(out FfmpegBin, FfprobeBin: string): Boolean;
var
  Tools: TToolPaths;
begin
  Tools := ResolveToolPaths;
  FfmpegBin := Tools.FfmpegBin;
  FfprobeBin := Tools.FfprobeBin;
  Result := (Trim(FfmpegBin) <> '') and (Trim(FfprobeBin) <> '');
end;

function ParseRateToFps(const RateStr: string): Double;
var
  S: string;
  P: SizeInt;
  N, D: Double;
  Code: Integer;
begin
  Result := 25.0;
  S := Trim(RateStr);
  if S = '' then
    Exit;

  P := Pos('/', S);
  if P <= 0 then
  begin
    Val(S, N, Code);
    if Code = 0 then
      Result := N;
    Exit;
  end;

  Val(Copy(S, 1, P - 1), N, Code);
  if Code <> 0 then
    Exit;

  Val(Copy(S, P + 1, MaxInt), D, Code);
  if (Code <> 0) or (D = 0) then
    Exit;

  Result := N / D;
end;

procedure LogFailedCommand(const Cmd: string; const R: TRunResult; const FfmpegBin, FfprobeBin,
  InputFile, OutputFile, FallbackLogDir, ContextNote: string);
var
  Tools: TToolPaths;
  LogInfo{%H-}: TCommandErrorLog;
  DummyPath: string;
  DummyError: string;
begin
  Tools := ResolveToolPaths;
  FillChar(LogInfo, SizeOf(LogInfo), 0);
  LogInfo.CommandLine := Cmd;
  LogInfo.StdOutErr := R.OutputText;
  LogInfo.ExitCode := R.ExitCode;
  LogInfo.FfmpegBin := FfmpegBin;
  if LogInfo.FfmpegBin = '' then
    LogInfo.FfmpegBin := Tools.FfmpegBin;
  LogInfo.FfprobeBin := FfprobeBin;
  if LogInfo.FfprobeBin = '' then
    LogInfo.FfprobeBin := Tools.FfprobeBin;
  LogInfo.InputFile := InputFile;
  LogInfo.OutputFile := OutputFile;
  LogInfo.WorkingDir := GetCurrentDir;
  LogInfo.PathValue := Tools.PathValue;
  LogInfo.ContextNote := ContextNote;
  WriteCommandErrorLog(LogInfo, ProgramDirectory, FallbackLogDir, DummyPath, DummyError);
end;

function ProbeFps(const FfprobeBin, InputFile, FallbackLogDir: string): Double;
var
  R: TRunResult;
  Rate: string;
  Cmd: string;
begin
  Cmd := QuoteForShell(FfprobeBin) +
    ' -v error -select_streams v:0 -show_entries stream=avg_frame_rate ' +
    '-of default=noprint_wrappers=1:nokey=1 ' + QuoteForShell(InputFile) +
{$IFDEF Windows}
    ' 2>NUL';
{$ELSE}
    ' 2>/dev/null';
{$ENDIF}
  R := RunCommandCapture(Cmd);
  if R.ExitCode <> 0 then
    LogFailedCommand(Cmd, R, '', FfprobeBin, InputFile, '', FallbackLogDir, 'ffprobe avg_frame_rate failed');

  Rate := Trim(R.OutputText);
  if (Rate = '') or (Rate = '0/0') then
  begin
    Cmd := QuoteForShell(FfprobeBin) +
      ' -v error -select_streams v:0 -show_entries stream=r_frame_rate ' +
      '-of default=noprint_wrappers=1:nokey=1 ' + QuoteForShell(InputFile) +
{$IFDEF Windows}
      ' 2>NUL';
{$ELSE}
      ' 2>/dev/null';
{$ENDIF}
    R := RunCommandCapture(Cmd);
    if R.ExitCode <> 0 then
      LogFailedCommand(Cmd, R, '', FfprobeBin, InputFile, '', FallbackLogDir, 'ffprobe r_frame_rate failed');
    Rate := Trim(R.OutputText);
  end;

  if (Rate = '') or (Rate = '0/0') then
    Exit(25.0);

  Result := ParseRateToFps(Rate);
end;

function CreateWorkDir(out WorkDir: string): Boolean;
var
  I: Integer;
  Candidate: string;
begin
  WorkDir := '';
  Randomize;
  for I := 1 to 20 do
  begin
    Candidate := IncludeTrailingPathDelimiter(GetTempDir(False)) +
      Format('m4v_mux_%d_%d', [GetProcessID, Random(1000000)]);
    if CreateDir(Candidate) then
    begin
      WorkDir := Candidate;
      Exit(True);
    end;
  end;
  Result := False;
end;

procedure CleanupWorkDir(const WorkDir: string);
begin
  if WorkDir = '' then
    Exit;
{$IFDEF Windows}
  RunCommandCapture('rmdir /s /q ' + QuoteForShell(WorkDir));
{$ELSE}
  RunCommandCapture('/bin/rm -rf ' + QuoteForShell(WorkDir));
{$ENDIF}
end;

function RunStep(const Cmd, ErrorPrefix, FfmpegBin, FfprobeBin, InputFile, OutputFile, FallbackLogDir: string;
  out ErrorText: string): Boolean;
var
  R: TRunResult;
  Tools: TToolPaths;
  LogInfo{%H-}: TCommandErrorLog;
  ErrorLogPath: string;
  ErrorLogNotice: string;
begin
  R := RunCommandCapture(Cmd);
  if R.ExitCode <> 0 then
  begin
    Tools := ResolveToolPaths;
    FillChar(LogInfo, SizeOf(LogInfo), 0);
    LogInfo.CommandLine := Cmd;
    LogInfo.StdOutErr := R.OutputText;
    LogInfo.ExitCode := R.ExitCode;
    LogInfo.FfmpegBin := FfmpegBin;
    if LogInfo.FfmpegBin = '' then
      LogInfo.FfmpegBin := Tools.FfmpegBin;
    LogInfo.FfprobeBin := FfprobeBin;
    if LogInfo.FfprobeBin = '' then
      LogInfo.FfprobeBin := Tools.FfprobeBin;
    LogInfo.InputFile := InputFile;
    LogInfo.OutputFile := OutputFile;
    LogInfo.WorkingDir := GetCurrentDir;
    LogInfo.PathValue := Tools.PathValue;
    LogInfo.ContextNote := ErrorPrefix;
    WriteCommandErrorLog(LogInfo, ProgramDirectory, FallbackLogDir, ErrorLogPath, ErrorLogNotice);

    ErrorText := ErrorPrefix + sLineBreak + Trim(R.OutputText);
    if ErrorLogPath <> '' then
      ErrorText += sLineBreak + 'Error log: ' + ErrorLogPath;
    if ErrorLogNotice <> '' then
      ErrorText += sLineBreak + ErrorLogNotice;
    Exit(False);
  end;
  Result := True;
end;

function CreateAppleM4V(const InputFile, OutputFile: string; const Opts: TAppleM4VOptions; out ErrorText: string): Boolean;
var
  FfmpegBin: string;
  FfprobeBin: string;
  Mp4BoxBin: string;
  Tools: TToolPaths;
  R: TRunResult;
  Fps: Double;
  FpsStr: string;
  Fmt: TFormatSettings;
  WorkDir: string;
  VideoMp4: string;
  AacM4a: string;
  Ac3Mp4: string;
  ChapteredM4V: string;
  Cmd: string;
  EffectiveOutputDir: string;
  EffectiveOutputFile: string;
  OutDirError: string;
begin
  Result := False;
  ErrorText := '';

  if not FileExists(InputFile) then
  begin
    ErrorText := 'Input file not found: ' + InputFile;
    Exit(False);
  end;

  Tools := ResolveToolPaths;
  FfmpegBin := Tools.FfmpegBin;
  FfprobeBin := Tools.FfprobeBin;
  Mp4BoxBin := Tools.Mp4BoxBin;

  if (FfmpegBin = '') or (FfprobeBin = '') then
  begin
    ErrorText := 'ffmpeg/ffprobe not found.';
    Exit(False);
  end;

  if Mp4BoxBin = '' then
  begin
    ErrorText := 'MP4Box not found. Install GPAC (sudo port install gpac) or place MP4Box in the app bundle.';
    Exit(False);
  end;

  EffectiveOutputDir := ExtractFileDir(OutputFile);
  if not EnsureOutputDirWritable(EffectiveOutputDir, EffectiveOutputDir, OutDirError) then
  begin
    ErrorText := 'Output preflight failed: ' + OutDirError;
    Exit(False);
  end;

  EffectiveOutputFile := IncludeTrailingPathDelimiter(EffectiveOutputDir) + ExtractFileName(OutputFile);

  Fps := ProbeFps(FfprobeBin, InputFile, EffectiveOutputDir);
  Fmt := DefaultFormatSettings;
  Fmt.DecimalSeparator := '.';
  FpsStr := Format('%.6f', [Fps], Fmt);

  if not CreateWorkDir(WorkDir) then
  begin
    ErrorText := 'Failed to create temporary work directory.';
    Exit(False);
  end;

  try
    VideoMp4 := IncludeTrailingPathDelimiter(WorkDir) + 'video_only.mp4';
    AacM4a := IncludeTrailingPathDelimiter(WorkDir) + 'audio_aac.m4a';
    Ac3Mp4 := IncludeTrailingPathDelimiter(WorkDir) + 'audio_ac3.mp4';
    ChapteredM4V := IncludeTrailingPathDelimiter(WorkDir) + 'with_chapters.m4v';

    Cmd := QuoteForShell(FfmpegBin) + ' -y -nostdin -i ' + QuoteForShell(InputFile) +
      Format(' -map 0:v:%d -c:v copy -an -sn -dn -f mp4 ', [Opts.VideoTrackIndex]) +
      QuoteForShell(VideoMp4);
    if not RunStep(Cmd, 'Video copy step failed.', FfmpegBin, FfprobeBin, InputFile, EffectiveOutputFile, EffectiveOutputDir, ErrorText) then
      Exit(False);

    Cmd := QuoteForShell(FfmpegBin) + ' -y -nostdin -i ' + QuoteForShell(InputFile) +
      Format(' -map 0:a:%d -c:a aac -profile:a aac_low -q:a %d -f mp4 ', [Opts.AudioTrackIndex, Opts.AacQuality]) +
      QuoteForShell(AacM4a);
    if not RunStep(Cmd, 'AAC encoding step failed.', FfmpegBin, FfprobeBin, InputFile, EffectiveOutputFile, EffectiveOutputDir, ErrorText) then
      Exit(False);

    Cmd := QuoteForShell(FfmpegBin) + ' -y -nostdin -i ' + QuoteForShell(InputFile) +
      Format(' -map 0:a:%d -c:a ac3 -b:a %dk -f mp4 ', [Opts.AudioTrackIndex, Opts.Ac3BitrateKbps]) +
      QuoteForShell(Ac3Mp4);
    if not RunStep(Cmd, 'AC3 encoding step failed.', FfmpegBin, FfprobeBin, InputFile, EffectiveOutputFile, EffectiveOutputDir, ErrorText) then
      Exit(False);

    Cmd := QuoteForShell(Mp4BoxBin) + ' -new -brand "M4V :0" -ab mp42 -ab isom ' +
      '-add ' + QuoteForShell(VideoMp4 + '#video:fps=' + FpsStr + ':name=Video') + ' ' +
      '-add ' + QuoteForShell(AacM4a + '#audio:name=AAC:lang=' + Opts.AudioLang) + ' ' +
      '-add ' + QuoteForShell(Ac3Mp4 + '#audio:name=AC3 ' + IntToStr(Opts.Ac3BitrateKbps) + 'k:lang=' + Opts.AudioLang) + ' ' +
      QuoteForShell(EffectiveOutputFile);
    if not RunStep(Cmd, 'MP4Box mux step failed.', FfmpegBin, FfprobeBin, InputFile, EffectiveOutputFile, EffectiveOutputDir, ErrorText) then
      Exit(False);

    if Opts.AddChapters then
    begin
      Cmd := QuoteForShell(FfmpegBin) + ' -y -nostdin -i ' + QuoteForShell(EffectiveOutputFile) +
        ' -i ' + QuoteForShell(InputFile) + ' -map 0 -map_chapters 1 -c copy ' +
        QuoteForShell(ChapteredM4V);
      R := RunCommandCapture(Cmd);
      if R.ExitCode = 0 then
      begin
        if FileExists(EffectiveOutputFile) then
          SysUtils.DeleteFile(EffectiveOutputFile);
        if not RenameFile(ChapteredM4V, EffectiveOutputFile) then
        begin
          if FileExists(ChapteredM4V) then
            SysUtils.DeleteFile(ChapteredM4V);
          WriteLn('Warning: Apple M4V chapters import completed but could not finalize output.');
        end;
      end
      else
      begin
        if FileExists(ChapteredM4V) then
          SysUtils.DeleteFile(ChapteredM4V);
        WriteLn('Warning: Apple M4V chapters import failed');
        LogFailedCommand(Cmd, R, FfmpegBin, FfprobeBin, InputFile, EffectiveOutputFile, EffectiveOutputDir,
          'Apple M4V chapters transfer failed');
      end;
    end;

    Result := True;
  finally
    CleanupWorkDir(WorkDir);
  end;
end;

end.
