unit linux_probe;

{$mode objfpc}{$H+}

interface

function ValidateVaapiDevice: Boolean;
function GetVaapiRenderNode: string;

implementation

uses SysUtils;

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

end.
