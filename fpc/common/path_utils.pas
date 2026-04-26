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

  if Codec = 'hevc_videotoolbox' then
    Ext := '.mp4'
  else if (Codec = 'prores') or (Codec = 'prores_ks') or
          (Codec = 'prores_videotoolbox') or (Codec = 'prores_ks_vulkan') then
    Ext := '.mov'
  else if (Codec = 'copy') or (Codec = 'mux') or
           (Codec = 'h264_vaapi') or (Codec = 'hevc_vaapi') then
    Ext := '.mkv'
  else if (Codec = 'h264_nvenc') or (Codec = 'hevc_nvenc') or
          (Codec = 'h264_amf') or (Codec = 'hevc_amf') or
          (Codec = 'h264_qsv') or (Codec = 'hevc_qsv') then
    Ext := '.mkv'
  else
    Ext := '.mov';

  Result := BaseName + '_converted' + Ext;
  if OutputDir <> '' then
    Result := IncludeTrailingPathDelimiter(OutputDir) + Result;
end;

end.
