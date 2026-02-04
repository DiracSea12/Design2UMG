// Copyright Epic Games, Inc. All Rights Reserved.

#include "PenTypeMapper.h"

FLinearColor FPenTypeMapper::ParseColor(const FString& HexColor)
{
	FString Hex = HexColor;
	if (Hex.StartsWith(TEXT("#")))
		Hex = Hex.Mid(1);

	if (Hex.Len() == 3)
	{
		// #RGB -> #RRGGBB
		Hex = FString::Printf(TEXT("%c%c%c%c%c%c"),
			Hex[0], Hex[0], Hex[1], Hex[1], Hex[2], Hex[2]);
	}

	uint32 R = 0, G = 0, B = 0, A = 255;
	if (Hex.Len() >= 6)
	{
		R = FParse::HexDigit(Hex[0]) * 16 + FParse::HexDigit(Hex[1]);
		G = FParse::HexDigit(Hex[2]) * 16 + FParse::HexDigit(Hex[3]);
		B = FParse::HexDigit(Hex[4]) * 16 + FParse::HexDigit(Hex[5]);
	}
	if (Hex.Len() >= 8)
		A = FParse::HexDigit(Hex[6]) * 16 + FParse::HexDigit(Hex[7]);

	return FLinearColor(R / 255.f, G / 255.f, B / 255.f, A / 255.f);
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

ETextJustify::Type FPenTypeMapper::MapTextAlign(EPenTextAlign Align)
{
	switch (Align)
	{
	case EPenTextAlign::Center:
		return ETextJustify::Center;
	case EPenTextAlign::Right:
		return ETextJustify::Right;
	default:
		return ETextJustify::Left;
	}
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
