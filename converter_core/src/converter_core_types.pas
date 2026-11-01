unit converter_core_types;
// Types used in the converter core module
// created by Viktor Razumovsky with so-avtor (Copilot AI assistance)
// Defines platform and tool lists for the converter core module
// Contains type definitions for platform lists and platform tools lists
// Provides classes and records for managing platforms and tools in the converter core module
{$mode objfpc}{$H+}

interface

type
  TPlatformToolsList = record // Stores paths to platform tools
    FFmpegPath: string; // Path to the FFmpeg executable
    FFprobePath: string; // Path to the FFprobe executable
    MkvmergePath: string; // Path to the mkvmerge executable
    mp4boxPath: string; // Path to the MP4Box executable
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