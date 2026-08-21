unit path_utils;

{$mode objfpc}{$H+}

interface

function QuoteForShell(const S: string): string;
function MakeOutputName(const InputPath, Codec, OutputDir: string; const Preset: string = ''): string;

implementation

uses
  SysUtils;

function QuoteForShell(const S: string): string;
begin
{$IFDEF Windows}
  { cmd.exe standard: embed a literal " as "" inside a double-quoted string }
  Result := '"' + StringReplace(S, '"', '""', [rfReplaceAll]) + '"';
{$ELSE}
  { POSIX single-quote wrapping: no shell metacharacter ($, `, \, !, etc.) is
    interpreted inside single quotes.  The only character that needs escaping
    is a literal single-quote itself, which is handled by ending the quoted
    string, inserting a backslash-escaped single-quote, and reopening:
      '  →  '\''
    Example: "it's a trap" → 'it'\''s a trap' }
  Result := '''' + StringReplace(S, '''', '''' + '\' + '''' + '''', [rfReplaceAll]) + '''';
{$ENDIF}
end;

function MakeOutputName(const InputPath, Codec, OutputDir: string; const Preset: string = ''): string;
var
  BaseName: string;
  Ext: string;
begin
  BaseName := ChangeFileExt(ExtractFileName(InputPath), '');

  if Codec = 'hevc_videotoolbox' then
    Ext := '.mp4'
  else if Codec = 'm4v' then
    Ext := '.m4v'
  else if Codec = 'mux' then
  begin
    { Final container depends on the mux preset (mkv/mov/m4v); default 'mkv'
      matches the pre-Phase-1 behavior for callers that don't pass Preset. }
    if Preset = 'mov' then
      Ext := '.mov'
    else if Preset = 'm4v' then
      Ext := '.m4v'
    else
      Ext := '.mkv';
  end
  else if (Codec = 'prores') or (Codec = 'prores_ks') or
          (Codec = 'prores_videotoolbox') or (Codec = 'prores_ks_vulkan') then
    Ext := '.mov'
  else if (Codec = 'copy') or
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
