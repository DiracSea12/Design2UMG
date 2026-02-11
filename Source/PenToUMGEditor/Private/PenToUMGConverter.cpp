// Copyright Epic Games, Inc. All Rights Reserved.

#include "PenToUMGConverter.h"
#include "PenTypeMapper.h"
#include "PenToUMGLog.h"
#include "PenLayoutMapper.h"
#include "PenVisualMapper.h"
#include "PenRefExpander.h"

namespace
{
	bool IsDesignerOnlyNodeType(EPenNodeType Type)
	{
		return Type == EPenNodeType::Connection
			|| Type == EPenNodeType::Note
			|| Type == EPenNodeType::Prompt
			|| Type == EPenNodeType::Context;
	}

	bool ShouldSkipNode(const TSharedPtr<FPenNode>& Node)
	{
		return !Node.IsValid() || !Node->bEnabled || IsDesignerOnlyNodeType(Node->Type);
	}
}

FUMGWidgetData FPenToUMGConverter::Convert(const FPenDocument& Document)
{
	FUMGWidgetData Result;
	Result.WidgetName = TEXT("WBP_FromPen");

	TArray<FUMGWidgetNode> RenderWidgets;
	for (const auto& Child : Document.Children)
	{
		if (ShouldSkipNode(Child) || Child->bReusable)
			continue;

		Child->X = 0.0f;
		Child->Y = 0.0f;

		FUMGWidgetNode ChildWidget = ConvertNode(Child, Document);
		if (!ChildWidget.Type.IsEmpty())
			RenderWidgets.Add(MoveTemp(ChildWidget));
	}

	if (RenderWidgets.Num() == 1)
	{
		Result.RootWidget = MoveTemp(RenderWidgets[0]);
		return Result;
	}

	Result.RootWidget.Type = TEXT("CanvasPanel");
	Result.RootWidget.Name = TEXT("Root");
	Result.RootWidget.Id = TEXT("Root");
	for (auto& W : RenderWidgets)
		Result.RootWidget.Children.Add(MoveTemp(W));

	return Result;
}

FUMGWidgetNode FPenToUMGConverter::ConvertNode(const TSharedPtr<FPenNode>& Node, const FPenDocument& Doc, const TMap<FString, FString>& ThemeContext)
{
	if (ShouldSkipNode(Node))
		return FUMGWidgetNode();

	TMap<FString, FString> ActiveTheme = ThemeContext;
	for (const auto& Pair : Node->Theme)
		ActiveTheme.Add(Pair.Key, Pair.Value);

	if (Node->Type == EPenNodeType::Ref)
	{
		auto Expanded = FPenRefExpander::ExpandRef(Node, Doc);
		if (!Expanded.IsValid())
			return FUMGWidgetNode();

		if (ActiveTheme.Num() > 0)
		{
			FPenVariableResolver Resolver(Doc.Variables);
			Resolver.SetThemeContext(ActiveTheme);
			FPenRefExpander::ReThemeNode(Expanded, Resolver);
		}
		return ConvertNode(Expanded, Doc, ActiveTheme);
	}

	FUMGWidgetNode Widget;

	switch (Node->Type)
	{
	case EPenNodeType::Text:
		Widget = CreateTextWidget(Node);
		break;
	case EPenNodeType::IconFont:
		Widget = CreateIconFontWidget(Node);
		break;
	case EPenNodeType::Frame:
	case EPenNodeType::Group:
		Widget = CreateContainerWidget(Node);
		break;
	case EPenNodeType::Ellipse:
		Widget = CreateContainerWidget(Node);
		UE_LOG(LogPenToUMG, Warning, TEXT("PenToUMG: Ellipse '%s' mapped to Border (UMG has no ellipse widget)"), *Node->Name);
		break;
	case EPenNodeType::Polygon:
		Widget = CreateContainerWidget(Node);
		UE_LOG(LogPenToUMG, Warning, TEXT("PenToUMG: Polygon '%s' mapped to Border (UMG has no polygon widget)"), *Node->Name);
		break;
	case EPenNodeType::Path:
		Widget = CreateContainerWidget(Node);
		UE_LOG(LogPenToUMG, Warning, TEXT("PenToUMG: Path '%s' mapped to Border (UMG has no SVG path widget)"), *Node->Name);
		break;
	case EPenNodeType::Line:
		Widget = CreateContainerWidget(Node);
		UE_LOG(LogPenToUMG, Warning, TEXT("PenToUMG: Line '%s' mapped to Border (UMG has no line widget)"), *Node->Name);
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

	FPenVisualMapper::ApplyVisualProperties(Widget, Node);

	for (const auto& Child : Node->Children)
	{
		if (ShouldSkipNode(Child))
			continue;

		TSharedPtr<FPenNode> EffectiveChild = Child;
		if (Child->Type == EPenNodeType::Ref)
		{
			auto Expanded = FPenRefExpander::ExpandRef(Child, Doc);
			if (Expanded.IsValid())
				EffectiveChild = Expanded;
		}

		auto ChildWidget = ConvertNode(Child, Doc, ActiveTheme);
		if (ChildWidget.Type.IsEmpty())
			continue;

		FPenLayoutMapper::ApplySlotProperties(ChildWidget, EffectiveChild, Node->Layout);
		FPenLayoutMapper::ApplyAlignItems(ChildWidget, Node->AlignItems, Node->Layout);

		const bool bHasFixedSize = EffectiveChild->Width.IsFixed() || EffectiveChild->Height.IsFixed();
		const bool bHasMinSize = (EffectiveChild->Width.IsFit() && EffectiveChild->Width.Value > 0)
			|| (EffectiveChild->Height.IsFit() && EffectiveChild->Height.Value > 0);
		bool bNeedsSizeBox = (Node->Layout == EPenLayout::Vertical || Node->Layout == EPenLayout::Horizontal)
			&& (bHasFixedSize || bHasMinSize);
		if (bNeedsSizeBox)
			Widget.Children.Add(FPenLayoutMapper::WrapWithSizeBox(MoveTemp(ChildWidget), EffectiveChild));
		else
			Widget.Children.Add(MoveTemp(ChildWidget));
	}

	if (Node->Gap > 0)
		FPenLayoutMapper::ApplyGap(Widget.Children, Node->Gap, Node->Layout);

	if (Node->JustifyContent != EPenJustifyContent::Start)
		FPenLayoutMapper::ApplyJustifyContent(Widget.Children, Node->JustifyContent);

	const bool bHasVisualFill = Node->Fill.bEnabled && Node->Fill.Type != EPenFillType::None;
	const bool bHasCornerRadius = Node->CornerRadius.bUniform ? Node->CornerRadius.Uniform > 0
		: (Node->CornerRadius.TopLeft > 0 || Node->CornerRadius.TopRight > 0
			|| Node->CornerRadius.BottomRight > 0 || Node->CornerRadius.BottomLeft > 0);
	bool bNeedsBorder = (Node->Type == EPenNodeType::Frame || Node->Type == EPenNodeType::Group)
		&& Widget.Type != TEXT("CanvasPanel")
		&& (bHasVisualFill || FPenLayoutMapper::HasPadding(Node) || bHasCornerRadius || Node->Stroke.HasStroke());

	if (bNeedsBorder)
		Widget = FPenVisualMapper::WrapWithBorder(MoveTemp(Widget), Node);
	else if (FPenLayoutMapper::HasPadding(Node))
		FPenLayoutMapper::ApplyPadding(Widget, Node->Padding);

	for (const FPenEffect& Effect : Node->Effects)
	{
		if (Effect.bEnabled && Effect.Type == EPenEffectType::BackgroundBlur && Effect.Radius > 0)
			return FPenVisualMapper::WrapWithBackgroundBlur(MoveTemp(Widget), Effect.Radius);
	}

	return Widget;
}

FUMGWidgetNode FPenToUMGConverter::CreateContainerWidget(const TSharedPtr<FPenNode>& Node)
{
	FUMGWidgetNode Widget;
	Widget.Type = FPenTypeMapper::MapNodeTypeToWidget(Node->Type, Node->Layout);
	return Widget;
}

FUMGWidgetNode FPenToUMGConverter::CreateTextWidget(const TSharedPtr<FPenNode>& Node)
{
	FUMGWidgetNode Widget;
	Widget.Type = TEXT("TextBlock");
	Widget.Properties.Add(TEXT("Text"), Node->Content);
	ApplyTextProperties(Widget, Node);
	return Widget;
}

void FPenToUMGConverter::ApplyTextProperties(FUMGWidgetNode& Widget, const TSharedPtr<FPenNode>& Node)
{
	if (Node->FontSize > 0)
	{
		Widget.Properties.Add(TEXT("FontSize"), FString::SanitizeFloat(Node->FontSize));
		Widget.Brush.FontSize = Node->FontSize;
	}

	if (!Node->FontFamily.IsEmpty())
		Widget.Properties.Add(TEXT("FontFamily"), Node->FontFamily);

	if (!Node->FontWeight.IsEmpty())
		Widget.Properties.Add(TEXT("TypefaceFontName"), FPenTypeMapper::MapFontWeight(Node->FontWeight).ToString());

	Widget.Properties.Add(TEXT("Justification"),
		Node->TextAlign == EPenTextAlign::Center ? TEXT("Center") :
		Node->TextAlign == EPenTextAlign::Right ? TEXT("Right") : TEXT("Left"));

	if (Node->LineHeight > 0)
		Widget.Properties.Add(TEXT("LineHeightPercentage"), FString::SanitizeFloat(Node->LineHeight));

	if (Node->LetterSpacing != 0)
	{
		Widget.Properties.Add(TEXT("LetterSpacing"), FString::SanitizeFloat(Node->LetterSpacing));
		Widget.Brush.LetterSpacing = Node->LetterSpacing;
		Widget.Brush.bHasLetterSpacing = true;
	}

	switch (Node->TextGrowth)
	{
	case EPenTextGrowth::FixedWidth:
		Widget.Properties.Add(TEXT("AutoWrapText"), TEXT("true"));
		break;
	case EPenTextGrowth::FixedWidthHeight:
		Widget.Properties.Add(TEXT("AutoWrapText"), TEXT("true"));
		Widget.Properties.Add(TEXT("Clipping"), TEXT("ClipToBounds"));
		break;
	default:
		break;
	}

	if (Node->TextAlignVertical != EPenTextAlignVertical::Top)
	{
		Widget.Properties.Add(TEXT("TextAlignVertical"),
			Node->TextAlignVertical == EPenTextAlignVertical::Middle ? TEXT("Center") : TEXT("Bottom"));
	}
}

FUMGWidgetNode FPenToUMGConverter::CreateIconFontWidget(const TSharedPtr<FPenNode>& Node)
{
	FUMGWidgetNode Widget;
	Widget.Type = TEXT("TextBlock");

	if (!Node->IconFontFamily.IsEmpty())
		Widget.Properties.Add(TEXT("FontFamily"), Node->IconFontFamily);

	Widget.Properties.Add(TEXT("IconName"), Node->IconFontName);
	if (!Node->IconFontName.IsEmpty())
		Widget.Properties.Add(TEXT("Text"), Node->IconFontName);

	if (Node->FontSize > 0)
	{
		Widget.Properties.Add(TEXT("FontSize"), FString::SanitizeFloat(Node->FontSize));
		Widget.Brush.FontSize = Node->FontSize;
	}

	if (Node->Fill.Type == EPenFillType::Color)
		Widget.Properties.Add(TEXT("Color"), Node->Fill.SolidColor);

	return Widget;
}