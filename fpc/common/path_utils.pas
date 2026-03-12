unit path_utils;

{$mode objfpc}{$H+}

interface

function QuoteForShell(const S: string): string;
function MakeOutputName(const InputPath, Codec, OutputDir: string): string;

implementation

uses
  SysUtils;

function QuoteForShell(const S: string): string;
begin
  Result := '"' + StringReplace(S, '"', '\"', [rfReplaceAll]) + '"';
end;

function MakeOutputName(const InputPath, Codec, OutputDir: string): string;
var
  BaseName: string;
  Ext: string;
begin
  BaseName := ChangeFileExt(ExtractFileName(InputPath), '');
  if (Codec = 'copy') or (Codec = 'h265_mi50') then
    Ext := '.mkv'
  else
    Ext := '.mov';

  Result := BaseName + '_converted' + Ext;
  if OutputDir <> '' then
    Result := IncludeTrailingPathDelimiter(OutputDir) + Result;
end;

end.
