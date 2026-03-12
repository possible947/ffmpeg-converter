unit converter_runner;

{$mode objfpc}{$H+}

interface

uses converter_types;

function ProbeDuration(const InputFile: string): Double;
function RunEncode(const CommandBase: string): TConverterError;

implementation

uses
  SysUtils,
  process_utils;

function ProbeDuration(const InputFile: string): Double;
var
  R: TRunResult;
  Code: Integer;
begin
  R := RunCommandCapture(
    'ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 ' +
    '"' + InputFile + '" 2>/dev/null');

  if R.ExitCode <> 0 then
    Exit(0.0);

  Val(Trim(R.OutputText), Result, Code);
  if Code <> 0 then
    Result := 0.0;
end;

function RunEncode(const CommandBase: string): TConverterError;
var
  R: TRunResult;
begin
  R := RunCommandCapture(CommandBase + ' -progress pipe:1 -nostats -nostdin 2>&1');
  if R.ExitCode <> 0 then
    Exit(ERR_FFMPEG_FAILED);
  Result := ERR_OK;
end;

end.
