// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGTextLayoutAccess.h"

#include "Misc/EngineVersionComparison.h"

#if UE_VERSION_OLDER_THAN(5, 4, 0)
#include "UObject/UnrealType.h"
#endif

namespace Design2UMGTextLayoutAccess
{
#if UE_VERSION_OLDER_THAN(5, 4, 0)
	namespace
	{
		FFloatProperty& GetLineHeightPercentageProperty()
		{
			static FFloatProperty* Property = FindFProperty<FFloatProperty>(
				UTextLayoutWidget::StaticClass(), TEXT("LineHeightPercentage"));
			checkf(Property, TEXT("UTextLayoutWidget.LineHeightPercentage must be a float property"));
			return *Property;
		}

		FBoolProperty& GetAutoWrapTextProperty()
		{
			static FBoolProperty* Property = FindFProperty<FBoolProperty>(
				UTextLayoutWidget::StaticClass(), TEXT("AutoWrapText"));
			checkf(Property, TEXT("UTextLayoutWidget.AutoWrapText must be a bool property"));
			return *Property;
		}

		FFloatProperty& GetWrapTextAtProperty()
		{
			static FFloatProperty* Property = FindFProperty<FFloatProperty>(
				UTextLayoutWidget::StaticClass(), TEXT("WrapTextAt"));
			checkf(Property, TEXT("UTextLayoutWidget.WrapTextAt must be a float property"));
			return *Property;
		}

		FStructProperty& GetMarginProperty()
		{
			static FStructProperty* Property = FindFProperty<FStructProperty>(
				UTextLayoutWidget::StaticClass(), TEXT("Margin"));
			checkf(Property && Property->Struct == FMargin::StaticStruct(),
				TEXT("UTextLayoutWidget.Margin must be an FMargin property"));
			return *Property;
		}

		FStructProperty& GetShapedTextOptionsProperty()
		{
			static FStructProperty* Property = FindFProperty<FStructProperty>(
				UTextLayoutWidget::StaticClass(), TEXT("ShapedTextOptions"));
			checkf(Property && Property->Struct == FShapedTextOptions::StaticStruct(),
				TEXT("UTextLayoutWidget.ShapedTextOptions must be an FShapedTextOptions property"));
			return *Property;
		}
	}
#endif

	void Apply(UTextLayoutWidget& TextWidget, const FUpdate& Update)
	{
#if UE_VERSION_OLDER_THAN(5, 4, 0)
		bool bNeedsSynchronize = false;

		FFloatProperty& LineHeightProperty = GetLineHeightPercentageProperty();
		const float EffectiveLineHeight = Update.LineHeightPercentage.Get(
			LineHeightProperty.GetPropertyValue_InContainer(&TextWidget));
		if (Update.ApplyLineHeightToBottomLine.IsSet())
		{
			checkf(Update.ApplyLineHeightToBottomLine.GetValue()
				|| FMath::IsNearlyEqual(EffectiveLineHeight, 1.0f),
				TEXT("UE 5.3 applies line height to the bottom line; false is valid only at a 1.0 line-height percentage"));
		}

		if (Update.LineHeightPercentage.IsSet())
		{
			LineHeightProperty.SetPropertyValue_InContainer(&TextWidget, Update.LineHeightPercentage.GetValue());
			bNeedsSynchronize = true;
		}
		if (Update.AutoWrapText.IsSet())
		{
			GetAutoWrapTextProperty().SetPropertyValue_InContainer(&TextWidget, Update.AutoWrapText.GetValue());
			bNeedsSynchronize = true;
		}
		if (Update.WrapTextAt.IsSet())
		{
			GetWrapTextAtProperty().SetPropertyValue_InContainer(&TextWidget, Update.WrapTextAt.GetValue());
			bNeedsSynchronize = true;
		}
		if (Update.Margin.IsSet())
		{
			const FMargin& Margin = Update.Margin.GetValue();
			FStructProperty& MarginProperty = GetMarginProperty();
			MarginProperty.CopyCompleteValue(
				MarginProperty.ContainerPtrToValuePtr<void>(&TextWidget),
				&Margin);
			bNeedsSynchronize = true;
		}
		if (Update.ShapedTextOptions.IsSet())
		{
			const FShapedTextOptions& ShapedTextOptions = Update.ShapedTextOptions.GetValue();
			FStructProperty& ShapedTextOptionsProperty = GetShapedTextOptionsProperty();
			ShapedTextOptionsProperty.CopyCompleteValue(
				ShapedTextOptionsProperty.ContainerPtrToValuePtr<void>(&TextWidget),
				&ShapedTextOptions);
			bNeedsSynchronize = true;
		}

		if (bNeedsSynchronize && TextWidget.GetCachedWidget().IsValid())
		{
			TextWidget.SynchronizeProperties();
		}
#else
		if (Update.LineHeightPercentage.IsSet())
		{
			TextWidget.SetLineHeightPercentage(Update.LineHeightPercentage.GetValue());
		}
		if (Update.ApplyLineHeightToBottomLine.IsSet())
		{
			TextWidget.SetApplyLineHeightToBottomLine(Update.ApplyLineHeightToBottomLine.GetValue());
		}
		if (Update.AutoWrapText.IsSet())
		{
			TextWidget.SetAutoWrapText(Update.AutoWrapText.GetValue());
		}
		if (Update.WrapTextAt.IsSet())
		{
			TextWidget.SetWrapTextAt(Update.WrapTextAt.GetValue());
		}
		if (Update.Margin.IsSet())
		{
			TextWidget.SetMargin(Update.Margin.GetValue());
		}
		if (Update.ShapedTextOptions.IsSet())
		{
			TextWidget.SetShapedTextOptions(Update.ShapedTextOptions.GetValue());
		}
#endif
	}
}
