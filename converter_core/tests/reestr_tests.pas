program reestr_tests;
// Checks the result of GetReestr (unit reestr).
// Usage:
//   reestr_tests              run all checks, print PASS/FAIL lines and a summary
//   reestr_tests --detected   dump the registry for 'all' + the hardware platforms of this machine
//   reestr_tests --dump N...  dump the registry for the given platform names

{$mode objfpc}{$H+}

uses
  SysUtils, converter_core_types, platform_detection, reestr;

var
  Passed, Failed: integer;

// Hardware platforms of the current OS. No tools are needed: only the GPU / Mac
// model is identified, so this works without converter_core/platform/tools.
procedure AddDetectedPlatforms(L: TPlatformlist);
begin
  {$IFDEF WINDOWS}
  WindowsPlatformList(L);
  {$ENDIF}
  {$IFDEF LINUX}
  LinuxPlatformList(L);
  {$ENDIF}
  {$IFDEF DARWIN}
  DarwinPlatformDetection(L);
  {$ENDIF}
end;

procedure Check(const Name: string; Cond: boolean; const Detail: string = '');
begin
  if Cond then
  begin
    Inc(Passed);
    WriteLn('PASS  ', Name);
  end
  else
  begin
    Inc(Failed);
    if Detail <> '' then
      WriteLn('FAIL  ', Name, '  (', Detail, ')')
    else
      WriteLn('FAIL  ', Name);
  end;
end;

function MakeList(const Names: array of string): TPlatformlist;
var
  I: integer;
begin
  Result := TPlatformlist.Create;
  for I := Low(Names) to High(Names) do
    Result.AddItem(Names[I]);
end;

function BuildReestr(const Names: array of string): TReestr;
var
  L: TPlatformlist;
begin
  L := MakeList(Names);
  try
    Result := GetReestr(L);
  finally
    L.Free;
  end;
end;

function CodecIDs(R: TReestr): string;
var
  C: TReestrCodec;
begin
  Result := '';
  for C in R.CodecList do
  begin
    if Result <> '' then
      Result := Result + ',';
    Result := Result + IntToStr(C.ID);
  end;
end;

function FindCodec(R: TReestr; ID: integer): TReestrCodec;
var
  C: TReestrCodec;
begin
  for C in R.CodecList do
    if C.ID = ID then
      Exit(C);
  Result := nil;
end;

function FindEncoder(C: TReestrCodec; ID: integer): TReestrEncoder;
var
  E: TReestrEncoder;
begin
  Result := nil;
  if C = nil then
    Exit;
  for E in C.EncoderList do
    if E.ID = ID then
      Exit(E);
end;

function AvailablePresets(E: TReestrEncoder): integer;
var
  S: TReestrPreset;
begin
  Result := 0;
  if E = nil then
    Exit;
  for S in E.PresetsList do
    if S.Available then
      Inc(Result);
end;

// Encoder exists, Available matches and the number of available presets is as expected
procedure CheckEncoder(const Name: string; R: TReestr; CodecID, EncID: integer;
  WantAvailable: boolean; WantPresets: integer);
var
  E: TReestrEncoder;
begin
  E := FindEncoder(FindCodec(R, CodecID), EncID);
  if E = nil then
  begin
    Check(Name, False, 'encoder not found');
    Exit;
  end;
  Check(Name, (E.Available = WantAvailable) and (AvailablePresets(E) = WantPresets),
    Format('available=%s presets=%d, want %s/%d',
      [BoolToStr(E.Available, True), AvailablePresets(E), BoolToStr(WantAvailable, True), WantPresets]));
end;

// Rules for the whole tree: available presets have an implementation,
// Current implies Available, an unavailable parent has no available children.
function TreeErrors(R: TReestr): string;
var
  C: TReestrCodec;
  E: TReestrEncoder;
  S: TReestrPreset;
  Where: string;
begin
  Result := '';
  for C in R.CodecList do
  begin
    if C.Current and not C.Available then
      Result := Result + Format(' codec %d current but not available;', [C.ID]);
    for E in C.EncoderList do
    begin
      Where := Format('%d/%d', [C.ID, E.ID]);
      if E.Current and not E.Available then
        Result := Result + ' encoder ' + Where + ' current but not available;';
      if E.Available and not C.Available then
        Result := Result + ' encoder ' + Where + ' available in unavailable codec;';
      for S in E.PresetsList do
      begin
        if S.Current and not S.Available then
          Result := Result + Format(' preset %s/%d current but not available;', [Where, S.ID]);
        if S.Available and not E.Available then
          Result := Result + Format(' preset %s/%d available in unavailable encoder;', [Where, S.ID]);
        if S.Available and (not S.PresetSet.Defined or (S.PresetSet.ToolID = TOOL_NOT_DEFINED)) then
          Result := Result + Format(' preset %s/%d available without PresetSet;', [Where, S.ID]);
      end;
    end;
  end;
end;

procedure Dump(R: TReestr);
var
  C: TReestrCodec;
  E: TReestrEncoder;
  S: TReestrPreset;
begin
  for C in R.CodecList do
  begin
    WriteLn(Format('codec %d %s available=%s current=%s',
      [C.ID, C.Name, BoolToStr(C.Available, True), BoolToStr(C.Current, True)]));
    for E in C.EncoderList do
    begin
      WriteLn(Format('  encoder %d %s available=%s current=%s',
        [E.ID, E.Name, BoolToStr(E.Available, True), BoolToStr(E.Current, True)]));
      for S in E.PresetsList do
      begin
        WriteLn(Format('    preset %d %s available=%s current=%s tool=%d',
          [S.ID, S.Name, BoolToStr(S.Available, True), BoolToStr(S.Current, True), S.PresetSet.ToolID]));
        if S.Available and S.PresetSet.HasData then
          WriteLn('      before="', S.PresetSet.BeforeOptions, '" after="', S.PresetSet.AfterOptions,
            '" filter="', S.PresetSet.FilterOptions, '" encoder="', S.PresetSet.EncoderOptions,
            '" muxer="', S.PresetSet.MuxerOptions, '"');
      end;
    end;
  end;
end;

procedure RunChecks;
const
  WINDOWS_NAMES: array[0..2] of string = (PLATFORM_WINDOWS_ID9, PLATFORM_WINDOWS_ID10, PLATFORM_WINDOWS_ID11);
  ALL_NAMES: array[0..11] of string = (PLATFORM_ALL_ID1, PLATFORM_DARWIN_ID2, PLATFORM_DARWIN_ID3,
    PLATFORM_DARWIN_ID4, PLATFORM_LINUX_ID5, PLATFORM_LINUX_ID6, PLATFORM_LINUX_ID7, PLATFORM_LINUX_ID8,
    PLATFORM_WINDOWS_ID9, PLATFORM_WINDOWS_ID10, PLATFORM_WINDOWS_ID11, PLATFORM_HQ_ID12);
var
  R, R2: TReestr;
  L: TPlatformlist;
  I: integer;
  Detected: string;
  E: TReestrEncoder;
begin
  // 1. Empty list still gives Software, Mux and Audio
  R := BuildReestr([]);
  Check('empty_list_base_groups', CodecIDs(R) = '1,2,3', CodecIDs(R));
  CheckEncoder('empty_list_aac_at_unavailable', R, CODEC_AUDIO, ENC_AUDIO_AAC_AT, False, 0);
  R.Free;

  // 2. Unknown, old (dgx1) and duplicate names are ignored
  R := BuildReestr(['unknown', PLATFORM_WINDOWS_ID9, PLATFORM_WINDOWS_ID9, 'windows_qsv_dgx1']);
  Check('unknown_and_duplicate_ignored', CodecIDs(R) = '1,2,3,9', CodecIDs(R));
  R.Free;

  // 3. Fixed codec order whatever the request order is
  R := BuildReestr([PLATFORM_WINDOWS_ID11, PLATFORM_WINDOWS_ID10, PLATFORM_WINDOWS_ID9]);
  R2 := BuildReestr(WINDOWS_NAMES);
  Check('fixed_codec_order', (CodecIDs(R) = '1,2,3,9,10,11') and (CodecIDs(R) = CodecIDs(R2)),
    CodecIDs(R) + ' vs ' + CodecIDs(R2));
  R2.Free;

  // 4. windows_nvenc7 (9): H264/HEVC only; no 10-bit on Windows, no AV1 on NVENC 7
  CheckEncoder('win_nvenc7_h264', R, CODEC_WINDOWS_NVENC7, ENC_H264, True, 4);
  CheckEncoder('win_nvenc7_hevc', R, CODEC_WINDOWS_NVENC7, ENC_HEVC, True, 4);
  CheckEncoder('win_nvenc7_hevc10_off', R, CODEC_WINDOWS_NVENC7, ENC_HEVC_10, False, 0);
  CheckEncoder('win_nvenc7_av1_off', R, CODEC_WINDOWS_NVENC7, ENC_AV1, False, 0);
  CheckEncoder('win_nvenc7_av1_10_off', R, CODEC_WINDOWS_NVENC7, ENC_AV1_10, False, 0);

  // 5. windows_qsv_dg2 (10): AV1 available, 10-bit not
  CheckEncoder('win_qsv_dg2_h264', R, CODEC_WINDOWS_QSV_DG2, ENC_H264, True, 4);
  CheckEncoder('win_qsv_dg2_hevc', R, CODEC_WINDOWS_QSV_DG2, ENC_HEVC, True, 4);
  CheckEncoder('win_qsv_dg2_hevc10_off', R, CODEC_WINDOWS_QSV_DG2, ENC_HEVC_10, False, 0);
  CheckEncoder('win_qsv_dg2_av1', R, CODEC_WINDOWS_QSV_DG2, ENC_AV1, True, 4);
  CheckEncoder('win_qsv_dg2_av1_10_off', R, CODEC_WINDOWS_QSV_DG2, ENC_AV1_10, False, 0);

  // 6. windows_vulkan_nvidia (11): ProRes with the prores_ks_vulkan preset
  CheckEncoder('win_vulkan_prores', R, CODEC_WINDOWS_VULKAN, ENC_PRORES, True, 1);
  E := FindEncoder(FindCodec(R, CODEC_WINDOWS_VULKAN), ENC_PRORES);
  Check('win_vulkan_prores_ks_vulkan',
    (E <> nil) and (Length(E.PresetsList) > 0) and (Pos('prores_ks_vulkan', E.PresetsList[0].PresetSet.EncoderOptions) > 0));
  Check('windows_tree_rules', TreeErrors(R) = '', TreeErrors(R));
  R.Free;

  // 7. Linux nvenc7 (6): HEVC_10 available, AV1 not; Linux qsv_dg2 (7): AV1 and AV1_10 available
  R := BuildReestr([PLATFORM_LINUX_ID6, PLATFORM_LINUX_ID7]);
  CheckEncoder('linux_nvenc7_hevc10', R, CODEC_LINUX_NVENC7, ENC_HEVC_10, True, 4);
  CheckEncoder('linux_nvenc7_av1_off', R, CODEC_LINUX_NVENC7, ENC_AV1, False, 0);
  CheckEncoder('linux_nvenc7_av1_10_off', R, CODEC_LINUX_NVENC7, ENC_AV1_10, False, 0);
  CheckEncoder('linux_qsv_dg2_av1', R, CODEC_LINUX_QSV_DG2, ENC_AV1, True, 4);
  CheckEncoder('linux_qsv_dg2_av1_10', R, CODEC_LINUX_QSV_DG2, ENC_AV1_10, True, 4);
  R.Free;

  // 8. Every platform at once: tree rules hold for the whole registry
  R := BuildReestr(ALL_NAMES);
  Check('all_platforms_tree_rules', TreeErrors(R) = '', TreeErrors(R));
  Check('all_platforms_codec_order', CodecIDs(R) = '1,2,3,4,4,5,6,7,8,9,10,11', CodecIDs(R));
  R.Free;

  // 9. Registry for the platforms detected on this machine
  L := TPlatformlist.Create;
  try
    L.AddItem(PLATFORM_ALL_ID1);
    AddDetectedPlatforms(L);
    Detected := '';
    for I := 0 to L.Count - 1 do
      Detected := Detected + ' ' + L.GetItem(I);
    R := GetReestr(L);
  finally
    L.Free;
  end;
  WriteLn('INFO  detected platforms:', Detected, '  -> codec groups ', CodecIDs(R));
  Check('detected_tree_rules', TreeErrors(R) = '', TreeErrors(R));
  R.Free;
end;

var
  L: TPlatformlist;
  R: TReestr;
  I: integer;
begin
  if (ParamCount >= 1) and (ParamStr(1) = '--dump') then
  begin
    L := TPlatformlist.Create;
    for I := 2 to ParamCount do
      L.AddItem(ParamStr(I));
    R := GetReestr(L);
    L.Free;
    Dump(R);
    R.Free;
    Exit;
  end;
  if (ParamCount >= 1) and (ParamStr(1) = '--detected') then
  begin
    L := TPlatformlist.Create;
    L.AddItem(PLATFORM_ALL_ID1);
    AddDetectedPlatforms(L);
    R := GetReestr(L);
    L.Free;
    Dump(R);
    R.Free;
    Exit;
  end;

  Passed := 0;
  Failed := 0;
  RunChecks;
  WriteLn;
  WriteLn(Passed, ' passed, ', Failed, ' failed');
  if Failed > 0 then
    Halt(1);
end.
