// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#include "Design2UMGConverter.h"
#include "PenTypeMapper.h"
#include "Design2UMGLog.h"
#include "Design2UMGSettings.h"
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

FUMGWidgetData FDesign2UMGConverter::Convert(const FPenDocument& Document)
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

FUMGWidgetNode FDesign2UMGConverter::ConvertNode(const TSharedPtr<FPenNode>& Node, const FPenDocument& Doc, const TMap<FString, FString>& ThemeContext, TArray<FString>* OutWarnings)
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
			float W = Node->Width.IsFixed() ? Node->Width.Value : 0.0f;
			float H = Node->Height.IsFixed() ? Node->Height.Value : 0.0f;
			// 有尺寸、有颜色填充、无描边 → 生成椭圆 SVG path，走栅格化
			// 有描边时走 fallback（Border 支持 OutlineThickness，Image 不支持）
			bool bHasStroke = Node->Stroke.Fill.Type != EPenFillType::None;
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
					FPenCornerRadius EllipseRadius;
					EllipseRadius.bUniform = true;
					EllipseRadius.Uniform = FMath::Min(W, H) / 2.0f;
					FPenVisualMapper::ApplyRoundedCorner(Widget, EllipseRadius);
				}
			}
		}
		break;
	case EPenNodeType::Polygon:
		Widget = CreateContainerWidget(Node);
		UE_LOG(LogDesign2UMG, Warning, TEXT("Design2UMG: Polygon '%s' mapped to Border (UMG has no polygon widget)"), *Node->Name);
		if (OutWarnings)
			OutWarnings->Add(FString::Printf(TEXT("Polygon '%s' → Border (UMG 无多边形控件)"), *Node->Name));
		break;
	case EPenNodeType::Path:
		Widget = CreatePathWidget(Node);
		break;
	case EPenNodeType::Line:
		Widget = CreateContainerWidget(Node);
		UE_LOG(LogDesign2UMG, Warning, TEXT("Design2UMG: Line '%s' mapped to Border (UMG has no line widget)"), *Node->Name);
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

	UE_LOG(LogDesign2UMG, Verbose, TEXT("ConvertNode: '%s' id=%s type=%s bounds=(%.1f,%.1f,%.1f,%.1f) layout=%d children=%d"),
		*Widget.Name, *Widget.Id, *Widget.Type,
		Widget.Bounds.X, Widget.Bounds.Y, Widget.Bounds.W, Widget.Bounds.H,
		(int32)Node->Layout, Node->Children.Num());

	FPenVisualMapper::ApplyVisualProperties(Widget, Node);

	// 预处理：在递归子节点之前，解析 FillContainer 尺寸写入 PenNode
	// 这样子节点递归时 Width.Value / Height.Value 已经是正确的像素值
	// 直接修改原始子节点（Ref 节点的 Width/Height 会被 ExpandRef 传递给展开节点）
	if (Widget.Bounds.W > 0 && Widget.Bounds.H > 0
		&& (Node->Layout == EPenLayout::Horizontal || Node->Layout == EPenLayout::Vertical))
	{
		const float PadL = Node->Padding.Left, PadR = Node->Padding.Right;
		const float PadT = Node->Padding.Top, PadB = Node->Padding.Bottom;
		const float InnerW = Widget.Bounds.W - PadL - PadR;
		const float InnerH = Widget.Bounds.H - PadT - PadB;
		const bool bHorizontal = (Node->Layout == EPenLayout::Horizontal);

		// 收集有效子节点（不展开 Ref，直接用原始节点）
		TArray<TSharedPtr<FPenNode>> EffChildren;
		for (const auto& Child : Node->Children)
		{
			if (!ShouldSkipNode(Child))
				EffChildren.Add(Child);
		}

		// 统计主轴 Fixed 占用和 Fill 数量
		float FixedMainSum = 0.0f;
		int32 FillCount = 0;
		const float TotalGap = EffChildren.Num() > 1 ? Node->Gap * (EffChildren.Num() - 1) : 0.0f;
		for (const auto& EC : EffChildren)
		{
			const FPenSize& MainSize = bHorizontal ? EC->Width : EC->Height;
			if (MainSize.IsFill())
				FillCount++;
			else
				FixedMainSum += MainSize.Value;
		}

		// 辅助 lambda：只修改 Value 不改 Mode，保留 FillContainer 语义给下游 Slot 映射
		// 同时设置 bHas 标志确保 Ref 节点的 ExpandRef 传递
		auto ResolveSize = [](TSharedPtr<FPenNode>& N, bool bWidth, float Px)
		{
			FPenSize& Size = bWidth ? N->Width : N->Height;
			Size.Value = FMath::Max(0.0f, Px);
			if (bWidth) N->bHasWidth = true;
			else N->bHasHeight = true;
		};

		if (FillCount > 0)
		{
			const float AvailMain = (bHorizontal ? InnerW : InnerH) - FixedMainSum - TotalGap;
			const float FillMain = FMath::Max(0.0f, AvailMain / FillCount);

			for (auto& EC : EffChildren)
			{
				const FPenSize& MainSize = bHorizontal ? EC->Width : EC->Height;
				if (MainSize.IsFill())
					ResolveSize(EC, bHorizontal, FillMain);
				const FPenSize& CrossSize = bHorizontal ? EC->Height : EC->Width;
				if (CrossSize.IsFill())
					ResolveSize(EC, !bHorizontal, bHorizontal ? InnerH : InnerW);
			}

			UE_LOG(LogDesign2UMG, Log, TEXT("PreResolveFill: '%s' layout=%s inner=(%.0f,%.0f) fixed=%.0f fill=%d each=%.0f"),
				*Widget.Name, bHorizontal ? TEXT("H") : TEXT("V"),
				InnerW, InnerH, FixedMainSum, FillCount, FillMain);
		}
		else
		{
			// 没有 Fill 子节点，但交叉轴可能有 FillContainer
			for (auto& EC : EffChildren)
			{
				const FPenSize& CrossSize = bHorizontal ? EC->Height : EC->Width;
				if (CrossSize.IsFill())
					ResolveSize(EC, !bHorizontal, bHorizontal ? InnerH : InnerW);
			}
		}
	}

	for (const auto& Child : Node->Children)
	{
		if (ShouldSkipNode(Child))
			continue;

		TSharedPtr<FPenNode> EffectiveChild = Child;
		if (Child->Type == EPenNodeType::Ref)
		{
			auto Expanded = FPenRefExpander::ExpandRef(Child, Doc);
			if (Expanded.IsValid())
			{
				if (ActiveTheme.Num() > 0)
				{
					FPenVariableResolver Resolver(Doc.Variables);
					Resolver.SetThemeContext(ActiveTheme);
					FPenRefExpander::ReThemeNode(Expanded, Resolver);
				}
				EffectiveChild = Expanded;
			}
		}

		auto ChildWidget = ConvertNode(EffectiveChild, Doc, ActiveTheme, OutWarnings);
		if (ChildWidget.Type.IsEmpty())
			continue;

		FPenLayoutMapper::ApplySlotProperties(ChildWidget, EffectiveChild, Node->Layout);
		FPenLayoutMapper::ApplyAlignItems(ChildWidget, Node->AlignItems, Node->Layout);

		const bool bHasFixedSize = EffectiveChild->Width.IsFixed() || EffectiveChild->Height.IsFixed();
		const bool bHasMinSize = (EffectiveChild->Width.IsFit() && EffectiveChild->Width.Value > 0)
			|| (EffectiveChild->Height.IsFit() && EffectiveChild->Height.Value > 0);
		// Canvas children with mixed FitContent+Fixed axes need SizeBox to enforce the Fixed axis
		const bool bCanvasMixedAxis = Node->Layout == EPenLayout::None
			&& ((EffectiveChild->Width.IsFit() && EffectiveChild->Height.IsFixed())
				|| (EffectiveChild->Height.IsFit() && EffectiveChild->Width.IsFixed()));
		bool bNeedsSizeBox = (bHasFixedSize || bHasMinSize || bCanvasMixedAxis);
		if (bNeedsSizeBox)
		{
			UE_LOG(LogDesign2UMG, Verbose, TEXT("  child '%s' → SizeBox wrap (fixed=%d min=%d w=%.1f h=%.1f)"),
				*ChildWidget.Name, bHasFixedSize, bHasMinSize,
				EffectiveChild->Width.Value, EffectiveChild->Height.Value);
			Widget.Children.Add(FPenLayoutMapper::WrapWithSizeBox(MoveTemp(ChildWidget), EffectiveChild));
		}
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
	{
		UE_LOG(LogDesign2UMG, Verbose, TEXT("ConvertNode: '%s' → WrapWithBorder (fill=%d corner=%d stroke=%d)"),
			*Widget.Name, bHasVisualFill, bHasCornerRadius, Node->Stroke.HasStroke());
		Widget = FPenVisualMapper::WrapWithBorder(MoveTemp(Widget), Node);
	}
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

FUMGWidgetNode FDesign2UMGConverter::CreateContainerWidget(const TSharedPtr<FPenNode>& Node)
{
	FUMGWidgetNode Widget;
	Widget.Type = FPenTypeMapper::MapNodeTypeToWidget(Node->Type, Node->Layout);
	return Widget;
}

FUMGWidgetNode FDesign2UMGConverter::CreateTextWidget(const TSharedPtr<FPenNode>& Node)
{
	FUMGWidgetNode Widget;
	Widget.Type = TEXT("TextBlock");
	Widget.Properties.Add(TEXT("Text"), Node->Content);
	ApplyTextProperties(Widget, Node);
	return Widget;
}

void FDesign2UMGConverter::ApplyTextProperties(FUMGWidgetNode& Widget, const TSharedPtr<FPenNode>& Node)
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

FUMGWidgetNode FDesign2UMGConverter::CreateIconFontWidget(const TSharedPtr<FPenNode>& Node)
{
	FUMGWidgetNode Widget;
	Widget.Type = TEXT("TextBlock");

	// 图标字体族 → UE5 字体资产路径（需用户在设置中配置映射）
	if (!Node->IconFontFamily.IsEmpty())
	{
		FString AssetPath = UDesign2UMGSettings::Get()->FindIconFontAssetPath(Node->IconFontFamily);
		if (AssetPath.IsEmpty())
		{
			UE_LOG(LogDesign2UMG, Warning,
				TEXT("IconFont '%s': font family '%s' has no asset mapping. "
					 "Configure in Editor Preferences > Plugins > Pen To UMG > Icon Font Asset Map"),
				*Node->IconFontName, *Node->IconFontFamily);
			Widget.Properties.Add(TEXT("FontFamily"), Node->IconFontFamily);
		}
		else
		{
			Widget.Properties.Add(TEXT("FontFamily"), AssetPath);
		}
	}

	// 图标名称即连字文本（如 "dashboard" 在 Material Symbols 中渲染为图标）
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

FUMGWidgetNode FDesign2UMGConverter::CreatePathWidget(const TSharedPtr<FPenNode>& Node)
{
	FUMGWidgetNode Widget;

	// 已被 FigmaImageExporter 转为 Image Fill → 走 Image 路径
	if (Node->Fill.Type == EPenFillType::Image && !Node->Fill.Image.Url.IsEmpty())
	{
		Widget.Type = TEXT("Image");
		Widget.Properties.Add(TEXT("TexturePath"), Node->Fill.Image.Url);
		return Widget;
	}

	// 有 geometry 数据 → 栅格化（fill 优先，stroke-only 回退用 stroke 颜色填充）
	if (!Node->Geometry.IsEmpty())
	{
		FString RasterColor;
		if (Node->Fill.Type == EPenFillType::Color)
			RasterColor = Node->Fill.SolidColor;
		else if (Node->Stroke.HasStroke() && Node->Stroke.Fill.Type == EPenFillType::Color)
			RasterColor = Node->Stroke.Fill.SolidColor;

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
	Widget.Type = FPenTypeMapper::MapNodeTypeToWidget(Node->Type, Node->Layout);
	return Widget;
}