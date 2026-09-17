// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
//
// QA white-box structural tests (run d2u-cmsr-slice-a, Component Mapping phases 1/2, unit/config level).
// Test design independent of the in-delivery dev tests, covering structural behaviors dev cases do not:
// - ini validation's "not a UUserWidget subclass" and "line unparseable" error-input paths
// - the provider gate of the mapping main path (Pencil stays out of the main path) and the scope-resolution failure path
// - the design-facing inputs source chain (ComponentText, componentProperties first,
//   variantProperties fallback, skip when both absent) and the reflection-write type-mismatch/invalid-value paths
// - manifest serialization semantics of the inputs/matchedAtSetLevel fields
// - Prototype inert diagnostics not falsely accusing ordinary nodes

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Design2UMGComponentMappingConfig.h"
#include "Design2UMGComponentMappingGeneration.h"
#include "Design2UMGGenerationManifest.h"
#include "UMGSchema.h"
#include "UMGSchemaSourceExtensions.h"

#include "Tests/Design2UMGMappingQAWidget.h"

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
	const TCHAR* QATestWidgetClassPath = TEXT("/Script/Design2UMGEditor.Design2UMGMappedComponentTestWidget");

	FString MakeQAConfigIniPath(const TCHAR* FileName)
	{
		const FString Path = FPaths::ProjectSavedDir() / TEXT("Design2UMG") / TEXT("Tests") / FileName;
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		return Path;
	}

	bool WriteQAIni(const FString& IniPath, const FString& Content)
	{
		return FFileHelper::SaveStringToFile(Content, *IniPath);
	}

	// An IR node carrying the INSTANCE identity (a mapped candidate).
	FUMGWidgetNode MakeQAMappedCandidateNode(const FString& NodeId)
	{
		FUMGWidgetNode Node;
		Node.Id = NodeId;
		Node.Name = TEXT("QAComponent");
		Node.Type = TEXT("Button");
		Node.MutableSource().SourceNodeId = NodeId;
		Node.MutableSource().SourceNodeType = TEXT("INSTANCE");
		Node.MutableSource().ComponentId = TEXT("12:34");
		Node.MutableSource().ComponentSetKey = TEXT("56:78");
		Node.MutableSource().MainComponentKey = TEXT("qa-main-key");
		Node.MutableSource().bMappedComponentCandidate = true;
		return Node;
	}

	// A directly-built single-entry mapping config (bypassing the ini; tests the generation layer).
	FDesign2UMGComponentMappingConfig MakeQAMappingConfig(
		const FString& Key,
		const TArray<DesignComponentMapping::FComponentInputDeclaration>& Inputs)
	{
		FDesign2UMGComponentMappingConfig Config;
		DesignComponentMapping::FComponentMappingEntry& Entry = Config.Table.Entries.AddDefaulted_GetRef();
		Entry.Key = Key;
		Entry.Provider = TEXT("figma");
		Entry.DocumentScope = TEXT("qa-gen-scope");
		Entry.ComponentKey = TEXT("qa-main-key");
		Entry.WidgetClass = QATestWidgetClassPath;
		Entry.Inputs = Inputs;
		return Config;
	}

	DesignComponentMapping::FComponentInputDeclaration MakeQAInput(
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
	UDesign2UMGMappingQAWidget* ApplyQAInputs(
		FUMGWidgetNode& Node,
		const FDesign2UMGComponentMappingConfig& Config,
		FDesign2UMGComponentMappingBuildState& BuildState)
	{
		Node.MutableSource().MappedComponentKey = TEXT("figma|qa-gen-scope|qa-main-key");
		UDesign2UMGMappingQAWidget* Instance = NewObject<UDesign2UMGMappingQAWidget>(GetTransientPackage());
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
	FDesign2UMGMappingQAConfigRejectsNonUserWidgetClassTest,
	"Design2UMG.ComponentMapping.QAConfigRejectsNonUserWidgetClass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGMappingQAConfigRejectsNonUserWidgetClassTest::RunTest(const FString&)
{
	const FString IniPath = MakeQAConfigIniPath(TEXT("QAConfigNonUserWidgetClass.ini"));
	const FString IniContent =
		TEXT("[/Design2UMG/ComponentMapping]\n")
		TEXT("+Mappings=(Key=\"figma|qa-cfg-scope|bad-class\",WidgetClass=\"/Script/UMG.TextBlock\")\n")
		TEXT("+Mappings=(Key=\"figma|qa-cfg-scope|good-class\",WidgetClass=\"/Script/Design2UMGEditor.Design2UMGMappedComponentTestWidget\")\n");
	TestTrue(TEXT("write temp mapping ini"), WriteQAIni(IniPath, IniContent));

	const FDesign2UMGComponentMappingConfig Config = Design2UMGComponentMappingConfig::Load(IniPath);
	IFileManager::Get().Delete(*IniPath, false, true);

	TestEqual(TEXT("only the valid entry survives"), Config.Table.Entries.Num(), 1);
	TestNotNull(TEXT("valid entry still loaded"),
		Config.Table.FindEntryByKey(TEXT("figma|qa-cfg-scope|good-class")));
	TestNull(TEXT("non-UserWidget entry dropped"),
		Config.Table.FindEntryByKey(TEXT("figma|qa-cfg-scope|bad-class")));

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
	FDesign2UMGMappingQAConfigRejectsUnparseableLineTest,
	"Design2UMG.ComponentMapping.QAConfigRejectsUnparseableLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGMappingQAConfigRejectsUnparseableLineTest::RunTest(const FString&)
{
	const FString IniPath = MakeQAConfigIniPath(TEXT("QAConfigUnparseableLine.ini"));
	const FString IniContent =
		TEXT("[/Design2UMG/ComponentMapping]\n")
		TEXT("+Mappings=this-line-is-not-a-struct\n");
	TestTrue(TEXT("write temp mapping ini"), WriteQAIni(IniPath, IniContent));

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
	FDesign2UMGMappingQAPencilProviderSkippedTest,
	"Design2UMG.ComponentMapping.QAPencilProviderSkipped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGMappingQAPencilProviderSkippedTest::RunTest(const FString&)
{
	FUMGWidgetData Data;
	Data.MutableSourceDocument().Provider = TEXT("pencil");
	Data.MutableSourceDocument().DocumentScope = TEXT("qa-gen-scope");
	Data.RootWidget = MakeQAMappedCandidateNode(TEXT("pen_inst_1"));

	// Entries built directly (bypassing the ini provider validation) prove the gate also holds on the generation side:
	// even a pencil entry matching the node identity character-for-character must not hit.
	FDesign2UMGComponentMappingConfig Config;
	DesignComponentMapping::FComponentMappingEntry& Entry = Config.Table.Entries.AddDefaulted_GetRef();
	Entry.Key = TEXT("pencil|qa-gen-scope|qa-main-key");
	Entry.Provider = TEXT("pencil");
	Entry.DocumentScope = TEXT("qa-gen-scope");
	Entry.ComponentKey = TEXT("qa-main-key");
	Entry.WidgetClass = QATestWidgetClassPath;

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
	FDesign2UMGMappingQAUnresolvedDocumentScopeTest,
	"Design2UMG.ComponentMapping.QAUnresolvedDocumentScope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGMappingQAUnresolvedDocumentScopeTest::RunTest(const FString&)
{
	FUMGWidgetData Data;
	Data.MutableSourceDocument().Provider = TEXT("figma");
	Data.MutableSourceDocument().DocumentScope = TEXT("");
	Data.MutableSourceDocument().DocumentScopeOrigin = TEXT("unresolved");
	Data.RootWidget = MakeQAMappedCandidateNode(TEXT("figma_inst_1"));

	FDesign2UMGComponentMappingConfig Config = MakeQAMappingConfig(
		TEXT("figma|qa-gen-scope|qa-main-key"), {});

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
	FDesign2UMGMappingQAComponentTextInputValueTest,
	"Design2UMG.ComponentMapping.QAComponentTextInputValue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGMappingQAComponentTextInputValueTest::RunTest(const FString&)
{
	const TArray<DesignComponentMapping::FComponentInputDeclaration> Inputs = {
		MakeQAInput(TEXT("label"), TEXT("TextValue"),
			DesignComponentMapping::EComponentInputType::Text,
			DesignComponentMapping::EComponentInputValueKind::ComponentText),
	};

	// Has direct text: write succeeds + the overwrite diagnostic carries the design value.
	FUMGWidgetNode NodeWithText = MakeQAMappedCandidateNode(TEXT("text_inst_1"));
	NodeWithText.Text.Text = FString(TEXT("Checkout"));
	FDesign2UMGComponentMappingConfig ConfigWithText =
		MakeQAMappingConfig(TEXT("figma|qa-gen-scope|qa-main-key"), Inputs);
	FDesign2UMGComponentMappingBuildState StateWithText(ConfigWithText);
	UDesign2UMGMappingQAWidget* InstanceWithText = ApplyQAInputs(NodeWithText, ConfigWithText, StateWithText);
	TestEqual(TEXT("component text written to declared target"),
		InstanceWithText->TextValue.ToString(), TEXT("Checkout"));
	FString AppliedMessage;
	TestEqual(TEXT("applied diagnostic emitted once"),
		CountDiagnosticsByKind(StateWithText, TEXT("mapping_input_applied"), &AppliedMessage), 1);
	TestTrue(TEXT("applied diagnostic reports the design value"),
		AppliedMessage.Contains(TEXT("overwritten with design value 'Checkout'")));
	TestEqual(TEXT("no skip diagnostic on success"),
		CountDiagnosticsByKind(StateWithText, TEXT("mapping_input_skipped")), 0);

	// No direct text: input skipped + diagnostic; the default value is not written.
	FUMGWidgetNode NodeWithoutText = MakeQAMappedCandidateNode(TEXT("text_inst_2"));
	FDesign2UMGComponentMappingConfig ConfigWithoutText =
		MakeQAMappingConfig(TEXT("figma|qa-gen-scope|qa-main-key"), Inputs);
	FDesign2UMGComponentMappingBuildState StateWithoutText(ConfigWithoutText);
	UDesign2UMGMappingQAWidget* InstanceWithoutText = ApplyQAInputs(NodeWithoutText, ConfigWithoutText, StateWithoutText);
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
	FDesign2UMGMappingQAComponentPropertyFallbackTest,
	"Design2UMG.ComponentMapping.QAComponentPropertyFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGMappingQAComponentPropertyFallbackTest::RunTest(const FString&)
{
	const TArray<DesignComponentMapping::FComponentInputDeclaration> Inputs = {
		MakeQAInput(TEXT("variant"), TEXT("NameValue"), DesignComponentMapping::EComponentInputType::Name),
	};

	// Both places have the same-named key: the componentProperties value wins.
	FUMGWidgetNode BothNode = MakeQAMappedCandidateNode(TEXT("prop_inst_1"));
	BothNode.MutableSource().ComponentPropertiesJson = TEXT("{\"variant\":{\"type\":\"VARIANT\",\"value\":\"Secondary\"}}");
	BothNode.MutableSource().VariantPropertiesJson = TEXT("{\"variant\":\"Primary\"}");
	FDesign2UMGComponentMappingConfig BothConfig =
		MakeQAMappingConfig(TEXT("figma|qa-gen-scope|qa-main-key"), Inputs);
	FDesign2UMGComponentMappingBuildState BothState(BothConfig);
	UDesign2UMGMappingQAWidget* BothInstance = ApplyQAInputs(BothNode, BothConfig, BothState);
	TestEqual(TEXT("componentProperties value wins over variantProperties"),
		BothInstance->NameValue, FName(TEXT("Secondary")));
	TestEqual(TEXT("componentProperties hit applies cleanly"),
		CountDiagnosticsByKind(BothState, TEXT("mapping_input_applied")), 1);

	// Only in variantProperties: the fallback takes effect.
	FUMGWidgetNode VariantOnlyNode = MakeQAMappedCandidateNode(TEXT("prop_inst_2"));
	VariantOnlyNode.MutableSource().VariantPropertiesJson = TEXT("{\"variant\":\"Primary\"}");
	FDesign2UMGComponentMappingConfig VariantOnlyConfig =
		MakeQAMappingConfig(TEXT("figma|qa-gen-scope|qa-main-key"), Inputs);
	FDesign2UMGComponentMappingBuildState VariantOnlyState(VariantOnlyConfig);
	UDesign2UMGMappingQAWidget* VariantOnlyInstance = ApplyQAInputs(VariantOnlyNode, VariantOnlyConfig, VariantOnlyState);
	TestEqual(TEXT("variantProperties fallback supplies the value"),
		VariantOnlyInstance->NameValue, FName(TEXT("Primary")));
	FString VariantAppliedMessage;
	TestEqual(TEXT("fallback apply diagnosed"),
		CountDiagnosticsByKind(VariantOnlyState, TEXT("mapping_input_applied"), &VariantAppliedMessage), 1);
	TestTrue(TEXT("fallback apply reports the design value"),
		VariantAppliedMessage.Contains(TEXT("'Primary'")));

	// Neither place has it: skipped + diagnostic.
	FUMGWidgetNode MissingNode = MakeQAMappedCandidateNode(TEXT("prop_inst_3"));
	MissingNode.MutableSource().ComponentPropertiesJson = TEXT("{\"other\":{\"type\":\"TEXT\",\"value\":\"x\"}}");
	MissingNode.MutableSource().VariantPropertiesJson = TEXT("{\"other\":\"x\"}");
	FDesign2UMGComponentMappingConfig MissingConfig =
		MakeQAMappingConfig(TEXT("figma|qa-gen-scope|qa-main-key"), Inputs);
	FDesign2UMGComponentMappingBuildState MissingState(MissingConfig);
	UDesign2UMGMappingQAWidget* MissingInstance = ApplyQAInputs(MissingNode, MissingConfig, MissingState);
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
	FDesign2UMGMappingQAInputTypeMismatchAndInvalidValuesTest,
	"Design2UMG.ComponentMapping.QAInputTypeMismatchAndInvalidValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGMappingQAInputTypeMismatchAndInvalidValuesTest::RunTest(const FString&)
{
	const TArray<DesignComponentMapping::FComponentInputDeclaration> Inputs = {
		// Bool declared but the target is an FText property: type mismatch.
		MakeQAInput(TEXT("label"), TEXT("TextValue"), DesignComponentMapping::EComponentInputType::Bool),
		// Bool declared but the design value is not a bool.
		MakeQAInput(TEXT("enabled"), TEXT("BoolValue"), DesignComponentMapping::EComponentInputType::Bool),
		// Float declared but the design value is not numeric.
		MakeQAInput(TEXT("value"), TEXT("DoubleValue"), DesignComponentMapping::EComponentInputType::Float),
		// Float declared writing a single-precision property: must succeed.
		MakeQAInput(TEXT("ratio"), TEXT("FloatValue"), DesignComponentMapping::EComponentInputType::Float),
	};

	FUMGWidgetNode Node = MakeQAMappedCandidateNode(TEXT("write_inst_1"));
	Node.MutableSource().ComponentPropertiesJson =
		TEXT("{\"label\":{\"type\":\"TEXT\",\"value\":\"Checkout\"},")
		TEXT("\"enabled\":{\"type\":\"BOOLEAN\",\"value\":\"maybe\"},")
		TEXT("\"value\":{\"type\":\"TEXT\",\"value\":\"abc\"},")
		TEXT("\"ratio\":{\"type\":\"NUMBER\",\"value\":2.5}}");

	FDesign2UMGComponentMappingConfig Config =
		MakeQAMappingConfig(TEXT("figma|qa-gen-scope|qa-main-key"), Inputs);
	FDesign2UMGComponentMappingBuildState BuildState(Config);
	UDesign2UMGMappingQAWidget* Instance = ApplyQAInputs(Node, Config, BuildState);

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
	FDesign2UMGMappingQAManifestFieldSerializationTest,
	"Design2UMG.ComponentMapping.QAManifestFieldSerialization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGMappingQAManifestFieldSerializationTest::RunTest(const FString&)
{
	FDesign2UMGGenerationManifestPayload Payload;
	Payload.Provider = TEXT("figma");
	Payload.DocumentScope = TEXT("qa-manifest-scope");
	Payload.SourceRootNodeId = TEXT("qa-root");
	Payload.SourceRootName = TEXT("QARootPage");

	FDesign2UMGMappedInstanceRecord SetLevelRecord;
	SetLevelRecord.DesignNodeId = TEXT("qa-mapped-1");
	SetLevelRecord.WidgetName = TEXT("WBP_QA_SetLevel");
	SetLevelRecord.MappedKey = TEXT("figma|qa-manifest-scope|56:78");
	SetLevelRecord.MappedClass = QATestWidgetClassPath;
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
	VariantRecord.DesignNodeId = TEXT("qa-mapped-2");
	VariantRecord.WidgetName = TEXT("WBP_QA_Variant");
	VariantRecord.MappedKey = TEXT("figma|qa-manifest-scope|qa-main-key");
	VariantRecord.MappedClass = QATestWidgetClassPath;
	VariantRecord.bMatchedAtSetLevel = false;
	Payload.MappedInstances.Add(VariantRecord);

	FDesign2UMGGeneratedInstanceRecord GeneratedRecord;
	GeneratedRecord.DesignNodeId = TEXT("qa-generated-1");
	GeneratedRecord.WidgetName = TEXT("Card_0");
	Payload.TopLevelGenerated.Add(GeneratedRecord);

	Payload.D2UOwnedNames = { TEXT("WBP_QA_SetLevel"), TEXT("WBP_QA_Variant"), TEXT("Card_0"), TEXT("Card_0_Caption") };

	const FString PackageName = TEXT("/Game/Design2UMG/_TestOutput/QAManifestFields");
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
	TestEqual(TEXT("document scope"), Manifest->GetStringField(TEXT("documentScope")), TEXT("qa-manifest-scope"));
	TestEqual(TEXT("source root node id"),
		Manifest->GetObjectField(TEXT("sourceRoot"))->GetStringField(TEXT("nodeId")), TEXT("qa-root"));
	TestEqual(TEXT("source root name"),
		Manifest->GetObjectField(TEXT("sourceRoot"))->GetStringField(TEXT("name")), TEXT("QARootPage"));

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
	TestEqual(TEXT("first record widget name"), Record1->GetStringField(TEXT("widgetName")), TEXT("WBP_QA_SetLevel"));
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
		Record3->GetStringField(TEXT("designNodeId")), TEXT("qa-generated-1"));
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
			OwnedNames.Includes(TSet<FString>({ TEXT("WBP_QA_SetLevel"), TEXT("WBP_QA_Variant"), TEXT("Card_0"), TEXT("Card_0_Caption") })));
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
	FDesign2UMGMappingQAPrototypeInertNoFalsePositiveTest,
	"Design2UMG.ComponentMapping.QAPrototypeInertNoFalsePositive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGMappingQAPrototypeInertNoFalsePositiveTest::RunTest(const FString&)
{
	FDesign2UMGComponentMappingConfig Config = MakeQAMappingConfig(TEXT("figma|qa-gen-scope|qa-main-key"), {});
	FDesign2UMGComponentMappingBuildState BuildState(Config);
	BuildState.AddMappedNodeId(TEXT("mapped_node_1"), false);

	FUMGPrototypeNavigationData NavigationData;
	FUMGPrototypeNavigationBinding& MappedBinding = NavigationData.ClickBindings.AddDefaulted_GetRef();
	MappedBinding.TriggerWidgetId = TEXT("mapped_node_1");
	MappedBinding.TriggerWidgetName = TEXT("QAComponent");
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
			Diagnostics[0].Message.Contains(TEXT("'QAComponent'")));
		TestTrue(TEXT("inert diagnostic states interactions are project-owned"),
			Diagnostics[0].Message.Contains(TEXT("project-owned")));
		TestTrue(TEXT("inert diagnostic states the interaction is inert"),
			Diagnostics[0].Message.Contains(TEXT("inert this phase")));
	}
	for (const FDesign2UMGMappingDiagnostic& Diagnostic : Diagnostics)
	{
		TestFalse(TEXT("plain node interaction never reported inert"),
			Diagnostic.Message.Contains(TEXT("'PlainCard'")));
	}
	return true;
}

#endif
