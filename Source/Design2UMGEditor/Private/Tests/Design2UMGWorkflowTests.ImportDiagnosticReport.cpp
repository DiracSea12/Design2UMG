// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Misc/AutomationTest.h"
#include "Design2UMGSettings.h"
#include "DesignImageBakeUtils.h"
#include "DesignImportPipeline.h"
#include "DesignSchema.h"
#include "DesignSchemaSourceExtensions.h"
#include "FontResolver.h"
#include "Tests/Design2UMGTestAssetCleanup.h"
#include "UMGSchema.h"
#include "Dom/JsonObject.h"
#include "Engine/Font.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

#if WITH_AUTOMATION_WORKER

using Design2UMGEditorTests::DeleteGeneratedAssetsAtPath;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImportDiagnosticReportStructureTest,
	"Design2UMG.Import.ImportDiagnosticReportStructure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FImportDiagnosticReportStructureTest::RunTest(const FString& Parameters)
{
	const FString ReportDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), TEXT("ImportReports"));
	TArray<FString> ReportsBefore;
	IFileManager::Get().FindFilesRecursive(ReportsBefore, *ReportDir, TEXT("*.import_report.json"), true, false, false);
	TSet<FString> ExistingReports;
	for (const FString& ExistingReport : ReportsBefore)
	{
		ExistingReports.Add(ExistingReport);
	}

	FDesignDocument Document;
	Document.Version = TEXT("figma-fixture-v1");
	Document.Metadata.Add(
		TEXT("design.sourceFilePath"),
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), FString::Printf(TEXT("fixture_%s.json"), *FGuid::NewGuid().ToString(EGuidFormats::Digits))));

	FDesignSkippedNodeReport SkippedNode;
	SkippedNode.NodeId = TEXT("slice1");
	SkippedNode.NodeName = TEXT("Slice Overlay");
	SkippedNode.NodeType = TEXT("SLICE");
	SkippedNode.Reason = TEXT("design_only");
	Document.MutableParseDiagnostics().SkippedDesignerNodes.Add(SkippedNode);

	TSharedPtr<FDesignNode> TextPathNode = MakeShared<FDesignNode>();
	TextPathNode->Id = TEXT("text_path_1");
	TextPathNode->Name = TEXT("Curved Label");
	TextPathNode->Type = EDesignNodeType::Text;
	TextPathNode->MutableSource().SourceNodeType = TEXT("TEXT_PATH");
	TextPathNode->Text.FontFamily = TEXT("DefinitelyMissingReportFont");

	TSharedPtr<FDesignNode> RenderFallbackNode = MakeShared<FDesignNode>();
	RenderFallbackNode->Id = TEXT("render_1");
	RenderFallbackNode->Name = TEXT("Blurred Hero");
	RenderFallbackNode->Type = EDesignNodeType::Rectangle;
	RenderFallbackNode->MutableSource().SourceNodeType = TEXT("RECTANGLE");
	RenderFallbackNode->MutableSource().RenderFallback.ImageRef = TEXT("render_hero");
	RenderFallbackNode->MutableSource().RenderFallback.Scope = TEXT("full");
	RenderFallbackNode->MutableSource().RenderFallback.Source = TEXT("figma_render_png");
	RenderFallbackNode->MutableSource().RenderFallback.Reason = TEXT("foreground_blur");

	Document.Children.Add(TextPathNode);
	Document.Children.Add(RenderFallbackNode);

	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_ImportReportFixture");
	Data.Warnings.Add(TEXT("Figma TEXT_PATH 'Curved Label' -> TextBlock (text-on-path downgraded to regular text)"));

	FUMGWidgetNode Wrapper;
	Wrapper.Type = TEXT("BackgroundBlur");
	Wrapper.Id = TEXT("hero_blur");
	Wrapper.Name = TEXT("HeroBlur");
	Wrapper.Effect.BlurStrength = 12.0f;

	FUMGWidgetNode Child;
	Child.Type = TEXT("TextBlock");
	Child.Id = TEXT("hero_text");
	Child.Name = TEXT("HeroText");
	Wrapper.Children.Add(Child);
	Data.RootWidget = Wrapper;

	const FString ImageReport =
		TEXT("Remote image import: imported=0 reused=0 repaired=0 failed=1 path=/Game/Design2UMG/ImportedImages\n")
		TEXT("Image 'Hero' download failed: https://example.com/hero.png (reason: Network request failed)");

	DesignImportPipeline::WriteImportDiagnosticReport(
		Document,
		Data,
		ImageReport,
		TEXT("/Game/Design2UMG/_TestOutput/ImportReport"),
		TEXT("G:/UE5/FPSRoguelike/Saved/Design2UMG/WBP_ImportReportFixture.png"),
		TEXT("/Game/Design2UMG/_TestOutput/ImportReport/WBP_ImportReportFixture"));

	TArray<FString> ReportsAfter;
	IFileManager::Get().FindFilesRecursive(ReportsAfter, *ReportDir, TEXT("*.import_report.json"), true, false, false);

	FString ReportPath;
	FDateTime LatestTimestamp = FDateTime::MinValue();
	for (const FString& Candidate : ReportsAfter)
	{
		const FDateTime Timestamp = IFileManager::Get().GetTimeStamp(*Candidate);
		if (!ExistingReports.Contains(Candidate))
		{
			ReportPath = Candidate;
			break;
		}
		if (Timestamp > LatestTimestamp)
		{
			LatestTimestamp = Timestamp;
			ReportPath = Candidate;
		}
	}

	TestTrue(TEXT("Import report was generated"), !ReportPath.IsEmpty() && FPaths::FileExists(ReportPath));
	if (ReportPath.IsEmpty() || !FPaths::FileExists(ReportPath))
	{
		return false;
	}

	FString ReportJson;
	TestTrue(TEXT("Load import report json"), FFileHelper::LoadFileToString(ReportJson, *ReportPath));
	if (ReportJson.IsEmpty())
	{
		return false;
	}

	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ReportJson);
	TestTrue(TEXT("Parse import report json"), FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid());
	if (!RootObject.IsValid())
	{
		return false;
	}
	TestEqual(TEXT("Report exposes schema version"), RootObject->GetStringField(TEXT("schemaVersion")), TEXT("design2umg-import-report-v2"));
	TestEqual(TEXT("Report exposes its own path"), RootObject->GetStringField(TEXT("reportPath")), ReportPath);

	const TSharedPtr<FJsonObject>* SourceObject = nullptr;
	TestTrue(TEXT("Report has source object"), RootObject->TryGetObjectField(TEXT("source"), SourceObject) && SourceObject);
	if (!SourceObject)
	{
		return false;
	}

	TestEqual(TEXT("Source origin inferred from json input"), (*SourceObject)->GetStringField(TEXT("origin")), TEXT("local_json"));
	TestTrue(TEXT("Source identity emitted"), !(*SourceObject)->GetStringField(TEXT("identity")).IsEmpty());
	TestEqual(TEXT("Top-level sourceFilePath aliases source.filePath"), RootObject->GetStringField(TEXT("sourceFilePath")), (*SourceObject)->GetStringField(TEXT("filePath")));
	TestEqual(TEXT("Top-level sourceIdentity aliases source.identity"), RootObject->GetStringField(TEXT("sourceIdentity")), (*SourceObject)->GetStringField(TEXT("identity")));

	const TSharedPtr<FJsonObject>* DiagnosticsObject = nullptr;
	TestTrue(TEXT("Report has diagnostics object"), RootObject->TryGetObjectField(TEXT("diagnostics"), DiagnosticsObject) && DiagnosticsObject);
	if (!DiagnosticsObject)
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Degradations = nullptr;
	TestTrue(TEXT("Diagnostics include degradations"), (*DiagnosticsObject)->TryGetArrayField(TEXT("degradations"), Degradations) && Degradations && Degradations->Num() >= 2);
	TestTrue(TEXT("Diagnostics include warnings"), (*DiagnosticsObject)->GetArrayField(TEXT("warnings")).Num() == 1);
		TestTrue(TEXT("Diagnostics include skipped designer nodes"), (*DiagnosticsObject)->GetArrayField(TEXT("skippedDesignerNodes")).Num() == 1);
	TestFalse(TEXT("Top-level warnings alias is retired"), RootObject->HasField(TEXT("warnings")));
	TestFalse(TEXT("Top-level skipped nodes alias is retired"), RootObject->HasField(TEXT("skippedDesignerNodes")));

	bool bFoundTextPathFallback = false;
	bool bFoundRenderFallback = false;
	for (const TSharedPtr<FJsonValue>& Value : *Degradations)
	{
		const TSharedPtr<FJsonObject> Entry = Value.IsValid() ? Value->AsObject() : nullptr;
		if (!Entry.IsValid())
		{
			continue;
		}

		const FString Kind = Entry->GetStringField(TEXT("kind"));
		bFoundTextPathFallback |= Kind == TEXT("text_path_fallback");
		bFoundRenderFallback |= Kind == TEXT("render_fallback");
	}
	TestTrue(TEXT("Report records text path fallback"), bFoundTextPathFallback);
	TestTrue(TEXT("Report records render fallback"), bFoundRenderFallback);

	const TSharedPtr<FJsonObject>* EffectsObject = nullptr;
	TestTrue(TEXT("Report has effects object"), RootObject->TryGetObjectField(TEXT("effects"), EffectsObject) && EffectsObject);
	TestEqual(TEXT("Report records effect wrapper count"), static_cast<int32>((*EffectsObject)->GetNumberField(TEXT("wrapperCount"))), 1);
	TestEqual(TEXT("Report records wrapper entry"), (*EffectsObject)->GetArrayField(TEXT("wrappers")).Num(), 1);

	const TSharedPtr<FJsonObject>* ResourcesObject = nullptr;
	TestTrue(TEXT("Report has resources object"), RootObject->TryGetObjectField(TEXT("resources"), ResourcesObject) && ResourcesObject);
	TestEqual(TEXT("Report records missing fonts"), (*ResourcesObject)->GetArrayField(TEXT("missingFonts")).Num(), 1);
	TestEqual(TEXT("Report records image failures"), (*ResourcesObject)->GetArrayField(TEXT("imageFailures")).Num(), 1);
	TestFalse(TEXT("Top-level missingFonts alias is retired"), RootObject->HasField(TEXT("missingFonts")));
	TestFalse(TEXT("Top-level imageImportReport alias is retired"), RootObject->HasField(TEXT("imageImportReport")));
	const TSharedPtr<FJsonObject>* FontResolutionObject = nullptr;
	TestTrue(TEXT("Report has compact font resolution object"),
		RootObject->TryGetObjectField(TEXT("fontResolution"), FontResolutionObject) && FontResolutionObject);
	if (FontResolutionObject)
	{
		const TArray<TSharedPtr<FJsonValue>>& FontResults = (*FontResolutionObject)->GetArrayField(TEXT("results"));
		TestEqual(TEXT("Missing project font is represented in font results"), FontResults.Num(), 1);
		if (FontResults.Num() == 1 && FontResults[0].IsValid())
		{
			const TSharedPtr<FJsonObject> FontResult = FontResults[0]->AsObject();
			TestEqual(TEXT("Missing font falls back with warning state"),
				FontResult->GetStringField(TEXT("resolutionState")),
				TEXT("default-font"));
			TestEqual(TEXT("Font result key is stable"),
				FontResult->GetStringField(TEXT("declarationKey")),
				TEXT("definitelymissingreportfont|none|400|normal"));
		}
	}

	const TSharedPtr<FJsonObject>* OutputObject = nullptr;
	TestTrue(TEXT("Report has output object"), RootObject->TryGetObjectField(TEXT("output"), OutputObject) && OutputObject);
	if (OutputObject)
	{
		TestEqual(TEXT("Top-level requestedOutputPath aliases output.requestedOutputPath"), RootObject->GetStringField(TEXT("requestedOutputPath")), (*OutputObject)->GetStringField(TEXT("requestedOutputPath")));
		TestEqual(TEXT("Top-level requestedRenderPngPath aliases output.requestedRenderPngPath"), RootObject->GetStringField(TEXT("requestedRenderPngPath")), (*OutputObject)->GetStringField(TEXT("requestedRenderPngPath")));
	}

	const TArray<TSharedPtr<FJsonValue>>* Recommendations = nullptr;
	TestTrue(TEXT("Report includes recommendations"), RootObject->TryGetArrayField(TEXT("recommendations"), Recommendations) && Recommendations && Recommendations->Num() > 0);
	IFileManager::Get().Delete(*ReportPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImportDiagnosticReportEngineFontFallbackWarnsTest,
	"Design2UMG.Import.ImportDiagnosticReportEngineFontFallbackWarns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FImportDiagnosticReportEngineFontFallbackWarnsTest::RunTest(const FString& Parameters)
{
	UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	TestTrue(TEXT("Settings available"), Settings != nullptr);
	if (!Settings)
	{
		return false;
	}

	const TMap<FString, TSoftObjectPtr<UFont>> SavedMappings = Settings->ExplicitFontMappings;
	const TSoftObjectPtr<UFont> SavedDefaultFont = Settings->DefaultFontAsset;
	Settings->ExplicitFontMappings.Empty();
	Settings->DefaultFontAsset.Reset();
	FFontResolver::ClearCache();

	FDesignDocument Document;
	Document.Version = TEXT("engine-font-fallback-report-v1");
	TSharedPtr<FDesignNode> TextNode = MakeShared<FDesignNode>();
	TextNode->Id = TEXT("engine_fallback_text");
	TextNode->Name = TEXT("Engine Fallback Text");
	TextNode->Type = EDesignNodeType::Text;
	TextNode->Text.FontFamily = TEXT("Roboto");
	TextNode->Text.FontWeight = TEXT("500");
	Document.Children.Add(TextNode);

	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_EngineFontFallbackReportFixture");
	Data.RootWidget.Type = TEXT("TextBlock");
	Data.RootWidget.Id = TextNode->Id;
	Data.RootWidget.Name = TextNode->Name;

	const FString ReportDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), TEXT("ImportReports"));
	TArray<FString> ReportsBefore;
	IFileManager::Get().FindFilesRecursive(ReportsBefore, *ReportDir, TEXT("*.import_report.json"), true, false, false);
	TSet<FString> ExistingReports;
	for (const FString& ExistingReport : ReportsBefore)
	{
		ExistingReports.Add(ExistingReport);
	}

	DesignImportPipeline::WriteImportDiagnosticReport(
		Document,
		Data,
		FString(),
		TEXT("/Game/Design2UMG/_TestOutput/EngineFontFallback"),
		FString(),
		TEXT("/Game/Design2UMG/_TestOutput/EngineFontFallback/WBP_EngineFontFallbackReportFixture"));

	Settings->ExplicitFontMappings = SavedMappings;
	Settings->DefaultFontAsset = SavedDefaultFont;
	FFontResolver::ClearCache();

	TArray<FString> ReportsAfter;
	IFileManager::Get().FindFilesRecursive(ReportsAfter, *ReportDir, TEXT("*.import_report.json"), true, false, false);

	FString ReportPath;
	for (const FString& Candidate : ReportsAfter)
	{
		if (!ExistingReports.Contains(Candidate))
		{
			ReportPath = Candidate;
			break;
		}
	}

	TestTrue(TEXT("Engine fallback report was generated"), !ReportPath.IsEmpty() && FPaths::FileExists(ReportPath));
	if (ReportPath.IsEmpty() || !FPaths::FileExists(ReportPath))
	{
		return false;
	}

	FString ReportJson;
	TestTrue(TEXT("Load engine fallback report json"), FFileHelper::LoadFileToString(ReportJson, *ReportPath));
	TSharedPtr<FJsonObject> RootObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ReportJson);
	TestTrue(TEXT("Parse engine fallback report json"), FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid());
	if (!RootObject.IsValid())
	{
		IFileManager::Get().Delete(*ReportPath);
		return false;
	}

	const TSharedPtr<FJsonObject>* FontResolutionObject = nullptr;
	TestTrue(TEXT("Report has compact font resolution object"),
		RootObject->TryGetObjectField(TEXT("fontResolution"), FontResolutionObject) && FontResolutionObject);
	if (FontResolutionObject)
	{
		const TArray<TSharedPtr<FJsonValue>>& FontResults = (*FontResolutionObject)->GetArrayField(TEXT("results"));
		TestEqual(TEXT("Engine fallback emits one font result"), FontResults.Num(), 1);
		if (FontResults.Num() == 1 && FontResults[0].IsValid())
		{
			const TSharedPtr<FJsonObject> FontResult = FontResults[0]->AsObject();
			TestEqual(TEXT("Engine fallback remains a warning fallback"),
				FontResult->GetStringField(TEXT("resolutionState")),
				TEXT("default-font"));
			TestEqual(TEXT("Engine fallback keeps warning severity"),
				FontResult->GetStringField(TEXT("severity")),
				TEXT("warning"));
			TestEqual(TEXT("Engine fallback reports the resolved engine font asset"),
				FontResult->GetStringField(TEXT("resolvedAssetPath")),
				TEXT("/Engine/EngineFonts/Roboto.Roboto"));
			bool bFoundMissingProjectIssue = false;
			for (const TSharedPtr<FJsonValue>& IssueValue : FontResult->GetArrayField(TEXT("issueCodes")))
			{
				bFoundMissingProjectIssue |= IssueValue.IsValid()
					&& IssueValue->AsString() == TEXT("missing-project-font");
			}
			TestTrue(TEXT("Engine fallback still records missing project font"), bFoundMissingProjectIssue);
		}
	}

	IFileManager::Get().Delete(*ReportPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImportDiagnosticReportInvalidExplicitFontMappingTest,
	"Design2UMG.Import.ImportDiagnosticReportInvalidExplicitFontMapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FImportDiagnosticReportInvalidExplicitFontMappingTest::RunTest(const FString& Parameters)
{
	UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	TestTrue(TEXT("Settings available"), Settings != nullptr);
	if (!Settings)
	{
		return false;
	}

	const TMap<FString, TSoftObjectPtr<UFont>> SavedMappings = Settings->ExplicitFontMappings;
	const TSoftObjectPtr<UFont> SavedDefaultFont = Settings->DefaultFontAsset;
	Settings->ExplicitFontMappings.Empty();
	Settings->DefaultFontAsset.Reset();

	UPackage* EmptyFontPackage = CreatePackage(TEXT("/Game/Design2UMG/Fonts/D2U_EmptyCompositeReportFont"));
	UFont* EmptyCompositeFont = NewObject<UFont>(
		EmptyFontPackage,
		TEXT("D2U_EmptyCompositeReportFont"),
		RF_Public | RF_Standalone);
	TestTrue(TEXT("Invalid explicit mapping fixture font created"), EmptyCompositeFont != nullptr);
	if (!EmptyCompositeFont)
	{
		Settings->ExplicitFontMappings = SavedMappings;
		Settings->DefaultFontAsset = SavedDefaultFont;
		return false;
	}

	Settings->ExplicitFontMappings.Add(
		TEXT("Inter"),
		TSoftObjectPtr<UFont>(FSoftObjectPath(EmptyCompositeFont)));
	FFontResolver::ClearCache();

	FDesignDocument Document;
	Document.Version = TEXT("invalid-explicit-font-mapping-v1");
	TSharedPtr<FDesignNode> TextNode = MakeShared<FDesignNode>();
	TextNode->Id = TEXT("explicit_mapping_text");
	TextNode->Name = TEXT("Explicit Mapping Text");
	TextNode->Type = EDesignNodeType::Text;
	TextNode->Text.FontFamily = TEXT("Inter");
	TextNode->Text.FontWeight = TEXT("700");
	Document.Children.Add(TextNode);

	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_InvalidExplicitFontMappingReportFixture");
	Data.RootWidget.Type = TEXT("TextBlock");
	Data.RootWidget.Id = TextNode->Id;
	Data.RootWidget.Name = TextNode->Name;

	const FString ReportDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), TEXT("ImportReports"));
	TArray<FString> ReportsBefore;
	IFileManager::Get().FindFilesRecursive(ReportsBefore, *ReportDir, TEXT("*.import_report.json"), true, false, false);
	TSet<FString> ExistingReports;
	for (const FString& ExistingReport : ReportsBefore)
	{
		ExistingReports.Add(ExistingReport);
	}

	DesignImportPipeline::WriteImportDiagnosticReport(
		Document,
		Data,
		FString(),
		TEXT("/Game/Design2UMG/_TestOutput/InvalidExplicitFontMapping"),
		FString(),
		TEXT("/Game/Design2UMG/_TestOutput/InvalidExplicitFontMapping/WBP_InvalidExplicitFontMappingReportFixture"));

	Settings->ExplicitFontMappings = SavedMappings;
	Settings->DefaultFontAsset = SavedDefaultFont;
	FFontResolver::ClearCache();

	TArray<FString> ReportsAfter;
	IFileManager::Get().FindFilesRecursive(ReportsAfter, *ReportDir, TEXT("*.import_report.json"), true, false, false);

	FString ReportPath;
	for (const FString& Candidate : ReportsAfter)
	{
		if (!ExistingReports.Contains(Candidate))
		{
			ReportPath = Candidate;
			break;
		}
	}

	TestTrue(TEXT("Invalid explicit mapping report was generated"), !ReportPath.IsEmpty() && FPaths::FileExists(ReportPath));
	if (ReportPath.IsEmpty() || !FPaths::FileExists(ReportPath))
	{
		return false;
	}

	FString ReportJson;
	TestTrue(TEXT("Load invalid explicit mapping report json"), FFileHelper::LoadFileToString(ReportJson, *ReportPath));
	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ReportJson);
	TestTrue(TEXT("Parse invalid explicit mapping report json"), FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid());
	if (!RootObject.IsValid())
	{
		IFileManager::Get().Delete(*ReportPath);
		return false;
	}

	const TSharedPtr<FJsonObject>* FontResolutionObject = nullptr;
	TestTrue(TEXT("Report has compact font resolution object"),
		RootObject->TryGetObjectField(TEXT("fontResolution"), FontResolutionObject) && FontResolutionObject);
	if (FontResolutionObject)
	{
		const TArray<TSharedPtr<FJsonValue>>& FontResults = (*FontResolutionObject)->GetArrayField(TEXT("results"));
		TestEqual(TEXT("Invalid explicit mapping emits one font result"), FontResults.Num(), 1);
		if (FontResults.Num() == 1 && FontResults[0].IsValid())
		{
			const TSharedPtr<FJsonObject> FontResult = FontResults[0]->AsObject();
			TestEqual(TEXT("Invalid explicit mapping is reported as rejected"),
				FontResult->GetStringField(TEXT("resolutionState")),
				TEXT("rejected"));
			TestEqual(TEXT("Invalid explicit mapping reports error severity"),
				FontResult->GetStringField(TEXT("severity")),
				TEXT("error"));
			TestFalse(TEXT("Invalid explicit mapping must not report an auto-substituted font asset"),
				FontResult->HasField(TEXT("resolvedAssetPath")));

			bool bFoundInvalidMappingIssue = false;
			for (const TSharedPtr<FJsonValue>& IssueValue : FontResult->GetArrayField(TEXT("issueCodes")))
			{
				bFoundInvalidMappingIssue |= IssueValue.IsValid()
					&& IssueValue->AsString() == TEXT("invalid-explicit-font-mapping");
			}
			TestTrue(TEXT("Invalid explicit mapping issue code is present"), bFoundInvalidMappingIssue);
		}
	}

	IFileManager::Get().Delete(*ReportPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImportDiagnosticReportExplicitMappingMissingTypefaceTest,
	"Design2UMG.Import.ImportDiagnosticReportExplicitMappingMissingTypeface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FImportDiagnosticReportExplicitMappingMissingTypefaceTest::RunTest(const FString& Parameters)
{
	UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	TestTrue(TEXT("Settings available"), Settings != nullptr);
	if (!Settings)
	{
		return false;
	}

	const TMap<FString, TSoftObjectPtr<UFont>> SavedMappings = Settings->ExplicitFontMappings;
	const TSoftObjectPtr<UFont> SavedDefaultFont = Settings->DefaultFontAsset;
	Settings->ExplicitFontMappings.Empty();
	Settings->DefaultFontAsset.Reset();

	UPackage* RegularOnlyPackage = CreatePackage(TEXT("/Game/Design2UMG/Fonts/D2U_RegularOnlyExplicitReportFont"));
	UFont* RegularOnlyFont = NewObject<UFont>(
		RegularOnlyPackage,
		TEXT("D2U_RegularOnlyExplicitReportFont"),
		RF_Public | RF_Standalone);
	TestTrue(TEXT("Regular-only explicit report font created"), RegularOnlyFont != nullptr);
	if (!RegularOnlyFont)
	{
		Settings->ExplicitFontMappings = SavedMappings;
		Settings->DefaultFontAsset = SavedDefaultFont;
		return false;
	}

	RegularOnlyFont->FontCacheType = EFontCacheType::Runtime;
	RegularOnlyFont->GetMutableInternalCompositeFont().DefaultTypeface.Fonts.Add(FTypefaceEntry(
		FName(TEXT("Regular")),
		FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts/Roboto-Regular.ttf")),
		EFontHinting::Default,
		EFontLoadingPolicy::LazyLoad));

	Settings->ExplicitFontMappings.Add(
		TEXT("D2URegularOnlyPS"),
		TSoftObjectPtr<UFont>(FSoftObjectPath(RegularOnlyFont)));
	FFontResolver::ClearCache();

	FDesignDocument Document;
	Document.Version = TEXT("explicit-missing-typeface-report-v1");
	TSharedPtr<FDesignNode> TextNode = MakeShared<FDesignNode>();
	TextNode->Id = TEXT("explicit_mapping_missing_typeface_text");
	TextNode->Name = TEXT("Explicit Mapping Missing Typeface Text");
	TextNode->Type = EDesignNodeType::Text;
	TextNode->Text.FontFamily = TEXT("D2URegularOnly");
	TextNode->Text.FontPostScriptName = TEXT("D2URegularOnlyPS");
	TextNode->Text.FontWeight = TEXT("700");
	Document.Children.Add(TextNode);

	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_ExplicitMappingMissingTypefaceReportFixture");
	Data.RootWidget.Type = TEXT("TextBlock");
	Data.RootWidget.Id = TextNode->Id;
	Data.RootWidget.Name = TextNode->Name;

	const FString ReportDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), TEXT("ImportReports"));
	TArray<FString> ReportsBefore;
	IFileManager::Get().FindFilesRecursive(ReportsBefore, *ReportDir, TEXT("*.import_report.json"), true, false, false);
	TSet<FString> ExistingReports;
	for (const FString& ExistingReport : ReportsBefore)
	{
		ExistingReports.Add(ExistingReport);
	}

	DesignImportPipeline::WriteImportDiagnosticReport(
		Document,
		Data,
		FString(),
		TEXT("/Game/Design2UMG/_TestOutput/ExplicitMappingMissingTypeface"),
		FString(),
		TEXT("/Game/Design2UMG/_TestOutput/ExplicitMappingMissingTypeface/WBP_ExplicitMappingMissingTypefaceReportFixture"));

	Settings->ExplicitFontMappings = SavedMappings;
	Settings->DefaultFontAsset = SavedDefaultFont;
	FFontResolver::ClearCache();

	TArray<FString> ReportsAfter;
	IFileManager::Get().FindFilesRecursive(ReportsAfter, *ReportDir, TEXT("*.import_report.json"), true, false, false);

	FString ReportPath;
	for (const FString& Candidate : ReportsAfter)
	{
		if (!ExistingReports.Contains(Candidate))
		{
			ReportPath = Candidate;
			break;
		}
	}

	TestTrue(TEXT("Explicit missing typeface report was generated"), !ReportPath.IsEmpty() && FPaths::FileExists(ReportPath));
	if (ReportPath.IsEmpty() || !FPaths::FileExists(ReportPath))
	{
		return false;
	}

	FString ReportJson;
	TestTrue(TEXT("Load explicit missing typeface report json"), FFileHelper::LoadFileToString(ReportJson, *ReportPath));
	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ReportJson);
	TestTrue(TEXT("Parse explicit missing typeface report json"), FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid());
	if (!RootObject.IsValid())
	{
		IFileManager::Get().Delete(*ReportPath);
		return false;
	}

	const TSharedPtr<FJsonObject>* FontResolutionObject = nullptr;
	TestTrue(TEXT("Report has compact font resolution object"),
		RootObject->TryGetObjectField(TEXT("fontResolution"), FontResolutionObject) && FontResolutionObject);
	if (FontResolutionObject)
	{
		const TArray<TSharedPtr<FJsonValue>>& FontResults = (*FontResolutionObject)->GetArrayField(TEXT("results"));
		TestEqual(TEXT("Missing explicit typeface emits one font result"), FontResults.Num(), 1);
		if (FontResults.Num() == 1 && FontResults[0].IsValid())
		{
			const TSharedPtr<FJsonObject> FontResult = FontResults[0]->AsObject();
			TestEqual(TEXT("Missing explicit typeface is reported as rejected"),
				FontResult->GetStringField(TEXT("resolutionState")),
				TEXT("rejected"));
			TestEqual(TEXT("Missing explicit typeface reports error severity"),
				FontResult->GetStringField(TEXT("severity")),
				TEXT("error"));
			TestFalse(TEXT("Missing explicit typeface must not report an auto-substituted font asset"),
				FontResult->HasField(TEXT("resolvedAssetPath")));
			TestFalse(TEXT("Missing explicit typeface must not report an auto-substituted typeface"),
				FontResult->HasField(TEXT("resolvedTypeface")));

			bool bFoundMissingTypefaceIssue = false;
			for (const TSharedPtr<FJsonValue>& IssueValue : FontResult->GetArrayField(TEXT("issueCodes")))
			{
				bFoundMissingTypefaceIssue |= IssueValue.IsValid()
					&& IssueValue->AsString() == TEXT("missing-explicit-font-typeface");
			}
			TestTrue(TEXT("Missing explicit typeface issue code is present"), bFoundMissingTypefaceIssue);
		}
	}

	IFileManager::Get().Delete(*ReportPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImportDiagnosticReportTypedSourcePathContractTest,
	"Design2UMG.Import.ImportDiagnosticReportTypedSourcePathContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FImportDiagnosticReportTypedSourcePathContractTest::RunTest(const FString& Parameters)
{
	const FString ReportDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), TEXT("ImportReports"));
	TArray<FString> ReportsBefore;
	IFileManager::Get().FindFilesRecursive(ReportsBefore, *ReportDir, TEXT("*.import_report.json"), true, false, false);
	TSet<FString> ExistingReports;
	for (const FString& ExistingReport : ReportsBefore)
	{
		ExistingReports.Add(ExistingReport);
	}

	FDesignDocument Document;
	Document.Version = TEXT("typed-source-path-v1");
	Document.MutableSourceDocument().SourceFilePath =
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), TEXT("typed_source_only.pen"));

	const FString ExpectedIdentity = DesignImageBakeUtils::BuildSourceDocumentIdentity(Document.SourceDocument().SourceFilePath);
	TestEqual(
		TEXT("Typed source path drives image bake identity"),
		DesignImageBakeUtils::ResolveDocumentImageBakeIdentity(Document, FString()),
		ExpectedIdentity);

	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_TypedSourcePathFixture");
	Data.RootWidget.Type = TEXT("CanvasPanel");
	Data.RootWidget.Id = TEXT("root");
	Data.RootWidget.Name = TEXT("Root");
	Data.RootWidget.Bounds = {0.0f, 0.0f, 64.0f, 64.0f};

	DesignImportPipeline::WriteImportDiagnosticReport(
		Document,
		Data,
		FString(),
		TEXT("/Game/Design2UMG/_TestOutput/TypedSourcePath"),
		TEXT("G:/UE5/FPSRoguelike/Saved/Design2UMG/WBP_TypedSourcePathFixture.png"),
		TEXT("/Game/Design2UMG/_TestOutput/TypedSourcePath/WBP_TypedSourcePathFixture"));

	TArray<FString> ReportsAfter;
	IFileManager::Get().FindFilesRecursive(ReportsAfter, *ReportDir, TEXT("*.import_report.json"), true, false, false);
	FString ReportPath;
	for (const FString& Candidate : ReportsAfter)
	{
		if (!ExistingReports.Contains(Candidate))
		{
			ReportPath = Candidate;
			break;
		}
	}

	TestTrue(TEXT("Typed source path report was generated"), !ReportPath.IsEmpty() && FPaths::FileExists(ReportPath));
	if (ReportPath.IsEmpty() || !FPaths::FileExists(ReportPath))
	{
		return false;
	}

	FString ReportJson;
	TestTrue(TEXT("Load typed-source report json"), FFileHelper::LoadFileToString(ReportJson, *ReportPath));
	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ReportJson);
	TestTrue(TEXT("Parse typed-source report json"), FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid());
	if (!RootObject.IsValid())
	{
		IFileManager::Get().Delete(*ReportPath);
		return false;
	}

	const TSharedPtr<FJsonObject>* SourceObject = nullptr;
	TestTrue(TEXT("Typed source path report has source object"), RootObject->TryGetObjectField(TEXT("source"), SourceObject) && SourceObject);
	if (SourceObject)
	{
		TestEqual(TEXT("Typed source path surfaces source.filePath"), (*SourceObject)->GetStringField(TEXT("filePath")), Document.SourceDocument().SourceFilePath);
		TestEqual(TEXT("Typed source path surfaces source.identity"), (*SourceObject)->GetStringField(TEXT("identity")), ExpectedIdentity);
	}
	TestEqual(TEXT("Typed source path keeps top-level sourceFilePath alias"), RootObject->GetStringField(TEXT("sourceFilePath")), Document.SourceDocument().SourceFilePath);
	TestEqual(TEXT("Typed source path keeps top-level sourceIdentity alias"), RootObject->GetStringField(TEXT("sourceIdentity")), ExpectedIdentity);

	IFileManager::Get().Delete(*ReportPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImportDiagnosticReportExecutePathTest,
	"Design2UMG.Import.ImportDiagnosticReportExecutePath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FImportDiagnosticReportExecutePathTest::RunTest(const FString& Parameters)
{
	const FString ReportDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), TEXT("ImportReports"));
	TArray<FString> ReportsBefore;
	IFileManager::Get().FindFilesRecursive(ReportsBefore, *ReportDir, TEXT("*.import_report.json"), true, false, false);
	TSet<FString> ExistingReports;
	for (const FString& ExistingReport : ReportsBefore)
	{
		ExistingReports.Add(ExistingReport);
	}

	FDesignDocument Document;
	Document.Version = TEXT("pen-fixture-v1");
	Document.Metadata.Add(
		TEXT("design.sourceFilePath"),
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), FString::Printf(TEXT("fixture_%s.pen"), *FGuid::NewGuid().ToString(EGuidFormats::Digits))));

	TSharedPtr<FDesignNode> RootNode = MakeShared<FDesignNode>();
	RootNode->Id = TEXT("root");
	RootNode->Name = TEXT("Root");
	RootNode->Type = EDesignNodeType::Frame;
	RootNode->Transform.Width = FDesignSize::Fixed(120.0f);
	RootNode->Transform.Height = FDesignSize::Fixed(60.0f);
	Document.Children.Add(RootNode);

	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_ImportReportExecuteFixture");
	Data.RootWidget.Type = TEXT("CanvasPanel");
	Data.RootWidget.Id = TEXT("root");
	Data.RootWidget.Name = TEXT("Root");
	Data.RootWidget.Bounds = {0.0f, 0.0f, 120.0f, 60.0f};

	FDesignImportRequest Request;
	Request.OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ImportReportExecute");
	Request.bUsePreview = false;
	Request.bResolveImages = false;
	Request.Generation.Mode = EDesignBlueprintGenerationMode::BlueprintOnly;
	Request.DiagnosticDocument = Document;

	DeleteGeneratedAssetsAtPath(Request.OutputPath);
	const FDesignImportExecutionResult Result = DesignImportPipeline::Execute(Data, Request);
	TestTrue(TEXT("Execute data path succeeds"), Result.bSucceeded);
	if (!Result.bSucceeded)
	{
		DeleteGeneratedAssetsAtPath(Request.OutputPath);
		return false;
	}

	TArray<FString> ReportsAfter;
	IFileManager::Get().FindFilesRecursive(ReportsAfter, *ReportDir, TEXT("*.import_report.json"), true, false, false);
	FString ReportPath;
	for (const FString& Candidate : ReportsAfter)
	{
		if (!ExistingReports.Contains(Candidate))
		{
			ReportPath = Candidate;
			break;
		}
	}

	TestTrue(TEXT("Execute data path writes report"), !ReportPath.IsEmpty() && FPaths::FileExists(ReportPath));
	if (ReportPath.IsEmpty() || !FPaths::FileExists(ReportPath))
	{
		DeleteGeneratedAssetsAtPath(Request.OutputPath);
		return false;
	}

	const TArray<uint8> ReportBytes = [&ReportPath]()
	{
		TArray<uint8> Bytes;
		FFileHelper::LoadFileToArray(Bytes, *ReportPath);
		return Bytes;
	}();
	TestFalse(TEXT("Execute data path report is UTF-8 without BOM"),
		ReportBytes.Num() >= 3
		&& ReportBytes[0] == 0xEF
		&& ReportBytes[1] == 0xBB
		&& ReportBytes[2] == 0xBF);

	FString ReportJson;
	TestTrue(TEXT("Load execute-path report json"), FFileHelper::LoadFileToString(ReportJson, *ReportPath));
	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ReportJson);
	TestTrue(TEXT("Parse execute-path report json"), FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid());
	if (RootObject.IsValid())
	{
		TestEqual(TEXT("Execute data path report keeps requested output"), RootObject->GetStringField(TEXT("requestedOutputPath")), Request.OutputPath);
		TestEqual(TEXT("Execute data path report keeps generated asset path"), RootObject->GetStringField(TEXT("generatedAssetPath")), Result.GeneratedAssetPath);
	}

	IFileManager::Get().Delete(*ReportPath);
	DeleteGeneratedAssetsAtPath(Request.OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImportDiagnosticReportFailurePathTest,
	"Design2UMG.Import.ImportDiagnosticReportFailurePath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FImportDiagnosticReportFailurePathTest::RunTest(const FString& Parameters)
{
	const FString ReportDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), TEXT("ImportReports"));
	TArray<FString> ReportsBefore;
	IFileManager::Get().FindFilesRecursive(ReportsBefore, *ReportDir, TEXT("*.import_report.json"), true, false, false);
	TSet<FString> ExistingReports;
	for (const FString ExistingReport : ReportsBefore)
	{
		ExistingReports.Add(ExistingReport);
	}

	FDesignDocument Document;
	Document.Version = TEXT("pen-fixture-failure-v1");
	Document.Metadata.Add(
		TEXT("design.sourceFilePath"),
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), FString::Printf(TEXT("fixture_%s.pen"), *FGuid::NewGuid().ToString(EGuidFormats::Digits))));

	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_ImportReportFailureFixture");
	Data.RootWidget.Type = TEXT("CanvasPanel");
	Data.RootWidget.Id = TEXT("root");
	Data.RootWidget.Name = TEXT("Root");
	Data.RootWidget.Bounds = {0.0f, 0.0f, 120.0f, 60.0f};

	FDesignImportRequest Request;
	Request.OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ImportReportFailure");
	Request.bUsePreview = false;
	Request.bResolveImages = false;
	Request.Generation.Mode = EDesignBlueprintGenerationMode::BlueprintAndPng;
	Request.DiagnosticDocument = Document;

	DeleteGeneratedAssetsAtPath(Request.OutputPath);
	const FDesignImportExecutionResult Result = DesignImportPipeline::Execute(Data, Request);
	TestTrue(TEXT("BlueprintAndPng path succeeds under NullRHI"), Result.bSucceeded);
	TestTrue(TEXT("BlueprintAndPng path saves generated asset path"), !Result.GeneratedAssetPath.IsEmpty());
	TestTrue(TEXT("BlueprintAndPng path saves rendered png path"), !Result.RenderPngPath.IsEmpty());

	TArray<FString> ReportsAfter;
	IFileManager::Get().FindFilesRecursive(ReportsAfter, *ReportDir, TEXT("*.import_report.json"), true, false, false);
	FString ReportPath;
	for (const FString& Candidate : ReportsAfter)
	{
		if (!ExistingReports.Contains(Candidate))
		{
			ReportPath = Candidate;
			break;
		}
	}

	TestTrue(TEXT("Failure path still writes report"), !ReportPath.IsEmpty() && FPaths::FileExists(ReportPath));
	if (!ReportPath.IsEmpty() && FPaths::FileExists(ReportPath))
	{
		FString ReportJson;
		TestTrue(TEXT("Load failure-path report json"), FFileHelper::LoadFileToString(ReportJson, *ReportPath));
		TSharedPtr<FJsonObject> RootObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ReportJson);
		TestTrue(TEXT("Parse failure-path report json"), FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid());
		if (RootObject.IsValid())
		{
			TestEqual(TEXT("BlueprintAndPng report keeps generated asset path"), RootObject->GetStringField(TEXT("generatedAssetPath")), Result.GeneratedAssetPath);
			TestEqual(TEXT("BlueprintAndPng report keeps rendered png path"), RootObject->GetStringField(TEXT("renderPngPath")), Result.RenderPngPath);
			TestTrue(TEXT("BlueprintAndPng report still records requested output"), RootObject->GetStringField(TEXT("requestedOutputPath")) == Request.OutputPath);
		}
		IFileManager::Get().Delete(*ReportPath);
	}

	DeleteGeneratedAssetsAtPath(Request.OutputPath);
	return true;
}
#endif
