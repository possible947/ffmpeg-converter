program reestr_tests;
// Checks the result of GetReestr (unit reestr).
// Usage:
//   reestr_tests              run all checks, print PASS/FAIL lines and a summary
//   reestr_tests --detected   dump the registry for 'all' + the hardware platforms of this machine
//   reestr_tests --dump N...  dump the registry for the given platform names
//   reestr_tests --hq DIR ... any of the above with the HQ_converter release in DIR
//                             (checks the real preset file; 'hq-converter' in --dump)

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

// HQ_converter release root (third_party/hq_converter), set by --root <repo>
var
  HqReleaseDir: string = '';

// Writes Content as <Dir>/HQ_PRESETS_FILE, creating the folders
procedure WriteHqPresets(const Dir, Content: string);
var
  FileName: string;
  F: TextFile;
begin
  FileName := IncludeTrailingPathDelimiter(Dir) + SetDirSeparators(HQ_PRESETS_FILE);
  ForceDirectories(ExtractFileDir(FileName));
  AssignFile(F, FileName);
  Rewrite(F);
  Write(F, Content);
  CloseFile(F);
end;

// Removes the preset file and the folders created by WriteHqPresets
procedure RemoveHqPresets(const Dir: string);
var
  Path: string;
begin
  Path := IncludeTrailingPathDelimiter(Dir) + SetDirSeparators(HQ_PRESETS_FILE);
  DeleteFile(Path);
  repeat
    Path := ExtractFileDir(Path);
    RemoveDir(Path);
  until (Length(Path) <= Length(ExcludeTrailingPathDelimiter(Dir)));
end;

// Registry for 'hq-converter' with the HQ folder set as TPlatformDetection does
function BuildHqReestr(const HqDir: string): TReestr;
var
  L: TPlatformlist;
begin
  L := MakeList([PLATFORM_HQ_ID12]);
  try
    L.Tools.HqConverterPath := HqDir;
    Result := GetReestr(L);
  finally
    L.Free;
  end;
end;

function PresetCount(C: TReestrCodec): integer;
var
  E: TReestrEncoder;
begin
  Result := 0;
  if C <> nil then
    for E in C.EncoderList do
      Inc(Result, Length(E.PresetsList));
end;

// Name of the Current preset of an encoder, '' when none or more than one
function CurrentPreset(E: TReestrEncoder): string;
var
  S: TReestrPreset;
  N: integer;
begin
  Result := '';
  N := 0;
  if E <> nil then
    for S in E.PresetsList do
      if S.Current then
      begin
        Result := S.Name;
        Inc(N);
      end;
  if N <> 1 then
    Result := '';
end;

procedure RunHqChecks;
const
  // Known keys, an unknown key, an encoder without presets (skipped) and a
  // default_preset that does not exist (first preset becomes current)
  SAMPLE =
    '{"schema_version": "v2", "encoders": {' +
    ' "x265": {"default_preset": "Slow", "presets": {"Medium": {}, "Slow": {}}},' +
    ' "new-enc": {"presets": {"one": {}, "two": {}}},' +
    ' "empty-enc": {"default_preset": "x", "presets": {}},' +
    ' "svt-av1": {"default_preset": "missing", "presets": {"balanced": {}}}}}';
var
  Dir, Names: string;
  R: TReestr;
  C: TReestrCodec;
  E: TReestrEncoder;
  S: TReestrPreset;
  PresetsOk: boolean;
begin
  Dir := IncludeTrailingPathDelimiter(GetTempDir(False)) + 'reestr_hq_' + IntToStr(GetProcessID);

  // 10. No HQ folder / no preset file: no HQ group
  R := BuildHqReestr('');
  Check('hq_no_path_no_group', CodecIDs(R) = '1,2,3', CodecIDs(R));
  R.Free;
  R := BuildHqReestr(Dir);
  Check('hq_no_file_no_group', CodecIDs(R) = '1,2,3', CodecIDs(R));
  R.Free;

  // 11. Broken JSON and a file without encoders: no HQ group, no exception
  WriteHqPresets(Dir, '{"encoders": {"x264": ');
  R := BuildHqReestr(Dir);
  Check('hq_invalid_json_no_group', CodecIDs(R) = '1,2,3', CodecIDs(R));
  R.Free;
  WriteHqPresets(Dir, '{"schema_version": "v2", "encoders": {}}');
  R := BuildHqReestr(Dir);
  Check('hq_empty_encoders_no_group', CodecIDs(R) = '1,2,3', CodecIDs(R));
  R.Free;

  // 12. Sample file: names, IDs, order, Current flags, rules and PresetSet
  WriteHqPresets(Dir, SAMPLE);
  R := BuildHqReestr(Dir);
  try
    C := FindCodec(R, CODEC_HQ);
    Check('hq_sample_group', (CodecIDs(R) = '1,2,3,12') and (C <> nil) and C.Available
      and not C.Current and (C.Name = 'HQ-converter'), CodecIDs(R));
    if C = nil then
      Exit;
    Names := '';
    for E in C.EncoderList do
      Names := Names + Format(' %d:%s', [E.ID, E.Name]);
    Check('hq_sample_encoders', Names = ' 19:x265 24:new-enc 21:svt-av1', Names);
    Check('hq_sample_current_encoder', (Length(C.EncoderList) = 3) and C.EncoderList[0].Current
      and not C.EncoderList[1].Current and not C.EncoderList[2].Current);
    Check('hq_sample_default_preset', CurrentPreset(FindEncoder(C, ENC_HQ_X265)) = 'Slow',
      CurrentPreset(FindEncoder(C, ENC_HQ_X265)));
    Check('hq_sample_first_preset_without_default', CurrentPreset(FindEncoder(C, 24)) = 'one',
      CurrentPreset(FindEncoder(C, 24)));
    Check('hq_sample_first_preset_unknown_default', CurrentPreset(FindEncoder(C, ENC_HQ_SVT_AV1)) = 'balanced',
      CurrentPreset(FindEncoder(C, ENC_HQ_SVT_AV1)));
    PresetsOk := PresetCount(C) = 5;
    for E in C.EncoderList do
      for S in E.PresetsList do
        PresetsOk := PresetsOk and S.Available and (S.ID = PRESET_HQ) and S.PresetSet.Defined
          and (S.PresetSet.ToolID = TOOL_HQ_VIDEO_ENCODER) and not S.PresetSet.HasData;
    Check('hq_sample_presets_id40_tool5', PresetsOk);
    E := FindEncoder(C, ENC_HQ_X265);
    Check('hq_sample_rules', (E <> nil) and E.ContainerMovAvailable and E.ContainerMkvAvailable
      and not E.ContainerMp4Available and E.AudioCopyAvailable and E.AudioPcmAvailable);
    Check('hq_sample_tree_rules', TreeErrors(R) = '', TreeErrors(R));
  finally
    R.Free;
    RemoveHqPresets(Dir);
  end;

  // 13. Real HQ_converter release, when it is placed in third_party/hq_converter
  if (HqReleaseDir = '') or not FileExists(IncludeTrailingPathDelimiter(HqReleaseDir) +
    SetDirSeparators(HQ_PRESETS_FILE)) then
  begin
    WriteLn('INFO  hq release: NOT AVAILABLE (third_party/hq_converter)');
    Exit;
  end;
  R := BuildHqReestr(HqReleaseDir);
  try
    C := FindCodec(R, CODEC_HQ);
    Names := '';
    if C <> nil then
      for E in C.EncoderList do
        Names := Names + Format(' %d:%s', [E.ID, E.Name]);
    WriteLn('INFO  hq release encoders:', Names, '  presets ', PresetCount(C));
    Check('hq_release_loaded', (C <> nil) and (Length(C.EncoderList) > 0) and (PresetCount(C) > 0));
    Check('hq_release_tree_rules', TreeErrors(R) = '', TreeErrors(R));
  finally
    R.Free;
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
  I, First: integer;
  Mode: string;
begin
  // Optional leading "--hq <dir>": HQ_converter release folder for the HQ checks and dumps
  First := 1;
  if (ParamCount >= 2) and (ParamStr(1) = '--hq') then
  begin
    HqReleaseDir := ParamStr(2);
    First := 3;
  end;
  Mode := ParamStr(First);

  if Mode = '--dump' then
  begin
    L := TPlatformlist.Create;
    L.Tools.HqConverterPath := HqReleaseDir;
    for I := First + 1 to ParamCount do
      L.AddItem(ParamStr(I));
    R := GetReestr(L);
    L.Free;
    Dump(R);
    R.Free;
    Exit;
  end;
  if Mode = '--detected' then
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
  RunHqChecks;
  WriteLn;
  WriteLn(Passed, ' passed, ', Failed, ' failed');
  if Failed > 0 then
    Halt(1);
end.
