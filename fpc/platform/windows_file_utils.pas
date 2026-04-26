unit windows_file_utils;

{$mode objfpc}{$H+}

interface

function AnsiToWide(const S: string): WideString;
function WideToAnsi(const W: WideString): string;
function FileIsRegularReadable(const Path: string): Boolean;
function DirIsWritable(const Path: string): Boolean;
function EnsureOutputDirExists(const Path: string): Boolean;

implementation

{$IFDEF Windows}
uses SysUtils, Windows;

function AnsiToWide(const S: string): WideString;
var
  Len: Integer;
begin
  if S = '' then
  begin
    Result := '';
    Exit;
  end;
  Len := MultiByteToWideChar(CP_UTF8, 0, PAnsiChar(S), Length(S), nil, 0);
  SetLength(Result, Len);
  if Len > 0 then
    MultiByteToWideChar(CP_UTF8, 0, PAnsiChar(S), Length(S), PWideChar(Result), Len);
end;

function WideToAnsi(const W: WideString): string;
var
  Len: Integer;
begin
  if W = '' then
  begin
    Result := '';
    Exit;
  end;
  Len := WideCharToMultiByte(CP_UTF8, 0, PWideChar(W), Length(W), nil, 0, nil, nil);
  SetLength(Result, Len);
  if Len > 0 then
    WideCharToMultiByte(CP_UTF8, 0, PWideChar(W), Length(W), PAnsiChar(Result), Len, nil, nil);
end;

function FileIsRegularReadable(const Path: string): Boolean;
var
  Attrs: DWORD;
  WidePath: WideString;
begin
  if Path = '' then
  begin
    Result := False;
    Exit;
  end;

  WidePath := AnsiToWide(Path);
  Attrs := GetFileAttributesW(PWideChar(WidePath));

  if Attrs = INVALID_FILE_ATTRIBUTES then
    Attrs := GetFileAttributesW(PWideChar(WideString(Path)));

  if Attrs = INVALID_FILE_ATTRIBUTES then
    Result := False
  else
    Result := (Attrs and FILE_ATTRIBUTE_DIRECTORY) = 0;
end;

function DirIsWritable(const Path: string): Boolean;
var
  TestFile: string;
  WidePath: WideString;
  Handle: THandle;
begin
  if Path = '' then
  begin
    Result := False;
    Exit;
  end;

  WidePath := AnsiToWide(Path);

  { Check if directory exists }
  if GetFileAttributesW(PWideChar(WidePath)) = INVALID_FILE_ATTRIBUTES then
  begin
    WidePath := WideString(Path);

    if GetFileAttributesW(PWideChar(WidePath)) <> INVALID_FILE_ATTRIBUTES then
    begin
      Result := True;
      Exit;
    end;

    { Try to create it }
    if not CreateDirectoryW(PWideChar(WidePath), nil) then
    begin
      Result := False;
      Exit;
    end;
  end;

  { Try to write a test file }
  TestFile := Path + '\test_write_' + IntToStr(GetTickCount) + '.tmp';
  WidePath := AnsiToWide(TestFile);
  if WidePath = '' then
    WidePath := WideString(TestFile);
  Handle := CreateFileW(PWideChar(WidePath), GENERIC_WRITE, 0, nil,
                        CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, 0);

  if Handle = INVALID_HANDLE_VALUE then
  begin
    Result := False;
    Exit;
  end;

  CloseHandle(Handle);
  DeleteFileW(PWideChar(WidePath));
  Result := True;
end;

function EnsureOutputDirExists(const Path: string): Boolean;
var
  WidePath: WideString;
begin
  if Path = '' then
  begin
    Result := True;
    Exit;
  end;

  WidePath := AnsiToWide(Path);

  if GetFileAttributesW(PWideChar(WidePath)) = INVALID_FILE_ATTRIBUTES then
  begin
    WidePath := WideString(Path);

    if GetFileAttributesW(PWideChar(WidePath)) <> INVALID_FILE_ATTRIBUTES then
      Result := True
    else
      Result := CreateDirectoryW(PWideChar(WidePath), nil);
  end
  else
    Result := True;
end;

{$ELSE}
uses SysUtils;

function AnsiToWide(const S: string): WideString;
begin
  Result := WideString(S);
end;

function WideToAnsi(const W: WideString): string;
begin
  Result := string(W);
end;

function FileIsRegularReadable(const Path: string): Boolean;
begin
  Result := (Path <> '') and FileExists(Path);
end;

function DirIsWritable(const Path: string): Boolean;
begin
  Result := (Path <> '') and DirectoryExists(Path);
end;

function EnsureOutputDirExists(const Path: string): Boolean;
begin
  if Path = '' then
    Exit(True);
  if not DirectoryExists(Path) then
    Result := ForceDirectories(Path)
  else
    Result := True;
end;
{$ENDIF}

end.
