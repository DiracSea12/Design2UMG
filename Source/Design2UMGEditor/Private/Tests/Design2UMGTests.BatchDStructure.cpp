// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignImportPlanning.h"
#include "Misc/AutomationTest.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UMGGeneratorRootHandoff.h"

namespace
{
	FString GetPluginFilePath(const FString& RelativePath)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Design2UMG"));
		return Plugin.IsValid()
			? FPaths::Combine(Plugin->GetBaseDir(), RelativePath)
			: FString();
	}

	bool LoadPluginFile(FAutomationTestBase& Test, const FString& RelativePath, FString& OutContent)
	{
		const FString AbsolutePath = GetPluginFilePath(RelativePath);
		Test.TestFalse(*FString::Printf(TEXT("%s path resolved"), *RelativePath), AbsolutePath.IsEmpty());
		if (AbsolutePath.IsEmpty())
		{
			return false;
		}

		Test.TestTrue(*FString::Printf(TEXT("%s exists"), *RelativePath), IFileManager::Get().FileExists(*AbsolutePath));
		if (!IFileManager::Get().FileExists(*AbsolutePath))
		{
			return false;
		}

		Test.TestTrue(*FString::Printf(TEXT("Load %s"), *RelativePath), FFileHelper::LoadFileToString(OutContent, *AbsolutePath));
		return !OutContent.IsEmpty();
	}

	FString ExtractBuildDependencySection(const FString& Source, const TCHAR* SectionName)
	{
		const int32 SectionStart = Source.Find(SectionName, ESearchCase::CaseSensitive);
		if (SectionStart == INDEX_NONE)
		{
			return FString();
		}

		const TCHAR* NextSectionName =
			FCString::Strstr(SectionName, TEXT("Public")) ? TEXT("PrivateDependencyModuleNames.AddRange") : nullptr;
		const int32 NextSectionStart = NextSectionName
			? Source.Find(NextSectionName, ESearchCase::CaseSensitive, ESearchDir::FromStart, SectionStart + 1)
			: INDEX_NONE;
		if (NextSectionStart == INDEX_NONE)
		{
			return Source.Mid(SectionStart);
		}

		return Source.Mid(SectionStart, NextSectionStart - SectionStart);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchDPrototypeStepGraphOwnerSliceStructureTest,
	"Design2UMG.BatchD.PrototypeStepGraphOwnerSliceStructure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchDPrototypeStepGraphOwnerSliceStructureTest::RunTest(const FString& Parameters)
{
	FString MainSource;
	if (!LoadPluginFile(*this, TEXT("Source/Design2UMGCore/Private/Design2UMGPrototypeStepGraph.cpp"), MainSource))
	{
		return false;
	}

	FString HelperSource;
	if (!LoadPluginFile(*this, TEXT("Source/Design2UMGCore/Private/Design2UMGPrototypeStepGraphAssembly.cpp"), HelperSource))
	{
		return false;
	}

	TestTrue(TEXT("Prototype step graph main file includes assembly helper"),
		MainSource.Contains(TEXT("#include \"Design2UMGPrototypeStepGraphAssembly.h\""), ESearchCase::CaseSensitive));
	TestFalse(TEXT("JSON action parsing no longer lives in prototype step graph main file"),
		MainSource.Contains(TEXT("ParsePrototypeActionFromJsonObject"), ESearchCase::CaseSensitive));
	TestFalse(TEXT("Transition assembly no longer lives in prototype step graph main file"),
		MainSource.Contains(TEXT("BuildPrototypeTransition"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Prototype assembly helper owns JSON action parsing"),
		HelperSource.Contains(TEXT("ParsePrototypeActionFromJsonObject"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Prototype assembly helper owns binding assembly"),
		HelperSource.Contains(TEXT("GatherNavigationBindings"), ESearchCase::CaseSensitive));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchDUMGGeneratorOwnerSliceStructureTest,
	"Design2UMG.BatchD.UMGGeneratorOwnerSliceStructure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchDUMGGeneratorOwnerSliceStructureTest::RunTest(const FString& Parameters)
{
	FString MainSource;
	if (!LoadPluginFile(*this, TEXT("Source/Design2UMGEditor/Private/UMGGenerator.cpp"), MainSource))
	{
		return false;
	}

	FString RootHandoffSource;
	if (!LoadPluginFile(*this, TEXT("Source/Design2UMGEditor/Private/UMGGeneratorRootHandoff.cpp"), RootHandoffSource))
	{
		return false;
	}

	FString AnimationBuilderSource;
	if (!LoadPluginFile(*this, TEXT("Source/Design2UMGEditor/Private/UMGPrototypeAnimationBuilder.cpp"), AnimationBuilderSource))
	{
		return false;
	}

	TestTrue(TEXT("UMGGenerator main file includes root handoff helper"),
		MainSource.Contains(TEXT("#include \"UMGGeneratorRootHandoff.h\""), ESearchCase::CaseSensitive));
	TestTrue(TEXT("UMGGenerator main file includes prototype animation helper"),
		MainSource.Contains(TEXT("#include \"UMGPrototypeAnimationBuilder.h\""), ESearchCase::CaseSensitive));
	TestFalse(TEXT("Viewport wrapping no longer lives in UMGGenerator main file"),
		MainSource.Contains(TEXT("WrapRootForViewport"), ESearchCase::CaseSensitive));
	TestFalse(TEXT("Prototype animation construction no longer lives in UMGGenerator main file"),
		MainSource.Contains(TEXT("CreatePrototypeTransitionAnimation"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Root handoff helper owns root preparation"),
		RootHandoffSource.Contains(TEXT("PrepareRootForBuild"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Prototype animation helper owns transition animation construction"),
		AnimationBuilderSource.Contains(TEXT("CreatePrototypeTransitionAnimation"), ESearchCase::CaseSensitive));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchDImportPipelineOwnerSliceStructureTest,
	"Design2UMG.BatchD.ImportPipelineOwnerSliceStructure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchDImportPipelineOwnerSliceStructureTest::RunTest(const FString& Parameters)
{
	FString MainSource;
	if (!LoadPluginFile(*this, TEXT("Source/Design2UMGEditor/Private/DesignImportPipeline.cpp"), MainSource))
	{
		return false;
	}

	FString PlanningSource;
	if (!LoadPluginFile(*this, TEXT("Source/Design2UMGEditor/Private/DesignImportPlanning.cpp"), PlanningSource))
	{
		return false;
	}

	FString DiagnosticReportSource;
	if (!LoadPluginFile(*this, TEXT("Source/Design2UMGEditor/Private/DesignImportDiagnosticReport.cpp"), DiagnosticReportSource))
	{
		return false;
	}

	TestTrue(TEXT("Import pipeline main file includes planning helper"),
		MainSource.Contains(TEXT("#include \"DesignImportPlanning.h\""), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Import pipeline main file includes diagnostic report helper"),
		MainSource.Contains(TEXT("#include \"DesignImportDiagnosticReport.h\""), ESearchCase::CaseSensitive));
	TestFalse(TEXT("Preview JSON parsing no longer lives in import pipeline main file"),
		MainSource.Contains(TEXT("ParseWidgetJson"), ESearchCase::CaseSensitive));
	TestFalse(TEXT("Document preparation no longer lives in import pipeline main file"),
		MainSource.Contains(TEXT("PrepareDocumentForGeneration"), ESearchCase::CaseSensitive));
	TestFalse(TEXT("Diagnostic degradation collection no longer lives in import pipeline main file"),
		MainSource.Contains(TEXT("CollectNodeDegradationsRecursive"), ESearchCase::CaseSensitive));
	TestFalse(TEXT("Diagnostic effect wrapper collection no longer lives in import pipeline main file"),
		MainSource.Contains(TEXT("CollectEffectWrappersRecursive"), ESearchCase::CaseSensitive));
	TestFalse(TEXT("Diagnostic image failure collection no longer lives in import pipeline main file"),
		MainSource.Contains(TEXT("CollectImageFailureEntries"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Planning helper owns preview JSON parsing"),
		PlanningSource.Contains(TEXT("ParseWidgetJson"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Planning helper owns import analysis"),
		PlanningSource.Contains(TEXT("AnalyzeDocument"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Diagnostic report helper owns public write seam"),
		DiagnosticReportSource.Contains(TEXT("void Write("), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Diagnostic report helper owns degradation collection"),
		DiagnosticReportSource.Contains(TEXT("CollectNodeDegradationsRecursive"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Diagnostic report helper owns recommendations"),
		DiagnosticReportSource.Contains(TEXT("AddUniqueRecommendation"), ESearchCase::CaseSensitive));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchDContributorFacingHonestyStructureTest,
	"Design2UMG.BatchD.ContributorFacingHonestyStructure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchDContributorFacingHonestyStructureTest::RunTest(const FString& Parameters)
{
	FString EditorBuildRules;
	if (!LoadPluginFile(*this, TEXT("Source/Design2UMGEditor/Design2UMGEditor.Build.cs"), EditorBuildRules))
	{
		return false;
	}

	FString SchemaFoundationHeader;
	if (!LoadPluginFile(*this, TEXT("Source/Design2UMGCore/Public/UMGSchemaFoundation.h"), SchemaFoundationHeader))
	{
		return false;
	}

	FString PluginDescriptor;
	if (!LoadPluginFile(*this, TEXT("Design2UMG.uplugin"), PluginDescriptor))
	{
		return false;
	}

	FString ArchitectureDoc;
	if (!LoadPluginFile(*this, TEXT("Docs/Architecture.md"), ArchitectureDoc))
	{
		return false;
	}

	const FString PublicDependencies = ExtractBuildDependencySection(EditorBuildRules, TEXT("PublicDependencyModuleNames.AddRange"));
	const FString PrivateDependencies = ExtractBuildDependencySection(EditorBuildRules, TEXT("PrivateDependencyModuleNames.AddRange"));
	TestTrue(TEXT("Editor Build.cs documents why public dependencies remain public"),
		EditorBuildRules.Contains(TEXT("Public dependencies are limited"), ESearchCase::CaseSensitive));
	TestFalse(TEXT("Editor-only asset/build modules stay out of the public dependency surface"),
		PublicDependencies.Contains(TEXT("\"UnrealEd\""), ESearchCase::CaseSensitive)
		|| PublicDependencies.Contains(TEXT("\"AssetTools\""), ESearchCase::CaseSensitive)
		|| PublicDependencies.Contains(TEXT("\"HTTP\""), ESearchCase::CaseSensitive)
		|| PublicDependencies.Contains(TEXT("\"Networking\""), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Editor-only asset/build modules stay private"),
		PrivateDependencies.Contains(TEXT("\"UnrealEd\""), ESearchCase::CaseSensitive)
		&& PrivateDependencies.Contains(TEXT("\"AssetTools\""), ESearchCase::CaseSensitive)
		&& PrivateDependencies.Contains(TEXT("\"HTTP\""), ESearchCase::CaseSensitive)
		&& PrivateDependencies.Contains(TEXT("\"Networking\""), ESearchCase::CaseSensitive));
	TestTrue(TEXT("FBounds is now named by design pixel semantics"),
		SchemaFoundationHeader.Contains(TEXT("struct FDesignPixelBounds"), ESearchCase::CaseSensitive));
	TestFalse(TEXT("Generic FBounds type name is not left on the public schema surface"),
		SchemaFoundationHeader.Contains(TEXT("struct FBounds"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Plugin descriptor states the stack is editor-only"),
		PluginDescriptor.Contains(TEXT("editor-only"), ESearchCase::IgnoreCase));
	TestTrue(TEXT("Architecture docs state Design2UMGCore is an editor module, not runtime"),
		ArchitectureDoc.Contains(TEXT("editor module"), ESearchCase::IgnoreCase)
		&& ArchitectureDoc.Contains(TEXT("not a runtime module"), ESearchCase::IgnoreCase));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchDUMGGeneratorRootHandoffSmokeTest,
	"Design2UMG.BatchD.UMGGeneratorRootHandoffSmoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchDUMGGeneratorRootHandoffSmokeTest::RunTest(const FString& Parameters)
{
	FUMGWidgetData WidgetData;
	WidgetData.RootWidget.Type = TEXT("CanvasPanel");
	WidgetData.RootWidget.Id = TEXT("root");
	WidgetData.RootWidget.Name = TEXT("Root");
	WidgetData.RootWidget.Bounds.W = 320.0f;
	WidgetData.RootWidget.Bounds.H = 180.0f;

	const UMGGeneratorRootHandoff::FPreparedRoot Prepared =
		UMGGeneratorRootHandoff::PrepareRootForBuild(WidgetData, TEXT("/Game/D2U_Test"), TEXT("WBP_D2U_Test"));

	TestEqual(TEXT("Root handoff keeps the root widget type"), Prepared.Root.Type, FString(TEXT("CanvasPanel")));
	TestEqual(TEXT("Root handoff derives design size from valid root bounds"), Prepared.DesignSize, FIntPoint(320, 180));
	TestEqual(TEXT("Root handoff preserves root id"), Prepared.Root.Id, FString(TEXT("root")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchDImportPlanningSmokeTest,
	"Design2UMG.BatchD.ImportPlanningSmoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchDImportPlanningSmokeTest::RunTest(const FString& Parameters)
{
	FUMGWidgetData WidgetData;
	WidgetData.WidgetName = TEXT("WBP_ImportPlanningSmoke");
	WidgetData.RootWidget.Type = TEXT("CanvasPanel");
	WidgetData.RootWidget.Id = TEXT("planning_root");
	WidgetData.RootWidget.Name = TEXT("PlanningRoot");
	WidgetData.RootWidget.Bounds.W = 256.0f;
	WidgetData.RootWidget.Bounds.H = 128.0f;

	FImportAnalysisInput Input;
	Input.bResolveImages = false;
	const FImportAnalysisResult Result = DesignImportPlanning::AnalyzeWidgetData(WidgetData, Input);

	TestEqual(TEXT("Import planning copies widget name"), Result.Data.WidgetName, WidgetData.WidgetName);
	TestEqual(TEXT("Import planning copies root type"), Result.Data.RootWidget.Type, FString(TEXT("CanvasPanel")));
	TestEqual(TEXT("Import planning copies root bounds width"), Result.Data.RootWidget.Bounds.W, 256.0f);
	TestTrue(TEXT("Import planning without image resolution produces no image report"), Result.ImageReport.IsEmpty());
	return true;
}
