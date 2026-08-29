// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "DesignSchemaFoundation.h"
#include "DesignSchemaSourceExtensions.generated.h"

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

/**
 * One CONDITIONAL action block: the condensed condition expression plus the
 * nested actions to run when the condition holds. Blocks and actions are
 * mutually recursive, so both sides stay plain members (UE reflection rejects
 * circular UPROPERTY containers); these structs live on the plain-C++ FDesignNode
 * side and never enter reflected serialization.
 */
struct FDesignPrototypeConditionalBlock;

USTRUCT()
struct FDesignPrototypeAction
{
	GENERATED_BODY()

	UPROPERTY() FString Type;
	UPROPERTY() FString DestinationId;
	UPROPERTY() FString Navigation;
	UPROPERTY() FString TransitionNodeId;
	UPROPERTY() FString TransitionJson;
	UPROPERTY() FDesignPrototypeTransitionData Transition;

	// SET_VARIABLE payload: variableId plus the condensed JSON of variableValue
	// (falling back to the legacy "value" field at parse time).
	UPROPERTY() FString VariableId;
	UPROPERTY() FString VariableValueJson;

	// SET_VARIABLE_MODE payload.
	UPROPERTY() FString VariableCollectionId;
	UPROPERTY() FString VariableModeId;

	// CONDITIONAL payload: one entry per conditionalBlock, each carrying the
	// condensed condition JSON and the fully typed nested actions.
	TArray<FDesignPrototypeConditionalBlock> ConditionalBlocks;
};

struct FDesignPrototypeConditionalBlock
{
	FString ConditionJson;
	TArray<FDesignPrototypeAction> Actions;
};

USTRUCT()
struct FDesignPrototypeInteraction
{
	GENERATED_BODY()

	UPROPERTY() FString TriggerType;
	UPROPERTY() FString TriggerJson;
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
	FDesignComponentInstanceData ComponentInstance;
	FDesignPrototypeNodeData Prototype;
	FString SourceNodeType;
};
