// Copyright Epic Games, Inc. All Rights Reserved.

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
	if (Weight == TEXT("100") || Weight == TEXT("thin"))
		return TEXT("Thin");
	if (Weight == TEXT("200") || Weight == TEXT("extralight"))
		return TEXT("ExtraLight");
	if (Weight == TEXT("300") || Weight == TEXT("light"))
		return TEXT("Light");
	if (Weight == TEXT("400") || Weight == TEXT("normal") || Weight == TEXT("regular"))
		return TEXT("Regular");
	if (Weight == TEXT("500") || Weight == TEXT("medium"))
		return TEXT("Medium");
	if (Weight == TEXT("600") || Weight == TEXT("semibold"))
		return TEXT("SemiBold");
	if (Weight == TEXT("700") || Weight == TEXT("bold"))
		return TEXT("Bold");
	if (Weight == TEXT("800") || Weight == TEXT("extrabold"))
		return TEXT("ExtraBold");
	if (Weight == TEXT("900") || Weight == TEXT("black"))
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
