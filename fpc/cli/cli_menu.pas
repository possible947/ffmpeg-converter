unit cli_menu;

{$mode objfpc}{$H+}

interface

uses converter_types;

function RunMenu(var Opts: TConvertOptions; out Files: array of PAnsiChar; out FileCount: LongInt): Boolean;

implementation

uses
  BaseUnix,
  fs_utils,
  SysUtils;

function IsDarwin: Boolean;
begin
{$IFDEF Darwin}
  Result := True;
{$ELSE}
  Result := False;
{$ENDIF}
end;

function IsLinux: Boolean;
begin
{$IFDEF Linux}
  Result := True;
{$ELSE}
  Result := False;
{$ENDIF}
end;

procedure ClearScreen;
begin
  Write(#27'[H'#27'[J');
end;

procedure ClearAllocated(var AFiles: array of PAnsiChar; Count: LongInt);
var
  I: LongInt;
begin
  for I := 0 to Count - 1 do
  begin
    if AFiles[I] <> nil then
    begin
      StrDispose(AFiles[I]);
      AFiles[I] := nil;
    end;
  end;
end;

function ReadChoice: Char;
var
  Line: string;
  I: SizeInt;
begin
  if EOF(Input) then
    Exit(#0);

  ReadLn(Line);
  if Line = '' then
    Exit(#10);

  for I := 1 to Length(Line) do
  begin
    if not (Line[I] in [' ', #9, #10, #13]) then
      Exit(Line[I]);
  end;

  Result := #10;
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

function ReadOutputDir(out OutDir: string; out Status: LongInt): Boolean;
var
  Tmp: string;
  ResolvedDir: string;
  DirError: string;
begin
  Status := 0;
  OutDir := '';

  WriteLn('output directory (default: $HOME/ffmpeg_converter):');
  Write('> ');
  if EOF(Input) then
    Exit(False);

  ReadLn(Tmp);
  Tmp := Trim(Tmp);

  if not EnsureOutputDirWritable(Tmp, ResolvedDir, DirError) then
  begin
    WriteLn('Error: ', DirError);
    Exit(False);
  end;

  Status := 1;
  OutDir := ResolvedDir;
  Result := True;
end;

function ProcessInputPath(const InputPath: string; out OutputPath: string): Boolean;
var
  I: SizeInt;
  C: Char;
  InQuotes: Boolean;
  QuoteChar: Char;
  EscapeNext: Boolean;
  Tmp: string;
begin
  OutputPath := '';
  if InputPath = '' then
    Exit(False);

  Tmp := '';
  InQuotes := False;
  QuoteChar := #0;
  EscapeNext := False;

  for I := 1 to Length(InputPath) do
  begin
    C := InputPath[I];

    if EscapeNext then
    begin
      Tmp += C;
      EscapeNext := False;
      Continue;
    end;

    if C = '\' then
    begin
      if I < Length(InputPath) then
      begin
        if InputPath[I + 1] in [' ', '\', '''', '"'] then
        begin
          EscapeNext := True;
          Continue;
        end;
      end;
      Tmp += C;
      Continue;
    end;

    if (not InQuotes) and (C in ['''', '"']) then
    begin
      InQuotes := True;
      QuoteChar := C;
      Continue;
    end;

    if InQuotes and (C = QuoteChar) then
    begin
      InQuotes := False;
      Continue;
    end;

    Tmp += C;
  end;

  Tmp := Trim(Tmp);
  if Tmp = '' then
    Exit(False);

  OutputPath := Tmp;
  Result := True;
end;

function ReadInputList(var OutFiles: array of PAnsiChar; out Count: LongInt): Boolean;
var
  Line: string;
  ProcessedPath: string;
  Info: Stat;
  Idx: LongInt;
begin
  Count := 0;
  Result := False;

  WriteLn('Enter file names (you can drag & drop files). Finish with empty line:');

  Idx := 0;
  while Idx < Length(OutFiles) do
  begin
    Write('File ', Idx + 1, ': ');
    if EOF(Input) then
      Break;

    ReadLn(Line);
    if Line = '' then
      Break;

    if (Line = 'c') or (Line = 'C') then
    begin
      ClearAllocated(OutFiles, Idx);
      Exit(False);
    end;

    if not ProcessInputPath(Line, ProcessedPath) then
    begin
      WriteLn('Error processing path');
      Continue;
    end;

    if fpStat(PChar(ProcessedPath), Info) <> 0 then
    begin
      WriteLn('File not found: ''', ProcessedPath, ''' (error: ', SysErrorMessage(fpgeterrno), ')');
      Continue;
    end;

    if not FPS_ISREG(Info.st_mode) then
    begin
      WriteLn('Not a regular file: ', ProcessedPath);
      Continue;
    end;

    OutFiles[Idx] := StrNew(PAnsiChar(AnsiString(ProcessedPath)));
    if OutFiles[Idx] = nil then
    begin
      ClearAllocated(OutFiles, Idx);
      Exit(False);
    end;

    Inc(Idx);
    WriteLn('+ Added: ', ProcessedPath);
  end;

  Count := Idx;
  if Count > 0 then
    WriteLn(#10'Successfully added ', Count, ' file(s)');

  Result := True;
end;

function RunMenu(var Opts: TConvertOptions; out Files: array of PAnsiChar; out FileCount: LongInt): Boolean;
var
  Step: LongInt;
  Codec: LongInt;
  Profile: LongInt;
  Deblock: LongInt;
  AudioNorm: LongInt;
  AudioOutput: LongInt;
  Genre: LongInt;
  Overwrite: LongInt;
  VideoTrackPath: string;
  OutputDir: string;
  OutputDirStatus: LongInt;
  TempFileCount: LongInt;
  Ch: Char;
begin
  FillChar(Opts, SizeOf(Opts), 0);
  ClearAllocated(Files, Length(Files));
  FileCount := 0;

  Step := 1;
  Codec := 1;
  Profile := 2;
  Deblock := 1;
  AudioNorm := 3;
  AudioOutput := 1;
  Genre := 1;
  Overwrite := 0;
  VideoTrackPath := '';
  OutputDir := '';
  OutputDirStatus := 0;
  TempFileCount := 0;

  while (Step <> 12) and (Step <> 0) do
  begin
    case Step of
      1:
        begin
          ClearScreen;
          WriteLn('----ffmpeg_converter_simple_gui----');
          WriteLn;
          WriteLn('select codec');
          WriteLn('----------------------');
          WriteLn('  1. copy (default)');
          WriteLn('  2. prores');
          WriteLn('  3. prores_ks');
          WriteLn('  4. mux');
          if IsLinux then
          begin
            WriteLn('  5. h264_vaapi');
            WriteLn('  6. hevc_vaapi');
          end;
          if IsDarwin then
          begin
            WriteLn('  7. prores_videotoolbox');
            WriteLn('  8. hevc_videotoolbox');
          end;
          WriteLn('----------------------');
          Write('select: number->choice,Enter->(default),c->cancel,b->back');
          WriteLn;
          Write('>');
          Ch := ReadChoice;
          if Ch = #10 then
            Step := 4
          else if Ch = '1' then
          begin
            Codec := 1;
            Step := 4;
          end
          else if Ch = '2' then
          begin
            Codec := 2;
            Step := 2;
          end
          else if Ch = '3' then
          begin
            Codec := 3;
            Step := 2;
          end
          else if Ch = '4' then
          begin
            Codec := 4;
            Step := 8;
          end
          else if (Ch = '5') and IsLinux then
          begin
            Codec := 5;
            Step := 4;
          end
          else if (Ch = '6') and IsLinux then
          begin
            Codec := 6;
            Step := 4;
          end
          else if (Ch = '7') and IsDarwin then
          begin
            Codec := 7;
            Step := 4;
          end
          else if (Ch = '8') and IsDarwin then
          begin
            Codec := 8;
            Step := 4;
          end
          else if (Ch = 'c') or (Ch = 'C') then
          begin
            ClearAllocated(Files, TempFileCount);
            Exit(False);
          end
          else if (Ch = 'b') or (Ch = 'B') then
            Step := 1
          else
            WriteLn('Invalid choice');
        end;

      2:
        begin
          ClearScreen;
          WriteLn('----ffmpeg_converter_simple_gui----');
          WriteLn;
          WriteLn('select profile');
          WriteLn('-----------------------');
          WriteLn('  1. lt');
          WriteLn('  2. standard (default)');
          WriteLn('  3. hq');
          WriteLn('  4. 4444');
          WriteLn('-----------------------');
          Write('select: number->choice,Enter->(default),c->cancel,b->back');
          WriteLn;
          Write('>');
          Ch := ReadChoice;
          if Ch = #10 then
          begin
            Profile := 2;
            Step := 3;
          end
          else if Ch = '1' then
          begin
            Profile := 1;
            Step := 3;
          end
          else if Ch = '2' then
          begin
            Profile := 2;
            Step := 3;
          end
          else if Ch = '3' then
          begin
            Profile := 3;
            Step := 3;
          end
          else if Ch = '4' then
          begin
            Profile := 4;
            Step := 3;
          end
          else if (Ch = 'c') or (Ch = 'C') then
          begin
            ClearAllocated(Files, TempFileCount);
            Exit(False);
          end
          else if (Ch = 'b') or (Ch = 'B') then
            Step := 1
          else
            WriteLn('Invalid choice');
        end;

      3:
        begin
          ClearScreen;
          WriteLn('----ffmpeg_converter_simple_gui----');
          WriteLn;
          WriteLn('select deblock');
          WriteLn('---------------------------');
          WriteLn('  1. none (default)');
          WriteLn('  2. weak (4K content)');
          WriteLn('  3. strong (1080p content)');
          WriteLn('---------------------------');
          Write('select: number->choice,Enter->(default),c->cancel,b->back');
          WriteLn;
          Write('>');
          Ch := ReadChoice;
          if Ch = #10 then
          begin
            Deblock := 1;
            Step := 4;
          end
          else if Ch = '1' then
          begin
            Deblock := 1;
            Step := 4;
          end
          else if Ch = '2' then
          begin
            Deblock := 2;
            Step := 4;
          end
          else if Ch = '3' then
          begin
            Deblock := 3;
            Step := 4;
          end
          else if (Ch = 'c') or (Ch = 'C') then
          begin
            ClearAllocated(Files, TempFileCount);
            Exit(False);
          end
          else if (Ch = 'b') or (Ch = 'B') then
            Step := 2
          else
            WriteLn('Invalid choice');
        end;

      4:
        begin
          ClearScreen;
          WriteLn('----ffmpeg_converter_simple_gui----');
          WriteLn;
          WriteLn('select audio normalization');
          WriteLn('---------------------------------');
          WriteLn('  1. none');
          WriteLn('  2. peak');
          WriteLn('  3. peak 2-pass (default)');
          WriteLn('  4. loudness normalization');
          WriteLn('  5. loudness normalization 2-pass');
          WriteLn('---------------------------------');
          Write('select: number->choice,Enter->(default),c->cancel,b->back');
          WriteLn;
          Write('>');
          Ch := ReadChoice;
          if Ch = #10 then
          begin
            AudioNorm := 3;
            Step := 6;
          end
          else if Ch = '1' then
          begin
            AudioNorm := 1;
            Step := 6;
          end
          else if Ch = '2' then
          begin
            AudioNorm := 2;
            Step := 6;
          end
          else if Ch = '3' then
          begin
            AudioNorm := 3;
            Step := 6;
          end
          else if Ch = '4' then
          begin
            AudioNorm := 4;
            Step := 6;
          end
          else if Ch = '5' then
          begin
            AudioNorm := 5;
            Step := 5;
          end
          else if (Ch = 'c') or (Ch = 'C') then
          begin
            ClearAllocated(Files, TempFileCount);
            Exit(False);
          end
          else if (Ch = 'b') or (Ch = 'B') then
            Step := 3
          else
            WriteLn('Invalid choice');
        end;

      5:
        begin
          ClearScreen;
          WriteLn('----ffmpeg_converter_simple_gui----');
          WriteLn;
          WriteLn('select audio normalization genre');
          WriteLn('---------------------------------');
          WriteLn('  1. EDM (default)');
          WriteLn('  2. Rock');
          WriteLn('  3. HipHop');
          WriteLn('  4. Classical');
          WriteLn('  5. Podcast');
          WriteLn('---------------------------------');
          Write('select: number->choice,Enter->(default),c->cancel,b->back');
          WriteLn;
          Write('>');
          Ch := ReadChoice;
          if Ch = #10 then
          begin
            Genre := 1;
            Step := 6;
          end
          else if Ch = '1' then
          begin
            Genre := 1;
            Step := 6;
          end
          else if Ch = '2' then
          begin
            Genre := 2;
            Step := 6;
          end
          else if Ch = '3' then
          begin
            Genre := 3;
            Step := 6;
          end
          else if Ch = '4' then
          begin
            Genre := 4;
            Step := 6;
          end
          else if Ch = '5' then
          begin
            Genre := 5;
            Step := 6;
          end
          else if (Ch = 'c') or (Ch = 'C') then
          begin
            ClearAllocated(Files, TempFileCount);
            Exit(False);
          end
          else if (Ch = 'b') or (Ch = 'B') then
            Step := 4
          else
            WriteLn('Invalid choice');
        end;

      6:
        begin
          ClearScreen;
          WriteLn('----ffmpeg_converter_simple_gui----');
          WriteLn;
          WriteLn('select audio output');
          WriteLn('----------------------------------');
          WriteLn('  1. pcm (default)');
          WriteLn('  2. fdk_aac_q5');
          WriteLn('  3. fdk_aac_q5 + ac3_b640');
          WriteLn('----------------------------------');
          Write('select: number->choice,Enter->(default),c->cancel,b->back');
          WriteLn;
          Write('>');
          Ch := ReadChoice;
          if Ch = #10 then
          begin
            AudioOutput := 1;
            Step := 7;
          end
          else if Ch = '1' then
          begin
            AudioOutput := 1;
            Step := 7;
          end
          else if Ch = '2' then
          begin
            AudioOutput := 2;
            Step := 7;
          end
          else if Ch = '3' then
          begin
            AudioOutput := 3;
            Step := 7;
          end
          else if (Ch = 'c') or (Ch = 'C') then
          begin
            ClearAllocated(Files, TempFileCount);
            Exit(False);
          end
          else if (Ch = 'b') or (Ch = 'B') then
            Step := 5
          else
            WriteLn('Invalid choice');
        end;

      7:
        begin
          WriteLn;
          WriteLn('choice if overwrite files: yes/No');
          Write('select:y/n,Enter->(default),c->cancel,b->back');
          WriteLn;
          Write('>');
          Ch := ReadChoice;
          if Ch = #10 then
          begin
            Overwrite := 0;
            Step := 8;
          end
          else if (Ch = 'y') or (Ch = 'Y') then
          begin
            Overwrite := 1;
            Step := 8;
          end
          else if (Ch = 'n') or (Ch = 'N') then
          begin
            Overwrite := 0;
            Step := 8;
          end
          else if (Ch = 'c') or (Ch = 'C') then
          begin
            ClearAllocated(Files, TempFileCount);
            Exit(False);
          end
          else if (Ch = 'b') or (Ch = 'B') then
            Step := 6
          else
            WriteLn('Invalid choice');
        end;

      8:
        begin
          ClearScreen;
          WriteLn('----ffmpeg_converter_simple_gui----');
          WriteLn;
          if ReadOutputDir(OutputDir, OutputDirStatus) then
            Step := 9
          else
          begin
            ClearAllocated(Files, TempFileCount);
            Exit(False);
          end;
        end;

      9:
        begin
          ClearScreen;
          WriteLn('----ffmpeg_converter_simple_gui----');
          WriteLn;
          if ReadInputList(Files, TempFileCount) then
          begin
            if (Codec = 4) and (TempFileCount <> 1) then
            begin
              WriteLn('Mux mode requires exactly one source file.');
              ClearAllocated(Files, TempFileCount);
              Exit(False);
            end;
            if Codec = 4 then
              Step := 10
            else
              Step := 11;
          end
          else
          begin
            ClearAllocated(Files, TempFileCount);
            Exit(False);
          end;
        end;

      10:
        begin
          WriteLn('video-track file for mux mode:');
          Write('> ');
          if EOF(Input) then
            Exit(False);
          ReadLn(VideoTrackPath);
          VideoTrackPath := Trim(VideoTrackPath);
          if not FileRegular(VideoTrackPath) or not FileReadable(VideoTrackPath) then
          begin
            WriteLn('Invalid video-track file: ', VideoTrackPath);
            ClearAllocated(Files, TempFileCount);
            Exit(False);
          end;
          Step := 11;
        end;

      11:
        begin
          case Codec of
            1: SetAnsiField(Opts.codec, 'copy');
            2: SetAnsiField(Opts.codec, 'prores');
            3: SetAnsiField(Opts.codec, 'prores_ks');
            4: SetAnsiField(Opts.codec, 'mux');
            5:
              if IsLinux then
                SetAnsiField(Opts.codec, 'h264_vaapi')
              else
                SetAnsiField(Opts.codec, 'copy');
            6:
              if IsLinux then
                SetAnsiField(Opts.codec, 'hevc_vaapi')
              else
                SetAnsiField(Opts.codec, 'copy');
            7:
              if IsDarwin then
                SetAnsiField(Opts.codec, 'prores_videotoolbox')
              else
                SetAnsiField(Opts.codec, 'copy');
            8:
              if IsDarwin then
                SetAnsiField(Opts.codec, 'hevc_videotoolbox')
              else
                SetAnsiField(Opts.codec, 'copy');
          end;

          Opts.profile := Profile;
          Opts.deblock := Deblock;

          case AudioNorm of
            1: SetAnsiField(Opts.audio_norm, 'none');
            2: SetAnsiField(Opts.audio_norm, 'peak_norm');
            3: SetAnsiField(Opts.audio_norm, 'peak_norm_2pass');
            4: SetAnsiField(Opts.audio_norm, 'loudness_norm');
            5: SetAnsiField(Opts.audio_norm, 'loudness_norm_2pass');
          end;

          case AudioOutput of
            1: SetAnsiField(Opts.audio_output_mode, 'pcm');
            2: SetAnsiField(Opts.audio_output_mode, 'fdk_aac_q5');
            3: SetAnsiField(Opts.audio_output_mode, 'fdk_aac_q5_ac3_640');
          end;

          Opts.genre := Genre;
          Opts.overwrite := Overwrite;
          SetAnsiField(Opts.output_dir, OutputDir);
          Opts.output_dir_status := OutputDirStatus;
          if Codec = 4 then
            SetAnsiField(Opts.video_track_path, VideoTrackPath);

          FileCount := TempFileCount;
          Step := 12;
        end;
    end;
  end;

  Result := FileCount > 0;
end;

end.
