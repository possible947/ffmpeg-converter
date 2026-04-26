program ffmpeg_converter_windows;

{$mode objfpc}{$H+}

uses
  SysUtils,
  converter_types,
  converter_api_c,
  cli_args,
  cli_menu,
  cli_callbacks,
  mux_postprocess,
  windows_utf8,
  windows_file_utils,
  windows_probe,
  windows_mkvmerge;

function ArrToStr(const A: array of AnsiChar): string;
begin
  Result := StrPas(@A[0]);
end;

var
  Opts: TConvertOptions;
  Files: array of PAnsiChar;
  FileCount: LongInt;
  Ctx: Pointer;
  Cb: TConverterCallbacks;
  Err: TConverterError;
  I: Integer;

begin
{$IFDEF Windows}
  { Set console code page to UTF-8 for proper filename handling }
  SetConsoleCP(CP_UTF8);
  SetConsoleOutputCP(CP_UTF8);
{$ENDIF}

  InitDefaultOptions(Opts);
  SetLength(Files, 4096);

  if (ParamCount = 1) and ((ParamStr(1) = '-h') or (ParamStr(1) = '--help')) then
  begin
    PrintUsage;
    Halt(0);
  end;

  if ParamCount = 0 then
  begin
    if not RunMenu(Opts, Files, FileCount) then
      Halt(1);
  end
  else
  begin
    if not ParseArgs(Opts, Files, FileCount) then
    begin
      PrintUsage;
      Halt(1);
    end;
  end;

  if FileCount <= 0 then
  begin
    PrintUsage;
    Halt(1);
  end;

  PrintSummary(Opts, Files, FileCount);

  if not VerifyAndCompactFiles(Files, FileCount) then
    Halt(1);

  if FileCount <= 0 then
    Halt(1);

  SetupCliCallbacks(Cb);

  Ctx := converter_create;
  if Ctx = nil then
  begin
    WriteLn('Failed to create converter.');
    Halt(1);
  end;

  converter_set_callbacks(Ctx, @Cb);
  Err := converter_set_options(Ctx, @Opts);
  if Err = ERR_OK then
    Err := converter_process_files(Ctx, @Files[0], FileCount);

  { Handle mux postprocess on Windows after main conversion }
{$IFDEF Windows}
  if (Err = ERR_OK) and (ArrToStr(Opts.codec) = 'mux') and (FileCount > 0) then
  begin
    WriteLn;
    WriteLn('Starting mux postprocess...');
    Err := RunMuxPostprocess(Opts, string(Files[0]));
  end;
{$ENDIF}

  converter_destroy(Ctx);

  for I := 0 to FileCount - 1 do
    if Files[I] <> nil then
      StrDispose(Files[I]);

  if Err <> ERR_OK then
    Halt(1);
end.
