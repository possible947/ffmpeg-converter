unit form;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, Forms, Controls, Graphics, Dialogs, StdCtrls, ComCtrls,
  converter_types, apple_m4v_creator;

type
  TMainForm = class;

  { TConverterThread }

  TConverterThread = class(TThread)
  private
    FOptions: TConvertOptions;
    FFiles: array of AnsiString;
    FConverter: Pointer;
  protected
    procedure Execute; override;
  public
    constructor Create(const Opts: TConvertOptions; const Files: array of string);
    property ConverterHandle: Pointer read FConverter;
  end;

  { TAppleM4VThread }

  TAppleM4VThread = class(TThread)
  private
    FFiles: array of AnsiString;
    FAppleOpts: TAppleM4VOptions;
    FConvertOpts: TConvertOptions;
    FUseEditFlow: Boolean;
    FSuccess: Boolean;
    FSuccessCount: Integer;
    FFailCount: Integer;
    FErrorText: string;
  protected
    procedure Execute; override;
  public
    constructor Create(const Files: array of string; const AppleOpts: TAppleM4VOptions;
      const ConvertOpts: TConvertOptions; UseEditFlow: Boolean);
    property Success: Boolean read FSuccess;
    property ErrorText: string read FErrorText;
    property SuccessCount: Integer read FSuccessCount;
    property FailCount: Integer read FFailCount;
    property UseEditFlow: Boolean read FUseEditFlow;
  end;

  { TMainForm }

  TMainForm = class(TForm)
    btnAddFiles: TButton;
    btnChooseOutputDir: TButton;
    btnClearList: TButton;
    btnAddTrack: TButton;
    btnRemoveSelected: TButton;
    btnStart: TButton;
    btnStop: TButton;
    btnAppleM4VCreator: TButton;
    chkM4VEditBeforeMux: TCheckBox;
    chkOverwrite: TCheckBox;
    cmbAudioNorm: TComboBox;
    cmbAudioOutput: TComboBox;
    cmbCodec: TComboBox;
    cmbDeblock: TComboBox;
    cmbGenre: TComboBox;
    cmbProfile: TComboBox;
    lblAudioNorm: TLabel;
    lblCodec: TLabel;
    lblDeblock: TLabel;
    lblGenre: TLabel;
    lblOutputDir: TLabel;
    lblOutputDirValue: TLabel;
    lblVideoTrack: TLabel;
    lblVideoTrackValue: TLabel;
    lblProfile: TLabel;
    lblProgressText: TLabel;
    lblStatus: TLabel;
    lstFiles: TListBox;
    lstLog: TListBox;
    pbProgress: TProgressBar;
    procedure FormCreate(Sender: TObject);
  private
    FOutputDir: string;
    FVideoTrackPath: string;
    FWorker: TConverterThread;
    FAppleWorker: TAppleM4VThread;

    procedure SetupControls;
    procedure UpdateDependentWidgets;
    procedure BuildCurrentOptions(out Opts: TConvertOptions);
    procedure CollectOptions(out Opts: TConvertOptions; out Files: array of string; out Count: Integer);

    procedure CodecChanged(Sender: TObject);
    procedure AudioNormChanged(Sender: TObject);
    procedure AddFilesClicked(Sender: TObject);
    procedure AddTrackClicked(Sender: TObject);
    procedure ChooseOutputDirClicked(Sender: TObject);
    procedure RemoveSelectedClicked(Sender: TObject);
    procedure ClearListClicked(Sender: TObject);
    procedure StartClicked(Sender: TObject);
    procedure StopClicked(Sender: TObject);
    procedure AppleM4VCreatorClicked(Sender: TObject);
    procedure WorkerTerminated(Sender: TObject);
    procedure AppleWorkerTerminated(Sender: TObject);
    function PromptAppleM4VOptions(var Opts: TAppleM4VOptions): Boolean;
    procedure SetAppleActionState(Busy: Boolean);
    procedure AsyncLog(Data: PtrInt);
    procedure AsyncStatus(Data: PtrInt);
    procedure AsyncStage(Data: PtrInt);
    procedure AsyncProgress(Data: PtrInt);
    procedure AsyncFileBegin(Data: PtrInt);
    procedure AsyncFileEnd(Data: PtrInt);
    procedure AsyncComplete(Data: PtrInt);

    procedure UiLog(const S: string);
    procedure UiStatus(const S: string);
    procedure UiStage(const S: string);
    procedure UiProgress(Percent, Fps, Eta: Single);
    procedure UiFileBegin(const FileName: string; Index, Total: Integer);
    procedure UiFileEnd(const FileName: string; Status: TConverterError);
    procedure UiComplete;
    procedure SetRunningState(Running: Boolean);
  public
  end;

var
  MainForm: TMainForm;

implementation

uses
  Math,
  converter_api_c,
  fs_utils,
  path_utils,
  process_utils,
  tool_paths;

type
  PLogData = ^TLogData;
  TLogData = record
    Msg: string;
  end;

  PStatusData = ^TStatusData;
  TStatusData = record
    Text: string;
  end;

  PStageData = ^TStageData;
  TStageData = record
    Stage: string;
  end;

  PProgressData = ^TProgressData;
  TProgressData = record
    Percent: Single;
    Fps: Single;
    Eta: Single;
  end;

  PFileBeginData = ^TFileBeginData;
  TFileBeginData = record
    FileName: string;
    Index: Integer;
    Total: Integer;
  end;

  PFileEndData = ^TFileEndData;
  TFileEndData = record
    FileName: string;
    Status: TConverterError;
  end;

var
  GMainForm: TMainForm = nil;

procedure SetAnsiField(var Dest: array of AnsiChar; const S: string); forward;
procedure QueueLog(const S: string); forward;
procedure CbFileBegin(filename: PAnsiChar; index, total: LongInt); cdecl; forward;
procedure CbFileEnd(filename: PAnsiChar; status: TConverterError); cdecl; forward;
procedure CbStage(stage_name: PAnsiChar); cdecl; forward;
procedure CbProgressEncode(percent, fps, eta_seconds: Single); cdecl; forward;
procedure CbProgressAnalysis(percent, eta_seconds: Single); cdecl; forward;
procedure CbMessage(text: PAnsiChar); cdecl; forward;
procedure CbError(text: PAnsiChar; code: TConverterError); cdecl; forward;
procedure CbComplete; cdecl; forward;

function ResolveOutputDirForInput(const InputFile, MainOutputDir: string): string;
begin
  if MainOutputDir <> '' then
    Result := MainOutputDir
  else
    Result := '';
end;

function BuildAppleOutputName(const SourceFile, TargetDir: string): string;
var
  BaseName: string;
begin
  BaseName := ChangeFileExt(ExtractFileName(SourceFile), '');
  Result := IncludeTrailingPathDelimiter(TargetDir) + BaseName + '.m4v';
end;

procedure SetupConverterCallbacks(var Cb: TConverterCallbacks);
begin
  FillChar(Cb, SizeOf(Cb), 0);
  Cb.on_file_begin := @CbFileBegin;
  Cb.on_file_end := @CbFileEnd;
  Cb.on_stage := @CbStage;
  Cb.on_progress_encode := @CbProgressEncode;
  Cb.on_progress_analysis := @CbProgressAnalysis;
  Cb.on_message := @CbMessage;
  Cb.on_error := @CbError;
  Cb.on_complete := @CbComplete;
end;

{ TAppleM4VThread }

constructor TAppleM4VThread.Create(const Files: array of string;
  const AppleOpts: TAppleM4VOptions; const ConvertOpts: TConvertOptions; UseEditFlow: Boolean);
var
  I: Integer;
begin
  inherited Create(True);
  FreeOnTerminate := True;
  SetLength(FFiles, Length(Files));
  for I := 0 to High(Files) do
    FFiles[I] := Files[I];
  FAppleOpts := AppleOpts;
  FConvertOpts := ConvertOpts;
  FUseEditFlow := UseEditFlow;
  FSuccess := False;
  FSuccessCount := 0;
  FFailCount := 0;
  FErrorText := '';
end;

procedure TAppleM4VThread.Execute;
var
  I: Integer;
  SourceFile: string;
  OutputDir: string;
  M4VOut: string;
  ConvertedFile: string;
  ErrText: string;
  Err: TConverterError;
  Conv: Pointer;
  Cb: TConverterCallbacks;
  TmpFiles: array of PAnsiChar;
  CodecName: string;
  CmdRes: TRunResult;
  MainOutputDir: string;
  Tools: TToolPaths;
  ResolvedOutDir: string;
  OutDirError: string;

  procedure AddError(const S: string);
  begin
    if FErrorText = '' then
      FErrorText := S
    else
      FErrorText := FErrorText + LineEnding + S;
  end;

  procedure IncFail(const S: string);
  begin
    Inc(FFailCount);
    AddError(S);
    QueueLog('Apple m4v creator ERROR: ' + S);
  end;

begin
  MainOutputDir := Trim(string(PAnsiChar(@FConvertOpts.output_dir[0])));

  if FUseEditFlow then
  begin
    QueueLog('Apple m4v creator: running main worker first...');
    Conv := converter_create;
    if Conv = nil then
    begin
      FErrorText := 'Failed to create converter handle for edit-before-mux flow.';
      Exit;
    end;

    try
      SetupConverterCallbacks(Cb);
      converter_set_callbacks(Conv, @Cb);

      Err := converter_set_options(Conv, @FConvertOpts);
      if Err <> ERR_OK then
      begin
        FErrorText := 'Failed to set main worker options: ' + string(converter_error_string(Err));
        Exit;
      end;

      SetLength(TmpFiles, Length(FFiles));
      for I := 0 to High(FFiles) do
        TmpFiles[I] := PAnsiChar(FFiles[I]);

      if Length(TmpFiles) > 0 then
      begin
        Err := converter_process_files(Conv, @TmpFiles[0], Length(TmpFiles));
        if Err <> ERR_OK then
        begin
          FErrorText := 'Main worker failed in edit-before-mux flow: ' + string(converter_error_string(Err));
          Exit;
        end;
      end;
    finally
      converter_destroy(Conv);
    end;
  end;

  CodecName := string(PAnsiChar(@FConvertOpts.codec[0]));
  for I := 0 to High(FFiles) do
  begin
    Tools := ResolveToolPaths;

    if FUseEditFlow then
    begin
      if MainOutputDir = '' then
      begin
        IncFail('Missing output folder for edit-before-mux mode.');
        Continue;
      end;
      if not EnsureOutputDirWritable(MainOutputDir, ResolvedOutDir, OutDirError) then
      begin
        IncFail('Output preflight failed: ' + OutDirError);
        Continue;
      end;

      SourceFile := MakeOutputName(string(FFiles[I]), CodecName, ResolvedOutDir);
      if not FileExists(SourceFile) then
      begin
        IncFail('Main worker output not found: ' + SourceFile);
        Continue;
      end;
      OutputDir := ResolvedOutDir;
    end
    else
    begin
      SourceFile := string(FFiles[I]);
      if not FileExists(SourceFile) then
      begin
        IncFail('Input file not found: ' + SourceFile);
        Continue;
      end;

      OutputDir := ResolveOutputDirForInput(SourceFile, MainOutputDir);
      if not EnsureOutputDirWritable(OutputDir, ResolvedOutDir, OutDirError) then
      begin
        IncFail('Output preflight failed: ' + OutDirError);
        Continue;
      end;
      OutputDir := ResolvedOutDir;
    end;

    if FUseEditFlow then
      M4VOut := BuildAppleOutputName(string(FFiles[I]), OutputDir)
    else
      M4VOut := BuildAppleOutputName(SourceFile, OutputDir);

    if FileExists(M4VOut) then
    begin
      if FConvertOpts.overwrite <> 0 then
      begin
        CmdRes := RunCommandCapture('/bin/rm -f ' + QuoteForShell(M4VOut));
        if CmdRes.ExitCode <> 0 then
        begin
          IncFail('Cannot overwrite existing file: ' + M4VOut);
          Continue;
        end;
      end
      else
      begin
        IncFail('Output already exists (enable overwrite): ' + M4VOut);
        Continue;
      end;
    end;

    QueueLog(Format('Apple m4v creator [%d/%d]: %s -> %s', [I + 1, Length(FFiles), SourceFile, M4VOut]));
    if not CreateAppleM4V(SourceFile, M4VOut, FAppleOpts, ErrText) then
    begin
      IncFail(ExtractFileName(SourceFile) + ': ' + ErrText);
      Continue;
    end;

    Inc(FSuccessCount);
    QueueLog('Apple m4v creator OK: ' + M4VOut);

    if FUseEditFlow then
    begin
      ConvertedFile := SourceFile;
      CmdRes := RunCommandCapture('/bin/rm -f ' + QuoteForShell(ConvertedFile));
      if CmdRes.ExitCode <> 0 then
        QueueLog('Apple m4v creator warning: failed to delete temp converted file: ' + ConvertedFile);
    end;
  end;

  FSuccess := (FFailCount = 0) and (FSuccessCount > 0);
  if (FSuccessCount = 0) and (FFailCount = 0) then
    FErrorText := 'No files to process.';
end;

procedure TMainForm.AsyncLog(Data: PtrInt);
var
  P: PLogData;
begin
  P := PLogData(Data);
  try
    UiLog(P^.Msg);
  finally
    Dispose(P);
  end;
end;

procedure TMainForm.AsyncStatus(Data: PtrInt);
var
  P: PStatusData;
begin
  P := PStatusData(Data);
  try
    UiStatus(P^.Text);
  finally
    Dispose(P);
  end;
end;

procedure TMainForm.AsyncStage(Data: PtrInt);
var
  P: PStageData;
begin
  P := PStageData(Data);
  try
    UiStage(P^.Stage);
  finally
    Dispose(P);
  end;
end;

procedure TMainForm.AsyncProgress(Data: PtrInt);
var
  P: PProgressData;
begin
  P := PProgressData(Data);
  try
    UiProgress(P^.Percent, P^.Fps, P^.Eta);
  finally
    Dispose(P);
  end;
end;

procedure TMainForm.AsyncFileBegin(Data: PtrInt);
var
  P: PFileBeginData;
begin
  P := PFileBeginData(Data);
  try
    UiFileBegin(P^.FileName, P^.Index, P^.Total);
  finally
    Dispose(P);
  end;
end;

procedure TMainForm.AsyncFileEnd(Data: PtrInt);
var
  P: PFileEndData;
begin
  P := PFileEndData(Data);
  try
    UiFileEnd(P^.FileName, P^.Status);
  finally
    Dispose(P);
  end;
end;

procedure TMainForm.AsyncComplete(Data: PtrInt);
begin
  UiComplete;
end;

procedure QueueLog(const S: string);
var
  P: PLogData;
begin
  New(P);
  P^.Msg := S;
  if Assigned(GMainForm) then
    Application.QueueAsyncCall(@GMainForm.AsyncLog, PtrInt(P))
  else
    Dispose(P);
end;

procedure QueueStatus(const S: string);
var
  P: PStatusData;
begin
  New(P);
  P^.Text := S;
  if Assigned(GMainForm) then
    Application.QueueAsyncCall(@GMainForm.AsyncStatus, PtrInt(P))
  else
    Dispose(P);
end;

procedure QueueStage(const S: string);
var
  P: PStageData;
begin
  New(P);
  P^.Stage := S;
  if Assigned(GMainForm) then
    Application.QueueAsyncCall(@GMainForm.AsyncStage, PtrInt(P))
  else
    Dispose(P);
end;

procedure QueueProgress(Percent, Fps, Eta: Single);
var
  P: PProgressData;
begin
  New(P);
  P^.Percent := Percent;
  P^.Fps := Fps;
  P^.Eta := Eta;
  if Assigned(GMainForm) then
    Application.QueueAsyncCall(@GMainForm.AsyncProgress, PtrInt(P))
  else
    Dispose(P);
end;

procedure QueueFileBegin(const FileName: string; Index, Total: Integer);
var
  P: PFileBeginData;
begin
  New(P);
  P^.FileName := FileName;
  P^.Index := Index;
  P^.Total := Total;
  if Assigned(GMainForm) then
    Application.QueueAsyncCall(@GMainForm.AsyncFileBegin, PtrInt(P))
  else
    Dispose(P);
end;

procedure QueueFileEnd(const FileName: string; Status: TConverterError);
var
  P: PFileEndData;
begin
  New(P);
  P^.FileName := FileName;
  P^.Status := Status;
  if Assigned(GMainForm) then
    Application.QueueAsyncCall(@GMainForm.AsyncFileEnd, PtrInt(P))
  else
    Dispose(P);
end;

procedure QueueComplete;
begin
  if Assigned(GMainForm) then
    Application.QueueAsyncCall(@GMainForm.AsyncComplete, 0);
end;

procedure CbFileBegin(filename: PAnsiChar; index, total: LongInt); cdecl;
begin
  QueueFileBegin(string(filename), index, total);
  QueueStatus(Format('[%d/%d] %s', [index, total, string(filename)]));
end;

procedure CbFileEnd(filename: PAnsiChar; status: TConverterError); cdecl;
begin
  QueueFileEnd(string(filename), status);
end;

procedure CbStage(stage_name: PAnsiChar); cdecl;
begin
  QueueStage('Stage: ' + string(stage_name));
  QueueStatus('Stage: ' + string(stage_name));
end;

procedure CbProgressEncode(percent, fps, eta_seconds: Single); cdecl;
begin
  QueueProgress(percent, fps, eta_seconds);
end;

procedure CbProgressAnalysis(percent, eta_seconds: Single); cdecl;
begin
  QueueProgress(percent, 0, eta_seconds);
end;

procedure CbMessage(text: PAnsiChar); cdecl;
begin
  QueueLog(string(text));
  QueueStatus(string(text));
end;

procedure CbError(text: PAnsiChar; code: TConverterError); cdecl;
begin
  QueueLog(Format('ERROR: %s (%s)', [string(text), string(converter_error_string(code))]));
  QueueStatus('ERROR: ' + string(text));
end;

procedure CbComplete; cdecl;
begin
  QueueComplete;
end;

{ TConverterThread }

constructor TConverterThread.Create(const Opts: TConvertOptions; const Files: array of string);
var
  I: Integer;
begin
  inherited Create(True);
  FreeOnTerminate := True;
  FOptions := Opts;
  SetLength(FFiles, Length(Files));
  for I := 0 to High(Files) do
    FFiles[I] := Files[I];
end;

procedure TConverterThread.Execute;
var
  Cb: TConverterCallbacks;
  Err: TConverterError;
  I: Integer;
  TmpFiles: array of PAnsiChar;
begin
  FConverter := converter_create;
  if FConverter = nil then
  begin
    QueueLog('Failed to create converter');
    Exit;
  end;

  SetupConverterCallbacks(Cb);

  converter_set_callbacks(FConverter, @Cb);
  Err := converter_set_options(FConverter, @FOptions);
  if Err <> ERR_OK then
  begin
    QueueLog('Failed to set options: ' + string(converter_error_string(Err)));
    converter_destroy(FConverter);
    FConverter := nil;
    Exit;
  end;

  SetLength(TmpFiles, Length(FFiles));
  for I := 0 to High(FFiles) do
    TmpFiles[I] := PAnsiChar(FFiles[I]);

  if Length(TmpFiles) > 0 then
  begin
    Err := converter_process_files(FConverter, @TmpFiles[0], Length(TmpFiles));
    if Err <> ERR_OK then
      QueueLog('Processing finished with errors.');
  end;

  converter_destroy(FConverter);
  FConverter := nil;
end;

{$R *.lfm}

procedure SetAnsiField(var Dest: array of AnsiChar; const S: string);
var
  N: SizeInt;
begin
  if Length(Dest) = 0 then
    Exit;
  FillChar(Dest[0], Length(Dest), 0);
  N := Length(Dest) - 1;
  StrPLCopy(@Dest[0], S, N);
end;

function FormatEta(Eta: Single): string;
var
  T, H, M, S: Integer;
begin
  if (not IsNan(Eta)) and (Eta > 0) then
  begin
    T := Trunc(Eta);
    H := T div 3600;
    M := (T mod 3600) div 60;
    S := T mod 60;
    Result := Format('ETA %.2d:%.2d:%.2d', [H, M, S]);
  end
  else
    Result := 'ETA --:--:--';
end;

{ TMainForm }

function CodecIsMux(const Codec: string): Boolean;
begin
  Result := Codec = 'mux';
end;

function CodecUsesSoftwareProres(const Codec: string): Boolean;
begin
  Result := (Codec = 'prores') or (Codec = 'prores_ks');
end;

function CodecUsesLinuxVaapi(const Codec: string): Boolean;
begin
  Result := (Codec = 'h264_vaapi') or (Codec = 'hevc_vaapi');
end;

procedure TMainForm.FormCreate(Sender: TObject);
var
  Tools: TToolPaths;
begin
  GMainForm := Self;
  ApplyBundledToolEnvironment;
  FOutputDir := DefaultOutputDir;
  FVideoTrackPath := '';
  FWorker := nil;
  FAppleWorker := nil;
  SetupControls;
  UpdateDependentWidgets;
  Tools := ResolveToolPaths;
  UiLog('Startup ffmpeg=' + Tools.FfmpegBin);
  UiLog('Startup ffprobe=' + Tools.FfprobeBin);
  UiLog('Startup PATH=' + Tools.PathValue);
end;

procedure TMainForm.SetupControls;
begin
  cmbCodec.Style := csDropDownList;
  cmbProfile.Style := csDropDownList;
  cmbDeblock.Style := csDropDownList;
  cmbAudioNorm.Style := csDropDownList;
  cmbAudioOutput.Style := csDropDownList;
  cmbGenre.Style := csDropDownList;

  cmbCodec.Items.Clear;
  cmbCodec.Items.Add('copy');
  cmbCodec.Items.Add('prores');
  cmbCodec.Items.Add('prores_ks');
{$IFDEF Linux}
  cmbCodec.Items.Add('h264_vaapi');
  cmbCodec.Items.Add('hevc_vaapi');
{$ENDIF}
{$IFDEF Darwin}
  cmbCodec.Items.Add('prores_videotoolbox');
  cmbCodec.Items.Add('hevc_videotoolbox');
{$ENDIF}
  cmbCodec.ItemIndex := 0;

  cmbProfile.Items.Clear;
  cmbProfile.Items.Add('lt');
  cmbProfile.Items.Add('standard');
  cmbProfile.Items.Add('hq');
  cmbProfile.Items.Add('4444');
  cmbProfile.ItemIndex := 1;

  cmbDeblock.Items.Clear;
  cmbDeblock.Items.Add('none');
  cmbDeblock.Items.Add('weak');
  cmbDeblock.Items.Add('strong');
  cmbDeblock.ItemIndex := 0;

  cmbAudioNorm.Items.Clear;
  cmbAudioNorm.Items.Add('none');
  cmbAudioNorm.Items.Add('peak_norm');
  cmbAudioNorm.Items.Add('peak_norm_2pass');
  cmbAudioNorm.Items.Add('loudness_norm');
  cmbAudioNorm.Items.Add('loudness_norm_2pass');
  cmbAudioNorm.ItemIndex := 0;

  cmbGenre.Items.Clear;
  cmbGenre.Items.Add('edm');
  cmbGenre.Items.Add('rock');
  cmbGenre.Items.Add('hiphop');
  cmbGenre.Items.Add('classical');
  cmbGenre.Items.Add('podcast');
  cmbGenre.ItemIndex := 0;

  cmbAudioOutput.Items.Clear;
  cmbAudioOutput.Items.Add('pcm');
  cmbAudioOutput.Items.Add('fdk_aac_q5');
  cmbAudioOutput.Items.Add('fdk_aac_q5_ac3_640');
  cmbAudioOutput.ItemIndex := 0;

  if FOutputDir <> '' then
    lblOutputDirValue.Caption := FOutputDir
  else
    lblOutputDirValue.Caption := '(default unavailable)';
  lblProgressText.Caption := '0%';
  lblStatus.Caption := 'Ready';
  chkM4VEditBeforeMux.Checked := False;
  lblVideoTrackValue.Caption := '(not set)';
  pbProgress.Min := 0;
  pbProgress.Max := 100;
  pbProgress.Position := 0;

  btnStop.Enabled := False;

  cmbCodec.OnChange := @CodecChanged;
  cmbAudioNorm.OnChange := @AudioNormChanged;
  btnAddFiles.OnClick := @AddFilesClicked;
  btnChooseOutputDir.OnClick := @ChooseOutputDirClicked;
  btnRemoveSelected.OnClick := @RemoveSelectedClicked;
  btnClearList.OnClick := @ClearListClicked;
  btnAddTrack.OnClick := @AddTrackClicked;
  btnStart.OnClick := @StartClicked;
  btnStop.OnClick := @StopClicked;
  btnAppleM4VCreator.OnClick := @AppleM4VCreatorClicked;
end;

procedure TMainForm.BuildCurrentOptions(out Opts: TConvertOptions);
var
  ResolvedDir: string;
  DirError: string;
begin
  InitDefaultOptions(Opts);

  if cmbCodec.ItemIndex >= 0 then
    SetAnsiField(Opts.codec, cmbCodec.Items[cmbCodec.ItemIndex]);

  case cmbProfile.ItemIndex of
    0: Opts.profile := 1;
    1: Opts.profile := 2;
    2: Opts.profile := 3;
    3: Opts.profile := 4;
  end;

  case cmbDeblock.ItemIndex of
    0: Opts.deblock := 1;
    1: Opts.deblock := 2;
    2: Opts.deblock := 3;
  end;

  if cmbAudioNorm.ItemIndex >= 0 then
    SetAnsiField(Opts.audio_norm, cmbAudioNorm.Items[cmbAudioNorm.ItemIndex]);

  if cmbAudioOutput.ItemIndex >= 0 then
    SetAnsiField(Opts.audio_output_mode, cmbAudioOutput.Items[cmbAudioOutput.ItemIndex]);

  if CodecIsMux(cmbCodec.Text) then
    SetAnsiField(Opts.video_track_path, FVideoTrackPath);

  Opts.genre := cmbGenre.ItemIndex + 1;
  Opts.overwrite := Ord(chkOverwrite.Checked);

  if EnsureOutputDirWritable(FOutputDir, ResolvedDir, DirError) then
  begin
    SetAnsiField(Opts.output_dir, ResolvedDir);
    Opts.output_dir_status := 1;
    FOutputDir := ResolvedDir;
  end
  else
  begin
    SetAnsiField(Opts.output_dir, '');
    Opts.output_dir_status := 0;
  end;
end;

procedure TMainForm.UpdateDependentWidgets;
var
  CodecText: string;
  AudioNormText: string;
begin
  CodecText := cmbCodec.Text;
  AudioNormText := cmbAudioNorm.Text;

  cmbProfile.Enabled := CodecUsesSoftwareProres(CodecText);
  cmbDeblock.Enabled := CodecUsesSoftwareProres(CodecText);
  cmbGenre.Enabled := (AudioNormText = 'loudness_norm_2pass');

  btnAddFiles.Enabled := not CodecIsMux(CodecText);
  btnAddTrack.Enabled := CodecIsMux(CodecText) and (lstFiles.Count = 1);

  if not CodecIsMux(CodecText) then
  begin
    FVideoTrackPath := '';
    lblVideoTrackValue.Caption := '(not set)';
  end;
end;

procedure TMainForm.CollectOptions(out Opts: TConvertOptions; out Files: array of string; out Count: Integer);
var
  I: Integer;
begin
  BuildCurrentOptions(Opts);

  Count := lstFiles.Items.Count;
  for I := 0 to Count - 1 do
    Files[I] := lstFiles.Items[I];
end;

procedure TMainForm.CodecChanged(Sender: TObject);
begin
  UpdateDependentWidgets;
end;

procedure TMainForm.AudioNormChanged(Sender: TObject);
begin
  UpdateDependentWidgets;
end;

procedure TMainForm.AddFilesClicked(Sender: TObject);
var
  Dlg: TOpenDialog;
  I: Integer;
begin
  Dlg := TOpenDialog.Create(Self);
  try
    Dlg.Options := [ofAllowMultiSelect, ofFileMustExist, ofPathMustExist];
    if Dlg.Execute then
      for I := 0 to Dlg.Files.Count - 1 do
        lstFiles.Items.Add(Dlg.Files[I]);
  finally
    Dlg.Free;
  end;
  UpdateDependentWidgets;
end;

procedure TMainForm.AddTrackClicked(Sender: TObject);
var
  Dlg: TOpenDialog;
begin
  Dlg := TOpenDialog.Create(Self);
  try
    Dlg.Options := [ofFileMustExist, ofPathMustExist];
    if Dlg.Execute then
    begin
      FVideoTrackPath := Dlg.FileName;
      lblVideoTrackValue.Caption := FVideoTrackPath;
    end;
  finally
    Dlg.Free;
  end;
end;

procedure TMainForm.ChooseOutputDirClicked(Sender: TObject);
var
  Dir: string;
begin
  Dir := FOutputDir;
  if SelectDirectory('Select Output Folder', '', Dir) then
  begin
    FOutputDir := Dir;
    lblOutputDirValue.Caption := FOutputDir;
  end;
end;

procedure TMainForm.RemoveSelectedClicked(Sender: TObject);
begin
  if lstFiles.ItemIndex >= 0 then
    lstFiles.Items.Delete(lstFiles.ItemIndex);
  UpdateDependentWidgets;
end;

procedure TMainForm.ClearListClicked(Sender: TObject);
begin
  lstFiles.Clear;
  FVideoTrackPath := '';
  lblVideoTrackValue.Caption := '(not set)';
  UpdateDependentWidgets;
end;

procedure TMainForm.StartClicked(Sender: TObject);
var
  Opts: TConvertOptions;
  FileArr: array of string;
  Count: Integer;
  ResolvedDir: string;
  DirError: string;
begin
  if Assigned(FAppleWorker) then
  begin
    MessageDlg('Apple m4v creator is running. Please wait for completion first.', mtWarning, [mbOK], 0);
    Exit;
  end;

  if CodecIsMux(cmbCodec.Text) then
  begin
    if lstFiles.Items.Count <> 1 then
    begin
      MessageDlg('Mux mode requires exactly one source file.', mtWarning, [mbOK], 0);
      Exit;
    end;
    if not FileRegular(FVideoTrackPath) or not FileReadable(FVideoTrackPath) then
    begin
      MessageDlg('Mux mode requires a readable video-track file.', mtWarning, [mbOK], 0);
      Exit;
    end;
  end;

  if lstFiles.Items.Count = 0 then
  begin
    MessageDlg('No files selected.', mtWarning, [mbOK], 0);
    Exit;
  end;

  SetLength(FileArr, lstFiles.Items.Count);
  CollectOptions(Opts, FileArr, Count);

  if not EnsureOutputDirWritable(string(PAnsiChar(@Opts.output_dir[0])), ResolvedDir, DirError) then
  begin
    MessageDlg('Output preflight failed: ' + DirError, mtError, [mbOK], 0);
    Exit;
  end;
  SetAnsiField(Opts.output_dir, ResolvedDir);
  FOutputDir := ResolvedDir;
  lblOutputDirValue.Caption := FOutputDir;

  lstLog.Clear;
  pbProgress.Position := 0;
  lblProgressText.Caption := '0%';
  lblStatus.Caption := 'Starting...';

  FWorker := TConverterThread.Create(Opts, FileArr);
  FWorker.OnTerminate := @WorkerTerminated;
  SetRunningState(True);
  FWorker.Start;
end;

procedure TMainForm.StopClicked(Sender: TObject);
begin
  if Assigned(FWorker) and (FWorker.ConverterHandle <> nil) then
    converter_stop(FWorker.ConverterHandle);

  lblStatus.Caption := 'Stopped';
  SetRunningState(False);
end;

procedure TMainForm.AppleM4VCreatorClicked(Sender: TObject);
var
  Files: array of string;
  I: Integer;
  ConvertOpts: TConvertOptions;
  Opts: TAppleM4VOptions;
  ResolvedDir: string;
  DirError: string;
begin
  if Assigned(FAppleWorker) then
  begin
    MessageDlg('Apple m4v creator is already running.', mtInformation, [mbOK], 0);
    Exit;
  end;

  if Assigned(FWorker) then
  begin
    MessageDlg('Main conversion is running. Please wait for completion first.', mtWarning, [mbOK], 0);
    Exit;
  end;

  if lstFiles.Items.Count = 0 then
  begin
    MessageDlg('No files selected in file list.', mtWarning, [mbOK], 0);
    Exit;
  end;

  BuildCurrentOptions(ConvertOpts);
  if not EnsureOutputDirWritable(string(PAnsiChar(@ConvertOpts.output_dir[0])), ResolvedDir, DirError) then
  begin
    MessageDlg('Output preflight failed: ' + DirError, mtError, [mbOK], 0);
    Exit;
  end;
  SetAnsiField(ConvertOpts.output_dir, ResolvedDir);
  FOutputDir := ResolvedDir;
  lblOutputDirValue.Caption := FOutputDir;

  if chkM4VEditBeforeMux.Checked and (Trim(string(PAnsiChar(@ConvertOpts.output_dir[0]))) = '') then
  begin
    MessageDlg('For "m4v edit" mode, select output folder first. This folder is used for both main and Apple outputs.', mtWarning, [mbOK], 0);
    Exit;
  end;

  Opts := DefaultAppleM4VOptions;
  if not PromptAppleM4VOptions(Opts) then
  begin
    UiLog('Apple m4v creator: cancelled by user.');
    UiStatus('Ready');
    Exit;
  end;

  SetLength(Files, lstFiles.Items.Count);
  for I := 0 to lstFiles.Items.Count - 1 do
    Files[I] := lstFiles.Items[I];

  UiLog(Format('Apple m4v creator: started for %d file(s).', [Length(Files)]));
  if chkM4VEditBeforeMux.Checked then
    UiLog('Apple m4v creator: edit-before-mux mode enabled (main worker -> m4v -> cleanup).')
  else
    UiLog('Apple m4v creator: direct mode enabled (source list -> m4v).');
  UiStatus('Apple m4v creator: processing...');

  SetAppleActionState(True);
  FAppleWorker := TAppleM4VThread.Create(Files, Opts, ConvertOpts, chkM4VEditBeforeMux.Checked);
  FAppleWorker.OnTerminate := @AppleWorkerTerminated;
  FAppleWorker.Start;
end;

procedure TMainForm.SetAppleActionState(Busy: Boolean);
begin
  btnAppleM4VCreator.Enabled := not Busy;
  btnAddFiles.Enabled := not Busy;
  btnRemoveSelected.Enabled := not Busy;
  btnClearList.Enabled := not Busy;
  btnChooseOutputDir.Enabled := not Busy;

  if Busy then
  begin
    btnStart.Enabled := False;
    btnStop.Enabled := False;
  end
  else
    SetRunningState(Assigned(FWorker));
end;

function TMainForm.PromptAppleM4VOptions(var Opts: TAppleM4VOptions): Boolean;
var
  S: string;
begin
  Result := False;

  S := IntToStr(Opts.VideoTrackIndex);
  if not InputQuery('Apple m4v creator', 'Video track index (0-based):', S) then
    Exit;
  if (not TryStrToInt(Trim(S), Opts.VideoTrackIndex)) or (Opts.VideoTrackIndex < 0) then
  begin
    MessageDlg('Invalid video track index.', mtError, [mbOK], 0);
    Exit;
  end;

  S := IntToStr(Opts.AudioTrackIndex);
  if not InputQuery('Apple m4v creator', 'Audio track index (0-based):', S) then
    Exit;
  if (not TryStrToInt(Trim(S), Opts.AudioTrackIndex)) or (Opts.AudioTrackIndex < 0) then
  begin
    MessageDlg('Invalid audio track index.', mtError, [mbOK], 0);
    Exit;
  end;

  S := IntToStr(Opts.AacQuality);
  if not InputQuery('Apple m4v creator', 'AAC quality (q:a integer, default 2):', S) then
    Exit;
  if (not TryStrToInt(Trim(S), Opts.AacQuality)) or (Opts.AacQuality < 1) or (Opts.AacQuality > 9) then
  begin
    MessageDlg('Invalid AAC quality. Use integer 1..9.', mtError, [mbOK], 0);
    Exit;
  end;

  S := IntToStr(Opts.Ac3BitrateKbps);
  if not InputQuery('Apple m4v creator', 'AC3 bitrate kbps (example 640):', S) then
    Exit;
  if (not TryStrToInt(Trim(S), Opts.Ac3BitrateKbps)) or (Opts.Ac3BitrateKbps < 96) then
  begin
    MessageDlg('Invalid AC3 bitrate. Use integer >= 96.', mtError, [mbOK], 0);
    Exit;
  end;

  S := Trim(Opts.AudioLang);
  if not InputQuery('Apple m4v creator', 'Audio language code (e.g. rus, eng):', S) then
    Exit;
  S := LowerCase(Trim(S));
  if S = '' then
  begin
    MessageDlg('Audio language cannot be empty.', mtError, [mbOK], 0);
    Exit;
  end;
  Opts.AudioLang := S;

  case MessageDlg('Import chapters from source?', mtConfirmation, [mbYes, mbNo, mbCancel], 0) of
    mrYes: Opts.AddChapters := True;
    mrNo: Opts.AddChapters := False;
  else
    Exit;
  end;

  Result := True;
end;

procedure TMainForm.WorkerTerminated(Sender: TObject);
begin
  FWorker := nil;
  SetAppleActionState(Assigned(FAppleWorker));
end;

procedure TMainForm.AppleWorkerTerminated(Sender: TObject);
var
  W: TAppleM4VThread;
begin
  W := TAppleM4VThread(Sender);
  if W.Success then
  begin
    UiLog(Format('Apple m4v creator: done (ok=%d, failed=%d).', [W.SuccessCount, W.FailCount]));
    UiStatus('Apple m4v creator: done');
    MessageDlg(Format('Apple m4v creator finished.' + LineEnding + 'Success: %d' + LineEnding + 'Failed: %d', [W.SuccessCount, W.FailCount]), mtInformation, [mbOK], 0);
  end
  else
  begin
    UiLog(Format('Apple m4v creator: finished with errors (ok=%d, failed=%d).', [W.SuccessCount, W.FailCount]));
    UiLog('Apple m4v creator ERROR: ' + W.ErrorText);
    UiStatus('Apple m4v creator: failed');
    MessageDlg('Apple m4v creator failed:' + LineEnding + W.ErrorText, mtError, [mbOK], 0);
  end;

  FAppleWorker := nil;
  SetAppleActionState(False);
end;

procedure TMainForm.UiLog(const S: string);
begin
  lstLog.Items.Add(S);
  lstLog.ItemIndex := lstLog.Items.Count - 1;
end;

procedure TMainForm.UiStatus(const S: string);
begin
  lblStatus.Caption := S;
end;

procedure TMainForm.UiStage(const S: string);
begin
  lblProgressText.Caption := S;
end;

procedure TMainForm.UiProgress(Percent, Fps, Eta: Single);
var
  EtaText: string;
begin
  if Percent < 0 then Percent := 0;
  if Percent > 100 then Percent := 100;
  pbProgress.Position := Round(Percent);
  EtaText := FormatEta(Eta);

  if Fps > 0 then
    lblProgressText.Caption := Format('%.0f fps | %s', [Fps, EtaText])
  else
    lblProgressText.Caption := Format('%d%% | %s', [Round(Percent), EtaText]);
end;

procedure TMainForm.UiFileBegin(const FileName: string; Index, Total: Integer);
begin
  UiLog(Format('[%d/%d] Processing: %s', [Index, Total, FileName]));
end;

procedure TMainForm.UiFileEnd(const FileName: string; Status: TConverterError);
begin
  UiLog(FileName + ': ' + string(converter_error_string(Status)));
end;

procedure TMainForm.UiComplete;
begin
  pbProgress.Position := 100;
  lblProgressText.Caption := '100%';

  if Assigned(FAppleWorker) and (not Assigned(FWorker)) then
  begin
    UiLog('Main worker phase finished. Continuing Apple m4v phase...');
    Exit;
  end;

  UiLog('All files processed.');
  lblStatus.Caption := 'All files processed.';
  SetRunningState(False);
  lstFiles.Clear;
end;

procedure TMainForm.SetRunningState(Running: Boolean);
begin
  btnStart.Enabled := not Running;
  btnStop.Enabled := Running;

  cmbCodec.Enabled := not Running;
  cmbAudioNorm.Enabled := not Running;
  cmbAudioOutput.Enabled := not Running;
  chkOverwrite.Enabled := not Running;
  btnChooseOutputDir.Enabled := not Running;
  btnAddFiles.Enabled := not Running;
  btnAddTrack.Enabled := not Running;
  btnAppleM4VCreator.Enabled := not Running;
  btnRemoveSelected.Enabled := not Running;
  btnClearList.Enabled := not Running;
  lstFiles.Enabled := not Running;

  if Running then
  begin
    cmbProfile.Enabled := False;
    cmbDeblock.Enabled := False;
    cmbGenre.Enabled := False;
  end
  else
    UpdateDependentWidgets;
end;

end.
