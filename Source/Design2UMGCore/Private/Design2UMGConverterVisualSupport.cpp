// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGConverterVisualSupport.h"

#include "Design2UMGLog.h"
#include "Design2UMGSourceMetadata.h"
#include "LayoutMapper.h"
#include "UMGPropertyKeys.h"
#include "UMGWidgetNodeAccess.h"

namespace
{
	bool HasVisibleFill(const FDesignFill& Fill)
	{
		return Fill.bEnabled && Fill.Type != EDesignFillType::None;
	}

	bool IsHexColorLiteral(const FString& ColorValue)
	{
		if (!ColorValue.StartsWith(TEXT("#")))
		{
			return false;
		}

		const int32 Len = ColorValue.Len();
		return Len == 4 || Len == 7 || Len == 9;
	}

	FString BuildHexColorWithAlpha(const FString& ColorValue, uint8 Alpha)
	{
		if (!IsHexColorLiteral(ColorValue))
		{
			return FString();
		}

		FColor Parsed = FColor::FromHex(ColorValue);
		Parsed.A = Alpha;
		return FString::Printf(TEXT("#%02X%02X%02X%02X"), Parsed.R, Parsed.G, Parsed.B, Parsed.A);
	}

	bool IsNeutralPlaceholderColor(const FString& ColorValue)
	{
		if (!IsHexColorLiteral(ColorValue))
		{
			return true;
		}

		const FColor Parsed = FColor::FromHex(ColorValue);
		const uint8 MaxChannel = FMath::Max3(Parsed.R, Parsed.G, Parsed.B);
		const uint8 MinChannel = FMath::Min3(Parsed.R, Parsed.G, Parsed.B);
		return (Parsed.A <= 0x20) || ((MaxChannel - MinChannel) <= 0x10);
	}

	bool HasWidgetStringProperty(const FUMGWidgetNode& Widget, const TCHAR* Key)
	{
		return Design2UMGWidgetNodeAccess::HasWidgetString(Widget, Key);
	}
}

namespace Design2UMGConverterVisualSupport
{
	bool HasVisualFill(const TSharedPtr<FDesignNode>& Node)
	{
		return Node.IsValid() && (HasVisibleFill(Node->Visual.Fill) || Node->Visual.Fills.Num() > 0);
	}

	void ApplyBaseVisualProperties(
		FUMGWidgetNode& Widget,
		const TSharedPtr<FDesignNode>& Node)
	{
		VisualMapper::ApplyVisualProperties(Widget, Node);
		if (Widget.Type == TEXT("Border")
			&& !HasWidgetStringProperty(Widget, UMGPropertyKeys::BrushColor)
			&& !HasWidgetStringProperty(Widget, UMGPropertyKeys::TexturePath)
			&& !HasWidgetStringProperty(Widget, UMGPropertyKeys::MaterialRef))
		{
			Widget.Brush.BrushColor = FLinearColor::Transparent;
			Widget.Brush.bHasBrushColor = true;
			if (HasWidgetStringProperty(Widget, UMGPropertyKeys::OutlineThickness))
			{
				Widget.Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
				Widget.Brush.bHasDrawAs = true;
			}
		}
	}

	void ApplyRenderFallbackVisuals(
		FUMGWidgetNode& Widget,
		const TSharedPtr<FDesignNode>& Node)
	{
		VisualMapper::ApplyRenderTransformProperties(Widget, Node);
	}

	bool TryApplyPlaceholderSurfaceTint(
		FUMGWidgetNode& Widget,
		const TSharedPtr<FDesignNode>& Node,
		const FConversionVisualSettingsPayload& VisualSettings)
	{
		const UDesign2UMGSettings::FResolvedPlaceholderHatchStyle& Style = VisualSettings.PlaceholderHatchStyle;
		const bool bExpandedRefInstancePlaceholder = Design2UMGSourceMetadata::HasNodeMetadataFlag(Node, TEXT("design2umg.expanded_ref_instance"));
		const FString& DefaultPlaceholderFillColor =
			bExpandedRefInstancePlaceholder ? Style.RefFillColor : Style.BaseFillColor;
		const FString& DefaultPlaceholderOutlineColor =
			bExpandedRefInstancePlaceholder ? Style.RefStripeColor : Style.BaseStripeColor;
		const bool bIsLeafContainer =
			(Node->Type == EDesignNodeType::Frame
				|| Node->Type == EDesignNodeType::Group
				|| Node->Type == EDesignNodeType::Rectangle)
			&& Node->Children.Num() == 0;
		const bool bIsExplicitEmptySlotSurface =
			Design2UMGSourceMetadata::HasNodeMetadataFlag(Node, TEXT("pen.slot.empty"))
			|| Design2UMGSourceMetadata::HasNodeMetadataFlag(Node, TEXT("source.slot.empty"));
		const bool bUseSlotPlaceholderTint =
			bIsLeafContainer
			&& Node->Transform.Width.Value >= 24.0f
			&& Node->Transform.Height.Value >= 12.0f
			&& (Node->LayoutData.bSemanticPlaceholderSurface || bIsExplicitEmptySlotSurface);
		if (!bUseSlotPlaceholderTint)
		{
			return false;
		}

		const FString FillColor = IsHexColorLiteral(Node->Visual.Fill.SolidColor) ? Node->Visual.Fill.SolidColor : FString();
		const FString StrokeColor = IsHexColorLiteral(Node->Visual.Stroke.Fill.SolidColor) ? Node->Visual.Stroke.Fill.SolidColor : FString();
		const FString PlaceholderOutlineColor =
			(!StrokeColor.IsEmpty() && !IsNeutralPlaceholderColor(StrokeColor)) ? StrokeColor : DefaultPlaceholderOutlineColor;
		FString PlaceholderFillColor =
			(!FillColor.IsEmpty() && !IsNeutralPlaceholderColor(FillColor)) ? FillColor : DefaultPlaceholderFillColor;
		if (PlaceholderFillColor.IsEmpty() && !PlaceholderOutlineColor.IsEmpty())
		{
			PlaceholderFillColor = BuildHexColorWithAlpha(PlaceholderOutlineColor, 0x33);
		}
		if (PlaceholderFillColor.IsEmpty() || PlaceholderOutlineColor.IsEmpty())
		{
			return false;
		}

		UE_LOG(LogDesign2UMG, Verbose, TEXT("ConvertNode: applying placeholder surface tint to '%s'"), *Node->Name);
		Widget.Type = TEXT("Image");
		Widget.Children.Empty();
		const int32 InsetPx = FMath::Clamp(
			FMath::RoundToInt(FMath::Min3(10.0f, Node->Transform.Width.Value * 0.25f, Node->Transform.Height.Value * 0.25f)),
			1,
			10);
		Widget.Brush = FUMGVisualBrushData();
		Widget.Brush.MaterialRef = TEXT("placeholdersurface");
		Widget.Brush.bHasMaterialRef = true;
		Widget.Brush.MaterialParams = FString::Printf(
			TEXT("{\"Color\":\"%s\",\"StripeColor\":\"%s\",\"StripeWidth\":%d,\"StripeGap\":%d,\"Inset\":%d}"),
			*PlaceholderFillColor,
			*PlaceholderOutlineColor,
			Style.StripeWidthPx,
			Style.StripeGapPx,
			InsetPx);
		Widget.Brush.bHasMaterialParams = true;
		if (Node->Transform.Width.Value > 0.0f)
		{
			Widget.WidgetStyle.WidthOverride = Node->Transform.Width.Value;
		}
		if (Node->Transform.Height.Value > 0.0f)
		{
			Widget.WidgetStyle.HeightOverride = Node->Transform.Height.Value;
		}
		return true;
	}

	bool ShouldWrapNodeWithBorder(
		const TSharedPtr<FDesignNode>& Node,
		bool bPlaceholderUsesDirectImage)
	{
		const bool bNeedsPaddingWrapper =
			LayoutMapper::HasPadding(Node)
			&& (Node->LayoutData.bHasPaddingRuntimeWrapDecision
				? Node->LayoutData.bPaddingRequiresRuntimeWrap
				: ((Node->LayoutData.Layout == EDesignLayout::None)
					|| ((Node->LayoutData.Layout == EDesignLayout::Wrap || Node->LayoutData.Layout == EDesignLayout::Grid)
						&& Node->Children.Num() > 0)));

		return (Node->Type == EDesignNodeType::Frame || Node->Type == EDesignNodeType::Group)
			&& (HasVisualFill(Node)
				|| bNeedsPaddingWrapper
				|| Node->Visual.CornerRadius.HasRadius()
				|| Node->Visual.Stroke.HasStroke())
			&& !bPlaceholderUsesDirectImage;
	}

	FUMGWidgetNode WrapWithBorder(
		FUMGWidgetNode Widget,
		const TSharedPtr<FDesignNode>& Node)
	{
		return VisualMapper::WrapWithBorder(MoveTemp(Widget), Node);
	}

	FUMGWidgetNode ApplyVisualEffectWrappers(
		FUMGWidgetNode Widget,
		const TSharedPtr<FDesignNode>& Node,
		const FVisualMapperOptions& VisualMapperOptions)
	{
		if (Node->HasMultiFill())
		{
			Widget = VisualMapper::WrapWithMultiFill(MoveTemp(Widget), Node, VisualMapperOptions);
		}

		const FDesignShadow* InnerShadow = nullptr;
		for (const FDesignEffect& Effect : Node->Visual.Effects)
		{
			if (!Effect.bEnabled || Effect.Type != EDesignEffectType::Shadow || !Effect.Shadow.bInner)
			{
				continue;
			}

			if (!InnerShadow || Effect.Shadow.Blur > InnerShadow->Blur)
			{
				InnerShadow = &Effect.Shadow;
			}
		}

		if (InnerShadow && Node.IsValid() && Node->Transform.Width.Value > 0.0f && Node->Transform.Height.Value > 0.0f)
		{
			Widget = VisualMapper::WrapWithInnerShadow(MoveTemp(Widget), Node, *InnerShadow);
		}

		const int32 ShadowCount = Widget.Shadow.BoxShadows.Num();
		if (ShadowCount > 0)
		{
			const TArray<FUMGBoxShadowLayerData> BoxShadows = Widget.Shadow.BoxShadows;
			Widget.Shadow.BoxShadows.Reset();
			for (int32 Index = 0; Index < ShadowCount; ++Index)
			{
				Widget = VisualMapper::WrapWithBoxShadow(MoveTemp(Widget), Node, BoxShadows[Index], Index);
			}
		}

		float MaxForegroundBlur = 0.0f;
		float MaxBackgroundBlur = 0.0f;
		for (const FDesignEffect& Effect : Node->Visual.Effects)
		{
			if (!Effect.bEnabled || Effect.Radius <= 0.0f)
			{
				continue;
			}

			if (Effect.Type == EDesignEffectType::Blur)
			{
				MaxForegroundBlur = FMath::Max(MaxForegroundBlur, Effect.Radius);
			}
			else if (Effect.Type == EDesignEffectType::BackgroundBlur)
			{
				MaxBackgroundBlur = FMath::Max(MaxBackgroundBlur, Effect.Radius);
			}
		}

		if (MaxForegroundBlur > 0.0f && Node.IsValid() && Node->Transform.Width.Value > 0.0f && Node->Transform.Height.Value > 0.0f)
		{
			Widget = VisualMapper::WrapWithForegroundBlur(MoveTemp(Widget), Node, MaxForegroundBlur);
		}

		if (MaxBackgroundBlur > 0.0f)
		{
			Widget = VisualMapper::WrapWithBackgroundBlur(MoveTemp(Widget), MaxBackgroundBlur);
		}

		return Widget;
	}
}
