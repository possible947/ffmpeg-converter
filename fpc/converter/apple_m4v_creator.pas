unit apple_m4v_creator;

{$mode objfpc}{$H+}

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
  fpjson,
  jsonparser,
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
  LogInfo: TCommandErrorLog;
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
  LogInfo: TCommandErrorLog;
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

function ParseFloatDot(const S: string; out V: Double): Boolean;
var
  Fmt: TFormatSettings;
begin
  Fmt := DefaultFormatSettings;
  Fmt.DecimalSeparator := '.';
  Result := TryStrToFloat(Trim(S), V, Fmt);
end;

function BuildChapterText(const ChaptersJsonFile, ChapterFile: string): Boolean;
var
  JsonText: string;
  J: TJSONData;
  Chapters: TJSONArray;
  I: Integer;
  ChObj, TagsObj: TJSONObject;
  StartTime: Double;
  Title: string;
  Lines: TStringList;
  H, M, S, Ms: Integer;
  T: Double;

  function ToHmsMs(const Seconds: Double): string;
  var
    Tmp: Double;
  begin
    Tmp := Seconds;
    if Tmp < 0 then
      Tmp := 0;

    H := Trunc(Tmp / 3600.0);
    Tmp := Tmp - H * 3600.0;
    M := Trunc(Tmp / 60.0);
    Tmp := Tmp - M * 60.0;
    S := Trunc(Tmp);
    Ms := Round((Tmp - S) * 1000.0);
    if Ms = 1000 then
    begin
      Inc(S);
      Ms := 0;
    end;
    Result := Format('%d:%.2d:%.2d.%.3d', [H, M, S, Ms]);
  end;
begin
  Result := False;
  J := nil;
  Lines := TStringList.Create;
  try
    if not FileExists(ChaptersJsonFile) then
      Exit(False);

    with TStringList.Create do
    try
      LoadFromFile(ChaptersJsonFile);
      JsonText := Text;
    finally
      Free;
    end;

    if Trim(JsonText) = '' then
      Exit(False);

    J := GetJSON(JsonText);
    if not (J is TJSONObject) then
      Exit(False);

    Chapters := TJSONObject(J).Arrays['chapters'];
    if (Chapters = nil) or (Chapters.Count = 0) then
      Exit(False);

    for I := 0 to Chapters.Count - 1 do
    begin
      if not (Chapters[I] is TJSONObject) then
        Continue;
      ChObj := TJSONObject(Chapters[I]);

      StartTime := 0.0;
      if ChObj.Find('start_time') <> nil then
      begin
        if not ParseFloatDot(ChObj.Get('start_time', '0'), T) then
          T := 0.0;
        StartTime := T;
      end;

      Title := Format('Chapter %d', [I + 1]);
      if (ChObj.Find('tags') <> nil) and (ChObj.Objects['tags'] <> nil) then
      begin
        TagsObj := ChObj.Objects['tags'];
        if TagsObj.Find('title') <> nil then
          Title := TagsObj.Get('title', Title);
      end;

      Lines.Add(ToHmsMs(StartTime) + ' ' + Title);
    end;

    if Lines.Count = 0 then
      Exit(False);

    Lines.SaveToFile(ChapterFile);
    Result := True;
  finally
    J.Free;
    Lines.Free;
  end;
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
  ChaptersJson: string;
  ChaptersTxt: string;
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
    ChaptersJson := IncludeTrailingPathDelimiter(WorkDir) + 'chapters.json';
    ChaptersTxt := IncludeTrailingPathDelimiter(WorkDir) + 'chapters.txt';

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
      Cmd := QuoteForShell(FfprobeBin) + ' -v error -print_format json -show_chapters ' +
        QuoteForShell(InputFile) + ' > ' + QuoteForShell(ChaptersJson);
      R := RunCommandCapture(Cmd);
      if R.ExitCode = 0 then
      begin
        if BuildChapterText(ChaptersJson, ChaptersTxt) and (FileExists(ChaptersTxt)) then
        begin
          Cmd := QuoteForShell(Mp4BoxBin) + ' -chap ' + QuoteForShell(ChaptersTxt) + ' ' + QuoteForShell(EffectiveOutputFile);
          RunCommandCapture(Cmd);
        end;
      end;
      if R.ExitCode <> 0 then
        LogFailedCommand(Cmd, R, FfmpegBin, FfprobeBin, InputFile, EffectiveOutputFile, EffectiveOutputDir,
          'ffprobe chapter extraction failed');
    end;

    Result := True;
  finally
    CleanupWorkDir(WorkDir);
  end;
end;

end.
