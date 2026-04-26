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
    HasQSV:           Boolean;
    HasVulkan:        Boolean;
    VulkanDeviceCount: Integer;
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
  SysUtils,
  process_utils
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

{ Probe how many Vulkan devices are working by testing vulkan:0 .. vulkan:7. }
function ProbeVulkanDeviceCount(const FfmpegBin: string): Integer;
{$IFDEF Windows}
var
  I: Integer;
  Cmd: string;
  R: TRunResult;
{$ENDIF}
begin
  Result := 0;
{$IFDEF Windows}
  for I := 0 to 7 do
  begin
    Cmd := '"' + FfmpegBin + '" -v error -hide_banner' +
           ' -init_hw_device vulkan=vk:' + IntToStr(I) + ' -filter_hw_device vk' +
           ' -f lavfi -i color=size=1920x1080:rate=1 -frames:v 1' +
           ' -vf format=yuv422p10le,hwupload -c:v prores_ks_vulkan -f null NUL 2>nul';
    R := RunCommandCapture(Cmd);
    if R.ExitCode = 0 then
      Inc(Result)
    else
      Break;
  end;
{$ENDIF}
end;

{ ---- public API -------------------------------------------------------- }

function DetectWindowsHardware(const FfmpegBin: string): TWindowsHWInfo;
var
  Bin: string;
begin
  FillChar(Result, SizeOf(Result), 0);

{$IFDEF Windows}
  if FfmpegBin = '' then
    Bin := 'ffmpeg'
  else
    Bin := FfmpegBin;

  Result.HasNVENC  := ProbeEncoder(Bin, 'h264_nvenc');
  Result.HasAMF    := ProbeEncoder(Bin, 'h264_amf');
  Result.HasQSV    := ProbeEncoder(Bin, 'h264_qsv');
  Result.HasVulkan := ProbeVulkanEncoder(Bin);
  if Result.HasVulkan then
    Result.VulkanDeviceCount := ProbeVulkanDeviceCount(Bin)
  else
    Result.VulkanDeviceCount := 0;
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
  if HW.HasVulkan then
    Add('HW detection: Vulkan device count=' + IntToStr(HW.VulkanDeviceCount));
  if HW.HasMkvmerge then
    Add('HW detection: mkvmerge=found')
  else
    Add('HW detection: mkvmerge=not found');
end;

end.
