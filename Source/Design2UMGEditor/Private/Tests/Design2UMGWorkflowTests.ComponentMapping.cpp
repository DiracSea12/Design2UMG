// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"

#include "Design2UMGComponentMappingConfig.h"
#include "Design2UMGComponentMappingGeneration.h"
#include "Design2UMGConverter.h"
#include "Design2UMGGenerationManifest.h"
#include "Design2UMGGenerationSettingsSnapshot.h"
#include "Design2UMGTestUtils.h"
#include "FigmaParser.h"
#include "UMGGenerator.h"
#include "UMGSchemaSourceExtensions.h"
#include "Tests/Design2UMGMappedComponentTestWidget.h"
#include "Tests/Design2UMGTestAssetCleanup.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/PanelWidget.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"

#if WITH_AUTOMATION_WORKER

namespace
{
	using namespace Design2UMGTestUtils;
	using Design2UMGEditorTests::DeleteGeneratedAssetsAtPath;

	const TCHAR* TestScope = TEXT("mapping-workflow-file-key");
	const TCHAR* TestWidgetClassPath = TEXT("/Script/Design2UMGEditor.Design2UMGMappedComponentTestWidget");

	FString MakeWorkflowMappingFigmaJson()
	{
		return TEXT(R"({
			"fileKey":"mapping-workflow-file-key",
			"document":{"id":"0:0","name":"Doc","type":"DOCUMENT","children":[
				{"id":"0:1","name":"Page","type":"CANVAS","children":[
					{"id":"page_root","name":"MappingWorkflowPage","type":"FRAME",
					 "absoluteBoundingBox":{"x":0,"y":0,"width":360,"height":200},"children":[
						{"id":"btn_inst_1","name":"PrimaryButton","type":"INSTANCE",
						 "componentId":"12:34","componentSetId":"56:78","mainComponentKey":"component-key-primary",
						 "componentProperties":{
							"label":{"type":"TEXT","value":"Checkout"},
							"enabled":{"type":"BOOLEAN","value":false},
							"variant":{"type":"VARIANT","value":"Primary"},
							"value":{"type":"NUMBER","value":12.5}},
						 "absoluteBoundingBox":{"x":24,"y":24,"width":120,"height":44},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.1,"g":0.4,"b":0.9,"a":1}}],
						 "children":[{"id":"btn_inst_1_label","name":"Label","type":"TEXT",
							"absoluteBoundingBox":{"x":32,"y":32,"width":80,"height":20},"characters":"Checkout",
							"style":{"fontFamily":"Inter","fontSize":16,"fontWeight":500,"textAlignHorizontal":"LEFT","textAlignVertical":"TOP","textDecoration":"NONE","letterSpacing":0,"lineHeightUnit":"AUTO","lineHeightPx":0}}]},
						{"id":"btn_inst_2","name":"PrimaryButton","type":"INSTANCE",
						 "componentId":"12:34","componentSetId":"56:78","mainComponentKey":"component-key-primary",
						 "componentProperties":{
							"label":{"type":"TEXT","value":"Pay Now"},
							"enabled":{"type":"BOOLEAN","value":true},
							"variant":{"type":"VARIANT","value":"Secondary"},
							"value":{"type":"NUMBER","value":3.25}},
						 "absoluteBoundingBox":{"x":24,"y":88,"width":120,"height":44},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.1,"g":0.4,"b":0.9,"a":1}}],
						 "children":[{"id":"btn_inst_2_label","name":"Label","type":"TEXT",
							"absoluteBoundingBox":{"x":32,"y":96,"width":80,"height":20},"characters":"Pay Now",
							"style":{"fontFamily":"Inter","fontSize":16,"fontWeight":500,"textAlignHorizontal":"LEFT","textAlignVertical":"TOP","textDecoration":"NONE","letterSpacing":0,"lineHeightUnit":"AUTO","lineHeightPx":0}}]},
						{"id":"plain_card","name":"PlainCard","type":"FRAME",
						 "absoluteBoundingBox":{"x":180,"y":24,"width":140,"height":108},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.9,"g":0.9,"b":0.9,"a":1}}],
						 "children":[{"id":"plain_card_text","name":"Caption","type":"TEXT",
							"absoluteBoundingBox":{"x":188,"y":32,"width":100,"height":20},"characters":"Not mapped",
							"style":{"fontFamily":"Inter","fontSize":14,"fontWeight":400,"textAlignHorizontal":"LEFT","textAlignVertical":"TOP","textDecoration":"NONE","letterSpacing":0,"lineHeightUnit":"AUTO","lineHeightPx":0}}]}
					]}
				]}
			]}
		})");
	}

	FDesign2UMGGenerationSettingsSnapshot MakeMappingGenerationSettings()
	{
		FDesign2UMGGenerationSettingsSnapshot Settings;
		// 名称型 ClassOverride 只允许作用于未命中 mapping 的普通节点：
		// 这里同时命中 "PrimaryButton"（mapped 节点名）与 "PlainCard"（未映射节点名），
		// 验证前者不改写 mapped 节点、后者照常生效。
		FWidgetClassOverride& MappedNameOverride = Settings.ClassOverrides.AddDefaulted_GetRef();
		MappedNameOverride.NamePattern = TEXT("PrimaryButton");
		MappedNameOverride.WidgetClass = FSoftClassPath(TEXT("/Script/UMG.Border"));
		FWidgetClassOverride& PlainNameOverride = Settings.ClassOverrides.AddDefaulted_GetRef();
		PlainNameOverride.NamePattern = TEXT("PlainCard");
		PlainNameOverride.WidgetClass = FSoftClassPath(TEXT("/Script/UMG.SizeBox"));

		DesignComponentMapping::FComponentMappingEntry& Entry =
			Settings.ComponentMapping.Table.Entries.AddDefaulted_GetRef();
		Entry.Key = FString::Printf(TEXT("figma|%s|component-key-primary"), TestScope);
		Entry.Provider = TEXT("figma");
		Entry.DocumentScope = TestScope;
		Entry.ComponentKey = TEXT("component-key-primary");
		Entry.WidgetClass = TestWidgetClassPath;

		auto MakeInput = [](const TCHAR* Source, const TCHAR* Target, const TCHAR* Type)
		{
			DesignComponentMapping::FComponentInputDeclaration Input;
			Input.Source = Source;
			Input.Target = Target;
			DesignComponentMapping::ParseComponentInputType(Type, Input.Type);
			return Input;
		};
		Entry.Inputs.Add(MakeInput(TEXT("label"), TEXT("Label"), TEXT("Text")));
		Entry.Inputs.Add(MakeInput(TEXT("enabled"), TEXT("EnabledFlag"), TEXT("Bool")));
		Entry.Inputs.Add(MakeInput(TEXT("variant"), TEXT("VariantName"), TEXT("Name")));
		Entry.Inputs.Add(MakeInput(TEXT("value"), TEXT("Value"), TEXT("Float")));
		// 目标属性不存在：只跳过这个 input，其余 input 与映射本身继续生效。
		Entry.Inputs.Add(MakeInput(TEXT("label"), TEXT("DoesNotExist"), TEXT("Text")));
		return Settings;
	}

	UDesign2UMGMappedComponentTestWidget* FindMappedInstanceByLabel(
		const UWidgetTree* WidgetTree, const FText& Label)
	{
		UDesign2UMGMappedComponentTestWidget* Match = nullptr;
		WidgetTree->ForEachWidget([&Label, &Match](UWidget* Widget)
		{
			if (UDesign2UMGMappedComponentTestWidget* Candidate = Cast<UDesign2UMGMappedComponentTestWidget>(Widget))
			{
				if (Candidate->Label.ToString() == Label.ToString())
				{
					Match = Candidate;
				}
			}
		});
		return Match;
	}

	bool ReadSidecarJson(const FString& AssetPackageName, TSharedPtr<FJsonObject>& OutObject)
	{
		const FString SidecarPath = Design2UMGGenerationManifest::BuildSidecarPathForAsset(AssetPackageName);
		FString Json;
		if (!FFileHelper::LoadFileToString(Json, *SidecarPath))
		{
			return false;
		}
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
	}

	// 清掉输出目录（含 .d2u.json sidecar 与残留资产），测试之间互不影响。
	void DeleteOutputDirectory(const FString& OutputPath)
	{
		const FString OutputDir = FPackageName::LongPackageNameToFilename(OutputPath);
		if (!OutputDir.IsEmpty())
		{
			IFileManager::Get().DeleteDirectory(*OutputDir, false, true);
		}
	}
}

// ---------------------------------------------------------------------------
// 生成资产断言：mapped 实例、子树未展开、inputs 写入、回退与诊断、manifest
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentMappingWorkflowTest,
	"Design2UMG.WidgetBlueprint.ComponentMapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FComponentMappingWorkflowTest::RunTest(const FString&)
{
	FDesignDocument Document;
	TestTrue(TEXT("Parse workflow mapping fixture"), FFigmaParser::ParseDocument(MakeWorkflowMappingFigmaJson(), Document));
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ComponentMappingWorkflow");
	DeleteGeneratedAssetsAtPath(OutputPath);

	const FDesign2UMGGenerationSettingsSnapshot Settings = MakeMappingGenerationSettings();
	FDesign2UMGGenerationOutput GenerationOutput;
	UWidgetBlueprint* Blueprint = FUMGGenerator::GenerateWidgetBlueprint(Data, OutputPath, &Settings, &GenerationOutput);
	TestTrue(TEXT("Generate mapped widget blueprint"), Blueprint != nullptr);
	if (!Blueprint || !Blueprint->WidgetTree)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UWidgetTree* WidgetTree = Blueprint->WidgetTree;

	// mapped 实例 1：项目类实例 + inputs 写入 + 子树未展开。
	UDesign2UMGMappedComponentTestWidget* Instance1 = FindMappedInstanceByLabel(WidgetTree, FText::FromString(TEXT("Checkout")));
	TestNotNull(TEXT("First mapped instance generated"), Instance1);
	if (Instance1)
	{
		TestEqual(TEXT("Mapped instance keeps its project class"), Instance1->GetClass()->GetPathName(), TestWidgetClassPath);
		TestFalse(TEXT("Mapped flag written from design"), Instance1->EnabledFlag);
		TestEqual(TEXT("Variant name written from design"), Instance1->VariantName, FName(TEXT("Primary")));
		TestTrue(TEXT("Float value written from design"), FMath::IsNearlyEqual(Instance1->Value, 12.5));
		TestNull(TEXT("Component internals not expanded into host tree"), WidgetTree->FindWidget(TEXT("btn_inst_1_label")));
	}

	// mapped 实例 2：同一组件定义的第二实例，值互不混淆。
	UDesign2UMGMappedComponentTestWidget* Instance2 = FindMappedInstanceByLabel(WidgetTree, FText::FromString(TEXT("Pay Now")));
	TestNotNull(TEXT("Second mapped instance generated"), Instance2);
	if (Instance2)
	{
		TestTrue(TEXT("Second instance flag from its own design values"), Instance2->EnabledFlag);
		TestEqual(TEXT("Second instance variant"), Instance2->VariantName, FName(TEXT("Secondary")));
		TestTrue(TEXT("Second instance value"), FMath::IsNearlyEqual(Instance2->Value, 3.25));
		TestNull(TEXT("Second instance internals not expanded into host tree"), WidgetTree->FindWidget(TEXT("btn_inst_2_label")));
	}
	TestTrue(TEXT("Two mapped instances are distinct widgets"), Instance1 != Instance2);
	if (Instance1 && Instance2)
	{
		TestNotEqual(TEXT("Instance widget names are unique"), Instance1->GetFName(), Instance2->GetFName());
	}

	// 未映射节点走现有转换链路：子树照常展开。
	UWidget* PlainText = WidgetTree->FindWidget(TEXT("Caption"));
	TestNotNull(TEXT("Unmapped node keeps normal conversion"), PlainText);
	TestTrue(TEXT("Unmapped node is a TextBlock, not the mapped class"),
		PlainText && PlainText->IsA(UTextBlock::StaticClass()));

	// 名称型 ClassOverride 优先级：不改写命中 mapping 的节点，只作用于未命中节点。
	TestFalse(TEXT("Name-based override does not rewrite mapped nodes"),
		Instance1 && Instance1->IsA(UBorder::StaticClass()));
	UWidget* PlainCard = WidgetTree->FindWidget(TEXT("PlainCard"));
	TestTrue(TEXT("Name-based override still applies to unmapped nodes"),
		PlainCard && PlainCard->IsA(USizeBox::StaticClass()));

	// 诊断：命中 + input 目标缺失跳过。
	int32 HitDiagnostics = 0;
	int32 SkippedInputDiagnostics = 0;
	for (const FDesign2UMGMappingDiagnostic& Diagnostic : GenerationOutput.MappingDiagnostics)
	{
		HitDiagnostics += Diagnostic.Kind == TEXT("mapping_hit") ? 1 : 0;
		SkippedInputDiagnostics += Diagnostic.Kind == TEXT("mapping_input_skipped") ? 1 : 0;
	}
	TestEqual(TEXT("Both instances report mapping hits"), HitDiagnostics, 2);
	TestEqual(TEXT("Bad-target input skipped with diagnostic"), SkippedInputDiagnostics, 2);

	// manifest：存在且内容正确（provider/scope/sourceRoot/两实例/d2uOwned）。
	const FString AssetPackageName = Blueprint->GetOutermost()->GetName();
	TSharedPtr<FJsonObject> Manifest;
	TestTrue(TEXT("Manifest sidecar written"), ReadSidecarJson(AssetPackageName, Manifest));
	if (Manifest.IsValid())
	{
		TestEqual(TEXT("Manifest schema version"), Manifest->GetIntegerField(TEXT("schemaVersion")), 1);
		TestEqual(TEXT("Manifest provider"), Manifest->GetStringField(TEXT("provider")), TEXT("figma"));
		TestEqual(TEXT("Manifest document scope"), Manifest->GetStringField(TEXT("documentScope")), TestScope);
		const TSharedPtr<FJsonObject>* SourceRoot = nullptr;
		TestTrue(TEXT("Manifest source root present"), Manifest->TryGetObjectField(TEXT("sourceRoot"), SourceRoot) && SourceRoot != nullptr);
		if (SourceRoot && (*SourceRoot).IsValid())
		{
			TestEqual(TEXT("Manifest source root node id"), (*SourceRoot)->GetStringField(TEXT("nodeId")), TEXT("page_root"));
			TestEqual(TEXT("Manifest source root name"), (*SourceRoot)->GetStringField(TEXT("name")), TEXT("MappingWorkflowPage"));
		}

		const TArray<TSharedPtr<FJsonValue>>* Instances = nullptr;
		TestTrue(TEXT("Manifest instances array present"), Manifest->TryGetArrayField(TEXT("instances"), Instances) && Instances != nullptr);
		if (Instances)
		{
			int32 MappedCount = 0;
			int32 GeneratedCount = 0;
			TSet<FString> MappedWidgetNames;
			TSet<FString> MappedDesignNodeIds;
			for (const TSharedPtr<FJsonValue>& Value : *Instances)
			{
				const TSharedPtr<FJsonObject> Instance = Value->AsObject();
				if (!Instance.IsValid())
				{
					continue;
				}
				if (Instance->GetStringField(TEXT("kind")) == TEXT("mapped"))
				{
					++MappedCount;
					MappedWidgetNames.Add(Instance->GetStringField(TEXT("widgetName")));
					MappedDesignNodeIds.Add(Instance->GetStringField(TEXT("designNodeId")));
					TestEqual(TEXT("Mapped instance records its class"), Instance->GetStringField(TEXT("mappedClass")), TestWidgetClassPath);
				}
				else
				{
					++GeneratedCount;
				}
			}
			TestEqual(TEXT("Manifest records both mapped instances"), MappedCount, 2);
			TestEqual(TEXT("Mapped instances have distinct widget names"), MappedWidgetNames.Num(), 2);
			TestEqual(TEXT("Mapped instances have distinct design node ids"), MappedDesignNodeIds.Num(), 2);

			const TArray<TSharedPtr<FJsonValue>>* D2UOwned = nullptr;
			TestTrue(TEXT("Manifest d2uOwned present"), Manifest->TryGetArrayField(TEXT("d2uOwned"), D2UOwned) && D2UOwned != nullptr);
			if (D2UOwned)
			{
				// d2uOwned 现在记录全部创建 widget 名（含嵌套叶子），是 instances 的超集。
				TestTrue(TEXT("d2uOwned covers every recorded instance"),
					D2UOwned->Num() >= MappedCount + GeneratedCount);
			}
		}
	}

	DeleteGeneratedAssetsAtPath(OutputPath);
	DeleteOutputDirectory(OutputPath);
	return true;
}

// ---------------------------------------------------------------------------
// 类缺失回退：mapping 指向的类加载失败时回退普通转换路径
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentMappingClassMissingFallbackTest,
	"Design2UMG.WidgetBlueprint.ComponentMappingClassMissingFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FComponentMappingClassMissingFallbackTest::RunTest(const FString&)
{
	FDesignDocument Document;
	TestTrue(TEXT("Parse fallback fixture"), FFigmaParser::ParseDocument(MakeWorkflowMappingFigmaJson(), Document));
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());

	FDesign2UMGGenerationSettingsSnapshot Settings;
	DesignComponentMapping::FComponentMappingEntry& Entry =
		Settings.ComponentMapping.Table.Entries.AddDefaulted_GetRef();
	Entry.Key = FString::Printf(TEXT("figma|%s|component-key-primary"), TestScope);
	Entry.Provider = TEXT("figma");
	Entry.DocumentScope = TestScope;
	Entry.ComponentKey = TEXT("component-key-primary");
	Entry.WidgetClass = TEXT("/Game/Design2UMG/_TestOutput/DoesNotExist.DoesNotExist_C");

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ComponentMappingFallback");
	DeleteGeneratedAssetsAtPath(OutputPath);

	FDesign2UMGGenerationOutput GenerationOutput;
	UWidgetBlueprint* Blueprint = FUMGGenerator::GenerateWidgetBlueprint(Data, OutputPath, &Settings, &GenerationOutput);
	TestTrue(TEXT("Blueprint still generates on mapped class load failure"), Blueprint != nullptr);
	if (Blueprint && Blueprint->WidgetTree)
	{
		bool bSawMappedInstance = false;
		Blueprint->WidgetTree->ForEachWidget([&bSawMappedInstance](UWidget* Widget)
		{
			bSawMappedInstance |= Widget->IsA<UDesign2UMGMappedComponentTestWidget>();
		});
		TestFalse(TEXT("No mapped instance survives class load failure"), bSawMappedInstance);

		// 组件实例回退为普通转换产物（子树照常展开，内部 Label 文本存在）。
		UWidget* FallbackLabel = Blueprint->WidgetTree->FindWidget(TEXT("Label"));
		TestNotNull(TEXT("Fallback conversion expands component internals"), FallbackLabel);

		int32 FallbackDiagnostics = 0;
		for (const FDesign2UMGMappingDiagnostic& Diagnostic : GenerationOutput.MappingDiagnostics)
		{
			FallbackDiagnostics += Diagnostic.Kind == TEXT("mapping_class_unavailable") ? 1 : 0;
		}
		TestEqual(TEXT("Class load failure diagnosed per node"), FallbackDiagnostics, 2);
	}

	DeleteGeneratedAssetsAtPath(OutputPath);
	DeleteOutputDirectory(OutputPath);
	return true;
}

// ---------------------------------------------------------------------------
// 重复导入：普通路径仍新建（auto-rename）且不动旧 manifest
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentMappingRepeatImportManifestTest,
	"Design2UMG.WidgetBlueprint.ComponentMappingRepeatImportManifest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FComponentMappingRepeatImportManifestTest::RunTest(const FString&)
{
	FDesignDocument Document;
	TestTrue(TEXT("Parse repeat-import fixture"), FFigmaParser::ParseDocument(MakeWorkflowMappingFigmaJson(), Document));
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ComponentMappingRepeat");
	DeleteGeneratedAssetsAtPath(OutputPath);

	const FDesign2UMGGenerationSettingsSnapshot Settings = MakeMappingGenerationSettings();

	UWidgetBlueprint* First = FUMGGenerator::GenerateWidgetBlueprint(Data, OutputPath, &Settings);
	TestTrue(TEXT("First import generates"), First != nullptr);
	if (!First)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	const FString FirstPackage = First->GetOutermost()->GetName();
	FString FirstManifestText;
	const FString FirstSidecar = Design2UMGGenerationManifest::BuildSidecarPathForAsset(FirstPackage);
	TestTrue(TEXT("First manifest written"), FFileHelper::LoadFileToString(FirstManifestText, *FirstSidecar));

	// 普通导入对同名资产走既有 auto-rename 新建路径，不原地更新。
	UWidgetBlueprint* Second = FUMGGenerator::GenerateWidgetBlueprint(Data, OutputPath, &Settings);
	TestTrue(TEXT("Second import generates"), Second != nullptr);
	if (Second)
	{
		TestNotEqual(TEXT("Second import auto-renames to a new asset"),
			Second->GetOutermost()->GetName(), FirstPackage);
		TestTrue(TEXT("Second asset name keeps _N suffix semantics"),
			Second->GetOutermost()->GetName().Contains(TEXT("_1")));

		// 旧资产与旧 manifest 都不动。
		FString FirstManifestAfter;
		TestTrue(TEXT("Old manifest still exists"), FFileHelper::LoadFileToString(FirstManifestAfter, *FirstSidecar));
		TestTrue(TEXT("Old manifest untouched"), FirstManifestAfter == FirstManifestText);

		TSharedPtr<FJsonObject> SecondManifest;
		TestTrue(TEXT("New manifest written for the renamed asset"),
			ReadSidecarJson(Second->GetOutermost()->GetName(), SecondManifest));
	}

	DeleteGeneratedAssetsAtPath(OutputPath);
	DeleteOutputDirectory(OutputPath);
	return true;
}

// ---------------------------------------------------------------------------
// Prototype 边界：设计交互挂在 mapped 身份上时不生成绑定，输出「不生效」诊断
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentMappingPrototypeInertTest,
	"Design2UMG.WidgetBlueprint.ComponentMappingPrototypeInert",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FComponentMappingPrototypeInertTest::RunTest(const FString&)
{
	const FString Json = TEXT(R"({
		"fileKey":"mapping-workflow-file-key",
		"document":{"id":"0:0","name":"Doc","type":"DOCUMENT","children":[
			{"id":"0:1","name":"Page","type":"CANVAS","children":[
				{"id":"page_one","name":"PageOne","type":"FRAME",
				 "absoluteBoundingBox":{"x":0,"y":0,"width":360,"height":240},"children":[
					{"id":"mapped_cta","name":"PrimaryButton","type":"INSTANCE",
					 "componentId":"12:34","componentSetId":"56:78","mainComponentKey":"component-key-primary",
					 "componentProperties":{"label":{"type":"TEXT","value":"Go"}},
					 "absoluteBoundingBox":{"x":40,"y":60,"width":120,"height":44},
					 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.1,"g":0.4,"b":0.9,"a":1}}],
					 "interactions":[{"trigger":{"type":"ON_CLICK"},"actions":[{"type":"NODE","destinationId":"2:1","navigation":"NAVIGATE"}]}],
					 "children":[]}
				]},
				{"id":"2:1","name":"PageTwo","type":"FRAME","visible":true,
				 "absoluteBoundingBox":{"x":0,"y":0,"width":360,"height":240},"children":[
					{"id":"page2_title","name":"Done","type":"TEXT","visible":true,
					 "absoluteBoundingBox":{"x":32,"y":48,"width":120,"height":32},"characters":"Done",
					 "style":{"fontFamily":"Inter","fontSize":24,"fontWeight":600,"textAlignHorizontal":"LEFT","textAlignVertical":"TOP","textDecoration":"NONE","letterSpacing":0,"lineHeightUnit":"AUTO","lineHeightPx":0}}]}
			]}
		]}
	})");

	FDesignDocument Document;
	TestTrue(TEXT("Parse prototype inert fixture"), FFigmaParser::ParseDocument(Json, Document));
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Navigation data carries the click binding"), Data.PrototypeNavigation().ClickBindings.Num() > 0);

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ComponentMappingPrototypeInert");
	DeleteGeneratedAssetsAtPath(OutputPath);

	const FDesign2UMGGenerationSettingsSnapshot Settings = MakeMappingGenerationSettings();
	FDesign2UMGGenerationOutput GenerationOutput;
	UWidgetBlueprint* Blueprint = FUMGGenerator::GenerateWidgetBlueprint(Data, OutputPath, &Settings, &GenerationOutput);
	TestTrue(TEXT("Blueprint with prototype interaction on mapped node generates"), Blueprint != nullptr);

	int32 InertDiagnostics = 0;
	for (const FDesign2UMGMappingDiagnostic& Diagnostic : GenerationOutput.MappingDiagnostics)
	{
		InertDiagnostics += Diagnostic.Kind == TEXT("mapping_prototype_inert") ? 1 : 0;
	}
	TestEqual(TEXT("Design interaction on mapped component reported inert"), InertDiagnostics, 1);

	DeleteGeneratedAssetsAtPath(OutputPath);
	DeleteOutputDirectory(OutputPath);
	return true;
}

// ---------------------------------------------------------------------------
// sidecar 写失败：资产保存成功、导入语义不变，写失败并入结构化诊断
// （GenerationOutput.MappingDiagnostics，随导入报告可见），且不落半截文件。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentMappingManifestWriteFailureTest,
	"Design2UMG.WidgetBlueprint.ComponentMappingManifestWriteFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FComponentMappingManifestWriteFailureTest::RunTest(const FString&)
{
	// sidecar 写失败会按设计打一条 Error 日志（WriteSidecarForAsset），
	// 本用例刻意触发它，登记为预期错误，避免自动化框架按意外错误判失败。
	AddExpectedError(
		TEXT("GenerationManifest: failed to write temp manifest"),
		EAutomationExpectedErrorFlags::Contains, 0);

	FDesignDocument Document;
	TestTrue(TEXT("Parse manifest write failure fixture"),
		FFigmaParser::ParseDocument(MakeWorkflowMappingFigmaJson(), Document));
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ComponentMappingManifestWriteFailure");
	DeleteGeneratedAssetsAtPath(OutputPath);
	DeleteOutputDirectory(OutputPath);

	const FDesign2UMGGenerationSettingsSnapshot Settings;
	FDesign2UMGGenerationOutput GenerationOutput;
	UWidgetBlueprint* WBP = FUMGGenerator::BuildWidgetBlueprint(Data, OutputPath, &Settings, &GenerationOutput);
	if (!TestTrue(TEXT("Build blueprint for the manifest failure case"), WBP != nullptr))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		DeleteOutputDirectory(OutputPath);
		return false;
	}

	// 用同名目录占住 sidecar 的 .tmp 临时文件路径，制造确定性的写失败。
	const FString AssetPackageName = WBP->GetOutermost()->GetName();
	const FString SidecarPath = Design2UMGGenerationManifest::BuildSidecarPathForAsset(AssetPackageName);
	const FString TempPath = SidecarPath + TEXT(".tmp");
	IFileManager::Get().MakeDirectory(*TempPath, true);

	const bool bSaved = FUMGGenerator::SaveWidgetBlueprint(WBP, &GenerationOutput);
	IFileManager::Get().DeleteDirectory(*TempPath, false, true);

	TestTrue(TEXT("Asset save still succeeds when the sidecar cannot be written"), bSaved);

	int32 ManifestFailureDiagnostics = 0;
	FString FailureMessage;
	for (const FDesign2UMGMappingDiagnostic& Diagnostic : GenerationOutput.MappingDiagnostics)
	{
		if (Diagnostic.Kind == TEXT("manifest_write_failed"))
		{
			++ManifestFailureDiagnostics;
			if (ManifestFailureDiagnostics == 1)
			{
				FailureMessage = Diagnostic.Message;
			}
		}
	}
	TestEqual(TEXT("Sidecar write failure diagnosed exactly once"), ManifestFailureDiagnostics, 1);
	TestTrue(TEXT("Diagnostic names the sidecar path"), FailureMessage.Contains(SidecarPath));
	TestFalse(TEXT("No half-written sidecar survives"), FPaths::FileExists(SidecarPath));

	DeleteGeneratedAssetsAtPath(OutputPath);
	DeleteOutputDirectory(OutputPath);
	return true;
}

#endif
