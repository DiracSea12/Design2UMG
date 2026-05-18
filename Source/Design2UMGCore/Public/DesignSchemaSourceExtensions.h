// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "DesignSchemaFoundation.h"
#include "DesignSchemaSourceExtensions.generated.h"

/**
 * Source-specific layout metadata that stays attached to parser-owned node state.
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

	TOptional<float> CounterAxisSpacing;
	TOptional<float> MinWidth;
	TOptional<float> MaxWidth;
	TOptional<float> MinHeight;
	TOptional<float> MaxHeight;
	TOptional<int32> GridRowCount;
	TOptional<int32> GridColumnCount;
	TOptional<float> GridRowGap;
	TOptional<float> GridColumnGap;
	TOptional<int32> GridRowSpan;
	TOptional<int32> GridColumnSpan;
	TOptional<int32> GridColumnAnchorIndex;
	TOptional<int32> GridRowAnchorIndex;
	TOptional<bool> ItemReverseZIndex;
	TOptional<bool> StrokesIncludedInLayout;
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
	TOptional<bool> IsExposedInstance;
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
	TOptional<float> Duration;
	UPROPERTY() FString EasingType;
	UPROPERTY() FString EasingJson;
	TOptional<bool> MatchLayers;

	bool HasAny() const
	{
		return !Type.IsEmpty()
			|| !Direction.IsEmpty()
			|| Duration.IsSet()
			|| !EasingType.IsEmpty()
			|| !EasingJson.IsEmpty()
			|| MatchLayers.IsSet();
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

	UPROPERTY() FString SourceFilePath;
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

	bool HasAny() const
	{
		return !ImageRef.IsEmpty()
			|| !Scope.IsEmpty()
			|| !Source.IsEmpty()
			|| !Reason.IsEmpty();
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
