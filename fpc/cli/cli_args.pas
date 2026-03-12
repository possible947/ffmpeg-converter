unit cli_args;

{$mode objfpc}{$H+}

interface

uses converter_types;

procedure PrintUsage;
function ParseArgs(var Opts: TConvertOptions; out Files: array of PAnsiChar; out FileCount: LongInt): Boolean;
procedure PrintSummary(const Opts: TConvertOptions; const Files: array of PAnsiChar; FileCount: LongInt);
function VerifyAndCompactFiles(var Files: array of PAnsiChar; var FileCount: LongInt): Boolean;

implementation

uses
  BaseUnix,
  SysUtils;

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

function ProfileToText(Profile: LongInt): string;
begin
  case Profile of
    1: Result := 'lt';
    2: Result := 'standard';
    3: Result := 'hq';
    4: Result := '4444';
  else
    Result := 'none';
  end;
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
begin
  WriteLn('Usage: ffmpeg_converter [options] file1 file2 ...');
  WriteLn;
  WriteLn('Options:');
  WriteLn('  -c, --codec <copy|prores|prores_ks|h265_mi50>');
  WriteLn('  -p, --profile <lt|standard|hq|4444>');
  WriteLn('  -d, --deblock <none|weak|strong>');
  WriteLn('  -a, --audio-norm <none|peak|peak2|loudnorm|loudnorm2>');
  WriteLn('  -g, --genre <edm|rock|hiphop|classical|podcast>');
  WriteLn('      (genre is used only with loudnorm2)');
  WriteLn('  --overwrite        overwrite output files');
  WriteLn('  -o, --output <directory> set output directory');
  WriteLn('  -h, --help         show this help');
  WriteLn;
  WriteLn('Examples:');
  WriteLn('  ffmpeg_converter input.mov');
  WriteLn('  ffmpeg_converter -c prores_ks -p hq input.mov');
  WriteLn('  ffmpeg_converter -a loudnorm2 -g rock input1.mov input2.mov');
  WriteLn;
end;

function ParseArgs(var Opts: TConvertOptions; out Files: array of PAnsiChar; out FileCount: LongInt): Boolean;
var
  I: LongInt;
  S: string;
  DirInfo: Stat;
begin
  InitDefaultOptions(Opts);
  FileCount := 0;

  for I := 0 to High(Files) do
    Files[I] := nil;

  I := 1;
  while I <= ParamCount do
  begin
    S := ParamStr(I);

    if (S = '-h') or (S = '--help') then
      Exit(False);

    if (S = '--codec') or (S = '-c') then
    begin
      if I + 1 > ParamCount then
        Exit(False);
      Inc(I);
      S := ParamStr(I);
      if (S = 'copy') or (S = 'prores') or (S = 'prores_ks') or (S = 'h265_mi50') then
        SetAnsiField(Opts.codec, S)
      else
        Exit(False);
      Inc(I);
      Continue;
    end;

    if (S = '--profile') or (S = '-p') then
    begin
      if I + 1 > ParamCount then
        Exit(False);
      Inc(I);
      S := ParamStr(I);
      if S = 'lt' then Opts.profile := 1
      else if S = 'standard' then Opts.profile := 2
      else if S = 'hq' then Opts.profile := 3
      else if S = '4444' then Opts.profile := 4
      else Exit(False);
      Inc(I);
      Continue;
    end;

    if (S = '--deblock') or (S = '-d') then
    begin
      if I + 1 > ParamCount then
        Exit(False);
      Inc(I);
      S := ParamStr(I);
      if S = 'none' then Opts.deblock := 1
      else if S = 'weak' then Opts.deblock := 2
      else if S = 'strong' then Opts.deblock := 3
      else Exit(False);
      Inc(I);
      Continue;
    end;

    if (S = '--audio-norm') or (S = '-a') then
    begin
      if I + 1 > ParamCount then
        Exit(False);
      Inc(I);
      S := ParamStr(I);
      if S = 'none' then SetAnsiField(Opts.audio_norm, 'none')
      else if S = 'peak' then SetAnsiField(Opts.audio_norm, 'peak_norm')
      else if S = 'peak2' then SetAnsiField(Opts.audio_norm, 'peak_norm_2pass')
      else if S = 'loudnorm' then SetAnsiField(Opts.audio_norm, 'loudness_norm')
      else if S = 'loudnorm2' then SetAnsiField(Opts.audio_norm, 'loudness_norm_2pass')
      else Exit(False);
      Inc(I);
      Continue;
    end;

    if (S = '--genre') or (S = '-g') then
    begin
      if I + 1 > ParamCount then
        Exit(False);
      Inc(I);
      S := ParamStr(I);
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

    if (S = '-o') or (S = '--output') then
    begin
      if I + 1 > ParamCount then
        Exit(False);
      Inc(I);
      S := ParamStr(I);
      SetAnsiField(Opts.output_dir, S);

      if (fpStat(PChar(S), DirInfo) = 0) and FPS_ISDIR(DirInfo.st_mode) and (fpAccess(PChar(S), W_OK) = 0) then
        Opts.output_dir_status := 1
      else
      begin
        Opts.output_dir_status := 0;
        WriteLn(StdErr, 'Warning: Output directory is not writable or does not exist: ', S);
      end;

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

  Result := True;
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

  if (Codec <> 'copy') and (Codec <> 'h265_mi50') then
  begin
    WriteLn('Profile:      ', ProfileToText(Opts.profile));
    WriteLn('Deblock:      ', DeblockToText(Opts.deblock));
  end
  else
  begin
    WriteLn('Profile:      (copy)');
    WriteLn('Deblock:      (copy)');
  end;

  WriteLn('Audio norm:   ', AudioNorm);

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
  Info: Stat;
  Readable: Boolean;
begin
  ValidCount := 0;
  WriteLn;
  WriteLn('Verifying files...');

  for I := 0 to FileCount - 1 do
  begin
    if fpStat(Files[I], Info) <> 0 then
    begin
      WriteLn('  X File not found: ', string(Files[I]));
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
      WriteLn('  X Not a regular file: ', string(Files[I]));
      if Files[I] <> nil then
      begin
        StrDispose(Files[I]);
        Files[I] := nil;
      end;
      Continue;
    end;

    Readable := fpAccess(Files[I], R_OK) = 0;
    if not Readable then
    begin
      WriteLn('  X File not readable: ', string(Files[I]));
      if Files[I] <> nil then
      begin
        StrDispose(Files[I]);
        Files[I] := nil;
      end;
      Continue;
    end;

    WriteLn('  + OK: ', string(Files[I]));
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
