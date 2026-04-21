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
		Out.Duration = static_cast<float>(Duration);
		Out.bHasDuration = true;
	}

	const TSharedPtr<FJsonValue> EasingField = DesignJsonUtils::TryGetField(Obj, TEXT("easing"));
	if (EasingField.IsValid())
		Out.EasingJson = DesignJsonUtils::ToCompactString(EasingField);

	const FString EasingType = DesignJsonUtils::GetString(DesignJsonUtils::GetObject(Obj, TEXT("easing")), TEXT("type"));
	if (!EasingType.IsEmpty())
		Out.EasingType = EasingType;

	bool bMatchLayers = false;
	if (Obj->TryGetBoolField(TEXT("matchLayers"), bMatchLayers))
	{
		Out.bMatchLayers = bMatchLayers;
		Out.bHasMatchLayers = true;
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
		Action.RawJson = DesignJsonUtils::ToCompactString(AV);

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
			Action.TransitionJson = DesignJsonUtils::ToCompactString(TransitionField);

		ParsePrototypeTransitionData(DesignJsonUtils::GetObject(AObj, TEXT("transition")), Action.Transition);
		OutActions.Add(MoveTemp(Action));
	}
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
		Interaction.RawJson = DesignJsonUtils::ToCompactString(IV);

		const TSharedPtr<FJsonValue> TriggerField = DesignJsonUtils::TryGetField(IObj, TEXT("trigger"));
		if (TriggerField.IsValid())
			Interaction.TriggerJson = DesignJsonUtils::ToCompactString(TriggerField);

		const FString TriggerType = DesignJsonUtils::GetString(DesignJsonUtils::GetObject(IObj, TEXT("trigger")), TEXT("type"));
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
	const FString OverflowDir = DesignJsonUtils::GetString(NodeObj, TEXT("overflowDirection"));
	if (!OverflowDir.IsEmpty())
		OutData.OverflowDirection = OverflowDir;

	const FString TransitionNodeId = DesignJsonUtils::GetString(NodeObj, TEXT("transitionNodeID"));
	if (!TransitionNodeId.IsEmpty())
		OutData.TransitionNodeId = TransitionNodeId;

	ParsePrototypeTransitionData(DesignJsonUtils::GetObject(NodeObj, TEXT("transition")), OutData.Transition);

	double TransitionDuration = 0.0;
	if (DesignJsonUtils::TryGetNumber(NodeObj, TEXT("transitionDuration"), TransitionDuration))
	{
		OutData.Transition.Duration = static_cast<float>(TransitionDuration);
		OutData.Transition.bHasDuration = true;
	}

	const TSharedPtr<FJsonValue> EasingField = DesignJsonUtils::TryGetField(NodeObj, TEXT("transitionEasing"));
	if (EasingField.IsValid())
		OutData.Transition.EasingJson = DesignJsonUtils::ToCompactString(EasingField);

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
		OutData.PrototypeDeviceJson = DesignJsonUtils::ToCompactString(DeviceField);

	ParseFlowStartingPoints(SourceObj, OutData.FlowStartingPoints);
}

} // namespace FigmaParserInternal
