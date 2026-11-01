Unit platform_detection;
// Platform detection module for the converter core
// Provides functions and procedures to detect the current platform
// Created by Viktor Razumovsky with so-avtor (Copilot AI assistance)
// Defines the interface for platform detection functions and procedures
// Rules: converter_core/docs/Developer_Descriptions.md

{$mode objfpc}{$H+}

interface

uses
  converter_core_types;

const
  PLATFORM_DARWIN = 'darwin';
  PLATFORM_LINUX = 'linux';
  PLATFORM_WINDOWS = 'windows';
  PLATFORM_ALL_ID1 = 'all';
  PLATFORM_DARWIN_ID2 = 'darwin_MacPro61';
  PLATFORM_DARWIN_ID3 = 'darwin_iMacPro11';
  PLATFORM_DARWIN_ID4 = 'darwin_MacPro71';
  PLATFORM_LINUX_ID5 = 'linux_vaapi';
  PLATFORM_LINUX_ID6 = 'linux_nvenc7';
  PLATFORM_LINUX_ID7 = 'linux_qsv_dgx1';
  PLATFORM_LINUX_ID8 = 'linux_vulkan_nvidia';
  PLATFORM_WINDOWS_ID9 = 'windows_nvenc7';
  PLATFORM_WINDOWS_ID10 = 'windows_qsv_dgx1';
  PLATFORM_WINDOWS_ID11 = 'windows_vulkan_nvidia';
  PLATFORM_HQ_ID12 = 'hq-converter';

  TOOLS_DIR_ENV = 'CONVERTER_CORE_TOOLS_DIR'; // Overrides the tools folder
  TOOLS_DIR_NAME = 'tools';
  HQ_CONVERTER_DIR_NAME = 'hq_converter';

var
  PlatformList: TPlatformlist; // The list of platforms available
  SEnvironment: string; // The current environment string
  function DetectPlatform: string; // Detects and returns the current platform as a string
  function GetCoreModulePath: string; // Full path of the binary that contains converter core
  function ResolveToolsDir: string; // Tools folder: CONVERTER_CORE_TOOLS_DIR or <core dir>/tools
  function ResolveHqConverterDir(const ToolsDir: string): string; // hq_converter folder next to the tools folder
  procedure PlatformDetection(Value: TPlatformlist); // Runs the complete detection and sets PlatformAllowed
  procedure AllPlatformDetection(Value: TPlatformlist); // Checks tools and ffmpeg components, adds platform 'all'
  procedure DarwinPlatformDetection(Value: TPlatformlist); // Adds the detected supported Mac model platform to the list
  procedure WindowsPlatformList(Value: TPlatformlist); // Initializes the platform list for Windows platforms
  procedure LinuxPlatformList(Value: TPlatformlist); // Initializes the platform list for Linux platforms
  procedure HqConverterDetection(Value: TPlatformlist; const ToolsDir: string); // Adds 'hq-converter' if present

implementation

uses
  SysUtils, Classes, process
  {$IFDEF UNIX}, BaseUnix, dl{$ENDIF}
  {$IFDEF WINDOWS}, Windows{$ENDIF}
  {$IFDEF DARWIN}, ctypes, sysctl{$ENDIF};

const
  {$IFDEF WINDOWS}
  EXE_SUFFIX = '.exe';
  {$ELSE}
  EXE_SUFFIX = '';
  {$ENDIF}

  REQUIRED_ENCODERS: array[0..3] of string = ('prores_ks', 'libx264', 'libfdk_aac', 'ac3');
  REQUIRED_DECODERS: array[0..0] of string = ('libdav1d');
  REQUIRED_BUILD_OPTIONS: array[0..0] of string = ('--enable-libsoxr');

  // Relative to the hq_converter folder; same set as the main project's CMake check.
  HQ_REQUIRED_FILES: array[0..5] of string = (
    'bin/ffmpeg',
    'bin/ffprobe',
    'bin/mkvmerge',
    'modules/encoder_orchestrator_v2/runner/encoder_orchestrator_v2_runner',
    'modules/encoder_orchestrator_v2/config/toolchain_runtime.ini',
    'modules/encoder_orchestrator_v2/config/presets/encoder_presets_v2.json');
  HQ_REQUIRED_DIRS: array[0..0] of string = ('native/libav-sdk/lib');

{$IFDEF UNIX}
var
  // dladdr() anchor; must be a unit-level variable (see GetCoreModulePath).
  ModuleAnchor: Byte;
{$ENDIF}

{$IFDEF DARWIN}
  // Returns the Mac model identifier (e.g. 'MacPro6,1'), or '' if unavailable.
  function GetMacModel: string;
  var
    Len: csize_t;
  begin
    Result := '';
    Len := 0;
    if FPsysctlbyname('hw.model', nil, @Len, nil, 0) <> 0 then
      Exit;
    if Len = 0 then
      Exit;
    SetLength(Result, Len);
    if FPsysctlbyname('hw.model', PChar(Result), @Len, nil, 0) <> 0 then
      Exit('');
    // Len includes the trailing #0.
    SetLength(Result, StrLen(PChar(Result)));
  end;
{$ENDIF}

  // Location of the library itself, not of the host program.
  function GetCoreModulePath: string;
  {$IFDEF UNIX}
  var
    Info: dl_info;
  {$ENDIF}
  {$IFDEF WINDOWS}
  var
    Buffer: array[0..32767] of WideChar;
    Len: DWORD;
  {$ENDIF}
  begin
    Result := '';
    {$IFDEF UNIX}
    // @GetCoreModulePath here would be the address of Result, so use a unit variable.
    FillChar(Info, SizeOf(Info), 0);
    if (dladdr(@ModuleAnchor, @Info) <> 0) and (Info.dli_fname <> nil) then
      Result := ExpandFileName(string(Info.dli_fname));
    {$IFDEF LINUX}
    // glibc may report argv[0] for the main program; /proc/self/exe is exact there.
    if (Result = '') or not FileExists(Result) then
      Result := fpReadLink('/proc/self/exe');
    {$ENDIF}
    {$ENDIF}
    {$IFDEF WINDOWS}
    Len := GetModuleFileNameW(HInstance, @Buffer[0], Length(Buffer));
    if (Len > 0) and (Len < Length(Buffer)) then
      Result := UTF8Encode(WideString(Copy(Buffer, 0, Len)));
    {$ENDIF}
  end;

  function ResolveToolsDir: string;
  var
    ModulePath: string;
  begin
    Result := GetEnvironmentVariable(TOOLS_DIR_ENV);
    if Result <> '' then
      Exit(ExcludeTrailingPathDelimiter(ExpandFileName(Result)));
    ModulePath := GetCoreModulePath;
    if ModulePath = '' then
      Exit('');
    Result := ExtractFilePath(ModulePath) + TOOLS_DIR_NAME;
  end;

  function ResolveHqConverterDir(const ToolsDir: string): string;
  begin
    Result := '';
    if ToolsDir = '' then
      Exit;
    Result := ExtractFilePath(ExcludeTrailingPathDelimiter(ToolsDir)) + HQ_CONVERTER_DIR_NAME;
  end;

  // Runs a tool; True when it started and exited with code 0.
  function RunTool(const Exe: string; const Args: array of string; out Output: string): boolean;
  var
    ExitStatus: integer;
  begin
    Output := '';
    ExitStatus := -1;
    try
      Result := (RunCommandInDir('', Exe, Args, Output, ExitStatus, [poNoConsole]) = 0)
        and (ExitStatus = 0);
    except
      Result := False;
    end;
  end;

  function IsExecutableFile(const Path: string): boolean;
  begin
    Result := FileExists(Path) and not DirectoryExists(Path);
    {$IFDEF UNIX}
    Result := Result and (fpAccess(Path, X_OK) = 0);
    {$ENDIF}
  end;

  // Checks one tool, stores its path; sets Value.ErrorMessage and returns False on failure.
  function CheckTool(Value: TPlatformlist; const ToolsDir, Name, VersionArg: string;
    out Path: string): boolean;
  var
    Output: string;
  begin
    Path := IncludeTrailingPathDelimiter(ToolsDir) + Name + EXE_SUFFIX;
    Result := False;
    if not FileExists(Path) then
      Value.ErrorMessage := Format('Required tool "%s" not found in "%s"', [Name, ToolsDir])
    else if not IsExecutableFile(Path) then
      Value.ErrorMessage := Format('Required tool "%s" is not executable: "%s"', [Name, Path])
    else if not RunTool(Path, [VersionArg], Output) then
      Value.ErrorMessage := Format('Required tool "%s" does not start: "%s %s"',
        [Name, Path, VersionArg])
    else
      Result := True;
  end;

  // True when a line of "ffmpeg -encoders/-decoders" has Name as its second column.
  function CodecListContains(const Listing, Name: string): boolean;
  var
    Lines, Columns: TStringList;
    I: integer;
  begin
    Result := False;
    Lines := TStringList.Create;
    Columns := TStringList.Create;
    try
      Lines.Text := Listing;
      Columns.Delimiter := ' ';
      Columns.StrictDelimiter := False;
      for I := 0 to Lines.Count - 1 do
      begin
        Columns.DelimitedText := Trim(Lines[I]);
        if (Columns.Count >= 2) and (Columns[1] = Name) then
          Exit(True);
      end;
    finally
      Columns.Free;
      Lines.Free;
    end;
  end;

  function CheckFfmpegComponents(Value: TPlatformlist; const FfmpegPath: string): boolean;
  var
    Output: string;
    I: integer;
  begin
    Result := False;

    if not RunTool(FfmpegPath, ['-hide_banner', '-encoders'], Output) then
    begin
      Value.ErrorMessage := Format('Cannot list ffmpeg encoders: "%s"', [FfmpegPath]);
      Exit;
    end;
    for I := Low(REQUIRED_ENCODERS) to High(REQUIRED_ENCODERS) do
      if not CodecListContains(Output, REQUIRED_ENCODERS[I]) then
      begin
        Value.ErrorMessage := Format('Required ffmpeg encoder "%s" is missing in "%s"',
          [REQUIRED_ENCODERS[I], FfmpegPath]);
        Exit;
      end;

    if not RunTool(FfmpegPath, ['-hide_banner', '-decoders'], Output) then
    begin
      Value.ErrorMessage := Format('Cannot list ffmpeg decoders: "%s"', [FfmpegPath]);
      Exit;
    end;
    for I := Low(REQUIRED_DECODERS) to High(REQUIRED_DECODERS) do
      if not CodecListContains(Output, REQUIRED_DECODERS[I]) then
      begin
        Value.ErrorMessage := Format('Required ffmpeg decoder "%s" is missing in "%s"',
          [REQUIRED_DECODERS[I], FfmpegPath]);
        Exit;
      end;

    if not RunTool(FfmpegPath, ['-hide_banner', '-buildconf'], Output) then
    begin
      Value.ErrorMessage := Format('Cannot read ffmpeg build configuration: "%s"', [FfmpegPath]);
      Exit;
    end;
    for I := Low(REQUIRED_BUILD_OPTIONS) to High(REQUIRED_BUILD_OPTIONS) do
      if Pos(REQUIRED_BUILD_OPTIONS[I], Output) = 0 then
      begin
        Value.ErrorMessage := Format('Required ffmpeg build option "%s" is missing in "%s"',
          [REQUIRED_BUILD_OPTIONS[I], FfmpegPath]);
        Exit;
      end;

    Result := True;
  end;

  // Shared part for all platforms. On failure only ErrorMessage is set.
  procedure AllPlatformDetection(Value: TPlatformlist);
  var
    ToolsDir: string;
    Tools: TPlatformToolsList;
  begin
    if Value = nil then
      Exit;
    Value.ErrorMessage := '';
    Tools := Default(TPlatformToolsList);

    ToolsDir := ResolveToolsDir;
    if ToolsDir = '' then
    begin
      Value.ErrorMessage := 'Cannot determine the converter core location to resolve the tools folder';
      Exit;
    end;
    if not DirectoryExists(ToolsDir) then
    begin
      Value.ErrorMessage := Format('Tools folder not found: "%s"', [ToolsDir]);
      Exit;
    end;

    if not CheckTool(Value, ToolsDir, 'ffmpeg', '-version', Tools.FFmpegPath) then Exit;
    if not CheckTool(Value, ToolsDir, 'ffprobe', '-version', Tools.FFprobePath) then Exit;
    if not CheckTool(Value, ToolsDir, 'mkvmerge', '--version', Tools.MkvmergePath) then Exit;
    if not CheckTool(Value, ToolsDir, 'MP4Box', '-version', Tools.mp4boxPath) then Exit;

    if not CheckFfmpegComponents(Value, Tools.FFmpegPath) then Exit;

    Value.Tools := Tools;
    Value.AddItem(PLATFORM_ALL_ID1);
  end;

  procedure DarwinPlatformDetection(Value: TPlatformlist);
  {$IFDEF DARWIN}
  var
    MacModel: string;
  {$ENDIF}
  begin
    if Value = nil then
      Exit;
    {$IFDEF DARWIN}
    MacModel := GetMacModel;
    if MacModel = 'MacPro6,1' then
      Value.AddItem(PLATFORM_DARWIN_ID2)
    else if MacModel = 'iMacPro1,1' then
      Value.AddItem(PLATFORM_DARWIN_ID3)
    else if MacModel = 'MacPro7,1' then
      Value.AddItem(PLATFORM_DARWIN_ID4);
    {$ENDIF}
  end;

  procedure WindowsPlatformList(Value: TPlatformlist);
  begin
    // Initialize the platform list for Windows platforms
  end;

  procedure LinuxPlatformList(Value: TPlatformlist);
  begin
    // Initialize the platform list for Linux platforms
  end;

  // HQ_converter is optional: its absence is not an error. Not available on Windows.
  procedure HqConverterDetection(Value: TPlatformlist; const ToolsDir: string);
  {$IFNDEF WINDOWS}
  var
    HqDir: string;
    I: integer;
  {$ENDIF}
  begin
    if Value = nil then
      Exit;
    {$IFNDEF WINDOWS}
    HqDir := ResolveHqConverterDir(ToolsDir);
    if (HqDir = '') or not DirectoryExists(HqDir) then
      Exit;
    HqDir := IncludeTrailingPathDelimiter(HqDir);
    for I := Low(HQ_REQUIRED_FILES) to High(HQ_REQUIRED_FILES) do
      if not FileExists(HqDir + HQ_REQUIRED_FILES[I]) then
        Exit;
    for I := Low(HQ_REQUIRED_DIRS) to High(HQ_REQUIRED_DIRS) do
      if not DirectoryExists(HqDir + HQ_REQUIRED_DIRS[I]) then
        Exit;
    Value.AddItem(PLATFORM_HQ_ID12);
    {$ENDIF}
  end;

  procedure PlatformDetection(Value: TPlatformlist);
  begin
    if Value = nil then
      Exit;
    Value.PlatformAllowed := False;

    AllPlatformDetection(Value);
    if Value.ErrorMessage <> '' then
      Exit;

    {$IF DEFINED(DARWIN)}
    DarwinPlatformDetection(Value);
    {$ELSEIF DEFINED(LINUX)}
    LinuxPlatformList(Value);
    {$ELSEIF DEFINED(WINDOWS)}
    WindowsPlatformList(Value);
    {$ENDIF}

    HqConverterDetection(Value, ResolveToolsDir);

    Value.PlatformAllowed := True;
  end;

  // How it works:
  // it uses Free Pascal's compile-time OS markers instead of checking at runtime.
  // The core is built separately for each OS, so the OS it's built for is the OS it runs on.
  // A build for any other OS stops with an error instead of quietly returning a wrong value.

  function DetectPlatform: string;
  begin
    // The core is compiled natively per OS, so the target OS is the runtime OS.
    {$IF DEFINED(DARWIN)}
    Result := PLATFORM_DARWIN;
    {$ELSEIF DEFINED(LINUX)}
    Result := PLATFORM_LINUX;
    {$ELSEIF DEFINED(WINDOWS)}
    Result := PLATFORM_WINDOWS;
    {$ELSE}
    {$FATAL Unsupported target platform: converter core supports only darwin, linux and windows}
    {$ENDIF}
  end;
end.
