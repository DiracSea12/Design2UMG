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

		FUMGWidgetNode ChildWidget = ConvertNode(Child, Document, TMap<FString, FString>(), &Result.Warnings);
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

FUMGWidgetNode FPenToUMGConverter::ConvertNode(const TSharedPtr<FPenNode>& Node, const FPenDocument& Doc, const TMap<FString, FString>& ThemeContext, TArray<FString>* OutWarnings)
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
		return ConvertNode(Expanded, Doc, ActiveTheme, OutWarnings);
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
	case EPenNodeType::Rectangle:
		Widget = CreateContainerWidget(Node);
		break;
	case EPenNodeType::Ellipse:
		{
			Widget = CreateContainerWidget(Node);
			// 用圆角近似椭圆：cornerRadius = min(w, h) / 2，不改写源 Node
			float W = Node->Width.IsFixed() ? Node->Width.Value : 0.0f;
			float H = Node->Height.IsFixed() ? Node->Height.Value : 0.0f;
			if (W > 0 && H > 0)
			{
				FPenCornerRadius EllipseRadius;
				EllipseRadius.bUniform = true;
				EllipseRadius.Uniform = FMath::Min(W, H) / 2.0f;
				FPenVisualMapper::ApplyRoundedCorner(Widget, EllipseRadius);
			}
		}
		break;
	case EPenNodeType::Polygon:
		Widget = CreateContainerWidget(Node);
		UE_LOG(LogPenToUMG, Warning, TEXT("PenToUMG: Polygon '%s' mapped to Border (UMG has no polygon widget)"), *Node->Name);
		if (OutWarnings)
			OutWarnings->Add(FString::Printf(TEXT("Polygon '%s' → Border (UMG 无多边形控件)"), *Node->Name));
		break;
	case EPenNodeType::Path:
		Widget = CreatePathWidget(Node);
		break;
	case EPenNodeType::Line:
		Widget = CreateContainerWidget(Node);
		UE_LOG(LogPenToUMG, Warning, TEXT("PenToUMG: Line '%s' mapped to Border (UMG has no line widget)"), *Node->Name);
		if (OutWarnings)
			OutWarnings->Add(FString::Printf(TEXT("Line '%s' → Border (UMG 无线条控件)"), *Node->Name));
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

		auto ChildWidget = ConvertNode(EffectiveChild, Doc, ActiveTheme, OutWarnings);
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

	const bool bHasVisualFill = (Node->Fill.bEnabled && Node->Fill.Type != EPenFillType::None)
		|| Node->Fills.Num() > 0;
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

	// 多层填充叠加（Overlay 包裹）
	if (Node->HasMultiFill())
		Widget = FPenVisualMapper::WrapWithMultiFill(MoveTemp(Widget), Node);

	if (Widget.Properties.Contains(TEXT("BoxShadowOffsetX")))
		Widget = FPenVisualMapper::WrapWithBoxShadow(MoveTemp(Widget), Node);

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
		Widget.TextStyle.FontSize = Node->FontSize;
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
		Widget.TextStyle.LetterSpacing = Node->LetterSpacing;
		Widget.TextStyle.bHasLetterSpacing = true;
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
		Widget.TextStyle.FontSize = Node->FontSize;
	}

	if (Node->Fill.Type == EPenFillType::Color)
		Widget.Properties.Add(TEXT("Color"), Node->Fill.SolidColor);

	return Widget;
}

FUMGWidgetNode FPenToUMGConverter::CreatePathWidget(const TSharedPtr<FPenNode>& Node)
{
	FUMGWidgetNode Widget;

	// 已被 FigmaImageExporter 转为 Image Fill → 走 Image 路径
	if (Node->Fill.Type == EPenFillType::Image && !Node->Fill.Image.Url.IsEmpty())
	{
		Widget.Type = TEXT("Image");
		Widget.Properties.Add(TEXT("TexturePath"), Node->Fill.Image.Url);
		return Widget;
	}

	// 有 geometry 数据且有颜色填充 → 栅格化；fill:none 的 stroke-only path 走 fallback
	if (!Node->Geometry.IsEmpty() && Node->Fill.Type == EPenFillType::Color)
	{
		int32 W = FMath::Max(1, FMath::RoundToInt(Node->Width.Value));
		int32 H = FMath::Max(1, FMath::RoundToInt(Node->Height.Value));

		Widget.Type = TEXT("Image");
		Widget.Properties.Add(TEXT("PathGeometry"), Node->Geometry);
		Widget.Properties.Add(TEXT("PathFillRule"), Node->FillRule);
		Widget.Properties.Add(TEXT("PathFillColor"), Node->Fill.SolidColor);
		Widget.Properties.Add(TEXT("WidthOverride"), FString::FromInt(W));
		Widget.Properties.Add(TEXT("HeightOverride"), FString::FromInt(H));
		return Widget;
	}

	// Fallback
	Widget.Type = FPenTypeMapper::MapNodeTypeToWidget(Node->Type, Node->Layout);
	return Widget;
}