unit process_utils;

{$mode objfpc}{$H+}

interface

type
  TRunResult = record
    ExitCode: LongInt;
    OutputText: string;
  end;

  TCommandErrorLog = record
    CommandLine: string;
    StdOutErr: string;
    ExitCode: LongInt;
    FfmpegBin: string;
    FfprobeBin: string;
    InputFile: string;
    OutputFile: string;
    WorkingDir: string;
    PathValue: string;
    ContextNote: string;
  end;

function RunCommandCapture(const CommandLine: string): TRunResult;
function BuildErrorLogFileName: string;
function ProgramDirectory: string;
function WriteCommandErrorLog(const Info: TCommandErrorLog; const PreferredDir, FallbackDir: string;
  out SavedPath: string; out SaveError: string): Boolean;

implementation

uses
  BaseUnix,
  Classes,
  Process,
  SysUtils;

function BuildErrorLogFileName: string;
begin
  Result := 'ffc_error_' + FormatDateTime('yyyymmdd_hhnnss', Now) + '.log';
end;

function ProgramDirectory: string;
var
  ExePath: string;
begin
  ExePath := ExpandFileName(ParamStr(0));
  Result := ExtractFileDir(ExePath);
  if Result = '' then
    Result := GetCurrentDir;
end;

function BuildErrorLogContent(const Info: TCommandErrorLog): string;
begin
  Result :=
    'timestamp=' + FormatDateTime('yyyy-mm-dd hh:nn:ss', Now) + LineEnding +
    'exit_code=' + IntToStr(Info.ExitCode) + LineEnding +
    'ffmpeg_bin=' + Info.FfmpegBin + LineEnding +
    'ffprobe_bin=' + Info.FfprobeBin + LineEnding +
    'path=' + Info.PathValue + LineEnding +
    'working_dir=' + Info.WorkingDir + LineEnding +
    'input_file=' + Info.InputFile + LineEnding +
    'output_file=' + Info.OutputFile + LineEnding +
    'context=' + Info.ContextNote + LineEnding +
    'command_line=' + Info.CommandLine + LineEnding +
    '----- process output begin -----' + LineEnding +
    Info.StdOutErr + LineEnding +
    '----- process output end -----' + LineEnding;
end;

function TryWriteTextFile(const FilePath, Content: string; out ErrorText: string): Boolean;
var
  F: TextFile;
begin
  Result := False;
  ErrorText := '';

  AssignFile(F, FilePath);
  try
    Rewrite(F);
    Write(F, Content);
    CloseFile(F);
    Result := True;
  except
    on E: Exception do
    begin
      ErrorText := E.Message;
      try
        CloseFile(F);
      except
      end;
    end;
  end;
end;

function CanWriteDir(const DirPath: string): Boolean;
begin
  Result := (DirPath <> '') and DirectoryExists(DirPath) and (fpAccess(PChar(DirPath), W_OK) = 0);
end;

function WriteCommandErrorLog(const Info: TCommandErrorLog; const PreferredDir, FallbackDir: string;
  out SavedPath: string; out SaveError: string): Boolean;
var
  FileName: string;
  Content: string;
  PrimaryPath: string;
  FallbackPath: string;
  ErrText: string;
begin
  Result := False;
  SavedPath := '';
  SaveError := '';

  FileName := BuildErrorLogFileName;
  Content := BuildErrorLogContent(Info);

  if CanWriteDir(PreferredDir) then
  begin
    PrimaryPath := IncludeTrailingPathDelimiter(PreferredDir) + FileName;
    if TryWriteTextFile(PrimaryPath, Content, ErrText) then
    begin
      SavedPath := PrimaryPath;
      Exit(True);
    end;
    SaveError := 'Failed writing primary log path ' + PrimaryPath + ': ' + ErrText;
  end
  else
    SaveError := 'Primary log directory is not writable: ' + PreferredDir;

  if CanWriteDir(FallbackDir) then
  begin
    FallbackPath := IncludeTrailingPathDelimiter(FallbackDir) + FileName;
    if TryWriteTextFile(FallbackPath, Content, ErrText) then
    begin
      SavedPath := FallbackPath;
      Exit(True);
    end;

    if SaveError <> '' then
      SaveError += LineEnding;
    SaveError += 'Failed writing fallback log path ' + FallbackPath + ': ' + ErrText;
  end
  else
  begin
    if SaveError <> '' then
      SaveError += LineEnding;
    SaveError += 'Fallback log directory is not writable: ' + FallbackDir;
  end;
end;

function RunCommandCapture(const CommandLine: string): TRunResult;
var
  P: TProcess;
  S: TStringStream;
begin
  Result.ExitCode := -1;
  Result.OutputText := '';

  P := TProcess.Create(nil);
  S := TStringStream.Create('');
  try
    P.Executable := '/bin/sh';
    P.Parameters.Add('-c');
    P.Parameters.Add(CommandLine);
    P.Options := [poUsePipes, poStderrToOutput, poWaitOnExit];

    P.Execute;
    S.CopyFrom(P.Output, 0);

    Result.ExitCode := P.ExitStatus;
    Result.OutputText := S.DataString;
  finally
    S.Free;
    P.Free;
  end;
end;

end.
