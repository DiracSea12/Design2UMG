// Copyright Epic Games, Inc. All Rights Reserved.

#include "PenToUMGConverter.h"
#include "PenTypeMapper.h"
#include "PenToUMGLog.h"

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

	FString BuildGradientMaterialParams(const FPenGradient& Gradient)
	{
		if (Gradient.Colors.Num() == 0)
			return FString();

		const FString& StartColor = Gradient.Colors[0].Color;
		const FString& EndColor = Gradient.Colors.Last().Color;

		if (Gradient.GradientType == EPenGradientType::Radial)
			return FString::Printf(TEXT("{\"ColorCenter\":\"%s\",\"ColorEdge\":\"%s\"}"), *StartColor, *EndColor);

		return FString::Printf(TEXT("{\"ColorStart\":\"%s\",\"ColorEnd\":\"%s\"}"), *StartColor, *EndColor);
	}

	void AddTextureSizeHints(FUMGWidgetNode& Widget, const TSharedPtr<FPenNode>& Node)
	{
		if (Node->Width.IsFixed() && Node->Width.Value > 0.0f)
		{
			Widget.Properties.Add(TEXT("WidthOverride"), FString::FromInt((int32)Node->Width.Value));
		}
		if (Node->Height.IsFixed() && Node->Height.Value > 0.0f)
		{
			Widget.Properties.Add(TEXT("HeightOverride"), FString::FromInt((int32)Node->Height.Value));
		}
	}

	void ApplyFillProperties(FUMGWidgetNode& Widget, const TSharedPtr<FPenNode>& Node)
	{
		if (!Node.IsValid() || !Node->Fill.bEnabled)
		{
			return;
		}

		if (Node->Fill.Type == EPenFillType::Color)
		{
			Widget.Properties.Add(TEXT("BrushColor"), Node->Fill.SolidColor);
			if (Node->Type == EPenNodeType::Text)
			{
				Widget.Properties.Add(TEXT("Color"), Node->Fill.SolidColor);
			}
			return;
		}

		if (Node->Fill.Type == EPenFillType::Gradient && Node->Fill.Gradient.Colors.Num() > 0)
		{
			FString MaterialRef = TEXT("gradienth");
			switch (Node->Fill.Gradient.GradientType)
			{
			case EPenGradientType::Radial:
			case EPenGradientType::Angular:
				MaterialRef = TEXT("radialgradient");
				break;
			case EPenGradientType::Linear:
			default:
			{
				float Rotation = FMath::Fmod(Node->Fill.Gradient.Rotation, 360.0f);
				if (Rotation < 0.0f)
				{
					Rotation += 360.0f;
				}
				const bool bVertical = (Rotation > 45.0f && Rotation < 135.0f) || (Rotation > 225.0f && Rotation < 315.0f);
				MaterialRef = bVertical ? TEXT("gradientv") : TEXT("gradienth");
				break;
			}
			}

			Widget.Properties.Add(TEXT("MaterialRef"), MaterialRef);
			const FString MaterialParams = BuildGradientMaterialParams(Node->Fill.Gradient);
			if (!MaterialParams.IsEmpty())
			{
				Widget.Properties.Add(TEXT("MaterialParams"), MaterialParams);
			}
			AddTextureSizeHints(Widget, Node);
			return;
		}

		if (Node->Fill.Type == EPenFillType::Image && !Node->Fill.Image.Url.IsEmpty())
		{
			Widget.Properties.Add(TEXT("TexturePath"), Node->Fill.Image.Url);
			AddTextureSizeHints(Widget, Node);
		}
	}
}

FUMGWidgetData FPenToUMGConverter::Convert(const FPenDocument& Document)
{
	FUMGWidgetData Result;
	Result.WidgetName = TEXT("WBP_FromPen");

	Result.RootWidget.Type = TEXT("CanvasPanel");
	Result.RootWidget.Name = TEXT("Root");
	Result.RootWidget.Id = TEXT("Root");

	for (const auto& Child : Document.Children)
	{
		if (ShouldSkipNode(Child))
		{
			continue;
		}

		FUMGWidgetNode ChildWidget = ConvertNode(Child, Document);
		if (!ChildWidget.Type.IsEmpty())
		{
			Result.RootWidget.Children.Add(MoveTemp(ChildWidget));
		}
	}

	return Result;
}

FUMGWidgetNode FPenToUMGConverter::ConvertNode(const TSharedPtr<FPenNode>& Node, const FPenDocument& Doc)
{
	if (ShouldSkipNode(Node))
		return FUMGWidgetNode();

	if (Node->Type == EPenNodeType::Ref)
	{
		auto Expanded = ExpandRef(Node, Doc);
		return Expanded.IsValid() ? ConvertNode(Expanded, Doc) : FUMGWidgetNode();
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

	ApplyVisualProperties(Widget, Node);

	for (const auto& Child : Node->Children)
	{
		if (ShouldSkipNode(Child))
		{
			continue;
		}

		auto ChildWidget = ConvertNode(Child, Doc);
		if (ChildWidget.Type.IsEmpty())
		{
			continue;
		}

		ApplySlotProperties(ChildWidget, Child, Node->Layout);
		ApplyAlignItems(ChildWidget, Node->AlignItems, Node->Layout);

		bool bNeedsSizeBox = (Node->Layout == EPenLayout::Vertical || Node->Layout == EPenLayout::Horizontal)
			&& (Child->Width.IsFixed() || Child->Height.IsFixed());
		if (bNeedsSizeBox)
			Widget.Children.Add(WrapWithSizeBox(MoveTemp(ChildWidget), Child));
		else
			Widget.Children.Add(MoveTemp(ChildWidget));
	}

	if (Node->Gap > 0)
		ApplyGap(Widget.Children, Node->Gap, Node->Layout);

	if (Node->JustifyContent != EPenJustifyContent::Start)
		ApplyJustifyContent(Widget.Children, Node->JustifyContent);

	const bool bHasVisualFill = Node->Fill.bEnabled && Node->Fill.Type != EPenFillType::None;
	const bool bHasCornerRadius = Node->CornerRadius.bUniform ? Node->CornerRadius.Uniform > 0
		: (Node->CornerRadius.TopLeft > 0 || Node->CornerRadius.TopRight > 0
			|| Node->CornerRadius.BottomRight > 0 || Node->CornerRadius.BottomLeft > 0);
	bool bNeedsBorder = (Node->Type == EPenNodeType::Frame || Node->Type == EPenNodeType::Group)
		&& Widget.Type != TEXT("CanvasPanel")
		&& (bHasVisualFill || HasPadding(Node) || bHasCornerRadius || Node->Stroke.HasStroke());

	if (bNeedsBorder)
		Widget = WrapWithBorder(MoveTemp(Widget), Node);
	else if (HasPadding(Node))
		ApplyPadding(Widget, Node->Padding);

	// BackgroundBlur Effect → 用 BackgroundBlur widget 包裹
	for (const FPenEffect& Effect : Node->Effects)
	{
		if (Effect.bEnabled && Effect.Type == EPenEffectType::BackgroundBlur && Effect.Radius > 0)
		{
			return WrapWithBackgroundBlur(MoveTemp(Widget), Effect.Radius);
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

	ApplyTextProperties(Widget, Node);
	return Widget;
}

void FPenToUMGConverter::ApplyTextProperties(FUMGWidgetNode& Widget, const TSharedPtr<FPenNode>& Node)
{
	if (Node->FontSize > 0)
		Widget.Properties.Add(TEXT("FontSize"), FString::FromInt((int32)Node->FontSize));

	if (!Node->FontFamily.IsEmpty())
		Widget.Properties.Add(TEXT("FontFamily"), Node->FontFamily);

	if (!Node->FontWeight.IsEmpty())
		Widget.Properties.Add(TEXT("TypefaceFontName"), FPenTypeMapper::MapFontWeight(Node->FontWeight).ToString());

	Widget.Properties.Add(TEXT("Justification"),
		Node->TextAlign == EPenTextAlign::Center ? TEXT("Center") :
		Node->TextAlign == EPenTextAlign::Right ? TEXT("Right") : TEXT("Left"));

	if (Node->LineHeight > 0)
	{
		Widget.Properties.Add(TEXT("LineHeightPercentage"), FString::SanitizeFloat(Node->LineHeight));
	}

	if (Node->LetterSpacing != 0)
		Widget.Properties.Add(TEXT("LetterSpacing"), FString::FromInt((int32)Node->LetterSpacing));

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

	// textAlignVertical → VerticalAlignment（用于父容器 Slot 对齐）
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
	// IconName 同时写入 Text，确保 TextBlock 属性应用器能消费
	if (!Node->IconFontName.IsEmpty())
		Widget.Properties.Add(TEXT("Text"), Node->IconFontName);

	if (Node->FontSize > 0)
		Widget.Properties.Add(TEXT("FontSize"), FString::FromInt((int32)Node->FontSize));

	if (Node->Fill.Type == EPenFillType::Color)
	{
		Widget.Properties.Add(TEXT("Color"), Node->Fill.SolidColor);
	}

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
		const bool bFillWidth = Node->Width.Mode == EPenSizeMode::FillContainer;
		const bool bFillHeight = Node->Height.Mode == EPenSizeMode::FillContainer;

		if (bFillWidth || bFillHeight)
		{
			Widget.Slot.Anchors.Min.X = 0.0f;
			Widget.Slot.Anchors.Max.X = bFillWidth ? 1.0f : 0.0f;
			Widget.Slot.Anchors.Min.Y = 0.0f;
			Widget.Slot.Anchors.Max.Y = bFillHeight ? 1.0f : 0.0f;
			Widget.Slot.Alignment.X = 0.0f;
			Widget.Slot.Alignment.Y = 0.0f;
			Widget.Slot.Offsets.Left = Node->X;
			Widget.Slot.Offsets.Top = Node->Y;
			Widget.Slot.Offsets.Right = bFillWidth ? 0.0f : Node->Width.Value;
			Widget.Slot.Offsets.Bottom = bFillHeight ? 0.0f : Node->Height.Value;
			return;
		}

		Widget.Slot.Offsets.Left = Node->X;
		Widget.Slot.Offsets.Top = Node->Y;
		Widget.Slot.Offsets.Right = Node->Width.Value;
		Widget.Slot.Offsets.Bottom = Node->Height.Value;
	}
}

void FPenToUMGConverter::ApplyVisualProperties(FUMGWidgetNode& Widget, const TSharedPtr<FPenNode>& Node)
{
	ApplyFillProperties(Widget, Node);

	if (Node->Opacity < 1.0f)
		Widget.Properties.Add(TEXT("RenderOpacity"), FString::SanitizeFloat(Node->Opacity));

	ApplyRoundedCorner(Widget, Node->CornerRadius);
	ApplyStroke(Widget, Node->Stroke);

	// rotation → RenderAngle
	if (!FMath::IsNearlyZero(Node->Rotation))
		Widget.Properties.Add(TEXT("RenderAngle"), FString::SanitizeFloat(Node->Rotation));

	// flipX/flipY → RenderTransformScale
	if (Node->bFlipX || Node->bFlipY)
	{
		float ScaleX = Node->bFlipX ? -1.0f : 1.0f;
		float ScaleY = Node->bFlipY ? -1.0f : 1.0f;
		Widget.Properties.Add(TEXT("RenderTransformScale"), FString::Printf(TEXT("%.1f,%.1f"), ScaleX, ScaleY));
	}

	// clip → Clipping
	if (Node->bClip)
		Widget.Properties.Add(TEXT("Clipping"), TEXT("ClipToBounds"));

	// Effects 处理
	for (const FPenEffect& Effect : Node->Effects)
	{
		if (!Effect.bEnabled)
			continue;

		switch (Effect.Type)
		{
		case EPenEffectType::Blur:
			UE_LOG(LogPenToUMG, Warning,
				TEXT("PenToUMG: Blur effect on '%s' ignored (UMG has no foreground blur widget)"),
				*Node->Name);
			break;
		case EPenEffectType::Shadow:
			if (Node->Type == EPenNodeType::Text)
			{
				Widget.Properties.Add(TEXT("ShadowOffset"),
					FString::Printf(TEXT("%.1f,%.1f"),
						Effect.Shadow.OffsetX, Effect.Shadow.OffsetY));
				if (!Effect.Shadow.Color.IsEmpty())
					Widget.Properties.Add(TEXT("ShadowColorAndOpacity"),
						Effect.Shadow.Color);
			}
			else
			{
				UE_LOG(LogPenToUMG, Warning,
					TEXT("PenToUMG: Shadow effect on '%s' ignored (UMG only supports text shadows)"),
					*Node->Name);
			}
			break;
		default:
			break;
		}
	}
}

TSharedPtr<FPenNode> FPenToUMGConverter::ExpandRef(const TSharedPtr<FPenNode>& RefNode, const FPenDocument& Doc)
{
	if (!RefNode.IsValid() || RefNode->Ref.IsEmpty())
		return nullptr;

	auto SourceNode = Doc.FindReusableNode(RefNode->Ref);
	if (!SourceNode.IsValid())
		return nullptr;

	auto Expanded = DeepCopyNode(SourceNode);

	for (const auto& Pair : RefNode->Descendants)
	{
		ApplyDescendantOverride(Expanded, Pair.Key, Pair.Value);
	}

	// 用实例节点的 Id/Name 替换源组件的，避免多实例 Name 冲突
	Expanded->Id = RefNode->Id;
	if (!RefNode->Name.IsEmpty())
		Expanded->Name = RefNode->Name;

	if (RefNode->bHasX) Expanded->X = RefNode->X;
	if (RefNode->bHasY) Expanded->Y = RefNode->Y;
	if (!RefNode->Width.IsFit()) Expanded->Width = RefNode->Width;
	if (!RefNode->Height.IsFit()) Expanded->Height = RefNode->Height;

	return Expanded;
}

void FPenToUMGConverter::ApplyGap(TArray<FUMGWidgetNode>& Children, float Gap, EPenLayout Layout)
{
	if (Gap <= 0 || Children.Num() < 2)
		return;

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

void FPenToUMGConverter::ApplyJustifyContent(TArray<FUMGWidgetNode>& Children, EPenJustifyContent Justify)
{
	if (Justify == EPenJustifyContent::Start || Children.Num() == 0)
		return;

	bool bHasFillChild = false;
	for (const auto& C : Children)
	{
		if (C.Slot.SizeRule == TEXT("Fill"))
		{
			bHasFillChild = true;
			break;
		}
	}
	if (bHasFillChild)
		return;

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
		for (int32 i = Children.Num() - 1; i > 1; --i)
			Children.Insert(CreateSpacer(), i);
		Children.Add(CreateSpacer());
		break;
	default:
		break;
	}
}

void FPenToUMGConverter::ApplyAlignItems(FUMGWidgetNode& Widget, EPenAlignItems Align, EPenLayout ParentLayout)
{
	if (ParentLayout == EPenLayout::Vertical)
	{
		if (Widget.Slot.HorizontalAlignment != TEXT("Fill"))
		{
			EHorizontalAlignment H = FPenTypeMapper::MapAlignItemsH(Align);
			Widget.Slot.HorizontalAlignment = H == HAlign_Center ? TEXT("Center") :
				H == HAlign_Right ? TEXT("Right") : TEXT("Left");
		}
	}
	else if (ParentLayout == EPenLayout::Horizontal)
	{
		if (Widget.Slot.VerticalAlignment != TEXT("Fill"))
		{
			EVerticalAlignment V = FPenTypeMapper::MapAlignItemsV(Align);
			Widget.Slot.VerticalAlignment = V == VAlign_Center ? TEXT("Center") :
				V == VAlign_Bottom ? TEXT("Bottom") : TEXT("Top");
		}
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
	else if (Radius.TopLeft > 0 || Radius.TopRight > 0 || Radius.BottomRight > 0 || Radius.BottomLeft > 0)
	{
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
		Widget.Properties.Add(TEXT("OutlineColor"), Stroke.Fill.SolidColor);
	}
}

TSharedPtr<FPenNode> FPenToUMGConverter::DeepCopyNode(const TSharedPtr<FPenNode>& Source)
{
	if (!Source.IsValid())
		return nullptr;

	auto Copy = MakeShared<FPenNode>();
	*Copy = *Source;

	Copy->Children.Empty();
	for (const auto& Child : Source->Children)
	{
		Copy->Children.Add(DeepCopyNode(Child));
	}

	return Copy;
}

void FPenToUMGConverter::ApplyDescendantOverride(TSharedPtr<FPenNode>& Node, const FString& Path, const TSharedPtr<FPenNode>& Override)
{
	if (!Node.IsValid() || !Override.IsValid())
		return;

	FString FirstPart, Remaining;
	if (Path.Split(TEXT("/"), &FirstPart, &Remaining))
	{
		for (auto& Child : Node->Children)
		{
			if (Child.IsValid() && Child->Id == FirstPart)
			{
				ApplyDescendantOverride(Child, Remaining, Override);
				return;
			}
		}
	}
	else
	{
		for (auto& Child : Node->Children)
		{
			if (Child.IsValid() && Child->Id == Path)
			{
				if (!Override->Content.IsEmpty())
					Child->Content = Override->Content;
				if (Override->FontSize > 0)
					Child->FontSize = Override->FontSize;
				if (!Override->FontWeight.IsEmpty())
					Child->FontWeight = Override->FontWeight;
				if (Override->Fill.Type != EPenFillType::None)
					Child->Fill = Override->Fill;
				if (Override->Width.Value > 0)
					Child->Width = Override->Width;
				if (Override->Height.Value > 0)
					Child->Height = Override->Height;
				if (Override->bHasX)
					Child->X = Override->X;
				if (Override->bHasY)
					Child->Y = Override->Y;
				if (!Override->bEnabled)
					Child->bEnabled = Override->bEnabled;
				return;
			}
		}
	}
}

bool FPenToUMGConverter::HasPadding(const TSharedPtr<FPenNode>& Node)
{
	return Node.IsValid() &&
		(Node->Padding.Top > 0 || Node->Padding.Right > 0 ||
		 Node->Padding.Bottom > 0 || Node->Padding.Left > 0);
}

FUMGWidgetNode FPenToUMGConverter::WrapWithSizeBox(FUMGWidgetNode&& ChildWidget, const TSharedPtr<FPenNode>& ChildNode)
{
	FUMGWidgetNode SizeBox;
	SizeBox.Type = TEXT("SizeBox");
	SizeBox.Id = ChildWidget.Id + TEXT("_sz");
	SizeBox.Name = ChildWidget.Name + TEXT("_sz");

	if (ChildNode->Width.IsFixed())
		SizeBox.Properties.Add(TEXT("WidthOverride"), FString::FromInt((int32)ChildNode->Width.Value));
	if (ChildNode->Height.IsFixed())
		SizeBox.Properties.Add(TEXT("HeightOverride"), FString::FromInt((int32)ChildNode->Height.Value));

	SizeBox.Slot = MoveTemp(ChildWidget.Slot);
	ChildWidget.Slot = FUMGSlotData();
	SizeBox.Children.Add(MoveTemp(ChildWidget));
	return SizeBox;
}

FUMGWidgetNode FPenToUMGConverter::WrapWithBorder(FUMGWidgetNode&& Widget, const TSharedPtr<FPenNode>& Node)
{
	FUMGWidgetNode Border;
	Border.Type = TEXT("Border");
	Border.Id = Widget.Id;
	Border.Name = Widget.Name;
	Border.Bounds = Widget.Bounds;

	bool bHasBackgroundProperty = false;
	for (const FString& Key : {TEXT("BrushColor"), TEXT("TexturePath"), TEXT("MaterialRef"), TEXT("MaterialParams"), TEXT("WidthOverride"), TEXT("HeightOverride")})
	{
		if (const FString* Value = Widget.Properties.Find(Key))
		{
			Border.Properties.Add(Key, *Value);
			Widget.Properties.Remove(Key);
			bHasBackgroundProperty = true;
		}
	}
	if (!bHasBackgroundProperty)
	{
		Border.Properties.Add(TEXT("BrushColor"), TEXT("#00000000"));
	}

	if (HasPadding(Node))
		ApplyPadding(Border, Node->Padding);

	for (const FString& Key : {TEXT("DrawAs"), TEXT("OutlineCornerRadius"), TEXT("CornerRadii"), TEXT("OutlineThickness"), TEXT("OutlineColor")})
	{
		if (const FString* V = Widget.Properties.Find(Key))
		{
			Border.Properties.Add(Key, *V);
			Widget.Properties.Remove(Key);
		}
	}

	if (const FString* Opacity = Widget.Properties.Find(TEXT("RenderOpacity")))
	{
		Border.Properties.Add(TEXT("RenderOpacity"), *Opacity);
		Widget.Properties.Remove(TEXT("RenderOpacity"));
	}
	else if (Node->Opacity < 1.0f)
	{
		Border.Properties.Add(TEXT("RenderOpacity"), FString::SanitizeFloat(Node->Opacity));
	}

	Widget.Id = Widget.Id + TEXT("_inner");
	Widget.Name = Widget.Name + TEXT("_inner");
	Widget.Bounds = FBounds();
	Widget.Slot.HorizontalAlignment = TEXT("Fill");
	Widget.Slot.VerticalAlignment = TEXT("Fill");

	Border.Children.Add(MoveTemp(Widget));
	return Border;
}

FUMGWidgetNode FPenToUMGConverter::WrapWithBackgroundBlur(FUMGWidgetNode&& Widget, float BlurStrength)
{
	FUMGWidgetNode Blur;
	Blur.Type = TEXT("BackgroundBlur");
	Blur.Id = Widget.Id + TEXT("_blur");
	Blur.Name = Widget.Name + TEXT("_blur");
	Blur.Bounds = Widget.Bounds;
	Blur.Slot = Widget.Slot;
	Blur.Properties.Add(TEXT("BlurStrength"), FString::SanitizeFloat(BlurStrength));

	Widget.Bounds = FBounds();
	Widget.Slot = FUMGSlotData();
	Widget.Slot.HorizontalAlignment = TEXT("Fill");
	Widget.Slot.VerticalAlignment = TEXT("Fill");

	Blur.Children.Add(MoveTemp(Widget));
	return Blur;
}
