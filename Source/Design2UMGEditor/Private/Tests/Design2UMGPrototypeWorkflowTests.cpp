// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"
#include "UMGWidgetTreeBuilder.h"
#include "DesignImportPlanning.h"
#include "Design2UMGConverter.h"
#include "Design2UMGTestUtils.h"
#include "Tests/Design2UMGTestAssetCleanup.h"
#include "FigmaParser.h"
#include "FigmaTestDocBuilder.h"
#include "UMGGenerator.h"
#include "DesignImportPipeline.h"
#include "UMGJsonSerializer.h"
#include "Misc/ScopeExit.h"
#include "UMGSchemaSourceExtensions.h"
#include "Animation/WidgetAnimation.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/CanvasPanel.h"
#include "Animation/UMGSequenceTickManager.h"
#include "Framework/Application/SlateApplication.h"
#include "TimerManager.h"
#include "Editor.h"
#include "Engine/LatentActionManager.h"
#include "Misc/EngineVersionComparison.h"
#if !UE_VERSION_OLDER_THAN(5, 1, 0)
#include "Extensions/WidgetBlueprintGeneratedClassExtension.h"
#include "WidgetBlueprintExtension.h"
#endif
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

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
			TEXT(",\"prototypeStartNodeID\":\"1:1\",\"design2umgPrototypeDurationUnit\":\"MILLISECONDS\""));
	}

	UCanvasPanel* FindPrototypePageHostWidget(UUserWidget* WidgetInstance, const FUMGWidgetData& Data)
	{
		const FUMGPrototypeNavigationData& NavigationData = Data.PrototypeNavigation();
		if (!WidgetInstance || !WidgetInstance->WidgetTree || NavigationData.PageHostWidgetName.IsEmpty())
		{
			return nullptr;
		}

		return Cast<UCanvasPanel>(WidgetInstance->WidgetTree->FindWidget(FName(*NavigationData.PageHostWidgetName)));
	}

	int32 CurrentPage(UUserWidget* Instance)
	{
		const auto* Property = FindFProperty<FIntProperty>(Instance->GetClass(), TEXT("__d2u_current_page"));
		return Property ? Property->GetPropertyValue_InContainer(Instance) : INDEX_NONE;
	}

	void TickPrototype(UUserWidget* Instance, float Seconds)
	{
		// A timer set after this frame's timer tick stays pending until the next one; activate it first.
		++GFrameCounter;
		Instance->GetWorld()->GetTimerManager().Tick(0.0f);
		++GFrameCounter;
		UUMGSequenceTickManager::Get(Instance)->OnWidgetTicked(Instance);
		FSlateApplication::Get().OnPreTick().Broadcast(Seconds);
		Instance->GetWorld()->GetLatentActionManager().BeginFrame();
		Instance->GetWorld()->GetLatentActionManager().ProcessLatentActions(Instance, Seconds);
		Instance->GetWorld()->GetTimerManager().Tick(Seconds);
		FSlateApplication::Get().OnPostTick().Broadcast(Seconds);
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

	int32 CountGeneratedDesign2UMGExtensions(UWidgetBlueprintGeneratedClass& GeneratedClass)
	{
#if UE_VERSION_OLDER_THAN(5, 1, 0)
		return 0;
#else
		return CountDesign2UMGExtensions(
			GeneratedClass.GetExtensions(UWidgetBlueprintGeneratedClassExtension::StaticClass()));
#endif
	}

	int32 CountBlueprintDesign2UMGExtensions(UWidgetBlueprint& Blueprint)
	{
#if UE_VERSION_OLDER_THAN(5, 1, 0)
		return 0;
#else
		return CountDesign2UMGExtensions(UWidgetBlueprintExtension::GetExtensions(&Blueprint));
#endif
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeRootPaintPlaybackTest,
	"Design2UMG.WidgetBlueprint.FigmaPrototypeRootPaintPlayback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeRootPaintPlaybackTest::RunTest(const FString&)
{
	const FString Action = TEXT(",\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"navigation\":\"NAVIGATE\",\"destinationId\":\"B\",\"transition\":{\"type\":\"SMART_ANIMATE\",\"duration\":300,\"easing\":{\"type\":\"LINEAR\"}}}]}]");
	const FString A = MakeContainerNode(TEXT("A"), TEXT("Card"), TEXT("FRAME"), 0, 0, 160, 100, FString(),
		TEXT(",\"opacity\":0.8,\"fills\":[") + MakeSolidFillJson(1, 0, 0) + TEXT("]") + Action);
	const FString B = MakeContainerNode(TEXT("B"), TEXT("Card"), TEXT("FRAME"), 0, 0, 160, 100, FString(),
		TEXT(",\"opacity\":0.4,\"fills\":[") + MakeSolidFillJson(0, 0, 1) + TEXT("]"));
	FDesignDocument Document;
	if (!TestTrue(TEXT("Interactive source paint fixture parses"), FFigmaParser::ParseDocument(MakeFigmaDocJson(A,
		FString::Printf(TEXT(",\"design2umgPrototypeDurationUnit\":\"MILLISECONDS\",\"design2umgPrototypePages\":[%s]"), *B)), Document))) return false;
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	const FString Folder = TEXT("/Game/Design2UMG/_TestOutput/RootPaintPlayback");
	DeleteGeneratedAssetsAtPath(Folder);
	ON_SCOPE_EXIT { DeleteGeneratedAssetsAtPath(Folder); };
	FUMGPrototypeNavigationData Navigation;
	UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, Folder, nullptr, nullptr, &Navigation);
	if (!TestNotNull(TEXT("Interactive source paint fixture builds"), Blueprint)
		|| !TestTrue(TEXT("Interactive source paint fixture compiles"), FUMGGenerator::SaveWidgetBlueprint(Blueprint, nullptr, &Navigation).IsSuccess())) return false;
	if (!TestTrue(*FString::Printf(TEXT("Root paint has native animation: %s"), *Navigation.ClickBindings[0].PrimaryStep().Transition.Diagnostic),
		!Navigation.ClickBindings[0].PrimaryStep().Transition.WidgetAnimationName.IsEmpty())) return false;
	UUserWidget* Instance = CreateWidget<UUserWidget>(GEditor->GetEditorWorldContext().World(), Blueprint->GeneratedClass.Get());
	if (!TestNotNull(TEXT("Interactive source paint instantiates"), Instance)) return false;
	const TSharedRef<SWidget> Slate = Instance->TakeWidget();
	ON_SCOPE_EXIT { Instance->RemoveFromParent(); };
	// The root trigger carries its design name; graph wiring resolves it by that base name.
	auto* Button = FUMGWidgetTreeBuilder::FindUniqueTreeWidget<UButton>(Instance->WidgetTree, TEXT("Card"));
	if (!TestNotNull(TEXT("The source root remains the real input Button"), Button)) return false;
	UWidget* Visual = Button->GetChildAt(0);
	const auto ReadColor = [&]
	{
		if (auto* Border = Cast<UBorder>(Visual)) return Border->GetBrushColor() * Border->Background.TintColor.GetSpecifiedColor();
		if (auto* Image = Cast<UImage>(Visual)) return Image->GetBrush().TintColor.GetSpecifiedColor() * Image->GetColorAndOpacity();
		return FLinearColor::Transparent;
	};
	TestTrue(TEXT("Initial root visual has its authored red paint"), ReadColor().Equals(FLinearColor::Red, 0.001f));
	Button->OnClicked.Broadcast();
	TickPrototype(Instance, 0);
	TickPrototype(Instance, 0.15f);
	const FLinearColor Middle = ReadColor();
	TestTrue(TEXT("Click animates the actual visual owner's paint"), Middle.R > 0 && Middle.R < 1 && Middle.B > 0 && Middle.B < 1);
	TestTrue(TEXT("Root opacity still animates on the whole input Button"), Button->GetRenderOpacity() > 0.4f && Button->GetRenderOpacity() < 0.8f);
	return true;
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

	FUMGPrototypeNavigationData NavigationData;
	UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(
		Data, OutputPath, nullptr, nullptr, &NavigationData);
	TestTrue(TEXT("Build prototype navigation widget blueprint"), Blueprint != nullptr);
	if (!Blueprint)
	{
		return false;
	}

	FUMGGenerator::SaveWidgetBlueprint(Blueprint, nullptr, &NavigationData);
	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass);
	TestTrue(TEXT("Generated class exists"), GeneratedClass != nullptr);
	if (!GeneratedClass)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	TestEqual(TEXT("Generated class carries no plugin-custom extensions"),
		CountGeneratedDesign2UMGExtensions(*GeneratedClass), 0);
	TestEqual(TEXT("Widget blueprint carries no plugin-custom extensions"),
		CountBlueprintDesign2UMGExtensions(*Blueprint), 0);

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	UUserWidget* WidgetInstance = World ? CreateWidget<UUserWidget>(World, GeneratedClass) : nullptr;
	TestTrue(TEXT("Prototype navigation widget instance created"), WidgetInstance != nullptr);
	if (!WidgetInstance || !WidgetInstance->WidgetTree)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	WidgetInstance->TakeWidget();
	UCanvasPanel* PageHost = FindPrototypePageHostWidget(WidgetInstance, Data);
	UButton* TriggerButton = Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("CTA")));
	TestTrue(TEXT("Navigation page host exists"), PageHost != nullptr);
	TestTrue(TEXT("Navigation trigger button exists"), TriggerButton != nullptr);
	if (!PageHost || !TriggerButton)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	TestEqual(TEXT("Navigation initial page index"), CurrentPage(WidgetInstance), 0);
	TriggerButton->OnClicked.Broadcast();
	// Both real pages participate in the shared clip; the current-page identity changes only at completion.
	TestEqual(TEXT("Page switch waits for the transition delay"), CurrentPage(WidgetInstance), 0);

	// Advance the native animation and timer clocks past the source transition duration.
	if (World)
	{
		TickPrototype(WidgetInstance, 0.5f);
	}
	TestEqual(TEXT("Wired click switches to the bound page after the transition delay"),
		CurrentPage(WidgetInstance), 1);

	const FName AnimationName(*NavigationData.ClickBindings[0].PrimaryStep().Transition.WidgetAnimationName);
	const FObjectProperty* AnimationProperty = FindFProperty<FObjectProperty>(GeneratedClass, AnimationName);
	UWidgetAnimation* Animation = AnimationProperty
		? Cast<UWidgetAnimation>(AnimationProperty->GetObjectPropertyValue_InContainer(WidgetInstance)) : nullptr;
	TestNotNull(TEXT("Instance carries the single page transition animation"), Animation);
	TestFalse(TEXT("The shared clip stops after page commit"), Animation && WidgetInstance->IsAnimationPlaying(Animation));
	TestTrue(TEXT("The outgoing page is collapsed after commit"), PageHost->GetChildAt(0)->GetVisibility() == ESlateVisibility::Collapsed);
	TestTrue(TEXT("The committed page is visible"), PageHost->GetChildAt(1)->GetVisibility() != ESlateVisibility::Collapsed);

	WidgetInstance->RemoveFromParent();
	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreviewContextPrototypeGenerationTest,
    "Design2UMG.PreviewContext.PrototypeGeneration", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPreviewContextPrototypeGenerationTest::RunTest(const FString& Parameters)
{
    FDesignDocument Document;
    if (!TestTrue(TEXT("Parse interactive input"), FFigmaParser::ParseDocument(BasicPrototypeNavigationJson(), Document))) return false;
    const FUMGWidgetData Original = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
    for (int32 Mode = 0; Mode < 3; ++Mode)
    {
        FUMGWidgetData Data = Original;
        if (Mode == 2) Data.RootWidget.Render.RenderOpacity = 0.8f;
        if (Mode != 0)
        {
            FPreviewRefreshInput Input;
            Input.OriginalData = MakeShared<FUMGWidgetData>(Original);
            Input.EditedJson = FUMGJsonSerializer::Serialize(Data);
            Input.bResolveImages = false;
            auto Refreshed = DesignImportPlanning::AnalyzePreviewRefreshInput(Input);
            if (!TestTrue(*Refreshed.ParseError, Refreshed.bSucceeded)) continue;
            Data = MoveTemp(Refreshed.Data);
        }
        const FString Folder = TEXT("/Game/Design2UMG/_TestOutput/PreviewNavigation_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
        ON_SCOPE_EXIT { DeleteGeneratedAssetsAtPath(Folder); };
        FUMGPrototypeNavigationData Navigation;
        UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, Folder, nullptr, nullptr, &Navigation);
        if (!TestNotNull(TEXT("Direct and preview paths build prototype"), Blueprint)) continue;
        FUMGGenerator::SaveWidgetBlueprint(Blueprint, nullptr, &Navigation);
        UWorld* World = GEditor->GetEditorWorldContext().World();
        UUserWidget* Instance = CreateWidget<UUserWidget>(World, Cast<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass));
        if (!TestNotNull(TEXT("Prototype instantiates"), Instance)) continue;
        Instance->TakeWidget();
        ON_SCOPE_EXIT { Instance->RemoveFromParent(); };
        UCanvasPanel* PageHost = FindPrototypePageHostWidget(Instance, Data);
        UButton* Trigger = Cast<UButton>(Instance->WidgetTree->FindWidget(TEXT("CTA")));
        if (!TestNotNull(TEXT("PageHost survives preview"), PageHost) || !TestNotNull(TEXT("Trigger survives preview"), Trigger)) continue;
        TestEqual(TEXT("Initial page retained"), CurrentPage(Instance), 0);
        Trigger->OnClicked.Broadcast();
        TickPrototype(Instance, 1.0f);
        TestEqual(TEXT("Real click reaches target after preview"), CurrentPage(Instance), 1);
    }
    return true;
}

#endif
