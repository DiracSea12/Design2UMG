// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignComponentMapping.h"
#include "Design2UMGComponentMappingConfig.h"
#include "Components/Widget.h"
#include "UMGSchema.h"

class UUserWidget;
class UWidget;
class UWidgetTree;
struct FUMGPrototypeNavigationData;

/** The manifest's input declaration record (source/target/valueKind triple): describes the mapping declaration itself
 *  only, carrying no design value or write result - reimport re-resolves by design identity for fresh values instead of stored ones.*/
struct FDesign2UMGMappedInputRecord
{
	FString Source;
	FString Target;
	FString ValueKind;
};

/** The manifest's mapped instance record: located by designNodeId + widgetName; multiple instances of one component definition never get confused. */
struct FDesign2UMGMappedInstanceRecord
{
	FString DesignNodeId;
	FString WidgetName;
	FString MappedKey;
	FString MappedClass;
	bool bMatchedAtSetLevel = false;
	TArray<FDesign2UMGMappedInputRecord> Inputs;
};

/** The manifest's record of a purely generated top-level node. */
struct FDesign2UMGGeneratedInstanceRecord
{
	FString DesignNodeId;
	FString WidgetName;
};

/**
 * Component Mapping state during one generation:
 * - holds the config (match table),
 * - records hit IR nodes, created mapped instances, and top-level generated nodes,
 * - collects generation-time diagnostics (hit/fallback/inputs write/Prototype inert).
 * Lives only within one BuildWidgetBlueprint run; not a global object.
 */
class FDesign2UMGComponentMappingBuildState
{
public:
	explicit FDesign2UMGComponentMappingBuildState(const FDesign2UMGComponentMappingConfig& InConfig)
		: Config(&InConfig)
	{
	}

	/** Called before root-widget construction begins: marks the next created widget as the page host root. */
	void BeginRootBuild()
	{
		bRootPending = true;
	}

	/** Called when reimport reuses the generation chain: the page host root already exists; identify it directly instead of capturing the next new widget. */
	void SetRootWidget(UWidget* Widget)
	{
		RootWidget = Widget;
		bRootPending = false;
	}

	/** Every widget created by the generation chain passes through here: used to capture the root widget (effective before children are built). */
	void OnWidgetConstructed(UWidget* ConstructedWidget)
	{
		if (bRootPending && ConstructedWidget)
		{
			RootWidget = ConstructedWidget;
			bRootPending = false;
		}
	}

	/** Whether a mapped node hit (the IR marker is written by ApplyMappingToWidgetTreeIR). */
	bool IsMappedNode(const FUMGWidgetNode& Node) const;

	const DesignComponentMapping::FComponentMappingEntry* FindEntryForKey(const FString& Key) const
	{
		return Config->Table.FindEntryByKey(Key);
	}

	/** Marker of an IR node relative to a mapped component: body hit (with match tier) or a node inside a hit subtree. */
	struct FMappedNodeMark
	{
		/** true = the node body hit a mapping (it is a mapped instance); false = merely a node inside a mapped subtree. */
		bool bMappedInstance = false;
		/** The body hit happened at component-set level; meaningful only when bMappedInstance. */
		bool bMatchedAtSetLevel = false;
	};

	/** Body hit: register the mapped instance node (with match tier). */
	void AddMappedNodeId(const FString& NodeId, bool bMatchedAtSetLevel)
	{
		FMappedNodeMark& Mark = MappedNodes.FindOrAdd(NodeId);
		Mark.bMappedInstance = true;
		Mark.bMatchedAtSetLevel = bMatchedAtSetLevel;
	}

	/** Inside subtree: inner nodes the mapped node no longer expands (a nested hit's instance marker is not overwritten). */
	void AddMappedSubtreeNodeId(const FString& NodeId)
	{
		MappedNodes.FindOrAdd(NodeId);
	}

	bool IsMappedNodeId(const FString& NodeId) const
	{
		const FMappedNodeMark* Mark = MappedNodes.Find(NodeId);
		return Mark != nullptr && Mark->bMappedInstance;
	}

	/** Whether the trigger sits inside a mapped subtree (not the instance body). */
	bool IsInsideMappedSubtree(const FString& NodeId) const
	{
		const FMappedNodeMark* Mark = MappedNodes.Find(NodeId);
		return Mark != nullptr && !Mark->bMappedInstance;
	}

	bool IsMappedAtSetLevel(const FString& NodeId) const
	{
		const FMappedNodeMark* Mark = MappedNodes.Find(NodeId);
		return Mark != nullptr && Mark->bMappedInstance && Mark->bMatchedAtSetLevel;
	}

	void AddDiagnostic(const FString& Kind, const FString& Message)
	{
		FDesign2UMGMappingDiagnostic Diagnostic;
		Diagnostic.Kind = Kind;
		Diagnostic.Message = Message;
		Diagnostics.Add(MoveTemp(Diagnostic));
	}

	/** Registered when the mapped instance is created (inputs backfilled afterwards by widgetName). */
	void RecordMappedInstance(const FDesign2UMGMappedInstanceRecord& Record)
	{
		MappedInstances.Add(Record);
	}

	FDesign2UMGMappedInstanceRecord* FindMappedInstanceByWidgetName(const FString& WidgetName)
	{
		for (FDesign2UMGMappedInstanceRecord& Record : MappedInstances)
		{
			if (Record.WidgetName == WidgetName)
			{
				return &Record;
			}
		}
		return nullptr;
	}

	/** Record top-level generated nodes (direct children of the root widget); mapped nodes were registered in RecordMappedInstance and are skipped here. */
	void RecordCreatedWidget(const FUMGWidgetNode& Node, const UWidget* CreatedWidget, const UWidget* ParentWidget);

	bool IsRootWidget(const UWidget* Widget) const
	{
		return Widget && RootWidget.Get() == Widget;
	}

	/** All widget names created by this generation run (including nested leaves; data source of manifest.d2uOwned for reimport). */
	const TSet<FString>& GetCreatedWidgetNames() const { return CreatedWidgetNames; }
	/** Merge instance-slot content without publishing its separate scope's instance records. */
	void IncludeInstanceContent(const FDesign2UMGComponentMappingBuildState& Other)
	{
		CreatedWidgetNames.Append(Other.GetCreatedWidgetNames());
		Diagnostics.Append(Other.GetDiagnostics());
		bHasDegradation |= Other.HasDegradation();
	}


	const TArray<FDesign2UMGMappedInstanceRecord>& GetMappedInstances() const { return MappedInstances; }
	const TArray<FDesign2UMGGeneratedInstanceRecord>& GetTopLevelGenerated() const { return TopLevelGenerated; }
	const TArray<FDesign2UMGMappingDiagnostic>& GetDiagnostics() const { return Diagnostics; }
	void MarkDegraded() { bHasDegradation = true; }
	bool HasDegradation() const { return bHasDegradation; }

private:
	const FDesign2UMGComponentMappingConfig* Config = nullptr;
	TWeakObjectPtr<UWidget> RootWidget;
	bool bRootPending = false;
	bool bHasDegradation = false;
	/** Single container recording both mapped instance bodies and nodes inside hit subtrees (key = IR node Id). */
	TMap<FString, FMappedNodeMark> MappedNodes;
	/** All widget names created this run (except the page host root); an ownership record, top-level or nested alike. */
	TSet<FString> CreatedWidgetNames;
	TArray<FDesign2UMGMappedInstanceRecord> MappedInstances;
	TArray<FDesign2UMGGeneratedInstanceRecord> TopLevelGenerated;
	TArray<FDesign2UMGMappingDiagnostic> Diagnostics;
};

namespace Design2UMGComponentMappingGeneration
{
	/**
	 * Pre-generation mapping on the IR: resolve identity -> two-tier match -> write hit mapping keys into the node source-data sidecar.
	 * Miss / no config / non-Figma documents all pass through untouched; hits emit scope and hit diagnostics.
	 */
	void ApplyMappingToWidgetTreeIR(
		FUMGWidgetNode& RootNode,
		const FDesign2UMGComponentMappingConfig& Config,
		const FUMGWidgetData& WidgetData,
		FDesign2UMGComponentMappingBuildState& BuildState);

	/**
	 * Create a project WBP class instance for the hit IR node (hung into the WidgetTree, named with the deduped name).
	 * Class-load failure logs a diagnostic and returns nullptr; the caller falls back to the plain conversion path; the component's internal subtree is not expanded by the caller.
	 */
	UUserWidget* TryCreateMappedWidget(
		UWidgetTree* WidgetTree,
		const FUMGWidgetNode& Node,
		const FName& WidgetName,
		FDesign2UMGComponentMappingBuildState& BuildState);

	/**
	 * Write explicit design-facing inputs into the mapped instance per declaration (reflection lookup of Target: WBP class properties -> UWidget inherited properties).
	 * A single input failure only skips that input with a diagnostic; the other inputs and the mapping itself stay in effect.
	 */
	void ApplyMappedComponentInputs(
		UUserWidget* Instance,
		const FUMGWidgetNode& Node,
		FDesign2UMGComponentMappingBuildState& BuildState);

	/**
	 * The BuildState-less variant above, reused by reimport: overwrite the latest design value directly into the existing
	 * instance per the mapping declaration (inputs are D2U-owned; the project-side current value is not read, no baseline recorded).
	 * Returns true only when every declared input was applied; failure skips that input, not the remaining inputs.
	 * Per-input overwrite/skip diagnostics; OutInputRecords (optional) backfills an inputs declaration record with the same structure as the import manifest.
	 */
	bool ApplyMappedComponentInputsToInstance(
		UUserWidget* Instance,
		const FUMGWidgetNode& Node,
		const DesignComponentMapping::FComponentMappingEntry& Entry,
		TArray<FDesign2UMGMappedInputRecord>* OutInputRecords,
		TArray<FDesign2UMGMappingDiagnostic>& OutDiagnostics);

	/** Emits "inert" diagnostics when design interactions sit on a mapped identity (v1 does not take over interactions inside a mapped WBP).
	 *  OutDiagnostics (optional) receives these entries too: reimport does not read BuildState's generation-time
	 *  diagnostics; pass it in when the specific entries must reach the reimport report.*/
	void ReportInertPrototypeBindings(
		const FUMGPrototypeNavigationData& NavigationData,
		FDesign2UMGComponentMappingBuildState& BuildState,
		TArray<FDesign2UMGMappingDiagnostic>* OutDiagnostics = nullptr);
}
