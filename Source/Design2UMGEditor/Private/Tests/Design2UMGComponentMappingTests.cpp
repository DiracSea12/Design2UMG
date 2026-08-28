// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"

#include "Design2UMGComponentMappingConfig.h"
#include "Design2UMGComponentMappingGeneration.h"
#include "Design2UMGConverter.h"
#include "Design2UMGTestUtils.h"
#include "FigmaParser.h"
#include "UMGSchema.h"
#include "UMGSchemaSourceExtensions.h"
#include "Tests/Design2UMGMappedComponentTestWidget.h"

#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"
#include "HAL/FileManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	const TCHAR* TestScope = TEXT("mapping-test-file-key");

	// 两实例（同一组件定义）+ 一个未映射普通节点的设计稿。
	FString MakeMappingTestFigmaJson()
	{
		return TEXT(R"({
			"fileKey":"mapping-test-file-key",
			"document":{"id":"0:0","name":"Doc","type":"DOCUMENT","children":[
				{"id":"0:1","name":"Page","type":"CANVAS","children":[
					{"id":"page_root","name":"MappingPage","type":"FRAME",
					 "absoluteBoundingBox":{"x":0,"y":0,"width":360,"height":200},"children":[
						{"id":"btn_inst_1","name":"PrimaryButton","type":"INSTANCE",
						 "componentId":"12:34","componentSetId":"56:78","mainComponentKey":"component-key-primary",
						 "componentProperties":{"label":{"type":"TEXT","value":"Checkout"}},
						 "absoluteBoundingBox":{"x":24,"y":24,"width":120,"height":44},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.1,"g":0.4,"b":0.9,"a":1}}],
						 "children":[{"id":"btn_inst_1_label","name":"Label","type":"TEXT",
							"absoluteBoundingBox":{"x":32,"y":32,"width":80,"height":20},"characters":"Checkout",
							"style":{"fontFamily":"Inter","fontSize":16,"fontWeight":500,"textAlignHorizontal":"LEFT","textAlignVertical":"TOP","textDecoration":"NONE","letterSpacing":0,"lineHeightUnit":"AUTO","lineHeightPx":0}}]},
						{"id":"btn_inst_2","name":"PrimaryButton","type":"INSTANCE",
						 "componentId":"12:34","componentSetId":"56:78","mainComponentKey":"component-key-primary",
						 "componentProperties":{"label":{"type":"TEXT","value":"Pay Now"}},
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

	FDesign2UMGComponentMappingConfig MakeDirectMappingConfig(const FString& WidgetClassPath)
	{
		FDesign2UMGComponentMappingConfig Config;
		DesignComponentMapping::FComponentMappingEntry& Entry =
			Config.Table.Entries.AddDefaulted_GetRef();
		Entry.Key = FString::Printf(TEXT("figma|%s|component-key-primary"), TestScope);
		Entry.Provider = TEXT("figma");
		Entry.DocumentScope = TestScope;
		Entry.ComponentKey = TEXT("component-key-primary");
		Entry.WidgetClass = WidgetClassPath;
		return Config;
	}
}

// ---------------------------------------------------------------------------
// IR 层断言（BatchC 模式）：Convert 候选标记 + ApplyMapping 的命中标记
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentMappingIRMarkingTest,
	"Design2UMG.BatchC.ComponentMapping.IRMarking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FComponentMappingIRMarkingTest::RunTest(const FString&)
{
	FDesignDocument Document;
	TestTrue(TEXT("Parse mapping fixture"), FFigmaParser::ParseDocument(MakeMappingTestFigmaJson(), Document));

	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());

	// 文档级身份：file key 作为 DocumentScope。
	TestTrue(TEXT("Converted data has source document identity"), Data.HasSourceDocumentData());
	if (Data.HasSourceDocumentData())
	{
		TestEqual(TEXT("Provider stamped as figma"), Data.SourceDocument().Provider, TEXT("figma"));
		TestEqual(TEXT("DocumentScope uses file key"), Data.SourceDocument().DocumentScope, TestScope);
		TestEqual(TEXT("DocumentScope origin is fileKey"), Data.SourceDocument().DocumentScopeOrigin, TEXT("fileKey"));
	}

	const FUMGWidgetNode* InstanceWidget = Design2UMGTestUtils::FindWidgetById(Data.RootWidget, TEXT("btn_inst_1"));
	const FUMGWidgetNode* PlainWidget = Design2UMGTestUtils::FindWidgetById(Data.RootWidget, TEXT("plain_card"));
	TestNotNull(TEXT("Instance widget converted"), InstanceWidget);
	TestNotNull(TEXT("Plain widget converted"), PlainWidget);
	if (!InstanceWidget || !PlainWidget)
	{
		return false;
	}

	// 转换期候选标记：带组件身份的实例是 mapped 候选；普通节点不是。
	TestTrue(TEXT("Component instance is a mapped candidate"), InstanceWidget->Source().bMappedComponentCandidate);
	TestFalse(TEXT("Plain node is not a mapped candidate"), PlainWidget->Source().bMappedComponentCandidate);
	TestTrue(TEXT("No mapping key before generation apply"), InstanceWidget->Source().MappedComponentKey.IsEmpty());

	// 生成前匹配：命中条目把 Key 写进 IR 边车；未命中/普通节点不受影响。
	FDesign2UMGComponentMappingConfig Config = MakeDirectMappingConfig(TEXT("/Script/Design2UMGEditor.Design2UMGMappedComponentTestWidget"));
	FDesign2UMGComponentMappingBuildState BuildState(Config);
	Design2UMGComponentMappingGeneration::ApplyMappingToWidgetTreeIR(
		Data.RootWidget, Config, Data, BuildState);

	TestEqual(TEXT("Mapped instance key written to IR"),
		InstanceWidget->Source().MappedComponentKey,
		FString::Printf(TEXT("figma|%s|component-key-primary"), TestScope));
	TestTrue(TEXT("Plain node stays unmapped"), PlainWidget->Source().MappedComponentKey.IsEmpty());

	bool bSawHit = false;
	bool bSawScope = false;
	for (const FDesign2UMGMappingDiagnostic& Diagnostic : BuildState.GetDiagnostics())
	{
		bSawHit |= Diagnostic.Kind == TEXT("mapping_hit");
		bSawScope |= Diagnostic.Kind == TEXT("mapping_scope");
	}
	TestTrue(TEXT("Hit diagnostic emitted"), bSawHit);
	TestTrue(TEXT("Scope diagnostic reports the resolved scope"), bSawScope);
	return true;
}

// ---------------------------------------------------------------------------
// 集组件集级匹配：variant 未命中时按 ComponentSetKey 兜底
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentMappingIRSetLevelMarkingTest,
	"Design2UMG.BatchC.ComponentMapping.SetLevelMarking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FComponentMappingIRSetLevelMarkingTest::RunTest(const FString&)
{
	FDesignDocument Document;
	TestTrue(TEXT("Parse mapping fixture"), FFigmaParser::ParseDocument(MakeMappingTestFigmaJson(), Document));
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());

	FDesign2UMGComponentMappingConfig Config;
	DesignComponentMapping::FComponentMappingEntry& Entry = Config.Table.Entries.AddDefaulted_GetRef();
	Entry.Key = FString::Printf(TEXT("figma|%s|56:78"), TestScope);
	Entry.Provider = TEXT("figma");
	Entry.DocumentScope = TestScope;
	Entry.ComponentKey = TEXT("56:78");
	Entry.WidgetClass = TEXT("/Script/Design2UMGEditor.Design2UMGMappedComponentTestWidget");

	FDesign2UMGComponentMappingBuildState BuildState(Config);
	Design2UMGComponentMappingGeneration::ApplyMappingToWidgetTreeIR(
		Data.RootWidget, Config, Data, BuildState);

	const FUMGWidgetNode* InstanceWidget = Design2UMGTestUtils::FindWidgetById(Data.RootWidget, TEXT("btn_inst_2"));
	TestNotNull(TEXT("Instance widget converted"), InstanceWidget);
	if (!InstanceWidget)
	{
		return false;
	}
	TestEqual(TEXT("Set-level key matches the instance"),
		InstanceWidget->Source().MappedComponentKey,
		FString::Printf(TEXT("figma|%s|56:78"), TestScope));
	return true;
}

// ---------------------------------------------------------------------------
// ini 加载与校验：合法条目入库、非法条目/非法 input 丢弃并出诊断
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentMappingConfigLoadTest,
	"Design2UMG.ComponentMapping.ConfigLoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FComponentMappingConfigLoadTest::RunTest(const FString&)
{
	const FString TempIni = FPaths::ProjectSavedDir() / TEXT("Design2UMG") / TEXT("Tests") / TEXT("MappingConfigLoadTest.ini");
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(TempIni), true);
	const FString IniContent =
		TEXT("[/Design2UMG/ComponentMapping]\n")
		TEXT("+Mappings=(Key=\"figma|scope-1|component-key-1\",WidgetClass=\"/Script/Design2UMGEditor.Design2UMGMappedComponentTestWidget\",")
		TEXT("Inputs=((Source=\"label\",Target=\"Label\",Type=\"Text\"),(Source=\"enabled\",Target=\"EnabledFlag\",Type=\"Bool\")))\n")
		TEXT("+Mappings=(Key=\"bad-key\",WidgetClass=\"/Script/UMG.TextBlock\")\n")
		TEXT("+Mappings=(Key=\"pencil|scope-1|component-key-2\",WidgetClass=\"/Script/UMG.TextBlock\")\n")
		TEXT("+Mappings=(Key=\"figma|scope-1|component-key-3\",WidgetClass=\"/Game/Missing/WBP_Missing.WBP_Missing_C\")\n")
		TEXT("+Mappings=(Key=\"figma|scope-1|component-key-4\",WidgetClass=\"/Script/UMG.UserWidget\",")
		TEXT("Inputs=((Source=\"x\",Target=\"Y\",Type=\"String\")))\n")
		TEXT("+Mappings=(Key=\"figma|scope-1|component-key-1\",WidgetClass=\"/Script/UMG.UserWidget\")\n");
	TestTrue(TEXT("Write temp mapping ini"), FFileHelper::SaveStringToFile(IniContent, *TempIni));

	const FDesign2UMGComponentMappingConfig Config = Design2UMGComponentMappingConfig::Load(TempIni);
	IFileManager::Get().Delete(*TempIni, false, true);

	TestTrue(TEXT("Missing ini stays silent and empty"), !Design2UMGComponentMappingConfig::Load(
		FPaths::ProjectSavedDir() / TEXT("Design2UMG") / TEXT("Tests") / TEXT("__absent__.ini")).HasMappings());

	// 合法条目（component-key-1）与“类合法但 input 非法”的条目（component-key-4）都入库；
	// 非法 input 只丢 input 本身，映射条目继续生效。
	TestEqual(TEXT("Valid entries survive validation"), Config.Table.Entries.Num(), 2);
	const DesignComponentMapping::FComponentMappingEntry* ValidEntry =
		Config.Table.FindEntryByKey(TEXT("figma|scope-1|component-key-1"));
	TestNotNull(TEXT("Fully valid entry kept"), ValidEntry);
	if (ValidEntry)
	{
		TestEqual(TEXT("Valid entry class"), ValidEntry->WidgetClass, TEXT("/Script/Design2UMGEditor.Design2UMGMappedComponentTestWidget"));
		TestEqual(TEXT("Valid entry keeps valid inputs"), ValidEntry->Inputs.Num(), 2);
	}
	const DesignComponentMapping::FComponentMappingEntry* PartialEntry =
		Config.Table.FindEntryByKey(TEXT("figma|scope-1|component-key-4"));
	TestNotNull(TEXT("Entry with one invalid input still applies"), PartialEntry);
	if (PartialEntry)
	{
		TestEqual(TEXT("Invalid input dropped from entry"), PartialEntry->Inputs.Num(), 0);
	}

	// 诊断覆盖：坏 Key、Pencil provider、类缺失、非法 input、重复 Key。
	int32 BadKeyDiagnostics = 0;
	int32 PencilDiagnostics = 0;
	int32 ClassUnavailableDiagnostics = 0;
	int32 InputInvalidDiagnostics = 0;
	int32 DuplicateKeyDiagnostics = 0;
	for (const FDesign2UMGMappingDiagnostic& Diagnostic : Config.ValidationDiagnostics)
	{
		BadKeyDiagnostics += Diagnostic.Kind == TEXT("mapping_config_invalid") && Diagnostic.Message.Contains(TEXT("bad-key")) ? 1 : 0;
		PencilDiagnostics += Diagnostic.Message.Contains(TEXT("pencil")) ? 1 : 0;
		ClassUnavailableDiagnostics += Diagnostic.Kind == TEXT("mapping_class_unavailable") ? 1 : 0;
		InputInvalidDiagnostics += Diagnostic.Kind == TEXT("mapping_input_invalid") ? 1 : 0;
		DuplicateKeyDiagnostics += Diagnostic.Message.Contains(TEXT("already exists")) ? 1 : 0;
	}
	TestEqual(TEXT("Malformed key diagnosed"), BadKeyDiagnostics, 1);
	TestEqual(TEXT("Pencil provider entry dropped with diagnostic"), PencilDiagnostics, 1);
	TestEqual(TEXT("Missing class entry diagnosed"), ClassUnavailableDiagnostics, 1);
	TestEqual(TEXT("Invalid input diagnosed"), InputInvalidDiagnostics, 1);
	TestEqual(TEXT("Duplicate key diagnosed"), DuplicateKeyDiagnostics, 1);
	return true;
}

namespace
{
	// 直构单条 input 的 mapping 配置（绕过 ini，测试生成层逻辑）。
	FDesign2UMGComponentMappingConfig MakeSingleInputMappingConfig(
		const FString& Key,
		const FString& WidgetClassPath,
		const DesignComponentMapping::FComponentInputDeclaration& Input)
	{
		FDesign2UMGComponentMappingConfig Config;
		DesignComponentMapping::FComponentMappingEntry& Entry = Config.Table.Entries.AddDefaulted_GetRef();
		Entry.Key = Key;
		Entry.Provider = TEXT("figma");
		Entry.DocumentScope = TEXT("inherited-target-scope");
		Entry.ComponentKey = TEXT("mk-single-input");
		Entry.WidgetClass = WidgetClassPath;
		Entry.Inputs.Add(Input);
		return Config;
	}

	int32 CountMappingDiagnosticsByKind(
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
// inputs 目标属性名解析：spec 示例名（enabled -> IsEnabled，Bool）必须写入
// 真实 UWidget 继承属性。UWidget 的真实属性名是 bIsEnabled；解析规则为
// 原名查找未命中且声明类型为 Bool 时按 UE bool 命名惯例（b 前缀）重试。
// 用真实编译的 WBP 生成类做目标，同时锁定「继承属性可按名命中」这一事实。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentMappingInheritedBoolTargetTest,
	"Design2UMG.ComponentMapping.InheritedBoolTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FComponentMappingInheritedBoolTargetTest::RunTest(const FString&)
{
	UBlueprint* TestBlueprint = FKismetEditorUtilities::CreateBlueprint(
		UUserWidget::StaticClass(), GetTransientPackage(), FName(TEXT("D2UInheritedBoolTargetBP")),
		BPTYPE_Normal, UWidgetBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass(), NAME_None);
	if (!TestTrue(TEXT("Create test widget blueprint"), TestBlueprint != nullptr))
	{
		return false;
	}
	FKismetEditorUtilities::CompileBlueprint(TestBlueprint, EBlueprintCompileOptions::None);
	UClass* GeneratedClass = TestBlueprint->GeneratedClass;
	if (!TestNotNull(TEXT("Compiled WBP has a generated class"), GeneratedClass))
	{
		return false;
	}

	UUserWidget* Instance = NewObject<UUserWidget>(GetTransientPackage(), GeneratedClass);
	TestTrue(TEXT("Instance starts enabled by default"), Instance->GetIsEnabled());

	FUMGWidgetNode Node;
	Node.Id = TEXT("inherited_bool_inst");
	Node.MutableSource().MappedComponentKey = TEXT("figma|inherited-target-scope|mk-single-input");
	Node.MutableSource().ComponentPropertiesJson = TEXT("{\"enabled\":{\"type\":\"BOOLEAN\",\"value\":\"false\"}}");

	DesignComponentMapping::FComponentInputDeclaration Input;
	Input.Source = TEXT("enabled");
	Input.Target = TEXT("IsEnabled");
	Input.Type = DesignComponentMapping::EComponentInputType::Bool;
	Input.ValueKind = DesignComponentMapping::EComponentInputValueKind::ComponentProperty;

	FDesign2UMGComponentMappingConfig Config = MakeSingleInputMappingConfig(
		TEXT("figma|inherited-target-scope|mk-single-input"),
		GeneratedClass->GetPathName(), Input);
	FDesign2UMGComponentMappingBuildState BuildState(Config);
	Design2UMGComponentMappingGeneration::ApplyMappedComponentInputs(Instance, Node, BuildState);

	TestFalse(TEXT("Spec-style Target=IsEnabled writes the UWidget inherited bool"), Instance->GetIsEnabled());
	FString AppliedMessage;
	TestEqual(TEXT("Inherited bool input applied once"),
		CountMappingDiagnosticsByKind(BuildState, TEXT("mapping_input_applied"), &AppliedMessage), 1);
	TestTrue(TEXT("Applied diagnostic reports the design value"),
		AppliedMessage.Contains(TEXT("overwritten with design value 'false'")));
	TestEqual(TEXT("No skip diagnostic on the inherited bool input"),
		CountMappingDiagnosticsByKind(BuildState, TEXT("mapping_input_skipped")), 0);
	return true;
}

// ---------------------------------------------------------------------------
// inputs 取值来源 ComponentText 的子树回退：典型 Figma 按钮的 label 由实例
// 内部 TEXT 承载（无 componentProperties.label），主体文本取内部首个 TEXT。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentMappingComponentTextFromSubtreeTest,
	"Design2UMG.ComponentMapping.ComponentTextFromSubtree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FComponentMappingComponentTextFromSubtreeTest::RunTest(const FString&)
{
	FUMGWidgetNode Node;
	Node.Id = TEXT("subtree_text_inst");
	Node.Type = TEXT("Button");
	Node.MutableSource().MappedComponentKey = TEXT("figma|inherited-target-scope|mk-single-input");
	// 实例自身无文本；label 挂在内部 TEXT 子节点上。
	FUMGWidgetNode& InnerText = Node.Children.AddDefaulted_GetRef();
	InnerText.Id = TEXT("subtree_text_inst_label");
	InnerText.Type = TEXT("TextBlock");
	InnerText.Text.Text = FString(TEXT("Place Order"));

	UDesign2UMGMappedComponentTestWidget* Instance =
		NewObject<UDesign2UMGMappedComponentTestWidget>(GetTransientPackage());
	TestTrue(TEXT("Target starts empty"), Instance->Label.IsEmpty());

	DesignComponentMapping::FComponentInputDeclaration Input;
	Input.Source = TEXT("label");
	Input.Target = TEXT("Label");
	Input.Type = DesignComponentMapping::EComponentInputType::Text;
	Input.ValueKind = DesignComponentMapping::EComponentInputValueKind::ComponentText;

	FDesign2UMGComponentMappingConfig Config = MakeSingleInputMappingConfig(
		TEXT("figma|inherited-target-scope|mk-single-input"),
		TEXT("/Script/Design2UMGEditor.Design2UMGMappedComponentTestWidget"), Input);
	FDesign2UMGComponentMappingBuildState BuildState(Config);
	Design2UMGComponentMappingGeneration::ApplyMappedComponentInputs(Instance, Node, BuildState);

	TestEqual(TEXT("Internal TEXT supplies the component text"),
		Instance->Label.ToString(), TEXT("Place Order"));
	FString AppliedMessage;
	TestEqual(TEXT("Subtree text input applied once"),
		CountMappingDiagnosticsByKind(BuildState, TEXT("mapping_input_applied"), &AppliedMessage), 1);
	TestTrue(TEXT("Applied diagnostic reports the design value"),
		AppliedMessage.Contains(TEXT("overwritten with design value 'Place Order'")));
	TestEqual(TEXT("No skip diagnostic when the subtree carries text"),
		CountMappingDiagnosticsByKind(BuildState, TEXT("mapping_input_skipped")), 0);
	return true;
}

// ---------------------------------------------------------------------------
// Prototype 子树边界：触发点位于 mapped 实例本体与其内部（未展开）子树的交互
// 都不生成绑定，都输出 mapping_prototype_inert 诊断，消息区分两种位置；
// 普通节点的交互不受影响。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentMappingPrototypeSubtreeInertTest,
	"Design2UMG.ComponentMapping.PrototypeSubtreeInert",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FComponentMappingPrototypeSubtreeInertTest::RunTest(const FString&)
{
	FUMGWidgetData Data;
	Data.MutableSourceDocument().Provider = TEXT("figma");
	Data.MutableSourceDocument().DocumentScope = TEXT("subtree-inert-scope");
	Data.RootWidget.Id = TEXT("subtree_page");
	Data.RootWidget.Type = TEXT("CanvasPanel");

	// mapped 实例（候选身份与 entry 精确匹配）+ 内部 TEXT 子节点。
	FUMGWidgetNode& MappedNode = Data.RootWidget.Children.AddDefaulted_GetRef();
	MappedNode.Id = TEXT("mapped_inst");
	MappedNode.Name = TEXT("MappedButton");
	MappedNode.Type = TEXT("Button");
	MappedNode.MutableSource().SourceNodeId = TEXT("mapped_inst");
	MappedNode.MutableSource().SourceNodeType = TEXT("INSTANCE");
	MappedNode.MutableSource().MainComponentKey = TEXT("mk-subtree-inert");
	MappedNode.MutableSource().bMappedComponentCandidate = true;
	FUMGWidgetNode& InnerText = MappedNode.Children.AddDefaulted_GetRef();
	InnerText.Id = TEXT("mapped_inst_label");
	InnerText.Name = TEXT("Label");
	InnerText.Type = TEXT("TextBlock");

	FDesign2UMGComponentMappingConfig Config;
	DesignComponentMapping::FComponentMappingEntry& Entry = Config.Table.Entries.AddDefaulted_GetRef();
	Entry.Key = TEXT("figma|subtree-inert-scope|mk-subtree-inert");
	Entry.Provider = TEXT("figma");
	Entry.DocumentScope = TEXT("subtree-inert-scope");
	Entry.ComponentKey = TEXT("mk-subtree-inert");
	Entry.WidgetClass = TEXT("/Script/Design2UMGEditor.Design2UMGMappedComponentTestWidget");

	FDesign2UMGComponentMappingBuildState BuildState(Config);
	Design2UMGComponentMappingGeneration::ApplyMappingToWidgetTreeIR(
		Data.RootWidget, Config, Data, BuildState);

	FUMGPrototypeNavigationData NavigationData;
	auto AddBinding = [&NavigationData](const TCHAR* TriggerId, const TCHAR* TriggerName)
	{
		FUMGPrototypeNavigationBinding& Binding = NavigationData.ClickBindings.AddDefaulted_GetRef();
		Binding.TriggerWidgetId = TriggerId;
		Binding.TriggerWidgetName = TriggerName;
		Binding.TriggerType = TEXT("ON_CLICK");
	};
	AddBinding(TEXT("mapped_inst"), TEXT("MappedButton"));
	AddBinding(TEXT("mapped_inst_label"), TEXT("Label"));
	AddBinding(TEXT("plain_node"), TEXT("PlainCard"));

	Design2UMGComponentMappingGeneration::ReportInertPrototypeBindings(NavigationData, BuildState);

	int32 InstanceBodyInert = 0;
	int32 SubtreeInert = 0;
	for (const FDesign2UMGMappingDiagnostic& Diagnostic : BuildState.GetDiagnostics())
	{
		if (Diagnostic.Kind != TEXT("mapping_prototype_inert"))
		{
			continue;
		}
		InstanceBodyInert += Diagnostic.Message.Contains(TEXT("attached to mapped component")) ? 1 : 0;
		SubtreeInert += Diagnostic.Message.Contains(TEXT("inside a mapped component's unexpanded subtree")) ? 1 : 0;
		if (Diagnostic.Message.Contains(TEXT("'PlainCard'")))
		{
			AddError(TEXT("Plain node interaction must never be reported inert"));
		}
	}
	TestEqual(TEXT("Interaction on the mapped instance body reported inert"), InstanceBodyInert, 1);
	TestEqual(TEXT("Interaction inside the mapped subtree reported inert"), SubtreeInert, 1);
	return true;
}

#endif
