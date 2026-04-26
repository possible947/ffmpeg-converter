unit linux_probe;

{$mode objfpc}{$H+}

interface

function ValidateVaapiDevice: Boolean;
function GetVaapiRenderNode: string;
function ProbeVaapiDevices: TStringArray;

implementation

uses SysUtils, Classes;

function ValidateVaapiDevice: Boolean;
begin
{$IFDEF Linux}
  { Checks for the primary render node only; systems with multiple GPUs
    may have additional nodes (renderD129, etc.) that are not checked here. }
  Result := FileExists('/dev/dri/renderD128') or FileExists('/dev/dri/card0');
{$ELSE}
  Result := False;
{$ENDIF}
end;

function GetVaapiRenderNode: string;
begin
{$IFDEF Linux}
  if FileExists('/dev/dri/renderD128') then
    Result := '/dev/dri/renderD128'
  else if FileExists('/dev/dri/card0') then
    Result := '/dev/dri/card0'
  else
    Result := '';
{$ELSE}
  Result := '';
{$ENDIF}
end;

function ProbeVaapiDevices: TStringArray;
var
  I: Integer;
  Devices: TStringArray;
begin
  Result := nil;
  {$IFDEF Linux}
  SetLength(Devices, 0);
  
  { Check for render nodes }
  for I := 128 to 135 do
  begin
    if FileExists(Format('/dev/dri/renderD%d', [I])) then
    begin
      SetLength(Devices, Length(Devices) + 1);
      Devices[High(Devices)] := Format('/dev/dri/renderD%d', [I]);
    end;
  end;
  
  { Check for card nodes }
  for I := 0 to 15 do
  begin
    if FileExists(Format('/dev/dri/card%d', [I])) then
    begin
      SetLength(Devices, Length(Devices) + 1);
      Devices[High(Devices)] := Format('/dev/dri/card%d', [I]);
    end;
  end;
  
  Result := Devices;
  {$ENDIF}
end;

end.
