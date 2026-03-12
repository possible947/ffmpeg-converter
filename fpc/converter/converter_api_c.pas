unit converter_api_c;

{$mode objfpc}{$H+}

interface

uses converter_types;

type
  PConverter = Pointer;

function converter_create: PConverter; cdecl;
procedure converter_destroy(c: PConverter); cdecl;
procedure converter_set_callbacks(c: PConverter; cb: PConverterCallbacks); cdecl;
function converter_set_options(c: PConverter; opts: PConvertOptions): TConverterError; cdecl;
function converter_process_files(c: PConverter; files: PPAnsiChar; file_count: LongInt): TConverterError; cdecl;
procedure converter_stop(c: PConverter); cdecl;
function converter_error_string(err: TConverterError): PAnsiChar; cdecl;

implementation

uses converter_core;

function converter_create: PConverter; cdecl;
begin
  Result := converter_core.converter_create;
end;

procedure converter_destroy(c: PConverter); cdecl;
begin
  converter_core.converter_destroy(c);
end;

procedure converter_set_callbacks(c: PConverter; cb: PConverterCallbacks); cdecl;
begin
  converter_core.converter_set_callbacks(c, cb);
end;

function converter_set_options(c: PConverter; opts: PConvertOptions): TConverterError; cdecl;
begin
  Result := converter_core.converter_set_options(c, opts);
end;

function converter_process_files(c: PConverter; files: PPAnsiChar; file_count: LongInt): TConverterError; cdecl;
begin
  Result := converter_core.converter_process_files(c, files, file_count);
end;

procedure converter_stop(c: PConverter); cdecl;
begin
  converter_core.converter_stop(c);
end;

function converter_error_string(err: TConverterError): PAnsiChar; cdecl;
begin
  Result := converter_core.converter_error_string(err);
end;

end.