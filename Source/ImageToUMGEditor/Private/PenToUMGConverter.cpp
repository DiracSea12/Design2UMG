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
		if (Child.IsValid())
		{
			auto ChildWidget = ConvertNode(Child, Doc);
			ApplySlotProperties(ChildWidget, Child, Node->Layout);
			Widget.Children.Add(ChildWidget);
		}
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

	if (Node->FontSize > 0)
		Widget.Properties.Add(TEXT("FontSize"), FString::FromInt((int32)Node->FontSize));

	if (!Node->FontFamily.IsEmpty())
		Widget.Properties.Add(TEXT("FontFamily"), Node->FontFamily);

	if (!Node->FontWeight.IsEmpty())
		Widget.Properties.Add(TEXT("TypefaceFontName"), FPenTypeMapper::MapFontWeight(Node->FontWeight).ToString());

	return Widget;
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
	if (Node->Fill.Type == EPenFillType::Color)
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
