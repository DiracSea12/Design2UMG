// Copyright Epic Games, Inc. All Rights Reserved.

#include "UMGWidgetUtils.h"
#include "PenToUMGLog.h"

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
// Slot 属性应用（函数指针映射表模式）
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
	}

	// VBox/HBox Slot 共用逻辑
	template<typename TSlot>
	void ApplyBoxSlotCommon(TSlot* BoxSlot, const FUMGSlotData& S)
	{
		BoxSlot->SetPadding(S.Padding);
		BoxSlot->SetHorizontalAlignment(FUMGWidgetUtils::ParseHAlign(S.HorizontalAlignment));
		BoxSlot->SetVerticalAlignment(FUMGWidgetUtils::ParseVAlign(S.VerticalAlignment));
		if (S.SizeRule == TEXT("Fill"))
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
		OS->SetHorizontalAlignment(FUMGWidgetUtils::ParseHAlign(S.HorizontalAlignment));
		OS->SetVerticalAlignment(FUMGWidgetUtils::ParseVAlign(S.VerticalAlignment));
	}

	void ApplyGridSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		UGridSlot* GS = CastChecked<UGridSlot>(Slot);
		GS->SetPadding(S.Padding);
		GS->SetRow(S.Row);
		GS->SetColumn(S.Column);
		GS->SetRowSpan(S.RowSpan);
		GS->SetColumnSpan(S.ColumnSpan);
		GS->SetHorizontalAlignment(FUMGWidgetUtils::ParseHAlign(S.HorizontalAlignment));
		GS->SetVerticalAlignment(FUMGWidgetUtils::ParseVAlign(S.VerticalAlignment));
	}

	void ApplyUniformGridSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		UUniformGridSlot* UGS = CastChecked<UUniformGridSlot>(Slot);
		UGS->SetRow(S.Row);
		UGS->SetColumn(S.Column);
		UGS->SetHorizontalAlignment(FUMGWidgetUtils::ParseHAlign(S.HorizontalAlignment));
		UGS->SetVerticalAlignment(FUMGWidgetUtils::ParseVAlign(S.VerticalAlignment));
	}

	void ApplyWrapBoxSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		UWrapBoxSlot* WS = CastChecked<UWrapBoxSlot>(Slot);
		WS->SetPadding(S.Padding);
		WS->SetHorizontalAlignment(FUMGWidgetUtils::ParseHAlign(S.HorizontalAlignment));
		WS->SetVerticalAlignment(FUMGWidgetUtils::ParseVAlign(S.VerticalAlignment));
		WS->SetFillEmptySpace(S.SizeRule == TEXT("Fill"));
	}

	void ApplyScrollBoxSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		UScrollBoxSlot* SS = CastChecked<UScrollBoxSlot>(Slot);
		SS->SetPadding(S.Padding);
		SS->SetHorizontalAlignment(FUMGWidgetUtils::ParseHAlign(S.HorizontalAlignment));
		SS->SetVerticalAlignment(FUMGWidgetUtils::ParseVAlign(S.VerticalAlignment));
		if (S.SizeRule == TEXT("Fill"))
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
		BS->SetPadding(S.Padding);
		BS->SetHorizontalAlignment(FUMGWidgetUtils::ParseHAlign(S.HorizontalAlignment));
		BS->SetVerticalAlignment(FUMGWidgetUtils::ParseVAlign(S.VerticalAlignment));
	}

	void ApplyButtonSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		UButtonSlot* BS = CastChecked<UButtonSlot>(Slot);
		BS->SetPadding(S.Padding);
		BS->SetHorizontalAlignment(FUMGWidgetUtils::ParseHAlign(S.HorizontalAlignment));
		BS->SetVerticalAlignment(FUMGWidgetUtils::ParseVAlign(S.VerticalAlignment));
	}

	void ApplySizeBoxSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		USizeBoxSlot* SS = CastChecked<USizeBoxSlot>(Slot);
		SS->SetPadding(S.Padding);
		SS->SetHorizontalAlignment(FUMGWidgetUtils::ParseHAlign(S.HorizontalAlignment));
		SS->SetVerticalAlignment(FUMGWidgetUtils::ParseVAlign(S.VerticalAlignment));
	}

	void ApplyScaleBoxSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		UScaleBoxSlot* SS = CastChecked<UScaleBoxSlot>(Slot);
		SS->SetHorizontalAlignment(FUMGWidgetUtils::ParseHAlign(S.HorizontalAlignment));
		SS->SetVerticalAlignment(FUMGWidgetUtils::ParseVAlign(S.VerticalAlignment));
	}

	void ApplyBackgroundBlurSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		UBackgroundBlurSlot* BS = CastChecked<UBackgroundBlurSlot>(Slot);
		BS->SetPadding(S.Padding);
		BS->SetHorizontalAlignment(FUMGWidgetUtils::ParseHAlign(S.HorizontalAlignment));
		BS->SetVerticalAlignment(FUMGWidgetUtils::ParseVAlign(S.VerticalAlignment));
	}

	void ApplyWidgetSwitcherSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		UWidgetSwitcherSlot* WS = CastChecked<UWidgetSwitcherSlot>(Slot);
		WS->SetPadding(S.Padding);
		WS->SetHorizontalAlignment(FUMGWidgetUtils::ParseHAlign(S.HorizontalAlignment));
		WS->SetVerticalAlignment(FUMGWidgetUtils::ParseVAlign(S.VerticalAlignment));
	}

	void ApplySafeZoneSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		USafeZoneSlot* SZ = CastChecked<USafeZoneSlot>(Slot);
		SZ->SetPadding(S.Padding);
		SZ->SetHorizontalAlignment(FUMGWidgetUtils::ParseHAlign(S.HorizontalAlignment));
		SZ->SetVerticalAlignment(FUMGWidgetUtils::ParseVAlign(S.VerticalAlignment));
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

void FUMGWidgetUtils::ApplySlotProperties(UPanelSlot* Slot, const FUMGSlotData& S)
{
	if (!Slot) return;
	const FUMGSlotData Default;
	const bool bHasNonDefaultCanvas =
		!FMath::IsNearlyEqual(S.Anchors.Min.X, Default.Anchors.Min.X) ||
		!FMath::IsNearlyEqual(S.Anchors.Min.Y, Default.Anchors.Min.Y) ||
		!FMath::IsNearlyEqual(S.Anchors.Max.X, Default.Anchors.Max.X) ||
		!FMath::IsNearlyEqual(S.Anchors.Max.Y, Default.Anchors.Max.Y) ||
		!FMath::IsNearlyEqual(S.Alignment.X, Default.Alignment.X) ||
		!FMath::IsNearlyEqual(S.Alignment.Y, Default.Alignment.Y) ||
		!FMath::IsNearlyEqual(S.Offsets.Left, Default.Offsets.Left) ||
		!FMath::IsNearlyEqual(S.Offsets.Top, Default.Offsets.Top) ||
		!FMath::IsNearlyEqual(S.Offsets.Right, Default.Offsets.Right) ||
		!FMath::IsNearlyEqual(S.Offsets.Bottom, Default.Offsets.Bottom);
	const bool bHasNonDefaultPadding = !FUMGWidgetUtils::IsNearlyEqualMargin(S.Padding, Default.Padding);
	const bool bHasNonDefaultBox =
		S.HorizontalAlignment != Default.HorizontalAlignment ||
		S.VerticalAlignment != Default.VerticalAlignment ||
		S.SizeRule != Default.SizeRule ||
		!FMath::IsNearlyEqual(S.FillWeight, Default.FillWeight);
	const bool bHasNonDefaultGrid =
		S.Row != Default.Row ||
		S.Column != Default.Column ||
		S.RowSpan != Default.RowSpan ||
		S.ColumnSpan != Default.ColumnSpan;

	const bool bIsCanvas = Slot->IsA<UCanvasPanelSlot>();
	const bool bIsGrid = Slot->IsA<UGridSlot>() || Slot->IsA<UUniformGridSlot>();
	if (bIsCanvas)
	{
		if (bHasNonDefaultPadding || bHasNonDefaultBox || bHasNonDefaultGrid)
		{
			UE_LOG(LogPenToUMG, Warning, TEXT("CanvasPanelSlot 忽略非Canvas字段（Padding/HAlign/VAlign/SizeRule/FillWeight/Row/Column等）"));
		}
	}
	else
	{
		if (bHasNonDefaultCanvas)
		{
			UE_LOG(LogPenToUMG, Warning, TEXT("%s 忽略 Anchors/Offsets/Alignment"), *Slot->GetClass()->GetName());
		}
		if (!bIsGrid && bHasNonDefaultGrid)
		{
			UE_LOG(LogPenToUMG, Warning, TEXT("%s 忽略 Row/Column/RowSpan/ColumnSpan"), *Slot->GetClass()->GetName());
		}
		if (Slot->IsA<UScaleBoxSlot>() && bHasNonDefaultPadding)
		{
			UE_LOG(LogPenToUMG, Warning, TEXT("ScaleBoxSlot 不支持 Padding"));
		}
	}

	if (const FApplySlotFunc* Func = GetSlotApplierMap().Find(Slot->GetClass()))
	{
		(*Func)(Slot, S);
	}
	else
	{
		UE_LOG(LogPenToUMG, Warning, TEXT("未支持的Slot类型: %s"), *Slot->GetClass()->GetName());
	}
}
