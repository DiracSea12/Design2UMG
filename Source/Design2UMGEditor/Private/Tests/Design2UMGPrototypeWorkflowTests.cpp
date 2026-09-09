// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"
#include "Design2UMGConverter.h"
#include "Design2UMGTestUtils.h"
#include "Tests/Design2UMGTestAssetCleanup.h"
#include "FigmaParser.h"
#include "FigmaTestDocBuilder.h"
#include "UMGGenerator.h"
#include "UMGSchemaSourceExtensions.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/WidgetSwitcher.h"
#include "Editor.h"
#include "Extensions/WidgetBlueprintGeneratedClassExtension.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintExtension.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace Design2UMGCoreTests;
using namespace Design2UMGTestUtils;
using Design2UMGEditorTests::DeleteGeneratedAssetsAtPath;

namespace
{
	const FString BasicPrototypeNavigationJson()
	{
		const FString CtaButtonJson = MakeLeafNode(TEXT("cta_button"), TEXT("CTA"), TEXT("RECTANGLE"), 40, 60, 120, 44, FString::Printf(
			TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"2:1\",\"navigation\":\"NAVIGATE\",\"transition\":{\"type\":\"DISSOLVE\",\"duration\":120,\"easing\":{\"type\":\"EASE_IN_AND_OUT\"}}}]}]"),
			*MakeSolidFillJson(0.1, 0.4, 0.9)));
		const FString PageTwoJson = MakeContainerNode(TEXT("2:1"), TEXT("PageTwo"), TEXT("FRAME"), 0, 0, 360, 240,
			MakeTextNode(TEXT("page2_title"), TEXT("Done"), 32, 48, 120, 32, TEXT("Done"),
				TEXT("\"fontFamily\":\"Inter\",\"fontSize\":24,\"fontWeight\":600,\"textAlignHorizontal\":\"LEFT\",\"textAlignVertical\":\"TOP\",\"textDecoration\":\"NONE\",\"letterSpacing\":0,\"lineHeightUnit\":\"AUTO\",\"lineHeightPx\":0")));
		return MakeFigmaDocJson(
			FString::Printf(TEXT("%s,%s"),
				*MakeContainerNode(TEXT("1:1"), TEXT("PageOne"), TEXT("FRAME"), 0, 0, 360, 240, CtaButtonJson),
				*PageTwoJson),
			TEXT(",\"prototypeStartNodeID\":\"1:1\""));
	}

	UWidgetSwitcher* FindPrototypeSwitcherWidget(UUserWidget* WidgetInstance, const FUMGWidgetData& Data)
	{
		const FUMGPrototypeNavigationData& NavigationData = Data.PrototypeNavigation();
		if (!WidgetInstance || !WidgetInstance->WidgetTree || NavigationData.SwitcherWidgetName.IsEmpty())
		{
			return nullptr;
		}

		return Cast<UWidgetSwitcher>(WidgetInstance->WidgetTree->FindWidget(FName(*NavigationData.SwitcherWidgetName)));
	}

	/** Structural purity contract: no plugin-custom (Design2UMG*) extension may be baked into a generated asset. */
	template <typename TContainer>
	int32 CountDesign2UMGExtensions(const TContainer& Extensions)
	{
		int32 Count = 0;
		for (const auto& Extension : Extensions)
		{
			const UObject* ExtensionObject = Extension;
			if (ExtensionObject && ExtensionObject->GetClass()->GetName().StartsWith(TEXT("Design2UMG")))
			{
				++Count;
			}
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeGraphNavigationWorkflowTest,
	"Design2UMG.WidgetBlueprint.FigmaPrototypeGraphNavigation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeGraphNavigationWorkflowTest::RunTest(const FString& Parameters)
{
	FDesignDocument Document;
	TestTrue(TEXT("Parse prototype navigation fixture"),
		FFigmaParser::ParseDocument(BasicPrototypeNavigationJson(), Document));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Prototype navigation data converts"), Data.PrototypeNavigation().IsValid());
	if (!Data.PrototypeNavigation().IsValid())
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/FigmaPrototypeGraphNav");
	DeleteGeneratedAssetsAtPath(OutputPath);

	UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, OutputPath);
	TestTrue(TEXT("Build prototype navigation widget blueprint"), Blueprint != nullptr);
	if (!Blueprint)
	{
		return false;
	}

	FUMGGenerator::SaveWidgetBlueprint(Blueprint, nullptr, &Data.PrototypeNavigation());
	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass);
	TestTrue(TEXT("Generated class exists"), GeneratedClass != nullptr);
	if (!GeneratedClass)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	TestEqual(TEXT("Generated class carries no plugin-custom extensions"),
		CountDesign2UMGExtensions(GeneratedClass->GetExtensions(UWidgetBlueprintGeneratedClassExtension::StaticClass())), 0);
	TestEqual(TEXT("Widget blueprint carries no plugin-custom extensions"),
		CountDesign2UMGExtensions(UWidgetBlueprintExtension::GetExtensions(Blueprint)), 0);

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	UUserWidget* WidgetInstance = World ? CreateWidget<UUserWidget>(World, GeneratedClass) : nullptr;
	TestTrue(TEXT("Prototype navigation widget instance created"), WidgetInstance != nullptr);
	if (!WidgetInstance || !WidgetInstance->WidgetTree)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	WidgetInstance->TakeWidget();
	UWidgetSwitcher* Switcher = FindPrototypeSwitcherWidget(WidgetInstance, Data);
	UButton* TriggerButton = Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("cta_button")));
	TestTrue(TEXT("Navigation switcher exists"), Switcher != nullptr);
	TestTrue(TEXT("Navigation trigger button exists"), TriggerButton != nullptr);
	if (!Switcher || !TriggerButton)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	TestEqual(TEXT("Navigation initial page index"), Switcher->GetActiveWidgetIndex(), 0);
	TriggerButton->OnClicked.Broadcast();
	TestEqual(TEXT("Wired click switches to the bound page"), Switcher->GetActiveWidgetIndex(), 1);

	WidgetInstance->RemoveFromParent();
	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

#endif
