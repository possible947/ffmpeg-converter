unit cli_args;

{$mode objfpc}{$H+}

interface

uses converter_types, SysUtils
  {$IFDEF Windows}
  , windows_probe
  {$ENDIF}
  ;

type
  TStringArray = array of string;

procedure PrintUsage;
procedure PrintCodecsList;
function ParseArgs(var Opts: TConvertOptions; out Files: array of PAnsiChar; out FileCount: LongInt): Boolean;
function ParseArgsFromArray(var Opts: TConvertOptions; out Files: array of PAnsiChar; out FileCount: LongInt; const Args: TStringArray): Boolean;
procedure PrintSummary(const Opts: TConvertOptions; const Files: array of PAnsiChar; FileCount: LongInt);
function VerifyAndCompactFiles(var Files: array of PAnsiChar; var FileCount: LongInt): Boolean;

implementation

uses
  {$IFNDEF Windows}
  BaseUnix,
  linux_probe,
  {$ENDIF}
  tool_paths,
  fs_utils,
  preset_loader;

  {$IFDEF Windows}
var
  GWindowsCodecsCached: Boolean = False;
  GWindowsCodecs: TWindowsCodecSupport;

function GetWindowsCodecSupport: TWindowsCodecSupport;
var
  Temp: TWindowsCodecSupport;
begin
  if not GWindowsCodecsCached then
  begin
    GWindowsCodecs := ProbeWindowsCodecSupport;
    GWindowsCodecsCached := True;
  end;
  Result := GWindowsCodecs;
end;
  {$ENDIF}

function IsCodecAllowedOnCurrentPlatform(const Codec: string): Boolean;
var
  {$IFDEF Windows}
  WinCaps: TWindowsCodecSupport;
  HasM4V: Boolean;
  {$ENDIF}
  {$IFDEF Linux}
  LinuxCaps: TLinuxCodecSupport;
  {$ENDIF}
begin
{$IFDEF Windows}
  WinCaps := GetWindowsCodecSupport;
  HasM4V := ResolveMp4BoxBin <> '';

  Result := (Codec = 'copy') or (Codec = 'prores') or (Codec = 'prores_ks');

  if HasM4V then
    Result := Result or (Codec = 'm4v');

  if WinCaps.HasMkvmerge then
    Result := Result or (Codec = 'mux');

  if WinCaps.HasNVENC then
    Result := Result or (Codec = 'h264_nvenc') or (Codec = 'hevc_nvenc');

  if WinCaps.HasAMF then
    Result := Result or (Codec = 'h264_amf') or (Codec = 'hevc_amf');

  if WinCaps.HasAV1AMF then
    Result := Result or (Codec = 'av1_amf');

  if WinCaps.HasQSV then
    Result := Result or (Codec = 'h264_qsv') or (Codec = 'hevc_qsv');

  if WinCaps.HasVulkan then
    Result := Result or (Codec = 'prores_ks_vulkan');
  if WinCaps.HasVulkanH264 then
    Result := Result or (Codec = 'h264_vulkan');
  if WinCaps.HasVulkanHEVC then
    Result := Result or (Codec = 'hevc_vulkan');
  if WinCaps.HasVulkanAV1 then
    Result := Result or (Codec = 'av1_vulkan');
{$ELSE}
  {$IFDEF Linux}
  LinuxCaps := ProbeLinuxCodecSupport;

  { copy, prores, prores_ks, and mux are always available on Linux.
    mkvmerge is expected to be in PATH; hardware codecs require GPU probe. }
  Result := (Codec = 'copy') or (Codec = 'prores') or (Codec = 'prores_ks') or
            (Codec = 'mux');

  if LinuxCaps.HasVaapiH264 then
    Result := Result or (Codec = 'h264_vaapi');
  if LinuxCaps.HasVaapiHEVC then
    Result := Result or (Codec = 'hevc_vaapi');
  if LinuxCaps.HasNVENC then
    Result := Result or (Codec = 'h264_nvenc') or (Codec = 'hevc_nvenc');
  if LinuxCaps.HasAMF then
    Result := Result or (Codec = 'h264_amf') or (Codec = 'hevc_amf');
  if LinuxCaps.HasAV1AMF then
    Result := Result or (Codec = 'av1_amf');
  if LinuxCaps.HasQSV then
    Result := Result or (Codec = 'h264_qsv') or (Codec = 'hevc_qsv');
  if LinuxCaps.HasVulkan then
    Result := Result or (Codec = 'prores_ks_vulkan');
  if LinuxCaps.HasVulkanH264 then
    Result := Result or (Codec = 'h264_vulkan');
  if LinuxCaps.HasVulkanHEVC then
    Result := Result or (Codec = 'hevc_vulkan');
  if LinuxCaps.HasVulkanAV1 then
    Result := Result or (Codec = 'av1_vulkan');
  if LinuxCaps.HasMp4Box then
    Result := Result or (Codec = 'm4v');
  {$ELSE}
  Result := (Codec = 'copy') or (Codec = 'prores') or (Codec = 'prores_ks') or (Codec = 'mux');
  {$ENDIF}
{$ENDIF}
end;

function CurrentPlatformName: string;
begin
{$IFDEF Windows}
  Result := 'windows';
{$ELSE}
  {$IFDEF Linux}
  Result := 'linux';
  {$ELSE}
  Result := 'macos';
  {$ENDIF}
{$ENDIF}
end;

function IsPresetAllowed(const Codec, Preset: string): Boolean;
var
  PresetDb: TPresetDb;
begin
  PresetDb := TPresetDb.Create;
  try
    PresetDb.Load;
    Result := PresetDb.GetPreset(CurrentPlatformName, Codec, Preset) <> nil;
  finally
    PresetDb.Free;
  end;
end;

{ Returns the preferred default preset for Codec on the current platform when
  the user did not pass --preset explicitly. Prefers 'standard' (the historic
  compile-time default, still expected for the ProRes family), then 'default'
  (used by codecs with a single preset), then falls back to the first entry
  presets.json actually defines; 'default' if the codec has no entries at all
  (e.g. presets.json missing). }
function DefaultPresetForCodec(const Codec: string): string;
var
  PresetDb: TPresetDb;
  Presets: TStringArray;
  I: Integer;
begin
  Result := 'default';
  PresetDb := TPresetDb.Create;
  try
    PresetDb.Load;
    Presets := PresetDb.ListPresets(CurrentPlatformName, Codec);
    if Length(Presets) = 0 then
      Exit;

    for I := 0 to Length(Presets) - 1 do
      if Presets[I] = 'standard' then
        Exit('standard');
    for I := 0 to Length(Presets) - 1 do
      if Presets[I] = 'default' then
        Exit('default');
    Result := Presets[0];
  finally
    PresetDb.Free;
  end;
end;

procedure SetAnsiField(var Dest: array of AnsiChar; const S: string);
var
  N: SizeInt;
begin
  if Length(Dest) = 0 then
    Exit;
  FillChar(Dest[0], Length(Dest), 0);
  N := Length(Dest) - 1;
  StrPLCopy(@Dest[0], S, N);
end;

function ArrToStr(const A: array of AnsiChar): string;
begin
  Result := StrPas(@A[0]);
end;

function DeblockToText(Deblock: LongInt): string;
begin
  case Deblock of
    1: Result := 'none';
    2: Result := 'weak';
    3: Result := 'strong';
  else
    Result := 'none';
  end;
end;

function GenreToText(Genre: LongInt): string;
begin
  case Genre of
    1: Result := 'edm';
    2: Result := 'rock';
    3: Result := 'hiphop';
    4: Result := 'classical';
    5: Result := 'podcast';
  else
    Result := 'none';
  end;
end;

procedure PrintUsage;
var
  {$IFDEF Windows}
  WinCaps: TWindowsCodecSupport;
  HasM4V: Boolean;
  {$ENDIF}
  {$IFDEF Linux}
  LinuxCaps: TLinuxCodecSupport;
  {$ENDIF}
  CodecList: string;
begin
  WriteLn('Usage: ffmpeg_converter [options] file1 file2 ...');
  WriteLn;
  WriteLn('Options:');
{$IFDEF Windows}
  WinCaps := GetWindowsCodecSupport;
  HasM4V := ResolveMp4BoxBin <> '';
  CodecList := 'copy|prores|prores_ks';
  if WinCaps.HasNVENC then
    CodecList += '|h264_nvenc|hevc_nvenc';
  if WinCaps.HasAMF then
    CodecList += '|h264_amf|hevc_amf';
  if WinCaps.HasAV1AMF then
    CodecList += '|av1_amf';
  if WinCaps.HasQSV then
    CodecList += '|h264_qsv|hevc_qsv';
  if WinCaps.HasVulkan then
    CodecList += '|prores_ks_vulkan';
  if WinCaps.HasVulkanH264 then
    CodecList += '|h264_vulkan';
  if WinCaps.HasVulkanHEVC then
    CodecList += '|hevc_vulkan';
  if WinCaps.HasVulkanAV1 then
    CodecList += '|av1_vulkan';
  if WinCaps.HasMkvmerge then
    CodecList += '|mux';
  if HasM4V then
    CodecList += '|m4v';
  WriteLn('  -c, --codec <', CodecList, '>');
{$ELSE}
  {$IFDEF Linux}
  LinuxCaps := ProbeLinuxCodecSupport;
  CodecList := 'copy|prores|prores_ks|mux';
  if LinuxCaps.HasVaapiH264 then
    CodecList += '|h264_vaapi';
  if LinuxCaps.HasVaapiHEVC then
    CodecList += '|hevc_vaapi';
  if LinuxCaps.HasNVENC then
    CodecList += '|h264_nvenc|hevc_nvenc';
  if LinuxCaps.HasAMF then
    CodecList += '|h264_amf|hevc_amf';
  if LinuxCaps.HasAV1AMF then
    CodecList += '|av1_amf';
  if LinuxCaps.HasQSV then
    CodecList += '|h264_qsv|hevc_qsv';
  if LinuxCaps.HasVulkan then
    CodecList += '|prores_ks_vulkan';
  if LinuxCaps.HasVulkanH264 then
    CodecList += '|h264_vulkan';
  if LinuxCaps.HasVulkanHEVC then
    CodecList += '|hevc_vulkan';
  if LinuxCaps.HasVulkanAV1 then
    CodecList += '|av1_vulkan';
  if LinuxCaps.HasMp4Box then
    CodecList += '|m4v';
  WriteLn('  -c, --codec <', CodecList, '>');
  {$ELSE}
  WriteLn('  -c, --codec <copy|prores|prores_ks|mux>');
  {$ENDIF}
{$ENDIF}
  WriteLn('  -p, --preset <preset>     Codec-specific preset (use --codecs-list for available presets)');
  WriteLn('      --profile <preset>     Deprecated alias for --preset');
  WriteLn('  -d, --deblock <none|weak|strong>');
  WriteLn('  -a, --audio-norm <none|peak|peak2|loudnorm|loudnorm2>');
  WriteLn('      --audio-output <pcm|fdk_aac_320|fdk_aac_320_ac3_640>');
{$IFDEF Windows}
  if WinCaps.HasMkvmerge then
    WriteLn('      --video-track <file> replacement video track for mux mode');
{$ELSE}
  WriteLn('      --video-track <file> replacement video track for mux mode');
{$ENDIF}
  WriteLn('  -g, --genre <edm|rock|hiphop|classical|podcast>');
  WriteLn('      (genre is used only with loudnorm2)');
  WriteLn('  --overwrite        overwrite output files');
  WriteLn('  -o, --output <directory> set output directory');
{$IFDEF Windows}
  if WinCaps.HasVulkan or WinCaps.HasVulkanH264 or WinCaps.HasVulkanHEVC or WinCaps.HasVulkanAV1 then
    WriteLn('      --vk-device <0..7> Select Vulkan adapter index for prores_ks_vulkan/h264_vulkan/hevc_vulkan/av1_vulkan');
  {$ELSE}
  {$IFDEF Linux}
  if LinuxCaps.HasVulkan or LinuxCaps.HasVulkanH264 or LinuxCaps.HasVulkanHEVC or LinuxCaps.HasVulkanAV1 then
    WriteLn('      --vk-device <0..7>  Select Vulkan adapter index for prores_ks_vulkan/h264_vulkan/hevc_vulkan/av1_vulkan');
  if LinuxCaps.HasVaapiH264 or LinuxCaps.HasVaapiHEVC then
    WriteLn('      --hw_device <path>  Override VAAPI device path (default: ', LinuxCaps.VaapiRenderNode, ')');
  {$ENDIF}
{$ENDIF}
  WriteLn('      --m4v-video-track <N>   video stream index for m4v (default: 0)');
  WriteLn('      --m4v-audio-track <N>   audio stream index for m4v (default: 0)');
  WriteLn('      --m4v-ac3-bitrate <kbps> AC3 bitrate for m4v (default: 640)');
  WriteLn('      --m4v-lang <tag>        audio language for m4v (default: rus)');
  WriteLn('      --m4v-chapters / --no-m4v-chapters');
  WriteLn('  -h, --help         show this help');
  WriteLn;
  WriteLn('Examples:');
  WriteLn('  ffmpeg_converter input.mov');
  WriteLn('  ffmpeg_converter -c prores_ks -p hq input.mov');
  WriteLn('  ffmpeg_converter -a loudnorm2 -g rock input1.mov input2.mov');
  WriteLn('  ffmpeg_converter -c mux --video-track replacement.hevc input.mkv');
  WriteLn;
end;

procedure PrintCodecsList;
var
  PresetDb: TPresetDb;
  CodecNames: TStringArray;
  PresetNames: TStringArray;
  I, J: Integer;
{$IFDEF Windows}
  WinCaps: TWindowsCodecSupport;
{$ELSE}
  {$IFDEF Linux}
  LinuxCaps: TLinuxCodecSupport;
  {$ENDIF}
{$ENDIF}
begin
  PresetDb := TPresetDb.Create;
  try
    PresetDb.Load;
    CodecNames := PresetDb.ListCodecs(CurrentPlatformName);
    WriteLn;
    WriteLn('Available Codecs and Presets:');
    WriteLn('============================');
    WriteLn;
    for I := 0 to Length(CodecNames) - 1 do
    begin
      if not IsCodecAllowedOnCurrentPlatform(CodecNames[I]) then
        Continue;
      PresetNames := PresetDb.ListPresets(CurrentPlatformName, CodecNames[I]);
      if Length(PresetNames) = 0 then
        Continue;
      WriteLn(CodecNames[I]);
      for J := 0 to Length(PresetNames) - 1 do
        WriteLn('  - ', PresetNames[J]);
      WriteLn;
    end;
  finally
    PresetDb.Free;
  end;
  Exit;

  WriteLn;
  WriteLn('Available Codecs and Presets:');
  WriteLn('============================');
  WriteLn;
  
  { Software codecs always available }
  WriteLn('copy');
  WriteLn('  - default');
  WriteLn;
  
  WriteLn('prores');
  WriteLn('  - lt');
  WriteLn('  - standard');
  WriteLn('  - hq');
  WriteLn('  - 4444');
  WriteLn;
  
  WriteLn('prores_ks');
  WriteLn('  - lt');
  WriteLn('  - standard');
  WriteLn('  - hq');
  WriteLn('  - 4444');
  WriteLn;
  
  WriteLn('mux');
  WriteLn('  - mkv');
  WriteLn('  - mov');
  WriteLn('  - m4v');
  WriteLn;

  { Hardware codecs }
{$IFDEF Windows}
  WinCaps := GetWindowsCodecSupport;
  
  if WinCaps.HasVulkan then
  begin
    WriteLn('prores_ks_vulkan');
    WriteLn('  - lt');
    WriteLn('  - standard');
    WriteLn('  - hq');
    WriteLn('  - 4444');
    WriteLn;
  end;
  
  if WinCaps.HasNVENC then
  begin
    WriteLn('h264_nvenc');
    WriteLn('  - default');
    WriteLn('  - speed');
    WriteLn('  - balance');
    WriteLn('  - quality');
    WriteLn;
    WriteLn('hevc_nvenc');
    WriteLn('  - default');
    WriteLn('  - speed');
    WriteLn('  - balance');
    WriteLn('  - quality');
    WriteLn;
  end;
  
  if WinCaps.HasAMF then
  begin
    WriteLn('h264_amf');
    WriteLn('  - default');
    WriteLn('  - speed');
    WriteLn('  - balance');
    WriteLn('  - quality');
    WriteLn;
    WriteLn('hevc_amf');
    WriteLn('  - default');
    WriteLn('  - speed');
    WriteLn('  - balance');
    WriteLn('  - quality');
    WriteLn;
  end;
  
  if WinCaps.HasAV1AMF then
  begin
    WriteLn('av1_amf');
    WriteLn('  - default');
    WriteLn('  - speed');
    WriteLn('  - balance');
    WriteLn('  - quality');
    WriteLn;
  end;
  
  if WinCaps.HasQSV then
  begin
    WriteLn('h264_qsv');
    WriteLn('  - default');
    WriteLn('  - speed');
    WriteLn('  - balance');
    WriteLn('  - quality');
    WriteLn;
    WriteLn('hevc_qsv');
    WriteLn('  - default');
    WriteLn('  - speed');
    WriteLn('  - balance');
    WriteLn('  - quality');
    WriteLn;
  end;
  
  if WinCaps.HasVulkanH264 then
  begin
    WriteLn('h264_vulkan');
    WriteLn('  - default');
    WriteLn('  - speed');
    WriteLn('  - balance');
    WriteLn('  - quality');
    WriteLn;
  end;
  
  if WinCaps.HasVulkanHEVC then
  begin
    WriteLn('hevc_vulkan');
    WriteLn('  - default');
    WriteLn('  - speed');
    WriteLn('  - balance');
    WriteLn('  - quality');
    WriteLn;
  end;
  
  if WinCaps.HasVulkanAV1 then
  begin
    WriteLn('av1_vulkan');
    WriteLn('  - default');
    WriteLn('  - speed');
    WriteLn('  - balance');
    WriteLn('  - quality');
    WriteLn;
  end;
  
  if WinCaps.HasMkvmerge then
  begin
    WriteLn('Note: Additional mux support available');
    WriteLn;
  end;
  
  if ResolveMp4BoxBin <> '' then
  begin
    WriteLn('m4v');
    WriteLn('  - default');
    WriteLn;
  end;
{$ELSE}
  {$IFDEF Linux}
  LinuxCaps := ProbeLinuxCodecSupport;
  
  if LinuxCaps.HasVaapiH264 then
  begin
    WriteLn('h264_vaapi');
    WriteLn('  - default');
    WriteLn('  - speed');
    WriteLn('  - balance');
    WriteLn('  - quality');
    WriteLn;
  end;
  
  if LinuxCaps.HasVaapiHEVC then
  begin
    WriteLn('hevc_vaapi');
    WriteLn('  - default');
    WriteLn('  - speed');
    WriteLn('  - balance');
    WriteLn('  - quality');
    WriteLn;
  end;
  
  if LinuxCaps.HasVulkan then
  begin
    WriteLn('prores_ks_vulkan');
    WriteLn('  - lt');
    WriteLn('  - standard');
    WriteLn('  - hq');
    WriteLn('  - 4444');
    WriteLn;
  end;
  
  if LinuxCaps.HasNVENC then
  begin
    WriteLn('h264_nvenc');
    WriteLn('  - default');
    WriteLn('  - speed');
    WriteLn('  - balance');
    WriteLn('  - quality');
    WriteLn;
    WriteLn('hevc_nvenc');
    WriteLn('  - default');
    WriteLn('  - speed');
    WriteLn('  - balance');
    WriteLn('  - quality');
    WriteLn;
  end;
  
  if LinuxCaps.HasAMF then
  begin
    WriteLn('h264_amf');
    WriteLn('  - default');
    WriteLn('  - speed');
    WriteLn('  - balance');
    WriteLn('  - quality');
    WriteLn;
    WriteLn('hevc_amf');
    WriteLn('  - default');
    WriteLn('  - speed');
    WriteLn('  - balance');
    WriteLn('  - quality');
    WriteLn;
  end;
  
  if LinuxCaps.HasAV1AMF then
  begin
    WriteLn('av1_amf');
    WriteLn('  - default');
    WriteLn('  - speed');
    WriteLn('  - balance');
    WriteLn('  - quality');
    WriteLn;
  end;
  
  if LinuxCaps.HasQSV then
  begin
    WriteLn('h264_qsv');
    WriteLn('  - default');
    WriteLn('  - speed');
    WriteLn('  - balance');
    WriteLn('  - quality');
    WriteLn;
    WriteLn('hevc_qsv');
    WriteLn('  - default');
    WriteLn('  - speed');
    WriteLn('  - balance');
    WriteLn('  - quality');
    WriteLn;
  end;
  
  if LinuxCaps.HasVulkanH264 then
  begin
    WriteLn('h264_vulkan');
    WriteLn('  - default');
    WriteLn('  - speed');
    WriteLn('  - balance');
    WriteLn('  - quality');
    WriteLn;
  end;
  
  if LinuxCaps.HasVulkanHEVC then
  begin
    WriteLn('hevc_vulkan');
    WriteLn('  - default');
    WriteLn('  - speed');
    WriteLn('  - balance');
    WriteLn('  - quality');
    WriteLn;
  end;
  
  if LinuxCaps.HasVulkanAV1 then
  begin
    WriteLn('av1_vulkan');
    WriteLn('  - default');
    WriteLn('  - speed');
    WriteLn('  - balance');
    WriteLn('  - quality');
    WriteLn;
  end;
  
  if LinuxCaps.HasMp4Box then
  begin
    WriteLn('m4v');
    WriteLn('  - default');
    WriteLn;
  end;
  {$ENDIF}
{$ENDIF}
end;

function ParseArgsFromArray(var Opts: TConvertOptions; out Files: array of PAnsiChar; out FileCount: LongInt; const Args: TStringArray): Boolean;
var
  I: LongInt;
  S: string;
  Codec: string;
  ResolvedDir: string;
  DirError: string;
  OutputDirExplicitlySet: Boolean;
  M4VVideoTrackIdx: Integer;
  M4VAudioTrackIdx: Integer;
  M4VAc3Bitrate: Integer;
  M4VLang: string;
  M4VAddChapters: Boolean;
  ParsedInt: Integer;
  PresetExplicitlySet: Boolean;
  {$IFDEF Linux}
  LinuxCaps: TLinuxCodecSupport;
  {$ENDIF}
begin
  InitDefaultOptions(Opts);
  FileCount := 0;
  M4VVideoTrackIdx := 0;
  M4VAudioTrackIdx := 0;
  M4VAc3Bitrate := 640;
  M4VLang := 'rus';
  M4VAddChapters := True;
  OutputDirExplicitlySet := False;
  PresetExplicitlySet := False;

  for I := 0 to High(Files) do
    Files[I] := nil;

  I := 1;
  while I <= High(Args) do
  begin
    S := Args[I];

    if (S = '-h') or (S = '--help') then
      Exit(False);

    if (S = '--codec') or (S = '-c') then
    begin
      if I + 1 > High(Args) then
        Exit(False);
      Inc(I);
      S := Args[I];
      if IsCodecAllowedOnCurrentPlatform(S) then
      begin
        SetAnsiField(Opts.codec, S);
{$IFDEF Linux}
        { For VAAPI codecs, populate hw_device from the probe result if not
          explicitly set by --hw_device.  ProbeLinuxCodecSupport is cached. }
        if (S = 'h264_vaapi') or (S = 'hevc_vaapi') then
        begin
          if ArrToStr(Opts.hw_device) = '' then
          begin
            LinuxCaps := ProbeLinuxCodecSupport;
            if LinuxCaps.VaapiRenderNode <> '' then
              SetAnsiField(Opts.hw_device, LinuxCaps.VaapiRenderNode)
            else
              WriteLn(StdErr, 'Warning: VAAPI render node not detected - encoding may fail');
          end;
        end;
        if S = 'm4v' then
        begin
          if ResolveMp4BoxBin = '' then
          begin
            WriteLn(StdErr, 'Error: MP4Box not found. Install GPAC for m4v support.');
            Exit(False);
          end;
        end;
{$ENDIF}
      end
      else
      begin
        WriteLn(StdErr, 'Error: codec is not supported on this platform: ', S);
        Exit(False);
      end;
      Inc(I);
      Continue;
    end;

    if (S = '--preset') or (S = '-p') or (S = '--profile') then
    begin
      if I + 1 > High(Args) then
        Exit(False);
      Inc(I);
      S := Args[I];
      SetAnsiField(Opts.preset, S);
      PresetExplicitlySet := True;
      Inc(I);
      Continue;
    end;

    if (S = '--deblock') or (S = '-d') then
    begin
      if I + 1 > High(Args) then
        Exit(False);
      Inc(I);
      S := Args[I];
      if S = 'none' then Opts.deblock := 1
      else if S = 'weak' then Opts.deblock := 2
      else if S = 'strong' then Opts.deblock := 3
      else Exit(False);
      Inc(I);
      Continue;
    end;

    if (S = '--audio-norm') or (S = '-a') then
    begin
      if I + 1 > High(Args) then
        Exit(False);
      Inc(I);
      S := Args[I];
      if S = 'none' then SetAnsiField(Opts.audio_norm, 'none')
      else if S = 'peak' then SetAnsiField(Opts.audio_norm, 'peak_norm')
      else if S = 'peak2' then SetAnsiField(Opts.audio_norm, 'peak_norm_2pass')
      else if S = 'loudnorm' then SetAnsiField(Opts.audio_norm, 'loudness_norm')
      else if S = 'loudnorm2' then SetAnsiField(Opts.audio_norm, 'loudness_norm_2pass')
      else Exit(False);
      Inc(I);
      Continue;
    end;

    if S = '--audio-output' then
    begin
      if I + 1 > High(Args) then
        Exit(False);
      Inc(I);
      S := Args[I];
      if S = 'fdk_aac_320' then
        S := 'fdk_aac_320'
      else if S = 'fdk_aac_320_ac3_640' then
        S := 'fdk_aac_320_ac3_640';

      if (S = 'pcm') or (S = 'fdk_aac_320') or (S = 'fdk_aac_320_ac3_640') then
        SetAnsiField(Opts.audio_output_mode, S)
      else
        Exit(False);
      Inc(I);
      Continue;
    end;

    if S = '--video-track' then
    begin
      if I + 1 > High(Args) then
        Exit(False);
      Inc(I);
      S := Args[I];
      SetAnsiField(Opts.video_track_path, S);
      Inc(I);
      Continue;
    end;

    if (S = '--genre') or (S = '-g') then
    begin
      if I + 1 > High(Args) then
        Exit(False);
      Inc(I);
      S := Args[I];
      if S = 'edm' then Opts.genre := 1
      else if S = 'rock' then Opts.genre := 2
      else if S = 'hiphop' then Opts.genre := 3
      else if S = 'classical' then Opts.genre := 4
      else if S = 'podcast' then Opts.genre := 5
      else Exit(False);
      Inc(I);
      Continue;
    end;

    if S = '--overwrite' then
    begin
      Opts.overwrite := 1;
      Inc(I);
      Continue;
    end;

    if (S = '--vk-device') or (S = '--vk_device') then
    begin
      if I + 1 > High(Args) then
        Exit(False);
      Inc(I);
      S := Args[I];
      if not TryStrToInt(S, ParsedInt) then
        Exit(False);
      if (ParsedInt < 0) or (ParsedInt > 7) then
        Exit(False);
      Opts.vulkan_device := ParsedInt;
      Inc(I);
      Continue;
    end;

    if S = '--m4v-video-track' then
    begin
      if I + 1 > High(Args) then
        Exit(False);
      Inc(I);
      if not TryStrToInt(Args[I], ParsedInt) then
        Exit(False);
      if ParsedInt < 0 then
        Exit(False);
      M4VVideoTrackIdx := ParsedInt;
      Inc(I);
      Continue;
    end;

    if S = '--m4v-audio-track' then
    begin
      if I + 1 > High(Args) then
        Exit(False);
      Inc(I);
      if not TryStrToInt(Args[I], ParsedInt) then
        Exit(False);
      if ParsedInt < 0 then
        Exit(False);
      M4VAudioTrackIdx := ParsedInt;
      Inc(I);
      Continue;
    end;

    if S = '--m4v-ac3-bitrate' then
    begin
      if I + 1 > High(Args) then
        Exit(False);
      Inc(I);
      if not TryStrToInt(Args[I], ParsedInt) then
        Exit(False);
      if ParsedInt <= 0 then
        Exit(False);
      M4VAc3Bitrate := ParsedInt;
      Inc(I);
      Continue;
    end;

    if S = '--m4v-lang' then
    begin
      if I + 1 > High(Args) then
        Exit(False);
      Inc(I);
      M4VLang := Trim(Args[I]);
      if M4VLang = '' then
        Exit(False);
      Inc(I);
      Continue;
    end;

    if S = '--m4v-chapters' then
    begin
      M4VAddChapters := True;
      Inc(I);
      Continue;
    end;

    if S = '--no-m4v-chapters' then
    begin
      M4VAddChapters := False;
      Inc(I);
      Continue;
    end;

    if (S = '-o') or (S = '--output') then
    begin
      if I + 1 > High(Args) then
        Exit(False);
      Inc(I);
      S := Args[I];
      OutputDirExplicitlySet := True;
      if not EnsureOutputDirWritable(S, ResolvedDir, DirError) then
      begin
        Opts.output_dir_status := 0;
        WriteLn(StdErr, 'Warning: ', DirError);
        WriteLn(StdErr, 'Will use default output directory');
      end
      else
      begin
        SetAnsiField(Opts.output_dir, ResolvedDir);
        Opts.output_dir_status := 1;
      end;

      Inc(I);
      Continue;
    end;

    if S = '--hw_device' then
    begin
      if I + 1 > High(Args) then
        Exit(False);
      Inc(I);
      S := Args[I];
      SetAnsiField(Opts.hw_device, S);
      Inc(I);
      Continue;
    end;

    if (Length(S) > 0) and (S[1] <> '-') then
    begin
      if FileCount >= Length(Files) then
        Exit(False);
      Files[FileCount] := StrNew(PAnsiChar(AnsiString(S)));
      if Files[FileCount] = nil then
        Exit(False);
      Inc(FileCount);
      Inc(I);
      Continue;
    end;

    Exit(False);
  end;

  if OutputDirExplicitlySet then
  begin
    if not EnsureOutputDirWritable(ArrToStr(Opts.output_dir), ResolvedDir, DirError) then
    begin
      Opts.output_dir_status := 0;
      WriteLn(StdErr, 'Warning: ', DirError);
      WriteLn(StdErr, 'Will use default output directory');
      Opts.output_dir[0] := #0;
    end
    else
    begin
      SetAnsiField(Opts.output_dir, ResolvedDir);
      Opts.output_dir_status := 1;
    end;
  end;

  Codec := ArrToStr(Opts.codec);
  if not PresetExplicitlySet then
    { No --preset/-p/--profile on the command line: the compile-time default
      ('standard') only applies to the ProRes family, so resolve the codec's
      own first preset instead of validating an unrelated fixed value. }
    SetAnsiField(Opts.preset, DefaultPresetForCodec(Codec))
  else if not IsPresetAllowed(Codec, ArrToStr(Opts.preset)) then
  begin
    WriteLn(StdErr, 'Error: preset ''', ArrToStr(Opts.preset),
      ''' is not available for codec ''', Codec,
      '''. Use --codecs-list to see valid combinations.');
    Exit(False);
  end;

  if Codec = 'mux' then
  begin
    if FileCount <> 1 then
    begin
      WriteLn(StdErr, 'Error: mux mode requires exactly one source file.');
      Exit(False);
    end;

    if not FileRegular(ArrToStr(Opts.video_track_path)) or not FileReadable(ArrToStr(Opts.video_track_path)) then
    begin
      WriteLn(StdErr, 'Error: mux mode requires a readable --video-track file.');
      Exit(False);
    end;
  end;

    if Codec = 'm4v' then
    begin
      SetAnsiField(Opts.video_track_path,
        IntToStr(M4VVideoTrackIdx) + '|' +
        IntToStr(M4VAudioTrackIdx) + '|' +
        IntToStr(M4VAc3Bitrate) + '|' +
        M4VLang + '|' +
        IntToStr(Ord(M4VAddChapters)));
    end;

  Result := True;
end;

function ParseArgs(var Opts: TConvertOptions; out Files: array of PAnsiChar; out FileCount: LongInt): Boolean;
var
  A: TStringArray;
  I: Integer;
begin
  SetLength(A, ParamCount + 1);
  for I := 0 to ParamCount do
    A[I] := ParamStr(I);
  Result := ParseArgsFromArray(Opts, Files, FileCount, A);
end;

procedure PrintSummary(const Opts: TConvertOptions; const Files: array of PAnsiChar; FileCount: LongInt);
var
  I: LongInt;
  Codec: string;
  AudioNorm: string;
  OutDir: string;
begin
  Write(#27'[1;1H'#27'[2J');
  WriteLn;
  WriteLn('=== Summary ===');

  Codec := ArrToStr(Opts.codec);
  AudioNorm := ArrToStr(Opts.audio_norm);
  OutDir := ArrToStr(Opts.output_dir);

  WriteLn('Codec:        ', Codec);

  if (Codec <> 'copy') and (Codec <> 'mux') then
  begin
    WriteLn('Profile:      ', ArrToStr(Opts.preset));
    WriteLn('Deblock:      ', DeblockToText(Opts.deblock));
  end
  else
  begin
    WriteLn('Profile:      (copy)');
    WriteLn('Deblock:      (copy)');
  end;

  WriteLn('Audio norm:   ', AudioNorm);
  WriteLn('Audio out:    ', ArrToStr(Opts.audio_output_mode));

  if Codec = 'mux' then
    WriteLn('Video track:  ', ArrToStr(Opts.video_track_path));

  if AudioNorm = 'loudness_norm_2pass' then
    WriteLn('Genre:        ', GenreToText(Opts.genre));

  if Opts.overwrite <> 0 then
    WriteLn('Overwrite:    yes')
  else
    WriteLn('Overwrite:    no');

  if OutDir <> '' then
    WriteLn('Output dir:   ', OutDir)
  else
    WriteLn('Output dir:   (same as input)');

  if OutDir <> '' then
  begin
    if Opts.output_dir_status <> 0 then
      WriteLn('Dir status:   OK')
    else
      WriteLn('Dir status:   ERROR (directory missing or not writable)');
  end;

  WriteLn;
  WriteLn('Files (', FileCount, '):');
  for I := 0 to FileCount - 1 do
  begin
    if Pos(' ', string(Files[I])) > 0 then
      WriteLn('  "', string(Files[I]), '"')
    else
      WriteLn('  ', string(Files[I]));
  end;
  WriteLn('===============');
end;

function VerifyAndCompactFiles(var Files: array of PAnsiChar; var FileCount: LongInt): Boolean;
var
  I: LongInt;
  ValidCount: LongInt;
  Line: string;
  FilePath: string;
{$IFNDEF Windows}
  Info: Stat;
  Readable: Boolean;
{$ENDIF}
begin
  ValidCount := 0;
  WriteLn;
  WriteLn('Verifying files...');

  for I := 0 to FileCount - 1 do
  begin
    FilePath := string(Files[I]);
{$IFDEF Windows}
    if not FileExists(FilePath) then
    begin
      WriteLn('  X File not found: ', FilePath);
      if Files[I] <> nil then
      begin
        StrDispose(Files[I]);
        Files[I] := nil;
      end;
      Continue;
    end;

    if not FileRegular(FilePath) then
    begin
      WriteLn('  X Not a regular file: ', FilePath);
      if Files[I] <> nil then
      begin
        StrDispose(Files[I]);
        Files[I] := nil;
      end;
      Continue;
    end;

    if not FileReadable(FilePath) then
    begin
      WriteLn('  X File not readable: ', FilePath);
      if Files[I] <> nil then
      begin
        StrDispose(Files[I]);
        Files[I] := nil;
      end;
      Continue;
    end;
{$ELSE}
    if fpStat(PChar(FilePath), Info) <> 0 then
    begin
      WriteLn('  X File not found: ', FilePath);
      WriteLn('      Error: ', SysErrorMessage(fpgeterrno));
      if Files[I] <> nil then
      begin
        StrDispose(Files[I]);
        Files[I] := nil;
      end;
      Continue;
    end;

    if not FPS_ISREG(Info.st_mode) then
    begin
      WriteLn('  X Not a regular file: ', FilePath);
      if Files[I] <> nil then
      begin
        StrDispose(Files[I]);
        Files[I] := nil;
      end;
      Continue;
    end;

    Readable := fpAccess(PChar(FilePath), R_OK) = 0;
    if not Readable then
    begin
      WriteLn('  X File not readable: ', FilePath);
      if Files[I] <> nil then
      begin
        StrDispose(Files[I]);
        Files[I] := nil;
      end;
      Continue;
    end;
{$ENDIF}

    WriteLn('  + OK: ', FilePath);
    if ValidCount <> I then
    begin
      Files[ValidCount] := Files[I];
      Files[I] := nil;
    end;
    Inc(ValidCount);
  end;

  WriteLn;
  WriteLn('Found ', ValidCount, ' valid file(s) out of ', FileCount);

  if ValidCount = 0 then
  begin
    WriteLn('No valid files to process.');
    FileCount := 0;
    Exit(False);
  end;

  if ValidCount < FileCount then
  begin
    Write('Continue with ', ValidCount, ' file(s)? [y/N]: ');
    ReadLn(Line);
    Line := Trim(Line);
    if (Line = '') or not (Line[1] in ['y', 'Y']) then
    begin
      FileCount := ValidCount;
      Exit(False);
    end;
  end;

  FileCount := ValidCount;
  Result := True;
end;

end.
