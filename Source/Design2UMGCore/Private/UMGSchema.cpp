// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGSchema.h"
#include "UMGSchemaSourceExtensions.h"

namespace
{
	const FUMGSourceNodeData EmptySourceNodeData;
	const FUMGSourceDocumentData EmptySourceDocumentData;
	const FUMGPrototypeNavigationData EmptyPrototypeNavigationData;
}

FUMGWidgetNode::FUMGWidgetNode() = default;
FUMGWidgetNode::FUMGWidgetNode(FUMGWidgetNode&& Other) noexcept = default;
FUMGWidgetNode::~FUMGWidgetNode() = default;
FUMGWidgetNode& FUMGWidgetNode::operator=(FUMGWidgetNode&& Other) noexcept = default;

FUMGWidgetNode::FUMGWidgetNode(const FUMGWidgetNode& Other)
	: Type(Other.Type)
	, Id(Other.Id)
	, Name(Other.Name)
	, Bounds(Other.Bounds)
	, Slot(Other.Slot)
	, TextStyle(Other.TextStyle)
	, WidgetStyle(Other.WidgetStyle)
	, Text(Other.Text)
	, Brush(Other.Brush)
	, Effect(Other.Effect)
	, Path(Other.Path)
	, Shadow(Other.Shadow)
	, Render(Other.Render)
	, WidgetState(Other.WidgetState)
	, ScrollBox(Other.ScrollBox)
	, Children(Other.Children)
	, bSyntheticJustifySpacer(Other.bSyntheticJustifySpacer)
{
	if (Other.SourceData)
	{
		SourceData = MakeUnique<FUMGSourceNodeData>(*Other.SourceData);
	}
}

FUMGWidgetNode& FUMGWidgetNode::operator=(const FUMGWidgetNode& Other)
{
	if (this == &Other)
	{
		return *this;
	}

	Type = Other.Type;
	Id = Other.Id;
	Name = Other.Name;
	Bounds = Other.Bounds;
	Slot = Other.Slot;
	TextStyle = Other.TextStyle;
	WidgetStyle = Other.WidgetStyle;
	Text = Other.Text;
	Brush = Other.Brush;
	Effect = Other.Effect;
	Path = Other.Path;
	Shadow = Other.Shadow;
	Render = Other.Render;
	WidgetState = Other.WidgetState;
	ScrollBox = Other.ScrollBox;
	Children = Other.Children;
	bSyntheticJustifySpacer = Other.bSyntheticJustifySpacer;
	SourceData = Other.SourceData ? MakeUnique<FUMGSourceNodeData>(*Other.SourceData) : nullptr;

	return *this;
}

bool FUMGWidgetNode::HasSourceData() const
{
	return SourceData != nullptr;
}

const FUMGSourceNodeData& FUMGWidgetNode::Source() const
{
	return SourceData ? *SourceData : EmptySourceNodeData;
}

FUMGSourceNodeData& FUMGWidgetNode::MutableSource()
{
	if (!SourceData)
	{
		SourceData = MakeUnique<FUMGSourceNodeData>();
	}

	return *SourceData;
}

void FUMGWidgetNode::ClearSourceDesignIdentity()
{
	if (SourceData)
	{
		SourceData->SourceNodeId.Reset();
	}
}

FString FUMGWidgetNode::GetStableDesignNodeId() const
{
	return HasSourceData() && !Source().SourceNodeId.IsEmpty()
		? Source().SourceNodeId
		: Id;
}

FUMGWidgetData::FUMGWidgetData() = default;
FUMGWidgetData::FUMGWidgetData(FUMGWidgetData&& Other) noexcept = default;
FUMGWidgetData::~FUMGWidgetData() = default;
FUMGWidgetData& FUMGWidgetData::operator=(FUMGWidgetData&& Other) noexcept = default;

FUMGWidgetData::FUMGWidgetData(const FUMGWidgetData& Other)
	: WidgetName(Other.WidgetName)
	, RootWidget(Other.RootWidget)
	, Warnings(Other.Warnings)
	, ConversionError(Other.ConversionError)
	, IconFontMappingChanges(Other.IconFontMappingChanges)
{
	if (Other.SourceDocumentData)
	{
		SourceDocumentData = MakeUnique<FUMGSourceDocumentData>(*Other.SourceDocumentData);
	}

	if (Other.PrototypeNavigationData)
	{
		PrototypeNavigationData = MakeUnique<FUMGPrototypeNavigationData>(*Other.PrototypeNavigationData);
	}
}

FUMGWidgetData& FUMGWidgetData::operator=(const FUMGWidgetData& Other)
{
	if (this == &Other)
	{
		return *this;
	}

	WidgetName = Other.WidgetName;
	RootWidget = Other.RootWidget;
	Warnings = Other.Warnings;
	ConversionError = Other.ConversionError;
	IconFontMappingChanges = Other.IconFontMappingChanges;
	SourceDocumentData = Other.SourceDocumentData
		? MakeUnique<FUMGSourceDocumentData>(*Other.SourceDocumentData)
		: nullptr;
	PrototypeNavigationData = Other.PrototypeNavigationData
		? MakeUnique<FUMGPrototypeNavigationData>(*Other.PrototypeNavigationData)
		: nullptr;

	return *this;
}

bool FUMGWidgetData::HasSourceDocumentData() const
{
	return SourceDocumentData != nullptr;
}

const FUMGSourceDocumentData& FUMGWidgetData::SourceDocument() const
{
	return SourceDocumentData ? *SourceDocumentData : EmptySourceDocumentData;
}

FUMGSourceDocumentData& FUMGWidgetData::MutableSourceDocument()
{
	if (!SourceDocumentData)
	{
		SourceDocumentData = MakeUnique<FUMGSourceDocumentData>();
	}

	return *SourceDocumentData;
}

bool FUMGWidgetData::HasPrototypeNavigationData() const
{
	return PrototypeNavigationData != nullptr;
}

const FUMGPrototypeNavigationData& FUMGWidgetData::PrototypeNavigation() const
{
	return PrototypeNavigationData ? *PrototypeNavigationData : EmptyPrototypeNavigationData;
}

FUMGPrototypeNavigationData& FUMGWidgetData::MutablePrototypeNavigation()
{
	if (!PrototypeNavigationData)
	{
		PrototypeNavigationData = MakeUnique<FUMGPrototypeNavigationData>();
	}

	return *PrototypeNavigationData;
}
