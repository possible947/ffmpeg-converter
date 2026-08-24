unit converter_cmd_builder;

{$mode objfpc}{$H+}

interface

uses converter_types;

function BuildFfmpegCommand(const Opts: TConvertOptions; const InputFile, OutputFile: string): string;

implementation

uses
  SysUtils,
  {$IFDEF Linux}
  linux_probe,
  {$ENDIF}
  path_utils,
  tool_paths;

function ArrToStr(const A: array of AnsiChar): string;
begin
  Result := StrPas(@A[0]);
end;

function PresetToProfileNum(const Preset: string): LongInt;
begin
  if Preset = 'lt' then
    Result := 1
  else if Preset = 'hq' then
    Result := 3
  else if Preset = '4444' then
    Result := 4
  else
    Result := 2;  { standard or default }
end;

{ Phase 2 speed/balance/quality preset tiers for GPU codecs.
  `default` strings are byte-for-byte identical to pre-Phase-2 behavior —
  zero regression for existing users/scripts. Values mirror presets.json
  Section 5 (v3.0-Phase2.md / src/converter/platform/converter_linux.c,
  converter_windows.c) and must be kept in sync with both. Returns ''
  if Codec is not a recognized Phase-2 tiered GPU codec. }
function GetHwCodecFlags(const Codec, Preset: string): string;
begin
  Result := '';

  if Codec = 'h264_vaapi' then
  begin
    if Preset = 'speed' then Result := '-c:v h264_vaapi -rc_mode CQP -qp 28 '
    else if Preset = 'balance' then Result := '-c:v h264_vaapi -rc_mode CQP -qp 24 '
    else if Preset = 'quality' then Result := '-c:v h264_vaapi -rc_mode CQP -qp 20 '
    else Result := '-c:v h264_vaapi -rc_mode auto ';
  end
  else if Codec = 'hevc_vaapi' then
  begin
    if Preset = 'speed' then Result := '-c:v hevc_vaapi -rc_mode CQP -qp 28 '
    else if Preset = 'balance' then Result := '-c:v hevc_vaapi -rc_mode CQP -qp 24 '
    else if Preset = 'quality' then Result := '-c:v hevc_vaapi -rc_mode CQP -qp 20 '
    else Result := '-c:v hevc_vaapi -rc_mode auto ';
  end
  else if Codec = 'h264_nvenc' then
  begin
    if Preset = 'speed' then Result := '-c:v h264_nvenc -preset p1 -qp 22 -spatial_aq 1 -temporal_aq 1 '
    else if Preset = 'balance' then Result := '-c:v h264_nvenc -preset p4 -qp 22 -spatial_aq 1 -temporal_aq 1 '
    else Result := '-c:v h264_nvenc -preset p7 -qp 22 -spatial_aq 1 -temporal_aq 1 ';
  end
  else if Codec = 'hevc_nvenc' then
  begin
    if Preset = 'speed' then Result := '-c:v hevc_nvenc -preset p1 -cq 25 -lookahead_level 0 '
    else if Preset = 'balance' then Result := '-c:v hevc_nvenc -preset p4 -cq 25 -lookahead_level auto '
    else Result := '-c:v hevc_nvenc -preset hq -cq 25 -lookahead_level auto ';
  end
  else if Codec = 'av1_nvenc' then
  begin
    { default uses p6 (distinct from 'quality' = p7), mirroring
      hevc_nvenc's default('hq') != quality('p7') split; av1_nvenc has
      no 'hq' alias, so p6 is the nearest equivalent. }
    if Preset = 'speed' then Result := '-c:v av1_nvenc -preset p1 -cq 30 -lookahead_level 0 '
    else if Preset = 'balance' then Result := '-c:v av1_nvenc -preset p4 -cq 30 -lookahead_level auto '
    else if Preset = 'quality' then Result := '-c:v av1_nvenc -preset p7 -cq 30 -lookahead_level auto '
    else Result := '-c:v av1_nvenc -preset p6 -cq 30 -lookahead_level auto ';
  end
  else if Codec = 'h264_amf' then
  begin
    if Preset = 'speed' then Result := '-c:v h264_amf -quality speed '
    else if Preset = 'balance' then Result := '-c:v h264_amf -quality balanced '
    else if Preset = 'quality' then Result := '-c:v h264_amf -quality quality '
    else Result := '-c:v h264_amf ';
  end
  else if Codec = 'hevc_amf' then
  begin
    if Preset = 'speed' then Result := '-c:v hevc_amf -quality speed '
    else if Preset = 'balance' then Result := '-c:v hevc_amf -quality balanced '
    else if Preset = 'quality' then Result := '-c:v hevc_amf -quality quality '
    else Result := '-c:v hevc_amf ';
  end
  else if Codec = 'av1_amf' then
  begin
    if Preset = 'speed' then Result := '-c:v av1_amf -quality speed '
    else if Preset = 'balance' then Result := '-c:v av1_amf -quality balanced '
    else if Preset = 'quality' then Result := '-c:v av1_amf -quality quality '
    else Result := '-c:v av1_amf ';
  end
  else if Codec = 'h264_qsv' then
  begin
    if Preset = 'speed' then Result := '-c:v h264_qsv -global_quality 22 -preset veryfast -extbrc 1 '
    else if Preset = 'balance' then Result := '-c:v h264_qsv -global_quality 22 -preset medium -look_ahead 1 -look_ahead_depth 40 -extbrc 1 '
    else Result := '-c:v h264_qsv -global_quality 22 -preset slower -look_ahead 1 -look_ahead_depth 40 -extbrc 1 ';
  end
  else if Codec = 'hevc_qsv' then
  begin
    if Preset = 'speed' then Result := '-c:v hevc_qsv -global_quality 25 -preset fast -g 240 -bf 4 '
    else if Preset = 'balance' then Result := '-c:v hevc_qsv -global_quality 25 -preset medium -g 240 -bf 4 -look_ahead 1 -look_ahead_depth 60 -extbrc 1 '
    else Result := '-c:v hevc_qsv -global_quality 25 -preset slow -g 240 -bf 4 -look_ahead 1 -look_ahead_depth 60 -extbrc 1 ';
  end
  else if Codec = 'av1_qsv' then
  begin
    { default: -g 240 -bf 4 added to match hevc_qsv's tuned default. }
    if Preset = 'speed' then Result := '-c:v av1_qsv -global_quality 28 -preset veryfast -extbrc 1 '
    else if Preset = 'balance' then Result := '-c:v av1_qsv -global_quality 28 -preset medium -look_ahead 1 -look_ahead_depth 60 -extbrc 1 '
    else Result := '-c:v av1_qsv -global_quality 28 -preset slow -g 240 -bf 4 -look_ahead 1 -look_ahead_depth 60 -extbrc 1 ';
  end
  else if Codec = 'h264_vulkan' then
  begin
    if Preset = 'speed' then Result := '-c:v h264_vulkan -qp 28 '
    else if Preset = 'balance' then Result := '-c:v h264_vulkan -qp 23 '
    else Result := '-c:v h264_vulkan -qp 18 ';
  end
  else if Codec = 'hevc_vulkan' then
  begin
    if Preset = 'speed' then Result := '-c:v hevc_vulkan -qp 28 '
    else if Preset = 'balance' then Result := '-c:v hevc_vulkan -qp 23 '
    else Result := '-c:v hevc_vulkan -qp 18 ';
  end
  else if Codec = 'av1_vulkan' then
  begin
    if Preset = 'speed' then Result := '-c:v av1_vulkan -qp 120 '
    else if Preset = 'balance' then Result := '-c:v av1_vulkan -qp 90 '
    else Result := '-c:v av1_vulkan -qp 60 ';
  end;
end;

function IsHwVulkanCodec(const Codec: string): Boolean;
begin
  Result := (Codec = 'h264_vulkan') or (Codec = 'hevc_vulkan') or (Codec = 'av1_vulkan');
end;

function InvariantFmt: TFormatSettings;
begin
  Result := DefaultFormatSettings;
  Result.DecimalSeparator := '.';
end;

function BuildFfmpegCommand(const Opts: TConvertOptions; const InputFile, OutputFile: string): string;
var
  Codec: string;
  AudioNorm: string;
  AudioOut: string;
  DevicePath: string;
  FfmpegBin: string;
  Tools: TToolPaths;
  Fmt: TFormatSettings;
{$IFDEF Linux}
  InputCodec: string;
  AV1Decoder: string;
{$ENDIF}
begin
  Codec := ArrToStr(Opts.codec);
  AudioNorm := ArrToStr(Opts.audio_norm);
  AudioOut := ArrToStr(Opts.audio_output_mode);
  if AudioOut = 'fdk_aac_320' then
    AudioOut := 'fdk_aac_320'
  else if AudioOut = 'fdk_aac_320_ac3_640' then
    AudioOut := 'fdk_aac_320_ac3_640';
  Tools := ResolveToolPaths;
  FfmpegBin := Tools.FfmpegBin;
  Fmt := InvariantFmt;

  { --- Overwrite flag --- }
  if Opts.overwrite <> 0 then
    Result := QuoteForShell(FfmpegBin) + ' -y '
  else
    Result := QuoteForShell(FfmpegBin) + ' -n ';

  { --- Pre-input hardware device setup ---
    Vulkan and VAAPI need their device context established before -i.
    NVENC/AMF/QSV auto-select the GPU and need nothing here. }
  if Codec = 'prores_ks_vulkan' then
  begin
    { Device index -1 means "auto" (first available); clamp to 0 for ffmpeg. }
    if Opts.vulkan_device < 0 then
      Result += '-init_hw_device vulkan=vk:0 -filter_hw_device vk '
    else
      Result += '-init_hw_device vulkan=vk:' + IntToStr(Opts.vulkan_device) + ' -filter_hw_device vk ';
  end
  else if IsHwVulkanCodec(Codec) then
  begin
    { h264_vulkan/hevc_vulkan/av1_vulkan share the same pre-input device
      init pattern and probed --vk_device index as prores_ks_vulkan. }
    if Opts.vulkan_device < 0 then
      Result += '-init_hw_device vulkan=vk:0 -filter_hw_device vk '
    else
      Result += '-init_hw_device vulkan=vk:' + IntToStr(Opts.vulkan_device) + ' -filter_hw_device vk ';
  end
  else if (Codec = 'h264_vaapi') or (Codec = 'hevc_vaapi') then
  begin
    DevicePath := ArrToStr(Opts.hw_device);
    if DevicePath = '' then
      DevicePath := GetEnvironmentVariable('VAAPI_DEVICE');
    if DevicePath = '' then
      DevicePath := '/dev/dri/renderD128';
    Result += '-vaapi_device ' + QuoteForShell(DevicePath) + ' ';
  end;

  { --- AV1 input decoder selection (Linux) ---
    The native av1 decoder in ffmpeg builds with --enable-nvdec probes NVDEC
    pixel formats internally, which causes a fatal crash on systems where the
    NVIDIA GPU does not support AV1 hardware decode.  We avoid this by
    preferring av1_qsv (Intel QSV) or libdav1d (pure software) when available.
    For non-AV1 inputs -hwaccel none is added to keep software decoding. }
{$IFDEF Linux}
  InputCodec := ProbeInputVideoCodec(Tools.FfprobeBin, InputFile);
  if InputCodec = 'av1' then
  begin
    AV1Decoder := GetBestAV1Decoder(FfmpegBin);
    if AV1Decoder = 'av1_qsv' then
      Result += '-hwaccel qsv -hwaccel_output_format nv12 -c:v av1_qsv '
    else if AV1Decoder = 'libdav1d' then
      Result += '-hwaccel none -c:v libdav1d '
    else
      Result += '-hwaccel none ';
  end
  else
    Result += '-hwaccel none ';
{$ENDIF}

  { --- Input file --- }
  Result += '-i ' + QuoteForShell(InputFile) + ' ';

  Result += '-map 0:v:0 ';
  if AudioOut <> 'fdk_aac_320_ac3_640' then
    Result += '-map 0:a:0 ';
  Result += '-map_metadata 0 ';

  if (Codec = 'prores') or (Codec = 'prores_ks') then
    Result += Format('-c:v %s -profile:v %d ', [Codec, PresetToProfileNum(ArrToStr(Opts.preset))], Fmt)
  else if Codec = 'prores_videotoolbox' then
    Result += Format('-c:v prores_videotoolbox -profile:v %d -allow_sw 1 ', [PresetToProfileNum(ArrToStr(Opts.preset))], Fmt)
  else if Codec = 'hevc_videotoolbox' then
  begin
    if Opts.hevc_vt_bitrate_kbps > 0 then
      Result += Format('-c:v hevc_videotoolbox -b:v %dk -tag:v hvc1 -spatial_aq 1 ',
        [Opts.hevc_vt_bitrate_kbps], Fmt)
    else
      Result += '-c:v hevc_videotoolbox -b:v 35000k -tag:v hvc1 -spatial_aq 1 ';
  end
  else if (Codec = 'h264_vaapi') or (Codec = 'hevc_vaapi') or
          (Codec = 'h264_nvenc') or (Codec = 'hevc_nvenc') or (Codec = 'av1_nvenc') or
          (Codec = 'h264_amf') or (Codec = 'hevc_amf') or (Codec = 'av1_amf') or
          (Codec = 'h264_qsv') or (Codec = 'hevc_qsv') or (Codec = 'av1_qsv') or
          (Codec = 'h264_vulkan') or (Codec = 'hevc_vulkan') or (Codec = 'av1_vulkan') then
    Result += GetHwCodecFlags(Codec, ArrToStr(Opts.preset))
  else if Codec = 'prores_ks_vulkan' then
  begin
    case PresetToProfileNum(ArrToStr(Opts.preset)) of
      1: Result += '-c:v prores_ks_vulkan -profile:v lt ';
      3: Result += '-c:v prores_ks_vulkan -profile:v hq ';
      4: Result += '-c:v prores_ks_vulkan -profile:v 4444 ';
    else
      Result += '-c:v prores_ks_vulkan -profile:v standard ';
    end;
  end
  else if Codec = 'm4v' then
    Result += '-c:v copy '
  else
    Result += '-c:v copy ';

  if Codec = 'prores_ks_vulkan' then
  begin
    if PresetToProfileNum(ArrToStr(Opts.preset)) = 4 then
      Result += '-vf "format=yuv444p10le,hwupload" '
    else
      Result += '-vf "format=yuv422p10le,hwupload" ';
  end
  else if (Codec = 'h264_vaapi') or (Codec = 'hevc_vaapi') or IsHwVulkanCodec(Codec) then
    Result += '-vf "format=nv12,hwupload" '
  else if (Codec <> 'hevc_videotoolbox') and (Codec <> 'prores_videotoolbox') then
  begin
    if Opts.deblock = 2 then
      Result += '-vf "deblock=filter=weak:block=4:planes=1" '
    else if Opts.deblock = 3 then
      Result += '-vf "deblock=filter=strong:block=4:alpha=0.12:beta=0.07:gamma=0.06:delta=0.05:planes=1" ';
  end;

  if (AudioOut = 'fdk_aac_320_ac3_640') then
    Result += '-filter_complex "[0:a:0]aresample=resampler=soxr:precision=28:cheby=1,asplit=2[aout0][aout1]" -map [aout0] -map [aout1] ';

  { --- Audio codec ---
    Windows and Linux: require libfdk_aac (validated upstream in converter_set_options).
    macOS and other Unix-like systems: fall back to native aac encoder. }
  if AudioOut = 'fdk_aac_320_ac3_640' then
  {$IFDEF Linux}
    Result += '-c:a:0 libfdk_aac -b:a:0 320k -ar:a:0 48000 -c:a:1 ac3 -b:a:1 640k -ar:a:1 48000 '
  {$ELSE}
  {$IFDEF Windows}
    Result += '-c:a:0 libfdk_aac -b:a:0 320k -ar:a:0 48000 -c:a:1 ac3 -b:a:1 640k -ar:a:1 48000 '
  {$ELSE}
    Result += '-c:a:0 aac -b:a:0 320k -ar:a:0 48000 -c:a:1 ac3 -b:a:1 640k -ar:a:1 48000 '
  {$ENDIF}
  {$ENDIF}
  else if AudioOut = 'fdk_aac_320' then
  {$IFDEF Linux}
    Result += '-c:a libfdk_aac -b:a 320k -ar 48000 '
  {$ELSE}
  {$IFDEF Windows}
    Result += '-c:a libfdk_aac -b:a 320k -ar 48000 '
  {$ELSE}
    Result += '-c:a aac -b:a 320k -ar 48000 '
  {$ENDIF}
  {$ENDIF}
  else if Opts.use_aac_for_h265 <> 0 then
    Result += '-c:a aac -b:a 320k -ar 48000 '
  else
    Result += '-c:a pcm_s16le -ar 48000 ';

  if AudioOut <> 'fdk_aac_320_ac3_640' then
  begin
    if AudioNorm = 'none' then
      Result += '-af "aresample=resampler=soxr:precision=28:cheby=1" '
    else if AudioNorm = 'peak_norm' then
      Result += '-af "aresample=resampler=soxr:precision=28:cheby=1,volume=-3dB" '
    else if AudioNorm = 'peak_norm_2pass' then
      Result += Format('-af "aresample=resampler=soxr:precision=28:cheby=1,volume=%.2fdB" ', [Opts.gain], Fmt)
    else if AudioNorm = 'loudness_norm' then
      Result += '-af "aresample=resampler=soxr:precision=28:cheby=1,loudnorm=I=-11:TP=-1.5:LRA=7" '
    else if AudioNorm = 'loudness_norm_2pass' then
      Result += Format('-af "aresample=resampler=soxr:precision=28:cheby=1,loudnorm=I=%.1f:TP=%.1f:LRA=%.1f:measured_I=%.2f:measured_TP=%.2f:measured_LRA=%.2f:measured_thresh=%.2f:offset=%.2f:linear=true" ',
        [Opts.I_target, Opts.TP_target, Opts.LRA_target, Opts.measured_I, Opts.measured_TP, Opts.measured_LRA, Opts.measured_thresh, Opts.measured_offset], Fmt);
  end;

  Result += '-progress pipe:1 -nostats -nostdin ';
  Result += QuoteForShell(OutputFile);
end;

end.
