// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"

#include "Design2UMGComponentMappingConfig.h"
#include "Design2UMGComponentMappingGeneration.h"
#include "Design2UMGConverter.h"
#include "Design2UMGTestUtils.h"
#include "FigmaParser.h"
#include "FigmaTestDocBuilder.h"
#include "UMGSchema.h"
#include "UMGSchemaSourceExtensions.h"
#include "Tests/ComponentPropertyFixtures.h"

#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"
#include "HAL/FileManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	const TCHAR* TestScope = TEXT("mapping-test-file-key");

	// A design with two instances (same component definition) + one unmapped ordinary node.
	FString MakeMappingTestFigmaJson()
	{
		using namespace Design2UMGCoreTests;
		const FString ButtonStyleJson =
			TEXT("\"fontFamily\":\"Inter\",\"fontSize\":16,\"fontWeight\":500,\"textAlignHorizontal\":\"LEFT\",\"textAlignVertical\":\"TOP\",\"textDecoration\":\"NONE\",\"letterSpacing\":0,\"lineHeightUnit\":\"AUTO\",\"lineHeightPx\":0");
		const FString InstanceOneJson = MakeContainerNode(TEXT("btn_inst_1"), TEXT("PrimaryButton"), TEXT("INSTANCE"), 24, 24, 120, 44,
			MakeTextNode(TEXT("btn_inst_1_label"), TEXT("Label"), 32, 32, 80, 20, TEXT("Checkout"), ButtonStyleJson),
			FString::Printf(
				TEXT(",\"componentId\":\"12:34\",\"componentSetId\":\"56:78\",\"componentSetKey\":\"published-set-key\",\"mainComponentKey\":\"component-key-primary\","
					 "\"componentProperties\":{\"label\":{\"type\":\"TEXT\",\"value\":\"Checkout\"}},\"fills\":[%s]"),
				*MakeSolidFillJson(0.1, 0.4, 0.9)));
		const FString InstanceTwoJson = MakeContainerNode(TEXT("btn_inst_2"), TEXT("PrimaryButton"), TEXT("INSTANCE"), 24, 88, 120, 44,
			MakeTextNode(TEXT("btn_inst_2_label"), TEXT("Label"), 32, 96, 80, 20, TEXT("Pay Now"), ButtonStyleJson),
			FString::Printf(
				TEXT(",\"componentId\":\"12:34\",\"componentSetId\":\"56:78\",\"componentSetKey\":\"published-set-key\",\"mainComponentKey\":\"component-key-primary\","
					 "\"componentProperties\":{\"label\":{\"type\":\"TEXT\",\"value\":\"Pay Now\"}},\"fills\":[%s]"),
				*MakeSolidFillJson(0.1, 0.4, 0.9)));
		const FString PlainCardJson = MakeContainerNode(TEXT("plain_card"), TEXT("PlainCard"), TEXT("FRAME"), 180, 24, 140, 108,
			MakeTextNode(TEXT("plain_card_text"), TEXT("Caption"), 188, 32, 100, 20, TEXT("Not mapped"),
				TEXT("\"fontFamily\":\"Inter\",\"fontSize\":14,\"fontWeight\":400,\"textAlignHorizontal\":\"LEFT\",\"textAlignVertical\":\"TOP\",\"textDecoration\":\"NONE\",\"letterSpacing\":0,\"lineHeightUnit\":\"AUTO\",\"lineHeightPx\":0")),
			FString::Printf(TEXT(",\"fills\":[%s]"), *MakeSolidFillJson(0.9, 0.9, 0.9)));
		return MakeFigmaDocJson(
			MakeContainerNode(TEXT("page_root"), TEXT("MappingPage"), TEXT("FRAME"), 0, 0, 360, 200,
				FString::Printf(TEXT("%s,%s,%s"), *InstanceOneJson, *InstanceTwoJson, *PlainCardJson)),
			TEXT(",\"fileKey\":\"mapping-test-file-key\""));
	}

	FDesign2UMGComponentMappingConfig MakeDirectMappingConfig(const FString& WidgetClassPath)
	{
		ComponentFixtures::MappedClass();
		FDesign2UMGComponentMappingConfig Config;
		DesignComponentMapping::FComponentMappingEntry& Entry =
			Config.Table.Entries.AddDefaulted_GetRef();
		Entry.Key = FString::Printf(TEXT("figma|%s|component-key-primary"), TestScope);
		Entry.Provider = TEXT("figma");
		Entry.DocumentScope = TestScope;
		Entry.ComponentKey = TEXT("component-key-primary");
		Entry.WidgetClass = WidgetClassPath;
		return Config;
	}
}

// ---------------------------------------------------------------------------
// IR-layer assertions : Convert candidate markers + ApplyMapping hit markers
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentMappingIRMarkingTest,
	"Design2UMG.ComponentMapping.IRMarking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FComponentMappingIRMarkingTest::RunTest(const FString&)
{
	FDesignDocument Document;
	TestTrue(TEXT("Parse mapping fixture"), FFigmaParser::ParseDocument(MakeMappingTestFigmaJson(), Document));

	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());

	// Document-level identity: the file key as DocumentScope.
	TestTrue(TEXT("Converted data has source document identity"), Data.HasSourceDocumentData());
	if (Data.HasSourceDocumentData())
	{
		TestEqual(TEXT("Provider stamped as figma"), Data.SourceDocument().Provider, TEXT("figma"));
		TestEqual(TEXT("DocumentScope uses file key"), Data.SourceDocument().DocumentScope, TestScope);
		TestEqual(TEXT("DocumentScope origin is fileKey"), Data.SourceDocument().DocumentScopeOrigin, TEXT("fileKey"));
	}

	const FUMGWidgetNode* InstanceWidget = Design2UMGTestUtils::FindWidgetById(Data.RootWidget, TEXT("btn_inst_1"));
	const FUMGWidgetNode* PlainWidget = Design2UMGTestUtils::FindWidgetById(Data.RootWidget, TEXT("plain_card"));
	TestNotNull(TEXT("Instance widget converted"), InstanceWidget);
	TestNotNull(TEXT("Plain widget converted"), PlainWidget);
	if (!InstanceWidget || !PlainWidget)
	{
		return false;
	}

	// Conversion-time candidate markers: instances with component identity are mapped candidates; ordinary nodes are not.
	TestTrue(TEXT("Component instance is a mapped candidate"), InstanceWidget->Source().bMappedComponentCandidate);
	TestFalse(TEXT("Plain node is not a mapped candidate"), PlainWidget->Source().bMappedComponentCandidate);
	TestTrue(TEXT("No mapping key before generation apply"), InstanceWidget->Source().MappedComponentKey.IsEmpty());

	// Pre-generation matching: hit entries write the Key into the IR sidecar; misses/ordinary nodes are unaffected.
	FDesign2UMGComponentMappingConfig Config = MakeDirectMappingConfig(ComponentFixtures::MappedClassPath);
	FDesign2UMGComponentMappingBuildState BuildState(Config);
	Design2UMGComponentMappingGeneration::ApplyMappingToWidgetTreeIR(
		Data.RootWidget, Config, Data, BuildState);

	TestEqual(TEXT("Mapped instance key written to IR"),
		InstanceWidget->Source().MappedComponentKey,
		FString::Printf(TEXT("figma|%s|component-key-primary"), TestScope));
	TestTrue(TEXT("Plain node stays unmapped"), PlainWidget->Source().MappedComponentKey.IsEmpty());

	bool bSawHit = false;
	bool bSawScope = false;
	for (const FDesign2UMGMappingDiagnostic& Diagnostic : BuildState.GetDiagnostics())
	{
		bSawHit |= Diagnostic.Kind == TEXT("mapping_hit");
		bSawScope |= Diagnostic.Kind == TEXT("mapping_scope");
	}
	TestTrue(TEXT("Hit diagnostic emitted"), bSawHit);
	TestTrue(TEXT("Scope diagnostic reports the resolved scope"), bSawScope);
	return true;
}

// ---------------------------------------------------------------------------
// Component-set-level matching: fall back to ComponentSetKey when the variant misses
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentMappingIRSetLevelMarkingTest,
	"Design2UMG.ComponentMapping.SetLevelMarking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FComponentMappingIRSetLevelMarkingTest::RunTest(const FString&)
{
	FDesignDocument Document;
	TestTrue(TEXT("Parse mapping fixture"), FFigmaParser::ParseDocument(MakeMappingTestFigmaJson(), Document));
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());

	FDesign2UMGComponentMappingConfig Config;
	DesignComponentMapping::FComponentMappingEntry& Entry = Config.Table.Entries.AddDefaulted_GetRef();
	Entry.Key = FString::Printf(TEXT("figma|%s|published-set-key"), TestScope);
	Entry.Provider = TEXT("figma");
	Entry.DocumentScope = TestScope;
	Entry.ComponentKey = TEXT("published-set-key");
	Entry.WidgetClass = ComponentFixtures::MappedClass()->GetPathName();

	FDesign2UMGComponentMappingBuildState BuildState(Config);
	Design2UMGComponentMappingGeneration::ApplyMappingToWidgetTreeIR(
		Data.RootWidget, Config, Data, BuildState);

	const FUMGWidgetNode* InstanceWidget = Design2UMGTestUtils::FindWidgetById(Data.RootWidget, TEXT("btn_inst_2"));
	TestNotNull(TEXT("Instance widget converted"), InstanceWidget);
	if (!InstanceWidget)
	{
		return false;
	}
	TestEqual(TEXT("Set-level key matches the instance"),
		InstanceWidget->Source().MappedComponentKey,
		FString::Printf(TEXT("figma|%s|published-set-key"), TestScope));
	return true;
}

// ---------------------------------------------------------------------------
// ini loading and validation: legal entries admitted; illegal entries/inputs dropped with diagnostics
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentMappingConfigLoadTest,
	"Design2UMG.ComponentMapping.ConfigLoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FComponentMappingConfigLoadTest::RunTest(const FString&)
{
	const FString TempIni = FPaths::ProjectSavedDir() / TEXT("Design2UMG") / TEXT("Tests") / TEXT("MappingConfigLoadTest.ini");
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(TempIni), true);
	const FString IniContent =
		TEXT("[/Design2UMG/ComponentMapping]\n")
		TEXT("+Mappings=(Key=\"figma|scope-1|component-key-1\",WidgetClass=\"/Game/Design2UMG/TestFixtures/WBP_MappedComponent.WBP_MappedComponent_C\",")
		TEXT("Inputs=((Source=\"label\",Target=\"Label\",Type=\"Text\"),(Source=\"enabled\",Target=\"EnabledFlag\",Type=\"Bool\")))\n")
		TEXT("+Mappings=(Key=\"bad-key\",WidgetClass=\"/Script/UMG.TextBlock\")\n")
		TEXT("+Mappings=(Key=\"pencil|scope-1|component-key-2\",WidgetClass=\"/Script/UMG.TextBlock\")\n")
		TEXT("+Mappings=(Key=\"figma|scope-1|component-key-3\",WidgetClass=\"/Game/Missing/WBP_Missing.WBP_Missing_C\")\n")
		TEXT("+Mappings=(Key=\"figma|scope-1|component-key-4\",WidgetClass=\"/Script/UMG.UserWidget\",")
		TEXT("Inputs=((Source=\"x\",Target=\"Y\",Type=\"String\")))\n")
		TEXT("+Mappings=(Key=\"figma|scope-1|component-key-1\",WidgetClass=\"/Script/UMG.UserWidget\")\n");
	TestTrue(TEXT("Write temp mapping ini"), FFileHelper::SaveStringToFile(IniContent, *TempIni));

	ComponentFixtures::MappedClass();
	const FDesign2UMGComponentMappingConfig Config = Design2UMGComponentMappingConfig::Load(TempIni);
	IFileManager::Get().Delete(*TempIni, false, true);

	TestTrue(TEXT("Missing ini stays silent and empty"), !Design2UMGComponentMappingConfig::Load(
		FPaths::ProjectSavedDir() / TEXT("Design2UMG") / TEXT("Tests") / TEXT("__absent__.ini")).HasMappings());

	// The legal entry (component-key-1) and the "class legal but input illegal" entry (component-key-4) are both
	// admitted; an illegal input drops only the input itself; the mapping entry stays in effect.
	TestEqual(TEXT("Valid entries survive validation"), Config.Table.Entries.Num(), 2);
	const DesignComponentMapping::FComponentMappingEntry* ValidEntry =
		Config.Table.FindEntryByKey(TEXT("figma|scope-1|component-key-1"));
	TestNotNull(TEXT("Fully valid entry kept"), ValidEntry);
	if (ValidEntry)
	{
		TestEqual(TEXT("Valid entry class"), ValidEntry->WidgetClass, ComponentFixtures::MappedClassPath);
		TestEqual(TEXT("Valid entry keeps valid inputs"), ValidEntry->Inputs.Num(), 2);
	}
	const DesignComponentMapping::FComponentMappingEntry* PartialEntry =
		Config.Table.FindEntryByKey(TEXT("figma|scope-1|component-key-4"));
	TestNotNull(TEXT("Entry with one invalid input still applies"), PartialEntry);
	if (PartialEntry)
	{
		TestEqual(TEXT("Invalid input dropped from entry"), PartialEntry->Inputs.Num(), 0);
	}

	// Diagnostic coverage: bad Key, Pencil provider, missing class, illegal input, duplicate Key.
	int32 BadKeyDiagnostics = 0;
	int32 PencilDiagnostics = 0;
	int32 ClassUnavailableDiagnostics = 0;
	int32 InputInvalidDiagnostics = 0;
	int32 DuplicateKeyDiagnostics = 0;
	for (const FDesign2UMGMappingDiagnostic& Diagnostic : Config.ValidationDiagnostics)
	{
		BadKeyDiagnostics += Diagnostic.Kind == TEXT("mapping_config_invalid") && Diagnostic.Message.Contains(TEXT("bad-key")) ? 1 : 0;
		PencilDiagnostics += Diagnostic.Message.Contains(TEXT("pencil")) ? 1 : 0;
		ClassUnavailableDiagnostics += Diagnostic.Kind == TEXT("mapping_class_unavailable") ? 1 : 0;
		InputInvalidDiagnostics += Diagnostic.Kind == TEXT("mapping_input_invalid") ? 1 : 0;
		DuplicateKeyDiagnostics += Diagnostic.Message.Contains(TEXT("already exists")) ? 1 : 0;
	}
	TestEqual(TEXT("Malformed key diagnosed"), BadKeyDiagnostics, 1);
	TestEqual(TEXT("Pencil provider entry dropped with diagnostic"), PencilDiagnostics, 1);
	TestEqual(TEXT("Missing class entry diagnosed"), ClassUnavailableDiagnostics, 1);
	TestEqual(TEXT("Invalid input diagnosed"), InputInvalidDiagnostics, 1);
	TestEqual(TEXT("Duplicate key diagnosed"), DuplicateKeyDiagnostics, 1);
	return true;
}

namespace
{
	// Build a single-input mapping config directly (bypassing the ini; tests the generation layer).
	FDesign2UMGComponentMappingConfig MakeSingleInputMappingConfig(
		const FString& Key,
		const FString& WidgetClassPath,
		const DesignComponentMapping::FComponentInputDeclaration& Input)
	{
		ComponentFixtures::MappedClass();
		FDesign2UMGComponentMappingConfig Config;
		DesignComponentMapping::FComponentMappingEntry& Entry = Config.Table.Entries.AddDefaulted_GetRef();
		Entry.Key = Key;
		Entry.Provider = TEXT("figma");
		Entry.DocumentScope = TEXT("inherited-target-scope");
		Entry.ComponentKey = TEXT("mk-single-input");
		Entry.WidgetClass = WidgetClassPath;
		Entry.Inputs.Add(Input);
		return Config;
	}

	int32 CountMappingDiagnosticsByKind(
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
// inputs target-name resolution: the spec example name (enabled -> IsEnabled, Bool) must write into the
// real UWidget inherited property. UWidget's real property name is bIsEnabled; the resolution rule is an
// exact-name lookup first and, when the declared type is Bool, one retry with UE's bool naming convention (b prefix).
// The target is a really compiled WBP generated class, also pinning the fact that inherited properties resolve by name.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentMappingInheritedBoolTargetTest,
	"Design2UMG.ComponentMapping.InheritedBoolTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FComponentMappingInheritedBoolTargetTest::RunTest(const FString&)
{
	UBlueprint* TestBlueprint = FKismetEditorUtilities::CreateBlueprint(
		UUserWidget::StaticClass(), GetTransientPackage(), FName(TEXT("D2UInheritedBoolTargetBP")),
		BPTYPE_Normal, UWidgetBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass(), NAME_None);
	if (!TestTrue(TEXT("Create test widget blueprint"), TestBlueprint != nullptr))
	{
		return false;
	}
	FKismetEditorUtilities::CompileBlueprint(TestBlueprint, EBlueprintCompileOptions::None);
	UClass* GeneratedClass = TestBlueprint->GeneratedClass;
	if (!TestNotNull(TEXT("Compiled WBP has a generated class"), GeneratedClass))
	{
		return false;
	}

	UUserWidget* Instance = NewObject<UUserWidget>(GetTransientPackage(), GeneratedClass);
	TestTrue(TEXT("Instance starts enabled by default"), Instance->GetIsEnabled());

	FUMGWidgetNode Node;
	Node.Id = TEXT("inherited_bool_inst");
	Node.MutableSource().MappedComponentKey = TEXT("figma|inherited-target-scope|mk-single-input");
	Node.MutableSource().ComponentPropertiesJson = TEXT("{\"enabled\":{\"type\":\"BOOLEAN\",\"value\":\"false\"}}");

	DesignComponentMapping::FComponentInputDeclaration Input;
	Input.Source = TEXT("enabled");
	Input.Target = TEXT("IsEnabled");
	Input.Type = DesignComponentMapping::EComponentInputType::Bool;
	Input.ValueKind = DesignComponentMapping::EComponentInputValueKind::ComponentProperty;

	FDesign2UMGComponentMappingConfig Config = MakeSingleInputMappingConfig(
		TEXT("figma|inherited-target-scope|mk-single-input"),
		GeneratedClass->GetPathName(), Input);
	FDesign2UMGComponentMappingBuildState BuildState(Config);
	Design2UMGComponentMappingGeneration::ApplyMappedComponentInputs(Instance, Node, BuildState);

	TestFalse(TEXT("Spec-style Target=IsEnabled writes the UWidget inherited bool"), Instance->GetIsEnabled());
	FString AppliedMessage;
	TestEqual(TEXT("Inherited bool input applied once"),
		CountMappingDiagnosticsByKind(BuildState, TEXT("mapping_input_applied"), &AppliedMessage), 1);
	TestTrue(TEXT("Applied diagnostic reports the design value"),
		AppliedMessage.Contains(TEXT("overwritten with design value 'false'")));
	TestEqual(TEXT("No skip diagnostic on the inherited bool input"),
		CountMappingDiagnosticsByKind(BuildState, TEXT("mapping_input_skipped")), 0);
	return true;
}

// ---------------------------------------------------------------------------
// inputs source ComponentText subtree fallback: a typical Figma button's label is carried by an inner TEXT of
// the instance (no componentProperties.label); the main text takes the first inner TEXT.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentMappingComponentTextFromSubtreeTest,
	"Design2UMG.ComponentMapping.ComponentTextFromSubtree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FComponentMappingComponentTextFromSubtreeTest::RunTest(const FString&)
{
	FUMGWidgetNode Node;
	Node.Id = TEXT("subtree_text_inst");
	Node.Type = TEXT("Button");
	Node.MutableSource().MappedComponentKey = TEXT("figma|inherited-target-scope|mk-single-input");
	// The instance itself has no text; the label hangs on an inner TEXT child.
	FUMGWidgetNode& InnerText = Node.Children.AddDefaulted_GetRef();
	InnerText.Id = TEXT("subtree_text_inst_label");
	InnerText.Type = TEXT("TextBlock");
	InnerText.Text.Text = FString(TEXT("Place Order"));

	UUserWidget* Instance =
		NewObject<UUserWidget>(GetTransientPackage(), ComponentFixtures::MappedClass());
	TestTrue(TEXT("Target starts empty"), ComponentFixtures::Read<FTextProperty>(Instance, TEXT("Label")).IsEmpty());

	DesignComponentMapping::FComponentInputDeclaration Input;
	Input.Source = TEXT("label");
	Input.Target = TEXT("Label");
	Input.Type = DesignComponentMapping::EComponentInputType::Text;
	Input.ValueKind = DesignComponentMapping::EComponentInputValueKind::ComponentText;

	FDesign2UMGComponentMappingConfig Config = MakeSingleInputMappingConfig(
		TEXT("figma|inherited-target-scope|mk-single-input"),
		ComponentFixtures::MappedClassPath, Input);
	FDesign2UMGComponentMappingBuildState BuildState(Config);
	Design2UMGComponentMappingGeneration::ApplyMappedComponentInputs(Instance, Node, BuildState);

	TestEqual(TEXT("Internal TEXT supplies the component text"),
		ComponentFixtures::Read<FTextProperty>(Instance, TEXT("Label")).ToString(), TEXT("Place Order"));
	FString AppliedMessage;
	TestEqual(TEXT("Subtree text input applied once"),
		CountMappingDiagnosticsByKind(BuildState, TEXT("mapping_input_applied"), &AppliedMessage), 1);
	TestTrue(TEXT("Applied diagnostic reports the design value"),
		AppliedMessage.Contains(TEXT("overwritten with design value 'Place Order'")));
	TestEqual(TEXT("No skip diagnostic when the subtree carries text"),
		CountMappingDiagnosticsByKind(BuildState, TEXT("mapping_input_skipped")), 0);
	return true;
}

// ---------------------------------------------------------------------------
// Prototype subtree boundary: interactions whose trigger sits on a mapped instance body or inside its
// (unexpanded) subtree generate no bindings; both emit the mapping_prototype_inert diagnostic with messages
// distinguishing the two locations; interactions of ordinary nodes are unaffected.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentMappingPrototypeSubtreeInertTest,
	"Design2UMG.ComponentMapping.PrototypeSubtreeInert",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FComponentMappingPrototypeSubtreeInertTest::RunTest(const FString&)
{
	FUMGWidgetData Data;
	Data.MutableSourceDocument().Provider = TEXT("figma");
	Data.MutableSourceDocument().DocumentScope = TEXT("subtree-inert-scope");
	Data.RootWidget.Id = TEXT("subtree_page");
	Data.RootWidget.Type = TEXT("CanvasPanel");

	// mapped instance (candidate identity exactly matches the entry) + an inner TEXT child.
	FUMGWidgetNode& MappedNode = Data.RootWidget.Children.AddDefaulted_GetRef();
	MappedNode.Id = TEXT("mapped_inst");
	MappedNode.Name = TEXT("MappedButton");
	MappedNode.Type = TEXT("Button");
	MappedNode.MutableSource().SourceNodeId = TEXT("mapped_inst");
	MappedNode.MutableSource().SourceNodeType = TEXT("INSTANCE");
	MappedNode.MutableSource().MainComponentKey = TEXT("mk-subtree-inert");
	MappedNode.MutableSource().bMappedComponentCandidate = true;
	FUMGWidgetNode& InnerText = MappedNode.Children.AddDefaulted_GetRef();
	InnerText.Id = TEXT("mapped_inst_label");
	InnerText.Name = TEXT("Label");
	InnerText.Type = TEXT("TextBlock");

	FDesign2UMGComponentMappingConfig Config;
	DesignComponentMapping::FComponentMappingEntry& Entry = Config.Table.Entries.AddDefaulted_GetRef();
	Entry.Key = TEXT("figma|subtree-inert-scope|mk-subtree-inert");
	Entry.Provider = TEXT("figma");
	Entry.DocumentScope = TEXT("subtree-inert-scope");
	Entry.ComponentKey = TEXT("mk-subtree-inert");
	Entry.WidgetClass = ComponentFixtures::MappedClass()->GetPathName();

	FDesign2UMGComponentMappingBuildState BuildState(Config);
	Design2UMGComponentMappingGeneration::ApplyMappingToWidgetTreeIR(
		Data.RootWidget, Config, Data, BuildState);

	FUMGPrototypeNavigationData NavigationData;
	auto AddBinding = [&NavigationData](const TCHAR* TriggerId, const TCHAR* TriggerName)
	{
		FUMGPrototypeNavigationBinding& Binding = NavigationData.ClickBindings.AddDefaulted_GetRef();
		Binding.TriggerWidgetId = TriggerId;
		Binding.TriggerWidgetName = TriggerName;
		Binding.TriggerType = TEXT("ON_CLICK");
	};
	AddBinding(TEXT("mapped_inst"), TEXT("MappedButton"));
	AddBinding(TEXT("mapped_inst_label"), TEXT("Label"));
	AddBinding(TEXT("plain_node"), TEXT("PlainCard"));

	Design2UMGComponentMappingGeneration::ReportInertPrototypeBindings(NavigationData, BuildState);

	int32 InstanceBodyInert = 0;
	int32 SubtreeInert = 0;
	for (const FDesign2UMGMappingDiagnostic& Diagnostic : BuildState.GetDiagnostics())
	{
		if (Diagnostic.Kind != TEXT("mapping_prototype_inert"))
		{
			continue;
		}
		InstanceBodyInert += Diagnostic.Message.Contains(TEXT("attached to a mapped component")) ? 1 : 0;
		SubtreeInert += Diagnostic.Message.Contains(TEXT("inside a mapped component's unexpanded subtree")) ? 1 : 0;
		if (Diagnostic.Message.Contains(TEXT("'PlainCard'")))
		{
			AddError(TEXT("Plain node interaction must never be reported inert"));
		}
	}
	TestEqual(TEXT("Interaction on the mapped instance body reported inert"), InstanceBodyInert, 1);
	TestEqual(TEXT("Interaction inside the mapped subtree reported inert"), SubtreeInert, 1);
	return true;
}

#endif
