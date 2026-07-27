// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignSchema.h"

EDesignNodeType ParseNodeType(const FString& S)
{
	if (S == TEXT("frame")) return EDesignNodeType::Frame;
	if (S == TEXT("rectangle")) return EDesignNodeType::Rectangle;
	if (S == TEXT("ellipse")) return EDesignNodeType::Ellipse;
	if (S == TEXT("line")) return EDesignNodeType::Line;
	if (S == TEXT("polygon")) return EDesignNodeType::Polygon;
	if (S == TEXT("text")) return EDesignNodeType::Text;
	if (S == TEXT("icon")) return EDesignNodeType::IconFont;
	if (S == TEXT("path")) return EDesignNodeType::Path;
	if (S == TEXT("ref")) return EDesignNodeType::Ref;
	if (S == TEXT("group")) return EDesignNodeType::Group;
	if (S == TEXT("connection")) return EDesignNodeType::Connection;
	if (S == TEXT("note")) return EDesignNodeType::Note;
	if (S == TEXT("prompt")) return EDesignNodeType::Prompt;
	if (S == TEXT("context")) return EDesignNodeType::Context;
	return EDesignNodeType::Unknown;
}

EDesignLayout ParseLayout(const FString& S)
{
	if (S == TEXT("vertical")) return EDesignLayout::Vertical;
	if (S == TEXT("horizontal")) return EDesignLayout::Horizontal;
	if (S == TEXT("wrap")) return EDesignLayout::Wrap;
	if (S == TEXT("grid")) return EDesignLayout::Grid;
	return EDesignLayout::None;
}

EDesignJustifyContent ParseJustifyContent(const FString& S)
{
	if (S == TEXT("center")) return EDesignJustifyContent::Center;
	if (S == TEXT("end")) return EDesignJustifyContent::End;
	if (S == TEXT("space_between")) return EDesignJustifyContent::SpaceBetween;
	if (S == TEXT("space_around")) return EDesignJustifyContent::SpaceAround;
	return EDesignJustifyContent::Start;
}

EDesignAlignItems ParseAlignItems(const FString& S)
{
	if (S == TEXT("center")) return EDesignAlignItems::Center;
	if (S == TEXT("end")) return EDesignAlignItems::End;
	return EDesignAlignItems::Start;
}

EDesignTextAlign ParseTextAlign(const FString& S)
{
	if (S == TEXT("center")) return EDesignTextAlign::Center;
	if (S == TEXT("right")) return EDesignTextAlign::Right;
	if (S == TEXT("justify")) return EDesignTextAlign::Justify;
	return EDesignTextAlign::Left;
}

EDesignTextAlignVertical ParseTextAlignVertical(const FString& S)
{
	if (S == TEXT("middle")) return EDesignTextAlignVertical::Middle;
	if (S == TEXT("bottom")) return EDesignTextAlignVertical::Bottom;
	return EDesignTextAlignVertical::Top;
}

EDesignTextGrowth ParseTextGrowth(const FString& S)
{
	if (S == TEXT("fixed-width")) return EDesignTextGrowth::FixedWidth;
	if (S == TEXT("fixed-width-height")) return EDesignTextGrowth::FixedWidthHeight;
	return EDesignTextGrowth::Auto;
}
