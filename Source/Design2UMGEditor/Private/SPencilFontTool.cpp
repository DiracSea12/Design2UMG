// Copyright (c) 2026, Diracsea12. SPDX-License-Identifier: MIT
#include "SPencilFontTool.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Misc/MessageDialog.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"

void SPencilFontTool::Construct(const FArguments& Args)
{
    Texts = FPencilFontToolText();
    bInstalled = PencilFontTool::IsInstalled();
    bPresent = PencilFontTool::HasInstallation();
    ChildSlot
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [
                SNew(SCheckBox)
                .IsChecked_Lambda([this]() { return bInstalled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .IsEnabled_Lambda([]() { return !PencilFontTool::IsBusy(); })
                .OnCheckStateChanged(this, &SPencilFontTool::OnCheck)
                [ SNew(STextBlock).Text(FText::FromString(Texts.Get(TEXT("Enable")))) ]
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(12, 0)
            [
                SNew(SButton)
                .Text(FText::FromString(Texts.Get(TEXT("Remove"))))
                .IsEnabled_Lambda([this]() { return bPresent && !PencilFontTool::IsBusy(); })
                .OnClicked(this, &SPencilFontTool::OnRemove)
            ]
            + SHorizontalBox::Slot().AutoWidth()
            [
                SNew(SButton)
                .Text(FText::FromString(Texts.Get(TEXT("CancelInstall"))))
                .Visibility_Lambda([this]() { return Operation ? EVisibility::Visible : EVisibility::Collapsed; })
                .OnClicked_Lambda([this]() { CancelInstall(); return FReply::Handled(); })
            ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
            [ SNew(STextBlock).Text(FText::FromString(Texts.Get(TEXT("Location")))) ]
            + SHorizontalBox::Slot().FillWidth(1)
            [
                SNew(SEditableTextBox)
                .Text_Lambda([]() { return FText::FromString(PencilFontTool::StorageDirectory()); })
                .IsReadOnly_Lambda([this]() { return bPresent || PencilFontTool::IsBusy(); })
                .ToolTipText(FText::FromString(Texts.Get(TEXT("LocationHelp"))))
                .OnTextCommitted(this, &SPencilFontTool::OnLocationCommitted)
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(8, 0, 0, 0)
            [
                SNew(SButton).Text(FText::FromString(Texts.Get(TEXT("Browse"))))
                .IsEnabled_Lambda([this]() { return !bPresent && !PencilFontTool::IsBusy(); })
                .ToolTipText(FText::FromString(Texts.Get(TEXT("LocationHelp"))))
                .OnClicked(this, &SPencilFontTool::OnBrowse)
            ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
        [
            SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]() { return ActualLocation; })
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
        [
            SNew(STextBlock).AutoWrapText(true)
            .Text_Lambda([this]()
            {
                if (!Status.IsEmpty()) return Status;
                return FText::FromString(Texts.Get(bPresent && !bInstalled ? TEXT("Damaged") : bInstalled ? TEXT("InstalledHelp") : TEXT("NotInstalledHelp")));
            })
        ]
    ];
}

void SPencilFontTool::OnCheck(ECheckBoxState State)
{
    Texts = FPencilFontToolText();
    if (State == ECheckBoxState::Unchecked) { OnRemove(); return; }
    PencilFontTool::FDownload Download;
    FString Error;
    if (!PencilFontTool::ReadDownload(Download, Error)) { Status = FText::FromString(Error); return; }
    const FString Message = Texts.Get(TEXT("InstallConfirm"), {
        {TEXT("Version"), Download.Version}, {TEXT("DownloadUrl"), Download.Url},
        {TEXT("DownloadMiB"), FString::Printf(TEXT("%.1f"), Download.ArchiveBytes / 1048576.0)},
        {TEXT("InstalledMiB"), FString::Printf(TEXT("%.1f"), Download.InstalledBytes / 1048576.0)},
        {TEXT("InstallDirectory"), Download.InstallDirectory()}});
    if (FMessageDialog::Open(EAppMsgType::YesNo, FText::FromString(Message)) != EAppReturnType::Yes) return;
    Operation = PencilFontTool::Install(Download);
    Status = FText::FromString(Texts.Get(Operation ? TEXT("InstallProgress") : TEXT("Busy")));
}

FReply SPencilFontTool::OnRemove()
{
    Texts = FPencilFontToolText();
    if (!bPresent || PencilFontTool::IsBusy()) return FReply::Handled();
    if (FMessageDialog::Open(EAppMsgType::YesNo, FText::FromString(Texts.Get(TEXT("RemoveConfirm")))) != EAppReturnType::Yes)
        return FReply::Handled();
    FString Error;
    if (PencilFontTool::Remove(Error)) { bInstalled = false; bPresent = false; Status = FText::FromString(Texts.Get(TEXT("Removed"))); }
    else Status = FText::FromString(Error);
    return FReply::Handled();
}

void SPencilFontTool::Tick(const FGeometry& Geometry, double Time, float Delta)
{
    SCompoundWidget::Tick(Geometry, Time, Delta);
    if (Operation && Operation->Poll())
    {
        Status = FText::FromString(Operation->Error.IsEmpty() ? Texts.Get(TEXT("Installed")) : Operation->Error);
        Operation.Reset();
        NextCheckTime = 0;
    }
    if (Time >= NextCheckTime)
    {
        bInstalled = PencilFontTool::IsInstalled();
        bPresent = PencilFontTool::HasInstallation();
        PencilFontTool::FDownload Download; FString Error;
        ActualLocation = FText::FromString(PencilFontTool::ReadDownload(Download, Error)
            ? Texts.Get(TEXT("ActualLocation"), {{TEXT("InstallDirectory"), Download.InstallDirectory()}}) : Error);
        NextCheckTime = Time + 1;
    }
}
void SPencilFontTool::CancelInstall()
{
    if (Operation)
    {
        Operation->Cancel();
        Operation.Reset();
        bInstalled = PencilFontTool::IsInstalled();
        Status = FText::FromString(Texts.Get(bInstalled ? TEXT("Installed") : TEXT("InstallCancelled")));
    }
}
SPencilFontTool::~SPencilFontTool() { CancelInstall(); }

void SPencilFontTool::OnLocationCommitted(const FText& Value, ETextCommit::Type Commit)
{
    if (Commit == ETextCommit::OnCleared) return;
    Texts = FPencilFontToolText();
    FString Error;
    Status = PencilFontTool::SetStorageDirectory(Value.ToString(), Error) ? FText::GetEmpty() : FText::FromString(Error);
    NextCheckTime = 0;
}
FReply SPencilFontTool::OnBrowse()
{
    Texts = FPencilFontToolText();
    if (PencilFontTool::IsBusy() || bPresent) return FReply::Handled();
    IDesktopPlatform* Desktop = FDesktopPlatformModule::Get();
    if (!Desktop) { Status = FText::FromString(Texts.Get(TEXT("BrowseUnavailable"))); return FReply::Handled(); }
    FString Directory;
    if (Desktop->OpenDirectoryDialog(FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
        Texts.Get(TEXT("BrowseTitle")), PencilFontTool::StorageDirectory(), Directory))
        OnLocationCommitted(FText::FromString(Directory), ETextCommit::OnEnter);
    return FReply::Handled();
}
