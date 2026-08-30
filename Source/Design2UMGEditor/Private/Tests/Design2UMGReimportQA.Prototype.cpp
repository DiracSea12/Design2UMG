// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

// Safe Reimport QA: prototype interaction refresh and dual-trigger conflicts (hover/click, Figma-style node ids).

#include "Misc/AutomationTest.h"

#include "Design2UMGPrototypeNavigationExtensions.h"
#include "DesignImportPipeline.h"
#include "../UMGWidgetUtils.h"

#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Widget.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"
#include "Design2UMGReimportQATestFixtures.h"

#if WITH_AUTOMATION_WORKER

using namespace Design2UMGReimportQA;

// ---------------------------------------------------------------------------
// Case 14 (integration): Prototype bindings refresh by stable design identity (no project conflict) - the URL
// change lands in the runtime extension data and the refresh diagnostic states how many entries took effect.
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
// Case 15 (integration): double-trigger conflict (hover trigger) - the same interaction already has a project
// binding -> the project behavior stays, the D2U binding yields + a conflict diagnostic; non-conflicting bindings refresh as usual.
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

	// A project binding occupies qa_hover_a's hover interaction.
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
// Case 15b (integration): double-trigger conflict (click trigger, real Figma node-id shape) - when the trigger
// node id contains ':', the button widget name is the slug-cleaned form (e.g. "12:7" -> "W127"), and project
// bindings register by that widget name; conflict detection must compare with the same cleaning rule, or every
// double-trigger conflict on a real Figma file is missed (project binding and refreshed D2U binding coexist).
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

	// The node id "12:7" becomes the button widget name "W127" after slug cleaning (drop ':') + digit-prefix handling;
	// the project binding is registered under that widget name in the UMG editor.
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

	// With no surviving design binding after yielding (0 bindings active), not attaching the prototype extension is
	// expected; the core lock-points of this case: deterministic naming (the button name derivable from the node id) + no missed conflict detection.
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

#endif

