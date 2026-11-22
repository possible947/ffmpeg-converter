unit converter_core_types;
// Types used in the converter core module
// created by Viktor Razumovsky with so-avtor (Copilot AI assistance)
// Defines platform and tool lists for the converter core module
// Contains type definitions for platform lists and platform tools lists
// Provides classes and records for managing platforms and tools in the converter core module
{$mode objfpc}{$H+}

interface

const
  // Registry platform names. TPlatformDetection adds them to TPlatformlist,
  // GetReestr (unit reestr) maps them to Registry Platform IDs.
  PLATFORM_ALL_ID1 = 'all';
  PLATFORM_DARWIN_ID2 = 'darwin_MacPro61';
  PLATFORM_DARWIN_ID3 = 'darwin_iMacPro11';
  PLATFORM_DARWIN_ID4 = 'darwin_MacPro71';
  PLATFORM_LINUX_ID5 = 'linux_vaapi';
  PLATFORM_LINUX_ID6 = 'linux_nvenc7';
  PLATFORM_LINUX_ID7 = 'linux_qsv_dg2';
  PLATFORM_LINUX_ID8 = 'linux_vulkan_nvidia';
  PLATFORM_WINDOWS_ID9 = 'windows_nvenc7';
  PLATFORM_WINDOWS_ID10 = 'windows_qsv_dg2';
  PLATFORM_WINDOWS_ID11 = 'windows_vulkan_nvidia';
  PLATFORM_HQ_ID12 = 'hq-converter';

  // HQ_converter preset file, relative to the hq_converter release folder.
  // Checked by TPlatformDetection and read by the registry (HQPresetLoader).
  HQ_PRESETS_FILE = 'modules/encoder_orchestrator_v2/config/presets/encoder_presets_v2.json';

type
  TPlatformToolsList = record // Stores paths to platform tools
    FFmpegPath: string; // Path to the FFmpeg executable
    FFprobePath: string; // Path to the FFprobe executable
    MkvmergePath: string; // Path to the mkvmerge executable
    mp4boxPath: string; // Path to the MP4Box executable
    HqConverterPath: string; // hq_converter release folder; empty when 'hq-converter' was not detected
  end;

  TPlatformlist = class // Manages a list of platforms
  private
    FItemsCount: integer;
    ItemsList: array of string;
  public
    PlatformAllowed: boolean; // Indicates if the platform is allowed
    ErrorMessage: string; // Stores an error message if any
    Tools: TPlatformToolsList; // Stores paths to platform tools
    constructor Create;
    function GetItem(Index: integer): string; // Retrieves the platform at the specified index
    procedure AddItem(const Value: string); // Adds a platform to the list
    property Count: integer read FItemsCount; // Number of platforms in the list
  end;

implementation

constructor TPlatformlist.Create;
begin
  FItemsCount := 0;
  PlatformAllowed := False;
  SetLength(ItemsList, 0);
  ErrorMessage := '';
  inherited Create;
end;

function TPlatformlist.GetItem(Index: integer): string;
begin
  Result := '';
  if (Index >= 0) and (Index < FItemsCount) then
    Result := ItemsList[Index];
end;

procedure TPlatformlist.AddItem(const Value: string);
begin
  SetLength(ItemsList, FItemsCount + 1);
  ItemsList[FItemsCount] := Value;
  Inc(FItemsCount);
end;

end.