// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignImportReimportInternal.h"

#include "Design2UMGLog.h"
#include "UMGSchemaSourceExtensions.h"

namespace DesignImportPipeline
{
	// ---------------------------------------------------------------------------
	// Small utilities
	// ---------------------------------------------------------------------------

	void AddReimportDiagnostic(
		TArray<FDesign2UMGMappingDiagnostic>& Diagnostics,
		const TCHAR* Kind,
		const FString& Message)
	{
		FDesign2UMGMappingDiagnostic Diagnostic;
		Diagnostic.Kind = Kind;
		Diagnostic.Message = Message;
		Diagnostics.Add(MoveTemp(Diagnostic));
		UE_LOG(LogDesign2UMG, Log, TEXT("Reimport[%s]: %s"), Kind, *Message);
	}

	/** "Real design node" = has a source identity (non-empty SourceNodeId); synthetic wrapper/pass-through layers do not. */
	bool HasRealSourceIdentity(const FUMGWidgetNode& Node)
	{
		return Node.HasSourceData() && !Node.Source().SourceNodeId.IsEmpty();
	}

	/** The same design identity key as the import side (single source of truth: FUMGWidgetNode::GetStableDesignNodeId). */
	FString GetDesignNodeId(const FUMGWidgetNode& Node)
	{
		return Node.GetStableDesignNodeId();
	}

	/**
	 * Pruning used when the generation chain rebuilds nodes: strip manifest-instance nodes already inside the subtree.
	 * Otherwise the move case "a newly added design container holding old nodes" first builds a duplicate widget,
	 * then reparent moves the old widget in - resulting in two copies. Stripped nodes are rehung by the reparent stage.
	 */
	void PruneManifestInstanceChildren(
		FUMGWidgetNode& Node,
		const TMap<FString, const FDesign2UMGGenerationManifestSidecar::FInstanceRecord*>& InstancesById)
	{
		for (FUMGWidgetNode& Child : Node.Children)
		{
			PruneManifestInstanceChildren(Child, InstancesById);
		}
		Node.Children.RemoveAll([&InstancesById](const FUMGWidgetNode& Child)
		{
			return InstancesById.Contains(GetDesignNodeId(Child));
		});
	}
}
