// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"
#include "DesignImportPipeline.h"
#include "PenParser.h"
#include "Design2UMGSettings.h"
#include "DesignPreviewMaterializationService.h"
#include "Design2UMGGenerationSettingsSnapshot.h"
#include "ExternalServices/PenMCPBridge.h"
#include "Tests/Design2UMGTestAssetCleanup.h"
#include "Design2UMGTestNodes.h"
#include "UMGGenerator.h"
#include "UMGWidgetUtils.h"

#include "Blueprint/WidgetTree.h"
#include "Components/BackgroundBlur.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "WidgetBlueprint.h"

#if WITH_AUTOMATION_WORKER

using Design2UMGEditorTests::DeleteGeneratedAssetsAtPath;

namespace
{
	UWidgetTree* CreateTransientWidgetTree()
	{
		UWidgetBlueprint* Blueprint = NewObject<UWidgetBlueprint>(GetTransientPackage(), NAME_None, RF_Transient);
		if (!Blueprint)
		{
			return nullptr;
		}

		Blueprint->WidgetTree = NewObject<UWidgetTree>(Blueprint, UWidgetTree::StaticClass(), NAME_None, RF_Transient);
		return Blueprint->WidgetTree;
	}

	FUMGWidgetNode MakeSimpleTextNode(const FString& NodeName)
	{
		FUMGWidgetNode Node;
		Node.Type = TEXT("TextBlock");
		Node.Id = TEXT("settings_boundary_text");
		Node.Name = NodeName;
		Node.Bounds = { 0.0f, 0.0f, 120.0f, 32.0f };
		Node.Text.Text = TEXT("Settings boundary");
		return Node;
	}

	// Semantic-placeholder specialization of the shared MakeNode: fixed source sizes via
	// SetWidthValue/SetHeightValue (same bHas*/bSourceHas* contract), no authored position,
	// plus the placeholder-surface semantic flag.
	TSharedPtr<FDesignNode> MakePlaceholderNode(const FString& NodeId)
	{
		TSharedPtr<FDesignNode> Node = Design2UMGEditorTests::MakeNode(
			NodeId, NodeId, EDesignNodeType::Frame, 120.0f, 48.0f);
		Node->LayoutData.bSemanticPlaceholderSurface = true;
		return Node;
	}

	const FUMGWidgetNode* FindWidgetNodeByMaterialParamFragment(
		const FUMGWidgetNode& Node,
		const FString& Fragment)
	{
		if (Node.Brush.MaterialParams.IsSet()
			&& Node.Brush.MaterialParams.GetValue().Contains(Fragment))
		{
			return &Node;
		}

		for (const FUMGWidgetNode& Child : Node.Children)
		{
			if (const FUMGWidgetNode* Match = FindWidgetNodeByMaterialParamFragment(Child, Fragment))
			{
				return Match;
			}
		}

		return nullptr;
	}

	FDesignPreviewMaterializationSettingsSnapshot MakePreviewMaterializationSettingsSnapshot(
		const FDesign2UMGGenerationSettingsSnapshot& GenerationSnapshot)
	{
		return FDesignPreviewMaterializationSettingsSnapshot::FromGenerationSettings(GenerationSnapshot);
	}

	FString LoadPenSourceImportFixtureJson()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Design2UMG"));
		if (!Plugin.IsValid())
		{
			return FString();
		}

		const FString FixturePath = FPaths::Combine(
			Plugin->GetBaseDir(),
			TEXT("Source/Design2UMGEditor/Private/Tests/Fixtures/pencil-welcome.pen"));
		FString FixtureJson;
		return FFileHelper::LoadFileToString(FixtureJson, *FixturePath) ? FixtureJson : FString();
	}

	struct FScopedClassOverrideSettings
	{
		FScopedClassOverrideSettings(const FString& NamePattern, UClass* OverrideClass)
		{
			Settings = UDesign2UMGSettings::Get();
			if (!Settings)
			{
				return;
			}

			SavedOverrides = Settings->ClassOverrides;
			Settings->ClassOverrides.Reset();
			if (OverrideClass)
			{
				FWidgetClassOverride& Rule = Settings->ClassOverrides.AddDefaulted_GetRef();
				Rule.NamePattern = NamePattern;
				Rule.WidgetClass = OverrideClass;
			}
		}

		~FScopedClassOverrideSettings()
		{
			if (Settings)
			{
				Settings->ClassOverrides = SavedOverrides;
			}
		}

		UDesign2UMGSettings* Settings = nullptr;
		TArray<FWidgetClassOverride> SavedOverrides;
	};

	struct FScopedPlaceholderHatchSettings
	{
		FScopedPlaceholderHatchSettings(
			const FString& InBaseFillColor,
			const FString& InBaseStripeColor,
			const int32 InStripeWidth,
			const int32 InStripeGap)
		{
			Settings = UDesign2UMGSettings::Get();
			if (!Settings)
			{
				return;
			}

			bSavedUseCustomOverride = Settings->bUseCustomPlaceholderHatchStyle;
			SavedBaseFillColor = Settings->PlaceholderHatchBackgroundColor;
			SavedBaseStripeColor = Settings->PlaceholderHatchStripeColor;
			SavedStripeWidth = Settings->PlaceholderHatchStripeWidth;
			SavedStripeGap = Settings->PlaceholderHatchStripeGap;

			Settings->bUseCustomPlaceholderHatchStyle = true;
			Settings->PlaceholderHatchBackgroundColor = InBaseFillColor;
			Settings->PlaceholderHatchStripeColor = InBaseStripeColor;
			Settings->PlaceholderHatchStripeWidth = InStripeWidth;
			Settings->PlaceholderHatchStripeGap = InStripeGap;
		}

		~FScopedPlaceholderHatchSettings()
		{
			if (!Settings)
			{
				return;
			}

			Settings->bUseCustomPlaceholderHatchStyle = bSavedUseCustomOverride;
			Settings->PlaceholderHatchBackgroundColor = SavedBaseFillColor;
			Settings->PlaceholderHatchStripeColor = SavedBaseStripeColor;
			Settings->PlaceholderHatchStripeWidth = SavedStripeWidth;
			Settings->PlaceholderHatchStripeGap = SavedStripeGap;
		}

		UDesign2UMGSettings* Settings = nullptr;
		bool bSavedUseCustomOverride = false;
		FString SavedBaseFillColor;
		FString SavedBaseStripeColor;
		int32 SavedStripeWidth = 0;
		int32 SavedStripeGap = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetClassOverrideDefaultSnapshotTest,
	"Design2UMG.Unit.GenerationSettings.WidgetClassOverride.DefaultSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FWidgetClassOverrideDefaultSnapshotTest::RunTest(const FString& Parameters)
{
	UWidgetTree* WidgetTree = CreateTransientWidgetTree();
	TestNotNull(TEXT("Transient widget tree exists"), WidgetTree);
	if (!WidgetTree)
	{
		return false;
	}

	const FDesign2UMGGenerationSettingsSnapshot EmptySnapshot;
	const Design2UMGGenerationSettingsContext::FScopedSettingsSnapshot ScopedSnapshot(EmptySnapshot);
	UWidget* Widget = FUMGWidgetUtils::CreateWidgetRecursive(
		WidgetTree,
		MakeSimpleTextNode(TEXT("DefaultClassNode")));
	TestNotNull(TEXT("Widget created with default snapshot"), Widget);
	TestTrue(TEXT("Default snapshot keeps resolved TextBlock class"), Widget && Widget->IsA<UTextBlock>());
	return Widget && Widget->IsA<UTextBlock>();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetClassOverrideCustomSnapshotTest,
	"Design2UMG.Unit.GenerationSettings.WidgetClassOverride.CustomSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FWidgetClassOverrideCustomSnapshotTest::RunTest(const FString& Parameters)
{
	UWidgetTree* WidgetTree = CreateTransientWidgetTree();
	TestNotNull(TEXT("Transient widget tree exists"), WidgetTree);
	if (!WidgetTree)
	{
		return false;
	}

	FDesign2UMGGenerationSettingsSnapshot OverrideSnapshot;
	FWidgetClassOverride& OverrideRule = OverrideSnapshot.ClassOverrides.AddDefaulted_GetRef();
	OverrideRule.NamePattern = TEXT("OverrideNode");
	OverrideRule.WidgetClass = UBorder::StaticClass();

	const Design2UMGGenerationSettingsContext::FScopedSettingsSnapshot ScopedSnapshot(OverrideSnapshot);
	UWidget* Widget = FUMGWidgetUtils::CreateWidgetRecursive(
		WidgetTree,
		MakeSimpleTextNode(TEXT("OverrideNode_Content")));
	TestNotNull(TEXT("Widget created with override snapshot"), Widget);
	TestTrue(TEXT("Custom snapshot applies widget class override"), Widget && Widget->IsA<UBorder>());
	return Widget && Widget->IsA<UBorder>();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreviewMaterializationUsesSettingsSnapshotTest,
	"Design2UMG.Unit.GenerationSettings.PreviewMaterializationUsesSettingsSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPreviewMaterializationUsesSettingsSnapshotTest::RunTest(const FString& Parameters)
{
	FDesign2UMGGenerationSettingsSnapshot GenerationSnapshot;
	FWidgetClassOverride& SnapshotOverride = GenerationSnapshot.ClassOverrides.AddDefaulted_GetRef();
	SnapshotOverride.NamePattern = TEXT("PreviewOverrideNode");
	SnapshotOverride.WidgetClass = UBorder::StaticClass();

	FScopedClassOverrideSettings LiveSettings(TEXT("PreviewOverrideNode"), UButton::StaticClass());

	FPreviewApprovedSnapshot ApprovedSnapshot;
	ApprovedSnapshot.RequestId = 7;
	ApprovedSnapshot.Data.WidgetName = TEXT("WBP_PreviewSettingsBoundary");
	ApprovedSnapshot.Data.RootWidget = MakeSimpleTextNode(TEXT("PreviewOverrideNode_Content"));
	ApprovedSnapshot.MaterializationSettingsSnapshot =
		MakePreviewMaterializationSettingsSnapshot(GenerationSnapshot);

	FDesignPreviewMaterializationRequest Request;
	Request.RequestId = ApprovedSnapshot.RequestId;
	Request.ApprovedSnapshot = ApprovedSnapshot;

	FDesignPreviewMaterializationResult Result;
	FDesignPreviewMaterializationService Service;
	TestTrue(TEXT("Preview materialization succeeds"), Service.MaterializePreview(Request, Result));

	UUserWidget* PreviewWidget = Service.GetMaterializedPreviewWidget();
	TestNotNull(TEXT("Preview widget exists"), PreviewWidget);
	if (!PreviewWidget || !PreviewWidget->WidgetTree)
	{
		return false;
	}

	UWidget* RootWidget = PreviewWidget->WidgetTree->RootWidget;
	TestNotNull(TEXT("Preview root widget exists"), RootWidget);
	TestTrue(TEXT("Preview materialization applies explicit snapshot override"), RootWidget && RootWidget->IsA<UBorder>());
	TestFalse(TEXT("Preview materialization does not late-read live settings"), RootWidget && RootWidget->IsA<UButton>());
	return RootWidget && RootWidget->IsA<UBorder>();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImportPlanningUsesExplicitSettingsSnapshotTest,
	"Design2UMG.Unit.GenerationSettings.ImportPlanningUsesExplicitSettingsSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FImportPlanningUsesExplicitSettingsSnapshotTest::RunTest(const FString& Parameters)
{
	FScopedPlaceholderHatchSettings SnapshotSettings(TEXT("#123456"), TEXT("#ABCDEF"), 7, 11);
	TestNotNull(TEXT("Snapshot settings available"), SnapshotSettings.Settings);
	if (!SnapshotSettings.Settings)
	{
		return false;
	}

	const FDesign2UMGImportSettingsSnapshot SettingsSnapshot =
		FDesign2UMGImportSettingsSnapshot::FromSettings(SnapshotSettings.Settings);

	FScopedPlaceholderHatchSettings CurrentSettings(TEXT("#654321"), TEXT("#FEDCBA"), 3, 5);

	FDesignDocument Document;
	Document.Children.Add(MakePlaceholderNode(TEXT("import_planning_placeholder")));

	FImportAnalysisInput AnalysisInput;
	AnalysisInput.bResolveImages = false;
	AnalysisInput.SettingsSnapshot = &SettingsSnapshot;
	const FImportAnalysisResult Result = DesignImportPipeline::AnalyzeImport(Document, AnalysisInput);

	const FUMGWidgetNode& RootWidget = Result.Data.RootWidget;
	TestEqual(TEXT("Placeholder converts to image"), RootWidget.Type, FString(TEXT("Image")));
	TestTrue(TEXT("Import planning uses captured placeholder base fill"),
		RootWidget.Brush.MaterialParams.IsSet()
		&& RootWidget.Brush.MaterialParams.GetValue().Contains(TEXT("\"Color\":\"#123456\"")));
	TestTrue(TEXT("Import planning uses captured placeholder stripe color"),
		RootWidget.Brush.MaterialParams.IsSet()
		&& RootWidget.Brush.MaterialParams.GetValue().Contains(TEXT("\"StripeColor\":\"#ABCDEF\"")));
	TestTrue(TEXT("Import planning uses captured placeholder stripe width"),
		RootWidget.Brush.MaterialParams.IsSet()
		&& RootWidget.Brush.MaterialParams.GetValue().Contains(TEXT("\"StripeWidth\":7")));
	TestTrue(TEXT("Import planning uses captured placeholder stripe gap"),
		RootWidget.Brush.MaterialParams.IsSet()
		&& RootWidget.Brush.MaterialParams.GetValue().Contains(TEXT("\"StripeGap\":11")));
	TestFalse(TEXT("Import planning does not late-read current placeholder base fill"),
		RootWidget.Brush.MaterialParams.IsSet()
		&& RootWidget.Brush.MaterialParams.GetValue().Contains(TEXT("\"Color\":\"#654321\"")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImportExecutionUsesExplicitGenerationSettingsSnapshotTest,
	"Design2UMG.Unit.GenerationSettings.ImportExecutionUsesExplicitGenerationSettingsSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FImportExecutionUsesExplicitGenerationSettingsSnapshotTest::RunTest(const FString& Parameters)
{
	FScopedClassOverrideSettings GlobalSettings(TEXT("ShouldNotBeUsed"), nullptr);

	FDesign2UMGImportSettingsSnapshot SettingsSnapshot;
	FWidgetClassOverride& OverrideRule = SettingsSnapshot.GenerationSettings.ClassOverrides.AddDefaulted_GetRef();
	OverrideRule.NamePattern = TEXT("ExecuteOverrideNode");
	OverrideRule.WidgetClass = UBorder::StaticClass();

	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_ExecuteSettingsBoundary");
	Data.RootWidget = MakeSimpleTextNode(TEXT("ExecuteOverrideNode_Content"));

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ExecuteSettingsBoundary");
	DeleteGeneratedAssetsAtPath(OutputPath);

	FDesignImportRequest Request;
	Request.OutputPath = OutputPath;
	Request.bUsePreview = false;
	Request.bResolveImages = false;
	Request.Generation.Mode = EDesignBlueprintGenerationMode::BlueprintOnly;
	Request.SettingsSnapshot = SettingsSnapshot;

	const FDesignImportExecutionResult Result = DesignImportPipeline::Execute(Data, Request);
	TestTrue(TEXT("Import execution succeeds with explicit snapshot"), Result.bSucceeded);
	TestNotNull(TEXT("Generated blueprint returned"), Result.GeneratedBlueprint);
	if (!Result.bSucceeded || !Result.GeneratedBlueprint || !Result.GeneratedBlueprint->WidgetTree)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UWidget* RootWidget = Result.GeneratedBlueprint->WidgetTree->RootWidget;
	TestNotNull(TEXT("Generated root widget exists"), RootWidget);
	const bool bUsesCapturedGenerationOverride = RootWidget && RootWidget->IsA<UBorder>();
	TestTrue(TEXT("Import execution uses captured generation override"), bUsesCapturedGenerationOverride);
	DeleteGeneratedAssetsAtPath(OutputPath);
	return bUsesCapturedGenerationOverride;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenBridgeImportUsesSingleExplicitSettingsSnapshotTest,
	"Design2UMG.Unit.GenerationSettings.PenBridgeImportUsesSingleExplicitSettingsSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPenBridgeImportUsesSingleExplicitSettingsSnapshotTest::RunTest(const FString& Parameters)
{
	const FString PenSourceJson = LoadPenSourceImportFixtureJson();
	TestFalse(TEXT("Pen fixture json is available"), PenSourceJson.IsEmpty());
	if (PenSourceJson.IsEmpty())
	{
		return false;
	}

	FScopedPlaceholderHatchSettings SnapshotPlaceholderSettings(TEXT("#123456"), TEXT("#ABCDEF"), 7, 11);
	TestNotNull(TEXT("Snapshot placeholder settings available"), SnapshotPlaceholderSettings.Settings);
	if (!SnapshotPlaceholderSettings.Settings)
	{
		return false;
	}

	FDesign2UMGImportSettingsSnapshot SettingsSnapshot =
		FDesign2UMGImportSettingsSnapshot::FromSettings(SnapshotPlaceholderSettings.Settings);
	FWidgetClassOverride& SnapshotOverride = SettingsSnapshot.GenerationSettings.ClassOverrides.AddDefaulted_GetRef();
	SnapshotOverride.NamePattern = TEXT("Tooltip Text");
	SnapshotOverride.WidgetClass = UBackgroundBlur::StaticClass();

	FUMGWidgetData Data;
	FDesignDocument Document;
	const bool bParsed = FPenParser::ParseDocument(PenSourceJson, Document);
	if (bParsed)
	{
		FImportAnalysisInput AnalysisInput;
		AnalysisInput.bResolveImages = false;
		AnalysisInput.SettingsSnapshot = &SettingsSnapshot;
		Data = DesignImportPipeline::AnalyzeImport(Document, AnalysisInput).Data;
	}
	TestTrue(TEXT("Pen source converts using explicit snapshot"), bParsed);

	const FUMGWidgetNode* PlaceholderNode =
		FindWidgetNodeByMaterialParamFragment(Data.RootWidget, TEXT("\"Color\":\"#123456\""));
	TestNotNull(TEXT("Converted placeholder node with snapshot hatch exists"), PlaceholderNode);
	if (!PlaceholderNode)
	{
		return false;
	}

	TestEqual(TEXT("Converted placeholder becomes image"), PlaceholderNode->Type, FString(TEXT("Image")));
	TestTrue(TEXT("Bridge conversion uses captured placeholder base fill"),
		PlaceholderNode->Brush.MaterialParams.IsSet()
		&& PlaceholderNode->Brush.MaterialParams.GetValue().Contains(TEXT("\"Color\":\"#123456\"")));
	TestTrue(TEXT("Bridge conversion uses captured placeholder stripe color"),
		PlaceholderNode->Brush.MaterialParams.IsSet()
		&& PlaceholderNode->Brush.MaterialParams.GetValue().Contains(TEXT("\"StripeColor\":\"#ABCDEF\"")));

	if (Data.RootWidget.Bounds.W <= 1.0f || Data.RootWidget.Bounds.H <= 1.0f)
	{
		Data.RootWidget.Bounds = { 0.0f, 0.0f, 640.0f, 360.0f };
	}

	FScopedPlaceholderHatchSettings LivePlaceholderSettings(TEXT("#654321"), TEXT("#FEDCBA"), 3, 5);
	FScopedClassOverrideSettings LiveClassOverride(TEXT("Tooltip Text"), UButton::StaticClass());

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/PenBridgeSingleSnapshotBoundary");
	DeleteGeneratedAssetsAtPath(OutputPath);

	FDesignImportRequest Request;
	Request.OutputPath = OutputPath;
	Request.bUsePreview = false;
	Request.bResolveImages = false;
	Request.Generation.Mode = EDesignBlueprintGenerationMode::BlueprintOnly;
	Request.SettingsSnapshot = SettingsSnapshot;

	const FDesignImportExecutionResult Result = DesignImportPipeline::Execute(Data, Request);
	TestTrue(TEXT("Import execution succeeds after live settings drift"), Result.bSucceeded);
	TestNotNull(TEXT("Generated blueprint returned"), Result.GeneratedBlueprint);
	if (!Result.bSucceeded || !Result.GeneratedBlueprint || !Result.GeneratedBlueprint->WidgetTree)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UWidget* OverriddenWidget =
		Result.GeneratedBlueprint->WidgetTree->FindWidget(FName(TEXT("TooltipText")));
	TestNotNull(TEXT("Generated override widget exists"), OverriddenWidget);
	const bool bUsesCapturedClassOverride = OverriddenWidget && OverriddenWidget->IsA<UBackgroundBlur>();
	const bool bLateReadLiveClassOverride = OverriddenWidget && OverriddenWidget->IsA<UButton>();
	TestTrue(TEXT("Generation uses captured class override after live drift"),
		bUsesCapturedClassOverride);
	TestFalse(TEXT("Generation does not late-read live class override"),
		bLateReadLiveClassOverride);

	DeleteGeneratedAssetsAtPath(OutputPath);
	return bUsesCapturedClassOverride;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreviewMaterializationSnapshotCopyBoundaryTest,
	"Design2UMG.Unit.GenerationSettings.PreviewMaterializationSnapshotCopyBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPreviewMaterializationSnapshotCopyBoundaryTest::RunTest(const FString& Parameters)
{
	// Forward boundary: generation -> preview materialization carries overrides.
	FDesign2UMGGenerationSettingsSnapshot GenerationSnapshot;
	GenerationSnapshot.PrototypeNavigationGenerationMode =
		EDesign2UMGPrototypeNavigationGenerationMode::TagHelper;
	FWidgetClassOverride& Override = GenerationSnapshot.ClassOverrides.AddDefaulted_GetRef();
	Override.NamePattern = TEXT("CopyBoundaryNode");
	Override.WidgetClass = UBorder::StaticClass();

	const FDesignPreviewMaterializationSettingsSnapshot MaterializationSnapshot =
		FDesignPreviewMaterializationSettingsSnapshot::FromGenerationSettings(GenerationSnapshot);
	TestEqual(TEXT("Materialization snapshot carries the override"),
		MaterializationSnapshot.ClassOverrides.Num(), 1);
	if (MaterializationSnapshot.ClassOverrides.Num() == 1)
	{
		TestEqual(TEXT("Materialization override pattern is preserved"),
			MaterializationSnapshot.ClassOverrides[0].NamePattern, FString(TEXT("CopyBoundaryNode")));
	}

	// Reverse boundary: approved materialization -> generation keeps overrides
	// and resets fields preview does not own to their defaults.
	const FDesign2UMGGenerationSettingsSnapshot RoundTripped =
		FDesign2UMGGenerationSettingsSnapshot::FromMaterializationSettings(MaterializationSnapshot);
	TestEqual(TEXT("Round-tripped snapshot carries the override"),
		RoundTripped.ClassOverrides.Num(), 1);
	TestTrue(TEXT("Round-tripped navigation mode returns to default (preview does not own it)"),
		RoundTripped.PrototypeNavigationGenerationMode == EDesign2UMGPrototypeNavigationGenerationMode::RuntimeExtension);

	// Unset materialization snapshot -> default-constructed generation settings.
	const FDesign2UMGGenerationSettingsSnapshot Defaulted =
		FDesign2UMGGenerationSettingsSnapshot::FromMaterializationSettings(
			FDesignPreviewMaterializationSettingsSnapshot());
	TestEqual(TEXT("Empty materialization produces empty overrides"), Defaulted.ClassOverrides.Num(), 0);

	return true;
}

#endif
