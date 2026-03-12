unit converter_core;

{$mode objfpc}{$H+}

interface

uses converter_types;

type
  PConverter = Pointer;

function converter_create: PConverter; cdecl;
procedure converter_destroy(c: PConverter); cdecl;
procedure converter_set_callbacks(c: PConverter; cb: PConverterCallbacks); cdecl;
function converter_set_options(c: PConverter; opts: PConvertOptions): TConverterError; cdecl;
function converter_process_files(c: PConverter; files: PPAnsiChar; file_count: LongInt): TConverterError; cdecl;
procedure converter_stop(c: PConverter); cdecl;
function converter_error_string(err: TConverterError): PAnsiChar; cdecl;

implementation

uses
  BaseUnix,
  SysUtils,
  path_utils,
  converter_cmd_builder,
  converter_analysis,
  converter_runner;

type
  TConverterObj = record
    Opts: TConvertOptions;
    Cb: TConverterCallbacks;
    StopFlag: LongInt;
  end;
  PConverterObj = ^TConverterObj;

  TPAnsiCharArray = array[0..(High(SizeInt) div SizeOf(PAnsiChar)) - 1] of PAnsiChar;
  PPAnsiCharArray = ^TPAnsiCharArray;

const
  ERR_STRINGS: array[TConverterError] of PAnsiChar = (
    'OK',
    'input file not found',
    'input file is not a regular file',
    'input file not readable',
    'output file exists',
    'file skipped',
    'peak analysis failed',
    'loudnorm analysis failed',
    'ffmpeg failed',
    'ffprobe failed',
    'popen failed',
    'pclose failed',
    'invalid options',
    'unknown error'
  );

function ArrToStr(const A: array of AnsiChar): string;
begin
  Result := StrPas(@A[0]);
end;

procedure EmitError(Ctx: PConverterObj; const Msg: string; Code: TConverterError);
begin
  if Assigned(Ctx^.Cb.on_error) then
    Ctx^.Cb.on_error(PAnsiChar(AnsiString(Msg)), Code);
end;

function CheckInputFile(Ctx: PConverterObj; const InputFile: string): TConverterError;
var
  Info: Stat;
begin
  if fpStat(PChar(InputFile), Info) <> 0 then
  begin
    EmitError(Ctx, 'input file not found', ERR_INPUT_NOT_FOUND);
    Exit(ERR_INPUT_NOT_FOUND);
  end;

  if not FPS_ISREG(Info.st_mode) then
  begin
    EmitError(Ctx, 'input file is not a regular file', ERR_INPUT_NOT_REGULAR);
    Exit(ERR_INPUT_NOT_REGULAR);
  end;

  if fpAccess(PChar(InputFile), R_OK) <> 0 then
  begin
    EmitError(Ctx, 'input file not readable', ERR_INPUT_NOT_READABLE);
    Exit(ERR_INPUT_NOT_READABLE);
  end;

  Result := ERR_OK;
end;

function CheckOutputExists(Ctx: PConverterObj; const OutputFile: string): TConverterError;
var
  Info: Stat;
begin
  if fpStat(PChar(OutputFile), Info) = 0 then
  begin
    if Ctx^.Opts.overwrite = 0 then
    begin
      if Assigned(Ctx^.Cb.on_message) then
        Ctx^.Cb.on_message('output file exists - skipping');
      Exit(ERR_OUTPUT_EXISTS);
    end;
  end;

  Result := ERR_OK;
end;

procedure ApplyGenreTargets(var Opts: TConvertOptions);
begin
  Opts.I_target := -11;
  Opts.TP_target := -1.5;
  Opts.LRA_target := 7;

  case Opts.genre of
    1:
      begin
        Opts.I_target := -11;
        Opts.TP_target := -1.5;
        Opts.LRA_target := 6;
      end;
    2:
      begin
        Opts.I_target := -11;
        Opts.TP_target := -1.0;
        Opts.LRA_target := 7;
      end;
    3:
      begin
        Opts.I_target := -12;
        Opts.TP_target := -1.0;
        Opts.LRA_target := 6;
      end;
    4:
      begin
        Opts.I_target := -16;
        Opts.TP_target := -2.0;
        Opts.LRA_target := 12;
      end;
    5:
      begin
        Opts.I_target := -16;
        Opts.TP_target := -1.5;
        Opts.LRA_target := 7;
      end;
  end;
end;

function converter_create: PConverter; cdecl;
var
  Ctx: PConverterObj;
begin
  New(Ctx);
  FillChar(Ctx^, SizeOf(TConverterObj), 0);
  InitDefaultOptions(Ctx^.Opts);
  Result := Ctx;
end;

procedure converter_destroy(c: PConverter); cdecl;
begin
  if c = nil then
    Exit;
  Dispose(PConverterObj(c));
end;

procedure converter_set_callbacks(c: PConverter; cb: PConverterCallbacks); cdecl;
begin
  if c = nil then
    Exit;

  if cb = nil then
    FillChar(PConverterObj(c)^.Cb, SizeOf(TConverterCallbacks), 0)
  else
    PConverterObj(c)^.Cb := cb^;
end;

function converter_set_options(c: PConverter; opts: PConvertOptions): TConverterError; cdecl;
begin
  if (c = nil) or (opts = nil) then
    Exit(ERR_INVALID_OPTIONS);

  PConverterObj(c)^.Opts := opts^;
  Result := ERR_OK;
end;

function converter_process_files(c: PConverter; files: PPAnsiChar; file_count: LongInt): TConverterError; cdecl;
var
  Ctx: PConverterObj;
  I: LongInt;
  InputFile: string;
  OutputFile: string;
  Cmd: string;
  Err: TConverterError;
  Gain: Double;
  AudioNorm: string;
begin
  if (c = nil) or (files = nil) or (file_count <= 0) then
    Exit(ERR_INVALID_OPTIONS);

  Ctx := PConverterObj(c);
  Ctx^.StopFlag := 0;

  for I := 0 to file_count - 1 do
  begin
    InputFile := string(PPAnsiCharArray(files)^[I]);

    if Assigned(Ctx^.Cb.on_file_begin) then
      Ctx^.Cb.on_file_begin(PAnsiChar(AnsiString(InputFile)), I + 1, file_count);

    if Ctx^.StopFlag <> 0 then
      Exit(ERR_SKIP_FILE);

    Err := CheckInputFile(Ctx, InputFile);
    if Err <> ERR_OK then
    begin
      if Assigned(Ctx^.Cb.on_file_end) then
        Ctx^.Cb.on_file_end(PAnsiChar(AnsiString(InputFile)), Err);
      Continue;
    end;

    OutputFile := MakeOutputName(InputFile, ArrToStr(Ctx^.Opts.codec), ArrToStr(Ctx^.Opts.output_dir));
    Err := CheckOutputExists(Ctx, OutputFile);
    if Err = ERR_OUTPUT_EXISTS then
    begin
      if Assigned(Ctx^.Cb.on_file_end) then
        Ctx^.Cb.on_file_end(PAnsiChar(AnsiString(InputFile)), ERR_SKIP_FILE);
      Continue;
    end;

    if Ctx^.StopFlag <> 0 then
      Exit(ERR_SKIP_FILE);

    AudioNorm := ArrToStr(Ctx^.Opts.audio_norm);
    if AudioNorm = 'peak_norm_2pass' then
    begin
      if Assigned(Ctx^.Cb.on_stage) then
        Ctx^.Cb.on_stage('peak analysis');

      Err := RunPeakTwoPass(InputFile, Gain);
      if Err <> ERR_OK then
      begin
        EmitError(Ctx, 'peak analysis failed', Err);
        if Assigned(Ctx^.Cb.on_file_end) then
          Ctx^.Cb.on_file_end(PAnsiChar(AnsiString(InputFile)), Err);
        Continue;
      end;

      Ctx^.Opts.gain := Gain;
      if Assigned(Ctx^.Cb.on_progress_analysis) then
        Ctx^.Cb.on_progress_analysis(100.0, 0.0);
    end;

    if AudioNorm = 'loudness_norm_2pass' then
    begin
      if Assigned(Ctx^.Cb.on_stage) then
        Ctx^.Cb.on_stage('loudnorm analysis');

      ApplyGenreTargets(Ctx^.Opts);
      Err := RunLoudnormTwoPass(InputFile, Ctx^.Opts);
      if Err <> ERR_OK then
      begin
        EmitError(Ctx, 'loudnorm analysis failed', Err);
        if Assigned(Ctx^.Cb.on_file_end) then
          Ctx^.Cb.on_file_end(PAnsiChar(AnsiString(InputFile)), Err);
        Continue;
      end;

      if Assigned(Ctx^.Cb.on_progress_analysis) then
        Ctx^.Cb.on_progress_analysis(100.0, 0.0);
    end;

    if ArrToStr(Ctx^.Opts.codec) = 'h265_mi50' then
      Ctx^.Opts.use_aac_for_h265 := 1
    else
      Ctx^.Opts.use_aac_for_h265 := 0;

    Cmd := BuildFfmpegCommand(Ctx^.Opts, InputFile, OutputFile);
    if Assigned(Ctx^.Cb.on_message) then
      Ctx^.Cb.on_message('ffmpeg command built');

    if Assigned(Ctx^.Cb.on_stage) then
      Ctx^.Cb.on_stage('encoding');

    Err := RunEncode(Cmd);
    if Err <> ERR_OK then
      EmitError(Ctx, 'ffmpeg failed', Err)
    else if Assigned(Ctx^.Cb.on_message) then
      Ctx^.Cb.on_message('encoding finished');

    if Assigned(Ctx^.Cb.on_progress_encode) then
      Ctx^.Cb.on_progress_encode(100.0, 0.0, 0.0);

    if Assigned(Ctx^.Cb.on_file_end) then
      Ctx^.Cb.on_file_end(PAnsiChar(AnsiString(InputFile)), Err);
  end;

  if Assigned(Ctx^.Cb.on_complete) then
    Ctx^.Cb.on_complete;

  Result := ERR_OK;
end;

procedure converter_stop(c: PConverter); cdecl;
begin
  if c = nil then
    Exit;
  PConverterObj(c)^.StopFlag := 1;
end;

function converter_error_string(err: TConverterError): PAnsiChar; cdecl;
begin
  if (Ord(err) < Ord(Low(TConverterError))) or (Ord(err) > Ord(High(TConverterError))) then
    Exit('unknown error');
  Result := ERR_STRINGS[err];
end;

end.
