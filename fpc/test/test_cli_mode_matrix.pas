program test_cli_mode_matrix;

{$mode objfpc}{$H+}

uses
  SysUtils,
  converter_types,
  converter_cmd_builder,
  path_utils;

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

procedure AssertContains(const LabelName, TextValue, Needle: string);
begin
  if Pos(Needle, TextValue) = 0 then
  begin
    WriteLn('FAIL [', LabelName, ']: missing substring: ', Needle);
    WriteLn('CMD: ', TextValue);
    Halt(1);
  end;
end;

procedure AssertEqual(const LabelName, Actual, Expected: string);
begin
  if Actual <> Expected then
  begin
    WriteLn('FAIL [', LabelName, ']: expected="', Expected, '" actual="', Actual, '"');
    Halt(1);
  end;
end;

var
  Opts: TConvertOptions;
  Cmd: string;
begin
  InitDefaultOptions(Opts);

  Cmd := BuildFfmpegCommand(Opts, 'in.mov', 'out.mov');
  AssertContains('default codec', Cmd, '-c:v prores_ks -profile:v 2');
  AssertContains('default audio norm', Cmd, 'volume=0.00dB');

  SetAnsiField(Opts.codec, 'copy');
  Cmd := BuildFfmpegCommand(Opts, 'in.mov', 'out.mov');
  AssertContains('copy codec', Cmd, '-c:v copy');
  AssertContains('copy audio pcm', Cmd, '-c:a pcm_s16le -ar 48000');

  SetAnsiField(Opts.codec, 'prores');
  Opts.profile := 4;
  Opts.deblock := 2;
  Cmd := BuildFfmpegCommand(Opts, 'in.mov', 'out.mov');
  AssertContains('prores profile', Cmd, '-c:v prores -profile:v 4');
  AssertContains('deblock weak', Cmd, 'deblock=filter=weak:block=4:planes=1');

  Opts.deblock := 3;
  Cmd := BuildFfmpegCommand(Opts, 'in.mov', 'out.mov');
  AssertContains('deblock strong', Cmd, 'deblock=filter=strong:block=4');

  SetAnsiField(Opts.codec, 'h264_vaapi');
  Cmd := BuildFfmpegCommand(Opts, 'in.mov', 'out.mov');
  AssertContains('h264 vaapi', Cmd, '-c:v h264_vaapi -rc_mode auto');

  SetAnsiField(Opts.codec, 'hevc_vaapi');
  Cmd := BuildFfmpegCommand(Opts, 'in.mov', 'out.mov');
  AssertContains('hevc vaapi', Cmd, '-c:v hevc_vaapi -rc_mode auto');

  SetAnsiField(Opts.codec, 'prores_videotoolbox');
  Cmd := BuildFfmpegCommand(Opts, 'in.mov', 'out.mov');
  AssertContains('prores videotoolbox', Cmd, '-c:v prores_videotoolbox');

  SetAnsiField(Opts.codec, 'hevc_videotoolbox');
  Opts.hevc_vt_bitrate_kbps := 42000;
  Cmd := BuildFfmpegCommand(Opts, 'in.mov', 'out.mov');
  AssertContains('hevc videotoolbox', Cmd, '-c:v hevc_videotoolbox -b:v 42000k');

  SetAnsiField(Opts.codec, 'copy');
  SetAnsiField(Opts.audio_norm, 'none');
  SetAnsiField(Opts.audio_output_mode, 'pcm');
  Cmd := BuildFfmpegCommand(Opts, 'in.mov', 'out.mov');
  AssertContains('audio none', Cmd, '-af "aresample=resampler=soxr:precision=28:cheby=1"');

  SetAnsiField(Opts.audio_norm, 'peak_norm');
  Cmd := BuildFfmpegCommand(Opts, 'in.mov', 'out.mov');
  AssertContains('audio peak', Cmd, 'volume=-3dB');

  SetAnsiField(Opts.audio_norm, 'peak_norm_2pass');
  Opts.gain := 1.75;
  Cmd := BuildFfmpegCommand(Opts, 'in.mov', 'out.mov');
  AssertContains('audio peak2', Cmd, 'volume=1.75dB');

  SetAnsiField(Opts.audio_norm, 'loudness_norm');
  Cmd := BuildFfmpegCommand(Opts, 'in.mov', 'out.mov');
  AssertContains('audio loudnorm', Cmd, 'loudnorm=I=-11:TP=-1.5:LRA=7');

  SetAnsiField(Opts.audio_norm, 'loudness_norm_2pass');
  Opts.I_target := -16.0;
  Opts.TP_target := -2.0;
  Opts.LRA_target := 12.0;
  Opts.measured_I := -15.12;
  Opts.measured_TP := -1.11;
  Opts.measured_LRA := 5.43;
  Opts.measured_thresh := -26.0;
  Opts.measured_offset := 1.25;
  Cmd := BuildFfmpegCommand(Opts, 'in.mov', 'out.mov');
  AssertContains('audio loudnorm2 targets', Cmd, 'loudnorm=I=-16.0:TP=-2.0:LRA=12.0');
  AssertContains('audio loudnorm2 measured', Cmd, 'measured_I=-15.12:measured_TP=-1.11:measured_LRA=5.43');

  SetAnsiField(Opts.audio_output_mode, 'fdk_aac_q5');
  SetAnsiField(Opts.audio_norm, 'none');
  Cmd := BuildFfmpegCommand(Opts, 'in.mov', 'out.mov');
  AssertContains('audio out q5', Cmd, '-c:a aac -q:a 2 -ar 48000');

  SetAnsiField(Opts.audio_output_mode, 'fdk_aac_q5_ac3_640');
  Cmd := BuildFfmpegCommand(Opts, 'in.mov', 'out.mov');
  AssertContains('audio out dual map', Cmd, '-filter_complex "[0:a:0]');
  AssertContains('audio out dual codecs', Cmd, '-c:a:0 aac -q:a:0 2 -ar:a:0 48000 -c:a:1 ac3 -b:a:1 640k -ar:a:1 48000');

  AssertEqual('output ext copy', MakeOutputName('/tmp/in.mov', 'copy', '/tmp/out'), '/tmp/out/in_converted.mkv');
  AssertEqual('output ext mux', MakeOutputName('/tmp/in.mov', 'mux', '/tmp/out'), '/tmp/out/in_converted.mkv');
  AssertEqual('output ext hevc vt', MakeOutputName('/tmp/in.mov', 'hevc_videotoolbox', '/tmp/out'), '/tmp/out/in_converted.mp4');
  AssertEqual('output ext prores', MakeOutputName('/tmp/in.mov', 'prores', '/tmp/out'), '/tmp/out/in_converted.mov');

  WriteLn('OK: CLI mode matrix');
end.
