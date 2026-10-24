// Copyright (c) 2026, Diracsea12. SPDX-License-Identifier: MIT
#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "PencilFontTool.h"
#include "PencilFontToolText.h"

class SPencilFontTool : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SPencilFontTool) {}
    SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    virtual ~SPencilFontTool();
    virtual void Tick(const FGeometry& Geometry, double Time, float Delta) override;
    void CancelInstall();
private:
    bool bInstalled = false;
    bool bPresent = false;
    double NextCheckTime = 0;
    FText Status;
    FText ActualLocation;
    FPencilFontToolText Texts;
    TSharedPtr<PencilFontTool::FInstall, ESPMode::ThreadSafe> Operation;
    void OnCheck(ECheckBoxState State);
    FReply OnRemove();
    FReply OnBrowse();
    void OnLocationCommitted(const FText& Value, ETextCommit::Type Commit);
};
