// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "DesignComponentMapping.h"
#include "UMGSchemaSourceExtensions.h"

namespace
{
	using namespace DesignComponentMapping;

	FUMGSourceNodeData MakeInstanceSource(
		const FString& ComponentId,
		const FString& ComponentSetId,
		const FString& MainComponentKey)
	{
		FUMGSourceNodeData Source;
		Source.SourceNodeId = TEXT("1:2");
		Source.SourceNodeType = TEXT("INSTANCE");
		Source.ComponentId = ComponentId;
		Source.ComponentSetId = ComponentSetId;
		Source.MainComponentKey = MainComponentKey;
		return Source;
	}

	FComponentMappingEntry MakeEntry(
		const FString& Key,
		const FString& WidgetClass,
		const TArray<FComponentInputDeclaration>& Inputs = {})
	{
		FComponentMappingEntry Entry;
		Entry.Key = Key;
		Entry.Provider = TEXT("figma");
		Entry.DocumentScope = TEXT("file-key-1");
		Entry.ComponentKey = TEXT("component-key-1");
		Entry.WidgetClass = WidgetClass;
		Entry.Inputs = Inputs;
		return Entry;
	}
}

// ---------------------------------------------------------------------------
// DocumentScope 解析：file key 优先，缺省回退导入源标识
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesignComponentMappingScopeFileKeyPreferredTest,
	"Design2UMG.Unit.ComponentMapping.DocumentScopeFileKeyPreferred",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesignComponentMappingScopeFileKeyPreferredTest::RunTest(const FString&)
{
	const FDocumentScopeResolution ByFileKey = ResolveFigmaDocumentScope(TEXT("abc123"), TEXT("G:/designs/export.json"));
	TestTrue(TEXT("file key wins"), ByFileKey.Value == TEXT("abc123"));
	TestEqual(TEXT("file key source reported"), ByFileKey.Source, TEXT("fileKey"));

	const FDocumentScopeResolution ByImportSource = ResolveFigmaDocumentScope(FString(), TEXT("G:/designs/export.json"));
	TestTrue(TEXT("import source fallback"), ByImportSource.Value == TEXT("G:/designs/export.json"));
	TestEqual(TEXT("import source source reported"), ByImportSource.Source, TEXT("importSource"));

	const FDocumentScopeResolution Unresolved = ResolveFigmaDocumentScope(FString(), FString());
	TestFalse(TEXT("no inputs stays unresolved"), Unresolved.IsResolved());
	TestEqual(TEXT("unresolved source reported"), Unresolved.Source, TEXT("unresolved"));
	return true;
}

// ---------------------------------------------------------------------------
// Figma 身份提取：mainComponentKey 优先、componentId 兜底、定义节点回填
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesignComponentMappingIdentityExtractionTest,
	"Design2UMG.Unit.ComponentMapping.IdentityExtraction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesignComponentMappingIdentityExtractionTest::RunTest(const FString&)
{
	const FString Scope = TEXT("file-key-1");

	FDesignComponentIdentity Identity;
	TestTrue(TEXT("instance with mainComponentKey resolves"),
		TryResolveFigmaComponentIdentity(
			MakeInstanceSource(TEXT("12:34"), TEXT("56:78"), TEXT("component-key-1")),
			Scope,
			Identity));
	TestEqual(TEXT("mainComponentKey preferred over componentId"), Identity.ComponentKey, TEXT("component-key-1"));
	TestEqual(TEXT("provider is figma"), Identity.Provider, TEXT("figma"));
	TestEqual(TEXT("document scope stamped"), Identity.DocumentScope, Scope);
	TestEqual(TEXT("component set key preserved"), Identity.ComponentSetKey, TEXT("56:78"));

	TestTrue(TEXT("instance without mainComponentKey falls back to componentId"),
		TryResolveFigmaComponentIdentity(
			MakeInstanceSource(TEXT("12:34"), FString(), FString()),
			Scope,
			Identity));
	TestEqual(TEXT("componentId fallback used"), Identity.ComponentKey, TEXT("12:34"));
	TestTrue(TEXT("set key stays empty when absent"), Identity.ComponentSetKey.IsEmpty());

	FUMGSourceNodeData DefinitionSource;
	DefinitionSource.SourceNodeId = TEXT("12:34");
	DefinitionSource.SourceNodeType = TEXT("COMPONENT");
	DefinitionSource.ComponentId = TEXT("12:34");
	TestTrue(TEXT("component definition resolves with its own id"),
		TryResolveFigmaComponentIdentity(DefinitionSource, Scope, Identity));
	TestEqual(TEXT("definition id becomes component key"), Identity.ComponentKey, TEXT("12:34"));

	FUMGSourceNodeData PlainSource;
	PlainSource.SourceNodeId = TEXT("99:1");
	PlainSource.SourceNodeType = TEXT("FRAME");
	FDesignComponentIdentity Discarded;
	TestFalse(TEXT("plain node has no component identity"),
		TryResolveFigmaComponentIdentity(PlainSource, Scope, Discarded));

	TestFalse(TEXT("empty document scope rejects identity"),
		TryResolveFigmaComponentIdentity(
			MakeInstanceSource(TEXT("12:34"), FString(), FString()),
			FString(),
			Discarded));
	return true;
}

// ---------------------------------------------------------------------------
// 两级匹配：variant 级精确优先，组件集级兜底，不做模糊匹配
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesignComponentMappingTwoLevelMatchTest,
	"Design2UMG.Unit.ComponentMapping.TwoLevelMatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesignComponentMappingTwoLevelMatchTest::RunTest(const FString&)
{
	const FString Scope = TEXT("file-key-1");

	FComponentMappingTable Table;
	// variant 级：一个 variant 一条 mapping。
	Table.Entries.Add(MakeEntry(TEXT("figma|file-key-1|component-key-1"), TEXT("/Game/UI/WBP_Variant.WBP_Variant_C")));
	// 组件集级：整组一条 mapping。
	Table.Entries.Add(MakeEntry(TEXT("figma|file-key-1|56:78"), TEXT("/Game/UI/WBP_Set.WBP_Set_C")));

	FDesignComponentIdentity Identity;
	bool bSetLevel = true;

	TestTrue(TEXT("variant identity resolves"),
		TryResolveFigmaComponentIdentity(MakeInstanceSource(TEXT("12:34"), TEXT("56:78"), TEXT("component-key-1")), Scope, Identity));
	const FComponentMappingEntry* VariantHit = Table.Match(Identity, bSetLevel);
	TestNotNull(TEXT("variant-level exact hit"), VariantHit);
	if (VariantHit)
	{
		TestFalse(TEXT("variant hit is not set level"), bSetLevel);
		TestEqual(TEXT("variant hit class"), VariantHit->WidgetClass, TEXT("/Game/UI/WBP_Variant.WBP_Variant_C"));
	}

	// 同一组件集下的另一个 variant（没有 variant 级条目）应命中组件集级条目。
	FDesignComponentIdentity SetIdentity;
	TestTrue(TEXT("second variant identity resolves"),
		TryResolveFigmaComponentIdentity(MakeInstanceSource(TEXT("90:12"), TEXT("56:78"), TEXT("component-key-2")), Scope, SetIdentity));
	const FComponentMappingEntry* SetHit = Table.Match(SetIdentity, bSetLevel);
	TestNotNull(TEXT("set-level fallback hit"), SetHit);
	if (SetHit)
	{
		TestTrue(TEXT("set hit flagged at set level"), bSetLevel);
		TestEqual(TEXT("set hit class"), SetHit->WidgetClass, TEXT("/Game/UI/WBP_Set.WBP_Set_C"));
	}

	// 无组件集归属且 variant 未命中：两级都不命中。
	FDesignComponentIdentity LoneIdentity;
	TestTrue(TEXT("lone variant identity resolves"),
		TryResolveFigmaComponentIdentity(MakeInstanceSource(TEXT("77:77"), FString(), TEXT("component-key-3")), Scope, LoneIdentity));
	TestNull(TEXT("unmapped variant without set stays unmatched"), Table.Match(LoneIdentity, bSetLevel));

	// 不同 DocumentScope 的条目不许跨文档命中。
	FDesignComponentIdentity OtherScopeIdentity;
	TestTrue(TEXT("other-scope identity resolves"),
		TryResolveFigmaComponentIdentity(MakeInstanceSource(TEXT("12:34"), TEXT("56:78"), TEXT("component-key-1")), TEXT("file-key-2"), OtherScopeIdentity));
	TestNull(TEXT("scope mismatch does not match"), Table.Match(OtherScopeIdentity, bSetLevel));

	// 名称相近但 key 不同：不做模糊匹配（也不因同组 set key 而扩大命中）。
	FDesignComponentIdentity NearIdentity;
	TestTrue(TEXT("near-name identity resolves"),
		TryResolveFigmaComponentIdentity(MakeInstanceSource(TEXT("12:34"), FString(), TEXT("component-key-1x")), Scope, NearIdentity));
	TestNull(TEXT("no fuzzy name matching"), Table.Match(NearIdentity, bSetLevel));

	TestNotNull(TEXT("find entry by key round-trips"),
		Table.FindEntryByKey(TEXT("figma|file-key-1|component-key-1")));
	return true;
}

// ---------------------------------------------------------------------------
// Key / inputs 声明解析与非法声明诊断
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesignComponentMappingDeclarationParsingTest,
	"Design2UMG.Unit.ComponentMapping.DeclarationParsing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesignComponentMappingDeclarationParsingTest::RunTest(const FString&)
{
	FString Provider;
	FString Scope;
	FString ComponentKey;
	TestTrue(TEXT("three-segment key parses"),
		ParseMappingKey(TEXT("figma|file-key-1|component-key-1"), Provider, Scope, ComponentKey));
	TestEqual(TEXT("key provider"), Provider, TEXT("figma"));
	TestEqual(TEXT("key scope"), Scope, TEXT("file-key-1"));
	TestEqual(TEXT("key component"), ComponentKey, TEXT("component-key-1"));

	TestFalse(TEXT("two-segment key rejected"), ParseMappingKey(TEXT("figma|component-key-1"), Provider, Scope, ComponentKey));
	TestFalse(TEXT("empty segment rejected"), ParseMappingKey(TEXT("figma||component-key-1"), Provider, Scope, ComponentKey));
	TestFalse(TEXT("four-segment key rejected"), ParseMappingKey(TEXT("a|b|c|d"), Provider, Scope, ComponentKey));

	FComponentInputDeclaration Declaration;
	FString Error;

	TestTrue(TEXT("valid text input parses"),
		TryParseComponentInputDeclaration(TEXT("label"), TEXT("Text"), TEXT("Text"), FString(), Declaration, Error));
	TestEqual(TEXT("default value kind is ComponentProperty"), Declaration.ValueKind, EComponentInputValueKind::ComponentProperty);

	TestTrue(TEXT("explicit ComponentText parses"),
		TryParseComponentInputDeclaration(TEXT("label"), TEXT("Label"), TEXT("Text"), TEXT("ComponentText"), Declaration, Error));
	TestEqual(TEXT("explicit value kind honored"), Declaration.ValueKind, EComponentInputValueKind::ComponentText);

	TestTrue(TEXT("bool input parses"),
		TryParseComponentInputDeclaration(TEXT("enabled"), TEXT("IsEnabled"), TEXT("Bool"), FString(), Declaration, Error));
	TestEqual(TEXT("bool type"), Declaration.Type, EComponentInputType::Bool);

	TestTrue(TEXT("name input parses"),
		TryParseComponentInputDeclaration(TEXT("variant"), TEXT("ButtonVariant"), TEXT("Name"), FString(), Declaration, Error));
	TestEqual(TEXT("name type"), Declaration.Type, EComponentInputType::Name);

	TestTrue(TEXT("float input parses"),
		TryParseComponentInputDeclaration(TEXT("value"), TEXT("Value"), TEXT("Float"), FString(), Declaration, Error));
	TestEqual(TEXT("float type"), Declaration.Type, EComponentInputType::Float);

	TestFalse(TEXT("empty source rejected"),
		TryParseComponentInputDeclaration(FString(), TEXT("Text"), TEXT("Text"), FString(), Declaration, Error));
	TestFalse(TEXT("empty target rejected"),
		TryParseComponentInputDeclaration(TEXT("label"), FString(), TEXT("Text"), FString(), Declaration, Error));
	TestFalse(TEXT("unknown type rejected"),
		TryParseComponentInputDeclaration(TEXT("label"), TEXT("Text"), TEXT("String"), FString(), Declaration, Error));
	TestTrue(TEXT("unknown type reports reason"), !Error.IsEmpty());
	TestFalse(TEXT("unknown value kind rejected"),
		TryParseComponentInputDeclaration(TEXT("label"), TEXT("Text"), TEXT("Text"), TEXT("Everything"), Declaration, Error));
	TestTrue(TEXT("unknown value kind reports reason"), !Error.IsEmpty());
	return true;
}

#endif
