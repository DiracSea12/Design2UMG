// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"

#include "Design2UMGComponentMappingConfig.h"
#include "Design2UMGConverter.h"
#include "Design2UMGGenerationManifest.h"
#include "Design2UMGPrototypeNavigationExtensions.h"
#include "DesignImportPipeline.h"
#include "FigmaParser.h"
#include "UMGGenerator.h"
#include "Tests/Design2UMGMappedComponentTestWidget.h"
#include "Tests/Design2UMGTestAssetCleanup.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tests/Design2UMGMappingQAWidget.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"

#if WITH_AUTOMATION_WORKER

namespace
{
	using Design2UMGEditorTests::DeleteGeneratedAssetsAtPath;

	const TCHAR* ReimportTestScope = TEXT("safe-reimport-file-key");
	const TCHAR* MappedTestClassPath = TEXT("/Script/Design2UMGEditor.Design2UMGMappedComponentTestWidget");
	const TCHAR* OtherMappedTestClassPath = TEXT("/Script/Design2UMGEditor.Design2UMGMappingQAWidget");

	// ---------------------------------------------------------------------------
	// Figma fixture builders（v1/v2/v3 共用同一套节点拼装）
	// ---------------------------------------------------------------------------

	FString TextStyleJson()
	{
		return TEXT("\"style\":{\"fontFamily\":\"Inter\",\"fontSize\":14,\"fontWeight\":400,"
			"\"textAlignHorizontal\":\"LEFT\",\"textAlignVertical\":\"TOP\",\"textDecoration\":\"NONE\","
			"\"letterSpacing\":0,\"lineHeightUnit\":\"AUTO\",\"lineHeightPx\":0}");
	}

	FString MakeTextNode(const TCHAR* Id, const TCHAR* Name, int32 X, int32 Y, const TCHAR* Text)
	{
		return FString::Printf(
			TEXT("{\"id\":\"%s\",\"name\":\"%s\",\"type\":\"TEXT\","
			 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":%d,\"width\":90,\"height\":20},\"characters\":\"%s\",%s}"),
			Id, Name, X, Y, Text, *TextStyleJson());
	}

	// 无背景填充 + 两个子节点：带填充的单子 FRAME 会转成单内容控件（Border 类），
	// 项目子控件会挤不进去；纯容器双子保持多子面板形态，测试才能往里加项目子控件/挪入节点。
	FString MakeCardNode(const TCHAR* Id, const TCHAR* Name, int32 X, int32 Y, const TCHAR* CaptionText)
	{
		return FString::Printf(
			TEXT("{\"id\":\"%s\",\"name\":\"%s\",\"type\":\"FRAME\","
			 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":%d,\"width\":140,\"height\":100},"
			 "\"children\":[{\"id\":\"%s_text\",\"name\":\"Caption\",\"type\":\"TEXT\","
			 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":%d,\"width\":100,\"height\":20},\"characters\":\"%s\",%s},"
			 "{\"id\":\"%s_hint\",\"name\":\"SubHint\",\"type\":\"TEXT\","
			 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":%d,\"width\":100,\"height\":20},\"characters\":\"hint\",%s}]}"),
			Id, Name, X, Y, Id, X + 8, Y + 8, CaptionText, *TextStyleJson(),
			Id, X + 8, Y + 40, *TextStyleJson());
	}

	FString MakeMappedButtonNode(int32 X, int32 Y, const TCHAR* Label)
	{
		return FString::Printf(
			TEXT("{\"id\":\"re_btn_1\",\"name\":\"PrimaryButton\",\"type\":\"INSTANCE\","
			 "\"componentId\":\"11:22\",\"componentSetId\":\"33:44\",\"mainComponentKey\":\"reimport-component-key\","
			 "\"componentProperties\":{\"label\":{\"type\":\"TEXT\",\"value\":\"%s\"}},"
			 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":%d,\"width\":120,\"height\":44},"
			 "\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":0.1,\"g\":0.4,\"b\":0.9,\"a\":1}}],"
			 "\"children\":[{\"id\":\"re_btn_1_label\",\"name\":\"Label\",\"type\":\"TEXT\","
			 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":%d,\"width\":80,\"height\":20},\"characters\":\"%s\",%s}]}"),
			Label, X, Y, X + 8, Y + 12, Label, *TextStyleJson());
	}

	// 无背景填充的纯容器：带填充的 FRAME 会被转换成单内容控件（Border 类），
	// 会让 move 的目标面板拒绝挂载；纯容器保持多子面板形态。
	FString MakeWrapperNode(const FString& ChildrenJson)
	{
		return FString::Printf(
			TEXT("{\"id\":\"re_wrapper\",\"name\":\"Wrapper\",\"type\":\"FRAME\","
			 "\"absoluteBoundingBox\":{\"x\":24,\"y\":140,\"width\":200,\"height\":90},"
			 "\"children\":[%s]}"),
			*ChildrenJson);
	}

	// 可指定身份/位置的无填充纯容器 FRAME（多子面板形态），容器删除+内容迁移用例的 v1/v2 容器。
	FString MakeNamedPanelNode(
		const TCHAR* Id, const TCHAR* Name, int32 X, int32 Y, const FString& ChildrenJson)
	{
		return FString::Printf(
			TEXT("{\"id\":\"%s\",\"name\":\"%s\",\"type\":\"FRAME\","
			 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":%d,\"width\":200,\"height\":120},"
			 "\"children\":[%s]}"),
			Id, Name, X, Y, *ChildrenJson);
	}

	FString MakeLinkNode(const TCHAR* Id, const TCHAR* Name, int32 X, const TCHAR* Url)
	{
		return FString::Printf(
			TEXT("{\"id\":\"%s\",\"name\":\"%s\",\"type\":\"FRAME\","
			 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":180,\"width\":120,\"height\":32},"
			 "\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"URL\",\"url\":\"%s\"}]}],"
			 "\"children\":[{\"id\":\"%s_text\",\"name\":\"%s_label\",\"type\":\"TEXT\","
			 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":186,\"width\":80,\"height\":20},\"characters\":\"Open\",%s}]}"),
			Id, Name, X, Url, Id, Name, X + 8, *TextStyleJson());
	}

	FString MakeReimportDoc(const FString& ChildrenJson)
	{
		return FString::Printf(
			TEXT("{\"fileKey\":\"%s\","
			 "\"document\":{\"id\":\"0:0\",\"name\":\"Doc\",\"type\":\"DOCUMENT\",\"children\":["
			 "{\"id\":\"0:1\",\"name\":\"Page\",\"type\":\"CANVAS\",\"children\":["
			 "{\"id\":\"re_root\",\"name\":\"ReimportPage\",\"type\":\"FRAME\","
			 "\"absoluteBoundingBox\":{\"x\":0,\"y\":0,\"width\":360,\"height\":240},\"children\":[%s]}"
			 "]}]}}"),
			ReimportTestScope, *ChildrenJson);
	}

	FString MakeOtherScopeDoc(const FString& ChildrenJson)
	{
		FString Doc = MakeReimportDoc(ChildrenJson);
		const FString ScopeKeyField = FString::Printf(TEXT("\"fileKey\":\"%s\""), ReimportTestScope);
		Doc.ReplaceInline(*ScopeKeyField, TEXT("\"fileKey\":\"another-file-key\""), ESearchCase::CaseSensitive);
		return Doc;
	}

	FDesign2UMGImportSettingsSnapshot MakeReimportSnapshot(const TCHAR* MappedClassPath)
	{
		FDesign2UMGImportSettingsSnapshot Snapshot;
		Snapshot.ConversionSettings = FDesign2UMGConversionSettings::Defaults();
		if (MappedClassPath)
		{
			DesignComponentMapping::FComponentMappingEntry& Entry =
				Snapshot.GenerationSettings.ComponentMapping.Table.Entries.AddDefaulted_GetRef();
			Entry.Key = FString::Printf(TEXT("figma|%s|reimport-component-key"), ReimportTestScope);
			Entry.Provider = TEXT("figma");
			Entry.DocumentScope = ReimportTestScope;
			Entry.ComponentKey = TEXT("reimport-component-key");
			Entry.WidgetClass = MappedClassPath;

			DesignComponentMapping::FComponentInputDeclaration& Input = Entry.Inputs.AddDefaulted_GetRef();
			Input.Source = TEXT("label");
			Input.Target = TEXT("Label");
			Input.Type = DesignComponentMapping::EComponentInputType::Text;
			Input.ValueKind = DesignComponentMapping::EComponentInputValueKind::ComponentText;
		}
		return Snapshot;
	}

	UWidgetBlueprint* ImportFromJson(const FString& Json, const TCHAR* MappedClassPath, const FString& OutputPath)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);

		FDesignDocument Document;
		if (!FFigmaParser::ParseDocument(Json, Document))
		{
			return nullptr;
		}
		FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
		const FDesign2UMGImportSettingsSnapshot Snapshot = MakeReimportSnapshot(MappedClassPath);
		return FUMGGenerator::GenerateWidgetBlueprint(Data, OutputPath, &Snapshot.GenerationSettings);
	}

	FDesignReimportResult ReimportFromJson(
		const FString& Json,
		const TCHAR* MappedClassPath,
		const FString& TargetPackage,
		const bool bGenerateResolutionCopy = false)
	{
		FDesignDocument Document;
		if (!FFigmaParser::ParseDocument(Json, Document))
		{
			FDesignReimportResult Failed;
			return Failed;
		}

		FDesignReimportRequest Request;
		Request.TargetAssetPath = TargetPackage;
		Request.SettingsSnapshot = MakeReimportSnapshot(MappedClassPath);
		Request.bGenerateResolutionCopy = bGenerateResolutionCopy;
		return DesignImportPipeline::Reimport(Document, Request);
	}

	int32 CountReimportDiagnostics(const FDesignReimportResult& Result, const TCHAR* Kind)
	{
		int32 Count = 0;
		for (const FDesign2UMGMappingDiagnostic& Diagnostic : Result.Diagnostics)
		{
			Count += Diagnostic.Kind == Kind ? 1 : 0;
		}
		return Count;
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

	void DeleteOutputDirectory(const FString& OutputPath)
	{
		const FString OutputDir = FPackageName::LongPackageNameToFilename(OutputPath);
		if (!OutputDir.IsEmpty())
		{
			IFileManager::Get().DeleteDirectory(*OutputDir, false, true);
		}
	}

	bool LoadManifestSidecar(const FString& AssetPackageName, FDesign2UMGGenerationManifestSidecar& OutSidecar)
	{
		FString Error;
		return Design2UMGGenerationManifest::TryLoadSidecarForAsset(AssetPackageName, OutSidecar, Error);
	}
}

// ---------------------------------------------------------------------------
// 属性级生命周期（tasks 3.2/3.4）：
// v1 导入 -> 开发者定制（加项目子控件 / 改 D2U 属性）-> v2 reimport ->
// D2U-owned 已更新（文本/布局/mapped inputs 覆写）且定制保留、mapped 实例不被替换。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportLifecycleTest,
	"Design2UMG.WidgetBlueprint.SafeReimportLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportLifecycleTest::RunTest(const FString&)
{
	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/SafeReimportLifecycle");

	const FString V1 = MakeReimportDoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeMappedButtonNode(24, 24, TEXT("Checkout")),
		*MakeCardNode(TEXT("re_card"), TEXT("PlainCard"), 180, 24, TEXT("Not mapped"))));

	UWidgetBlueprint* WBP = ImportFromJson(V1, MappedTestClassPath, OutputPath);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		DeleteOutputDirectory(OutputPath);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	UDesign2UMGMappedComponentTestWidget* MappedBefore = nullptr;
	WBP->WidgetTree->ForEachWidget([&MappedBefore](UWidget* Widget)
	{
		if (UDesign2UMGMappedComponentTestWidget* Candidate = Cast<UDesign2UMGMappedComponentTestWidget>(Widget))
		{
			MappedBefore = Candidate;
		}
	});
	if (!TestNotNull(TEXT("Mapped instance generated"), MappedBefore))
	{
		DeleteOutputDirectory(OutputPath);
		return false;
	}
	TWeakObjectPtr<UWidget> MappedWeak = MappedBefore;

	UCanvasPanelSlot* MappedSlotBefore = Cast<UCanvasPanelSlot>(MappedBefore->Slot);
	const float BeforeY = MappedSlotBefore ? MappedSlotBefore->GetPosition().Y : -1.0f;

	// 模拟开发者定制：项目加一个子控件 + 改 D2U 文本属性（应被设计值刷新）。
	UTextBlock* ProjectBadge = WBP->WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("ProjectBadge"));
	ProjectBadge->SetText(FText::FromString(TEXT("Project Content")));
	if (UPanelWidget* RootPanel = Cast<UPanelWidget>(WBP->WidgetTree->RootWidget))
	{
		RootPanel->AddChild(ProjectBadge);
	}
	if (UTextBlock* Caption = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Caption"))))
	{
		Caption->SetText(FText::FromString(TEXT("DeveloperEdit")));
	}

	// v2：label 换值、按钮下移、卡片文本换值。
	const FString V2 = MakeReimportDoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeMappedButtonNode(40, 88, TEXT("Pay Now")),
		*MakeCardNode(TEXT("re_card"), TEXT("PlainCard"), 180, 24, TEXT("Caption v2"))));

	const FDesignReimportResult Result = ReimportFromJson(V2, MappedTestClassPath, TargetPackage);
	TestTrue(TEXT("v2 reimport succeeds"), Result.bSucceeded);

	// D2U-owned 已更新：文本、mapped inputs 直接覆写。
	UTextBlock* CaptionAfter = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Caption")));
	TestNotNull(TEXT("Caption widget still present"), CaptionAfter);
	if (CaptionAfter)
	{
		TestEqual(TEXT("D2U text refreshed from design"), CaptionAfter->GetText().ToString(), TEXT("Caption v2"));
	}

	UWidget* MappedAfter = MappedWeak.Get();
	TestNotNull(TEXT("Mapped instance still alive"), MappedAfter);
	if (UDesign2UMGMappedComponentTestWidget* Mapped = Cast<UDesign2UMGMappedComponentTestWidget>(MappedAfter))
	{
		TestTrue(TEXT("Mapped instance not replaced"), Mapped == MappedBefore);
		TestEqual(TEXT("Declared input overwritten with latest design value"),
			Mapped->Label.ToString(), TEXT("Pay Now"));
		// 外层布局更新：按钮从 y=24 下移到 y=88。
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Mapped->Slot))
		{
			TestTrue(TEXT("Mapped instance outer layout updated"),
				FMath::Abs(Slot->GetPosition().Y - 88.0f) < 4.0f && FMath::Abs(Slot->GetPosition().X - 40.0f) < 4.0f);
		}
		else
		{
			AddError(TEXT("Mapped instance slot is not a canvas slot"));
		}
	}
	TestTrue(TEXT("Pre-reimport slot Y was the v1 position"), FMath::Abs(BeforeY - 24.0f) < 4.0f);

	// 定制保留：项目加的子控件还在（且不属于 d2uOwned）。
	TestNotNull(TEXT("Project-added widget preserved"), WBP->WidgetTree->FindWidget(TEXT("ProjectBadge")));

	// 诊断：matched 更新 + inputs 覆写。
	TestTrue(TEXT("Update diagnostics present"), CountReimportDiagnostics(Result, TEXT("reimport_updated")) >= 2);
	TestTrue(TEXT("Input overwrite diagnostics present"),
		CountReimportDiagnostics(Result, TEXT("mapping_input_applied")) >= 1);

	// manifest 更新：mapped 实例 + 顶层 generated 记录在，项目 widget 不在 d2uOwned。
	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("Manifest reloaded after reimport"), LoadManifestSidecar(TargetPackage, Sidecar)))
	{
		TestTrue(TEXT("Mapped instance still recorded"),
			Sidecar.FindInstanceByDesignNodeId(TEXT("re_btn_1")) != nullptr);
		TestTrue(TEXT("Generated instance still recorded"),
			Sidecar.FindInstanceByDesignNodeId(TEXT("re_card")) != nullptr);
		TestTrue(TEXT("Nested D2U widget owned"), Sidecar.D2UOwned.Contains(TEXT("Caption")));
		TestFalse(TEXT("Project-added widget not owned"), Sidecar.D2UOwned.Contains(TEXT("ProjectBadge")));
	}

	DeleteGeneratedAssetsAtPath(OutputPath);
	DeleteOutputDirectory(OutputPath);
	return true;
}

// ---------------------------------------------------------------------------
// 结构变化（tasks 4.1/4.2/4.3）：add（新顶层节点）/ move（挪进新容器）/ remove（纯 D2U 删除）。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportStructureTest,
	"Design2UMG.WidgetBlueprint.SafeReimportStructure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportStructureTest::RunTest(const FString&)
{
	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/SafeReimportStructure");

	const FString V1 = MakeReimportDoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeCardNode(TEXT("re_card"), TEXT("PlainCard"), 24, 24, TEXT("Card v1")),
		*MakeTextNode(TEXT("re_stat"), TEXT("StatText"), 200, 24, TEXT("Old"))));

	UWidgetBlueprint* WBP = ImportFromJson(V1, nullptr, OutputPath);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		DeleteOutputDirectory(OutputPath);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	TestNotNull(TEXT("v1 has StatText"), WBP->WidgetTree->FindWidget(TEXT("StatText")));

	// v2：StatText 消失（remove）；PlainCard 挪进新容器 Wrapper（move，容器里还有第二个
	// 子节点保证它转换成多子面板）；新增 NewBadge（add）。
	const FString V2 = MakeReimportDoc(FString::Printf(
		TEXT("%s,%s,%s"),
		*MakeWrapperNode(FString::Printf(
			TEXT("%s,%s"),
			*MakeCardNode(TEXT("re_card"), TEXT("PlainCard"), 32, 152, TEXT("Card v2")),
			*MakeTextNode(TEXT("re_wrapper_hint"), TEXT("WrapperHint"), 32, 190, TEXT("Hint")))),
		*MakeTextNode(TEXT("re_badge"), TEXT("NewBadge"), 200, 140, TEXT("Added")),
		*MakeTextNode(TEXT("re_keep"), TEXT("KeepText"), 200, 24, TEXT("Keep"))));

	const FDesignReimportResult Result = ReimportFromJson(V2, nullptr, TargetPackage);
	TestTrue(TEXT("v2 reimport succeeds"), Result.bSucceeded);

	// remove：纯 D2U 节点安全删除。
	TestNull(TEXT("Removed design node deleted from tree"), WBP->WidgetTree->FindWidget(TEXT("StatText")));
	TestTrue(TEXT("Safe remove diagnosed"), CountReimportDiagnostics(Result, TEXT("reimport_removed")) >= 1);

	// add：新顶层节点加入并记录进 manifest。
	UWidget* NewBadge = WBP->WidgetTree->FindWidget(TEXT("NewBadge"));
	TestNotNull(TEXT("New design node added"), NewBadge);
	TestTrue(TEXT("Add diagnosed"), CountReimportDiagnostics(Result, TEXT("reimport_added")) >= 1);

	// move：PlainCard 挂进 Wrapper，widget 身份保留。
	UWidget* Wrapper = WBP->WidgetTree->FindWidget(TEXT("Wrapper"));
	UWidget* Card = WBP->WidgetTree->FindWidget(TEXT("PlainCard"));
	TestNotNull(TEXT("New wrapper container added"), Wrapper);
	TestNotNull(TEXT("Moved card still exists"), Card);
	if (Wrapper && Card)
	{
		TestEqual(TEXT("Card reparented under the wrapper"), Card->GetParent()->GetFName(), Wrapper->GetFName());
	}
	TestTrue(TEXT("Move diagnosed"), CountReimportDiagnostics(Result, TEXT("reimport_moved")) >= 1);

	// 卡片内部文本同样被刷新。
	if (UTextBlock* Caption = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Caption"))))
	{
		TestEqual(TEXT("Moved subtree text refreshed"), Caption->GetText().ToString(), TEXT("Card v2"));
	}

	// manifest：Wrapper/NewBadge 记录为顶层实例；PlainCard 挪出顶层后不再是实例但仍在 d2uOwned。
	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("Manifest updated after structural reimport"), LoadManifestSidecar(TargetPackage, Sidecar)))
	{
		TestNotNull(TEXT("Wrapper recorded as instance"), Sidecar.FindInstanceByDesignNodeId(TEXT("re_wrapper")));
		TestNotNull(TEXT("NewBadge recorded as instance"), Sidecar.FindInstanceByDesignNodeId(TEXT("re_badge")));
		TestNull(TEXT("Moved card no longer a top-level instance"), Sidecar.FindInstanceByDesignNodeId(TEXT("re_card")));
		TestTrue(TEXT("Moved card still D2U-owned"), Sidecar.D2UOwned.Contains(TEXT("PlainCard")));
		TestTrue(TEXT("Removed node dropped from ownership"), !Sidecar.D2UOwned.Contains(TEXT("StatText")));
		TestTrue(TEXT("New node ownership recorded"), Sidecar.D2UOwned.Contains(TEXT("NewBadge")));
	}

	DeleteGeneratedAssetsAtPath(OutputPath);
	DeleteOutputDirectory(OutputPath);
	return true;
}

// ---------------------------------------------------------------------------
// 容器删除 + 内容迁移（嵌套 mapped 实例，safe-reimport spec 的 identity-preserving
// move）：v1 FrameA>Button(命中 mapping)；v2 删 FrameA、新增 FrameC>Button。
// 旧容器整删判定必须识别子树内仍存活的 manifest 实例 -> 保守保留 FrameA，
// Button 走自己的 move 计划挂到 FrameC 下：实例存活、identity 保留、manifest
// 无幽灵记录（每条实例记录都能定位到树里的 widget）、无重复记录、诊断可读。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportNestedInstanceMigrationTest,
	"Design2UMG.WidgetBlueprint.SafeReimportNestedInstanceMigration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportNestedInstanceMigrationTest::RunTest(const FString&)
{
	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/SafeReimportNestedMigration");

	// v1：FrameA（纯容器双子保持面板形态）内挂 mapped 实例按钮 + 一条占位文本。
	const FString V1 = MakeReimportDoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeNamedPanelNode(TEXT("re_frame_a"), TEXT("FrameA"), 24, 24, FString::Printf(
			TEXT("%s,%s"),
			*MakeMappedButtonNode(32, 32, TEXT("Checkout")),
			*MakeTextNode(TEXT("re_a_pad"), TEXT("FrameAPad"), 32, 84, TEXT("Pad")))),
		*MakeTextNode(TEXT("re_keep"), TEXT("KeepText"), 250, 24, TEXT("Keep"))));

	UWidgetBlueprint* WBP = ImportFromJson(V1, MappedTestClassPath, OutputPath);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		DeleteOutputDirectory(OutputPath);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	UDesign2UMGMappedComponentTestWidget* MappedBefore = nullptr;
	WBP->WidgetTree->ForEachWidget([&MappedBefore](UWidget* Widget)
	{
		if (UDesign2UMGMappedComponentTestWidget* Candidate = Cast<UDesign2UMGMappedComponentTestWidget>(Widget))
		{
			MappedBefore = Candidate;
		}
	});
	if (!TestNotNull(TEXT("v1 has the nested mapped instance"), MappedBefore)
		|| !TestTrue(TEXT("v1 mapped instance sits under FrameA"),
			MappedBefore->GetParent() && MappedBefore->GetParent()->GetFName() == FName(TEXT("FrameA"))))
	{
		DeleteOutputDirectory(OutputPath);
		return false;
	}
	TWeakObjectPtr<UDesign2UMGMappedComponentTestWidget> MappedWeak = MappedBefore;

	// v2：删 FrameA，新增 FrameC 收编同一个按钮（designNodeId 不变，仍是 mapped）。
	const FString V2 = MakeReimportDoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeNamedPanelNode(TEXT("re_frame_c"), TEXT("FrameC"), 24, 108, FString::Printf(
			TEXT("%s,%s"),
			*MakeMappedButtonNode(32, 116, TEXT("Pay Now")),
			*MakeTextNode(TEXT("re_c_pad"), TEXT("FrameCPad"), 32, 170, TEXT("Pad")))),
		*MakeTextNode(TEXT("re_keep"), TEXT("KeepText"), 250, 24, TEXT("Keep"))));

	const FDesignReimportResult Result = ReimportFromJson(V2, MappedTestClassPath, TargetPackage);
	TestTrue(TEXT("v2 reimport succeeds"), Result.bSucceeded);

	// 实例存活 + identity 保留：同一个对象，挂到新容器 FrameC 下。
	UDesign2UMGMappedComponentTestWidget* MappedAfter = MappedWeak.Get();
	TestNotNull(TEXT("nested mapped instance survived the container swap"), MappedAfter);
	UWidget* NewContainer = WBP->WidgetTree->FindWidget(TEXT("FrameC"));
	TestNotNull(TEXT("new container added"), NewContainer);
	if (MappedAfter && NewContainer)
	{
		TestTrue(TEXT("widget object identity preserved"), MappedAfter == MappedBefore);
		TestEqual(TEXT("instance reparented under the new container"),
			MappedAfter->GetParent()->GetFName(), NewContainer->GetFName());
		TestEqual(TEXT("declared input overwritten with the v2 design value"),
			MappedAfter->Label.ToString(), TEXT("Pay Now"));
	}

	// 旧容器保守保留（子树内含存活实例，不可整删）。
	TestNotNull(TEXT("old container preserved conservatively"), WBP->WidgetTree->FindWidget(TEXT("FrameA")));

	// 诊断可读：保守保留点名存活实例；move 点名 identity；不得出现 node_missing 幽灵路径。
	TestEqual(TEXT("one remove conflict for the old container"),
		CountReimportDiagnostics(Result, TEXT("reimport_remove_conflict")), 1);
	TestTrue(TEXT("remove-conflict message names the live instance"),
		[&Result]()
		{
			for (const FDesign2UMGMappingDiagnostic& Diagnostic : Result.Diagnostics)
			{
				if (Diagnostic.Kind == TEXT("reimport_remove_conflict")
					&& Diagnostic.Message.Contains(TEXT("FrameA"))
					&& Diagnostic.Message.Contains(TEXT("PrimaryButton")))
				{
					return true;
				}
			}
			return false;
		}());
	TestEqual(TEXT("one identity-preserving move diagnosed"),
		CountReimportDiagnostics(Result, TEXT("reimport_moved")), 1);
	TestEqual(TEXT("no ghost node-missing diagnostics"),
		CountReimportDiagnostics(Result, TEXT("reimport_node_missing")), 0);

	// manifest 自洽：实例在、新旧容器都在、无幽灵记录、无重复记录。
	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("Manifest updated after migration"), LoadManifestSidecar(TargetPackage, Sidecar)))
	{
		TestNotNull(TEXT("migrated instance still recorded"),
			Sidecar.FindInstanceByDesignNodeId(TEXT("re_btn_1")));
		TestNotNull(TEXT("preserved old container stays recorded"),
			Sidecar.FindInstanceByDesignNodeId(TEXT("re_frame_a")));
		TestNotNull(TEXT("new container recorded"),
			Sidecar.FindInstanceByDesignNodeId(TEXT("re_frame_c")));

		int32 ButtonRecords = 0;
		int32 GhostRecords = 0;
		for (const FDesign2UMGGenerationManifestSidecar::FInstanceRecord& Record : Sidecar.Instances)
		{
			ButtonRecords += Record.DesignNodeId == TEXT("re_btn_1") ? 1 : 0;
			GhostRecords += WBP->WidgetTree->FindWidget(FName(*Record.WidgetName)) ? 0 : 1;
		}
		TestEqual(TEXT("migrated instance recorded exactly once"), ButtonRecords, 1);
		TestEqual(TEXT("no ghost records (every record resolves to a live widget)"), GhostRecords, 0);
		TestTrue(TEXT("migrated instance still D2U-owned"), Sidecar.D2UOwned.Contains(TEXT("PrimaryButton")));
	}

	DeleteGeneratedAssetsAtPath(OutputPath);
	DeleteOutputDirectory(OutputPath);
	return true;
}

// ---------------------------------------------------------------------------
// 身份转换（task 4.6）：generated -> mapped（v2 补建 mapping）-> generated（v3 删 mapping）。
// 旧子树纯 D2U-owned，两次都替换为新形态并诊断。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportKindChangedTest,
	"Design2UMG.WidgetBlueprint.SafeReimportKindChanged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportKindChangedTest::RunTest(const FString&)
{
	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/SafeReimportKindChanged");

	// v1：没有 mapping 配置 -> 组件实例走普通展开（generated）。
	const FString V1 = MakeReimportDoc(MakeMappedButtonNode(24, 24, TEXT("Checkout")));
	UWidgetBlueprint* WBP = ImportFromJson(V1, nullptr, OutputPath);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		DeleteOutputDirectory(OutputPath);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	TestNotNull(TEXT("v1 expands component internals"),
		WBP->WidgetTree->FindWidget(TEXT("Label")));

	// v2：补建 mapping -> 同一设计节点变 mapped；旧子树纯 D2U-owned -> 替换。
	const FString V2 = MakeReimportDoc(MakeMappedButtonNode(24, 24, TEXT("Checkout")));
	FDesignReimportResult Result = ReimportFromJson(V2, MappedTestClassPath, TargetPackage);
	TestTrue(TEXT("v2 reimport succeeds"), Result.bSucceeded);
	TestTrue(TEXT("Kind change diagnosed"), CountReimportDiagnostics(Result, TEXT("reimport_kind_changed")) == 1);

	bool bHasMappedInstance = false;
	bool bHasExpandedLabel = false;
	WBP->WidgetTree->ForEachWidget([&bHasMappedInstance, &bHasExpandedLabel](UWidget* Widget)
	{
		bHasMappedInstance |= Widget->IsA<UDesign2UMGMappedComponentTestWidget>();
		bHasExpandedLabel |= (Widget->GetFName() == FName(TEXT("Label")));
	});
	TestTrue(TEXT("Old generated subtree replaced with mapped instance"), bHasMappedInstance && !bHasExpandedLabel);

	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("Manifest readable after kind change"), LoadManifestSidecar(TargetPackage, Sidecar)))
	{
		const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* Record =
			Sidecar.FindInstanceByDesignNodeId(TEXT("re_btn_1"));
		TestNotNull(TEXT("Transitioned node still recorded"), Record);
		if (Record)
		{
			TestEqual(TEXT("Manifest kind updated to mapped"), Record->Kind, FString(TEXT("mapped")));
			TestEqual(TEXT("Manifest mapped class updated"), Record->MappedClass, FString(MappedTestClassPath));
		}
	}

	// v3：删除 mapping -> 变回 generated 展开，同样替换。
	const FString V3 = MakeReimportDoc(MakeMappedButtonNode(24, 24, TEXT("Checkout")));
	Result = ReimportFromJson(V3, nullptr, TargetPackage);
	TestTrue(TEXT("v3 reimport succeeds"), Result.bSucceeded);
	TestTrue(TEXT("Reverse kind change diagnosed"), CountReimportDiagnostics(Result, TEXT("reimport_kind_changed")) == 1);

	bool bHasMappedAfterV3 = false;
	WBP->WidgetTree->ForEachWidget([&bHasMappedAfterV3](UWidget* Widget)
	{
		bHasMappedAfterV3 |= Widget->IsA<UDesign2UMGMappedComponentTestWidget>();
	});
	TestFalse(TEXT("Mapped instance replaced back with generated expansion"), bHasMappedAfterV3);
	TestNotNull(TEXT("Generated expansion restored"), WBP->WidgetTree->FindWidget(TEXT("Label")));

	DeleteGeneratedAssetsAtPath(OutputPath);
	DeleteOutputDirectory(OutputPath);
	return true;
}

// ---------------------------------------------------------------------------
// 冲突（tasks 4.4/4.5）：Mapped Class 变化不替换；同一交互已有项目绑定时
// D2U Prototype 绑定让位。（remove 含项目内容的冲突见 SafeReimportRemoveConflict。）
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportConflictTest,
	"Design2UMG.WidgetBlueprint.SafeReimportConflict",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportConflictTest::RunTest(const FString&)
{
	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/SafeReimportConflict");

	const FString V1 = MakeReimportDoc(FString::Printf(
		TEXT("%s,%s,%s"),
		*MakeMappedButtonNode(24, 24, TEXT("Checkout")),
		*MakeLinkNode(TEXT("re_link_btn"), TEXT("LinkArea"), 24, TEXT("https://example.com/old")),
		*MakeLinkNode(TEXT("re_link_btn2"), TEXT("LinkArea2"), 160, TEXT("https://example.com/keep"))));

	UWidgetBlueprint* WBP = ImportFromJson(V1, MappedTestClassPath, OutputPath);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		DeleteOutputDirectory(OutputPath);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// 模拟开发者定制：
	// 1) 给 re_link_btn 挂项目 OnClicked 绑定（双触发冲突）。
	FDelegateEditorBinding ProjectBinding;
	ProjectBinding.ObjectName = TEXT("re_link_btn");
	ProjectBinding.PropertyName = TEXT("OnClicked");
	ProjectBinding.FunctionName = TEXT("HandleLinkClick");
	WBP->Bindings.Add(ProjectBinding);
	// 2) 改 mapped 实例的输入值（Mapped Class 冲突时不允许被刷新）。
	TWeakObjectPtr<UDesign2UMGMappedComponentTestWidget> MappedBefore;
	WBP->WidgetTree->ForEachWidget([&MappedBefore](UWidget* Widget)
	{
		if (UDesign2UMGMappedComponentTestWidget* Candidate = Cast<UDesign2UMGMappedComponentTestWidget>(Widget))
		{
			MappedBefore = Candidate;
		}
	});
	if (UDesign2UMGMappedComponentTestWidget* Mapped = MappedBefore.Get())
	{
		Mapped->Label = FText::FromString(TEXT("Hacked"));
	}

	// v2：两个交互仍在（URL 变化）；mapping 换到另一个类。
	const FString V2 = MakeReimportDoc(FString::Printf(
		TEXT("%s,%s,%s"),
		*MakeMappedButtonNode(24, 24, TEXT("Pay Now")),
		*MakeLinkNode(TEXT("re_link_btn"), TEXT("LinkArea"), 24, TEXT("https://example.com/new")),
		*MakeLinkNode(TEXT("re_link_btn2"), TEXT("LinkArea2"), 160, TEXT("https://example.com/keep"))));

	const FDesignReimportResult Result = ReimportFromJson(V2, OtherMappedTestClassPath, TargetPackage);
	TestTrue(TEXT("v2 reimport itself succeeds (conflicts are per-node)"), Result.bSucceeded);

	// 冲突 1：Mapped Class 变化 -> 实例保留、不替换、inputs 不覆写。
	UDesign2UMGMappedComponentTestWidget* MappedAfter = Cast<UDesign2UMGMappedComponentTestWidget>(MappedBefore.Get());
	TestNotNull(TEXT("Mapped instance kept"), MappedAfter);
	if (MappedAfter)
	{
		TestEqual(TEXT("Mapped instance class unchanged"), MappedAfter->GetClass()->GetPathName(), FString(MappedTestClassPath));
		TestEqual(TEXT("Inputs not overwritten on mapped-class conflict"),
			MappedAfter->Label.ToString(), TEXT("Hacked"));
	}
	TestTrue(TEXT("Mapped class conflict diagnosed"), CountReimportDiagnostics(Result, TEXT("reimport_mapped_class_conflict")) == 1);

	// 冲突 2：双触发 -> 项目绑定保留，D2U 绑定让位（只剩无冲突的第二条）。
	TestTrue(TEXT("Dual-trigger conflict diagnosed"), CountReimportDiagnostics(Result, TEXT("reimport_prototype_conflict")) == 1);
	TestTrue(TEXT("Project binding kept in WBP"), WBP->Bindings.Contains(ProjectBinding));
	if (UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(WBP->GeneratedClass))
	{
		const UDesign2UMGPrototypeNavigationClassExtension* RuntimeExtension =
			GeneratedClass->GetExtension<UDesign2UMGPrototypeNavigationClassExtension>();
		if (TestNotNull(TEXT("Prototype class extension present after reimport"), RuntimeExtension))
		{
			const FUMGPrototypeNavigationData& NavData = RuntimeExtension->GetNavigationDataForTest();
			TestEqual(TEXT("D2U binding yields to project binding"), NavData.ClickBindings.Num(), 1);
			if (NavData.ClickBindings.Num() == 1)
			{
				TestEqual(TEXT("Remaining binding is the unconflicted one"),
					NavData.ClickBindings[0].TriggerWidgetName, FString(TEXT("re_link_btn2")));
			}
		}
	}

	DeleteGeneratedAssetsAtPath(OutputPath);
	DeleteOutputDirectory(OutputPath);
	return true;
}

// ---------------------------------------------------------------------------
// remove 含项目内容（task 4.3 冲突面）：设计删除的节点子树里有项目后加的子控件 ->
// 不删，保留 + 冲突诊断（对照：不含项目内容的删除见 SafeReimportStructure）。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportRemoveConflictTest,
	"Design2UMG.WidgetBlueprint.SafeReimportRemoveConflict",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportRemoveConflictTest::RunTest(const FString&)
{
	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/SafeReimportRemoveConflict");

	const FString V1 = MakeReimportDoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeCardNode(TEXT("re_card"), TEXT("PlainCard"), 24, 24, TEXT("Doomed")),
		*MakeTextNode(TEXT("re_keep"), TEXT("KeepText"), 200, 24, TEXT("Keep"))));

	UWidgetBlueprint* WBP = ImportFromJson(V1, nullptr, OutputPath);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		DeleteOutputDirectory(OutputPath);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// 模拟开发者定制：在 PlainCard 子树里加项目子控件（设计删除时必须保留）。
	if (UPanelWidget* CardPanel = Cast<UPanelWidget>(WBP->WidgetTree->FindWidget(TEXT("PlainCard"))))
	{
		UTextBlock* ProjectChild = WBP->WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), TEXT("ProjectChild"));
		ProjectChild->SetText(FText::FromString(TEXT("Project")));
		CardPanel->AddChild(ProjectChild);
		TestNotNull(TEXT("Project child added"), WBP->WidgetTree->FindWidget(TEXT("ProjectChild")));
	}

	// v2：PlainCard 从设计里消失。
	const FString V2 = MakeReimportDoc(MakeTextNode(TEXT("re_keep"), TEXT("KeepText"), 200, 24, TEXT("Keep")));
	const FDesignReimportResult Result = ReimportFromJson(V2, nullptr, TargetPackage);
	TestTrue(TEXT("v2 reimport succeeds (conflict is per-node)"), Result.bSucceeded);

	TestNotNull(TEXT("Removed-with-project-content node preserved"), WBP->WidgetTree->FindWidget(TEXT("PlainCard")));
	TestNotNull(TEXT("Project child preserved with it"), WBP->WidgetTree->FindWidget(TEXT("ProjectChild")));
	TestTrue(TEXT("Remove conflict diagnosed"), CountReimportDiagnostics(Result, TEXT("reimport_remove_conflict")) == 1);
	TestTrue(TEXT("No silent delete"), CountReimportDiagnostics(Result, TEXT("reimport_removed")) == 0);

	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("Manifest readable after conflict"), LoadManifestSidecar(TargetPackage, Sidecar)))
	{
		TestNotNull(TEXT("Conflicted node stays recorded"), Sidecar.FindInstanceByDesignNodeId(TEXT("re_card")));
		TestTrue(TEXT("Project widget stays outside d2uOwned"), !Sidecar.D2UOwned.Contains(TEXT("ProjectChild")));
	}

	DeleteGeneratedAssetsAtPath(OutputPath);
	DeleteOutputDirectory(OutputPath);
	return true;
}

// ---------------------------------------------------------------------------
// 并列副本兜底（task 4.7）：scope 不一致 / manifest 缺失 = 全量冲突；
// 默认保留原资产；用户显式选择才生成并列 WBP + 新 manifest。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportResolutionCopyTest,
	"Design2UMG.WidgetBlueprint.SafeReimportResolutionCopy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportResolutionCopyTest::RunTest(const FString&)
{
	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/SafeReimportResolutionCopy");

	const FString V1 = MakeReimportDoc(MakeCardNode(TEXT("re_card"), TEXT("PlainCard"), 24, 24, TEXT("Card v1")));
	UWidgetBlueprint* WBP = ImportFromJson(V1, nullptr, OutputPath);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		DeleteOutputDirectory(OutputPath);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	const FString OriginalAssetFile =
		FPackageName::LongPackageNameToFilename(TargetPackage, FPackageName::GetAssetPackageExtension());

	const FString V2 = MakeReimportDoc(MakeCardNode(TEXT("re_card"), TEXT("PlainCard"), 24, 24, TEXT("Card v2")));

	// 场景 1：DocumentScope 不一致 -> 全量冲突，默认不生成副本。
	const FString OtherScopeDoc = MakeOtherScopeDoc(MakeCardNode(TEXT("re_card"), TEXT("PlainCard"), 24, 24, TEXT("Card v2")));
	FDesignReimportResult Result = ReimportFromJson(OtherScopeDoc, nullptr, TargetPackage);
	TestFalse(TEXT("Scope mismatch reimport does not update in place"), Result.bSucceeded);
	TestTrue(TEXT("Scope mismatch reported as full conflict"), Result.bFullConflict);
	TestTrue(TEXT("Full conflict diagnosed"), CountReimportDiagnostics(Result, TEXT("reimport_full_conflict")) == 1);
	TestTrue(TEXT("Original asset file preserved"), FPaths::FileExists(OriginalAssetFile));
	TestTrue(TEXT("No copy without explicit choice"), Result.ResolutionCopyPath.IsEmpty());

	// 场景 2：manifest 缺失 -> 全量冲突；显式选择后生成并列副本（原资产、原状态不动）。
	IFileManager::Get().Delete(*Design2UMGGenerationManifest::BuildSidecarPathForAsset(TargetPackage), false, true);
	const int64 OriginalSize = IFileManager::Get().FileSize(*OriginalAssetFile);

	Result = ReimportFromJson(V2, nullptr, TargetPackage);
	TestTrue(TEXT("Missing manifest reported as full conflict"), Result.bFullConflict);
	TestFalse(TEXT("Default action preserves without copy"), Result.bSucceeded);
	TestTrue(TEXT("Still no implicit copy"), Result.ResolutionCopyPath.IsEmpty());

	Result = ReimportFromJson(V2, nullptr, TargetPackage, /*bGenerateResolutionCopy=*/true);
	TestTrue(TEXT("Explicit choice still reports the full conflict"), Result.bFullConflict);
	TestTrue(TEXT("Resolution copy generated"), !Result.ResolutionCopyPath.IsEmpty());

	const FString CopyPackage = FPaths::GetPath(TargetPackage) / TEXT("WBP_ReimportPage_Resolution");
	TestEqual(TEXT("Resolution copy path reported"), Result.ResolutionCopyPath, CopyPackage + TEXT(".WBP_ReimportPage_Resolution"));
	FDesign2UMGGenerationManifestSidecar CopySidecar;
	TestTrue(TEXT("Resolution copy has its own manifest"),
		LoadManifestSidecar(CopyPackage, CopySidecar));
	TestTrue(TEXT("Original asset untouched"), FPaths::FileExists(OriginalAssetFile));
	TestEqual(TEXT("Original asset bytes unchanged"),
		IFileManager::Get().FileSize(*OriginalAssetFile), OriginalSize);
	TestFalse(TEXT("Original still has no manifest"),
		FPaths::FileExists(Design2UMGGenerationManifest::BuildSidecarPathForAsset(TargetPackage)));

	DeleteGeneratedAssetsAtPath(OutputPath);
	DeleteOutputDirectory(OutputPath);
	return true;
}

// ---------------------------------------------------------------------------
// 保存失败语义（task 3.3）：保存失败 -> 事务回滚、资产保持 reimport 前状态、manifest 不更新。
// 保存失败由 FUMGGenerator::ArmAutomationSaveFailureOnce() 注入（引擎的 SavePackage 失败
// 走 appError，不能当测试注入手段）。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportSaveFailureTest,
	"Design2UMG.WidgetBlueprint.SafeReimportSaveFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportSaveFailureTest::RunTest(const FString&)
{
	// 保存失败的注入路径会按设计打错误日志，登记为预期。
	AddExpectedError(TEXT("failed to save"), EAutomationExpectedErrorFlags::Contains, 0);

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/SafeReimportSaveFailure");

	const FString V1 = MakeReimportDoc(MakeCardNode(TEXT("re_card"), TEXT("PlainCard"), 24, 24, TEXT("Card v1")));
	UWidgetBlueprint* WBP = ImportFromJson(V1, nullptr, OutputPath);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		DeleteOutputDirectory(OutputPath);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	const FString SidecarPath = Design2UMGGenerationManifest::BuildSidecarPathForAsset(TargetPackage);
	FString ManifestBefore;
	TestTrue(TEXT("v1 manifest exists"), FFileHelper::LoadFileToString(ManifestBefore, *SidecarPath));

	const FString V2 = MakeReimportDoc(MakeCardNode(TEXT("re_card"), TEXT("PlainCard"), 24, 24, TEXT("Card v2")));

	// 用测试注入让下一次 SaveWidgetBlueprint 确定性失败一次（生产路径不受影响）。
	// 引擎对 SavePackage 失败会走 appError，不能作为测试注入手段。
	FUMGGenerator::ArmAutomationSaveFailureOnce();
	const FDesignReimportResult Result = ReimportFromJson(V2, nullptr, TargetPackage);

	TestFalse(TEXT("Reimport reports failure"), Result.bSucceeded);
	TestTrue(TEXT("Save failure diagnosed"), CountReimportDiagnostics(Result, TEXT("reimport_save_failed")) == 1);

	// 事务回滚：内存里的文本属性保持 reimport 前状态（开发者定制值），不是 v2 设计值。
	if (UTextBlock* Caption = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Caption"))))
	{
		TestEqual(TEXT("In-memory state rolled back to pre-reimport"),
			Caption->GetText().ToString(), TEXT("Card v1"));
	}
	else
	{
		AddError(TEXT("Caption widget missing after rollback"));
	}

	// manifest 不更新：字节级不变。
	FString ManifestAfter;
	TestTrue(TEXT("Manifest still exists"), FFileHelper::LoadFileToString(ManifestAfter, *SidecarPath));
	TestTrue(TEXT("Manifest untouched on save failure"), ManifestAfter == ManifestBefore);

	DeleteGeneratedAssetsAtPath(OutputPath);
	DeleteOutputDirectory(OutputPath);
	return true;
}

// ---------------------------------------------------------------------------
// 控制台命令冒烟：Design2UMG.Reimport 命令行入口（菜单 UI 之外的自动化路径）。
// 只验证入口本身——命令可达、参数正确映射进 FDesignReimportRequest、成功路径
// 走完同一 DesignImportPipeline::Reimport；reimport 语义由上面的测试覆盖。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportConsoleCommandTest,
	"Design2UMG.WidgetBlueprint.SafeReimportConsoleCommand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportConsoleCommandTest::RunTest(const FString&)
{
	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/SafeReimportConsoleCommand");

	const FString V1 = MakeReimportDoc(MakeCardNode(TEXT("re_card"), TEXT("PlainCard"), 24, 24, TEXT("Card v1")));
	UWidgetBlueprint* WBP = ImportFromJson(V1, nullptr, OutputPath);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		DeleteOutputDirectory(OutputPath);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// 命令入口只收磁盘上的 design.json 路径，v2 源先落盘（无 mapping 场景，默认设置即可匹配）。
	const FString V2JsonPath = FPaths::ProjectSavedDir() / TEXT("Design2UMG") / TEXT("Temp")
		/ TEXT("SafeReimportConsoleCommand_v2.json");
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(V2JsonPath), true);
	const FString V2 = MakeReimportDoc(MakeCardNode(TEXT("re_card"), TEXT("PlainCard"), 24, 24, TEXT("Card v2")));
	if (!TestTrue(TEXT("v2 design.json written"),
		FFileHelper::SaveStringToFile(V2, *V2JsonPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)))
	{
		IFileManager::Get().Delete(*V2JsonPath, false, true);
		DeleteGeneratedAssetsAtPath(OutputPath);
		DeleteOutputDirectory(OutputPath);
		return false;
	}

	IConsoleObject* ConsoleObject = IConsoleManager::Get().FindConsoleObject(TEXT("Design2UMG.Reimport"));
	if (TestNotNull(TEXT("Design2UMG.Reimport console command registered"), ConsoleObject))
	{
		const TArray<FString> Args = { TargetPackage, V2JsonPath };
		TestTrue(TEXT("console command executes"), ConsoleObject->AsCommand()->Execute(Args, nullptr, *GLog));

		// 成功路径打点：命令入口走完同一 pipeline，v2 设计文本已刷新进目标资产。
		if (UTextBlock* Caption = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Caption"))))
		{
			TestEqual(TEXT("v2 text refreshed through the console command"),
				Caption->GetText().ToString(), TEXT("Card v2"));
		}
		else
		{
			AddError(TEXT("Caption widget missing after console reimport"));
		}
	}

	IFileManager::Get().Delete(*V2JsonPath, false, true);
	DeleteGeneratedAssetsAtPath(OutputPath);
	DeleteOutputDirectory(OutputPath);
	return true;
}

#endif
