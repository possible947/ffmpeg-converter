unit form;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, Forms, Controls, Graphics, Dialogs, StdCtrls, ComCtrls,
  converter_types;

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

  { TMainForm }

  TMainForm = class(TForm)
    btnAddFiles: TButton;
    btnChooseOutputDir: TButton;
    btnClearList: TButton;
    btnRemoveSelected: TButton;
    btnStart: TButton;
    btnStop: TButton;
    btnAppleM4VCreator: TButton;
    chkOverwrite: TCheckBox;
    cmbAudioNorm: TComboBox;
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
    lblProfile: TLabel;
    lblProgressText: TLabel;
    lblStatus: TLabel;
    lstFiles: TListBox;
    lstLog: TListBox;
    pbProgress: TProgressBar;
    procedure FormCreate(Sender: TObject);
  private
    FOutputDir: string;
    FWorker: TConverterThread;

    procedure SetupControls;
    procedure UpdateDependentWidgets;
    procedure CollectOptions(out Opts: TConvertOptions; out Files: array of string; out Count: Integer);

    procedure CodecChanged(Sender: TObject);
    procedure AudioNormChanged(Sender: TObject);
    procedure AddFilesClicked(Sender: TObject);
    procedure ChooseOutputDirClicked(Sender: TObject);
    procedure RemoveSelectedClicked(Sender: TObject);
    procedure ClearListClicked(Sender: TObject);
    procedure StartClicked(Sender: TObject);
    procedure StopClicked(Sender: TObject);
    procedure AppleM4VCreatorClicked(Sender: TObject);
    procedure WorkerTerminated(Sender: TObject);
    function PromptAppleM4VOptions(var Opts: TAppleM4VOptions): Boolean;

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
  apple_m4v_creator;

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

procedure AsyncLog(Data: PtrInt);
var
  P: PLogData;
begin
  P := PLogData(Data);
  try
    if Assigned(GMainForm) then
      GMainForm.UiLog(P^.Msg);
  finally
    Dispose(P);
  end;
end;

procedure AsyncStatus(Data: PtrInt);
var
  P: PStatusData;
begin
  P := PStatusData(Data);
  try
    if Assigned(GMainForm) then
      GMainForm.UiStatus(P^.Text);
  finally
    Dispose(P);
  end;
end;

procedure AsyncStage(Data: PtrInt);
var
  P: PStageData;
begin
  P := PStageData(Data);
  try
    if Assigned(GMainForm) then
      GMainForm.UiStage(P^.Stage);
  finally
    Dispose(P);
  end;
end;

procedure AsyncProgress(Data: PtrInt);
var
  P: PProgressData;
begin
  P := PProgressData(Data);
  try
    if Assigned(GMainForm) then
      GMainForm.UiProgress(P^.Percent, P^.Fps, P^.Eta);
  finally
    Dispose(P);
  end;
end;

procedure AsyncFileBegin(Data: PtrInt);
var
  P: PFileBeginData;
begin
  P := PFileBeginData(Data);
  try
    if Assigned(GMainForm) then
      GMainForm.UiFileBegin(P^.FileName, P^.Index, P^.Total);
  finally
    Dispose(P);
  end;
end;

procedure AsyncFileEnd(Data: PtrInt);
var
  P: PFileEndData;
begin
  P := PFileEndData(Data);
  try
    if Assigned(GMainForm) then
      GMainForm.UiFileEnd(P^.FileName, P^.Status);
  finally
    Dispose(P);
  end;
end;

procedure AsyncComplete(Data: PtrInt);
begin
  if Assigned(GMainForm) then
    GMainForm.UiComplete;
end;

procedure QueueLog(const S: string);
var
  P: PLogData;
begin
  New(P);
  P^.Msg := S;
  Application.QueueAsyncCall(@AsyncLog, PtrInt(P));
end;

procedure QueueStatus(const S: string);
var
  P: PStatusData;
begin
  New(P);
  P^.Text := S;
  Application.QueueAsyncCall(@AsyncStatus, PtrInt(P));
end;

procedure QueueStage(const S: string);
var
  P: PStageData;
begin
  New(P);
  P^.Stage := S;
  Application.QueueAsyncCall(@AsyncStage, PtrInt(P));
end;

procedure QueueProgress(Percent, Fps, Eta: Single);
var
  P: PProgressData;
begin
  New(P);
  P^.Percent := Percent;
  P^.Fps := Fps;
  P^.Eta := Eta;
  Application.QueueAsyncCall(@AsyncProgress, PtrInt(P));
end;

procedure QueueFileBegin(const FileName: string; Index, Total: Integer);
var
  P: PFileBeginData;
begin
  New(P);
  P^.FileName := FileName;
  P^.Index := Index;
  P^.Total := Total;
  Application.QueueAsyncCall(@AsyncFileBegin, PtrInt(P));
end;

procedure QueueFileEnd(const FileName: string; Status: TConverterError);
var
  P: PFileEndData;
begin
  New(P);
  P^.FileName := FileName;
  P^.Status := Status;
  Application.QueueAsyncCall(@AsyncFileEnd, PtrInt(P));
end;

procedure QueueComplete;
begin
  Application.QueueAsyncCall(@AsyncComplete, 0);
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

  FillChar(Cb, SizeOf(Cb), 0);
  Cb.on_file_begin := @CbFileBegin;
  Cb.on_file_end := @CbFileEnd;
  Cb.on_stage := @CbStage;
  Cb.on_progress_encode := @CbProgressEncode;
  Cb.on_progress_analysis := @CbProgressAnalysis;
  Cb.on_message := @CbMessage;
  Cb.on_error := @CbError;
  Cb.on_complete := @CbComplete;

  converter_set_callbacks(FConverter, @Cb);
  converter_set_options(FConverter, @FOptions);

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

procedure TMainForm.FormCreate(Sender: TObject);
begin
  GMainForm := Self;
  FOutputDir := '';
  FWorker := nil;
  SetupControls;
  UpdateDependentWidgets;
end;

procedure TMainForm.SetupControls;
begin
  cmbCodec.Style := csDropDownList;
  cmbProfile.Style := csDropDownList;
  cmbDeblock.Style := csDropDownList;
  cmbAudioNorm.Style := csDropDownList;
  cmbGenre.Style := csDropDownList;

  cmbCodec.Items.Clear;
  cmbCodec.Items.Add('copy');
  cmbCodec.Items.Add('prores');
  cmbCodec.Items.Add('prores_ks');
  cmbCodec.Items.Add('h265_mi50');
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

  lblOutputDirValue.Caption := '(same as input)';
  lblProgressText.Caption := '0%';
  lblStatus.Caption := 'Ready';
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
  btnStart.OnClick := @StartClicked;
  btnStop.OnClick := @StopClicked;
  btnAppleM4VCreator.OnClick := @AppleM4VCreatorClicked;
end;

procedure TMainForm.UpdateDependentWidgets;
var
  CodecText: string;
  AudioNormText: string;
begin
  CodecText := cmbCodec.Text;
  AudioNormText := cmbAudioNorm.Text;

  cmbProfile.Enabled := (CodecText <> 'copy') and (CodecText <> 'h265_mi50');
  cmbDeblock.Enabled := (CodecText <> 'copy') and (CodecText <> 'h265_mi50');
  cmbGenre.Enabled := (AudioNormText = 'loudness_norm_2pass');
end;

procedure TMainForm.CollectOptions(out Opts: TConvertOptions; out Files: array of string; out Count: Integer);
var
  I: Integer;
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

  Opts.genre := cmbGenre.ItemIndex + 1;
  Opts.overwrite := Ord(chkOverwrite.Checked);

  if FOutputDir <> '' then
  begin
    SetAnsiField(Opts.output_dir, FOutputDir);
    Opts.output_dir_status := 1;
  end;

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
end;

procedure TMainForm.ClearListClicked(Sender: TObject);
begin
  lstFiles.Clear;
end;

procedure TMainForm.StartClicked(Sender: TObject);
var
  Opts: TConvertOptions;
  FileArr: array of string;
  Count: Integer;
begin
  if lstFiles.Items.Count = 0 then
  begin
    MessageDlg('No files selected.', mtWarning, [mbOK], 0);
    Exit;
  end;

  SetLength(FileArr, lstFiles.Items.Count);
  CollectOptions(Opts, FileArr, Count);

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
  InDlg: TOpenDialog;
  OutDlg: TSaveDialog;
  InFile: string;
  OutFile: string;
  Opts: TAppleM4VOptions;
  ErrText: string;
begin
  InDlg := TOpenDialog.Create(Self);
  OutDlg := TSaveDialog.Create(Self);
  try
    InDlg.Options := [ofFileMustExist, ofPathMustExist];
    InDlg.Filter := 'Video files|*.mkv;*.mov;*.mp4;*.m4v|All files|*.*';
    if not InDlg.Execute then
      Exit;

    InFile := InDlg.FileName;

    OutDlg.Filter := 'Apple M4V|*.m4v|All files|*.*';
    OutDlg.DefaultExt := 'm4v';
    OutDlg.FileName := ChangeFileExt(ExtractFileName(InFile), '.m4v');
    if not OutDlg.Execute then
      Exit;

    OutFile := OutDlg.FileName;

    UiLog('Apple m4v creator: started for ' + InFile);
    UiStatus('Apple m4v creator: processing...');

    Opts := DefaultAppleM4VOptions;
    if not PromptAppleM4VOptions(Opts) then
    begin
      UiLog('Apple m4v creator: cancelled by user.');
      UiStatus('Ready');
      Exit;
    end;

    if not CreateAppleM4V(InFile, OutFile, Opts, ErrText) then
    begin
      UiLog('Apple m4v creator ERROR: ' + ErrText);
      UiStatus('Apple m4v creator: failed');
      MessageDlg('Apple m4v creator failed:' + LineEnding + ErrText, mtError, [mbOK], 0);
      Exit;
    end;

    UiLog('Apple m4v creator: done -> ' + OutFile);
    UiStatus('Apple m4v creator: done');
    MessageDlg('Apple-compatible M4V created:' + LineEnding + OutFile, mtInformation, [mbOK], 0);
  finally
    OutDlg.Free;
    InDlg.Free;
  end;
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
  SetRunningState(False);
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
begin
  if Percent < 0 then Percent := 0;
  if Percent > 100 then Percent := 100;
  pbProgress.Position := Round(Percent);

  if Fps > 0 then
    lblProgressText.Caption := Format('%.0f fps', [Fps])
  else
    lblProgressText.Caption := Format('%d%%', [Round(Percent)]);

  lblStatus.Caption := FormatEta(Eta);
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
  UiLog('All files processed.');
  lblStatus.Caption := 'All files processed.';
  SetRunningState(False);
  lstFiles.Clear;
end;

procedure TMainForm.SetRunningState(Running: Boolean);
begin
  btnStart.Enabled := not Running;
  btnStop.Enabled := Running;
end;

end.
