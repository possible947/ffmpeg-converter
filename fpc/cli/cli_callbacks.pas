unit cli_callbacks;

{$mode objfpc}{$H+}

interface

uses converter_types;

procedure SetupCliCallbacks(out Cb: TConverterCallbacks);

implementation

uses
  SysUtils,
  cli_progress,
  converter_api_c;

procedure OnFileBegin(filename: PAnsiChar; index, total: LongInt); cdecl;
begin
  ProgressEnd;
  WriteLn;
  WriteLn('[', index, '/', total, '] Processing: ', string(filename));
end;

procedure OnFileEnd(filename: PAnsiChar; status: TConverterError); cdecl;
begin
  ProgressEnd;
  if status = ERR_OK then
    WriteLn('Completed: ', string(filename))
  else
    WriteLn('Error on ', string(filename), ': ', string(converter_error_string(status)));
end;

procedure OnStage(stage_name: PAnsiChar); cdecl;
begin
  ProgressEnd;
  WriteLn('Stage: ', string(stage_name));
end;

procedure OnProgressEncode(percent, fps, eta_seconds: Single); cdecl;
begin
  ProgressUpdate(percent, fps, eta_seconds);
end;

procedure OnProgressAnalysis(percent, eta_seconds: Single); cdecl;
begin
  ProgressUpdate(percent, 0, eta_seconds);
end;

procedure OnMessage(text: PAnsiChar); cdecl;
begin
  ProgressEnd;
  WriteLn(string(text));
end;

procedure OnError(text: PAnsiChar; code: TConverterError); cdecl;
begin
  ProgressEnd;
  WriteLn('ERROR: ', string(text), ' (', string(converter_error_string(code)), ')');
end;

procedure OnComplete; cdecl;
begin
  ProgressEnd;
  WriteLn;
  WriteLn('All files processed.');
end;

procedure SetupCliCallbacks(out Cb: TConverterCallbacks);
begin
  FillChar(Cb, SizeOf(Cb), 0);
  Cb.on_file_begin := @OnFileBegin;
  Cb.on_file_end := @OnFileEnd;
  Cb.on_stage := @OnStage;
  Cb.on_progress_encode := @OnProgressEncode;
  Cb.on_progress_analysis := @OnProgressAnalysis;
  Cb.on_message := @OnMessage;
  Cb.on_error := @OnError;
  Cb.on_complete := @OnComplete;
end;

end.
