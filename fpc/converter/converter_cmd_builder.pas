unit converter_cmd_builder;

{$mode objfpc}{$H+}

interface

uses converter_types;

function BuildFfmpegCommand(const Opts: TConvertOptions; const InputFile, OutputFile: string): string;

implementation

uses
  SysUtils,
  path_utils,
  tool_paths;

function ArrToStr(const A: array of AnsiChar): string;
begin
  Result := StrPas(@A[0]);
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
begin
  Codec := ArrToStr(Opts.codec);
  AudioNorm := ArrToStr(Opts.audio_norm);
  AudioOut := ArrToStr(Opts.audio_output_mode);
  Tools := ResolveToolPaths;
  FfmpegBin := Tools.FfmpegBin;
  Fmt := InvariantFmt;

  if (Codec = 'h264_vaapi') or (Codec = 'hevc_vaapi') then
  begin
    DevicePath := ArrToStr(Opts.hw_device);
    if DevicePath = '' then
      DevicePath := GetEnvironmentVariable('VAAPI_DEVICE');
    if DevicePath = '' then
      DevicePath := '/dev/dri/renderD128';
    if Opts.overwrite <> 0 then
      Result := QuoteForShell(FfmpegBin) + ' -y -vaapi_device ' + QuoteForShell(DevicePath) + ' -i ' + QuoteForShell(InputFile) + ' '
    else
      Result := QuoteForShell(FfmpegBin) + ' -n -vaapi_device ' + QuoteForShell(DevicePath) + ' -i ' + QuoteForShell(InputFile) + ' ';
  end
  else
  begin
    if Opts.overwrite <> 0 then
      Result := QuoteForShell(FfmpegBin) + ' -y -i ' + QuoteForShell(InputFile) + ' '
    else
      Result := QuoteForShell(FfmpegBin) + ' -n -i ' + QuoteForShell(InputFile) + ' ';
  end;

  Result += '-map 0:v:0 ';
  if AudioOut <> 'fdk_aac_q5_ac3_640' then
    Result += '-map 0:a:0 ';
  Result += '-map_metadata 0 ';

  if (Codec = 'prores') or (Codec = 'prores_ks') then
    Result += Format('-c:v %s -profile:v %d ', [Codec, Opts.profile], Fmt)
  else if Codec = 'prores_videotoolbox' then
    Result += Format('-c:v prores_videotoolbox -profile:v %d -allow_sw 1 ', [Opts.profile], Fmt)
  else if Codec = 'hevc_videotoolbox' then
  begin
    if Opts.hevc_vt_bitrate_kbps > 0 then
      Result += Format('-c:v hevc_videotoolbox -b:v %dk -tag:v hvc1 -spatial_aq 1 ',
        [Opts.hevc_vt_bitrate_kbps], Fmt)
    else
      Result += '-c:v hevc_videotoolbox -b:v 35000k -tag:v hvc1 -spatial_aq 1 ';
  end
  else if Codec = 'h264_vaapi' then
    Result += '-c:v h264_vaapi -rc_mode auto '
  else if Codec = 'hevc_vaapi' then
    Result += '-c:v hevc_vaapi -rc_mode auto '
  else
    Result += '-c:v copy ';

  if (Codec = 'h264_vaapi') or (Codec = 'hevc_vaapi') then
    Result += '-vf "format=nv12,hwupload" '
  else if (Codec <> 'hevc_videotoolbox') and (Codec <> 'prores_videotoolbox') then
  begin
    if Opts.deblock = 2 then
      Result += '-vf "deblock=filter=weak:block=4:planes=1" '
    else if Opts.deblock = 3 then
      Result += '-vf "deblock=filter=strong:block=4:alpha=0.12:beta=0.07:gamma=0.06:delta=0.05:planes=1" ';
  end;

  if (AudioOut = 'fdk_aac_q5_ac3_640') then
    Result += '-filter_complex "[0:a:0]aresample=resampler=soxr:precision=28:cheby=1,asplit=2[aout0][aout1]" -map [aout0] -map [aout1] ';

  if AudioOut = 'fdk_aac_q5_ac3_640' then
    Result += '-c:a:0 aac -q:a:0 2 -ar:a:0 48000 -c:a:1 ac3 -b:a:1 640k -ar:a:1 48000 '
  else if AudioOut = 'fdk_aac_q5' then
    Result += '-c:a aac -q:a 2 -ar 48000 '
  else if Opts.use_aac_for_h265 <> 0 then
    Result += '-c:a aac -q:a 2 -ar 48000 '
  else
    Result += '-c:a pcm_s16le -ar 48000 ';

  if AudioOut <> 'fdk_aac_q5_ac3_640' then
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
