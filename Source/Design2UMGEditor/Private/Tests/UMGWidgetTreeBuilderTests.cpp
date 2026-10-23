// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Components/Spacer.h"
#include "Components/NamedSlot.h"
#include "Editor.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/ScopeExit.h"
#include "Misc/AutomationTest.h"
#include "UMGWidgetTreeBuilder.h"
#include "UMGGenerator.h"
#include "UMGWidgetTypes.h"
#include "Tests/Design2UMGTestAssetCleanup.h"
#include "UObject/StrongObjectPtr.h"
#include "WidgetBlueprint.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTreeUniqueNamesTest,
	"Design2UMG.Generation.UniqueWidgetNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FWidgetTreeUniqueNamesTest::RunTest(const FString& Parameters)
{
	const TStrongObjectPtr<UWidgetTree> Tree(NewObject<UWidgetTree>());
	for (int32 Index = 0; Index < 1000; ++Index)
	{
		const FString Name = Index == 0 ? TEXT("Repeated") : FString::Printf(TEXT("Repeated_%d"), Index);
		if (!TestNotNull(TEXT("Occupied widget name created"),
			Tree->ConstructWidget<USpacer>(USpacer::StaticClass(), FName(*Name))))
		{
			return false;
		}
	}
	for (int32 Index = 1; Index < 1000; ++Index)
	{
		if (!TestNotNull(TEXT("Occupied property-collision name created"),
			Tree->ConstructWidget<USpacer>(USpacer::StaticClass(),
				FName(*FString::Printf(TEXT("ToolTipText_w%d"), Index)))))
		{
			return false;
		}
	}

	FUMGWidgetNode Node;
	Node.Type = TEXT("Spacer");
	Node.Name = TEXT("Repeated");
	UWidget* RepeatedWidget = FUMGWidgetTreeBuilder::BuildWidgetTree(Tree.Get(), Node, nullptr, nullptr);
	if (TestNotNull(TEXT("Repeated-name widget created"), RepeatedWidget))
	{
		TestEqual(TEXT("Duplicate names remain unique beyond 999 suffixes"),
			RepeatedWidget->GetFName(), FName(TEXT("Repeated_1000")));
		TestEqual(TEXT("Reimport preserves the duplicate-name suffix rank"),
			FUMGWidgetTreeBuilder::GetWidgetNameSuffixRank(RepeatedWidget->GetName(), TEXT("Repeated")), 1999);
	}
	Node.Name = TEXT("ToolTipText");
	UWidget* PropertyCollisionWidget = FUMGWidgetTreeBuilder::BuildWidgetTree(Tree.Get(), Node, nullptr, nullptr);
	if (TestNotNull(TEXT("Property-collision widget created"), PropertyCollisionWidget))
	{
		TestEqual(TEXT("Property-collision names remain unique beyond 999 suffixes"),
			PropertyCollisionWidget->GetFName(), FName(TEXT("ToolTipText_w1000")));
		TestEqual(TEXT("Reimport preserves the property-collision suffix rank"),
			FUMGWidgetTreeBuilder::GetWidgetNameSuffixRank(PropertyCollisionWidget->GetName(), TEXT("ToolTipText")), 2000);
	}
	const TStrongObjectPtr<UWidgetBlueprint> Blueprint(NewObject<UWidgetBlueprint>());
	Blueprint->ParentClass = UUserWidget::StaticClass();
	Blueprint->WidgetTree = NewObject<UWidgetTree>(Blueprint.Get());
	Blueprint->FunctionGraphs.Add(FBlueprintEditorUtils::CreateNewGraph(Blueprint.Get(), TEXT("ImportedAction"),
		UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass()));
	for (const FString Name : {FString(TEXT("Tick")), FString(TEXT("ImportedAction")), FString(TEXT("Content"))})
	{
		Node.Name = Name;
		UWidget* Widget = FUMGWidgetTreeBuilder::BuildWidgetTree(Blueprint->WidgetTree, Node, nullptr, nullptr);
		if (TestNotNull(TEXT("Blueprint member-name widget created"), Widget))
		{
			const FString Expected = Name == TEXT("Content") ? Name : Name + TEXT("_w1");
			TestEqual(TEXT("Only colliding Blueprint member names are renamed"), Widget->GetName(), Expected);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTreeRebuildNamesTest,
	"Design2UMG.Generation.RebuiltWidgetNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FWidgetTreeRebuildNamesTest::RunTest(const FString&)
{
	const FString Folder = TEXT("/Game/Design2UMG/Automation/RebuiltWidgetNames_")
		+ FGuid::NewGuid().ToString(EGuidFormats::Digits);
	ON_SCOPE_EXIT { Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(Folder); };
	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_RebuiltWidgetNames");
	Data.RootWidget.Id = TEXT("root");
	Data.RootWidget.Type = UMGWidgetTypes::CanvasPanel;
	Data.RootWidget.Name = TEXT("GeneratedRoot");
	Data.RootWidget.Bounds.W = Data.RootWidget.Bounds.H = 100;
	FUMGWidgetNode Slot;
	Slot.Id = TEXT("content-slot");
	Slot.Name = TEXT("D2U_Content_0");
	Slot.Type = TEXT("NamedSlot");
	Slot.Bounds.W = Slot.Bounds.H = 100;
	FUMGWidgetNode Tick;
	Tick.Id = TEXT("tick-shape");
	Tick.Name = TEXT("Tick");
	Tick.Type = TEXT("Spacer");
	Tick.Bounds.W = Tick.Bounds.H = 1;
	Data.RootWidget.Children = {Slot, Tick};
	UWidgetBlueprint* Blueprint = nullptr;
	for (int32 Build = 0; Build < 2; ++Build)
	{
		UWidgetBlueprint* Rebuilt = FUMGGenerator::BuildWidgetBlueprint(Data, Folder, nullptr, nullptr, nullptr, Blueprint);
		if (!TestNotNull(TEXT("Widget Blueprint builds"), Rebuilt)) return false;
		if (Blueprint) TestTrue(TEXT("Rebuild retains the same Blueprint asset"), Rebuilt == Blueprint);
		Blueprint = Rebuilt;
		TestNotNull(TEXT("Generated root name remains stable"), Blueprint->WidgetTree->FindWidget(TEXT("GeneratedRoot")));
		TestNotNull(TEXT("Inherited Tick remains avoided"), Blueprint->WidgetTree->FindWidget(TEXT("Tick_w1")));
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipDefaultObjectValidation);
		if (!TestTrue(TEXT("Rebuilt Blueprint compiles"),
			Blueprint->Status == BS_UpToDate || Blueprint->Status == BS_UpToDateWithWarnings)) return false;
		const TStrongObjectPtr<UUserWidget> Instance(NewObject<UUserWidget>(
			GEditor->GetEditorWorldContext().World(), Blueprint->GeneratedClass));
		if (!TestTrue(TEXT("Rebuilt instance initializes"), Instance->Initialize())) return false;
		const FName SlotName(TEXT("D2U_Content_0"));
		UNamedSlot* NamedSlot = Cast<UNamedSlot>(Instance->WidgetTree->FindWidget(SlotName));
		if (!TestNotNull(TEXT("Generated NamedSlot keeps its authored binding name"), NamedSlot)) return false;
		USpacer* Content = NewObject<USpacer>(Instance.Get());
		Instance->SetContentForSlot(SlotName, Content);
		TestTrue(TEXT("Stable slot name binds actual instance content"), NamedSlot->GetContent() == Content);
	}
	return true;
}

#endif
