unit converter_cmd_builder;

{$mode objfpc}{$H+}

interface

uses converter_types;

function BuildFfmpegCommand(const Opts: TConvertOptions; const InputFile, OutputFile: string): string;

implementation

uses
  SysUtils,
  path_utils;

function ArrToStr(const A: array of AnsiChar): string;
begin
  Result := StrPas(@A[0]);
end;

function BuildFfmpegCommand(const Opts: TConvertOptions; const InputFile, OutputFile: string): string;
var
  Codec: string;
  AudioNorm: string;
  DevicePath: string;
begin
  Codec := ArrToStr(Opts.codec);
  AudioNorm := ArrToStr(Opts.audio_norm);

  if Codec = 'h265_mi50' then
  begin
    DevicePath := GetEnvironmentVariable('VAAPI_DEVICE');
    if DevicePath = '' then
      DevicePath := '/dev/dri/renderD128';
    if Opts.overwrite <> 0 then
      Result := 'ffmpeg -y -vaapi_device ' + QuoteForShell(DevicePath) + ' -i ' + QuoteForShell(InputFile) + ' '
    else
      Result := 'ffmpeg -n -vaapi_device ' + QuoteForShell(DevicePath) + ' -i ' + QuoteForShell(InputFile) + ' ';
  end
  else
  begin
    if Opts.overwrite <> 0 then
      Result := 'ffmpeg -y -i ' + QuoteForShell(InputFile) + ' '
    else
      Result := 'ffmpeg -n -i ' + QuoteForShell(InputFile) + ' ';
  end;

  Result += '-map 0:v:0 -map 0:a:0 -map_metadata 0 ';

  if (Codec = 'prores') or (Codec = 'prores_ks') then
    Result += Format('-c:v %s -profile:v %d ', [Codec, Opts.profile])
  else if Codec = 'h265_mi50' then
    Result += '-c:v hevc_vaapi -rc_mode:v auto -qp 25 -profile:v main -level:v 5.1 '
  else
    Result += '-c:v copy ';

  if Codec = 'h265_mi50' then
    Result += '-vf "format=nv12,hwupload" '
  else if Opts.deblock = 2 then
    Result += '-vf "deblock=filter=weak:block=4:planes=1" '
  else if Opts.deblock = 3 then
    Result += '-vf "deblock=filter=strong:block=4:alpha=0.12:beta=0.07:gamma=0.06:delta=0.05:planes=1" ';

  if Opts.use_aac_for_h265 <> 0 then
    Result += '-c:a aac -q:a 2 -ar 48000 '
  else
    Result += '-c:a pcm_s16le -ar 48000 ';

  if AudioNorm = 'none' then
    Result += '-af "aresample=resampler=soxr:precision=28:cheby=1" '
  else if AudioNorm = 'peak_norm' then
    Result += '-af "aresample=resampler=soxr:precision=28:cheby=1,volume=-3dB" '
  else if AudioNorm = 'peak_norm_2pass' then
    Result += Format('-af "aresample=resampler=soxr:precision=28:cheby=1,volume=%.2fdB" ', [Opts.gain])
  else if AudioNorm = 'loudness_norm' then
    Result += '-af "aresample=resampler=soxr:precision=28:cheby=1,loudnorm=I=-11:TP=-1.5:LRA=7" '
  else if AudioNorm = 'loudness_norm_2pass' then
    Result += Format('-af "aresample=resampler=soxr:precision=28:cheby=1,loudnorm=I=%.1f:TP=%.1f:LRA=%.1f:measured_I=%.2f:measured_TP=%.2f:measured_LRA=%.2f:measured_thresh=%.2f:offset=%.2f:linear=true" ',
      [Opts.I_target, Opts.TP_target, Opts.LRA_target, Opts.measured_I, Opts.measured_TP, Opts.measured_LRA, Opts.measured_thresh, Opts.measured_offset]);

  Result += QuoteForShell(OutputFile);
end;

end.
