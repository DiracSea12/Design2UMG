// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "PenParserNodeExtensions.h"
#include "DesignJsonUtils.h"
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

		// Preserve raw Pen slot keys for source truth; only pen.slot.empty is the supplementary explicit-empty hint.
		Node.Metadata.Add(TEXT("pen.slot.present"), TEXT("true"));
		Node.Metadata.Add(TEXT("pen.slot.count"), FString::FromInt(SlotMetadata.SlotNodeCount));
		if (SlotMetadata.SlotNodeCount == 0)
		{
			Node.Metadata.Add(TEXT("pen.slot.empty"), TEXT("true"));
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

		Node->Metadata.Add(TEXT("text.content.raw"), DesignJsonUtils::ToCompactString(ContentField));
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
				SegmentRun.RawJson = DesignJsonUtils::ToCompactString(SegmentVal);
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
			SegmentRun.RawJson = DesignJsonUtils::ToCompactString(SegmentVal);
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
		const FDesignVariableResolver& Resolver,
		const PenParserNodeExtensions::FParseCallbacks& Callbacks)
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

		Callbacks.ApplyNodeTextStyleProperties(JsonObj, *Node, Resolver);
		if (JsonObj->HasField(TEXT("fill")))
		{
			Callbacks.ParseNodeFillProperties(JsonObj->TryGetField(TEXT("fill")), *Node, Resolver);
		}
		Callbacks.ParseNodeStrokeProperties(JsonObj->TryGetField(TEXT("stroke")), *Node, Resolver);
		Callbacks.ApplyNodeBoxProperties(JsonObj, *Node, EDesignNodeType::Frame, Resolver);
		TryGetResolvedBoolField(JsonObj, TEXT("enabled"), Resolver, Node->Transform.bEnabled);
		Callbacks.ApplyNodeLayoutProperties(JsonObj, *Node, Resolver, true);
		JsonObj->TryGetStringField(TEXT("name"), Node->Name);
		Callbacks.AppendParsedChildren(JsonObj->TryGetField(TEXT("children")), Resolver, Node->Children);
		PenParserNodeExtensions::FinalizePenPaddingRuntimeWrapPolicy(Node);
		PenParserNodeExtensions::ParsePenSemanticMetadata(JsonObj, *Node, Resolver);
		return Node;
	}

	void ParseDescendants(
		const TSharedPtr<FJsonObject>& JsonObj,
		TSharedPtr<FDesignNode>& Node,
		const FDesignVariableResolver& Resolver,
		const PenParserNodeExtensions::FParseCallbacks& Callbacks)
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
				if (TSharedPtr<FDesignNode> FullNode = Callbacks.ParseNode(OverrideObj, Resolver))
				{
					Node->Descendants.Add(Pair.Key, FullNode);
				}
				continue;
			}

			if (TSharedPtr<FDesignNode> DescNode = ParseDescendantOverrideNode(OverrideObj, Resolver, Callbacks))
			{
				Node->Descendants.Add(Pair.Key, DescNode);
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

	Node->LayoutData.bPaddingRequiresRuntimeWrap = !bHasExplicitChildPosition;
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
	const FDesignVariableResolver& Resolver,
	const FParseCallbacks& Callbacks)
{
	ParseNodeContentField(JsonObj->TryGetField(TEXT("content")), Resolver, Node);
	Callbacks.ApplyNodeTextStyleProperties(JsonObj, *Node, Resolver);
	TryGetResolvedNumberField(JsonObj, TEXT("lineHeight"), Resolver, Node->Text.LineHeight);

	float LetterSpacing = 0.0f;
	if (TryGetResolvedNumberField(JsonObj, TEXT("letterSpacing"), Resolver, LetterSpacing))
	{
		Node->Text.SetLetterSpacingValue(LetterSpacing);
	}

	{
		FString TextAlign;
		if (JsonObj->TryGetStringField(TEXT("textAlign"), TextAlign))
		{
			Node->Text.TextAlign = Callbacks.ParseTextAlign(TextAlign);
		}
	}
	{
		FString TextAlignVertical;
		if (JsonObj->TryGetStringField(TEXT("textAlignVertical"), TextAlignVertical))
		{
			Node->Text.TextAlignVertical = Callbacks.ParseTextAlignVertical(TextAlignVertical);
		}
	}
	{
		FString TextGrowth;
		if (JsonObj->TryGetStringField(TEXT("textGrowth"), TextGrowth))
		{
			Node->Text.TextGrowth = Callbacks.ParseTextGrowth(TextGrowth);
		}
	}

	JsonObj->TryGetStringField(TEXT("iconFontName"), Node->Text.IconFontName);
	{
		FString RawIconFontFamily;
		if (JsonObj->TryGetStringField(TEXT("iconFontFamily"), RawIconFontFamily))
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

				FString ShadowColorStr = TEXT("#00000040");
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
	ParseDescendants(JsonObj, Node, Resolver, Callbacks);
}
