// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "PenParserInternal.h"
#include "DesignSchema.h"

namespace PenParser
{
EDesignNodeType ParseNodeType(const FString& Token)
{
	if (Token == TEXT("frame")) return EDesignNodeType::Frame;
	if (Token == TEXT("rectangle")) return EDesignNodeType::Rectangle;
	if (Token == TEXT("ellipse")) return EDesignNodeType::Ellipse;
	if (Token == TEXT("line")) return EDesignNodeType::Line;
	if (Token == TEXT("polygon")) return EDesignNodeType::Polygon;
	if (Token == TEXT("text")) return EDesignNodeType::Text;
	if (Token == TEXT("icon") || Token == TEXT("icon_font")) return EDesignNodeType::IconFont;
	if (Token == TEXT("path")) return EDesignNodeType::Path;
	if (Token == TEXT("ref")) return EDesignNodeType::Ref;
	if (Token == TEXT("group")) return EDesignNodeType::Group;
	if (Token == TEXT("connection")) return EDesignNodeType::Connection;
	if (Token == TEXT("note")) return EDesignNodeType::Note;
	if (Token == TEXT("prompt")) return EDesignNodeType::Prompt;
	if (Token == TEXT("context")) return EDesignNodeType::Context;
	if (Token == TEXT("script")) return EDesignNodeType::Script;
	if (Token == TEXT("browser")) return EDesignNodeType::Browser;
	return EDesignNodeType::Unknown;
}

EDesignLayout ParseLayout(const FString& Token)
{
	if (Token == TEXT("vertical")) return EDesignLayout::Vertical;
	if (Token == TEXT("horizontal")) return EDesignLayout::Horizontal;
	if (Token == TEXT("wrap")) return EDesignLayout::Wrap;
	if (Token == TEXT("grid")) return EDesignLayout::Grid;
	return EDesignLayout::None;
}

EDesignTextAlignVertical ParseTextAlignVertical(const FString& Token)
{
	if (Token == TEXT("middle")) return EDesignTextAlignVertical::Middle;
	if (Token == TEXT("bottom")) return EDesignTextAlignVertical::Bottom;
	return EDesignTextAlignVertical::Top;
}

EDesignTextGrowth ParseTextGrowth(const FString& Token)
{
	if (Token == TEXT("fixed-width")) return EDesignTextGrowth::FixedWidth;
	if (Token == TEXT("fixed-width-height")) return EDesignTextGrowth::FixedWidthHeight;
	return EDesignTextGrowth::Auto;
}
}
