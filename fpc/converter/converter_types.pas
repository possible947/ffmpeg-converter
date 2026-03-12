unit converter_types;

{$mode objfpc}{$H+}

interface

type
  TConverterError = (
    ERR_OK,
    ERR_INPUT_NOT_FOUND,
    ERR_INPUT_NOT_REGULAR,
    ERR_INPUT_NOT_READABLE,
    ERR_OUTPUT_EXISTS,
    ERR_SKIP_FILE,
    ERR_PEAK_ANALYSIS_FAILED,
    ERR_LOUDNORM_ANALYSIS_FAILED,
    ERR_FFMPEG_FAILED,
    ERR_FFPROBE_FAILED,
    ERR_POPEN_FAILED,
    ERR_PCLOSE_FAILED,
    ERR_INVALID_OPTIONS,
    ERR_UNKNOWN
  );

  TConvertOptions = packed record
    codec: array[0..31] of AnsiChar;
    profile: LongInt;
    deblock: LongInt;
    audio_norm: array[0..31] of AnsiChar;
    genre: LongInt;

    gain: Double;
    I_target: Double;
    TP_target: Double;
    LRA_target: Double;
    measured_I: Double;
    measured_TP: Double;
    measured_LRA: Double;
    measured_thresh: Double;
    measured_offset: Double;

    overwrite: LongInt;
    output_dir: array[0..1023] of AnsiChar;
    output_dir_status: LongInt;
    use_aac_for_h265: LongInt;
  end;
  PConvertOptions = ^TConvertOptions;

  TOnFileBegin = procedure(filename: PAnsiChar; index, total: LongInt); cdecl;
  TOnFileEnd = procedure(filename: PAnsiChar; status: TConverterError); cdecl;
  TOnStage = procedure(stage_name: PAnsiChar); cdecl;
  TOnProgressEncode = procedure(percent, fps, eta_seconds: Single); cdecl;
  TOnProgressAnalysis = procedure(percent, eta_seconds: Single); cdecl;
  TOnMessage = procedure(text: PAnsiChar); cdecl;
  TOnError = procedure(text: PAnsiChar; code: TConverterError); cdecl;
  TOnComplete = procedure; cdecl;

  TConverterCallbacks = packed record
    on_file_begin: TOnFileBegin;
    on_file_end: TOnFileEnd;
    on_stage: TOnStage;
    on_progress_encode: TOnProgressEncode;
    on_progress_analysis: TOnProgressAnalysis;
    on_message: TOnMessage;
    on_error: TOnError;
    on_complete: TOnComplete;
  end;
  PConverterCallbacks = ^TConverterCallbacks;

const
  DEFAULT_CODEC = 'prores_ks';
  DEFAULT_AUDIO_NORM = 'peak_norm_2pass';

procedure InitDefaultOptions(out Opts: TConvertOptions);

implementation

uses SysUtils;

procedure InitDefaultOptions(out Opts: TConvertOptions);
begin
  FillChar(Opts, SizeOf(Opts), 0);

  StrPLCopy(@Opts.codec[0], DEFAULT_CODEC, Length(DEFAULT_CODEC));
  Opts.profile := 2;
  Opts.deblock := 1;

  StrPLCopy(@Opts.audio_norm[0], DEFAULT_AUDIO_NORM, Length(DEFAULT_AUDIO_NORM));
  Opts.genre := 1;

  Opts.overwrite := 0;
  Opts.output_dir[0] := #0;
  Opts.output_dir_status := 0;
  Opts.use_aac_for_h265 := 0;
end;

end.