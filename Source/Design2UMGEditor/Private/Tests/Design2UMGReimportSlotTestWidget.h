// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "Blueprint/UserWidget.h"
#include "Design2UMGReimportSlotTestWidget.generated.h"

/** Test-only named-slot host: rejection exercises commit failure without production hooks. */
UCLASS()
class UDesign2UMGReimportSlotTestWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	FName RejectedSlot;

	virtual void GetSlotNames(TArray<FName>& SlotNames) const override
	{
		SlotNames.Append({TEXT("D2U_Content_0"), TEXT("D2U_Content_0_0"), TEXT("D2U_Content_0_1")});
	}

	virtual void SetContentForSlot(FName SlotName, UWidget* Content) override
	{
		if (SlotName != RejectedSlot) Super::SetContentForSlot(SlotName, Content);
	}
};
