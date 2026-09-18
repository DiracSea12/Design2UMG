// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignSchema.h"
#include "DesignSchemaSourceExtensions.h"
#include "Design2UMGLog.h"

namespace
{
	const FDesignSourceNodeData EmptySourceNodeData;
	const FDesignSourceDocumentData EmptySourceDocumentData;
	const FDesignParseDiagnostics EmptyParseDiagnostics;

	void IndexReusableNodes(
		const TArray<TSharedPtr<FDesignNode>>& Nodes,
		TMap<FString, TSharedPtr<FDesignNode>>& OutNodesById,
		TSet<FString>& RepeatedIds)
	{
		for (const TSharedPtr<FDesignNode>& Node : Nodes)
		{
			if (!Node.IsValid())
			{
				continue;
			}
			if (Node->bReusable)
			{
				if (const TSharedPtr<FDesignNode>* Existing = OutNodesById.Find(Node->Id))
				{
					if (*Existing != Node) RepeatedIds.Add(Node->Id);
				}
				else
				{
					OutNodesById.Add(Node->Id, Node);
				}
			}
			IndexReusableNodes(Node->Children, OutNodesById, RepeatedIds);
		}
	}
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
	, bExpandedRefInstance(Other.bExpandedRefInstance)
	, RefOverrides(Other.RefOverrides)
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
	bExpandedRefInstance = Other.bExpandedRefInstance;
	RefOverrides = Other.RefOverrides;
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
	TMap<FString, TSharedPtr<FDesignNode>> NodesById;
	BuildReusableNodeIndex(NodesById);
	return NodesById.FindRef(Id);
}

void FDesignDocument::BuildReusableNodeIndex(TMap<FString, TSharedPtr<FDesignNode>>& OutNodesById,
	TArray<FString>* OutWarnings) const
{
	OutNodesById.Reset();
	TSet<FString> RepeatedIds;
	IndexReusableNodes(Children, OutNodesById, RepeatedIds);
	if (!RepeatedIds.IsEmpty())
	{
		// A selected subtree can also contain definitions supplied separately by the import source.
		// Keep the established document-order lookup; do not confuse repeated definitions with instance identity.
		TArray<FString> Ids = RepeatedIds.Array();
		Ids.Sort();
		const FString Warning = FString::Printf(
			TEXT("Repeated reusable definition ids [%s]; first occurrence in document order retained"),
			*FString::Join(Ids, TEXT(", ")));
		if (OutWarnings) OutWarnings->Add(Warning);
		UE_LOG(LogDesign2UMG, Warning, TEXT("%s"), *Warning);
	}
}
