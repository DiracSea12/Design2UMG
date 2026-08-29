// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignTypeMapper.h"
#include "FontResolver.h"

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
	// FFontResolver owns the single weight table: alias/numeric parsing plus the
	// 100..900 -> typeface-name mapping shared with font resolution.
	return FName(*FFontResolver::TypefaceNameForWeightValue(FFontResolver::ParseWeightValue(Weight)));
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



