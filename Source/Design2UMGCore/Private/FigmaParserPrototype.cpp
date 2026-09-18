// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "FigmaParserPrototype.h"
#include "DesignJsonUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace FigmaParserInternal
{

static void AppendUniqueFlowStartingPoint(TArray<FDesignFlowStartingPoint>& Points, const FDesignFlowStartingPoint& Candidate)
{
	if (Candidate.NodeId.IsEmpty() && Candidate.Name.IsEmpty())
		return;
	for (const FDesignFlowStartingPoint& E : Points)
	{
		if (E.NodeId == Candidate.NodeId && E.Name == Candidate.Name)
			return;
	}
	Points.Add(Candidate);
}

static void ParsePrototypeTransitionData(const TSharedPtr<FJsonObject>& Obj, FDesignPrototypeTransitionData& Out)
{
	if (!Obj.IsValid())
		return;

	const FString Type = DesignJsonUtils::GetString(Obj, TEXT("type"));
	if (!Type.IsEmpty())
		Out.Type = Type;

	const FString Direction = DesignJsonUtils::GetString(Obj, TEXT("direction"));
	if (!Direction.IsEmpty())
		Out.Direction = Direction;

	double Duration = 0.0;
	if (DesignJsonUtils::TryGetNumber(Obj, TEXT("duration"), Duration))
	{
		Out.Duration.Emplace(static_cast<float>(Duration));
	}

	const TSharedPtr<FJsonValue> EasingField = DesignJsonUtils::TryGetField(Obj, TEXT("easing"));
	if (EasingField.IsValid())
		Out.EasingJson = DesignJsonUtils::ToPrettyString(EasingField);

	const FString EasingType = DesignJsonUtils::GetString(DesignJsonUtils::GetObject(Obj, TEXT("easing")), TEXT("type"));
	if (!EasingType.IsEmpty())
		Out.EasingType = EasingType;

	bool bMatchLayers = false;
	if (Obj->TryGetBoolField(TEXT("matchLayers"), bMatchLayers))
	{
		Out.MatchLayers.Emplace(bMatchLayers);
	}
}

static void ParsePrototypeActions(const TSharedPtr<FJsonObject>& InteractionObj, TArray<FDesignPrototypeAction>& OutActions);

// Keyword vocabulary for terminal prototype actions. Kept verbatim from the
// former consumer-side matchers in Design2UMGPrototypeStepGraphAssembly; the
// match results must stay identical.

static bool IsPrototypeNavigateNavigation(const FString& Value)
{
	return Value.IsEmpty()
		|| Value.Equals(TEXT("NAVIGATE"), ESearchCase::IgnoreCase)
		|| Value.Equals(TEXT("SWAP"), ESearchCase::IgnoreCase);
}

static bool IsPrototypeOverlayKeyword(const FString& Value)
{
	return Value.Equals(TEXT("OVERLAY"), ESearchCase::IgnoreCase)
		|| Value.Equals(TEXT("OPEN_OVERLAY"), ESearchCase::IgnoreCase)
		|| Value.Equals(TEXT("SWAP_OVERLAY"), ESearchCase::IgnoreCase);
}

static bool IsPrototypeBackKeyword(const FString& Value)
{
	return Value.Equals(TEXT("BACK"), ESearchCase::IgnoreCase);
}

static bool IsPrototypeOpenLinkKeyword(const FString& Value)
{
	return Value.Equals(TEXT("URL"), ESearchCase::IgnoreCase)
		|| Value.Equals(TEXT("OPEN_URL"), ESearchCase::IgnoreCase)
		|| Value.Equals(TEXT("OPEN_LINK"), ESearchCase::IgnoreCase)
		|| Value.Equals(TEXT("LINK"), ESearchCase::IgnoreCase);
}

static bool LooksLikeExternalPrototypeLink(const FString& Value)
{
	return Value.Contains(TEXT("://"))
		|| Value.StartsWith(TEXT("mailto:"), ESearchCase::IgnoreCase)
		|| Value.StartsWith(TEXT("tel:"), ESearchCase::IgnoreCase)
		|| Value.StartsWith(TEXT("www."), ESearchCase::IgnoreCase);
}

// Collapses the raw Type/Navigation keyword pair (plus the external-link look
// of DestinationId) into one terminal intent, applying the same precedence the
// consumer's match chain used: Back, then OpenLink, then Overlay, then Navigate.
static EDesignPrototypeActionIntent ResolvePrototypeActionIntent(const FDesignPrototypeAction& Action)
{
	if (IsPrototypeBackKeyword(Action.Type) || IsPrototypeBackKeyword(Action.Navigation))
	{
		return EDesignPrototypeActionIntent::Back;
	}

	if (IsPrototypeOpenLinkKeyword(Action.Type)
		|| IsPrototypeOpenLinkKeyword(Action.Navigation)
		|| LooksLikeExternalPrototypeLink(Action.DestinationId))
	{
		return EDesignPrototypeActionIntent::OpenLink;
	}

	const bool bOverlayKeyword =
		IsPrototypeOverlayKeyword(Action.Navigation) || IsPrototypeOverlayKeyword(Action.Type);
	const bool bNodeLike =
		Action.Type.IsEmpty()
		|| Action.Type.Equals(TEXT("NODE"), ESearchCase::IgnoreCase)
		|| IsPrototypeOverlayKeyword(Action.Type);
	if (bOverlayKeyword && bNodeLike)
	{
		return EDesignPrototypeActionIntent::Overlay;
	}

	if (Action.Type.Equals(TEXT("NODE"), ESearchCase::IgnoreCase)
		&& IsPrototypeNavigateNavigation(Action.Navigation))
	{
		return EDesignPrototypeActionIntent::Navigate;
	}

	return EDesignPrototypeActionIntent::Unresolved;
}

static void ParsePrototypeConditionalBlocks(
	const TSharedPtr<FJsonObject>& AObj,
	TArray<FDesignPrototypeConditionalBlock>& OutBlocks)
{
	const TArray<TSharedPtr<FJsonValue>>* Blocks = DesignJsonUtils::GetArray(AObj, TEXT("conditionalBlocks"));
	if (!Blocks)
		return;

	for (const TSharedPtr<FJsonValue>& BV : *Blocks)
	{
		const TSharedPtr<FJsonObject> BObj = BV->AsObject();
		if (!BObj.IsValid())
			continue;

		FDesignPrototypeConditionalBlock Block;
		Block.ConditionJson = DesignJsonUtils::ToCondensedString(
			DesignJsonUtils::TryGetField(BObj, TEXT("condition")));
		ParsePrototypeActions(BObj, Block.Actions);
		OutBlocks.Add(MoveTemp(Block));
	}
}

static void ParsePrototypeActions(const TSharedPtr<FJsonObject>& InteractionObj, TArray<FDesignPrototypeAction>& OutActions)
{
	const TArray<TSharedPtr<FJsonValue>>* Actions = DesignJsonUtils::GetArray(InteractionObj, TEXT("actions"));
	if (!Actions)
		return;

	for (const TSharedPtr<FJsonValue>& AV : *Actions)
	{
		const TSharedPtr<FJsonObject> AObj = AV->AsObject();
		if (!AObj.IsValid())
			continue;

		FDesignPrototypeAction Action;

		const FString ActionType = DesignJsonUtils::GetString(AObj, TEXT("type"));
		if (!ActionType.IsEmpty())
			Action.Type = ActionType;

		FString DestId = DesignJsonUtils::GetString(AObj, TEXT("destinationId"));
		if (DestId.IsEmpty())
			DestId = DesignJsonUtils::GetString(AObj, TEXT("url"));
		if (DestId.IsEmpty())
			DestId = DesignJsonUtils::GetString(AObj, TEXT("uri"));
		if (!DestId.IsEmpty())
			Action.DestinationId = DestId;

		const FString Navigation = DesignJsonUtils::GetString(AObj, TEXT("navigation"));
		if (!Navigation.IsEmpty())
			Action.Navigation = Navigation;

		const FString TransitionNodeId = DesignJsonUtils::GetString(AObj, TEXT("transitionNodeID"));
		if (!TransitionNodeId.IsEmpty())
			Action.TransitionNodeId = TransitionNodeId;

		const TSharedPtr<FJsonValue> TransitionField = DesignJsonUtils::TryGetField(AObj, TEXT("transition"));
		if (TransitionField.IsValid())
			Action.TransitionJson = DesignJsonUtils::ToPrettyString(TransitionField);

		ParsePrototypeTransitionData(DesignJsonUtils::GetObject(AObj, TEXT("transition")), Action.Transition);

		const FString VariableId = DesignJsonUtils::GetString(AObj, TEXT("variableId"));
		if (!VariableId.IsEmpty())
			Action.VariableId = VariableId;

		// Variable payload is condensed once here so the step graph never
		// re-parses the raw action JSON; "value" is the legacy field name.
		TSharedPtr<FJsonValue> VariableValueField = DesignJsonUtils::TryGetField(AObj, TEXT("variableValue"));
		if (!VariableValueField.IsValid())
			VariableValueField = DesignJsonUtils::TryGetField(AObj, TEXT("value"));
		if (VariableValueField.IsValid())
			Action.VariableValueJson = DesignJsonUtils::ToCondensedString(VariableValueField);

		const FString VariableCollectionId = DesignJsonUtils::GetString(AObj, TEXT("variableCollectionId"));
		if (!VariableCollectionId.IsEmpty())
			Action.VariableCollectionId = VariableCollectionId;

		const FString VariableModeId = DesignJsonUtils::GetString(AObj, TEXT("variableModeId"));
		if (!VariableModeId.IsEmpty())
			Action.VariableModeId = VariableModeId;

		ParsePrototypeConditionalBlocks(AObj, Action.ConditionalBlocks);
		Action.Intent = ResolvePrototypeActionIntent(Action);
		OutActions.Add(MoveTemp(Action));
	}
}

static TArray<FString> ParsePrototypeTriggerKeyCodes(const TSharedPtr<FJsonObject>& TriggerObject)
{
	TArray<FString> KeyCodes;

	if (!TriggerObject.IsValid())
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

static void ParsePrototypeInteractions(const TSharedPtr<FJsonObject>& Obj, TArray<FDesignPrototypeInteraction>& OutInteractions)
{
	const TArray<TSharedPtr<FJsonValue>>* Interactions = DesignJsonUtils::GetArray(Obj, TEXT("interactions"));
	if (!Interactions)
		return;

	for (const TSharedPtr<FJsonValue>& IV : *Interactions)
	{
		const TSharedPtr<FJsonObject> IObj = IV->AsObject();
		if (!IObj.IsValid())
			continue;

		FDesignPrototypeInteraction Interaction;

		const TSharedPtr<FJsonValue> TriggerField = DesignJsonUtils::TryGetField(IObj, TEXT("trigger"));
		if (TriggerField.IsValid())
			Interaction.TriggerJson = DesignJsonUtils::ToPrettyString(TriggerField);

		const TSharedPtr<FJsonObject> TriggerObject = DesignJsonUtils::GetObject(IObj, TEXT("trigger"));
		Interaction.TriggerKeyCodes = ParsePrototypeTriggerKeyCodes(TriggerObject);
		const FString TriggerType = DesignJsonUtils::GetString(TriggerObject, TEXT("type"));
		if (!TriggerType.IsEmpty())
			Interaction.TriggerType = TriggerType;

		ParsePrototypeActions(IObj, Interaction.Actions);
		OutInteractions.Add(MoveTemp(Interaction));
	}
}

static void ParseFlowStartingPoints(const TSharedPtr<FJsonObject>& Obj, TArray<FDesignFlowStartingPoint>& OutPoints)
{
	const TArray<TSharedPtr<FJsonValue>>* Flows = DesignJsonUtils::GetArray(Obj, TEXT("flowStartingPoints"));
	if (!Flows)
		return;

	for (const TSharedPtr<FJsonValue>& FV : *Flows)
	{
		const TSharedPtr<FJsonObject> FObj = FV->AsObject();
		if (!FObj.IsValid())
			continue;

		FDesignFlowStartingPoint Flow;
		Flow.NodeId = DesignJsonUtils::GetString(FObj, TEXT("nodeId"));
		Flow.Name = DesignJsonUtils::GetString(FObj, TEXT("name"));
		AppendUniqueFlowStartingPoint(OutPoints, Flow);
	}
}

void ParsePrototypeNodeData(const TSharedPtr<FJsonObject>& NodeObj, FDesignPrototypeNodeData& OutData)
{
	const FString TransitionNodeId = DesignJsonUtils::GetString(NodeObj, TEXT("transitionNodeID"));
	if (!TransitionNodeId.IsEmpty())
		OutData.TransitionNodeId = TransitionNodeId;

	ParsePrototypeTransitionData(DesignJsonUtils::GetObject(NodeObj, TEXT("transition")), OutData.Transition);

	double TransitionDuration = 0.0;
	if (DesignJsonUtils::TryGetNumber(NodeObj, TEXT("transitionDuration"), TransitionDuration))
	{
		OutData.Transition.Duration.Emplace(static_cast<float>(TransitionDuration));
	}

	const TSharedPtr<FJsonValue> EasingField = DesignJsonUtils::TryGetField(NodeObj, TEXT("transitionEasing"));
	if (EasingField.IsValid())
		OutData.Transition.EasingJson = DesignJsonUtils::ToPrettyString(EasingField);

	const FString EasingType = DesignJsonUtils::GetString(DesignJsonUtils::GetObject(NodeObj, TEXT("transitionEasing")), TEXT("type"));
	if (!EasingType.IsEmpty())
		OutData.Transition.EasingType = EasingType;

	ParsePrototypeInteractions(NodeObj, OutData.Interactions);
	ParseFlowStartingPoints(NodeObj, OutData.FlowStartingPoints);
}

void MergePrototypeDocumentData(const TSharedPtr<FJsonObject>& SourceObj, FDesignSourceDocumentData& OutData)
{
	const FString StartNodeId = DesignJsonUtils::GetString(SourceObj, TEXT("prototypeStartNodeID"));
	if (!StartNodeId.IsEmpty())
		OutData.PrototypeStartNodeId = StartNodeId;

	const TSharedPtr<FJsonValue> DeviceField = DesignJsonUtils::TryGetField(SourceObj, TEXT("prototypeDevice"));
	if (DeviceField.IsValid())
		OutData.PrototypeDeviceJson = DesignJsonUtils::ToPrettyString(DeviceField);

	ParseFlowStartingPoints(SourceObj, OutData.FlowStartingPoints);
}

} // namespace FigmaParserInternal
