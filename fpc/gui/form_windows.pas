unit form_windows;

{$mode objfpc}{$H+}

{ Windows-specific hardware detection for the GUI.
  Probes NVENC/AMF/QSV/Vulkan availability at startup and provides helpers
  for logging and codec-menu population. }

interface

type
  { Per-encoder availability flags returned by DetectWindowsHardware. }
  TWindowsHWInfo = record
    HasNVENC:         Boolean;
    HasAMF:           Boolean;
    HasAV1AMF:        Boolean;
    HasQSV:           Boolean;
    HasAV1QSV:        Boolean;
    HasAV1NVENC:      Boolean;
    HasVulkan:        Boolean;
    VulkanDeviceIndex: Integer;
    VulkanDeviceCount: Integer;
    { Phase 2: hardware Vulkan video encoders. }
    HasVulkanH264:    Boolean;
    HasVulkanHEVC:    Boolean;
    HasVulkanAV1:     Boolean;
    VulkanHwDeviceIndex: Integer;
    VulkanHwDeviceCount: Integer;
    HasMkvmerge:      Boolean;
  end;

{ Run ffmpeg -encoders and probe each hardware encoder.
  FfmpegBin should be the full path to ffmpeg (or 'ffmpeg' for PATH lookup).
  This function may be slow (runs several sub-processes) and should be called
  from a background thread or during form initialisation. }
function DetectWindowsHardware(const FfmpegBin: string): TWindowsHWInfo;

{ Build a list of human-readable log lines describing the detected hardware.
  Returns an array of strings suitable for adding to a log list. }
procedure GetWindowsHardwareLogLines(const HW: TWindowsHWInfo;
  var Lines: array of string; out LineCount: Integer);

implementation

uses
  SysUtils
  {$IFDEF Windows}
  , windows_mkvmerge
  , windows_probe
  {$ENDIF}
  ;

{ ---- internal helpers -------------------------------------------------- }

function YesNo(B: Boolean): string; inline;
begin
  if B then Result := 'yes' else Result := 'no';
end;

{ ---- public API -------------------------------------------------------- }

function DetectWindowsHardware(const FfmpegBin: string): TWindowsHWInfo;
var
  Bin: string;
  VulkanBest, VulkanCount: Integer;
  VHwBest, VHwCount, BestHwCount: Integer;
begin
  FillChar(Result, SizeOf(Result), 0);
  Result.VulkanDeviceIndex := -1;
  Result.VulkanHwDeviceIndex := -1;

{$IFDEF Windows}
  if FfmpegBin = '' then
    Bin := 'ffmpeg'
  else
    Bin := FfmpegBin;

  Result.HasNVENC  := ProbeEncoder(Bin, 'h264_nvenc');
  Result.HasAMF    := ProbeEncoder(Bin, 'h264_amf');
  Result.HasAV1AMF := ProbeEncoder(Bin, 'av1_amf');
  Result.HasQSV    := ProbeEncoder(Bin, 'h264_qsv');
  Result.HasAV1QSV   := ProbeEncoder(Bin, 'av1_qsv');
  Result.HasAV1NVENC := ProbeEncoder(Bin, 'av1_nvenc');

  Result.HasVulkan := ProbeVulkanEncoder(Bin, VulkanBest, VulkanCount);
  Result.VulkanDeviceIndex := VulkanBest;
  Result.VulkanDeviceCount := VulkanCount;

  { Phase 2: hardware Vulkan video encoders, probed independently of the
    prores_ks_vulkan-only Vulkan stats above — they may only work on a
    different physical GPU. Keep whichever codec found the most devices,
    mirroring the Linux probe's merge logic. }
  BestHwCount := 0;
  Result.HasVulkanH264 := ProbeVulkanHwEncoder(Bin, 'h264_vulkan', VHwBest, VHwCount);
  if VHwCount > BestHwCount then
  begin
    BestHwCount := VHwCount;
    Result.VulkanHwDeviceIndex := VHwBest;
  end;
  Result.HasVulkanHEVC := ProbeVulkanHwEncoder(Bin, 'hevc_vulkan', VHwBest, VHwCount);
  if VHwCount > BestHwCount then
  begin
    BestHwCount := VHwCount;
    Result.VulkanHwDeviceIndex := VHwBest;
  end;
  Result.HasVulkanAV1 := ProbeVulkanHwEncoder(Bin, 'av1_vulkan', VHwBest, VHwCount);
  if VHwCount > BestHwCount then
  begin
    BestHwCount := VHwCount;
    Result.VulkanHwDeviceIndex := VHwBest;
  end;
  Result.VulkanHwDeviceCount := BestHwCount;

  Result.HasMkvmerge := FindMkvmergeBin <> '';
{$ENDIF}
end;

procedure GetWindowsHardwareLogLines(const HW: TWindowsHWInfo;
  var Lines: array of string; out LineCount: Integer);

  procedure Add(const S: string);
  begin
    if LineCount <= High(Lines) then
    begin
      Lines[LineCount] := S;
      Inc(LineCount);
    end;
  end;

begin
  LineCount := 0;
  Add('HW detection: NVENC='  + YesNo(HW.HasNVENC) +
      '  AMF='                + YesNo(HW.HasAMF)   +
      '  QSV='                + YesNo(HW.HasQSV)   +
      '  Vulkan='             + YesNo(HW.HasVulkan));
  if HW.HasAV1AMF or HW.HasAV1QSV or HW.HasAV1NVENC then
    Add('HW detection: AV1='  + YesNo(HW.HasAV1AMF or HW.HasAV1QSV or HW.HasAV1NVENC) +
        '  (AMF='  + YesNo(HW.HasAV1AMF) +
        '  QSV='   + YesNo(HW.HasAV1QSV) +
        '  NVENC=' + YesNo(HW.HasAV1NVENC) + ')');
  if HW.HasVulkan then
    Add('HW detection: Vulkan device count=' + IntToStr(HW.VulkanDeviceCount));
  if HW.HasVulkanH264 or HW.HasVulkanHEVC or HW.HasVulkanAV1 then
    Add('HW detection: HW Vulkan (h264/hevc/av1) device count=' +
        IntToStr(HW.VulkanHwDeviceCount));
  if HW.HasMkvmerge then
    Add('HW detection: mkvmerge=found')
  else
    Add('HW detection: mkvmerge=not found');
end;

end.
