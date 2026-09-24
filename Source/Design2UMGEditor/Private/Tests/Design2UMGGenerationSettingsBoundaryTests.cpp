// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"
#include "DesignImportPipeline.h"
#include "Design2UMGConverter.h"
#include "FigmaParser.h"
#include "PenParser.h"
#include "Design2UMGSettings.h"
#include "DesignPreviewMaterializationService.h"
#include "Design2UMGGenerationSettingsSnapshot.h"
#include "ExternalServices/PenMCPBridge.h"
#include "Tests/Design2UMGTestAssetCleanup.h"
#include "Design2UMGTestNodes.h"
#include "UMGGenerator.h"
#include "UMGWidgetTreeBuilder.h"

#include "Blueprint/WidgetTree.h"
#include "Components/BackgroundBlur.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "WidgetBlueprint.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"

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

	const FUMGWidgetNode* FindWidgetNodeByShapeColor(
		const FUMGWidgetNode& Node,
		const FLinearColor& Color)
	{
		if (Node.Brush.Shape.IsSet() && Node.Brush.Shape->Color.IsSet()
			&& Node.Brush.Shape->Color->Equals(Color))
		{
			return &Node;
		}

		for (const FUMGWidgetNode& Child : Node.Children)
		{
			if (const FUMGWidgetNode* Match = FindWidgetNodeByShapeColor(Child, Color))
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
				// Same ini-leak guard as the placeholder hatch scope above.
				Settings->SaveConfig();
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
			// Imports during the test can persist settings (e.g. icon-font mappings call
			// SaveConfig), which snapshots the drifted hatch values into the per-project
			// ini. Write the restored values back so the drift cannot leak into later runs.
			Settings->SaveConfig();
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
	UWidget* Widget = FUMGWidgetTreeBuilder::CreateWidgetRecursive(
		WidgetTree,
		MakeSimpleTextNode(TEXT("DefaultClassNode")),
		nullptr,
		nullptr,
		nullptr,
		nullptr,
		nullptr,
		&EmptySnapshot);
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

	UWidget* Widget = FUMGWidgetTreeBuilder::CreateWidgetRecursive(
		WidgetTree,
		MakeSimpleTextNode(TEXT("OverrideNode_Content")),
		nullptr,
		nullptr,
		nullptr,
		nullptr,
		nullptr,
		&OverrideSnapshot);
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
	if (!TestTrue(TEXT("Typed shape payload is present"), RootWidget.Brush.Shape.IsSet())) return false;
	const auto& Shape = RootWidget.Brush.Shape.GetValue();
	TestEqual(TEXT("Placeholder converts to image"), RootWidget.Type, FString(TEXT("Image")));
	TestTrue(TEXT("Import planning uses captured placeholder base fill"),
		Shape.Color.IsSet() && Shape.Color->Equals(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#123456")))));
	TestTrue(TEXT("Import planning uses captured placeholder stripe color"),
		Shape.StripeColor.IsSet() && Shape.StripeColor->Equals(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#ABCDEF")))));
	TestTrue(TEXT("Import planning uses captured placeholder stripe width"),
		Shape.StripeWidth.IsSet() && Shape.StripeWidth.GetValue() == 7);
	TestTrue(TEXT("Import planning uses captured placeholder stripe gap"),
		Shape.StripeGap.IsSet() && Shape.StripeGap.GetValue() == 11);
	TestFalse(TEXT("Import planning does not late-read current placeholder base fill"),
		Shape.Color.IsSet() && Shape.Color->Equals(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#654321")))));
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
		FindWidgetNodeByShapeColor(Data.RootWidget, FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#123456"))));
	TestNotNull(TEXT("Converted placeholder node with snapshot hatch exists"), PlaceholderNode);
	if (!PlaceholderNode)
	{
		return false;
	}

	TestEqual(TEXT("Converted placeholder becomes image"), PlaceholderNode->Type, FString(TEXT("Image")));
	if (!TestTrue(TEXT("Typed shape payload is present"), PlaceholderNode->Brush.Shape.IsSet())) return false;
	const auto& Shape = PlaceholderNode->Brush.Shape.GetValue();
	TestTrue(TEXT("Bridge conversion uses captured placeholder base fill"),
		Shape.Color.IsSet() && Shape.Color->Equals(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#123456")))));
	TestTrue(TEXT("Bridge conversion uses captured placeholder stripe color"),
		Shape.StripeColor.IsSet() && Shape.StripeColor->Equals(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#ABCDEF")))));

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

	// The IR name "Tooltip Text" compiles to widget name TooltipText, which collides with
	// UWidget::ToolTipText (case-insensitive) and is uniquified during tree construction.
	// The uniquifier uses a "_wN" tail so it can never collide with the tree's own
	// numeric "_N" uniquifier chain; the class override itself still matches the IR node name.
	UWidget* OverriddenWidget =
		Result.GeneratedBlueprint->WidgetTree->FindWidget(FName(TEXT("TooltipText_w1")));
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
	const FDesign2UMGGenerationSettingsSnapshot Deserialized =
		FDesign2UMGGenerationSettingsSnapshot::FromMaterializationSettings(MaterializationSnapshot);
	TestEqual(TEXT("Reconstructed snapshot carries the override"),
		Deserialized.ClassOverrides.Num(), 1);

	// Unset materialization snapshot -> default-constructed generation settings.
	const FDesign2UMGGenerationSettingsSnapshot Defaulted =
		FDesign2UMGGenerationSettingsSnapshot::FromMaterializationSettings(
			FDesignPreviewMaterializationSettingsSnapshot());
	TestEqual(TEXT("Empty materialization produces empty overrides"), Defaulted.ClassOverrides.Num(), 0);

	return true;
}

// Reimport rebuilds added subtrees through the widget generation chain; those rebuilds must honor
// the captured generation settings exactly like the import path. User-visible outcome: a Widget
// Class Override rule applies to nodes added by a MODIFIED design on reimport, not only on import.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReimportAddedNodesUseGenerationSettingsTest,
	"Design2UMG.Unit.GenerationSettings.ReimportAddedNodesUseClassOverrides",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FReimportAddedNodesUseGenerationSettingsTest::RunTest(const FString& Parameters)
{
	FDesign2UMGImportSettingsSnapshot SettingsSnapshot;
	FWidgetClassOverride& OverrideRule = SettingsSnapshot.GenerationSettings.ClassOverrides.AddDefaulted_GetRef();
	OverrideRule.NamePattern = TEXT("Badge");
	OverrideRule.WidgetClass = UBorder::StaticClass();

	// Plain single-frame document (no prototype): root frame with an inner container and one leaf.
	const auto MakeNodeJson = [](const TCHAR* Id, const TCHAR* Name, int32 X, int32 Y)
	{
		return FString::Printf(
			TEXT("{\"id\":\"%s\",\"name\":\"%s\",\"type\":\"RECTANGLE\","
				 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":%d,\"width\":60,\"height\":24},"
				 "\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":0.2,\"g\":0.5,\"b\":0.3,\"a\":1}}]}"),
			Id, Name, X, Y);
	};
	const auto MakeContainerJson = [](const TCHAR* Id, const TCHAR* Name, int32 X, int32 Y, const FString& ChildrenJson)
	{
		return FString::Printf(
			TEXT("{\"id\":\"%s\",\"name\":\"%s\",\"type\":\"FRAME\","
				 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":%d,\"width\":200,\"height\":120},\"children\":[%s]}"),
			Id, Name, X, Y, *ChildrenJson);
	};
	const auto MakeDocJson = [&MakeContainerJson](const FString& RootChildrenJson)
	{
		return FString::Printf(
			TEXT("{\"fileKey\":\"reimport-override-scope-7d41\",\"document\":{\"id\":\"0:0\",\"name\":\"Doc\",\"type\":\"DOCUMENT\",\"children\":["
				 "{\"id\":\"0:1\",\"name\":\"Page\",\"type\":\"CANVAS\",\"children\":[%s]}]}}"),
			*MakeContainerJson(TEXT("ovr_root"), TEXT("RootFrame"), 0, 0, RootChildrenJson));
	};

	const FString V1 = MakeDocJson(FString::Printf(TEXT("%s,%s"),
		*MakeContainerJson(TEXT("ovr_card"), TEXT("Card"), 12, 12,
			MakeNodeJson(TEXT("ovr_card_text"), TEXT("card_text"), 0, 0)),
		*MakeNodeJson(TEXT("ovr_badge"), TEXT("ovr_badge"), 140, 12)));

	FDesignDocument DocV1;
	if (!TestTrue(TEXT("v1 parse succeeds"), FFigmaParser::ParseDocument(V1, DocV1)))
	{
		return false;
	}
	FUMGWidgetData DataV1 = FDesign2UMGConverter::Convert(DocV1, SettingsSnapshot.ConversionSettings);

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ReimportOverrideSettings");
	DeleteGeneratedAssetsAtPath(OutputPath);
	UWidgetBlueprint* WBP = FUMGGenerator::GenerateWidgetBlueprint(DataV1, OutputPath, &SettingsSnapshot.GenerationSettings);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr && WBP->WidgetTree != nullptr))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	UWidget* ImportedBadge = WBP->WidgetTree->FindWidget(TEXT("ovr_badge"));
	if (!TestNotNull(TEXT("Imported badge node exists"), ImportedBadge))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestTrue(TEXT("Import applies the class override"), ImportedBadge->IsA<UBorder>());

	// v2 adds a new nested node (inside the card container) and a new top-level node, both named
	// to match the same override rule.
	const FString V2 = MakeDocJson(FString::Printf(TEXT("%s,%s,%s"),
		*MakeContainerJson(TEXT("ovr_card"), TEXT("Card"), 12, 12, FString::Printf(TEXT("%s,%s"),
			*MakeNodeJson(TEXT("ovr_card_text"), TEXT("card_text"), 0, 0),
			*MakeNodeJson(TEXT("ovr_card_badge"), TEXT("ovr_card_badge"), 0, 40))),
		*MakeNodeJson(TEXT("ovr_badge"), TEXT("ovr_badge"), 140, 12),
		*MakeNodeJson(TEXT("ovr_badge_top"), TEXT("ovr_badge_top"), 140, 60)));

	FDesignDocument DocV2;
	if (!TestTrue(TEXT("v2 parse succeeds"), FFigmaParser::ParseDocument(V2, DocV2)))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	FDesignReimportRequest Request;
	Request.TargetAssetPath = WBP->GetOutermost()->GetName();
	Request.SettingsSnapshot = SettingsSnapshot;
	const FDesignReimportResult Result = DesignImportPipeline::Reimport(DocV2, Request);
	TestTrue(TEXT("Reimport succeeds"), Result.bSucceeded);
	TestFalse(TEXT("Reimport is not a full conflict"), Result.bFullConflict);

	UWidget* NestedAddedBadge = WBP->WidgetTree->FindWidget(TEXT("ovr_card_badge"));
	UWidget* TopLevelAddedBadge = WBP->WidgetTree->FindWidget(TEXT("ovr_badge_top"));
	if (!TestNotNull(TEXT("Nested added node exists after reimport"), NestedAddedBadge)
		|| !TestNotNull(TEXT("Top-level added node exists after reimport"), TopLevelAddedBadge))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	const bool bNestedUsesOverride = NestedAddedBadge->IsA<UBorder>();
	const bool bTopLevelUsesOverride = TopLevelAddedBadge->IsA<UBorder>();
	TestTrue(TEXT("Reimport applies the class override to a nested added node"), bNestedUsesOverride);
	TestTrue(TEXT("Reimport applies the class override to a top-level added node"), bTopLevelUsesOverride);

	DeleteGeneratedAssetsAtPath(OutputPath);
	return bNestedUsesOverride && bTopLevelUsesOverride;
}

// ---------------------------------------------------------------------------
// Generator widget-name tail contract: reimport's same-name reconciliation parses both
// generator-produced shapes (Base_<N> from the duplicate-name probe, Base_w<N> from the
// UWidget-property-collision uniquifier) and must reject everything else, including the
// overlay mirror suffix. The rank feeding the candidate sort has to be a total order, or
// the comparison stops being transitive and pairing order becomes unspecified.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeneratedWidgetSuffixContractTest,
	"Design2UMG.PureLogic.GeneratedWidgetSuffixContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FGeneratedWidgetSuffixContractTest::RunTest(const FString&)
{
	const FString Base = TEXT("TooltipText");

	// Accepted generated shapes.
	TestTrue(TEXT("bare base name matches"), FUMGWidgetTreeBuilder::MatchesWidgetBaseName(Base, Base));
	TestTrue(TEXT("numeric dedup tail matches"), FUMGWidgetTreeBuilder::MatchesWidgetBaseName(TEXT("TooltipText_1"), Base));
	TestTrue(TEXT("high numeric dedup tail matches"), FUMGWidgetTreeBuilder::MatchesWidgetBaseName(TEXT("TooltipText_42"), Base));
	TestTrue(TEXT("property-collision tail matches"), FUMGWidgetTreeBuilder::MatchesWidgetBaseName(TEXT("TooltipText_w1"), Base));
	TestTrue(TEXT("uppercase W tail matches"), FUMGWidgetTreeBuilder::MatchesWidgetBaseName(TEXT("TooltipText_W2"), Base));

	// Rejected: mirror suffix, zero ordinals, non-numeric tails, other bases.
	TestFalse(TEXT("overlay mirror suffix never matches a base name"),
		FUMGWidgetTreeBuilder::MatchesWidgetBaseName(TEXT("TooltipText__d2u_ov"), Base));
	TestFalse(TEXT("zero numeric ordinal is rejected"),
		FUMGWidgetTreeBuilder::MatchesWidgetBaseName(TEXT("TooltipText_0"), Base));
	TestFalse(TEXT("zero w-ordinal is rejected"),
		FUMGWidgetTreeBuilder::MatchesWidgetBaseName(TEXT("TooltipText_w0"), Base));
	TestFalse(TEXT("bare w with no digits is rejected"),
		FUMGWidgetTreeBuilder::MatchesWidgetBaseName(TEXT("TooltipText_w"), Base));
	TestFalse(TEXT("word tail starting with w is rejected"),
		FUMGWidgetTreeBuilder::MatchesWidgetBaseName(TEXT("TooltipText_widget"), Base));
	TestFalse(TEXT("different base does not match"),
		FUMGWidgetTreeBuilder::MatchesWidgetBaseName(TEXT("TooltipTextX_1"), Base));
	TestFalse(TEXT("missing separator does not match"),
		FUMGWidgetTreeBuilder::MatchesWidgetBaseName(TEXT("TooltipText1"), Base));

	const int32 RankBase = FUMGWidgetTreeBuilder::GetWidgetNameSuffixRank(Base, Base);
	const int32 RankNumeric1 = FUMGWidgetTreeBuilder::GetWidgetNameSuffixRank(TEXT("TooltipText_1"), Base);
	const int32 RankProperty1 = FUMGWidgetTreeBuilder::GetWidgetNameSuffixRank(TEXT("TooltipText_w1"), Base);
	const int32 RankNumeric2 = FUMGWidgetTreeBuilder::GetWidgetNameSuffixRank(TEXT("TooltipText_2"), Base);
	const int32 RankProperty2 = FUMGWidgetTreeBuilder::GetWidgetNameSuffixRank(TEXT("TooltipText_w2"), Base);
	const int32 RankUnmatched = FUMGWidgetTreeBuilder::GetWidgetNameSuffixRank(TEXT("TooltipText__d2u_ov"), Base);

	TestEqual(TEXT("base name ranks first"), RankBase, 0);
	TestEqual(TEXT("non-generated names rank last"), RankUnmatched, MAX_int32);
	TestTrue(TEXT("every generated shape outranks a non-match"),
		RankNumeric2 < RankUnmatched && RankProperty2 < RankUnmatched);

	// The two tail shapes must stay disjoint: Base_1 and Base_w1 sharing one rank was the
	// original defect (the pairing sort then stopped being a strict weak order).
	TSet<int32> DistinctRanks;
	DistinctRanks.Add(RankBase);
	DistinctRanks.Add(RankNumeric1);
	DistinctRanks.Add(RankProperty1);
	DistinctRanks.Add(RankNumeric2);
	DistinctRanks.Add(RankProperty2);
	TestEqual(TEXT("all five generated shapes rank distinctly"), DistinctRanks.Num(), 5);
	TestTrue(TEXT("numeric tail ranks before the property tail of the same ordinal"), RankNumeric1 < RankProperty1);
	TestTrue(TEXT("ordinals still increase across shapes"), RankProperty1 < RankNumeric2);

	return true;
}

#endif
