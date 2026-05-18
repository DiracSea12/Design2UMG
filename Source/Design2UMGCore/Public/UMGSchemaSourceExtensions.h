// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UMGSchemaSourceExtensions.generated.h"

USTRUCT()
struct FUMGPrototypeTransitionData
{
	GENERATED_BODY()

	UPROPERTY()
	FString Type;

	UPROPERTY()
	FString Direction;

	UPROPERTY()
	float DurationSeconds = 0.0f;

	UPROPERTY()
	FString EasingType;

	UPROPERTY()
	FString EasingJson;

	UPROPERTY()
	TOptional<bool> HasMatchLayers;

	UPROPERTY()
	FString OutgoingAnimationName;

	UPROPERTY()
	FString IncomingAnimationName;

	bool IsAnimated() const { return DurationSeconds > KINDA_SMALL_NUMBER; }
	bool HasGeneratedAnimations() const { return !OutgoingAnimationName.IsEmpty() || !IncomingAnimationName.IsEmpty(); }

	bool UsesSlide() const
	{
		return Type.Equals(TEXT("SMART_ANIMATE"), ESearchCase::IgnoreCase)
			|| Type.Equals(TEXT("PUSH"), ESearchCase::IgnoreCase)
			|| Type.Equals(TEXT("MOVE_IN"), ESearchCase::IgnoreCase)
			|| Type.Equals(TEXT("MOVE_OUT"), ESearchCase::IgnoreCase)
			|| Type.Equals(TEXT("SLIDE"), ESearchCase::IgnoreCase);
	}

	bool UsesFadeOnly() const { return !Type.IsEmpty() && !UsesSlide(); }

	bool UsesBidirectionalSlide() const
	{
		return Type.Equals(TEXT("SMART_ANIMATE"), ESearchCase::IgnoreCase)
			|| Type.Equals(TEXT("PUSH"), ESearchCase::IgnoreCase)
			|| Type.Equals(TEXT("SLIDE"), ESearchCase::IgnoreCase);
	}

	bool UsesOutgoingSlideOnly() const { return Type.Equals(TEXT("MOVE_OUT"), ESearchCase::IgnoreCase); }
	bool UsesIncomingSlideOnly() const { return Type.Equals(TEXT("MOVE_IN"), ESearchCase::IgnoreCase); }
};

USTRUCT()
struct FUMGSourceNodeData
{
	GENERATED_BODY()

	UPROPERTY() FString SourceNodeId;
	UPROPERTY() FString SourceNodeName;
	UPROPERTY() FString SourceNodeType;
	UPROPERTY() FString ComponentId;
	UPROPERTY() FString ComponentSetId;
	UPROPERTY() FString MainComponentKey;
	UPROPERTY() FString ComponentPropertiesJson;
	UPROPERTY() FString VariantPropertiesJson;
	UPROPERTY() FString OverridesJson;

	UPROPERTY()
	TOptional<bool> HasIsExposedInstance;

	bool IsInstance() const
	{
		return SourceNodeType.Equals(TEXT("INSTANCE"), ESearchCase::IgnoreCase)
			|| !ComponentId.IsEmpty()
			|| !ComponentPropertiesJson.IsEmpty()
			|| !VariantPropertiesJson.IsEmpty()
			|| !OverridesJson.IsEmpty();
	}

	bool IsComponentDefinition() const { return SourceNodeType.Equals(TEXT("COMPONENT"), ESearchCase::IgnoreCase); }
	bool IsComponentSet() const { return SourceNodeType.Equals(TEXT("COMPONENT_SET"), ESearchCase::IgnoreCase); }
	bool HasAnyComponentSemantics() const { return IsInstance() || IsComponentDefinition() || IsComponentSet(); }

	bool HasAnyData() const
	{
		return !SourceNodeId.IsEmpty()
			|| !SourceNodeName.IsEmpty()
			|| !SourceNodeType.IsEmpty()
			|| !ComponentId.IsEmpty()
			|| !ComponentSetId.IsEmpty()
			|| !MainComponentKey.IsEmpty()
			|| !ComponentPropertiesJson.IsEmpty()
			|| !VariantPropertiesJson.IsEmpty()
			|| !OverridesJson.IsEmpty()
			|| HasIsExposedInstance.IsSet();
	}
};

USTRUCT()
struct FUMGSourceDocumentData
{
	GENERATED_BODY()

	UPROPERTY() FString ComponentsJson;
	UPROPERTY() FString ComponentSetsJson;

	bool HasAnyComponentCatalog() const { return !ComponentsJson.IsEmpty() || !ComponentSetsJson.IsEmpty(); }
};

UENUM()
enum class EUMGPrototypeNavigationAction : uint8
{
	Navigate, Overlay, Back, OpenLink, SetVariable, SetVariableMode, Conditional
};

UENUM()
enum class EUMGPrototypeNavigationStepKind : uint8
{
	Navigate, Overlay, Back, OpenLink, SetVariable, SetVariableMode, ConditionJump, Jump
};

USTRUCT()
struct FUMGPrototypeNavigationStep
{
	GENERATED_BODY()

	UPROPERTY() EUMGPrototypeNavigationStepKind Kind = EUMGPrototypeNavigationStepKind::Navigate;
	UPROPERTY() FString TargetPageId;
	UPROPERTY() int32 TargetPageIndex = INDEX_NONE;
	UPROPERTY() FString ExternalUrl;
	UPROPERTY() FUMGPrototypeTransitionData Transition;
	UPROPERTY() FString VariableId;
	UPROPERTY() FString VariableValueJson;
	UPROPERTY() FString VariableCollectionId;
	UPROPERTY() FString VariableModeId;
	UPROPERTY() FString ConditionJson;
	UPROPERTY() int32 JumpTargetIndex = INDEX_NONE;

	bool RequiresPageTarget() const { return Kind == EUMGPrototypeNavigationStepKind::Navigate || Kind == EUMGPrototypeNavigationStepKind::Overlay; }
	bool RequiresSwitcher() const { return Kind == EUMGPrototypeNavigationStepKind::Navigate || Kind == EUMGPrototypeNavigationStepKind::Overlay || Kind == EUMGPrototypeNavigationStepKind::Back; }
	bool IsOverlayAction() const { return Kind == EUMGPrototypeNavigationStepKind::Overlay; }
	bool IsTerminalAction() const { return Kind == EUMGPrototypeNavigationStepKind::Navigate || Kind == EUMGPrototypeNavigationStepKind::Overlay || Kind == EUMGPrototypeNavigationStepKind::Back || Kind == EUMGPrototypeNavigationStepKind::OpenLink; }

	bool IsValid() const
	{
		switch (Kind)
		{
		case EUMGPrototypeNavigationStepKind::Navigate:
		case EUMGPrototypeNavigationStepKind::Overlay:
			return !TargetPageId.IsEmpty() && TargetPageIndex >= 0;
		case EUMGPrototypeNavigationStepKind::Back: return true;
		case EUMGPrototypeNavigationStepKind::OpenLink: return !ExternalUrl.IsEmpty();
		case EUMGPrototypeNavigationStepKind::SetVariable: return !VariableId.IsEmpty() && !VariableValueJson.IsEmpty();
		case EUMGPrototypeNavigationStepKind::SetVariableMode: return !VariableCollectionId.IsEmpty() && !VariableModeId.IsEmpty();
		case EUMGPrototypeNavigationStepKind::ConditionJump:
		case EUMGPrototypeNavigationStepKind::Jump: return JumpTargetIndex >= 0;
		default: return false;
		}
	}
};

USTRUCT()
struct FUMGPrototypeNavigationBinding
{
	GENERATED_BODY()

	UPROPERTY() FString TriggerWidgetId;
	UPROPERTY() FString TriggerWidgetName;
	UPROPERTY() FString TriggerType;
	UPROPERTY() FString TriggerJson;
	UPROPERTY() TArray<FString> TriggerKeyCodes;
	UPROPERTY() FString TargetPageId;
	UPROPERTY() int32 TargetPageIndex = INDEX_NONE;
	UPROPERTY() EUMGPrototypeNavigationAction Action = EUMGPrototypeNavigationAction::Navigate;
	UPROPERTY() FString ExternalUrl;
	UPROPERTY() FUMGPrototypeTransitionData Transition;
	UPROPERTY() TArray<FUMGPrototypeNavigationStep> Steps;

	static bool IsClickTriggerType(const FString& T) { return T.IsEmpty() || T.Equals(TEXT("ON_CLICK"), ESearchCase::IgnoreCase); }
	static bool IsHoverTriggerType(const FString& T) { return T.Equals(TEXT("ON_HOVER"), ESearchCase::IgnoreCase); }
	static bool IsPressTriggerType(const FString& T) { return T.Equals(TEXT("ON_PRESS"), ESearchCase::IgnoreCase); }
	static bool IsDragTriggerType(const FString& T) { return T.Equals(TEXT("ON_DRAG"), ESearchCase::IgnoreCase); }
	static bool IsKeyDownTriggerType(const FString& T) { return T.Equals(TEXT("ON_KEY_DOWN"), ESearchCase::IgnoreCase); }
	static bool IsKeyUpTriggerType(const FString& T) { return T.Equals(TEXT("ON_KEY_UP"), ESearchCase::IgnoreCase); }

	bool UsesClickTrigger() const { return IsClickTriggerType(TriggerType); }
	bool UsesHoverTrigger() const { return IsHoverTriggerType(TriggerType); }
	bool UsesPressTrigger() const { return IsPressTriggerType(TriggerType); }
	bool UsesDragTrigger() const { return IsDragTriggerType(TriggerType); }
	bool UsesKeyDownTrigger() const { return IsKeyDownTriggerType(TriggerType); }
	bool UsesKeyUpTrigger() const { return IsKeyUpTriggerType(TriggerType); }
	bool UsesKeyboardTrigger() const { return UsesKeyDownTrigger() || UsesKeyUpTrigger(); }

	bool RequiresPageTarget() const
	{
		if (Steps.Num() > 0) { for (const auto& S : Steps) { if (S.RequiresPageTarget()) return true; } return false; }
		return Action == EUMGPrototypeNavigationAction::Navigate || Action == EUMGPrototypeNavigationAction::Overlay;
	}

	bool RequiresSwitcher() const
	{
		if (Steps.Num() > 0) { for (const auto& S : Steps) { if (S.RequiresSwitcher()) return true; } return false; }
		return Action != EUMGPrototypeNavigationAction::OpenLink;
	}

	bool HasOverlayAction() const
	{
		if (Steps.Num() > 0) { for (const auto& S : Steps) { if (S.IsOverlayAction()) return true; } return false; }
		return Action == EUMGPrototypeNavigationAction::Overlay;
	}

	bool IsValid() const
	{
		if (TriggerWidgetId.IsEmpty() || TriggerWidgetName.IsEmpty()) return false;
		if (!UsesClickTrigger() && !UsesHoverTrigger() && !UsesPressTrigger() && !UsesDragTrigger() && !UsesKeyDownTrigger() && !UsesKeyUpTrigger()) return false;
		if (UsesKeyboardTrigger() && TriggerKeyCodes.Num() == 0) return false;
		if (Steps.Num() > 0) { for (const auto& S : Steps) { if (!S.IsValid()) return false; } return true; }
		switch (Action)
		{
		case EUMGPrototypeNavigationAction::Navigate:
		case EUMGPrototypeNavigationAction::Overlay: return !TargetPageId.IsEmpty() && TargetPageIndex >= 0;
		case EUMGPrototypeNavigationAction::Back: return true;
		case EUMGPrototypeNavigationAction::OpenLink: return !ExternalUrl.IsEmpty();
		default: return false;
		}
	}
};

USTRUCT()
struct FUMGPrototypeNavigationData
{
	GENERATED_BODY()

	UPROPERTY() FString SwitcherWidgetId;
	UPROPERTY() FString SwitcherWidgetName;
	UPROPERTY() FString OverlayHostWidgetId;
	UPROPERTY() FString OverlayHostWidgetName;
	UPROPERTY() int32 InitialPageIndex = 0;
	UPROPERTY() TArray<FString> PageIds;
	UPROPERTY() TArray<FUMGPrototypeNavigationBinding> ClickBindings;

	bool HasOverlayBindings() const { for (const auto& B : ClickBindings) { if (B.HasOverlayAction()) return true; } return false; }
	bool RequiresSwitcher() const { for (const auto& B : ClickBindings) { if (B.RequiresSwitcher()) return true; } return false; }
	bool HasOverlayLayer() const { return !OverlayHostWidgetId.IsEmpty() && !OverlayHostWidgetName.IsEmpty(); }

	bool IsValid() const
	{
		if (ClickBindings.Num() == 0) return false;
		for (const auto& B : ClickBindings) { if (!B.IsValid()) return false; }
		if (!RequiresSwitcher()) return true;
		if (SwitcherWidgetId.IsEmpty() || SwitcherWidgetName.IsEmpty() || PageIds.Num() <= 1) return false;
		return !HasOverlayBindings() || HasOverlayLayer();
	}
};
