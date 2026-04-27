unit m4v_postprocess;

{$mode objfpc}{$H+}

interface

uses converter_types;

function RunM4VPostprocess(const Opts: TConvertOptions;
                           const InputFile: string): TConverterError;

implementation

uses
  SysUtils,
  path_utils,
  fs_utils,
  apple_m4v_creator;

function ArrToStr(const A: array of AnsiChar): string;
begin
  Result := StrPas(@A[0]);
end;

function ExtractM4VParam(const S: string; Index: Integer; var Remaining: string): string;
var
  P: SizeInt;
begin
  Result := '';
  if (Index < 1) or (S = '') then
    Exit;

  Remaining := S;
  for P := 1 to Index - 1 do
  begin
    if Remaining = '' then
    begin
      Result := '';
      Exit;
    end;
    if Pos('|', Remaining) > 0 then
      Delete(Remaining, 1, Pos('|', Remaining));
  end;

  if Remaining = '' then
    Exit;

  if Pos('|', Remaining) > 0 then
  begin
    Result := Copy(Remaining, 1, Pos('|', Remaining) - 1);
    Delete(Remaining, 1, Pos('|', Remaining));
  end
  else
  begin
    Result := Remaining;
    Remaining := '';
  end;
end;

function RunM4VPostprocess(const Opts: TConvertOptions;
                           const InputFile: string): TConverterError;
var
  IntermediateFile: string;
  OutputFile: string;
  M4VParams: string;
  Remaining{%H-}: string;
  VideoTrackIdx, AudioTrackIdx, AacQuality, Ac3Bitrate: Integer;
  Lang: string;
  AddChapters: Boolean;
  M4VOpts: TAppleM4VOptions;
  ErrorText: string;
  EffectiveOutputDir: string;
begin
  { Resolve effective output directory }
  EffectiveOutputDir := ArrToStr(Opts.output_dir);
  if EffectiveOutputDir = '' then
    EffectiveOutputDir := DefaultOutputDir;

  { Intermediate file is the result of the ffmpeg copy step }
  IntermediateFile := MakeOutputName(InputFile, 'copy', EffectiveOutputDir);
  if not FileExists(IntermediateFile) then
  begin
    WriteLn(StdErr, 'Error: intermediate file not found: ', IntermediateFile);
    Exit(ERR_INPUT_NOT_FOUND);
  end;

  { Parse M4V parameters from video_track_path field }
  M4VParams := ArrToStr(Opts.video_track_path);
  if M4VParams = '' then
  begin
    WriteLn(StdErr, 'Error: M4V parameters not specified');
    Exit(ERR_INVALID_OPTIONS);
  end;

  { Parse parameters: video_idx|audio_idx|aac_quality|ac3_bitrate|lang|add_chapters }
  try
    VideoTrackIdx := StrToIntDef(ExtractM4VParam(M4VParams, 1, Remaining), 0);
    AudioTrackIdx := StrToIntDef(ExtractM4VParam(M4VParams, 2, Remaining), 0);
    AacQuality := StrToIntDef(ExtractM4VParam(M4VParams, 3, Remaining), 5);
    Ac3Bitrate := StrToIntDef(ExtractM4VParam(M4VParams, 4, Remaining), 640);
    Lang := ExtractM4VParam(M4VParams, 5, Remaining);
    if Lang = '' then Lang := 'rus';
    AddChapters := StrToIntDef(ExtractM4VParam(M4VParams, 6, Remaining), 1) <> 0;
  except
    WriteLn(StdErr, 'Error: invalid M4V parameters format');
    Exit(ERR_INVALID_OPTIONS);
  end;

  { Set up M4V options }
  M4VOpts.VideoTrackIndex := VideoTrackIdx;
  M4VOpts.AudioTrackIndex := AudioTrackIdx;
  M4VOpts.AacQuality := AacQuality;
  M4VOpts.Ac3BitrateKbps := Ac3Bitrate;
  M4VOpts.AudioLang := Lang;
  M4VOpts.AddChapters := AddChapters;

  { Output file name }
  OutputFile := MakeOutputName(InputFile, 'm4v', EffectiveOutputDir);

  { Create Apple M4V file }
  WriteLn('Creating Apple M4V file...');
  WriteLn('Input: ', IntermediateFile);
  WriteLn('Output: ', OutputFile);

  if CreateAppleM4V(IntermediateFile, OutputFile, M4VOpts, ErrorText) then
  begin
    WriteLn('M4V creation successful: ', OutputFile);
    Result := ERR_OK;
  end
  else
  begin
    WriteLn(StdErr, 'Error: M4V creation failed: ', ErrorText);
    Result := ERR_FFMPEG_FAILED;
  end;
end;

end.