// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

// Component-mapping configuration, provider scope, reflected inputs and manifest contracts.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Design2UMGComponentMappingConfig.h"
#include "Design2UMGComponentMappingGeneration.h"
#include "Design2UMGGenerationManifest.h"
#include "UMGSchema.h"
#include "UMGSchemaSourceExtensions.h"

#include "Tests/Design2UMGComponentInputTestWidget.h"

#include "Blueprint/UserWidget.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"

namespace
{
	const TCHAR* FixtureTestWidgetClassPath = TEXT("/Script/Design2UMGEditor.Design2UMGMappedComponentTestWidget");

	FString MakeFixtureConfigIniPath(const TCHAR* FileName)
	{
		const FString Path = FPaths::ProjectSavedDir() / TEXT("Design2UMG") / TEXT("Tests") / FileName;
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		return Path;
	}

	bool WriteFixtureIni(const FString& IniPath, const FString& Content)
	{
		return FFileHelper::SaveStringToFile(Content, *IniPath);
	}

	// An IR node carrying the INSTANCE identity (a mapped candidate).
	FUMGWidgetNode MakeFixtureMappedCandidateNode(const FString& NodeId)
	{
		FUMGWidgetNode Node;
		Node.Id = NodeId;
		Node.Name = TEXT("FixtureComponent");
		Node.Type = TEXT("Button");
		Node.MutableSource().SourceNodeId = NodeId;
		Node.MutableSource().SourceNodeType = TEXT("INSTANCE");
		Node.MutableSource().ComponentId = TEXT("12:34");
		Node.MutableSource().ComponentSetKey = TEXT("56:78");
		Node.MutableSource().MainComponentKey = TEXT("fixture-main-key");
		Node.MutableSource().bMappedComponentCandidate = true;
		return Node;
	}

	// A directly-built single-entry mapping config (bypassing the ini; tests the generation layer).
	FDesign2UMGComponentMappingConfig MakeFixtureMappingConfig(
		const FString& Key,
		const TArray<DesignComponentMapping::FComponentInputDeclaration>& Inputs)
	{
		FDesign2UMGComponentMappingConfig Config;
		DesignComponentMapping::FComponentMappingEntry& Entry = Config.Table.Entries.AddDefaulted_GetRef();
		Entry.Key = Key;
		Entry.Provider = TEXT("figma");
		Entry.DocumentScope = TEXT("fixture-gen-scope");
		Entry.ComponentKey = TEXT("fixture-main-key");
		Entry.WidgetClass = FixtureTestWidgetClassPath;
		Entry.Inputs = Inputs;
		return Config;
	}

	DesignComponentMapping::FComponentInputDeclaration MakeFixtureInput(
		const TCHAR* Source,
		const TCHAR* Target,
		DesignComponentMapping::EComponentInputType Type,
		DesignComponentMapping::EComponentInputValueKind ValueKind =
			DesignComponentMapping::EComponentInputValueKind::ComponentProperty)
	{
		DesignComponentMapping::FComponentInputDeclaration Input;
		Input.Source = Source;
		Input.Target = Target;
		Input.Type = Type;
		Input.ValueKind = ValueKind;
		return Input;
	}

	// Apply inputs to the instance after the node carries the hit marker. The caller owns the Config:
	// BuildState stores only a Config pointer, whose lifetime must cover every use of BuildState.
	UDesign2UMGComponentInputTestWidget* ApplyFixtureInputs(
		FUMGWidgetNode& Node,
		const FDesign2UMGComponentMappingConfig& Config,
		FDesign2UMGComponentMappingBuildState& BuildState)
	{
		Node.MutableSource().MappedComponentKey = TEXT("figma|fixture-gen-scope|fixture-main-key");
		UDesign2UMGComponentInputTestWidget* Instance = NewObject<UDesign2UMGComponentInputTestWidget>(GetTransientPackage());
		Design2UMGComponentMappingGeneration::ApplyMappedComponentInputs(Instance, Node, BuildState);
		return Instance;
	}

	int32 CountDiagnosticsByKind(
		const FDesign2UMGComponentMappingBuildState& BuildState,
		const TCHAR* Kind,
		FString* OutFirstMessage = nullptr)
	{
		int32 Count = 0;
		for (const FDesign2UMGMappingDiagnostic& Diagnostic : BuildState.GetDiagnostics())
		{
			if (Diagnostic.Kind == Kind)
			{
				++Count;
				if (OutFirstMessage && Count == 1)
				{
					*OutFirstMessage = Diagnostic.Message;
				}
			}
		}
		return Count;
	}
}

// ---------------------------------------------------------------------------
// ini validation: when WidgetClass resolves to a non-UUserWidget subclass (an engine native widget class),
// the whole entry must be dropped with a readable diagnostic; other legal entries in the same ini must still be admitted.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesign2UMGMappingConfigRejectsNonUserWidgetClassTest,
	"Design2UMG.ComponentMapping.ConfigRejectsNonUserWidgetClass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGMappingConfigRejectsNonUserWidgetClassTest::RunTest(const FString&)
{
	const FString IniPath = MakeFixtureConfigIniPath(TEXT("FixtureConfigNonUserWidgetClass.ini"));
	const FString IniContent =
		TEXT("[/Design2UMG/ComponentMapping]\n")
		TEXT("+Mappings=(Key=\"figma|fixture-cfg-scope|bad-class\",WidgetClass=\"/Script/UMG.TextBlock\")\n")
		TEXT("+Mappings=(Key=\"figma|fixture-cfg-scope|good-class\",WidgetClass=\"/Script/Design2UMGEditor.Design2UMGMappedComponentTestWidget\")\n");
	TestTrue(TEXT("write temp mapping ini"), WriteFixtureIni(IniPath, IniContent));

	const FDesign2UMGComponentMappingConfig Config = Design2UMGComponentMappingConfig::Load(IniPath);
	IFileManager::Get().Delete(*IniPath, false, true);

	TestEqual(TEXT("only the valid entry survives"), Config.Table.Entries.Num(), 1);
	TestNotNull(TEXT("valid entry still loaded"),
		Config.Table.FindEntryByKey(TEXT("figma|fixture-cfg-scope|good-class")));
	TestNull(TEXT("non-UserWidget entry dropped"),
		Config.Table.FindEntryByKey(TEXT("figma|fixture-cfg-scope|bad-class")));

	int32 NonUserWidgetDiagnostics = 0;
	FString FirstMessage;
	for (const FDesign2UMGMappingDiagnostic& Diagnostic : Config.ValidationDiagnostics)
	{
		if (Diagnostic.Kind == TEXT("mapping_class_unavailable"))
		{
			++NonUserWidgetDiagnostics;
			if (NonUserWidgetDiagnostics == 1)
			{
				FirstMessage = Diagnostic.Message;
			}
		}
	}
	// Load the UClass, then judge IsChildOf(UUserWidget) explicitly: a loadable engine class that is not a UUserWidget
	// subclass must report "is not a UUserWidget subclass", distinct from "class missing/load failed".
	TestEqual(TEXT("non-UserWidget class diagnosed exactly once"), NonUserWidgetDiagnostics, 1);
	TestTrue(TEXT("diagnostic names the offending class"),
		FirstMessage.Contains(TEXT("/Script/UMG.TextBlock")));
	TestTrue(TEXT("diagnostic names the offending mapping key"),
		FirstMessage.Contains(TEXT("bad-class")));
	TestTrue(TEXT("diagnostic states the class is not a UUserWidget subclass"),
		FirstMessage.Contains(TEXT("is not a UUserWidget subclass")));
	return true;
}

// ---------------------------------------------------------------------------
// ini validation: when a struct line cannot be parsed by the UE config system (missing "(" prefix), emit a
// mapping_config_invalid diagnostic (containing "could not be parsed") and keep the entry out of the table.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesign2UMGMappingConfigRejectsUnparseableLineTest,
	"Design2UMG.ComponentMapping.ConfigRejectsUnparseableLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGMappingConfigRejectsUnparseableLineTest::RunTest(const FString&)
{
	const FString IniPath = MakeFixtureConfigIniPath(TEXT("FixtureConfigUnparseableLine.ini"));
	const FString IniContent =
		TEXT("[/Design2UMG/ComponentMapping]\n")
		TEXT("+Mappings=this-line-is-not-a-struct\n");
	TestTrue(TEXT("write temp mapping ini"), WriteFixtureIni(IniPath, IniContent));

	const FDesign2UMGComponentMappingConfig Config = Design2UMGComponentMappingConfig::Load(IniPath);
	IFileManager::Get().Delete(*IniPath, false, true);

	TestEqual(TEXT("unparseable line produces no entries"), Config.Table.Entries.Num(), 0);

	int32 UnparseableDiagnostics = 0;
	FString FirstMessage;
	for (const FDesign2UMGMappingDiagnostic& Diagnostic : Config.ValidationDiagnostics)
	{
		if (Diagnostic.Kind == TEXT("mapping_config_invalid")
			&& Diagnostic.Message.Contains(TEXT("could not be parsed")))
		{
			++UnparseableDiagnostics;
			if (UnparseableDiagnostics == 1)
			{
				FirstMessage = Diagnostic.Message;
			}
		}
	}
	TestEqual(TEXT("unparseable line diagnosed exactly once"), UnparseableDiagnostics, 1);
	TestTrue(TEXT("diagnostic echoes the offending line"),
		FirstMessage.Contains(TEXT("this-line-is-not-a-struct")));
	return true;
}

// ---------------------------------------------------------------------------
// Main-path provider gate: a Pencil (provider "pencil") document stays out of the v1 main path even with a
// perfectly matching Key configured - it emits a mapping_skipped diagnostic and the node does not hit.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesign2UMGMappingPencilProviderSkippedTest,
	"Design2UMG.ComponentMapping.PencilProviderSkipped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGMappingPencilProviderSkippedTest::RunTest(const FString&)
{
	FUMGWidgetData Data;
	Data.MutableSourceDocument().Provider = TEXT("pencil");
	Data.MutableSourceDocument().DocumentScope = TEXT("fixture-gen-scope");
	Data.RootWidget = MakeFixtureMappedCandidateNode(TEXT("pen_inst_1"));

	// Entries built directly (bypassing the ini provider validation) prove the gate also holds on the generation side:
	// even a pencil entry matching the node identity character-for-character must not hit.
	FDesign2UMGComponentMappingConfig Config;
	DesignComponentMapping::FComponentMappingEntry& Entry = Config.Table.Entries.AddDefaulted_GetRef();
	Entry.Key = TEXT("pencil|fixture-gen-scope|fixture-main-key");
	Entry.Provider = TEXT("pencil");
	Entry.DocumentScope = TEXT("fixture-gen-scope");
	Entry.ComponentKey = TEXT("fixture-main-key");
	Entry.WidgetClass = FixtureTestWidgetClassPath;

	FDesign2UMGComponentMappingBuildState BuildState(Config);
	Design2UMGComponentMappingGeneration::ApplyMappingToWidgetTreeIR(
		Data.RootWidget, Config, Data, BuildState);

	int32 SkippedDiagnostics = 0;
	FString FirstMessage;
	for (const FDesign2UMGMappingDiagnostic& Diagnostic : BuildState.GetDiagnostics())
	{
		if (Diagnostic.Kind == TEXT("mapping_skipped"))
		{
			++SkippedDiagnostics;
			if (SkippedDiagnostics == 1)
			{
				FirstMessage = Diagnostic.Message;
			}
		}
	}
	TestEqual(TEXT("pen provider skipped with one diagnostic"), SkippedDiagnostics, 1);
	TestTrue(TEXT("skip diagnostic names the provider"), FirstMessage.Contains(TEXT("provider 'pencil'")));
	TestTrue(TEXT("skip diagnostic states only figma is matched"),
		FirstMessage.Contains(TEXT("only figma documents are matched")));
	TestEqual(TEXT("pen provider emits no hit diagnostics"),
		CountDiagnosticsByKind(BuildState, TEXT("mapping_hit")), 0);
	TestTrue(TEXT("pen document node stays unmapped"),
		Data.RootWidget.Source().MappedComponentKey.IsEmpty());
	return true;
}

// ---------------------------------------------------------------------------
// Main-path scope gate: a figma document whose DocumentScope failed to resolve (empty) emits the
// "scope unresolvable, no node can possibly hit" diagnostic, and no matching happens.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesign2UMGMappingUnresolvedDocumentScopeTest,
	"Design2UMG.ComponentMapping.UnresolvedDocumentScope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGMappingUnresolvedDocumentScopeTest::RunTest(const FString&)
{
	FUMGWidgetData Data;
	Data.MutableSourceDocument().Provider = TEXT("figma");
	Data.MutableSourceDocument().DocumentScope = TEXT("");
	Data.MutableSourceDocument().DocumentScopeOrigin = TEXT("unresolved");
	Data.RootWidget = MakeFixtureMappedCandidateNode(TEXT("figma_inst_1"));

	FDesign2UMGComponentMappingConfig Config = MakeFixtureMappingConfig(
		TEXT("figma|fixture-gen-scope|fixture-main-key"), {});

	FDesign2UMGComponentMappingBuildState BuildState(Config);
	Design2UMGComponentMappingGeneration::ApplyMappingToWidgetTreeIR(
		Data.RootWidget, Config, Data, BuildState);

	int32 ScopeDiagnostics = 0;
	FString FirstMessage;
	for (const FDesign2UMGMappingDiagnostic& Diagnostic : BuildState.GetDiagnostics())
	{
		if (Diagnostic.Kind == TEXT("mapping_scope")
			&& Diagnostic.Message.Contains(TEXT("could not resolve a document scope")))
		{
			++ScopeDiagnostics;
			if (ScopeDiagnostics == 1)
			{
				FirstMessage = Diagnostic.Message;
			}
		}
	}
	TestEqual(TEXT("unresolved scope diagnosed exactly once"), ScopeDiagnostics, 1);
	TestTrue(TEXT("scope diagnostic states no node can match"),
		FirstMessage.Contains(TEXT("no node can match")));
	TestEqual(TEXT("unresolved scope emits no hit diagnostics"),
		CountDiagnosticsByKind(BuildState, TEXT("mapping_hit")), 0);
	TestTrue(TEXT("node stays unmapped on unresolved scope"),
		Data.RootWidget.Source().MappedComponentKey.IsEmpty());
	return true;
}

// ---------------------------------------------------------------------------
// inputs source ComponentText: the component's main text is written into the target property;
// when neither the node itself nor any inner TEXT has text, the input is skipped with a readable diagnostic and the mapping itself stays in effect.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesign2UMGMappingComponentTextInputValueTest,
	"Design2UMG.ComponentMapping.ComponentTextInputValue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGMappingComponentTextInputValueTest::RunTest(const FString&)
{
	const TArray<DesignComponentMapping::FComponentInputDeclaration> Inputs = {
		MakeFixtureInput(TEXT("label"), TEXT("TextValue"),
			DesignComponentMapping::EComponentInputType::Text,
			DesignComponentMapping::EComponentInputValueKind::ComponentText),
	};

	// Has direct text: write succeeds + the overwrite diagnostic carries the design value.
	FUMGWidgetNode NodeWithText = MakeFixtureMappedCandidateNode(TEXT("text_inst_1"));
	NodeWithText.Text.Text = FString(TEXT("Checkout"));
	FDesign2UMGComponentMappingConfig ConfigWithText =
		MakeFixtureMappingConfig(TEXT("figma|fixture-gen-scope|fixture-main-key"), Inputs);
	FDesign2UMGComponentMappingBuildState StateWithText(ConfigWithText);
	UDesign2UMGComponentInputTestWidget* InstanceWithText = ApplyFixtureInputs(NodeWithText, ConfigWithText, StateWithText);
	TestEqual(TEXT("component text written to declared target"),
		InstanceWithText->TextValue.ToString(), TEXT("Checkout"));
	FString AppliedMessage;
	TestEqual(TEXT("applied diagnostic emitted once"),
		CountDiagnosticsByKind(StateWithText, TEXT("mapping_input_applied"), &AppliedMessage), 1);
	TestTrue(TEXT("applied diagnostic reports the design value"),
		AppliedMessage.Contains(TEXT("overwritten with design value 'Checkout'")));
	TestEqual(TEXT("no skip diagnostic on success"),
		CountDiagnosticsByKind(StateWithText, TEXT("mapping_input_skipped")), 0);

	TestFalse(TEXT("Applied input is not degraded"), StateWithText.HasDegradation());

	// No direct text: input skipped + diagnostic; the default value is not written.
	FUMGWidgetNode NodeWithoutText = MakeFixtureMappedCandidateNode(TEXT("text_inst_2"));
	FDesign2UMGComponentMappingConfig ConfigWithoutText =
		MakeFixtureMappingConfig(TEXT("figma|fixture-gen-scope|fixture-main-key"), Inputs);
	FDesign2UMGComponentMappingBuildState StateWithoutText(ConfigWithoutText);
	UDesign2UMGComponentInputTestWidget* InstanceWithoutText = ApplyFixtureInputs(NodeWithoutText, ConfigWithoutText, StateWithoutText);
	TestTrue(TEXT("target untouched when component has no text"),
		InstanceWithoutText->TextValue.IsEmpty());
	FString SkippedMessage;
	TestEqual(TEXT("missing text skipped with diagnostic"),
		CountDiagnosticsByKind(StateWithoutText, TEXT("mapping_input_skipped"), &SkippedMessage), 1);
	TestTrue(TEXT("skip diagnostic states the reason"),
		SkippedMessage.Contains(TEXT("component has no text content")));
	TestTrue(TEXT("skip diagnostic says mapping remains active"),
		SkippedMessage.Contains(TEXT("other inputs and the mapping remain active")));
	return true;
}

// ---------------------------------------------------------------------------
// inputs source ComponentProperty: the componentProperties key wins;
// on absence, fall back to variantProperties; when neither exists, skip with a diagnostic.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesign2UMGMappingComponentPropertyFallbackTest,
	"Design2UMG.ComponentMapping.ComponentPropertyFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGMappingComponentPropertyFallbackTest::RunTest(const FString&)
{
	const TArray<DesignComponentMapping::FComponentInputDeclaration> Inputs = {
		MakeFixtureInput(TEXT("variant"), TEXT("NameValue"), DesignComponentMapping::EComponentInputType::Name),
	};

	// Both places have the same-named key: the componentProperties value wins.
	FUMGWidgetNode BothNode = MakeFixtureMappedCandidateNode(TEXT("prop_inst_1"));
	BothNode.MutableSource().ComponentPropertiesJson = TEXT("{\"variant\":{\"type\":\"VARIANT\",\"value\":\"Secondary\"}}");
	BothNode.MutableSource().VariantPropertiesJson = TEXT("{\"variant\":\"Primary\"}");
	FDesign2UMGComponentMappingConfig BothConfig =
		MakeFixtureMappingConfig(TEXT("figma|fixture-gen-scope|fixture-main-key"), Inputs);
	FDesign2UMGComponentMappingBuildState BothState(BothConfig);
	UDesign2UMGComponentInputTestWidget* BothInstance = ApplyFixtureInputs(BothNode, BothConfig, BothState);
	TestEqual(TEXT("componentProperties value wins over variantProperties"),
		BothInstance->NameValue, FName(TEXT("Secondary")));
	TestEqual(TEXT("componentProperties hit applies cleanly"),
		CountDiagnosticsByKind(BothState, TEXT("mapping_input_applied")), 1);

	// Only in variantProperties: the fallback takes effect.
	FUMGWidgetNode VariantOnlyNode = MakeFixtureMappedCandidateNode(TEXT("prop_inst_2"));
	VariantOnlyNode.MutableSource().VariantPropertiesJson = TEXT("{\"variant\":\"Primary\"}");
	FDesign2UMGComponentMappingConfig VariantOnlyConfig =
		MakeFixtureMappingConfig(TEXT("figma|fixture-gen-scope|fixture-main-key"), Inputs);
	FDesign2UMGComponentMappingBuildState VariantOnlyState(VariantOnlyConfig);
	UDesign2UMGComponentInputTestWidget* VariantOnlyInstance = ApplyFixtureInputs(VariantOnlyNode, VariantOnlyConfig, VariantOnlyState);
	TestEqual(TEXT("variantProperties fallback supplies the value"),
		VariantOnlyInstance->NameValue, FName(TEXT("Primary")));
	FString VariantAppliedMessage;
	TestEqual(TEXT("fallback apply diagnosed"),
		CountDiagnosticsByKind(VariantOnlyState, TEXT("mapping_input_applied"), &VariantAppliedMessage), 1);
	TestTrue(TEXT("fallback apply reports the design value"),
		VariantAppliedMessage.Contains(TEXT("'Primary'")));

	// Neither place has it: skipped + diagnostic.
	FUMGWidgetNode MissingNode = MakeFixtureMappedCandidateNode(TEXT("prop_inst_3"));
	MissingNode.MutableSource().ComponentPropertiesJson = TEXT("{\"other\":{\"type\":\"TEXT\",\"value\":\"x\"}}");
	MissingNode.MutableSource().VariantPropertiesJson = TEXT("{\"other\":\"x\"}");
	FDesign2UMGComponentMappingConfig MissingConfig =
		MakeFixtureMappingConfig(TEXT("figma|fixture-gen-scope|fixture-main-key"), Inputs);
	FDesign2UMGComponentMappingBuildState MissingState(MissingConfig);
	UDesign2UMGComponentInputTestWidget* MissingInstance = ApplyFixtureInputs(MissingNode, MissingConfig, MissingState);
	TestEqual(TEXT("target untouched when key is absent"), MissingInstance->NameValue, FName());
	FString MissingMessage;
	TestEqual(TEXT("absent property key skipped with diagnostic"),
		CountDiagnosticsByKind(MissingState, TEXT("mapping_input_skipped"), &MissingMessage), 1);
	TestTrue(TEXT("skip diagnostic names the missing key"),
		MissingMessage.Contains(TEXT("component property 'variant' not found in componentProperties/variantProperties")));
	return true;
}

// ---------------------------------------------------------------------------
// inputs reflection write: when the declared type mismatches the target property type, or the design value
// cannot be parsed as the declared type, skip that input with a precise diagnostic; a Float declaration writing a single-precision float property succeeds.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesign2UMGMappingInputTypeMismatchAndInvalidValuesTest,
	"Design2UMG.ComponentMapping.InputTypeMismatchAndInvalidValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGMappingInputTypeMismatchAndInvalidValuesTest::RunTest(const FString&)
{
	const TArray<DesignComponentMapping::FComponentInputDeclaration> Inputs = {
		// Bool declared but the target is an FText property: type mismatch.
		MakeFixtureInput(TEXT("label"), TEXT("TextValue"), DesignComponentMapping::EComponentInputType::Bool),
		// Bool declared but the design value is not a bool.
		MakeFixtureInput(TEXT("enabled"), TEXT("BoolValue"), DesignComponentMapping::EComponentInputType::Bool),
		// Float declared but the design value is not numeric.
		MakeFixtureInput(TEXT("value"), TEXT("DoubleValue"), DesignComponentMapping::EComponentInputType::Float),
		// Float declared writing a single-precision property: must succeed.
		MakeFixtureInput(TEXT("ratio"), TEXT("FloatValue"), DesignComponentMapping::EComponentInputType::Float),
	};

	FUMGWidgetNode Node = MakeFixtureMappedCandidateNode(TEXT("write_inst_1"));
	Node.MutableSource().ComponentPropertiesJson =
		TEXT("{\"label\":{\"type\":\"TEXT\",\"value\":\"Checkout\"},")
		TEXT("\"enabled\":{\"type\":\"BOOLEAN\",\"value\":\"maybe\"},")
		TEXT("\"value\":{\"type\":\"TEXT\",\"value\":\"abc\"},")
		TEXT("\"ratio\":{\"type\":\"NUMBER\",\"value\":2.5}}");

	FDesign2UMGComponentMappingConfig Config =
		MakeFixtureMappingConfig(TEXT("figma|fixture-gen-scope|fixture-main-key"), Inputs);
	FDesign2UMGComponentMappingBuildState BuildState(Config);
	UDesign2UMGComponentInputTestWidget* Instance = ApplyFixtureInputs(Node, Config, BuildState);

	TestTrue(TEXT("Skipped inputs mark generation as degraded"), BuildState.HasDegradation());

	// All three failed inputs keep their default property values.
	TestTrue(TEXT("text target untouched on type mismatch"), Instance->TextValue.IsEmpty());
	TestTrue(TEXT("bool target keeps default on unparseable value"), Instance->BoolValue);
	TestEqual(TEXT("double target keeps default on non-numeric value"), Instance->DoubleValue, 0.0);
	// The single-precision float write succeeds.
	TestEqual(TEXT("float target written through FFloatProperty branch"),
		Instance->FloatValue, 2.5f);

	// Check the diagnostic message fragments one by one.
	int32 TypeMismatch = 0;
	int32 NotABool = 0;
	int32 NotAFloat = 0;
	int32 Applied = 0;
	for (const FDesign2UMGMappingDiagnostic& Diagnostic : BuildState.GetDiagnostics())
	{
		if (Diagnostic.Kind == TEXT("mapping_input_skipped"))
		{
			TypeMismatch += Diagnostic.Message.Contains(TEXT("does not match declared input type 'Bool'")) ? 1 : 0;
			NotABool += Diagnostic.Message.Contains(TEXT("value 'maybe' is not a bool")) ? 1 : 0;
			NotAFloat += Diagnostic.Message.Contains(TEXT("value 'abc' is not a float")) ? 1 : 0;
		}
		else if (Diagnostic.Kind == TEXT("mapping_input_applied"))
		{
			Applied += Diagnostic.Message.Contains(TEXT("FloatValue")) ? 1 : 0;
		}
	}
	TestEqual(TEXT("type mismatch diagnosed once"), TypeMismatch, 1);
	TestEqual(TEXT("non-bool value diagnosed once"), NotABool, 1);
	TestEqual(TEXT("non-float value diagnosed once"), NotAFloat, 1);
	TestEqual(TEXT("float write into single-precision property diagnosed once"), Applied, 1);
	return true;
}

// ---------------------------------------------------------------------------
// manifest serialization: inputs store declarations only (source/target/valueKind), never design values
// (property values stay out of the manifest; reimport re-converts for fresh values);
// matchedAtSetLevel is written only when true; generated records carry no mappedKey;
// d2uOwned is whatever the payload explicitly gave; no .tmp temp file remains after writing.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesign2UMGMappingManifestFieldSerializationTest,
	"Design2UMG.ComponentMapping.ManifestFieldSerialization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGMappingManifestFieldSerializationTest::RunTest(const FString&)
{
	FDesign2UMGGenerationManifestPayload Payload;
	Payload.Provider = TEXT("figma");
	Payload.DocumentScope = TEXT("fixture-manifest-scope");
	Payload.SourceRootNodeId = TEXT("fixture-root");
	Payload.SourceRootName = TEXT("FixtureRootPage");

	FDesign2UMGMappedInstanceRecord SetLevelRecord;
	SetLevelRecord.DesignNodeId = TEXT("fixture-mapped-1");
	SetLevelRecord.WidgetName = TEXT("WBP_Fixture_SetLevel");
	SetLevelRecord.MappedKey = TEXT("figma|fixture-manifest-scope|56:78");
	SetLevelRecord.MappedClass = FixtureTestWidgetClassPath;
	SetLevelRecord.bMatchedAtSetLevel = true;
	FDesign2UMGMappedInputRecord AppliedInput;
	AppliedInput.Source = TEXT("label");
	AppliedInput.Target = TEXT("TextValue");
	AppliedInput.ValueKind = TEXT("ComponentText");
	FDesign2UMGMappedInputRecord SkippedInput;
	SkippedInput.Source = TEXT("missing");
	SkippedInput.Target = TEXT("Nowhere");
	SkippedInput.ValueKind = TEXT("ComponentProperty");
	SetLevelRecord.Inputs = { AppliedInput, SkippedInput };
	Payload.MappedInstances.Add(SetLevelRecord);

	FDesign2UMGMappedInstanceRecord VariantRecord;
	VariantRecord.DesignNodeId = TEXT("fixture-mapped-2");
	VariantRecord.WidgetName = TEXT("WBP_Fixture_Variant");
	VariantRecord.MappedKey = TEXT("figma|fixture-manifest-scope|fixture-main-key");
	VariantRecord.MappedClass = FixtureTestWidgetClassPath;
	VariantRecord.bMatchedAtSetLevel = false;
	Payload.MappedInstances.Add(VariantRecord);

	FDesign2UMGGeneratedInstanceRecord GeneratedRecord;
	GeneratedRecord.DesignNodeId = TEXT("fixture-generated-1");
	GeneratedRecord.WidgetName = TEXT("Card_0");
	Payload.TopLevelGenerated.Add(GeneratedRecord);

	Payload.D2UOwnedNames = { TEXT("WBP_Fixture_SetLevel"), TEXT("WBP_Fixture_Variant"), TEXT("Card_0"), TEXT("Card_0_Caption") };

	const FString PackageName = TEXT("/Game/Design2UMG/_TestOutput/FixtureManifestFields");
	const FString SidecarPath = Design2UMGGenerationManifest::BuildSidecarPathForAsset(PackageName);
	IFileManager::Get().Delete(*SidecarPath, false, true);
	TestTrue(TEXT("manifest sidecar written"),
		Design2UMGGenerationManifest::WriteSidecarForAsset(PackageName, Payload) == EDesign2UMGFileState::Saved);

	TSharedPtr<FJsonObject> Manifest;
	FString Json;
	{
		TestTrue(TEXT("sidecar file readable"), FFileHelper::LoadFileToString(Json, *SidecarPath));
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		TestTrue(TEXT("sidecar is valid JSON"), FJsonSerializer::Deserialize(Reader, Manifest) && Manifest.IsValid());
	}
	TestFalse(TEXT("no temp file left behind"), FPaths::FileExists(SidecarPath + TEXT(".tmp")));

	if (!Manifest.IsValid())
	{
		IFileManager::Get().Delete(*SidecarPath, false, true);
		return false;
	}

	TestEqual(TEXT("schema version"), Manifest->GetIntegerField(TEXT("schemaVersion")), 1);
	TestEqual(TEXT("provider"), Manifest->GetStringField(TEXT("provider")), TEXT("figma"));
	TestEqual(TEXT("document scope"), Manifest->GetStringField(TEXT("documentScope")), TEXT("fixture-manifest-scope"));
	TestEqual(TEXT("source root node id"),
		Manifest->GetObjectField(TEXT("sourceRoot"))->GetStringField(TEXT("nodeId")), TEXT("fixture-root"));
	TestEqual(TEXT("source root name"),
		Manifest->GetObjectField(TEXT("sourceRoot"))->GetStringField(TEXT("name")), TEXT("FixtureRootPage"));

	const TArray<TSharedPtr<FJsonValue>>* Instances = nullptr;
	TestTrue(TEXT("instances array present"), Manifest->TryGetArrayField(TEXT("instances"), Instances) && Instances);
	if (!Instances)
	{
		IFileManager::Get().Delete(*SidecarPath, false, true);
		return false;
	}
	TestEqual(TEXT("instance order is mapped entries then generated"), Instances->Num(), 3);

	// Record 1: set-level hit + applied/skipped inputs.
	const TSharedPtr<FJsonObject> Record1 = (*Instances)[0]->AsObject();
	TestEqual(TEXT("first record kind"), Record1->GetStringField(TEXT("kind")), TEXT("mapped"));
	TestEqual(TEXT("first record widget name"), Record1->GetStringField(TEXT("widgetName")), TEXT("WBP_Fixture_SetLevel"));
	TestTrue(TEXT("set-level match flag written only when true"),
		Record1->HasField(TEXT("matchedAtSetLevel")) && Record1->GetBoolField(TEXT("matchedAtSetLevel")));
	const TArray<TSharedPtr<FJsonValue>>* Record1Inputs = nullptr;
	TestTrue(TEXT("mapped record carries its inputs"),
		Record1->TryGetArrayField(TEXT("inputs"), Record1Inputs) && Record1Inputs && Record1Inputs->Num() == 2);
	if (Record1Inputs && Record1Inputs->Num() == 2)
	{
		const TSharedPtr<FJsonObject> Input1 = (*Record1Inputs)[0]->AsObject();
		TestEqual(TEXT("applied input source"), Input1->GetStringField(TEXT("source")), TEXT("label"));
		TestEqual(TEXT("applied input value kind"), Input1->GetStringField(TEXT("valueKind")), TEXT("ComponentText"));
		TestTrue(TEXT("inputs never serialize design values"), !Input1->HasField(TEXT("value")));
		const TSharedPtr<FJsonObject> Input2 = (*Record1Inputs)[1]->AsObject();
		TestTrue(TEXT("skipped input omits the value field"), !Input2->HasField(TEXT("value")));
		TestEqual(TEXT("skipped input still records source"), Input2->GetStringField(TEXT("source")), TEXT("missing"));
	}

	// Record 2: variant-level hit; the matchedAtSetLevel field is omitted.
	const TSharedPtr<FJsonObject> Record2 = (*Instances)[1]->AsObject();
	TestEqual(TEXT("second record kind"), Record2->GetStringField(TEXT("kind")), TEXT("mapped"));
	TestFalse(TEXT("variant-level record omits matchedAtSetLevel"),
		Record2->HasField(TEXT("matchedAtSetLevel")));

	// Record 3: generated, no mappedKey/mappedClass.
	const TSharedPtr<FJsonObject> Record3 = (*Instances)[2]->AsObject();
	TestEqual(TEXT("third record kind"), Record3->GetStringField(TEXT("kind")), TEXT("generated"));
	TestEqual(TEXT("generated record design node id"),
		Record3->GetStringField(TEXT("designNodeId")), TEXT("fixture-generated-1"));
	TestFalse(TEXT("generated record has no mapped key"), Record3->HasField(TEXT("mappedKey")));

	// d2uOwned matches the names the payload explicitly gave.
	const TArray<TSharedPtr<FJsonValue>>* D2UOwned = nullptr;
	TestTrue(TEXT("d2uOwned array present"), Manifest->TryGetArrayField(TEXT("d2uOwned"), D2UOwned) && D2UOwned);
	if (D2UOwned && D2UOwned->Num() == 4)
	{
		TSet<FString> OwnedNames;
		for (const TSharedPtr<FJsonValue>& OwnedValue : *D2UOwned)
		{
			OwnedNames.Add(OwnedValue->AsString());
		}
		TestTrue(TEXT("d2uOwned carries the payload names"),
			OwnedNames.Includes(TSet<FString>({ TEXT("WBP_Fixture_SetLevel"), TEXT("WBP_Fixture_Variant"), TEXT("Card_0"), TEXT("Card_0_Caption") })));
	}
	else
	{
		AddError(FString::Printf(TEXT("d2uOwned expected 4 entries, got %d"),
			D2UOwned ? D2UOwned->Num() : -1));
	}

	IFileManager::Get().Delete(*SidecarPath, false, true);
	return true;
}

// ---------------------------------------------------------------------------
// Prototype boundary contrast: one interaction each on a mapped and an ordinary node in the same
// navigation data; the inert diagnostic is emitted only for the mapped identity (with trigger name and
// the project-owned note); the ordinary node's interaction must not produce an inert diagnostic (no false accusation).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesign2UMGMappingPrototypeInertNoFalsePositiveTest,
	"Design2UMG.ComponentMapping.PrototypeInertNoFalsePositive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGMappingPrototypeInertNoFalsePositiveTest::RunTest(const FString&)
{
	FDesign2UMGComponentMappingConfig Config = MakeFixtureMappingConfig(TEXT("figma|fixture-gen-scope|fixture-main-key"), {});
	FDesign2UMGComponentMappingBuildState BuildState(Config);
	BuildState.AddMappedNodeId(TEXT("mapped_node_1"), false);

	FUMGPrototypeNavigationData NavigationData;
	FUMGPrototypeNavigationBinding& MappedBinding = NavigationData.ClickBindings.AddDefaulted_GetRef();
	MappedBinding.TriggerWidgetId = TEXT("mapped_node_1");
	MappedBinding.TriggerWidgetName = TEXT("FixtureComponent");
	MappedBinding.TriggerType = TEXT("ON_CLICK");
	FUMGPrototypeNavigationBinding& PlainBinding = NavigationData.ClickBindings.AddDefaulted_GetRef();
	PlainBinding.TriggerWidgetId = TEXT("plain_node_9");
	PlainBinding.TriggerWidgetName = TEXT("PlainCard");
	PlainBinding.TriggerType = TEXT("ON_CLICK");

	Design2UMGComponentMappingGeneration::ReportInertPrototypeBindings(NavigationData, BuildState);

	const TArray<FDesign2UMGMappingDiagnostic>& Diagnostics = BuildState.GetDiagnostics();
	TestEqual(TEXT("exactly one inert diagnostic"), Diagnostics.Num(), 1);
	if (Diagnostics.Num() == 1)
	{
		TestEqual(TEXT("inert diagnostic kind"), Diagnostics[0].Kind, TEXT("mapping_prototype_inert"));
		TestTrue(TEXT("inert diagnostic names the mapped trigger"),
			Diagnostics[0].Message.Contains(TEXT("'FixtureComponent'")));
		TestTrue(TEXT("inert diagnostic states interactions are project-owned"),
			Diagnostics[0].Message.Contains(TEXT("project-owned")));
		TestTrue(TEXT("diagnostic states the interaction is not generated"),
			Diagnostics[0].Message.Contains(TEXT("will not be generated")));
	}
	for (const FDesign2UMGMappingDiagnostic& Diagnostic : Diagnostics)
	{
		TestFalse(TEXT("plain node interaction never reported inert"),
			Diagnostic.Message.Contains(TEXT("'PlainCard'")));
	}
	return true;
}

#endif
