// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGPrototypeStepGraphAssembly.h"
#include "DesignSchemaSourceExtensions.h"

#include "Dom/JsonObject.h"
#include "Misc/Crc.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"

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
		Transition.HasMatchLayers.bValue = Source.MatchLayers.Get(false);
		Transition.HasMatchLayers.bIsSet = Source.MatchLayers.IsSet();

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

	FString SerializePrototypeJsonValue(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid())
		{
			return FString();
		}

		FString Json;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
		if (FJsonSerializer::Serialize(Value.ToSharedRef(), FString(), Writer))
		{
			return Json;
		}

		return FString();
	}

	FString SerializePrototypeJsonObject(const TSharedPtr<FJsonObject>& Object)
	{
		return Object.IsValid()
			? SerializePrototypeJsonValue(MakeShared<FJsonValueObject>(Object.ToSharedRef()))
			: FString();
	}

	bool TryParsePrototypeJsonObject(const FString& Json, TSharedPtr<FJsonObject>& OutObject)
	{
		OutObject.Reset();
		if (Json.IsEmpty())
		{
			return false;
		}

		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
	}

	FString GetPrototypeJsonString(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName)
	{
		if (!Object.IsValid())
		{
			return FString();
		}

		FString Value;
		if (Object->TryGetStringField(FieldName, Value))
		{
			return Value;
		}

		return FString();
	}

	bool TryGetPrototypeJsonNumber(
		const TSharedPtr<FJsonObject>& Object,
		const TCHAR* FieldName,
		double& OutValue)
	{
		return Object.IsValid() && Object->TryGetNumberField(FieldName, OutValue);
	}

	TSharedPtr<FJsonObject> GetPrototypeJsonObject(
		const TSharedPtr<FJsonObject>& Object,
		const TCHAR* FieldName)
	{
		if (!Object.IsValid())
		{
			return nullptr;
		}

		const TSharedPtr<FJsonObject>* ObjectPtr = nullptr;
		if (Object->TryGetObjectField(FieldName, ObjectPtr))
		{
			return *ObjectPtr;
		}

		return nullptr;
	}

	const TArray<TSharedPtr<FJsonValue>>* GetPrototypeJsonArray(
		const TSharedPtr<FJsonObject>& Object,
		const TCHAR* FieldName)
	{
		if (!Object.IsValid())
		{
			return nullptr;
		}

		const TArray<TSharedPtr<FJsonValue>>* ArrayPtr = nullptr;
		if (Object->TryGetArrayField(FieldName, ArrayPtr))
		{
			return ArrayPtr;
		}

		return nullptr;
	}

	void ParsePrototypeTransitionFromJsonObject(
		const TSharedPtr<FJsonObject>& TransitionObject,
		FDesignPrototypeTransitionData& OutTransition)
	{
		if (!TransitionObject.IsValid())
		{
			return;
		}

		OutTransition.Type = GetPrototypeJsonString(TransitionObject, TEXT("type"));
		OutTransition.Direction = GetPrototypeJsonString(TransitionObject, TEXT("direction"));

		double DurationValue = 0.0;
		if (TryGetPrototypeJsonNumber(TransitionObject, TEXT("duration"), DurationValue))
		{
			OutTransition.Duration.Emplace(static_cast<float>(DurationValue));
		}

		if (const TSharedPtr<FJsonObject> EasingObject =
			GetPrototypeJsonObject(TransitionObject, TEXT("easing")))
		{
			OutTransition.EasingJson = SerializePrototypeJsonObject(EasingObject);
			OutTransition.EasingType = GetPrototypeJsonString(EasingObject, TEXT("type"));
		}

		bool bMatchLayers = false;
		if (TransitionObject->TryGetBoolField(TEXT("matchLayers"), bMatchLayers))
		{
			OutTransition.MatchLayers.Emplace(bMatchLayers);
		}
	}

	FDesignPrototypeAction ParsePrototypeActionFromJsonObject(
		const TSharedPtr<FJsonObject>& ActionObject)
	{
		FDesignPrototypeAction Action;
		if (!ActionObject.IsValid())
		{
			return Action;
		}

		Action.RawJson = SerializePrototypeJsonObject(ActionObject);
		Action.Type = GetPrototypeJsonString(ActionObject, TEXT("type"));
		Action.DestinationId = GetPrototypeJsonString(ActionObject, TEXT("destinationId"));
		if (Action.DestinationId.IsEmpty())
		{
			Action.DestinationId = GetPrototypeJsonString(ActionObject, TEXT("url"));
		}

		if (Action.DestinationId.IsEmpty())
		{
			Action.DestinationId = GetPrototypeJsonString(ActionObject, TEXT("uri"));
		}

		Action.Navigation = GetPrototypeJsonString(ActionObject, TEXT("navigation"));
		Action.TransitionNodeId = GetPrototypeJsonString(ActionObject, TEXT("transitionNodeID"));
		if (const TSharedPtr<FJsonObject> TransitionObject =
			GetPrototypeJsonObject(ActionObject, TEXT("transition")))
		{
			Action.TransitionJson = SerializePrototypeJsonObject(TransitionObject);
			ParsePrototypeTransitionFromJsonObject(TransitionObject, Action.Transition);
		}

		return Action;
	}

	void CollectPrototypeReservedTokens(
		const TSharedPtr<FDesignNode>& Node,
		TSet<FString>& OutReservedTokens)
	{
		if (!Node.IsValid())
		{
			return;
		}

		if (!Node->Id.IsEmpty())
		{
			OutReservedTokens.Add(Node->Id);
		}

		if (!Node->Name.IsEmpty())
		{
			OutReservedTokens.Add(Node->Name);
		}

		for (const TSharedPtr<FDesignNode>& Child : Node->Children)
		{
			CollectPrototypeReservedTokens(Child, OutReservedTokens);
		}
	}

	uint32 BuildPrototypeInternalTokenHash(
		const TArray<TSharedPtr<FDesignNode>>& PrototypePages)
	{
		FString HashSource;
		for (const TSharedPtr<FDesignNode>& PageNode : PrototypePages)
		{
			if (!PageNode.IsValid())
			{
				continue;
			}

			HashSource += PageNode->Id;
			HashSource += TEXT("|");
			HashSource += PageNode->Name;
			HashSource += TEXT(";");
		}

		return FCrc::StrCrc32(*HashSource);
	}

	FString SanitizePrototypeTokenSuffix(const FString& Suffix)
	{
		FString Sanitized;
		Sanitized.Reserve(Suffix.Len());

		for (const TCHAR Character : Suffix)
		{
			if (FChar::IsAlnum(Character))
			{
				Sanitized.AppendChar(FChar::ToLower(Character));
			}
			else
			{
				Sanitized.AppendChar(TEXT('_'));
			}
		}

		while (Sanitized.ReplaceInline(TEXT("__"), TEXT("_"), ESearchCase::CaseSensitive) > 0)
		{
		}

		Sanitized.TrimCharInline(TEXT('_'), nullptr);
		if (Sanitized.IsEmpty())
		{
			return TEXT("node");
		}

		return Sanitized;
	}

	FString MakeUniquePrototypeInternalToken(
		const TSet<FString>& ReservedTokens,
		const uint32 PrototypeInternalTokenHash,
		const FString& Suffix)
	{
		const FString SanitizedSuffix = SanitizePrototypeTokenSuffix(Suffix);
		const FString BaseToken = FString::Printf(
			TEXT("__d2u_prototype_%s_%08x"),
			*SanitizedSuffix,
			PrototypeInternalTokenHash);

		FString Candidate = BaseToken;
		int32 CollisionIndex = 0;
		while (ReservedTokens.Contains(Candidate))
		{
			++CollisionIndex;
			Candidate = FString::Printf(TEXT("%s_%d"), *BaseToken, CollisionIndex);
		}

		return Candidate;
	}

	void MapNodeIdsToTopLevelPage(
		const TSharedPtr<FDesignNode>& Node,
		const FString& PageId,
		TMap<FString, FString>& OutNodeToPageId)
	{
		if (!Node.IsValid())
		{
			return;
		}

		if (!Node->Id.IsEmpty() && !OutNodeToPageId.Contains(Node->Id))
		{
			OutNodeToPageId.Add(Node->Id, PageId);
		}

		for (const TSharedPtr<FDesignNode>& Child : Node->Children)
		{
			MapNodeIdsToTopLevelPage(Child, PageId, OutNodeToPageId);
		}
	}

	FPrototypeRoutingContext BuildRoutingContext(
		const TArray<TSharedPtr<FDesignNode>>& PrototypePages)
	{
		FPrototypeRoutingContext Context;
		for (const TSharedPtr<FDesignNode>& PageNode : PrototypePages)
		{
			CollectPrototypeReservedTokens(PageNode, Context.ReservedTokens);
		}

		Context.InternalTokenHash = BuildPrototypeInternalTokenHash(PrototypePages);

		if (PrototypePages.Num() <= 1)
		{
			return Context;
		}

		for (int32 PageIndex = 0; PageIndex < PrototypePages.Num(); ++PageIndex)
		{
			const TSharedPtr<FDesignNode>& PageNode = PrototypePages[PageIndex];
			if (!PageNode.IsValid() || PageNode->Id.IsEmpty())
			{
				continue;
			}

			Context.PageIds.Add(PageNode->Id);
			Context.PageIdToIndex.Add(PageNode->Id, PageIndex);
			MapNodeIdsToTopLevelPage(PageNode, PageNode->Id, Context.NodeToPageId);
		}

		return Context;
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

	bool AppendPrototypeActionStepFromTypedAction(
		const TSharedPtr<FDesignNode>& Node,
		const FDesignPrototypeAction& Action,
		const TMap<FString, FString>& NodeToPageId,
		const TMap<FString, int32>& PageIdToIndex,
		TArray<FUMGPrototypeNavigationStep>& OutSteps,
		TArray<FString>& Warnings)
	{
		const FString DirectDestinationId = Action.DestinationId;
		const FString ResolvedNodeDestinationId = ResolvePrototypeActionDestinationId(Node, Action);

		FUMGPrototypeNavigationStep Step;
		Step.Transition = BuildPrototypeTransition(Node, Action);

		if (IsPrototypeBackAction(Action))
		{
			Step.Kind = EUMGPrototypeNavigationStepKind::Back;
		}
		else if (IsPrototypeOpenLinkAction(Action, DirectDestinationId)
			&& !DirectDestinationId.IsEmpty())
		{
			Step.Kind = EUMGPrototypeNavigationStepKind::OpenLink;
			Step.ExternalUrl = DirectDestinationId;
		}
		else if (IsPrototypeOverlayAction(Action) && !ResolvedNodeDestinationId.IsEmpty())
		{
			Step.Kind = EUMGPrototypeNavigationStepKind::Overlay;
			if (!ResolvePrototypePageTarget(
				Node,
				EUMGPrototypeNavigationAction::Overlay,
				ResolvedNodeDestinationId,
				NodeToPageId,
				PageIdToIndex,
				Step,
				Warnings))
			{
				return false;
			}
		}
		else if (IsPrototypeNavigateAction(Action) && !ResolvedNodeDestinationId.IsEmpty())
		{
			Step.Kind = EUMGPrototypeNavigationStepKind::Navigate;
			if (!ResolvePrototypePageTarget(
				Node,
				EUMGPrototypeNavigationAction::Navigate,
				ResolvedNodeDestinationId,
				NodeToPageId,
				PageIdToIndex,
				Step,
				Warnings))
			{
				return false;
			}
		}
		else
		{
			return false;
		}

		if (!Step.IsValid())
		{
			return false;
		}

		OutSteps.Add(MoveTemp(Step));
		return true;
	}

	bool AppendPrototypeActionStepsFromJsonObject(
		const TSharedPtr<FDesignNode>& Node,
		const TSharedPtr<FJsonObject>& ActionObject,
		const TMap<FString, FString>& NodeToPageId,
		const TMap<FString, int32>& PageIdToIndex,
		TArray<FUMGPrototypeNavigationStep>& OutSteps,
		TArray<FString>& Warnings)
	{
		if (!ActionObject.IsValid())
		{
			return false;
		}

		const FDesignPrototypeAction ParsedAction = ParsePrototypeActionFromJsonObject(ActionObject);
		const FString ActionTypeUpper = ParsedAction.Type.ToUpper();

		if (ActionTypeUpper.Equals(TEXT("SET_VARIABLE"), ESearchCase::CaseSensitive))
		{
			FUMGPrototypeNavigationStep Step;
			Step.Kind = EUMGPrototypeNavigationStepKind::SetVariable;
			Step.VariableId = GetPrototypeJsonString(ActionObject, TEXT("variableId"));

			const TSharedPtr<FJsonValue> VariableValueField =
				ActionObject->TryGetField(TEXT("variableValue"));
			const TSharedPtr<FJsonValue> LegacyValueField =
				ActionObject->TryGetField(TEXT("value"));
			Step.VariableValueJson = SerializePrototypeJsonValue(
				VariableValueField.IsValid() ? VariableValueField : LegacyValueField);

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
			Step.VariableCollectionId =
				GetPrototypeJsonString(ActionObject, TEXT("variableCollectionId"));
			Step.VariableModeId = GetPrototypeJsonString(ActionObject, TEXT("variableModeId"));

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
			|| GetPrototypeJsonArray(ActionObject, TEXT("conditionalBlocks")) != nullptr;
		if (bLooksConditional)
		{
			const TArray<TSharedPtr<FJsonValue>>* ConditionalBlocks =
				GetPrototypeJsonArray(ActionObject, TEXT("conditionalBlocks"));
			if (!ConditionalBlocks || ConditionalBlocks->Num() == 0)
			{
				Warnings.AddUnique(FString::Printf(
					TEXT("Prototype conditional skipped on '%s': no conditional blocks"),
					Node.IsValid() ? *Node->Id : TEXT("<unknown>")));
				return false;
			}

			const int32 StartIndex = OutSteps.Num();
			int32 PendingFalseJumpIndex = INDEX_NONE;
			TArray<int32> ExitJumpIndices;

			for (const TSharedPtr<FJsonValue>& BlockValue : *ConditionalBlocks)
			{
				if (!BlockValue.IsValid())
				{
					continue;
				}

				const TSharedPtr<FJsonObject> BlockObject = BlockValue->AsObject();
				if (!BlockObject.IsValid())
				{
					continue;
				}

				const int32 BlockStartIndex = OutSteps.Num();
				if (PendingFalseJumpIndex != INDEX_NONE)
				{
					OutSteps[PendingFalseJumpIndex].JumpTargetIndex = BlockStartIndex;
				}

				FUMGPrototypeNavigationStep ConditionStep;
				ConditionStep.Kind = EUMGPrototypeNavigationStepKind::ConditionJump;
				ConditionStep.ConditionJson = SerializePrototypeJsonValue(
					BlockObject->TryGetField(TEXT("condition")));
				const int32 ConditionStepIndex = OutSteps.Add(MoveTemp(ConditionStep));
				PendingFalseJumpIndex = ConditionStepIndex;

				if (const TArray<TSharedPtr<FJsonValue>>* NestedActions =
					GetPrototypeJsonArray(BlockObject, TEXT("actions")))
				{
					for (const TSharedPtr<FJsonValue>& NestedActionValue : *NestedActions)
					{
						if (!NestedActionValue.IsValid())
						{
							continue;
						}

						const TSharedPtr<FJsonObject> NestedActionObject =
							NestedActionValue->AsObject();
						if (!NestedActionObject.IsValid())
						{
							continue;
						}

						AppendPrototypeActionStepsFromJsonObject(
							Node,
							NestedActionObject,
							NodeToPageId,
							PageIdToIndex,
							OutSteps,
							Warnings);
					}
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

		if (AppendPrototypeActionStepFromTypedAction(
			Node,
			ParsedAction,
			NodeToPageId,
			PageIdToIndex,
			OutSteps,
			Warnings))
		{
			return true;
		}

		if (!ParsedAction.Type.IsEmpty())
		{
			Warnings.AddUnique(FString::Printf(
				TEXT("Prototype action '%s' on node '%s' not supported yet"),
				*ParsedAction.Type,
				Node.IsValid() ? *Node->Id : TEXT("<unknown>")));
		}

		return false;
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
		if (!TryParsePrototypeJsonObject(TriggerJson, TriggerObject))
		{
			return KeyCodes;
		}

		if (const TArray<TSharedPtr<FJsonValue>>* KeyCodeArray =
			GetPrototypeJsonArray(TriggerObject, TEXT("keyCodes")))
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

		const FString SingleKeyCode = GetPrototypeJsonString(TriggerObject, TEXT("keyCode"));
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

			TSharedPtr<FJsonObject> InteractionObject;
			if (TryParsePrototypeJsonObject(Interaction.RawJson, InteractionObject))
			{
				if (const TArray<TSharedPtr<FJsonValue>>* RawActions =
					GetPrototypeJsonArray(InteractionObject, TEXT("actions")))
				{
					for (const TSharedPtr<FJsonValue>& RawActionValue : *RawActions)
					{
						if (!RawActionValue.IsValid())
						{
							continue;
						}

						const TSharedPtr<FJsonObject> RawActionObject = RawActionValue->AsObject();
						if (!RawActionObject.IsValid())
						{
							continue;
						}

						AppendPrototypeActionStepsFromJsonObject(
							Node,
							RawActionObject,
							NodeToPageId,
							PageIdToIndex,
							Binding.Steps,
							Warnings);
					}
				}
			}
			else
			{
				for (const FDesignPrototypeAction& Action : Interaction.Actions)
				{
					AppendPrototypeActionStepFromTypedAction(
						Node,
						Action,
						NodeToPageId,
						PageIdToIndex,
						Binding.Steps,
						Warnings);
				}
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
