// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
//
// QA 白盒结构测试（run d2u-cmsr-slice-a，Component Mapping 阶段 1/2，单元/配置级）。
// 独立于交付内的开发测试设计，覆盖开发用例未覆盖的结构行为：
// - ini 校验的“非 UUserWidget 子类”与“行不可解析”错误输入路径
// - mapping 匹配主路径的 provider 门禁（Pencil 不进主路径）与 scope 解析失败路径
// - design-facing inputs 的取值来源链（ComponentText、componentProperties 优先、
//   variantProperties 回退、皆无跳过）与反射写入的类型不匹配/非法值路径
// - manifest 序列化的 inputs/matchedAtSetLevel 字段语义
// - Prototype inert 诊断不误报普通节点

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Design2UMGComponentMappingConfig.h"
#include "Design2UMGComponentMappingGeneration.h"
#include "Design2UMGGenerationManifest.h"
#include "UMGSchema.h"
#include "UMGSchemaSourceExtensions.h"

#include "Tests/Design2UMGMappingQAWidget.h"

#include "Blueprint/UserWidget.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"

namespace
{
	const TCHAR* QATestWidgetClassPath = TEXT("/Script/Design2UMGEditor.Design2UMGMappedComponentTestWidget");

	FString MakeQAConfigIniPath(const TCHAR* FileName)
	{
		const FString Path = FPaths::ProjectSavedDir() / TEXT("Design2UMG") / TEXT("Tests") / FileName;
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		return Path;
	}

	bool WriteQAIni(const FString& IniPath, const FString& Content)
	{
		return FFileHelper::SaveStringToFile(Content, *IniPath);
	}

	// 带 INSTANCE 身份的 IR 节点（mapped 候选）。
	FUMGWidgetNode MakeQAMappedCandidateNode(const FString& NodeId)
	{
		FUMGWidgetNode Node;
		Node.Id = NodeId;
		Node.Name = TEXT("QAComponent");
		Node.Type = TEXT("Button");
		Node.MutableSource().SourceNodeId = NodeId;
		Node.MutableSource().SourceNodeType = TEXT("INSTANCE");
		Node.MutableSource().ComponentId = TEXT("12:34");
		Node.MutableSource().ComponentSetId = TEXT("56:78");
		Node.MutableSource().MainComponentKey = TEXT("qa-main-key");
		Node.MutableSource().bMappedComponentCandidate = true;
		return Node;
	}

	// 单条 entry 的直构 mapping 配置（绕过 ini，测试生成层逻辑）。
	FDesign2UMGComponentMappingConfig MakeQAMappingConfig(
		const FString& Key,
		const TArray<DesignComponentMapping::FComponentInputDeclaration>& Inputs)
	{
		FDesign2UMGComponentMappingConfig Config;
		DesignComponentMapping::FComponentMappingEntry& Entry = Config.Table.Entries.AddDefaulted_GetRef();
		Entry.Key = Key;
		Entry.Provider = TEXT("figma");
		Entry.DocumentScope = TEXT("qa-gen-scope");
		Entry.ComponentKey = TEXT("qa-main-key");
		Entry.WidgetClass = QATestWidgetClassPath;
		Entry.Inputs = Inputs;
		return Config;
	}

	DesignComponentMapping::FComponentInputDeclaration MakeQAInput(
		const TCHAR* Source,
		const TCHAR* Target,
		DesignComponentMapping::EComponentInputType Type,
		DesignComponentMapping::EComponentInputValueKind ValueKind =
			DesignComponentMapping::EComponentInputValueKind::ComponentProperty)
	{
		DesignComponentMapping::FComponentInputDeclaration Input;
		Input.Source = Source;
		Input.Target = Target;
		Input.Type = Type;
		Input.ValueKind = ValueKind;
		return Input;
	}

	// 节点带上命中标记后对实例应用 inputs。Config 由调用方持有：
	// BuildState 只保存 Config 指针，其生命周期必须覆盖 BuildState 的全部使用。
	UDesign2UMGMappingQAWidget* ApplyQAInputs(
		FUMGWidgetNode& Node,
		const FDesign2UMGComponentMappingConfig& Config,
		FDesign2UMGComponentMappingBuildState& BuildState)
	{
		Node.MutableSource().MappedComponentKey = TEXT("figma|qa-gen-scope|qa-main-key");
		UDesign2UMGMappingQAWidget* Instance = NewObject<UDesign2UMGMappingQAWidget>(GetTransientPackage());
		Design2UMGComponentMappingGeneration::ApplyMappedComponentInputs(Instance, Node, BuildState);
		return Instance;
	}

	int32 CountDiagnosticsByKind(
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
// ini 校验：WidgetClass 解析得到一个非 UUserWidget 子类（引擎原生控件类）时，
// 该条目必须整体丢弃并输出可读诊断；同 ini 的其他合法条目必须继续入库。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesign2UMGMappingQAConfigRejectsNonUserWidgetClassTest,
	"Design2UMG.ComponentMapping.QAConfigRejectsNonUserWidgetClass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGMappingQAConfigRejectsNonUserWidgetClassTest::RunTest(const FString&)
{
	const FString IniPath = MakeQAConfigIniPath(TEXT("QAConfigNonUserWidgetClass.ini"));
	const FString IniContent =
		TEXT("[/Design2UMG/ComponentMapping]\n")
		TEXT("+Mappings=(Key=\"figma|qa-cfg-scope|bad-class\",WidgetClass=\"/Script/UMG.TextBlock\")\n")
		TEXT("+Mappings=(Key=\"figma|qa-cfg-scope|good-class\",WidgetClass=\"/Script/Design2UMGEditor.Design2UMGMappedComponentTestWidget\")\n");
	TestTrue(TEXT("write temp mapping ini"), WriteQAIni(IniPath, IniContent));

	const FDesign2UMGComponentMappingConfig Config = Design2UMGComponentMappingConfig::Load(IniPath);
	IFileManager::Get().Delete(*IniPath, false, true);

	TestEqual(TEXT("only the valid entry survives"), Config.Table.Entries.Num(), 1);
	TestNotNull(TEXT("valid entry still loaded"),
		Config.Table.FindEntryByKey(TEXT("figma|qa-cfg-scope|good-class")));
	TestNull(TEXT("non-UserWidget entry dropped"),
		Config.Table.FindEntryByKey(TEXT("figma|qa-cfg-scope|bad-class")));

	int32 NonUserWidgetDiagnostics = 0;
	FString FirstMessage;
	for (const FDesign2UMGMappingDiagnostic& Diagnostic : Config.ValidationDiagnostics)
	{
		if (Diagnostic.Kind == TEXT("mapping_class_unavailable"))
		{
			++NonUserWidgetDiagnostics;
			if (NonUserWidgetDiagnostics == 1)
			{
				FirstMessage = Diagnostic.Message;
			}
		}
	}
	// 加载 UClass 后显式 IsChildOf(UUserWidget) 判定：可加载但非 UUserWidget 子类的
	// 引擎类必须报「is not a UUserWidget subclass」，与「类缺失/加载失败」分开。
	TestEqual(TEXT("non-UserWidget class diagnosed exactly once"), NonUserWidgetDiagnostics, 1);
	TestTrue(TEXT("diagnostic names the offending class"),
		FirstMessage.Contains(TEXT("/Script/UMG.TextBlock")));
	TestTrue(TEXT("diagnostic names the offending mapping key"),
		FirstMessage.Contains(TEXT("bad-class")));
	TestTrue(TEXT("diagnostic states the class is not a UUserWidget subclass"),
		FirstMessage.Contains(TEXT("is not a UUserWidget subclass")));
	return true;
}

// ---------------------------------------------------------------------------
// ini 校验：结构体行无法被 UE config 系统解析（缺 "(" 前缀）时输出
// mapping_config_invalid 诊断（含 "could not be parsed"），条目不入表。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesign2UMGMappingQAConfigRejectsUnparseableLineTest,
	"Design2UMG.ComponentMapping.QAConfigRejectsUnparseableLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGMappingQAConfigRejectsUnparseableLineTest::RunTest(const FString&)
{
	const FString IniPath = MakeQAConfigIniPath(TEXT("QAConfigUnparseableLine.ini"));
	const FString IniContent =
		TEXT("[/Design2UMG/ComponentMapping]\n")
		TEXT("+Mappings=this-line-is-not-a-struct\n");
	TestTrue(TEXT("write temp mapping ini"), WriteQAIni(IniPath, IniContent));

	const FDesign2UMGComponentMappingConfig Config = Design2UMGComponentMappingConfig::Load(IniPath);
	IFileManager::Get().Delete(*IniPath, false, true);

	TestEqual(TEXT("unparseable line produces no entries"), Config.Table.Entries.Num(), 0);

	int32 UnparseableDiagnostics = 0;
	FString FirstMessage;
	for (const FDesign2UMGMappingDiagnostic& Diagnostic : Config.ValidationDiagnostics)
	{
		if (Diagnostic.Kind == TEXT("mapping_config_invalid")
			&& Diagnostic.Message.Contains(TEXT("could not be parsed")))
		{
			++UnparseableDiagnostics;
			if (UnparseableDiagnostics == 1)
			{
				FirstMessage = Diagnostic.Message;
			}
		}
	}
	TestEqual(TEXT("unparseable line diagnosed exactly once"), UnparseableDiagnostics, 1);
	TestTrue(TEXT("diagnostic echoes the offending line"),
		FirstMessage.Contains(TEXT("this-line-is-not-a-struct")));
	return true;
}

// ---------------------------------------------------------------------------
// 匹配主路径 provider 门禁：Pencil（provider "pencil"）文档即使配置了
// 完全匹配的 Key 也不进 v1 主路径——输出 mapping_skipped 诊断、节点不命中。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesign2UMGMappingQAPencilProviderSkippedTest,
	"Design2UMG.ComponentMapping.QAPencilProviderSkipped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGMappingQAPencilProviderSkippedTest::RunTest(const FString&)
{
	FUMGWidgetData Data;
	Data.MutableSourceDocument().Provider = TEXT("pencil");
	Data.MutableSourceDocument().DocumentScope = TEXT("qa-gen-scope");
	Data.RootWidget = MakeQAMappedCandidateNode(TEXT("pen_inst_1"));

	// 直接构造条目（绕过 ini 的 provider 校验），证明门禁在生成侧同样成立：
	// 即使表里存在与节点身份逐字符匹配的 pencil 条目，也不得命中。
	FDesign2UMGComponentMappingConfig Config;
	DesignComponentMapping::FComponentMappingEntry& Entry = Config.Table.Entries.AddDefaulted_GetRef();
	Entry.Key = TEXT("pencil|qa-gen-scope|qa-main-key");
	Entry.Provider = TEXT("pencil");
	Entry.DocumentScope = TEXT("qa-gen-scope");
	Entry.ComponentKey = TEXT("qa-main-key");
	Entry.WidgetClass = QATestWidgetClassPath;

	FDesign2UMGComponentMappingBuildState BuildState(Config);
	Design2UMGComponentMappingGeneration::ApplyMappingToWidgetTreeIR(
		Data.RootWidget, Config, Data, BuildState);

	int32 SkippedDiagnostics = 0;
	FString FirstMessage;
	for (const FDesign2UMGMappingDiagnostic& Diagnostic : BuildState.GetDiagnostics())
	{
		if (Diagnostic.Kind == TEXT("mapping_skipped"))
		{
			++SkippedDiagnostics;
			if (SkippedDiagnostics == 1)
			{
				FirstMessage = Diagnostic.Message;
			}
		}
	}
	TestEqual(TEXT("pen provider skipped with one diagnostic"), SkippedDiagnostics, 1);
	TestTrue(TEXT("skip diagnostic names the provider"), FirstMessage.Contains(TEXT("provider 'pencil'")));
	TestTrue(TEXT("skip diagnostic states only figma is matched"),
		FirstMessage.Contains(TEXT("only figma documents are matched")));
	TestEqual(TEXT("pen provider emits no hit diagnostics"),
		CountDiagnosticsByKind(BuildState, TEXT("mapping_hit")), 0);
	TestTrue(TEXT("pen document node stays unmapped"),
		Data.RootWidget.Source().MappedComponentKey.IsEmpty());
	return true;
}

// ---------------------------------------------------------------------------
// 匹配主路径 scope 门禁：figma 文档但 DocumentScope 解析失败（空）时输出
// “无法解析 scope，任何节点都不可能命中”诊断，且不做匹配。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesign2UMGMappingQAUnresolvedDocumentScopeTest,
	"Design2UMG.ComponentMapping.QAUnresolvedDocumentScope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGMappingQAUnresolvedDocumentScopeTest::RunTest(const FString&)
{
	FUMGWidgetData Data;
	Data.MutableSourceDocument().Provider = TEXT("figma");
	Data.MutableSourceDocument().DocumentScope = TEXT("");
	Data.MutableSourceDocument().DocumentScopeOrigin = TEXT("unresolved");
	Data.RootWidget = MakeQAMappedCandidateNode(TEXT("figma_inst_1"));

	FDesign2UMGComponentMappingConfig Config = MakeQAMappingConfig(
		TEXT("figma|qa-gen-scope|qa-main-key"), {});

	FDesign2UMGComponentMappingBuildState BuildState(Config);
	Design2UMGComponentMappingGeneration::ApplyMappingToWidgetTreeIR(
		Data.RootWidget, Config, Data, BuildState);

	int32 ScopeDiagnostics = 0;
	FString FirstMessage;
	for (const FDesign2UMGMappingDiagnostic& Diagnostic : BuildState.GetDiagnostics())
	{
		if (Diagnostic.Kind == TEXT("mapping_scope")
			&& Diagnostic.Message.Contains(TEXT("could not resolve a document scope")))
		{
			++ScopeDiagnostics;
			if (ScopeDiagnostics == 1)
			{
				FirstMessage = Diagnostic.Message;
			}
		}
	}
	TestEqual(TEXT("unresolved scope diagnosed exactly once"), ScopeDiagnostics, 1);
	TestTrue(TEXT("scope diagnostic states no node can match"),
		FirstMessage.Contains(TEXT("no node can match")));
	TestEqual(TEXT("unresolved scope emits no hit diagnostics"),
		CountDiagnosticsByKind(BuildState, TEXT("mapping_hit")), 0);
	TestTrue(TEXT("node stays unmapped on unresolved scope"),
		Data.RootWidget.Source().MappedComponentKey.IsEmpty());
	return true;
}

// ---------------------------------------------------------------------------
// inputs 取值来源 ComponentText：设计组件主体文本写入目标属性；
// 组件自身与内部 TEXT 都无文本时跳过该 input 并输出可读诊断，映射本身保持生效。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesign2UMGMappingQAComponentTextInputValueTest,
	"Design2UMG.ComponentMapping.QAComponentTextInputValue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGMappingQAComponentTextInputValueTest::RunTest(const FString&)
{
	const TArray<DesignComponentMapping::FComponentInputDeclaration> Inputs = {
		MakeQAInput(TEXT("label"), TEXT("TextValue"),
			DesignComponentMapping::EComponentInputType::Text,
			DesignComponentMapping::EComponentInputValueKind::ComponentText),
	};

	// 有直接文本：写入成功 + 覆写诊断带设计值。
	FUMGWidgetNode NodeWithText = MakeQAMappedCandidateNode(TEXT("text_inst_1"));
	NodeWithText.Text.Text = FString(TEXT("Checkout"));
	FDesign2UMGComponentMappingConfig ConfigWithText =
		MakeQAMappingConfig(TEXT("figma|qa-gen-scope|qa-main-key"), Inputs);
	FDesign2UMGComponentMappingBuildState StateWithText(ConfigWithText);
	UDesign2UMGMappingQAWidget* InstanceWithText = ApplyQAInputs(NodeWithText, ConfigWithText, StateWithText);
	TestEqual(TEXT("component text written to declared target"),
		InstanceWithText->TextValue.ToString(), TEXT("Checkout"));
	FString AppliedMessage;
	TestEqual(TEXT("applied diagnostic emitted once"),
		CountDiagnosticsByKind(StateWithText, TEXT("mapping_input_applied"), &AppliedMessage), 1);
	TestTrue(TEXT("applied diagnostic reports the design value"),
		AppliedMessage.Contains(TEXT("overwritten with design value 'Checkout'")));
	TestEqual(TEXT("no skip diagnostic on success"),
		CountDiagnosticsByKind(StateWithText, TEXT("mapping_input_skipped")), 0);

	// 无直接文本：跳过该 input + 诊断，不写默认值。
	FUMGWidgetNode NodeWithoutText = MakeQAMappedCandidateNode(TEXT("text_inst_2"));
	FDesign2UMGComponentMappingConfig ConfigWithoutText =
		MakeQAMappingConfig(TEXT("figma|qa-gen-scope|qa-main-key"), Inputs);
	FDesign2UMGComponentMappingBuildState StateWithoutText(ConfigWithoutText);
	UDesign2UMGMappingQAWidget* InstanceWithoutText = ApplyQAInputs(NodeWithoutText, ConfigWithoutText, StateWithoutText);
	TestTrue(TEXT("target untouched when component has no text"),
		InstanceWithoutText->TextValue.IsEmpty());
	FString SkippedMessage;
	TestEqual(TEXT("missing text skipped with diagnostic"),
		CountDiagnosticsByKind(StateWithoutText, TEXT("mapping_input_skipped"), &SkippedMessage), 1);
	TestTrue(TEXT("skip diagnostic states the reason"),
		SkippedMessage.Contains(TEXT("component has no text content")));
	TestTrue(TEXT("skip diagnostic says mapping remains active"),
		SkippedMessage.Contains(TEXT("other inputs and the mapping remain active")));
	return true;
}

// ---------------------------------------------------------------------------
// inputs 取值来源 ComponentProperty：componentProperties 里的键优先；
// 缺失时回退 variantProperties；两处都没有时跳过并诊断。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesign2UMGMappingQAComponentPropertyFallbackTest,
	"Design2UMG.ComponentMapping.QAComponentPropertyFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGMappingQAComponentPropertyFallbackTest::RunTest(const FString&)
{
	const TArray<DesignComponentMapping::FComponentInputDeclaration> Inputs = {
		MakeQAInput(TEXT("variant"), TEXT("NameValue"), DesignComponentMapping::EComponentInputType::Name),
	};

	// 两处都有同名键：componentProperties 的值优先。
	FUMGWidgetNode BothNode = MakeQAMappedCandidateNode(TEXT("prop_inst_1"));
	BothNode.MutableSource().ComponentPropertiesJson = TEXT("{\"variant\":{\"type\":\"VARIANT\",\"value\":\"Secondary\"}}");
	BothNode.MutableSource().VariantPropertiesJson = TEXT("{\"variant\":\"Primary\"}");
	FDesign2UMGComponentMappingConfig BothConfig =
		MakeQAMappingConfig(TEXT("figma|qa-gen-scope|qa-main-key"), Inputs);
	FDesign2UMGComponentMappingBuildState BothState(BothConfig);
	UDesign2UMGMappingQAWidget* BothInstance = ApplyQAInputs(BothNode, BothConfig, BothState);
	TestEqual(TEXT("componentProperties value wins over variantProperties"),
		BothInstance->NameValue, FName(TEXT("Secondary")));
	TestEqual(TEXT("componentProperties hit applies cleanly"),
		CountDiagnosticsByKind(BothState, TEXT("mapping_input_applied")), 1);

	// 只在 variantProperties：回退生效。
	FUMGWidgetNode VariantOnlyNode = MakeQAMappedCandidateNode(TEXT("prop_inst_2"));
	VariantOnlyNode.MutableSource().VariantPropertiesJson = TEXT("{\"variant\":\"Primary\"}");
	FDesign2UMGComponentMappingConfig VariantOnlyConfig =
		MakeQAMappingConfig(TEXT("figma|qa-gen-scope|qa-main-key"), Inputs);
	FDesign2UMGComponentMappingBuildState VariantOnlyState(VariantOnlyConfig);
	UDesign2UMGMappingQAWidget* VariantOnlyInstance = ApplyQAInputs(VariantOnlyNode, VariantOnlyConfig, VariantOnlyState);
	TestEqual(TEXT("variantProperties fallback supplies the value"),
		VariantOnlyInstance->NameValue, FName(TEXT("Primary")));
	FString VariantAppliedMessage;
	TestEqual(TEXT("fallback apply diagnosed"),
		CountDiagnosticsByKind(VariantOnlyState, TEXT("mapping_input_applied"), &VariantAppliedMessage), 1);
	TestTrue(TEXT("fallback apply reports the design value"),
		VariantAppliedMessage.Contains(TEXT("'Primary'")));

	// 两处都没有：跳过 + 诊断。
	FUMGWidgetNode MissingNode = MakeQAMappedCandidateNode(TEXT("prop_inst_3"));
	MissingNode.MutableSource().ComponentPropertiesJson = TEXT("{\"other\":{\"type\":\"TEXT\",\"value\":\"x\"}}");
	MissingNode.MutableSource().VariantPropertiesJson = TEXT("{\"other\":\"x\"}");
	FDesign2UMGComponentMappingConfig MissingConfig =
		MakeQAMappingConfig(TEXT("figma|qa-gen-scope|qa-main-key"), Inputs);
	FDesign2UMGComponentMappingBuildState MissingState(MissingConfig);
	UDesign2UMGMappingQAWidget* MissingInstance = ApplyQAInputs(MissingNode, MissingConfig, MissingState);
	TestEqual(TEXT("target untouched when key is absent"), MissingInstance->NameValue, FName());
	FString MissingMessage;
	TestEqual(TEXT("absent property key skipped with diagnostic"),
		CountDiagnosticsByKind(MissingState, TEXT("mapping_input_skipped"), &MissingMessage), 1);
	TestTrue(TEXT("skip diagnostic names the missing key"),
		MissingMessage.Contains(TEXT("component property 'variant' not found in componentProperties/variantProperties")));
	return true;
}

// ---------------------------------------------------------------------------
// inputs 反射写入：声明类型与目标属性类型不匹配、或设计值无法解析为声明类型时，
// 跳过该 input 并给出精确诊断；Float 声明写入单精度 float 属性时成功。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesign2UMGMappingQAInputTypeMismatchAndInvalidValuesTest,
	"Design2UMG.ComponentMapping.QAInputTypeMismatchAndInvalidValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGMappingQAInputTypeMismatchAndInvalidValuesTest::RunTest(const FString&)
{
	const TArray<DesignComponentMapping::FComponentInputDeclaration> Inputs = {
		// Bool 声明但目标是 FText 属性：类型不匹配。
		MakeQAInput(TEXT("label"), TEXT("TextValue"), DesignComponentMapping::EComponentInputType::Bool),
		// Bool 声明但设计值不是 bool。
		MakeQAInput(TEXT("enabled"), TEXT("BoolValue"), DesignComponentMapping::EComponentInputType::Bool),
		// Float 声明但设计值不是数值。
		MakeQAInput(TEXT("value"), TEXT("DoubleValue"), DesignComponentMapping::EComponentInputType::Float),
		// Float 声明写入单精度属性：应成功。
		MakeQAInput(TEXT("ratio"), TEXT("FloatValue"), DesignComponentMapping::EComponentInputType::Float),
	};

	FUMGWidgetNode Node = MakeQAMappedCandidateNode(TEXT("write_inst_1"));
	Node.MutableSource().ComponentPropertiesJson =
		TEXT("{\"label\":{\"type\":\"TEXT\",\"value\":\"Checkout\"},")
		TEXT("\"enabled\":{\"type\":\"BOOLEAN\",\"value\":\"maybe\"},")
		TEXT("\"value\":{\"type\":\"TEXT\",\"value\":\"abc\"},")
		TEXT("\"ratio\":{\"type\":\"NUMBER\",\"value\":2.5}}");

	FDesign2UMGComponentMappingConfig Config =
		MakeQAMappingConfig(TEXT("figma|qa-gen-scope|qa-main-key"), Inputs);
	FDesign2UMGComponentMappingBuildState BuildState(Config);
	UDesign2UMGMappingQAWidget* Instance = ApplyQAInputs(Node, Config, BuildState);

	// 三个失败 input 的属性终值都保持默认。
	TestTrue(TEXT("text target untouched on type mismatch"), Instance->TextValue.IsEmpty());
	TestTrue(TEXT("bool target keeps default on unparseable value"), Instance->BoolValue);
	TestEqual(TEXT("double target keeps default on non-numeric value"), Instance->DoubleValue, 0.0);
	// 单精度 float 写入成功。
	TestEqual(TEXT("float target written through FFloatProperty branch"),
		Instance->FloatValue, 2.5f);

	// 逐条核对诊断消息片段。
	int32 TypeMismatch = 0;
	int32 NotABool = 0;
	int32 NotAFloat = 0;
	int32 Applied = 0;
	for (const FDesign2UMGMappingDiagnostic& Diagnostic : BuildState.GetDiagnostics())
	{
		if (Diagnostic.Kind == TEXT("mapping_input_skipped"))
		{
			TypeMismatch += Diagnostic.Message.Contains(TEXT("does not match declared input type 'Bool'")) ? 1 : 0;
			NotABool += Diagnostic.Message.Contains(TEXT("value 'maybe' is not a bool")) ? 1 : 0;
			NotAFloat += Diagnostic.Message.Contains(TEXT("value 'abc' is not a float")) ? 1 : 0;
		}
		else if (Diagnostic.Kind == TEXT("mapping_input_applied"))
		{
			Applied += Diagnostic.Message.Contains(TEXT("FloatValue")) ? 1 : 0;
		}
	}
	TestEqual(TEXT("type mismatch diagnosed once"), TypeMismatch, 1);
	TestEqual(TEXT("non-bool value diagnosed once"), NotABool, 1);
	TestEqual(TEXT("non-float value diagnosed once"), NotAFloat, 1);
	TestEqual(TEXT("float write into single-precision property diagnosed once"), Applied, 1);
	return true;
}

// ---------------------------------------------------------------------------
// manifest 序列化：inputs 只存声明（source/target/valueKind），不存设计值
// （属性值不进 manifest，reimport 靠重新转换拿最新值）；
// matchedAtSetLevel 只在 true 时写入；generated 记录无 mappedKey；
// d2uOwned 由 payload 显式给定；写完不残留 .tmp 临时文件。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesign2UMGMappingQAManifestFieldSerializationTest,
	"Design2UMG.ComponentMapping.QAManifestFieldSerialization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGMappingQAManifestFieldSerializationTest::RunTest(const FString&)
{
	FDesign2UMGGenerationManifestPayload Payload;
	Payload.Provider = TEXT("figma");
	Payload.DocumentScope = TEXT("qa-manifest-scope");
	Payload.SourceRootNodeId = TEXT("qa-root");
	Payload.SourceRootName = TEXT("QARootPage");

	FDesign2UMGMappedInstanceRecord SetLevelRecord;
	SetLevelRecord.DesignNodeId = TEXT("qa-mapped-1");
	SetLevelRecord.WidgetName = TEXT("WBP_QA_SetLevel");
	SetLevelRecord.MappedKey = TEXT("figma|qa-manifest-scope|56:78");
	SetLevelRecord.MappedClass = QATestWidgetClassPath;
	SetLevelRecord.bMatchedAtSetLevel = true;
	FDesign2UMGMappedInputRecord AppliedInput;
	AppliedInput.Source = TEXT("label");
	AppliedInput.Target = TEXT("TextValue");
	AppliedInput.ValueKind = TEXT("ComponentText");
	FDesign2UMGMappedInputRecord SkippedInput;
	SkippedInput.Source = TEXT("missing");
	SkippedInput.Target = TEXT("Nowhere");
	SkippedInput.ValueKind = TEXT("ComponentProperty");
	SetLevelRecord.Inputs = { AppliedInput, SkippedInput };
	Payload.MappedInstances.Add(SetLevelRecord);

	FDesign2UMGMappedInstanceRecord VariantRecord;
	VariantRecord.DesignNodeId = TEXT("qa-mapped-2");
	VariantRecord.WidgetName = TEXT("WBP_QA_Variant");
	VariantRecord.MappedKey = TEXT("figma|qa-manifest-scope|qa-main-key");
	VariantRecord.MappedClass = QATestWidgetClassPath;
	VariantRecord.bMatchedAtSetLevel = false;
	Payload.MappedInstances.Add(VariantRecord);

	FDesign2UMGGeneratedInstanceRecord GeneratedRecord;
	GeneratedRecord.DesignNodeId = TEXT("qa-generated-1");
	GeneratedRecord.WidgetName = TEXT("Card_0");
	Payload.TopLevelGenerated.Add(GeneratedRecord);

	Payload.D2UOwnedNames = { TEXT("WBP_QA_SetLevel"), TEXT("WBP_QA_Variant"), TEXT("Card_0"), TEXT("Card_0_Caption") };

	const FString PackageName = TEXT("/Game/Design2UMG/_TestOutput/QAManifestFields");
	const FString SidecarPath = Design2UMGGenerationManifest::BuildSidecarPathForAsset(PackageName);
	IFileManager::Get().Delete(*SidecarPath, false, true);
	TestTrue(TEXT("manifest sidecar written"),
		Design2UMGGenerationManifest::WriteSidecarForAsset(PackageName, Payload));

	TSharedPtr<FJsonObject> Manifest;
	FString Json;
	{
		TestTrue(TEXT("sidecar file readable"), FFileHelper::LoadFileToString(Json, *SidecarPath));
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		TestTrue(TEXT("sidecar is valid JSON"), FJsonSerializer::Deserialize(Reader, Manifest) && Manifest.IsValid());
	}
	TestFalse(TEXT("no temp file left behind"), FPaths::FileExists(SidecarPath + TEXT(".tmp")));

	if (!Manifest.IsValid())
	{
		IFileManager::Get().Delete(*SidecarPath, false, true);
		return false;
	}

	TestEqual(TEXT("schema version"), Manifest->GetIntegerField(TEXT("schemaVersion")), 1);
	TestEqual(TEXT("provider"), Manifest->GetStringField(TEXT("provider")), TEXT("figma"));
	TestEqual(TEXT("document scope"), Manifest->GetStringField(TEXT("documentScope")), TEXT("qa-manifest-scope"));
	TestEqual(TEXT("source root node id"),
		Manifest->GetObjectField(TEXT("sourceRoot"))->GetStringField(TEXT("nodeId")), TEXT("qa-root"));
	TestEqual(TEXT("source root name"),
		Manifest->GetObjectField(TEXT("sourceRoot"))->GetStringField(TEXT("name")), TEXT("QARootPage"));

	const TArray<TSharedPtr<FJsonValue>>* Instances = nullptr;
	TestTrue(TEXT("instances array present"), Manifest->TryGetArrayField(TEXT("instances"), Instances) && Instances);
	if (!Instances)
	{
		IFileManager::Get().Delete(*SidecarPath, false, true);
		return false;
	}
	TestEqual(TEXT("instance order is mapped entries then generated"), Instances->Num(), 3);

	// 记录 1：set 级命中 + applied/skipped 两个 input。
	const TSharedPtr<FJsonObject> Record1 = (*Instances)[0]->AsObject();
	TestEqual(TEXT("first record kind"), Record1->GetStringField(TEXT("kind")), TEXT("mapped"));
	TestEqual(TEXT("first record widget name"), Record1->GetStringField(TEXT("widgetName")), TEXT("WBP_QA_SetLevel"));
	TestTrue(TEXT("set-level match flag written only when true"),
		Record1->HasField(TEXT("matchedAtSetLevel")) && Record1->GetBoolField(TEXT("matchedAtSetLevel")));
	const TArray<TSharedPtr<FJsonValue>>* Record1Inputs = nullptr;
	TestTrue(TEXT("mapped record carries its inputs"),
		Record1->TryGetArrayField(TEXT("inputs"), Record1Inputs) && Record1Inputs && Record1Inputs->Num() == 2);
	if (Record1Inputs && Record1Inputs->Num() == 2)
	{
		const TSharedPtr<FJsonObject> Input1 = (*Record1Inputs)[0]->AsObject();
		TestEqual(TEXT("applied input source"), Input1->GetStringField(TEXT("source")), TEXT("label"));
		TestEqual(TEXT("applied input value kind"), Input1->GetStringField(TEXT("valueKind")), TEXT("ComponentText"));
		TestTrue(TEXT("inputs never serialize design values"), !Input1->HasField(TEXT("value")));
		const TSharedPtr<FJsonObject> Input2 = (*Record1Inputs)[1]->AsObject();
		TestTrue(TEXT("skipped input omits the value field"), !Input2->HasField(TEXT("value")));
		TestEqual(TEXT("skipped input still records source"), Input2->GetStringField(TEXT("source")), TEXT("missing"));
	}

	// 记录 2：variant 级命中，matchedAtSetLevel 字段省略。
	const TSharedPtr<FJsonObject> Record2 = (*Instances)[1]->AsObject();
	TestEqual(TEXT("second record kind"), Record2->GetStringField(TEXT("kind")), TEXT("mapped"));
	TestFalse(TEXT("variant-level record omits matchedAtSetLevel"),
		Record2->HasField(TEXT("matchedAtSetLevel")));

	// 记录 3：generated，无 mappedKey/mappedClass。
	const TSharedPtr<FJsonObject> Record3 = (*Instances)[2]->AsObject();
	TestEqual(TEXT("third record kind"), Record3->GetStringField(TEXT("kind")), TEXT("generated"));
	TestEqual(TEXT("generated record design node id"),
		Record3->GetStringField(TEXT("designNodeId")), TEXT("qa-generated-1"));
	TestFalse(TEXT("generated record has no mapped key"), Record3->HasField(TEXT("mappedKey")));

	// d2uOwned 与 payload 显式给定的名字一致。
	const TArray<TSharedPtr<FJsonValue>>* D2UOwned = nullptr;
	TestTrue(TEXT("d2uOwned array present"), Manifest->TryGetArrayField(TEXT("d2uOwned"), D2UOwned) && D2UOwned);
	if (D2UOwned && D2UOwned->Num() == 4)
	{
		TSet<FString> OwnedNames;
		for (const TSharedPtr<FJsonValue>& OwnedValue : *D2UOwned)
		{
			OwnedNames.Add(OwnedValue->AsString());
		}
		TestTrue(TEXT("d2uOwned carries the payload names"),
			OwnedNames.Includes(TSet<FString>({ TEXT("WBP_QA_SetLevel"), TEXT("WBP_QA_Variant"), TEXT("Card_0"), TEXT("Card_0_Caption") })));
	}
	else
	{
		AddError(FString::Printf(TEXT("d2uOwned expected 4 entries, got %d"),
			D2UOwned ? D2UOwned->Num() : -1));
	}

	IFileManager::Get().Delete(*SidecarPath, false, true);
	return true;
}

// ---------------------------------------------------------------------------
// Prototype 边界对照：同一导航数据里 mapped 与普通节点各挂一条交互，
// inert 诊断只对 mapped 身份输出（含 trigger 名与 project-owned 说明），
// 普通节点的交互不得产生 inert 诊断（不误报）。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesign2UMGMappingQAPrototypeInertNoFalsePositiveTest,
	"Design2UMG.ComponentMapping.QAPrototypeInertNoFalsePositive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGMappingQAPrototypeInertNoFalsePositiveTest::RunTest(const FString&)
{
	FDesign2UMGComponentMappingConfig Config = MakeQAMappingConfig(TEXT("figma|qa-gen-scope|qa-main-key"), {});
	FDesign2UMGComponentMappingBuildState BuildState(Config);
	BuildState.AddMappedNodeId(TEXT("mapped_node_1"), false);

	FUMGPrototypeNavigationData NavigationData;
	FUMGPrototypeNavigationBinding& MappedBinding = NavigationData.ClickBindings.AddDefaulted_GetRef();
	MappedBinding.TriggerWidgetId = TEXT("mapped_node_1");
	MappedBinding.TriggerWidgetName = TEXT("QAComponent");
	MappedBinding.TriggerType = TEXT("ON_CLICK");
	FUMGPrototypeNavigationBinding& PlainBinding = NavigationData.ClickBindings.AddDefaulted_GetRef();
	PlainBinding.TriggerWidgetId = TEXT("plain_node_9");
	PlainBinding.TriggerWidgetName = TEXT("PlainCard");
	PlainBinding.TriggerType = TEXT("ON_CLICK");

	Design2UMGComponentMappingGeneration::ReportInertPrototypeBindings(NavigationData, BuildState);

	const TArray<FDesign2UMGMappingDiagnostic>& Diagnostics = BuildState.GetDiagnostics();
	TestEqual(TEXT("exactly one inert diagnostic"), Diagnostics.Num(), 1);
	if (Diagnostics.Num() == 1)
	{
		TestEqual(TEXT("inert diagnostic kind"), Diagnostics[0].Kind, TEXT("mapping_prototype_inert"));
		TestTrue(TEXT("inert diagnostic names the mapped trigger"),
			Diagnostics[0].Message.Contains(TEXT("'QAComponent'")));
		TestTrue(TEXT("inert diagnostic states interactions are project-owned"),
			Diagnostics[0].Message.Contains(TEXT("project-owned")));
		TestTrue(TEXT("inert diagnostic states the interaction is inert"),
			Diagnostics[0].Message.Contains(TEXT("inert this phase")));
	}
	for (const FDesign2UMGMappingDiagnostic& Diagnostic : Diagnostics)
	{
		TestFalse(TEXT("plain node interaction never reported inert"),
			Diagnostic.Message.Contains(TEXT("'PlainCard'")));
	}
	return true;
}

#endif
