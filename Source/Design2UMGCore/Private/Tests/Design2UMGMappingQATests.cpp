// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
//
// QA 白盒结构测试（run d2u-cmsr-slice-a，Component Mapping 阶段 1/2）。
// 独立于交付内的开发测试设计：聚焦映射表比较语义中被实现锁定、
// 但开发用例未覆盖的行为——Key 的大小写不敏感比较与无效身份的拒绝。

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "DesignComponentMapping.h"

namespace
{
	using namespace DesignComponentMapping;

	FComponentMappingEntry MakeQAEntry(const FString& Key)
	{
		FComponentMappingEntry Entry;
		Entry.Key = Key;
		Entry.Provider = TEXT("figma");
		Entry.DocumentScope = TEXT("qa-case-scope");
		Entry.ComponentKey = TEXT("component-key-1");
		Entry.WidgetClass = TEXT("/Game/UI/WBP_QACase.WBP_QACase_C");
		return Entry;
	}
}

// ---------------------------------------------------------------------------
// 匹配表比较语义：Key 比较大小写不敏感（Provider/DocumentScope/ComponentKey
// 三段拼出的规范串整体 IgnoreCase 比较）；任一字段为空的身份是无效身份，
// 两级匹配都必须拒绝，不得因大小写或空字段产生意外命中或漏配。
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesign2UMGMappingQAKeyCaseInsensitiveAndInvalidIdentityTest,
	"Design2UMG.Unit.ComponentMapping.QAKeyCaseInsensitiveAndInvalidIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGMappingQAKeyCaseInsensitiveAndInvalidIdentityTest::RunTest(const FString&)
{
	FComponentMappingTable Table;
	// ini 里写的是全大写 scope；导入侧解析出的 Key 用小写。
	Table.Entries.Add(MakeQAEntry(TEXT("figma|QA-CASE-SCOPE|component-key-1")));

	FDesignComponentIdentity Identity;
	Identity.Provider = TEXT("figma");
	Identity.DocumentScope = TEXT("qa-case-scope");
	Identity.ComponentKey = TEXT("component-key-1");

	bool bSetLevel = true;
	const FComponentMappingEntry* Hit = Table.Match(Identity, bSetLevel);
	TestNotNull(TEXT("key match is case-insensitive across all three segments"), Hit);
	if (Hit)
	{
		TestFalse(TEXT("exact variant hit is not flagged as set level"), bSetLevel);
		TestEqual(TEXT("case-insensitive hit returns the configured class"),
			Hit->WidgetClass, TEXT("/Game/UI/WBP_QACase.WBP_QACase_C"));
	}

	// 无效身份（ComponentKey 空）：两级匹配都必须拒绝，不能走到 set 级兜底。
	FDesignComponentIdentity EmptyComponentKey;
	EmptyComponentKey.Provider = TEXT("figma");
	EmptyComponentKey.DocumentScope = TEXT("qa-case-scope");
	EmptyComponentKey.ComponentKey = TEXT("");
	EmptyComponentKey.ComponentSetKey = TEXT("56:78");
	bool bUnusedSetLevel = false;
	TestNull(TEXT("empty component key identity never matches"),
		Table.Match(EmptyComponentKey, bUnusedSetLevel));

	FDesignComponentIdentity EmptyProvider;
	EmptyProvider.Provider = TEXT("");
	EmptyProvider.DocumentScope = TEXT("qa-case-scope");
	EmptyProvider.ComponentKey = TEXT("component-key-1");
	TestNull(TEXT("empty provider identity never matches"),
		Table.Match(EmptyProvider, bUnusedSetLevel));

	// 生成阶段按 Key 回查条目（TryCreateMappedWidget / ApplyMappedComponentInputs）
	// 走 FindEntryByKey，与 Match 的比较语义必须一致：大小写不敏感。
	const FComponentMappingEntry* ByKey = Table.FindEntryByKey(TEXT("FIGMA|qa-case-scope|COMPONENT-KEY-1"));
	TestNotNull(TEXT("find-by-key is case-insensitive"), ByKey);
	if (ByKey)
	{
		TestEqual(TEXT("find-by-key returns the same entry"),
			ByKey->WidgetClass, TEXT("/Game/UI/WBP_QACase.WBP_QACase_C"));
	}
	TestNull(TEXT("find-by-key with unknown key returns null"),
		Table.FindEntryByKey(TEXT("figma|qa-case-scope|component-key-999")));

	// 键解析对大小写不敏感的输入同样成立（ini 里 Provider 大小写漂移时仍三段解析）。
	FString OutProvider;
	FString OutScope;
	FString OutComponentKey;
	TestTrue(TEXT("mixed-case key still parses into three segments"),
		ParseMappingKey(TEXT("Figma|QA-CASE-SCOPE|Component-Key-1"), OutProvider, OutScope, OutComponentKey));
	TestEqual(TEXT("parsed provider preserves original text"), OutProvider, TEXT("Figma"));
	TestEqual(TEXT("parsed scope preserves original text"), OutScope, TEXT("QA-CASE-SCOPE"));
	TestEqual(TEXT("parsed component key preserves original text"), OutComponentKey, TEXT("Component-Key-1"));
	return true;
}

#endif
