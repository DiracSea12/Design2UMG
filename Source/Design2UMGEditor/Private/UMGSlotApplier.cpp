// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGWidgetTreeBuilder.h"
#include "UMGValueParsing.h"
#include "WidgetLayoutAdapter.h"
#include "Design2UMGLog.h"

#include "Components/PanelSlot.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/OverlaySlot.h"
#include "Components/GridSlot.h"
#include "Components/UniformGridSlot.h"
#include "Components/WrapBoxSlot.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/BorderSlot.h"
#include "Components/ButtonSlot.h"
#include "Components/SizeBoxSlot.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/BackgroundBlurSlot.h"
#include "Components/WidgetSwitcherSlot.h"
#include "Components/SafeZoneSlot.h"

// ============================================================================
// Slot property application using a function-pointer mapping table.
// ============================================================================

namespace
{
	using FApplySlotFunc = void(*)(UPanelSlot*, const FUMGSlotData&);

	void ApplyCanvasPanelSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		UCanvasPanelSlot* CS = CastChecked<UCanvasPanelSlot>(Slot);
		FAnchors Anchors;
		Anchors.Minimum = FVector2D(S.Anchors.Min.X, S.Anchors.Min.Y);
		Anchors.Maximum = FVector2D(S.Anchors.Max.X, S.Anchors.Max.Y);
		CS->SetAnchors(Anchors);
		CS->SetAlignment(FVector2D(S.Alignment.X, S.Alignment.Y));
		CS->SetOffsets(FMargin(S.Offsets.Left, S.Offsets.Top, S.Offsets.Right, S.Offsets.Bottom));
		CS->SetAutoSize(S.bAutoSize);
	}

	// Shared logic for VBox/HBox slots.
	template<typename TSlot>
	void ApplyBoxSlotCommon(TSlot* BoxSlot, const FUMGSlotData& S)
	{
		BoxSlot->SetPadding(S.Padding);
		BoxSlot->SetHorizontalAlignment(FUMGValueParser::ToSlateHorizontalAlignment(S.HorizontalAlignment));
		BoxSlot->SetVerticalAlignment(FUMGValueParser::ToSlateVerticalAlignment(S.VerticalAlignment));
		if (FUMGValueParser::IsFillSizeRule(S.SizeRule))
		{
			FSlateChildSize Size;
			Size.SizeRule = ESlateSizeRule::Fill;
			Size.Value = S.FillWeight;
			BoxSlot->SetSize(Size);
		}
	}

	void ApplyVerticalBoxSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		ApplyBoxSlotCommon(CastChecked<UVerticalBoxSlot>(Slot), S);
	}

	void ApplyHorizontalBoxSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		ApplyBoxSlotCommon(CastChecked<UHorizontalBoxSlot>(Slot), S);
	}

	void ApplyOverlaySlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		UOverlaySlot* OS = CastChecked<UOverlaySlot>(Slot);
		OS->SetPadding(S.Padding);
		OS->SetHorizontalAlignment(FUMGValueParser::ToSlateHorizontalAlignment(S.HorizontalAlignment));
		OS->SetVerticalAlignment(FUMGValueParser::ToSlateVerticalAlignment(S.VerticalAlignment));
	}

	void ApplyGridSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		UGridSlot* GS = CastChecked<UGridSlot>(Slot);
		GS->SetPadding(S.Padding);
		GS->SetRow(S.Row);
		GS->SetColumn(S.Column);
		GS->SetRowSpan(S.RowSpan);
		GS->SetColumnSpan(S.ColumnSpan);
		GS->SetHorizontalAlignment(FUMGValueParser::ToSlateHorizontalAlignment(S.HorizontalAlignment));
		GS->SetVerticalAlignment(FUMGValueParser::ToSlateVerticalAlignment(S.VerticalAlignment));
	}

	void ApplyUniformGridSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		UUniformGridSlot* UGS = CastChecked<UUniformGridSlot>(Slot);
		UGS->SetRow(S.Row);
		UGS->SetColumn(S.Column);
		UGS->SetHorizontalAlignment(FUMGValueParser::ToSlateHorizontalAlignment(S.HorizontalAlignment));
		UGS->SetVerticalAlignment(FUMGValueParser::ToSlateVerticalAlignment(S.VerticalAlignment));
	}

	void ApplyWrapBoxSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		UWrapBoxSlot* WS = CastChecked<UWrapBoxSlot>(Slot);
		WS->SetPadding(S.Padding);
		WS->SetHorizontalAlignment(FUMGValueParser::ToSlateHorizontalAlignment(S.HorizontalAlignment));
		WS->SetVerticalAlignment(FUMGValueParser::ToSlateVerticalAlignment(S.VerticalAlignment));
		WS->SetFillEmptySpace(FUMGValueParser::IsFillSizeRule(S.SizeRule));
	}

	void ApplyScrollBoxSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		UScrollBoxSlot* SS = CastChecked<UScrollBoxSlot>(Slot);
		SS->SetPadding(S.Padding);
		SS->SetHorizontalAlignment(FUMGValueParser::ToSlateHorizontalAlignment(S.HorizontalAlignment));
		SS->SetVerticalAlignment(FUMGValueParser::ToSlateVerticalAlignment(S.VerticalAlignment));
		if (FUMGValueParser::IsFillSizeRule(S.SizeRule))
		{
			FSlateChildSize Size;
			Size.SizeRule = ESlateSizeRule::Fill;
			Size.Value = S.FillWeight;
			SS->SetSize(Size);
		}
	}

	void ApplyBorderSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		UBorderSlot* BS = CastChecked<UBorderSlot>(Slot);
		// Only override border padding when the slot explicitly carries non-default padding.
		const FUMGSlotData Default;
		if (!FUMGValueParser::IsNearlyEqualMargin(S.Padding, Default.Padding))
		{
			BS->SetPadding(S.Padding);
		}
		BS->SetHorizontalAlignment(FUMGValueParser::ToSlateHorizontalAlignment(S.HorizontalAlignment));
		BS->SetVerticalAlignment(FUMGValueParser::ToSlateVerticalAlignment(S.VerticalAlignment));
	}

	void ApplyButtonSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		UButtonSlot* BS = CastChecked<UButtonSlot>(Slot);
		BS->SetPadding(S.Padding);
		BS->SetHorizontalAlignment(FUMGValueParser::ToSlateHorizontalAlignment(S.HorizontalAlignment));
		BS->SetVerticalAlignment(FUMGValueParser::ToSlateVerticalAlignment(S.VerticalAlignment));
	}

	void ApplySizeBoxSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		USizeBoxSlot* SS = CastChecked<USizeBoxSlot>(Slot);
		SS->SetPadding(S.Padding);
		SS->SetHorizontalAlignment(FUMGValueParser::ToSlateHorizontalAlignment(S.HorizontalAlignment));
		SS->SetVerticalAlignment(FUMGValueParser::ToSlateVerticalAlignment(S.VerticalAlignment));
	}

	void ApplyScaleBoxSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		UScaleBoxSlot* SS = CastChecked<UScaleBoxSlot>(Slot);
		SS->SetHorizontalAlignment(FUMGValueParser::ToSlateHorizontalAlignment(S.HorizontalAlignment));
		SS->SetVerticalAlignment(FUMGValueParser::ToSlateVerticalAlignment(S.VerticalAlignment));
	}

	void ApplyBackgroundBlurSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		UBackgroundBlurSlot* BS = CastChecked<UBackgroundBlurSlot>(Slot);
		BS->SetPadding(S.Padding);
		BS->SetHorizontalAlignment(FUMGValueParser::ToSlateHorizontalAlignment(S.HorizontalAlignment));
		BS->SetVerticalAlignment(FUMGValueParser::ToSlateVerticalAlignment(S.VerticalAlignment));
	}

	void ApplyWidgetSwitcherSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		UWidgetSwitcherSlot* WS = CastChecked<UWidgetSwitcherSlot>(Slot);
		WS->SetPadding(S.Padding);
		WS->SetHorizontalAlignment(FUMGValueParser::ToSlateHorizontalAlignment(S.HorizontalAlignment));
		WS->SetVerticalAlignment(FUMGValueParser::ToSlateVerticalAlignment(S.VerticalAlignment));
	}

	void ApplySafeZoneSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		USafeZoneSlot* SZ = CastChecked<USafeZoneSlot>(Slot);
		SZ->SetPadding(S.Padding);
		SZ->SetHorizontalAlignment(FUMGValueParser::ToSlateHorizontalAlignment(S.HorizontalAlignment));
		SZ->SetVerticalAlignment(FUMGValueParser::ToSlateVerticalAlignment(S.VerticalAlignment));
	}

	const TMap<UClass*, FApplySlotFunc>& GetSlotApplierMap()
	{
		static const TMap<UClass*, FApplySlotFunc> Map = {
			{UCanvasPanelSlot::StaticClass(), &ApplyCanvasPanelSlot},
			{UVerticalBoxSlot::StaticClass(), &ApplyVerticalBoxSlot},
			{UHorizontalBoxSlot::StaticClass(), &ApplyHorizontalBoxSlot},
			{UOverlaySlot::StaticClass(), &ApplyOverlaySlot},
			{UGridSlot::StaticClass(), &ApplyGridSlot},
			{UUniformGridSlot::StaticClass(), &ApplyUniformGridSlot},
			{UWrapBoxSlot::StaticClass(), &ApplyWrapBoxSlot},
			{UScrollBoxSlot::StaticClass(), &ApplyScrollBoxSlot},
			{UBorderSlot::StaticClass(), &ApplyBorderSlot},
			{UButtonSlot::StaticClass(), &ApplyButtonSlot},
			{USizeBoxSlot::StaticClass(), &ApplySizeBoxSlot},
			{UScaleBoxSlot::StaticClass(), &ApplyScaleBoxSlot},
			{UBackgroundBlurSlot::StaticClass(), &ApplyBackgroundBlurSlot},
			{UWidgetSwitcherSlot::StaticClass(), &ApplyWidgetSwitcherSlot},
			{USafeZoneSlot::StaticClass(), &ApplySafeZoneSlot},
		};
		return Map;
	}
}

void FUMGWidgetTreeBuilder::ApplySlotProperties(UPanelSlot* Slot, const FUMGSlotData& S)
{
	if (!Slot) return;
	const FWidgetLayoutAdapter::FNonDefaultGroups NonDefault = FWidgetLayoutAdapter::GetNonDefaultSlotGroups(S);

	const bool bIsCanvas = Slot->IsA<UCanvasPanelSlot>();
	const bool bIsGrid = Slot->IsA<UGridSlot>() || Slot->IsA<UUniformGridSlot>();
	if (bIsCanvas)
	{
		if (NonDefault.bPadding || NonDefault.bBox || NonDefault.bGrid)
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("CanvasPanelSlot ignored non-canvas fields (Padding/HAlign/VAlign/SizeRule/FillWeight/Row/Column, etc.)."));
		}
	}
	else
	{
		if (NonDefault.bCanvas)
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("%s ignored Anchors/Offsets/Alignment."), *Slot->GetClass()->GetName());
		}
		if (!bIsGrid && NonDefault.bGrid)
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("%s ignored Row/Column/RowSpan/ColumnSpan."), *Slot->GetClass()->GetName());
		}
		if (Slot->IsA<UScaleBoxSlot>() && NonDefault.bPadding)
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("ScaleBoxSlot does not support Padding."));
		}
	}

	if (const FApplySlotFunc* Func = GetSlotApplierMap().Find(Slot->GetClass()))
	{
		(*Func)(Slot, S);
	}
	else
	{
		UE_LOG(LogDesign2UMG, Warning, TEXT("Unsupported slot type: %s"), *Slot->GetClass()->GetName());
	}
}

