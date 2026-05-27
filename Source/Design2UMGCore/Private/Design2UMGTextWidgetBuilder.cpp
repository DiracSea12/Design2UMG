// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGTextWidgetBuilder.h"

#include "Design2UMGLog.h"
#include "Design2UMGSettings.h"
#include "Design2UMGSourceMetadata.h"
#include "DesignTypeMapper.h"
#include "FontResolver.h"
#include "TextMeasurement.h"
#include "UMGValueParser.h"
#include "UMGWidgetNodeAccess.h"
#include "Misc/PackageName.h"
#include "RichTextStyleRowSerializer.h"

namespace
{
	struct FGeneratedRichTextContent
	{
		FString Markup;
		FString StyleRowsJson;
		FString SingleHref;
		bool bHasStyleRows = false;
	};

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

	void ApplyTextProperties(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node)
	{
		FString ResolvedFontAssetPath;
		const float ResolvedFontSize = TextMeasurement::ResolveTextFontSizeForUMG(Node);
		if (ResolvedFontSize > 0)
		{
			Widget.TextStyle.FontSize = ResolvedFontSize;
			Widget.TextStyle.SourceFontSize = Node->Text.FontSize;
		}

		const bool bExplicitMapping = FFontResolver::HasExplicitFontMapping(Node->Text.FontFamily, Node->Text.FontPostScriptName);
		FString Typeface;
		if (!Node->Text.FontFamily.IsEmpty() || !Node->Text.FontPostScriptName.IsEmpty())
		{
			const FString AssetPath = FFontResolver::FindFontAssetPath(Node->Text.FontFamily, Node->Text.FontPostScriptName);
			if (!AssetPath.IsEmpty())
			{
				const bool bMissingExplicitTypeface = bExplicitMapping
					&& FFontResolver::FindExactTypefaceForAssetPath(AssetPath, Node->Text.FontWeight).IsEmpty();
				if (!bMissingExplicitTypeface)
				{
					ResolvedFontAssetPath = AssetPath;
					Widget.Text.FontFamily = AssetPath;
				}
			}
		}

		if (!Node->Text.FontWeight.IsEmpty())
		{
			Typeface = bExplicitMapping
				? FFontResolver::FindExactTypefaceForAssetPath(ResolvedFontAssetPath, Node->Text.FontWeight)
				: FFontResolver::FindBestTypefaceForAssetPath(ResolvedFontAssetPath, Node->Text.FontWeight);
			if (!bExplicitMapping && Typeface.IsEmpty())
			{
				Typeface = FFontResolver::FindBestTypeface(Node->Text.FontFamily, Node->Text.FontWeight);
			}
			if (!bExplicitMapping && Typeface.IsEmpty())
			{
				Typeface = DesignTypeMapper::MapFontWeight(Node->Text.FontWeight).ToString();
			}
			if (!Typeface.IsEmpty())
			{
				Widget.Text.TypefaceFontName = Typeface;
			}
		}

		if (!Node->Text.FontStyle.IsEmpty())
		{
			Widget.Text.FontStyle = Node->Text.FontStyle;
		}

		Widget.Text.Justification =
			Node->Text.TextAlign == EDesignTextAlign::Center ? TEXT("Center") :
			Node->Text.TextAlign == EDesignTextAlign::Right ? TEXT("Right") : TEXT("Left");

		const float ResolvedWrapTextAt = TextMeasurement::ResolveTextWrapAt(Node, 0.0f);
		const bool bNeedsKnownWidthRuntimeWrap =
			Node->Text.TextGrowth == EDesignTextGrowth::Auto
			&& Node->Transform.Width.IsFill()
			&& Node->Transform.Width.Value > 0.0f
			&& !Design2UMGSourceMetadata::HasNodeMetadataFlag(Node, TEXT("source.text.truncate"));
		if (Node->Text.TextAlign == EDesignTextAlign::Justify)
		{
			Widget.Text.bAutoWrapText = true;
			if (ResolvedWrapTextAt > 0.0f)
			{
				Widget.Text.WrapTextAt = ResolvedWrapTextAt;
			}
		}
		else if (bNeedsKnownWidthRuntimeWrap && ResolvedWrapTextAt > 0.0f)
		{
			Widget.Text.bAutoWrapText = true;
			Widget.Text.WrapTextAt = ResolvedWrapTextAt;
		}

		const bool bApplyRuntimeLineHeight =
			TextMeasurement::ShouldApplyExplicitLineHeight(Node, ResolvedFontSize);
		if (bApplyRuntimeLineHeight)
		{
			Widget.Text.LineHeightPercentage = Node->Text.LineHeight;
		}

		// Preserve authored explicit-zero semantics on the design model, but avoid
		// redundantly emitting runtime letter spacing when the effective value is zero.
		if (Node->Text.LetterSpacing != 0)
		{
			Widget.TextStyle.LetterSpacing = Node->Text.LetterSpacing;
		}

		if (Node->Text.bUnderline)
		{
			Widget.Text.bUnderline = true;
		}
		if (Node->Text.bStrikethrough)
		{
			Widget.Text.bStrikethrough = true;
		}
		if (!Node->Text.Href.IsEmpty())
		{
			Widget.Text.Href = Node->Text.Href;
		}

		switch (Node->Text.TextGrowth)
		{
		case EDesignTextGrowth::FixedWidth:
		case EDesignTextGrowth::FixedWidthHeight:
			if (Design2UMGSourceMetadata::HasNodeMetadataFlag(Node, TEXT("source.text.truncate")))
			{
				Widget.Render.Clipping = TEXT("ClipToBounds");
				break;
			}

			Widget.Text.bAutoWrapText = true;
			if (ResolvedWrapTextAt > 0.0f)
			{
				Widget.Text.WrapTextAt = ResolvedWrapTextAt;
			}
			if (Node->Text.TextGrowth == EDesignTextGrowth::FixedWidthHeight)
			{
				Widget.Render.Clipping = TEXT("ClipToBounds");
			}
			break;
		default:
			break;
		}

		if (Node->Text.TextAlignVertical != EDesignTextAlignVertical::Top)
		{
			Widget.Text.TextAlignVertical =
				Node->Text.TextAlignVertical == EDesignTextAlignVertical::Middle ? TEXT("Center") : TEXT("Bottom");

			// textAlignVertical is an internal text-box alignment hint. It must not
			// move the outer layout slot, but fixed-height text still needs internal
			// padding so the runtime TextBlock approximates the authored vertical
			// centering/bottom alignment.
			if (Node->Transform.Height.Value > 0.0f)
			{
				const FVector2D MeasuredTextSize = TextMeasurement::MeasureTextNodeSize(Node, 0.0f);
				const float ContentHeight = MeasuredTextSize.Y;
				const float SlackHeight = Node->Transform.Height.Value - ContentHeight;
				if (ContentHeight > 0.0f && SlackHeight > 0.5f)
				{
					FMargin InternalPadding = Widget.WidgetStyle.Padding.Get(FMargin(0.0f));
					if (Node->Text.TextAlignVertical == EDesignTextAlignVertical::Middle)
					{
						const float SplitPadding = SlackHeight * 0.5f;
						InternalPadding.Top += SplitPadding;
						InternalPadding.Bottom += SplitPadding;
					}
					else
					{
						InternalPadding.Top += SlackHeight;
					}

					Widget.WidgetStyle.Padding = InternalPadding;
				}
			}
		}
	}
}

FString Design2UMGTextWidgetBuilder::FIconFontSettingsSnapshot::FindIconFontAssetPath(const FString& FontFamily) const
{
	if (FontFamily.IsEmpty())
	{
		return FString();
	}

	if (const FString* ExactMatch = IconFontAssetPaths.Find(FontFamily))
	{
		return *ExactMatch;
	}

	for (const TPair<FString, FString>& Pair : IconFontAssetPaths)
	{
		if (Pair.Key.Equals(FontFamily, ESearchCase::IgnoreCase))
		{
			return Pair.Value;
		}
	}

	return FString();
}

Design2UMGTextWidgetBuilder::FIconFontSettingsSnapshot Design2UMGTextWidgetBuilder::BuildIconFontSettingsSnapshot(const UDesign2UMGSettings* Settings)
{
	FIconFontSettingsSnapshot Snapshot;
	if (!Settings)
	{
		return Snapshot;
	}

	Snapshot.DefaultIconFontAssetPath = Settings->DefaultIconFontAssetPath;
	Snapshot.MissingIconFontPlaceholder = Settings->MissingIconFontPlaceholder;
	for (const TPair<FString, TSoftObjectPtr<UFont>>& Pair : Settings->IconFontAssetMap)
	{
		const FSoftObjectPath AssetPath = Pair.Value.ToSoftObjectPath();
		if (!AssetPath.IsNull())
		{
			Snapshot.IconFontAssetPaths.Add(Pair.Key, AssetPath.ToString());
		}
	}

	return Snapshot;
}

FUMGWidgetNode Design2UMGTextWidgetBuilder::CreateTextWidget(const TSharedPtr<FDesignNode>& Node)
{
	FUMGWidgetNode Widget;

	const auto EscapeRichTextText = [](const FString& Value)
	{
		FString Escaped = Value;
		Escaped.ReplaceInline(TEXT("&"), TEXT("&amp;"));
		Escaped.ReplaceInline(TEXT("<"), TEXT("&lt;"));
		Escaped.ReplaceInline(TEXT(">"), TEXT("&gt;"));
		return Escaped;
	};

	const auto EscapeRichTextAttribute = [&EscapeRichTextText](const FString& Value)
	{
		FString Escaped = EscapeRichTextText(Value);
		Escaped.ReplaceInline(TEXT("\""), TEXT("&quot;"));
		Escaped.ReplaceInline(TEXT("'"), TEXT("&apos;"));
		return Escaped;
	};

	const auto ResolveFontAssetPath = [](const FString& FontFamily, const FString& PostScriptName, const FString& FontWeight)
	{
		if (FontFamily.IsEmpty() && PostScriptName.IsEmpty())
		{
			return FString();
		}

		const FString AssetPath = FFontResolver::FindFontAssetPath(FontFamily, PostScriptName);
		if (FFontResolver::HasExplicitFontMapping(FontFamily, PostScriptName)
			&& FFontResolver::FindExactTypefaceForAssetPath(AssetPath, FontWeight).IsEmpty())
		{
			return FString();
		}
		return AssetPath;
	};

	const auto ResolveTypefaceName = [](const FString& FontFamily, const FString& PostScriptName, const FString& FontAssetPath, const FString& FontWeight)
	{
		const FString EffectiveWeight = FontWeight.IsEmpty() ? TEXT("normal") : FontWeight;
		const bool bExplicitMapping = FFontResolver::HasExplicitFontMapping(FontFamily, PostScriptName);
		FString Typeface = bExplicitMapping
			? FFontResolver::FindExactTypefaceForAssetPath(FontAssetPath, EffectiveWeight)
			: FFontResolver::FindBestTypefaceForAssetPath(FontAssetPath, EffectiveWeight);
		if (!bExplicitMapping && Typeface.IsEmpty())
		{
			Typeface = FFontResolver::FindBestTypeface(FontFamily, EffectiveWeight);
		}
		if (!bExplicitMapping && Typeface.IsEmpty())
		{
			Typeface = DesignTypeMapper::MapFontWeight(EffectiveWeight).ToString();
		}
		return Typeface;
	};

	const auto TextRunNeedsRichText = [](const FDesignTextRun& Run)
	{
		return !Run.FontFamily.IsEmpty()
			|| !Run.FontPostScriptName.IsEmpty()
			|| Run.FontSize.IsSet()
			|| !Run.FontWeight.IsEmpty()
			|| !Run.FontStyle.IsEmpty()
			|| Run.LineHeight.IsSet()
			|| Run.LetterSpacing.IsSet()
			|| Run.bUnderline.IsSet()
			|| Run.bStrikethrough.IsSet()
			|| !Run.Href.IsEmpty();
	};

	const auto ShouldUseRichTextBlock = [&TextRunNeedsRichText](const TSharedPtr<FDesignNode>& InNode)
	{
		if (!InNode.IsValid())
		{
			return false;
		}

		if (!InNode->Text.Href.IsEmpty())
		{
			return true;
		}

		for (const FDesignTextRun& Run : InNode->Text.TextRuns)
		{
			if (TextRunNeedsRichText(Run))
			{
				return true;
			}
		}

		return false;
	};

	const auto BuildResolvedRichTextRun =
		[&ResolveFontAssetPath, &ResolveTypefaceName](const TSharedPtr<FDesignNode>& InNode, const FDesignTextRun* Run, int32 RunIndex, float ResolvedNodeFontSize)
	{
		FResolvedRichTextRun Result;
		Result.RowName = FString::Printf(TEXT("d2u_run_%d"), RunIndex);
		Result.Content = Run ? Run->Content : InNode->Text.Content;

		if (!Run || !Run->FontFamily.IsEmpty() || !Run->FontPostScriptName.IsEmpty())
		{
			const FString EffectiveFontFamily = (Run && !Run->FontFamily.IsEmpty()) ? Run->FontFamily : InNode->Text.FontFamily;
			const FString EffectivePostScriptName = Run
				? Run->FontPostScriptName
				: InNode->Text.FontPostScriptName;
			const FString EffectiveFontWeight = (Run && !Run->FontWeight.IsEmpty()) ? Run->FontWeight : InNode->Text.FontWeight;
			Result.FontFamily = ResolveFontAssetPath(EffectiveFontFamily, EffectivePostScriptName, EffectiveFontWeight);
			Result.TypefaceFontName = ResolveTypefaceName(
				EffectiveFontFamily,
				EffectivePostScriptName,
				Result.FontFamily,
				EffectiveFontWeight);
		}
		else if (!Run->FontWeight.IsEmpty())
		{
			const FString ParentFontAssetPath = ResolveFontAssetPath(InNode->Text.FontFamily, InNode->Text.FontPostScriptName, Run->FontWeight);
			Result.TypefaceFontName = ResolveTypefaceName(
				InNode->Text.FontFamily,
				InNode->Text.FontPostScriptName,
				ParentFontAssetPath,
				Run->FontWeight);
		}

		const bool bRunHasFontSize = Run && Run->FontSize.IsSet();
		const float EffectiveDesignFontSize = bRunHasFontSize ? Run->FontSize.GetValue() : InNode->Text.FontSize;
		if (EffectiveDesignFontSize > 0.0f)
		{
			Result.DesignFontSize = EffectiveDesignFontSize;
			const float ResolvedRunFontSize = (bRunHasFontSize && InNode->Text.FontSize > KINDA_SMALL_NUMBER && ResolvedNodeFontSize > KINDA_SMALL_NUMBER)
				? (Run->FontSize.GetValue() * (ResolvedNodeFontSize / InNode->Text.FontSize))
				: (bRunHasFontSize ? Run->FontSize.GetValue() : ResolvedNodeFontSize);
			if (ResolvedRunFontSize > 0.0f)
			{
				Result.FontSize = ResolvedRunFontSize;
			}
		}

		const bool bHasNodeColor = InNode->Visual.Fill.Type == EDesignFillType::Color;
		if (bHasNodeColor)
		{
			Result.Color = Design2UMGWidgetNodeAccess::SerializeWidgetColor(FUMGValueParser::ParseColor(InNode->Visual.Fill.SolidColor));
			if (Result.Color->IsEmpty())
			{
				Result.Color.Reset();
			}
		}

		const bool bRunUnderline = Run ? Run->bUnderline.Get(InNode->Text.bUnderline) : InNode->Text.bUnderline;
		Result.bStrikethrough = Run ? Run->bStrikethrough.Get(InNode->Text.bStrikethrough) : InNode->Text.bStrikethrough;
		Result.Href = (Run && !Run->Href.IsEmpty()) ? Run->Href : InNode->Text.Href;
		Result.bUnderline = bRunUnderline || !Result.Href.IsEmpty();

		const bool bRunHasLetterSpacing = Run && Run->LetterSpacing.IsSet();
		const float EffectiveLetterSpacing = bRunHasLetterSpacing ? Run->LetterSpacing.GetValue() : InNode->Text.LetterSpacing;
		const bool bHasLetterSpacing = bRunHasLetterSpacing || InNode->Text.HasLetterSpacingValue();
		if (bHasLetterSpacing)
		{
			Result.LetterSpacing = EffectiveLetterSpacing;
		}

		return Result;
	};

	const auto BuildRichTextContent =
		[&BuildResolvedRichTextRun, &EscapeRichTextAttribute, &EscapeRichTextText](const TSharedPtr<FDesignNode>& InNode)
	{
		FGeneratedRichTextContent Result;
		if (!InNode.IsValid())
		{
			return Result;
		}

		const float ResolvedNodeFontSize = TextMeasurement::ResolveTextFontSizeForUMG(InNode);
		TArray<FResolvedRichTextRun> Runs;
		if (InNode->Text.TextRuns.Num() > 0)
		{
			for (int32 RunIndex = 0; RunIndex < InNode->Text.TextRuns.Num(); ++RunIndex)
			{
				Runs.Add(BuildResolvedRichTextRun(InNode, &InNode->Text.TextRuns[RunIndex], RunIndex, ResolvedNodeFontSize));
			}
		}
		else
		{
			Runs.Add(BuildResolvedRichTextRun(InNode, nullptr, 0, ResolvedNodeFontSize));
		}

		TSet<FString> UniqueHrefs;
		for (const FResolvedRichTextRun& Run : Runs)
		{
			Result.Markup.AppendChar(TEXT('<'));
			Result.Markup.Append(Run.RowName);
			if (!Run.Href.IsEmpty())
			{
				Result.Markup.Append(TEXT(" href=\""));
				Result.Markup.Append(EscapeRichTextAttribute(Run.Href));
				Result.Markup.AppendChar(TEXT('"'));
				UniqueHrefs.Add(Run.Href);
			}
			Result.Markup.AppendChar(TEXT('>'));
			Result.Markup.Append(EscapeRichTextText(Run.Content));
			Result.Markup.Append(TEXT("</>"));
		}

		if (UniqueHrefs.Num() == 1)
		{
			for (const FString& UniqueHref : UniqueHrefs)
			{
				Result.SingleHref = UniqueHref;
				break;
			}
		}

		Result.bHasStyleRows = RichTextStyleRowSerializer::Serialize(Runs, Result.StyleRowsJson);
		return Result;
	};

	const bool bUseRichTextBlock = ShouldUseRichTextBlock(Node);
	const FGeneratedRichTextContent RichTextContent = bUseRichTextBlock ? BuildRichTextContent(Node) : FGeneratedRichTextContent();
	Widget.Type = bUseRichTextBlock ? TEXT("RichTextBlock") : TEXT("TextBlock");
	Widget.Text.Text = bUseRichTextBlock ? RichTextContent.Markup : TextMeasurement::PrepareRuntimeTextContent(Node->Text.Content);
	ApplyTextProperties(Widget, Node);
	if (bUseRichTextBlock && RichTextContent.bHasStyleRows)
	{
		Widget.Text.RichTextStyleRowsJson = RichTextContent.StyleRowsJson;
	}
	if (!RichTextContent.SingleHref.IsEmpty())
	{
		Widget.WidgetStyle.ToolTipText = RichTextContent.SingleHref;
	}
	return Widget;
}

Design2UMGTextWidgetBuilder::FIconFontBuildResult Design2UMGTextWidgetBuilder::CreateIconFontWidget(
	const TSharedPtr<FDesignNode>& Node,
	const FIconFontBuildArgs& Args)
{
	FIconFontBuildResult Result;
	if (Args.SettingsSnapshot == nullptr)
	{
		const FString NodeLabel =
			Node.IsValid()
				? (Node->Name.IsEmpty() ? Node->Id : Node->Name)
				: FString(TEXT("<invalid>"));
		const FString Warning = FString::Printf(
			TEXT("Icon-font node '%s' requires an explicit settings snapshot; silent EmptySettingsSnapshot fallback is retired."),
			*NodeLabel);
		Result.Warnings.Add(Warning);
		Result.bMissingSettingsSnapshot = true;
		UE_LOG(LogDesign2UMG, Warning, TEXT("%s"), *Warning);
		return Result;
	}

	FUMGWidgetNode& Widget = Result.Widget;
	Widget.Type = TEXT("TextBlock");
	const FIconFontSettingsSnapshot& SettingsSnapshot = *Args.SettingsSnapshot;

	const auto ResolveIconTypefaceWeightToken = [](const TSharedPtr<FDesignNode>& InNode, bool bUsingFallbackFontAsset)
	{
		if (!InNode.IsValid())
		{
			return FString(TEXT("Regular"));
		}

		const FString NormalizedFontWeight = InNode->Text.FontWeight.TrimStartAndEnd();
		const bool bHasExplicitNodeFontWeight =
			!NormalizedFontWeight.IsEmpty()
			&& !NormalizedFontWeight.Equals(TEXT("normal"), ESearchCase::IgnoreCase)
			&& !NormalizedFontWeight.Equals(TEXT("regular"), ESearchCase::IgnoreCase)
			&& !NormalizedFontWeight.Equals(TEXT("400"), ESearchCase::IgnoreCase);
		if (bHasExplicitNodeFontWeight)
		{
			return NormalizedFontWeight;
		}

		// Fallback non-icon fonts (for example the default Roboto asset used when an
		// icon family is missing) should not inherit Material Symbols weight tokens.
		// Real icon font assets still honor the authored icon weight.
		return bUsingFallbackFontAsset
			? FString(TEXT("Regular"))
			: FString::FromInt(FMath::Clamp(InNode->Text.IconWeight, 100, 900));
	};

	FString AssetPath;
	FString ResolvedIconGlyph;
	bool bUsedDefaultIconPath = false;
	bool bUsedResolverPath = false;
	if (!Node->Text.IconFontFamily.IsEmpty())
	{
		AssetPath = SettingsSnapshot.FindIconFontAssetPath(Node->Text.IconFontFamily);
		if (AssetPath.IsEmpty())
		{
			AssetPath = SettingsSnapshot.DefaultIconFontAssetPath;
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
				*Node->Text.IconFontFamily,
				*AssetPath);
			if (!Node->Text.IconFontFamily.IsEmpty())
			{
				Result.PrunedInvalidMappings.Add(Node->Text.IconFontFamily);
			}
			AssetPath.Empty();
			bUsedDefaultIconPath = false;
		}
	}

	// Fallback: resolve icon family through general font resolver.
	// This avoids "Last resort fallback font" when icon font has been auto-imported
	// but IconFontAssetMap is not explicitly configured.
	if (AssetPath.IsEmpty() && !Node->Text.IconFontFamily.IsEmpty())
	{
		FString ResolvedAssetPath = FFontResolver::FindFontAssetPath(Node->Text.IconFontFamily);
		FString ValidResolvedPath;
		if (TryNormalizeValidFontAssetPath(ResolvedAssetPath, ValidResolvedPath))
		{
			AssetPath = MoveTemp(ValidResolvedPath);
			bUsedResolverPath = true;
		}
	}

	// If family mapping is missing but we found a usable path, persist mapping once.
	const bool bNeedsPersistMapping = (bUsedDefaultIconPath || bUsedResolverPath)
		&& !Node->Text.IconFontFamily.IsEmpty();
	if (bNeedsPersistMapping)
	{
		Result.PendingIconFontMappings.Add(Node->Text.IconFontFamily, AssetPath);
		UE_LOG(LogDesign2UMG, Display,
			TEXT("Queued icon font family mapping '%s' -> '%s'%s"),
			*Node->Text.IconFontFamily,
			*AssetPath,
			bUsedResolverPath ? TEXT(" via FontResolver") : TEXT(""));
	}

	if (!AssetPath.IsEmpty())
	{
		Widget.Text.FontFamily = AssetPath;
		ResolvedIconGlyph = FFontResolver::FindKnownIconGlyph(Node->Text.IconFontFamily, Node->Text.IconFontName);
		const bool bUsingFallbackFontAsset = bUsedDefaultIconPath;
		FString WeightToken = ResolveIconTypefaceWeightToken(Node, bUsingFallbackFontAsset);
		FString Typeface = FFontResolver::FindBestTypeface(Node->Text.IconFontFamily, WeightToken);
		if (Typeface.IsEmpty())
		{
			Typeface = DesignTypeMapper::MapFontWeight(WeightToken).ToString();
		}
		if (!Typeface.IsEmpty())
		{
			Widget.Text.TypefaceFontName = Typeface;
		}
	}
	else if (!Node->Text.IconFontFamily.IsEmpty())
	{
		const bool bAlreadyWarnedFamily =
			Args.ExistingWarnedFamilies != nullptr
			&& Args.ExistingWarnedFamilies->Contains(Node->Text.IconFontFamily);
		if (!bAlreadyWarnedFamily)
		{
			UE_LOG(LogDesign2UMG, Verbose,
				TEXT("IconFont '%s': font family '%s' has no asset mapping"),
				*Node->Text.IconFontName, *Node->Text.IconFontFamily);
			UE_LOG(LogDesign2UMG, Warning,
				TEXT("Icon font '%s' (family '%s') has no configured asset mapping. Configure it in Editor Preferences > Plugins > Design2UMG."),
				*Node->Text.IconFontName, *Node->Text.IconFontFamily);
			Result.NewlyWarnedFamilies.Add(Node->Text.IconFontFamily);
		}
	}

	// The icon name itself is the ligature text for fonts such as Material Symbols.
	Widget.Text.IconName = Node->Text.IconFontName;
	FString IconText = !ResolvedIconGlyph.IsEmpty()
		? ResolvedIconGlyph
		: FFontResolver::FindKnownIconGlyph(Node->Text.IconFontFamily, Node->Text.IconFontName);
	if (IconText.IsEmpty())
	{
		IconText = Node->Text.IconFontName;
	}
	if (AssetPath.IsEmpty() && !SettingsSnapshot.MissingIconFontPlaceholder.IsEmpty())
	{
		IconText = SettingsSnapshot.MissingIconFontPlaceholder;
	}
	if (!IconText.IsEmpty())
	{
		Widget.Text.Text = IconText;
	}

	if (Node->Text.FontSize > 0)
	{
		Widget.TextStyle.FontSize = Node->Text.FontSize;
		Widget.TextStyle.SourceFontSize = Node->Text.FontSize;
	}

	if (Node->Visual.Fill.Type == EDesignFillType::Color)
	{
		Widget.Text.Color = FUMGValueParser::ParseColor(Node->Visual.Fill.SolidColor);
	}

	return Result;
}

