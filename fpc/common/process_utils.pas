unit process_utils;

{$mode objfpc}{$H+}
{$WARN 5057 OFF}
{$NOTES OFF}

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
  {$IFNDEF Windows}
  BaseUnix,
  {$ENDIF}
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
{$IFDEF Windows}
  Result := (DirPath <> '') and DirectoryExists(DirPath);
{$ELSE}
  Result := (DirPath <> '') and DirectoryExists(DirPath) and (fpAccess(PChar(DirPath), W_OK) = 0);
{$ENDIF}
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
{$IFDEF Windows}
  EffectiveCmd: string;
{$ENDIF}
  DataAvailable: Boolean;
  ReadCount: LongInt;
  ReadBuf{%H-}: array[0..4095] of Byte;
  Chunk: AnsiString;
  MaxWaitAttempts: Integer;
  WaitAttempts: Integer;
begin
  Result.ExitCode := -1;
  Result.OutputText := '';

  P := TProcess.Create(nil);
  S := TStringStream.Create('');
  try
{$IFDEF Windows}
    EffectiveCmd := Trim(CommandLine);
    if (EffectiveCmd <> '') and (EffectiveCmd[1] = '"') then
      EffectiveCmd := '"' + EffectiveCmd + '"';

    P.Executable := 'cmd.exe';
    P.Parameters.Add('/c');
    P.Parameters.Add(EffectiveCmd);
{$ELSE}
    P.Executable := '/bin/sh';
    P.Parameters.Add('-c');
    P.Parameters.Add(CommandLine);
{$ENDIF}
    { Use poUsePipes but NOT poWaitOnExit to avoid deadlock }
    P.Options := [poUsePipes, poStderrToOutput, poNoConsole];

    P.Execute;

    { Read output in a loop without waiting for process to exit first }
    MaxWaitAttempts := 1000;  { 10 second timeout at 10ms per iteration }
    WaitAttempts := 0;
    repeat
      DataAvailable := False;
      try
        while P.Output.NumBytesAvailable > 0 do
        begin
          ReadCount := P.Output.Read(ReadBuf, SizeOf(ReadBuf));
          if ReadCount > 0 then
          begin
            SetString(Chunk, PAnsiChar(@ReadBuf[0]), ReadCount);
            S.WriteString(Chunk);
            DataAvailable := True;
            WaitAttempts := 0;  { Reset counter when we get data }
          end;
        end;
      except
        { If reading fails, break out }
        break;
      end;

      { Check if process has finished }
      if not P.Running then
        break;

      { If no data available, wait a bit and try again }
      if not DataAvailable then
      begin
        Sleep(10);
        Inc(WaitAttempts);
        if WaitAttempts > MaxWaitAttempts then
          break;  { Timeout }
      end;
    until False;

    { Make sure we read any remaining data after process exits }
    try
      while P.Output.NumBytesAvailable > 0 do
      begin
        ReadCount := P.Output.Read(ReadBuf, SizeOf(ReadBuf));
        if ReadCount > 0 then
        begin
          SetString(Chunk, PAnsiChar(@ReadBuf[0]), ReadCount);
          S.WriteString(Chunk);
        end;
      end;
    except
    end;

    { Wait for process to finish and get exit code }
    P.WaitOnExit;
    Result.ExitCode := P.ExitStatus;
    Result.OutputText := S.DataString;
  finally
    S.Free;
    P.Free;
  end;
end;

end.
