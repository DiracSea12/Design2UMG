// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "PenParserInternal.h"
#include "DesignSchema.h"
#include "DesignVariableResolver.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "ParserSharedSemantics.h"
#include "DesignSchemaSourceExtensions.h"

namespace PenParser
{
void ApplyNodeBoxProperties(const TSharedPtr<FJsonObject>& JsonObj, FDesignNode& OutNode, EDesignNodeType SizeNodeType, const FDesignVariableResolver& Resolver)
{
	if (JsonObj->HasField(TEXT("width")))
	{
		OutNode.Transform.SetWidthValue(ParseSize(JsonObj->TryGetField(TEXT("width")), SizeNodeType, Resolver));
	}
	if (JsonObj->HasField(TEXT("height")))
	{
		OutNode.Transform.SetHeightValue(ParseSize(JsonObj->TryGetField(TEXT("height")), SizeNodeType, Resolver));
	}
	if (JsonObj->HasField(TEXT("x")))
	{
		if (TryGetResolvedNumberField(JsonObj, TEXT("x"), Resolver, OutNode.Transform.X))
			OutNode.Transform.ExplicitX = OutNode.Transform.X;
		else
			OutNode.Transform.ExplicitX.Reset();
	}
	if (JsonObj->HasField(TEXT("y")))
	{
		if (TryGetResolvedNumberField(JsonObj, TEXT("y"), Resolver, OutNode.Transform.Y))
			OutNode.Transform.ExplicitY = OutNode.Transform.Y;
		else
			OutNode.Transform.ExplicitY.Reset();
	}
}

void ApplyNodeLayoutProperties(const TSharedPtr<FJsonObject>& JsonObj, FDesignNode& OutNode, const FDesignVariableResolver& Resolver)
{
	FString LayoutPosition;
	if (JsonObj->TryGetStringField(TEXT("layoutPosition"), LayoutPosition))
		OutNode.LayoutData.bAbsoluteLayoutPositioning = LayoutPosition == TEXT("absolute");
	if (JsonObj->HasField(TEXT("opacity")))
	{
		if (TryGetResolvedNumberField(JsonObj, TEXT("opacity"), Resolver, OutNode.Transform.Opacity))
			OutNode.Transform.ExplicitOpacity = OutNode.Transform.Opacity;
		else
			OutNode.Transform.ExplicitOpacity.Reset();
	}
	if (JsonObj->HasField(TEXT("gap")))
	{
		float ResolvedGap = OutNode.LayoutData.ExplicitGap.Get(0.0f);
		if (TryGetResolvedNumberField(JsonObj, TEXT("gap"), Resolver, ResolvedGap))
			OutNode.LayoutData.SetGap(ResolvedGap);
	}
	if (JsonObj->HasField(TEXT("padding")))
	{
		OutNode.LayoutData.SetPadding(ParsePadding(JsonObj->TryGetField(TEXT("padding")), Resolver));
	}
	OutNode.LayoutData.PaddingRequiresRuntimeWrap =
		OutNode.LayoutData.Layout == EDesignLayout::None
		|| OutNode.LayoutData.Layout == EDesignLayout::Wrap
		|| OutNode.LayoutData.Layout == EDesignLayout::Grid;
	FString JustifyContent;
	if (JsonObj->TryGetStringField(TEXT("justifyContent"), JustifyContent))
		OutNode.LayoutData.JustifyContent = ParserSharedSemantics::ParseJustifyContentToken(JustifyContent);
	FString AlignItems;
	if (JsonObj->TryGetStringField(TEXT("alignItems"), AlignItems))
		OutNode.LayoutData.AlignItems = ParserSharedSemantics::ParseAlignItemsToken(AlignItems);
	if (JsonObj->HasField(TEXT("clip")))
	{
		bool bClipValue = false;
		if (TryGetResolvedBoolField(JsonObj, TEXT("clip"), Resolver, bClipValue))
			OutNode.LayoutData.SetClipValue(bClipValue);
		else
			OutNode.LayoutData.ExplicitClip.Reset();
	}

}

void ApplyNodeTextStyleProperties(const TSharedPtr<FJsonObject>& JsonObj, FDesignNode& OutNode, const FDesignVariableResolver& Resolver)
{
	// Pencil's paragraph renderer distributes authored line-height leading on both sides.
	OutNode.Text.bSourceHalfLeadingBaseline = true;
	OutNode.Text.bSourceRoundLineHeightToPixels = true;
	FString FontFamily;
	if (JsonObj->TryGetStringField(TEXT("fontFamily"), FontFamily))
		OutNode.Text.FontFamily = Resolver.ResolveString(FontFamily);
	JsonObj->TryGetStringField(TEXT("fontPostScriptName"), OutNode.Text.FontPostScriptName);
	TryGetResolvedNumberField(JsonObj, TEXT("fontSize"), Resolver, OutNode.Text.FontSize);
	FString FontWeight, FontStyle;
	if (JsonObj->TryGetStringField(TEXT("fontWeight"), FontWeight)) OutNode.Text.FontWeight = Resolver.ResolveString(FontWeight);
	if (JsonObj->TryGetStringField(TEXT("fontStyle"), FontStyle)) OutNode.Text.FontStyle = Resolver.ResolveString(FontStyle);
	float LetterSpacing = 0.0f;
	if (TryGetResolvedNumberField(JsonObj, TEXT("letterSpacing"), Resolver, LetterSpacing))
	{
		OutNode.Text.SetLetterSpacingValue(LetterSpacing);
	}
	TryGetResolvedBoolField(JsonObj, TEXT("underline"), Resolver, OutNode.Text.bUnderline);
	TryGetResolvedBoolField(JsonObj, TEXT("strikethrough"), Resolver, OutNode.Text.bStrikethrough);
	JsonObj->TryGetStringField(TEXT("href"), OutNode.Text.Href);
}
}

namespace PenParser
{
FDesignNodePatch CaptureNodePatch(const TSharedPtr<FJsonObject>& JsonObj, const FDesignNode& Node, const FDesignVariableResolver& Resolver)
{
	FDesignNodePatch Patch;
	Patch.PencilProperties = Node.Source().Pencil.Properties;
	for (const auto& Pair : Node.Descendants) Patch.Descendants.Add(Pair.Key, MakeShared<FDesignNodeOverride>(Pair.Value));
	if (JsonObj->HasField(TEXT("ref"))) Patch.Ref = Node.Ref;
	if (JsonObj->HasField(TEXT("reusable"))) Patch.Reusable = Node.bReusable;
	if (JsonObj->HasField(TEXT("theme"))) Patch.Theme = Node.Theme;
	if (JsonObj->HasField(TEXT("layoutPosition"))) Patch.AbsoluteLayoutPositioning = Node.LayoutData.bAbsoluteLayoutPositioning;
	if (JsonObj->HasField(TEXT("context"))) Patch.SourceContext = Node.Source().Pencil.Context;
	if (JsonObj->HasField(TEXT("model"))) Patch.SourceModel = Node.Source().Pencil.Model;
	if (JsonObj->HasField(TEXT("metadata"))) Patch.SourceMetadataJson = Node.Source().Pencil.MetadataJson;
	if (JsonObj->HasField(TEXT("slot"))) Patch.SourceSlotRecommendations.Emplace(Node.Source().Pencil.SlotRecommendations);
	if (JsonObj->HasTypedField<EJson::String>(TEXT("name"))) Patch.Name = Node.Name;
	if (JsonObj->HasTypedField<EJson::String>(TEXT("fontFamily"))) Patch.FontFamily = Node.Text.FontFamily;
	if (JsonObj->HasTypedField<EJson::String>(TEXT("fontPostScriptName"))) Patch.FontPostScriptName = Node.Text.FontPostScriptName;
	if (JsonObj->HasTypedField<EJson::String>(TEXT("fontWeight"))) Patch.FontWeight = Node.Text.FontWeight;
	if (JsonObj->HasTypedField<EJson::String>(TEXT("fontStyle"))) Patch.FontStyle = Node.Text.FontStyle;
	if (JsonObj->HasTypedField<EJson::String>(TEXT("href"))) Patch.Href = Node.Text.Href;
	if (JsonObj->HasTypedField<EJson::String>(TEXT("icon")) || JsonObj->HasTypedField<EJson::String>(TEXT("iconFontName"))) Patch.IconFontName = Node.Text.IconFontName;
	if (JsonObj->HasTypedField<EJson::String>(TEXT("library")) || JsonObj->HasTypedField<EJson::String>(TEXT("iconFontFamily"))) Patch.IconFontFamily = Node.Text.IconFontFamily;
	if (JsonObj->HasTypedField<EJson::String>(TEXT("geometry"))) Patch.Geometry = Node.Shape.Geometry;
	if (JsonObj->HasField(TEXT("viewBox"))) Patch.ViewBox = Node.Shape.ViewBox;
	if (JsonObj->HasTypedField<EJson::String>(TEXT("fillRule"))) Patch.FillRule = Node.Shape.FillRule;
	if (JsonObj->HasTypedField<EJson::String>(TEXT("layout"))) Patch.Layout = Node.LayoutData.Layout;
	if (JsonObj->HasTypedField<EJson::String>(TEXT("justifyContent"))) Patch.JustifyContent = Node.LayoutData.JustifyContent;
	if (JsonObj->HasTypedField<EJson::String>(TEXT("alignItems"))) Patch.AlignItems = Node.LayoutData.AlignItems;
	if (JsonObj->HasTypedField<EJson::String>(TEXT("textAlign"))) Patch.TextAlign = Node.Text.TextAlign;
	if (JsonObj->HasTypedField<EJson::String>(TEXT("textAlignVertical"))) Patch.TextAlignVertical = Node.Text.TextAlignVertical;
	if (JsonObj->HasTypedField<EJson::String>(TEXT("textGrowth"))) Patch.TextGrowth = Node.Text.TextGrowth;
	if (JsonObj->HasTypedField<EJson::String>(TEXT("strokeAlignment"))) Patch.StrokeAlign = Node.Visual.Stroke.Align;
	if (JsonObj->HasTypedField<EJson::String>(TEXT("strokeLinecap"))) Patch.StrokeCap = Node.Visual.Stroke.Cap;
	if (JsonObj->HasTypedField<EJson::String>(TEXT("strokeLinejoin"))) Patch.StrokeJoin = Node.Visual.Stroke.Join;
	{
		float Value = 0;
		if (TryGetResolvedNumberField(JsonObj, TEXT("rotation"), Resolver, Value)) Patch.Rotation = Value;
	}
	{
		float Value = 0;
		if (TryGetResolvedNumberField(JsonObj, TEXT("layoutGrow"), Resolver, Value)) Patch.LayoutGrow = Value;
	}
	{
		float Value = 0;
		if (TryGetResolvedNumberField(JsonObj, TEXT("fontSize"), Resolver, Value)) Patch.FontSize = Value;
	}
	{
		float Value = 0;
		if (TryGetResolvedNumberField(JsonObj, TEXT("lineHeight"), Resolver, Value)) Patch.LineHeight = Value;
	}
	{
		int32 Value = 0;
		if (TryGetResolvedNumberField(JsonObj, TEXT("weight"), Resolver, Value)) Patch.IconWeight = Value;
	}
	{
		float Value = 0;
		if (TryGetResolvedNumberField(JsonObj, TEXT("innerRadius"), Resolver, Value)) Patch.InnerRadius = Value;
	}
	{
		float Value = 0;
		if (TryGetResolvedNumberField(JsonObj, TEXT("startAngle"), Resolver, Value)) Patch.StartAngle = Value;
	}
	{
		float Value = 0;
		if (TryGetResolvedNumberField(JsonObj, TEXT("sweepAngle"), Resolver, Value)) Patch.SweepAngle = Value;
	}
	{
		int32 Value = 0;
		if (TryGetResolvedNumberField(JsonObj, TEXT("polygonCount"), Resolver, Value)) Patch.PolygonCount = Value;
	}
	{
		bool Value = false;
		if (TryGetResolvedBoolField(JsonObj, TEXT("enabled"), Resolver, Value)) Patch.Enabled = Value;
	}
	{
		bool Value = false;
		if (TryGetResolvedBoolField(JsonObj, TEXT("flipX"), Resolver, Value)) Patch.FlipX = Value;
	}
	{
		bool Value = false;
		if (TryGetResolvedBoolField(JsonObj, TEXT("flipY"), Resolver, Value)) Patch.FlipY = Value;
	}
	{
		bool Value = false;
		if (TryGetResolvedBoolField(JsonObj, TEXT("underline"), Resolver, Value)) Patch.Underline = Value;
	}
	{
		bool Value = false;
		if (TryGetResolvedBoolField(JsonObj, TEXT("strikethrough"), Resolver, Value)) Patch.Strikethrough = Value;
	}
	if (JsonObj->HasField(TEXT("x"))) Patch.X = Node.Transform.ExplicitX;
	if (JsonObj->HasField(TEXT("y"))) Patch.Y = Node.Transform.ExplicitY;
	if (JsonObj->HasField(TEXT("opacity"))) Patch.Opacity = Node.Transform.ExplicitOpacity;
	if (JsonObj->HasField(TEXT("width")) || JsonObj->HasField(TEXT("size"))) Patch.Width = Node.Transform.Width;
	if (JsonObj->HasField(TEXT("height")) || JsonObj->HasField(TEXT("size"))) Patch.Height = Node.Transform.Height;
	if (JsonObj->HasField(TEXT("gap"))) Patch.Gap = Node.LayoutData.ExplicitGap;
	if (JsonObj->HasField(TEXT("padding"))) Patch.Padding = Node.LayoutData.ExplicitPadding;
	// New children without baked coordinates need the inherited inset at runtime.
	// Child coordinates alone cannot revoke ownership of padding authored by another layer.
	if (Patch.Padding.IsSet() || (JsonObj->HasTypedField<EJson::Array>(TEXT("children"))
		&& Node.LayoutData.PaddingRequiresRuntimeWrap.Get(false)))
	{
		Patch.PaddingRequiresRuntimeWrap = Node.LayoutData.PaddingRequiresRuntimeWrap;
	}
	if (JsonObj->HasField(TEXT("clip"))) Patch.Clip = Node.LayoutData.ExplicitClip;
	if (JsonObj->HasField(TEXT("letterSpacing"))) Patch.LetterSpacing = Node.Text.LetterSpacing;
	if (JsonObj->HasField(TEXT("stroke")) && Node.Visual.Stroke.Fill.IsSet()) Patch.StrokeFill.Emplace(Node.Visual.Stroke.Fill.GetValue());
	// Pencil no-fill removes implicit stroke paint unless the fragment authors stroke paint too.
	else if (JsonObj->HasField(TEXT("fill")) && Node.Visual.bFillExplicitClear) Patch.StrokeFill.Emplace();
	if (JsonObj->HasField(TEXT("strokeWidth"))) Patch.StrokeThickness = Node.Visual.Stroke.Thickness;
	if (JsonObj->HasField(TEXT("cornerRadius"))) Patch.CornerRadius = Node.Visual.CornerRadius;
	if (JsonObj->HasField(TEXT("fill"))) Patch.Fills = Node.Visual.Fills;
	if (JsonObj->HasTypedField<EJson::Array>(TEXT("effect")) || JsonObj->HasTypedField<EJson::Object>(TEXT("effect"))
		|| JsonObj->HasTypedField<EJson::Array>(TEXT("effects")) || JsonObj->HasTypedField<EJson::Object>(TEXT("effects")))
	{
		Patch.Effects = Node.Visual.Effects;
	}
	if (JsonObj->HasTypedField<EJson::String>(TEXT("content")) || JsonObj->HasTypedField<EJson::Array>(TEXT("content")))
	{
		Patch.Content = Node.Text.Content;
		Patch.TextRuns = Node.Text.TextRuns;
	}
	if (JsonObj->HasTypedField<EJson::Array>(TEXT("children"))) Patch.Children = Node.Children;
	if (JsonObj->HasField(TEXT("placeholder")) || JsonObj->HasField(TEXT("slot")))
	{
		Patch.SemanticPlaceholderSurface = Node.LayoutData.bSemanticPlaceholderSurface;
	}
	Patch.Metadata = Node.Metadata;
	Patch.Metadata.Remove(TEXT("source.provider"));
	return Patch;
}
}
