unit linux_probe;

{$mode objfpc}{$H+}

interface

type
  TStringArray = array of string;

  TLinuxCodecSupport = record
    HasVaapiH264: Boolean;
    HasVaapiHEVC: Boolean;
    HasNVENC:     Boolean;
    HasAMF:       Boolean;
    HasQSV:       Boolean;
    HasVulkan:    Boolean;
    VulkanDeviceIndex: Integer;
    VulkanDeviceCount: Integer;
    HasMkvmerge:  Boolean;
    HasMp4Box:    Boolean;
    VaapiRenderNode: string;
  end;

function ValidateVaapiDevice: Boolean;
function GetVaapiRenderNode: string;
function ProbeVaapiDevices: TStringArray;
function ProbeLinuxCodecSupport: TLinuxCodecSupport;
function GetBestAV1Decoder(const FfmpegBin: string): string;
function ProbeInputVideoCodec(const FfprobeBin, InputFile: string): string;
function ProbeFdkAacEncoder(const FfmpegBin: string): Boolean;

implementation

uses SysUtils, Classes, process_utils, path_utils, tool_paths;

{ --------------------------------------------------------------------------
  VAAPI device helpers (unchanged from original)
  -------------------------------------------------------------------------- }

function ValidateVaapiDevice: Boolean;
begin
{$IFDEF Linux}
  Result := FileExists('/dev/dri/renderD128') or FileExists('/dev/dri/card0');
{$ELSE}
  Result := False;
{$ENDIF}
end;

function GetVaapiRenderNode: string;
begin
{$IFDEF Linux}
  if FileExists('/dev/dri/renderD128') then
    Result := '/dev/dri/renderD128'
  else if FileExists('/dev/dri/card0') then
    Result := '/dev/dri/card0'
  else
    Result := '';
{$ELSE}
  Result := '';
{$ENDIF}
end;

function ProbeVaapiDevices: TStringArray;
var
  I: Integer;
  Devices: TStringArray;
begin
  Result := nil;
  Devices := nil;
{$IFDEF Linux}
  SetLength(Devices, 0);
  for I := 128 to 135 do
  begin
    if FileExists(Format('/dev/dri/renderD%d', [I])) then
    begin
      SetLength(Devices, Length(Devices) + 1);
      Devices[High(Devices)] := Format('/dev/dri/renderD%d', [I]);
    end;
  end;
  for I := 0 to 15 do
  begin
    if FileExists(Format('/dev/dri/card%d', [I])) then
    begin
      SetLength(Devices, Length(Devices) + 1);
      Devices[High(Devices)] := Format('/dev/dri/card%d', [I]);
    end;
  end;
  Result := Devices;
{$ENDIF}
end;

{ --------------------------------------------------------------------------
  Internal probe helpers
  -------------------------------------------------------------------------- }

{ Run a one-frame null encode to test a software-style encoder (NVENC, AMF, QSV).
  No device path required — these auto-select the GPU on Linux. }
function ProbeSimpleEncoder(const FfmpegBin, EncoderName: string): Boolean;
var
  Cmd: string;
  R: TRunResult;
begin
{$IFDEF Linux}
  Result := False;
  if FfmpegBin = '' then
    Exit;
  Cmd := QuoteForShell(FfmpegBin) +
         ' -v error -hide_banner' +
         ' -f lavfi -i color=size=1920x1080:rate=1' +
         ' -frames:v 1 -c:v ' + EncoderName +
         ' -f null /dev/null 2>&1';
  R := RunCommandCapture(Cmd);
  Result := R.ExitCode = 0;
{$ELSE}
  Result := False;
{$ENDIF}
end;

{ Run a one-frame VAAPI null encode for a specific render node. }
function ProbeVaapiEncoder(const FfmpegBin, RenderNode, EncoderName: string): Boolean;
var
  Cmd: string;
  R: TRunResult;
begin
{$IFDEF Linux}
  Result := False;
  if (FfmpegBin = '') or (RenderNode = '') then
    Exit;
  Cmd := QuoteForShell(FfmpegBin) +
         ' -v error -hide_banner' +
         ' -init_hw_device vaapi=va:' + QuoteForShell(RenderNode) +
         ' -f lavfi -i color=size=1920x1080:rate=1' +
         ' -frames:v 1 -vf format=nv12,hwupload' +
         ' -c:v ' + EncoderName +
         ' -f null /dev/null 2>&1';
  R := RunCommandCapture(Cmd);
  Result := R.ExitCode = 0;
{$ELSE}
  Result := False;
{$ENDIF}
end;

{ Probe Vulkan prores_ks_vulkan on devices vk:0..vk:7.
  Returns True if at least one device succeeds.
  BestDevice is the highest working index, DeviceCount is number of working devices. }
function ProbeVulkanEncoder(const FfmpegBin: string; out BestDevice: Integer; out DeviceCount: Integer): Boolean;
var
  I: Integer;
  Cmd: string;
  R: TRunResult;
begin
{$IFDEF Linux}
  Result := False;
  BestDevice := -1;
  DeviceCount := 0;
  if FfmpegBin = '' then
    Exit;
  for I := 0 to 7 do
  begin
    Cmd := QuoteForShell(FfmpegBin) +
           ' -v error -hide_banner' +
           ' -init_hw_device vulkan=vk:' + IntToStr(I) + ' -filter_hw_device vk' +
           ' -f lavfi -i color=size=1920x1080:rate=1 -frames:v 1' +
           ' -vf format=yuv422p10le,hwupload' +
           ' -c:v prores_ks_vulkan -f null /dev/null 2>&1';
    R := RunCommandCapture(Cmd);
    if R.ExitCode = 0 then
    begin
      Inc(DeviceCount);
      BestDevice := I;
      Result := True;
    end;
    { Stop early if no successes after 3 attempts — no Vulkan GPU present. }
    if (DeviceCount = 0) and (I >= 2) then
      Break;
  end;
{$ELSE}
  BestDevice := -1;
  DeviceCount := 0;
  Result := False;
{$ENDIF}
end;

{ --------------------------------------------------------------------------
  Hardware codec support detection (cached)
  -------------------------------------------------------------------------- }

var
  GSupportCached: Boolean = False;
  GSupport: TLinuxCodecSupport;

function DetectLinuxCodecSupport: TLinuxCodecSupport;
var
  Tools: TToolPaths;
  FfmpegBin: string;
  RenderNode: string;
  I: Integer;
  VulkanBest: Integer;
  VulkanCount: Integer;
begin
  Result.HasVaapiH264 := False;
  Result.HasVaapiHEVC := False;
  Result.HasNVENC := False;
  Result.HasAMF := False;
  Result.HasQSV := False;
  Result.HasVulkan := False;
  Result.VulkanDeviceIndex := 0;
  Result.VulkanDeviceCount := 0;
  Result.HasMkvmerge := False;
  Result.HasMp4Box := False;
  Result.VaapiRenderNode := '';
{$IFDEF Linux}
  Tools := ResolveToolPaths;
  FfmpegBin := Tools.FfmpegBin;

  Result.HasMkvmerge := Tools.MkvmergeBin <> '';
  Result.HasMp4Box   := Tools.Mp4BoxBin <> '';

  { VAAPI — probe each render node }
  for I := 128 to 135 do
  begin
    RenderNode := Format('/dev/dri/renderD%d', [I]);
    if not FileExists(RenderNode) then
      Continue;
    if ProbeVaapiEncoder(FfmpegBin, RenderNode, 'h264_vaapi') then
    begin
      Result.HasVaapiH264 := True;
      if Result.VaapiRenderNode = '' then
        Result.VaapiRenderNode := RenderNode;
    end;
    if ProbeVaapiEncoder(FfmpegBin, RenderNode, 'hevc_vaapi') then
    begin
      Result.HasVaapiHEVC := True;
      if Result.VaapiRenderNode = '' then
        Result.VaapiRenderNode := RenderNode;
    end;
  end;

  { NVENC — NVIDIA (no device path required) }
  Result.HasNVENC := ProbeSimpleEncoder(FfmpegBin, 'h264_nvenc') or
                     ProbeSimpleEncoder(FfmpegBin, 'hevc_nvenc');

  { AMF — AMD (no device path required) }
  Result.HasAMF := ProbeSimpleEncoder(FfmpegBin, 'h264_amf') or
                   ProbeSimpleEncoder(FfmpegBin, 'hevc_amf');

  { QSV — Intel (no device path required) }
  Result.HasQSV := ProbeSimpleEncoder(FfmpegBin, 'h264_qsv') or
                   ProbeSimpleEncoder(FfmpegBin, 'hevc_qsv');

  { Vulkan — any GPU with Vulkan 1.1+ }
  Result.HasVulkan := ProbeVulkanEncoder(FfmpegBin, VulkanBest, VulkanCount);
  Result.VulkanDeviceCount := VulkanCount;
  if VulkanBest >= 0 then
    Result.VulkanDeviceIndex := VulkanBest
  else
    Result.VulkanDeviceIndex := 0;
{$ENDIF}
end;

function ProbeLinuxCodecSupport: TLinuxCodecSupport;
begin
{$IFDEF Linux}
  if not GSupportCached then
  begin
    GSupport := DetectLinuxCodecSupport;
    GSupportCached := True;
  end;
  Result := GSupport;
{$ELSE}
  FillChar(Result, SizeOf(Result), 0);
{$ENDIF}
end;

{ --------------------------------------------------------------------------
  AV1 decoder selection
  -------------------------------------------------------------------------- }

var
  GAV1DecoderCached: Boolean = False;
  GAV1Decoder: string;

{ Return the best available AV1 decoder for the current ffmpeg build.
  Priority: av1_qsv (Intel QSV hardware decode) > libdav1d (pure software) > '' (native).
  The native av1 decoder may crash on systems with NVDEC that lacks AV1 support;
  libdav1d and av1_qsv bypass this issue. }
function GetBestAV1Decoder(const FfmpegBin: string): string;
var
  Cmd: string;
  R: TRunResult;
  HasLibdav1d: Boolean;
  HasAV1QSV: Boolean;
begin
  Result := '';
{$IFDEF Linux}
  if GAV1DecoderCached then
  begin
    Result := GAV1Decoder;
    Exit;
  end;

  if FfmpegBin = '' then
  begin
    GAV1DecoderCached := True;
    GAV1Decoder := '';
    Exit;
  end;

  Cmd := QuoteForShell(FfmpegBin) + ' -hide_banner -v error -decoders 2>/dev/null';
  R := RunCommandCapture(Cmd);
  if R.ExitCode = 0 then
  begin
    HasAV1QSV    := Pos(' av1_qsv',  R.OutputText) > 0;
    HasLibdav1d  := Pos(' libdav1d', R.OutputText) > 0;
    if HasAV1QSV then
      Result := 'av1_qsv'
    else if HasLibdav1d then
      Result := 'libdav1d';
  end;

  GAV1Decoder := Result;
  GAV1DecoderCached := True;
{$ENDIF}
end;

{ Probe the video codec of the first video stream in InputFile using ffprobe.
  Returns a lowercase codec name such as 'av1', 'h264', 'hevc', or '' on error. }
function ProbeInputVideoCodec(const FfprobeBin, InputFile: string): string;
var
  Cmd: string;
  R: TRunResult;
begin
  Result := '';
{$IFDEF Linux}
  if (FfprobeBin = '') or (InputFile = '') then
    Exit;
  Cmd := QuoteForShell(FfprobeBin) +
         ' -v error -select_streams v:0 -show_entries stream=codec_name' +
         ' -of default=noprint_wrappers=1:nokey=1 ' +
         QuoteForShell(InputFile) + ' 2>/dev/null';
  R := RunCommandCapture(Cmd);
  if R.ExitCode = 0 then
    Result := LowerCase(Trim(R.OutputText));
{$ENDIF}
end;

{ --------------------------------------------------------------------------
  AAC encoder availability
  -------------------------------------------------------------------------- }

var
  GFdkAacCached: Boolean = False;
  GFdkAacAvailable: Boolean;

{ Return True if libfdk_aac is compiled into the given ffmpeg binary. }
function ProbeFdkAacEncoder(const FfmpegBin: string): Boolean;
var
  Cmd: string;
  R: TRunResult;
begin
  Result := False;
{$IFDEF Linux}
  if GFdkAacCached then
  begin
    Result := GFdkAacAvailable;
    Exit;
  end;

  if FfmpegBin = '' then
  begin
    GFdkAacCached    := True;
    GFdkAacAvailable := False;
    Exit;
  end;

  Cmd := QuoteForShell(FfmpegBin) + ' -hide_banner -v error -encoders 2>/dev/null';
  R := RunCommandCapture(Cmd);
  Result := (R.ExitCode = 0) and (Pos(' libfdk_aac', R.OutputText) > 0);

  GFdkAacAvailable := Result;
  GFdkAacCached    := True;
{$ENDIF}
end;

end.

