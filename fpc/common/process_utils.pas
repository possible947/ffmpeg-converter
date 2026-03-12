unit process_utils;

{$mode objfpc}{$H+}

interface

type
  TRunResult = record
    ExitCode: LongInt;
    OutputText: string;
  end;

function RunCommandCapture(const CommandLine: string): TRunResult;

implementation

uses
  Classes,
  Process,
  SysUtils;

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
