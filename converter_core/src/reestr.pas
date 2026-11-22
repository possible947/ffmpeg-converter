unit reestr;

// Registry (Reestr) of the converter core.
// Rules: converter_core/docs/TReestr_description.md,
//        converter_core/docs/converter_core_archtecture.md (step 4).
//
// GetReestr is the only entry point. It receives the platform names found by
// TPlatformDetection and returns one TReestr object:
//
//   TReestr
//     CodecList[]      codec groups of all requested platforms
//       EncoderList[]  encoders with their rules
//         PresetsList[] presets with a ready PresetSet
//
// Internally it uses two prepare steps:
//   PrepareHighReestr - WHAT is allowed: platforms -> codecs -> encoders -> presets
//   PrepareLowReestr  - HOW it is implemented: Preset ID + Platform ID -> PresetSet
// After GetReestr finishes, the platform level and the low registry are gone;
// the core works only with the returned TReestr and frees it with TReestr.Free.

{$mode objfpc}{$H+}

interface

uses
  converter_core_types;

const
  // Tool IDs (TReestr_description.md, "Идентификаторы инструментов и назначений")
  TOOL_NOT_DEFINED = 0;
  TOOL_FFMPEG_VIDEO_ENCODER = 1;
  TOOL_FFMPEG_AUDIO_ENCODER = 2;
  TOOL_FDK_AAC_AUDIO_ENCODER = 3;
  TOOL_AAC_AT_AUDIO_ENCODER = 4;
  TOOL_HQ_VIDEO_ENCODER = 5;
  TOOL_FFMPEG_MUXER = 6;
  TOOL_MUX_MUXER = 7;
  TOOL_FFMPEG_AUDIO_FILTER = 10;
  TOOL_FFMPEG_VIDEO_FILTER = 11;

  // Codec group IDs
  CODEC_SOFTWARE = 1;
  CODEC_MUX = 2;
  CODEC_AUDIO = 3;
  CODEC_VIDEOTOOLBOX = 4;
  CODEC_VAAPI = 5;
  CODEC_LINUX_NVENC7 = 6;
  CODEC_LINUX_QSV_DG2 = 7;
  CODEC_LINUX_VULKAN = 8;
  CODEC_WINDOWS_NVENC7 = 9;
  CODEC_WINDOWS_QSV_DG2 = 10;
  CODEC_WINDOWS_VULKAN = 11;

  // Encoder IDs. The same encoder ID in different codec groups has the same rules.
  ENC_PRORES = 1;
  ENC_PRORES_KS = 2;
  ENC_X264 = 3;
  ENC_MUX_COPY = 4;
  ENC_MUX_MKV = 5;
  ENC_MUX_MOV = 6;
  ENC_MUX_MP4 = 7;
  ENC_AUDIO_COPY = 8;
  ENC_AUDIO_PCM = 9;
  ENC_AUDIO_AAC_FDK = 10;
  ENC_AUDIO_AC3 = 11;
  ENC_AUDIO_AAC_AT = 12;
  ENC_H264 = 13;
  ENC_HEVC = 14;
  ENC_HEVC_10 = 15;
  ENC_AV1 = 16;
  ENC_AV1_10 = 17;

type
  // Implementation data of one preset for the current platform (from the low registry)
  TPresetSet = record
    Defined: boolean;        // False: no implementation for this preset on this platform
    ToolID: integer;         // TOOL_* constant
    HasData: boolean;        // False: implementation needs no extra data (VariantData = null)
    BeforeOptions: string;   // FFmpeg options before -i
    AfterOptions: string;    // FFmpeg options after the input file
    FilterOptions: string;   // filter / filtergraph options
    EncoderOptions: string;  // encoder selection and settings
    MuxerOptions: string;    // FFmpeg muxer options
  end;

  // TReestrPreset represents a preset of an encoder

  TReestrPreset = class
  private
    FLowPlatformID: integer; // Platform ID used to resolve PresetSet; only used inside GetReestr
  public
    ID: integer;
    Name: string;
    Available: boolean;
    Current: boolean;
    PresetSet: TPresetSet;
  end;

  // TReestrEncoder represents an encoder of a codec group and its rules

  TReestrEncoder = class
  private
    ItemsCount: integer;
  public
    ID: integer;
    Name: string;
    Available: boolean;
    Current: boolean;
    ContainerMovAvailable: boolean;    // mov container is allowed
    ContainerMkvAvailable: boolean;    // mkv container is allowed
    ContainerMp4Available: boolean;    // mp4 container is allowed
    AudioPcmAvailable: boolean;        // pcm audio encoder is allowed
    AudioCopyAvailable: boolean;       // audio copy is allowed when possible
    AudioAacAvailable: boolean;        // aac audio encoder is allowed
    AudioAc3Available: boolean;        // ac3 audio encoder is allowed
    MediaChaptersAvailable: boolean;   // chapters import is allowed
    MediaSubtitlesAvailable: boolean;  // subtitles import is allowed
    MediaMetaDataAvailable: boolean;   // metadata import is allowed
    FiltersOnePass: boolean;           // filters in the same pass are allowed
    FiltersTwoPass: boolean;           // filters in a separate pass are allowed
    AudioAllTracksAvailable: boolean;  // audio encoders: all audio tracks are processed
    AudioFilterAvailable: boolean;     // audio encoders: audio filters are allowed
    PresetsList: array of TReestrPreset;
    destructor Destroy; override;
    function GetItemsCount: integer;
    procedure AddItem(Item: TReestrPreset);
  end;

  // TReestrCodec represents a codec group

  TReestrCodec = class
  private
    ItemsCount: integer;
  public
    ID: integer;
    Name: string;
    Available: boolean;
    Current: boolean;
    EncoderList: array of TReestrEncoder;
    destructor Destroy; override;
    function GetItemsCount: integer;
    procedure AddItem(Item: TReestrEncoder);
  end;

  // TReestr is the result of GetReestr: all codec groups of the requested platforms

  TReestr = class
  private
    ItemsCount: integer;
  public
    CodecList: array of TReestrCodec;
    destructor Destroy; override;
    function GetItemsCount: integer;
    procedure AddItem(Item: TReestrCodec);
  end;

// Builds the registry for the platform names in PlatformList.
// Platform 'all' (ID 1) is always included. Unknown names are ignored.
// The caller owns the result and frees it with TReestr.Free.
function GetReestr(PlatformList: TPlatformlist): TReestr;

implementation

const
  PLATFORM_COUNT = 12;
  PLATFORM_NAMES: array[1..PLATFORM_COUNT] of string = (
    PLATFORM_ALL_ID1, PLATFORM_DARWIN_ID2, PLATFORM_DARWIN_ID3, PLATFORM_DARWIN_ID4,
    PLATFORM_LINUX_ID5, PLATFORM_LINUX_ID6, PLATFORM_LINUX_ID7, PLATFORM_LINUX_ID8,
    PLATFORM_WINDOWS_ID9, PLATFORM_WINDOWS_ID10, PLATFORM_WINDOWS_ID11, PLATFORM_HQ_ID12);

type
  // Encoder rules, one row of the table "Правила TEncoder"
  TEncoderRules = record
    Mov, Mkv, Mp4, Pcm, ACopy, Aac, Ac3, Chapters, Subtitles, Metadata, OnePass, TwoPass: boolean;
  end;

const
  //                                 MOV          MKV         MP4         PCM        Audio copy    AAC         AC3         Chapters         Subtitles         Metadata         One pass        Pre-pass
  RULES_NONE:   TEncoderRules = (Mov: False; Mkv: False; Mp4: False; Pcm: False; ACopy: False; Aac: False; Ac3: False; Chapters: False; Subtitles: False; Metadata: False; OnePass: False; TwoPass: False);
  RULES_PRORES: TEncoderRules = (Mov: True;  Mkv: False; Mp4: False; Pcm: True;  ACopy: False; Aac: False; Ac3: False; Chapters: True;  Subtitles: False; Metadata: False; OnePass: True;  TwoPass: True);
  RULES_X264:   TEncoderRules = (Mov: True;  Mkv: False; Mp4: False; Pcm: True;  ACopy: False; Aac: False; Ac3: False; Chapters: True;  Subtitles: False; Metadata: False; OnePass: True;  TwoPass: True);
  RULES_COPY:   TEncoderRules = (Mov: False; Mkv: True;  Mp4: False; Pcm: True;  ACopy: True;  Aac: True;  Ac3: True;  Chapters: True;  Subtitles: True;  Metadata: True;  OnePass: True;  TwoPass: False);
  RULES_MOV:    TEncoderRules = (Mov: True;  Mkv: False; Mp4: False; Pcm: True;  ACopy: False; Aac: False; Ac3: False; Chapters: True;  Subtitles: False; Metadata: False; OnePass: True;  TwoPass: True);
  RULES_MKV:    TEncoderRules = (Mov: False; Mkv: True;  Mp4: False; Pcm: True;  ACopy: True;  Aac: True;  Ac3: True;  Chapters: True;  Subtitles: True;  Metadata: True;  OnePass: True;  TwoPass: True);
  RULES_MP4:    TEncoderRules = (Mov: False; Mkv: False; Mp4: True;  Pcm: True;  ACopy: False; Aac: True;  Ac3: True;  Chapters: True;  Subtitles: True;  Metadata: True;  OnePass: True;  TwoPass: False);
  RULES_H264:   TEncoderRules = (Mov: False; Mkv: True;  Mp4: True;  Pcm: True;  ACopy: False; Aac: True;  Ac3: True;  Chapters: True;  Subtitles: True;  Metadata: True;  OnePass: True;  TwoPass: True);
  RULES_HEVC:   TEncoderRules = (Mov: False; Mkv: True;  Mp4: True;  Pcm: True;  ACopy: False; Aac: True;  Ac3: True;  Chapters: True;  Subtitles: True;  Metadata: True;  OnePass: True;  TwoPass: True);
  RULES_AV1:    TEncoderRules = (Mov: False; Mkv: True;  Mp4: False; Pcm: True;  ACopy: False; Aac: True;  Ac3: True;  Chapters: True;  Subtitles: True;  Metadata: True;  OnePass: True;  TwoPass: True);

type
  // TReestrPlatform is the internal platform level of the high registry.
  // GetReestr moves its codec groups into TReestr and then frees it.
  TReestrPlatform = class
  private
    ItemsCount: integer;
  public
    ID: integer;
    Name: string;
    Available: boolean;
    Current: boolean;
    CodecList: array of TReestrCodec;
    destructor Destroy; override;
    procedure AddItem(Item: TReestrCodec);
  end;
  TReestrPlatformArray = array of TReestrPlatform;

  // One element of the low registry: Preset ID + Platform ID -> implementation
  TLowEntry = record
    PresetID: integer;
    PlatformID: integer;
    Data: TPresetSet;
  end;
  TLowEntryArray = array of TLowEntry;

{ TReestrEncoder }

destructor TReestrEncoder.Destroy;
var
  I: integer;
begin
  for I := 0 to ItemsCount - 1 do
    PresetsList[I].Free;
  inherited Destroy;
end;

function TReestrEncoder.GetItemsCount: integer;
begin
  Result := ItemsCount;
end;

procedure TReestrEncoder.AddItem(Item: TReestrPreset);
begin
  Inc(ItemsCount);
  SetLength(PresetsList, ItemsCount);
  PresetsList[ItemsCount - 1] := Item;
end;

{ TReestrCodec }

destructor TReestrCodec.Destroy;
var
  I: integer;
begin
  for I := 0 to ItemsCount - 1 do
    EncoderList[I].Free;
  inherited Destroy;
end;

function TReestrCodec.GetItemsCount: integer;
begin
  Result := ItemsCount;
end;

procedure TReestrCodec.AddItem(Item: TReestrEncoder);
begin
  Inc(ItemsCount);
  SetLength(EncoderList, ItemsCount);
  EncoderList[ItemsCount - 1] := Item;
end;

{ TReestr }

destructor TReestr.Destroy;
var
  I: integer;
begin
  for I := 0 to ItemsCount - 1 do
    CodecList[I].Free;
  inherited Destroy;
end;

function TReestr.GetItemsCount: integer;
begin
  Result := ItemsCount;
end;

procedure TReestr.AddItem(Item: TReestrCodec);
begin
  Inc(ItemsCount);
  SetLength(CodecList, ItemsCount);
  CodecList[ItemsCount - 1] := Item;
end;

{ TReestrPlatform }

destructor TReestrPlatform.Destroy;
var
  I: integer;
begin
  for I := 0 to ItemsCount - 1 do
    CodecList[I].Free;
  inherited Destroy;
end;

procedure TReestrPlatform.AddItem(Item: TReestrCodec);
begin
  Inc(ItemsCount);
  SetLength(CodecList, ItemsCount);
  CodecList[ItemsCount - 1] := Item;
end;

{ High registry helpers }

function NewPlatform(AID: integer; AAvailable, ACurrent: boolean): TReestrPlatform;
begin
  Result := TReestrPlatform.Create;
  Result.ID := AID;
  Result.Name := PLATFORM_NAMES[AID];
  Result.Available := AAvailable;
  Result.Current := ACurrent;
end;

function AddCodec(P: TReestrPlatform; AID: integer; const AName: string;
  AAvailable, ACurrent: boolean): TReestrCodec;
begin
  Result := TReestrCodec.Create;
  Result.ID := AID;
  Result.Name := AName;
  Result.Available := AAvailable;
  Result.Current := ACurrent;
  P.AddItem(Result);
end;

function AddEncoder(C: TReestrCodec; AID: integer; const AName: string;
  AAvailable, ACurrent: boolean; const R: TEncoderRules): TReestrEncoder;
begin
  Result := TReestrEncoder.Create;
  Result.ID := AID;
  Result.Name := AName;
  Result.Available := AAvailable;
  Result.Current := ACurrent;
  Result.ContainerMovAvailable := R.Mov;
  Result.ContainerMkvAvailable := R.Mkv;
  Result.ContainerMp4Available := R.Mp4;
  Result.AudioPcmAvailable := R.Pcm;
  Result.AudioCopyAvailable := R.ACopy;
  Result.AudioAacAvailable := R.Aac;
  Result.AudioAc3Available := R.Ac3;
  Result.MediaChaptersAvailable := R.Chapters;
  Result.MediaSubtitlesAvailable := R.Subtitles;
  Result.MediaMetaDataAvailable := R.Metadata;
  Result.FiltersOnePass := R.OnePass;
  Result.FiltersTwoPass := R.TwoPass;
  C.AddItem(Result);
end;

function AddAudioEncoder(C: TReestrCodec; AID: integer; const AName: string;
  AAvailable, ACurrent, AAllTracks, AFilter: boolean): TReestrEncoder;
begin
  Result := AddEncoder(C, AID, AName, AAvailable, ACurrent, RULES_NONE);
  Result.AudioAllTracksAvailable := AAllTracks;
  Result.AudioFilterAvailable := AFilter;
end;

function AddPreset(E: TReestrEncoder; AID: integer; const AName: string;
  AAvailable, ACurrent: boolean; APlatformID: integer): TReestrPreset;
begin
  Result := TReestrPreset.Create;
  Result.ID := AID;
  Result.Name := AName;
  Result.Available := AAvailable;
  Result.Current := ACurrent;
  Result.FLowPlatformID := APlatformID;
  E.AddItem(Result);
end;

// Adds an encoder with the four standard presets default/low/balanced/high.
// "default" is Current when the presets are available.
procedure AddStandardEncoder(P: TReestrPlatform; C: TReestrCodec; AID: integer;
  const AName: string; AAvailable, ACurrent: boolean; const R: TEncoderRules;
  AFirstPresetID: integer; APresetsAvailable: boolean);
var
  E: TReestrEncoder;
begin
  E := AddEncoder(C, AID, AName, AAvailable, ACurrent, R);
  AddPreset(E, AFirstPresetID,     'default',  APresetsAvailable, APresetsAvailable, P.ID);
  AddPreset(E, AFirstPresetID + 1, 'low',      APresetsAvailable, False, P.ID);
  AddPreset(E, AFirstPresetID + 2, 'balanced', APresetsAvailable, False, P.ID);
  AddPreset(E, AFirstPresetID + 3, 'high',     APresetsAvailable, False, P.ID);
end;

{ High registry: platforms }

// Platform all (ID 1): Software, Mux, Audio
function BuildPlatformAll(out AacAt: TReestrEncoder): TReestrPlatform;
var
  C: TReestrCodec;
  E: TReestrEncoder;
begin
  Result := NewPlatform(1, True, True);

  // Software (available - true, current - true)
  C := AddCodec(Result, CODEC_SOFTWARE, 'Software', True, True);
  E := AddEncoder(C, ENC_PRORES, 'ProRes', True, True, RULES_PRORES);
  AddPreset(E, 1, 'LT',       True,  False, 1);
  AddPreset(E, 2, 'Standard', True,  True,  1);
  AddPreset(E, 3, 'HQ',       True,  False, 1);
  AddPreset(E, 4, '4444',     False, False, 1);
  E := AddEncoder(C, ENC_PRORES_KS, 'ProRes_ks', True, False, RULES_PRORES);
  AddPreset(E, 5, 'LT',       True, False, 1);
  AddPreset(E, 6, 'Standard', True, True,  1);
  AddPreset(E, 7, 'HQ',       True, False, 1);
  AddPreset(E, 8, '4444',     True, False, 1);
  E := AddEncoder(C, ENC_X264, 'x264', False, False, RULES_X264);
  AddPreset(E, 13, 'lossless_10', False, False, 1);

  // Mux (available - true, current - false)
  C := AddCodec(Result, CODEC_MUX, 'Mux', True, False);
  E := AddEncoder(C, ENC_MUX_COPY, 'copy', True, False, RULES_COPY);
  AddPreset(E, 9, 'copy', True, False, 1);
  E := AddEncoder(C, ENC_MUX_MKV, 'mkv', True, True, RULES_MKV);
  AddPreset(E, 10, 'mkv', True, True, 1);
  E := AddEncoder(C, ENC_MUX_MOV, 'mov', True, False, RULES_MOV);
  AddPreset(E, 11, 'mov', True, False, 1);
  E := AddEncoder(C, ENC_MUX_MP4, 'mp4', True, False, RULES_MP4);
  AddPreset(E, 12, 'mp4', True, False, 1);

  // Audio (available - true, current - true)
  // aac_at is enabled only when a darwin platform (ID 2, 3, 4) is requested
  C := AddCodec(Result, CODEC_AUDIO, 'Audio', True, True);
  E := AddAudioEncoder(C, ENC_AUDIO_COPY, 'copy', True, False, True, False);
  AddPreset(E, 14, 'copy', True, False, 1);
  E := AddAudioEncoder(C, ENC_AUDIO_PCM, 'pcm', True, True, False, True);
  AddPreset(E, 15, 'pcm', True, True, 1);
  E := AddAudioEncoder(C, ENC_AUDIO_AAC_FDK, 'aac_fdk', True, False, False, True);
  AddPreset(E, 16, 'aac', True, False, 1);
  E := AddAudioEncoder(C, ENC_AUDIO_AC3, 'ac3', True, False, False, True);
  AddPreset(E, 17, 'ac3', True, False, 1);
  AacAt := AddAudioEncoder(C, ENC_AUDIO_AAC_AT, 'aac_at', False, False, False, True);
  AddPreset(AacAt, 18, 'aac_at', False, False, 1);
end;

// darwin_iMacPro11 (ID 3) and darwin_MacPro71 (ID 4): Videotoolbox
function BuildPlatformDarwin(AID: integer): TReestrPlatform;
var
  C: TReestrCodec;
  E: TReestrEncoder;
begin
  Result := NewPlatform(AID, True, True);
  C := AddCodec(Result, CODEC_VIDEOTOOLBOX, 'Videotoolbox', True, True);
  E := AddEncoder(C, ENC_PRORES, 'ProRes', True, True, RULES_PRORES);
  AddPreset(E, 1, 'LT',       True, False, AID);
  AddPreset(E, 2, 'Standard', True, True,  AID);
  AddPreset(E, 3, 'HQ',       True, False, AID);
  AddPreset(E, 4, '4444',     True, False, AID);
  AddStandardEncoder(Result, C, ENC_H264, 'H264', True, False, RULES_H264, 19, True);
  AddStandardEncoder(Result, C, ENC_HEVC, 'HEVC', True, False, RULES_HEVC, 23, True);
end;

// linux_vaapi (5), linux_nvenc7 (6), linux_qsv_dg2 (7), windows_nvenc7 (9), windows_qsv_dg2 (10).
// On Windows (TenBit = False) HEVC_10 and AV1_10 are not available.
// NVENC 7th generation (Turing/Ampere, Av1 = False) has no AV1 encoder: AV1 and AV1_10 are not available.
function BuildPlatformHw(AID, ACodecID: integer; const ACodecName: string;
  TenBit, Av1: boolean): TReestrPlatform;
var
  C: TReestrCodec;
begin
  Result := NewPlatform(AID, True, False);
  C := AddCodec(Result, ACodecID, ACodecName, True, False);
  AddStandardEncoder(Result, C, ENC_H264,    'H264',    True,            True,  RULES_H264, 19, True);
  AddStandardEncoder(Result, C, ENC_HEVC,    'HEVC',    True,            False, RULES_HEVC, 23, True);
  AddStandardEncoder(Result, C, ENC_HEVC_10, 'HEVC_10', TenBit,          False, RULES_HEVC, 27, TenBit);
  AddStandardEncoder(Result, C, ENC_AV1,     'AV1',     Av1,             False, RULES_AV1,  31, Av1);
  AddStandardEncoder(Result, C, ENC_AV1_10,  'AV1_10',  TenBit and Av1,  False, RULES_AV1,  35, TenBit and Av1);
end;

// linux_vulkan_nvidia (8, ProRes not available) and windows_vulkan_nvidia (11, ProRes available)
function BuildPlatformVulkan(AID, ACodecID: integer; ProResAvailable: boolean): TReestrPlatform;
var
  C: TReestrCodec;
  E: TReestrEncoder;
begin
  Result := NewPlatform(AID, True, True);
  C := AddCodec(Result, ACodecID, 'VULKAN', True, True);
  E := AddEncoder(C, ENC_PRORES, 'ProRes', ProResAvailable, ProResAvailable, RULES_PRORES);
  AddPreset(E, 39, 'default', ProResAvailable, ProResAvailable, AID);
end;

function PlatformNameToID(const AName: string): integer;
var
  I: integer;
begin
  for I := 1 to PLATFORM_COUNT do
    if PLATFORM_NAMES[I] = AName then
      Exit(I);
  Result := 0;
end;

// Builds the platform level of the high registry for the requested platform names
function PrepareHighReestr(PlatformList: TPlatformlist): TReestrPlatformArray;
var
  Requested: array[1..PLATFORM_COUNT] of boolean;
  AacAt: TReestrEncoder;
  I, ID: integer;

  procedure Add(P: TReestrPlatform);
  begin
    SetLength(Result, Length(Result) + 1);
    Result[High(Result)] := P;
  end;

  // aac_at implementation is taken from the first requested darwin platform
  procedure EnableAacAt(APlatformID: integer);
  begin
    if AacAt.Available then
      Exit;
    AacAt.Available := True;
    AacAt.PresetsList[0].Available := True;
    AacAt.PresetsList[0].FLowPlatformID := APlatformID;
  end;

begin
  Result := nil;
  for ID := 1 to PLATFORM_COUNT do
    Requested[ID] := False;
  for I := 0 to PlatformList.Count - 1 do
  begin
    ID := PlatformNameToID(PlatformList.GetItem(I));
    if ID > 0 then
      Requested[ID] := True;
  end;

  // 5.1 platform all is always created
  Add(BuildPlatformAll(AacAt));

  // 5.2 - 5.3 darwin: aac_at becomes available, ID 3 and 4 add Videotoolbox
  for ID := 2 to 4 do
    if Requested[ID] then
      EnableAacAt(ID);
  if Requested[3] then Add(BuildPlatformDarwin(3));
  if Requested[4] then Add(BuildPlatformDarwin(4));

  // 5.4 - 5.10
  //                                     ID  codec group           name               TenBit Av1
  if Requested[5]  then Add(BuildPlatformHw(5,  CODEC_VAAPI,           'VAAPI',           True,  True));
  if Requested[6]  then Add(BuildPlatformHw(6,  CODEC_LINUX_NVENC7,    'LINUX_NVENC7',    True,  False));
  if Requested[7]  then Add(BuildPlatformHw(7,  CODEC_LINUX_QSV_DG2,   'LINUX_QSV_DG2',   True,  True));
  if Requested[8]  then Add(BuildPlatformVulkan(8,  CODEC_LINUX_VULKAN,   False));
  if Requested[9]  then Add(BuildPlatformHw(9,  CODEC_WINDOWS_NVENC7,  'WINDOWS_NVENC7',  False, False));
  if Requested[10] then Add(BuildPlatformHw(10, CODEC_WINDOWS_QSV_DG2, 'WINDOWS_QSV_DG2', False, True));
  if Requested[11] then Add(BuildPlatformVulkan(11, CODEC_WINDOWS_VULKAN, True));

  // 5.11 hq-converter (ID 12): HQPresetLoader is not implemented yet
end;

{ Low registry }

// Low registry from TReestr_description.md ("Экземпляры TFFmpegVideoEncoder",
// "TFFmpegAudioEncoder", "TFFmpegMuxer"). Empty strings are placeholders from the
// specification and must not be filled with guessed values.
function PrepareLowReestr: TLowEntryArray;
var
  Count: integer;

  procedure Add(APresetID, APlatformID, AToolID: integer; AHasData: boolean;
    const ABefore, AAfter, AFilter, AEncoder, AMuxer: string);
  begin
    if Count = Length(Result) then
      SetLength(Result, Count + 64);
    Result[Count].PresetID := APresetID;
    Result[Count].PlatformID := APlatformID;
    Result[Count].Data.Defined := True;
    Result[Count].Data.ToolID := AToolID;
    Result[Count].Data.HasData := AHasData;
    Result[Count].Data.BeforeOptions := ABefore;
    Result[Count].Data.AfterOptions := AAfter;
    Result[Count].Data.FilterOptions := AFilter;
    Result[Count].Data.EncoderOptions := AEncoder;
    Result[Count].Data.MuxerOptions := AMuxer;
    Inc(Count);
  end;

  // TFFmpegVideoEncoder
  procedure V(APresetID, APlatformID: integer; const ABefore, AAfter, AFilter, AEncoder: string);
  begin
    Add(APresetID, APlatformID, TOOL_FFMPEG_VIDEO_ENCODER, True, ABefore, AAfter, AFilter, AEncoder, '');
  end;

  // TFFmpegAudioEncoder
  procedure A(APresetID, APlatformID: integer; const AFilter, AEncoder: string);
  begin
    Add(APresetID, APlatformID, TOOL_FFMPEG_AUDIO_ENCODER, True, '', '', AFilter, AEncoder, '');
  end;

  // TFFmpegMuxer
  procedure M(APresetID, APlatformID: integer; const AMuxer: string);
  begin
    Add(APresetID, APlatformID, TOOL_FFMPEG_MUXER, True, '', '', '', '', AMuxer);
  end;

  // Implementation without extra data (VariantData = null)
  procedure N(APresetID, APlatformID, AToolID: integer);
  begin
    Add(APresetID, APlatformID, AToolID, False, '', '', '', '', '');
  end;

begin
  Result := nil;
  Count := 0;

  // ProRes - LT (Preset ID 1)
  V(1, 1, '', '', '', '-c:v prores -profile:v 1');
  V(1, 3, '', '', '', '-c:v prores_videotoolbox -profile:v 1 -allow_sw 1');
  V(1, 4, '', '', '', '-c:v prores_videotoolbox -profile:v 1 -allow_sw 1');
  // ProRes - Standard (Preset ID 2)
  V(2, 1, '', '', '', '-c:v prores -profile:v 2');
  V(2, 3, '', '', '', '-c:v prores_videotoolbox -profile:v 2 -allow_sw 1');
  V(2, 4, '', '', '', '-c:v prores_videotoolbox -profile:v 2 -allow_sw 1');
  // ProRes - HQ (Preset ID 3)
  V(3, 1, '', '', '', '-c:v prores -profile:v 3');
  V(3, 3, '', '', '', '-c:v prores_videotoolbox -profile:v 3 -allow_sw 1');
  V(3, 4, '', '', '', '-c:v prores_videotoolbox -profile:v 3 -allow_sw 1');
  // ProRes - 4444 (Preset ID 4)
  V(4, 1, '', '', '', '-c:v prores -profile:v 4');
  V(4, 3, '', '', '', '-c:v prores_videotoolbox -profile:v 4 -allow_sw 1');
  V(4, 4, '', '', '', '-c:v prores_videotoolbox -profile:v 4 -allow_sw 1');
  // ProRes_ks - LT (Preset ID 5)
  V(5, 1, '', '', '', '-c:v prores_ks -profile:v lt');
  // ProRes_ks - Standard (Preset ID 6)
  V(6, 1, '', '', '', '-c:v prores_ks -profile:v standard');
  // ProRes_ks - HQ (Preset ID 7)
  V(7, 1, '', '', '', '-c:v prores_ks -profile:v hq');
  // ProRes_ks - 4444 (Preset ID 8)
  V(8, 1, '', '', '', '-c:v prores_ks -profile:v 4444');
  // x264 - lossless_10 (Preset ID 13)
  V(13, 1, '', '', '', '');
  // H264 - default (Preset ID 19)
  V(19, 3, '', '', '', '-c:v h264_videotoolbox -spatial_aq 1');
  V(19, 4, '', '', '', '-c:v h264_videotoolbox -spatial_aq 1');
  V(19, 5, '-vaapi_device {vaapi_device}', '', '-vf "format=nv12,hwupload"', '-c:v h264_vaapi -rc_mode auto');
  V(19, 6, '', '', '', '-c:v h264_nvenc -preset p7 -qp 22 -spatial-aq 1 -temporal-aq 1');
  V(19, 7, '', '', '', '-c:v h264_qsv -global_quality 22 -preset slower -look_ahead 1 -look_ahead_depth 40 -extbrc 1');
  V(19, 9, '', '', '', '-c:v h264_nvenc -preset p7 -qp 22 -spatial-aq 1 -temporal-aq 1');
  V(19, 10, '', '', '', '-c:v h264_qsv -global_quality 22 -preset slower -look_ahead 1 -look_ahead_depth 40 -extbrc 1');
  // H264 - low (Preset ID 20)
  V(20, 3, '', '', '', '-c:v h264_videotoolbox -b:v {bpp:0.05} -spatial_aq 1');
  V(20, 4, '', '', '', '-c:v h264_videotoolbox -b:v {bpp:0.05} -spatial_aq 1');
  V(20, 5, '-vaapi_device {vaapi_device}', '', '-vf "format=nv12,hwupload"', '-c:v h264_vaapi -rc_mode CQP -qp 28');
  V(20, 6, '', '', '', '-c:v h264_nvenc -preset p1 -qp 22');
  V(20, 7, '', '', '', '-c:v h264_qsv -global_quality 22 -preset fast');
  V(20, 9, '', '', '', '-c:v h264_nvenc -preset p1 -qp 22 -spatial-aq 1 -temporal-aq 1');
  V(20, 10, '', '', '', '-c:v h264_qsv -global_quality 22 -preset veryfast -extbrc 1');
  // H264 - balanced (Preset ID 21)
  V(21, 3, '', '', '', '-c:v h264_videotoolbox -b:v {bpp:0.08} -spatial_aq 1');
  V(21, 4, '', '', '', '-c:v h264_videotoolbox -b:v {bpp:0.08} -spatial_aq 1');
  V(21, 5, '-vaapi_device {vaapi_device}', '', '-vf "format=nv12,hwupload"', '-c:v h264_vaapi -rc_mode CQP -qp 25');
  V(21, 6, '', '', '', '-c:v h264_nvenc -preset p4 -qp 22');
  V(21, 7, '', '', '', '-c:v h264_qsv -global_quality 22 -preset medium');
  V(21, 9, '', '', '', '-c:v h264_nvenc -preset p4 -qp 22 -spatial-aq 1 -temporal-aq 1');
  V(21, 10, '', '', '', '-c:v h264_qsv -global_quality 22 -preset medium -look_ahead 1 -look_ahead_depth 40 -extbrc 1');
  // H264 - high (Preset ID 22)
  V(22, 3, '', '', '', '-c:v h264_videotoolbox -b:v {bpp:0.12} -spatial_aq 1');
  V(22, 4, '', '', '', '-c:v h264_videotoolbox -b:v {bpp:0.12} -spatial_aq 1');
  V(22, 5, '-vaapi_device {vaapi_device}', '', '-vf "format=nv12,hwupload"', '-c:v h264_vaapi -rc_mode CQP -qp 22');
  V(22, 6, '', '', '', '-c:v h264_nvenc -preset p7 -qp 22');
  V(22, 7, '', '', '', '-c:v h264_qsv -global_quality 22 -preset slow');
  V(22, 9, '', '', '', '-c:v h264_nvenc -preset p7 -qp 22 -spatial-aq 1 -temporal-aq 1');
  V(22, 10, '', '', '', '-c:v h264_qsv -global_quality 22 -preset slower -look_ahead 1 -look_ahead_depth 40 -extbrc 1');
  // HEVC - default (Preset ID 23)
  V(23, 3, '', '', '', '-c:v hevc_videotoolbox -tag:v hvc1 -spatial_aq 1');
  V(23, 4, '', '', '', '-c:v hevc_videotoolbox -tag:v hvc1 -spatial_aq 1');
  V(23, 5, '-vaapi_device {vaapi_device}', '', '-vf "format=nv12,hwupload"', '-c:v hevc_vaapi -rc_mode auto');
  V(23, 6, '', '', '', '-c:v hevc_nvenc -preset p7 -cq 25 -lookahead_level auto');
  V(23, 7, '', '', '', '-c:v hevc_qsv -global_quality 25 -preset slower -g 240 -bf 4 -look_ahead 1 -look_ahead_depth 60 -extbrc 1');
  V(23, 9, '', '', '', '-c:v hevc_nvenc -preset medium -cq 25 -lookahead_level auto');
  V(23, 10, '', '', '', '-c:v hevc_qsv -global_quality 25 -preset slow -g 240 -bf 4 -look_ahead 1 -look_ahead_depth 60 -extbrc 1');
  // HEVC - low (Preset ID 24)
  V(24, 3, '', '', '', '-c:v hevc_videotoolbox -b:v {bpp:0.030} -tag:v hvc1 -spatial_aq 1');
  V(24, 4, '', '', '', '-c:v hevc_videotoolbox -b:v {bpp:0.030} -tag:v hvc1 -spatial_aq 1');
  V(24, 5, '-vaapi_device {vaapi_device}', '', '-vf "format=nv12,hwupload"', '-c:v hevc_vaapi -rc_mode CQP -qp 28');
  V(24, 6, '', '', '', '-c:v hevc_nvenc -preset p1 -cq 25');
  V(24, 7, '', '', '', '-c:v hevc_qsv -global_quality 25 -preset fast');
  V(24, 9, '', '', '', '-c:v hevc_nvenc -preset p1 -cq 25 -lookahead_level 0');
  V(24, 10, '', '', '', '-c:v hevc_qsv -global_quality 25 -preset fast -g 240 -bf 4');
  // HEVC - balanced (Preset ID 25)
  V(25, 3, '', '', '', '-c:v hevc_videotoolbox -b:v {bpp:0.050} -tag:v hvc1 -spatial_aq 1');
  V(25, 4, '', '', '', '-c:v hevc_videotoolbox -b:v {bpp:0.050} -tag:v hvc1 -spatial_aq 1');
  V(25, 5, '-vaapi_device {vaapi_device}', '', '-vf "format=nv12,hwupload"', '-c:v hevc_vaapi -rc_mode CQP -qp 25');
  V(25, 6, '', '', '', '-c:v hevc_nvenc -preset p4 -cq 25');
  V(25, 7, '', '', '', '-c:v hevc_qsv -global_quality 25 -preset medium');
  V(25, 9, '', '', '', '-c:v hevc_nvenc -preset p4 -cq 25 -lookahead_level auto');
  V(25, 10, '', '', '', '-c:v hevc_qsv -global_quality 25 -preset medium -g 240 -bf 4 -look_ahead 1 -look_ahead_depth 60 -extbrc 1');
  // HEVC - high (Preset ID 26)
  V(26, 3, '', '', '', '-c:v hevc_videotoolbox -b:v {bpp:0.075} -tag:v hvc1 -spatial_aq 1');
  V(26, 4, '', '', '', '-c:v hevc_videotoolbox -b:v {bpp:0.075} -tag:v hvc1 -spatial_aq 1');
  V(26, 5, '-vaapi_device {vaapi_device}', '', '-vf "format=nv12,hwupload"', '-c:v hevc_vaapi -rc_mode CQP -qp 22');
  V(26, 6, '', '', '', '-c:v hevc_nvenc -preset p7 -cq 25');
  V(26, 7, '', '', '', '-c:v hevc_qsv -global_quality 25 -preset slow');
  V(26, 9, '', '', '', '-c:v hevc_nvenc -preset medium -cq 25 -lookahead_level auto');
  V(26, 10, '', '', '', '-c:v hevc_qsv -global_quality 25 -preset slow -g 240 -bf 4 -look_ahead 1 -look_ahead_depth 60 -extbrc 1');
  // HEVC_10 - default (Preset ID 27)
  V(27, 5, '-vaapi_device {vaapi_device}', '', '-vf "format=p010le,hwupload"', '-c:v hevc_vaapi -rc_mode auto -profile:v main10');
  V(27, 6, '', '', '-vf "format=p010le"', '-c:v hevc_nvenc -preset p7 -cq 25 -lookahead_level auto -profile:v main10');
  V(27, 7, '', '', '-vf "format=p010le"', '-c:v hevc_qsv -global_quality 25 -preset slower -g 240 -bf 4 -look_ahead 1 -look_ahead_depth 60 -extbrc 1 -profile:v main10');
  V(27, 9, '', '', '', '');
  V(27, 10, '', '', '', '');
  // HEVC_10 - low (Preset ID 28)
  V(28, 5, '-vaapi_device {vaapi_device}', '', '-vf "format=p010le,hwupload"', '-c:v hevc_vaapi -rc_mode CQP -qp 28 -profile:v main10');
  V(28, 6, '', '', '-vf "format=p010le"', '-c:v hevc_nvenc -preset p1 -cq 25 -profile:v main10');
  V(28, 7, '', '', '-vf "format=p010le"', '-c:v hevc_qsv -global_quality 25 -preset fast -profile:v main10');
  V(28, 9, '', '', '', '');
  V(28, 10, '', '', '', '');
  // HEVC_10 - balanced (Preset ID 29)
  V(29, 5, '-vaapi_device {vaapi_device}', '', '-vf "format=p010le,hwupload"', '-c:v hevc_vaapi -rc_mode CQP -qp 25 -profile:v main10');
  V(29, 6, '', '', '-vf "format=p010le"', '-c:v hevc_nvenc -preset p4 -cq 25 -profile:v main10');
  V(29, 7, '', '', '-vf "format=p010le"', '-c:v hevc_qsv -global_quality 25 -preset medium -profile:v main10');
  V(29, 9, '', '', '', '');
  V(29, 10, '', '', '', '');
  // HEVC_10 - high (Preset ID 30)
  V(30, 5, '-vaapi_device {vaapi_device}', '', '-vf "format=p010le,hwupload"', '-c:v hevc_vaapi -rc_mode CQP -qp 22 -profile:v main10');
  V(30, 6, '', '', '-vf "format=p010le"', '-c:v hevc_nvenc -preset p7 -cq 25 -profile:v main10');
  V(30, 7, '', '', '-vf "format=p010le"', '-c:v hevc_qsv -global_quality 25 -preset slow -profile:v main10');
  V(30, 9, '', '', '', '');
  V(30, 10, '', '', '', '');
  // AV1 / AV1_10: NVENC 7th generation (platforms 6 and 9) has no AV1 encoder, so there is no implementation.
  // AV1 - default (Preset ID 31)
  V(31, 5, '-vaapi_device {vaapi_device}', '', '-vf "format=nv12,hwupload"', '-c:v av1_vaapi -rc_mode auto');
  V(31, 7, '', '', '', '-c:v av1_qsv -global_quality 28 -preset slow -g 240 -bf 4 -look_ahead 1 -look_ahead_depth 60 -extbrc 1');
  V(31, 10, '', '', '', '-c:v av1_qsv -global_quality 28 -preset slow -g 240 -bf 4 -look_ahead 1 -look_ahead_depth 60 -extbrc 1');
  // AV1 - low (Preset ID 32)
  V(32, 5, '-vaapi_device {vaapi_device}', '', '-vf "format=nv12,hwupload"', '-c:v av1_vaapi -rc_mode CQP -global_quality 28');
  V(32, 7, '', '', '', '-c:v av1_qsv -global_quality 28 -preset veryfast');
  V(32, 10, '', '', '', '-c:v av1_qsv -global_quality 28 -preset veryfast -extbrc 1');
  // AV1 - balanced (Preset ID 33)
  V(33, 5, '-vaapi_device {vaapi_device}', '', '-vf "format=nv12,hwupload"', '-c:v av1_vaapi -rc_mode CQP -global_quality 25');
  V(33, 7, '', '', '', '-c:v av1_qsv -global_quality 28 -preset medium');
  V(33, 10, '', '', '', '-c:v av1_qsv -global_quality 28 -preset medium -look_ahead 1 -look_ahead_depth 60 -extbrc 1');
  // AV1 - high (Preset ID 34)
  V(34, 5, '-vaapi_device {vaapi_device}', '', '-vf "format=nv12,hwupload"', '-c:v av1_vaapi -rc_mode CQP -global_quality 22');
  V(34, 7, '', '', '', '-c:v av1_qsv -global_quality 28 -preset slow');
  V(34, 10, '', '', '', '-c:v av1_qsv -global_quality 28 -preset slow -g 240 -bf 4 -look_ahead 1 -look_ahead_depth 60 -extbrc 1');
  // AV1_10 - default (Preset ID 35)
  V(35, 5, '-vaapi_device {vaapi_device}', '', '-vf "format=p010le,hwupload"', '-c:v av1_vaapi -rc_mode auto');
  V(35, 7, '', '', '-vf "format=p010le"', '-c:v av1_qsv -global_quality 28 -preset slow -g 240 -bf 4 -look_ahead 1 -look_ahead_depth 60 -extbrc 1');
  V(35, 10, '', '', '', '');
  // AV1_10 - low (Preset ID 36)
  V(36, 5, '-vaapi_device {vaapi_device}', '', '-vf "format=p010le,hwupload"', '-c:v av1_vaapi -rc_mode CQP -global_quality 28');
  V(36, 7, '', '', '-vf "format=p010le"', '-c:v av1_qsv -global_quality 28 -preset veryfast');
  V(36, 10, '', '', '', '');
  // AV1_10 - balanced (Preset ID 37)
  V(37, 5, '-vaapi_device {vaapi_device}', '', '-vf "format=p010le,hwupload"', '-c:v av1_vaapi -rc_mode CQP -global_quality 25');
  V(37, 7, '', '', '-vf "format=p010le"', '-c:v av1_qsv -global_quality 28 -preset medium');
  V(37, 10, '', '', '', '');
  // AV1_10 - high (Preset ID 38)
  V(38, 5, '-vaapi_device {vaapi_device}', '', '-vf "format=p010le,hwupload"', '-c:v av1_vaapi -rc_mode CQP -global_quality 22');
  V(38, 7, '', '', '-vf "format=p010le"', '-c:v av1_qsv -global_quality 28 -preset slow');
  V(38, 10, '', '', '', '');
  // ProRes - default (Preset ID 39)
  V(39, 8, '', '', '', '');
  V(39, 11, '-init_hw_device vulkan=vk:{vulkan_device} -filter_hw_device vk', '', '-vf "format=yuv422p10le,hwupload"', '-c:v prores_ks_vulkan -profile:v standard');
  // Audio - copy (Preset ID 14)
  A(14, 1, '', '-c:a copy');
  // Audio - pcm (Preset ID 15)
  A(15, 1, 'aresample=resampler=soxr:precision=28:cheby=1', '-c:a pcm_s16le -ar 48000');
  // Audio - aac (Preset ID 16)
  A(16, 1, 'aresample=resampler=soxr:precision=28:cheby=1', '-c:a libfdk_aac -b:a 320k -ar 48000');
  // Audio - ac3 (Preset ID 17)
  A(17, 1, 'aresample=resampler=soxr:precision=28:cheby=1', '-c:a:1 ac3 -b:a:1 640k -ar:a:1 48000');
  // Audio - aac_at (Preset ID 18)
  A(18, 2, 'aresample=resampler=soxr:precision=28:cheby=1', '-c:a aac_at -b:a 320k -ar 48000');
  A(18, 3, 'aresample=resampler=soxr:precision=28:cheby=1', '-c:a aac_at -b:a 320k -ar 48000');
  A(18, 4, 'aresample=resampler=soxr:precision=28:cheby=1', '-c:a aac_at -b:a 320k -ar 48000');
  // Mux - mov (Preset ID 11)
  M(11, 1, '-c copy -movflags use_metadata_tags -f mov');
  // Mux - copy (Preset ID 9)
  N(9, 1, TOOL_MUX_MUXER);
  // Mux - mkv (Preset ID 10)
  N(10, 1, TOOL_MUX_MUXER);
  // Mux - mp4 (Preset ID 12)
  N(12, 1, TOOL_MUX_MUXER);
  // HQ_converter - all (Preset ID 40)
  N(40, 12, TOOL_HQ_VIDEO_ENCODER);

  SetLength(Result, Count);
end;

function FindLow(const Low: TLowEntryArray; APresetID, APlatformID: integer;
  out Data: TPresetSet): boolean;
var
  I: integer;
begin
  for I := 0 to High(Low) do
    if (Low[I].PresetID = APresetID) and (Low[I].PlatformID = APlatformID) then
    begin
      Data := Low[I].Data;
      Exit(True);
    end;
  Data := Default(TPresetSet);
  Result := False;
end;

{ GetReestr }

function GetReestr(PlatformList: TPlatformlist): TReestr;
var
  Platforms: TReestrPlatformArray;
  Low: TLowEntryArray;
  P: TReestrPlatform;
  C: TReestrCodec;
  E: TReestrEncoder;
  S: TReestrPreset;
begin
  Platforms := PrepareHighReestr(PlatformList);
  Low := PrepareLowReestr;
  Result := TReestr.Create;
  for P in Platforms do
  begin
    for C in P.CodecList do
    begin
      for E in C.EncoderList do
        for S in E.PresetsList do
          // No implementation for Preset ID + Platform ID: the preset is not available (no fallback)
          if not FindLow(Low, S.ID, S.FLowPlatformID, S.PresetSet) then
          begin
            S.Available := False;
            S.Current := False;
          end;
      Result.AddItem(C);
    end;
    // Codec groups now belong to Result; free only the platform level
    P.CodecList := nil;
    P.ItemsCount := 0;
    P.Free;
  end;
end;

end.
