// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGPrototypeStepGraphAssembly.h"
#include "DesignJsonUtils.h"
#include "DesignSchemaSourceExtensions.h"

#include "Dom/JsonObject.h"

namespace Design2UMGPrototypeStepGraphAssembly
{
	struct FResolvedPrototypeAction
	{
		EUMGPrototypeNavigationAction Kind = EUMGPrototypeNavigationAction::Navigate;
		FString Payload;
		FUMGPrototypeTransitionData Transition;
		bool bResolved = false;
	};

	FString ResolvePrototypeActionDestinationId(
		const TSharedPtr<FDesignNode>& Node,
		const FDesignPrototypeAction& Action)
	{
		if (!Action.DestinationId.IsEmpty())
		{
			return Action.DestinationId;
		}

		if (!Action.TransitionNodeId.IsEmpty())
		{
			return Action.TransitionNodeId;
		}

		return Node.IsValid() ? Node->Source().Prototype.TransitionNodeId : FString();
	}

	bool IsPrototypeNavigateNavigation(const FString& Value)
	{
		return Value.IsEmpty()
			|| Value.Equals(TEXT("NAVIGATE"), ESearchCase::IgnoreCase)
			|| Value.Equals(TEXT("SWAP"), ESearchCase::IgnoreCase);
	}

	bool IsPrototypeOverlayKeyword(const FString& Value)
	{
		return Value.Equals(TEXT("OVERLAY"), ESearchCase::IgnoreCase)
			|| Value.Equals(TEXT("OPEN_OVERLAY"), ESearchCase::IgnoreCase)
			|| Value.Equals(TEXT("SWAP_OVERLAY"), ESearchCase::IgnoreCase);
	}

	bool IsPrototypeBackKeyword(const FString& Value)
	{
		return Value.Equals(TEXT("BACK"), ESearchCase::IgnoreCase);
	}

	bool IsPrototypeOpenLinkKeyword(const FString& Value)
	{
		return Value.Equals(TEXT("URL"), ESearchCase::IgnoreCase)
			|| Value.Equals(TEXT("OPEN_URL"), ESearchCase::IgnoreCase)
			|| Value.Equals(TEXT("OPEN_LINK"), ESearchCase::IgnoreCase)
			|| Value.Equals(TEXT("LINK"), ESearchCase::IgnoreCase);
	}

	bool LooksLikeExternalPrototypeLink(const FString& Value)
	{
		return Value.Contains(TEXT("://"))
			|| Value.StartsWith(TEXT("mailto:"), ESearchCase::IgnoreCase)
			|| Value.StartsWith(TEXT("tel:"), ESearchCase::IgnoreCase)
			|| Value.StartsWith(TEXT("www."), ESearchCase::IgnoreCase);
	}

	bool IsPrototypeNavigateAction(const FDesignPrototypeAction& Action)
	{
		return Action.Type.Equals(TEXT("NODE"), ESearchCase::IgnoreCase)
			&& IsPrototypeNavigateNavigation(Action.Navigation);
	}

	bool IsPrototypeOverlayAction(const FDesignPrototypeAction& Action)
	{
		const bool bOverlayKeyword =
			IsPrototypeOverlayKeyword(Action.Navigation) || IsPrototypeOverlayKeyword(Action.Type);
		const bool bNodeLike =
			Action.Type.IsEmpty()
			|| Action.Type.Equals(TEXT("NODE"), ESearchCase::IgnoreCase)
			|| IsPrototypeOverlayKeyword(Action.Type);
		return bOverlayKeyword && bNodeLike;
	}

	bool IsPrototypeBackAction(const FDesignPrototypeAction& Action)
	{
		return IsPrototypeBackKeyword(Action.Type) || IsPrototypeBackKeyword(Action.Navigation);
	}

	bool IsPrototypeOpenLinkAction(
		const FDesignPrototypeAction& Action,
		const FString& DestinationId)
	{
		return IsPrototypeOpenLinkKeyword(Action.Type)
			|| IsPrototypeOpenLinkKeyword(Action.Navigation)
			|| LooksLikeExternalPrototypeLink(DestinationId);
	}

	const TCHAR* DescribePrototypeActionKind(const EUMGPrototypeNavigationAction Kind)
	{
		switch (Kind)
		{
		case EUMGPrototypeNavigationAction::Navigate:
			return TEXT("navigation");
		case EUMGPrototypeNavigationAction::Overlay:
			return TEXT("overlay");
		case EUMGPrototypeNavigationAction::Back:
			return TEXT("back");
		case EUMGPrototypeNavigationAction::OpenLink:
			return TEXT("open link");
		default:
			return TEXT("prototype action");
		}
	}

	FDesignPrototypeTransitionData ResolvePrototypeTransitionSource(
		const TSharedPtr<FDesignNode>& Node,
		const FDesignPrototypeAction& Action)
	{
		FDesignPrototypeTransitionData Resolved;
		if (Node.IsValid() && Node->Source().Prototype.Transition.HasAny())
		{
			Resolved = Node->Source().Prototype.Transition;
		}

		if (!Action.Transition.HasAny())
		{
			return Resolved;
		}

		if (!Action.Transition.Type.IsEmpty())
		{
			Resolved.Type = Action.Transition.Type;
		}

		if (!Action.Transition.Direction.IsEmpty())
		{
			Resolved.Direction = Action.Transition.Direction;
		}

		if (Action.Transition.Duration.IsSet())
		{
			Resolved.Duration = Action.Transition.Duration;
		}

		if (!Action.Transition.EasingType.IsEmpty())
		{
			Resolved.EasingType = Action.Transition.EasingType;
		}

		if (!Action.Transition.EasingJson.IsEmpty())
		{
			Resolved.EasingJson = Action.Transition.EasingJson;
		}

		if (Action.Transition.MatchLayers.IsSet())
		{
			Resolved.MatchLayers = Action.Transition.MatchLayers;
		}

		return Resolved;
	}

	FUMGPrototypeTransitionData BuildPrototypeTransition(
		const TSharedPtr<FDesignNode>& Node,
		const FDesignPrototypeAction& Action)
	{
		const FDesignPrototypeTransitionData Source = ResolvePrototypeTransitionSource(Node, Action);

		FUMGPrototypeTransitionData Transition;
		Transition.Type = Source.Type.ToUpper();
		Transition.Direction = Source.Direction.ToUpper();
		Transition.EasingType = Source.EasingType.ToUpper();
		Transition.HasMatchLayers = Source.MatchLayers;

		if (Source.Duration.IsSet())
		{
			Transition.DurationSeconds = FMath::Max(0.0f, Source.Duration.GetValue() / 1000.0f);
		}
		else if (!Transition.Type.IsEmpty())
		{
			Transition.DurationSeconds = 0.2f;
		}

		if (Transition.Type.IsEmpty() && Transition.DurationSeconds > KINDA_SMALL_NUMBER)
		{
			Transition.Type = TEXT("DISSOLVE");
		}

		return Transition;
	}

	EUMGPrototypeNavigationAction MapPrototypeStepKindToLegacyAction(
		const EUMGPrototypeNavigationStepKind Kind)
	{
		switch (Kind)
		{
		case EUMGPrototypeNavigationStepKind::Navigate:
			return EUMGPrototypeNavigationAction::Navigate;
		case EUMGPrototypeNavigationStepKind::Overlay:
			return EUMGPrototypeNavigationAction::Overlay;
		case EUMGPrototypeNavigationStepKind::Back:
			return EUMGPrototypeNavigationAction::Back;
		case EUMGPrototypeNavigationStepKind::OpenLink:
			return EUMGPrototypeNavigationAction::OpenLink;
		case EUMGPrototypeNavigationStepKind::SetVariable:
			return EUMGPrototypeNavigationAction::SetVariable;
		case EUMGPrototypeNavigationStepKind::SetVariableMode:
			return EUMGPrototypeNavigationAction::SetVariableMode;
		case EUMGPrototypeNavigationStepKind::ConditionJump:
		case EUMGPrototypeNavigationStepKind::Jump:
			return EUMGPrototypeNavigationAction::Conditional;
		default:
			return EUMGPrototypeNavigationAction::Navigate;
		}
	}

	bool ResolvePrototypePageTarget(
		const TSharedPtr<FDesignNode>& Node,
		const EUMGPrototypeNavigationAction ActionKind,
		const FString& TargetNodeId,
		const TMap<FString, FString>& NodeToPageId,
		const TMap<FString, int32>& PageIdToIndex,
		FUMGPrototypeNavigationStep& InOutStep,
		TArray<FString>& Warnings)
	{
		const FString* ResolvedPageId = NodeToPageId.Find(TargetNodeId);
		if (!ResolvedPageId)
		{
			Warnings.AddUnique(FString::Printf(
				TEXT("Prototype %s skipped on '%s': unresolved destination '%s'"),
				DescribePrototypeActionKind(ActionKind),
				Node.IsValid() ? *Node->Id : TEXT("<unknown>"),
				*TargetNodeId));
			return false;
		}

		const int32* ResolvedPageIndex = PageIdToIndex.Find(*ResolvedPageId);
		if (!ResolvedPageIndex || *ResolvedPageIndex < 0)
		{
			return false;
		}

		InOutStep.TargetPageId = *ResolvedPageId;
		InOutStep.TargetPageIndex = *ResolvedPageIndex;
		return true;
	}

	// Single typed entry point for prototype actions: set-variable, set-variable-mode
	// and conditional programs first, then the terminal kinds (Back/OpenLink/Overlay/
	// Navigate). Payloads are parsed once by the source parser; no raw JSON re-parse.
	bool AppendPrototypeActionStepsFromTypedAction(
		const TSharedPtr<FDesignNode>& Node,
		const FDesignPrototypeAction& Action,
		const TMap<FString, FString>& NodeToPageId,
		const TMap<FString, int32>& PageIdToIndex,
		TArray<FUMGPrototypeNavigationStep>& OutSteps,
		TArray<FString>& Warnings)
	{
		const FString ActionTypeUpper = Action.Type.ToUpper();

		if (ActionTypeUpper.Equals(TEXT("SET_VARIABLE"), ESearchCase::CaseSensitive))
		{
			FUMGPrototypeNavigationStep Step;
			Step.Kind = EUMGPrototypeNavigationStepKind::SetVariable;
			Step.VariableId = Action.VariableId;
			Step.VariableValueJson = Action.VariableValueJson;

			if (!Step.IsValid())
			{
				Warnings.AddUnique(FString::Printf(
					TEXT("Prototype set variable skipped on '%s': missing variable payload"),
					Node.IsValid() ? *Node->Id : TEXT("<unknown>")));
				return false;
			}

			OutSteps.Add(MoveTemp(Step));
			return true;
		}

		if (ActionTypeUpper.Equals(TEXT("SET_VARIABLE_MODE"), ESearchCase::CaseSensitive))
		{
			FUMGPrototypeNavigationStep Step;
			Step.Kind = EUMGPrototypeNavigationStepKind::SetVariableMode;
			Step.VariableCollectionId = Action.VariableCollectionId;
			Step.VariableModeId = Action.VariableModeId;

			if (!Step.IsValid())
			{
				Warnings.AddUnique(FString::Printf(
					TEXT("Prototype set variable mode skipped on '%s': missing mode payload"),
					Node.IsValid() ? *Node->Id : TEXT("<unknown>")));
				return false;
			}

			OutSteps.Add(MoveTemp(Step));
			return true;
		}

		const bool bLooksConditional =
			ActionTypeUpper.Equals(TEXT("CONDITIONAL"), ESearchCase::CaseSensitive)
			|| Action.ConditionalBlocks.Num() > 0;
		if (bLooksConditional)
		{
			if (Action.ConditionalBlocks.Num() == 0)
			{
				Warnings.AddUnique(FString::Printf(
					TEXT("Prototype conditional skipped on '%s': no conditional blocks"),
					Node.IsValid() ? *Node->Id : TEXT("<unknown>")));
				return false;
			}

			const int32 StartIndex = OutSteps.Num();
			int32 PendingFalseJumpIndex = INDEX_NONE;
			TArray<int32> ExitJumpIndices;

			for (const FDesignPrototypeConditionalBlock& Block : Action.ConditionalBlocks)
			{
				const int32 BlockStartIndex = OutSteps.Num();
				if (PendingFalseJumpIndex != INDEX_NONE)
				{
					OutSteps[PendingFalseJumpIndex].JumpTargetIndex = BlockStartIndex;
				}

				FUMGPrototypeNavigationStep ConditionStep;
				ConditionStep.Kind = EUMGPrototypeNavigationStepKind::ConditionJump;
				ConditionStep.ConditionJson = Block.ConditionJson;
				const int32 ConditionStepIndex = OutSteps.Add(MoveTemp(ConditionStep));
				PendingFalseJumpIndex = ConditionStepIndex;

				for (const FDesignPrototypeAction& NestedAction : Block.Actions)
				{
					AppendPrototypeActionStepsFromTypedAction(
						Node,
						NestedAction,
						NodeToPageId,
						PageIdToIndex,
						OutSteps,
						Warnings);
				}

				FUMGPrototypeNavigationStep ExitJumpStep;
				ExitJumpStep.Kind = EUMGPrototypeNavigationStepKind::Jump;
				ExitJumpIndices.Add(OutSteps.Add(MoveTemp(ExitJumpStep)));
			}

			if (OutSteps.Num() == StartIndex)
			{
				return false;
			}

			const int32 AfterConditionalIndex = OutSteps.Num();
			if (PendingFalseJumpIndex != INDEX_NONE)
			{
				OutSteps[PendingFalseJumpIndex].JumpTargetIndex = AfterConditionalIndex;
			}

			for (const int32 ExitJumpIndex : ExitJumpIndices)
			{
				OutSteps[ExitJumpIndex].JumpTargetIndex = AfterConditionalIndex;
			}

			return true;
		}

		const FString DirectDestinationId = Action.DestinationId;
		const FString ResolvedNodeDestinationId = ResolvePrototypeActionDestinationId(Node, Action);

		FUMGPrototypeNavigationStep Step;
		Step.Transition = BuildPrototypeTransition(Node, Action);

		bool bMatched = false;
		if (IsPrototypeBackAction(Action))
		{
			Step.Kind = EUMGPrototypeNavigationStepKind::Back;
			bMatched = true;
		}
		else if (IsPrototypeOpenLinkAction(Action, DirectDestinationId)
			&& !DirectDestinationId.IsEmpty())
		{
			Step.Kind = EUMGPrototypeNavigationStepKind::OpenLink;
			Step.ExternalUrl = DirectDestinationId;
			bMatched = true;
		}
		else if (IsPrototypeOverlayAction(Action) && !ResolvedNodeDestinationId.IsEmpty())
		{
			Step.Kind = EUMGPrototypeNavigationStepKind::Overlay;
			bMatched = ResolvePrototypePageTarget(
				Node,
				EUMGPrototypeNavigationAction::Overlay,
				ResolvedNodeDestinationId,
				NodeToPageId,
				PageIdToIndex,
				Step,
				Warnings);
		}
		else if (IsPrototypeNavigateAction(Action) && !ResolvedNodeDestinationId.IsEmpty())
		{
			Step.Kind = EUMGPrototypeNavigationStepKind::Navigate;
			bMatched = ResolvePrototypePageTarget(
				Node,
				EUMGPrototypeNavigationAction::Navigate,
				ResolvedNodeDestinationId,
				NodeToPageId,
				PageIdToIndex,
				Step,
				Warnings);
		}

		if (!bMatched || !Step.IsValid())
		{
			if (!Action.Type.IsEmpty())
			{
				Warnings.AddUnique(FString::Printf(
					TEXT("Prototype action '%s' on node '%s' not supported yet"),
					*Action.Type,
					Node.IsValid() ? *Node->Id : TEXT("<unknown>")));
			}
			return false;
		}

		OutSteps.Add(MoveTemp(Step));
		return true;
	}

	FString NormalizePrototypeTriggerType(const FString& TriggerType)
	{
		if (FUMGPrototypeNavigationBinding::IsClickTriggerType(TriggerType))
		{
			return TEXT("ON_CLICK");
		}

		if (FUMGPrototypeNavigationBinding::IsHoverTriggerType(TriggerType))
		{
			return TEXT("ON_HOVER");
		}

		if (FUMGPrototypeNavigationBinding::IsPressTriggerType(TriggerType))
		{
			return TEXT("ON_PRESS");
		}

		if (FUMGPrototypeNavigationBinding::IsDragTriggerType(TriggerType))
		{
			return TEXT("ON_DRAG");
		}

		if (FUMGPrototypeNavigationBinding::IsKeyDownTriggerType(TriggerType))
		{
			return TEXT("ON_KEY_DOWN");
		}

		if (FUMGPrototypeNavigationBinding::IsKeyUpTriggerType(TriggerType))
		{
			return TEXT("ON_KEY_UP");
		}

		return TriggerType;
	}

	TArray<FString> ParsePrototypeTriggerKeyCodes(const FString& TriggerJson)
	{
		TArray<FString> KeyCodes;

		TSharedPtr<FJsonObject> TriggerObject;
		if (!DesignJsonUtils::TryParseObject(TriggerJson, TriggerObject))
		{
			return KeyCodes;
		}

		if (const TArray<TSharedPtr<FJsonValue>>* KeyCodeArray =
			DesignJsonUtils::GetArray(TriggerObject, TEXT("keyCodes")))
		{
			for (const TSharedPtr<FJsonValue>& KeyCodeValue : *KeyCodeArray)
			{
				if (!KeyCodeValue.IsValid())
				{
					continue;
				}

				FString KeyCodeString;
				if (KeyCodeValue->TryGetString(KeyCodeString) && !KeyCodeString.IsEmpty())
				{
					KeyCodes.AddUnique(KeyCodeString.ToUpper());
				}
			}
		}

		const FString SingleKeyCode = DesignJsonUtils::GetString(TriggerObject, TEXT("keyCode"));
		if (!SingleKeyCode.IsEmpty())
		{
			KeyCodes.AddUnique(SingleKeyCode.ToUpper());
		}

		return KeyCodes;
	}

	bool BuildPrototypeBindingSummary(
		const TArray<FUMGPrototypeNavigationStep>& Steps,
		FResolvedPrototypeAction& OutSummary)
	{
		if (Steps.Num() == 0)
		{
			return false;
		}

		OutSummary = FResolvedPrototypeAction();
		for (const FUMGPrototypeNavigationStep& Step : Steps)
		{
			switch (Step.Kind)
			{
			case EUMGPrototypeNavigationStepKind::Navigate:
				OutSummary.Kind = EUMGPrototypeNavigationAction::Navigate;
				OutSummary.Payload = Step.TargetPageId;
				OutSummary.Transition = Step.Transition;
				OutSummary.bResolved = true;
				return true;
			case EUMGPrototypeNavigationStepKind::Overlay:
				OutSummary.Kind = EUMGPrototypeNavigationAction::Overlay;
				OutSummary.Payload = Step.TargetPageId;
				OutSummary.Transition = Step.Transition;
				OutSummary.bResolved = true;
				return true;
			case EUMGPrototypeNavigationStepKind::Back:
				OutSummary.Kind = EUMGPrototypeNavigationAction::Back;
				OutSummary.Payload.Reset();
				OutSummary.Transition = Step.Transition;
				OutSummary.bResolved = true;
				return true;
			case EUMGPrototypeNavigationStepKind::OpenLink:
				OutSummary.Kind = EUMGPrototypeNavigationAction::OpenLink;
				OutSummary.Payload = Step.ExternalUrl;
				OutSummary.Transition = Step.Transition;
				OutSummary.bResolved = true;
				return true;
			default:
				break;
			}
		}

		const FUMGPrototypeNavigationStep& FirstStep = Steps[0];
		OutSummary.Kind = MapPrototypeStepKindToLegacyAction(FirstStep.Kind);
		OutSummary.Transition = FirstStep.Transition;
		OutSummary.bResolved = true;
		return true;
	}

	FString BuildPrototypeStepSignature(const FUMGPrototypeNavigationStep& Step)
	{
		return FString::Printf(
			TEXT("%d|%s|%d|%s|%s|%s|%s|%s|%s|%d|%s|%s|%f"),
			static_cast<int32>(Step.Kind),
			*Step.TargetPageId,
			Step.TargetPageIndex,
			*Step.ExternalUrl,
			*Step.VariableId,
			*Step.VariableValueJson,
			*Step.VariableCollectionId,
			*Step.VariableModeId,
			*Step.ConditionJson,
			Step.JumpTargetIndex,
			*Step.Transition.Type,
			*Step.Transition.Direction,
			Step.Transition.DurationSeconds);
	}

	void GatherNavigationBindings(
		const TSharedPtr<FDesignNode>& Node,
		const TMap<FString, FString>& NodeToPageId,
		const TMap<FString, int32>& PageIdToIndex,
		TArray<FUMGPrototypeNavigationBinding>& OutBindings,
		TSet<FString>& OutTriggerIds,
		TSet<FString>& InOutSeenBindingKeys,
		TArray<FString>& Warnings)
	{
		if (!Node.IsValid())
		{
			return;
		}

		for (const FDesignPrototypeInteraction& Interaction : Node->Source().Prototype.Interactions)
		{
			const FString NormalizedTriggerType = NormalizePrototypeTriggerType(Interaction.TriggerType);
			const bool bUsesSupportedTrigger =
				FUMGPrototypeNavigationBinding::IsClickTriggerType(NormalizedTriggerType)
				|| FUMGPrototypeNavigationBinding::IsHoverTriggerType(NormalizedTriggerType)
				|| FUMGPrototypeNavigationBinding::IsPressTriggerType(NormalizedTriggerType)
				|| FUMGPrototypeNavigationBinding::IsDragTriggerType(NormalizedTriggerType)
				|| FUMGPrototypeNavigationBinding::IsKeyDownTriggerType(NormalizedTriggerType)
				|| FUMGPrototypeNavigationBinding::IsKeyUpTriggerType(NormalizedTriggerType);
			if (!bUsesSupportedTrigger && Interaction.Actions.Num() > 0)
			{
				Warnings.AddUnique(FString::Printf(
					TEXT("Prototype trigger '%s' on node '%s' not supported yet"),
					*Interaction.TriggerType,
					*Node->Id));
				continue;
			}

			FUMGPrototypeNavigationBinding Binding;
			Binding.TriggerWidgetId = Node->Id;
			Binding.TriggerWidgetName = Node->Id;
			Binding.TriggerType = NormalizedTriggerType;
			Binding.TriggerJson = Interaction.TriggerJson;
			Binding.TriggerKeyCodes = ParsePrototypeTriggerKeyCodes(Interaction.TriggerJson);
			if (Binding.UsesKeyboardTrigger() && Binding.TriggerKeyCodes.Num() == 0)
			{
				Warnings.AddUnique(FString::Printf(
					TEXT("Prototype keyboard trigger '%s' on node '%s' skipped: missing keyCodes"),
					*Interaction.TriggerType,
					*Node->Id));
				continue;
			}

			for (const FDesignPrototypeAction& Action : Interaction.Actions)
			{
				AppendPrototypeActionStepsFromTypedAction(
					Node,
					Action,
					NodeToPageId,
					PageIdToIndex,
					Binding.Steps,
					Warnings);
			}

			if (Binding.Steps.Num() == 0)
			{
				continue;
			}

			FResolvedPrototypeAction Summary;
			if (BuildPrototypeBindingSummary(Binding.Steps, Summary))
			{
				Binding.Action = Summary.Kind;
				Binding.Transition = Summary.Transition;
				if (Summary.Kind == EUMGPrototypeNavigationAction::Navigate
					|| Summary.Kind == EUMGPrototypeNavigationAction::Overlay)
				{
					Binding.TargetPageId = Summary.Payload;
					if (const int32* PageIndex = PageIdToIndex.Find(Summary.Payload))
					{
						Binding.TargetPageIndex = *PageIndex;
					}
				}
				else if (Summary.Kind == EUMGPrototypeNavigationAction::OpenLink)
				{
					Binding.ExternalUrl = Summary.Payload;
				}
			}

			const FString StepSignature = FString::JoinBy(
				Binding.Steps,
				TEXT(";"),
				[](const FUMGPrototypeNavigationStep& Step)
				{
					return BuildPrototypeStepSignature(Step);
				});
			const FString BindingKey = FString::Printf(
				TEXT("%s::%s::%s::%s"),
				*Node->Id,
				*Binding.TriggerType,
				*FString::Join(Binding.TriggerKeyCodes, TEXT(",")),
				*StepSignature);
			if (InOutSeenBindingKeys.Contains(BindingKey) || !Binding.IsValid())
			{
				continue;
			}

			OutBindings.Add(Binding);
			OutTriggerIds.Add(Node->Id);
			InOutSeenBindingKeys.Add(BindingKey);
		}

		for (const TSharedPtr<FDesignNode>& Child : Node->Children)
		{
			GatherNavigationBindings(
				Child,
				NodeToPageId,
				PageIdToIndex,
				OutBindings,
				OutTriggerIds,
				InOutSeenBindingKeys,
				Warnings);
		}
	}

}
