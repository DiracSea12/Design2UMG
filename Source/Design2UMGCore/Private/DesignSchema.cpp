// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignSchema.h"
#include "DesignSchemaSourceExtensions.h"

namespace
{
	const FDesignSourceNodeData EmptySourceNodeData;
	const FDesignSourceDocumentData EmptySourceDocumentData;
	const FDesignParseDiagnostics EmptyParseDiagnostics;
}

FDesignNode::FDesignNode() = default;
FDesignNode::FDesignNode(FDesignNode&& Other) noexcept = default;
FDesignNode::~FDesignNode() = default;
FDesignNode& FDesignNode::operator=(FDesignNode&& Other) noexcept = default;

FDesignNode::FDesignNode(const FDesignNode& Other)
	: Id(Other.Id)
	, Name(Other.Name)
	, Type(Other.Type)
	, Theme(Other.Theme)
	, Transform(Other.Transform)
	, Metadata(Other.Metadata)
	, LayoutData(Other.LayoutData)
	, Visual(Other.Visual)
	, Text(Other.Text)
	, Shape(Other.Shape)
	, Ref(Other.Ref)
	, bReusable(Other.bReusable)
	, Descendants(Other.Descendants)
	, Children(Other.Children)
{
	if (Other.SourceData)
	{
		SourceData = MakeUnique<FDesignSourceNodeData>(*Other.SourceData);
	}
}

FDesignNode& FDesignNode::operator=(const FDesignNode& Other)
{
	if (this == &Other)
	{
		return *this;
	}

	Id = Other.Id;
	Name = Other.Name;
	Type = Other.Type;
	Theme = Other.Theme;
	Transform = Other.Transform;
	Metadata = Other.Metadata;
	LayoutData = Other.LayoutData;
	Visual = Other.Visual;
	Text = Other.Text;
	Shape = Other.Shape;
	Ref = Other.Ref;
	bReusable = Other.bReusable;
	Descendants = Other.Descendants;
	Children = Other.Children;
	SourceData = Other.SourceData ? MakeUnique<FDesignSourceNodeData>(*Other.SourceData) : nullptr;

	return *this;
}

bool FDesignNode::HasSourceData() const
{
	return SourceData != nullptr;
}

const FDesignSourceNodeData& FDesignNode::Source() const
{
	return SourceData ? *SourceData : EmptySourceNodeData;
}

FDesignSourceNodeData& FDesignNode::MutableSource()
{
	if (!SourceData)
	{
		SourceData = MakeUnique<FDesignSourceNodeData>();
	}

	return *SourceData;
}

FDesignDocument::FDesignDocument() = default;
FDesignDocument::FDesignDocument(FDesignDocument&& Other) noexcept = default;
FDesignDocument::~FDesignDocument() = default;
FDesignDocument& FDesignDocument::operator=(FDesignDocument&& Other) noexcept = default;

FDesignDocument::FDesignDocument(const FDesignDocument& Other)
	: Version(Other.Version)
	, Fonts(Other.Fonts)
	, Themes(Other.Themes)
	, Variables(Other.Variables)
	, Metadata(Other.Metadata)
	, Children(Other.Children)
{
	if (Other.SourceDocumentData)
	{
		SourceDocumentData = MakeUnique<FDesignSourceDocumentData>(*Other.SourceDocumentData);
	}

	if (Other.ParseDiagnosticsData)
	{
		ParseDiagnosticsData = MakeUnique<FDesignParseDiagnostics>(*Other.ParseDiagnosticsData);
	}
}

FDesignDocument& FDesignDocument::operator=(const FDesignDocument& Other)
{
	if (this == &Other)
	{
		return *this;
	}

	Version = Other.Version;
	Fonts = Other.Fonts;
	Themes = Other.Themes;
	Variables = Other.Variables;
	Metadata = Other.Metadata;
	Children = Other.Children;
	SourceDocumentData = Other.SourceDocumentData ? MakeUnique<FDesignSourceDocumentData>(*Other.SourceDocumentData) : nullptr;
	ParseDiagnosticsData = Other.ParseDiagnosticsData ? MakeUnique<FDesignParseDiagnostics>(*Other.ParseDiagnosticsData) : nullptr;

	return *this;
}

bool FDesignDocument::HasSourceDocumentData() const
{
	return SourceDocumentData != nullptr;
}

const FDesignSourceDocumentData& FDesignDocument::SourceDocument() const
{
	return SourceDocumentData ? *SourceDocumentData : EmptySourceDocumentData;
}

FDesignSourceDocumentData& FDesignDocument::MutableSourceDocument()
{
	if (!SourceDocumentData)
	{
		SourceDocumentData = MakeUnique<FDesignSourceDocumentData>();
	}

	return *SourceDocumentData;
}

bool FDesignDocument::HasParseDiagnostics() const
{
	return ParseDiagnosticsData != nullptr;
}

const FDesignParseDiagnostics& FDesignDocument::ParseDiagnostics() const
{
	return ParseDiagnosticsData ? *ParseDiagnosticsData : EmptyParseDiagnostics;
}

FDesignParseDiagnostics& FDesignDocument::MutableParseDiagnostics()
{
	if (!ParseDiagnosticsData)
	{
		ParseDiagnosticsData = MakeUnique<FDesignParseDiagnostics>();
	}

	return *ParseDiagnosticsData;
}

TSharedPtr<FDesignNode> FDesignDocument::FindReusableNode(const FString& Id) const
{
	return FindNodeRecursive(Children, Id, true);
}

TSharedPtr<FDesignNode> FDesignDocument::FindNodeRecursive(
	const TArray<TSharedPtr<FDesignNode>>& Nodes,
	const FString& Id,
	bool bReusableOnly)
{
	for (const TSharedPtr<FDesignNode>& Node : Nodes)
	{
		if (!Node.IsValid())
			continue;
		if (Node->Id == Id && (!bReusableOnly || Node->bReusable))
			return Node;
		if (const TSharedPtr<FDesignNode> Found = FindNodeRecursive(Node->Children, Id, bReusableOnly))
			return Found;
	}
	return nullptr;
}
