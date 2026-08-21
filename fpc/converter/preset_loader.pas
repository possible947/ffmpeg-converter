unit preset_loader;

{$mode objfpc}{$H+}
{$WARN 5057 OFF}

interface

uses
  SysUtils, Contnrs, fpjson, jsonparser;

type
  // Record to hold preset information (mirrors C PresetInfo)
  TPresetInfo = record
    FFmpegArgs: string;
    Container: string;
    PixFmt: string;
    PreInputArgs: string;
    VideoFilter: string;
    Pipeline: string;
    Requires: TStringArray;
  end;
  PPresetInfo = ^TPresetInfo;

  // Forward declaration of opaque type
  TPresetDb = class;

  // Preset entry: stores a single preset
  TPresetEntry = class(TObject)
  public
    Name: string;
    Info: TPresetInfo;
    constructor Create(const AName: string);
  end;

  // Codec entry: stores presets for a codec
  TCodecEntry = class(TObject)
  private
    FPresets: TObjectList; // TPresetEntry
  public
    Name: string;
    constructor Create(const AName: string);
    destructor Destroy; override;
    function GetPreset(const PresetName: string): PPresetInfo;
    function PresetCount: Integer;
    function GetPresetName(Index: Integer): string;
  end;

  // Platform entry: stores codecs for a platform
  TPlatformEntry = class(TObject)
  private
    FCodecs: TObjectList; // TCodecEntry
  public
    Name: string;
    constructor Create(const AName: string);
    destructor Destroy; override;
    function GetCodec(const CodecName: string): TCodecEntry;
    function CodecCount: Integer;
    function GetCodecName(Index: Integer): string;
  end;

  // Main preset database
  TPresetDb = class(TObject)
  private
    FPlatforms: TObjectList; // TPlatformEntry
    FLastError: string;
    procedure SetError(const Fmt: string; Args: array of const);
    function LoadFromFile(const FilePath: string): Boolean;
    function ParsePresetJson(JSONObj: TJSONObject; out Preset: TPresetInfo): Boolean;
  public
    constructor Create;
    destructor Destroy; override;
    procedure Load(const SearchPath: string = '');
    function GetPreset(const OSName, CodecName, PresetName: string): PPresetInfo;
    function ListCodecs(const OSName: string): TStringArray;
    function ListPresets(const OSName, CodecName: string): TStringArray;
    function GetLastError: string;
  end;

procedure SubstitutePlaceholders(const Template: string; out Output: string;
  const VaapiDevice: string = ''; VkDevice: Integer = -1; VtBitrate: Integer = 0);

implementation

uses
  Classes;

const
  { Built-in minimal preset JSON as fallback }
  BUILTIN_PRESETS_JSON =
    '{' +
    '  "version": "1.0",' +
    '  "linux": {' +
    '    "copy": {' +
    '      "default": {' +
    '        "ffmpeg_args": "-c:v copy ",' +
    '        "container": "mkv"' +
    '      }' +
    '    },' +
    '    "prores": {' +
    '      "default": {' +
    '        "ffmpeg_args": "-c:v prores -profile:v 2 ",' +
    '        "container": "mov"' +
    '      }' +
    '    },' +
    '    "prores_ks": {' +
    '      "default": {' +
    '        "ffmpeg_args": "-c:v prores_ks -profile:v standard ",' +
    '        "container": "mov"' +
    '      }' +
    '    }' +
    '  },' +
    '  "macos": {' +
    '    "copy": {' +
    '      "default": {' +
    '        "ffmpeg_args": "-c:v copy ",' +
    '        "container": "mkv"' +
    '      }' +
    '    },' +
    '    "prores": {' +
    '      "default": {' +
    '        "ffmpeg_args": "-c:v prores -profile:v 2 ",' +
    '        "container": "mov"' +
    '      }' +
    '    },' +
    '    "prores_ks": {' +
    '      "default": {' +
    '        "ffmpeg_args": "-c:v prores_ks -profile:v standard ",' +
    '        "container": "mov"' +
    '      }' +
    '    }' +
    '  },' +
    '  "windows": {' +
    '    "copy": {' +
    '      "default": {' +
    '        "ffmpeg_args": "-c:v copy ",' +
    '        "container": "mkv"' +
    '      }' +
    '    },' +
    '    "prores": {' +
    '      "default": {' +
    '        "ffmpeg_args": "-c:v prores -profile:v 2 ",' +
    '        "container": "mov"' +
    '      }' +
    '    },' +
    '    "prores_ks": {' +
    '      "default": {' +
    '        "ffmpeg_args": "-c:v prores_ks -profile:v standard ",' +
    '        "container": "mov"' +
    '      }' +
    '    }' +
    '  }' +
    '}';

// Utility: get executable directory
function GetExecutableDir: string;
begin
  Result := ExtractFileDir(ParamStr(0));
end;

// Utility: get config directory
function GetConfigDir: string;
begin
  {$IFDEF Windows}
    Result := GetEnvironmentVariable('APPDATA');
  {$ELSE}
    {$IFDEF Darwin}
      Result := IncludeTrailingPathDelimiter(GetEnvironmentVariable('HOME')) +
                'Library/Preferences';
    {$ELSE}
      // Linux
      Result := GetEnvironmentVariable('XDG_CONFIG_HOME');
      if Result = '' then
        Result := IncludeTrailingPathDelimiter(GetEnvironmentVariable('HOME')) +
                  '.config';
    {$ENDIF}
  {$ENDIF}
end;

// ============================================================================
//  TPresetEntry Implementation
// ============================================================================

constructor TPresetEntry.Create(const AName: string);
begin
  inherited Create;
  Name := AName;
  FillChar(Info, SizeOf(Info), 0);
end;

// ============================================================================
//  TCodecEntry Implementation
// ============================================================================

constructor TCodecEntry.Create(const AName: string);
begin
  inherited Create;
  Name := AName;
  FPresets := TObjectList.Create(True);
end;

destructor TCodecEntry.Destroy;
begin
  FPresets.Free;
  inherited Destroy;
end;

function TCodecEntry.GetPreset(const PresetName: string): PPresetInfo;
var
  I: Integer;
  Entry: TPresetEntry;
begin
  Result := nil;
  for I := 0 to FPresets.Count - 1 do
  begin
    Entry := TPresetEntry(FPresets[I]);
    if Entry.Name = PresetName then
    begin
      Result := @Entry.Info;
      Exit;
    end;
  end;
end;

function TCodecEntry.PresetCount: Integer;
begin
  Result := FPresets.Count;
end;

function TCodecEntry.GetPresetName(Index: Integer): string;
begin
  if (Index >= 0) and (Index < FPresets.Count) then
    Result := TPresetEntry(FPresets[Index]).Name
  else
    Result := '';
end;

// ============================================================================
//  TPlatformEntry Implementation
// ============================================================================

constructor TPlatformEntry.Create(const AName: string);
begin
  inherited Create;
  Name := AName;
  FCodecs := TObjectList.Create(True);
end;

destructor TPlatformEntry.Destroy;
begin
  FCodecs.Free;
  inherited Destroy;
end;

function TPlatformEntry.GetCodec(const CodecName: string): TCodecEntry;
var
  I: Integer;
begin
  for I := 0 to FCodecs.Count - 1 do
  begin
    Result := TCodecEntry(FCodecs[I]);
    if Result.Name = CodecName then
      Exit;
  end;
  Result := nil;
end;

function TPlatformEntry.CodecCount: Integer;
begin
  Result := FCodecs.Count;
end;

function TPlatformEntry.GetCodecName(Index: Integer): string;
begin
  if (Index >= 0) and (Index < FCodecs.Count) then
    Result := TCodecEntry(FCodecs[Index]).Name
  else
    Result := '';
end;

// ============================================================================
//  TPresetDb Implementation
// ============================================================================

constructor TPresetDb.Create;
begin
  inherited Create;
  FPlatforms := TObjectList.Create(True);
  FLastError := 'No error';
end;

destructor TPresetDb.Destroy;
begin
  FPlatforms.Free;
  inherited Destroy;
end;

procedure TPresetDb.SetError(const Fmt: string; Args: array of const);
begin
  FLastError := Format(Fmt, Args);
end;

function TPresetDb.ParsePresetJson(JSONObj: TJSONObject; out Preset: TPresetInfo): Boolean;
var
  D: TJSONData;
  I, J: Integer;
  ReqArray: TJSONArray;
  ReqList: TStringList;
begin
  FillChar(Preset, SizeOf(Preset), 0);
  Result := False;

  // Required: ffmpeg_args
  D := JSONObj.Find('ffmpeg_args');
  if (D = nil) or (D.JSONType <> jtString) then
  begin
    SetError('Preset missing required field: ffmpeg_args', []);
    Exit;
  end;
  Preset.FFmpegArgs := D.AsString;

  // Required: container
  D := JSONObj.Find('container');
  if (D = nil) or (D.JSONType <> jtString) then
  begin
    SetError('Preset missing required field: container', []);
    Exit;
  end;
  Preset.Container := D.AsString;

  // Optional: pix_fmt
  D := JSONObj.Find('pix_fmt');
  if (D <> nil) and (D.JSONType = jtString) then
    Preset.PixFmt := D.AsString;

  // Optional: pre_input_args
  D := JSONObj.Find('pre_input_args');
  if (D <> nil) and (D.JSONType = jtString) then
    Preset.PreInputArgs := D.AsString;

  // Optional: video_filter
  D := JSONObj.Find('video_filter');
  if (D <> nil) and (D.JSONType = jtString) then
    Preset.VideoFilter := D.AsString;

  // Optional: pipeline
  D := JSONObj.Find('pipeline');
  if (D <> nil) and (D.JSONType = jtString) then
    Preset.Pipeline := D.AsString;

  // Optional: requires array
  D := JSONObj.Find('requires');
  if (D <> nil) and (D.JSONType = jtArray) then
  begin
    ReqArray := TJSONArray(D);
    SetLength(Preset.Requires, ReqArray.Count);
    ReqList := TStringList.Create;
    try
      for I := 0 to ReqArray.Count - 1 do
      begin
        if ReqArray[I].JSONType = jtString then
          ReqList.Add(ReqArray[I].AsString);
      end;
      for J := 0 to ReqList.Count - 1 do
        Preset.Requires[J] := ReqList[J];
    finally
      ReqList.Free;
    end;
  end;

  Result := True;
end;

function TPresetDb.LoadFromFile(const FilePath: string): Boolean;
var
  JSON: TJSONData;
  RootObj: TJSONObject;
  PlatformData: TJSONData;
  PlatformObj: TJSONObject;
  CodecData: TJSONData;
  CodecObj: TJSONObject;
  PresetData: TJSONData;
  PresetObj: TJSONObject;
  PresetEntry: TPresetEntry;
  CodecEntry: TCodecEntry;
  PlatformEntry: TPlatformEntry;
  PlatformIdx, CodecIdx, PresetIdx: Integer;
  PlatformKey, CodecKey, PresetKey: string;
  FileContent: string;
  UseBuiltIn: Boolean;
begin
  Result := False;
  JSON := nil;

  try
    UseBuiltIn := (FilePath = '');
    
    if not UseBuiltIn then
    begin
      if not FileExists(FilePath) then
      begin
        SetError('Presets file not found: %s', [FilePath]);
        Exit;
      end;

      try
        with TStringList.Create do
        try
          LoadFromFile(FilePath);
          FileContent := Text;
        finally
          Free;
        end;

        JSON := GetJSON(FileContent);
      except
        on E: Exception do
        begin
          SetError('Failed to parse JSON: %s', [E.Message]);
          Exit;
        end;
      end;
    end
    else
    begin
      try
        JSON := GetJSON(BUILTIN_PRESETS_JSON);
      except
        on E: Exception do
        begin
          SetError('Failed to parse built-in JSON: %s', [E.Message]);
          Exit;
        end;
      end;
    end;

    if not (JSON is TJSONObject) then
    begin
      SetError('JSON root must be an object', []);
      Exit;
    end;

    RootObj := TJSONObject(JSON);

    // Iterate platforms
    for PlatformIdx := 0 to RootObj.Count - 1 do
    begin
      PlatformKey := RootObj.Names[PlatformIdx];
      PlatformData := RootObj.Items[PlatformIdx];
      
      if not (PlatformData is TJSONObject) then
        Continue;

      PlatformObj := TJSONObject(PlatformData);
      PlatformEntry := TPlatformEntry.Create(PlatformKey);
      FPlatforms.Add(PlatformEntry);

      // Iterate codecs
      for CodecIdx := 0 to PlatformObj.Count - 1 do
      begin
        CodecKey := PlatformObj.Names[CodecIdx];
        CodecData := PlatformObj.Items[CodecIdx];
        
        if not (CodecData is TJSONObject) then
          Continue;

        CodecObj := TJSONObject(CodecData);
        CodecEntry := TCodecEntry.Create(CodecKey);
        PlatformEntry.FCodecs.Add(CodecEntry);

        // Iterate presets
        for PresetIdx := 0 to CodecObj.Count - 1 do
        begin
          PresetKey := CodecObj.Names[PresetIdx];
          PresetData := CodecObj.Items[PresetIdx];
          
          if not (PresetData is TJSONObject) then
            Continue;

          PresetObj := TJSONObject(PresetData);
          PresetEntry := TPresetEntry.Create(PresetKey);
          
          if ParsePresetJson(PresetObj, PresetEntry.Info) then
          begin
            CodecEntry.FPresets.Add(PresetEntry);
          end
          else
          begin
            PresetEntry.Free;
          end;
        end;
      end;
    end;

    Result := True;
  finally
    if JSON <> nil then
      JSON.Free;
  end;
end;

procedure TPresetDb.Load(const SearchPath: string = '');
var
  PresetsPath: string;
  FileFound: Boolean;
  EnvPath: string;
begin
  FPlatforms.Clear;
  FileFound := False;

  // 1. Try explicit search path
  if SearchPath <> '' then
  begin
    PresetsPath := IncludeTrailingPathDelimiter(SearchPath) + 'presets.json';
    if FileExists(PresetsPath) then
    begin
      if LoadFromFile(PresetsPath) then
        FileFound := True
      else
        SetError('Failed to load presets from: %s - %s', [PresetsPath, FLastError]);
    end;
  end;

  // 2. Try PRESETS_PATH environment variable (for AppImage)
  if not FileFound then
  begin
    EnvPath := GetEnvironmentVariable('PRESETS_PATH');
    if EnvPath <> '' then
    begin
      PresetsPath := IncludeTrailingPathDelimiter(EnvPath) + 'presets.json';
      if FileExists(PresetsPath) then
      begin
        if LoadFromFile(PresetsPath) then
          FileFound := True;
      end;
    end;
  end;

  // 3. Try executable-adjacent
  if not FileFound then
  begin
    PresetsPath := IncludeTrailingPathDelimiter(GetExecutableDir) + 'presets.json';
    if FileExists(PresetsPath) then
    begin
      if LoadFromFile(PresetsPath) then
        FileFound := True;
    end;
  end;

  // 4. Try config directory
  if not FileFound then
  begin
    PresetsPath := IncludeTrailingPathDelimiter(GetConfigDir) +
                   'ffmpeg_converter' + PathDelim + 'presets.json';
    if FileExists(PresetsPath) then
    begin
      if LoadFromFile(PresetsPath) then
        FileFound := True;
    end;
  end;

  // 5. Use built-in fallback
  if not FileFound then
  begin
    SetError('presets.json not found, using built-in fallback', []);
    if LoadFromFile('') then
      FileFound := True;
  end;

  if not FileFound then
  begin
    SetError('Failed to load any presets', []);
  end;
end;

function TPresetDb.GetPreset(const OSName, CodecName, PresetName: string): PPresetInfo;
var
  I: Integer;
  PlatformEntry: TPlatformEntry;
  CodecEntry: TCodecEntry;
begin
  Result := nil;

  // Find platform
  for I := 0 to FPlatforms.Count - 1 do
  begin
    PlatformEntry := TPlatformEntry(FPlatforms[I]);
    if PlatformEntry.Name = OSName then
    begin
      // Find codec
      CodecEntry := PlatformEntry.GetCodec(CodecName);
      if CodecEntry <> nil then
      begin
        // Find preset (returns persistent pointer)
        Result := CodecEntry.GetPreset(PresetName);
        if Result <> nil then
          Exit;
      end;
      Break;
    end;
  end;

  SetError('Preset not found: %s/%s/%s', [OSName, CodecName, PresetName]);
end;

function TPresetDb.ListCodecs(const OSName: string): TStringArray;
var
  I, J: Integer;
  PlatformEntry: TPlatformEntry;
begin
  SetLength(Result, 0);

  for I := 0 to FPlatforms.Count - 1 do
  begin
    PlatformEntry := TPlatformEntry(FPlatforms[I]);
    if PlatformEntry.Name = OSName then
    begin
      SetLength(Result, PlatformEntry.CodecCount);
      for J := 0 to PlatformEntry.CodecCount - 1 do
        Result[J] := PlatformEntry.GetCodecName(J);
      Exit;
    end;
  end;
end;

function TPresetDb.ListPresets(const OSName, CodecName: string): TStringArray;
var
  I, J: Integer;
  PlatformEntry: TPlatformEntry;
  CodecEntry: TCodecEntry;
begin
  SetLength(Result, 0);

  for I := 0 to FPlatforms.Count - 1 do
  begin
    PlatformEntry := TPlatformEntry(FPlatforms[I]);
    if PlatformEntry.Name = OSName then
    begin
      CodecEntry := PlatformEntry.GetCodec(CodecName);
      if CodecEntry <> nil then
      begin
        SetLength(Result, CodecEntry.PresetCount);
        for J := 0 to CodecEntry.PresetCount - 1 do
          Result[J] := CodecEntry.GetPresetName(J);
      end;
      Exit;
    end;
  end;
end;

function TPresetDb.GetLastError: string;
begin
  Result := FLastError;
end;

// ============================================================================
//  Global Functions
// ============================================================================

procedure SubstitutePlaceholders(const Template: string; out Output: string;
  const VaapiDevice: string = ''; VkDevice: Integer = -1; VtBitrate: Integer = 0);
var
  I: Integer;
  DeviceStr: string;
begin
  Output := Template;

  // Replace {vaapi_device}
  if Pos('{vaapi_device}', Output) > 0 then
  begin
    if VaapiDevice <> '' then
      DeviceStr := VaapiDevice
    else
      DeviceStr := '/dev/dri/renderD128';
    Output := StringReplace(Output, '{vaapi_device}', DeviceStr, [rfReplaceAll]);
  end;

  // Replace {vk_device}
  if Pos('{vk_device}', Output) > 0 then
  begin
    if VkDevice >= 0 then
      DeviceStr := IntToStr(VkDevice)
    else
      DeviceStr := '0';
    Output := StringReplace(Output, '{vk_device}', DeviceStr, [rfReplaceAll]);
  end;

  // Replace {vt_bitrate}
  if Pos('{vt_bitrate}', Output) > 0 then
  begin
    DeviceStr := IntToStr(VtBitrate);
    Output := StringReplace(Output, '{vt_bitrate}', DeviceStr, [rfReplaceAll]);
  end;
end;

end.
