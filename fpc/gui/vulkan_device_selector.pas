unit vulkan_device_selector;

{$mode objfpc}{$H+}

{ Vulkan device selector dialog.
  Shows a simple selection dialog listing available Vulkan devices by index,
  allowing the user to choose one or select "Auto" (index -1). }

interface

{ Show a modal dialog to select a Vulkan device.
  DeviceCount is the number of working devices detected (0 = none probed).
  ParentHandle is the owner form handle (may be 0 for top-level).
  Returns the selected device index (0-based), or -1 for "Auto / default". }
function SelectVulkanDevice(DeviceCount: Integer): Integer;

{ Build a display name for the given Vulkan device index.
  Index -1 returns 'Auto (default)'. }
function VulkanDeviceDisplayName(Index: Integer): string;

implementation

uses
  SysUtils,
  Forms,
  Dialogs,
  StdCtrls,
  Controls,
  Classes,
  Graphics;

{ ---- VulkanDeviceDisplayName ------------------------------------------- }

function VulkanDeviceDisplayName(Index: Integer): string;
begin
  if Index < 0 then
    Result := 'Auto (default)'
  else
    Result := 'vulkan:' + IntToStr(Index);
end;

{ ---- SelectVulkanDevice ------------------------------------------------ }

function SelectVulkanDevice(DeviceCount: Integer): Integer;
var
  Dlg:  TForm;
  Lbl:  TLabel;
  Cmb:  TComboBox;
  BtnOK, BtnCancel: TButton;
  I:    Integer;
begin
  Result := -1;   { default: Auto }

  Dlg := TForm.CreateNew(Application);
  try
    Dlg.Caption     := 'Select Vulkan Device';
    Dlg.Width       := 340;
    Dlg.Height      := 160;
    Dlg.Position    := poScreenCenter;
    Dlg.BorderStyle := bsDialog;

    Lbl := TLabel.Create(Dlg);
    Lbl.Parent   := Dlg;
    Lbl.Caption  := 'Choose Vulkan device for prores_ks_vulkan:';
    Lbl.Left     := 16;
    Lbl.Top      := 16;
    Lbl.Width    := 300;
    Lbl.AutoSize := True;

    Cmb := TComboBox.Create(Dlg);
    Cmb.Parent    := Dlg;
    Cmb.Style     := csDropDownList;
    Cmb.Left      := 16;
    Cmb.Top       := 40;
    Cmb.Width     := 300;

    { Always add "Auto" as the first choice }
    Cmb.Items.Add(VulkanDeviceDisplayName(-1));

    if DeviceCount > 0 then
      for I := 0 to DeviceCount - 1 do
        Cmb.Items.Add(VulkanDeviceDisplayName(I))
    else
    begin
      { No devices detected; still allow manual entry for device 0 }
      Cmb.Items.Add(VulkanDeviceDisplayName(0));
    end;

    Cmb.ItemIndex := 0;

    BtnOK := TButton.Create(Dlg);
    BtnOK.Parent    := Dlg;
    BtnOK.Caption   := 'OK';
    BtnOK.Left      := 144;
    BtnOK.Top       := 90;
    BtnOK.Width     := 80;
    BtnOK.ModalResult := mrOK;
    BtnOK.Default   := True;

    BtnCancel := TButton.Create(Dlg);
    BtnCancel.Parent      := Dlg;
    BtnCancel.Caption     := 'Cancel';
    BtnCancel.Left        := 240;
    BtnCancel.Top         := 90;
    BtnCancel.Width       := 80;
    BtnCancel.ModalResult := mrCancel;
    BtnCancel.Cancel      := True;

    if Dlg.ShowModal = mrOK then
    begin
      { Item 0 = "Auto" -> return -1; item N+1 = device N -> return N }
      if Cmb.ItemIndex <= 0 then
        Result := -1
      else
        Result := Cmb.ItemIndex - 1;
    end;
  finally
    Dlg.Free;
  end;
end;

end.
