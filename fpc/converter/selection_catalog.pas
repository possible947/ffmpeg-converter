unit selection_catalog;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, fpjson, jsonparser;

type
  TSelectionCapabilityFunc = function(const GroupName, EncoderName,
    FinalCodec: string; const Requires: TStringArray): Boolean;

  TSelectionCatalog = class
  private
    FRoot: TJSONObject;
    FPlatform: string;
    FLastError: string;
    function GroupObject(const GroupName: string): TJSONObject;
    function ItemsObject(const GroupName: string; GroupData: TJSONObject): TJSONObject;
    function ItemObject(const GroupName, EncoderName: string): TJSONObject;
    function IsEnabled(Data: TJSONData): Boolean;
    function ReadRequires(Item: TJSONObject): TStringArray;
    procedure AddString(var Values: TStringArray; const Value: string);
    function LoadFromFile(const FilePath: string): Boolean;
  public
    constructor Create;
    destructor Destroy; override;
    function Load(const FilePath, PlatformName: string): Boolean;
    function ListGroups(Capability: TSelectionCapabilityFunc = nil): TStringArray;
    function ListEncoders(const GroupName: string;
      Capability: TSelectionCapabilityFunc = nil): TStringArray;
    function ListPresets(const GroupName, EncoderName: string): TStringArray;
    function Resolve(const GroupName, EncoderName: string;
      out FinalCodec: string): Boolean;
    function GetLastError: string;
  end;

implementation

constructor TSelectionCatalog.Create;
begin
  inherited Create;
  FRoot := nil;
  FPlatform := '';
  FLastError := '';
end;

destructor TSelectionCatalog.Destroy;
begin
  FRoot.Free;
  inherited Destroy;
end;

procedure TSelectionCatalog.AddString(var Values: TStringArray; const Value: string);
var
  Count: Integer;
begin
  Count := Length(Values);
  SetLength(Values, Count + 1);
  Values[Count] := Value;
end;

function TSelectionCatalog.IsEnabled(Data: TJSONData): Boolean;
begin
  Result := (Data <> nil) and (Data is TJSONObject) and
            TJSONObject(Data).Get('enabled', False);
end;

function TSelectionCatalog.GroupObject(const GroupName: string): TJSONObject;
var
  SelectionData, CommonData, PlatformsData, PlatformData, HardwareData,
  GroupsData, GroupData: TJSONData;
begin
  Result := nil;
  if FRoot = nil then
    Exit;
  SelectionData := FRoot.Find('selection');
  if not (SelectionData is TJSONObject) then
    Exit;
  CommonData := TJSONObject(SelectionData).Find('common');
  if CommonData is TJSONObject then
  begin
    GroupData := TJSONObject(CommonData).Find(GroupName);
    if GroupData is TJSONObject then
      Exit(TJSONObject(GroupData));
  end;

  PlatformsData := TJSONObject(SelectionData).Find('platforms');
  PlatformData := nil;
  if PlatformsData is TJSONObject then
    PlatformData := TJSONObject(PlatformsData).Find(FPlatform);
  if not (PlatformData is TJSONObject) then
    Exit;
  HardwareData := TJSONObject(PlatformData).Find('hwaccel');
  if not IsEnabled(HardwareData) then
    Exit;
  GroupsData := TJSONObject(HardwareData).Find('groups');
  if GroupsData is TJSONObject then
  begin
    GroupData := TJSONObject(GroupsData).Find(GroupName);
    if GroupData is TJSONObject then
      Result := TJSONObject(GroupData);
  end;
end;

function TSelectionCatalog.ItemsObject(const GroupName: string;
  GroupData: TJSONObject): TJSONObject;
var
  Data: TJSONData;
begin
  Result := nil;
  if GroupData = nil then
    Exit;
  if GroupName = 'mux' then
    Data := GroupData.Find('modes')
  else
    Data := GroupData.Find('encoders');
  if Data is TJSONObject then
    Result := TJSONObject(Data);
end;

function TSelectionCatalog.ItemObject(const GroupName, EncoderName: string): TJSONObject;
var
  GroupData, ItemsData: TJSONObject;
  ItemData: TJSONData;
begin
  Result := nil;
  GroupData := GroupObject(GroupName);
  ItemsData := ItemsObject(GroupName, GroupData);
  if ItemsData = nil then
    Exit;
  ItemData := ItemsData.Find(EncoderName);
  if ItemData is TJSONObject then
    Result := TJSONObject(ItemData);
end;

function TSelectionCatalog.ReadRequires(Item: TJSONObject): TStringArray;
var
  Data: TJSONData;
  ArrayData: TJSONArray;
  I: Integer;
begin
  Result := nil;
  if Item = nil then
    Exit;
  Data := Item.Find('requires');
  if not (Data is TJSONArray) then
    Exit;
  ArrayData := TJSONArray(Data);
  for I := 0 to ArrayData.Count - 1 do
    if ArrayData.Items[I].JSONType = jtString then
      AddString(Result, ArrayData.Strings[I]);
end;

function TSelectionCatalog.LoadFromFile(const FilePath: string): Boolean;
var
  JSON: TJSONData;
  Content: TStringList;
begin
  Result := False;
  FLastError := '';
  FRoot.Free;
  FRoot := nil;
  if not FileExists(FilePath) then
  begin
    FLastError := Format('Selection catalog not found: %s', [FilePath]);
    Exit;
  end;
  Content := TStringList.Create;
  try
    Content.LoadFromFile(FilePath);
    try
      JSON := GetJSON(Content.Text);
    except
      on E: Exception do
      begin
        FLastError := 'Failed to parse selection catalog: ' + E.Message;
        Exit;
      end;
    end;
  finally
    Content.Free;
  end;
  if not (JSON is TJSONObject) then
  begin
    JSON.Free;
    FLastError := 'Selection catalog root must be an object';
    Exit;
  end;
  FRoot := TJSONObject(JSON);
  Result := True;
end;

function TSelectionCatalog.Load(const FilePath, PlatformName: string): Boolean;
begin
  FPlatform := PlatformName;
  Result := LoadFromFile(FilePath);
end;

function TSelectionCatalog.ListEncoders(const GroupName: string;
  Capability: TSelectionCapabilityFunc): TStringArray;
var
  GroupData, ItemsData, ItemData: TJSONObject;
  I: Integer;
  EncoderName, FinalCodec: string;
  Requires: TStringArray;
begin
  Result := nil;
  if GroupName = 'mux' then
  begin
    AddString(Result, 'copy');
    AddString(Result, 'mkv');
    AddString(Result, 'mov');
    AddString(Result, 'm4v');
    Exit;
  end;
  GroupData := GroupObject(GroupName);
  if not IsEnabled(GroupData) then
    Exit;
  ItemsData := ItemsObject(GroupName, GroupData);
  if ItemsData = nil then
    Exit;
  for I := 0 to ItemsData.Count - 1 do
  begin
    EncoderName := ItemsData.Names[I];
    ItemData := TJSONObject(ItemsData.Items[I]);
    if not IsEnabled(ItemData) then
      Continue;
    FinalCodec := ItemData.Get('final_codec',
      ItemData.Get('execution_codec', ''));
    Requires := ReadRequires(ItemData);
    if (not Assigned(Capability)) or
       Capability(GroupName, EncoderName, FinalCodec, Requires) then
      AddString(Result, EncoderName);
  end;
end;

function TSelectionCatalog.ListGroups(Capability: TSelectionCapabilityFunc): TStringArray;
var
  SelectionData, CommonData, PlatformsData, PlatformData, HardwareData,
  GroupsData, GroupData: TJSONObject;
  I: Integer;
  GroupName: string;
  Encoders: TStringArray;
begin
  Result := nil;
  if FRoot = nil then
    Exit;
  SelectionData := TJSONObject(FRoot.Find('selection'));
  if SelectionData = nil then
    Exit;
  CommonData := TJSONObject(SelectionData.Find('common'));
  if CommonData <> nil then
    for I := 0 to CommonData.Count - 1 do
    begin
      GroupName := CommonData.Names[I];
      GroupData := TJSONObject(CommonData.Items[I]);
      if not IsEnabled(GroupData) then
        Continue;
      if GroupName = 'mux' then
      begin
        Encoders := ListEncoders('mux', Capability);
        if Length(Encoders) > 0 then AddString(Result, 'mux');
      end
      else
      begin
        Encoders := ListEncoders(GroupName, Capability);
        if Length(Encoders) > 0 then AddString(Result, GroupName);
      end;
    end;

  PlatformsData := TJSONObject(SelectionData.Find('platforms'));
  PlatformData := nil;
  if PlatformsData <> nil then
    PlatformData := TJSONObject(PlatformsData.Find(FPlatform));
  if PlatformData = nil then
    Exit;
  HardwareData := TJSONObject(PlatformData.Find('hwaccel'));
  if not IsEnabled(HardwareData) then
    Exit;
  GroupsData := TJSONObject(HardwareData.Find('groups'));
  if GroupsData = nil then
    Exit;
  for I := 0 to GroupsData.Count - 1 do
  begin
    GroupName := GroupsData.Names[I];
    if not IsEnabled(GroupsData.Items[I]) then
      Continue;
    Encoders := ListEncoders(GroupName, Capability);
    if Length(Encoders) > 0 then
      AddString(Result, GroupName);
  end;
end;

function TSelectionCatalog.Resolve(const GroupName, EncoderName: string;
  out FinalCodec: string): Boolean;
var
  ItemData: TJSONObject;
  Value: string;
begin
  FinalCodec := '';
  if (GroupName = 'mux') and (EncoderName = 'copy') then
    FinalCodec := 'copy'
  else if (GroupName = 'mux') and (EncoderName = 'm4v') then
    FinalCodec := 'm4v'
  else if (GroupName = 'mux') and
          ((EncoderName = 'mkv') or (EncoderName = 'mov') or
           (EncoderName = 'm4v')) then
    FinalCodec := 'mux'
  else
  begin
    ItemData := ItemObject(GroupName, EncoderName);
    if not IsEnabled(ItemData) then
      Exit(False);
    Value := ItemData.Get('final_codec',
      ItemData.Get('execution_codec', ''));
    if Value = '' then
      Exit(False);
    FinalCodec := Value;
    if Pos('_10bit', EncoderName) > 0 then
      FinalCodec := FinalCodec + '_10bit';
  end;
  Result := FinalCodec <> '';
end;

function TSelectionCatalog.ListPresets(const GroupName, EncoderName: string): TStringArray;
var
  ItemData, ExecutionData, CodecData: TJSONObject;
  PresetsData: TJSONData;
  FinalCodec, ExecutionCodec, PresetName: string;
  I: Integer;
begin
  Result := nil;
  if not Resolve(GroupName, EncoderName, FinalCodec) then
    Exit;
  ItemData := ItemObject(GroupName, EncoderName);
  PresetsData := nil;
  if ItemData <> nil then
    PresetsData := ItemData.Find('presets');
  if PresetsData is TJSONArray then
  begin
    for I := 0 to TJSONArray(PresetsData).Count - 1 do
      if TJSONArray(PresetsData).Items[I].JSONType = jtString then
        AddString(Result, TJSONArray(PresetsData).Strings[I]);
    Exit;
  end;

  if (GroupName = 'mux') and (EncoderName = 'copy') then ExecutionCodec := 'copy'
  else if (GroupName = 'mux') and (EncoderName = 'm4v') then ExecutionCodec := 'm4v'
  else if GroupName = 'mux' then ExecutionCodec := 'mux'
  else ExecutionCodec := FinalCodec;
  if FRoot = nil then Exit;
  ExecutionData := TJSONObject(FRoot.Find(FPlatform));
  if ExecutionData = nil then Exit;
  CodecData := TJSONObject(ExecutionData.Find(ExecutionCodec));
  if CodecData = nil then Exit;
  for I := 0 to CodecData.Count - 1 do
  begin
    PresetName := CodecData.Names[I];
    AddString(Result, PresetName);
  end;
end;

function TSelectionCatalog.GetLastError: string;
begin
  Result := FLastError;
end;

end.
