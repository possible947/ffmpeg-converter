unit windows_probe;

{$mode objfpc}{$H+}

interface

type
  TWindowsCodecSupport = record
    HasNVENC: Boolean;
    HasAMF: Boolean;
    HasAV1AMF: Boolean;
    HasQSV: Boolean;
    HasVulkan: Boolean;
    VulkanDeviceIndex: Integer;
    VulkanDeviceCount: Integer;
    { Phase 2: hardware Vulkan video encoders, probed independently of the
      prores_ks_vulkan-only HasVulkan flag above. }
    HasVulkanH264: Boolean;
    HasVulkanHEVC: Boolean;
    HasVulkanAV1: Boolean;
    VulkanHwDeviceIndex: Integer;
    VulkanHwDeviceCount: Integer;
    HasMkvmerge: Boolean;
  end;

function ProbeWindowsCodecSupport: TWindowsCodecSupport;
function IsNVENCAvailable: Boolean;
function IsAMFAvailable: Boolean;
function IsQSVAvailable: Boolean;
function IsVulkanAvailable: Boolean;
function ProbeEncoder(const FfmpegBin, EncoderName: string): Boolean;
function ProbeVulkanEncoder(const FfmpegBin: string; out BestDevice: Integer; out DeviceCount: Integer): Boolean;
function ProbeVulkanHwEncoder(const FfmpegBin, EncoderName: string;
  out BestDevice: Integer; out DeviceCount: Integer): Boolean;

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

function ProbeVulkanEncoder(const FfmpegBin: string; out BestDevice: Integer; out DeviceCount: Integer): Boolean;
var
  Cmd: string;
  R: TRunResult;
  DeviceIdx: Integer;
begin
  Result := False;
  BestDevice := -1;
  DeviceCount := 0;
{$IFDEF Windows}
  if FfmpegBin = '' then
    Exit;

  for DeviceIdx := 0 to 7 do
  begin
    Cmd := QuoteForShell(FfmpegBin) + ' -v error -hide_banner ' +
           '-init_hw_device vulkan=vk:' + IntToStr(DeviceIdx) + ' -filter_hw_device vk ' +
           '-f lavfi -i color=size=1920x1080:rate=1 -frames:v 1 ' +
           '-vf format=yuv422p10le,hwupload -c:v prores_ks_vulkan -f null NUL 2>nul';
    R := RunCommandCapture(Cmd);
    if R.ExitCode = 0 then
    begin
      Inc(DeviceCount);
      BestDevice := DeviceIdx;
      Result := True;
    end;
  end;
{$ENDIF}
end;

{ Probe a hardware Vulkan video encoder (h264_vulkan/hevc_vulkan/av1_vulkan)
  on devices vk:0..vk:7. Mirrors ProbeVulkanEncoder above but for a
  caller-supplied encoder name (no software-device exclusion — that's a
  Linux/llvmpipe-specific concern per v3.0-Phase2.md). }
function ProbeVulkanHwEncoder(const FfmpegBin, EncoderName: string;
  out BestDevice: Integer; out DeviceCount: Integer): Boolean;
var
  Cmd: string;
  R: TRunResult;
  DeviceIdx: Integer;
begin
  Result := False;
  BestDevice := -1;
  DeviceCount := 0;
{$IFDEF Windows}
  if FfmpegBin = '' then
    Exit;

  for DeviceIdx := 0 to 7 do
  begin
    Cmd := QuoteForShell(FfmpegBin) + ' -v error -hide_banner ' +
           '-init_hw_device vulkan=vk:' + IntToStr(DeviceIdx) + ' -filter_hw_device vk ' +
           '-f lavfi -i color=size=1920x1080:rate=1 -frames:v 1 ' +
           '-vf format=nv12,hwupload -c:v ' + EncoderName + ' -f null NUL 2>nul';
    R := RunCommandCapture(Cmd);
    if R.ExitCode = 0 then
    begin
      Inc(DeviceCount);
      BestDevice := DeviceIdx;
      Result := True;
    end;
  end;
{$ENDIF}
end;

function DetectWindowsCodecSupport: TWindowsCodecSupport;
var
  FfmpegBin: string;
  VulkanBest, VulkanCount: Integer;
  VHwBest, VHwCount, BestHwCount: Integer;
begin
  FillChar(Result, SizeOf(Result), 0);
  Result.VulkanDeviceIndex := -1;
  Result.VulkanHwDeviceIndex := -1;
{$IFDEF Windows}
  FfmpegBin := ResolveFfmpegBin;
  if FfmpegBin <> '' then
  begin
    Result.HasNVENC := ProbeEncoder(FfmpegBin, 'h264_nvenc') or ProbeEncoder(FfmpegBin, 'hevc_nvenc');
    Result.HasAMF := ProbeEncoder(FfmpegBin, 'h264_amf') or ProbeEncoder(FfmpegBin, 'hevc_amf');
    Result.HasAV1AMF := ProbeEncoder(FfmpegBin, 'av1_amf');
    Result.HasQSV := ProbeEncoder(FfmpegBin, 'h264_qsv') or ProbeEncoder(FfmpegBin, 'hevc_qsv');

    Result.HasVulkan := ProbeVulkanEncoder(FfmpegBin, VulkanBest, VulkanCount);
    Result.VulkanDeviceIndex := VulkanBest;
    Result.VulkanDeviceCount := VulkanCount;

    { Phase 2: hardware Vulkan video encoders (h264_vulkan/hevc_vulkan/
      av1_vulkan). Each is probed independently — like Linux, the shared
      "best device" stats keep whichever codec found the most working
      devices, since prores-Vulkan and hw-Vulkan may work on different
      physical GPUs. }
    BestHwCount := 0;
    Result.HasVulkanH264 := ProbeVulkanHwEncoder(FfmpegBin, 'h264_vulkan', VHwBest, VHwCount);
    if VHwCount > BestHwCount then
    begin
      BestHwCount := VHwCount;
      Result.VulkanHwDeviceIndex := VHwBest;
    end;
    Result.HasVulkanHEVC := ProbeVulkanHwEncoder(FfmpegBin, 'hevc_vulkan', VHwBest, VHwCount);
    if VHwCount > BestHwCount then
    begin
      BestHwCount := VHwCount;
      Result.VulkanHwDeviceIndex := VHwBest;
    end;
    Result.HasVulkanAV1 := ProbeVulkanHwEncoder(FfmpegBin, 'av1_vulkan', VHwBest, VHwCount);
    if VHwCount > BestHwCount then
    begin
      BestHwCount := VHwCount;
      Result.VulkanHwDeviceIndex := VHwBest;
    end;
    Result.VulkanHwDeviceCount := BestHwCount;
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
