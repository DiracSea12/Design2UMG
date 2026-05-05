// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignTypeMapper.h"

namespace
{
	FString MapLayoutToContainer(EDesignLayout Layout)
	{
		switch (Layout)
		{
		case EDesignLayout::Vertical:
			return TEXT("VerticalBox");
		case EDesignLayout::Horizontal:
			return TEXT("HorizontalBox");
		case EDesignLayout::Wrap:
			return TEXT("WrapBox");
		case EDesignLayout::Grid:
			return TEXT("UniformGridPanel");
		default:
			return TEXT("CanvasPanel");
		}
	}
}

FString DesignTypeMapper::MapNodeTypeToWidget(EDesignNodeType Type, EDesignLayout Layout)
{
	switch (Type)
	{
	case EDesignNodeType::Frame:
	case EDesignNodeType::Ref:
		return MapLayoutToContainer(Layout);

	case EDesignNodeType::Rectangle:
	case EDesignNodeType::Ellipse:
	case EDesignNodeType::Polygon:
	case EDesignNodeType::Path:
		return TEXT("Border");

	case EDesignNodeType::Text:
	case EDesignNodeType::IconFont:
		return TEXT("TextBlock");

	case EDesignNodeType::Line:
		return TEXT("Image");

	case EDesignNodeType::Group:
		return TEXT("CanvasPanel");

	default:
		return TEXT("CanvasPanel");
	}
}

FName DesignTypeMapper::MapFontWeight(const FString& Weight)
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

EHorizontalAlignment DesignTypeMapper::MapAlignItemsH(EDesignAlignItems Align)
{
	switch (Align)
	{
	case EDesignAlignItems::Center:
		return HAlign_Center;
	case EDesignAlignItems::End:
		return HAlign_Right;
	default:
		return HAlign_Left;
	}
}

EVerticalAlignment DesignTypeMapper::MapAlignItemsV(EDesignAlignItems Align)
{
	switch (Align)
	{
	case EDesignAlignItems::Center:
		return VAlign_Center;
	case EDesignAlignItems::End:
		return VAlign_Bottom;
	default:
		return VAlign_Top;
	}
}



