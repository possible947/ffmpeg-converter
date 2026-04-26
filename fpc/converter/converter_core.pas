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
procedure converter_make_output_name(input: PAnsiChar; opts: PConvertOptions; out_buf: PAnsiChar; out_sz: QWord); cdecl;
procedure converter_stop(c: PConverter); cdecl;
function converter_error_string(err: TConverterError): PAnsiChar; cdecl;

implementation

uses
  BaseUnix,
  SysUtils,
  process_utils,
  path_utils,
  fs_utils,
  tool_paths,
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

function CodecIsMux(const Codec: string): Boolean;
begin
  Result := Codec = 'mux';
end;

function CodecIsLinuxVaapi(const Codec: string): Boolean;
begin
  Result := (Codec = 'h264_vaapi') or (Codec = 'hevc_vaapi');
end;

function AudioOutputModeValid(const Mode: string): Boolean;
begin
  Result := (Mode = '') or (Mode = 'pcm') or (Mode = 'fdk_aac_q5') or
    (Mode = 'fdk_aac_q5_ac3_640') or (Mode = 'fdk_aac_q2') or (Mode = 'fdk_aac_q2_ac3_640');
end;

procedure EmitMessage(Ctx: PConverterObj; const Msg: string);
var
  S: AnsiString;
begin
  if Assigned(Ctx^.Cb.on_message) then
  begin
    S := AnsiString(Msg);
    Ctx^.Cb.on_message(PAnsiChar(S));
  end;
end;

procedure EmitStage(Ctx: PConverterObj; const StageName: string);
var
  S: AnsiString;
begin
  if Assigned(Ctx^.Cb.on_stage) then
  begin
    S := AnsiString(StageName);
    Ctx^.Cb.on_stage(PAnsiChar(S));
  end;
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
      EmitMessage(Ctx, 'output file exists - skipping');
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
var
  Ctx: PConverterObj;
  Codec: string;
  AudioOut: string;
{$IFDEF Linux}
  Cmd: string;
  Probe: TRunResult;
{$ENDIF}
begin
  if (c = nil) or (opts = nil) then
    Exit(ERR_INVALID_OPTIONS);

  Ctx := PConverterObj(c);
  Ctx^.Opts := opts^;

  Codec := ArrToStr(Ctx^.Opts.codec);
  AudioOut := ArrToStr(Ctx^.Opts.audio_output_mode);

  if not AudioOutputModeValid(AudioOut) then
    Exit(ERR_INVALID_OPTIONS);

{$IFDEF Darwin}
  if CodecIsLinuxVaapi(Codec) then
    Exit(ERR_INVALID_OPTIONS);
{$ENDIF}

{$IFNDEF Linux}
  if CodecIsLinuxVaapi(Codec) then
    Exit(ERR_INVALID_OPTIONS);
{$ENDIF}

{$IFDEF Linux}
  if CodecIsLinuxVaapi(Codec) then
  begin
    if ArrToStr(Ctx^.Opts.hw_device) = '' then
      SetAnsiField(Ctx^.Opts.hw_device, '/dev/dri/renderD128');

    Cmd := QuoteForShell(ResolveFfmpegBin) + ' -v error -hide_banner -encoders 2>/dev/null';
    Probe := RunCommandCapture(Cmd);
    if Probe.ExitCode <> 0 then
      Exit(ERR_INVALID_OPTIONS);

    if (Codec = 'h264_vaapi') and (Pos(' h264_vaapi', Probe.OutputText) = 0) then
      Exit(ERR_INVALID_OPTIONS);
    if (Codec = 'hevc_vaapi') and (Pos(' hevc_vaapi', Probe.OutputText) = 0) then
      Exit(ERR_INVALID_OPTIONS);

    Ctx^.Opts.hwaccel_enabled := 1;
  end;
{$ENDIF}

  Result := ERR_OK;
end;

function converter_process_files(c: PConverter; files: PPAnsiChar; file_count: LongInt): TConverterError; cdecl;
var
  Ctx: PConverterObj;
  I: LongInt;
  InputFile: string;
  OutputFile: string;
  EffectiveOutDir: string;
  DirError: string;
  Codec: string;
  Cmd: string;
  Err: TConverterError;
  ErrorLogPath: string;
  ErrorLogNotice: string;
  Gain: Double;
  AudioNorm: string;
  FileOpts: TConvertOptions;
  WorkOpts: TConvertOptions;
  IntermediateFile: string;

begin
  if (c = nil) or (files = nil) or (file_count <= 0) then
    Exit(ERR_INVALID_OPTIONS);

  Ctx := PConverterObj(c);
  Ctx^.StopFlag := 0;

  if not EnsureOutputDirWritable(ArrToStr(Ctx^.Opts.output_dir), EffectiveOutDir, DirError) then
  begin
    EmitError(Ctx, 'output preflight failed: ' + DirError, ERR_INVALID_OPTIONS);
    Exit(ERR_INVALID_OPTIONS);
  end;

  Codec := ArrToStr(Ctx^.Opts.codec);
  if CodecIsMux(Codec) then
  begin
    if file_count <> 1 then
    begin
      EmitError(Ctx, 'mux mode requires exactly one source file', ERR_INVALID_OPTIONS);
      Exit(ERR_INVALID_OPTIONS);
    end;

    if not FileRegular(ArrToStr(Ctx^.Opts.video_track_path)) or not FileReadable(ArrToStr(Ctx^.Opts.video_track_path)) then
    begin
      EmitError(Ctx, 'mux mode requires a readable --video-track file', ERR_INVALID_OPTIONS);
      Exit(ERR_INVALID_OPTIONS);
    end;
  end;

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

    OutputFile := MakeOutputName(InputFile, Codec, EffectiveOutDir);
    Err := CheckOutputExists(Ctx, OutputFile);
    if Err = ERR_OUTPUT_EXISTS then
    begin
      if Assigned(Ctx^.Cb.on_file_end) then
        Ctx^.Cb.on_file_end(PAnsiChar(AnsiString(InputFile)), ERR_SKIP_FILE);
      Continue;
    end;

    if Ctx^.StopFlag <> 0 then
      Exit(ERR_SKIP_FILE);

    FileOpts := Ctx^.Opts;
    SetAnsiField(FileOpts.output_dir, EffectiveOutDir);
    FileOpts.output_dir_status := 1;
    FileOpts.gain := 0.0;
    FileOpts.I_target := 0.0;
    FileOpts.TP_target := 0.0;
    FileOpts.LRA_target := 0.0;
    FileOpts.measured_I := 0.0;
    FileOpts.measured_TP := 0.0;
    FileOpts.measured_LRA := 0.0;
    FileOpts.measured_thresh := 0.0;
    FileOpts.measured_offset := 0.0;

    AudioNorm := ArrToStr(FileOpts.audio_norm);
    if AudioNorm = 'peak_norm_2pass' then
    begin
      EmitStage(Ctx, 'peak analysis');

      Err := RunPeakTwoPass(InputFile, EffectiveOutDir, ErrorLogPath, ErrorLogNotice, Gain);
      if Err <> ERR_OK then
      begin
        if ErrorLogPath <> '' then
          EmitError(Ctx, 'peak analysis failed; log: ' + ErrorLogPath, Err)
        else
          EmitError(Ctx, 'peak analysis failed', Err);

        if ErrorLogNotice <> '' then
          EmitError(Ctx, ErrorLogNotice, Err);

        if Assigned(Ctx^.Cb.on_file_end) then
          Ctx^.Cb.on_file_end(PAnsiChar(AnsiString(InputFile)), Err);
        Continue;
      end;

      FileOpts.gain := Gain;
      if Assigned(Ctx^.Cb.on_progress_analysis) then
        Ctx^.Cb.on_progress_analysis(100.0, 0.0);
    end;

    if AudioNorm = 'loudness_norm_2pass' then
    begin
      EmitStage(Ctx, 'loudnorm analysis');

      ApplyGenreTargets(FileOpts);
      Err := RunLoudnormTwoPass(InputFile, EffectiveOutDir, ErrorLogPath, ErrorLogNotice, FileOpts);
      if Err <> ERR_OK then
      begin
        if ErrorLogPath <> '' then
          EmitError(Ctx, 'loudnorm analysis failed; log: ' + ErrorLogPath, Err)
        else
          EmitError(Ctx, 'loudnorm analysis failed', Err);

        if ErrorLogNotice <> '' then
          EmitError(Ctx, ErrorLogNotice, Err);

        if Assigned(Ctx^.Cb.on_file_end) then
          Ctx^.Cb.on_file_end(PAnsiChar(AnsiString(InputFile)), Err);
        Continue;
      end;

      if Assigned(Ctx^.Cb.on_progress_analysis) then
        Ctx^.Cb.on_progress_analysis(100.0, 0.0);
    end;

    FileOpts.use_aac_for_h265 := 0;

    WorkOpts := FileOpts;
    if CodecIsMux(Codec) then
    begin
      SetAnsiField(WorkOpts.codec, 'copy');
      WorkOpts.profile := 0;
      WorkOpts.deblock := 0;
      IntermediateFile := MakeOutputName(InputFile, 'copy', EffectiveOutDir);
    end
    else
      IntermediateFile := OutputFile;

    Cmd := BuildFfmpegCommand(WorkOpts, InputFile, IntermediateFile);

    EmitStage(Ctx, 'encoding');

    Err := RunEncode(
      Cmd,
      InputFile,
      IntermediateFile,
      EffectiveOutDir,
      Ctx^.Cb.on_progress_encode,
      Ctx^.Cb.on_message,
      @Ctx^.StopFlag,
      ErrorLogPath,
      ErrorLogNotice
    );
    if Err <> ERR_OK then
    begin
      if Err <> ERR_SKIP_FILE then
      begin
        if ErrorLogPath <> '' then
          EmitError(Ctx, 'ffmpeg failed; log: ' + ErrorLogPath, Err)
        else
          EmitError(Ctx, 'ffmpeg failed', Err);

        if ErrorLogNotice <> '' then
          EmitError(Ctx, ErrorLogNotice, Err);
      end;
    end;

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

procedure converter_make_output_name(input: PAnsiChar; opts: PConvertOptions; out_buf: PAnsiChar; out_sz: QWord); cdecl;
var
  InputFile: string;
  Codec: string;
  OutDir: string;
  Name: AnsiString;
begin
  if (input = nil) or (opts = nil) or (out_buf = nil) or (out_sz = 0) then
    Exit;

  InputFile := string(input);
  Codec := ArrToStr(opts^.codec);
  OutDir := ArrToStr(opts^.output_dir);
  Name := AnsiString(MakeOutputName(InputFile, Codec, OutDir));

  StrPLCopy(out_buf, Name, out_sz - 1);
end;

function converter_error_string(err: TConverterError): PAnsiChar; cdecl;
begin
  if (Ord(err) < Ord(Low(TConverterError))) or (Ord(err) > Ord(High(TConverterError))) then
    Exit('unknown error');
  Result := ERR_STRINGS[err];
end;

end.
