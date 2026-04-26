unit windows_probe;

{$mode objfpc}{$H+}

interface

type
  TWindowsCodecSupport = record
    HasNVENC: Boolean;
    HasAMF: Boolean;
    HasQSV: Boolean;
    HasVulkan: Boolean;
    HasMkvmerge: Boolean;
  end;

function ProbeWindowsCodecSupport: TWindowsCodecSupport;
function IsNVENCAvailable: Boolean;
function IsAMFAvailable: Boolean;
function IsQSVAvailable: Boolean;
function IsVulkanAvailable: Boolean;

implementation

uses SysUtils, process_utils, windows_mkvmerge;

function IsNVENCAvailable: Boolean;
{$IFDEF Windows}
var
  CmdRes: TRunResult;
{$ENDIF}
begin
{$IFDEF Windows}
  CmdRes := RunCommandCapture('ffmpeg -encoders 2>&1 | findstr /i nvenc');
  Result := CmdRes.ExitCode = 0;
{$ELSE}
  Result := False;
{$ENDIF}
end;

function IsAMFAvailable: Boolean;
{$IFDEF Windows}
var
  CmdRes: TRunResult;
{$ENDIF}
begin
{$IFDEF Windows}
  CmdRes := RunCommandCapture('ffmpeg -encoders 2>&1 | findstr /i amf');
  Result := CmdRes.ExitCode = 0;
{$ELSE}
  Result := False;
{$ENDIF}
end;

function IsQSVAvailable: Boolean;
{$IFDEF Windows}
var
  CmdRes: TRunResult;
{$ENDIF}
begin
{$IFDEF Windows}
  CmdRes := RunCommandCapture('ffmpeg -encoders 2>&1 | findstr /i qsv');
  Result := CmdRes.ExitCode = 0;
{$ELSE}
  Result := False;
{$ENDIF}
end;

function IsVulkanAvailable: Boolean;
{$IFDEF Windows}
var
  CmdRes: TRunResult;
{$ENDIF}
begin
{$IFDEF Windows}
  CmdRes := RunCommandCapture('ffmpeg -encoders 2>&1 | findstr /i vulkan');
  Result := CmdRes.ExitCode = 0;
{$ELSE}
  Result := False;
{$ENDIF}
end;

function ProbeWindowsCodecSupport: TWindowsCodecSupport;
begin
  Result.HasNVENC := IsNVENCAvailable;
  Result.HasAMF := IsAMFAvailable;
  Result.HasQSV := IsQSVAvailable;
  Result.HasVulkan := IsVulkanAvailable;
  Result.HasMkvmerge := FindMkvmergeBin <> '';
end;

end.
