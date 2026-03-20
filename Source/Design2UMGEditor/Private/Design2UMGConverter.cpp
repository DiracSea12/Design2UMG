// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGConverter.h"
#include "DesignTypeMapper.h"
#include "Design2UMGLog.h"
#include "Design2UMGSettings.h"
#include "FontResolver.h"
#include "LayoutMapper.h"
#include "VisualMapper.h"
#include "RefExpander.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Engine/Font.h"
#include "Components/TextBlock.h"
#include "Misc/PackageName.h"

namespace
{
	constexpr int32 MaxTreeRecursionDepth = 512;

	thread_local int32 GConvertNodeDepth = 0;
	thread_local int32 GPrecomputeDepth = 0;
	thread_local int32 GEstimateDepth = 0;
	TMap<FString, float> GResolvedTextFontScaleCache;

	struct FRecursionGuard
	{
		explicit FRecursionGuard(int32& InDepth, const TCHAR* InName = TEXT(""))
			: Depth(InDepth)
			, bValid(InDepth < MaxTreeRecursionDepth)
		{
			if (bValid)
			{
				++Depth;
			}
			else
			{
				UE_LOG(LogDesign2UMG, Verbose, TEXT("%s: recursion depth exceeded (%d)"), InName, MaxTreeRecursionDepth);
				UE_LOG(LogDesign2UMG, Error, TEXT("设计文件节点嵌套层级过深（超过 %d 层），可能存在循环引用。"), MaxTreeRecursionDepth);
			}
		}

		~FRecursionGuard()
		{
			if (bValid)
				--Depth;
		}

		int32& Depth;
		bool bValid = false;
	};

	const TCHAR* ToSizeModeLogString(EDesignSizeMode Mode)
	{
		switch (Mode)
		{
		case EDesignSizeMode::Fixed: return TEXT("fixed");
		case EDesignSizeMode::FillContainer: return TEXT("fill");
		case EDesignSizeMode::FitContent: return TEXT("fit");
		default: return TEXT("unknown");
		}
	}

	bool IsDesignerOnlyNodeType(EDesignNodeType Type)
	{
		return Type == EDesignNodeType::Connection
			|| Type == EDesignNodeType::Note
			|| Type == EDesignNodeType::Prompt
			|| Type == EDesignNodeType::Context;
	}

	bool HasVisibleFill(const FDesignFill& Fill)
	{
		return Fill.bEnabled && Fill.Type != EDesignFillType::None;
	}

	bool HasRenderableVisual(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
			return false;

		if (Node->Type == EDesignNodeType::Text || Node->Type == EDesignNodeType::IconFont)
			return true;

		if (HasVisibleFill(Node->Fill))
			return true;

		for (const FDesignFill& Fill : Node->Fills)
		{
			if (HasVisibleFill(Fill))
				return true;
		}

		if (Node->Stroke.HasStroke())
			return true;

		for (const FDesignEffect& Effect : Node->Effects)
		{
			if (Effect.bEnabled)
				return true;
		}

		return false;
	}

	FString GetSourceExtension(const TSharedPtr<FDesignNode>& Node, const FString& Key)
	{
		if (!Node.IsValid())
			return FString();
		if (const FString* Value = Node->Metadata.Find(Key))
			return *Value;
		return FString();
	}

	bool TryNormalizeValidFontAssetPath(const FString& InPath, FString& OutPath)
	{
		OutPath = InPath.TrimStartAndEnd();
		if (OutPath.IsEmpty())
		{
			return false;
		}

		if (LoadObject<UFont>(nullptr, *OutPath))
		{
			return true;
		}

		if (!OutPath.Contains(TEXT(".")))
		{
			const FString AssetName = FPackageName::GetLongPackageAssetName(OutPath);
			if (!AssetName.IsEmpty())
			{
				const FString CandidateObjectPath = OutPath + TEXT(".") + AssetName;
				if (LoadObject<UFont>(nullptr, *CandidateObjectPath))
				{
					OutPath = CandidateObjectPath;
					return true;
				}
			}
		}

		return false;
	}

	struct FChildSizeSnapshot
	{
		TSharedPtr<FDesignNode> Child;
		FDesignSize RawWidth;
		FDesignSize RawHeight;
		FDesignSize EffectiveWidth;
		FDesignSize EffectiveHeight;
		bool bUsedExpandedRef = false;
	};

	TSharedPtr<FDesignNode> ExpandRefForConversion(
		const TSharedPtr<FDesignNode>& Node,
		const FDesignDocument& Doc,
		const TMap<FString, FString>& ThemeContext)
	{
		if (!Node.IsValid() || Node->Type != EDesignNodeType::Ref)
		{
			return Node;
		}

		TSharedPtr<FDesignNode> Expanded = FRefExpander::ExpandRef(Node, Doc);
		if (!Expanded.IsValid())
		{
			return nullptr;
		}

		if (ThemeContext.Num() > 0)
		{
			FDesignVariableResolver Resolver(Doc.Variables);
			Resolver.SetThemeContext(ThemeContext);
			FRefExpander::ReThemeNode(Expanded, Resolver);
		}

		return Expanded;
	}

	FChildSizeSnapshot BuildChildSizeSnapshot(const TSharedPtr<FDesignNode>& Child, const FDesignDocument& Doc)
	{
		FChildSizeSnapshot Snapshot;
		Snapshot.Child = Child;
		if (!Child.IsValid())
			return Snapshot;

		Snapshot.RawWidth = Child->Width;
		Snapshot.RawHeight = Child->Height;
		Snapshot.EffectiveWidth = Child->Width;
		Snapshot.EffectiveHeight = Child->Height;

		if (Child->Type == EDesignNodeType::Ref)
		{
			if (auto Expanded = FRefExpander::ExpandRef(Child, Doc))
			{
				Snapshot.bUsedExpandedRef = true;
				if (!Child->bHasWidth)
					Snapshot.EffectiveWidth = Expanded->Width;
				if (!Child->bHasHeight)
					Snapshot.EffectiveHeight = Expanded->Height;
			}
		}
		return Snapshot;
	}

	bool ShouldSkipNode(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid() || !Node->bEnabled || IsDesignerOnlyNodeType(Node->Type))
			return true;

		// Ref 节点必须保留，后续展开后才有实际内容
		if (Node->Type == EDesignNodeType::Ref)
			return false;

		// Keep explicit flow containers (including empty GRID/WRAP/H/V frames) so layout semantics are not dropped.
		if (Node->Children.Num() == 0 && !HasRenderableVisual(Node))
		{
			const bool bIsExplicitFlowContainer =
				(Node->Type == EDesignNodeType::Frame || Node->Type == EDesignNodeType::Group)
				&& Node->Layout != EDesignLayout::None;
			if (!bIsExplicitFlowContainer)
				return true;
		}

		return false;
	}

	FSlateFontInfo BuildSlateFontInfoForSize(const TSharedPtr<FDesignNode>& Node, float FontSize);

	/** Use FSlateFontMeasure for precise text size; falls back to heuristic. */
	FVector2D MeasureTextSize(const FString& Text, float FontSize, const FString& FontFamily, const FString& FontWeight, int32 LetterSpacing = 0)
	{
		if (FSlateApplication::IsInitialized())
		{
			FSlateRenderer* Renderer = FSlateApplication::Get().GetRenderer();
			if (Renderer)
			{
				TSharedRef<FSlateFontMeasure> Measure = Renderer->GetFontMeasureService();
				TSharedPtr<FDesignNode> MeasureNode = MakeShared<FDesignNode>();
				MeasureNode->FontFamily = FontFamily;
				MeasureNode->FontWeight = FontWeight;
				MeasureNode->FontSize = FontSize;
				FSlateFontInfo FontInfo = BuildSlateFontInfoForSize(MeasureNode, FontSize);
				FontInfo.LetterSpacing = LetterSpacing;
				FVector2D Size = Measure->Measure(Text, FontInfo);
				if (Size.X > 0 && Size.Y > 0)
					return Size;
			}
		}
		// Fallback: heuristic
		float W = Text.Len() * FontSize * 0.6f;
		float H = FontSize * 1.5f;
		return FVector2D(W, H);
	}

	float ResolveWidthHint(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth)
	{
		if (!Node.IsValid())
		{
			return 0.0f;
		}

		if (Node->Width.IsFill())
		{
			if (Node->Width.Value > 0.0f)
			{
				return Node->Width.Value;
			}
			return ParentContentWidth;
		}

		if (Node->Width.Value > 0.0f)
		{
			return Node->Width.Value;
		}

		return 0.0f;
	}

	void GatherVisibleChildren(const TSharedPtr<FDesignNode>& Node, TArray<TSharedPtr<FDesignNode>>& OutVisibleChildren)
	{
		if (!Node.IsValid())
		{
			return;
		}

		for (const TSharedPtr<FDesignNode>& Child : Node->Children)
		{
			if (!ShouldSkipNode(Child))
			{
				OutVisibleChildren.Add(Child);
			}
		}
	}

	float ComputeChildContentWidthHint(const TSharedPtr<FDesignNode>& Node, const TSharedPtr<FDesignNode>& Child, float ParentContentWidth)
	{
		if (!Node.IsValid() || !Child.IsValid())
		{
			return 0.0f;
		}

		if (Node->Layout == EDesignLayout::Wrap || Node->Layout == EDesignLayout::Grid)
		{
			return 0.0f;
		}

		TArray<TSharedPtr<FDesignNode>> VisibleChildren;
		GatherVisibleChildren(Node, VisibleChildren);
		if (VisibleChildren.Num() <= 0)
		{
			return 0.0f;
		}

		const float ResolvedWidth = ResolveWidthHint(Node, ParentContentWidth);
		if (ResolvedWidth <= 0.0f)
		{
			return 0.0f;
		}

		const float InnerWidth = FMath::Max(0.0f, ResolvedWidth - Node->Padding.Left - Node->Padding.Right);
		if (InnerWidth <= 0.0f)
		{
			return 0.0f;
		}

		if (Node->Layout != EDesignLayout::Horizontal || VisibleChildren.Num() == 1)
		{
			return InnerWidth;
		}

		float RemainingWidth = InnerWidth - Node->Gap * (VisibleChildren.Num() - 1);
		int32 FillChildCount = 0;
		for (const TSharedPtr<FDesignNode>& VisibleChild : VisibleChildren)
		{
			if (VisibleChild->Width.IsFill())
			{
				++FillChildCount;
				continue;
			}

			RemainingWidth -= ResolveWidthHint(VisibleChild, 0.0f);
		}

		if (!Child->Width.IsFill() || FillChildCount <= 0 || RemainingWidth <= 0.0f)
		{
			return InnerWidth;
		}

		return RemainingWidth / FillChildCount;
	}

	float ResolveTextFontSizeForUMG(const TSharedPtr<FDesignNode>& Node);

	bool IsHexColorLiteral(const FString& ColorValue)
	{
		if (!ColorValue.StartsWith(TEXT("#")))
			return false;
		const int32 Len = ColorValue.Len();
		return Len == 4 || Len == 7 || Len == 9;
	}

	FString BuildHexColorWithAlpha(const FString& ColorValue, uint8 Alpha)
	{
		if (!IsHexColorLiteral(ColorValue))
			return FString();

		FColor Parsed = FColor::FromHex(ColorValue);
		Parsed.A = Alpha;
		return FString::Printf(TEXT("#%02X%02X%02X%02X"), Parsed.R, Parsed.G, Parsed.B, Parsed.A);
	}

	bool IsNeutralPlaceholderColor(const FString& ColorValue)
	{
		if (!IsHexColorLiteral(ColorValue))
			return true;

		const FColor Parsed = FColor::FromHex(ColorValue);
		const uint8 MaxChannel = FMath::Max3(Parsed.R, Parsed.G, Parsed.B);
		const uint8 MinChannel = FMath::Min3(Parsed.R, Parsed.G, Parsed.B);
		return (Parsed.A <= 0x20) || ((MaxChannel - MinChannel) <= 0x10);
	}

	bool HasNodeMetadataFlag(const TSharedPtr<FDesignNode>& Node, const TCHAR* Key)
	{
		const FString* Value = Node->Metadata.Find(Key);
		return Value && Value->Equals(TEXT("true"), ESearchCase::IgnoreCase);
	}

	bool TryApplyPlaceholderSurfaceTint(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node)
	{
		const bool bExpandedRefInstancePlaceholder = HasNodeMetadataFlag(Node, TEXT("design2umg.expanded_ref_instance"));
		const FString DefaultPlaceholderFillColor = bExpandedRefInstancePlaceholder ? TEXT("#9682FF33") : TEXT("#D282FF33");
		const FString DefaultPlaceholderOutlineColor = bExpandedRefInstancePlaceholder ? TEXT("#9580FF") : TEXT("#D480FF");
		const bool bIsLeafContainer =
			(Node->Type == EDesignNodeType::Frame
				|| Node->Type == EDesignNodeType::Group
				|| Node->Type == EDesignNodeType::Rectangle)
			&& Node->Children.Num() == 0;
		const bool bIsExplicitEmptySlotSurface =
			HasNodeMetadataFlag(Node, TEXT("pen.slot.empty")) || HasNodeMetadataFlag(Node, TEXT("figma.slot.empty"));
		const bool bUseSlotPlaceholderTint =
			bIsLeafContainer
			&& Node->Width.Value >= 24.0f
			&& Node->Height.Value >= 12.0f
			&& (Node->bSemanticPlaceholderSurface || bIsExplicitEmptySlotSurface);
		if (!bUseSlotPlaceholderTint)
			return false;

		const FString FillColor = IsHexColorLiteral(Node->Fill.SolidColor) ? Node->Fill.SolidColor : FString();
		const FString StrokeColor = IsHexColorLiteral(Node->Stroke.Fill.SolidColor) ? Node->Stroke.Fill.SolidColor : FString();
		const FString PlaceholderOutlineColor =
			(!StrokeColor.IsEmpty() && !IsNeutralPlaceholderColor(StrokeColor)) ? StrokeColor : DefaultPlaceholderOutlineColor;
		FString PlaceholderFillColor =
			(!FillColor.IsEmpty() && !IsNeutralPlaceholderColor(FillColor)) ? FillColor : DefaultPlaceholderFillColor;
		if (PlaceholderFillColor.IsEmpty() && !PlaceholderOutlineColor.IsEmpty())
			PlaceholderFillColor = BuildHexColorWithAlpha(PlaceholderOutlineColor, 0x33);
		if (PlaceholderFillColor.IsEmpty() || PlaceholderOutlineColor.IsEmpty())
			return false;

		UE_LOG(LogDesign2UMG, Verbose, TEXT("ConvertNode: applying placeholder surface tint to '%s'"), *Node->Name);
		Widget.Type = TEXT("Image");
		Widget.Children.Empty();
		const int32 InsetPx = FMath::Clamp(
			FMath::RoundToInt(FMath::Min3(10.0f, Node->Width.Value * 0.25f, Node->Height.Value * 0.25f)),
			1,
			10);
		Widget.Brush = FUMGVisualBrushData();
		for (const TCHAR* Key : {TEXT("BrushColor"), TEXT("TexturePath"), TEXT("DrawAs"), TEXT("OutlineThickness"), TEXT("OutlineColor"), TEXT("StrokePadding"), TEXT("StrokeMargin")})
			Widget.Properties.Remove(Key);
		Widget.Properties.Add(TEXT("MaterialRef"), TEXT("placeholdersurface"));
		Widget.Properties.Add(TEXT("MaterialParams"),
			FString::Printf(TEXT("{\"Color\":\"%s\",\"StripeColor\":\"%s\",\"StripeWidth\":1,\"Inset\":%d}"),
				*PlaceholderFillColor, *PlaceholderOutlineColor, InsetPx));
		if (Node->Width.Value > 0.0f)
			Widget.Properties.Add(TEXT("WidthOverride"), FString::SanitizeFloat(Node->Width.Value));
		if (Node->Height.Value > 0.0f)
			Widget.Properties.Add(TEXT("HeightOverride"), FString::SanitizeFloat(Node->Height.Value));
		return true;
	}

	bool ShouldWrapNodeWithBorder(const TSharedPtr<FDesignNode>& Node, bool bPlaceholderUsesDirectImage)
	{
		const bool bHasVisualFill = (Node->Fill.bEnabled && Node->Fill.Type != EDesignFillType::None) || Node->Fills.Num() > 0;
		const bool bHasCornerRadius = Node->CornerRadius.bUniform ? Node->CornerRadius.Uniform > 0
			: (Node->CornerRadius.TopLeft > 0 || Node->CornerRadius.TopRight > 0
				|| Node->CornerRadius.BottomRight > 0 || Node->CornerRadius.BottomLeft > 0);
		return (Node->Type == EDesignNodeType::Frame || Node->Type == EDesignNodeType::Group)
			&& (bHasVisualFill
				|| (Node->Layout == EDesignLayout::None && FLayoutMapper::HasPadding(Node))
				|| bHasCornerRadius
				|| Node->Stroke.HasStroke())
			&& !bPlaceholderUsesDirectImage;
	}

	FUMGWidgetNode ApplyVisualEffectWrappers(FUMGWidgetNode Widget, const TSharedPtr<FDesignNode>& Node)
	{
		if (Node->HasMultiFill())
			Widget = FVisualMapper::WrapWithMultiFill(MoveTemp(Widget), Node);

		if (const FString* ShadowCountStr = Widget.Properties.Find(TEXT("BoxShadowCount")))
		{
			const int32 ShadowCount = FCString::Atoi(**ShadowCountStr);
			Widget.Properties.Remove(TEXT("BoxShadowCount"));
			for (int32 Index = 0; Index < ShadowCount; ++Index)
				Widget = FVisualMapper::WrapWithBoxShadow(MoveTemp(Widget), Node, Index);
		}

		for (const FDesignEffect& Effect : Node->Effects)
		{
			if (Effect.bEnabled && Effect.Type == EDesignEffectType::BackgroundBlur && Effect.Radius > 0)
				return FVisualMapper::WrapWithBackgroundBlur(MoveTemp(Widget), Effect.Radius);
		}

		return Widget;
	}

	bool ShouldUseSingleLineTextHeight(const TSharedPtr<FDesignNode>& Node, float UnwrappedWidth, float ParentContentWidth)
	{
		if (!Node.IsValid())
		{
			return false;
		}

		if (Node->Content.Contains(TEXT("\n")))
		{
			return false;
		}

		const bool bWrappingMode =
			Node->TextGrowth == EDesignTextGrowth::FixedWidth
			|| Node->TextGrowth == EDesignTextGrowth::FixedWidthHeight
			|| Node->TextAlign == EDesignTextAlign::Justify;
		if (!bWrappingMode)
		{
			return true;
		}

		float WidthLimit = ResolveWidthHint(Node, ParentContentWidth);
		if (WidthLimit <= 0.0f)
		{
			return false;
		}

		const float AppliedFontSize = ResolveTextFontSizeForUMG(Node);
		const float HeuristicFontSize = AppliedFontSize > 0.0f ? AppliedFontSize : Node->FontSize;
		const float HeuristicWidth = Node->Content.Len() * HeuristicFontSize * 0.55f;
		const float EffectiveWidth = UnwrappedWidth > 0.0f ? UnwrappedWidth : HeuristicWidth;
		return EffectiveWidth <= WidthLimit + 0.5f;
	}

	float EstimateSingleLineTextHeight(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid() || Node->FontSize <= 0.0f)
		{
			return 0.0f;
		}

		// Layout sizing stays in design-space px. FontSize written into Slate is scaled for
		// raster fidelity, but fit-content heights must still match the design's px line box.
		const float BaseFontSize = Node->FontSize;
		const float LineHeightMultiplier = Node->LineHeight > 0.0f
			? Node->LineHeight
			: 1.285714f;
		return FMath::Max(BaseFontSize, BaseFontSize * LineHeightMultiplier);
	}

	int32 ConvertLetterSpacingPxToTracking(float PixelSpacing, float DesignFontSize, float AppliedFontSize)
	{
		const float BaseFontSize = DesignFontSize > 0.0f ? DesignFontSize : AppliedFontSize;
		if (BaseFontSize <= 0.0f)
		{
			return 0;
		}

		return FMath::RoundToInt(PixelSpacing / BaseFontSize * 1000.0f);
	}

	ETextJustify::Type ToSlateJustification(EDesignTextAlign TextAlign)
	{
		switch (TextAlign)
		{
		case EDesignTextAlign::Center:
			return ETextJustify::Center;
		case EDesignTextAlign::Right:
			return ETextJustify::Right;
		default:
			return ETextJustify::Left;
		}
	}

	FString NormalizeFontFamilyKey(const FString& FontFamily)
	{
		FString Normalized = FontFamily.ToLower();
		Normalized.ReplaceInline(TEXT(" "), TEXT(""));
		Normalized.ReplaceInline(TEXT("-"), TEXT(""));
		Normalized.ReplaceInline(TEXT("_"), TEXT(""));
		return Normalized;
	}

	FString ResolveTypefaceName(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return FString();
		}

		FString Typeface = FFontResolver::FindBestTypeface(Node->FontFamily, Node->FontWeight);
		if (Typeface.IsEmpty() && !Node->FontWeight.IsEmpty())
		{
			Typeface = FDesignTypeMapper::MapFontWeight(Node->FontWeight).ToString();
		}
		return Typeface;
	}

	float ResolveTextFontSizeFallback(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return 0.0f;
		}

		const FString NormalizedFamily = NormalizeFontFamilyKey(Node->FontFamily);
		if (NormalizedFamily == TEXT("funneldisplay"))
		{
			return Node->FontSize * 0.75f;
		}
		if (NormalizedFamily == TEXT("rubik"))
		{
			return Node->FontSize * 0.70f;
		}
		if (NormalizedFamily == TEXT("geist"))
		{
			return Node->FontSize * 0.71f;
		}

		// Design tools report text sizes in px while Slate font sizes are effectively pt-based.
		// Map px -> pt globally so compiled widget renders match the exported design raster.
		return Node->FontSize * 0.75f;
	}

	FString BuildTextFontCalibrationKey(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return FString();
		}

		const FString LineHeightKey = Node->LineHeight > 0.0f
			? FString::Printf(TEXT("%.4f"), Node->LineHeight)
			: TEXT("default");
		return FString::Printf(
			TEXT("%s|%s|%s|%.1f"),
			*NormalizeFontFamilyKey(Node->FontFamily),
			*ResolveTypefaceName(Node),
			*LineHeightKey,
			Node->FontSize);
	}

	bool IsWrappingTextMode(const TSharedPtr<FDesignNode>& Node)
	{
		return Node.IsValid()
			&& (Node->TextGrowth == EDesignTextGrowth::FixedWidth
				|| Node->TextGrowth == EDesignTextGrowth::FixedWidthHeight
				|| Node->TextAlign == EDesignTextAlign::Justify);
	}

	bool ShouldApplyExplicitLineHeight(const TSharedPtr<FDesignNode>& Node, float ProbeFontSize, float ParentContentWidth = 0.0f)
	{
		if (!Node.IsValid() || Node->LineHeight <= 0.0f)
		{
			return false;
		}

		if (Node->Content.Contains(TEXT("\n")))
		{
			return true;
		}

		const bool bLargeSingleLineText =
			Node->FontSize >= 24.0f
			&& !IsWrappingTextMode(Node);
		if (bLargeSingleLineText)
		{
			return true;
		}

		if (!IsWrappingTextMode(Node))
		{
			return false;
		}

		const float WidthLimit = ResolveWidthHint(Node, ParentContentWidth);
		if (WidthLimit <= 0.0f)
		{
			return false;
		}

		const float EffectiveProbeFontSize = ProbeFontSize > 0.0f ? ProbeFontSize : Node->FontSize;
		const int32 LetterSpacingTracking = (EffectiveProbeFontSize > 0.0f && Node->LetterSpacing != 0.0f)
			? ConvertLetterSpacingPxToTracking(Node->LetterSpacing, Node->FontSize, EffectiveProbeFontSize)
			: 0;
		const FVector2D UnwrappedSize = MeasureTextSize(
			Node->Content,
			EffectiveProbeFontSize,
			Node->FontFamily,
			Node->FontWeight,
			LetterSpacingTracking);
		return UnwrappedSize.X > WidthLimit + 0.5f;
	}

	FSlateFontInfo BuildSlateFontInfoForSize(const TSharedPtr<FDesignNode>& Node, float FontSize)
	{
		FSlateFontInfo FontInfo = FTextBlockStyle::GetDefault().Font;
		if (!Node.IsValid())
		{
			return FontInfo;
		}

		if (FontSize > 0.0f)
		{
			FontInfo.Size = FMath::Max(1, FMath::RoundToInt(FontSize));
		}

		if (!Node->FontFamily.IsEmpty())
		{
			const FString AssetPath = FFontResolver::FindFontAssetPath(Node->FontFamily);
			if (!AssetPath.IsEmpty())
			{
				if (UObject* FontObject = LoadObject<UObject>(nullptr, *AssetPath))
				{
					FontInfo.FontObject = FontObject;
				}
			}
		}

		const FString Typeface = ResolveTypefaceName(Node);
		if (!Typeface.IsEmpty())
		{
			FontInfo.TypefaceFontName = FName(*Typeface);
		}

		if (Node->LetterSpacing != 0.0f && FontInfo.Size > 0)
		{
			FontInfo.LetterSpacing = ConvertLetterSpacingPxToTracking(Node->LetterSpacing, Node->FontSize, FontInfo.Size);
		}

		return FontInfo;
	}

	FVector2D MeasureUMGTextBlockDesiredSize(
		const TSharedPtr<FDesignNode>& Node,
		float FontSize,
		const FString& Text,
		float WrapTextAt = 0.0f,
		bool bAutoWrapText = false)
	{
		if (!Node.IsValid() || FontSize <= 0.0f || !FSlateApplication::IsInitialized())
		{
			return FVector2D::ZeroVector;
		}

		FSlateRenderer* Renderer = FSlateApplication::Get().GetRenderer();
		if (!Renderer)
		{
			return FVector2D::ZeroVector;
		}

		UTextBlock* ProbeTextBlock = NewObject<UTextBlock>();
		if (!ProbeTextBlock)
		{
			return FVector2D::ZeroVector;
		}

		ProbeTextBlock->SetText(FText::FromString(Text));
		ProbeTextBlock->SetFont(BuildSlateFontInfoForSize(Node, FontSize));
		ProbeTextBlock->SetJustification(ToSlateJustification(Node->TextAlign));
		const bool bApplyExplicitLineHeight = ShouldApplyExplicitLineHeight(Node, FontSize, WrapTextAt);
		ProbeTextBlock->SetLineHeightPercentage(bApplyExplicitLineHeight ? Node->LineHeight : 1.0f);
		ProbeTextBlock->SetApplyLineHeightToBottomLine(bApplyExplicitLineHeight);
		ProbeTextBlock->SetAutoWrapText(bAutoWrapText);
		ProbeTextBlock->SetWrapTextAt(WrapTextAt);

		TSharedRef<SWidget> SlateWidget = ProbeTextBlock->TakeWidget();
		SlateWidget->SlatePrepass(1.0f);
		SlateWidget->SlatePrepass(1.0f);
		return SlateWidget->GetDesiredSize();
	}

	float MeasureSingleLineTextBlockHeight(const TSharedPtr<FDesignNode>& Node, float FontSize)
	{
		return MeasureUMGTextBlockDesiredSize(Node, FontSize, TEXT("Hg")).Y;
	}

	float ResolveTextFontSizeForUMG(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return 0.0f;
		}

		const float FallbackFontSize = ResolveTextFontSizeFallback(Node);
		if (Node->FontSize <= 0.0f || !FSlateApplication::IsInitialized())
		{
			return FallbackFontSize;
		}

		const float TargetLineBoxHeight = EstimateSingleLineTextHeight(Node);
		if (TargetLineBoxHeight <= 0.0f)
		{
			return FallbackFontSize;
		}

		if (!ShouldApplyExplicitLineHeight(Node, FallbackFontSize))
		{
			const float TargetTextWidth = (!Node->Width.IsFill() && Node->Width.Value > 0.0f && !Node->Content.Contains(TEXT("\n")))
				? Node->Width.Value
				: 0.0f;
			if (TargetTextWidth <= 0.0f || !FSlateApplication::IsInitialized())
			{
				return FallbackFontSize;
			}

			auto MeasureWidth = [&](float CandidateFontSize) -> float
			{
				return MeasureUMGTextBlockDesiredSize(Node, CandidateFontSize, Node->Content, 0.0f, false).X;
			};

			const int32 MaxProbeFontSize = FMath::Max(
				1,
				FMath::CeilToInt(FMath::Max(Node->FontSize * 1.5f, FallbackFontSize * 2.0f)));
			int32 BestFontSize = FMath::Max(1, FMath::RoundToInt(FallbackFontSize));
			float BestError = TNumericLimits<float>::Max();

			for (int32 CandidateFontSize = 1; CandidateFontSize <= MaxProbeFontSize; ++CandidateFontSize)
			{
				const float CandidateWidth = MeasureWidth(static_cast<float>(CandidateFontSize));
				if (CandidateWidth <= 0.0f)
				{
					continue;
				}

				const float Error = FMath::Abs(CandidateWidth - TargetTextWidth);
				const float DistanceToFallback = FMath::Abs(static_cast<float>(CandidateFontSize) - FallbackFontSize);
				const float BestDistanceToFallback = FMath::Abs(static_cast<float>(BestFontSize) - FallbackFontSize);
				if (Error + KINDA_SMALL_NUMBER < BestError
					|| (FMath::IsNearlyEqual(Error, BestError, 0.01f) && DistanceToFallback < BestDistanceToFallback))
				{
					BestFontSize = CandidateFontSize;
					BestError = Error;
				}
			}

			if (BestError == TNumericLimits<float>::Max())
			{
				return FallbackFontSize;
			}

			UE_LOG(
				LogDesign2UMG,
				Verbose,
				TEXT("TextFontWidthCalibration: text='%s' designSize=%.1f targetWidth=%.1f fallback=%.2f resolved=%.2f"),
				*Node->Content,
				Node->FontSize,
				TargetTextWidth,
				FallbackFontSize,
				static_cast<float>(BestFontSize));
			return static_cast<float>(BestFontSize);
		}

		const FString CalibrationKey = BuildTextFontCalibrationKey(Node);
		if (const float* CachedScale = GResolvedTextFontScaleCache.Find(CalibrationKey))
		{
			return FMath::Max(1.0f, Node->FontSize * (*CachedScale));
		}

		auto MeasureHeight = [&](float CandidateFontSize) -> float
		{
			return MeasureSingleLineTextBlockHeight(Node, CandidateFontSize);
		};

		const int32 MaxProbeFontSize = FMath::Max(
			1,
			FMath::CeilToInt(FMath::Max(Node->FontSize * 1.5f, FallbackFontSize * 2.0f)));
		int32 BestFontSize = FMath::Max(1, FMath::RoundToInt(FallbackFontSize));
		float BestError = TNumericLimits<float>::Max();

		for (int32 CandidateFontSize = 1; CandidateFontSize <= MaxProbeFontSize; ++CandidateFontSize)
		{
			const float CandidateHeight = MeasureHeight(static_cast<float>(CandidateFontSize));
			if (CandidateHeight <= 0.0f)
			{
				continue;
			}

			const float Error = FMath::Abs(CandidateHeight - TargetLineBoxHeight);
			const float DistanceToFallback = FMath::Abs(static_cast<float>(CandidateFontSize) - FallbackFontSize);
			const float BestDistanceToFallback = FMath::Abs(static_cast<float>(BestFontSize) - FallbackFontSize);
			if (Error + KINDA_SMALL_NUMBER < BestError
				|| (FMath::IsNearlyEqual(Error, BestError, 0.01f) && DistanceToFallback < BestDistanceToFallback))
			{
				BestFontSize = CandidateFontSize;
				BestError = Error;
			}
		}

		if (BestError == TNumericLimits<float>::Max())
		{
			BestFontSize = FMath::Max(1, FMath::RoundToInt(FallbackFontSize));
			BestError = 0.0f;
		}

		const float ResolvedScale = static_cast<float>(BestFontSize) / Node->FontSize;
		GResolvedTextFontScaleCache.Add(CalibrationKey, ResolvedScale);
		UE_LOG(
			LogDesign2UMG,
			Verbose,
			TEXT("TextFontCalibration: key='%s' designSize=%.1f targetLineBox=%.1f fallback=%.2f resolved=%.2f scale=%.4f"),
			*CalibrationKey,
			Node->FontSize,
			TargetLineBoxHeight,
			FallbackFontSize,
			static_cast<float>(BestFontSize),
			ResolvedScale);
		return static_cast<float>(BestFontSize);
	}

	FSlateFontInfo BuildSlateFontInfo(const TSharedPtr<FDesignNode>& Node)
	{
		return BuildSlateFontInfoForSize(Node, ResolveTextFontSizeForUMG(Node));
	}

	float ResolveTextWrapAt(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth)
	{
		if (!Node.IsValid())
		{
			return 0.0f;
		}

		const float WidthLimit = ResolveWidthHint(Node, ParentContentWidth);
		if (WidthLimit <= 0.0f)
		{
			return 0.0f;
		}

		const bool bNeedsExplicitWrap =
			Node->TextGrowth == EDesignTextGrowth::FixedWidth
			|| Node->TextGrowth == EDesignTextGrowth::FixedWidthHeight
			|| Node->TextAlign == EDesignTextAlign::Justify
			|| Node->Content.Contains(TEXT("\n"));
		return bNeedsExplicitWrap ? WidthLimit : 0.0f;
	}

bool ShouldAutoWrapText(const TSharedPtr<FDesignNode>& Node)
	{
		return IsWrappingTextMode(Node);
	}

	FVector2D MeasureTextNodeSize(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth)
	{
		if (!Node.IsValid())
		{
			return FVector2D::ZeroVector;
		}

		const float AppliedFontSize = ResolveTextFontSizeForUMG(Node);
		const int32 LetterSpacingTracking = (AppliedFontSize > 0.0f && Node->LetterSpacing != 0.0f)
			? ConvertLetterSpacingPxToTracking(Node->LetterSpacing, Node->FontSize, AppliedFontSize)
			: 0;
		FVector2D FallbackSize = MeasureTextSize(
			Node->Content,
			AppliedFontSize > 0.0f ? AppliedFontSize : Node->FontSize,
			Node->FontFamily,
			Node->FontWeight,
			LetterSpacingTracking);

		if (ShouldUseSingleLineTextHeight(Node, FallbackSize.X, ParentContentWidth))
		{
			FallbackSize.Y = EstimateSingleLineTextHeight(Node);
			return FallbackSize;
		}

		const float WrapTextAt = ResolveTextWrapAt(Node, ParentContentWidth);

		if (WrapTextAt <= 0.0f && !Node->Content.Contains(TEXT("\n")))
		{
			return FallbackSize;
		}

		if (FSlateApplication::IsInitialized())
		{
			FSlateRenderer* Renderer = FSlateApplication::Get().GetRenderer();
			if (Renderer)
			{
				const FSlateFontInfo FontInfo = BuildSlateFontInfo(Node);

				const FVector2D Size = MeasureUMGTextBlockDesiredSize(
					Node,
					FontInfo.Size,
					Node->Content,
					WrapTextAt,
					ShouldAutoWrapText(Node));
				if (Size.X > 0.0f && Size.Y > 0.0f)
				{
					return Size;
				}
			}
		}

		if (WrapTextAt > 0.0f && FallbackSize.X > WrapTextAt + 0.5f)
		{
			const float LineHeight = EstimateSingleLineTextHeight(Node);
			const int32 EstimatedLineCount = FMath::Max(1, FMath::CeilToInt(FallbackSize.X / FMath::Max(1.0f, WrapTextAt)));
			FallbackSize.X = WrapTextAt;
			FallbackSize.Y = LineHeight * EstimatedLineCount;
		}

		return FallbackSize;
	}

	/** Estimate a single node's size, handling leaf nodes (text, icon) via font metrics. */
	FVector2D EstimateNodeSize(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth = 0.0f);

	FVector2D EstimateContentSize(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth = 0.0f);

	FVector2D EstimateNodeSize(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth)
	{
		FRecursionGuard Guard(GEstimateDepth, TEXT("EstimateNodeSize"));
		if (!Guard.bValid)
			return FVector2D::ZeroVector;

		float W = Node->Width.Value;
		float H = Node->Height.Value;
		if (Node->Width.IsFill())
		{
			const float FillWidth = ResolveWidthHint(Node, ParentContentWidth);
			if (FillWidth > 0.0f)
			{
				W = FMath::Max(W, FillWidth);
			}
		}

		// Text leaf: fit_content(N) uses N as a minimum, so measured text may grow past it.
		if (Node->Type == EDesignNodeType::Text)
		{
			if ((W == 0 || H == 0 || Node->Width.IsFit() || Node->Height.IsFit())
				&& Node->FontSize > 0
				&& !Node->Content.IsEmpty())
			{
				FVector2D Measured = MeasureTextNodeSize(Node, ParentContentWidth);
				if (W == 0) W = Measured.X;
				else if (Node->Width.IsFit()) W = FMath::Max(W, Measured.X);
				if (H == 0) H = Measured.Y;
				else if (Node->Height.IsFit()) H = FMath::Max(H, Measured.Y);
			}
			else
			{
				if (H == 0 && Node->FontSize > 0)
					H = Node->FontSize * 1.5f;
			}
			return FVector2D(W, H);
		}

		// IconFont leaf: square based on FontSize
		if (Node->Type == EDesignNodeType::IconFont)
		{
			const float S = Node->FontSize > 0 ? Node->FontSize : 16.0f;
			if (W == 0) W = S;
			else if (Node->Width.IsFit()) W = FMath::Max(W, S);
			if (H == 0) H = S;
			else if (Node->Height.IsFit()) H = FMath::Max(H, S);
			return FVector2D(W, H);
		}

		// Container with children: recurse. fit_content(N) keeps N as a minimum.
		if (Node->Children.Num() > 0)
		{
			const FVector2D Sub(EstimateContentSize(Node, ParentContentWidth));
			if (W == 0) W = Sub.X;
			else if (Node->Width.IsFit()) W = FMath::Max(W, Sub.X);
			if (H == 0) H = Sub.Y;
			else if (Node->Height.IsFit()) H = FMath::Max(H, Sub.Y);
		}

		return FVector2D(W, H);
	}

	/**
	 * Estimate content size for a FitContent node by examining its children.
	 * Returns (width, height) based on layout mode and children's known sizes.
	 */
	FVector2D EstimateContentSize(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth)
	{
		FRecursionGuard Guard(GEstimateDepth, TEXT("EstimateContentSize"));
		if (!Guard.bValid)
			return FVector2D::ZeroVector;

		float W = 0.0f, H = 0.0f;
		const float PadH = Node->Padding.Left + Node->Padding.Right;
		const float PadV = Node->Padding.Top + Node->Padding.Bottom;

		TArray<TSharedPtr<FDesignNode>> Visible;
		GatherVisibleChildren(Node, Visible);

		if (Visible.Num() == 0)
			return FVector2D(PadH, PadV);

		const float TotalGap = Visible.Num() > 1 ? Node->Gap * (Visible.Num() - 1) : 0.0f;

		if (Node->Layout == EDesignLayout::Horizontal)
		{
			for (const auto& C : Visible)
			{
				const float ChildContentWidth = ComputeChildContentWidthHint(Node, C, ParentContentWidth);
				const FVector2D CS(EstimateNodeSize(C, ChildContentWidth));
				W += CS.X;
				H = FMath::Max(H, CS.Y);
			}
			W += TotalGap;
		}
		else if (Node->Layout == EDesignLayout::Vertical)
		{
			for (const auto& C : Visible)
			{
				const float ChildContentWidth = ComputeChildContentWidthHint(Node, C, ParentContentWidth);
				const FVector2D CS(EstimateNodeSize(C, ChildContentWidth));
				W = FMath::Max(W, CS.X);
				H += CS.Y;
			}
			H += TotalGap;
		}
		else
		{
			// Absolute positioning: max extent
			for (const auto& C : Visible)
			{
				const float ChildContentWidth = ComputeChildContentWidthHint(Node, C, ParentContentWidth);
				const FVector2D CS(EstimateNodeSize(C, ChildContentWidth));
				W = FMath::Max(W, C->X + CS.X);
				H = FMath::Max(H, C->Y + CS.Y);
			}
		}

		return FVector2D(W + PadH, H + PadV);
	}

	FVector2D PreComputeFitContentSizesImpl(const TSharedPtr<FDesignNode>& Node, const FDesignDocument& Doc, float ParentContentWidth)
	{
		FRecursionGuard Guard(GPrecomputeDepth, TEXT("PreComputeFitContentSizes"));
		if (!Guard.bValid)
			return FVector2D::ZeroVector;

		if (!Node.IsValid() || !Node->bEnabled)
		{
			return FVector2D(0.0, 0.0);
		}

		// Ref 节点：展开后递归，回写尺寸到原始节点
		if (Node->Type == EDesignNodeType::Ref)
		{
			auto Expanded = FRefExpander::ExpandRef(Node, Doc);
			if (Expanded.IsValid())
			{
				const FVector2D RefSize(PreComputeFitContentSizesImpl(Expanded, Doc, ParentContentWidth));
				if (Node->Width.IsFit() && RefSize.X > Node->Width.Value && !Expanded->Width.IsFill())
					Node->Width.Value = static_cast<float>(RefSize.X);
				if (Node->Height.IsFit() && RefSize.Y > Node->Height.Value && !Expanded->Height.IsFill())
					Node->Height.Value = static_cast<float>(RefSize.Y);
				return RefSize;
			}
			return FVector2D(0.0, 0.0);
		}

		// Text 叶子节点：用 FontSize 估算
		if (Node->Type == EDesignNodeType::Text)
		{
			float W = Node->Width.Value;
			float H = Node->Height.Value;
			if (Node->Width.IsFill())
			{
				const float FillWidth = ResolveWidthHint(Node, ParentContentWidth);
				if (FillWidth > 0.0f)
				{
					W = FMath::Max(W, FillWidth);
				}
			}
			const bool bNeedWidthEstimate = !Node->Width.IsFixed() && W <= 0.0f;
			const bool bNeedHeightEstimate = !Node->Height.IsFixed() && H <= 0.0f;
			if ((bNeedWidthEstimate || bNeedHeightEstimate || Node->Width.IsFit() || Node->Height.IsFit())
				&& Node->FontSize > 0
				&& !Node->Content.IsEmpty())
			{
				FVector2D Measured = MeasureTextNodeSize(Node, ParentContentWidth);
				if (bNeedWidthEstimate) W = Measured.X;
				else if (Node->Width.IsFit()) W = FMath::Max(W, Measured.X);
				if (bNeedHeightEstimate) H = Measured.Y;
				else if (Node->Height.IsFit()) H = FMath::Max(H, Measured.Y);
			}
			else
			{
				if (bNeedHeightEstimate && Node->FontSize > 0)
					H = Node->FontSize * 1.5f;
			}
			// fit_content(N) 中的 N 是最小值，不是固定值。
			if (W > Node->Width.Value && Node->Width.IsFit())
				Node->Width.Value = W;
			if (H > Node->Height.Value && Node->Height.IsFit())
				Node->Height.Value = H;
			return FVector2D(W, H);
		}

		// IconFont 叶子节点
		if (Node->Type == EDesignNodeType::IconFont)
		{
			const float S = Node->FontSize > 0 ? Node->FontSize : 16.0f;
			float W = Node->Width.Value;
			float H = Node->Height.Value;
			if (!Node->Width.IsFixed() && W <= 0.0f)
				W = S;
			else if (Node->Width.IsFit())
				W = FMath::Max(W, S);
			if (!Node->Height.IsFixed() && H <= 0.0f)
				H = S;
			else if (Node->Height.IsFit())
				H = FMath::Max(H, S);

			// fit_content(N) 中的 N 是最小值，不是固定值。
			if (W > Node->Width.Value && Node->Width.IsFit())
				Node->Width.Value = W;
			if (H > Node->Height.Value && Node->Height.IsFit())
				Node->Height.Value = H;
			return FVector2D(W, H);
		}

		// 矢量叶子节点（Path/Line/Ellipse/Polygon）：亚像素钳制到 1px 并回写
		// CreatePathWidget 用 Max(1, RoundToInt) 算 WidthOverride，这里对齐
		const bool bIsVectorLeaf = Node->Type == EDesignNodeType::Path
			|| Node->Type == EDesignNodeType::Line
			|| Node->Type == EDesignNodeType::Ellipse
			|| Node->Type == EDesignNodeType::Polygon;
		if (bIsVectorLeaf)
		{
			float W = FMath::Max(1.0f, FMath::RoundToFloat(Node->Width.Value));
			float H = FMath::Max(1.0f, FMath::RoundToFloat(Node->Height.Value));
			if (!Node->Width.IsFixed() || Node->Width.Value < 1.0f)
				Node->Width.Value = W;
			if (!Node->Height.IsFixed() || Node->Height.Value < 1.0f)
				Node->Height.Value = H;
			return FVector2D(W, H);
		}

		// 其他非容器叶子节点
		if (!Node->IsContainer() && Node->Type != EDesignNodeType::Rectangle)
		{
			return FVector2D(
				Node->Width.IsFixed() ? Node->Width.Value : 0.0f,
				Node->Height.IsFixed() ? Node->Height.Value : 0.0f);
		}

		// 容器节点：先递归子节点
		for (const auto& Child : Node->Children)
		{
			const float ChildContentWidth = ComputeChildContentWidthHint(Node, Child, ParentContentWidth);
			(void)PreComputeFitContentSizesImpl(Child, Doc, ChildContentWidth);
		}

		// fit_content(N) 中的 N 是最小值，不是固定值。
		const bool bNeedW = Node->Width.IsFit();
		const bool bNeedH = Node->Height.IsFit();

		float FinalW = Node->Width.Value;
		float FinalH = Node->Height.Value;

		if (bNeedW || bNeedH)
		{
			const FVector2D Est(EstimateContentSize(Node, ParentContentWidth));
			if (bNeedW && Est.X > Node->Width.Value)
			{
				Node->Width.Value = static_cast<float>(Est.X);
				FinalW = static_cast<float>(Est.X);
				UE_LOG(LogDesign2UMG, Log, TEXT("PreCompute: '%s' width fit→%.0f"), *Node->Name, Est.X);
			}
			if (bNeedH && Est.Y > Node->Height.Value)
			{
				Node->Height.Value = static_cast<float>(Est.Y);
				FinalH = static_cast<float>(Est.Y);
				UE_LOG(LogDesign2UMG, Log, TEXT("PreCompute: '%s' height fit→%.0f"), *Node->Name, Est.Y);
			}
		}

		return FVector2D(FinalW, FinalH);
	}
}

FUMGWidgetData FDesign2UMGConverter::Convert(const FDesignDocument& Document)
{
	FUMGWidgetData Result;

	// 深拷贝 Children，避免通过 TSharedPtr 修改调用者的原始数据
	TArray<TSharedPtr<FDesignNode>> Children;
	Children.Reserve(Document.Children.Num());
	for (const auto& Child : Document.Children)
		Children.Add(FRefExpander::DeepCopyNode(Child));

	TArray<TSharedPtr<FDesignNode>> VisibleChildren;
	VisibleChildren.Reserve(Children.Num());
	for (const auto& Child : Children)
	{
		if (!ShouldSkipNode(Child))
		{
			VisibleChildren.Add(Child);
		}
	}

	// WidgetName 使用第一个非跳过 frame 的名称，fallback 到 WBP_FromPen
	for (const auto& Child : VisibleChildren)
	{
		if (!Child->bReusable && !Child->Name.IsEmpty())
		{
			Result.WidgetName = FString::Printf(TEXT("WBP_%s"), *Child->Name);
			break;
		}
	}
	if (Result.WidgetName.IsEmpty())
		Result.WidgetName = TEXT("WBP_FromPen");

	// 预计算 FitContent 节点的实际尺寸（从子节点递归推算）
	for (const auto& Child : VisibleChildren)
	{
		PreComputeFitContentSizes(Child, Document);
	}

	TArray<FUMGWidgetNode> RenderWidgets;
	for (const auto& Child : VisibleChildren)
	{
		if (Child->bReusable)
			continue;

		Child->X = 0.0f;
		Child->Y = 0.0f;

		FUMGWidgetNode ChildWidget = ConvertNode(Child, Document, TMap<FString, FString>(), &Result.Warnings);
		if (!ChildWidget.Type.IsEmpty())
			RenderWidgets.Add(MoveTemp(ChildWidget));
	}

	if (RenderWidgets.Num() == 1)
	{
		UE_LOG(LogDesign2UMG, Log, TEXT("Convert: single root '%s' type=%s"), *RenderWidgets[0].Name, *RenderWidgets[0].Type);
		Result.RootWidget = MoveTemp(RenderWidgets[0]);
		return Result;
	}

	UE_LOG(LogDesign2UMG, Log, TEXT("Convert: %d top-level widgets → CanvasPanel root"), RenderWidgets.Num());
	Result.RootWidget.Type = TEXT("CanvasPanel");
	Result.RootWidget.Name = TEXT("Root");
	Result.RootWidget.Id = TEXT("Root");
	for (auto& W : RenderWidgets)
		Result.RootWidget.Children.Add(MoveTemp(W));

	return Result;
}

FUMGWidgetNode FDesign2UMGConverter::ConvertNode(const TSharedPtr<FDesignNode>& Node, const FDesignDocument& Doc, const TMap<FString, FString>& ThemeContext, TArray<FString>* OutWarnings, EDesignLayout ParentLayout)
{
	FRecursionGuard Guard(GConvertNodeDepth, TEXT("ConvertNode"));
	if (!Guard.bValid)
		return FUMGWidgetNode();

	if (ShouldSkipNode(Node))
		return FUMGWidgetNode();

	TMap<FString, FString> ActiveTheme = ThemeContext;
	for (const auto& Pair : Node->Theme)
		ActiveTheme.Add(Pair.Key, Pair.Value);

	if (Node->Type == EDesignNodeType::Ref)
	{
		auto Expanded = ExpandRefForConversion(Node, Doc, ActiveTheme);
		if (!Expanded.IsValid())
			return FUMGWidgetNode();
		PreComputeFitContentSizes(Expanded, Doc);
		return ConvertNode(Expanded, Doc, ActiveTheme, OutWarnings, ParentLayout);
	}

	FUMGWidgetNode Widget;

	switch (Node->Type)
	{
	case EDesignNodeType::Text:
		Widget = CreateTextWidget(Node);
		break;
	case EDesignNodeType::IconFont:
		Widget = CreateIconFontWidget(Node);
		break;
	case EDesignNodeType::Frame:
	case EDesignNodeType::Group:
		Widget = CreateContainerWidget(Node);
		break;
	case EDesignNodeType::Rectangle:
		Widget = CreateContainerWidget(Node);
		break;
	case EDesignNodeType::Ellipse:
		{
			float W = Node->Width.IsFixed() ? Node->Width.Value : 0.0f;
			float H = Node->Height.IsFixed() ? Node->Height.Value : 0.0f;
			// 有尺寸、有颜色填充、无描边 → 生成椭圆 SVG path，走栅格化
			// 有描边时走 fallback（Border 支持 OutlineThickness，Image 不支持）
			bool bHasStroke = Node->Stroke.HasStroke();
			if (W > 0 && H > 0 && Node->Fill.IsColor() && Node->Fill.bEnabled && !bHasStroke)
			{
				float Rx = W / 2.0f, Ry = H / 2.0f;
				Node->Geometry = FString::Printf(
					TEXT("M 0,%.2f A %.2f,%.2f 0 1,1 %.2f,%.2f A %.2f,%.2f 0 1,1 0,%.2f Z"),
					Ry, Rx, Ry, W, Ry, Rx, Ry, Ry);
				Widget = CreatePathWidget(Node);
			}
			else
			{
				// Fallback: 圆角近似
				Widget = CreateContainerWidget(Node);
				if (W > 0 && H > 0)
				{
					FDesignCornerRadius EllipseRadius;
					EllipseRadius.bUniform = true;
					EllipseRadius.Uniform = FMath::Min(W, H) / 2.0f;
					FVisualMapper::ApplyRoundedCorner(Widget, EllipseRadius);
				}
			}
		}
		break;
	case EDesignNodeType::Polygon:
		if (!Node->Geometry.IsEmpty())
		{
			Widget = CreatePathWidget(Node);
		}
		else
		{
			Widget = CreateContainerWidget(Node);
			UE_LOG(LogDesign2UMG, Warning, TEXT("多边形 '%s' 无 PathGeometry，降级为 Border"), *Node->Name);
			if (OutWarnings)
				OutWarnings->Add(FString::Printf(TEXT("Polygon '%s' → Border (无 PathGeometry)"), *Node->Name));
		}
		break;
	case EDesignNodeType::Path:
		Widget = CreatePathWidget(Node);
		break;
	case EDesignNodeType::Line:
		if (!Node->Geometry.IsEmpty())
		{
			Widget = CreatePathWidget(Node);
		}
		else
		{
			// 无 Geometry 时用 Width/Height 构造水平线段
			float W = FMath::Max(1.0f, Node->Width.Value);
			float H = FMath::Max(1.0f, Node->Height.Value);
			Node->Geometry = FString::Printf(TEXT("M 0,%.1f L %.1f,%.1f"), H * 0.5f, W, H * 0.5f);
			Widget = CreatePathWidget(Node);
		}
		break;
	default:
		Widget = CreateContainerWidget(Node);
		break;
	}

	Widget.Id = Node->Id;
	Widget.Name = Node->Name.IsEmpty() ? Node->Id : Node->Name;
	Widget.Bounds.X = Node->X;
	Widget.Bounds.Y = Node->Y;
	Widget.Bounds.W = Node->Width.Value;
	Widget.Bounds.H = Node->Height.Value;

	UE_LOG(LogDesign2UMG, Verbose, TEXT("ConvertNode: '%s' id=%s type=%s bounds=(%.1f,%.1f,%.1f,%.1f) layout=%d children=%d"),
		*Widget.Name, *Widget.Id, *Widget.Type,
		Widget.Bounds.X, Widget.Bounds.Y, Widget.Bounds.W, Widget.Bounds.H,
		(int32)Node->Layout, Node->Children.Num());

	FVisualMapper::ApplyVisualProperties(Widget, Node);
	if (Widget.Type == TEXT("Border")
		&& !Widget.Properties.Contains(TEXT("BrushColor"))
		&& !Widget.Properties.Contains(TEXT("TexturePath"))
		&& !Widget.Properties.Contains(TEXT("MaterialRef")))
	{
		// Stroke-only / no-fill Border should keep transparent interior.
		// UBorder default brush is opaque white, which can cover siblings.
		Widget.Properties.Add(TEXT("BrushColor"), TEXT("#00000000"));
		if (Widget.Properties.Contains(TEXT("OutlineThickness")))
		{
			// Box + WhiteSquareTexture can still flash an opaque interior in UBorder.
			// RoundedBox with zero radii keeps the interior transparent while preserving the outline.
			Widget.Properties.Add(TEXT("DrawAs"), TEXT("RoundedBox"));
		}
	}

	TArray<TSharedPtr<FDesignNode>> VisibleChildren;
	GatherVisibleChildren(Node, VisibleChildren);
	PreResolveFlowChildren(Node, Doc, Widget, VisibleChildren, ActiveTheme);
	AppendConvertedChildren(Widget, Node, Doc, VisibleChildren, ActiveTheme, OutWarnings);

	if (Node->Gap > 0)
	{
		FLayoutMapper::ApplyGap(Widget.Children, Node->Gap, Node->Layout);
		if (Node->Layout == EDesignLayout::Wrap)
		{
			Widget.Properties.Add(TEXT("InnerSlotPadding"),
				FString::Printf(TEXT("%.1f,%.1f"), Node->Gap, Node->Gap));
		}
		else if (Node->Layout == EDesignLayout::Grid)
		{
			Widget.Properties.Add(TEXT("SlotPadding"),
				FString::Printf(TEXT("%.1f,%.1f,%.1f,%.1f"), Node->Gap, Node->Gap, Node->Gap, Node->Gap));
		}
	}

	if (Node->JustifyContent != EDesignJustifyContent::Start)
	{
		// Determine the container's main-axis size mode for Spacer suppression.
		// ParentLayout tells us if this container sits inside a CanvasPanel.
		const EDesignSizeMode MainAxisMode = (Node->Layout == EDesignLayout::Horizontal)
			? Node->Width.Mode : Node->Height.Mode;
		FLayoutMapper::ApplyJustifyContent(Widget.Children, Node->JustifyContent, MainAxisMode, ParentLayout);
	}

	const bool bPlaceholderUsesDirectImage = TryApplyPlaceholderSurfaceTint(Widget, Node);
	if (ShouldWrapNodeWithBorder(Node, bPlaceholderUsesDirectImage))
	{
		const bool bHasVisualFill = (Node->Fill.bEnabled && Node->Fill.Type != EDesignFillType::None) || Node->Fills.Num() > 0;
		const bool bHasCornerRadius = Node->CornerRadius.bUniform ? Node->CornerRadius.Uniform > 0
			: (Node->CornerRadius.TopLeft > 0 || Node->CornerRadius.TopRight > 0
				|| Node->CornerRadius.BottomRight > 0 || Node->CornerRadius.BottomLeft > 0);
		UE_LOG(LogDesign2UMG, Verbose, TEXT("ConvertNode: '%s' → WrapWithBorder (fill=%d corner=%d stroke=%d)"),
			*Widget.Name, bHasVisualFill, bHasCornerRadius, Node->Stroke.HasStroke());
		Widget = FVisualMapper::WrapWithBorder(MoveTemp(Widget), Node);
	}
	else if (FLayoutMapper::HasPadding(Node) && !bPlaceholderUsesDirectImage)
	{
		FLayoutMapper::ApplyPadding(Widget, Node->Padding);
	}

	return ApplyVisualEffectWrappers(MoveTemp(Widget), Node);
}

FUMGWidgetNode FDesign2UMGConverter::CreateContainerWidget(const TSharedPtr<FDesignNode>& Node)
{
	FUMGWidgetNode Widget;
	Widget.Type = FDesignTypeMapper::MapNodeTypeToWidget(Node->Type, Node->Layout);
	return Widget;
}

FUMGWidgetNode FDesign2UMGConverter::CreateTextWidget(const TSharedPtr<FDesignNode>& Node)
{
	FUMGWidgetNode Widget;
	Widget.Type = TEXT("TextBlock");
	Widget.Properties.Add(TEXT("Text"), Node->Content);
	ApplyTextProperties(Widget, Node);
	return Widget;
}

void FDesign2UMGConverter::ApplyTextProperties(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node)
{
	const float ResolvedFontSize = ResolveTextFontSizeForUMG(Node);
	if (ResolvedFontSize > 0)
	{
		Widget.Properties.Add(TEXT("FontSize"), FString::SanitizeFloat(ResolvedFontSize));
		Widget.Properties.Add(TEXT("DesignFontSize"), FString::SanitizeFloat(Node->FontSize));
		Widget.TextStyle.FontSize = ResolvedFontSize;
		Widget.TextStyle.SourceFontSize = Node->FontSize;
	}

	if (!Node->FontFamily.IsEmpty())
	{
		FString AssetPath = FFontResolver::FindFontAssetPath(Node->FontFamily);
		Widget.Properties.Add(TEXT("FontFamily"), AssetPath.IsEmpty() ? Node->FontFamily : AssetPath);
	}

	if (!Node->FontWeight.IsEmpty())
	{
		FString Typeface = FFontResolver::FindBestTypeface(Node->FontFamily, Node->FontWeight);
		if (Typeface.IsEmpty())
			Typeface = FDesignTypeMapper::MapFontWeight(Node->FontWeight).ToString();
		Widget.Properties.Add(TEXT("TypefaceFontName"), Typeface);
	}

	if (!Node->FontStyle.IsEmpty())
	{
		Widget.Properties.Add(TEXT("FontStyle"), Node->FontStyle);
	}

	Widget.Properties.Add(TEXT("Justification"),
		Node->TextAlign == EDesignTextAlign::Center ? TEXT("Center") :
		Node->TextAlign == EDesignTextAlign::Right ? TEXT("Right") : TEXT("Left"));

	if (Node->TextAlign == EDesignTextAlign::Justify)
		Widget.Properties.Add(TEXT("AutoWrapText"), TEXT("true"));

	// textAlignVertical is alignment inside the text box, not the parent layout slot.
	// Mapping it onto the parent slot shifts text widgets inside buttons/tabs/tables and
	// causes widespread component drift. Keep it as text metadata only.

	const bool bApplyRuntimeLineHeight =
		ShouldApplyExplicitLineHeight(Node, ResolvedFontSize)
		|| (Node->LineHeight > 0.0f
			&& Node->FontSize >= 24.0f
			&& !Node->Content.Contains(TEXT("\n"))
			&& !IsWrappingTextMode(Node));
	if (bApplyRuntimeLineHeight)
		Widget.Properties.Add(TEXT("LineHeightPercentage"), FString::SanitizeFloat(Node->LineHeight));

	if (Node->LetterSpacing != 0)
	{
		Widget.Properties.Add(TEXT("LetterSpacing"), FString::SanitizeFloat(Node->LetterSpacing));
		Widget.TextStyle.LetterSpacing = Node->LetterSpacing;
		Widget.TextStyle.bHasLetterSpacing = true;
	}

	if (Node->bUnderline)
	{
		Widget.Properties.Add(TEXT("Underline"), TEXT("true"));
	}
	if (Node->bStrikethrough)
	{
		Widget.Properties.Add(TEXT("Strikethrough"), TEXT("true"));
	}
	if (!Node->Href.IsEmpty())
	{
		Widget.Properties.Add(TEXT("Href"), Node->Href);
	}

	switch (Node->TextGrowth)
	{
	case EDesignTextGrowth::FixedWidth:
		Widget.Properties.Add(TEXT("AutoWrapText"), TEXT("true"));
		if (const float WrapTextAt = ResolveTextWrapAt(Node, 0.0f); WrapTextAt > 0.0f)
		{
			Widget.Properties.Add(TEXT("WrapTextAt"), FString::SanitizeFloat(WrapTextAt));
		}
		break;
	case EDesignTextGrowth::FixedWidthHeight:
		Widget.Properties.Add(TEXT("AutoWrapText"), TEXT("true"));
		if (const float WrapTextAt = ResolveTextWrapAt(Node, 0.0f); WrapTextAt > 0.0f)
		{
			Widget.Properties.Add(TEXT("WrapTextAt"), FString::SanitizeFloat(WrapTextAt));
		}
		Widget.Properties.Add(TEXT("Clipping"), TEXT("ClipToBounds"));
		break;
	default:
		break;
	}

	if (Node->TextAlignVertical != EDesignTextAlignVertical::Top)
	{
		Widget.Properties.Add(TEXT("TextAlignVertical"),
			Node->TextAlignVertical == EDesignTextAlignVertical::Middle ? TEXT("Center") : TEXT("Bottom"));
	}
}

FUMGWidgetNode FDesign2UMGConverter::CreateIconFontWidget(const TSharedPtr<FDesignNode>& Node)
{
	FUMGWidgetNode Widget;
	Widget.Type = TEXT("TextBlock");

	UDesign2UMGSettings* MutableSettings = UDesign2UMGSettings::Get();
	const UDesign2UMGSettings* Settings = MutableSettings;
	FString AssetPath;
	bool bUsedDefaultIconPath = false;
	bool bUsedResolverPath = false;
	if (Settings && !Node->IconFontFamily.IsEmpty())
	{
		AssetPath = Settings->FindIconFontAssetPath(Node->IconFontFamily);
		if (AssetPath.IsEmpty())
		{
			AssetPath = Settings->DefaultIconFontAssetPath;
			bUsedDefaultIconPath = !AssetPath.IsEmpty();
		}
	}

	if (!AssetPath.IsEmpty())
	{
		FString ValidAssetPath;
		if (TryNormalizeValidFontAssetPath(AssetPath, ValidAssetPath))
		{
			AssetPath = MoveTemp(ValidAssetPath);
		}
		else
		{
			UE_LOG(LogDesign2UMG, Warning,
				TEXT("Icon font path is stale or invalid for family '%s': %s"),
				*Node->IconFontFamily,
				*AssetPath);
			if (MutableSettings && !Node->IconFontFamily.IsEmpty())
			{
				static TSet<FString> PrunedInvalidMappings;
				if (!PrunedInvalidMappings.Contains(Node->IconFontFamily)
					&& MutableSettings->IconFontAssetMap.Remove(Node->IconFontFamily) > 0)
				{
					PrunedInvalidMappings.Add(Node->IconFontFamily);
					MutableSettings->SaveConfig();
				}
			}
			AssetPath.Empty();
			bUsedDefaultIconPath = false;
		}
	}

	// Fallback: resolve icon family through general font resolver.
	// This avoids "Last resort fallback font" when icon font has been auto-imported
	// but IconFontAssetMap is not explicitly configured.
	if (AssetPath.IsEmpty() && !Node->IconFontFamily.IsEmpty())
	{
		FString ResolvedAssetPath = FFontResolver::FindFontAssetPath(Node->IconFontFamily);
		FString ValidResolvedPath;
		if (TryNormalizeValidFontAssetPath(ResolvedAssetPath, ValidResolvedPath))
		{
			AssetPath = MoveTemp(ValidResolvedPath);
			bUsedResolverPath = true;
		}
	}

	// If family mapping is missing but we found a usable path, persist mapping once.
	const bool bNeedsPersistMapping = (bUsedDefaultIconPath || bUsedResolverPath)
		&& MutableSettings
		&& !Node->IconFontFamily.IsEmpty();
	if (bNeedsPersistMapping)
	{
		const TSoftObjectPtr<UFont>* Existing = MutableSettings->IconFontAssetMap.Find(Node->IconFontFamily);
		if (!Existing || Existing->IsNull())
		{
			MutableSettings->IconFontAssetMap.Add(Node->IconFontFamily, TSoftObjectPtr<UFont>(FSoftObjectPath(AssetPath)));
			UE_LOG(LogDesign2UMG, Display,
				TEXT("Auto-mapped icon font family '%s' to '%s'%s"),
				*Node->IconFontFamily,
				*AssetPath,
				bUsedResolverPath ? TEXT(" via FontResolver") : TEXT(""));
			MutableSettings->SaveConfig();
		}
	}

	if (!AssetPath.IsEmpty())
	{
		Widget.Properties.Add(TEXT("FontFamily"), AssetPath);
		const FString Typeface = !Node->FontWeight.IsEmpty()
			? FFontResolver::FindBestTypeface(Node->IconFontFamily, Node->FontWeight)
			: TEXT("Regular");
		if (!Typeface.IsEmpty())
		{
			Widget.Properties.Add(TEXT("TypefaceFontName"), Typeface);
		}
	}
	else if (!Node->IconFontFamily.IsEmpty())
	{
		static TSet<FString> WarnedFamilies;
		if (!WarnedFamilies.Contains(Node->IconFontFamily))
		{
			UE_LOG(LogDesign2UMG, Verbose,
				TEXT("IconFont '%s': font family '%s' has no asset mapping"),
				*Node->IconFontName, *Node->IconFontFamily);
			UE_LOG(LogDesign2UMG, Warning,
				TEXT("图标字体 '%s' (字体族 '%s') 未配置资产映射。请在 编辑器偏好设置 > 插件 > Design2UMG 中配置。"),
				*Node->IconFontName, *Node->IconFontFamily);
			WarnedFamilies.Add(Node->IconFontFamily);
		}
	}

	// 图标名称即连字文本（如 "dashboard" 在 Material Symbols 中渲染为图标）
	Widget.Properties.Add(TEXT("IconName"), Node->IconFontName);
	FString IconText = FFontResolver::FindKnownIconGlyph(Node->IconFontFamily, Node->IconFontName);
	if (IconText.IsEmpty())
	{
		IconText = Node->IconFontName;
	}
	if (AssetPath.IsEmpty() && Settings && !Settings->MissingIconFontPlaceholder.IsEmpty())
	{
		IconText = Settings->MissingIconFontPlaceholder;
	}
	if (!IconText.IsEmpty())
		Widget.Properties.Add(TEXT("Text"), IconText);

	if (Node->FontSize > 0)
	{
		Widget.Properties.Add(TEXT("FontSize"), FString::SanitizeFloat(Node->FontSize));
		Widget.TextStyle.FontSize = Node->FontSize;
	}

	if (Node->Fill.Type == EDesignFillType::Color)
		Widget.Properties.Add(TEXT("Color"), Node->Fill.SolidColor);

	return Widget;
}

FUMGWidgetNode FDesign2UMGConverter::CreatePathWidget(const TSharedPtr<FDesignNode>& Node)
{
	FUMGWidgetNode Widget;

	// 已被 FigmaImageExporter 转为 Image Fill → 走 Image 路径
	if (Node->Fill.Type == EDesignFillType::Image && !Node->Fill.Image.Url.IsEmpty())
	{
		Widget.Type = TEXT("Image");
		Widget.Properties.Add(TEXT("TexturePath"), Node->Fill.Image.Url);
		if (Node->Fill.Image.Mode == EDesignImageMode::Crop && !Node->Fill.Image.Transform.IsIdentity())
		{
			const FDesignImageTransform& T = Node->Fill.Image.Transform;
			Widget.Properties.Add(TEXT("ImageMode"), TEXT("Crop"));
			Widget.Properties.Add(TEXT("ImageTransform"),
				FString::Printf(TEXT("%.6f,%.6f,%.6f,%.6f,%.6f,%.6f"),
					T.A, T.B, T.TX, T.C, T.D, T.TY));
		}
		else if (Node->Fill.Image.Mode == EDesignImageMode::Fill)
		{
			Widget.Properties.Add(TEXT("ImageMode"), TEXT("Fill"));
		}
		else if (Node->Fill.Image.Mode == EDesignImageMode::Fit)
		{
			Widget.Properties.Add(TEXT("ImageMode"), TEXT("Fit"));
		}
		if (Node->Fill.Image.Opacity < 1.0f)
		{
			Widget.Properties.Add(TEXT("RenderOpacity"), FString::SanitizeFloat(Node->Fill.Image.Opacity));
		}
		int32 W = FMath::Max(1, FMath::RoundToInt(Node->Width.Value));
		int32 H = FMath::Max(1, FMath::RoundToInt(Node->Height.Value));
		Widget.Properties.Add(TEXT("WidthOverride"), FString::FromInt(W));
		Widget.Properties.Add(TEXT("HeightOverride"), FString::FromInt(H));
		return Widget;
	}

	// 有 geometry 数据 → 栅格化（fill 优先，stroke-only 回退用 stroke 颜色填充）
	if (!Node->Geometry.IsEmpty())
	{
		FString RasterColor;
		if (Node->Fill.Type == EDesignFillType::Color)
			RasterColor = Node->Fill.SolidColor;
		else if (Node->Fill.Type == EDesignFillType::Gradient && Node->Fill.Gradient.Colors.Num() > 0)
			RasterColor = Node->Fill.Gradient.Colors[0].Color;
		else if (Node->Stroke.HasStroke() && Node->Stroke.Fill.Type == EDesignFillType::Color)
			RasterColor = Node->Stroke.Fill.SolidColor;
		else
		{
			for (const FDesignFill& Fill : Node->Fills)
			{
				if (Fill.Type == EDesignFillType::Color)
				{
					RasterColor = Fill.SolidColor;
					break;
				}
				if (Fill.Type == EDesignFillType::Gradient && Fill.Gradient.Colors.Num() > 0)
				{
					RasterColor = Fill.Gradient.Colors[0].Color;
					break;
				}
			}
		}

		if (!RasterColor.IsEmpty())
		{
			int32 W = FMath::Max(1, FMath::RoundToInt(Node->Width.Value));
			int32 H = FMath::Max(1, FMath::RoundToInt(Node->Height.Value));

			UE_LOG(LogDesign2UMG, Log, TEXT("CreatePathWidget: '%s' rasterize %dx%d (raw=%.1fx%.1f) geometry=%d chars"),
				*Node->Name, W, H, Node->Width.Value, Node->Height.Value, Node->Geometry.Len());

			Widget.Type = TEXT("Image");
			Widget.Properties.Add(TEXT("PathGeometry"), Node->Geometry);
			Widget.Properties.Add(TEXT("PathFillRule"), Node->FillRule);
			Widget.Properties.Add(TEXT("PathFillColor"), RasterColor);
			Widget.Properties.Add(TEXT("WidthOverride"), FString::FromInt(W));
			Widget.Properties.Add(TEXT("HeightOverride"), FString::FromInt(H));
			return Widget;
		}
	}

	// Fallback
	Widget.Type = FDesignTypeMapper::MapNodeTypeToWidget(Node->Type, Node->Layout);
	return Widget;
}

TSharedPtr<FDesignNode> FDesign2UMGConverter::ResolveExpandedChildForConversion(
	const TSharedPtr<FDesignNode>& Child,
	const FDesignDocument& Doc,
	const TMap<FString, FString>& ThemeContext,
	bool bPreComputeFitContent)
{
	TSharedPtr<FDesignNode> ExpandedChild = ExpandRefForConversion(Child, Doc, ThemeContext);
	if (!ExpandedChild.IsValid())
		return Child;
	if (bPreComputeFitContent && ExpandedChild != Child)
		PreComputeFitContentSizes(ExpandedChild, Doc);
	return ExpandedChild;
}

void FDesign2UMGConverter::PreResolveFlowChildren(
	const TSharedPtr<FDesignNode>& Node,
	const FDesignDocument& Doc,
	const FUMGWidgetNode& Widget,
	const TArray<TSharedPtr<FDesignNode>>& VisibleChildren,
	const TMap<FString, FString>& ThemeContext)
{
	if (Widget.Bounds.W > 0 && Widget.Bounds.H > 0
		&& (Node->Layout == EDesignLayout::Horizontal || Node->Layout == EDesignLayout::Vertical))
	{
		const float InnerW = Widget.Bounds.W - Node->Padding.Left - Node->Padding.Right;
		const float InnerH = Widget.Bounds.H - Node->Padding.Top - Node->Padding.Bottom;
		const bool bHorizontal = Node->Layout == EDesignLayout::Horizontal;
		TArray<FChildSizeSnapshot> EffectiveChildren;
		for (const TSharedPtr<FDesignNode>& Child : VisibleChildren)
			EffectiveChildren.Add(BuildChildSizeSnapshot(Child, Doc));

		float FixedMainSum = 0.0f;
		int32 FillCount = 0;
		const float TotalGap = EffectiveChildren.Num() > 1 ? Node->Gap * (EffectiveChildren.Num() - 1) : 0.0f;
		for (const FChildSizeSnapshot& ChildSnapshot : EffectiveChildren)
		{
			const FDesignSize& MainSize = bHorizontal ? ChildSnapshot.EffectiveWidth : ChildSnapshot.EffectiveHeight;
			if (MainSize.IsFill())
				++FillCount;
			else
				FixedMainSum += MainSize.Value;

			const FDesignSize& RawMain = bHorizontal ? ChildSnapshot.RawWidth : ChildSnapshot.RawHeight;
			const FDesignSize& RawCross = bHorizontal ? ChildSnapshot.RawHeight : ChildSnapshot.RawWidth;
			const FDesignSize& EffectiveCross = bHorizontal ? ChildSnapshot.EffectiveHeight : ChildSnapshot.EffectiveWidth;
			UE_LOG(LogDesign2UMG, Verbose, TEXT("PreResolveFill Child '%s': ref=%d rawMain=%s(%.0f) effMain=%s(%.0f) rawCross=%s(%.0f) effCross=%s(%.0f)"),
				ChildSnapshot.Child.IsValid() ? *ChildSnapshot.Child->Id : TEXT("<invalid>"),
				ChildSnapshot.bUsedExpandedRef ? 1 : 0,
				ToSizeModeLogString(RawMain.Mode), RawMain.Value,
				ToSizeModeLogString(MainSize.Mode), MainSize.Value,
				ToSizeModeLogString(RawCross.Mode), RawCross.Value,
				ToSizeModeLogString(EffectiveCross.Mode), EffectiveCross.Value);
		}

		auto ResolveSize = [](TSharedPtr<FDesignNode>& TargetNode, bool bWidth, float Pixels)
		{
			FDesignSize& Size = bWidth ? TargetNode->Width : TargetNode->Height;
			Size.Value = FMath::Max(0.0f, Pixels);
			if (bWidth) TargetNode->bHasWidth = true;
			else TargetNode->bHasHeight = true;
		};

		if (FillCount > 0)
		{
			const float AvailableMain = (bHorizontal ? InnerW : InnerH) - FixedMainSum - TotalGap;
			const float FillMain = FMath::Max(0.0f, AvailableMain / FillCount);
			for (FChildSizeSnapshot& ChildSnapshot : EffectiveChildren)
			{
				const FDesignSize& MainSize = bHorizontal ? ChildSnapshot.EffectiveWidth : ChildSnapshot.EffectiveHeight;
				if (MainSize.IsFill())
					ResolveSize(ChildSnapshot.Child, bHorizontal, FillMain);
				const FDesignSize& CrossSize = bHorizontal ? ChildSnapshot.EffectiveHeight : ChildSnapshot.EffectiveWidth;
				if (CrossSize.IsFill())
					ResolveSize(ChildSnapshot.Child, !bHorizontal, bHorizontal ? InnerH : InnerW);
			}

			UE_LOG(LogDesign2UMG, Log, TEXT("PreResolveFill: '%s' layout=%s inner=(%.0f,%.0f) fixed=%.0f fill=%d each=%.0f"),
				*Widget.Name, bHorizontal ? TEXT("H") : TEXT("V"),
				InnerW, InnerH, FixedMainSum, FillCount, FillMain);
		}
		else
		{
			for (FChildSizeSnapshot& ChildSnapshot : EffectiveChildren)
			{
				const FDesignSize& CrossSize = bHorizontal ? ChildSnapshot.EffectiveHeight : ChildSnapshot.EffectiveWidth;
				if (CrossSize.IsFill())
					ResolveSize(ChildSnapshot.Child, !bHorizontal, bHorizontal ? InnerH : InnerW);
			}
		}
	}

	for (const TSharedPtr<FDesignNode>& Child : VisibleChildren)
	{
		auto ResolveFit = [](const TSharedPtr<FDesignNode>& TargetNode, bool bWidth, float Pixels)
		{
			FDesignSize& Size = bWidth ? TargetNode->Width : TargetNode->Height;
			if (Size.IsFit() && Pixels > Size.Value)
			{
				Size.Value = Pixels;
				if (bWidth) TargetNode->bHasWidth = true;
				else TargetNode->bHasHeight = true;
			}
		};

		if (Child->Width.IsFit() || Child->Height.IsFit())
		{
			const float ChildContentWidth = ComputeChildContentWidthHint(Node, Child, 0.0f);
			const TSharedPtr<FDesignNode> EstimateChild = ResolveExpandedChildForConversion(Child, Doc, ThemeContext, false);
			const FVector2D Estimate = EstimateNodeSize(EstimateChild, ChildContentWidth);
			ResolveFit(Child, true, Estimate.X);
			ResolveFit(Child, false, Estimate.Y);
			if (Estimate.X > 0 || Estimate.Y > 0)
			{
				UE_LOG(LogDesign2UMG, Log, TEXT("PreResolveFitContent: '%s' estimated=(%.0f,%.0f)"),
					*Child->Name, Estimate.X, Estimate.Y);
			}
		}
	}
}

bool FDesign2UMGConverter::ShouldWrapChildWithSizeBox(
	const TSharedPtr<FDesignNode>& ParentNode,
	const TSharedPtr<FDesignNode>& ChildNode,
	const FUMGWidgetNode& ChildWidget)
{
	const bool bHasFixedSize = ChildNode->Width.IsFixed() || ChildNode->Height.IsFixed();
	const bool bHasMinSize = (ChildNode->Width.IsFit() && ChildNode->Width.Value > 0)
		|| (ChildNode->Height.IsFit() && ChildNode->Height.Value > 0);
	const bool bCanvasMixedAxis = ParentNode->Layout == EDesignLayout::None
		&& ((ChildNode->Width.IsFit() && ChildNode->Height.IsFixed())
			|| (ChildNode->Height.IsFit() && ChildNode->Width.IsFixed()));
	const bool bWidgetOwnsDesiredSize =
		ParentNode->Layout != EDesignLayout::None
		&& ChildWidget.Type == TEXT("Image")
		&& ChildNode->Children.Num() == 0
		&& (ChildWidget.Properties.Contains(TEXT("WidthOverride")) || ChildWidget.Properties.Contains(TEXT("HeightOverride")))
		&& (ChildWidget.Properties.Contains(TEXT("MaterialRef"))
			|| ChildWidget.Properties.Contains(TEXT("TexturePath"))
			|| ChildWidget.Properties.Contains(TEXT("PathGeometry")));
	return ParentNode->Layout == EDesignLayout::None
		? bCanvasMixedAxis
		: (bHasFixedSize || bHasMinSize) && !bWidgetOwnsDesiredSize;
}

void FDesign2UMGConverter::AppendConvertedChildren(
	FUMGWidgetNode& Widget,
	const TSharedPtr<FDesignNode>& Node,
	const FDesignDocument& Doc,
	const TArray<TSharedPtr<FDesignNode>>& VisibleChildren,
	const TMap<FString, FString>& ActiveTheme,
	TArray<FString>* OutWarnings)
{
	for (const TSharedPtr<FDesignNode>& Child : VisibleChildren)
	{
		TSharedPtr<FDesignNode> EffectiveChild = ResolveExpandedChildForConversion(Child, Doc, ActiveTheme, true);
		FUMGWidgetNode ChildWidget = ConvertNode(EffectiveChild, Doc, ActiveTheme, OutWarnings, Node->Layout);
		if (ChildWidget.Type.IsEmpty())
			continue;

		FLayoutMapper::ApplySlotProperties(ChildWidget, EffectiveChild, Node->Layout);
		FLayoutMapper::ApplyAlignItems(ChildWidget, Node->AlignItems, Node->Layout);
		if (Node->Layout != EDesignLayout::None && !EffectiveChild->bAbsoluteLayoutPositioning)
		{
			ChildWidget.Bounds.X = 0.0f;
			ChildWidget.Bounds.Y = 0.0f;
		}

		if (ShouldWrapChildWithSizeBox(Node, EffectiveChild, ChildWidget))
		{
			const bool bHasFixedSize = EffectiveChild->Width.IsFixed() || EffectiveChild->Height.IsFixed();
			const bool bHasMinSize = (EffectiveChild->Width.IsFit() && EffectiveChild->Width.Value > 0)
				|| (EffectiveChild->Height.IsFit() && EffectiveChild->Height.Value > 0);
			UE_LOG(LogDesign2UMG, Verbose, TEXT("  child '%s' → SizeBox wrap (fixed=%d min=%d w=%.1f h=%.1f)"),
				*ChildWidget.Name, bHasFixedSize, bHasMinSize,
				EffectiveChild->Width.Value, EffectiveChild->Height.Value);
			Widget.Children.Add(FLayoutMapper::WrapWithSizeBox(MoveTemp(ChildWidget), EffectiveChild));
			continue;
		}

		Widget.Children.Add(MoveTemp(ChildWidget));
	}
}

FVector2D FDesign2UMGConverter::PreComputeFitContentSizes(const TSharedPtr<FDesignNode>& Node, const FDesignDocument& Doc)
{
	return PreComputeFitContentSizesImpl(Node, Doc, 0.0f);
}



