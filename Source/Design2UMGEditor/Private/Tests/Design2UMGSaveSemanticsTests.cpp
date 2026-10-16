// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGAssetRegistryAccess.h"
#include "Design2UMGWidgetPropertyAccess.h"
#include "Design2UMGAutomationTestAdapter.h"
#include "DesignImportGeneration.h"
#include "DesignBlueprintRenderPreparation.h"
#include "UMGGenerator.h"
#include "UMGWidgetTreeBuilder.h"
#include "Tests/Design2UMGTestAssetCleanup.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/RetainerBox.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "WidgetBlueprint.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "Misc/AutomationTest.h"
#include "Misc/EngineVersionComparison.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RenderingThread.h"

#if WITH_DEV_AUTOMATION_TESTS

using Design2UMGEditorTests::DeleteGeneratedAssetsAtPath;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesign2UMGSaveWidgetBlueprintFailureTest,
	"Design2UMG.Generation.SaveWidgetBlueprintFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGSaveWidgetBlueprintFailureTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("Widget Blueprint is null"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("missing valid DesignTimeSize"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Null Blueprint cannot be reported as saved"), FUMGGenerator::SaveWidgetBlueprint(nullptr).IsSuccess());

	UWidgetBlueprint* BlueprintWithoutDesignSize = NewObject<UWidgetBlueprint>(GetTransientPackage());
	TestNotNull(TEXT("Transient Blueprint created"), BlueprintWithoutDesignSize);
	if (BlueprintWithoutDesignSize)
	{
		TestFalse(
			TEXT("Blueprint without a source design size cannot be reported as saved"),
			FUMGGenerator::SaveWidgetBlueprint(BlueprintWithoutDesignSize).IsSuccess());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesign2UMGFailedSaveDiscardsUnsavedAssetTest,
	"Design2UMG.Generation.FailedSaveDiscardsUnsavedAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGFailedSaveDiscardsUnsavedAssetTest::RunTest(const FString& Parameters)
{
	const FString OutputPath = TEXT("/Game/Design2UMG/Automation/FailedSaveCleanupV2");
	DeleteGeneratedAssetsAtPath(OutputPath);

	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_FailedSaveCleanupV2");
	Data.RootWidget.Type = TEXT("CanvasPanel");
	Data.RootWidget.Id = TEXT("root");
	Data.RootWidget.Name = TEXT("Root");
	Data.RootWidget.Bounds.W = 96.0f;
	Data.RootWidget.Bounds.H = 64.0f;

	UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, OutputPath);
	if (!TestNotNull(TEXT("Blueprint is built before the forced save failure"), Blueprint))
	{
		return false;
	}

	const FSoftObjectPath OriginalObjectPath(Blueprint);
	UPackage* TargetPackage = Blueprint->GetOutermost();
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	UUserWidget* GeneratedCDO = Blueprint->GeneratedClass
		? Cast<UUserWidget>(Blueprint->GeneratedClass->GetDefaultObject())
		: nullptr;
	if (!TestNotNull(TEXT("Generated Blueprint CDO exists"), GeneratedCDO))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	GeneratedCDO->DesignTimeSize = FVector2D::ZeroVector;

	AddExpectedError(TEXT("missing valid DesignTimeSize"), EAutomationExpectedErrorFlags::Contains, 2);
	TestFalse(TEXT("Invalid Blueprint save fails"), FUMGGenerator::SaveWidgetBlueprint(Blueprint).IsSuccess());
	TestFalse(TEXT("Failed save leaves no registered asset"),
		Design2UMGAssetRegistryAccess::GetAssetByObjectPath(AssetRegistry, OriginalObjectPath).IsValid());
	TestFalse(TEXT("Failed save clears the target package dirty flag"), TargetPackage->IsDirty());
	TestEqual(TEXT("Failed save moves the Blueprint out of the target package"), Blueprint->GetOutermost(), GetTransientPackage());

	Data.WidgetName = TEXT("WBP_ExistingFailedSave");
	UWidgetBlueprint* ExistingBlueprint = FUMGGenerator::BuildWidgetBlueprint(Data, OutputPath);
	if (!TestNotNull(TEXT("Existing Blueprint is built before its baseline save"), ExistingBlueprint))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestTrue(TEXT("Existing Blueprint baseline save succeeds"), FUMGGenerator::SaveWidgetBlueprint(ExistingBlueprint).IsSuccess());
	UPackage* ExistingPackage = ExistingBlueprint->GetOutermost();
	const FSoftObjectPath ExistingObjectPath(ExistingBlueprint);
	TestTrue(TEXT("Existing Blueprint is registered before save"),
		Design2UMGAssetRegistryAccess::GetAssetByObjectPath(AssetRegistry, ExistingObjectPath).IsValid());
	UUserWidget* ExistingCDO = ExistingBlueprint->GeneratedClass
		? Cast<UUserWidget>(ExistingBlueprint->GeneratedClass->GetDefaultObject())
		: nullptr;
	if (!TestNotNull(TEXT("Existing Blueprint CDO exists"), ExistingCDO))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	ExistingCDO->DesignTimeSize = FVector2D::ZeroVector;
	TestFalse(TEXT("Invalid existing Blueprint save fails"), FUMGGenerator::SaveWidgetBlueprint(ExistingBlueprint).IsSuccess());
	TestEqual(TEXT("Failed save keeps an existing Blueprint in its package"), ExistingBlueprint->GetOutermost(), ExistingPackage);
	TestTrue(TEXT("Failed save keeps an existing Blueprint registered"),
		Design2UMGAssetRegistryAccess::GetAssetByObjectPath(AssetRegistry, ExistingObjectPath).IsValid());

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesign2UMGInvalidGenerationClearsArtifactOutputsTest,
	"Design2UMG.Generation.InvalidGenerationClearsArtifactOutputs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGInvalidGenerationClearsArtifactOutputsTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("source data did not provide a valid root design size"), EAutomationExpectedErrorFlags::Contains, 2);
	AddExpectedError(TEXT("Root design size is invalid"), EAutomationExpectedErrorFlags::Contains, 2);
	AddExpectedError(TEXT("Widget Blueprint build failed"), EAutomationExpectedErrorFlags::Contains, 1);

	FUMGWidgetData InvalidData;
	InvalidData.WidgetName = TEXT("WBP_InvalidSaveSemantics");

	FArtifactGenerationRequest Request;
	Request.OutputPath = TEXT("/Game/Design2UMG/Automation/SaveSemantics");
	FArtifactGenerationResult Result = DesignImportGeneration::GenerateBlueprintAndPng(InvalidData, Request);
	TestFalse(TEXT("Invalid source data fails Blueprint and PNG generation"), Result.bSucceeded);
	TestTrue(TEXT("Failed generation has no generated asset path"), Result.GeneratedAssetPath.IsEmpty());
	TestTrue(TEXT("Failed generation has no render path"), Result.RenderPngPath.IsEmpty());

	InvalidData.WidgetName = TEXT("WBP_InvalidCompiledSaveSemantics");
	Request.Generation.CompiledPngFilePath = TEXT("G:/invalid/not-written.png");
	Result = DesignImportGeneration::GenerateBlueprintAndCompiledPng(InvalidData, Request);
	TestFalse(TEXT("Invalid source data fails compiled PNG generation"), Result.bSucceeded);
	TestTrue(TEXT("Failed compiled generation has no generated asset path"), Result.GeneratedAssetPath.IsEmpty());
	TestEqual(TEXT("Failed compiled generation has no rendered size"), Result.RenderedSize, FIntPoint::ZeroValue);
	return true;
}

namespace
{
	FUMGWidgetData MakeSaveSemanticsWidgetData(const TCHAR* WidgetName)
	{
		FUMGWidgetData Data;
		Data.WidgetName = WidgetName;
		Data.RootWidget.Type = TEXT("CanvasPanel");
		Data.RootWidget.Id = TEXT("root");
		Data.RootWidget.Name = TEXT("Root");
		Data.RootWidget.Bounds.W = 96.0f;
		Data.RootWidget.Bounds.H = 64.0f;
		return Data;
	}
}

// A reload (ResetLoaders + ReloadPackage + CollectGarbage) of a blueprint whose
// package has unsaved changes would discard those changes. The pre-render reload
// must skip dirty packages and keep rendering the in-memory version.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesign2UMGReloadSkipsDirtyBlueprintPackageTest,
	"Design2UMG.Generation.ReloadSkipsDirtyBlueprintPackage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGReloadSkipsDirtyBlueprintPackageTest::RunTest(const FString& Parameters)
{
	Design2UMGAutomationTestAdapter::FScopedExpectedMessages ExpectedLogMessages(*this);
	const FString OutputPath = TEXT("/Game/Design2UMG/Automation/ReloadDirtyGuard");
	DeleteGeneratedAssetsAtPath(OutputPath);

	FUMGWidgetData Data = MakeSaveSemanticsWidgetData(TEXT("WBP_ReloadDirtyGuard"));
	UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, OutputPath);
	if (!TestNotNull(TEXT("Blueprint is built before the dirty reload guard test"), Blueprint))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	if (!TestTrue(TEXT("Baseline save succeeds"), FUMGGenerator::SaveWidgetBlueprint(Blueprint).IsSuccess()))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	const FString AssetObjectPath = Blueprint->GetPathName();
	UWidgetBlueprint* Loaded = LoadObject<UWidgetBlueprint>(nullptr, *AssetObjectPath);
	if (!TestNotNull(TEXT("Saved blueprint loads"), Loaded))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	UPackage* Package = Loaded->GetOutermost();

	UUserWidget* LoadedCDO = Loaded->GeneratedClass
		? Cast<UUserWidget>(Loaded->GeneratedClass->GetDefaultObject())
		: nullptr;
	const FVector2D UnsavedSize(321.0f, 123.0f);
	if (TestNotNull(TEXT("Loaded blueprint CDO exists"), LoadedCDO))
	{
		LoadedCDO->Modify();
		LoadedCDO->DesignTimeSize = UnsavedSize;
	}
	Package->MarkPackageDirty();
	if (!TestTrue(TEXT("Package is dirty before the reload"), Package->IsDirty()))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	ExpectedLogMessages.Expect(TEXT("has unsaved changes; skipping pre-render reload"),
		ELogVerbosity::Warning,
		Design2UMGAutomationTestAdapter::EMessageMatch::Contains,
		1);
	const TWeakObjectPtr<UImage> UnreferencedObject(NewObject<UImage>(GetTransientPackage()));
	UWidgetBlueprint* Kept = DesignBlueprintRenderPreparation::ReloadWidgetBlueprintForRender(Loaded);
	TestTrue(TEXT("Absent preparation does not collect transient objects for a dirty blueprint"), UnreferencedObject.IsValid());
	TestTrue(TEXT("Dirty package skips the reload and keeps the in-memory instance"), Kept == Loaded);
	TestTrue(TEXT("Dirty package stays dirty after the skipped reload"), Package->IsDirty());
	if (LoadedCDO)
	{
		TestEqual(TEXT("Unsaved in-memory edit survives the skipped reload"), LoadedCDO->DesignTimeSize, UnsavedSize);
	}

	// Clean-package control: after saving the edit, the reload path must run again.
	if (!TestTrue(TEXT("Save after the skipped reload succeeds"), FUMGGenerator::SaveWidgetBlueprint(Loaded).IsSuccess()))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	if (!TestFalse(TEXT("Saved package is clean"), Package->IsDirty()))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	UWidgetBlueprint* Reloaded = DesignBlueprintRenderPreparation::ReloadWidgetBlueprintForRender(Loaded);
	if (!TestNotNull(TEXT("Clean package reloads the blueprint"), Reloaded))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestTrue(TEXT("Clean package reload produces a fresh instance"), Reloaded != Loaded);
	if (UUserWidget* ReloadedCDO = Reloaded->GeneratedClass
		? Cast<UUserWidget>(Reloaded->GeneratedClass->GetDefaultObject())
		: nullptr)
	{
		TestEqual(TEXT("Reloaded blueprint carries the saved edit from disk"), ReloadedCDO->DesignTimeSize, UnsavedSize);
	}

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(
	FDesign2UMGReloadRenderPreparationTest,
	"Design2UMG.Generation.ReloadRenderPreparation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

void FDesign2UMGReloadRenderPreparationTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	for (const TCHAR* Name : { TEXT("DirtyRefresh"), TEXT("CleanRefresh"), TEXT("DirtyReset"), TEXT("DirtyPreparationFailure"), TEXT("CleanPreparationFailure") })
	{
		OutBeautifiedNames.Add(Name);
		OutTestCommands.Add(Name);
	}
}

bool FDesign2UMGReloadRenderPreparationTest::RunTest(const FString& Parameters)
{
	Design2UMGAutomationTestAdapter::FScopedExpectedMessages ExpectedLogMessages(*this);
	const bool bDirtyBlueprint = Parameters.StartsWith(TEXT("Dirty"));
	const bool bFailPreparation = Parameters.EndsWith(TEXT("Failure"));
	const FString OutputPath = TEXT("/Game/Design2UMG/Automation/RenderPreparation/") + Parameters;
	const FString TexturePackageName = OutputPath / TEXT("D2U_PathTextures/T_RenderPreparation");
	DeleteGeneratedAssetsAtPath(OutputPath);
	ON_SCOPE_EXIT
	{
		if (UPackage* TexturePackage = FindPackage(nullptr, *TexturePackageName))
		{
			TexturePackage->ClearPackageFlags(PKG_InMemoryOnly);
		}
		DeleteGeneratedAssetsAtPath(OutputPath);
	};

	UPackage* TexturePackage = CreatePackage(*TexturePackageName);
	UTexture2D* Texture = NewObject<UTexture2D>(TexturePackage, TEXT("T_RenderPreparation"), RF_Public | RF_Standalone);
	FAssetRegistryModule::AssetCreated(Texture);
	const FColor Pixel = FColor::White;
	Texture->Source.Init(1, 1, 1, 1, TSF_BGRA8, reinterpret_cast<const uint8*>(&Pixel));
	Texture->SRGB = true;
	const FString TextureFilename = FPackageName::LongPackageNameToFilename(TexturePackageName, FPackageName::GetAssetPackageExtension());
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(TextureFilename), true);
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	if (!TestTrue(TEXT("Texture fixture saves"), UPackage::SavePackage(TexturePackage, Texture, *TextureFilename, SaveArgs)))
	{
		return false;
	}

	UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(MakeSaveSemanticsWidgetData(TEXT("WBP_RenderPreparation")), OutputPath);
	if (!TestNotNull(TEXT("Blueprint fixture builds"), Blueprint))
	{
		return false;
	}
	UImage* Image = Blueprint->WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("PreparedImage"));
	Image->SetBrushFromTexture(Texture);
	CastChecked<UPanelWidget>(Blueprint->WidgetTree->RootWidget)->AddChild(Image);
	if (!TestTrue(TEXT("Blueprint fixture saves"), FUMGGenerator::SaveWidgetBlueprint(Blueprint).IsSuccess()))
	{
		return false;
	}
	const FString BlueprintPath = Blueprint->GetPathName();
	const TWeakObjectPtr<UWidgetBlueprint> OriginalBlueprint(Blueprint);
	UPackage* BlueprintPackage = Blueprint->GetOutermost();
	UUserWidget* CDO = CastChecked<UUserWidget>(Blueprint->GeneratedClass->GetDefaultObject());
	const FVector2D UnsavedSize(321.0f, 123.0f);
	if (bDirtyBlueprint)
	{
		CDO->DesignTimeSize = UnsavedSize;
		BlueprintPackage->MarkPackageDirty();
		// No test-owned strong reference may hide a missing GC guard in preparation.
		Blueprint->ClearFlags(RF_Standalone);
		ExpectedLogMessages.Expect(TEXT("has unsaved changes; skipping pre-render reload"), ELogVerbosity::Warning, Design2UMGAutomationTestAdapter::EMessageMatch::Contains, 1);
	}
	TestEqual(TEXT("Fixture blueprint dirty state"), BlueprintPackage->IsDirty(), bDirtyBlueprint);
	Texture->SRGB = false;
	TestFalse(TEXT("Texture memory edit is not marked dirty"), TexturePackage->IsDirty());
	// Keep only the failing resource alive across the requested reset; do not retain the blueprint.
	const TStrongObjectPtr<UTexture2D> FailureTexture(bFailPreparation ? Texture : nullptr);
	if (bFailPreparation)
	{
		TexturePackage->SetPackageFlags(PKG_InMemoryOnly);
		ExpectedLogMessages.Expect(TEXT("marked PKG_InMemoryOnly"), ELogVerbosity::Warning, Design2UMGAutomationTestAdapter::EMessageMatch::Contains, 1);
	}
	const TWeakObjectPtr<UImage> UnreferencedObject(NewObject<UImage>(GetTransientPackage()));
	FBlueprintRenderPreparationOptions Preparation;
	Preparation.bResetTransientRenderState = bFailPreparation || Parameters.EndsWith(TEXT("Reset"));
	Preparation.bRefreshGeneratedTextures = !bFailPreparation;
	Preparation.ImportedFontPackagePath = bFailPreparation ? FPaths::GetPath(TexturePackageName) : FString();
	FString PreparationError;
	UWidgetBlueprint* Prepared = DesignBlueprintRenderPreparation::ReloadWidgetBlueprintForRender(Blueprint, &Preparation, &PreparationError);
	TestFalse(TEXT("Resource preparation collects unreferenced transient objects"), UnreferencedObject.IsValid());
	if (bFailPreparation)
	{
		TestNull(TEXT("Resource preparation failure is returned"), Prepared);
		TestTrue(TEXT("Preparation error identifies failed resource package"), PreparationError.Contains(TexturePackageName));
	}
	else
	{
		if (!TestNotNull(TEXT("Prepared blueprint is returned"), Prepared))
		{
			return false;
		}
		TestTrue(TEXT("Successful preparation has no error"), PreparationError.IsEmpty());
		UImage* PreparedImage = Cast<UImage>(Prepared->WidgetTree->FindWidget(TEXT("PreparedImage")));
		UTexture2D* PreparedTexture = PreparedImage ? Cast<UTexture2D>(Design2UMGWidgetPropertyAccess::GetBrush(*PreparedImage).GetResourceObject()) : nullptr;
		if (TestNotNull(TEXT("Image retains its texture through package reload"), PreparedTexture))
		{
			TestTrue(TEXT("Texture was refreshed from its saved package"), PreparedTexture->SRGB);
			TestNotNull(TEXT("Refreshed texture has a render resource"), PreparedTexture->GetResource());
		}
	}
	if (bDirtyBlueprint)
	{
		if (TestTrue(TEXT("Dirty blueprint survives preparation garbage collection"), OriginalBlueprint.IsValid()))
		{
			UWidgetBlueprint* Kept = OriginalBlueprint.Get();
			TestTrue(TEXT("Dirty blueprint remains the registered in-memory asset"), FindObject<UWidgetBlueprint>(nullptr, *BlueprintPath) == Kept);
			TestTrue(TEXT("Preparation preserves dirty state"), Kept->GetOutermost()->IsDirty());
			TestEqual(TEXT("Preparation preserves the unsaved CDO edit"), CastChecked<UUserWidget>(Kept->GeneratedClass->GetDefaultObject())->DesignTimeSize, UnsavedSize);
			if (!bFailPreparation)
			{
				TestTrue(TEXT("Dirty blueprint is not reloaded"), Prepared == Kept);
			}
		}
	}
	else if (Prepared)
	{
		TestFalse(TEXT("Clean blueprint remains clean"), Prepared->GetOutermost()->IsDirty());
		TestFalse(TEXT("Clean blueprint was reloaded"), OriginalBlueprint.IsValid());
	}
	return true;
}

// Generation saves the asset to disk; a leftover editor transaction would let
// Ctrl+Z roll the in-memory state back while the saved .uasset keeps the import,
// leaving memory and disk inconsistent. Generation must not touch the undo stack.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesign2UMGGenerationLeavesNoUndoTransactionTest,
	"Design2UMG.Generation.LeavesNoUndoTransaction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGGenerationLeavesNoUndoTransactionTest::RunTest(const FString& Parameters)
{
	UTransactor* Transactor = GEditor ? GEditor->Trans.Get() : nullptr;
	if (!TestNotNull(TEXT("Editor transactor is available"), Transactor))
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/Automation/GenerationNoUndo");
	DeleteGeneratedAssetsAtPath(OutputPath);

	FUMGWidgetData Data = MakeSaveSemanticsWidgetData(TEXT("WBP_GenerationNoUndo"));
	const int32 UndoCountBefore = Transactor->GetUndoCount();
	UWidgetBlueprint* Generated = DesignImportGeneration::GenerateBlueprint(Data, OutputPath);
	if (!TestNotNull(TEXT("Blueprint generation succeeds"), Generated))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestEqual(
		TEXT("Generation leaves no undo transaction behind"),
		Transactor->GetUndoCount(),
		UndoCountBefore);

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

// A missing function in a live execution chain forces the real Kismet compiler to fail.
// Merely arming the save-failure hook would not exercise this boundary.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBlueprintCompileFailurePreservesDiskTest,
	"Design2UMG.Generation.CompileFailurePreservesDisk",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBlueprintCompileFailurePreservesDiskTest::RunTest(const FString&)
{
	const FString Folder = TEXT("/Game/Design2UMG/_TestOutput/CompileFailure");
	DeleteGeneratedAssetsAtPath(Folder);
	ON_SCOPE_EXIT { DeleteGeneratedAssetsAtPath(Folder); };
	AddExpectedError(TEXT("D2UMissingFunction"), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("SaveWidgetBlueprint: compilation failed"), EAutomationExpectedErrorFlags::Contains, 2);
	for (const bool bExisting : {false, true})
	{
		UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(
			MakeSaveSemanticsWidgetData(bExisting ? TEXT("WBP_ExistingCompileFailure") : TEXT("WBP_NewCompileFailure")), Folder);
		if (!TestNotNull(TEXT("Valid blueprint is built before graph corruption"), Blueprint)) return false;
		UPackage* Package = Blueprint->GetOutermost();
		const FString File = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		TArray<uint8> Before;
		if (bExisting)
		{
			if (!TestTrue(TEXT("Existing asset baseline saves"), FUMGGenerator::SaveWidgetBlueprint(Blueprint).IsSuccess())
				|| !TestTrue(TEXT("Existing bytes can be read"), FFileHelper::LoadFileToArray(Before, *File))) return false;
		}

		UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
		if (!TestNotNull(TEXT("Event graph exists"), Graph)) return false;
		UK2Node_CustomEvent* Event = NewObject<UK2Node_CustomEvent>(Graph);
		Event->CustomFunctionName = TEXT("InvokeBrokenCall");
		Graph->AddNode(Event);
		Event->CreateNewGuid();
		Event->AllocateDefaultPins();
		UK2Node_CallFunction* Call = NewObject<UK2Node_CallFunction>(Graph);
		Call->SetFromFunction(UKismetSystemLibrary::StaticClass()->FindFunctionByName(
			GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString)));
		Graph->AddNode(Call);
		Call->CreateNewGuid();
		Call->AllocateDefaultPins();
		if (!TestTrue(TEXT("Invalid call is on an executable path"), GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(
			Event->FindPinChecked(UEdGraphSchema_K2::PN_Then), Call->FindPinChecked(UEdGraphSchema_K2::PN_Execute)))) return false;
		Call->FunctionReference.SetExternalMember(TEXT("D2UMissingFunction"), UKismetSystemLibrary::StaticClass());

		const FDesign2UMGCommitResult Result = FUMGGenerator::SaveWidgetBlueprint(Blueprint);
		TestEqual(TEXT("The real compiler rejects the graph"), Blueprint->Status, BS_Error);
		TestFalse(TEXT("Compilation failure cannot be reported as success"), Result.IsSuccess());
		TestEqual(TEXT("Compilation failure never commits asset bytes"), Result.Asset, EDesign2UMGFileState::Unchanged);
		if (bExisting)
		{
			TArray<uint8> After;
			TestTrue(TEXT("Original file remains readable"), FFileHelper::LoadFileToArray(After, *File));
			TestTrue(TEXT("Original asset bytes are unchanged"), Before == After);
			TestEqual(TEXT("Existing asset is retained in its package"), Blueprint->GetOutermost(), Package);
		}
		else
		{
			TestFalse(TEXT("Failed new asset never reaches disk"), FPaths::FileExists(File));
			TestEqual(TEXT("Unsaved failed asset is discarded"), Blueprint->GetOutermost(), GetTransientPackage());
		}
		// Remove the deliberately invalid source before editor cleanup can compile it again.
		Graph->RemoveNode(Call);
		Graph->RemoveNode(Event);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTemplateSlateFailedSavePreservesStateTest,
	"Design2UMG.Generation.TemplateSlateFailedSavePreservesState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTemplateSlateFailedSavePreservesStateTest::RunTest(const FString&)
{
	const FString Folder = TEXT("/Game/Design2UMG/_TestOutput/TemplateSlateFailedSave_")
		+ FGuid::NewGuid().ToString(EGuidFormats::Digits);
	UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(
		MakeSaveSemanticsWidgetData(TEXT("WBP_TemplateSlateFailedSave")), Folder);
	if (!TestNotNull(TEXT("Blueprint exists before save failure"), Blueprint)) return false;
	Blueprint->AddToRoot();
	ON_SCOPE_EXIT
	{
		Blueprint->WidgetTree->ForEachWidget([](UWidget* Widget) { Widget->ReleaseSlateResources(true); });
		FlushRenderingCommands();
		Blueprint->RemoveFromRoot();
		DeleteGeneratedAssetsAtPath(Folder);
	};
	URetainerBox* Retainer = Blueprint->WidgetTree->ConstructWidget<URetainerBox>();
	UTextBlock* Text = Blueprint->WidgetTree->ConstructWidget<UTextBlock>();
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
	Blueprint->WidgetVariableNameToGuidMap.Add(Blueprint->WidgetTree->RootWidget->GetFName(), FGuid::NewGuid());
	Blueprint->WidgetVariableNameToGuidMap.Add(Retainer->GetFName(), FGuid::NewGuid());
	Blueprint->WidgetVariableNameToGuidMap.Add(Text->GetFName(), FGuid::NewGuid());
#endif
	CastChecked<UPanelWidget>(Blueprint->WidgetTree->RootWidget)->AddChild(Retainer);
	Retainer->SetContent(Text);
	FUMGWidgetNode RetainerNode;
	RetainerNode.Type = TEXT("RetainerBox");
	RetainerNode.Effect.bRetainRendering = true;
	RetainerNode.Effect.EffectMaterial = TEXT("d2u.retainer.inner_shadow");
	RetainerNode.Effect.TextureParameter = TEXT("Texture");
	RetainerNode.Effect.EffectColor = FLinearColor(0.1f, 0.2f, 0.3f, 0.4f);
	RetainerNode.Effect.EffectOffset = FVector2D(0.05f, -0.03f);
	RetainerNode.Effect.EffectBlurUVStep = FVector2D(0.01f, 0.02f);
	FUMGWidgetTreeBuilder::ApplyWidgetProperties(Retainer, RetainerNode);
	Retainer->TakeWidget();
	const UMaterialInterface* Material = Design2UMGWidgetPropertyAccess::GetRetainerEffectMaterialInterface(*Retainer);
	UMaterialInstanceDynamic* Effect = Retainer->GetEffectMaterial();
	if (!TestNotNull(TEXT("Typed effect material exists"), Effect)) return false;
	const FLinearColor ExpectedBlur = Effect->K2_GetVectorParameterValue(TEXT("BlurUVStep"));
	Text->SetText(FText::FromString(TEXT("Saved text")));
	if (!TestTrue(TEXT("Baseline asset saves"), FUMGGenerator::SaveWidgetBlueprint(Blueprint).IsSuccess())) return false;
	UPackage* Package = Blueprint->GetOutermost();
	const FString File = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	TArray<uint8> Before;
	if (!TestTrue(TEXT("Saved bytes are readable"), FFileHelper::LoadFileToArray(Before, *File))) return false;
	const FSlateFontInfo SavedFont = Design2UMGWidgetPropertyAccess::GetFont(*Text);
	FSlateFontInfo UnsavedFont = SavedFont;
	UnsavedFont.Size += 3;
	Text->Modify();
	Text->SetFont(UnsavedFont);
	Text->SetText(FText::FromString(TEXT("Unsaved text")));
	UUserWidget* CDO = CastChecked<UUserWidget>(Blueprint->GeneratedClass->GetDefaultObject());
	CDO->Modify();
	CDO->DesignTimeSize = FVector2D::ZeroVector;
	Package->MarkPackageDirty();
	Retainer->TakeWidget();
	TestTrue(TEXT("Retainer Slate is materialized before failure"), Retainer->GetCachedWidget().IsValid());
	TestTrue(TEXT("Text Slate is materialized before failure"), Text->GetCachedWidget().IsValid());
	AddExpectedError(TEXT("missing valid DesignTimeSize"), EAutomationExpectedErrorFlags::Contains, 1);
	const FDesign2UMGCommitResult Result = FUMGGenerator::SaveWidgetBlueprint(Blueprint);
	TestFalse(TEXT("Invalid design size produces a real save failure"), Result.IsSuccess());
	TestFalse(TEXT("Failed save releases Retainer Slate"), Retainer->GetCachedWidget().IsValid());
	TestFalse(TEXT("Failed save releases Text Slate"), Text->GetCachedWidget().IsValid());
	TestEqual(TEXT("Existing Blueprint retains package identity"), Blueprint->GetOutermost(), Package);
	TestTrue(TEXT("Existing package retains unsaved changes"), Package->IsDirty());
	TestEqual(TEXT("Unsaved text survives"), Text->GetText().ToString(), FString(TEXT("Unsaved text")));
	TestEqual(TEXT("Font asset survives"), Design2UMGWidgetPropertyAccess::GetFont(*Text).FontObject, SavedFont.FontObject);
	TestEqual(TEXT("Unsaved font size survives"), Design2UMGWidgetPropertyAccess::GetFont(*Text).Size, UnsavedFont.Size);
	TestTrue(TEXT("Font asset was exercised"), SavedFont.FontObject != nullptr);
	TestTrue(TEXT("Material property survives"), Design2UMGWidgetPropertyAccess::GetRetainerEffectMaterialInterface(*Retainer) == Material);
	TestEqual(TEXT("Unsaved CDO edit survives"), CDO->DesignTimeSize, FVector2D::ZeroVector);
	TArray<uint8> After;
	TestTrue(TEXT("Disk version remains readable"), FFileHelper::LoadFileToArray(After, *File));
	TestTrue(TEXT("Disk version is unchanged"), Before == After);
	Retainer->TakeWidget();
	UMaterialInstanceDynamic* RestoredEffect = Retainer->GetEffectMaterial();
	if (TestNotNull(TEXT("Typed effect rebuilds after save failure"), RestoredEffect))
	{
		TestTrue(TEXT("Typed material parameters survive save failure and Slate rebuild"),
			RestoredEffect->K2_GetVectorParameterValue(TEXT("BlurUVStep")).Equals(ExpectedBlur, 0.0001f));
	}
	return true;
}

#endif
