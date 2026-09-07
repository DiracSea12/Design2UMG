// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "PenParserNodeExtensions.h"
#include "DesignColorUtils.h"
#include "DesignJsonUtils.h"
#include "ParserSharedSemantics.h"
#include "PenParserInternal.h"

namespace
{
	struct FPenSlotMetadata
	{
		bool bHasSlot = false;
		int32 SlotNodeCount = 0;
	};

	bool IsTextLikeNode(const FDesignNode& Node)
	{
		return Node.Type == EDesignNodeType::Text
			|| Node.Type == EDesignNodeType::IconFont
			|| !Node.Text.Content.IsEmpty()
			|| !Node.Text.FontFamily.IsEmpty()
			|| !Node.Text.IconFontName.IsEmpty();
	}

	FPenSlotMetadata ParseSlotMetadata(const TSharedPtr<FJsonObject>& JsonObj)
	{
		FPenSlotMetadata Result;
		const TSharedPtr<FJsonValue> SlotField = JsonObj->TryGetField(TEXT("slot"));
		if (!SlotField.IsValid() || SlotField->Type != EJson::Array)
		{
			return Result;
		}

		Result.bHasSlot = true;
		for (const TSharedPtr<FJsonValue>& SlotVal : SlotField->AsArray())
		{
			if (!SlotVal.IsValid())
			{
				continue;
			}

			FString SlotId;
			if (SlotVal->TryGetString(SlotId))
			{
				++Result.SlotNodeCount;
			}
		}

		return Result;
	}

	void ApplySlotMetadata(FDesignNode& Node, const FPenSlotMetadata& SlotMetadata)
	{
		if (!SlotMetadata.bHasSlot)
		{
			return;
		}

		// Preserve source slot keys for source truth (same key family as the Figma parser); only source.slot.empty is the supplementary explicit-empty hint.
		Node.Metadata.Add(ParserSharedSemantics::SourceSlotPresentMetadataKey, TEXT("true"));
		Node.Metadata.Add(ParserSharedSemantics::SourceSlotCountMetadataKey, FString::FromInt(SlotMetadata.SlotNodeCount));
		if (SlotMetadata.SlotNodeCount == 0)
		{
			Node.Metadata.Add(ParserSharedSemantics::SourceSlotEmptyMetadataKey, TEXT("true"));
		}
	}

	void ApplySemanticHints(FDesignNode& Node, const bool bPenPlaceholder, const FPenSlotMetadata& SlotMetadata)
	{
		const bool bTextLikeNode = IsTextLikeNode(Node);
		const bool bNameLooksLikePlaceholder =
			!bTextLikeNode && Node.Name.Contains(TEXT("placeholder"), ESearchCase::IgnoreCase);
		const bool bNameLooksLikeSlot =
			!bTextLikeNode && Node.Name.Contains(TEXT("slot"), ESearchCase::IgnoreCase);
		const bool bNameLooksLikeContentSlot =
			!bTextLikeNode && SlotMetadata.bHasSlot && Node.Name.Contains(TEXT("content"), ESearchCase::IgnoreCase);
		// Placeholder surface is the runtime contract; present/count remain preservation-only metadata.
		Node.LayoutData.bSemanticPlaceholderSurface =
			!bTextLikeNode
			&& (bPenPlaceholder
				|| SlotMetadata.bHasSlot
				|| SlotMetadata.SlotNodeCount > 0
				|| bNameLooksLikePlaceholder
				|| bNameLooksLikeContentSlot
				|| bNameLooksLikeSlot);
	}

	void ParseTextRunFromObject(
		const TSharedPtr<FJsonObject>& SegmentObj,
		const FDesignVariableResolver& Resolver,
		FDesignTextRun& OutRun)
	{
		if (!SegmentObj.IsValid())
		{
			return;
		}

		FString SegmentText;
		if (!SegmentObj->TryGetStringField(TEXT("content"), SegmentText))
		{
			SegmentObj->TryGetStringField(TEXT("text"), SegmentText);
		}
		OutRun.Content = Resolver.ResolveString(SegmentText);

		TSharedPtr<FJsonObject> StyleObj = SegmentObj;
		const TSharedPtr<FJsonObject>* NestedStyle = nullptr;
		if (SegmentObj->TryGetObjectField(TEXT("style"), NestedStyle) && NestedStyle && NestedStyle->IsValid())
		{
			StyleObj = *NestedStyle;
		}
		else if (SegmentObj->TryGetObjectField(TEXT("textStyle"), NestedStyle) && NestedStyle && NestedStyle->IsValid())
		{
			StyleObj = *NestedStyle;
		}

		FString FontFamily;
		if (StyleObj->TryGetStringField(TEXT("fontFamily"), FontFamily))
		{
			OutRun.FontFamily = Resolver.ResolveString(FontFamily);
		}
		StyleObj->TryGetStringField(TEXT("fontPostScriptName"), OutRun.FontPostScriptName);

		float FontSize = 0.0f;
		if (TryGetResolvedNumberField(StyleObj, TEXT("fontSize"), Resolver, FontSize))
		{
			OutRun.FontSize = FontSize;
		}
		StyleObj->TryGetStringField(TEXT("fontWeight"), OutRun.FontWeight);
		StyleObj->TryGetStringField(TEXT("fontStyle"), OutRun.FontStyle);

		float LineHeight = 0.0f;
		if (TryGetResolvedNumberField(StyleObj, TEXT("lineHeight"), Resolver, LineHeight))
		{
			OutRun.LineHeight = LineHeight;
		}

		float LetterSpacing = 0.0f;
		if (TryGetResolvedNumberField(StyleObj, TEXT("letterSpacing"), Resolver, LetterSpacing))
		{
			OutRun.LetterSpacing = LetterSpacing;
		}

		bool bUnderline = false;
		if (TryGetResolvedBoolField(StyleObj, TEXT("underline"), Resolver, bUnderline))
		{
			OutRun.bUnderline = bUnderline;
		}

		bool bStrikethrough = false;
		if (TryGetResolvedBoolField(StyleObj, TEXT("strikethrough"), Resolver, bStrikethrough))
		{
			OutRun.bStrikethrough = bStrikethrough;
		}

		StyleObj->TryGetStringField(TEXT("href"), OutRun.Href);
	}

	void ParseNodeContentField(
		const TSharedPtr<FJsonValue>& ContentField,
		const FDesignVariableResolver& Resolver,
		TSharedPtr<FDesignNode>& Node)
	{
		if (!ContentField.IsValid())
		{
			return;
		}

		if (ContentField->Type == EJson::String)
		{
			Node->Text.Content = Resolver.ResolveString(ContentField->AsString());
			return;
		}

		if (ContentField->Type != EJson::Array)
		{
			return;
		}

		Node->Metadata.Add(TEXT("text.content.raw"), DesignJsonUtils::ToPrettyString(ContentField));
		FString CombinedContent;
		Node->Text.TextRuns.Reset();
		for (const TSharedPtr<FJsonValue>& SegmentVal : ContentField->AsArray())
		{
			if (!SegmentVal.IsValid())
			{
				continue;
			}

			if (SegmentVal->Type == EJson::String)
			{
				FDesignTextRun SegmentRun;
				SegmentRun.Content = Resolver.ResolveString(SegmentVal->AsString());
				CombinedContent += SegmentRun.Content;
				Node->Text.TextRuns.Add(MoveTemp(SegmentRun));
				continue;
			}

			if (SegmentVal->Type != EJson::Object)
			{
				continue;
			}

			const TSharedPtr<FJsonObject>& SegmentObj = SegmentVal->AsObject();
			if (!SegmentObj.IsValid())
			{
				continue;
			}

			FDesignTextRun SegmentRun;
			ParseTextRunFromObject(SegmentObj, Resolver, SegmentRun);
			if (!SegmentRun.Content.IsEmpty())
			{
				CombinedContent += SegmentRun.Content;
			}
			Node->Text.TextRuns.Add(MoveTemp(SegmentRun));
		}

		Node->Text.Content = CombinedContent;
	}

	TSharedPtr<FDesignNode> ParseDescendantOverrideNode(
		const TSharedPtr<FJsonObject>& JsonObj,
		const FDesignVariableResolver& Resolver)
	{
		if (!JsonObj.IsValid())
		{
			return nullptr;
		}

		TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
		Node->Text.FontSize = 0.0f;

		FString RawContent;
		if (JsonObj->TryGetStringField(TEXT("content"), RawContent))
		{
			Node->Text.Content = Resolver.ResolveString(RawContent);
		}

		PenParser::ApplyNodeTextStyleProperties(JsonObj, *Node, Resolver);
		if (JsonObj->HasField(TEXT("fill")))
		{
			PenParser::ParseNodeFillProperties(JsonObj->TryGetField(TEXT("fill")), *Node, Resolver);
		}
		PenParser::ParseNodeStrokeProperties(JsonObj, *Node, Resolver);
		PenParser::ApplyNodeBoxProperties(JsonObj, *Node, EDesignNodeType::Frame, Resolver);
		TryGetResolvedBoolField(JsonObj, TEXT("enabled"), Resolver, Node->Transform.bEnabled);
		PenParser::ApplyNodeLayoutProperties(JsonObj, *Node, Resolver, true);
		JsonObj->TryGetStringField(TEXT("name"), Node->Name);
		PenParser::AppendParsedChildren(JsonObj->TryGetField(TEXT("children")), Resolver, Node->Children);
		PenParserNodeExtensions::FinalizePenPaddingRuntimeWrapPolicy(Node);
		PenParserNodeExtensions::ParsePenSemanticMetadata(JsonObj, *Node, Resolver);
		return Node;
	}

	void ParseDescendants(
		const TSharedPtr<FJsonObject>& JsonObj,
		TSharedPtr<FDesignNode>& Node,
		const FDesignVariableResolver& Resolver)
	{
		const TSharedPtr<FJsonValue> DescField = JsonObj->TryGetField(TEXT("descendants"));
		if (!DescField.IsValid() || DescField->Type != EJson::Object)
		{
			return;
		}

		const TSharedPtr<FJsonObject>& DescObj = DescField->AsObject();
		for (const auto& Pair : DescObj->Values)
		{
			if (Pair.Value->Type != EJson::Object)
			{
				continue;
			}

			const TSharedPtr<FJsonObject>& OverrideObj = Pair.Value->AsObject();
			if (OverrideObj->HasField(TEXT("type")))
			{
				if (TSharedPtr<FDesignNode> FullNode = PenParser::ParseNode(OverrideObj, Resolver))
				{
					Node->Descendants.Add(FString(*Pair.Key), FullNode);
				}
				continue;
			}

			if (TSharedPtr<FDesignNode> DescNode = ParseDescendantOverrideNode(OverrideObj, Resolver))
			{
				Node->Descendants.Add(FString(*Pair.Key), DescNode);
			}
		}
	}
}

void PenParserNodeExtensions::FinalizePenPaddingRuntimeWrapPolicy(const TSharedPtr<FDesignNode>& Node)
{
	if (!Node.IsValid()
		|| Node->LayoutData.Layout != EDesignLayout::Horizontal
		&& Node->LayoutData.Layout != EDesignLayout::Vertical)
	{
		return;
	}

	bool bHasExplicitChildPosition = false;
	for (const TSharedPtr<FDesignNode>& Child : Node->Children)
	{
		if (!Child.IsValid() || Child->LayoutData.bAbsoluteLayoutPositioning)
		{
			continue;
		}

		if (Child->Transform.HasExplicitPosition())
		{
			bHasExplicitChildPosition = true;
			break;
		}
	}

	Node->LayoutData.PaddingRequiresRuntimeWrap = !bHasExplicitChildPosition;
}

void PenParserNodeExtensions::ParsePenSemanticMetadata(
	const TSharedPtr<FJsonObject>& JsonObj,
	FDesignNode& Node,
	const FDesignVariableResolver& Resolver)
{
	bool bPenPlaceholder = false;
	FlexibleFieldParsing::TryGetResolvedBoolField(JsonObj, TEXT("placeholder"), Resolver, bPenPlaceholder);

	const FPenSlotMetadata SlotMetadata = ParseSlotMetadata(JsonObj);
	ApplySlotMetadata(Node, SlotMetadata);
	ApplySemanticHints(Node, bPenPlaceholder, SlotMetadata);
}

void PenParserNodeExtensions::ParseNodeExtended(
	const TSharedPtr<FJsonObject>& JsonObj,
	TSharedPtr<FDesignNode>& Node,
	const FDesignVariableResolver& Resolver)
{
	ParseNodeContentField(JsonObj->TryGetField(TEXT("content")), Resolver, Node);
	PenParser::ApplyNodeTextStyleProperties(JsonObj, *Node, Resolver);
	float LineHeight = 0.0f;
	if (TryGetResolvedNumberField(JsonObj, TEXT("lineHeight"), Resolver, LineHeight) && LineHeight > 0.0f)
	{
		Node->Text.LineHeight = LineHeight;
	}
	else if (Node->Type == EDesignNodeType::Text)
	{
		Node->Text.LineHeight = 0.0f;
	}

	float LetterSpacing = 0.0f;
	if (TryGetResolvedNumberField(JsonObj, TEXT("letterSpacing"), Resolver, LetterSpacing))
	{
		Node->Text.SetLetterSpacingValue(LetterSpacing);
	}

	{
		FString TextAlign;
		if (JsonObj->TryGetStringField(TEXT("textAlign"), TextAlign))
		{
			Node->Text.TextAlign = ParserSharedSemantics::ParseTextAlignToken(TextAlign);
		}
	}
	{
		FString TextAlignVertical;
		if (JsonObj->TryGetStringField(TEXT("textAlignVertical"), TextAlignVertical))
		{
			Node->Text.TextAlignVertical = PenParser::ParseTextAlignVertical(TextAlignVertical);
		}
	}
	{
		FString TextGrowth;
		if (JsonObj->TryGetStringField(TEXT("textGrowth"), TextGrowth))
		{
			Node->Text.TextGrowth = PenParser::ParseTextGrowth(TextGrowth);
		}
	}

	{
		FString RawIconFontName;
		if (JsonObj->TryGetStringField(TEXT("icon"), RawIconFontName))
		{
			Node->Text.IconFontName = RawIconFontName;
		}
	}
	{
		FString RawIconFontFamily;
		if (JsonObj->TryGetStringField(TEXT("library"), RawIconFontFamily))
		{
			Node->Text.IconFontFamily = Resolver.ResolveString(RawIconFontFamily);
		}
	}
	TryGetResolvedNumberField(JsonObj, TEXT("weight"), Resolver, Node->Text.IconWeight);
	TryGetResolvedNumberField(JsonObj, TEXT("innerRadius"), Resolver, Node->Shape.InnerRadius);
	TryGetResolvedNumberField(JsonObj, TEXT("startAngle"), Resolver, Node->Shape.StartAngle);
	TryGetResolvedNumberField(JsonObj, TEXT("sweepAngle"), Resolver, Node->Shape.SweepAngle);
	TryGetResolvedNumberField(JsonObj, TEXT("polygonCount"), Resolver, Node->Shape.PolygonCount);
	JsonObj->TryGetStringField(TEXT("geometry"), Node->Shape.Geometry);
	JsonObj->TryGetStringField(TEXT("fillRule"), Node->Shape.FillRule);

	const FString EffectKey = JsonObj->HasField(TEXT("effect")) ? TEXT("effect") : TEXT("effects");
	if (JsonObj->HasField(EffectKey))
	{
		TArray<TSharedPtr<FJsonValue>> EffectsArr;
		const TSharedPtr<FJsonValue>& EffectField = JsonObj->TryGetField(EffectKey);
		if (EffectField->Type == EJson::Array)
		{
			EffectsArr = EffectField->AsArray();
			Node->Visual.bEffectsExplicitClear = EffectsArr.Num() == 0;
		}
		else if (EffectField->Type == EJson::Object)
		{
			EffectsArr.Add(EffectField);
		}

		for (const TSharedPtr<FJsonValue>& EffectVal : EffectsArr)
		{
			const TSharedPtr<FJsonObject>& EffectObj = EffectVal->AsObject();
			if (!EffectObj.IsValid())
			{
				continue;
			}

			FDesignEffect Effect;
			TryGetResolvedBoolField(EffectObj, TEXT("enabled"), Resolver, Effect.bEnabled);
			EffectObj->TryGetStringField(TEXT("blendMode"), Effect.BlendMode);

			FString TypeStr;
			EffectObj->TryGetStringField(TEXT("type"), TypeStr);
			if (TypeStr == TEXT("blur"))
			{
				Effect.Type = EDesignEffectType::Blur;
			}
			else if (TypeStr == TEXT("background_blur"))
			{
				Effect.Type = EDesignEffectType::BackgroundBlur;
			}
			else if (TypeStr == TEXT("shadow"))
			{
				Effect.Type = EDesignEffectType::Shadow;
			}

			TryGetResolvedNumberField(EffectObj, TEXT("radius"), Resolver, Effect.Radius);
			if (Effect.Type == EDesignEffectType::Shadow)
			{
				Effect.Shadow.BlendMode = Effect.BlendMode;

				FString ShadowTypeStr;
				if (EffectObj->TryGetStringField(TEXT("shadowType"), ShadowTypeStr))
				{
					Effect.Shadow.bInner = (ShadowTypeStr == TEXT("inner"));
				}
				else
				{
					TryGetResolvedBoolField(EffectObj, TEXT("inner"), Resolver, Effect.Shadow.bInner);
				}

				const TSharedPtr<FJsonValue>& OffsetField = EffectObj->TryGetField(TEXT("offset"));
				if (OffsetField.IsValid() && OffsetField->Type == EJson::Object)
				{
					const TSharedPtr<FJsonObject>& OffsetObj = OffsetField->AsObject();
					TryGetResolvedNumberField(OffsetObj, TEXT("x"), Resolver, Effect.Shadow.OffsetX);
					TryGetResolvedNumberField(OffsetObj, TEXT("y"), Resolver, Effect.Shadow.OffsetY);
				}
				else
				{
					TryGetResolvedNumberField(EffectObj, TEXT("offsetX"), Resolver, Effect.Shadow.OffsetX);
					TryGetResolvedNumberField(EffectObj, TEXT("offsetY"), Resolver, Effect.Shadow.OffsetY);
				}

				TryGetResolvedNumberField(EffectObj, TEXT("blur"), Resolver, Effect.Shadow.Blur);
				TryGetResolvedNumberField(EffectObj, TEXT("spread"), Resolver, Effect.Shadow.Spread);

				FString ShadowColorStr = DesignColorUtils::DefaultShadowColorHex;
				EffectObj->TryGetStringField(TEXT("color"), ShadowColorStr);
				if (FDesignVariableResolver::IsVariableRef(ShadowColorStr))
				{
					Effect.Shadow.ColorSourceVar = ShadowColorStr;
				}
				Effect.Shadow.Color = Resolver.ResolveColor(ShadowColorStr);
			}

			Node->Visual.Effects.Add(MoveTemp(Effect));
		}
	}

	JsonObj->TryGetStringField(TEXT("ref"), Node->Ref);
	TryGetResolvedBoolField(JsonObj, TEXT("reusable"), Resolver, Node->bReusable);
	ParseDescendants(JsonObj, Node, Resolver);
}

void PenParserNodeExtensions::SynthesizeStickyCard(const TSharedPtr<FDesignNode>& Node)
{
	if (!Node.IsValid())
	{
		return;
	}

	// Sticky-card theme and geometry as rendered by the Pencil canvas for
	// designer sticky nodes (StickyNode view): one header band with the type
	// label, a monospace body, and a prompt-only copy footer.
	struct FStickyTheme
	{
		const TCHAR* HeaderText;
		const TCHAR* HeaderBackground;
		const TCHAR* Background;
		const TCHAR* OutlineColor;
		const TCHAR* TextColor;
		float ContentMinHeight;
		bool bHasFooter;
	};

	const FStickyTheme* Theme = nullptr;
	if (Node->Type == EDesignNodeType::Note)
	{
		static const FStickyTheme NoteTheme = { TEXT("Note"), TEXT("#FFF1D6"), TEXT("#FFF7E5"), TEXT("#8B6311"), TEXT("#664500"), 150.0f, false };
		Theme = &NoteTheme;
	}
	else if (Node->Type == EDesignNodeType::Context)
	{
		static const FStickyTheme ContextTheme = { TEXT("Context"), TEXT("#FFFFFF"), TEXT("#F0F0F0"), TEXT("#767676"), TEXT("#2C2C2C"), 150.0f, false };
		Theme = &ContextTheme;
	}
	else if (Node->Type == EDesignNodeType::Prompt)
	{
		static const FStickyTheme PromptTheme = { TEXT("Prompt"), TEXT("#C3E8FF"), TEXT("#E8F6FF"), TEXT("#009DFF"), TEXT("#006CAF"), 116.0f, true };
		Theme = &PromptTheme;
	}
	if (!Theme)
	{
		return;
	}

	const FString StickyTextContent = Node->Text.Content;
	// Sticky-card geometry replicated from the Pencil canvas StickyNode view:
	// 14px body text on a 1.5em line advance, a 45px header band with 12px inner
	// padding and a 16px chevron, and a 1px dashed (4px on / 4px off) outline.
	constexpr float StickyFontSize = 14.0f;
	constexpr float StickyLineHeight = 1.5f;
	constexpr float HeaderHeight = 45.0f;
	constexpr float StickyMinWidth = 250.0f;
	constexpr float HeaderPaddingPx = 12.0f;
	constexpr float ChevronIconSize = 16.0f;

	const auto MakeStyledText = [](const FString& InId, const FString& InName, const FString& InContent,
		const TCHAR* InFontWeight, const float InFontSize, const TCHAR* InColorHex) -> TSharedPtr<FDesignNode>
	{
		TSharedPtr<FDesignNode> Text = MakeShared<FDesignNode>();
		Text->Id = InId;
		Text->Name = InName;
		Text->Type = EDesignNodeType::Text;
		Text->Text.Content = InContent;
		Text->Text.FontFamily = TEXT("JetBrains Mono");
		Text->Text.FontSize = InFontSize;
		Text->Text.FontWeight = InFontWeight;
		Text->Text.LineHeight = StickyLineHeight;
		Text->Visual.Fills.Add(FDesignFill::FromColor(InColorHex));
		return Text;
	};

	const auto MakeHeader = [&Theme, &Node, &MakeStyledText]() -> TSharedPtr<FDesignNode>
	{
		TSharedPtr<FDesignNode> Header = MakeShared<FDesignNode>();
		Header->Id = Node->Id + TEXT("-sticky-header");
		Header->Name = TEXT("Sticky Header");
		Header->Type = EDesignNodeType::Frame;
		Header->Transform.SetWidthValue(FDesignSize::Fill());
		Header->Transform.SetHeightValue(FDesignSize::Fixed(HeaderHeight));
		Header->LayoutData.Layout = EDesignLayout::Horizontal;
		Header->LayoutData.AlignItems = EDesignAlignItems::Center;
		Header->LayoutData.SetGap(2.0f);
		FDesignPadding HeaderPadding;
		HeaderPadding.Top = HeaderPadding.Right = HeaderPadding.Bottom = HeaderPadding.Left = HeaderPaddingPx;
		Header->LayoutData.SetPadding(HeaderPadding);
		Header->Visual.Fills.Add(FDesignFill::FromColor(Theme->HeaderBackground));
		Header->Visual.Stroke.Align = EDesignStrokeAlign::Inside;
		Header->Visual.Stroke.Thickness.bUniform = true;
		Header->Visual.Stroke.Thickness.Uniform = 1.0f;
		Header->Visual.Stroke.Fill = FDesignFill::FromColor(Theme->OutlineColor);
		Header->Visual.Stroke.DashPattern = { 4.0f, 4.0f };

		Header->Children.Add(MakeStyledText(
			Node->Id + TEXT("-sticky-header-label"), TEXT("Sticky Header Label"),
			Theme->HeaderText, TEXT("500"), StickyFontSize, Theme->TextColor));

		TSharedPtr<FDesignNode> Chevron = MakeShared<FDesignNode>();
		Chevron->Id = Node->Id + TEXT("-sticky-header-chevron");
		Chevron->Name = TEXT("Sticky Header Chevron");
		Chevron->Type = EDesignNodeType::IconFont;
		Chevron->Transform.SetWidthValue(FDesignSize::Fixed(ChevronIconSize));
		Chevron->Transform.SetHeightValue(FDesignSize::Fixed(ChevronIconSize));
		Chevron->Text.IconFontName = TEXT("chevron-down");
		Chevron->Text.IconFontFamily = TEXT("lucide");
		Chevron->Visual.Fills.Add(FDesignFill::FromColor(Theme->TextColor));
		Header->Children.Add(MoveTemp(Chevron));
		return Header;
	};

	const auto MakeBody = [&Theme, &Node, &MakeStyledText, &StickyTextContent]() -> TSharedPtr<FDesignNode>
	{
		TSharedPtr<FDesignNode> Body = MakeShared<FDesignNode>();
		Body->Id = Node->Id + TEXT("-sticky-body");
		Body->Name = TEXT("Sticky Body");
		Body->Type = EDesignNodeType::Frame;
		Body->Transform.SetWidthValue(FDesignSize::Fill());
		Body->Transform.SetHeightValue(FDesignSize::Fit());
		Body->LayoutData.Layout = EDesignLayout::Vertical;
		FDesignPadding BodyPadding;
		BodyPadding.Top = BodyPadding.Right = BodyPadding.Bottom = BodyPadding.Left = 12.0f;
		Body->LayoutData.SetPadding(BodyPadding);

		TSharedPtr<FDesignNode> ContentText = MakeStyledText(
			Node->Id + TEXT("-sticky-content"), TEXT("Sticky Content"),
			StickyTextContent, TEXT("normal"), StickyFontSize, Theme->TextColor);
		ContentText->Transform.SetWidthValue(FDesignSize::Fill());
		ContentText->Transform.SetHeightValue(FDesignSize::Fit(Theme->ContentMinHeight));
		ContentText->Text.TextGrowth = EDesignTextGrowth::FixedWidth;
		Body->Children.Add(MoveTemp(ContentText));
		return Body;
	};

	const auto MakeFooter = [&Theme, &Node, &MakeStyledText]() -> TSharedPtr<FDesignNode>
	{
		TSharedPtr<FDesignNode> Footer = MakeShared<FDesignNode>();
		Footer->Id = Node->Id + TEXT("-sticky-footer");
		Footer->Name = TEXT("Sticky Footer");
		Footer->Type = EDesignNodeType::Frame;
		Footer->Transform.SetWidthValue(FDesignSize::Fill());
		Footer->Transform.SetHeightValue(FDesignSize::Fit());
		Footer->LayoutData.Layout = EDesignLayout::Horizontal;
		Footer->LayoutData.AlignItems = EDesignAlignItems::Center;
		Footer->LayoutData.SetGap(8.0f);
		FDesignPadding FooterPadding;
		FooterPadding.Top = 8.0f;
		Footer->LayoutData.SetPadding(FooterPadding);

		TSharedPtr<FDesignNode> Spacer = MakeShared<FDesignNode>();
		Spacer->Id = Node->Id + TEXT("-sticky-footer-spacer");
		Spacer->Name = TEXT("Sticky Footer Spacer");
		Spacer->Type = EDesignNodeType::Frame;
		Spacer->Transform.SetWidthValue(FDesignSize::Fill());
		Spacer->Transform.SetHeightValue(FDesignSize::Fixed(0.0f));
		Footer->Children.Add(MoveTemp(Spacer));

		TSharedPtr<FDesignNode> CopyButton = MakeShared<FDesignNode>();
		CopyButton->Id = Node->Id + TEXT("-sticky-footer-copy");
		CopyButton->Name = TEXT("Sticky Copy Button");
		CopyButton->Type = EDesignNodeType::Frame;
		CopyButton->Transform.SetHeightValue(FDesignSize::Fit());
		CopyButton->LayoutData.Layout = EDesignLayout::Horizontal;
		CopyButton->LayoutData.AlignItems = EDesignAlignItems::Center;
		CopyButton->LayoutData.JustifyContent = EDesignJustifyContent::Center;
		CopyButton->LayoutData.SetGap(6.0f);
		FDesignPadding ButtonPadding;
		ButtonPadding.Top = 4.0f;
		ButtonPadding.Right = 12.0f;
		ButtonPadding.Bottom = 4.0f;
		ButtonPadding.Left = 10.0f;
		CopyButton->LayoutData.SetPadding(ButtonPadding);
		CopyButton->Visual.CornerRadius.bUniform = true;
		CopyButton->Visual.CornerRadius.Uniform = 6.0f;
		CopyButton->Visual.Fills.Add(FDesignFill::FromColor(Theme->TextColor));

		TSharedPtr<FDesignNode> CopyLabel = MakeStyledText(
			Node->Id + TEXT("-sticky-footer-copy-label"), TEXT("Sticky Copy Label"),
			TEXT("Copy"), TEXT("500"), 12.0f, TEXT("#FFFFFF"));
		CopyButton->Children.Add(MoveTemp(CopyLabel));
		Footer->Children.Add(MoveTemp(CopyButton));
		return Footer;
	};

	// The card itself reuses the sticky node: width keeps the declared sizing
	// (clamped to the 250px sticky minimum for fixed widths), height is always
	// content-fit because the declared sticky height is editor chrome state.
	if (Node->Transform.Width.IsFixed())
	{
		Node->Transform.SetWidthValue(FDesignSize::Fixed(FMath::Max(StickyMinWidth, Node->Transform.Width.Value)));
	}
	else if (!Node->Transform.Width.IsFill())
	{
		Node->Transform.SetWidthValue(FDesignSize::Fixed(StickyMinWidth));
	}
	Node->Transform.SetHeightValue(FDesignSize::Fit());
	Node->Type = EDesignNodeType::Frame;
	Node->LayoutData.Layout = EDesignLayout::Vertical;
	Node->LayoutData.SetClipValue(true);
	Node->Visual.Fills.Reset();
	Node->Visual.Fills.Add(FDesignFill::FromColor(Theme->Background));
	Node->Visual.Stroke.Align = EDesignStrokeAlign::Inside;
	Node->Visual.Stroke.Thickness.bUniform = true;
	Node->Visual.Stroke.Thickness.Uniform = 1.0f;
	Node->Visual.Stroke.Fill = FDesignFill::FromColor(Theme->OutlineColor);
	Node->Visual.CornerRadius.bUniform = true;
	Node->Visual.CornerRadius.Uniform = 8.0f;
	Node->Visual.Effects.Reset();
	Node->Children.Reset();
	Node->Text = FDesignTextNodeData();

	Node->Children.Add(MakeHeader());
	Node->Children.Add(MakeBody());
	if (Theme->bHasFooter)
	{
		Node->Children.Add(MakeFooter());
	}
}
