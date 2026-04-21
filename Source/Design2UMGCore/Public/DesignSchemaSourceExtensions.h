// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "DesignSchemaFoundation.h"
#include "DesignSchemaSourceExtensions.generated.h"

/**
 * Source-specific layout metadata that stays attached to parser-owned node state.
 * Non-UPROPERTY `bHas*` fields are intentional transient presence markers and are
 * not meant to participate in reflected serialization.
 */
USTRUCT()
struct FDesignLayoutExtensions
{
	GENERATED_BODY()

	UPROPERTY()
	FString LayoutWrap;

	UPROPERTY()
	FString CounterAxisAlignContent;

	UPROPERTY()
	FString LayoutPositioning;

	UPROPERTY()
	FString LayoutAlign;

	UPROPERTY()
	FString PrimaryAxisSizingMode;

	UPROPERTY()
	FString CounterAxisSizingMode;

	UPROPERTY()
	FString GridRowsSizing;

	UPROPERTY()
	FString GridColumnsSizing;

	UPROPERTY()
	FString GridChildHorizontalAlign;

	UPROPERTY()
	FString GridChildVerticalAlign;

	UPROPERTY()
	FString BoundVariablesJson;

	UPROPERTY()
	float CounterAxisSpacing = 0.0f;

	bool bHasCounterAxisSpacing = false;

	UPROPERTY()
	float MinWidth = 0.0f;

	bool bHasMinWidth = false;

	UPROPERTY()
	float MaxWidth = 0.0f;

	bool bHasMaxWidth = false;

	UPROPERTY()
	float MinHeight = 0.0f;

	bool bHasMinHeight = false;

	UPROPERTY()
	float MaxHeight = 0.0f;

	bool bHasMaxHeight = false;

	UPROPERTY()
	int32 GridRowCount = 0;

	bool bHasGridRowCount = false;

	UPROPERTY()
	int32 GridColumnCount = 0;

	bool bHasGridColumnCount = false;

	UPROPERTY()
	float GridRowGap = 0.0f;

	bool bHasGridRowGap = false;

	UPROPERTY()
	float GridColumnGap = 0.0f;

	bool bHasGridColumnGap = false;

	UPROPERTY()
	int32 GridRowSpan = 0;

	bool bHasGridRowSpan = false;

	UPROPERTY()
	int32 GridColumnSpan = 0;

	bool bHasGridColumnSpan = false;

	UPROPERTY()
	int32 GridColumnAnchorIndex = 0;

	bool bHasGridColumnAnchorIndex = false;

	UPROPERTY()
	int32 GridRowAnchorIndex = 0;

	bool bHasGridRowAnchorIndex = false;

	UPROPERTY()
	bool bItemReverseZIndex = false;

	bool bHasItemReverseZIndex = false;

	UPROPERTY()
	bool bStrokesIncludedInLayout = false;

	bool bHasStrokesIncludedInLayout = false;
};

USTRUCT()
struct FDesignComponentInstanceData
{
	GENERATED_BODY()

	UPROPERTY() bool bIsInstance = false;
	UPROPERTY() bool bInInstanceSubtree = false;
	UPROPERTY() FString ComponentId;
	UPROPERTY() FString ComponentSetId;
	UPROPERTY() FString MainComponentKey;
	UPROPERTY() FString ComponentPropertiesJson;
	UPROPERTY() FString VariantPropertiesJson;
	UPROPERTY() FString OverridesJson;
	UPROPERTY()
	bool bIsExposedInstance = false;

	bool bHasIsExposedInstance = false;
};

/**
 * Prototype transition payload parsed from source design tools.
 */
USTRUCT()
struct FDesignPrototypeTransitionData
{
	GENERATED_BODY()

	UPROPERTY() FString Type;
	UPROPERTY() FString Direction;
	UPROPERTY() float Duration = 0.0f;
	bool bHasDuration = false;
	UPROPERTY() FString EasingType;
	UPROPERTY() FString EasingJson;
	UPROPERTY() bool bMatchLayers = false;
	bool bHasMatchLayers = false;

	bool HasAny() const
	{
		return !Type.IsEmpty()
			|| !Direction.IsEmpty()
			|| bHasDuration
			|| !EasingType.IsEmpty()
			|| !EasingJson.IsEmpty()
			|| bHasMatchLayers;
	}
};

USTRUCT()
struct FDesignPrototypeAction
{
	GENERATED_BODY()

	UPROPERTY() FString Type;
	UPROPERTY() FString DestinationId;
	UPROPERTY() FString Navigation;
	UPROPERTY() FString TransitionNodeId;
	UPROPERTY() FString TransitionJson;
	UPROPERTY() FString RawJson;
	UPROPERTY() FDesignPrototypeTransitionData Transition;
};

USTRUCT()
struct FDesignPrototypeInteraction
{
	GENERATED_BODY()

	UPROPERTY() FString TriggerType;
	UPROPERTY() FString TriggerJson;
	UPROPERTY() FString RawJson;
	UPROPERTY() TArray<FDesignPrototypeAction> Actions;
};

USTRUCT()
struct FDesignFlowStartingPoint
{
	GENERATED_BODY()

	UPROPERTY() FString NodeId;
	UPROPERTY() FString Name;
};

USTRUCT()
struct FDesignPrototypeNodeData
{
	GENERATED_BODY()

	UPROPERTY() FString OverflowDirection;
	UPROPERTY() FString TransitionNodeId;
	UPROPERTY() FDesignPrototypeTransitionData Transition;
	UPROPERTY() TArray<FDesignPrototypeInteraction> Interactions;
	UPROPERTY() TArray<FDesignFlowStartingPoint> FlowStartingPoints;
};

USTRUCT()
struct FDesignSourceDocumentData
{
	GENERATED_BODY()

	UPROPERTY() FString ComponentsJson;
	UPROPERTY() FString ComponentSetsJson;
	UPROPERTY() FString PrototypeStartNodeId;
	UPROPERTY() FString PrototypeDeviceJson;
	UPROPERTY() TArray<FDesignFlowStartingPoint> FlowStartingPoints;
};

USTRUCT()
struct FDesignRenderFallbackData
{
	GENERATED_BODY()

	UPROPERTY() FString ImageRef;
	UPROPERTY() FString Scope;
	UPROPERTY() FString Source;
	UPROPERTY() FString Reason;
	UPROPERTY() FString OffsetX;
	UPROPERTY() FString OffsetY;
	UPROPERTY() FString Width;
	UPROPERTY() FString Height;

	bool HasAny() const
	{
		return !ImageRef.IsEmpty()
			|| !Scope.IsEmpty()
			|| !Source.IsEmpty()
			|| !Reason.IsEmpty()
			|| !OffsetX.IsEmpty()
			|| !OffsetY.IsEmpty()
			|| !Width.IsEmpty()
			|| !Height.IsEmpty();
	}
};

USTRUCT()
struct FDesignSkippedNodeReport
{
	GENERATED_BODY()

	UPROPERTY() FString NodeId;
	UPROPERTY() FString NodeName;
	UPROPERTY() FString NodeType;
	UPROPERTY() FString Reason;
};

USTRUCT()
struct FDesignParseDiagnostics
{
	GENERATED_BODY()

	UPROPERTY() TArray<FDesignSkippedNodeReport> SkippedDesignerNodes;
};

/**
 * Source-only metadata carried alongside the shared design node.
 */
struct FDesignSourceNodeData
{
	FDesignLayoutExtensions LayoutExtensions;
	FDesignComponentInstanceData ComponentInstance;
	FDesignPrototypeNodeData Prototype;
	FString SourceNodeType;
	bool bExpandedRefInstance = false;
	FString VectorRasterFallbackImageRef;
	FDesignRenderFallbackData RenderFallback;
};
