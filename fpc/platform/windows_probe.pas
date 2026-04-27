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
function ProbeEncoder(const FfmpegBin, EncoderName: string): Boolean;
function ProbeVulkanEncoder(const FfmpegBin: string): Boolean;

implementation

uses SysUtils, process_utils, windows_mkvmerge, tool_paths, path_utils;

var
  GSupportCached: Boolean = False;
  GSupport: TWindowsCodecSupport;

{ Run a short ffmpeg null-encode test to check whether an encoder is usable.
  Returns True if ffmpeg exits with code 0. }
function ProbeEncoder(const FfmpegBin, EncoderName: string): Boolean;
var
  Cmd: string;
  R: TRunResult;
begin
{$IFDEF Windows}
  Cmd := QuoteForShell(FfmpegBin) + ' -f lavfi -i color=c=black:s=64x64:d=1 -vframes 1' +
         ' -vcodec ' + EncoderName + ' -f null NUL 2>nul';
  R := RunCommandCapture(Cmd);
  Result := R.ExitCode = 0;
{$ELSE}
  Result := False;
{$ENDIF}
end;

function ProbeVulkanEncoder(const FfmpegBin: string): Boolean;
var
  Cmd: string;
  R: TRunResult;
  DeviceIdx: Integer;
begin
{$IFDEF Windows}
  if FfmpegBin = '' then
    Exit(False);

  for DeviceIdx := 0 to 7 do
  begin
    Cmd := QuoteForShell(FfmpegBin) + ' -v error -hide_banner ' +
           '-init_hw_device vulkan=vk:' + IntToStr(DeviceIdx) + ' -filter_hw_device vk ' +
           '-f lavfi -i color=size=1920x1080:rate=1 -frames:v 1 ' +
           '-vf format=yuv422p10le,hwupload -c:v prores_ks_vulkan -f null NUL 2>nul';
    R := RunCommandCapture(Cmd);
    if R.ExitCode = 0 then
      Exit(True);
  end;

  Result := False;
{$ELSE}
  Result := False;
{$ENDIF}
end;

function DetectWindowsCodecSupport: TWindowsCodecSupport;
var
  FfmpegBin: string;
begin
  FillChar(Result, SizeOf(Result), 0);
{$IFDEF Windows}
  FfmpegBin := ResolveFfmpegBin;
  if FfmpegBin <> '' then
  begin
    Result.HasNVENC := ProbeEncoder(FfmpegBin, 'h264_nvenc') or ProbeEncoder(FfmpegBin, 'hevc_nvenc');
    Result.HasAMF := ProbeEncoder(FfmpegBin, 'h264_amf') or ProbeEncoder(FfmpegBin, 'hevc_amf');
    Result.HasQSV := ProbeEncoder(FfmpegBin, 'h264_qsv') or ProbeEncoder(FfmpegBin, 'hevc_qsv');
    Result.HasVulkan := ProbeVulkanEncoder(FfmpegBin);
  end;
  Result.HasMkvmerge := FindMkvmergeBin <> '';
{$ENDIF}
end;

function GetCachedSupport: TWindowsCodecSupport;
begin
  if not GSupportCached then
  begin
    GSupport := DetectWindowsCodecSupport;
    GSupportCached := True;
  end;
  Result := GSupport;
end;

function IsNVENCAvailable: Boolean;
begin
{$IFDEF Windows}
  Result := GetCachedSupport.HasNVENC;
{$ELSE}
  Result := False;
{$ENDIF}
end;

function IsAMFAvailable: Boolean;
begin
{$IFDEF Windows}
  Result := GetCachedSupport.HasAMF;
{$ELSE}
  Result := False;
{$ENDIF}
end;

function IsQSVAvailable: Boolean;
begin
{$IFDEF Windows}
  Result := GetCachedSupport.HasQSV;
{$ELSE}
  Result := False;
{$ENDIF}
end;

function IsVulkanAvailable: Boolean;
begin
{$IFDEF Windows}
  Result := GetCachedSupport.HasVulkan;
{$ELSE}
  Result := False;
{$ENDIF}
end;

function ProbeWindowsCodecSupport: TWindowsCodecSupport;
begin
  Result := GetCachedSupport;
end;

end.
