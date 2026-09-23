// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/Design2UMGFileFailureFixture.h"
#include "FigmaParser.h"
#include "Design2UMGConverter.h"
#include "Design2UMGGenerationSettingsSnapshot.h"
#include "Design2UMGSettings.h"
#include "DesignImportPipeline.h"
#include "DesignImportUtils.h"
#include "UMGWidgetTypes.h"
#include "Design2UMGEditorImportServices.h"
#include "Design2UMGLocalFigmaImport.h"
#include "Interfaces/IPluginManager.h"
#include "Design2UMGHttpTestTransport.h"
#include "FigmaComponentDependencies.h"
#include "UMGSchemaSourceExtensions.h"
#include "Misc/ScopeExit.h"
#include "Templates/UnrealTemplate.h"
#include "ImageUtils.h"
#include "ImageCore.h"
#include "UMGGenerator.h"
#include "UMGJsonSerializer.h"
#include "GeneratedAssetLifecycle.h"
#include "Tests/Design2UMGTestAssetCleanup.h"
#include "UMGWidgetUtils.h"
#include "Tests/Design2UMGComponentInputTestWidget.h"
#include "Automation/Design2UMGAutomationStructureSnapshot.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/PanelWidget.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CustomEvent.h"
#include "Components/RichTextBlock.h"
#include "Engine/DataTable.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformFileManager.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "WidgetBlueprint.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Modules/ModuleManager.h"

namespace
{
	FString MakeComponentImportJson(const FString& SecondText)
	{
		return FString::Printf(TEXT(R"JSON({"fileKey":"component-blueprint-test","document":{
		 "id":"root","name":"Parent","type":"FRAME","absoluteBoundingBox":{"x":0,"y":0,"width":300,"height":100},"children":[
		 {"id":"a","name":"First","type":"INSTANCE","componentId":"main","mainComponentKey":"button-key",
		  "absoluteBoundingBox":{"x":10,"y":10,"width":100,"height":40},"children":[
		  {"id":"Ia;label","name":"Label","type":"TEXT","characters":"Confirm","style":{"fontSize":16,"fontFamily":"Roboto"},"absoluteBoundingBox":{"x":10,"y":10,"width":90,"height":24}}]},
		 {"id":"b","name":"Second","type":"INSTANCE","componentId":"main","mainComponentKey":"button-key",
		  "absoluteBoundingBox":{"x":150,"y":10,"width":100,"height":40},"children":[
		  {"id":"Ib;label","name":"Label","type":"TEXT","characters":"%s","style":{"fontSize":16,"fontFamily":"Roboto"},"absoluteBoundingBox":{"x":150,"y":10,"width":90,"height":24}}]}]},
		 "design2umgComponentDependencies":{"schemaVersion":1,"instances":{"a":{"id":"main","key":"button-key"},"b":{"id":"main","key":"button-key"}},"errors":[],"definitions":[
		 {"id":"main","key":"button-key","document":{"id":"main","name":"Button","type":"COMPONENT",
		 "absoluteBoundingBox":{"x":0,"y":0,"width":100,"height":40},"children":[
		 {"id":"label","name":"Label","type":"TEXT","characters":"Default","style":{"fontSize":16,"fontFamily":"Roboto"},"absoluteBoundingBox":{"x":0,"y":0,"width":90,"height":24}}]}}]}})JSON"), *SecondText);
	}

	TArray<UUserWidget*> FindInstances(UWidgetTree* Tree)
	{
		TArray<UUserWidget*> Result;
		Tree->ForEachWidget([&](UWidget* Widget) { if (UUserWidget* UserWidget = Cast<UUserWidget>(Widget)) Result.Add(UserWidget); });
		return Result;
	}

	TArray<FString> FindText(UWidgetTree* Tree)
	{
		TArray<FString> Result;
		Tree->ForEachWidget([&](UWidget* Widget) { if (const UTextBlock* Text = Cast<UTextBlock>(Widget)) Result.Add(Text->GetText().ToString()); });
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentDefinitionWarningsTest,
	"Design2UMG.ComponentBlueprints.DefinitionWarnings", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FComponentDefinitionWarningsTest::RunTest(const FString&)
{
	FString Source = MakeComponentImportJson(TEXT("Cancel"));
	Source.ReplaceInline(TEXT("\"characters\":\"Default\",\"style\":{\"fontSize\":16,\"fontFamily\":\"Roboto\"}"),
		TEXT("\"characters\":\"Default\",\"style\":{\"fontSize\":16,\"fontFamily\":\"D2UMissingDefinitionFace\"}"));
	FDesignDocument Document;
	if (!TestTrue(TEXT("Parse definition-only font request"), FFigmaParser::ParseDocument(Source, Document))) return false;
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestFalse(TEXT("Selected page does not carry the definition-only warning"), Data.Warnings.ContainsByPredicate(
		[](const FString& Warning) { return Warning.Contains(TEXT("D2UMissingDefinitionFace")); }));
	FDesign2UMGGenerationOutput Output;
	UWidgetBlueprint* Blueprint = DesignImportUtils::BuildWidgetBlueprint(Data,
		TEXT("/Game/Design2UMG/Automation/DefinitionWarnings_") + FGuid::NewGuid().ToString(EGuidFormats::Digits), nullptr, &Output);
	if (!TestNotNull(TEXT("Missing local face is reported without losing successful component generation"), Blueprint)) return false;
	TestTrue(TEXT("Dependency-only warning degrades the batch output"), Output.bHasDegradation);
	TestTrue(TEXT("Root generation output reports definition warning with stable component identity"),
		Output.MappingDiagnostics.ContainsByPredicate([](const FDesign2UMGMappingDiagnostic& Diagnostic)
		{
			return Diagnostic.Message.Contains(TEXT("D2UMissingDefinitionFace")) && Diagnostic.Message.Contains(TEXT("button-key"));
		}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentDependencyPartialSyncTest,
	"Design2UMG.ComponentBlueprints.DependencyPartialSync",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FComponentDependencyPartialSyncTest::RunTest(const FString&)
{
	const FString Folder = TEXT("/Game/Design2UMG/Automation/DependencyPartialSync_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	ON_SCOPE_EXIT { Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(Folder); };
	FDesignDocument Document;
	const FString Source = MakeComponentImportJson(TEXT("Cancel"));
	if (!TestTrue(TEXT("Parse first page"), FFigmaParser::ParseDocument(Source, Document))) return false;
	FDesign2UMGGenerationSettingsSnapshot Settings;
	FDesign2UMGGenerationOutput Initial;
	UWidgetBlueprint* First = DesignImportUtils::GenerateWidgetBlueprint(
		FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults()), Folder, &Settings, &Initial);
	if (!TestNotNull(TEXT("First component batch saved"), First)) return false;
	TestEqual(TEXT("Fresh complete batch is ordinary success"), Initial.Commit.Status, EDesign2UMGCommitStatus::Succeeded);

	// A new page shares the existing dependency: only its component merge can produce partial sync.
	FDesignDocument SecondDocument;
	if (!TestTrue(TEXT("Parse second page"), FFigmaParser::ParseDocument(
		Source.Replace(TEXT("\"id\":\"root\""), TEXT("\"id\":\"second-root\"")), SecondDocument))) return false;
	FDesign2UMGGenerationOutput Output;
	FUMGPrototypeNavigationData Navigation;
	UWidgetBlueprint* Second = DesignImportUtils::BuildWidgetBlueprint(
		FDesign2UMGConverter::Convert(SecondDocument, FDesign2UMGConversionSettings::Defaults()), Folder, &Settings, &Output, &Navigation);
	if (!TestNotNull(TEXT("New page built"), Second)
		|| !TestEqual(TEXT("One shared dependency"), Output.Components.Num(), 1)) return false;
	TestTrue(TEXT("Page itself is new"), Second != First);
	TestTrue(TEXT("Existing dependency carries its apply degradation"), Output.Components[0].Output->bHasDegradation);
	TestTrue(TEXT("Dependency degradation survives new root assembly"), Output.bHasDegradation);
	TestTrue(TEXT("Component partial synchronization is explained"), Output.MappingDiagnostics.ContainsByPredicate(
		[](const FDesign2UMGMappingDiagnostic& Diagnostic) { return Diagnostic.Kind == TEXT("reimport_partial_sync"); }));
	const FDesign2UMGCommitResult Saved = FUMGGenerator::SaveWidgetBlueprint(Second, &Output, &Navigation);
	TestEqual(TEXT("Dependency-only degradation reaches the batch commit"), Saved.Status, EDesign2UMGCommitStatus::SucceededWithDegradation);
	TestEqual(TEXT("Dependency save also reports degradation"), Output.Components[0].Output->Commit.Status,
		EDesign2UMGCommitStatus::SucceededWithDegradation);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentOverrideInputDegradationTest,
	"Design2UMG.ComponentBlueprints.OverrideInputDegradation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FComponentOverrideInputDegradationTest::RunTest(const FString&)
{
	TSharedPtr<FJsonObject> Json;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(MakeComponentImportJson(TEXT("Cancel"))), Json)) return false;
	const auto Outer = Json->GetObjectField(TEXT("document"))->GetArrayField(TEXT("children"))[0]->AsObject();
	const auto Label = Outer->GetArrayField(TEXT("children"))[0];
	TSharedRef<FJsonObject> Override = MakeShared<FJsonObject>();
	Override->SetStringField(TEXT("id"), TEXT("override-instance"));
	Override->SetStringField(TEXT("name"), TEXT("MappedOverride"));
	Override->SetStringField(TEXT("type"), TEXT("INSTANCE"));
	Override->SetStringField(TEXT("componentId"), TEXT("override-main"));
	Override->SetStringField(TEXT("mainComponentKey"), TEXT("override-key"));
	Override->SetObjectField(TEXT("absoluteBoundingBox"), Label->AsObject()->GetObjectField(TEXT("absoluteBoundingBox")));
	Override->SetArrayField(TEXT("children"), {Label});
	Outer->SetArrayField(TEXT("children"), {MakeShared<FJsonValueObject>(Override)});
	TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
	Value->SetStringField(TEXT("type"), TEXT("TEXT"));
	Value->SetStringField(TEXT("value"), TEXT("invalid-bool"));
	TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
	Properties->SetObjectField(TEXT("label"), Value);
	Override->SetObjectField(TEXT("componentProperties"), Properties);
	FString Source;
	FJsonSerializer::Serialize(Json.ToSharedRef(), TJsonWriterFactory<>::Create(&Source));
	FDesignDocument Document;
	if (!TestTrue(TEXT("Parse mapped instance override"), FFigmaParser::ParseDocument(Source, Document))) return false;
	FDesign2UMGGenerationSettingsSnapshot Settings;
	auto& Entry = Settings.ComponentMapping.Table.Entries.AddDefaulted_GetRef();
	Entry.Provider = TEXT("figma");
	Entry.DocumentScope = TEXT("component-blueprint-test");
	Entry.ComponentKey = TEXT("override-key");
	Entry.Key = Entry.Provider + TEXT("|") + Entry.DocumentScope + TEXT("|") + Entry.ComponentKey;
	Entry.WidgetClass = UDesign2UMGComponentInputTestWidget::StaticClass()->GetPathName();
	auto& Input = Entry.Inputs.AddDefaulted_GetRef();
	Input.Source = TEXT("label");
	Input.Target = TEXT("BoolValue");
	Input.Type = DesignComponentMapping::EComponentInputType::Bool;
	Input.ValueKind = DesignComponentMapping::EComponentInputValueKind::ComponentProperty;
	const FString Folder = TEXT("/Game/Design2UMG/Automation/OverrideInput_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	ON_SCOPE_EXIT { Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(Folder); };
	FDesign2UMGGenerationOutput Output;
	UWidgetBlueprint* Blueprint = DesignImportUtils::GenerateWidgetBlueprint(
		FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults()), Folder, &Settings, &Output);
	if (!TestNotNull(TEXT("Skipped override input does not fail generation"), Blueprint)) return false;
	UDesign2UMGComponentInputTestWidget* Mapped = nullptr;
	Blueprint->WidgetTree->ForEachWidget([&](UWidget* Widget)
	{
		if (auto* Candidate = Cast<UDesign2UMGComponentInputTestWidget>(Widget)) Mapped = Candidate;
	});
	if (!TestNotNull(TEXT("Override actually materializes the mapped class"), Mapped)) return false;
	TestTrue(TEXT("Invalid override input preserves default bool"), Mapped->BoolValue);
	TestTrue(TEXT("Override input diagnostic reaches root output"), Output.MappingDiagnostics.ContainsByPredicate(
		[](const FDesign2UMGMappingDiagnostic& Diagnostic) { return Diagnostic.Kind == TEXT("mapping_input_skipped"); }));
	TestTrue(TEXT("Override input degradation reaches root output"), Output.bHasDegradation);
	TestEqual(TEXT("Override input loss reaches final commit"), Output.Commit.Status, EDesign2UMGCommitStatus::SucceededWithDegradation);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentBuildOptionalOutputTest,
	"Design2UMG.ComponentBlueprints.BuildWithoutOutput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FComponentBuildOptionalOutputTest::RunTest(const FString&)
{
	FDesignDocument Document;
	if (!TestTrue(TEXT("Parse component source"), FFigmaParser::ParseDocument(MakeComponentImportJson(TEXT("Cancel")), Document))) return false;
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	UWidgetBlueprint* Blueprint = DesignImportUtils::BuildWidgetBlueprint(Data,
		TEXT("/Game/Design2UMG/Automation/OptionalOutput_") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
	if (!TestNotNull(TEXT("Build accepts omitted optional output for component data"), Blueprint)) return false;
	const auto Instances = FindInstances(Blueprint->WidgetTree);
	TestEqual(TEXT("Unsaved parent contains both component instances"), Instances.Num(), 2);
	if (Instances.Num() == 2) TestTrue(TEXT("Both instances retain the shared child class"), Instances[0]->GetClass() == Instances[1]->GetClass());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentBlueprintSharedInstancesTest,
	"Design2UMG.ComponentBlueprints.SharedInstances", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FComponentBlueprintSharedInstancesTest::RunTest(const FString&)
{
	FDesignDocument Document;
	if (!TestTrue(TEXT("Source parses"), FFigmaParser::ParseDocument(MakeComponentImportJson(TEXT("Cancel")), Document))) return false;
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	FDesign2UMGGenerationSettingsSnapshot Settings;
	FDesign2UMGGenerationOutput Output;
	const FString Folder = TEXT("/Game/Design2UMG/Automation/ComponentBlueprints_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	UWidgetBlueprint* Blueprint = DesignImportUtils::GenerateWidgetBlueprint(Data, Folder, &Settings, &Output);
	if (!TestNotNull(TEXT("Parent blueprint generated"), Blueprint)) return false;
	const auto Templates = FindInstances(Blueprint->WidgetTree);
	const auto Snapshot = Design2UMGAutomationStructureSnapshot::BuildWidgetTreeStructureSnapshot(Blueprint->WidgetTree);
	if (!TestTrue(TEXT("Saved blueprint structure snapshot exists"), Snapshot.IsValid())) return false;
	const auto& SnapshotTexts = Snapshot->GetArrayField(TEXT("visible_texts"));
	TestEqual(TEXT("Structure snapshot counts text inside both nested instances"), SnapshotTexts.Num(), 2);
	if (!TestEqual(TEXT("Parent contains two independent UserWidget instances, not expanded components"), Templates.Num(), 2)) return false;
	TestTrue(TEXT("Both instances share one component class"), Templates[0]->GetClass() == Templates[1]->GetClass());
	for (UUserWidget* Template : Templates)
	{
		TArray<FName> Slots;
		Template->GetSlotNames(Slots);
		int32 OverrideCount = 0;
		for (FName Slot : Slots) if (Template->GetContentForSlot(Slot)) ++OverrideCount;
		TestTrue(TEXT("Saved template retains instance override bindings"), OverrideCount > 0);
		TestNull(TEXT("Text-only differences keep the shared component layout, not a full root replacement"),
			Template->GetContentForSlot(FUMGWidgetUtils::SanitizeWidgetName(TEXT("D2U_Content_0"), TEXT("NamedSlot"))));
	}
	TestTrue(TEXT("Component class uses native UUserWidget, not a plugin runtime class"), Templates[0]->GetClass()->GetSuperClass() == UUserWidget::StaticClass());
	UUserWidget* Runtime = NewObject<UUserWidget>(GEditor->GetEditorWorldContext().World(), Blueprint->GeneratedClass);
	Runtime->Initialize();
	Runtime->TakeWidget();
	const auto Instances = FindInstances(Runtime->WidgetTree);
	if (!TestEqual(TEXT("Runtime contains both component instances"), Instances.Num(), 2)) return false;
	TArray<FString> Values;
	for (UUserWidget* Instance : Instances) Values.Append(FindText(Instance->WidgetTree));
	TestTrue(TEXT("First instance keeps its text override"), Values.Contains(TEXT("Confirm")));
	TestTrue(TEXT("Second instance keeps its text override"), Values.Contains(TEXT("Cancel")));
	TestFalse(TEXT("Main component default did not overwrite instance text"), Values.Contains(TEXT("Default")));
	const FString AssetPath = Blueprint->GetPathName();
	Runtime->ReleaseSlateResources(true);
	FDesignDocument UpdatedDocument;
	FFigmaParser::ParseDocument(MakeComponentImportJson(TEXT("Updated")), UpdatedDocument);
	Data = FDesign2UMGConverter::Convert(UpdatedDocument, FDesign2UMGConversionSettings::Defaults());
	UWidgetBlueprint* Updated = DesignImportUtils::GenerateWidgetBlueprint(Data, Folder, &Settings, &Output);
	if (!TestNotNull(TEXT("Repeated import updates the asset"), Updated)) return false;
	TestEqual(TEXT("Repeated import keeps the parent asset path"), Updated->GetPathName(), AssetPath);
	UUserWidget* UpdatedRuntime = NewObject<UUserWidget>(GEditor->GetEditorWorldContext().World(), Updated->GeneratedClass);
	UpdatedRuntime->Initialize();
	UpdatedRuntime->TakeWidget();
	TArray<FString> UpdatedValues;
	for (UUserWidget* Instance : FindInstances(UpdatedRuntime->WidgetTree)) UpdatedValues.Append(FindText(Instance->WidgetTree));
	TestTrue(TEXT("Repeated import updates actual runtime text"), UpdatedValues.Contains(TEXT("Updated")));
	TestFalse(TEXT("Repeated import removes old override value"), UpdatedValues.Contains(TEXT("Cancel")));
	UpdatedRuntime->ReleaseSlateResources(true);
	TArray<uint8> Png;
	FIntPoint RenderedSize;
	FString RenderError;
	if (TestTrue(TEXT("Nested asset renders"), FUMGWidgetUtils::RenderWidgetToPng(Updated, Png, RenderedSize, &RenderError)))
	{
		TestEqual(TEXT("Parent render preserves design dimensions"), RenderedSize, FIntPoint(300, 100));
		TestTrue(TEXT("Save functional render"), FFileHelper::SaveArrayToFile(Png,
			*(FPaths::ProjectSavedDir() / TEXT("Design2UMG/ComponentBlueprint-functional.png"))));
	}
	TestTrue(TEXT("Write cross-process reload fixture"), FFileHelper::SaveStringToFile(AssetPath,
		*(FPaths::ProjectSavedDir() / TEXT("Design2UMG/ComponentBlueprintReload.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentBlueprintReloadTest,
	"Design2UMG.ComponentBlueprintPersistence.Reload", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FComponentBlueprintReloadTest::RunTest(const FString&)
{
	FString AssetPath;
	if (!TestTrue(TEXT("Previous process supplied saved asset"), FFileHelper::LoadFileToString(AssetPath,
		*(FPaths::ProjectSavedDir() / TEXT("Design2UMG/ComponentBlueprintReload.txt"))))) return false;
	UWidgetBlueprint* Blueprint = LoadObject<UWidgetBlueprint>(nullptr, *AssetPath);
	if (!TestNotNull(TEXT("Load saved parent and dependency classes"), Blueprint)) return false;
	UUserWidget* Runtime = NewObject<UUserWidget>(GEditor->GetEditorWorldContext().World(), Blueprint->GeneratedClass);
	Runtime->Initialize();
	Runtime->TakeWidget();
	const auto Instances = FindInstances(Runtime->WidgetTree);
	TestEqual(TEXT("Reload preserves two child instances"), Instances.Num(), 2);
	TArray<FString> Values;
	for (UUserWidget* Instance : Instances) Values.Append(FindText(Instance->WidgetTree));
	TestTrue(TEXT("First persisted override executes"), Values.Contains(TEXT("Confirm")));
	TestTrue(TEXT("Updated persisted override executes"), Values.Contains(TEXT("Updated")));
	TestFalse(TEXT("Default text does not replace persisted overrides"), Values.Contains(TEXT("Default")));
	if (Instances.Num() == 2) TestTrue(TEXT("Reload retains shared component class"), Instances[0]->GetClass() == Instances[1]->GetClass());
	Runtime->ReleaseSlateResources(true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentBlueprintNestedTest,
	"Design2UMG.ComponentBlueprints.NestedDependencies", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FComponentBlueprintNestedTest::RunTest(const FString&)
{
	TSharedPtr<FJsonObject> Json;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(MakeComponentImportJson(TEXT("Cancel"))), Json);
	const auto Bundle = Json->GetObjectField(TEXT("design2umgComponentDependencies"));
	TArray<TSharedPtr<FJsonValue>> Definitions = Bundle->GetArrayField(TEXT("definitions"));
	const auto Definition = Definitions[0]->AsObject()->GetObjectField(TEXT("document"));
	const auto DefaultLabel = Definition->GetArrayField(TEXT("children"))[0];
	auto Wrap = [](TSharedPtr<FJsonValue> Label, const FString& Id)
	{
		TSharedRef<FJsonObject> Instance = MakeShared<FJsonObject>();
		Instance->SetStringField(TEXT("id"), Id);
		Instance->SetStringField(TEXT("name"), TEXT("Nested"));
		Instance->SetStringField(TEXT("type"), TEXT("INSTANCE"));
		Instance->SetStringField(TEXT("componentId"), TEXT("inner"));
		Instance->SetStringField(TEXT("mainComponentKey"), TEXT("inner-key"));
		Instance->SetObjectField(TEXT("absoluteBoundingBox"), Label->AsObject()->GetObjectField(TEXT("absoluteBoundingBox")));
		Instance->SetArrayField(TEXT("children"), {Label});
		return MakeShared<FJsonValueObject>(Instance);
	};
	TSharedRef<FJsonObject> InnerDocument = MakeShared<FJsonObject>();
	InnerDocument->SetStringField(TEXT("id"), TEXT("inner"));
	InnerDocument->SetStringField(TEXT("name"), TEXT("Button")); // Same display name, different identity.
	InnerDocument->SetStringField(TEXT("type"), TEXT("COMPONENT"));
	InnerDocument->SetObjectField(TEXT("absoluteBoundingBox"), DefaultLabel->AsObject()->GetObjectField(TEXT("absoluteBoundingBox")));
	InnerDocument->SetArrayField(TEXT("children"), {DefaultLabel});
	TSharedRef<FJsonObject> InnerDefinition = MakeShared<FJsonObject>();
	InnerDefinition->SetStringField(TEXT("id"), TEXT("inner"));
	InnerDefinition->SetStringField(TEXT("key"), TEXT("inner-key"));
	InnerDefinition->SetObjectField(TEXT("document"), InnerDocument);
	Definitions.Add(MakeShared<FJsonValueObject>(InnerDefinition));
	Bundle->SetArrayField(TEXT("definitions"), Definitions);
	Definition->SetArrayField(TEXT("children"), {Wrap(DefaultLabel, TEXT("nested-default"))});
	for (const auto& Parent : Json->GetObjectField(TEXT("document"))->GetArrayField(TEXT("children")))
	{
		const auto Object = Parent->AsObject();
		Object->SetArrayField(TEXT("children"), {Wrap(Object->GetArrayField(TEXT("children"))[0], Object->GetStringField(TEXT("id")) + TEXT("-inner"))});
	}
	FString Source;
	FJsonSerializer::Serialize(Json.ToSharedRef(), TJsonWriterFactory<>::Create(&Source));
	FDesignDocument Document;
	if (!TestTrue(TEXT("Nested fixture parses"), FFigmaParser::ParseDocument(Source, Document))) return false;
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	FDesign2UMGGenerationSettingsSnapshot Settings;
	FDesign2UMGGenerationOutput Output;
	const FString Folder = TEXT("/Game/Design2UMG/Automation/Nested_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	UWidgetBlueprint* Blueprint = DesignImportUtils::GenerateWidgetBlueprint(Data, Folder, &Settings, &Output);
	if (!TestNotNull(TEXT("Nested parent generated"), Blueprint)) return false;
	TestEqual(TEXT("Same-named distinct components produce two assets"), Output.Components.Num(), 2);
	if (Output.Components.Num() == 2)
	{
		TestTrue(TEXT("Distinct identities have distinct asset paths"), Output.Components[0].Blueprint->GetPathName() != Output.Components[1].Blueprint->GetPathName());
	}
	UUserWidget* Runtime = NewObject<UUserWidget>(GEditor->GetEditorWorldContext().World(), Blueprint->GeneratedClass);
	Runtime->Initialize();
	Runtime->TakeWidget();
	TSet<UUserWidget*> Visited;
	TArray<FString> Values;
	TFunction<void(UUserWidget*)> Visit = [&](UUserWidget* Widget)
	{
		if (Visited.Contains(Widget)) return;
		Visited.Add(Widget);
		Values.Append(FindText(Widget->WidgetTree));
		for (UUserWidget* Child : FindInstances(Widget->WidgetTree)) Visit(Child);
	};
	Visit(Runtime);
	TestEqual(TEXT("Root plus two outer and two inner runtime instances"), Visited.Num(), 5);
	TestTrue(TEXT("Nested first override survives"), Values.Contains(TEXT("Confirm")));
	TestTrue(TEXT("Nested second override survives"), Values.Contains(TEXT("Cancel")));
	Runtime->ReleaseSlateResources(true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentReadOnlyBulkSaveTest,
	"Design2UMG.ComponentBlueprints.ReadOnlyBulkSave", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FComponentReadOnlyBulkSaveTest::RunTest(const FString&)
{
	FDesignDocument Document;
	if (!FFigmaParser::ParseDocument(MakeComponentImportJson(TEXT("Before")), Document)) return false;
	FDesign2UMGImportSettingsSnapshot Settings;
	Settings.ConversionSettings = FDesign2UMGConversionSettings::Defaults();
	const FString Folder = TEXT("/Game/Design2UMG/Automation/ReadOnlyBulk_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	FDesign2UMGGenerationOutput Output;
	UWidgetBlueprint* Root = DesignImportUtils::GenerateWidgetBlueprint(
		FDesign2UMGConverter::Convert(Document, Settings.ConversionSettings), Folder, &Settings.GenerationSettings, &Output);
	if (!TestNotNull(TEXT("Initial batch saved"), Root) || !TestEqual(TEXT("One dependency saved"), Output.Components.Num(), 1)) return false;
	const FString RootPackage = Root->GetOutermost()->GetName();
	const FString ChildPackage = Output.Components[0].Blueprint->GetOutermost()->GetName();
	const FString RootHash = Design2UMGGenerationManifest::ComputeSavedAssetHash(RootPackage);
	const FString ChildHash = Design2UMGGenerationManifest::ComputeSavedAssetHash(ChildPackage);
	const FString Sidecar = Design2UMGGenerationManifest::BuildSidecarPathForAsset(ChildPackage);
	ON_SCOPE_EXIT { FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*Sidecar, false); };
	if (!TestTrue(TEXT("Dependency sidecar made read-only"), FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*Sidecar, true))) return false;
	FDesignDocument UpdatedDocument;
	if (!FFigmaParser::ParseDocument(MakeComponentImportJson(TEXT("After")), UpdatedDocument)) return false;
	const int32 UndoEntriesBefore = GEditor->Trans->GetQueueLength() - GEditor->Trans->GetUndoCount();
	FDesign2UMGGenerationOutput UpdatedOutput;
	TestNull(TEXT("Read-only dependency prevents batch save"), DesignImportUtils::GenerateWidgetBlueprint(
		FDesign2UMGConverter::Convert(UpdatedDocument, Settings.ConversionSettings), Folder, &Settings.GenerationSettings, &UpdatedOutput));
	TestTrue(TEXT("Bulk preflight identifies the read-only dependency file"), UpdatedOutput.MappingDiagnostics.ContainsByPredicate(
		[&](const FDesign2UMGMappingDiagnostic& Diagnostic)
		{
			return Diagnostic.Kind == TEXT("component_save_blocked") && Diagnostic.Message.Contains(Sidecar)
				&& Diagnostic.Message.Contains(TEXT("read-only"));
		}));
	TestEqual(TEXT("Root saved content unchanged"), Design2UMGGenerationManifest::ComputeSavedAssetHash(RootPackage), RootHash);
	TestEqual(TEXT("Child saved content unchanged"), Design2UMGGenerationManifest::ComputeSavedAssetHash(ChildPackage), ChildHash);
	TestEqual(TEXT("Failed batch does not commit an undo entry"),
		GEditor->Trans->GetQueueLength() - GEditor->Trans->GetUndoCount(), UndoEntriesBefore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentBlueprintReimportTest,
	"Design2UMG.ComponentBlueprints.ReimportAndConflicts", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FComponentBlueprintReimportTest::RunTest(const FString&)
{
	FDesignDocument Document;
	FFigmaParser::ParseDocument(MakeComponentImportJson(TEXT("Cancel")), Document);
	FDesign2UMGImportSettingsSnapshot Settings;
	Settings.ConversionSettings = FDesign2UMGConversionSettings::Defaults();
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, Settings.ConversionSettings);
	const FString Folder = TEXT("/Game/Design2UMG/Automation/ReimportComponents_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	FDesign2UMGGenerationOutput Output;
	UWidgetBlueprint* Blueprint = DesignImportUtils::GenerateWidgetBlueprint(Data, Folder, &Settings.GenerationSettings, &Output);
	if (!TestNotNull(TEXT("Initial component batch generated"), Blueprint)) return false;
	FDesignDocument UpdatedDocument;
	FFigmaParser::ParseDocument(MakeComponentImportJson(TEXT("Reimported")), UpdatedDocument);
	if (!TestEqual(TEXT("Fixture generated one reusable component"), Output.Components.Num(), 1)) return false;
	UWidgetBlueprint* ChildBlueprint = Output.Components[0].Blueprint;
	const FString ChildPackage = ChildBlueprint->GetOutermost()->GetName();
	const FString ChildHashBefore = Design2UMGGenerationManifest::ComputeSavedAssetHash(ChildPackage);
	FDesignReimportRequest ChildRequest;
	ChildRequest.TargetAssetPath = ChildBlueprint->GetPathName();
	ChildRequest.SettingsSnapshot = Settings;
	const FDesignReimportResult ChildResult = DesignImportPipeline::Reimport(UpdatedDocument, ChildRequest);
	TestFalse(TEXT("Child asset is not treated as a component batch root"), ChildResult.bSucceeded);
	TestTrue(TEXT("Child reimport explains the required batch-root entry"), ChildResult.Diagnostics.ContainsByPredicate(
		[](const FDesign2UMGMappingDiagnostic& Diagnostic) { return Diagnostic.Kind == TEXT("reimport_component_batch_root_required"); }));
	TestEqual(TEXT("Rejected child entry preserves its saved asset"),
		Design2UMGGenerationManifest::ComputeSavedAssetHash(ChildPackage), ChildHashBefore);
	FDesignReimportRequest Request;
	Request.TargetAssetPath = Blueprint->GetPathName();
	Request.SettingsSnapshot = Settings;
	const FDesignReimportResult Result = DesignImportPipeline::Reimport(UpdatedDocument, Request);
	if (!TestTrue(TEXT("Existing Reimport API updates component batch"), Result.bSucceeded)) return false;
	TestTrue(TEXT("Reimport preserves original blueprint object"), Result.UpdatedBlueprint == Blueprint);
	TestTrue(TEXT("Nested preservation is diagnosed in the component batch"), Result.Diagnostics.ContainsByPredicate(
		[](const FDesign2UMGMappingDiagnostic& Diagnostic) { return Diagnostic.Kind == TEXT("reimport_partial_sync"); }));
	TestTrue(TEXT("Component batch preserves the apply degradation signal"), Result.bHasDegradation);
	TestEqual(TEXT("Partial component synchronization is not ordinary success"), Result.Commit.Status,
		EDesign2UMGCommitStatus::SucceededWithDegradation);
	UUserWidget* Runtime = NewObject<UUserWidget>(GEditor->GetEditorWorldContext().World(), Blueprint->GeneratedClass);
	Runtime->Initialize();
	Runtime->TakeWidget();
	TArray<FString> Values;
	for (UUserWidget* Child : FindInstances(Runtime->WidgetTree)) Values.Append(FindText(Child->WidgetTree));
	TestTrue(TEXT("Reimported value executes"), Values.Contains(TEXT("Reimported")));
	Runtime->ReleaseSlateResources(true);
	const FString SavedPath = Blueprint->GetPathName();
	const FString SavedPackage = Blueprint->GetOutermost()->GetName();
	const FString SavedHash = Design2UMGGenerationManifest::ComputeSavedAssetHash(SavedPackage);
	FDesignDocument FailedUpdate;
	TSharedPtr<FJsonObject> FailedJson;
	TSharedPtr<FJsonObject> ExtraJson;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(MakeComponentImportJson(TEXT("ShouldRollback"))), FailedJson);
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(MakeComponentImportJson(TEXT("Extra"))
		.Replace(TEXT("button-key"), TEXT("extra-key")).Replace(TEXT("\"main\""), TEXT("\"extra\""))
		.Replace(TEXT("\"a\""), TEXT("\"c\"")).Replace(TEXT("Ia;label"), TEXT("Ic;label"))), ExtraJson);
	const auto FailedPage = FailedJson->GetObjectField(TEXT("document"));
	TArray<TSharedPtr<FJsonValue>> FailedChildren = FailedPage->GetArrayField(TEXT("children"));
	FailedChildren.Add(ExtraJson->GetObjectField(TEXT("document"))->GetArrayField(TEXT("children"))[0]);
	FailedPage->SetArrayField(TEXT("children"), FailedChildren);
	const auto FailedCatalog = FailedJson->GetObjectField(TEXT("design2umgComponentDependencies"));
	const auto ExtraCatalog = ExtraJson->GetObjectField(TEXT("design2umgComponentDependencies"));
	TArray<TSharedPtr<FJsonValue>> FailedDefinitions = FailedCatalog->GetArrayField(TEXT("definitions"));
	FailedDefinitions[0]->AsObject()->GetObjectField(TEXT("document"))->GetArrayField(TEXT("children"))[0]
		->AsObject()->SetStringField(TEXT("characters"), TEXT("Changed default"));
	FailedDefinitions.Add(ExtraCatalog->GetArrayField(TEXT("definitions"))[0]);
	FailedCatalog->SetArrayField(TEXT("definitions"), FailedDefinitions);
	FailedCatalog->GetObjectField(TEXT("instances"))->SetObjectField(TEXT("c"), ExtraCatalog->GetObjectField(TEXT("instances"))->GetObjectField(TEXT("c")));
	FString FailedSource;
	FJsonSerializer::Serialize(FailedJson.ToSharedRef(), TJsonWriterFactory<>::Create(&FailedSource));
	FFigmaParser::ParseDocument(FailedSource, FailedUpdate);
	const FString SavedChildHash = Design2UMGGenerationManifest::ComputeSavedAssetHash(ChildPackage);
	UTextBlock* UndoProbe = NewObject<UTextBlock>(GetTransientPackage(), NAME_None, RF_Transactional);
	UndoProbe->AddToRoot();
	ON_SCOPE_EXIT { UndoProbe->RemoveFromRoot(); };
	UndoProbe->SetText(FText::FromString(TEXT("Before user edit")));
	GEditor->BeginTransaction(TEXT("User edit fixture"), FText::FromString(TEXT("User edit fixture")), UndoProbe);
	UndoProbe->Modify();
	UndoProbe->SetText(FText::FromString(TEXT("After user edit")));
	GEditor->EndTransaction();
	const int32 UndoEntriesBeforeSaveFailure = GEditor->Trans->GetQueueLength() - GEditor->Trans->GetUndoCount();
	const int32 UndoCountBeforeSaveFailure = GEditor->Trans->GetUndoCount();
	Design2UMGEditorTests::FScopedFileFailures FileFailures;
	FileFailures.RejectWrite = [](const FString& Path) { return Path.EndsWith(TEXT(".d2u.json.tmp")); };
	AddExpectedError(TEXT("GenerationManifest: failed to write temp manifest"), EAutomationExpectedErrorFlags::Contains, 1);
	const FDesignReimportResult SaveFailure = DesignImportPipeline::Reimport(FailedUpdate, Request);
	TestFalse(TEXT("Sidecar save failure fails the complete batch"), SaveFailure.bSucceeded);
	TestTrue(TEXT("Bulk did not commit"), SaveFailure.Commit.Status == EDesign2UMGCommitStatus::NotCommitted);
	TestTrue(TEXT("Asset files restored"), SaveFailure.Commit.Asset == EDesign2UMGFileState::Restored);
	TestTrue(TEXT("Manifests restored"), SaveFailure.Commit.Manifest == EDesign2UMGFileState::Restored);
	TestTrue(TEXT("Memory restored independently"), SaveFailure.Commit.Memory == EDesign2UMGMemoryState::Restored);
	TestEqual(TEXT("Failed batch reimport leaves no committed undo entry"),
		GEditor->Trans->GetQueueLength() - GEditor->Trans->GetUndoCount(), UndoEntriesBeforeSaveFailure);
	TestEqual(TEXT("Failed batch is not available for redo"), GEditor->Trans->GetUndoCount(), UndoCountBeforeSaveFailure);
	TestEqual(TEXT("Failed batch restores the exact original parent file"), Design2UMGGenerationManifest::ComputeSavedAssetHash(SavedPackage), SavedHash);
	TestEqual(TEXT("Failed batch restores the exact original dependency file"), Design2UMGGenerationManifest::ComputeSavedAssetHash(ChildPackage), SavedChildHash);
	Blueprint = LoadObject<UWidgetBlueprint>(nullptr, *SavedPath);
	if (!TestNotNull(TEXT("Restored parent reloads"), Blueprint)) return false;
	UUserWidget* RestoredRuntime = NewObject<UUserWidget>(GEditor->GetEditorWorldContext().World(), Blueprint->GeneratedClass);
	RestoredRuntime->Initialize();
	RestoredRuntime->TakeWidget();
	TArray<FString> RestoredValues;
	for (UUserWidget* Child : FindInstances(RestoredRuntime->WidgetTree)) RestoredValues.Append(FindText(Child->WidgetTree));
	TestTrue(TEXT("Restored batch executes its original override"), RestoredValues.Contains(TEXT("Reimported")));
	TestFalse(TEXT("Failed update did not leak into runtime"), RestoredValues.Contains(TEXT("ShouldRollback")));
	RestoredRuntime->ReleaseSlateResources(true);
	UUserWidget* RestoredDefault = NewObject<UUserWidget>(GEditor->GetEditorWorldContext().World(), ChildBlueprint->GeneratedClass);
	RestoredDefault->Initialize();
	RestoredDefault->TakeWidget();
	TestTrue(TEXT("Changed component definition is rolled back in memory"), FindText(RestoredDefault->WidgetTree).Contains(TEXT("Default")));
	RestoredDefault->ReleaseSlateResources(true);
	TestEqual(TEXT("Bulk failure preserves unrelated user edit"), UndoProbe->GetText().ToString(), FString(TEXT("After user edit")));
	TestTrue(TEXT("Earlier user edit still supports undo"), GEditor->UndoTransaction(false));
	TestEqual(TEXT("Earlier undo restores its actual value"), UndoProbe->GetText().ToString(), FString(TEXT("Before user edit")));
	TestFalse(TEXT("No batch transaction remains open"), GEditor->IsTransactionActive());
	Blueprint->Modify();
	Blueprint->BlueprintDescription = TEXT("User-authored description");
	Blueprint->MarkPackageDirty();
	AddExpectedError(TEXT("Component import failed: Generated asset conflict"), EAutomationExpectedErrorFlags::Contains, 1);
	const FDesignReimportResult Conflict = DesignImportPipeline::Reimport(UpdatedDocument, Request);
	TestFalse(TEXT("Unsaved user edit blocks regeneration"), Conflict.bSucceeded);
	TestTrue(TEXT("Reimport reports an ownership conflict distinctly from generation failure"), Conflict.bFullConflict);
	TestEqual(TEXT("User-authored value preserved"), Blueprint->BlueprintDescription, FString(TEXT("User-authored description")));
	TSharedPtr<FJsonObject> Json;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(MakeComponentImportJson(TEXT("Cancel"))), Json);
	Json->GetObjectField(TEXT("design2umgComponentDependencies"))->SetArrayField(TEXT("definitions"), {});
	FString MissingSource;
	FJsonSerializer::Serialize(Json.ToSharedRef(), TJsonWriterFactory<>::Create(&MissingSource));
	FDesignDocument MissingDocument;
	FFigmaParser::ParseDocument(MissingSource, MissingDocument);
	Data = FDesign2UMGConverter::Convert(MissingDocument, Settings.ConversionSettings);
	AddExpectedError(TEXT("Component import failed:"), EAutomationExpectedErrorFlags::Contains, 1);
	TestNull(TEXT("Missing dependency fails rather than flattening"), DesignImportUtils::GenerateWidgetBlueprint(Data, Folder / TEXT("Missing"), &Settings.GenerationSettings, &Output));
	TestTrue(TEXT("Missing dependency diagnostic identifies the component and reason"),
		Output.MappingDiagnostics.ContainsByPredicate([](const FDesign2UMGMappingDiagnostic& Diagnostic)
		{
			return Diagnostic.Kind == TEXT("component_generation_failed")
				&& Diagnostic.Message.Contains(TEXT("button-key"))
				&& Diagnostic.Message.Contains(TEXT("missing or inaccessible"));
		}));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentResourceDiagnosticTest,
	"Design2UMG.ComponentBlueprints.ResourceFailureDiagnostics", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FComponentResourceDiagnosticTest::RunTest(const FString&)
{
	TSharedPtr<FJsonObject> Json, Effect;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(MakeComponentImportJson(TEXT("Cancel"))), Json) ||
		!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(TEXT(R"JSON({"type":"DROP_SHADOW","visible":true,"radius":0,"spread":1,"offset":{"x":1,"y":1},"color":{"r":0,"g":0,"b":0,"a":1}})JSON"))), Effect)) return false;
	Json->GetObjectField(TEXT("design2umgComponentDependencies"))->GetArrayField(TEXT("definitions"))[0]->AsObject()
		->GetObjectField(TEXT("document"))->SetArrayField(TEXT("effects"), {MakeShared<FJsonValueObject>(Effect)});
	FString Source;
	FJsonSerializer::Serialize(Json.ToSharedRef(), TJsonWriterFactory<>::Create(&Source));
	FDesignDocument Document;
	if (!TestTrue(TEXT("Component effect fixture parses"), FFigmaParser::ParseDocument(Source, Document))) return false;
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	FDesign2UMGGenerationSettingsSnapshot Settings;
	FDesign2UMGGenerationOutput Output;
	AddExpectedError(TEXT("uses spread, which is not yet represented"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("Component import failed:"), EAutomationExpectedErrorFlags::Contains, 1);
	TestNull(TEXT("A child resource failure fails the whole import"), DesignImportUtils::GenerateWidgetBlueprint(Data,
		TEXT("/Game/Design2UMG/Automation/ComponentResourceFailure_") + FGuid::NewGuid().ToString(EGuidFormats::Digits), &Settings, &Output));
	TestTrue(TEXT("Child resource report retains component identity and actual cause"),
		Output.MappingDiagnostics.ContainsByPredicate([](const FDesign2UMGMappingDiagnostic& Diagnostic)
		{
			return Diagnostic.Kind == TEXT("resource_preparation_failed") && Diagnostic.Message.Contains(TEXT("button-key"))
				&& Diagnostic.Message.Contains(TEXT("uses spread"));
		}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentBlueprintBuildFailureTest,
	"Design2UMG.ComponentBlueprints.BuildFailureRestoresExistingBulk", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FComponentBlueprintBuildFailureTest::RunTest(const FString&)
{
	FDesignDocument Document;
	if (!FFigmaParser::ParseDocument(MakeComponentImportJson(TEXT("Original")), Document)) return false;
	FDesign2UMGImportSettingsSnapshot Settings;
	Settings.ConversionSettings = FDesign2UMGConversionSettings::Defaults();
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, Settings.ConversionSettings);
	const FString Folder = TEXT("/Game/Design2UMG/Automation/BuildFailureComponents_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	FDesign2UMGGenerationOutput Output;
	UWidgetBlueprint* Blueprint = DesignImportUtils::GenerateWidgetBlueprint(Data, Folder, &Settings.GenerationSettings, &Output);
	if (!TestNotNull(TEXT("Original batch saved"), Blueprint)) return false;
	const FString SavedPath = Blueprint->GetPathName();
	const FString SavedPackage = Blueprint->GetOutermost()->GetName();
	const FString SavedHash = Design2UMGGenerationManifest::ComputeSavedAssetHash(SavedPackage);
	Data.RootWidget.Bounds.W = 0.0f;
	AddExpectedError(TEXT("Component import failed"), EAutomationExpectedErrorFlags::Contains, 1);
	FDesign2UMGGenerationOutput FailedOutput;
	TestNull(TEXT("Invalid root fails after dependencies were built"),
		DesignImportUtils::GenerateWidgetBlueprint(Data, Folder, &Settings.GenerationSettings, &FailedOutput));
	TestTrue(TEXT("Invalid root report retains the source node and concrete reason"),
		FailedOutput.MappingDiagnostics.ContainsByPredicate([](const FDesign2UMGMappingDiagnostic& Diagnostic)
		{
			return Diagnostic.Kind == TEXT("invalid_root_design_size") && Diagnostic.Message.Contains(TEXT("source node 'root'"))
				&& Diagnostic.Message.Contains(TEXT("valid root design size"));
		}));
	Blueprint = LoadObject<UWidgetBlueprint>(nullptr, *SavedPath);
	if (!TestNotNull(TEXT("Original parent remains available"), Blueprint)) return false;
	TestNotNull(TEXT("Failed build restores original parent tree in memory"), Blueprint->WidgetTree->RootWidget.Get());
	TestFalse(TEXT("Failed build leaves no dirty parent awaiting accidental save"), Blueprint->GetOutermost()->IsDirty());
	TestEqual(TEXT("Failed build preserves saved parent"), Design2UMGGenerationManifest::ComputeSavedAssetHash(SavedPackage), SavedHash);
	TestTrue(TEXT("Failed build releases partial dependency output"), FailedOutput.Components.IsEmpty());
	if (Blueprint->WidgetTree->RootWidget)
	{
		UUserWidget* Runtime = NewObject<UUserWidget>(GEditor->GetEditorWorldContext().World(), Blueprint->GeneratedClass);
		Runtime->Initialize();
		Runtime->TakeWidget();
		TArray<FString> Values;
		for (UUserWidget* Child : FindInstances(Runtime->WidgetTree)) Values.Append(FindText(Child->WidgetTree));
		TestTrue(TEXT("Restored compiled parent retains original nested override"), Values.Contains(TEXT("Original")));
		Runtime->ReleaseSlateResources(true);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentBlueprintNewSaveFailureTest,
	"Design2UMG.ComponentBlueprints.NewSaveFailureRemovesPartialAssets", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FComponentBlueprintNewSaveFailureTest::RunTest(const FString&)
{
	FDesignDocument Document;
	if (!FFigmaParser::ParseDocument(MakeComponentImportJson(TEXT("Cancel")), Document)) return false;
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	const FString Folder = TEXT("/Game/Design2UMG/Automation/NewBulkSaveFailure_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	FDesign2UMGGenerationSettingsSnapshot Settings;
	FDesign2UMGGenerationOutput Output;
	Design2UMGEditorTests::FScopedFileFailures FileFailures;
	FileFailures.RejectWrite = [](const FString& Path) { return Path.EndsWith(TEXT(".d2u.json.tmp")); };
	AddExpectedError(TEXT("GenerationManifest: failed to write temp manifest"), EAutomationExpectedErrorFlags::Contains, 1);
	TestNull(TEXT("Failed new batch is not returned as success"), DesignImportUtils::GenerateWidgetBlueprint(Data, Folder, &Settings, &Output));
	TArray<FAssetData> Assets;
	FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssetsByPath(FName(*Folder), Assets, true);
	TestTrue(TEXT("Failed new batch leaves no phantom asset registry entries"), Assets.IsEmpty());
	TestTrue(TEXT("Failed new batch releases partial dependency output"), Output.Components.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentBlueprintVariantsTest,
	"Design2UMG.ComponentBlueprints.VariantsVisibilityAndSwap", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FComponentBlueprintVariantsTest::RunTest(const FString&)
{
	TSharedPtr<FJsonObject> Json;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(MakeComponentImportJson(TEXT("Cancel"))), Json);
	const auto Bundle = Json->GetObjectField(TEXT("design2umgComponentDependencies"));
	TArray<TSharedPtr<FJsonValue>> Definitions = Bundle->GetArrayField(TEXT("definitions"));
	Definitions[0]->AsObject()->SetStringField(TEXT("componentSetId"), TEXT("button-set"));
	Definitions[0]->AsObject()->SetStringField(TEXT("componentSetKey"), TEXT("published-button-set"));
	FString Clone;
	FJsonSerializer::Serialize(Definitions[0]->AsObject().ToSharedRef(), TJsonWriterFactory<>::Create(&Clone));
	TSharedPtr<FJsonObject> Variant;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Clone), Variant);
	Variant->SetStringField(TEXT("id"), TEXT("main-variant"));
	Variant->SetStringField(TEXT("key"), TEXT("variant-key"));
	Variant->GetObjectField(TEXT("document"))->SetStringField(TEXT("id"), TEXT("main-variant"));
	Definitions.Add(MakeShared<FJsonValueObject>(Variant));
	Bundle->SetArrayField(TEXT("definitions"), Definitions);
	const auto Children = Json->GetObjectField(TEXT("document"))->GetArrayField(TEXT("children"));
	for (const auto& Child : Children) Child->AsObject()->SetStringField(TEXT("componentSetId"), TEXT("button-set"));
	for (const auto& Child : Children) Child->AsObject()->SetStringField(TEXT("componentSetKey"), TEXT("published-button-set"));
	Children[1]->AsObject()->SetStringField(TEXT("componentId"), TEXT("main-variant"));
	Children[1]->AsObject()->SetStringField(TEXT("mainComponentKey"), TEXT("variant-key"));
	Bundle->GetObjectField(TEXT("instances"))->GetObjectField(TEXT("b"))->SetStringField(TEXT("id"), TEXT("main-variant"));
	Bundle->GetObjectField(TEXT("instances"))->GetObjectField(TEXT("b"))->SetStringField(TEXT("key"), TEXT("variant-key"));
	const FString Folder = TEXT("/Game/Design2UMG/Automation/Variants_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	FDesign2UMGGenerationSettingsSnapshot Settings;
	FDesign2UMGGenerationOutput Output;
	auto Generate = [&]() -> UWidgetBlueprint*
	{
		FString Source;
		FJsonSerializer::Serialize(Json.ToSharedRef(), TJsonWriterFactory<>::Create(&Source));
		FDesignDocument Document;
		if (!FFigmaParser::ParseDocument(Source, Document)) return nullptr;
		return DesignImportUtils::GenerateWidgetBlueprint(FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults()), Folder, &Settings, &Output);
	};
	UWidgetBlueprint* Blueprint = Generate();
	if (!TestNotNull(TEXT("Variant batch generated"), Blueprint)) return false;
	TestEqual(TEXT("Two variants have two assets"), Output.Components.Num(), 2);
	if (Output.Components.Num() != 2) return false;
	TestEqual(TEXT("Variants are grouped in one set directory"), FPaths::GetPath(Output.Components[0].Blueprint->GetOutermost()->GetName()), FPaths::GetPath(Output.Components[1].Blueprint->GetOutermost()->GetName()));
	const auto FirstInstances = FindInstances(Blueprint->WidgetTree);
	if (!TestEqual(TEXT("Frame remains a container, not a third component"), FirstInstances.Num(), 2)) return false;
	TestTrue(TEXT("Different variants use different classes"), FirstInstances[0]->GetClass() != FirstInstances[1]->GetClass());
	const FString RootPath = Blueprint->GetPathName();
	TArray<FString> OriginalVariantPaths;
	for (const auto& Component : Output.Components) OriginalVariantPaths.Add(Component.Blueprint->GetPathName());
	for (const auto& Definition : Definitions) Definition->AsObject()->SetStringField(TEXT("componentSetId"), TEXT("moved-button-set"));
	for (const auto& Child : Children) Child->AsObject()->SetStringField(TEXT("componentSetId"), TEXT("moved-button-set"));
	Blueprint = Generate();
	if (!TestNotNull(TEXT("Moving components to another set regenerates"), Blueprint)) return false;
	if (!TestEqual(TEXT("Moving a set keeps two dependencies"), Output.Components.Num(), OriginalVariantPaths.Num())) return false;
	for (int32 Index = 0; Index < Output.Components.Num(); ++Index)
	{
		TestEqual(TEXT("Component identity survives a changed set without another asset"),
			Output.Components[Index].Blueprint->GetPathName(), OriginalVariantPaths[Index]);
	}
	Children[1]->AsObject()->SetStringField(TEXT("componentId"), TEXT("main"));
	Children[1]->AsObject()->SetStringField(TEXT("mainComponentKey"), TEXT("button-key"));
	Bundle->GetObjectField(TEXT("instances"))->GetObjectField(TEXT("b"))->SetStringField(TEXT("id"), TEXT("main"));
	Bundle->GetObjectField(TEXT("instances"))->GetObjectField(TEXT("b"))->SetStringField(TEXT("key"), TEXT("button-key"));
	Children[1]->AsObject()->GetArrayField(TEXT("children"))[0]->AsObject()->SetBoolField(TEXT("visible"), false);
	Blueprint = Generate();
	if (!TestNotNull(TEXT("Swapped component with hidden label regenerates"), Blueprint)) return false;
	TestEqual(TEXT("Swap preserves root asset"), Blueprint->GetPathName(), RootPath);
	const auto Swapped = FindInstances(Blueprint->WidgetTree);
	if (!TestEqual(TEXT("Swap retains two instances"), Swapped.Num(), 2)) return false;
	TestTrue(TEXT("Swap selects shared target component class"), Swapped[0]->GetClass() == Swapped[1]->GetClass());
	UUserWidget* Runtime = NewObject<UUserWidget>(GEditor->GetEditorWorldContext().World(), Blueprint->GeneratedClass);
	Runtime->Initialize();
	Runtime->TakeWidget();
	TArray<FString> Values;
	for (UUserWidget* Child : FindInstances(Runtime->WidgetTree)) Values.Append(FindText(Child->WidgetTree));
	TestTrue(TEXT("Visible label remains"), Values.Contains(TEXT("Confirm")));
	TestFalse(TEXT("Hidden instance label is absent"), Values.Contains(TEXT("Cancel")));
	TestFalse(TEXT("Hidden override does not reveal the default"), Values.Contains(TEXT("Default")));
	Runtime->ReleaseSlateResources(true);
	const FString ExistingClass = Swapped[0]->GetClass()->GetPathName();
	auto& Entry = Settings.ComponentMapping.Table.Entries.AddDefaulted_GetRef();
	Entry.Provider = TEXT("figma");
	Entry.DocumentScope = TEXT("component-blueprint-test");
	Entry.ComponentKey = TEXT("published-button-set");
	Entry.Key = Entry.Provider + TEXT("|") + Entry.DocumentScope + TEXT("|") + Entry.ComponentKey;
	Entry.WidgetClass = ExistingClass;
	Blueprint = Generate();
	if (!TestNotNull(TEXT("Explicit set mapping takes precedence"), Blueprint)) return false;
	TestEqual(TEXT("User mapping prevents automatic child asset generation"), Output.Components.Num(), 0);
	for (UUserWidget* Instance : FindInstances(Blueprint->WidgetTree))
	{
		TestEqual(TEXT("Instances reference the user-selected class"), Instance->GetClass()->GetPathName(), ExistingClass);
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentBlueprintRestTest,
	"Design2UMG.ComponentBlueprints.RestRemoteDependencies", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FComponentBlueprintRestTest::RunTest(const FString&)
{
	TSharedPtr<FJsonObject> Json;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(MakeComponentImportJson(TEXT("Cancel"))), Json);
	const auto Definition = Json->GetObjectField(TEXT("design2umgComponentDependencies"))->GetArrayField(TEXT("definitions"))[0]->AsObject()->GetObjectField(TEXT("document"));
	Json->RemoveField(TEXT("design2umgComponentDependencies"));
	TSharedRef<FJsonObject> Metadata = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("key"), TEXT("button-key"));
	Metadata->SetObjectField(TEXT("main"), Entry);
	Json->SetObjectField(TEXT("components"), Metadata);
	for (const auto& Child : Json->GetObjectField(TEXT("document"))->GetArrayField(TEXT("children"))) Child->AsObject()->RemoveField(TEXT("mainComponentKey"));
	TSharedRef<FJsonObject> UnusedMetadata = MakeShared<FJsonObject>();
	UnusedMetadata->SetStringField(TEXT("key"), TEXT("unused-key"));
	Metadata->SetObjectField(TEXT("unused-main"), UnusedMetadata);
	TSharedRef<FJsonObject> UnusedInstance = MakeShared<FJsonObject>();
	UnusedInstance->SetStringField(TEXT("id"), TEXT("unused-instance"));
	UnusedInstance->SetStringField(TEXT("type"), TEXT("INSTANCE"));
	UnusedInstance->SetStringField(TEXT("componentId"), TEXT("unused-main"));
	UnusedInstance->SetStringField(TEXT("mainComponentKey"), TEXT("unused-key"));
	TSharedRef<FJsonObject> UnusedFrame = MakeShared<FJsonObject>();
	UnusedFrame->SetStringField(TEXT("id"), TEXT("unused-frame"));
	UnusedFrame->SetStringField(TEXT("type"), TEXT("FRAME"));
	UnusedFrame->SetArrayField(TEXT("children"), {MakeShared<FJsonValueObject>(UnusedInstance)});
	const TSharedPtr<FJsonObject> SelectedRoot = Json->GetObjectField(TEXT("document"));
	TSharedRef<FJsonObject> RestDocument = MakeShared<FJsonObject>();
	RestDocument->SetStringField(TEXT("id"), TEXT("rest-document"));
	RestDocument->SetStringField(TEXT("type"), TEXT("DOCUMENT"));
	RestDocument->SetArrayField(TEXT("children"), {
		MakeShared<FJsonValueObject>(SelectedRoot), MakeShared<FJsonValueObject>(UnusedFrame)});
	Json->SetObjectField(TEXT("document"), RestDocument);
	FString RootResponse;
	FJsonSerializer::Serialize(Json.ToSharedRef(), TJsonWriterFactory<>::Create(&RootResponse));
	TSharedRef<FJsonObject> Library = MakeShared<FJsonObject>();
	Library->SetObjectField(TEXT("components"), Metadata);
	Library->SetObjectField(TEXT("document"), Definition);
	FString LibraryResponse;
	FJsonSerializer::Serialize(Library, TJsonWriterFactory<>::Create(&LibraryResponse));
	const TArray<FString> Responses = {RootResponse, TEXT("{\"meta\":{\"file_key\":\"library\",\"node_id\":\"main\"}}"), LibraryResponse, TEXT("{\"meta\":{\"images\":{}}}")};
	int32 Requests = 0;
	Design2UMGEditorTests::FScopedHttpTestTransport Transport;
	Transport.StartRequest = [&](int32, const FHttpRequestPtr& Request)
	{
		if (Requests >= Responses.Num()) return false;
		const FString Response = Responses[Requests++];
		Design2UMGEditorTests::FScopedHttpTestTransport::Reply(Request, Response);
		return true;
	};
	Design2UMGEditorImportServices::FFigmaImportService Owner;
	FDesign2UMGImportSettingsSnapshot Settings;
	TSharedPtr<FDesignDocument> ResolvedDocument;
	const auto Session = MakeShared<Design2UMGEditorImportServices::FFigmaImportSession>(
		Owner, TEXT("component-blueprint-test"), TEXT("test-token"), 1, Settings,
		[&](const TSharedRef<FDesignDocument>& Document, bool bImageFailure) { ResolvedDocument = Document; }, TEXT("root"), Transport);
	Session->Start();
	TestEqual(TEXT("Root, published identity, library and image requests executed once"), Requests, 4);
	if (!TestTrue(TEXT("Remote dependency resolution completes"), ResolvedDocument.IsValid())) return false;
	const FDesignDocument& Document = *ResolvedDocument;

	TestEqual(TEXT("Remote selection keeps exactly one root"), Document.Children.Num(), 1);
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestEqual(TEXT("Remote selection preserves source identity"), Data.RootWidget.GetStableDesignNodeId(), FString(TEXT("root")));
	TestEqual(TEXT("Remote selection preserves width"), Data.RootWidget.Bounds.W, 300.0f);
	TestEqual(TEXT("Remote selection preserves height"), Data.RootWidget.Bounds.H, 100.0f);
	FigmaComponentDependencies::FCatalog Catalog;
	FString Error;
	TestTrue(TEXT("REST path retains explicit dependency bundle"), FigmaComponentDependencies::Parse(Data.SourceDocument().ComponentDependenciesJson, Catalog, Error));
	TestTrue(TEXT("Remote component definition is merged by stable key"), Catalog.Definitions.Contains(TEXT("button-key")));
	TestFalse(TEXT("Unrelated page instance is absent from dependency evidence"), Catalog.InstanceKeys.Contains(TEXT("unused-instance")));
	TestEqual(TEXT("Unrelated page produces no missing dependency diagnostic"), Catalog.Errors.Num(), 0);
	FDesign2UMGGenerationOutput Output;
	const FString Folder = TEXT("/Game/Design2UMG/Automation/RestComponents_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	UWidgetBlueprint* Blueprint = DesignImportUtils::GenerateWidgetBlueprint(Data, Folder, &Settings.GenerationSettings, &Output);
	if (!TestNotNull(TEXT("Resolved REST data generates component batch"), Blueprint)) return false;
	TestEqual(TEXT("Remote definition reused once"), Output.Components.Num(), 1);
	TestEqual(TEXT("REST instances reference child blueprints"), FindInstances(Blueprint->WidgetTree).Num(), 2);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRemoteSetMappingDependencyTest,
	"Design2UMG.ComponentBlueprints.RemoteSetMappingSkipsLibrary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRemoteSetMappingDependencyTest::RunTest(const FString&)
{
	TSharedPtr<FJsonObject> Json;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(MakeComponentImportJson(TEXT("Cancel"))), Json);
	Json->RemoveField(TEXT("design2umgComponentDependencies"));
	TSharedRef<FJsonObject> Metadata = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> Component = MakeShared<FJsonObject>();
	Component->SetStringField(TEXT("key"), TEXT("button-key"));
	Component->SetStringField(TEXT("componentSetId"), TEXT("set-id"));
	Metadata->SetObjectField(TEXT("main"), Component);
	Json->SetObjectField(TEXT("components"), Metadata);
	TSharedRef<FJsonObject> Sets = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> Set = MakeShared<FJsonObject>();
	Set->SetStringField(TEXT("key"), TEXT("published-set"));
	Sets->SetObjectField(TEXT("set-id"), Set);
	Json->SetObjectField(TEXT("componentSets"), Sets);
	FString Response;
	FJsonSerializer::Serialize(Json.ToSharedRef(), TJsonWriterFactory<>::Create(&Response));
	int32 Requests = 0;
	Design2UMGEditorTests::FScopedHttpTestTransport Transport;
	Transport.StartRequest = [&](int32, const FHttpRequestPtr& Request)
	{
		++Requests;
		if (Requests != 1) return false;
		Design2UMGEditorTests::FScopedHttpTestTransport::Reply(Request, Response);
		return true;
	};
	FDesign2UMGImportSettingsSnapshot Settings;
	auto& Mapping = Settings.GenerationSettings.ComponentMapping.Table.Entries.AddDefaulted_GetRef();
	Mapping.Provider = TEXT("figma");
	Mapping.DocumentScope = TEXT("component-blueprint-test");
	Mapping.ComponentKey = TEXT("published-set");
	Mapping.Key = TEXT("figma|component-blueprint-test|published-set");
	Mapping.WidgetClass = TEXT("/Script/Design2UMGEditor.Design2UMGComponentInputTestWidget");
	Design2UMGEditorImportServices::FFigmaImportService Owner;
	bool Resolved = false;
	const auto Session = MakeShared<Design2UMGEditorImportServices::FFigmaImportSession>(
		Owner, TEXT("component-blueprint-test"), TEXT("test-token"), 1, Settings,
		[&](const TSharedRef<FDesignDocument>&, bool bImageFailure) { Resolved = !bImageFailure; }, FString(), Transport);
	Session->Start();
	TestTrue(TEXT("Set mapping resolves without the source library"), Resolved);
	TestEqual(TEXT("Only the selected document is fetched"), Requests, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentLayoutCarrierIdentityTest,
	"Design2UMG.ComponentBlueprints.LayoutCarrierIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FComponentLayoutCarrierIdentityTest::RunTest(const FString&)
{
	TSharedPtr<FJsonObject> Json;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(MakeComponentImportJson(TEXT("Cancel"))), Json);
	Json->GetObjectField(TEXT("document"))->SetStringField(TEXT("layoutMode"), TEXT("HORIZONTAL"));
	FString Source;
	FJsonSerializer::Serialize(Json.ToSharedRef(), TJsonWriterFactory<>::Create(&Source));
	FDesignDocument Document;
	if (!TestTrue(TEXT("Auto-layout source parses"), FFigmaParser::ParseDocument(Source, Document))) return false;
	FDesign2UMGGenerationSettingsSnapshot Settings;
	FDesign2UMGGenerationOutput Output;
	UWidgetBlueprint* Blueprint = DesignImportUtils::GenerateWidgetBlueprint(
		FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults()),
		TEXT("/Game/Design2UMG/Automation/ComponentCarriers_") + FGuid::NewGuid().ToString(EGuidFormats::Digits), &Settings, &Output);
	if (!TestNotNull(TEXT("Auto-layout parent generates"), Blueprint)) return false;
	const auto Snapshot = Design2UMGAutomationStructureSnapshot::BuildWidgetTreeStructureSnapshot(Blueprint->WidgetTree);
	if (!TestTrue(TEXT("Compiled nested structure is available"), Snapshot.IsValid())) return false;
	int32 ComponentInstances = 0;
	for (const auto& Value : Snapshot->GetArrayField(TEXT("structure_nodes")))
	{
		const FString Class = Value->AsObject()->GetStringField(TEXT("class"));
		for (const auto& Component : Output.Components)
		{
			if (Component.Blueprint && Component.Blueprint->GeneratedClass && Class == Component.Blueprint->GeneratedClass->GetName()) ++ComponentInstances;
		}
	}
	TestEqual(TEXT("Layout wrappers do not create additional component instances"), ComponentInstances, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentDefinitionSourceBoundsTest,
	"Design2UMG.ComponentBlueprints.DefinitionSourceBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FComponentDefinitionSourceBoundsTest::RunTest(const FString&)
{
	for (const bool bHiddenLibraryDefinition : {true, false})
	{
		TSharedPtr<FJsonObject> Json;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(MakeComponentImportJson(TEXT("Cancel"))), Json);
		const auto Definition = Json->GetObjectField(TEXT("design2umgComponentDependencies"))->
			GetArrayField(TEXT("definitions"))[0]->AsObject()->GetObjectField(TEXT("document"));
		const FVector2D ExpectedSize(bHiddenLibraryDefinition ? 100 : 1, 40);
		if (bHiddenLibraryDefinition)
		{
			Definition->SetBoolField(TEXT("visible"), false);
		}
		else
		{
			Definition->SetArrayField(TEXT("children"), {});
			Definition->GetObjectField(TEXT("absoluteBoundingBox"))->SetNumberField(TEXT("width"), ExpectedSize.X);
		}
		FString Source;
		FJsonSerializer::Serialize(Json.ToSharedRef(), TJsonWriterFactory<>::Create(&Source));
		FDesignDocument Document;
		if (!TestTrue(TEXT("Definition source parses"), FFigmaParser::ParseDocument(Source, Document))) return false;
		FDesign2UMGGenerationSettingsSnapshot Settings;
		FDesign2UMGGenerationOutput Output;
		UWidgetBlueprint* Blueprint = DesignImportUtils::GenerateWidgetBlueprint(
			FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults()),
			TEXT("/Game/Design2UMG/Automation/DefinitionBounds_") + FGuid::NewGuid().ToString(EGuidFormats::Digits), &Settings, &Output);
		if (!TestNotNull(bHiddenLibraryDefinition ? TEXT("Hidden library definition supports visible instances") : TEXT("One-pixel component generates"), Blueprint)) continue;
		TestEqual(TEXT("Independent asset generated"), Output.Components.Num(), 1);
		if (Output.Components.Num() == 1)
		{
			const auto Size = FUMGWidgetUtils::GetBlueprintDesignTimeSize(Output.Components[0].Blueprint);
			TestTrue(TEXT("Source dimensions are preserved"), Size.IsSet() && Size.GetValue().Equals(ExpectedSize));
		}
		UUserWidget* Runtime = NewObject<UUserWidget>(GEditor->GetEditorWorldContext().World(), Blueprint->GeneratedClass);
		Runtime->Initialize();
		Runtime->TakeWidget();
		TArray<FString> Values;
		for (UUserWidget* Child : FindInstances(Runtime->WidgetTree)) Values.Append(FindText(Child->WidgetTree));
		TestTrue(TEXT("Both instance text overrides survive"), Values.Contains(TEXT("Confirm")) && Values.Contains(TEXT("Cancel")));
		Runtime->ReleaseSlateResources(true);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEmptyComponentDefinitionTest,
	"Design2UMG.ComponentBlueprints.EmptyDefinitionRetainsBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FEmptyComponentDefinitionTest::RunTest(const FString&)
{
	TSharedPtr<FJsonObject> Json;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(MakeComponentImportJson(TEXT("Cancel"))), Json);
	const auto Definition = Json->GetObjectField(TEXT("design2umgComponentDependencies"))->
		GetArrayField(TEXT("definitions"))[0]->AsObject()->GetObjectField(TEXT("document"));
	Definition->SetArrayField(TEXT("children"), {});
	Definition->SetStringField(TEXT("layoutMode"), TEXT("HORIZONTAL"));
	Definition->SetStringField(TEXT("layoutSizingHorizontal"), TEXT("HUG"));
	Definition->SetStringField(TEXT("layoutSizingVertical"), TEXT("HUG"));
	Definition->SetStringField(TEXT("primaryAxisSizingMode"), TEXT("AUTO"));
	Definition->SetStringField(TEXT("counterAxisSizingMode"), TEXT("AUTO"));
	FString Source;
	FJsonSerializer::Serialize(Json.ToSharedRef(), TJsonWriterFactory<>::Create(&Source));
	FDesignDocument Document;
	if (!TestTrue(TEXT("Empty main definition parses"), FFigmaParser::ParseDocument(Source, Document))) return false;
	FDesign2UMGGenerationSettingsSnapshot Settings;
	FDesign2UMGGenerationOutput Output;
	UWidgetBlueprint* Blueprint = DesignImportUtils::GenerateWidgetBlueprint(
		FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults()),
		TEXT("/Game/Design2UMG/Automation/EmptyDefinition_") + FGuid::NewGuid().ToString(EGuidFormats::Digits), &Settings, &Output);
	if (!TestNotNull(TEXT("Empty reusable template can receive instance content"), Blueprint)) return false;
	TestEqual(TEXT("Independent empty template asset is generated"), Output.Components.Num(), 1);
	if (Output.Components.Num() == 1)
	{
		const auto Size = FUMGWidgetUtils::GetBlueprintDesignTimeSize(Output.Components[0].Blueprint);
		TestTrue(TEXT("Empty template retains its declared 100 by 40 layout bounds"),
			Size.IsSet() && Size.GetValue().Equals(FVector2D(100, 40)));
	}
	UUserWidget* Runtime = NewObject<UUserWidget>(GEditor->GetEditorWorldContext().World(), Blueprint->GeneratedClass);
	Runtime->Initialize();
	Runtime->TakeWidget();
	TArray<FString> Values;
	for (UUserWidget* Child : FindInstances(Runtime->WidgetTree)) Values.Append(FindText(Child->WidgetTree));
	TestTrue(TEXT("Empty templates retain instance contents"), Values.Contains(TEXT("Confirm")) && Values.Contains(TEXT("Cancel")));
	Runtime->ReleaseSlateResources(true);
	Definition->RemoveField(TEXT("layoutMode"));
	Definition->RemoveField(TEXT("layoutSizingHorizontal"));
	Definition->RemoveField(TEXT("layoutSizingVertical"));
	Source.Reset();
	FJsonSerializer::Serialize(Json.ToSharedRef(), TJsonWriterFactory<>::Create(&Source));
	Document = FDesignDocument();
	if (!TestTrue(TEXT("Empty absolute-layout definition parses"), FFigmaParser::ParseDocument(Source, Document))) return false;
	Blueprint = DesignImportUtils::GenerateWidgetBlueprint(
		FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults()),
		TEXT("/Game/Design2UMG/Automation/EmptyAbsoluteDefinition_") + FGuid::NewGuid().ToString(EGuidFormats::Digits), &Settings, &Output);
	TestNotNull(TEXT("Empty absolute-layout component remains an independent asset"), Blueprint);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentBlueprintRealDesignTest,
	"Design2UMG.ComponentBlueprints.RealDesignLayouts", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FComponentBlueprintRealDesignTest::RunTest(const FString&)
{
	FString ExportRoot;
	if (!FParse::Value(FCommandLine::Get(), TEXT("D2URealDesignExportRoot="), ExportRoot))
	{
		AddError(TEXT("Missing required -D2URealDesignExportRoot=<directory> for real-design component layout coverage"));
		return false;
	}
	ExportRoot = FPaths::ConvertRelativePathToFull(ExportRoot);

	for (const FString& Name : { FString(TEXT("figma_game_party_home")), FString(TEXT("figma_general_data_analytics")) })
	{
		const FString SourcePath = ExportRoot / Name / TEXT("design.json");
		FString Source, Prepared, Error;
		if (!TestTrue(TEXT("Read existing real design without modifying it"), FFileHelper::LoadFileToString(Source, *SourcePath))) return false;
		if (!TestTrue(TEXT("Resolve real file component identities"), FigmaComponentDependencies::PrepareRestFile(Source, SourcePath, Prepared, Error))) return false;
		FDesignDocument Document;
		if (!TestTrue(TEXT("Real component source parses"), FFigmaParser::ParseDocument(Prepared, Document))) return false;
		Design2UMGLocalFigmaImport::ResolveDocumentImages(Document, SourcePath);
		FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
		FDesign2UMGGenerationSettingsSnapshot Settings;
		FDesign2UMGGenerationOutput Output;
		const FString Folder = TEXT("/Game/Design2UMG/Automation/RealComponents_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
		UWidgetBlueprint* Blueprint = DesignImportUtils::GenerateWidgetBlueprint(Data, Folder, &Settings, &Output);
		if (!TestNotNull(TEXT("Real design generates independent components"), Blueprint)) return false;
		TestTrue(TEXT("Real dependency assets were generated"), Output.Components.Num() > 0);
		TArray<uint8> NestedPng;
		FIntPoint NestedSize;
		if (!TestTrue(TEXT("Real nested asset renders"), FUMGWidgetUtils::RenderWidgetToPng(Blueprint, NestedPng, NestedSize, &Error))) return false;
		FFileHelper::SaveArrayToFile(NestedPng, *(FPaths::ProjectSavedDir() / TEXT("Design2UMG") / (Name + TEXT("-nested-render.png"))));
		// Compare against the existing flat generation path using the same parsed source and conversion settings.
		// Explicitly construct a non-component control, rather than treating missing
		// dependency metadata as an instruction to flatten a production import.
		TFunction<void(FUMGWidgetNode&)> FlattenControl = [&](FUMGWidgetNode& Node)
		{
			if (Node.HasSourceData() && Node.Source().SourceNodeType == TEXT("INSTANCE"))
				Node.MutableSource().SourceNodeType = TEXT("FRAME");
			for (auto& Child : Node.Children) FlattenControl(Child);
		};
		FlattenControl(Data.RootWidget);
		Data.MutableSourceDocument().ComponentDependenciesJson.Reset();
		UWidgetBlueprint* Flat = DesignImportUtils::GenerateWidgetBlueprint(Data, Folder / TEXT("FlatControl"), &Settings);
		if (!TestNotNull(TEXT("Flat control asset generated"), Flat)) return false;
		TArray<uint8> FlatPng;
		FIntPoint FlatSize;
		if (!TestTrue(TEXT("Flat control renders"), FUMGWidgetUtils::RenderWidgetToPng(Flat, FlatPng, FlatSize, &Error))) return false;
		TestEqual(TEXT("Nesting retains the real design output dimensions"), NestedSize, FlatSize);
		FImage NestedImage, FlatImage;
		if (TestTrue(TEXT("Decode generated renders"), FImageUtils::DecompressImage(NestedPng.GetData(), NestedPng.Num(), NestedImage)
			&& FImageUtils::DecompressImage(FlatPng.GetData(), FlatPng.Num(), FlatImage)))
		{
			TestTrue(TEXT("Independent component assets preserve every rendered pixel of the existing source conversion"), NestedImage.RawData == FlatImage.RawData);
		}
		FFileHelper::SaveArrayToFile(FlatPng, *(FPaths::ProjectSavedDir() / TEXT("Design2UMG") / (Name + TEXT("-flat-render.png"))));
	}
	return true;
}
namespace
{
	bool CheckPersistedRichTextInstances(FAutomationTestBase& Test, UWidgetBlueprint* Blueprint)
	{
		UUserWidget* Runtime = NewObject<UUserWidget>(GEditor->GetEditorWorldContext().World(), Blueprint->GeneratedClass);
		Runtime->Initialize();
		Runtime->TakeWidget();
		ON_SCOPE_EXIT { Runtime->ReleaseSlateResources(true); };
		const auto Instances = FindInstances(Runtime->WidgetTree);
		if (!Test.TestEqual(TEXT("Mixed-style parent has two component instances"), Instances.Num(), 2)) return false;
		Test.TestTrue(TEXT("Mixed-style instances share their component class"), Instances[0]->GetClass() == Instances[1]->GetClass());
		TArray<UDataTable*> Tables;
		TSet<URichTextBlock*> VisitedLabels;
		int32 LabelVisits = 0;
		bool bRed = false, bGreen = false;
		Runtime->WidgetTree->ForEachWidgetAndDescendants([&](UWidget* Widget)
		{
			URichTextBlock* RichText = Cast<URichTextBlock>(Widget);
			if (!RichText) return;
			++LabelVisits;
			// Named-slot content is reached through both the host binding and child tree.
			if (VisitedLabels.Contains(RichText)) return;
			VisitedLabels.Add(RichText);
			UDataTable* Table = RichText->GetTextStyleSet();
			if (!Test.TestNotNull(TEXT("Rich text retains its style table"), Table)) return;
			Tables.Add(Table);
			const FRichTextStyleRow* Row = Table->FindRow<FRichTextStyleRow>(TEXT("d2u_run_1"), TEXT("Persistence test"), false);
			if (!Test.TestNotNull(TEXT("Overridden style row persists"), Row)) return;
			Test.TestNotNull(TEXT("Embedded style resolves its font asset"), Row->TextStyle.Font.FontObject.Get());
			Test.TestEqual(TEXT("Per-run bold face persists"), Row->TextStyle.Font.TypefaceFontName, FName(TEXT("Bold")));
			Test.TestEqual(TEXT("Per-run source24px maps to18pt"), Row->TextStyle.Font.Size, 18.0f);
			const FLinearColor Color = Row->TextStyle.ColorAndOpacity.GetSpecifiedColor();
			bRed |= Color.Equals(FLinearColor::Red);
			bGreen |= Color.Equals(FLinearColor::Green);
		});
		Test.AddInfo(FString::Printf(TEXT("Rich-text traversal: %d visits, %d distinct widget objects"), LabelVisits, VisitedLabels.Num()));
		Test.TestEqual(TEXT("Both instance override labels retain rich text"), Tables.Num(), 2);
		if (Tables.Num() == 2) Test.TestTrue(TEXT("Instance-specific style tables remain separate"), Tables[0] != Tables[1]);
		Test.TestTrue(TEXT("First instance retains its red style"), bRed);
		Test.TestTrue(TEXT("Second instance retains its green style"), bGreen);
		return !Test.HasAnyErrors();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentRichTextSaveTest,
	"Design2UMG.ComponentBlueprintPersistence.RichTextSave", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FComponentRichTextSaveTest::RunTest(const FString&)
{
	TSharedPtr<FJsonObject> Json;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(MakeComponentImportJson(TEXT("CD"))), Json)) return false;
	const auto& Children = Json->GetObjectField(TEXT("document"))->GetArrayField(TEXT("children"));
	for (int32 Index = 0; Index < Children.Num(); ++Index)
	{
		const auto Label = Children[Index]->AsObject()->GetArrayField(TEXT("children"))[0]->AsObject();
		Label->SetStringField(TEXT("characters"), Index == 0 ? TEXT("AB") : TEXT("CD"));
		Label->SetArrayField(TEXT("characterStyleOverrides"), {MakeShared<FJsonValueNumber>(0), MakeShared<FJsonValueNumber>(1)});
		TSharedPtr<FJsonObject> Override;
		const FString StyleJson = FString::Printf(TEXT("{\"fontSize\":24,\"fontWeight\":700,\"fills\":[{\"type\":\"SOLID\",\"color\":{\"r\":%d,\"g\":%d,\"b\":0}}]}"), Index == 0 ? 1 : 0, Index == 0 ? 0 : 1);
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(StyleJson), Override)) return false;
		const auto Table = MakeShared<FJsonObject>();
		Table->SetObjectField(TEXT("1"), Override);
		Label->SetObjectField(TEXT("styleOverrideTable"), Table);
	}
	FString Source;
	FJsonSerializer::Serialize(Json.ToSharedRef(), TJsonWriterFactory<>::Create(&Source));
	FDesignDocument Document;
	if (!TestTrue(TEXT("Mixed instance styles parse"), FFigmaParser::ParseDocument(Source, Document))) return false;
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	FDesign2UMGGenerationSettingsSnapshot Settings;
	FDesign2UMGGenerationOutput Output;
	const FString Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	UWidgetBlueprint* Blueprint = DesignImportUtils::GenerateWidgetBlueprint(Data, TEXT("/Game/Design2UMG/Automation/RichTextPersistence_") + Id, &Settings, &Output);
	if (!TestNotNull(TEXT("Save parent and shared rich-style child"), Blueprint)) return false;
	if (!CheckPersistedRichTextInstances(*this, Blueprint)) return false;
	TArray<uint8> Png;
	FIntPoint Size;
	FString Error;
	if (!TestTrue(TEXT("Saved mixed-style parent renders"), FUMGWidgetUtils::RenderWidgetToPng(Blueprint, Png, Size, &Error))) return false;
	FImage Rendered;
	if (!TestTrue(TEXT("Decode mixed-style runtime image"), FImageUtils::DecompressImage(Png.GetData(), Png.Num(), Rendered)) ||
		!TestTrue(TEXT("Runtime image uses BGRA8"), Rendered.Format == ERawImageFormat::BGRA8)) return false;
	int32 RedPixels = 0, GreenPixels = 0;
	const FColor* Pixels = reinterpret_cast<const FColor*>(Rendered.RawData.GetData());
	for (int64 Index = 0; Index < static_cast<int64>(Rendered.SizeX) * Rendered.SizeY; ++Index)
	{
		const FColor Pixel = Pixels[Index];
		if (Pixel.A > 32 && Pixel.R > 200 && Pixel.G < 30 && Pixel.B < 30) ++RedPixels;
		if (Pixel.A > 32 && Pixel.G > 200 && Pixel.R < 30 && Pixel.B < 30) ++GreenPixels;
	}
	if (!TestTrue(TEXT("First nested override visibly renders red glyphs"), RedPixels > 3) ||
		!TestTrue(TEXT("Second nested override visibly renders green glyphs"), GreenPixels > 3)) return false;
	const FString PngPath = FPaths::ProjectSavedDir() / TEXT("Design2UMG/RichTextPersistence_") + Id + TEXT(".png");
	if (!TestTrue(TEXT("Save original runtime pixels"), FFileHelper::SaveArrayToFile(Png, *PngPath))) return false;
	const auto Manifest = MakeShared<FJsonObject>();
	Manifest->SetStringField(TEXT("asset"), Blueprint->GetPathName());
	Manifest->SetStringField(TEXT("png"), PngPath);
	Manifest->SetNumberField(TEXT("process"), FPlatformProcess::GetCurrentProcessId());
	FString ManifestJson;
	FJsonSerializer::Serialize(Manifest, TJsonWriterFactory<>::Create(&ManifestJson));
	return TestTrue(TEXT("Write fresh-process reload manifest"), FFileHelper::SaveStringToFile(ManifestJson,
		*(FPaths::ProjectSavedDir() / TEXT("Design2UMG/RichTextPersistence.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentRichTextReloadTest,
	"Design2UMG.ComponentBlueprintPersistence.RichTextReload", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FComponentRichTextReloadTest::RunTest(const FString&)
{
	FString Json;
	if (!TestTrue(TEXT("Previous process supplied rich-style assets"), FFileHelper::LoadFileToString(Json,
		*(FPaths::ProjectSavedDir() / TEXT("Design2UMG/RichTextPersistence.json"))))) return false;
	TSharedPtr<FJsonObject> Manifest;
	if (!TestTrue(TEXT("Read reload manifest"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Manifest))) return false;
	if (!TestNotEqual(TEXT("Verification runs in a fresh process"), static_cast<uint32>(Manifest->GetNumberField(TEXT("process"))), FPlatformProcess::GetCurrentProcessId())) return false;
	UWidgetBlueprint* Blueprint = LoadObject<UWidgetBlueprint>(nullptr, *Manifest->GetStringField(TEXT("asset")));
	if (!TestNotNull(TEXT("Load saved parent and its dependency classes"), Blueprint)) return false;
	if (!CheckPersistedRichTextInstances(*this, Blueprint)) return false;
	TArray<uint8> BeforePng, AfterPng;
	FIntPoint Size;
	FString Error;
	if (!TestTrue(TEXT("Read pre-reload pixels"), FFileHelper::LoadFileToArray(BeforePng, *Manifest->GetStringField(TEXT("png")))) ||
		!TestTrue(TEXT("Reloaded assets render"), FUMGWidgetUtils::RenderWidgetToPng(Blueprint, AfterPng, Size, &Error))) return false;
	FImage Before, After;
	if (!TestTrue(TEXT("Decode both renders"), FImageUtils::DecompressImage(BeforePng.GetData(), BeforePng.Num(), Before)
		&& FImageUtils::DecompressImage(AfterPng.GetData(), AfterPng.Num(), After))) return false;
	TestEqual(TEXT("Reload preserves render width"), After.SizeX, Before.SizeX);
	TestEqual(TEXT("Reload preserves render height"), After.SizeY, Before.SizeY);
	TestTrue(TEXT("Reload preserves every rendered mixed-style pixel"), Before.RawData == After.RawData);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEmptyComponentBundleImportTest,
	"Design2UMG.ComponentBlueprints.EmptyBundleKeepsOrdinaryImport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FEmptyComponentBundleImportTest::RunTest(const FString&)
{
	const FString Json = TEXT(R"JSON({"fileKey":"empty-bundle-test","document":{"id":"doc","type":"CANVAS","children":[{"id":"root","name":"OrdinaryPage","type":"FRAME","fills":[{"type":"SOLID","color":{"r":0.1,"g":0.2,"b":0.3}}],"absoluteBoundingBox":{"x":0,"y":0,"width":100,"height":100}}]},"design2umgComponentDependencies":{"schemaVersion":1,"definitions":[],"instances":{},"errors":[]}})JSON");
	FDesignDocument Document;
	if (!TestTrue(TEXT("Empty bundle source parses"), FFigmaParser::ParseDocument(Json, Document))) return false;
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	const FString Folder = TEXT("/Game/Design2UMG/Automation/EmptyBundle_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	FDesign2UMGGenerationSettingsSnapshot Settings;
	FDesign2UMGGenerationOutput FirstOutput, SecondOutput;
	UWidgetBlueprint* First = DesignImportUtils::GenerateWidgetBlueprint(Data, Folder, &Settings, &FirstOutput);
	UWidgetBlueprint* Second = DesignImportUtils::GenerateWidgetBlueprint(Data, Folder, &Settings, &SecondOutput);
	if (!TestNotNull(TEXT("First ordinary import"), First) || !TestNotNull(TEXT("Second ordinary import"), Second)) return false;
	TestTrue(TEXT("Ordinary import creates separate assets"), First != Second && First->GetPathName() != Second->GetPathName());
	TestFalse(TEXT("Ordinary page is not wholly generator-owned"), FirstOutput.Manifest.bWholeAssetOwned);
	TSharedPtr<FJsonObject> OtherPage;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(MakeComponentImportJson(TEXT("Other page"))), OtherPage);
	FString NonEmptyBundle;
	FJsonSerializer::Serialize(OtherPage->GetObjectField(TEXT("design2umgComponentDependencies")).ToSharedRef(),
		TJsonWriterFactory<>::Create(&NonEmptyBundle));
	Data.MutableSourceDocument().ComponentDependenciesJson = NonEmptyBundle;
	FDesign2UMGGenerationOutput UnrelatedOutput;
	UWidgetBlueprint* Third = DesignImportUtils::GenerateWidgetBlueprint(Data, Folder, &Settings, &UnrelatedOutput);
	if (!TestNotNull(TEXT("Ordinary page imports with unrelated component definitions"), Third)) return false;
	TestFalse(TEXT("Unrelated definitions do not switch page ownership"), UnrelatedOutput.Manifest.bComponentBulkRoot);
	TestTrue(TEXT("Ordinary page retains its authored asset name"), Third->GetName().StartsWith(Data.WidgetName));
	UWidgetBlueprint* Fourth = DesignImportUtils::GenerateWidgetBlueprint(Data, Folder, &Settings, &UnrelatedOutput);
	TestTrue(TEXT("Unrelated definitions preserve ordinary repeated-import behavior"), Fourth && Fourth != Third);
	Data.MutableSourceDocument().ComponentDependenciesJson = TEXT("{\"schemaVersion\":2}");
	FDesign2UMGGenerationOutput InvalidOutput;
	AddExpectedError(TEXT("Component import failed"), EAutomationExpectedErrorFlags::Contains, 1);
	TestNull(TEXT("Invalid bundle is reported rather than treated as empty"), DesignImportUtils::BuildWidgetBlueprint(Data, Folder, &Settings, &InvalidOutput));
	TestTrue(TEXT("Invalid bundle has diagnostic"), InvalidOutput.MappingDiagnostics.Num() > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSingleBlueprintIgnoresDependencyMetadataTest,
	"Design2UMG.ComponentBlueprints.SingleBlueprintIgnoresDependencyMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSingleBlueprintIgnoresDependencyMetadataTest::RunTest(const FString&)
{
	const FString Folder = TEXT("/Game/Design2UMG/Automation/SingleBlueprintBoundary_")
		+ FGuid::NewGuid().ToString(EGuidFormats::Digits);
	ON_SCOPE_EXIT { Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(Folder); };
	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_SingleBlueprintBoundary");
	Data.RootWidget.Id = TEXT("root");
	Data.RootWidget.Type = UMGWidgetTypes::CanvasPanel;
	Data.RootWidget.Name = TEXT("Root");
	Data.RootWidget.Bounds.W = 100;
	Data.RootWidget.Bounds.H = 100;
	Data.MutableSourceDocument().Provider = TEXT("figma");
	Data.MutableSourceDocument().DocumentScope = TEXT("single-blueprint-boundary");
	Data.MutableSourceDocument().ComponentDependenciesJson = TEXT("{\"schemaVersion\":2}");
	FDesign2UMGGenerationSettingsSnapshot Settings;
	FDesign2UMGGenerationOutput Output;
	UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, Folder, &Settings, &Output);
	TestNotNull(TEXT("Single-blueprint assembly does not interpret dependency metadata"), Blueprint);
	TestFalse(TEXT("Single-blueprint assembly does not take component batch ownership"), Output.Manifest.bComponentBulkRoot);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMissingComponentBundleImportTest,
	"Design2UMG.ComponentBlueprints.MissingBundleReportsInstances",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMissingComponentBundleImportTest::RunTest(const FString&)
{
	FDesignDocument Document;
	if (!TestTrue(TEXT("Source parses"), FFigmaParser::ParseDocument(MakeComponentImportJson(TEXT("Cancel")), Document))) return false;
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	Data.MutableSourceDocument().ComponentDependenciesJson.Reset();
	FDesign2UMGGenerationSettingsSnapshot Settings;
	FDesign2UMGGenerationOutput Output;
	AddExpectedError(TEXT("Component import failed"), EAutomationExpectedErrorFlags::Contains, 1);
	TestNull(TEXT("Missing bundle does not silently flatten instances"), DesignImportUtils::BuildWidgetBlueprint(
		Data, TEXT("/Game/Design2UMG/Automation/MissingBundle"), &Settings, &Output));
	TestTrue(TEXT("Diagnostic identifies missing dependency data and affected component"),
		Output.MappingDiagnostics.ContainsByPredicate([](const FDesign2UMGMappingDiagnostic& Diagnostic)
		{
			return Diagnostic.Message.Contains(TEXT("dependency")) && Diagnostic.Message.Contains(TEXT("button-key"));
		}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLocalComponentDependencyImportTest,
	"Design2UMG.ComponentBlueprints.LocalDependencyPreparation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FLocalComponentDependencyImportTest::RunTest(const FString&)
{
	const FString Path = FPaths::ProjectSavedDir() / TEXT("Design2UMG") /
		(FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".json"));
	ON_SCOPE_EXIT { IFileManager::Get().Delete(*Path); };
	const FString Exported = MakeComponentImportJson(TEXT("Cancel"));
	if (!TestTrue(TEXT("Write exported source"), FFileHelper::SaveStringToFile(Exported, *Path,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))) return false;
	FDesignDocument Document;
	FString Error;
	if (!TestTrue(TEXT("Load exporter bundle"), Design2UMGLocalFigmaImport::LoadLocalFigmaDocument(Path, Document, Error))) return false;
	FigmaComponentDependencies::FCatalog Catalog;
	if (!TestTrue(TEXT("Preserve exporter-only definitions"), FigmaComponentDependencies::Parse(
		Document.SourceDocument().ComponentDependenciesJson, Catalog, Error))) return false;
	TestTrue(TEXT("External definition remains available"), Catalog.Definitions.Contains(TEXT("button-key")));

	TSharedPtr<FJsonObject> Root;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Exported), Root);
	const auto Definition = Root->GetObjectField(TEXT("design2umgComponentDependencies"))->
		GetArrayField(TEXT("definitions"))[0]->AsObject()->GetObjectField(TEXT("document"));
	auto Children = Root->GetObjectField(TEXT("document"))->GetArrayField(TEXT("children"));
	Children.Add(MakeShared<FJsonValueObject>(Definition));
	Root->GetObjectField(TEXT("document"))->SetArrayField(TEXT("children"), Children);
	Root->RemoveField(TEXT("design2umgComponentDependencies"));
	TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("key"), TEXT("button-key"));
	TSharedRef<FJsonObject> Components = MakeShared<FJsonObject>();
	Components->SetObjectField(TEXT("main"), Entry);
	Root->SetObjectField(TEXT("components"), Components);
	FString Rest;
	FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&Rest));
	if (!TestTrue(TEXT("Write REST source"), FFileHelper::SaveStringToFile(Rest, *Path,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))) return false;
	if (!TestTrue(TEXT("Prepare local REST dependencies"), Design2UMGLocalFigmaImport::LoadLocalFigmaDocument(Path, Document, Error))) return false;
	if (!TestTrue(TEXT("Prepared catalog parses"), FigmaComponentDependencies::Parse(
		Document.SourceDocument().ComponentDependenciesJson, Catalog, Error))) return false;
	TestTrue(TEXT("Local main component collected"), Catalog.Definitions.Contains(TEXT("button-key")));
	TestEqual(TEXT("Both instance references collected"), Catalog.InstanceKeys.Num(), 2);
	TestEqual(TEXT("Published file identity preserved"), Document.Metadata.FindRef(TEXT("fileKey")), FString(TEXT("component-blueprint-test")));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentPageUserContentTest,
	"Design2UMG.ComponentBlueprints.SavedUserPageContents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FComponentPageUserContentTest::RunTest(const FString&)
{
	FDesignDocument Document;
	if (!FFigmaParser::ParseDocument(MakeComponentImportJson(TEXT("Cancel")), Document)) return false;
	FDesign2UMGGenerationSettingsSnapshot Settings;
	FDesign2UMGGenerationOutput Output;
	const FString Folder = TEXT("/Game/Design2UMG/Automation/UserPage_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	UWidgetBlueprint* Blueprint = DesignImportUtils::GenerateWidgetBlueprint(
		FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults()), Folder, &Settings, &Output);
	if (!TestNotNull(TEXT("Initial page"), Blueprint)) return false;
	UPanelWidget* Panel = Cast<UPanelWidget>(Blueprint->WidgetTree->RootWidget);
	if (!TestNotNull(TEXT("Page layout root"), Panel)) return false;
	UTextBlock* UserLabel = Blueprint->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("UserLabel"));
	UserLabel->SetText(FText::FromString(TEXT("Manual label")));
	Panel->AddChild(UserLabel);
	Blueprint->WidgetVariableNameToGuidMap.Add(UserLabel->GetFName(), FGuid::NewGuid());
	Blueprint->BlueprintDescription = TEXT("User page description");
	FEdGraphPinType Type;
	Type.PinCategory = UEdGraphSchema_K2::PC_Int;
	TestTrue(TEXT("User variable added"), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("UserCounter"), Type, TEXT("42")));
	UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, TEXT("UserEvents"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	FBlueprintEditorUtils::AddUbergraphPage(Blueprint, Graph);
	UK2Node_CustomEvent* Event = NewObject<UK2Node_CustomEvent>(Graph);
	Event->CustomFunctionName = TEXT("UserOnlyEvent");
	Event->CreateNewGuid();
	Graph->AddNode(Event);
	Event->AllocateDefaultPins();
	if (!TestTrue(TEXT("Save user changes independently of generator manifest"), FUMGGenerator::SaveWidgetBlueprint(Blueprint).IsSuccess())) return false;
	FDesignDocument Updated;
	FFigmaParser::ParseDocument(MakeComponentImportJson(TEXT("AfterUserEdit")), Updated);
	UWidgetBlueprint* Result = DesignImportUtils::GenerateWidgetBlueprint(
		FDesign2UMGConverter::Convert(Updated, FDesign2UMGConversionSettings::Defaults()), Folder, &Settings, &Output);
	if (!TestNotNull(TEXT("Repeated import updates saved user page"), Result)) return false;
	TestTrue(TEXT("Same page asset"), Result == Blueprint);
	TestEqual(TEXT("Manual description preserved"), Result->BlueprintDescription, FString(TEXT("User page description")));
	TestTrue(TEXT("Manual widget preserved"), Result->WidgetTree->FindWidget(TEXT("UserLabel")) == UserLabel);
	TestEqual(TEXT("Manual text preserved"), UserLabel->GetText().ToString(), FString(TEXT("Manual label")));
	TestTrue(TEXT("User event graph preserved"), Result->UbergraphPages.Contains(Graph) && Graph->Nodes.Contains(Event));
	TestTrue(TEXT("User variable preserved"), Result->NewVariables.ContainsByPredicate([](const FBPVariableDescription& V) { return V.VarName == TEXT("UserCounter"); }));
	UUserWidget* Runtime = NewObject<UUserWidget>(GEditor->GetEditorWorldContext().World(), Result->GeneratedClass);
	Runtime->Initialize();
	Runtime->TakeWidget();
	TArray<FString> Values;
	for (UUserWidget* Instance : FindInstances(Runtime->WidgetTree)) Values.Append(FindText(Instance->WidgetTree));
	TestTrue(TEXT("Design text updated despite manual additions"), Values.Contains(TEXT("AfterUserEdit")));
	Runtime->ReleaseSlateResources(true);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentUnicodeIdentityTest,
	"Design2UMG.ComponentBlueprints.UnicodeIdentity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FComponentUnicodeIdentityTest::RunTest(const FString&)
{
	const FString Folder = TEXT("/Game/Design2UMG/Automation/Unicode_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	FDesign2UMGGenerationSettingsSnapshot Settings;
	TArray<FString> Paths;
	for (const FString Scope : {FString(TEXT("文档甲")), FString(TEXT("文档乙"))})
	{
		FDesignDocument Document;
		if (!FFigmaParser::ParseDocument(MakeComponentImportJson(TEXT("Cancel")).Replace(TEXT("component-blueprint-test"), *Scope), Document)) return false;
		const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
		FDesign2UMGGenerationOutput Output;
		UWidgetBlueprint* Blueprint = DesignImportUtils::GenerateWidgetBlueprint(Data, Folder, &Settings, &Output);
		if (!TestNotNull(TEXT("Unicode scope generates"), Blueprint) || !TestEqual(TEXT("One shared component"), Output.Components.Num(), 1)) return false;
		const FString RootPath = Blueprint->GetPathName();
		const FString ChildPath = Output.Components[0].Blueprint->GetPathName();
		TestFalse(TEXT("Different scopes have distinct roots"), Paths.Contains(RootPath));
		TestFalse(TEXT("Different scopes have distinct components"), Paths.Contains(ChildPath));
		Paths.Append({RootPath, ChildPath});
		Blueprint = DesignImportUtils::GenerateWidgetBlueprint(Data, Folder, &Settings, &Output);
		if (!TestNotNull(TEXT("Unicode scope reimports"), Blueprint) || Output.Components.Num() != 1) return false;
		TestEqual(TEXT("Root identity stable"), Blueprint->GetPathName(), RootPath);
		TestEqual(TEXT("Component identity stable"), Output.Components[0].Blueprint->GetPathName(), ChildPath);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentMappingFailureFallbackTest,
	"Design2UMG.ComponentBlueprints.MappingFailureFallback", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FComponentMappingFailureFallbackTest::RunTest(const FString&)
{
	const auto MakeSource = [](const FString& Text)
	{
		TSharedPtr<FJsonObject> Json;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(MakeComponentImportJson(Text)), Json);
		for (const auto& Value : Json->GetObjectField(TEXT("document"))->GetArrayField(TEXT("children")))
		{
			const auto Outer = Value->AsObject();
			auto Inner = MakeShared<FJsonObject>();
			Inner->SetStringField(TEXT("id"), Outer->GetStringField(TEXT("id")) + TEXT(";nested"));
			Inner->SetStringField(TEXT("name"), TEXT("Nested"));
			Inner->SetStringField(TEXT("type"), TEXT("INSTANCE"));
			Inner->SetStringField(TEXT("componentId"), TEXT("nested-main"));
			Inner->SetStringField(TEXT("mainComponentKey"), TEXT("nested-key"));
			Inner->SetObjectField(TEXT("absoluteBoundingBox"), Outer->GetObjectField(TEXT("absoluteBoundingBox")));
			Inner->SetArrayField(TEXT("children"), Outer->GetArrayField(TEXT("children")));
			Outer->SetArrayField(TEXT("children"), {MakeShared<FJsonValueObject>(Inner)});
		}
		FString Result;
		FJsonSerializer::Serialize(Json.ToSharedRef(), TJsonWriterFactory<>::Create(&Result));
		return Result;
	};
	FDesignDocument Document;
	if (!FFigmaParser::ParseDocument(MakeSource(TEXT("Cancel")), Document)) return false;
	FDesign2UMGGenerationSettingsSnapshot Settings;
	auto& Entry = Settings.ComponentMapping.Table.Entries.AddDefaulted_GetRef();
	Entry.Provider = TEXT("figma");
	Entry.DocumentScope = TEXT("component-blueprint-test");
	Entry.ComponentKey = TEXT("button-key");
	Entry.Key = Entry.Provider + TEXT("|") + Entry.DocumentScope + TEXT("|") + Entry.ComponentKey;
	// A loadable non-widget class exercises class rejection without a missing-package log.
	Entry.WidgetClass = TEXT("/Script/CoreUObject.Object");
	auto& Override = Settings.ClassOverrides.AddDefaulted_GetRef();
	Override.NamePattern = TEXT("First");
	Override.WidgetClass = UDesign2UMGComponentInputTestWidget::StaticClass();
	const FString Folder = TEXT("/Game/Design2UMG/Automation/MappingFallback_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	FDesign2UMGGenerationOutput Output;
	UWidgetBlueprint* Blueprint = DesignImportUtils::GenerateWidgetBlueprint(
		FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults()), Folder, &Settings, &Output);
	if (!TestNotNull(TEXT("Unavailable explicit mapping uses ordinary conversion"), Blueprint)) return false;
	TestTrue(TEXT("Unavailable active mapping explicitly degrades generation"), Output.bHasDegradation);
	TestEqual(TEXT("Mapping fallback is not ordinary success"), Output.Commit.Status, EDesign2UMGCommitStatus::SucceededWithDegradation);
	TestEqual(TEXT("No generated substitute for explicit mapping"), Output.Components.Num(), 0);
	TestTrue(TEXT("Mapping failure diagnosed"), Output.MappingDiagnostics.ContainsByPredicate(
		[](const FDesign2UMGMappingDiagnostic& D) { return D.Kind == TEXT("mapping_class_unavailable"); }));
	UUserWidget* Runtime = NewObject<UUserWidget>(GEditor->GetEditorWorldContext().World(), Blueprint->GeneratedClass);
	Runtime->TakeWidget();
	const TArray<FString> Texts = FindText(Runtime->WidgetTree);
	TestTrue(TEXT("First instance content preserved"), Texts.Contains(TEXT("Confirm")));
	TestTrue(TEXT("Second instance content preserved"), Texts.Contains(TEXT("Cancel")));
	Runtime->ReleaseSlateResources(true);
	// Initial import may fall back, but a failed mapped target cannot commit a reimport.
	const FString Package = Blueprint->GetOutermost()->GetName();
	const FString AssetPath = FPackageName::LongPackageNameToFilename(Package, FPackageName::GetAssetPackageExtension());
	const FString SidecarPath = Design2UMGGenerationManifest::BuildSidecarPathForAsset(Package);
	TArray<uint8> AssetBefore, SidecarBefore;
	if (!TestTrue(TEXT("Read saved asset"), FFileHelper::LoadFileToArray(AssetBefore, *AssetPath))
		|| !TestTrue(TEXT("Read saved manifest"), FFileHelper::LoadFileToArray(SidecarBefore, *SidecarPath))) return false;
	const TArray<UWidget*> OriginalWidgets = Blueprint->GetAllSourceWidgets();
	FDesignDocument Updated;
	FFigmaParser::ParseDocument(MakeSource(TEXT("Updated fallback")), Updated);
	AddExpectedError(TEXT("Page ownership merge failed"), EAutomationExpectedErrorFlags::Contains, 1);
	UWidgetBlueprint* Reimported = DesignImportUtils::GenerateWidgetBlueprint(
		FDesign2UMGConverter::Convert(Updated, FDesign2UMGConversionSettings::Defaults()), Folder, &Settings, &Output);
	TestNull(TEXT("Invalid mapped target fails the component batch reimport"), Reimported);
	TestTrue(TEXT("Class failure reaches batch diagnostics"), Output.MappingDiagnostics.ContainsByPredicate(
		[](const FDesign2UMGMappingDiagnostic& D) { return D.Kind == TEXT("mapping_class_unavailable"); }));
	TestFalse(TEXT("Failed batch has no completion or replacement success"), Output.MappingDiagnostics.ContainsByPredicate(
		[](const FDesign2UMGMappingDiagnostic& D) { return D.Kind == TEXT("reimport_kind_changed") || D.Kind == TEXT("reimport_completed"); }));
	for (UWidget* Widget : OriginalWidgets)
	{
		TestTrue(TEXT("Original widget identity preserved"), Blueprint->WidgetTree->FindWidget(Widget->GetFName()) == Widget);
	}
	TestEqual(TEXT("No orphan widgets after failed batch"), Blueprint->GetAllSourceWidgets().Num(), OriginalWidgets.Num());
	TestTrue(TEXT("Previous fallback text retained"), FindText(Blueprint->WidgetTree).Contains(TEXT("Cancel")));
	TestFalse(TEXT("Failed reimport did not apply changed text"), FindText(Blueprint->WidgetTree).Contains(TEXT("Updated fallback")));
	TArray<uint8> AssetAfter, SidecarAfter;
	TestTrue(TEXT("Saved asset remains readable"), FFileHelper::LoadFileToArray(AssetAfter, *AssetPath));
	TestTrue(TEXT("Saved manifest remains readable"), FFileHelper::LoadFileToArray(SidecarAfter, *SidecarPath));
	TestTrue(TEXT("Asset bytes unchanged"), AssetAfter == AssetBefore);
	TestTrue(TEXT("Manifest bytes unchanged"), SidecarAfter == SidecarBefore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentRemovedSlotUserContentTest,
	"Design2UMG.ComponentBlueprints.RemovedSlotUserContent", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FComponentRemovedSlotUserContentTest::RunTest(const FString&)
{
	TSharedPtr<FJsonObject> Json;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(MakeComponentImportJson(TEXT("Cancel"))), Json);
	const auto Definition = Json->GetObjectField(TEXT("design2umgComponentDependencies"))->GetArrayField(TEXT("definitions"))[0]->AsObject()->GetObjectField(TEXT("document"));
	TArray<TSharedPtr<FJsonValue>> Bodies = Json->GetObjectField(TEXT("document"))->GetArrayField(TEXT("children"));
	Bodies.Add(MakeShared<FJsonValueObject>(Definition));
	for (const auto& Body : Bodies)
	{
		TArray<TSharedPtr<FJsonValue>> Children = Body->AsObject()->GetArrayField(TEXT("children"));
		auto Second = MakeShared<FJsonObject>(*Children[0]->AsObject());
		Second->SetStringField(TEXT("id"), Body->AsObject()->GetStringField(TEXT("id")) + TEXT(";second-label"));
		Second->SetStringField(TEXT("name"), TEXT("SecondLabel"));
		Children.Add(MakeShared<FJsonValueObject>(Second));
		Body->AsObject()->SetArrayField(TEXT("children"), Children);
	}
	FString InitialJson;
	FJsonSerializer::Serialize(Json.ToSharedRef(), TJsonWriterFactory<>::Create(&InitialJson));
	FDesignDocument Document;
	if (!FFigmaParser::ParseDocument(InitialJson, Document)) return false;
	const FString Folder = TEXT("/Game/Design2UMG/Automation/RemovedSlot_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	FDesign2UMGGenerationSettingsSnapshot Settings;
	FDesign2UMGGenerationOutput Output;
	UWidgetBlueprint* Blueprint = DesignImportUtils::GenerateWidgetBlueprint(
		FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults()), Folder, &Settings, &Output);
	if (!TestNotNull(TEXT("Initial page"), Blueprint) || Output.Components.Num() != 1) return false;
	const auto Instances = FindInstances(Blueprint->WidgetTree);
	if (Instances.IsEmpty()) return false;
	TArray<FName> Slots;
	Instances[0]->GetSlotNames(Slots);
	const FName RemovedSlot = FUMGWidgetUtils::SanitizeWidgetName(TEXT("D2U_Content_0_1"), TEXT("NamedSlot"));
	if (!TestTrue(TEXT("Second child exposes removable slot"), Slots.Contains(RemovedSlot))) return false;
	UTextBlock* Manual = Blueprint->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ManualSlotContent"));
	Manual->SetText(FText::FromString(TEXT("Keep this user content")));
	Instances[0]->SetContentForSlot(RemovedSlot, Manual);
	Blueprint->WidgetVariableNameToGuidMap.Add(Manual->GetFName(), FGuid::NewGuid());
	if (!TestTrue(TEXT("User content saved"), FUMGGenerator::SaveWidgetBlueprint(Blueprint).IsSuccess())) return false;
	const FString ChildPackage = Output.Components[0].Blueprint->GetOutermost()->GetName();
	const FString ChildHash = Design2UMGGenerationManifest::ComputeSavedAssetHash(ChildPackage);
	const FString ParentHash = Design2UMGGenerationManifest::ComputeSavedAssetHash(Blueprint->GetOutermost()->GetName());
	for (const auto& Body : Bodies)
	{
		TArray<TSharedPtr<FJsonValue>> Children = Body->AsObject()->GetArrayField(TEXT("children"));
		Children.RemoveAt(1);
		Body->AsObject()->SetArrayField(TEXT("children"), Children);
	}
	FString UpdatedJson;
	FJsonSerializer::Serialize(Json.ToSharedRef(), TJsonWriterFactory<>::Create(&UpdatedJson));
	FDesignDocument Updated;
	if (!FFigmaParser::ParseDocument(UpdatedJson, Updated)) return false;
	AddExpectedError(TEXT("contains user changes:"), EAutomationExpectedErrorFlags::Contains, 1);
	TestNull(TEXT("Removed slot with user content blocks batch"), DesignImportUtils::GenerateWidgetBlueprint(
		FDesign2UMGConverter::Convert(Updated, FDesign2UMGConversionSettings::Defaults()), Folder, &Settings, &Output));
	TestTrue(TEXT("Conflict identifies slot"), Output.MappingDiagnostics.ContainsByPredicate(
		[](const FDesign2UMGMappingDiagnostic& D) { return D.Kind == TEXT("component_asset_conflict") && D.Message.Contains(TEXT("slot")); }));
	TestEqual(TEXT("Child asset untouched"), Design2UMGGenerationManifest::ComputeSavedAssetHash(ChildPackage), ChildHash);
	TestEqual(TEXT("Parent asset untouched"), Design2UMGGenerationManifest::ComputeSavedAssetHash(Blueprint->GetOutermost()->GetName()), ParentHash);
	TestTrue(TEXT("Old binding still present"), Instances[0]->GetContentForSlot(RemovedSlot) == Manual);
	TestTrue(TEXT("Saving after rejected update retains valid old slot"), FUMGGenerator::SaveWidgetBlueprint(Blueprint).IsSuccess());
	UUserWidget* Runtime = NewObject<UUserWidget>(GEditor->GetEditorWorldContext().World(), Blueprint->GeneratedClass);
	Runtime->TakeWidget();
	TArray<FString> Texts;
	for (UUserWidget* Instance : FindInstances(Runtime->WidgetTree)) Texts.Append(FindText(Instance->WidgetTree));
	TestTrue(TEXT("Saved manual slot content executes"), Texts.Contains(TEXT("Keep this user content")));
	Runtime->ReleaseSlateResources(true);
	const auto Manifest = MakeShared<FJsonObject>();
	Manifest->SetStringField(TEXT("asset"), Blueprint->GetPathName());
	Manifest->SetNumberField(TEXT("process"), FPlatformProcess::GetCurrentProcessId());
	FString ManifestJson;
	FJsonSerializer::Serialize(Manifest, TJsonWriterFactory<>::Create(&ManifestJson));
	return TestTrue(TEXT("Write slot reload manifest"), FFileHelper::SaveStringToFile(ManifestJson,
		*(FPaths::ProjectSavedDir() / TEXT("Design2UMG/SlotConflictPersistence.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentSlotConflictReloadTest,
	"Design2UMG.ComponentBlueprintPersistence.SlotConflictReload", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FComponentSlotConflictReloadTest::RunTest(const FString&)
{
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *(FPaths::ProjectSavedDir() / TEXT("Design2UMG/SlotConflictPersistence.json")))) return false;
	TSharedPtr<FJsonObject> Manifest;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Manifest)) return false;
	if (!TestNotEqual(TEXT("Slot verification runs in a fresh process"), static_cast<uint32>(Manifest->GetNumberField(TEXT("process"))), FPlatformProcess::GetCurrentProcessId())) return false;
	UWidgetBlueprint* Blueprint = LoadObject<UWidgetBlueprint>(nullptr, *Manifest->GetStringField(TEXT("asset")));
	if (!TestNotNull(TEXT("Reload page after rejected slot removal"), Blueprint)) return false;
	UUserWidget* Runtime = NewObject<UUserWidget>(GEditor->GetEditorWorldContext().World(), Blueprint->GeneratedClass);
	Runtime->TakeWidget();
	TArray<FString> Texts;
	for (UUserWidget* Instance : FindInstances(Runtime->WidgetTree)) Texts.Append(FindText(Instance->WidgetTree));
	TestTrue(TEXT("Reloaded manual content executes"), Texts.Contains(TEXT("Keep this user content")));
	Runtime->ReleaseSlateResources(true);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentBulkIncompleteRecoveryTest,
	"Design2UMG.CommitResult.BulkIncompleteRecovery", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FComponentBulkIncompleteRecoveryTest::RunTest(const FString&)
{
	AddExpectedError(TEXT("Error moving file"), EAutomationExpectedErrorFlags::Contains, 2);
	AddExpectedError(TEXT("GenerationManifest: failed to finalize manifest"), EAutomationExpectedErrorFlags::Contains, 2);
	for (const bool bFailUndo : {true, false})
	{
		Design2UMGEditorTests::FScopedTransactionBuffer Transactions;
		const FString Folder = TEXT("/Game/Design2UMG/Automation/IncompleteBulk_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
		ON_SCOPE_EXIT { Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(Folder); };
		FDesignDocument Document;
		if (!FFigmaParser::ParseDocument(MakeComponentImportJson(TEXT("Before")), Document)) return false;
		FDesign2UMGImportSettingsSnapshot Settings;
		Settings.ConversionSettings = FDesign2UMGConversionSettings::Defaults();
		FDesign2UMGGenerationOutput Output;
		UWidgetBlueprint* Blueprint = DesignImportUtils::GenerateWidgetBlueprint(
			FDesign2UMGConverter::Convert(Document, Settings.ConversionSettings), Folder, &Settings.GenerationSettings, &Output);
		if (!TestNotNull(TEXT("Initial batch imported"), Blueprint) || !TestEqual(TEXT("One dependency"), Output.Components.Num(), 1)) return false;
		const FString RootPackage = Blueprint->GetOutermost()->GetName();
		const FString ChildPackage = Output.Components[0].Blueprint->GetOutermost()->GetName();
		TMap<FString, TArray<uint8>> Originals;
		for (const FString& Package : {ChildPackage, RootPackage})
		{
			for (const FString& Path : {FPackageName::LongPackageNameToFilename(Package, TEXT(".uasset")),
				Design2UMGGenerationManifest::BuildSidecarPathForAsset(Package)})
			{
				if (!TestTrue(TEXT("Original file readable"), FFileHelper::LoadFileToArray(Originals.Add(Path), *Path))) return false;
			}
		}
		FDesignDocument Updated;
		if (!FFigmaParser::ParseDocument(MakeComponentImportJson(TEXT("After")).Replace(TEXT("Default"), TEXT("ChangedDefault")), Updated)) return false;
		FDesignReimportRequest Request;
		Request.TargetAssetPath = RootPackage;
		Request.SettingsSnapshot = Settings;
		const int32 UndoEntries = GEditor->Trans->GetQueueLength() - GEditor->Trans->GetUndoCount();
		Design2UMGEditorTests::FScopedFileFailures FinalizeFailure;
		FinalizeFailure.RejectMove = [](const FString& Path, const FString&) { return Path.EndsWith(TEXT(".d2u.json")); };
		Transactions.Buffer->bRejectUndo = bFailUndo;
		if (!bFailUndo)
		{
			const FString ChildFile = FPaths::ConvertRelativePathToFull(FPackageName::LongPackageNameToFilename(ChildPackage, TEXT(".uasset")));
			FinalizeFailure.RejectWrite = [ChildFile](const FString& Path) { return Path == ChildFile; };
		}
		const FDesignReimportResult Result = DesignImportPipeline::Reimport(Updated, Request);
		TestFalse(TEXT("Incomplete recovery is not success"), Result.bSucceeded);
		TestTrue(TEXT("Recovery explicitly incomplete"), Result.Commit.Status == EDesign2UMGCommitStatus::RollbackIncomplete);
		TestTrue(TEXT("Manifest restoration independent of other failures"), Result.Commit.Manifest == EDesign2UMGFileState::Restored);
		TestTrue(TEXT("Asset recovery reflects the injected failure"), Result.Commit.Asset == (bFailUndo
			? EDesign2UMGFileState::Restored : EDesign2UMGFileState::Indeterminate));
		TestTrue(TEXT("Memory recovery reflects undo failure versus skipped undo"), Result.Commit.Memory == (bFailUndo
			? EDesign2UMGMemoryState::RecoveryFailed : EDesign2UMGMemoryState::Modified));
		const FString ChildFile = FPackageName::LongPackageNameToFilename(ChildPackage, TEXT(".uasset"));
		for (const auto& Original : Originals)
		{
			TArray<uint8> Bytes;
			TestTrue(TEXT("File readable after attempted recovery"), FFileHelper::LoadFileToArray(Bytes, *Original.Key));
			if (!bFailUndo && Original.Key == ChildFile)
				TestTrue(TEXT("Skipped file restore really leaves changed bytes"), Bytes != Original.Value);
			else TestTrue(TEXT("Reported restored files match original bytes"), Bytes == Original.Value);
		}
		TestFalse(TEXT("Incomplete recovery never emits batch-restored diagnostic"), Result.ComponentMappingDiagnostics.ContainsByPredicate(
			[](const FDesign2UMGMappingDiagnostic& Diagnostic) { return Diagnostic.Kind == TEXT("component_batch_restored"); }));
		Transactions.Buffer->bRejectUndo = false;
		if (GEditor->Trans->GetQueueLength() - GEditor->Trans->GetUndoCount() > UndoEntries)
			TestTrue(TEXT("Caller can undo retained batch changes"), GEditor->UndoTransaction(false));
	}
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreviewContextDependencyGenerationTest,
	"Design2UMG.PreviewContext.DependencyGeneration", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPreviewContextDependencyGenerationTest::RunTest(const FString& Parameters)
{
	FDesignDocument Document;
	if (!TestTrue(TEXT("Parse dependencies"), FFigmaParser::ParseDocument(MakeComponentImportJson(TEXT("Cancel")), Document))) return false;
	const FUMGWidgetData Original = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	for (int32 Mode = 0; Mode < 3; ++Mode)
	{
		FUMGWidgetData Data = Original;
		if (Mode == 2) Data.RootWidget.Render.RenderOpacity = 0.8f;
		if (Mode != 0)
		{
			FPreviewRefreshInput Input;
			Input.OriginalData = MakeShared<FUMGWidgetData>(Original);
			Input.EditedJson = FUMGJsonSerializer::Serialize(Data);
			Input.bResolveImages = false;
			auto Refreshed = DesignImportPipeline::AnalyzePreviewRefresh(Input);
			if (!TestTrue(TEXT("Preview refresh with dependencies succeeds"), Refreshed.bSucceeded)) continue;
			Data = MoveTemp(Refreshed.Data);
		}
		const FString Folder = TEXT("/Game/Design2UMG/Automation/PreviewDependencies_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
		ON_SCOPE_EXIT { Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(Folder); };
		FDesign2UMGGenerationOutput Output;
		UWidgetBlueprint* Blueprint = DesignImportUtils::GenerateWidgetBlueprint(Data, Folder, nullptr, &Output);
		if (!TestNotNull(TEXT("Direct and preview paths generate"), Blueprint)) continue;
		TestEqual(TEXT("Shared component dependency is generated"), Output.Components.Num(), 1);
		TestEqual(TEXT("Both component instances are retained"), FindInstances(Blueprint->WidgetTree).Num(), 2);
	}
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentBulkSharesGeneratedTextureBudgetTest,
	"Design2UMG.ComponentBlueprints.SharedGeneratedTextureBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FComponentBulkSharesGeneratedTextureBudgetTest::RunTest(const FString&)
{
	TGuardValue<int32> RestoreBudget(UDesign2UMGSettings::Get()->GeneratedTextureBudgetMiB, 1);
	TSharedPtr<FJsonObject> Json;
	if (!FJsonSerializer::Deserialize(
		TJsonReaderFactory<>::Create(MakeComponentImportJson(TEXT("Cancel"))), Json)) return false;

	auto MakeGradientRectangle = [](const TCHAR* Id, const TCHAR* Name, const double Y)
	{
		TSharedRef<FJsonObject> Node = MakeShared<FJsonObject>();
		Node->SetStringField(TEXT("id"), Id);
		Node->SetStringField(TEXT("name"), Name);
		Node->SetStringField(TEXT("type"), TEXT("RECTANGLE"));
		TSharedRef<FJsonObject> Bounds = MakeShared<FJsonObject>();
		Bounds->SetNumberField(TEXT("x"), 0.0);
		Bounds->SetNumberField(TEXT("y"), Y);
		Bounds->SetNumberField(TEXT("width"), 400.0);
		Bounds->SetNumberField(TEXT("height"), 400.0);
		Node->SetObjectField(TEXT("absoluteBoundingBox"), Bounds);

		TSharedRef<FJsonObject> Fill = MakeShared<FJsonObject>();
		Fill->SetStringField(TEXT("type"), TEXT("GRADIENT_LINEAR"));
		Fill->SetBoolField(TEXT("visible"), true);
		TArray<TSharedPtr<FJsonValue>> Handles;
		for (const FVector2D Point : {FVector2D(0.0, 0.5), FVector2D(1.0, 0.5), FVector2D(0.0, 1.0)})
		{
			TSharedRef<FJsonObject> Handle = MakeShared<FJsonObject>();
			Handle->SetNumberField(TEXT("x"), Point.X);
			Handle->SetNumberField(TEXT("y"), Point.Y);
			Handles.Add(MakeShared<FJsonValueObject>(Handle));
		}
		Fill->SetArrayField(TEXT("gradientHandlePositions"), Handles);
		TArray<TSharedPtr<FJsonValue>> Stops;
		for (int32 Index = 0; Index < 2; ++Index)
		{
			TSharedRef<FJsonObject> Stop = MakeShared<FJsonObject>();
			Stop->SetNumberField(TEXT("position"), Index);
			TSharedRef<FJsonObject> Color = MakeShared<FJsonObject>();
			Color->SetNumberField(TEXT("r"), Index == 0 ? 1.0 : 0.0);
			Color->SetNumberField(TEXT("g"), 0.0);
			Color->SetNumberField(TEXT("b"), Index == 0 ? 0.0 : 1.0);
			Color->SetNumberField(TEXT("a"), 1.0);
			Stop->SetObjectField(TEXT("color"), Color);
			Stops.Add(MakeShared<FJsonValueObject>(Stop));
		}
		Fill->SetArrayField(TEXT("gradientStops"), Stops);
		Node->SetArrayField(TEXT("fills"), {MakeShared<FJsonValueObject>(Fill)});
		return MakeShared<FJsonValueObject>(Node);
	};

	TSharedPtr<FJsonObject> Page = Json->GetObjectField(TEXT("document"));
	TArray<TSharedPtr<FJsonValue>> PageChildren = Page->GetArrayField(TEXT("children"));
	PageChildren.Add(MakeGradientRectangle(TEXT("page-gradient"), TEXT("PageGradient"), 100.0));
	Page->SetArrayField(TEXT("children"), PageChildren);

	TSharedPtr<FJsonObject> Catalog = Json->GetObjectField(TEXT("design2umgComponentDependencies"));
	TArray<TSharedPtr<FJsonValue>> Definitions = Catalog->GetArrayField(TEXT("definitions"));
	TSharedPtr<FJsonObject> ComponentDocument = Definitions[0]->AsObject()->GetObjectField(TEXT("document"));
	TArray<TSharedPtr<FJsonValue>> ComponentChildren = ComponentDocument->GetArrayField(TEXT("children"));
	ComponentChildren.Add(MakeGradientRectangle(TEXT("component-gradient"), TEXT("ComponentGradient"), 40.0));
	ComponentDocument->SetArrayField(TEXT("children"), ComponentChildren);

	FString Source;
	FJsonSerializer::Serialize(Json.ToSharedRef(), TJsonWriterFactory<>::Create(&Source));
	FDesignDocument Document;
	if (!TestTrue(TEXT("Budget fixture parses"), FFigmaParser::ParseDocument(Source, Document))) return false;
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	const FString Folder = TEXT("/Game/Design2UMG/Automation/ComponentBudget_")
		+ FGuid::NewGuid().ToString(EGuidFormats::Digits);
	ON_SCOPE_EXIT { Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(Folder); };
	FDesign2UMGGenerationOutput Output;
	AddExpectedError(
		TEXT("Failed to generate 'WBP_"),
		EAutomationExpectedErrorFlags::Contains,
		1);
	AddExpectedError(
		TEXT("Component import failed: Failed to build component batch root: root"),
		EAutomationExpectedErrorFlags::Contains,
		1);
	TestNull(TEXT("Component and parent builds share one generated-texture budget"),
		DesignImportUtils::GenerateWidgetBlueprint(Data, Folder, nullptr, &Output));
	TestTrue(TEXT("The parent allocation reports the cumulative operation limit"),
		Output.MappingDiagnostics.ContainsByPredicate([](const FDesign2UMGMappingDiagnostic& Diagnostic)
		{
			return Diagnostic.Message.Contains(TEXT("operation limit"));
		}));
	return true;
}
#endif
