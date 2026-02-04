// Copyright Epic Games, Inc. All Rights Reserved.

#include "PenToUMGConverter.h"
#include "PenTypeMapper.h"

FUMGWidgetData FPenToUMGConverter::Convert(const FPenDocument& Document)
{
	FUMGWidgetData Result;
	Result.WidgetName = TEXT("WBP_FromPen");

	// 创建根 CanvasPanel
	Result.RootWidget.Type = TEXT("CanvasPanel");
	Result.RootWidget.Name = TEXT("Root");
	Result.RootWidget.Id = TEXT("Root");

	// 转换所有顶层节点
	for (const auto& Child : Document.Children)
	{
		if (Child.IsValid())
			Result.RootWidget.Children.Add(ConvertNode(Child, Document));
	}

	return Result;
}

FUMGWidgetNode FPenToUMGConverter::ConvertNode(const TSharedPtr<FPenNode>& Node, const FPenDocument& Doc)
{
	if (!Node.IsValid())
		return FUMGWidgetNode();

	// 处理 ref 节点
	if (Node->Type == EPenNodeType::Ref)
	{
		auto Expanded = ExpandRef(Node, Doc);
		if (Expanded.IsValid())
			return ConvertNode(Expanded, Doc);
	}

	FUMGWidgetNode Widget;

	// 根据类型创建 Widget
	switch (Node->Type)
	{
	case EPenNodeType::Text:
		Widget = CreateTextWidget(Node);
		break;
	case EPenNodeType::Frame:
	case EPenNodeType::Group:
		Widget = CreateContainerWidget(Node);
		break;
	default:
		Widget = CreateContainerWidget(Node);
		break;
	}

	Widget.Id = Node->Id;
	Widget.Name = Node->Name.IsEmpty() ? Node->Id : Node->Name;

	// 设置 Bounds
	Widget.Bounds.X = Node->X;
	Widget.Bounds.Y = Node->Y;
	Widget.Bounds.W = Node->Width.Value;
	Widget.Bounds.H = Node->Height.Value;

	// 应用视觉属性
	ApplyVisualProperties(Widget, Node);

	// 递归转换子节点
	for (const auto& Child : Node->Children)
	{
		if (Child.IsValid() && Child->bEnabled)
		{
			auto ChildWidget = ConvertNode(Child, Doc);
			ApplySlotProperties(ChildWidget, Child, Node->Layout);
			ApplyAlignItems(ChildWidget, Node->AlignItems, Node->Layout);
			Widget.Children.Add(ChildWidget);
		}
	}

	// 应用布局属性
	if (Node->Gap > 0)
		ApplyGap(Widget.Children, Node->Gap, Node->Layout);

	if (Node->Padding.Top > 0 || Node->Padding.Right > 0 || Node->Padding.Bottom > 0 || Node->Padding.Left > 0)
		ApplyPadding(Widget, Node->Padding);

	if (Node->JustifyContent != EPenJustifyContent::Start)
		ApplyJustifyContent(Widget.Children, Node->JustifyContent, Node->Layout);

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
	// 字体大小
	if (Node->FontSize > 0)
		Widget.Properties.Add(TEXT("FontSize"), FString::FromInt((int32)Node->FontSize));

	// 字体族
	if (!Node->FontFamily.IsEmpty())
		Widget.Properties.Add(TEXT("FontFamily"), Node->FontFamily);

	// 字重
	if (!Node->FontWeight.IsEmpty())
		Widget.Properties.Add(TEXT("TypefaceFontName"), FPenTypeMapper::MapFontWeight(Node->FontWeight).ToString());

	// 文本对齐
	Widget.Properties.Add(TEXT("Justification"),
		Node->TextAlign == EPenTextAlign::Center ? TEXT("Center") :
		Node->TextAlign == EPenTextAlign::Right ? TEXT("Right") : TEXT("Left"));

	// 行高
	if (Node->LineHeight > 0 && Node->FontSize > 0)
	{
		float Percentage = Node->LineHeight / Node->FontSize;
		Widget.Properties.Add(TEXT("LineHeightPercentage"), FString::SanitizeFloat(Percentage));
	}

	// 字间距
	if (Node->LetterSpacing != 0)
		Widget.Properties.Add(TEXT("LetterSpacing"), FString::FromInt((int32)Node->LetterSpacing));

	// textGrowth 处理
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
}

void FPenToUMGConverter::ApplySlotProperties(FUMGWidgetNode& Widget, const TSharedPtr<FPenNode>& Node, EPenLayout ParentLayout)
{
	if (ParentLayout == EPenLayout::Vertical)
	{
		Widget.Slot.HorizontalAlignment = Node->Width.Mode == EPenSizeMode::FillContainer ? TEXT("Fill") : TEXT("Left");
		Widget.Slot.SizeRule = Node->Height.Mode == EPenSizeMode::FillContainer ? TEXT("Fill") : TEXT("Auto");
	}
	else if (ParentLayout == EPenLayout::Horizontal)
	{
		Widget.Slot.VerticalAlignment = Node->Height.Mode == EPenSizeMode::FillContainer ? TEXT("Fill") : TEXT("Top");
		Widget.Slot.SizeRule = Node->Width.Mode == EPenSizeMode::FillContainer ? TEXT("Fill") : TEXT("Auto");
	}
	else
	{
		// CanvasPanel: 使用绝对定位
		Widget.Slot.Offsets.Left = Node->X;
		Widget.Slot.Offsets.Top = Node->Y;
		Widget.Slot.Offsets.Right = Node->Width.Value;
		Widget.Slot.Offsets.Bottom = Node->Height.Value;
	}
}

void FPenToUMGConverter::ApplyVisualProperties(FUMGWidgetNode& Widget, const TSharedPtr<FPenNode>& Node)
{
	// 填充颜色
	if (Node->Fill.Type == EPenFillType::Color && Node->Fill.bEnabled)
	{
		FLinearColor Color = FPenTypeMapper::ParseColor(Node->Fill.SolidColor);
		Widget.Properties.Add(TEXT("BrushColor"), Color.ToString());
	}

	// 文本颜色
	if (Node->Type == EPenNodeType::Text && Node->Fill.Type == EPenFillType::Color)
	{
		FLinearColor Color = FPenTypeMapper::ParseColor(Node->Fill.SolidColor);
		Widget.Properties.Add(TEXT("ColorAndOpacity"), Color.ToString());
	}

	// 透明度
	if (Node->Opacity < 1.0f)
		Widget.Properties.Add(TEXT("RenderOpacity"), FString::SanitizeFloat(Node->Opacity));

	// 圆角
	ApplyRoundedCorner(Widget, Node->CornerRadius);

	// 描边
	ApplyStroke(Widget, Node->Stroke);
}

TSharedPtr<FPenNode> FPenToUMGConverter::ExpandRef(const TSharedPtr<FPenNode>& RefNode, const FPenDocument& Doc)
{
	if (!RefNode.IsValid() || RefNode->Ref.IsEmpty())
		return nullptr;

	return FindReusableComponent(RefNode->Ref, Doc.Children);
}

TSharedPtr<FPenNode> FPenToUMGConverter::FindReusableComponent(const FString& RefId, const TArray<TSharedPtr<FPenNode>>& Nodes)
{
	for (const auto& Node : Nodes)
	{
		if (!Node.IsValid())
			continue;

		if (Node->bReusable && Node->Id == RefId)
			return Node;

		// 递归搜索
		auto Found = FindReusableComponent(RefId, Node->Children);
		if (Found.IsValid())
			return Found;
	}
	return nullptr;
}

void FPenToUMGConverter::ApplyGap(TArray<FUMGWidgetNode>& Children, float Gap, EPenLayout Layout)
{
	if (Gap <= 0 || Children.Num() < 2)
		return;

	// 从第二个子元素开始添加 Padding
	for (int32 i = 1; i < Children.Num(); ++i)
	{
		if (Layout == EPenLayout::Vertical)
			Children[i].Slot.Padding.Top = Gap;
		else if (Layout == EPenLayout::Horizontal)
			Children[i].Slot.Padding.Left = Gap;
	}
}

void FPenToUMGConverter::ApplyPadding(FUMGWidgetNode& Widget, const FPenPadding& Padding)
{
	Widget.Properties.Add(TEXT("Padding"), FString::Printf(TEXT("%.1f,%.1f,%.1f,%.1f"),
		Padding.Left, Padding.Top, Padding.Right, Padding.Bottom));
}

void FPenToUMGConverter::ApplyJustifyContent(TArray<FUMGWidgetNode>& Children, EPenJustifyContent Justify, EPenLayout Layout)
{
	if (Justify == EPenJustifyContent::Start || Children.Num() == 0)
		return;

	// 创建 Spacer Widget
	auto CreateSpacer = []() -> FUMGWidgetNode {
		FUMGWidgetNode Spacer;
		Spacer.Type = TEXT("Spacer");
		Spacer.Slot.SizeRule = TEXT("Fill");
		return Spacer;
	};

	switch (Justify)
	{
	case EPenJustifyContent::Center:
		Children.Insert(CreateSpacer(), 0);
		Children.Add(CreateSpacer());
		break;
	case EPenJustifyContent::End:
		Children.Insert(CreateSpacer(), 0);
		break;
	case EPenJustifyContent::SpaceBetween:
		if (Children.Num() > 1)
		{
			for (int32 i = Children.Num() - 1; i > 0; --i)
				Children.Insert(CreateSpacer(), i);
		}
		break;
	case EPenJustifyContent::SpaceAround:
		Children.Insert(CreateSpacer(), 0);
		for (int32 i = Children.Num() - 1; i > 0; --i)
			Children.Insert(CreateSpacer(), i);
		Children.Add(CreateSpacer());
		break;
	default:
		break;
	}
}

void FPenToUMGConverter::ApplyAlignItems(FUMGWidgetNode& Widget, EPenAlignItems Align, EPenLayout ParentLayout)
{
	// fill_container 优先级高于 alignItems
	if (ParentLayout == EPenLayout::Vertical)
	{
		if (Widget.Slot.HorizontalAlignment != TEXT("Fill"))
			Widget.Slot.HorizontalAlignment = FPenTypeMapper::MapAlignItemsH(Align) == HAlign_Center ? TEXT("Center") :
				FPenTypeMapper::MapAlignItemsH(Align) == HAlign_Right ? TEXT("Right") : TEXT("Left");
	}
	else if (ParentLayout == EPenLayout::Horizontal)
	{
		if (Widget.Slot.VerticalAlignment != TEXT("Fill"))
			Widget.Slot.VerticalAlignment = FPenTypeMapper::MapAlignItemsV(Align) == VAlign_Center ? TEXT("Center") :
				FPenTypeMapper::MapAlignItemsV(Align) == VAlign_Bottom ? TEXT("Bottom") : TEXT("Top");
	}
}

void FPenToUMGConverter::ApplyRoundedCorner(FUMGWidgetNode& Widget, const FPenCornerRadius& Radius)
{
	if (Radius.bUniform)
	{
		if (Radius.Uniform > 0)
		{
			Widget.Properties.Add(TEXT("DrawAs"), TEXT("RoundedBox"));
			Widget.Properties.Add(TEXT("OutlineCornerRadius"), FString::SanitizeFloat(Radius.Uniform));
		}
	}
	else
	{
		// 四值圆角
		Widget.Properties.Add(TEXT("DrawAs"), TEXT("RoundedBox"));
		Widget.Properties.Add(TEXT("CornerRadii"), FString::Printf(TEXT("%.1f,%.1f,%.1f,%.1f"),
			Radius.TopLeft, Radius.TopRight, Radius.BottomRight, Radius.BottomLeft));
	}
}

void FPenToUMGConverter::ApplyStroke(FUMGWidgetNode& Widget, const FPenStroke& Stroke)
{
	float Thickness = Stroke.Thickness.bUniform ? Stroke.Thickness.Uniform : Stroke.Thickness.Top;
	if (Thickness <= 0)
		return;

	Widget.Properties.Add(TEXT("OutlineThickness"), FString::SanitizeFloat(Thickness));

	if (Stroke.Fill.Type == EPenFillType::Color)
	{
		FLinearColor Color = FPenTypeMapper::ParseColor(Stroke.Fill.SolidColor);
		Widget.Properties.Add(TEXT("OutlineColor"), Color.ToString());
	}
}
