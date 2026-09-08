// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGComponentMappingGeneration.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "DesignJsonUtils.h"
#include "Dom/JsonObject.h"
#include "UMGSchemaSourceExtensions.h"
#include "UMGWidgetTypes.h"
#include "UObject/UnrealType.h"

namespace
{
	// Subtrees of mapped hit nodes are not expanded: all descendants get the "inside mapped subtree" marker;
	// design interactions attached to them are equally inert (ReportInertPrototypeBindings uses this for diagnostics).
	void MarkMappedSubtreeRecursive(const FUMGWidgetNode& Node, FDesign2UMGComponentMappingBuildState& BuildState)
	{
		for (const FUMGWidgetNode& Child : Node.Children)
		{
			BuildState.AddMappedSubtreeNodeId(Child.Id);
			MarkMappedSubtreeRecursive(Child, BuildState);
		}
	}

	void ApplyMappingRecursive(
		FUMGWidgetNode& Node,
		const FString& DocumentScope,
		const DesignComponentMapping::FComponentMappingTable& Table,
		FDesign2UMGComponentMappingBuildState& BuildState)
	{
		// Subtrees already swallowed by an outer mapped hit no longer participate in matching: a hit would create no
		// instance (the generation chain does not expand it), and emitting mapping_hit would only make users believe the inner component was wired to the project WBP (false reporting).
		if (!BuildState.IsInsideMappedSubtree(Node.Id) && Node.HasSourceData())
		{
			DesignComponentMapping::FDesignComponentIdentity Identity;
			if (Node.Source().bMappedComponentCandidate
				&& DesignComponentMapping::TryResolveFigmaComponentIdentity(Node.Source(), DocumentScope, Identity))
			{
				bool bMatchedAtSetLevel = false;
				const DesignComponentMapping::FComponentMappingEntry* Entry = Table.Match(Identity, bMatchedAtSetLevel);
				if (Entry)
				{
					// Hit: write the mapping Key into the IR sidecar; the generation stage attaches the project WBP instance based on it.
					Node.MutableSource().MappedComponentKey = Entry->Key;
					BuildState.AddMappedNodeId(Node.Id, bMatchedAtSetLevel);
					MarkMappedSubtreeRecursive(Node, BuildState);
					const FString& MatchedIdentity = bMatchedAtSetLevel
						? Identity.BuildSetKey()
						: Identity.BuildVariantKey();
					BuildState.AddDiagnostic(
						TEXT("mapping_hit"),
						FString::Printf(
							TEXT("Mapped component '%s' (identity %s) -> %s%s"),
							*Node.Name,
							*MatchedIdentity,
							*Entry->WidgetClass,
							bMatchedAtSetLevel ? TEXT(" (component-set level match)") : TEXT("")));
				}
			}
		}

		for (FUMGWidgetNode& Child : Node.Children)
		{
			ApplyMappingRecursive(Child, DocumentScope, Table, BuildState);
		}
	}

	// Lossless text rendering of NUMBER values: try increasing precision and take the first representation that
	// round-trips to the exact same double. The %g default (6 significant digits) silently rounds design values;
	// a fixed %.17g is lossless but stretches 9.99 into 17 digits of noise. The shortest round-trippable form loses no precision and keeps value text clean in diagnostics and the manifest.
	FString NumberToLosslessString(const double NumberValue)
	{
		for (int32 Precision = 1; Precision < 17; ++Precision)
		{
			const FString Candidate = FString::Printf(TEXT("%.*g"), Precision, NumberValue);
			double ParsedBack = 0.0;
			if (LexTryParseString(ParsedBack, *Candidate) && ParsedBack == NumberValue)
			{
				return Candidate;
			}
		}
		return FString::Printf(TEXT("%.17g"), NumberValue);
	}

	// Take the value field of the Source key from the componentProperties JSON ({name:{type,value}}).
	bool TryGetComponentPropertyValue(const FString& PropertiesJson, const FString& SourceKey, FString& OutValue)
	{
		TSharedPtr<FJsonObject> PropertiesObject;
		if (!DesignJsonUtils::TryParseObject(PropertiesJson, PropertiesObject))
		{
			return false;
		}

		const TSharedPtr<FJsonObject>* EntryObject = nullptr;
		if (!PropertiesObject->TryGetObjectField(SourceKey, EntryObject) || !EntryObject || !(*EntryObject).IsValid())
		{
			return false;
		}

		if ((*EntryObject)->TryGetStringField(TEXT("value"), OutValue))
		{
			return true;
		}
		double NumberValue = 0.0;
		if ((*EntryObject)->TryGetNumberField(TEXT("value"), NumberValue))
		{
			OutValue = NumberToLosslessString(NumberValue);
			return true;
		}
		bool BoolValue = false;
		if ((*EntryObject)->TryGetBoolField(TEXT("value"), BoolValue))
		{
			OutValue = BoolValue ? TEXT("true") : TEXT("false");
			return true;
		}
		return false;
	}

	// Variant values of component-set instances live in variantProperties (a flat {name:value-string} map), the same component-property source.
	bool TryGetVariantPropertyValue(const FString& VariantPropertiesJson, const FString& SourceKey, FString& OutValue)
	{
		TSharedPtr<FJsonObject> PropertiesObject;
		if (!DesignJsonUtils::TryParseObject(VariantPropertiesJson, PropertiesObject))
		{
			return false;
		}
		return PropertiesObject->TryGetStringField(SourceKey, OutValue);
	}

	// Subtree fallback for the component's main text: depth-first, the first TEXT child inside the component with non-empty text.
	// In typical Figma data the button label almost always sits on an inner TEXT node, not on the instance node itself.
	void FindFirstTextChildRecursive(const FUMGWidgetNode& Node, FString& OutText)
	{
		for (const FUMGWidgetNode& Child : Node.Children)
		{
			if (Child.Type == UMGWidgetTypes::TextBlock)
			{
				const FString ChildText = Child.Text.Text.Get(FString());
				if (!ChildText.IsEmpty())
				{
					OutText = ChildText;
					return;
				}
			}
			FindFirstTextChildRecursive(Child, OutText);
			if (!OutText.IsEmpty())
			{
				return;
			}
		}
	}

	// Resolve design values by declared source: ComponentProperty takes the componentProperties key (falling back to variantProperties),
	// ComponentText takes the component's main text (the node's own text, else the first inner TEXT child's text).
	bool TryResolveComponentInputValue(
		const FUMGWidgetNode& Node,
		const DesignComponentMapping::FComponentInputDeclaration& Input,
		FString& OutValue,
		FString& OutError)
	{
		if (Input.ValueKind == DesignComponentMapping::EComponentInputValueKind::ComponentText)
		{
			OutValue = Node.Text.Text.Get(FString());
			if (OutValue.IsEmpty())
			{
				FindFirstTextChildRecursive(Node, OutValue);
			}
			if (OutValue.IsEmpty())
			{
				OutError = TEXT("component has no text content (node text and internal TEXT nodes are empty)");
				return false;
			}
			return true;
		}

		FString ComponentPropertiesJson;
		FString VariantPropertiesJson;
		if (Node.HasSourceData())
		{
			ComponentPropertiesJson = Node.Source().ComponentPropertiesJson;
			VariantPropertiesJson = Node.Source().VariantPropertiesJson;
		}
		if (TryGetComponentPropertyValue(ComponentPropertiesJson, Input.Source, OutValue))
		{
			return true;
		}
		if (TryGetVariantPropertyValue(VariantPropertiesJson, Input.Source, OutValue))
		{
			return true;
		}
		OutError = FString::Printf(
			TEXT("component property '%s' not found in componentProperties/variantProperties"),
			*Input.Source);
		return false;
	}

	bool ParseBoolText(const FString& Value, bool& OutBool)
	{
		if (Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Value == TEXT("1"))
		{
			OutBool = true;
			return true;
		}
		if (Value.Equals(TEXT("false"), ESearchCase::IgnoreCase) || Value == TEXT("0"))
		{
			OutBool = false;
			return true;
		}
		return false;
	}

	// Reflection write: resolve the target property first, then match the declared type strictly; a type mismatch fails (diagnostic + skip that input).
	// Name resolution (generic rule): exact match on the declared name first; on miss, when the declared type is Bool,
	// retry once with UE's canonical bool property naming (b prefix), e.g. the spec example IsEnabled -> bIsEnabled.
	// The class property chain (PropertyLink) is rebuilt by Link with inherited properties included, so WBP and native class properties are both reachable by name.
	bool TryWriteInputValue(
		UUserWidget* Instance,
		const DesignComponentMapping::FComponentInputDeclaration& Input,
		const FString& Value,
		FString& OutError)
	{
		UClass* InstanceClass = Instance->GetClass();
		FProperty* Property = InstanceClass->FindPropertyByName(FName(*Input.Target));
		if (!Property && Input.Type == DesignComponentMapping::EComponentInputType::Bool)
		{
			const FString BoolTarget = FString(TEXT("b")) + Input.Target;
			Property = InstanceClass->FindPropertyByName(FName(*BoolTarget));
		}
		if (!Property)
		{
			OutError = FString::Printf(TEXT("target property '%s' not found on %s"), *Input.Target, *InstanceClass->GetName());
			return false;
		}

		using namespace DesignComponentMapping;
		switch (Input.Type)
		{
		case EComponentInputType::Text:
			if (FTextProperty* TextProperty = CastField<FTextProperty>(Property))
			{
				TextProperty->SetPropertyValue_InContainer(Instance, FText::FromString(Value));
				return true;
			}
			break;
		case EComponentInputType::Bool:
			if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
			{
				bool BoolValue = false;
				if (!ParseBoolText(Value, BoolValue))
				{
					OutError = FString::Printf(TEXT("value '%s' is not a bool"), *Value);
					return false;
				}
				BoolProperty->SetPropertyValue_InContainer(Instance, BoolValue);
				return true;
			}
			break;
		case EComponentInputType::Name:
			if (FNameProperty* NameProperty = CastField<FNameProperty>(Property))
			{
				NameProperty->SetPropertyValue_InContainer(Instance, FName(Value));
				return true;
			}
			break;
		case EComponentInputType::Float:
			if (FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(Property))
			{
				double DoubleValue = 0.0;
				if (!LexTryParseString(DoubleValue, *Value))
				{
					OutError = FString::Printf(TEXT("value '%s' is not a float"), *Value);
					return false;
				}
				DoubleProperty->SetPropertyValue_InContainer(Instance, DoubleValue);
				return true;
			}
			if (FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
			{
				double DoubleValue = 0.0;
				if (!LexTryParseString(DoubleValue, *Value))
				{
					OutError = FString::Printf(TEXT("value '%s' is not a float"), *Value);
					return false;
				}
				FloatProperty->SetPropertyValue_InContainer(Instance, static_cast<float>(DoubleValue));
				return true;
			}
			break;
		default:
			break;
		}

		OutError = FString::Printf(
			TEXT("target property '%s' (%s) does not match declared input type '%s'"),
			*Input.Target,
			*Property->GetClass()->GetName(),
			*GetComponentInputTypeName(Input.Type));
		return false;
	}
}

bool FDesign2UMGComponentMappingBuildState::IsMappedNode(const FUMGWidgetNode& Node) const
{
	return Node.HasSourceData() && !Node.Source().MappedComponentKey.IsEmpty();
}

void FDesign2UMGComponentMappingBuildState::RecordCreatedWidget(
	const FUMGWidgetNode& Node,
	const UWidget* CreatedWidget,
	const UWidget* ParentWidget)
{
	if (!CreatedWidget)
	{
		return;
	}

	// The root widget itself is not recorded as an instance (the manifest's sourceRoot describes it separately).
	if (IsRootWidget(CreatedWidget))
	{
		return;
	}

	// Ownership record: every widget created this run (including nested leaves) is registered by name;
	// the manifest's d2uOwned uses this as its data source; reimport uses it to decide delete/move boundaries.
	CreatedWidgetNames.Add(CreatedWidget->GetFName().ToString());

	// mapped instances were already registered in TryCreateMappedWidget (including inputs backfill); not recorded again here.
	if (IsMappedNode(Node))
	{
		return;
	}

	// Only the direct children of the page-host root widget are recorded as top-level generation relations; leaves are not recorded individually.
	// Wrapper nodes inserted by the converter (SizeBox/Button wraps) carry no source-data sidecar, so designNodeId falls back to the IR Id
	// (the IR Id is deterministically reproducible for wrappers too; reimport recovers them by designNodeId + widgetName).
	if (ParentWidget && IsRootWidget(ParentWidget))
	{
		FDesign2UMGGeneratedInstanceRecord Record;
		Record.DesignNodeId = Node.GetStableDesignNodeId();
		Record.WidgetName = CreatedWidget->GetFName().ToString();
		TopLevelGenerated.Add(MoveTemp(Record));
	}
}

namespace
{
	// Write inputs one by one per declaration: resolve design value -> reflection write; a single failure only skips with a diagnostic.
	// The import path and the reimport path share this; only the diagnostic/record destinations differ.
	void ApplyMappedComponentInputsInternal(
		UUserWidget* Instance,
		const FUMGWidgetNode& Node,
		const DesignComponentMapping::FComponentMappingEntry& Entry,
		FDesign2UMGMappedInstanceRecord* Record,
		TArray<FDesign2UMGMappingDiagnostic>& Diagnostics)
	{
		for (const DesignComponentMapping::FComponentInputDeclaration& Input : Entry.Inputs)
		{
			FDesign2UMGMappedInputRecord InputRecord;
			InputRecord.Source = Input.Source;
			InputRecord.Target = Input.Target;
			InputRecord.ValueKind = DesignComponentMapping::GetComponentInputValueKindName(Input.ValueKind);

			FString Value;
			FString Error;
			const bool bResolved = TryResolveComponentInputValue(Node, Input, Value, Error);
			const bool bWritten = bResolved && TryWriteInputValue(Instance, Input, Value, Error);
			if (Record)
			{
				Record->Inputs.Add(InputRecord);
			}

			FDesign2UMGMappingDiagnostic Diagnostic;
			if (bWritten)
			{
				// inputs are D2U-owned: write the latest design value directly - no comparison against the project-side current value, no baseline recording.
				Diagnostic.Kind = TEXT("mapping_input_applied");
				Diagnostic.Message = FString::Printf(
					TEXT("Input '%s' -> %s.%s (%s) overwritten with design value '%s'"),
					*Input.Source,
					*Instance->GetClass()->GetName(),
					*Input.Target,
					*DesignComponentMapping::GetComponentInputTypeName(Input.Type),
					*Value);
			}
			else
			{
				Diagnostic.Kind = TEXT("mapping_input_skipped");
				Diagnostic.Message = FString::Printf(
					TEXT("Input '%s' -> %s.%s skipped: %s; other inputs and the mapping remain active"),
					*Input.Source,
					*Instance->GetClass()->GetName(),
					*Input.Target,
					*Error);
			}
			Diagnostics.Add(MoveTemp(Diagnostic));
		}
	}
}

namespace Design2UMGComponentMappingGeneration
{
	void ApplyMappingToWidgetTreeIR(
		FUMGWidgetNode& RootNode,
		const FDesign2UMGComponentMappingConfig& Config,
		const FUMGWidgetData& WidgetData,
		FDesign2UMGComponentMappingBuildState& BuildState)
	{
		if (!Config.HasMappings())
		{
			return;
		}

		FString Provider;
		FString DocumentScope;
		FString DocumentScopeOrigin;
		if (WidgetData.HasSourceDocumentData())
		{
			const FUMGSourceDocumentData& SourceDocument = WidgetData.SourceDocument();
			Provider = SourceDocument.Provider;
			DocumentScope = SourceDocument.DocumentScope;
			DocumentScopeOrigin = SourceDocument.DocumentScopeOrigin;
		}

		if (!Provider.Equals(DesignComponentMapping::ProviderFigma, ESearchCase::IgnoreCase))
		{
			// Pencil / unknown sources stay out of the mapping main path.
			BuildState.AddDiagnostic(
				TEXT("mapping_skipped"),
				FString::Printf(
					TEXT("Component mapping config ignored for provider '%s'; only figma documents are matched in v1"),
					*Provider));
			return;
		}

		if (!DocumentScope.IsEmpty())
		{
			BuildState.AddDiagnostic(
				TEXT("mapping_scope"),
				FString::Printf(
					TEXT("Component mapping active: provider=figma documentScope=%s (source: %s)"),
					*DocumentScope,
					*DocumentScopeOrigin));
		}
		else
		{
			BuildState.AddDiagnostic(
				TEXT("mapping_scope"),
				TEXT("Component mapping could not resolve a document scope (no file key and no import source identifier); no node can match"));
			return;
		}

		ApplyMappingRecursive(RootNode, DocumentScope, Config.Table, BuildState);
	}

	UUserWidget* TryCreateMappedWidget(
		UWidgetTree* WidgetTree,
		const FUMGWidgetNode& Node,
		const FName& WidgetName,
		FDesign2UMGComponentMappingBuildState& BuildState)
	{
		const DesignComponentMapping::FComponentMappingEntry* Entry =
			BuildState.FindEntryForKey(Node.Source().MappedComponentKey);
		if (!Entry)
		{
			BuildState.AddDiagnostic(
				TEXT("mapping_class_unavailable"),
				FString::Printf(
					TEXT("Mapping entry for key '%s' disappeared; node '%s' falls back to normal conversion"),
					*Node.Source().MappedComponentKey,
					*Node.Name));
			return nullptr;
		}

		const FSoftClassPath WidgetClassPath(Entry->WidgetClass);
		UClass* WidgetClass = WidgetClassPath.TryLoadClass<UUserWidget>();
		if (!WidgetClass || !WidgetClass->IsChildOf(UUserWidget::StaticClass()))
		{
			BuildState.AddDiagnostic(
				TEXT("mapping_class_unavailable"),
				FString::Printf(
					TEXT("Mapped class '%s' failed to load for node '%s'; falling back to normal conversion"),
					*Entry->WidgetClass,
					*Node.Name));
			return nullptr;
		}

		// The project WBP class instance hangs directly in the WidgetTree (equivalent to dragging a WBP instance in
		// by hand); the outer layout (position/size/slot) is still handled by the generation chain; the component's internal subtree is not expanded.
		UUserWidget* MappedInstance = WidgetTree->ConstructWidget<UUserWidget>(WidgetClass, WidgetName);
		if (!MappedInstance)
		{
			BuildState.AddDiagnostic(
				TEXT("mapping_class_unavailable"),
				FString::Printf(
					TEXT("Failed to construct mapped widget instance of '%s' for node '%s'; falling back to normal conversion"),
					*Entry->WidgetClass,
					*Node.Name));
			return nullptr;
		}

		FDesign2UMGMappedInstanceRecord Record;
		Record.DesignNodeId = Node.GetStableDesignNodeId();
		Record.WidgetName = WidgetName.ToString();
		Record.MappedKey = Entry->Key;
		Record.MappedClass = Entry->WidgetClass;
		Record.bMatchedAtSetLevel = BuildState.IsMappedAtSetLevel(Node.Id);
		BuildState.RecordMappedInstance(Record);
		return MappedInstance;
	}

	void ApplyMappedComponentInputs(
		UUserWidget* Instance,
		const FUMGWidgetNode& Node,
		FDesign2UMGComponentMappingBuildState& BuildState)
	{
		const DesignComponentMapping::FComponentMappingEntry* Entry =
			BuildState.FindEntryForKey(Node.Source().MappedComponentKey);
		if (!Entry || Entry->Inputs.Num() == 0)
		{
			return;
		}

		FDesign2UMGMappedInstanceRecord* Record =
			BuildState.FindMappedInstanceByWidgetName(Instance->GetFName().ToString());

		TArray<FDesign2UMGMappingDiagnostic> Diagnostics;
		ApplyMappedComponentInputsInternal(Instance, Node, *Entry, Record, Diagnostics);
		for (FDesign2UMGMappingDiagnostic& Diagnostic : Diagnostics)
		{
			BuildState.AddDiagnostic(MoveTemp(Diagnostic.Kind), MoveTemp(Diagnostic.Message));
		}
	}

	void ApplyMappedComponentInputsToInstance(
		UUserWidget* Instance,
		const FUMGWidgetNode& Node,
		const DesignComponentMapping::FComponentMappingEntry& Entry,
		TArray<FDesign2UMGMappedInputRecord>* OutInputRecords,
		TArray<FDesign2UMGMappingDiagnostic>& OutDiagnostics)
	{
		FDesign2UMGMappedInstanceRecord ScratchRecord;
		FDesign2UMGMappedInstanceRecord* Record = OutInputRecords ? &ScratchRecord : nullptr;
		ApplyMappedComponentInputsInternal(Instance, Node, Entry, Record, OutDiagnostics);
		if (OutInputRecords)
		{
			*OutInputRecords = MoveTemp(ScratchRecord.Inputs);
		}
	}

	void ReportInertPrototypeBindings(
		const FUMGPrototypeNavigationData& NavigationData,
		FDesign2UMGComponentMappingBuildState& BuildState,
		TArray<FDesign2UMGMappingDiagnostic>* OutDiagnostics)
	{
		// The same diagnostic goes into BuildState (import report exit) and the optional OutDiagnostics (reimport report exit).
		const auto ReportBoth = [&BuildState, OutDiagnostics](const TCHAR* Kind, const FString& Message)
		{
			BuildState.AddDiagnostic(Kind, Message);
			if (OutDiagnostics)
			{
				FDesign2UMGMappingDiagnostic Diagnostic;
				Diagnostic.Kind = Kind;
				Diagnostic.Message = Message;
				OutDiagnostics->Add(MoveTemp(Diagnostic));
			}
		};

		for (const FUMGPrototypeNavigationBinding& Binding : NavigationData.ClickBindings)
		{
			// A trigger on the mapped instance body and one inside its subtree are two different locations; the messages say
			// so explicitly; neither generates a binding, and both need the "inert" diagnostic.
			if (BuildState.IsMappedNodeId(Binding.TriggerWidgetId)
				|| BuildState.IsMappedNodeId(Binding.TriggerWidgetName))
			{
				ReportBoth(
					TEXT("mapping_prototype_inert"),
					FString::Printf(
						TEXT("Design interaction (trigger '%s', type '%s') attached to mapped component is inert this phase: mapped WBP interactions are project-owned"),
						*Binding.TriggerWidgetName,
						*Binding.TriggerType));
			}
			else if (BuildState.IsInsideMappedSubtree(Binding.TriggerWidgetId)
				|| BuildState.IsInsideMappedSubtree(Binding.TriggerWidgetName))
			{
				ReportBoth(
					TEXT("mapping_prototype_inert"),
					FString::Printf(
						TEXT("Design interaction (trigger '%s', type '%s') inside a mapped component's unexpanded subtree is inert this phase: the subtree is replaced by the project-owned mapped WBP instance"),
						*Binding.TriggerWidgetName,
						*Binding.TriggerType));
			}
		}
	}
}
