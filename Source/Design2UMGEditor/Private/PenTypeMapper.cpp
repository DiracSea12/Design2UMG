// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#include "PenTypeMapper.h"
#include "UMGWidgetUtils.h"

FLinearColor FPenTypeMapper::ParseColor(const FString& HexColor)
{
	return FUMGWidgetUtils::ParseColor(HexColor);
}

FString FPenTypeMapper::MapNodeTypeToWidget(EPenNodeType Type, EPenLayout Layout)
{
	switch (Type)
	{
	case EPenNodeType::Frame:
		if (Layout == EPenLayout::Vertical)
			return TEXT("VerticalBox");
		if (Layout == EPenLayout::Horizontal)
			return TEXT("HorizontalBox");
		return TEXT("CanvasPanel");

	case EPenNodeType::Rectangle:
	case EPenNodeType::Ellipse:
	case EPenNodeType::Polygon:
	case EPenNodeType::Path:
		return TEXT("Border");

	case EPenNodeType::Text:
		return TEXT("TextBlock");

	case EPenNodeType::IconFont:
		return TEXT("TextBlock");

	case EPenNodeType::Line:
		return TEXT("Image");

	case EPenNodeType::Group:
		return TEXT("CanvasPanel");

	default:
		return TEXT("CanvasPanel");
	}
}

FName FPenTypeMapper::MapFontWeight(const FString& Weight)
{
	FString W = Weight.ToLower();
	if (W == TEXT("100") || W == TEXT("thin"))
		return TEXT("Thin");
	if (W == TEXT("200") || W == TEXT("extralight"))
		return TEXT("ExtraLight");
	if (W == TEXT("300") || W == TEXT("light"))
		return TEXT("Light");
	if (W == TEXT("400") || W == TEXT("normal") || W == TEXT("regular"))
		return TEXT("Regular");
	if (W == TEXT("500") || W == TEXT("medium"))
		return TEXT("Medium");
	if (W == TEXT("600") || W == TEXT("semibold"))
		return TEXT("SemiBold");
	if (W == TEXT("700") || W == TEXT("bold"))
		return TEXT("Bold");
	if (W == TEXT("800") || W == TEXT("extrabold"))
		return TEXT("ExtraBold");
	if (W == TEXT("900") || W == TEXT("black"))
		return TEXT("Black");
	return TEXT("Regular");
}

EHorizontalAlignment FPenTypeMapper::MapAlignItemsH(EPenAlignItems Align)
{
	switch (Align)
	{
	case EPenAlignItems::Center:
		return HAlign_Center;
	case EPenAlignItems::End:
		return HAlign_Right;
	default:
		return HAlign_Left;
	}
}

EVerticalAlignment FPenTypeMapper::MapAlignItemsV(EPenAlignItems Align)
{
	switch (Align)
	{
	case EPenAlignItems::Center:
		return VAlign_Center;
	case EPenAlignItems::End:
		return VAlign_Bottom;
	default:
		return VAlign_Top;
	}
}
