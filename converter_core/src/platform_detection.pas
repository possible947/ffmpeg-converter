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
  // Registry platform names (PLATFORM_ALL_ID1 ... PLATFORM_HQ_ID12) are in converter_core_types

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
  SysUtils, Classes, process, dynlibs
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
    HQ_PRESETS_FILE);
  HQ_REQUIRED_DIRS: array[0..0] of string = ('native/libav-sdk/lib');

  // GPU architecture detection (Windows/Linux). Only the PCI vendor/device ID is
  // read: no encoder is started. Which encoders and presets a detected platform
  // offers is decided by the registry (unit reestr).
  PCI_VENDOR_NVIDIA = $10DE;
  PCI_VENDOR_INTEL = $8086;

type
  TPciIdRange = record
    First, Last: Word;
  end;

  TPciDevice = record
    VendorID, DeviceID: Word;
  end;
  TPciDeviceArray = array of TPciDevice;

  // Architectures known to the registry; gaOther is any GPU without a registry platform.
  TGpuArchitecture = (gaOther, gaNvidiaNvenc7, gaIntelDg2);

const
  // NVIDIA, 7th generation NVENC platform "nvenc7" (no AV1 encoder).
  // Gaps between the ranges are excluded on purpose: GA100 (0x2080-0x20FF) and
  // GH100 (0x2300-0x23FF) have no NVENC; Pascal and older are below 0x1D80;
  // Ada Lovelace (0x2680+) and newer have an AV1 encoder and are not nvenc7.
  NVENC7_DEVICE_RANGES: array[0..3] of TPciIdRange = (
    (First: $1D80; Last: $1DFF),  // Volta GV100 (Titan V, Quadro GV100, Tesla V100)
    (First: $1E00; Last: $1FFF),  // Turing TU102, TU104, TU106, TU117
    (First: $2180; Last: $21FF),  // Turing TU116
    (First: $2200; Last: $25FF)); // Ampere GA102, GA103, GA104, GA106, GA107 (GH100 is excluded below)
  NVENC7_EXCLUDED_RANGES: array[0..0] of TPciIdRange = (
    (First: $2300; Last: $23FF)); // Hopper GH100: no NVENC

  // Intel Arc Alchemist (DG2) discrete GPUs, including Arc Pro and Data Center Flex (ATS-M).
  // Integrated graphics (including Meteor Lake "Arc" iGPU) and Battlemage are not DG2.
  DG2_DEVICE_RANGES: array[0..0] of TPciIdRange = (
    (First: $5690; Last: $56CF));

  {$IFDEF WINDOWS}
  VULKAN_LOADER_NAME = 'vulkan-1.dll';
  {$ELSE}
  VULKAN_LOADER_NAME = 'libvulkan.so.1';
  {$ENDIF}

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
    Result := SysUtils.GetEnvironmentVariable(TOOLS_DIR_ENV);
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

  function InPciRanges(DeviceID: Word; const Ranges: array of TPciIdRange): boolean;
  var
    I: integer;
  begin
    for I := Low(Ranges) to High(Ranges) do
      if (DeviceID >= Ranges[I].First) and (DeviceID <= Ranges[I].Last) then
        Exit(True);
    Result := False;
  end;

  function ClassifyGpu(const Device: TPciDevice): TGpuArchitecture;
  begin
    Result := gaOther;
    case Device.VendorID of
      PCI_VENDOR_NVIDIA:
        if InPciRanges(Device.DeviceID, NVENC7_DEVICE_RANGES)
          and not InPciRanges(Device.DeviceID, NVENC7_EXCLUDED_RANGES) then
          Result := gaNvidiaNvenc7;
      PCI_VENDOR_INTEL:
        if InPciRanges(Device.DeviceID, DG2_DEVICE_RANGES) then
          Result := gaIntelDg2;
    end;
  end;

  // True when the Vulkan loader library is installed. It is only loaded and
  // unloaded; no Vulkan instance is created.
  function VulkanLoaderPresent: boolean;
  var
    Handle: TLibHandle;
  begin
    Handle := SafeLoadLibrary(VULKAN_LOADER_NAME);
    Result := Handle <> NilHandle;
    if Result then
      UnloadLibrary(Handle);
  end;

  // A GPU with several outputs or several GPUs of one architecture give one platform name.
  procedure AddPlatformOnce(Value: TPlatformlist; const Name: string);
  var
    I: integer;
  begin
    for I := 0 to Value.Count - 1 do
      if Value.GetItem(I) = Name then
        Exit;
    Value.AddItem(Name);
  end;

  // Shared decision for Windows and Linux: maps the found display adapters to
  // registry platform names. Adapters without a registry platform add nothing.
  procedure AddGpuPlatforms(Value: TPlatformlist; const Devices: TPciDeviceArray;
    const Nvenc7Name, Dg2Name, VulkanNvidiaName: string);
  var
    I: integer;
    HasNvenc7, HasDg2, HasNvidia: boolean;
  begin
    HasNvenc7 := False;
    HasDg2 := False;
    HasNvidia := False;
    for I := 0 to High(Devices) do
    begin
      if Devices[I].VendorID = PCI_VENDOR_NVIDIA then
        HasNvidia := True;
      case ClassifyGpu(Devices[I]) of
        gaNvidiaNvenc7: HasNvenc7 := True;
        gaIntelDg2: HasDg2 := True;
        gaOther: ;
      end;
    end;

    // Fixed order: nvenc7, qsv_dg2, vulkan_nvidia (registry platform ID order).
    if HasNvenc7 then
      AddPlatformOnce(Value, Nvenc7Name);
    if HasDg2 then
      AddPlatformOnce(Value, Dg2Name);
    // Vulkan is reported for any NVIDIA GPU when the Vulkan loader is installed.
    if HasNvidia and VulkanLoaderPresent then
      AddPlatformOnce(Value, VulkanNvidiaName);
  end;

  procedure AddPciDevice(var Devices: TPciDeviceArray; VendorID, DeviceID: Word);
  begin
    SetLength(Devices, Length(Devices) + 1);
    Devices[High(Devices)].VendorID := VendorID;
    Devices[High(Devices)].DeviceID := DeviceID;
  end;

{$IFDEF WINDOWS}
type
  // DISPLAY_DEVICEW from wingdi.h; not declared by the FPC 3.2 Windows unit.
  TDisplayDeviceW = record
    cb: DWORD;
    DeviceName: array[0..31] of WideChar;
    DeviceString: array[0..127] of WideChar;
    StateFlags: DWORD;
    DeviceID: array[0..127] of WideChar;
    DeviceKey: array[0..127] of WideChar;
  end;

const
  DISPLAY_DEVICE_MIRRORING_DRIVER = $00000008;

  function EnumDisplayDevicesW(lpDevice: PWideChar; iDevNum: DWORD;
    var lpDisplayDevice: TDisplayDeviceW; dwFlags: DWORD): BOOL; stdcall;
    external 'user32' name 'EnumDisplayDevicesW';

  // Reads a 4-digit hex value that follows Key in a PnP ID such as
  // "PCI\VEN_10DE&DEV_2204&SUBSYS_...". Returns False if it is missing.
  function ParsePnpHex(const PnpID, Key: string; out Value: Word): boolean;
  var
    P, Parsed: integer;
  begin
    Value := 0;
    P := Pos(Key, UpperCase(PnpID));
    if P = 0 then
      Exit(False);
    Result := TryStrToInt('$' + Copy(PnpID, P + Length(Key), 4), Parsed);
    if Result then
      Value := Word(Parsed);
  end;

  // Display adapters known to Windows (one entry per adapter output, duplicates are harmless).
  // Only PCI adapters are returned; mirroring and software drivers are skipped.
  function ListPciDisplayAdapters: TPciDeviceArray;
  var
    Device: TDisplayDeviceW;
    Index: DWORD;
    PnpID: string;
    VendorID, DeviceID: Word;
  begin
    Result := nil;
    Index := 0;
    while True do
    begin
      FillChar(Device, SizeOf(Device), 0);
      Device.cb := SizeOf(Device);
      if not EnumDisplayDevicesW(nil, Index, Device, 0) then
        Break;
      Inc(Index);
      if (Device.StateFlags and DISPLAY_DEVICE_MIRRORING_DRIVER) <> 0 then
        Continue;
      PnpID := UTF8Encode(WideString(PWideChar(@Device.DeviceID[0])));
      if not SameText(Copy(PnpID, 1, 4), 'PCI\') then
        Continue;
      if ParsePnpHex(PnpID, 'VEN_', VendorID) and ParsePnpHex(PnpID, 'DEV_', DeviceID) then
        AddPciDevice(Result, VendorID, DeviceID);
    end;
  end;
{$ENDIF}

{$IFDEF LINUX}
const
  SYSFS_PCI_DEVICES = '/sys/bus/pci/devices/';

  // Reads the first line of a sysfs attribute such as "0x10de"; sysfs reports a
  // fake file size, so text I/O is used instead of a stream.
  function ReadSysfsHex(const FileName: string; out Value: longint): boolean;
  var
    F: TextFile;
    Line: string;
  begin
    Value := 0;
    Result := False;
    if not FileExists(FileName) then
      Exit;
    AssignFile(F, FileName);
    {$I-}
    Reset(F);
    {$I+}
    if IOResult <> 0 then
      Exit;
    try
      ReadLn(F, Line);
    finally
      CloseFile(F);
    end;
    Line := Trim(Line);
    if SameText(Copy(Line, 1, 2), '0x') then
      Delete(Line, 1, 2);
    Result := TryStrToInt('$' + Line, Value);
  end;

  // PCI devices with the display controller base class (0x03): VGA, 3D and other display controllers.
  function ListPciDisplayAdapters: TPciDeviceArray;
  var
    Search: TSearchRec;
    Dir: string;
    ClassCode, VendorID, DeviceID: longint;
  begin
    Result := nil;
    if FindFirst(SYSFS_PCI_DEVICES + '*', faAnyFile, Search) <> 0 then
      Exit;
    try
      repeat
        if (Search.Name = '.') or (Search.Name = '..') then
          Continue;
        Dir := SYSFS_PCI_DEVICES + Search.Name + '/';
        if not ReadSysfsHex(Dir + 'class', ClassCode) then
          Continue;
        if (ClassCode shr 16) <> $03 then
          Continue;
        if ReadSysfsHex(Dir + 'vendor', VendorID) and ReadSysfsHex(Dir + 'device', DeviceID) then
          AddPciDevice(Result, Word(VendorID), Word(DeviceID));
      until FindNext(Search) <> 0;
    finally
      FindClose(Search);
    end;
  end;
{$ENDIF}

  // Adds windows_nvenc7 (Turing/Ampere NVENC 7), windows_qsv_dg2 (Intel Arc DG2)
  // and windows_vulkan_nvidia (NVIDIA GPU + Vulkan loader). Nothing is added for
  // other GPUs; this is not an error and ErrorMessage is not changed.
  procedure WindowsPlatformList(Value: TPlatformlist);
  begin
    if Value = nil then
      Exit;
    {$IFDEF WINDOWS}
    AddGpuPlatforms(Value, ListPciDisplayAdapters,
      PLATFORM_WINDOWS_ID9, PLATFORM_WINDOWS_ID10, PLATFORM_WINDOWS_ID11);
    {$ENDIF}
  end;

  // Same rules as WindowsPlatformList with the linux_* names.
  // linux_vaapi (Radeon without AMF) is not detected yet.
  procedure LinuxPlatformList(Value: TPlatformlist);
  begin
    if Value = nil then
      Exit;
    {$IFDEF LINUX}
    AddGpuPlatforms(Value, ListPciDisplayAdapters,
      PLATFORM_LINUX_ID6, PLATFORM_LINUX_ID7, PLATFORM_LINUX_ID8);
    {$ENDIF}
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
    // The registry reads the HQ preset file from this folder
    Value.Tools.HqConverterPath := HqDir;
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
