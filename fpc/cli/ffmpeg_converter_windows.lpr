program ffmpeg_converter_windows;

{$mode objfpc}{$H+}

uses
  SysUtils,
  Windows,
  converter_types,
  converter_api_c,
  cli_args,
  cli_menu,
  cli_callbacks,
  apple_m4v_creator,
  mux_postprocess,
  m4v_postprocess,
  fs_utils,
  windows_utf8,
  windows_file_utils,
  windows_probe,
  windows_mkvmerge;

const
  FFMPEG_CONVERTER_VERSION = '2.6.0';

function ArrToStr(const A: array of AnsiChar): string;
begin
  Result := StrPas(@A[0]);
end;

procedure ParseM4VEncodedOptions(const Encoded: string; out M4VOpts: TAppleM4VOptions);
var
  V: Integer;
  Token: string;
  Rest: string;

  function NextToken(var S: string): string;
  var
    K: SizeInt;
  begin
    K := Pos('|', S);
    if K <= 0 then
    begin
      Result := S;
      S := '';
      Exit;
    end;
    Result := Copy(S, 1, K - 1);
    Delete(S, 1, K);
  end;

begin
  M4VOpts := DefaultAppleM4VOptions;
  if Trim(Encoded) = '' then
    Exit;

  Rest := Encoded;

  Token := NextToken(Rest);
  if TryStrToInt(Token, V) and (V >= 0) then
    M4VOpts.VideoTrackIndex := V;

  Token := NextToken(Rest);
  if TryStrToInt(Token, V) and (V >= 0) then
    M4VOpts.AudioTrackIndex := V;

  Token := NextToken(Rest);
  if TryStrToInt(Token, V) and (V > 0) then
    M4VOpts.Ac3BitrateKbps := V;

  Token := NextToken(Rest);
  if Trim(Token) <> '' then
    M4VOpts.AudioLang := Trim(Token);

  Token := NextToken(Rest);
  if TryStrToInt(Token, V) then
    M4VOpts.AddChapters := V <> 0;
end;

function BuildM4VOutputName(const InputFile, OutputDir: string): string;
var
  BaseName: string;
begin
  BaseName := ChangeFileExt(ExtractFileName(InputFile), '');
  if Trim(OutputDir) = '' then
    Result := BaseName + '.m4v'
  else
    Result := IncludeTrailingPathDelimiter(OutputDir) + BaseName + '.m4v';
end;

var
  Opts: TConvertOptions;
  Files: array of PAnsiChar;
  FileCount: LongInt;
  Ctx: Pointer;
  Cb: TConverterCallbacks;
  Err: TConverterError;
  I: Integer;
  UTF8Args: TStringArray;
  M4VOpts: TAppleM4VOptions;
  M4VCodec: string;
  M4VOut: string;
  M4VError: string;
  M4VFileErr: Boolean;

begin
{$IFDEF Windows}
  { Set console code page to UTF-8 for proper filename handling }
  SetConsoleCP(CP_UTF8);
  SetConsoleOutputCP(CP_UTF8);
{$ENDIF}

  { Retrieve arguments via WideChar API so non-ASCII filenames are handled }
  UTF8Args := GetUTF8Arguments;

  InitDefaultOptions(Opts);
  SetLength(Files, 4096);

  if (Length(UTF8Args) = 2) and ((UTF8Args[1] = '-h') or (UTF8Args[1] = '--help')) then
  begin
    PrintUsage;
    Halt(0);
  end;

  if (Length(UTF8Args) = 2) and (UTF8Args[1] = '--version') then
  begin
    WriteLn('ffmpeg_converter version ', FFMPEG_CONVERTER_VERSION);
    Halt(0);
  end;

  if (Length(UTF8Args) = 2) and (UTF8Args[1] = '--codecs-list') then
  begin
    PrintCodecsList;
    Halt(0);
  end;

  if Length(UTF8Args) <= 1 then
  begin
    if not RunMenu(Opts, Files, FileCount) then
      Halt(1);
  end
  else
  begin
    if not ParseArgsFromArray(Opts, Files, FileCount, UTF8Args) then
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

  M4VCodec := ArrToStr(Opts.codec);
  if M4VCodec = 'm4v' then
  begin
    { For Windows, we'll use the new postprocessing approach }
    { This will be handled after the main conversion loop }
  end;

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
  end
  else if (Err = ERR_OK) and (ArrToStr(Opts.codec) = 'm4v') and (FileCount > 0) then
  begin
    WriteLn;
    WriteLn('Starting m4v postprocess...');
    Err := RunM4VPostprocess(Opts, string(Files[0]));
  end;
{$ENDIF}

  converter_destroy(Ctx);

  for I := 0 to FileCount - 1 do
    if Files[I] <> nil then
      StrDispose(Files[I]);

  if Err <> ERR_OK then
    Halt(1);
end.
