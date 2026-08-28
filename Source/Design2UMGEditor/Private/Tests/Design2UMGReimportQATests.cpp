// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

// Safe Reimport（切片 B：阶段 3 属性级 reimport + 阶段 4 结构变化）白盒结构测试。
// 独立于交付内开发测试设计：自带 Figma fixture 构造器与 QA 替身 mapped 类
// （UDesign2UMGMappingQAWidget），用资产/结构/输入/行为/identity/manifest 断言验收，
// 不用视觉分数。每条用例对应本文件里的一个 RunTest 函数（一对一）。

#include "Misc/AutomationTest.h"

#include "Design2UMGComponentMappingGeneration.h"
#include "Design2UMGConverter.h"
#include "Design2UMGGenerationManifest.h"
#include "Design2UMGPrototypeNavigationExtensions.h"
#include "Design2UMGWidgetBindingTools.h"
#include "DesignImportPipeline.h"
#include "FigmaParser.h"
#include "UMGGenerator.h"
#include "../UMGWidgetUtils.h"
#include "Tests/Design2UMGMappingQAWidget.h"
#include "Tests/Design2UMGTestAssetCleanup.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/Button.h"
#include "Components/Widget.h"
#include "Components/WidgetSwitcher.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "PackageTools.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintEditorUtils.h"

#include <initializer_list>

#if WITH_AUTOMATION_WORKER

namespace
{
	using Design2UMGEditorTests::DeleteGeneratedAssetsAtPath;

	// 本 QA 文件专属 fixture 身份：scope / 组件 Key / mapped 替身类与交付内开发测试互不共用。
	const TCHAR* QAReimportScope = TEXT("qa-reimport-doc-9f31");
	const TCHAR* QAReimportOtherScope = TEXT("qa-reimport-other-2c66");
	const TCHAR* QAReimportComponentKey = TEXT("qa-reimport-comp-key");
	const TCHAR* QAMappedClassPath = TEXT("/Script/Design2UMGEditor.Design2UMGMappingQAWidget");
	const TCHAR* QAOtherMappedClassPath = TEXT("/Script/Design2UMGEditor.Design2UMGMappedComponentTestWidget");
	const TCHAR* QAMappingKey = TEXT("figma|qa-reimport-doc-9f31|qa-reimport-comp-key");

	// ---------------------------------------------------------------------------
	// Figma fixture 构造器（本文件独立维护）
	// ---------------------------------------------------------------------------

	FString QATextStyle()
	{
		return TEXT("\"style\":{\"fontFamily\":\"Inter\",\"fontSize\":14,\"fontWeight\":400,"
			"\"textAlignHorizontal\":\"LEFT\",\"textAlignVertical\":\"TOP\",\"textDecoration\":\"NONE\","
			"\"letterSpacing\":0,\"lineHeightUnit\":\"AUTO\",\"lineHeightPx\":0}");
	}

	FString MakeQAText(const TCHAR* Id, const TCHAR* Name, int32 X, int32 Y, const TCHAR* Text)
	{
		return FString::Printf(
			TEXT("{\"id\":\"%s\",\"name\":\"%s\",\"type\":\"TEXT\","
			 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":%d,\"width\":90,\"height\":20},\"characters\":\"%s\",%s}"),
			Id, Name, X, Y, Text, *QATextStyle());
	}

	// 无背景填充 + 多子节点的纯容器 FRAME：保持多子面板形态，项目/结构操作可挂载。
	FString MakeQAPanel(const TCHAR* Id, const TCHAR* Name, int32 X, int32 Y, const FString& ChildrenJson)
	{
		return FString::Printf(
			TEXT("{\"id\":\"%s\",\"name\":\"%s\",\"type\":\"FRAME\","
			 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":%d,\"width\":200,\"height\":120},\"children\":[%s]}"),
			Id, Name, X, Y, *ChildrenJson);
	}

	FString MakeQAButton(const TCHAR* Id, const TCHAR* Name, int32 X, int32 Y, const TCHAR* Label)
	{
		return FString::Printf(
			TEXT("{\"id\":\"%s\",\"name\":\"%s\",\"type\":\"INSTANCE\","
			 "\"componentId\":\"77:88\",\"componentSetId\":\"99:00\",\"mainComponentKey\":\"%s\","
			 "\"componentProperties\":{\"label\":{\"type\":\"TEXT\",\"value\":\"%s\"}},"
			 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":%d,\"width\":120,\"height\":44},"
			 "\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":0.2,\"g\":0.5,\"b\":0.8,\"a\":1}}],"
			 "\"children\":[{\"id\":\"%s_label\",\"name\":\"InnerLabel\",\"type\":\"TEXT\","
			 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":%d,\"width\":80,\"height\":18},\"characters\":\"%s\",%s}]}"),
			Id, Name, QAReimportComponentKey, Label, X, Y, Id, X + 8, Y + 12, Label, *QATextStyle());
	}

	FString MakeQAInteractive(const TCHAR* Id, const TCHAR* Name, int32 X, const TCHAR* TriggerType, const TCHAR* Url)
	{
		return FString::Printf(
			TEXT("{\"id\":\"%s\",\"name\":\"%s\",\"type\":\"FRAME\","
			 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":40,\"width\":110,\"height\":30},"
			 "\"interactions\":[{\"trigger\":{\"type\":\"%s\"},\"actions\":[{\"type\":\"URL\",\"url\":\"%s\"}]}],"
			 "\"children\":[{\"id\":\"%s_text\",\"name\":\"%s_label\",\"type\":\"TEXT\","
			 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":46,\"width\":80,\"height\":18},\"characters\":\"Go\",%s}]}"),
			Id, Name, X, TriggerType, Url, Id, Name, X + 8, *QATextStyle());
	}

	FString MakeQADoc(
		const FString& ChildrenJson,
		const TCHAR* FileKey = QAReimportScope,
		int32 Width = 360,
		int32 Height = 240)
	{
		return FString::Printf(
			TEXT("{\"fileKey\":\"%s\","
			 "\"document\":{\"id\":\"0:0\",\"name\":\"QADoc\",\"type\":\"DOCUMENT\",\"children\":["
			 "{\"id\":\"0:1\",\"name\":\"Page\",\"type\":\"CANVAS\",\"children\":["
			 "{\"id\":\"qa_root\",\"name\":\"QAPage\",\"type\":\"FRAME\","
			 "\"absoluteBoundingBox\":{\"x\":0,\"y\":0,\"width\":%d,\"height\":%d},\"children\":[%s]}"
			 "]}]}}"),
			FileKey, Width, Height, *ChildrenJson);
	}

	FDesign2UMGImportSettingsSnapshot MakeQASnapshot(const TCHAR* MappedClassPath)
	{
		FDesign2UMGImportSettingsSnapshot Snapshot;
		Snapshot.ConversionSettings = FDesign2UMGConversionSettings::Defaults();
		if (MappedClassPath)
		{
			DesignComponentMapping::FComponentMappingEntry& Entry =
				Snapshot.GenerationSettings.ComponentMapping.Table.Entries.AddDefaulted_GetRef();
			Entry.Key = QAMappingKey;
			Entry.Provider = TEXT("figma");
			Entry.DocumentScope = QAReimportScope;
			Entry.ComponentKey = QAReimportComponentKey;
			Entry.WidgetClass = MappedClassPath;

			DesignComponentMapping::FComponentInputDeclaration& Input = Entry.Inputs.AddDefaulted_GetRef();
			Input.Source = TEXT("label");
			Input.Target = TEXT("TextValue");
			Input.Type = DesignComponentMapping::EComponentInputType::Text;
			Input.ValueKind = DesignComponentMapping::EComponentInputValueKind::ComponentProperty;
		}
		return Snapshot;
	}

	UWidgetBlueprint* QAImport(const FString& Json, const TCHAR* MappedClassPath, const FString& OutputDir, bool bCleanFirst = true)
	{
		if (bCleanFirst)
		{
			DeleteGeneratedAssetsAtPath(OutputDir);
		}

		FDesignDocument Document;
		if (!FFigmaParser::ParseDocument(Json, Document))
		{
			return nullptr;
		}
		FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
		const FDesign2UMGImportSettingsSnapshot Snapshot = MakeQASnapshot(MappedClassPath);
		return FUMGGenerator::GenerateWidgetBlueprint(Data, OutputDir, &Snapshot.GenerationSettings);
	}

	FDesignReimportResult QAReimport(
		const FString& Json,
		const TCHAR* MappedClassPath,
		const FString& TargetPackage,
		bool bGenerateResolutionCopy = false)
	{
		FDesignDocument Document;
		if (!FFigmaParser::ParseDocument(Json, Document))
		{
			return FDesignReimportResult();
		}

		FDesignReimportRequest Request;
		Request.TargetAssetPath = TargetPackage;
		Request.SettingsSnapshot = MakeQASnapshot(MappedClassPath);
		Request.bGenerateResolutionCopy = bGenerateResolutionCopy;
		return DesignImportPipeline::Reimport(Document, Request);
	}

	int32 QACountKind(const FDesignReimportResult& Result, const TCHAR* Kind)
	{
		int32 Count = 0;
		for (const FDesign2UMGMappingDiagnostic& Diagnostic : Result.Diagnostics)
		{
			Count += Diagnostic.Kind == Kind ? 1 : 0;
		}
		return Count;
	}

	bool QAHasMessage(const FDesignReimportResult& Result, const TCHAR* Kind, const TCHAR* Substring)
	{
		for (const FDesign2UMGMappingDiagnostic& Diagnostic : Result.Diagnostics)
		{
			if (Diagnostic.Kind == Kind && Diagnostic.Message.Contains(Substring))
			{
				return true;
			}
		}
		return false;
	}

	/** 同一条诊断消息必须同时包含全部子串（防止跨条拼凑的假阳性配对）。 */
	bool QAHasMessageWithAll(const FDesignReimportResult& Result, const TCHAR* Kind, std::initializer_list<const TCHAR*> Substrings)
	{
		for (const FDesign2UMGMappingDiagnostic& Diagnostic : Result.Diagnostics)
		{
			if (Diagnostic.Kind != Kind)
			{
				continue;
			}
			bool bAll = true;
			for (const TCHAR* Substring : Substrings)
			{
				bAll = bAll && Diagnostic.Message.Contains(Substring);
			}
			if (bAll)
			{
				return true;
			}
		}
		return false;
	}

	/** mapping_ 前缀诊断的单一归属是 componentMapping 章节：分类断言查 ComponentMappingDiagnostics。 */
	bool QAHasMappingMessageWithAll(const FDesignReimportResult& Result, const TCHAR* Kind, std::initializer_list<const TCHAR*> Substrings)
	{
		for (const FDesign2UMGMappingDiagnostic& Diagnostic : Result.ComponentMappingDiagnostics)
		{
			if (Diagnostic.Kind != Kind)
			{
				continue;
			}
			bool bAll = true;
			for (const TCHAR* Substring : Substrings)
			{
				bAll = bAll && Diagnostic.Message.Contains(Substring);
			}
			if (bAll)
			{
				return true;
			}
		}
		return false;
	}

	bool QALoadSidecar(const FString& AssetPackageName, FDesign2UMGGenerationManifestSidecar& OutSidecar)
	{
		FString Error;
		return Design2UMGGenerationManifest::TryLoadSidecarForAsset(AssetPackageName, OutSidecar, Error);
	}

	bool QAReadFileString(const FString& FilePath, FString& OutString)
	{
		return FFileHelper::LoadFileToString(OutString, *FilePath);
	}

	bool QAWriteFileString(const FString& FilePath, const FString& Contents)
	{
		const FString Dir = FPaths::GetPath(FilePath);
		if (!Dir.IsEmpty())
		{
			IFileManager::Get().MakeDirectory(*Dir, true);
		}
		return FFileHelper::SaveStringToFile(Contents, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	bool QAParseJsonObject(const FString& Json, TSharedPtr<FJsonObject>& OutObject)
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
	}

	void QAWipeOutput(const FString& OutputDir)
	{
		DeleteGeneratedAssetsAtPath(OutputDir);
		const FString DirOnDisk = FPackageName::LongPackageNameToFilename(OutputDir);
		if (!DirOnDisk.IsEmpty())
		{
			IFileManager::Get().DeleteDirectory(*DirOnDisk, false, true);
		}
	}

	FString QAAssetFilePath(const FString& PackageName)
	{
		return FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	}

	UDesign2UMGMappingQAWidget* QAFindMappedInstance(UWidgetBlueprint* WBP)
	{
		UDesign2UMGMappingQAWidget* Found = nullptr;
		WBP->WidgetTree->ForEachWidget([&Found](UWidget* Widget)
		{
			if (UDesign2UMGMappingQAWidget* Candidate = Cast<UDesign2UMGMappingQAWidget>(Widget))
			{
				Found = Candidate;
			}
		});
		return Found;
	}

	// 多页原型文档 fixture（切片 D）：页 = 带 fills 的顶层 FRAME（转换成 Border 页形态，
	// 页 widget 名 = 页 id）；页内放一条带跨页 ON_CLICK NODE 导航的文本（目标页必须
	// 真实存在，绑定才有效），文档因此构成多页原型，转换链合成 switcher 宿主根
	// （id = 页集合内容指纹，页集合一变即改写）。
	FString MakeProtoNavText(const TCHAR* Id, const TCHAR* Name, int32 X, int32 Y, const TCHAR* DestinationPageId)
	{
		return FString::Printf(
			TEXT("{\"id\":\"%s\",\"name\":\"%s\",\"type\":\"TEXT\","
				 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":%d,\"width\":90,\"height\":20},\"characters\":\"nav\",%s,"
				 "\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"%s\",\"navigation\":\"NAVIGATE\"}]}]}"),
			Id, Name, X, Y, *QATextStyle(), DestinationPageId);
	}

	FString MakeProtoPage(const TCHAR* Id, const TCHAR* Name, int32 X, const FString& ChildrenJson)
	{
		return FString::Printf(
			TEXT("{\"id\":\"%s\",\"name\":\"%s\",\"type\":\"FRAME\","
				 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":0,\"width\":360,\"height\":240},"
				 "\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":0.9,\"g\":0.9,\"b\":0.9,\"a\":1}}],"
				 "\"children\":[%s]}"),
			Id, Name, X, *ChildrenJson);
	}

	FString MakeProtoDoc(const FString& PagesJson)
	{
		return FString::Printf(
			TEXT("{\"fileKey\":\"%s\",\"document\":{\"id\":\"0:0\",\"name\":\"ProtoDoc\",\"type\":\"DOCUMENT\",\"children\":["
				 "{\"id\":\"0:1\",\"name\":\"Page\",\"type\":\"CANVAS\",\"children\":[%s]}]}}"),
			QAReimportScope, *PagesJson);
	}
}

// ---------------------------------------------------------------------------
// 用例 1（unit）：manifest schema 收口——inputs 只落声明（source/target/valueKind），
// 设计值不进 manifest；历史 sidecar 里的 inputs[].value 字段一律忽略。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAManifestInputSchemaTest,
	"Design2UMG.SafeReimportQA.ManifestInputSchemaDeclarationsOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAManifestInputSchemaTest::RunTest(const FString&)
{
	const FString Package = TEXT("/Game/Design2UMG/_TestOutput/QAReimportManifestSchema");
	const FString SidecarPath = Design2UMGGenerationManifest::BuildSidecarPathForAsset(Package);

	FDesign2UMGGenerationManifestPayload Payload;
	Payload.Provider = TEXT("figma");
	Payload.DocumentScope = TEXT("unit-scope-1");
	Payload.SourceRootNodeId = TEXT("qa-unit-root");
	Payload.SourceRootName = TEXT("QAPage");
	{
		FDesign2UMGMappedInstanceRecord Mapped;
		Mapped.DesignNodeId = TEXT("qa-m-1");
		Mapped.WidgetName = TEXT("W1");
		Mapped.MappedKey = TEXT("figma|unit-scope-1|k");
		Mapped.MappedClass = TEXT("/Script/Design2UMGEditor.Design2UMGMappingQAWidget");
		Mapped.bMatchedAtSetLevel = true;
		FDesign2UMGMappedInputRecord Input;
		Input.Source = TEXT("label");
		Input.Target = TEXT("TextValue");
		Input.ValueKind = TEXT("ComponentProperty");
		Mapped.Inputs.Add(MoveTemp(Input));
		Payload.MappedInstances.Add(MoveTemp(Mapped));
	}
	{
		FDesign2UMGGeneratedInstanceRecord Generated;
		Generated.DesignNodeId = TEXT("qa-g-1");
		Generated.WidgetName = TEXT("G1");
		Payload.TopLevelGenerated.Add(MoveTemp(Generated));
	}
	Payload.D2UOwnedNames = { TEXT("W1"), TEXT("G1") };

	if (!TestTrue(TEXT("WriteSidecarForAsset succeeds"), Design2UMGGenerationManifest::WriteSidecarForAsset(Package, Payload)))
	{
		IFileManager::Get().Delete(*SidecarPath, false, true);
		return false;
	}

	// 写侧：inputs 对象只有 source/target/valueKind 三个字段，value/bApplied 不落盘。
	FString RawJson;
	TestTrue(TEXT("sidecar file readable"), QAReadFileString(SidecarPath, RawJson));
	TSharedPtr<FJsonObject> Root;
	if (TestTrue(TEXT("sidecar JSON parses"), QAParseJsonObject(RawJson, Root)))
	{
		TestEqual(TEXT("schemaVersion is 1"), static_cast<int32>(Root->GetNumberField(TEXT("schemaVersion"))), 1);
		TestEqual(TEXT("provider written"), Root->GetStringField(TEXT("provider")), FString(TEXT("figma")));
		TestEqual(TEXT("documentScope written"), Root->GetStringField(TEXT("documentScope")), FString(TEXT("unit-scope-1")));
		const TSharedPtr<FJsonObject>* SourceRoot = nullptr;
		if (TestTrue(TEXT("sourceRoot present"), Root->TryGetObjectField(TEXT("sourceRoot"), SourceRoot) && SourceRoot != nullptr))
		{
			TestEqual(TEXT("sourceRoot.nodeId written"), (*SourceRoot)->GetStringField(TEXT("nodeId")), FString(TEXT("qa-unit-root")));
		}

		const TArray<TSharedPtr<FJsonValue>>* Instances = nullptr;
		if (TestTrue(TEXT("instances present"), Root->TryGetArrayField(TEXT("instances"), Instances) && Instances != nullptr)
			&& TestEqual(TEXT("two instances written"), Instances->Num(), 2))
		{
			const TSharedPtr<FJsonObject> MappedObject = (*Instances)[0]->AsObject();
			const TSharedPtr<FJsonObject> GeneratedObject = (*Instances)[1]->AsObject();
			if (TestTrue(TEXT("mapped instance object"), MappedObject.IsValid())
				&& TestTrue(TEXT("generated instance object"), GeneratedObject.IsValid()))
			{
				TestEqual(TEXT("mapped kind"), MappedObject->GetStringField(TEXT("kind")), FString(TEXT("mapped")));
				TestEqual(TEXT("set-level flag written"), MappedObject->GetBoolField(TEXT("matchedAtSetLevel")), true);
				TestEqual(TEXT("mappedKey written"), MappedObject->GetStringField(TEXT("mappedKey")), FString(TEXT("figma|unit-scope-1|k")));

				const TArray<TSharedPtr<FJsonValue>>* Inputs = nullptr;
				if (TestTrue(TEXT("inputs array present"), MappedObject->TryGetArrayField(TEXT("inputs"), Inputs) && Inputs != nullptr)
					&& TestEqual(TEXT("one input written"), Inputs->Num(), 1))
				{
					const TSharedPtr<FJsonObject> InputObject = (*Inputs)[0]->AsObject();
					if (TestTrue(TEXT("input object"), InputObject.IsValid()))
					{
						TestEqual(TEXT("input source"), InputObject->GetStringField(TEXT("source")), FString(TEXT("label")));
						TestEqual(TEXT("input target"), InputObject->GetStringField(TEXT("target")), FString(TEXT("TextValue")));
						TestEqual(TEXT("input valueKind"), InputObject->GetStringField(TEXT("valueKind")), FString(TEXT("ComponentProperty")));
						TestFalse(TEXT("inputs[].value NOT serialized"), InputObject->HasField(TEXT("value")));
						TestFalse(TEXT("inputs[].bApplied NOT serialized"), InputObject->HasField(TEXT("bApplied")));
					}
				}
				TestEqual(TEXT("generated kind"), GeneratedObject->GetStringField(TEXT("kind")), FString(TEXT("generated")));
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* Owned = nullptr;
		if (TestTrue(TEXT("d2uOwned present"), Root->TryGetArrayField(TEXT("d2uOwned"), Owned) && Owned != nullptr)
			&& TestEqual(TEXT("d2uOwned count"), Owned->Num(), 2))
		{
			TestEqual(TEXT("d2uOwned[0]"), (*Owned)[0]->AsString(), FString(TEXT("W1")));
			TestEqual(TEXT("d2uOwned[1]"), (*Owned)[1]->AsString(), FString(TEXT("G1")));
		}
	}

	// 读侧：声明三字段原样回读（记录结构里已无 value/bApplied 字段可还原）。
	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("TryLoadSidecarForAsset succeeds"), QALoadSidecar(Package, Sidecar)))
	{
		TestEqual(TEXT("loaded schemaVersion"), Sidecar.SchemaVersion, 1);
		TestEqual(TEXT("loaded provider"), Sidecar.Provider, FString(TEXT("figma")));
		TestEqual(TEXT("loaded documentScope"), Sidecar.DocumentScope, FString(TEXT("unit-scope-1")));
		TestEqual(TEXT("loaded sourceRoot id"), Sidecar.SourceRootNodeId, FString(TEXT("qa-unit-root")));
		TestEqual(TEXT("loaded sourceRoot name"), Sidecar.SourceRootName, FString(TEXT("QAPage")));
		TestEqual(TEXT("instance count"), Sidecar.Instances.Num(), 2);
		if (Sidecar.Instances.Num() == 2)
		{
			const FDesign2UMGGenerationManifestSidecar::FInstanceRecord& M = Sidecar.Instances[0];
			TestEqual(TEXT("loaded mapped kind"), M.Kind, FString(TEXT("mapped")));
			TestTrue(TEXT("loaded set-level flag"), M.bMatchedAtSetLevel);
			TestEqual(TEXT("loaded mappedClass"), M.MappedClass, FString(QAMappedClassPath));
			if (TestEqual(TEXT("loaded input count"), M.Inputs.Num(), 1) && M.Inputs.Num() == 1)
			{
				TestEqual(TEXT("loaded input source"), M.Inputs[0].Source, FString(TEXT("label")));
				TestEqual(TEXT("loaded input target"), M.Inputs[0].Target, FString(TEXT("TextValue")));
				TestEqual(TEXT("loaded input valueKind"), M.Inputs[0].ValueKind, FString(TEXT("ComponentProperty")));
			}
			TestEqual(TEXT("loaded generated kind"), Sidecar.Instances[1].Kind, FString(TEXT("generated")));
			TestEqual(TEXT("loaded generated id"), Sidecar.Instances[1].DesignNodeId, FString(TEXT("qa-g-1")));
		}
		TestTrue(TEXT("d2uOwned loaded"), Sidecar.D2UOwned.Contains(TEXT("W1")) && Sidecar.D2UOwned.Contains(TEXT("G1")));
	}

	// 历史 sidecar（带 value 字段）按 schema 收口规则忽略 value，加载成功无报错。
	// 注入用与序列化空白无关的针（值本身），在 valueKind 值后面补一个历史 value 兄弟字段。
	FString LegacyJson = RawJson;
	const TCHAR* DeclarationNeedle = TEXT("\"ComponentProperty\"");
	LegacyJson.ReplaceInline(
		DeclarationNeedle,
		TEXT("\"ComponentProperty\",\"value\":\"legacy-77f\""),
		ESearchCase::CaseSensitive);
	TestTrue(TEXT("legacy value injected"), LegacyJson != RawJson);
	TestTrue(TEXT("legacy sidecar written"), QAWriteFileString(SidecarPath, LegacyJson));

	FDesign2UMGGenerationManifestSidecar LegacySidecar;
	if (TestTrue(TEXT("legacy sidecar still loads"), QALoadSidecar(Package, LegacySidecar))
		&& TestEqual(TEXT("legacy input count"), LegacySidecar.Instances.Num() > 0 ? LegacySidecar.Instances[0].Inputs.Num() : -1, 1)
		&& LegacySidecar.Instances.Num() > 0 && LegacySidecar.Instances[0].Inputs.Num() == 1)
	{
		TestEqual(TEXT("legacy inputs[].value ignored; declarations intact"),
			LegacySidecar.Instances[0].Inputs[0].Source, FString(TEXT("label")));
		TestEqual(TEXT("legacy inputs[] declaration target intact"),
			LegacySidecar.Instances[0].Inputs[0].Target, FString(TEXT("TextValue")));
		TestEqual(TEXT("legacy inputs[] declaration valueKind intact"),
			LegacySidecar.Instances[0].Inputs[0].ValueKind, FString(TEXT("ComponentProperty")));
	}

	IFileManager::Get().Delete(*SidecarPath, false, true);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 2（unit）：sidecar 读取失败的错误分类——文件缺失 / JSON 非法 / schema 版本
// 不识别 / 缺 provider|documentScope / instances 为空，均返回 false + 精确原因。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAManifestLoadRejectsBadSidecarsTest,
	"Design2UMG.SafeReimportQA.ManifestLoadRejectsBadSidecars",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAManifestLoadRejectsBadSidecarsTest::RunTest(const FString&)
{
	const FString Package = TEXT("/Game/Design2UMG/_TestOutput/QAReimportManifestErrors");
	const FString SidecarPath = Design2UMGGenerationManifest::BuildSidecarPathForAsset(Package);

	// 1) 文件缺失。
	FDesign2UMGGenerationManifestSidecar Sidecar;
	FString Error;
	TestFalse(TEXT("missing sidecar rejected"), Design2UMGGenerationManifest::TryLoadSidecarForAsset(Package, Sidecar, Error));
	TestTrue(TEXT("missing-file reason"), Error.Contains(TEXT("generation manifest sidecar not found")));

	// 2) JSON 非法。
	TestTrue(TEXT("garbage written"), QAWriteFileString(SidecarPath, TEXT("{ this is not json")));
	TestFalse(TEXT("invalid JSON rejected"), Design2UMGGenerationManifest::TryLoadSidecarForAsset(Package, Sidecar, Error));
	TestTrue(TEXT("invalid-JSON reason"), Error.Contains(TEXT("generation manifest sidecar is not valid JSON")));

	// 3) schema 版本不识别。
	TestTrue(TEXT("v2 schema written"), QAWriteFileString(
		SidecarPath,
		TEXT("{\"schemaVersion\":2,\"provider\":\"figma\",\"documentScope\":\"s\",\"sourceRoot\":{},\"instances\":[{}],\"d2uOwned\":[]}")));
	TestFalse(TEXT("unsupported schema rejected"), Design2UMGGenerationManifest::TryLoadSidecarForAsset(Package, Sidecar, Error));
	TestTrue(TEXT("schema-version reason"), Error.Contains(TEXT("schema version 2 is not supported (expected 1)")));

	// 4) provider / documentScope 缺失。
	TestTrue(TEXT("empty provider written"), QAWriteFileString(
		SidecarPath,
		TEXT("{\"schemaVersion\":1,\"provider\":\"\",\"documentScope\":\"\",\"sourceRoot\":{},\"instances\":[{}],\"d2uOwned\":[]}")));
	TestFalse(TEXT("missing provider/documentScope rejected"), Design2UMGGenerationManifest::TryLoadSidecarForAsset(Package, Sidecar, Error));
	TestTrue(TEXT("missing-provider reason"), Error.Contains(TEXT("missing provider/documentScope")));

	// 5) instances 为空。
	TestTrue(TEXT("empty instances written"), QAWriteFileString(
		SidecarPath,
		TEXT("{\"schemaVersion\":1,\"provider\":\"figma\",\"documentScope\":\"s\",\"sourceRoot\":{},\"instances\":[],\"d2uOwned\":[]}")));
	TestFalse(TEXT("empty instances rejected"), Design2UMGGenerationManifest::TryLoadSidecarForAsset(Package, Sidecar, Error));
	TestTrue(TEXT("empty-instances reason"), Error.Contains(TEXT("records no instances")));

	IFileManager::Get().Delete(*SidecarPath, false, true);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 3（integration）：sidecar 损坏（非法 JSON）= 关系不清 → 全量冲突，
// 资产字节保持原状，未显式选择时不生成并列副本。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAUnreadableManifestConflictTest,
	"Design2UMG.SafeReimportQA.FullConflictOnUnreadableManifest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAUnreadableManifestConflictTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportBadManifest");

	const FString V1 = MakeQADoc(MakeQAText(TEXT("qa_t1"), TEXT("AlphaText"), 24, 24, TEXT("One")));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	const FString AssetFile = QAAssetFilePath(TargetPackage);
	const int64 BytesBefore = IFileManager::Get().FileSize(*AssetFile);

	// 损坏 sidecar：合法 JSON 都不是。
	TestTrue(TEXT("corrupt sidecar written"), QAWriteFileString(
		Design2UMGGenerationManifest::BuildSidecarPathForAsset(TargetPackage), TEXT("<<<corrupt-not-json>>>")));

	const FDesignReimportResult Result = QAReimport(V1, nullptr, TargetPackage);
	TestFalse(TEXT("reimport does not succeed"), Result.bSucceeded);
	TestTrue(TEXT("full conflict flagged"), Result.bFullConflict);
	TestEqual(TEXT("exactly one full-conflict diagnostic"), QACountKind(Result, TEXT("reimport_full_conflict")), 1);
	TestTrue(TEXT("conflict reason cites invalid JSON"),
		QAHasMessage(Result, TEXT("reimport_full_conflict"), TEXT("is not valid JSON")));
	TestTrue(TEXT("target asset bytes unchanged"), IFileManager::Get().FileSize(*AssetFile) == BytesBefore);
	TestTrue(TEXT("no implicit resolution copy"), Result.ResolutionCopyPath.IsEmpty());
	TestFalse(TEXT("no _Resolution asset file"),
		FPaths::FileExists(QAAssetFilePath(FPaths::GetPath(TargetPackage) / TEXT("WBP_QAPage_Resolution"))));

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 4（integration）：DocumentScope 不一致（新源 fileKey 不同）= 关系不清 →
// 全量冲突，诊断写明两侧 provider/scope，资产不做原地更新。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAScopeMismatchConflictTest,
	"Design2UMG.SafeReimportQA.FullConflictOnDocumentScopeMismatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAScopeMismatchConflictTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportScopeMismatch");

	const FString V1 = MakeQADoc(MakeQAText(TEXT("qa_t1"), TEXT("AlphaText"), 24, 24, TEXT("One")));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	const FString AssetFile = QAAssetFilePath(TargetPackage);
	const int64 BytesBefore = IFileManager::Get().FileSize(*AssetFile);

	const FString V2 = MakeQADoc(
		MakeQAText(TEXT("qa_t1"), TEXT("AlphaText"), 24, 24, TEXT("Two")),
		QAReimportOtherScope);
	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);

	TestFalse(TEXT("scope mismatch does not update in place"), Result.bSucceeded);
	TestTrue(TEXT("full conflict flagged"), Result.bFullConflict);
	TestEqual(TEXT("exactly one full-conflict diagnostic"), QACountKind(Result, TEXT("reimport_full_conflict")), 1);

	const FString ExpectedReason = FString::Printf(
		TEXT("document scope mismatch: manifest provider='figma' scope='%s', new source provider='figma' scope='%s'"),
		QAReimportScope, QAReimportOtherScope);
	bool bExactMessage = false;
	for (const FDesign2UMGMappingDiagnostic& Diagnostic : Result.Diagnostics)
	{
		if (Diagnostic.Kind == TEXT("reimport_full_conflict") && Diagnostic.Message.Contains(ExpectedReason))
		{
			bExactMessage = true;
		}
	}
	TestTrue(TEXT("conflict reason reports both scopes verbatim"), bExactMessage);

	// 资产保持原状：文件字节不变，内存文本仍是 v1 值（管线在匹配阶段即返回）。
	TestTrue(TEXT("asset bytes unchanged"), IFileManager::Get().FileSize(*AssetFile) == BytesBefore);
	if (UTextBlock* Alpha = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("AlphaText"))))
	{
		TestEqual(TEXT("in-memory text untouched"), Alpha->GetText().ToString(), FString(TEXT("One")));
	}
	else
	{
		AddError(TEXT("AlphaText widget missing"));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 5（integration）：项目侧重构（manifest 记录的实例在目标树里一个都定位不到）
// → 全量冲突；匹配阶段先行完成的诊断可见；项目侧结构不被改动。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQARestructureConflictTest,
	"Design2UMG.SafeReimportQA.FullConflictOnProjectRestructure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQARestructureConflictTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportRestructure");

	const FString V1 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAPanel(TEXT("qa_panel"), TEXT("InfoPanel"), 24, 24,
			FString::Printf(
				TEXT("%s,%s"),
				*MakeQAText(TEXT("qa_head"), TEXT("Head"), 32, 32, TEXT("Head v1")),
				*MakeQAText(TEXT("qa_tail"), TEXT("Tail"), 32, 60, TEXT("Tail v1")))),
		*MakeQAText(TEXT("qa_t2"), TEXT("AlphaText"), 250, 24, TEXT("One"))));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	const FString AssetFile = QAAssetFilePath(TargetPackage);
	const int64 BytesBefore = IFileManager::Get().FileSize(*AssetFile);

	// 模拟项目重构：把 manifest 记录的实例全部从树里移走，换成项目自己的控件。
	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (!TestTrue(TEXT("sidecar loads"), QALoadSidecar(TargetPackage, Sidecar)))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	for (const FDesign2UMGGenerationManifestSidecar::FInstanceRecord& Record : Sidecar.Instances)
	{
		if (UWidget* Recorded = WBP->WidgetTree->FindWidget(FName(*Record.WidgetName)))
		{
			WBP->WidgetTree->RemoveWidget(Recorded);
		}
	}
	UTextBlock* ProjectReplacement = WBP->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ProjectReplacement"));
	if (UPanelWidget* RootPanel = Cast<UPanelWidget>(WBP->WidgetTree->RootWidget))
	{
		RootPanel->AddChild(ProjectReplacement);
	}

	const FDesignReimportResult Result = QAReimport(V1, nullptr, TargetPackage);
	TestFalse(TEXT("restructure does not update in place"), Result.bSucceeded);
	TestTrue(TEXT("full conflict flagged"), Result.bFullConflict);
	TestTrue(TEXT("restructure reason reported"),
		QAHasMessage(Result, TEXT("reimport_full_conflict"), TEXT("none of the recorded instances could be located in the target widget tree")));
	// spec：先完成 source matching / instance matching / ownership 分类，才谈应用；
	// scope 匹配诊断必须先出现，随后才是全量冲突。
	TestTrue(TEXT("scope matching completed first"), QACountKind(Result, TEXT("reimport_scope_matched")) == 1);

	// 项目侧结构不被改动。
	TestNotNull(TEXT("project replacement widget preserved"), WBP->WidgetTree->FindWidget(TEXT("ProjectReplacement")));
	TestTrue(TEXT("asset bytes unchanged"), IFileManager::Get().FileSize(*AssetFile) == BytesBefore);

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 6（integration）：matched 更新主流程——D2U-owned 文本/布局刷新、mapped 实例
// 只更外层布局 + 显式 inputs 直接覆写（实例不被替换、内部不展开）、项目子控件保留、
// 根尺寸刷新、manifest 收口；无全量冲突时 copy 标志被忽略。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAMatchedUpdateTest,
	"Design2UMG.SafeReimportQA.MatchedRefreshesDesignOwned",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAMatchedUpdateTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportMatched");

	const FString InfoPanelV1 = MakeQAPanel(TEXT("qa_panel"), TEXT("InfoPanel"), 180, 24,
		FString::Printf(
			TEXT("%s,%s"),
			*MakeQAText(TEXT("qa_head"), TEXT("Head"), 188, 32, TEXT("Head v1")),
			*MakeQAText(TEXT("qa_tail"), TEXT("Tail"), 188, 60, TEXT("Tail v1"))));
	const FString V1 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAButton(TEXT("qa_btn_1"), TEXT("HeroButton"), 24, 24, TEXT("Checkout")),
		*InfoPanelV1),
		QAReimportScope, 360, 240);

	UWidgetBlueprint* WBP = QAImport(V1, QAMappedClassPath, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	UDesign2UMGMappingQAWidget* MappedBefore = QAFindMappedInstance(WBP);
	if (!TestNotNull(TEXT("mapped instance generated"), MappedBefore))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	TWeakObjectPtr<UDesign2UMGMappingQAWidget> MappedWeak = MappedBefore;

	// 模拟开发者定制：改 mapped 输入值、改 D2U 文本、加项目子控件。
	MappedBefore->TextValue = FText::FromString(TEXT("Hacked"));
	if (UTextBlock* Head = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Head"))))
	{
		Head->SetText(FText::FromString(TEXT("DevEdit")));
	}
	UTextBlock* ProjectNote = WBP->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ProjectNote"));
	if (UPanelWidget* RootPanel = Cast<UPanelWidget>(WBP->WidgetTree->RootWidget))
	{
		RootPanel->AddChild(ProjectNote);
	}

	const FString InfoPanelV2 = MakeQAPanel(TEXT("qa_panel"), TEXT("InfoPanel"), 180, 24,
		FString::Printf(
			TEXT("%s,%s"),
			*MakeQAText(TEXT("qa_head"), TEXT("Head"), 188, 32, TEXT("Head v2")),
			*MakeQAText(TEXT("qa_tail"), TEXT("Tail"), 188, 60, TEXT("Tail v2"))));
	const FString V2 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAButton(TEXT("qa_btn_1"), TEXT("HeroButton"), 60, 96, TEXT("Pay Now")),
		*InfoPanelV2),
		QAReimportScope, 480, 320);

	// copy 标志在无全量冲突时必须被忽略（不生成副本）。
	const FDesignReimportResult Result = QAReimport(V2, QAMappedClassPath, TargetPackage, /*bGenerateResolutionCopy=*/true);
	TestTrue(TEXT("v2 reimport succeeds"), Result.bSucceeded);
	TestFalse(TEXT("not a full conflict"), Result.bFullConflict);
	TestTrue(TEXT("updated blueprint returned"), Result.UpdatedBlueprint == WBP);

	// D2U-owned 文本被设计值刷新（开发者改动被覆写——文本属 D2U-owned）。
	if (UTextBlock* Head = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Head"))))
	{
		TestEqual(TEXT("D2U text refreshed from design"), Head->GetText().ToString(), FString(TEXT("Head v2")));
	}
	else
	{
		AddError(TEXT("Head widget missing after reimport"));
	}

	// mapped 实例：同一个对象（不替换）、类不变、inputs 直接覆写、外层布局更新、内部子树不展开。
	UDesign2UMGMappingQAWidget* MappedAfter = MappedWeak.Get();
	TestNotNull(TEXT("mapped instance still alive"), MappedAfter);
	if (MappedAfter)
	{
		TestTrue(TEXT("mapped instance object identity preserved"), MappedAfter == MappedBefore);
		TestEqual(TEXT("mapped instance class unchanged"), MappedAfter->GetClass()->GetPathName(), FString(QAMappedClassPath));
		TestEqual(TEXT("declared input overwritten with latest design value"), MappedAfter->TextValue.ToString(), FString(TEXT("Pay Now")));
		// 组件内部子树不展开：设计里的内部 label 永远不会以生成控件形态出现在宿主树里。
		TestNull(TEXT("mapped component internals not expanded into the host tree"),
			WBP->WidgetTree->FindWidget(TEXT("InnerLabel")));
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(MappedAfter->Slot))
		{
			TestTrue(TEXT("mapped outer layout updated"),
				FMath::Abs(Slot->GetPosition().X - 60.0f) < 4.0f && FMath::Abs(Slot->GetPosition().Y - 96.0f) < 4.0f);
		}
		else
		{
			AddError(TEXT("mapped instance slot is not a canvas slot"));
		}
	}

	// 项目子控件保留。
	TestNotNull(TEXT("project-added widget preserved"), WBP->WidgetTree->FindWidget(TEXT("ProjectNote")));

	// 根尺寸变化写入预览尺寸。
	if (UUserWidget* CDO = Cast<UUserWidget>(WBP->GeneratedClass ? WBP->GeneratedClass->GetDefaultObject() : nullptr))
	{
		TestEqual(TEXT("root design size refreshed (X)"), static_cast<int32>(CDO->DesignTimeSize.X), 480);
		TestEqual(TEXT("root design size refreshed (Y)"), static_cast<int32>(CDO->DesignTimeSize.Y), 320);
		TestEqual(TEXT("design size mode custom"), static_cast<int32>(CDO->DesignSizeMode), static_cast<int32>(EDesignPreviewSizeMode::Custom));
	}
	else
	{
		AddError(TEXT("CDO missing after reimport"));
	}

	// 诊断：matched 更新 + inputs 覆写（含具体设计值）。
	TestTrue(TEXT("update diagnostics present"), QACountKind(Result, TEXT("reimport_updated")) >= 2);
	TestTrue(TEXT("input overwrite diagnostic carries the design value"),
		QAHasMessage(Result, TEXT("mapping_input_applied"), TEXT("Pay Now")));

	// copy 标志被忽略：无副本。
	TestTrue(TEXT("no resolution copy without full conflict"), Result.ResolutionCopyPath.IsEmpty());
	TestFalse(TEXT("no _Resolution asset file"),
		FPaths::FileExists(QAAssetFilePath(FPaths::GetPath(TargetPackage) / TEXT("WBP_QAPage_Resolution"))));

	// manifest：实例仍在；项目 widget 不进 d2uOwned。
	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads after reimport"), QALoadSidecar(TargetPackage, Sidecar)))
	{
		TestNotNull(TEXT("mapped instance still recorded"), Sidecar.FindInstanceByDesignNodeId(TEXT("qa_btn_1")));
		TestNotNull(TEXT("generated instance still recorded"), Sidecar.FindInstanceByDesignNodeId(TEXT("qa_panel")));
		TestTrue(TEXT("nested D2U widget owned"), Sidecar.D2UOwned.Contains(TEXT("Head")));
		TestFalse(TEXT("project widget not owned"), Sidecar.D2UOwned.Contains(TEXT("ProjectNote")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 7（integration）：同名兄弟节点的确定性对照——第 k 个同名设计子节点对到
// 后缀序第 k 个既有 widget，刷新不串位。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQASiblingNameCorrespondenceTest,
	"Design2UMG.SafeReimportQA.SiblingNameCorrespondence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQASiblingNameCorrespondenceTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportSiblingNames");

	auto BuildDoc = [](const TCHAR* SecondText)
	{
		return MakeQADoc(MakeQAPanel(TEXT("qa_stats"), TEXT("Stats"), 24, 24,
			FString::Printf(
				TEXT("%s,%s"),
				*MakeQAText(TEXT("qa_title_a"), TEXT("Title"), 32, 32, TEXT("First")),
				*MakeQAText(TEXT("qa_title_b"), TEXT("Title"), 32, 60, SecondText))));
	};

	UWidgetBlueprint* WBP = QAImport(BuildDoc(TEXT("Second")), nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	UTextBlock* TitleA = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Title")));
	UTextBlock* TitleB = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Title_1")));
	if (!TestNotNull(TEXT("first sibling named Title"), TitleA) || !TestNotNull(TEXT("second sibling named Title_1"), TitleB))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	TestEqual(TEXT("v1 first text"), TitleA->GetText().ToString(), FString(TEXT("First")));
	TestEqual(TEXT("v1 second text"), TitleB->GetText().ToString(), FString(TEXT("Second")));

	const FDesignReimportResult Result = QAReimport(BuildDoc(TEXT("Second v2")), nullptr, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);

	TitleA = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Title")));
	TitleB = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Title_1")));
	if (TestNotNull(TEXT("Title still present"), TitleA) && TestNotNull(TEXT("Title_1 still present"), TitleB))
	{
		TestEqual(TEXT("first same-name sibling keeps its own design node value"), TitleA->GetText().ToString(), FString(TEXT("First")));
		TestEqual(TEXT("second same-name sibling refreshed to its own design node value"), TitleB->GetText().ToString(), FString(TEXT("Second v2")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 8（integration）：added——新顶层纯生成节点与新 mapped 实例加入树并记录进
// 新 manifest（mapped 记录含 key/class/inputs 声明，inputs 值已按设计写入实例）。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAAddedNodesTest,
	"Design2UMG.SafeReimportQA.AddedNodesAndMappedInstances",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAAddedNodesTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportAdded");

	const FString V1 = MakeQADoc(MakeQAText(TEXT("qa_t1"), TEXT("AlphaText"), 24, 24, TEXT("One")));
	UWidgetBlueprint* WBP = QAImport(V1, QAMappedClassPath, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	TestNull(TEXT("v1 has no ExtraPanel yet"), WBP->WidgetTree->FindWidget(TEXT("ExtraPanel")));
	TestNull(TEXT("v1 has no Sidekick yet"), WBP->WidgetTree->FindWidget(TEXT("Sidekick")));

	const FString V2 = MakeQADoc(FString::Printf(
		TEXT("%s,%s,%s"),
		*MakeQAText(TEXT("qa_t1"), TEXT("AlphaText"), 24, 24, TEXT("One")),
		*MakeQAPanel(TEXT("qa_extra"), TEXT("ExtraPanel"), 24, 90,
			FString::Printf(
				TEXT("%s,%s"),
				*MakeQAText(TEXT("qa_beta"), TEXT("Beta"), 32, 98, TEXT("Beta")),
				*MakeQAText(TEXT("qa_beta2"), TEXT("BetaTwo"), 32, 126, TEXT("Beta 2")))),
		*MakeQAButton(TEXT("qa_btn_2"), TEXT("Sidekick"), 250, 90, TEXT("Jump In"))));

	const FDesignReimportResult Result = QAReimport(V2, QAMappedClassPath, TargetPackage);
	TestTrue(TEXT("v2 reimport succeeds"), Result.bSucceeded);
	TestEqual(TEXT("two top-level adds diagnosed"), QACountKind(Result, TEXT("reimport_added")), 2);
	TestTrue(TEXT("added panel diagnosed"), QAHasMessage(Result, TEXT("reimport_added"), TEXT("ExtraPanel")));
	TestTrue(TEXT("added mapped instance diagnosed"), QAHasMessage(Result, TEXT("reimport_added"), TEXT("Sidekick")));

	// 新纯生成节点 + 嵌套子节点加入。
	UWidget* ExtraPanel = WBP->WidgetTree->FindWidget(TEXT("ExtraPanel"));
	TestNotNull(TEXT("new generated node added"), ExtraPanel);
	if (UTextBlock* Beta = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Beta"))))
	{
		TestEqual(TEXT("nested new child text applied"), Beta->GetText().ToString(), FString(TEXT("Beta")));
		if (ExtraPanel)
		{
			TestEqual(TEXT("nested new child parented under the new panel"), Beta->GetParent()->GetFName(), ExtraPanel->GetFName());
		}
	}
	else
	{
		AddError(TEXT("nested new child missing"));
	}

	// 新 mapped 实例加入且 inputs 已按设计写入。
	UDesign2UMGMappingQAWidget* Sidekick = Cast<UDesign2UMGMappingQAWidget>(WBP->WidgetTree->FindWidget(TEXT("Sidekick")));
	TestNotNull(TEXT("new mapped instance added"), Sidekick);
	if (Sidekick)
	{
		TestEqual(TEXT("new mapped instance input written"), Sidekick->TextValue.ToString(), FString(TEXT("Jump In")));
	}

	// manifest 记录新关系。
	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads"), QALoadSidecar(TargetPackage, Sidecar)))
	{
		const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* Extra =
			Sidecar.FindInstanceByDesignNodeId(TEXT("qa_extra"));
		if (TestNotNull(TEXT("new generated node recorded"), Extra))
		{
			TestEqual(TEXT("generated kind recorded"), Extra->Kind, FString(TEXT("generated")));
			TestEqual(TEXT("generated widget name recorded"), Extra->WidgetName, FString(TEXT("ExtraPanel")));
		}
		const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* Mapped =
			Sidecar.FindInstanceByDesignNodeId(TEXT("qa_btn_2"));
		if (TestNotNull(TEXT("new mapped instance recorded"), Mapped))
		{
			TestEqual(TEXT("mapped kind recorded"), Mapped->Kind, FString(TEXT("mapped")));
			TestEqual(TEXT("mapped key recorded"), Mapped->MappedKey, FString(QAMappingKey));
			TestEqual(TEXT("mapped class recorded"), Mapped->MappedClass, FString(QAMappedClassPath));
			if (TestEqual(TEXT("mapped inputs declaration recorded"), Mapped->Inputs.Num(), 1) && Mapped->Inputs.Num() == 1)
			{
				TestEqual(TEXT("input source recorded"), Mapped->Inputs[0].Source, FString(TEXT("label")));
				TestEqual(TEXT("input target recorded"), Mapped->Inputs[0].Target, FString(TEXT("TextValue")));
				TestEqual(TEXT("input valueKind recorded"), Mapped->Inputs[0].ValueKind, FString(TEXT("ComponentProperty")));
			}
		}
		TestTrue(TEXT("new panel ownership recorded"), Sidecar.D2UOwned.Contains(TEXT("ExtraPanel")));
		TestTrue(TEXT("new nested child ownership recorded"), Sidecar.D2UOwned.Contains(TEXT("Beta")));
		TestTrue(TEXT("new mapped instance ownership recorded"), Sidecar.D2UOwned.Contains(TEXT("Sidekick")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 9（integration）：safe remove 正向——纯 D2U-owned 节点安全删除；
// 设计删除但资产里已被项目删掉的节点只从记录移除（不报错、不误删）。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQARemovePureNodeTest,
	"Design2UMG.SafeReimportQA.RemoveDeletesPureD2UNodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQARemovePureNodeTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportRemovePure");

	const FString V1 = MakeQADoc(FString::Printf(
		TEXT("%s,%s,%s"),
		*MakeQAText(TEXT("qa_clean"), TEXT("CleanStat"), 24, 24, TEXT("Old")),
		*MakeQAText(TEXT("qa_ghost"), TEXT("GhostStat"), 140, 24, TEXT("Gone")),
		*MakeQAText(TEXT("qa_keep"), TEXT("Keeper"), 250, 24, TEXT("Keep"))));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// 项目侧已在编辑器里删掉 GhostStat 的 widget（manifest 仍记录它）。
	// 走编辑器同款 DeleteWidgets（销毁对象 + 清理变量 GUID）；裸 RemoveWidget 只是从
	// 树上摘下、对象仍存活，后续任何编译都会因游离 widget/GUID 触发编译器 ensure。
	if (UWidget* Ghost = WBP->WidgetTree->FindWidget(TEXT("GhostStat")))
	{
		FWidgetBlueprintEditorUtils::DeleteWidgets(
			WBP, { Ghost }, FWidgetBlueprintEditorUtils::EDeleteWidgetWarningType::DeleteSilently);
	}

	const FString V2 = MakeQADoc(MakeQAText(TEXT("qa_keep"), TEXT("Keeper"), 250, 24, TEXT("Keep")));
	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);

	TestNull(TEXT("purely owned node deleted from tree"), WBP->WidgetTree->FindWidget(TEXT("CleanStat")));
	TestNull(TEXT("already-absent node stays absent"), WBP->WidgetTree->FindWidget(TEXT("GhostStat")));
	TestNotNull(TEXT("kept node still present"), WBP->WidgetTree->FindWidget(TEXT("Keeper")));

	TestEqual(TEXT("two removal diagnostics"), QACountKind(Result, TEXT("reimport_removed")), 2);
	TestTrue(TEXT("safe-delete flow diagnosed for present widget"),
		QAHasMessageWithAll(Result, TEXT("reimport_removed"), { TEXT("CleanStat"), TEXT("removed through the safe delete flow") }));
	TestTrue(TEXT("already-absent branch diagnosed"),
		QAHasMessageWithAll(Result, TEXT("reimport_removed"), { TEXT("GhostStat"), TEXT("widget already absent from the tree") }));
	TestEqual(TEXT("no remove conflicts"), QACountKind(Result, TEXT("reimport_remove_conflict")), 0);

	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads"), QALoadSidecar(TargetPackage, Sidecar)))
	{
		TestNull(TEXT("deleted node dropped from instances"), Sidecar.FindInstanceByDesignNodeId(TEXT("qa_clean")));
		TestNull(TEXT("absent node dropped from instances"), Sidecar.FindInstanceByDesignNodeId(TEXT("qa_ghost")));
		TestNotNull(TEXT("kept node still recorded"), Sidecar.FindInstanceByDesignNodeId(TEXT("qa_keep")));
		TestFalse(TEXT("deleted node dropped from d2uOwned"), Sidecar.D2UOwned.Contains(TEXT("CleanStat")));
		TestFalse(TEXT("absent node dropped from d2uOwned"), Sidecar.D2UOwned.Contains(TEXT("GhostStat")));
		TestTrue(TEXT("kept node still owned"), Sidecar.D2UOwned.Contains(TEXT("Keeper")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 10（integration）：remove 冲突——被删节点子树含项目内容（项目后加的子控件 /
// 开发者绑定）时不删、保留 + 冲突诊断写明原因，且新 manifest 保留该实例。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQARemoveProjectContentTest,
	"Design2UMG.SafeReimportQA.RemovePreservesProjectContent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQARemoveProjectContentTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportRemoveConflict");

	const FString V1 = MakeQADoc(FString::Printf(
		TEXT("%s,%s,%s"),
		*MakeQAPanel(TEXT("qa_doom"), TEXT("DoomedPanel"), 24, 24,
			FString::Printf(
				TEXT("%s,%s"),
				*MakeQAText(TEXT("qa_dhead"), TEXT("DoomHead"), 32, 32, TEXT("Doom")),
				*MakeQAText(TEXT("qa_dtail"), TEXT("DoomTail"), 32, 60, TEXT("Doom")))),
		*MakeQAPanel(TEXT("qa_bound"), TEXT("BoundPanel"), 24, 160,
			FString::Printf(
				TEXT("%s,%s"),
				*MakeQAText(TEXT("qa_bhead"), TEXT("BoundHead"), 32, 168, TEXT("Bound")),
				*MakeQAText(TEXT("qa_btail"), TEXT("BoundTail"), 32, 196, TEXT("Bound")))),
		*MakeQAText(TEXT("qa_keep"), TEXT("Keeper"), 250, 24, TEXT("Keep"))));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// 项目内容信号 A：DoomedPanel 子树里挂项目后加的子控件。
	if (UPanelWidget* Doomed = Cast<UPanelWidget>(WBP->WidgetTree->FindWidget(TEXT("DoomedPanel"))))
	{
		UTextBlock* ProjectChild = WBP->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ProjectChild"));
		Doomed->AddChild(ProjectChild);
	}
	// 项目内容信号 B：WBP Bindings 引用 BoundPanel 子树内 widget。
	FDelegateEditorBinding ProjectBinding;
	ProjectBinding.ObjectName = TEXT("BoundPanel");
	ProjectBinding.PropertyName = TEXT("OnClicked");
	ProjectBinding.FunctionName = TEXT("HandleBound");
	WBP->Bindings.Add(ProjectBinding);

	const FString V2 = MakeQADoc(MakeQAText(TEXT("qa_keep"), TEXT("Keeper"), 250, 24, TEXT("Keep")));
	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport itself succeeds (conflicts are per-node)"), Result.bSucceeded);

	TestNotNull(TEXT("panel with project child preserved"), WBP->WidgetTree->FindWidget(TEXT("DoomedPanel")));
	TestNotNull(TEXT("project child preserved"), WBP->WidgetTree->FindWidget(TEXT("ProjectChild")));
	TestNotNull(TEXT("panel with developer binding preserved"), WBP->WidgetTree->FindWidget(TEXT("BoundPanel")));
	TestNotNull(TEXT("kept node still present"), WBP->WidgetTree->FindWidget(TEXT("Keeper")));

	TestEqual(TEXT("no silent delete"), QACountKind(Result, TEXT("reimport_removed")), 0);
	TestEqual(TEXT("two remove conflicts"), QACountKind(Result, TEXT("reimport_remove_conflict")), 2);
	TestTrue(TEXT("unowned-widget reason names the project child"),
		QAHasMessageWithAll(Result, TEXT("reimport_remove_conflict"),
			{ TEXT("DoomedPanel"), TEXT("widgets outside the generation record"), TEXT("ProjectChild") }));
	TestTrue(TEXT("developer-binding reason names the node"),
		QAHasMessageWithAll(Result, TEXT("reimport_remove_conflict"),
			{ TEXT("BoundPanel"), TEXT("developer bindings in the blueprint") }));

	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads"), QALoadSidecar(TargetPackage, Sidecar)))
	{
		TestNotNull(TEXT("conflicted panel stays recorded (unowned child)"), Sidecar.FindInstanceByDesignNodeId(TEXT("qa_doom")));
		TestNotNull(TEXT("conflicted panel stays recorded (binding)"), Sidecar.FindInstanceByDesignNodeId(TEXT("qa_bound")));
		TestTrue(TEXT("subtree widgets stay owned"), Sidecar.D2UOwned.Contains(TEXT("DoomHead")) && Sidecar.D2UOwned.Contains(TEXT("BoundTail")));
		TestFalse(TEXT("project child stays outside d2uOwned"), Sidecar.D2UOwned.Contains(TEXT("ProjectChild")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 11（integration）：kind-changed 冲突——mapped→generated 转换但旧子树含
// 项目内容（开发者绑定）→ 保留现状 + 冲突诊断，不替换；manifest 记录不变。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAKindChangeProjectContentTest,
	"Design2UMG.SafeReimportQA.KindChangePreservesProjectContent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAKindChangeProjectContentTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportKindConflict");

	const FString V1 = MakeQADoc(MakeQAButton(TEXT("qa_btn_1"), TEXT("HeroButton"), 24, 24, TEXT("Checkout")));
	UWidgetBlueprint* WBP = QAImport(V1, QAMappedClassPath, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	UDesign2UMGMappingQAWidget* MappedBefore = QAFindMappedInstance(WBP);
	if (!TestNotNull(TEXT("mapped instance generated"), MappedBefore))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	TWeakObjectPtr<UDesign2UMGMappingQAWidget> MappedWeak = MappedBefore;

	// 开发者绑定挂在 mapped 实例（子树根）上：旧子树不再是纯 D2U-owned。
	FDelegateEditorBinding ProjectBinding;
	ProjectBinding.ObjectName = TEXT("HeroButton");
	ProjectBinding.PropertyName = TEXT("OnClicked");
	ProjectBinding.FunctionName = TEXT("HandleHero");
	WBP->Bindings.Add(ProjectBinding);

	// v2：同一设计节点，但本轮不再提供 mapping → kind mapped→generated。
	const FString V2 = MakeQADoc(MakeQAButton(TEXT("qa_btn_1"), TEXT("HeroButton"), 24, 24, TEXT("Checkout")));
	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport itself succeeds (conflict is per-node)"), Result.bSucceeded);

	UDesign2UMGMappingQAWidget* MappedAfter = MappedWeak.Get();
	TestNotNull(TEXT("mapped instance kept"), MappedAfter);
	if (MappedAfter)
	{
		TestEqual(TEXT("mapped instance class unchanged"), MappedAfter->GetClass()->GetPathName(), FString(QAMappedClassPath));
	}
	TestEqual(TEXT("kind change conflict diagnosed once"), QACountKind(Result, TEXT("reimport_kind_conflict")), 1);
	TestTrue(TEXT("conflict reports mapped -> generated and the reason on one message"),
		QAHasMessageWithAll(Result, TEXT("reimport_kind_conflict"),
			{ TEXT("HeroButton"), TEXT("mapped -> generated"), TEXT("developer bindings in the blueprint") }));
	TestEqual(TEXT("no replacement performed"), QACountKind(Result, TEXT("reimport_kind_changed")), 0);

	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads"), QALoadSidecar(TargetPackage, Sidecar)))
	{
		const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* Record =
			Sidecar.FindInstanceByDesignNodeId(TEXT("qa_btn_1"));
		if (TestNotNull(TEXT("transitioned node stays recorded"), Record))
		{
			TestEqual(TEXT("manifest kind stays mapped"), Record->Kind, FString(TEXT("mapped")));
			TestEqual(TEXT("manifest mapped class stays"), Record->MappedClass, FString(QAMappedClassPath));
			TestEqual(TEXT("manifest widget name stays"), Record->WidgetName, FString(TEXT("HeroButton")));
		}
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 12（integration）：move 正向——设计把节点挪进新增容器，两端明确 D2U-owned
// → widget 换父但对象身份保留；manifest 顶层实例收口、所有权保留。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAMoveWithinOwnedBoundsTest,
	"Design2UMG.SafeReimportQA.MoveReparentWithinOwnedBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAMoveWithinOwnedBoundsTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportMoveOk");

	// MoverPanel 两版同为带两个子节点的纯容器（类型跨版本一致），v1 挂在宿主根下。
	const FString MoverPanelJson = MakeQAPanel(TEXT("qa_mover"), TEXT("MoverPanel"), 40, 32,
		FString::Printf(
			TEXT("%s,%s"),
			*MakeQAText(TEXT("qa_ma"), TEXT("MoverA"), 48, 40, TEXT("A")),
			*MakeQAText(TEXT("qa_mb"), TEXT("MoverB"), 48, 68, TEXT("B"))));

	const FString V1 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MoverPanelJson,
		*MakeQAText(TEXT("qa_keep"), TEXT("Keeper"), 250, 24, TEXT("Keep"))));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	UWidget* MoverBefore = WBP->WidgetTree->FindWidget(TEXT("MoverPanel"));
	if (!TestNotNull(TEXT("MoverPanel present in v1"), MoverBefore))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	TWeakObjectPtr<UWidget> MoverWeak = MoverBefore;
	TestEqual(TEXT("v1 parent is the page host root"),
		MoverBefore->GetParent()->GetFName(), WBP->WidgetTree->RootWidget->GetFName());

	// v2：新容器 TargetBox 收编 MoverPanel（整棵子树随迁）。
	const FString V2 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAPanel(TEXT("qa_target"), TEXT("TargetBox"), 24, 120,
			FString::Printf(
				TEXT("%s,%s"),
				*MakeQAText(TEXT("qa_pad"), TEXT("PadText"), 32, 128, TEXT("Pad")),
				*MakeQAPanel(TEXT("qa_mover"), TEXT("MoverPanel"), 32, 160,
					FString::Printf(
						TEXT("%s,%s"),
						*MakeQAText(TEXT("qa_ma"), TEXT("MoverA"), 40, 168, TEXT("A")),
						*MakeQAText(TEXT("qa_mb"), TEXT("MoverB"), 40, 196, TEXT("B")))))),
		*MakeQAText(TEXT("qa_keep"), TEXT("Keeper"), 250, 24, TEXT("Keep"))));

	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);

	UWidget* MoverAfter = MoverWeak.Get();
	TestNotNull(TEXT("moved widget still alive"), MoverAfter);
	UWidget* TargetBox = WBP->WidgetTree->FindWidget(TEXT("TargetBox"));
	TestNotNull(TEXT("target container added"), TargetBox);
	if (MoverAfter && TargetBox)
	{
		TestTrue(TEXT("widget object identity preserved across reparent"), MoverAfter == MoverBefore);
		TestEqual(TEXT("moved under the new container"), MoverAfter->GetParent()->GetFName(), TargetBox->GetFName());
		if (UWidget* MoverChild = WBP->WidgetTree->FindWidget(TEXT("MoverA")))
		{
			TestEqual(TEXT("mover subtree travelled with it"), MoverChild->GetParent()->GetFName(), MoverAfter->GetFName());
		}
		else
		{
			AddError(TEXT("mover subtree child missing after move"));
		}
	}

	TestEqual(TEXT("one move diagnosed"), QACountKind(Result, TEXT("reimport_moved")), 1);
	TestTrue(TEXT("move diagnostic reports identity preserved on one message"),
		QAHasMessageWithAll(Result, TEXT("reimport_moved"), { TEXT("MoverPanel"), TEXT("widget identity preserved") }));

	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads"), QALoadSidecar(TargetPackage, Sidecar)))
	{
		TestNull(TEXT("moved node no longer a top-level instance"), Sidecar.FindInstanceByDesignNodeId(TEXT("qa_mover")));
		TestNotNull(TEXT("new container recorded as instance"), Sidecar.FindInstanceByDesignNodeId(TEXT("qa_target")));
		TestTrue(TEXT("moved node still D2U-owned"), Sidecar.D2UOwned.Contains(TEXT("MoverPanel")));
		TestTrue(TEXT("moved subtree still D2U-owned"), Sidecar.D2UOwned.Contains(TEXT("MoverA")) && Sidecar.D2UOwned.Contains(TEXT("MoverB")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 13（integration）：move 冲突——新设计父端不是 manifest 实例也不是本轮新建
// 的 D2U 节点（嵌套容器，归属不明）→ 结构保留 + move 冲突诊断，不挪。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAMoveBoundaryConflictTest,
	"Design2UMG.SafeReimportQA.MoveConflictOnUnclearBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAMoveBoundaryConflictTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportMoveConflict");

	// v1：SidePanel > InnerZone（嵌套容器，非 manifest 实例）+ 顶层 Mover。
	const FString InnerZoneChildren = FString::Printf(
		TEXT("%s,%s"),
		*MakeQAText(TEXT("qa_i1"), TEXT("InnerA"), 40, 40, TEXT("A")),
		*MakeQAText(TEXT("qa_i2"), TEXT("InnerB"), 40, 68, TEXT("B")));
	const FString V1 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAPanel(TEXT("qa_side"), TEXT("SidePanel"), 24, 24,
			FString::Printf(
				TEXT("%s,%s"),
				*MakeQAPanel(TEXT("qa_inner"), TEXT("InnerZone"), 32, 32, InnerZoneChildren),
				*MakeQAText(TEXT("qa_spad"), TEXT("SidePad"), 140, 32, TEXT("Pad")))),
		*MakeQAText(TEXT("qa_mover"), TEXT("Mover"), 250, 24, TEXT("Move Me"))));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// v2：设计把 Mover 挪进 InnerZone（嵌套容器，边界不清）。
	const FString V2 = MakeQADoc(FString::Printf(
		TEXT("%s"),
		*MakeQAPanel(TEXT("qa_side"), TEXT("SidePanel"), 24, 24,
			FString::Printf(
				TEXT("%s,%s"),
				*MakeQAPanel(TEXT("qa_inner"), TEXT("InnerZone"), 32, 32,
					FString::Printf(
						TEXT("%s,%s,%s"),
						*MakeQAText(TEXT("qa_i1"), TEXT("InnerA"), 40, 40, TEXT("A")),
						*MakeQAText(TEXT("qa_i2"), TEXT("InnerB"), 40, 68, TEXT("B")),
						*MakeQAText(TEXT("qa_mover"), TEXT("Mover"), 40, 96, TEXT("Move Me")))),
				*MakeQAText(TEXT("qa_spad"), TEXT("SidePad"), 140, 32, TEXT("Pad"))))));

	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport itself succeeds (conflict is per-node)"), Result.bSucceeded);

	UWidget* Mover = WBP->WidgetTree->FindWidget(TEXT("Mover"));
	TestNotNull(TEXT("mover still exists"), Mover);
	if (Mover)
	{
		TestEqual(TEXT("structure preserved: mover stays under the page host root"),
			Mover->GetParent()->GetFName(), WBP->WidgetTree->RootWidget->GetFName());
	}
	TestEqual(TEXT("move conflict diagnosed once"), QACountKind(Result, TEXT("reimport_move_conflict")), 1);
	TestTrue(TEXT("unclear-ownership reason reported on one message"),
		QAHasMessageWithAll(Result, TEXT("reimport_move_conflict"), { TEXT("Mover"), TEXT("not clearly D2U-owned") }));
	TestEqual(TEXT("no moves applied"), QACountKind(Result, TEXT("reimport_moved")), 0);

	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads"), QALoadSidecar(TargetPackage, Sidecar)))
	{
		TestNotNull(TEXT("mover stays recorded as instance"), Sidecar.FindInstanceByDesignNodeId(TEXT("qa_mover")));
		TestTrue(TEXT("mover stays owned"), Sidecar.D2UOwned.Contains(TEXT("Mover")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 14（integration）：Prototype 绑定按稳定设计身份刷新（无项目冲突）——
// URL 变化写入运行时扩展数据，刷新诊断写明生效条数。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAPrototypeRefreshTest,
	"Design2UMG.SafeReimportQA.PrototypeRefreshByDesignIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAPrototypeRefreshTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportPrototypeRefresh");

	const FString V1 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAInteractive(TEXT("qa_link_a"), TEXT("LinkAlpha"), 24, TEXT("ON_CLICK"), TEXT("https://qa.example/old-a")),
		*MakeQAInteractive(TEXT("qa_link_b"), TEXT("LinkBeta"), 160, TEXT("ON_CLICK"), TEXT("https://qa.example/old-b"))));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	const FString V2 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAInteractive(TEXT("qa_link_a"), TEXT("LinkAlpha"), 24, TEXT("ON_CLICK"), TEXT("https://qa.example/new-a")),
		*MakeQAInteractive(TEXT("qa_link_b"), TEXT("LinkBeta"), 160, TEXT("ON_CLICK"), TEXT("https://qa.example/new-b"))));

	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);

	TestEqual(TEXT("no prototype conflicts"), QACountKind(Result, TEXT("reimport_prototype_conflict")), 0);
	TestEqual(TEXT("refresh diagnosed once"), QACountKind(Result, TEXT("reimport_prototype_refreshed")), 1);
	TestTrue(TEXT("refresh diagnostic reports both bindings active"),
		QAHasMessage(Result, TEXT("reimport_prototype_refreshed"), TEXT("D2U prototype bindings refreshed by stable design identity (2 binding(s) active)")));

	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(WBP->GeneratedClass);
	const UDesign2UMGPrototypeNavigationClassExtension* Extension =
		GeneratedClass ? GeneratedClass->GetExtension<UDesign2UMGPrototypeNavigationClassExtension>() : nullptr;
	if (TestNotNull(TEXT("prototype extension present after reimport"), Extension))
	{
		const FUMGPrototypeNavigationData& NavData = Extension->GetNavigationDataForTest();
		TestEqual(TEXT("both bindings kept"), NavData.ClickBindings.Num(), 2);
		const FUMGPrototypeNavigationBinding* BindingA = nullptr;
		const FUMGPrototypeNavigationBinding* BindingB = nullptr;
		for (const FUMGPrototypeNavigationBinding& Binding : NavData.ClickBindings)
		{
			if (Binding.TriggerWidgetId == TEXT("qa_link_a")) { BindingA = &Binding; }
			if (Binding.TriggerWidgetId == TEXT("qa_link_b")) { BindingB = &Binding; }
		}
		if (TestNotNull(TEXT("binding for qa_link_a present"), BindingA))
		{
			TestEqual(TEXT("binding A url refreshed"), BindingA->ExternalUrl, FString(TEXT("https://qa.example/new-a")));
		}
		if (TestNotNull(TEXT("binding for qa_link_b present"), BindingB))
		{
			TestEqual(TEXT("binding B url refreshed"), BindingB->ExternalUrl, FString(TEXT("https://qa.example/new-b")));
		}
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 15（integration）：双触发冲突（hover 触发）——同一交互已有项目绑定 →
// 项目行为保留、D2U 绑定让位 + 冲突诊断；未冲突绑定照常刷新。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAPrototypeHoverConflictTest,
	"Design2UMG.SafeReimportQA.PrototypeHoverConflict",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAPrototypeHoverConflictTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportPrototypeHover");

	const FString V1 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAInteractive(TEXT("qa_hover_a"), TEXT("HoverAlpha"), 24, TEXT("ON_HOVER"), TEXT("https://qa.example/old-a")),
		*MakeQAInteractive(TEXT("qa_hover_b"), TEXT("HoverBeta"), 160, TEXT("ON_HOVER"), TEXT("https://qa.example/old-b"))));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// 项目绑定占了 qa_hover_a 的 hover 交互。
	FDelegateEditorBinding ProjectBinding;
	ProjectBinding.ObjectName = TEXT("qa_hover_a");
	ProjectBinding.PropertyName = TEXT("OnHovered");
	ProjectBinding.FunctionName = TEXT("HandleHover");
	WBP->Bindings.Add(ProjectBinding);

	const FString V2 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAInteractive(TEXT("qa_hover_a"), TEXT("HoverAlpha"), 24, TEXT("ON_HOVER"), TEXT("https://qa.example/new-a")),
		*MakeQAInteractive(TEXT("qa_hover_b"), TEXT("HoverBeta"), 160, TEXT("ON_HOVER"), TEXT("https://qa.example/new-b"))));

	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);

	TestEqual(TEXT("hover dual-trigger conflict diagnosed once"), QACountKind(Result, TEXT("reimport_prototype_conflict")), 1);
	TestTrue(TEXT("conflict names trigger, type and outcome on one message"),
		QAHasMessageWithAll(Result, TEXT("reimport_prototype_conflict"),
			{ TEXT("trigger 'qa_hover_a'"), TEXT("type 'ON_HOVER'"), TEXT("project behavior kept") }));

	TestTrue(TEXT("project binding kept in the blueprint"), WBP->Bindings.Contains(ProjectBinding));

	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(WBP->GeneratedClass);
	const UDesign2UMGPrototypeNavigationClassExtension* Extension =
		GeneratedClass ? GeneratedClass->GetExtension<UDesign2UMGPrototypeNavigationClassExtension>() : nullptr;
	if (TestNotNull(TEXT("prototype extension present after reimport"), Extension))
	{
		const FUMGPrototypeNavigationData& NavData = Extension->GetNavigationDataForTest();
		TestEqual(TEXT("only the unconflicted binding remains"), NavData.ClickBindings.Num(), 1);
		if (NavData.ClickBindings.Num() == 1)
		{
			TestEqual(TEXT("remaining binding is the unconflicted one"),
				NavData.ClickBindings[0].TriggerWidgetId, FString(TEXT("qa_hover_b")));
			TestEqual(TEXT("remaining binding refreshed to the new url"),
				NavData.ClickBindings[0].ExternalUrl, FString(TEXT("https://qa.example/new-b")));
		}
	}
	TestTrue(TEXT("refresh diagnostic reports one binding active"),
		QAHasMessage(Result, TEXT("reimport_prototype_refreshed"), TEXT("(1 binding(s) active)")));

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 15b（integration）：双触发冲突（click 触发、Figma 真实节点 id 形态）——
// 触发节点 id 含 ':' 时按钮 widget 名是 id 清洗后的形态（如 "12:7" -> "W127"），
// 项目绑定按 widget 名登记；冲突检测必须按同一清洗规则比对，否则真实 Figma
// 稿上的双触发冲突全部漏判（项目绑定与 D2U 刷新绑定并存）。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAPrototypeConflictFigmaIdTest,
	"Design2UMG.SafeReimportQA.PrototypeConflictFigmaIdSanitizedMatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAPrototypeConflictFigmaIdTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportProtoFigmaId");

	const FString V1 = MakeQADoc(
		MakeQAInteractive(TEXT("12:7"), TEXT("BuyButton"), 24, TEXT("ON_CLICK"), TEXT("https://qa.example/old")));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// 节点 id "12:7" 经 slug 清洗（删 ':'）+ 数字前缀处理得到按钮 widget 名 "W127"；
	// 项目绑定在 UMG 编辑器里登记的是这个 widget 名。
	const FName SanitizedButtonName = FUMGWidgetUtils::SanitizeWidgetName(TEXT("12:7"), TEXT("Button"));
	if (!TestEqual(TEXT("sanitized button name for Figma-style id"), SanitizedButtonName, FName(TEXT("W127"))))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	FDelegateEditorBinding ProjectBinding;
	ProjectBinding.ObjectName = SanitizedButtonName.ToString();
	ProjectBinding.PropertyName = TEXT("OnClicked");
	ProjectBinding.FunctionName = TEXT("HandleBuy");
	WBP->Bindings.Add(ProjectBinding);

	const FString V2 = MakeQADoc(
		MakeQAInteractive(TEXT("12:7"), TEXT("BuyButton"), 24, TEXT("ON_CLICK"), TEXT("https://qa.example/new")));
	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);

	TestEqual(TEXT("dual-trigger conflict on Figma-style id diagnosed once"),
		QACountKind(Result, TEXT("reimport_prototype_conflict")), 1);
	TestTrue(TEXT("conflict names the original design id"),
		QAHasMessageWithAll(Result, TEXT("reimport_prototype_conflict"),
			{ TEXT("trigger '12:7'"), TEXT("type 'ON_CLICK'"), TEXT("project behavior kept") }));
	TestTrue(TEXT("project binding kept in the blueprint"), WBP->Bindings.Contains(ProjectBinding));

	// 让位后无存活设计绑定（0 binding active）时不再挂 prototype 扩展属预期；
	// 本用例的核心锁定点：确定性命名（按钮名可从节点 id 推导）+ 冲突检测不漏。
	if (WBP->WidgetTree)
	{
		int32 ButtonCount = 0;
		WBP->WidgetTree->ForEachWidget([&ButtonCount](UWidget* W)
		{
			if (W->IsA<UButton>()) { ++ButtonCount; }
		});
		TestEqual(TEXT("single deterministic trigger button, no duplicate from failed match"), ButtonCount, 1);
		TestNotNull(TEXT("deterministically named button present"), WBP->WidgetTree->FindWidget(TEXT("W127")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 16（integration）：Mapped Class 变化冲突——mapping 换指向别的 WBP 类 →
// 既有实例保留、不替换、inputs 不覆写；诊断报新旧类；manifest 记录保持旧类。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAMappedClassChangeTest,
	"Design2UMG.SafeReimportQA.MappedClassChangeIsConflict",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAMappedClassChangeTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportMappedClass");

	const FString V1 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAButton(TEXT("qa_btn_1"), TEXT("HeroButton"), 24, 24, TEXT("Checkout")),
		*MakeQAText(TEXT("qa_keep"), TEXT("Keeper"), 200, 24, TEXT("Keep"))));
	UWidgetBlueprint* WBP = QAImport(V1, QAMappedClassPath, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	UDesign2UMGMappingQAWidget* MappedBefore = QAFindMappedInstance(WBP);
	if (!TestNotNull(TEXT("mapped instance generated"), MappedBefore))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	TWeakObjectPtr<UDesign2UMGMappingQAWidget> MappedWeak = MappedBefore;
	MappedBefore->TextValue = FText::FromString(TEXT("Hacked"));

	// v2：同一 Key 的 mapping 指向另一个 WBP 类。
	const FString V2 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAButton(TEXT("qa_btn_1"), TEXT("HeroButton"), 24, 24, TEXT("Pay Now")),
		*MakeQAText(TEXT("qa_keep"), TEXT("Keeper"), 200, 24, TEXT("Keep"))));
	const FDesignReimportResult Result = QAReimport(V2, QAOtherMappedClassPath, TargetPackage);
	TestTrue(TEXT("reimport itself succeeds (conflict is per-node)"), Result.bSucceeded);

	UDesign2UMGMappingQAWidget* MappedAfter = MappedWeak.Get();
	if (TestNotNull(TEXT("mapped instance kept (not destroyed, not replaced)"), MappedAfter))
	{
		TestTrue(TEXT("instance object identity preserved"), MappedAfter == MappedBefore);
		TestEqual(TEXT("instance class not replaced"), MappedAfter->GetClass()->GetPathName(), FString(QAMappedClassPath));
		TestEqual(TEXT("inputs not overwritten on mapped-class conflict"), MappedAfter->TextValue.ToString(), FString(TEXT("Hacked")));
	}

	TestEqual(TEXT("mapped class conflict diagnosed once"), QACountKind(Result, TEXT("reimport_mapped_class_conflict")), 1);
	TestTrue(TEXT("conflict reports node, old class, new class and no-replacement on one message"),
		QAHasMessageWithAll(Result, TEXT("reimport_mapped_class_conflict"),
			{ TEXT("HeroButton"), QAMappedClassPath, QAOtherMappedClassPath, TEXT("not replaced in v1") }));

	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads"), QALoadSidecar(TargetPackage, Sidecar)))
	{
		const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* Record =
			Sidecar.FindInstanceByDesignNodeId(TEXT("qa_btn_1"));
		if (TestNotNull(TEXT("conflicted instance stays recorded"), Record))
		{
			TestEqual(TEXT("manifest keeps the old mapped class"), Record->MappedClass, FString(QAMappedClassPath));
		}
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 17（integration）：保存失败语义——事务回滚（内存文本与结构回到 reimport 前）、
// manifest 字节不变、诊断写明失败；随后重试同一 reimport 可成功（资产未被污染）。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQASaveFailureRollbackTest,
	"Design2UMG.SafeReimportQA.SaveFailureRollsBackAndKeepsManifest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQASaveFailureRollbackTest::RunTest(const FString&)
{
	// 保存失败注入路径按设计打错误日志，登记为预期。
	AddExpectedError(TEXT("failed to save"), EAutomationExpectedErrorFlags::Contains, 0);

	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportSaveFailure");

	const FString V1 = MakeQADoc(MakeQAPanel(TEXT("qa_panel"), TEXT("InfoPanel"), 24, 24,
		FString::Printf(
			TEXT("%s,%s"),
			*MakeQAText(TEXT("qa_head"), TEXT("Head"), 32, 32, TEXT("Head v1")),
			*MakeQAText(TEXT("qa_tail"), TEXT("Tail"), 32, 60, TEXT("Tail v1")))));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	const FString SidecarPath = Design2UMGGenerationManifest::BuildSidecarPathForAsset(TargetPackage);
	FString ManifestBefore;
	if (!TestTrue(TEXT("v1 manifest exists"), QAReadFileString(SidecarPath, ManifestBefore)))
	{
		QAWipeOutput(OutputDir);
		return false;
	}

	const FString V2 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAPanel(TEXT("qa_panel"), TEXT("InfoPanel"), 24, 24,
			FString::Printf(
				TEXT("%s,%s"),
				*MakeQAText(TEXT("qa_head"), TEXT("Head"), 32, 32, TEXT("Head v2")),
				*MakeQAText(TEXT("qa_tail"), TEXT("Tail"), 32, 60, TEXT("Tail v2")))),
		*MakeQAText(TEXT("qa_fresh"), TEXT("FreshText"), 250, 24, TEXT("Fresh"))));

	FUMGGenerator::ArmAutomationSaveFailureOnce();
	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);

	TestFalse(TEXT("reimport reports failure"), Result.bSucceeded);
	TestFalse(TEXT("failure is not a full conflict"), Result.bFullConflict);
	TestNull(TEXT("no updated blueprint on failure"), Result.UpdatedBlueprint);
	TestEqual(TEXT("save failure diagnosed once"), QACountKind(Result, TEXT("reimport_save_failed")), 1);
	TestTrue(TEXT("failure diagnostic states rollback and untouched manifest on one message"),
		QAHasMessageWithAll(Result, TEXT("reimport_save_failed"),
			{ TEXT("transaction rolled back"), TEXT("asset keeps its pre-reimport state"), TEXT("manifest not updated") }));

	// 事务回滚：内存状态保持 reimport 前（v1 设计值，v2 文本未写入）。
	if (UTextBlock* Head = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Head"))))
	{
		TestEqual(TEXT("in-memory text rolled back"), Head->GetText().ToString(), FString(TEXT("Head v1")));
	}
	else
	{
		AddError(TEXT("Head widget missing after rollback"));
	}
	TestNull(TEXT("reimport-added widget rolled back"), WBP->WidgetTree->FindWidget(TEXT("FreshText")));

	// manifest 不更新：字节级不变。
	FString ManifestAfter;
	TestTrue(TEXT("manifest still exists"), QAReadFileString(SidecarPath, ManifestAfter));
	TestTrue(TEXT("manifest untouched on save failure"), ManifestAfter == ManifestBefore);

	// 资产未被污染：同一 v2 立即重试可以成功应用。
	const FDesignReimportResult Retry = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("retry after rollback succeeds"), Retry.bSucceeded);
	if (UTextBlock* Head = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Head"))))
	{
		TestEqual(TEXT("retry applies the design update"), Head->GetText().ToString(), FString(TEXT("Head v2")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 18（integration）：并列副本兜底——manifest 缺失 = 全量冲突；显式选择后生成
// 并列 WBP + 自有 manifest，原资产字节不动；副本已存在时再次显式选择只提示跳过，
// 不重复生成、不覆盖。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAResolutionCopySkipsExistingTest,
	"Design2UMG.SafeReimportQA.ResolutionCopySkipsExisting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAResolutionCopySkipsExistingTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportResolutionCopy");

	const FString V1 = MakeQADoc(MakeQAText(TEXT("qa_t1"), TEXT("AlphaText"), 24, 24, TEXT("One")));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	const FString AssetFile = QAAssetFilePath(TargetPackage);
	const int64 BytesBefore = IFileManager::Get().FileSize(*AssetFile);
	const FString CopyPackage = FPaths::GetPath(TargetPackage) / TEXT("WBP_QAPage_Resolution");
	const FString CopyAssetFile = QAAssetFilePath(CopyPackage);

	// 制造全量冲突：删掉 sidecar。
	IFileManager::Get().Delete(*Design2UMGGenerationManifest::BuildSidecarPathForAsset(TargetPackage), false, true);

	// 第一次显式选择：生成副本。
	FDesignReimportResult Result = QAReimport(V1, nullptr, TargetPackage, /*bGenerateResolutionCopy=*/true);
	TestTrue(TEXT("full conflict still reported"), Result.bFullConflict);
	TestFalse(TEXT("in-place update did not happen"), Result.bSucceeded);
	TestEqual(TEXT("resolution copy path reported"),
		Result.ResolutionCopyPath, CopyPackage + TEXT(".WBP_QAPage_Resolution"));
	TestTrue(TEXT("copy asset file exists"), FPaths::FileExists(CopyAssetFile));
	FDesign2UMGGenerationManifestSidecar CopySidecar;
	TestTrue(TEXT("copy has its own manifest"), QALoadSidecar(CopyPackage, CopySidecar));
	TestTrue(TEXT("original asset bytes unchanged"), IFileManager::Get().FileSize(*AssetFile) == BytesBefore);
	TestFalse(TEXT("original still has no manifest"),
		FPaths::FileExists(Design2UMGGenerationManifest::BuildSidecarPathForAsset(TargetPackage)));
	const int64 CopyBytes = IFileManager::Get().FileSize(*CopyAssetFile);

	// 第二次显式选择：副本已存在 → 跳过，不覆盖。
	Result = QAReimport(V1, nullptr, TargetPackage, /*bGenerateResolutionCopy=*/true);
	TestTrue(TEXT("full conflict still reported on retry"), Result.bFullConflict);
	TestTrue(TEXT("no second copy path reported"), Result.ResolutionCopyPath.IsEmpty());
	TestTrue(TEXT("skip diagnosed on one message"),
		QAHasMessageWithAll(Result, TEXT("reimport_resolution_copy"),
			{ TEXT("Resolution copy skipped"), TEXT("already exists") }));
	TestEqual(TEXT("copy counted once"), QACountKind(Result, TEXT("reimport_resolution_copy")), 1);
	TestTrue(TEXT("existing copy asset untouched"), IFileManager::Get().FileSize(*CopyAssetFile) == CopyBytes);
	TestTrue(TEXT("original asset still untouched"), IFileManager::Get().FileSize(*AssetFile) == BytesBefore);

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 19（integration）：普通导入回归面——同名资产仍走自动改名新建（不动旧资产、
// 不动旧 manifest），原地更新只发生在 Reimport 路径。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQANormalImportAutoRenameTest,
	"Design2UMG.SafeReimportQA.NormalImportStillAutoRenames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQANormalImportAutoRenameTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportAutoRename");

	const FString V1 = MakeQADoc(MakeQAText(TEXT("qa_t1"), TEXT("AlphaText"), 24, 24, TEXT("One")));

	// 第一次普通导入。
	UWidgetBlueprint* First = QAImport(V1, nullptr, OutputDir, /*bCleanFirst=*/true);
	if (!TestTrue(TEXT("first import generates"), First != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString FirstPackage = First->GetOutermost()->GetName();
	const FString FirstSidecarPath = Design2UMGGenerationManifest::BuildSidecarPathForAsset(FirstPackage);
	FString FirstManifest;
	if (!TestTrue(TEXT("first manifest exists"), QAReadFileString(FirstSidecarPath, FirstManifest)))
	{
		QAWipeOutput(OutputDir);
		return false;
	}

	// 第二次普通导入（同名资产已存在）：自动改名新建，不动第一份。
	UWidgetBlueprint* Second = QAImport(V1, nullptr, OutputDir, /*bCleanFirst=*/false);
	TestNotNull(TEXT("second import generates a renamed asset"), Second);
	if (Second)
	{
		const FString SecondPackage = Second->GetOutermost()->GetName();
		TestTrue(TEXT("second asset is auto-renamed"), SecondPackage != FirstPackage);
		TestEqual(TEXT("auto-rename appends _1"), SecondPackage, FirstPackage + TEXT("_1"));
		TestTrue(TEXT("second import has its own manifest"),
			FPaths::FileExists(Design2UMGGenerationManifest::BuildSidecarPathForAsset(SecondPackage)));
	}

	// 原资产未被原地更新：可加载、manifest 字节不变。
	UWidgetBlueprint* FirstReloaded = LoadObject<UWidgetBlueprint>(
		nullptr, *(FirstPackage + TEXT(".") + FPaths::GetBaseFilename(FirstPackage)));
	TestNotNull(TEXT("original asset still loads"), FirstReloaded);
	FString FirstManifestAfter;
	TestTrue(TEXT("original manifest still readable"), QAReadFileString(FirstSidecarPath, FirstManifestAfter));
	TestTrue(TEXT("original manifest untouched by the second normal import"), FirstManifestAfter == FirstManifest);

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 20（integration）：嵌套结构边界——设计在既有容器内新增嵌套节点走生成链补建；
// 设计消失的嵌套子节点保留现状（manifest 只记录顶层实例，嵌套删除不是安全删除范围）。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQANestedStructureBoundaryTest,
	"Design2UMG.SafeReimportQA.NestedAddAndRemovalBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQANestedStructureBoundaryTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportNestedBoundary");

	const FString V1 = MakeQADoc(MakeQAPanel(TEXT("qa_panel"), TEXT("InfoPanel"), 24, 24,
		FString::Printf(
			TEXT("%s,%s"),
			*MakeQAText(TEXT("qa_head"), TEXT("Head"), 32, 32, TEXT("Head v1")),
			*MakeQAText(TEXT("qa_tail"), TEXT("Tail"), 32, 60, TEXT("Tail v1")))));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// v2：容器内 Head 文本变化 + 新增 FreshLine；Tail 从设计里消失。
	const FString V2 = MakeQADoc(MakeQAPanel(TEXT("qa_panel"), TEXT("InfoPanel"), 24, 24,
		FString::Printf(
			TEXT("%s,%s"),
			*MakeQAText(TEXT("qa_head"), TEXT("Head"), 32, 32, TEXT("Head v2")),
			*MakeQAText(TEXT("qa_fresh"), TEXT("FreshLine"), 32, 60, TEXT("Fresh v2")))));

	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);

	if (UTextBlock* Head = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Head"))))
	{
		TestEqual(TEXT("nested kept node refreshed"), Head->GetText().ToString(), FString(TEXT("Head v2")));
	}
	else
	{
		AddError(TEXT("Head widget missing"));
	}

	UTextBlock* Fresh = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("FreshLine")));
	TestNotNull(TEXT("nested new node added"), Fresh);
	if (Fresh)
	{
		TestEqual(TEXT("nested new node text applied"), Fresh->GetText().ToString(), FString(TEXT("Fresh v2")));
		TestEqual(TEXT("nested new node parented under the container"),
			Fresh->GetParent()->GetFName(), FName(TEXT("InfoPanel")));
	}
	TestTrue(TEXT("nested add diagnosed with parent context on one message"),
		QAHasMessageWithAll(Result, TEXT("reimport_added"), { TEXT("FreshLine"), TEXT("added under 'InfoPanel'") }));

	// 设计消失的嵌套子节点：保留现状（非 manifest 实例，不进安全删除流）。
	TestNotNull(TEXT("design-removed nested node preserved"), WBP->WidgetTree->FindWidget(TEXT("Tail")));
	TestEqual(TEXT("no removal performed"), QACountKind(Result, TEXT("reimport_removed")), 0);
	TestEqual(TEXT("no removal conflict"), QACountKind(Result, TEXT("reimport_remove_conflict")), 0);

	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads"), QALoadSidecar(TargetPackage, Sidecar)))
	{
		TestNotNull(TEXT("container stays recorded as instance"), Sidecar.FindInstanceByDesignNodeId(TEXT("qa_panel")));
		TestTrue(TEXT("nested widgets stay owned"), Sidecar.D2UOwned.Contains(TEXT("Head")) && Sidecar.D2UOwned.Contains(TEXT("Tail")));
		TestTrue(TEXT("nested new node ownership recorded"), Sidecar.D2UOwned.Contains(TEXT("FreshLine")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 21（integration，R7a）：kind-changed 正向替换两方向——旧子树纯 D2U-owned 时
// mapped->generated 与 generated->mapped 都整体替换为新形态 + reimport_kind_changed
// 诊断（一条消息点名节点与方向）+ manifest kind 更新（对照：冲突分支见用例 11）。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAKindChangePositiveTest,
	"Design2UMG.SafeReimportQA.KindChangeReplacesPureSubtreeBothDirections",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAKindChangePositiveTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportKindPositive");

	const auto BuildDoc = [](const TCHAR* Label)
	{
		return MakeQADoc(FString::Printf(
			TEXT("%s,%s"),
			*MakeQAButton(TEXT("qa_btn_1"), TEXT("HeroButton"), 24, 24, Label),
			*MakeQAText(TEXT("qa_keep"), TEXT("Keeper"), 200, 24, TEXT("Keep"))));
	};

	// v1：mapping 生效 -> 组件实例是 mapped 形态。
	UWidgetBlueprint* WBP = QAImport(BuildDoc(TEXT("Checkout")), QAMappedClassPath, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	if (!TestNotNull(TEXT("v1 mapped instance present"), QAFindMappedInstance(WBP)))
	{
		QAWipeOutput(OutputDir);
		return false;
	}

	// v2：撤掉 mapping -> 同一设计节点 mapped->generated，旧子树纯 D2U-owned -> 整体替换。
	FDesignReimportResult Result = QAReimport(BuildDoc(TEXT("Checkout")), nullptr, TargetPackage);
	TestTrue(TEXT("mapped->generated reimport succeeds"), Result.bSucceeded);
	TestEqual(TEXT("kind change diagnosed once"), QACountKind(Result, TEXT("reimport_kind_changed")), 1);
	TestTrue(TEXT("transition message names node and direction"),
		QAHasMessageWithAll(Result, TEXT("reimport_kind_changed"), { TEXT("HeroButton"), TEXT("mapped -> generated") }));
	TestEqual(TEXT("no kind conflict on the pure subtree"), QACountKind(Result, TEXT("reimport_kind_conflict")), 0);

	bool bHasMappedAfterV2 = false;
	WBP->WidgetTree->ForEachWidget([&bHasMappedAfterV2](UWidget* Widget)
	{
		bHasMappedAfterV2 |= Widget->IsA<UDesign2UMGMappingQAWidget>();
	});
	TestFalse(TEXT("old mapped instance replaced away"), bHasMappedAfterV2);
	TestNotNull(TEXT("generated expansion present (component internals expanded)"),
		WBP->WidgetTree->FindWidget(TEXT("InnerLabel")));
	TestNotNull(TEXT("unrelated keeper untouched"), WBP->WidgetTree->FindWidget(TEXT("Keeper")));

	FDesign2UMGGenerationManifestSidecar SidecarAfterV2;
	if (TestTrue(TEXT("manifest reloads after mapped->generated"), QALoadSidecar(TargetPackage, SidecarAfterV2)))
	{
		const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* Record =
			SidecarAfterV2.FindInstanceByDesignNodeId(TEXT("qa_btn_1"));
		if (TestNotNull(TEXT("transitioned node stays recorded"), Record))
		{
			TestEqual(TEXT("manifest kind updated to generated"), Record->Kind, FString(TEXT("generated")));
			TestNotNull(TEXT("replaced record resolves to a live widget (no ghost)"),
				WBP->WidgetTree->FindWidget(FName(*Record->WidgetName)));
		}
	}

	// v3：恢复 mapping -> generated->mapped，旧展开子树同样纯 D2U-owned -> 替换回实例。
	Result = QAReimport(BuildDoc(TEXT("Checkout")), QAMappedClassPath, TargetPackage);
	TestTrue(TEXT("generated->mapped reimport succeeds"), Result.bSucceeded);
	TestEqual(TEXT("reverse kind change diagnosed once"), QACountKind(Result, TEXT("reimport_kind_changed")), 1);
	TestTrue(TEXT("reverse transition message names node and direction"),
		QAHasMessageWithAll(Result, TEXT("reimport_kind_changed"), { TEXT("HeroButton"), TEXT("generated -> mapped") }));

	UDesign2UMGMappingQAWidget* Remapped = QAFindMappedInstance(WBP);
	TestNotNull(TEXT("mapped instance re-created"), Remapped);
	if (Remapped)
	{
		TestEqual(TEXT("declared input written on the new instance"), Remapped->TextValue.ToString(), FString(TEXT("Checkout")));
	}
	TestNull(TEXT("generated expansion replaced away"), WBP->WidgetTree->FindWidget(TEXT("InnerLabel")));

	FDesign2UMGGenerationManifestSidecar SidecarAfterV3;
	if (TestTrue(TEXT("manifest reloads after generated->mapped"), QALoadSidecar(TargetPackage, SidecarAfterV3)))
	{
		const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* Record =
			SidecarAfterV3.FindInstanceByDesignNodeId(TEXT("qa_btn_1"));
		if (TestNotNull(TEXT("transitioned node stays recorded"), Record))
		{
			TestEqual(TEXT("manifest kind updated back to mapped"), Record->Kind, FString(TEXT("mapped")));
			TestEqual(TEXT("manifest mapped class updated"), Record->MappedClass, FString(QAMappedClassPath));
			TestEqual(TEXT("manifest mapped key updated"), Record->MappedKey, FString(QAMappingKey));
			if (TestEqual(TEXT("input declaration recorded"), Record->Inputs.Num(), 1) && Record->Inputs.Num() == 1)
			{
				TestEqual(TEXT("input source recorded"), Record->Inputs[0].Source, FString(TEXT("label")));
				TestEqual(TEXT("input target recorded"), Record->Inputs[0].Target, FString(TEXT("TextValue")));
				TestEqual(TEXT("input valueKind recorded"), Record->Inputs[0].ValueKind, FString(TEXT("ComponentProperty")));
			}
		}
		TestNotNull(TEXT("keeper still recorded"), SidecarAfterV3.FindInstanceByDesignNodeId(TEXT("qa_keep")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 22（integration，R7b）：generated 既有 D2U-owned 节点的几何与源派生视觉属性
// 刷新——v2 改纯生成节点的位置/尺寸（ApplySlotForNode 路径）与文本填充色
// （ApplyWidgetProperties 路径）后 reimport，断言新值生效。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAGeneratedGeometryVisualRefreshTest,
	"Design2UMG.SafeReimportQA.GeneratedGeometryAndVisualRefresh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAGeneratedGeometryVisualRefreshTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportGeometryVisual");

	// 几何观察对象：无填充纯容器 panel（Fixed 尺寸 -> 宿主根下的直接 canvas slot）。
	const auto MakeGeoPanel = [](int32 X, int32 Y, int32 W, int32 H)
	{
		return FString::Printf(
			TEXT("{\"id\":\"qa_geo\",\"name\":\"GeoPanel\",\"type\":\"FRAME\","
				 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d},"
				 "\"children\":[{\"id\":\"qa_geo_pad\",\"name\":\"GeoPad\",\"type\":\"TEXT\","
				 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":%d,\"width\":80,\"height\":18},"
				 "\"characters\":\"Pad\",%s}]}"),
			X, Y, W, H, X + 8, Y + 8, *QATextStyle());
	};

	// 视觉观察对象：带 SOLID 填充的 TEXT（fills -> ColorAndOpacity，源派生视觉属性）。
	const auto MakeStyledText = [](double R, double G, double B, int32 X, int32 Y, int32 W, int32 H, const TCHAR* Text)
	{
		return FString::Printf(
			TEXT("{\"id\":\"qa_style\",\"name\":\"StatusLine\",\"type\":\"TEXT\","
				 "\"absoluteBoundingBox\":{\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d},"
				 "\"characters\":\"%s\","
				 "\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":%.4f,\"g\":%.4f,\"b\":%.4f,\"a\":1}}],%s}"),
			X, Y, W, H, Text, R, G, B, *QATextStyle());
	};

	const FString V1 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeGeoPanel(24, 24, 200, 120),
		*MakeStyledText(0.20, 0.20, 0.90, 24, 160, 90, 20, TEXT("v1 line"))));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// v1 基线：位置 (24,24)、尺寸 200x120、填充色 rgb(51,51,230)（fills 0.20/0.20/0.90 -> 字节）。
	UWidget* GeoPanel = WBP->WidgetTree->FindWidget(TEXT("GeoPanel"));
	UTextBlock* StatusLine = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("StatusLine")));
	if (!TestNotNull(TEXT("GeoPanel present"), GeoPanel) || !TestNotNull(TEXT("StatusLine present"), StatusLine))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const TWeakObjectPtr<UWidget> GeoPanelWeak = GeoPanel;
	if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(GeoPanel->Slot))
	{
		TestTrue(TEXT("v1 panel at design position"),
			FMath::Abs(Slot->GetPosition().X - 24.0f) < 1.0f && FMath::Abs(Slot->GetPosition().Y - 24.0f) < 1.0f);
		TestTrue(TEXT("v1 panel at design size"),
			FMath::Abs(Slot->GetSize().X - 200.0f) < 1.0f && FMath::Abs(Slot->GetSize().Y - 120.0f) < 1.0f);
	}
	else
	{
		AddError(TEXT("v1 GeoPanel slot is not a canvas slot"));
	}

	// v2：panel 挪到 (60,96) 缩到 160x90；文本填充色改 0.95/0.30/0.15 -> rgb(242,77,38)。
	const FString V2 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeGeoPanel(60, 96, 160, 90),
		*MakeStyledText(0.95, 0.30, 0.15, 40, 200, 150, 28, TEXT("v2 line"))));

	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);
	TestTrue(TEXT("generated nodes refreshed (diagnosed)"),
		QAHasMessage(Result, TEXT("reimport_updated"), TEXT("GeoPanel"))
		&& QAHasMessage(Result, TEXT("reimport_updated"), TEXT("StatusLine")));

	GeoPanel = WBP->WidgetTree->FindWidget(TEXT("GeoPanel"));
	StatusLine = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("StatusLine")));
	if (TestNotNull(TEXT("GeoPanel still present"), GeoPanel)
		&& TestNotNull(TEXT("StatusLine still present"), StatusLine))
	{
		// 几何刷新：ApplySlotForNode 路径（新位置 + 新尺寸）。
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(GeoPanel->Slot))
		{
			TestTrue(TEXT("panel position refreshed from design"),
				FMath::Abs(Slot->GetPosition().X - 60.0f) < 1.0f && FMath::Abs(Slot->GetPosition().Y - 96.0f) < 1.0f);
			TestTrue(TEXT("panel size refreshed from design"),
				FMath::Abs(Slot->GetSize().X - 160.0f) < 1.0f && FMath::Abs(Slot->GetSize().Y - 90.0f) < 1.0f);
		}
		else
		{
			AddError(TEXT("GeoPanel slot is not a canvas slot after reimport"));
		}

		// 视觉刷新：ApplyWidgetProperties 路径（fills -> ColorAndOpacity）+ 文本内容。
		const FColor ColorAfter = StatusLine->GetColorAndOpacity().GetSpecifiedColor().ToFColor(true);
		TestTrue(TEXT("text fill color refreshed from design"),
			FMath::Abs(ColorAfter.R - 242) <= 1 && FMath::Abs(ColorAfter.G - 77) <= 1 && FMath::Abs(ColorAfter.B - 38) <= 1);
		TestEqual(TEXT("text content refreshed from design"), StatusLine->GetText().ToString(), FString(TEXT("v2 line")));
	}

	// 生成几何变化的节点身份不变（刷新而非重建）。
	TestTrue(TEXT("GeoPanel widget identity preserved (refreshed in place)"), GeoPanelWeak.Get() == GeoPanel);

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 23（integration，R2）：嵌套 mapped 实例的 IR 父是非实例嵌套容器
// （SidePanel>InnerZone>Button）且无结构变化时，reimport 不得输出误导性的
// reimport_move_conflict（诊断必须意味着真的跳过了结构变化）；实例照常刷新。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQANestedParentNoNoiseTest,
	"Design2UMG.SafeReimportQA.NestedParentNoMoveConflictNoise",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQANestedParentNoNoiseTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportNestedNoNoise");

	// SidePanel > InnerZone（嵌套纯容器，非 manifest 实例）> mapped 按钮。
	const auto BuildDoc = [](const TCHAR* Label)
	{
		return MakeQADoc(FString::Printf(
			TEXT("%s,%s"),
			*MakeQAPanel(TEXT("qa_side"), TEXT("SidePanel"), 24, 24, FString::Printf(
				TEXT("%s,%s"),
				*MakeQAPanel(TEXT("qa_inner"), TEXT("InnerZone"), 32, 32,
					MakeQAButton(TEXT("qa_btn_1"), TEXT("HeroButton"), 40, 40, Label)),
				*MakeQAText(TEXT("qa_spad"), TEXT("SidePad"), 240, 32, TEXT("Pad")))),
			*MakeQAText(TEXT("qa_keep"), TEXT("Keeper"), 250, 200, TEXT("Keep"))));
	};

	UWidgetBlueprint* WBP = QAImport(BuildDoc(TEXT("Checkout")), QAMappedClassPath, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	UDesign2UMGMappingQAWidget* Mapped = QAFindMappedInstance(WBP);
	if (!TestNotNull(TEXT("nested mapped instance generated"), Mapped)
		|| !TestTrue(TEXT("instance sits under InnerZone"),
			Mapped->GetParent() && Mapped->GetParent()->GetFName() == FName(TEXT("InnerZone"))))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	TWeakObjectPtr<UDesign2UMGMappingQAWidget> MappedWeak = Mapped;

	// 无结构变化的 reimport（只换 input 值）：不得报 move 冲突噪音。
	const FDesignReimportResult Result = QAReimport(BuildDoc(TEXT("Pay Now")), QAMappedClassPath, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);
	TestEqual(TEXT("no move-conflict noise for the unchanged nested parent"),
		QACountKind(Result, TEXT("reimport_move_conflict")), 0);
	TestEqual(TEXT("no moves applied"), QACountKind(Result, TEXT("reimport_moved")), 0);

	UDesign2UMGMappingQAWidget* MappedAfter = MappedWeak.Get();
	if (TestNotNull(TEXT("instance still alive"), MappedAfter))
	{
		TestEqual(TEXT("instance stays under the nested container"),
			MappedAfter->GetParent()->GetFName(), FName(TEXT("InnerZone")));
		TestEqual(TEXT("instance still refreshed normally"), MappedAfter->TextValue.ToString(), FString(TEXT("Pay Now")));
	}
	TestTrue(TEXT("instance refresh diagnosed"), QAHasMessage(Result, TEXT("reimport_updated"), TEXT("HeroButton")));

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 24（integration，R3 嵌套 move）：设计把嵌套节点 N 从 ContainerA 挪到
// ContainerB——源端纯 D2U-owned 时移动 widget（identity 保持、名字不变、源端
// 不残留、目标端不建副本）+ reimport_moved 诊断；不再误报 reimport_added。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQANestedMoveIdentityTest,
	"Design2UMG.SafeReimportQA.NestedMovePreservesIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQANestedMoveIdentityTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportNestedMove");

	const FString V1 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAPanel(TEXT("qa_ca"), TEXT("ContainerA"), 20, 20, FString::Printf(
			TEXT("%s,%s"),
			*MakeQAText(TEXT("qa_n"), TEXT("N"), 30, 30, TEXT("mv")),
			*MakeQAText(TEXT("qa_ah"), TEXT("AHint"), 30, 80, TEXT("a-hint")))),
		*MakeQAPanel(TEXT("qa_cb"), TEXT("ContainerB"), 260, 20,
			MakeQAText(TEXT("qa_bh"), TEXT("BHint"), 30, 80, TEXT("b-hint")))));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	UTextBlock* NBefore = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("N")));
	if (!TestNotNull(TEXT("nested N present in v1"), NBefore)
		|| !TestTrue(TEXT("N sits under ContainerA"),
			NBefore->GetParent() && NBefore->GetParent()->GetFName() == FName(TEXT("ContainerA"))))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const TWeakObjectPtr<UTextBlock> NWeak = NBefore;

	// v2：N 从 ContainerA 挪到 ContainerB。
	const FString V2 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAPanel(TEXT("qa_ca"), TEXT("ContainerA"), 20, 20,
			MakeQAText(TEXT("qa_ah"), TEXT("AHint"), 30, 80, TEXT("a-hint"))),
		*MakeQAPanel(TEXT("qa_cb"), TEXT("ContainerB"), 260, 20, FString::Printf(
			TEXT("%s,%s"),
			*MakeQAText(TEXT("qa_n"), TEXT("N"), 30, 30, TEXT("mv")),
			*MakeQAText(TEXT("qa_bh"), TEXT("BHint"), 30, 80, TEXT("b-hint"))))));

	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);

	UTextBlock* NAfter = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("N")));
	if (TestNotNull(TEXT("N still present after move"), NAfter))
	{
		TestTrue(TEXT("widget object identity preserved"), NAfter == NWeak.Get());
		TestEqual(TEXT("N moved under ContainerB"), NAfter->GetParent()->GetFName(), FName(TEXT("ContainerB")));
		TestEqual(TEXT("moved N keeps its design text"), NAfter->GetText().ToString(), FString(TEXT("mv")));
	}
	TestNull(TEXT("no duplicate copy created under the target"), WBP->WidgetTree->FindWidget(TEXT("N_1")));
	TestEqual(TEXT("nested move diagnosed once"), QACountKind(Result, TEXT("reimport_moved")), 1);
	TestTrue(TEXT("nested move diagnostic names node and identity preservation"),
		QAHasMessageWithAll(Result, TEXT("reimport_moved"), { TEXT("'N'"), TEXT("widget identity preserved") }));
	TestFalse(TEXT("N is not misreported as added"),
		QAHasMessage(Result, TEXT("reimport_added"), TEXT("'N'")));

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 25（integration，R3 嵌套 move 冲突）：开发者已把嵌套节点 N 挂到项目容器
// DevPanel 下，设计又把 N 挪到 ContainerB——N 保留在 DevPanel、ContainerB 下
// 不得新建副本，输出 reimport_move_conflict 点名 N 与原因。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQANestedMoveConflictNoCopyTest,
	"Design2UMG.SafeReimportQA.NestedMoveConflictCreatesNoCopy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQANestedMoveConflictNoCopyTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportNestedMoveConflict");

	const FString V1 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAPanel(TEXT("qa_ca"), TEXT("ContainerA"), 20, 20, FString::Printf(
			TEXT("%s,%s"),
			*MakeQAText(TEXT("qa_n"), TEXT("N"), 30, 30, TEXT("mv")),
			*MakeQAText(TEXT("qa_ah"), TEXT("AHint"), 30, 80, TEXT("a-hint")))),
		*MakeQAPanel(TEXT("qa_cb"), TEXT("ContainerB"), 260, 20,
			MakeQAText(TEXT("qa_bh"), TEXT("BHint"), 30, 80, TEXT("b-hint")))));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// 开发者定制：项目容器 DevPanel 挂进宿主根，把 N 挪进去。
	UVerticalBox* DevPanel = WBP->WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DevPanel"));
	if (UPanelWidget* Host = Cast<UPanelWidget>(WBP->WidgetTree->RootWidget))
	{
		Host->AddChild(DevPanel);
	}
	UWidget* N = WBP->WidgetTree->FindWidget(TEXT("N"));
	if (!TestNotNull(TEXT("N present before customization"), N) || !TestTrue(TEXT("DevPanel added"), DevPanel != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	WBP->WidgetTree->RemoveWidget(N);
	DevPanel->AddChild(N);
	const TWeakObjectPtr<UWidget> NWeak = N;

	// v2：设计把 N 挪到 ContainerB。
	const FString V2 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAPanel(TEXT("qa_ca"), TEXT("ContainerA"), 20, 20,
			MakeQAText(TEXT("qa_ah"), TEXT("AHint"), 30, 80, TEXT("a-hint"))),
		*MakeQAPanel(TEXT("qa_cb"), TEXT("ContainerB"), 260, 20, FString::Printf(
			TEXT("%s,%s"),
			*MakeQAText(TEXT("qa_n"), TEXT("N"), 30, 30, TEXT("mv")),
			*MakeQAText(TEXT("qa_bh"), TEXT("BHint"), 30, 80, TEXT("b-hint"))))));

	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport itself succeeds (conflict is per-node)"), Result.bSucceeded);

	UWidget* NAfter = NWeak.Get();
	if (TestNotNull(TEXT("N still alive"), NAfter))
	{
		TestEqual(TEXT("N kept under the project container DevPanel"),
			NAfter->GetParent()->GetFName(), FName(TEXT("DevPanel")));
	}
	TestNull(TEXT("no copy created under ContainerB"), WBP->WidgetTree->FindWidget(TEXT("N_1")));
	TestEqual(TEXT("nested move conflict diagnosed once"), QACountKind(Result, TEXT("reimport_move_conflict")), 1);
	TestTrue(TEXT("conflict names N and the project-container reason"),
		QAHasMessageWithAll(Result, TEXT("reimport_move_conflict"),
			{ TEXT("'N'"), TEXT("project-side container"), TEXT("no copy created") }));
	TestFalse(TEXT("N is not misreported as added"), QAHasMessage(Result, TEXT("reimport_added"), TEXT("'N'")));

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 26（integration，R3 嵌套删除冲突）：设计删除的嵌套节点 Tail 含项目子控件
// （DevChild）——Tail 保留 + reimport_remove_conflict 点名节点与原因；
// 纯 D2U 嵌套残留的静默保留由用例 20（NestedAddAndRemovalBoundary）覆盖。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQANestedRemoveConflictTest,
	"Design2UMG.SafeReimportQA.NestedRemoveConflictNamesNode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQANestedRemoveConflictTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportNestedRemoveConflict");

	const FString V1 = MakeQADoc(MakeQAPanel(TEXT("qa_panel"), TEXT("InfoPanel"), 24, 24, FString::Printf(
		TEXT("%s,%s"),
		*MakeQAText(TEXT("qa_head"), TEXT("Head"), 32, 32, TEXT("Head v1")),
		*MakeQAPanel(TEXT("qa_tail"), TEXT("Tail"), 32, 60,
			MakeQAText(TEXT("qa_tail_in"), TEXT("TailInner"), 40, 66, TEXT("inner"))))));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// 开发者定制：项目子控件 DevChild 挂进嵌套节点 Tail。
	UWidget* Tail = WBP->WidgetTree->FindWidget(TEXT("Tail"));
	if (!TestNotNull(TEXT("nested Tail present in v1"), Tail))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	UTextBlock* DevChild = WBP->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DevChild"));
	DevChild->SetText(FText::FromString(TEXT("dev")));
	Cast<UPanelWidget>(Tail)->AddChild(DevChild);

	// v2：Tail 从设计里消失（Head 保留并更新）。
	const FString V2 = MakeQADoc(MakeQAPanel(TEXT("qa_panel"), TEXT("InfoPanel"), 24, 24,
		MakeQAText(TEXT("qa_head"), TEXT("Head"), 32, 32, TEXT("Head v2"))));

	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);

	TestNotNull(TEXT("design-removed nested node preserved"), WBP->WidgetTree->FindWidget(TEXT("Tail")));
	TestNotNull(TEXT("project child stays inside the preserved node"),
		WBP->WidgetTree->FindWidget(TEXT("DevChild")));
	TestEqual(TEXT("nested remove conflict diagnosed once"), QACountKind(Result, TEXT("reimport_remove_conflict")), 1);
	TestTrue(TEXT("conflict names the node and the project-content reason"),
		QAHasMessageWithAll(Result, TEXT("reimport_remove_conflict"), { TEXT("'Tail'"), TEXT("DevChild") }));
	if (UTextBlock* Head = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Head"))))
	{
		TestEqual(TEXT("kept sibling still refreshed"), Head->GetText().ToString(), FString(TEXT("Head v2")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 27（integration，宿主根形态变化）：v1 带交互文档生成合成 switcher 宿主根
// （root_pc 成为顶层实例），v2 交互消失后 root_pc 直接成为 IR 根。reimport 不得
// 把 root_pc 的嵌套子节点误报为「宿主根顶层 added」（Keep_1），也不得把 root_pc
// 本身误报 move 冲突；被删除且含项目内容的嵌套节点 N 有 remove 冲突诊断。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAHostRootShapeChangeTest,
	"Design2UMG.SafeReimportQA.HostRootShapeChangeNoMisdiagnosis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAHostRootShapeChangeTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportHostRootShape");

	// v1：root_pc 下有带 ON_CLICK 交互的按钮（文档有效导航 -> 合成 switcher 根）。
	const auto MakeInteractiveButton = []()
	{
		return FString::Printf(
			TEXT("{\"id\":\"pc_btn\",\"name\":\"Btn\",\"type\":\"FRAME\","
				 "\"absoluteBoundingBox\":{\"x\":40,\"y\":40,\"width\":120,\"height\":44},"
				 "\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":0.2,\"g\":0.2,\"b\":0.2,\"a\":1}}],"
				 "\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"URL\",\"url\":\"https://qa.example/rm\"}]}],"
				 "\"children\":[{\"id\":\"pc_btn_t\",\"name\":\"BtnText\",\"type\":\"TEXT\","
				 "\"absoluteBoundingBox\":{\"x\":48,\"y\":48,\"width\":80,\"height\":20},\"characters\":\"press\",%s}]}"),
			*QATextStyle());
	};
	const auto MakeRoot = [&MakeInteractiveButton](bool bWithN)
	{
		const FString NBlock = FString::Printf(
			TEXT("{\"id\":\"pc_n\",\"name\":\"N\",\"type\":\"FRAME\","
				 "\"absoluteBoundingBox\":{\"x\":40,\"y\":40,\"width\":180,\"height\":120},"
				 "\"children\":[%s]},"),
			*MakeInteractiveButton());
		const FString KeepBlock = FString::Printf(
			TEXT("{\"id\":\"pc_other\",\"name\":\"Keep\",\"type\":\"TEXT\","
				 "\"absoluteBoundingBox\":{\"x\":260,\"y\":40,\"width\":120,\"height\":30},"
				 "\"characters\":\"keep\",%s}"),
			*QATextStyle());
		return FString::Printf(
			TEXT("{\"fileKey\":\"%s\",\"document\":{\"id\":\"0:0\",\"name\":\"Doc\",\"type\":\"DOCUMENT\",\"children\":["
				 "{\"id\":\"0:1\",\"name\":\"Page\",\"type\":\"CANVAS\",\"children\":["
				 "{\"id\":\"root_pc\",\"name\":\"RemoveProjChild\",\"type\":\"FRAME\","
				 "\"absoluteBoundingBox\":{\"x\":0,\"y\":0,\"width\":440,\"height\":240},"
				 "\"children\":[%s%s]}]}]}}"),
			QAReimportScope,
			bWithN ? *NBlock : TEXT(""),
			*KeepBlock);
	};

	UWidgetBlueprint* WBP = QAImport(MakeRoot(true), nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// 前置确认：合成 switcher 根 + root_pc 是顶层实例。
	if (!TestTrue(TEXT("v1 host root is the synthetic switcher"),
			WBP->WidgetTree->RootWidget && WBP->WidgetTree->RootWidget->IsA<UWidgetSwitcher>()))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	UWidget* RootPc = WBP->WidgetTree->FindWidget(TEXT("root_pc"));
	UWidget* N = WBP->WidgetTree->FindWidget(TEXT("N"));
	if (!TestNotNull(TEXT("root_pc is the page under the switcher"), RootPc)
		|| !TestNotNull(TEXT("nested N present in v1"), N))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	UTextBlock* DevChild = WBP->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DevChild"));
	DevChild->SetText(FText::FromString(TEXT("dev")));
	Cast<UPanelWidget>(N)->AddChild(DevChild);

	// v2：交互与 N 整体消失 -> 无有效导航 -> root_pc 直接成为根。
	const FDesignReimportResult Result = QAReimport(MakeRoot(false), nullptr, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);

	TestNull(TEXT("Keep is not duplicated onto the host root"), WBP->WidgetTree->FindWidget(TEXT("Keep_1")));
	TestNotNull(TEXT("Keep stays under root_pc"), WBP->WidgetTree->FindWidget(TEXT("Keep")));
	TestEqual(TEXT("no false top-level add"), QACountKind(Result, TEXT("reimport_added")), 0);
	TestEqual(TEXT("no false move conflict for the page root"), QACountKind(Result, TEXT("reimport_move_conflict")), 0);
	TestEqual(TEXT("N design-deletion reported as remove conflict"), QACountKind(Result, TEXT("reimport_remove_conflict")), 1);
	TestTrue(TEXT("remove conflict names N"),
		QAHasMessage(Result, TEXT("reimport_remove_conflict"), TEXT("'N'")));
	TestNotNull(TEXT("N preserved with its project child"), WBP->WidgetTree->FindWidget(TEXT("DevChild")));

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 28（integration，S1 保存前可写性 pre-check）：场景 B（仅 sidecar 只读）
// 与场景 A（uasset+sidecar 只读）都必须在进入保存流程前拦截——uasset 不落新状态、
// sidecar 不变、不报 completed、bSucceeded=false 且诊断写明具体文件。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQASaveWritabilityPrecheckTest,
	"Design2UMG.SafeReimportQA.SaveWritabilityPrecheckBlocksHalfUpdate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQASaveWritabilityPrecheckTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportWritability");

	const auto BuildDoc = [](const TCHAR* Text)
	{
		return MakeQADoc(MakeQAPanel(TEXT("qa_panel"), TEXT("InfoPanel"), 24, 24,
			MakeQAText(TEXT("qa_head"), TEXT("Head"), 32, 32, Text)));
	};

	UWidgetBlueprint* WBP = QAImport(BuildDoc(TEXT("fail-v1")), nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	const FString AssetFile = QAAssetFilePath(TargetPackage);
	const FString SidecarFile = Design2UMGGenerationManifest::BuildSidecarPathForAsset(TargetPackage);
	const FMD5Hash AssetHashBefore = FMD5Hash::HashFile(*AssetFile);
	FString SidecarBefore;
	if (!TestTrue(TEXT("v1 manifest exists"), QAReadFileString(SidecarFile, SidecarBefore)))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString V2 = BuildDoc(TEXT("fail-v2"));

	// 恢复可写的兜底（断言失败提前 return 时也不能把只读文件留给清理）。
	const auto MakeWritable = [&]()
	{
		FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*AssetFile, false);
		FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*SidecarFile, false);
	};

	// 场景 B：仅 sidecar 只读。pre-check 必须拦住保存：不得出现
	// 「uasset 已更新 + sidecar 还是旧值」的半更新组合。
	TestTrue(TEXT("sidecar set read-only"), FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*SidecarFile, true));
	{
		const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
		TestFalse(TEXT("scenario B reports failure"), Result.bSucceeded);
		TestFalse(TEXT("scenario B is not a full conflict"), Result.bFullConflict);
		TestEqual(TEXT("scenario B save failure diagnosed once"), QACountKind(Result, TEXT("reimport_save_failed")), 1);
		TestTrue(TEXT("scenario B diagnostic names the blocked sidecar file"),
			QAHasMessageWithAll(Result, TEXT("reimport_save_failed"), { *SidecarFile, TEXT("read-only") }));
		TestEqual(TEXT("scenario B does not report completion"), QACountKind(Result, TEXT("reimport_completed")), 0);
		// 组合断言：磁盘完全未被触碰（uasset hash 不变 + sidecar 字节不变）。
		TestTrue(TEXT("scenario B leaves the uasset untouched on disk"),
			FMD5Hash::HashFile(*AssetFile) == AssetHashBefore);
		FString SidecarNow;
		TestTrue(TEXT("scenario B sidecar still readable"), QAReadFileString(SidecarFile, SidecarNow));
		TestTrue(TEXT("scenario B sidecar untouched"), SidecarNow == SidecarBefore);
		if (UTextBlock* Head = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Head"))))
		{
			TestEqual(TEXT("scenario B in-memory text rolled back"), Head->GetText().ToString(), FString(TEXT("fail-v1")));
		}
	}
	FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*SidecarFile, false);

	// 场景 A：uasset + sidecar 都只读（Perforce 未 checkout 的完整形态）。
	TestTrue(TEXT("uasset set read-only"), FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*AssetFile, true));
	TestTrue(TEXT("sidecar set read-only again"), FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*SidecarFile, true));
	{
		const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
		TestFalse(TEXT("scenario A reports failure"), Result.bSucceeded);
		TestEqual(TEXT("scenario A save failure diagnosed once"), QACountKind(Result, TEXT("reimport_save_failed")), 1);
		TestTrue(TEXT("scenario A diagnostic names both blocked files"),
			QAHasMessage(Result, TEXT("reimport_save_failed"), *AssetFile)
			&& QAHasMessage(Result, TEXT("reimport_save_failed"), *SidecarFile));
		TestEqual(TEXT("scenario A does not report completion"), QACountKind(Result, TEXT("reimport_completed")), 0);
		TestTrue(TEXT("scenario A leaves the uasset untouched on disk"),
			FMD5Hash::HashFile(*AssetFile) == AssetHashBefore);
		FString SidecarNow;
		TestTrue(TEXT("scenario A sidecar still readable"), QAReadFileString(SidecarFile, SidecarNow));
		TestTrue(TEXT("scenario A sidecar untouched"), SidecarNow == SidecarBefore);
	}
	MakeWritable();

	// 恢复可写后同一 v2 重试可以成功（资产未被前两轮失败污染）。
	const FDesignReimportResult Retry = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("retry succeeds after restoring writability"), Retry.bSucceeded);
	if (UTextBlock* Head = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Head"))))
	{
		TestEqual(TEXT("retry applies the design update"), Head->GetText().ToString(), FString(TEXT("fail-v2")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 29（integration，S2 sidecar 写失败传导）：uasset 已保存但 manifest 写失败
// （pre-check 拦不住的 IO 失败）——不报 completed，输出 reimport_save_failed
// 说明资产与记录可能不一致，bSucceeded=false；内存保持新状态（磁盘已是新资产）。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAManifestWriteFailurePropagatesTest,
	"Design2UMG.SafeReimportQA.ManifestWriteFailurePropagates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAManifestWriteFailurePropagatesTest::RunTest(const FString&)
{
	AddExpectedError(TEXT("automation-injected sidecar write failure"), EAutomationExpectedErrorFlags::Contains, 0);

	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportManifestWriteFail");

	const auto BuildDoc = [](const TCHAR* Text)
	{
		return MakeQADoc(MakeQAPanel(TEXT("qa_panel"), TEXT("InfoPanel"), 24, 24,
			MakeQAText(TEXT("qa_head"), TEXT("Head"), 32, 32, Text)));
	};

	UWidgetBlueprint* WBP = QAImport(BuildDoc(TEXT("half-v1")), nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	const FString SidecarFile = Design2UMGGenerationManifest::BuildSidecarPathForAsset(TargetPackage);
	FString SidecarV1;
	if (!TestTrue(TEXT("v1 manifest exists"), QAReadFileString(SidecarFile, SidecarV1)))
	{
		QAWipeOutput(OutputDir);
		return false;
	}

	// 注入 sidecar 写失败（真实只读 sidecar 会被 pre-check 拦住，测不到该分支）。
	Design2UMGGenerationManifest::ArmAutomationSidecarWriteFailureOnce();
	const FDesignReimportResult Result = QAReimport(BuildDoc(TEXT("half-v2")), nullptr, TargetPackage);

	TestFalse(TEXT("manifest write failure is not reported as success"), Result.bSucceeded);
	TestFalse(TEXT("not a full conflict"), Result.bFullConflict);
	TestEqual(TEXT("save failure diagnosed once"), QACountKind(Result, TEXT("reimport_save_failed")), 1);
	TestTrue(TEXT("diagnostic states asset/manifest inconsistency"),
		QAHasMessageWithAll(Result, TEXT("reimport_save_failed"),
			{ TEXT("was saved"), TEXT("manifest could not be written"), TEXT("inconsistent") }));
	TestTrue(TEXT("underlying manifest_write_failed surfaces in the report"),
		QACountKind(Result, TEXT("manifest_write_failed")) >= 1);
	TestEqual(TEXT("completion not reported"), QACountKind(Result, TEXT("reimport_completed")), 0);

	// uasset 已保存新状态：内存与磁盘一致（不回滚成旧值）。
	if (UTextBlock* Head = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Head"))))
	{
		TestEqual(TEXT("in-memory text stays at the saved v2 value"), Head->GetText().ToString(), FString(TEXT("half-v2")));
	}
	FString SidecarAfter;
	TestTrue(TEXT("sidecar still readable"), QAReadFileString(SidecarFile, SidecarAfter));
	TestTrue(TEXT("sidecar keeps the stale v1 content (inconsistency is reported, not hidden)"),
		SidecarAfter == SidecarV1);

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 30（integration，S4 mapped 交互专门诊断）：挂在 mapped 组件上的设计交互
// 在 reimport 时也输出专门「不生效」条目（mapping_prototype_inert，点名触发点与
// 类型），不只有通用的 mapped-refresh 条目。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAMappedInteractionInertTest,
	"Design2UMG.SafeReimportQA.MappedInteractionInertDiagnostic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAMappedInteractionInertTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportMappedInert");

	// mapped 按钮 + ON_CLICK 设计交互（挂在 mapped 实例本体上）。
	const auto BuildDoc = [](const TCHAR* Label)
	{
		return FString::Printf(
			TEXT("{\"fileKey\":\"%s\",\"document\":{\"id\":\"0:0\",\"name\":\"QADoc\",\"type\":\"DOCUMENT\",\"children\":["
				 "{\"id\":\"0:1\",\"name\":\"Page\",\"type\":\"CANVAS\",\"children\":["
				 "{\"id\":\"qa_root\",\"name\":\"QAPage\",\"type\":\"FRAME\","
				 "\"absoluteBoundingBox\":{\"x\":0,\"y\":0,\"width\":360,\"height\":240},\"children\":["
				 "{\"id\":\"qa_mib\",\"name\":\"HeroButton\",\"type\":\"INSTANCE\","
				 "\"componentId\":\"77:88\",\"componentSetId\":\"99:00\",\"mainComponentKey\":\"%s\","
				 "\"componentProperties\":{\"label\":{\"type\":\"TEXT\",\"value\":\"%s\"}},"
				 "\"absoluteBoundingBox\":{\"x\":24,\"y\":24,\"width\":120,\"height\":44},"
				 "\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":0.2,\"g\":0.5,\"b\":0.8,\"a\":1}}],"
				 "\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"URL\",\"url\":\"https://qa.example/inert\"}]}],"
				 "\"children\":[{\"id\":\"qa_mib_label\",\"name\":\"InnerLabel\",\"type\":\"TEXT\","
				 "\"absoluteBoundingBox\":{\"x\":32,\"y\":36,\"width\":80,\"height\":18},\"characters\":\"%s\",%s}]}"
				 "]}]}]}}"),
			QAReimportScope, QAReimportComponentKey, Label, Label, *QATextStyle());
	};

	UWidgetBlueprint* WBP = QAImport(BuildDoc(TEXT("Checkout")), QAMappedClassPath, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr)
		|| !TestNotNull(TEXT("mapped instance generated"), QAFindMappedInstance(WBP)))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	const FDesignReimportResult Result = QAReimport(BuildDoc(TEXT("Pay Now")), QAMappedClassPath, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);

	TestTrue(TEXT("dedicated inert diagnostic present and names trigger + type"),
		QAHasMappingMessageWithAll(Result, TEXT("mapping_prototype_inert"),
			{ TEXT("qa_mib"), TEXT("ON_CLICK"), TEXT("mapped WBP interactions are project-owned") }));

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 31（integration，S5 嵌套更新诊断粒度）：嵌套 D2U-owned 节点的属性刷新
// 也发 reimport_updated 点名节点（诊断粒度与顶层一致），不只是顶层实例。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQANestedUpdateDiagnosticTest,
	"Design2UMG.SafeReimportQA.NestedUpdateDiagnosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQANestedUpdateDiagnosticTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportNestedUpdateDiag");

	const auto BuildDoc = [](const TCHAR* Text)
	{
		return MakeQADoc(MakeQAPanel(TEXT("qa_panel"), TEXT("InfoPanel"), 24, 24,
			MakeQAText(TEXT("qa_t"), TEXT("Status"), 32, 32, Text)));
	};

	UWidgetBlueprint* WBP = QAImport(BuildDoc(TEXT("life-v1")), nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	const FDesignReimportResult Result = QAReimport(BuildDoc(TEXT("life-v2")), nullptr, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);

	TestTrue(TEXT("nested node update diagnosed with node name and design id"),
		QAHasMessageWithAll(Result, TEXT("reimport_updated"), { TEXT("Status"), TEXT("qa_t") }));
	if (UTextBlock* Status = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Status"))))
	{
		TestEqual(TEXT("nested text actually refreshed"), Status->GetText().ToString(), FString(TEXT("life-v2")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 32（integration，切片 C C1）：多页原型文档（合成 switcher 宿主根）下，
// mapped 实例在 v2 获得交互（IR 侧同名节点变成以节点 id 命名的触发包装形态）。
// reimport 更新计划必须把它按 Matched 路径处理：实例不被重复创建、无按 id 命名的
// 副本、sidecar 不出现同 designNodeId 双记录、无「内容位被占用」的误报 move 冲突。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQASwitcherMappedNoDuplicateTest,
	"Design2UMG.SafeReimportQA.SwitcherHostMappedNoDuplicate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQASwitcherMappedNoDuplicateTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportSwitcherMapped");

	// mapped 实例：v1 无交互；v2 获得 ON_CLICK NODE 导航（变成触发包装形态）。
	const auto MakeMappedButton = [](const TCHAR* Label, const bool bWithInteraction)
	{
		const FString Interactions = bWithInteraction
			? TEXT(",\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"c41_pagea\",\"navigation\":\"NAVIGATE\"}]}]")
			: FString();
		return FString::Printf(
			TEXT("{\"id\":\"qa_btn_1\",\"name\":\"PrimaryButton\",\"type\":\"INSTANCE\","
				 "\"componentId\":\"77:88\",\"componentSetId\":\"99:00\",\"mainComponentKey\":\"%s\","
				 "\"componentProperties\":{\"label\":{\"type\":\"TEXT\",\"value\":\"%s\"}},"
				 "\"absoluteBoundingBox\":{\"x\":24,\"y\":24,\"width\":120,\"height\":44},"
				 "\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":0.2,\"g\":0.5,\"b\":0.8,\"a\":1}}]%s}"),
			QAReimportComponentKey, Label, *Interactions);
	};
	// PageA 内的文本带跨页导航（让文档成为多页原型文档，v1/v2 都合成 switcher 宿主根）。
	const auto MakePageAText = []()
	{
		return FString::Printf(
			TEXT("{\"id\":\"c41_pat\",\"name\":\"PageAText\",\"type\":\"TEXT\","
				 "\"absoluteBoundingBox\":{\"x\":40,\"y\":40,\"width\":90,\"height\":20},\"characters\":\"to-main\",%s,"
				 "\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"c41_page\",\"navigation\":\"NAVIGATE\"}]}]}"),
			*QATextStyle());
	};
	const auto MakeSwitcherDoc = [&MakeMappedButton, &MakePageAText](const bool bButtonInteractive)
	{
		return FString::Printf(
			TEXT("{\"fileKey\":\"%s\",\"document\":{\"id\":\"0:0\",\"name\":\"Doc\",\"type\":\"DOCUMENT\",\"children\":["
				 "{\"id\":\"0:1\",\"name\":\"Page\",\"type\":\"CANVAS\",\"children\":["
				 "{\"id\":\"c41_page\",\"name\":\"MainPage\",\"type\":\"FRAME\","
				 "\"absoluteBoundingBox\":{\"x\":0,\"y\":0,\"width\":360,\"height\":240},"
				 "\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":0.9,\"g\":0.9,\"b\":0.9,\"a\":1}}],"
				 "\"children\":[%s]},"
				 "{\"id\":\"c41_pagea\",\"name\":\"PageA\",\"type\":\"FRAME\","
				 "\"absoluteBoundingBox\":{\"x\":400,\"y\":0,\"width\":360,\"height\":240},"
				 "\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":0.8,\"g\":0.8,\"b\":0.8,\"a\":1}}],"
				 "\"children\":[%s]}]}]}}"),
			QAReimportScope,
			*MakeMappedButton(bButtonInteractive ? TEXT("proto-v2") : TEXT("proto-v1"), bButtonInteractive),
			*MakePageAText());
	};

	UWidgetBlueprint* WBP = QAImport(MakeSwitcherDoc(false), QAMappedClassPath, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	if (!TestTrue(TEXT("v1 host root is the synthetic switcher"),
			WBP->WidgetTree->RootWidget && WBP->WidgetTree->RootWidget->IsA<UWidgetSwitcher>()))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	UDesign2UMGMappingQAWidget* MappedBefore = QAFindMappedInstance(WBP);
	if (!TestNotNull(TEXT("v1 mapped instance generated"), MappedBefore)
		|| !TestEqual(TEXT("v1 mapped instance keeps its design name"), MappedBefore->GetFName(), FName(TEXT("PrimaryButton")))
		|| !TestTrue(TEXT("v1 mapped instance sits under the page inner layer"),
			MappedBefore->GetParent() && MappedBefore->GetParent()->GetFName() == FName(TEXT("MainPage_inner"))))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const TWeakObjectPtr<UDesign2UMGMappingQAWidget> MappedWeak = MappedBefore;

	const FDesignReimportResult Result = QAReimport(MakeSwitcherDoc(true), QAMappedClassPath, TargetPackage);
	TestTrue(TEXT("v2 reimport succeeds"), Result.bSucceeded);

	// 唯一实例 + identity 保持 + 名字不变（不出现按节点 id 命名的副本）。
	int32 MappedInstanceCount = 0;
	UDesign2UMGMappingQAWidget* MappedAfter = nullptr;
	WBP->WidgetTree->ForEachWidget([&MappedInstanceCount, &MappedAfter](UWidget* Widget)
	{
		if (UDesign2UMGMappingQAWidget* Candidate = Cast<UDesign2UMGMappingQAWidget>(Widget))
		{
			++MappedInstanceCount;
			MappedAfter = Candidate;
		}
	});
	TestEqual(TEXT("exactly one mapped instance in the tree"), MappedInstanceCount, 1);
	TestNull(TEXT("no duplicate named after the design node id"),
		WBP->WidgetTree->FindWidget(TEXT("qa_btn_1")));
	if (TestNotNull(TEXT("mapped instance still alive"), MappedAfter))
	{
		TestTrue(TEXT("mapped instance object identity preserved"), MappedAfter == MappedWeak.Get());
		TestEqual(TEXT("mapped instance name unchanged"), MappedAfter->GetFName(), FName(TEXT("PrimaryButton")));
		// Matched 路径的 inputs 直接覆写。
		TestEqual(TEXT("declared input overwritten with the v2 design value"),
			MappedAfter->TextValue.ToString(), FString(TEXT("proto-v2")));
	}

	// 诊断：刷新点名；无 reimport_added（不重复创建）；无误报 move 冲突。
	TestTrue(TEXT("mapped refresh diagnosed"),
		QAHasMessageWithAll(Result, TEXT("reimport_updated"), { TEXT("PrimaryButton"), TEXT("qa_btn_1") }));
	TestEqual(TEXT("no added duplicate"), QACountKind(Result, TEXT("reimport_added")), 0);
	TestEqual(TEXT("no false move conflict from the synthetic inner layer"),
		QACountKind(Result, TEXT("reimport_move_conflict")), 0);

	// sidecar：同 designNodeId 只有一条 mapped 记录。
	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads"), QALoadSidecar(TargetPackage, Sidecar)))
	{
		int32 RecordsForButton = 0;
		const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* ButtonRecord = nullptr;
		for (const FDesign2UMGGenerationManifestSidecar::FInstanceRecord& Record : Sidecar.Instances)
		{
			if (Record.DesignNodeId == TEXT("qa_btn_1"))
			{
				++RecordsForButton;
				ButtonRecord = &Record;
			}
		}
		TestEqual(TEXT("single manifest record for the design node"), RecordsForButton, 1);
		if (TestNotNull(TEXT("mapped record present"), ButtonRecord))
		{
			TestEqual(TEXT("record kind mapped"), ButtonRecord->Kind, FString(TEXT("mapped")));
			TestEqual(TEXT("record widget name preserved"), ButtonRecord->WidgetName, FString(TEXT("PrimaryButton")));
		}
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 33（integration，切片 C C2）：canvas 容器在 v2 获得视觉填充后，转换器给它
// 插入合成 inner 层（Border + inner canvas）。容器既有的嵌套子节点与新挪进来的
// 节点都必须穿透合成层完成同名匹配：M 原地刷新、N 从 A 挪入 C（identity 保持），
// 不产生重复副本、不误报新增。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQACompositeLayerNestedMoveTest,
	"Design2UMG.SafeReimportQA.CompositeLayerNestedMove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQACompositeLayerNestedMoveTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportCompositeLayerMove");

	// C：无 auto-layout 容器；bWithFill 决定本版是否合成 inner 层。A：VERTICAL 容器。
	const auto MakeBox = [](const TCHAR* Id, const TCHAR* Name, const bool bWithFill, const bool bVertical, const FString& Children)
	{
		const FString Fills = bWithFill
			? TEXT("\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":0.9,\"g\":0.9,\"b\":0.9,\"a\":1}}],")
			: FString();
		const FString Layout = bVertical ? TEXT("\"layoutMode\":\"VERTICAL\",") : FString();
		return FString::Printf(
			TEXT("{\"id\":\"%s\",\"name\":\"%s\",\"type\":\"FRAME\","
				 "\"absoluteBoundingBox\":{\"x\":20,\"y\":20,\"width\":200,\"height\":120},%s%s\"children\":[%s]}"),
			Id, Name, *Fills, *Layout, *Children);
	};

	// v1：C 无填充（无 inner 层）直接装 M；A（VERTICAL）装 N。
	const FString V1 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeBox(TEXT("qa_c"), TEXT("C"), false, false, MakeQAText(TEXT("qa_m"), TEXT("M"), 40, 40, TEXT("m-v1"))),
		*MakeBox(TEXT("qa_a"), TEXT("A"), true, true, MakeQAText(TEXT("qa_n"), TEXT("N"), 40, 40, TEXT("mv")))));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	UTextBlock* MBefore = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("M")));
	UTextBlock* NBefore = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("N")));
	UWidget* CBefore = WBP->WidgetTree->FindWidget(TEXT("C"));
	if (!TestNotNull(TEXT("M present in v1"), MBefore)
		|| !TestNotNull(TEXT("N present in v1"), NBefore)
		|| !TestNotNull(TEXT("C present in v1"), CBefore)
		|| !TestTrue(TEXT("v1 M sits directly under C (no synthetic layer)"),
			MBefore->GetParent() && MBefore->GetParent()->GetFName() == FName(TEXT("C"))))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const TWeakObjectPtr<UTextBlock> MWeak = MBefore;
	const TWeakObjectPtr<UTextBlock> NWeak = NBefore;

	// v2：C 获得填充（转换器插入合成 inner 层）且 N 从 A 挪入 C。
	const FString V2 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeBox(TEXT("qa_c"), TEXT("C"), true, false, FString::Printf(
			TEXT("%s,%s"),
			*MakeQAText(TEXT("qa_m"), TEXT("M"), 40, 40, TEXT("m-v2")),
			*MakeQAText(TEXT("qa_n"), TEXT("N"), 40, 80, TEXT("mv")))),
		*MakeBox(TEXT("qa_a"), TEXT("A"), true, true, FString())));

	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("v2 reimport succeeds"), Result.bSucceeded);

	UTextBlock* MAfter = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("M")));
	UTextBlock* NAfter = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("N")));
	if (TestNotNull(TEXT("M still present"), MAfter))
	{
		TestTrue(TEXT("M identity preserved through the synthetic layer"), MAfter == MWeak.Get());
		TestEqual(TEXT("M keeps its parent C"), MAfter->GetParent()->GetFName(), FName(TEXT("C")));
		TestEqual(TEXT("M refreshed from the design"), MAfter->GetText().ToString(), FString(TEXT("m-v2")));
	}
	if (TestNotNull(TEXT("N still present"), NAfter))
	{
		TestTrue(TEXT("N identity preserved by the nested move"), NAfter == NWeak.Get());
		TestEqual(TEXT("N moved under C"), NAfter->GetParent()->GetFName(), FName(TEXT("C")));
		TestEqual(TEXT("moved N keeps its design text"), NAfter->GetText().ToString(), FString(TEXT("mv")));
	}
	TestNull(TEXT("no duplicate M created"), WBP->WidgetTree->FindWidget(TEXT("M_1")));
	TestNull(TEXT("no duplicate N created"), WBP->WidgetTree->FindWidget(TEXT("N_1")));
	TestTrue(TEXT("nested move diagnosed with identity preservation"),
		QAHasMessageWithAll(Result, TEXT("reimport_moved"), { TEXT("'N'"), TEXT("widget identity preserved") }));
	TestFalse(TEXT("N is not misreported as added"),
		QAHasMessage(Result, TEXT("reimport_added"), TEXT("'N'")));

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 34（integration，切片 C C3）：单页 canvas root（带填充 -> 宿主根是 Border +
// 合成 inner 层，manifest 顶层实例记录的是 inner 层）删除页面唯一子树 N 时，
// 安全删除诊断必须同时点名被删子树里的设计节点（widget 名由设计节点名确定性
// 派生），不能只引用宿主合成层。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAEmptyRootRemovalNamesDesignNodesTest,
	"Design2UMG.SafeReimportQA.EmptyRootRemovalNamesDesignNodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAEmptyRootRemovalNamesDesignNodesTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportEmptyRootRemoval");

	const auto MakeFilledRootDoc = [](const FString& Children)
	{
		return FString::Printf(
			TEXT("{\"fileKey\":\"%s\",\"document\":{\"id\":\"0:0\",\"name\":\"Doc\",\"type\":\"DOCUMENT\",\"children\":["
				 "{\"id\":\"0:1\",\"name\":\"Page\",\"type\":\"CANVAS\",\"children\":["
				 "{\"id\":\"qa_root\",\"name\":\"QAPage\",\"type\":\"FRAME\","
				 "\"absoluteBoundingBox\":{\"x\":0,\"y\":0,\"width\":360,\"height\":240},"
				 "\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":0.9,\"g\":0.9,\"b\":0.9,\"a\":1}}],"
				 "\"children\":[%s]}]}]}}"),
			QAReimportScope, *Children);
	};
	// 页面唯一子树：N（VERTICAL 带填充 -> Border）内含 Caption 文本。
	const FString NSubtree = FString::Printf(
		TEXT("{\"id\":\"qa_n\",\"name\":\"N\",\"type\":\"FRAME\",\"layoutMode\":\"VERTICAL\","
			 "\"absoluteBoundingBox\":{\"x\":40,\"y\":40,\"width\":180,\"height\":120},"
			 "\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":0.8,\"g\":0.8,\"b\":0.8,\"a\":1}}],"
			 "\"children\":[%s]}"),
		*MakeQAText(TEXT("qa_cap"), TEXT("Caption"), 48, 48, TEXT("cap")));

	UWidgetBlueprint* WBP = QAImport(MakeFilledRootDoc(NSubtree), nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	if (!TestNotNull(TEXT("v1 N present"), WBP->WidgetTree->FindWidget(TEXT("N")))
		|| !TestNotNull(TEXT("v1 Caption present"), WBP->WidgetTree->FindWidget(TEXT("Caption")))
		|| !TestNotNull(TEXT("v1 inner layer present"), WBP->WidgetTree->FindWidget(TEXT("QAPage_inner"))))
	{
		QAWipeOutput(OutputDir);
		return false;
	}

	// v2：删除唯一子树，页面变空 root。
	const FDesignReimportResult Result = QAReimport(MakeFilledRootDoc(FString()), nullptr, TargetPackage);
	TestTrue(TEXT("v2 reimport succeeds"), Result.bSucceeded);

	TestNull(TEXT("N removed"), WBP->WidgetTree->FindWidget(TEXT("N")));
	TestNull(TEXT("Caption removed"), WBP->WidgetTree->FindWidget(TEXT("Caption")));
	TestNull(TEXT("inner layer removed"), WBP->WidgetTree->FindWidget(TEXT("QAPage_inner")));

	// 诊断粒度：宿主合成层之外必须点名设计节点（N、Caption）。
	TestEqual(TEXT("one removal diagnostic"), QACountKind(Result, TEXT("reimport_removed")), 1);
	TestTrue(TEXT("removal diagnostic names the synthetic host layer and the design nodes"),
		QAHasMessageWithAll(Result, TEXT("reimport_removed"),
			{ TEXT("QAPage_inner"), TEXT("N"), TEXT("Caption"), TEXT("removed through the safe delete flow") }));

	// 空页面是合法终态：sidecar 写出 0 实例。切片 B 语义要求加载侧拒绝空实例
	// sidecar（下一轮 reimport 会按关系不清走全量冲突），所以这里直接读原始 JSON
	// 断言记录确实清空，而不是走 TryLoadSidecarForAsset。
	FString RawSidecar;
	if (TestTrue(TEXT("sidecar file readable"), QAReadFileString(Design2UMGGenerationManifest::BuildSidecarPathForAsset(TargetPackage), RawSidecar)))
	{
		TSharedPtr<FJsonObject> SidecarRoot;
		if (TestTrue(TEXT("sidecar JSON parses"), QAParseJsonObject(RawSidecar, SidecarRoot)))
		{
			const TArray<TSharedPtr<FJsonValue>>* Instances = nullptr;
			if (TestTrue(TEXT("instances array present"), SidecarRoot->TryGetArrayField(TEXT("instances"), Instances) && Instances != nullptr))
			{
				TestEqual(TEXT("inner layer instance dropped from the manifest"), Instances->Num(), 0);
			}
			const TArray<TSharedPtr<FJsonValue>>* Owned = nullptr;
			if (TestTrue(TEXT("d2uOwned array present"), SidecarRoot->TryGetArrayField(TEXT("d2uOwned"), Owned) && Owned != nullptr))
			{
				// 宿主根 Border 本来就不进 d2uOwned（生成链不记录根 widget），
				// 唯一子树删除后 owned 名单清空是合法终态。
				TArray<FString> OwnedNames;
				for (const TSharedPtr<FJsonValue>& OwnedValue : *Owned)
				{
					OwnedNames.Add(OwnedValue->AsString());
				}
				TestFalse(TEXT("N dropped from d2uOwned"), OwnedNames.Contains(TEXT("N")));
				TestFalse(TEXT("Caption dropped from d2uOwned"), OwnedNames.Contains(TEXT("Caption")));
				TestFalse(TEXT("inner layer dropped from d2uOwned"), OwnedNames.Contains(TEXT("QAPage_inner")));
			}
		}
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 35（integration，切片 C 5.1 连续生命周期）：v1 导入 -> 开发者经 C4 通道给
// 触发按钮加真实事件绑定（FDelegateEditorBinding + DevHandler 函数图）并加项目子
// 控件 -> v2 reimport（断言项目逻辑仍在、设计更新生效、D2U 绑定按双触发让位）
// -> v3 reimport（断言 Widget 关系与 identity 稳定、绑定仍在、manifest 一致）。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAContinuousLifecycleBindingsTest,
	"Design2UMG.SafeReimportQA.ContinuousLifecycleWithBindings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAContinuousLifecycleBindingsTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportLifecycleBindings");

	const auto MakeLifeDoc = [](const TCHAR* ButtonLabel, const TCHAR* HeadText)
	{
		return MakeQADoc(FString::Printf(
			TEXT("%s,%s,%s"),
			*MakeQAButton(TEXT("qa_btn_1"), TEXT("HeroButton"), 24, 24, ButtonLabel),
			*MakeQAPanel(TEXT("qa_panel"), TEXT("InfoPanel"), 180, 24,
				MakeQAText(TEXT("qa_head"), TEXT("Head"), 188, 32, HeadText)),
			*MakeQAInteractive(TEXT("qa_link"), TEXT("JumpLink"), 24, TEXT("ON_CLICK"), TEXT("https://qa.example/jump"))));
	};

	UWidgetBlueprint* WBP = QAImport(MakeLifeDoc(TEXT("life-v1"), TEXT("head-v1")), QAMappedClassPath, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	UWidget* LinkButton = WBP->WidgetTree->FindWidget(TEXT("qa_link"));
	UDesign2UMGMappingQAWidget* MappedBefore = QAFindMappedInstance(WBP);
	UTextBlock* HeadBefore = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Head")));
	if (!TestNotNull(TEXT("trigger button widget present"), LinkButton)
		|| !TestNotNull(TEXT("mapped instance present"), MappedBefore)
		|| !TestNotNull(TEXT("Head text present"), HeadBefore))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const TWeakObjectPtr<UWidget> LinkWeak = LinkButton;
	const TWeakObjectPtr<UDesign2UMGMappingQAWidget> MappedWeak = MappedBefore;
	const TWeakObjectPtr<UTextBlock> HeadWeak = HeadBefore;

	// 开发者定制：C4 通道加真实事件绑定（函数图 + FDelegateEditorBinding）。
	FString BindingError;
	if (!TestTrue(TEXT("C4 channel adds the event binding"),
			Design2UMGWidgetBindingTools::AddEventBinding(
				WBP, TEXT("qa_link"), TEXT("OnClicked"), TEXT("DevHandler"), BindingError)))
	{
		AddError(FString::Printf(TEXT("AddEventBinding failed: %s"), *BindingError));
		QAWipeOutput(OutputDir);
		return false;
	}
	// 幂等：同一绑定重复添加不产生第二条。
	TestTrue(TEXT("AddEventBinding is idempotent"),
		Design2UMGWidgetBindingTools::AddEventBinding(
			WBP, TEXT("qa_link"), TEXT("OnClicked"), TEXT("DevHandler"), BindingError));
	int32 BindingCount = 0;
	for (const FDelegateEditorBinding& Binding : WBP->Bindings)
	{
		BindingCount += (Binding.ObjectName == TEXT("qa_link") && Binding.PropertyName == TEXT("OnClicked")) ? 1 : 0;
	}
	TestEqual(TEXT("exactly one editor binding for the trigger"), BindingCount, 1);

	UTextBlock* ProjectNote = WBP->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ProjectNote"));
	if (UPanelWidget* InfoPanel = Cast<UPanelWidget>(WBP->WidgetTree->FindWidget(TEXT("InfoPanel"))))
	{
		InfoPanel->AddChild(ProjectNote);
	}

	// v2 reimport：设计值更新 + 项目逻辑保留 + 双触发让位。
	const FDesignReimportResult ResultV2 = QAReimport(MakeLifeDoc(TEXT("life-v2"), TEXT("head-v2")), QAMappedClassPath, TargetPackage);
	TestTrue(TEXT("v2 reimport succeeds"), ResultV2.bSucceeded);

	const bool bV2HasDevBinding = WBP->Bindings.ContainsByPredicate([](const FDelegateEditorBinding& Binding)
	{
		return Binding.ObjectName == TEXT("qa_link")
			&& Binding.PropertyName == TEXT("OnClicked")
			&& Binding.FunctionName == TEXT("DevHandler");
	});
	TestTrue(TEXT("project event binding preserved across v2 reimport"), bV2HasDevBinding);
	TestNotNull(TEXT("DevHandler function still resolvable"),
		WBP->SkeletonGeneratedClass ? WBP->SkeletonGeneratedClass->FindFunctionByName(TEXT("DevHandler")) : nullptr);
	TestNotNull(TEXT("project-added child preserved"), WBP->WidgetTree->FindWidget(TEXT("ProjectNote")));
	TestTrue(TEXT("D2U click binding yields to the project binding (dual trigger)"),
		QAHasMessageWithAll(ResultV2, TEXT("reimport_prototype_conflict"),
			{ TEXT("trigger 'qa_link'"), TEXT("project behavior kept") }));
	if (UDesign2UMGMappingQAWidget* MappedV2 = Cast<UDesign2UMGMappingQAWidget>(MappedWeak.Get()))
	{
		TestTrue(TEXT("mapped identity preserved across v2"), MappedV2 == MappedBefore);
		TestEqual(TEXT("mapped input refreshed in v2"), MappedV2->TextValue.ToString(), FString(TEXT("life-v2")));
	}
	if (UTextBlock* HeadV2 = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Head"))))
	{
		TestEqual(TEXT("D2U text refreshed in v2"), HeadV2->GetText().ToString(), FString(TEXT("head-v2")));
	}

	// v3 reimport：设计属性再变一轮，断言关系/identity/绑定全部稳定。
	const FDesignReimportResult ResultV3 = QAReimport(MakeLifeDoc(TEXT("life-v3"), TEXT("head-v3")), QAMappedClassPath, TargetPackage);
	TestTrue(TEXT("v3 reimport succeeds"), ResultV3.bSucceeded);

	UWidget* LinkV3 = WBP->WidgetTree->FindWidget(TEXT("qa_link"));
	if (TestNotNull(TEXT("trigger button still present after v3"), LinkV3))
	{
		TestTrue(TEXT("trigger button identity stable across both reimports"), LinkV3 == LinkWeak.Get());
	}
	if (UDesign2UMGMappingQAWidget* MappedV3 = Cast<UDesign2UMGMappingQAWidget>(MappedWeak.Get()))
	{
		TestTrue(TEXT("mapped identity stable across both reimports"), MappedV3 == MappedBefore);
		TestEqual(TEXT("mapped input refreshed in v3"), MappedV3->TextValue.ToString(), FString(TEXT("life-v3")));
	}
	if (UTextBlock* HeadV3 = Cast<UTextBlock>(HeadWeak.Get()))
	{
		TestTrue(TEXT("Head identity stable across both reimports"), HeadV3 == WBP->WidgetTree->FindWidget(TEXT("Head")));
		TestEqual(TEXT("D2U text refreshed in v3"), HeadV3->GetText().ToString(), FString(TEXT("head-v3")));
		TestTrue(TEXT("Head parent relationship stable"),
			HeadV3->GetParent() && HeadV3->GetParent()->GetFName() == FName(TEXT("InfoPanel")));
	}
	const bool bV3HasDevBinding = WBP->Bindings.ContainsByPredicate([](const FDelegateEditorBinding& Binding)
	{
		return Binding.ObjectName == TEXT("qa_link")
			&& Binding.PropertyName == TEXT("OnClicked")
			&& Binding.FunctionName == TEXT("DevHandler");
	});
	TestTrue(TEXT("project event binding preserved across v3 reimport"), bV3HasDevBinding);
	TestNull(TEXT("no duplicate Head created"), WBP->WidgetTree->FindWidget(TEXT("Head_1")));
	TestNull(TEXT("no duplicate trigger created"), WBP->WidgetTree->FindWidget(TEXT("qa_link_1")));

	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads after v3"), QALoadSidecar(TargetPackage, Sidecar)))
	{
		TestNotNull(TEXT("mapped instance still recorded"), Sidecar.FindInstanceByDesignNodeId(TEXT("qa_btn_1")));
		// 带交互文档的宿主根是合成 switcher：页面根 qa_root 是顶层实例；
		// InfoPanel 等是页面内的嵌套节点，不进顶层实例记录。
		TestNotNull(TEXT("page instance still recorded"), Sidecar.FindInstanceByDesignNodeId(TEXT("qa_root")));
		TestFalse(TEXT("project child stays outside d2uOwned"), Sidecar.D2UOwned.Contains(TEXT("ProjectNote")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 35b（integration）：reimport 报告的 componentMapping 章节——mapping 配置
// 错误（ini 校验诊断 + 映射类不可加载回退）在 reimport 路径必须与导入路径同样可见：
// 结果的 ComponentMappingDiagnostics 与落盘报告 JSON 的 componentMapping.entries 都要含对应条目。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAMappingDiagnosticsSurfacedTest,
	"Design2UMG.SafeReimportQA.MappingDiagnosticsSurfaced",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAMappingDiagnosticsSurfacedTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportMappingDiagnostics");

	const FString V1 = MakeQADoc(MakeQAButton(TEXT("qa_btn_1"), TEXT("HeroButton"), 24, 24, TEXT("Checkout")));
	UWidgetBlueprint* WBP = QAImport(V1, QAMappedClassPath, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// 报告文件按 reimport 前后目录差集定位（文件名含时间戳 + GUID，无法预知）。
	const FString ReportDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), TEXT("ImportReports"));
	TSet<FString> ReportsBefore;
	{
		TArray<FString> Found;
		IFileManager::Get().FindFiles(Found, *(ReportDir / TEXT("*.import_report.json")), true, false);
		for (const FString& FileName : Found)
		{
			ReportsBefore.Add(FileName);
		}
	}

	// v2 reimport：映射表指向不可加载类（ini 配置错误的类加载失败形态），
	// 同时随快照携带一条 ini 校验诊断（Load() 在非法条目上产出的同类载荷）。
	const TCHAR* BrokenClassPath = TEXT("/Script/Design2UMGEditor.NoSuchMappedWidgetClass");
	FDesignDocument Document;
	if (!TestTrue(TEXT("v2 source parses"), FFigmaParser::ParseDocument(V1, Document)))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	FDesignReimportRequest Request;
	Request.TargetAssetPath = TargetPackage;
	Request.SettingsSnapshot = MakeQASnapshot(BrokenClassPath);
	{
		FDesign2UMGMappingDiagnostic ValidationDiagnostic;
		ValidationDiagnostic.Kind = TEXT("mapping_config_invalid");
		ValidationDiagnostic.Message = TEXT("Mapping 'figma|qa-reimport-doc-9f31|qa-reimport-comp-key' dropped (QA seeded)");
		Request.SettingsSnapshot->GenerationSettings.ComponentMapping.ValidationDiagnostics.Add(
			MoveTemp(ValidationDiagnostic));
	}
	const FDesignReimportResult Result = DesignImportPipeline::Reimport(Document, Request);
	TestTrue(TEXT("reimport itself succeeds (class fallback is a per-node path)"), Result.bSucceeded);

	// 结果出口：ini 校验诊断 + BuildState 收集的匹配/回退诊断都在 ComponentMappingDiagnostics。
	auto HasMappingDiagnostic = [&Result](const TCHAR* Kind, const TCHAR* Substring = nullptr)
	{
		for (const FDesign2UMGMappingDiagnostic& Diagnostic : Result.ComponentMappingDiagnostics)
		{
			if (Diagnostic.Kind == Kind && (!Substring || Diagnostic.Message.Contains(Substring)))
			{
				return true;
			}
		}
		return false;
	};
	TestTrue(TEXT("seeded ini validation diagnostic transported"),
		HasMappingDiagnostic(TEXT("mapping_config_invalid"), TEXT("QA seeded")));
	TestTrue(TEXT("mapping scope diagnostic present"),
		HasMappingDiagnostic(TEXT("mapping_scope"), QAReimportScope));
	TestTrue(TEXT("mapping hit diagnostic present"),
		HasMappingDiagnostic(TEXT("mapping_hit"), TEXT("HeroButton")));
	TestTrue(TEXT("unavailable mapped class diagnostic carries the configured class path"),
		HasMappingDiagnostic(TEXT("mapping_class_unavailable"), BrokenClassPath));
	// 分类对齐导入路径：mapping 诊断不混进 reimport 章节（reimport_* 前缀）。
	TestEqual(TEXT("mapping diagnostics stay out of the reimport section"),
		static_cast<int32>(Result.Diagnostics.ContainsByPredicate([](const FDesign2UMGMappingDiagnostic& Diagnostic)
		{
			return Diagnostic.Kind.StartsWith(TEXT("mapping_"));
		})), 0);

	// 落盘出口：新写出的报告 JSON 的 componentMapping.entries 含类加载失败条目。
	TArray<FString> ReportsAfter;
	IFileManager::Get().FindFiles(ReportsAfter, *(ReportDir / TEXT("*.import_report.json")), true, false);
	FString NewReport;
	for (const FString& FileName : ReportsAfter)
	{
		if (!ReportsBefore.Contains(FileName))
		{
			NewReport = ReportDir / FileName;
		}
	}
	if (TestTrue(TEXT("a new import report was written by reimport"), !NewReport.IsEmpty()))
	{
		FString ReportJson;
		if (TestTrue(TEXT("report file readable"), QAReadFileString(NewReport, ReportJson)))
		{
			TSharedPtr<FJsonObject> ReportRoot;
			if (TestTrue(TEXT("report JSON parses"), QAParseJsonObject(ReportJson, ReportRoot)))
			{
				const TSharedPtr<FJsonObject>* ComponentMapping = nullptr;
				bool bFoundClassUnavailable = false;
				if (TestTrue(TEXT("componentMapping section present"),
						ReportRoot->TryGetObjectField(TEXT("componentMapping"), ComponentMapping) && ComponentMapping != nullptr))
				{
					const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
					if (TestTrue(TEXT("componentMapping.entries present"),
							(*ComponentMapping)->TryGetArrayField(TEXT("entries"), Entries) && Entries != nullptr))
					{
						for (const TSharedPtr<FJsonValue>& EntryValue : *Entries)
						{
							const TSharedPtr<FJsonObject> Entry = EntryValue->AsObject();
							if (Entry.IsValid()
								&& Entry->GetStringField(TEXT("kind")) == TEXT("mapping_class_unavailable")
								&& Entry->GetStringField(TEXT("message")).Contains(BrokenClassPath))
							{
								bFoundClassUnavailable = true;
							}
						}
					}
				}
				TestTrue(TEXT("report componentMapping entries carry the unavailable-class diagnostic"),
					bFoundClassUnavailable);
			}
		}
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 35c（integration）：C4 绑定通道的函数名撞名——请求的函数名与既有成员
// （member variable）同名时，函数图以唯一名创建，绑定与 guid 查询用同一个最终名，
// 编译保存后函数可解析、绑定仍在（不再指向不存在的函数）。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQABindingFunctionNameCollisionTest,
	"Design2UMG.SafeReimportQA.BindingSurvivesFunctionNameCollision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQABindingFunctionNameCollisionTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportBindingNameCollision");

	const FString V1 = MakeQADoc(MakeQAInteractive(TEXT("qa_link"), TEXT("JumpLink"), 24, TEXT("ON_CLICK"), TEXT("https://qa.example/jump")));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}

	// 制造撞名：member variable 与请求的函数名同名（该名字不是函数，但占用 Kismet 名字空间）。
	FEdGraphPinType BoolPin;
	BoolPin.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	TestTrue(TEXT("colliding member variable added"),
		FBlueprintEditorUtils::AddMemberVariable(WBP, TEXT("ClashHandler"), BoolPin));

	FString BindingError;
	if (!TestTrue(TEXT("binding added despite the name clash"),
			Design2UMGWidgetBindingTools::AddEventBinding(
				WBP, TEXT("qa_link"), TEXT("OnClicked"), TEXT("ClashHandler"), BindingError)))
	{
		AddError(FString::Printf(TEXT("AddEventBinding failed: %s"), *BindingError));
		QAWipeOutput(OutputDir);
		return false;
	}

	const FDelegateEditorBinding* Binding = nullptr;
	for (const FDelegateEditorBinding& Candidate : WBP->Bindings)
	{
		if (Candidate.ObjectName == TEXT("qa_link") && Candidate.PropertyName == TEXT("OnClicked"))
		{
			Binding = &Candidate;
		}
	}
	if (!TestNotNull(TEXT("editor binding recorded"), Binding))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	// 绑定必须指向实际创建的唯一名函数（撞名时请求名 ClashHandler 已被 member variable 占用）。
	TestTrue(TEXT("binding points at the unique function name, not the clashing requested name"),
		Binding->FunctionName != TEXT("ClashHandler"));
	bool bGraphExists = false;
	for (const UEdGraph* Graph : WBP->FunctionGraphs)
	{
		bGraphExists = bGraphExists || (Graph && Graph->GetFName() == Binding->FunctionName);
	}
	TestTrue(TEXT("function graph exists under the bound name"), bGraphExists);

	// 编译保存（生成链同一条路）后：函数在生成类上可解析，绑定未被剥离。
	if (!TestTrue(TEXT("compile + save succeeds with the collision binding"),
			FUMGGenerator::SaveWidgetBlueprint(WBP, nullptr)))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const bool bBindingSurvivesCompile = WBP->Bindings.ContainsByPredicate(
		[Binding](const FDelegateEditorBinding& Candidate)
		{
			return Candidate.ObjectName == TEXT("qa_link")
				&& Candidate.PropertyName == TEXT("OnClicked")
				&& Candidate.FunctionName == Binding->FunctionName;
		});
	TestTrue(TEXT("binding survives compile with the unique function name"), bBindingSurvivesCompile);
	TestNotNull(TEXT("bound function resolvable on the generated class"),
		WBP->GeneratedClass ? WBP->GeneratedClass->FindFunctionByName(Binding->FunctionName) : nullptr);

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 36（integration，跨会话磁盘加载态）：v1 导入保存后卸载包（确认不再是内存
// 命中），再 LoadPackage 从磁盘加载——引擎在 uasset 上重新持有不含写共享的读句柄
// （新编辑器会话对已有资产执行 reimport 的常态）。此时 reimport 必须成功：uasset
// 保存前置检查不做写探测（本进程读句柄会让探测假阳性，误杀本可成功的保存），
// 真正的外部 IO 失败由保存失败路径兜底（reimport_save_failed + 回滚）。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQADiskLoadedAssetTest,
	"Design2UMG.SafeReimportQA.DiskLoadedAssetReimportSucceeds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQADiskLoadedAssetTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportDiskLoaded");

	const auto BuildDoc = [](const TCHAR* Text)
	{
		return MakeQADoc(MakeQAPanel(TEXT("qa_panel"), TEXT("InfoPanel"), 24, 24,
			MakeQAText(TEXT("qa_head"), TEXT("Head"), 32, 32, Text)));
	};

	UWidgetBlueprint* WBP = QAImport(BuildDoc(TEXT("disk-v1")), nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	if (!TestTrue(TEXT("v1 asset exists on disk"),
			IFileManager::Get().FileSize(*QAAssetFilePath(TargetPackage)) > 0))
	{
		QAWipeOutput(OutputDir);
		return false;
	}

	// 卸载包（编辑器同一条 UPackageTools 卸载路径：关编辑器引用 + 清 RF_Standalone +
	// ResetLoaders + GC），确认资产不再是内存命中。
	FText UnloadError;
	TestTrue(TEXT("package unloads"), UPackageTools::UnloadPackages({ WBP->GetOutermost() }, UnloadError));
	TestNull(TEXT("package no longer resident in memory"), FindObject<UPackage>(nullptr, *TargetPackage));

	// 从磁盘重新加载：引擎重新持有 uasset 的读句柄（旧写探测假阳性的触发条件）。
	UPackage* const LoadedPackage = LoadPackage(nullptr, *TargetPackage, LOAD_None);
	if (!TestNotNull(TEXT("package reloads from disk"), LoadedPackage))
	{
		QAWipeOutput(OutputDir);
		return false;
	}

	const FDesignReimportResult Result = QAReimport(BuildDoc(TEXT("disk-v2")), nullptr, TargetPackage);
	TestTrue(TEXT("reimport succeeds on the disk-loaded asset"), Result.bSucceeded);
	TestFalse(TEXT("not a full conflict"), Result.bFullConflict);
	TestEqual(TEXT("no save failure diagnosed"), QACountKind(Result, TEXT("reimport_save_failed")), 0);
	TestEqual(TEXT("completion diagnosed once"), QACountKind(Result, TEXT("reimport_completed")), 1);

	// 资产更新生效：磁盘加载回来的 WBP 文本被 v2 设计值刷新，manifest 正常收口。
	UWidgetBlueprint* const LoadedWBP = Result.UpdatedBlueprint;
	if (TestNotNull(TEXT("updated blueprint returned"), LoadedWBP))
	{
		if (UTextBlock* Head = Cast<UTextBlock>(LoadedWBP->WidgetTree->FindWidget(TEXT("Head"))))
		{
			TestEqual(TEXT("design update applied to the disk-loaded asset"),
				Head->GetText().ToString(), FString(TEXT("disk-v2")));
		}
		else
		{
			AddError(TEXT("Head widget missing on the disk-loaded asset"));
		}
	}
	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads after the disk-loaded reimport"), QALoadSidecar(TargetPackage, Sidecar)))
	{
		TestNotNull(TEXT("container still recorded"), Sidecar.FindInstanceByDesignNodeId(TEXT("qa_panel")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 37（integration，切片 D）：多页原型文档两页→三页。switcher 宿主根 id 是页
// 集合内容指纹，增页必然改写指纹：宿主延续后 reimport 不得整体拒绝——新页按纯生成
// added 挂进同一宿主 switcher（宿主与既有页 widget identity 保留），manifest 源根
// 身份刷新为新指纹（下次 reimport 以新指纹为基准）。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQASwitcherHostAddPageTest,
	"Design2UMG.SafeReimportQA.SwitcherHostAddPageContinuesHost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQASwitcherHostAddPageTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportSwitcherAddPage");

	// v1：两页互指导航（构成多页原型）。
	const FString V1 = MakeProtoDoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeProtoPage(TEXT("sd_pg1"), TEXT("Page1"), 0, MakeProtoNavText(TEXT("sd_t1"), TEXT("P1Nav"), 20, 20, TEXT("sd_pg2"))),
		*MakeProtoPage(TEXT("sd_pg2"), TEXT("Page2"), 400, MakeProtoNavText(TEXT("sd_t2"), TEXT("P2Nav"), 20, 20, TEXT("sd_pg1")))));

	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr)
		|| !TestTrue(TEXT("v1 host root is the synthetic switcher"),
			WBP->WidgetTree->RootWidget && WBP->WidgetTree->RootWidget->IsA<UWidgetSwitcher>()))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	FDesign2UMGGenerationManifestSidecar SidecarV1;
	if (!TestTrue(TEXT("v1 manifest readable"), QALoadSidecar(TargetPackage, SidecarV1))
		|| !TestTrue(TEXT("v1 records the fingerprinted switcher root"),
			SidecarV1.SourceRootNodeId.StartsWith(TEXT("__d2u_prototype_switcher_"))))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const TWeakObjectPtr<UWidget> HostBefore = WBP->WidgetTree->RootWidget;
	const TWeakObjectPtr<UWidget> Page1Before = WBP->WidgetTree->FindWidget(TEXT("sd_pg1"));

	// v2：新增第三页（页内导航指向既有页，绑定有效）。
	const FString V2 = MakeProtoDoc(FString::Printf(
		TEXT("%s,%s,%s"),
		*MakeProtoPage(TEXT("sd_pg1"), TEXT("Page1"), 0, MakeProtoNavText(TEXT("sd_t1"), TEXT("P1Nav"), 20, 20, TEXT("sd_pg2"))),
		*MakeProtoPage(TEXT("sd_pg2"), TEXT("Page2"), 400, MakeProtoNavText(TEXT("sd_t2"), TEXT("P2Nav"), 20, 20, TEXT("sd_pg1"))),
		*MakeProtoPage(TEXT("sd_pg3"), TEXT("Page3"), 800, MakeProtoNavText(TEXT("sd_t3"), TEXT("P3Nav"), 20, 20, TEXT("sd_pg1")))));

	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("add-page reimport succeeds"), Result.bSucceeded);
	TestFalse(TEXT("not a full conflict"), Result.bFullConflict);
	TestEqual(TEXT("no full-conflict diagnostic"), QACountKind(Result, TEXT("reimport_full_conflict")), 0);
	TestEqual(TEXT("host continuation diagnosed once"),
		QACountKind(Result, TEXT("reimport_prototype_host_continued")), 1);

	// 宿主延续：同一宿主对象，新页挂进宿主 switcher。
	TestTrue(TEXT("host switcher object identity preserved"),
		WBP->WidgetTree->RootWidget == HostBefore.Get());
	UWidget* Page3 = WBP->WidgetTree->FindWidget(TEXT("sd_pg3"));
	if (TestNotNull(TEXT("new page generated"), Page3))
	{
		TestTrue(TEXT("new page attached under the host switcher"),
			Page3->GetParent() == WBP->WidgetTree->RootWidget);
	}
	TestTrue(TEXT("existing page widget identity preserved"),
		WBP->WidgetTree->FindWidget(TEXT("sd_pg1")) == Page1Before.Get());
	TestEqual(TEXT("add diagnosed once"), QACountKind(Result, TEXT("reimport_added")), 1);
	TestTrue(TEXT("add names the new page"),
		QAHasMessage(Result, TEXT("reimport_added"), TEXT("sd_pg3")));

	FDesign2UMGGenerationManifestSidecar SidecarV2;
	if (TestTrue(TEXT("manifest reloads after add-page reimport"), QALoadSidecar(TargetPackage, SidecarV2)))
	{
		TestTrue(TEXT("manifest source root renewed to the new fingerprint"),
			SidecarV2.SourceRootNodeId != SidecarV1.SourceRootNodeId
			&& SidecarV2.SourceRootNodeId.StartsWith(TEXT("__d2u_prototype_switcher_")));
		TestNotNull(TEXT("new page recorded as instance"),
			SidecarV2.FindInstanceByDesignNodeId(TEXT("sd_pg3")));
		TestNotNull(TEXT("existing pages stay recorded"),
			SidecarV2.FindInstanceByDesignNodeId(TEXT("sd_pg1")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 38（integration，切片 D）：多页原型删纯 D2U 页。被删页按既有安全删除流程
// 移除（树与 sidecar 都不再有它）+ removed 诊断点名；宿主延续不整体拒绝。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQASwitcherHostRemovePageTest,
	"Design2UMG.SafeReimportQA.SwitcherHostRemovePageSafeDelete",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQASwitcherHostRemovePageTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportSwitcherRemovePage");

	// v1：三页；pg1/pg2 互指，pg3 指向 pg1（删 pg3 后剩余导航仍全部有效）。
	const auto MakeV1Pages = []()
	{
		return FString::Printf(
			TEXT("%s,%s,%s"),
			*MakeProtoPage(TEXT("sd_pg1"), TEXT("Page1"), 0, MakeProtoNavText(TEXT("sd_t1"), TEXT("P1Nav"), 20, 20, TEXT("sd_pg2"))),
			*MakeProtoPage(TEXT("sd_pg2"), TEXT("Page2"), 400, MakeProtoNavText(TEXT("sd_t2"), TEXT("P2Nav"), 20, 20, TEXT("sd_pg1"))),
			*MakeProtoPage(TEXT("sd_pg3"), TEXT("Page3"), 800, MakeProtoNavText(TEXT("sd_t3"), TEXT("P3Nav"), 20, 20, TEXT("sd_pg1"))));
	};

	UWidgetBlueprint* WBP = QAImport(MakeProtoDoc(MakeV1Pages()), nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr)
		|| !TestTrue(TEXT("v1 host root is the synthetic switcher"),
			WBP->WidgetTree->RootWidget && WBP->WidgetTree->RootWidget->IsA<UWidgetSwitcher>())
		|| !TestNotNull(TEXT("v1 has the doomed page"), WBP->WidgetTree->FindWidget(TEXT("sd_pg3"))))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	const TWeakObjectPtr<UWidget> HostBefore = WBP->WidgetTree->RootWidget;

	// v2：pg3 消失。
	const FString V2 = MakeProtoDoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeProtoPage(TEXT("sd_pg1"), TEXT("Page1"), 0, MakeProtoNavText(TEXT("sd_t1"), TEXT("P1Nav"), 20, 20, TEXT("sd_pg2"))),
		*MakeProtoPage(TEXT("sd_pg2"), TEXT("Page2"), 400, MakeProtoNavText(TEXT("sd_t2"), TEXT("P2Nav"), 20, 20, TEXT("sd_pg1")))));

	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("remove-page reimport succeeds"), Result.bSucceeded);
	TestFalse(TEXT("not a full conflict"), Result.bFullConflict);
	TestEqual(TEXT("host continuation diagnosed once"),
		QACountKind(Result, TEXT("reimport_prototype_host_continued")), 1);
	TestTrue(TEXT("host switcher object identity preserved"),
		WBP->WidgetTree->RootWidget == HostBefore.Get());

	TestNull(TEXT("removed page deleted from the tree"), WBP->WidgetTree->FindWidget(TEXT("sd_pg3")));
	TestEqual(TEXT("safe remove diagnosed once"), QACountKind(Result, TEXT("reimport_removed")), 1);
	TestTrue(TEXT("removed diagnostic names the page"),
		QAHasMessage(Result, TEXT("reimport_removed"), TEXT("sd_pg3")));

	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads after remove-page reimport"), QALoadSidecar(TargetPackage, Sidecar)))
	{
		TestNull(TEXT("removed page dropped from the manifest"),
			Sidecar.FindInstanceByDesignNodeId(TEXT("sd_pg3")));
		TestTrue(TEXT("removed page dropped from ownership"), !Sidecar.D2UOwned.Contains(TEXT("sd_pg3")));
		TestNotNull(TEXT("surviving page stays recorded"),
			Sidecar.FindInstanceByDesignNodeId(TEXT("sd_pg1")));
		TestTrue(TEXT("manifest source root renewed"),
			Sidecar.SourceRootNodeId.StartsWith(TEXT("__d2u_prototype_switcher_")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 39（integration，切片 D）：多页原型删含项目内容的页（页 inner 层挂着开发者
// 子控件）。该页保留 + remove 冲突诊断点名，项目子控件存活；宿主延续不整体拒绝，
// 其余页照常更新。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQASwitcherHostRemovePageProjectContentTest,
	"Design2UMG.SafeReimportQA.SwitcherHostRemovePageProjectContentPreserved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQASwitcherHostRemovePageProjectContentTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportSwitcherRemoveProjPage");

	// v1：三页；pg2 是将被设计删除、但含项目内容的页（pg1/pg3 互指不受影响）。
	const FString V1 = MakeProtoDoc(FString::Printf(
		TEXT("%s,%s,%s"),
		*MakeProtoPage(TEXT("sd_pg1"), TEXT("Page1"), 0, MakeProtoNavText(TEXT("sd_t1"), TEXT("P1Nav"), 20, 20, TEXT("sd_pg3"))),
		*MakeProtoPage(TEXT("sd_pg2"), TEXT("Page2"), 400, MakeProtoNavText(TEXT("sd_t2"), TEXT("P2Nav"), 20, 20, TEXT("sd_pg1"))),
		*MakeProtoPage(TEXT("sd_pg3"), TEXT("Page3"), 800, MakeProtoNavText(TEXT("sd_t3"), TEXT("P3Nav"), 20, 20, TEXT("sd_pg1")))));

	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr)
		|| !TestTrue(TEXT("v1 host root is the synthetic switcher"),
			WBP->WidgetTree->RootWidget && WBP->WidgetTree->RootWidget->IsA<UWidgetSwitcher>()))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// 开发者定制：项目子控件挂进 pg2 的 inner 层（合成层名 = 页显示名 + "_inner"；
	// 设计删除时必须保留）。
	if (UPanelWidget* Page2Inner = Cast<UPanelWidget>(WBP->WidgetTree->FindWidget(TEXT("Page2_inner"))))
	{
		UTextBlock* ProjectNote = WBP->WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), TEXT("ProjectNote"));
		ProjectNote->SetText(FText::FromString(TEXT("project")));
		Page2Inner->AddChild(ProjectNote);
		TestNotNull(TEXT("project child added"), WBP->WidgetTree->FindWidget(TEXT("ProjectNote")));
	}
	else
	{
		AddError(TEXT("page inner layer not found for the project child"));
	}

	// v2：pg2 从设计消失。
	const FString V2 = MakeProtoDoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeProtoPage(TEXT("sd_pg1"), TEXT("Page1"), 0, MakeProtoNavText(TEXT("sd_t1"), TEXT("P1Nav"), 20, 20, TEXT("sd_pg3"))),
		*MakeProtoPage(TEXT("sd_pg3"), TEXT("Page3"), 800, MakeProtoNavText(TEXT("sd_t3"), TEXT("P3Nav"), 20, 20, TEXT("sd_pg1")))));

	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport succeeds (conflict is per-page)"), Result.bSucceeded);
	TestFalse(TEXT("not a full conflict"), Result.bFullConflict);
	TestEqual(TEXT("host continuation diagnosed once"),
		QACountKind(Result, TEXT("reimport_prototype_host_continued")), 1);

	TestNotNull(TEXT("removed-with-project-content page preserved"),
		WBP->WidgetTree->FindWidget(TEXT("sd_pg2")));
	TestNotNull(TEXT("project child preserved with it"),
		WBP->WidgetTree->FindWidget(TEXT("ProjectNote")));
	TestTrue(TEXT("remove conflict diagnosed for the page"),
		QAHasMessage(Result, TEXT("reimport_remove_conflict"), TEXT("sd_pg2")));
	TestEqual(TEXT("no silent delete of the page"), QACountKind(Result, TEXT("reimport_removed")), 0);

	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads after the conflict reimport"), QALoadSidecar(TargetPackage, Sidecar)))
	{
		TestNotNull(TEXT("conflicted page stays recorded"),
			Sidecar.FindInstanceByDesignNodeId(TEXT("sd_pg2")));
		TestTrue(TEXT("project widget stays outside d2uOwned"), !Sidecar.D2UOwned.Contains(TEXT("ProjectNote")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 40（integration，切片 D，回归锁定）：多页原型降级单页（宿主消失：新源不再
// 构成多页原型，无 switcher 合成根）。维持全量冲突的保守语义（切片 C CASE-011
// 已定性），资产保持 reimport 前状态。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQASwitcherHostDegradeToSinglePageTest,
	"Design2UMG.SafeReimportQA.SwitcherHostDegradeToSinglePageStillFullConflict",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQASwitcherHostDegradeToSinglePageTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportSwitcherDegrade");

	// v1：两页互指导航。
	const FString V1 = MakeProtoDoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeProtoPage(TEXT("sd_pg1"), TEXT("Page1"), 0, MakeProtoNavText(TEXT("sd_t1"), TEXT("P1Nav"), 20, 20, TEXT("sd_pg2"))),
		*MakeProtoPage(TEXT("sd_pg2"), TEXT("Page2"), 400, MakeProtoNavText(TEXT("sd_t2"), TEXT("P2Nav"), 20, 20, TEXT("sd_pg1")))));

	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr)
		|| !TestTrue(TEXT("v1 host root is the synthetic switcher"),
			WBP->WidgetTree->RootWidget && WBP->WidgetTree->RootWidget->IsA<UWidgetSwitcher>()))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// v2：只剩一页、无交互（不再构成多页原型，新 IR 根是页自身而非 switcher 合成根）。
	const FString V2 = MakeProtoDoc(MakeProtoPage(
		TEXT("sd_pg1"), TEXT("Page1"), 0, MakeQAText(TEXT("sd_t1"), TEXT("P1Text"), 20, 20, TEXT("solo"))));

	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestFalse(TEXT("degraded reimport does not update in place"), Result.bSucceeded);
	TestTrue(TEXT("degraded reimport is a full conflict"), Result.bFullConflict);
	TestEqual(TEXT("full conflict diagnosed once"), QACountKind(Result, TEXT("reimport_full_conflict")), 1);
	TestEqual(TEXT("no host continuation for the degraded source"),
		QACountKind(Result, TEXT("reimport_prototype_host_continued")), 0);

	// 资产保持原状态：宿主与第二页原样保留。
	TestTrue(TEXT("host switcher untouched"), WBP->WidgetTree->RootWidget->IsA<UWidgetSwitcher>());
	TestNotNull(TEXT("second page untouched"), WBP->WidgetTree->FindWidget(TEXT("sd_pg2")));

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 41（integration，S2 mapped 自身属性分歧报告）：mapped 实例自身视觉属性
// （节点级 opacity）导入时随通用属性写入，reimport 按「mapped 内部归项目」边界
// 不刷新，但设计声明值与实例现状的分歧必须点名（项目值 vs 设计值）。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAMappedOwnAttrDivergenceTest,
	"Design2UMG.SafeReimportQA.MappedOwnAttrDivergenceReported",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAMappedOwnAttrDivergenceTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportMappedOwnAttr");

	// mapped 实例节点带节点级 opacity（Figma "opacity" -> Render.RenderOpacity，仅 <1 时声明）。
	const auto BuildDoc = [](const TCHAR* NodeOpacity)
	{
		return FString::Printf(
			TEXT("{\"fileKey\":\"%s\",\"document\":{\"id\":\"0:0\",\"name\":\"QADoc\",\"type\":\"DOCUMENT\",\"children\":["
				 "{\"id\":\"0:1\",\"name\":\"Page\",\"type\":\"CANVAS\",\"children\":["
				 "{\"id\":\"qa_root\",\"name\":\"QAPage\",\"type\":\"FRAME\","
				 "\"absoluteBoundingBox\":{\"x\":0,\"y\":0,\"width\":360,\"height\":240},\"children\":["
				 "{\"id\":\"qa_moa\",\"name\":\"HeroButton\",\"type\":\"INSTANCE\","
				 "\"componentId\":\"77:88\",\"componentSetId\":\"99:00\",\"mainComponentKey\":\"%s\","
				 "\"componentProperties\":{\"label\":{\"type\":\"TEXT\",\"value\":\"Checkout\"}},"
				 "\"opacity\":%s,"
				 "\"absoluteBoundingBox\":{\"x\":24,\"y\":24,\"width\":120,\"height\":44},"
				 "\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":0.2,\"g\":0.5,\"b\":0.8,\"a\":1}}],"
				 "\"children\":[{\"id\":\"qa_moa_label\",\"name\":\"InnerLabel\",\"type\":\"TEXT\","
				 "\"absoluteBoundingBox\":{\"x\":32,\"y\":36,\"width\":80,\"height\":18},\"characters\":\"Checkout\",%s}]}"
				 "]}]}]}}"),
			QAReimportScope, QAReimportComponentKey, NodeOpacity, *QATextStyle());
	};

	UWidgetBlueprint* WBP = QAImport(BuildDoc(TEXT("0.5")), QAMappedClassPath, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	UDesign2UMGMappingQAWidget* MappedInstance = QAFindMappedInstance(WBP);
	if (!TestNotNull(TEXT("mapped instance generated"), MappedInstance))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	TestEqual(TEXT("v1 import writes design opacity onto the instance"),
		MappedInstance->GetRenderOpacity(), 0.5f);
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// v2：设计把节点透明度改成 0.25。reimport 不刷新自身属性（项目侧保留 0.5），
	// 但分歧必须点名报告。
	const FDesignReimportResult Result = QAReimport(BuildDoc(TEXT("0.25")), QAMappedClassPath, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);

	TestEqual(TEXT("own opacity kept as project-side"),
		QAFindMappedInstance(WBP)->GetRenderOpacity(), 0.5f);
	TestTrue(TEXT("divergence diagnostic names kept + design values"),
		QAHasMessageWithAll(Result, TEXT("reimport_mapped_attr_kept"),
			{ TEXT("renderOpacity project=0.500 design=0.250") }));

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// 用例 42（integration，R3 顶层 reparent 源端所有权）：开发者把顶层生成节点挪进
// 项目侧容器后，reimport 不得把它拽回宿主根——保留在项目容器 + reimport_move_conflict
// 点名；设计侧内容更新（文本）照常到达（摆放归项目，内容归设计）。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQATopLevelReparentSourceOwnedTest,
	"Design2UMG.SafeReimportQA.TopLevelReparentSourceProjectContainerPreserved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQATopLevelReparentSourceOwnedTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportTopMoveSourceOwned");

	// v1：N 是 qa_root 直下顶层节点（manifest instance），另有一个设计容器。
	const FString V1 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAText(TEXT("qa_n"), TEXT("N"), 30, 30, TEXT("mv")),
		*MakeQAPanel(TEXT("qa_c"), TEXT("ContainerA"), 260, 20,
			MakeQAText(TEXT("qa_h"), TEXT("AHint"), 30, 80, TEXT("a-hint")))));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// 开发者定制：项目容器 DevPanel 挂进宿主根，把顶层节点 N 挪进去。
	UVerticalBox* DevPanel = WBP->WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DevPanel"));
	if (UPanelWidget* Host = Cast<UPanelWidget>(WBP->WidgetTree->RootWidget))
	{
		Host->AddChild(DevPanel);
	}
	UWidget* N = WBP->WidgetTree->FindWidget(TEXT("N"));
	if (!TestNotNull(TEXT("N present before customization"), N) || !TestTrue(TEXT("DevPanel added"), DevPanel != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	WBP->WidgetTree->RemoveWidget(N);
	DevPanel->AddChild(N);
	const TWeakObjectPtr<UWidget> NWeak = N;

	// v2：设计里 N 仍是根下顶层节点（设计侧没挪它），文本更新。
	const FString V2 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAText(TEXT("qa_n"), TEXT("N"), 30, 30, TEXT("mv-v2")),
		*MakeQAPanel(TEXT("qa_c"), TEXT("ContainerA"), 260, 20,
			MakeQAText(TEXT("qa_h"), TEXT("AHint"), 30, 80, TEXT("a-hint")))));

	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport itself succeeds (conflict is per-node)"), Result.bSucceeded);

	UWidget* NAfter = NWeak.Get();
	if (TestNotNull(TEXT("N still alive"), NAfter))
	{
		TestEqual(TEXT("N kept under the project container DevPanel"),
			NAfter->GetParent()->GetFName(), FName(TEXT("DevPanel")));
	}
	TestEqual(TEXT("top-level move conflict diagnosed once"), QACountKind(Result, TEXT("reimport_move_conflict")), 1);
	TestTrue(TEXT("conflict names N and the project-container reason"),
		QAHasMessageWithAll(Result, TEXT("reimport_move_conflict"),
			{ TEXT("'N'"), TEXT("project-side container") }));
	TestEqual(TEXT("no moves applied"), QACountKind(Result, TEXT("reimport_moved")), 0);

	QAWipeOutput(OutputDir);
	return true;
}

#endif
