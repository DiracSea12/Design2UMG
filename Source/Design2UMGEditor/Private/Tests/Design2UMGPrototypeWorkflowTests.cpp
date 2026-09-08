// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"
#include "Design2UMGConverter.h"
#include "Design2UMGPrototypeNavigationExtensions.h"
#include "Design2UMGPrototypeNavigationTagHelper.h"
#include "Design2UMGSettings.h"
#include "Design2UMGTestUtils.h"
#include "Tests/Design2UMGTestAssetCleanup.h"
#include "FigmaParser.h"
#include "FigmaTestDocBuilder.h"
#include "UMGGenerator.h"
#include "UMGSchemaSourceExtensions.h"
#include "Animation/WidgetAnimation.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Overlay.h"
#include "Components/PanelWidget.h"
#include "Components/WidgetSwitcher.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "InputCoreTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace Design2UMGCoreTests;
using namespace Design2UMGTestUtils;
using Design2UMGEditorTests::DeleteGeneratedAssetsAtPath;

extern bool GDesign2UMGPrototypeNavigationCaptureOpenUrl;
extern FString GDesign2UMGPrototypeNavigationLastOpenedUrl;

namespace
{
	struct FScopedPrototypeOpenUrlCapture
	{
		FScopedPrototypeOpenUrlCapture()
		{
			bSavedCapture = GDesign2UMGPrototypeNavigationCaptureOpenUrl;
			SavedLastOpenedUrl = GDesign2UMGPrototypeNavigationLastOpenedUrl;
			GDesign2UMGPrototypeNavigationCaptureOpenUrl = true;
			GDesign2UMGPrototypeNavigationLastOpenedUrl.Reset();
		}

		~FScopedPrototypeOpenUrlCapture()
		{
			GDesign2UMGPrototypeNavigationCaptureOpenUrl = bSavedCapture;
			GDesign2UMGPrototypeNavigationLastOpenedUrl = SavedLastOpenedUrl;
		}

		bool bSavedCapture = false;
		FString SavedLastOpenedUrl;
	};

	struct FScopedPrototypeNavigationGenerationMode
	{
		explicit FScopedPrototypeNavigationGenerationMode(
			EDesign2UMGPrototypeNavigationGenerationMode InMode)
		{
			Settings = UDesign2UMGSettings::Get();
			if (Settings)
			{
				SavedMode = Settings->PrototypeNavigationGenerationMode;
				Settings->PrototypeNavigationGenerationMode = InMode;
			}
		}

		~FScopedPrototypeNavigationGenerationMode()
		{
			if (Settings)
			{
				Settings->PrototypeNavigationGenerationMode = SavedMode;
			}
		}

		TObjectPtr<UDesign2UMGSettings> Settings;
		EDesign2UMGPrototypeNavigationGenerationMode SavedMode =
			EDesign2UMGPrototypeNavigationGenerationMode::RuntimeExtension;
	};

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

	UOverlay* FindPrototypeOverlayHostWidget(UUserWidget* WidgetInstance, const FUMGWidgetData& Data)
	{
		const FUMGPrototypeNavigationData& NavigationData = Data.PrototypeNavigation();
		if (!WidgetInstance || !WidgetInstance->WidgetTree || NavigationData.OverlayHostWidgetName.IsEmpty())
		{
			return nullptr;
		}

		return Cast<UOverlay>(WidgetInstance->WidgetTree->FindWidget(FName(*NavigationData.OverlayHostWidgetName)));
	}

	void TickCoreTickerFor(const float TotalSeconds, const float StepSeconds = 1.0f / 60.0f)
	{
		float Elapsed = 0.0f;
		while (Elapsed < TotalSeconds)
		{
			const float CurrentStep = FMath::Min(StepSeconds, TotalSeconds - Elapsed);
			FTSTicker::GetCoreTicker().Tick(CurrentStep);
			Elapsed += CurrentStep;
		}
	}

	UWidgetAnimation* FindGeneratedAnimation(
		const UWidgetBlueprintGeneratedClass* GeneratedClass,
		const TCHAR* AnimationName)
	{
		if (!GeneratedClass || !AnimationName)
		{
			return nullptr;
		}

		const FName TargetName(AnimationName);
		for (UWidgetAnimation* Animation : GeneratedClass->Animations)
		{
			if (!Animation)
			{
				continue;
			}

			if (Animation->GetFName() == TargetName
				|| Animation->GetDisplayLabel().Equals(AnimationName, ESearchCase::IgnoreCase))
			{
				return Animation;
			}
		}

		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeNavigationGenerationModeDefaultTest,
	"Design2UMG.Unit.PrototypeNavigationGenerationMode.DefaultDisabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPrototypeNavigationGenerationModeDefaultTest::RunTest(const FString& Parameters)
{
	// The product default is Disabled: runtime-extension classes are editor-module types
	// that cook strips from packaged games. A NewObject instance only carries this default
	// when the CDO has not been overridden by the editor's per-project ini, so pin the CDO
	// around the check to keep the assertion independent of local config state.
	UDesign2UMGSettings* SettingsCDO = UDesign2UMGSettings::Get();
	const EDesign2UMGPrototypeNavigationGenerationMode SavedMode = SettingsCDO->PrototypeNavigationGenerationMode;
	SettingsCDO->PrototypeNavigationGenerationMode = EDesign2UMGPrototypeNavigationGenerationMode::Disabled;
	const UDesign2UMGSettings* FreshSettings =
		NewObject<UDesign2UMGSettings>(GetTransientPackage());
	TestEqual(
		TEXT("Prototype navigation generation defaults to disabled"),
		FreshSettings->PrototypeNavigationGenerationMode,
		EDesign2UMGPrototypeNavigationGenerationMode::Disabled);
	SettingsCDO->PrototypeNavigationGenerationMode = SavedMode;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeNavigationRuntimeWorkflowTest,
	"Design2UMG.WidgetBlueprint.FigmaPrototypeNavigationRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeNavigationRuntimeWorkflowTest::RunTest(const FString& Parameters)
{
	FScopedPrototypeNavigationGenerationMode ScopedMode(
		EDesign2UMGPrototypeNavigationGenerationMode::RuntimeExtension);

	const FString Json = BasicPrototypeNavigationJson();

	FDesignDocument Document;
	TestTrue(TEXT("Parse prototype navigation fixture"), FFigmaParser::ParseDocument(Json, Document));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	const FUMGPrototypeNavigationData& NavigationData = Data.PrototypeNavigation();
	TestTrue(TEXT("Prototype navigation data available"), NavigationData.IsValid());
	if (!NavigationData.IsValid())
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/FigmaPrototypeNavigation");
	DeleteGeneratedAssetsAtPath(OutputPath);

	UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, OutputPath);
	TestTrue(TEXT("Build prototype navigation widget blueprint"), Blueprint != nullptr);
	if (!Blueprint)
	{
		return false;
	}

	FUMGGenerator::SaveWidgetBlueprint(Blueprint);
	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass);
	TestTrue(TEXT("Generated class exists"), GeneratedClass != nullptr);
	const UDesign2UMGPrototypeNavigationClassExtension* RuntimeExtension =
		GeneratedClass ? GeneratedClass->GetExtension<UDesign2UMGPrototypeNavigationClassExtension>() : nullptr;
	TestTrue(TEXT("Prototype runtime class extension attached"), RuntimeExtension != nullptr);
	if (!GeneratedClass || !RuntimeExtension)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UWidgetAnimation* DissolveOutgoingAnimation =
		FindGeneratedAnimation(GeneratedClass, TEXT("__d2u_nav_anim_000_out"));
	UWidgetAnimation* DissolveIncomingAnimation =
		FindGeneratedAnimation(GeneratedClass, TEXT("__d2u_nav_anim_000_in"));
	TestTrue(TEXT("Prototype dissolve outgoing animation generated"), DissolveOutgoingAnimation != nullptr);
	TestTrue(TEXT("Prototype dissolve incoming animation generated"), DissolveIncomingAnimation != nullptr);

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestTrue(TEXT("Editor world exists"), World != nullptr);
	UUserWidget* WidgetInstance = World ? CreateWidget<UUserWidget>(World, GeneratedClass) : nullptr;
	TestTrue(TEXT("Prototype widget instance created"), WidgetInstance != nullptr);
	if (!WidgetInstance || !WidgetInstance->WidgetTree)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	WidgetInstance->TakeWidget();
	WidgetInstance->AddToViewport();
	WidgetInstance->SetKeyboardFocus();

	UWidgetSwitcher* Switcher = FindPrototypeSwitcherWidget(WidgetInstance, Data);
	UButton* TriggerButton = Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("cta_button")));
	TestTrue(TEXT("Prototype switcher exists on widget instance"), Switcher != nullptr);
	TestTrue(TEXT("Prototype trigger button exists on widget instance"), TriggerButton != nullptr);
	if (!Switcher || !TriggerButton)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	TestEqual(TEXT("Prototype initial page index"), Switcher->GetActiveWidgetIndex(), 0);
	const float InitialOpacity = Switcher->GetRenderOpacity();
	TriggerButton->OnClicked.Broadcast();
	TickCoreTickerFor(0.03f);
	TestEqual(TEXT("Prototype dissolve keeps old page before halfway"), Switcher->GetActiveWidgetIndex(), 0);
	TestTrue(TEXT("Prototype dissolve lowers opacity during transition"), Switcher->GetRenderOpacity() < InitialOpacity - 0.01f);
	TickCoreTickerFor(0.04f);
	TestEqual(TEXT("Prototype dissolve switches page after halfway"), Switcher->GetActiveWidgetIndex(), 1);
	TickCoreTickerFor(0.08f);
	TestEqual(TEXT("Prototype dissolve finishes on target page"), Switcher->GetActiveWidgetIndex(), 1);
	TestTrue(TEXT("Prototype dissolve restores opacity"), FMath::IsNearlyEqual(Switcher->GetRenderOpacity(), InitialOpacity, 0.001f));
	TestTrue(TEXT("Prototype dissolve restores translation"), Switcher->GetRenderTransform().Translation.IsNearlyZero());

	WidgetInstance->RemoveFromParent();
	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeTagHelperRuntimeWorkflowTest,
	"Design2UMG.WidgetBlueprint.FigmaPrototypeTagHelperRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeTagHelperRuntimeWorkflowTest::RunTest(const FString& Parameters)
{
	FScopedPrototypeNavigationGenerationMode ScopedMode(
		EDesign2UMGPrototypeNavigationGenerationMode::TagHelper);

	FDesignDocument Document;
	TestTrue(TEXT("Parse prototype tag helper fixture"),
		FFigmaParser::ParseDocument(BasicPrototypeNavigationJson(), Document));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Prototype tag helper data available"), Data.PrototypeNavigation().IsValid());
	if (!Data.PrototypeNavigation().IsValid())
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/FigmaPrototypeTagHelper");
	DeleteGeneratedAssetsAtPath(OutputPath);

	UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, OutputPath);
	TestTrue(TEXT("Build prototype tag helper widget blueprint"), Blueprint != nullptr);
	if (!Blueprint)
	{
		return false;
	}

	FUMGGenerator::SaveWidgetBlueprint(Blueprint);
	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass);
	const UDesign2UMGPrototypeNavigationClassExtension* RuntimeExtension =
		GeneratedClass ? GeneratedClass->GetExtension<UDesign2UMGPrototypeNavigationClassExtension>() : nullptr;
	const UDesign2UMGPrototypeNavigationTagHelperClassExtension* TagMetadata =
		GeneratedClass ? GeneratedClass->GetExtension<UDesign2UMGPrototypeNavigationTagHelperClassExtension>() : nullptr;
	TestTrue(TEXT("Generated class exists"), GeneratedClass != nullptr);
	TestNull(TEXT("TagHelper mode does not attach prototype runtime class extension"), RuntimeExtension);
	TestNotNull(TEXT("TagHelper mode attaches data-only helper metadata"), TagMetadata);
	if (!GeneratedClass || !TagMetadata)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestEqual(TEXT("TagHelper metadata switcher name"), TagMetadata->GetSwitcherWidgetName(), FName(*Data.PrototypeNavigation().SwitcherWidgetName));
	TestEqual(TEXT("TagHelper metadata page count"), TagMetadata->GetPageIds().Num(), 2);
	TestEqual(TEXT("TagHelper metadata binding count"), TagMetadata->GetBindings().Num(), 1);

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	UUserWidget* WidgetInstance = World ? CreateWidget<UUserWidget>(World, GeneratedClass) : nullptr;
	TestTrue(TEXT("Prototype tag helper widget instance created"), WidgetInstance != nullptr);
	if (!WidgetInstance || !WidgetInstance->WidgetTree)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	WidgetInstance->TakeWidget();
	UWidgetSwitcher* Switcher = FindPrototypeSwitcherWidget(WidgetInstance, Data);
	UButton* TriggerButton = Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("cta_button")));
	TestTrue(TEXT("TagHelper switcher exists"), Switcher != nullptr);
	TestTrue(TEXT("TagHelper trigger exists"), TriggerButton != nullptr);
	if (!Switcher || !TriggerButton)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestTrue(TEXT("TagHelper setup binds click navigation"),
		UDesign2UMGPrototypeNavigationTagHelper::SetupNavigation(Switcher, WidgetInstance));

	TestEqual(TEXT("TagHelper initial page index"), Switcher->GetActiveWidgetIndex(), 0);
	TriggerButton->OnClicked.Broadcast();
	TestEqual(TEXT("TagHelper click switches to target page"), Switcher->GetActiveWidgetIndex(), 1);

	WidgetInstance->RemoveFromParent();
	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeDisabledRuntimeWorkflowTest,
	"Design2UMG.WidgetBlueprint.FigmaPrototypeDisabledRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeDisabledRuntimeWorkflowTest::RunTest(const FString& Parameters)
{
	FScopedPrototypeNavigationGenerationMode ScopedMode(
		EDesign2UMGPrototypeNavigationGenerationMode::Disabled);

	FDesignDocument Document;
	TestTrue(TEXT("Parse prototype disabled fixture"),
		FFigmaParser::ParseDocument(BasicPrototypeNavigationJson(), Document));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Prototype disabled mode still converts navigation data"), Data.PrototypeNavigation().IsValid());
	if (!Data.PrototypeNavigation().IsValid())
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/FigmaPrototypeDisabled");
	DeleteGeneratedAssetsAtPath(OutputPath);

	UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, OutputPath);
	TestTrue(TEXT("Build prototype disabled widget blueprint"), Blueprint != nullptr);
	if (!Blueprint)
	{
		return false;
	}

	FUMGGenerator::SaveWidgetBlueprint(Blueprint);
	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass);
	const UDesign2UMGPrototypeNavigationClassExtension* RuntimeExtension =
		GeneratedClass ? GeneratedClass->GetExtension<UDesign2UMGPrototypeNavigationClassExtension>() : nullptr;
	const UDesign2UMGPrototypeNavigationTagHelperClassExtension* TagMetadata =
		GeneratedClass ? GeneratedClass->GetExtension<UDesign2UMGPrototypeNavigationTagHelperClassExtension>() : nullptr;
	TestTrue(TEXT("Generated class exists"), GeneratedClass != nullptr);
	TestNull(TEXT("Disabled mode does not attach prototype runtime class extension"), RuntimeExtension);
	TestNull(TEXT("Disabled mode does not attach tag helper metadata"), TagMetadata);
	if (!GeneratedClass)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	UUserWidget* WidgetInstance = World ? CreateWidget<UUserWidget>(World, GeneratedClass) : nullptr;
	TestTrue(TEXT("Prototype disabled widget instance created"), WidgetInstance != nullptr);
	if (!WidgetInstance || !WidgetInstance->WidgetTree)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	WidgetInstance->TakeWidget();
	UWidgetSwitcher* Switcher = FindPrototypeSwitcherWidget(WidgetInstance, Data);
	UButton* TriggerButton = Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("cta_button")));
	TestTrue(TEXT("Disabled switcher exists"), Switcher != nullptr);
	TestTrue(TEXT("Disabled trigger button exists"), TriggerButton != nullptr);
	if (!Switcher || !TriggerButton)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestFalse(TEXT("Disabled tag helper setup does not bind"),
		UDesign2UMGPrototypeNavigationTagHelper::SetupNavigation(Switcher, WidgetInstance));

	TestEqual(TEXT("Disabled initial page index"), Switcher->GetActiveWidgetIndex(), 0);
	TriggerButton->OnClicked.Broadcast();
	TestEqual(TEXT("Disabled click does not switch page"), Switcher->GetActiveWidgetIndex(), 0);

	WidgetInstance->RemoveFromParent();
	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeTagHelperOpenLinkNoMetadataWorkflowTest,
	"Design2UMG.WidgetBlueprint.FigmaPrototypeTagHelperOpenLinkNoMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeTagHelperOpenLinkNoMetadataWorkflowTest::RunTest(const FString& Parameters)
{
	FScopedPrototypeNavigationGenerationMode ScopedMode(
		EDesign2UMGPrototypeNavigationGenerationMode::TagHelper);

	const FString Json = MakeFigmaDocJson(
		MakeContainerNode(TEXT("1:1"), TEXT("PageOne"), TEXT("FRAME"), 0, 0, 360, 240,
			MakeLeafNode(TEXT("link_button"), TEXT("Link"), TEXT("RECTANGLE"), 40, 120, 120, 44, FString::Printf(
				TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"URL\",\"url\":\"https://example.com/tag-helper\"}]}]"),
				*MakeSolidFillJson(0.2, 0.7, 0.5)))));

	FDesignDocument Document;
	TestTrue(TEXT("Parse TagHelper open-link fixture"), FFigmaParser::ParseDocument(Json, Document));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	const FUMGPrototypeNavigationData& NavigationData = Data.PrototypeNavigation();
	TestTrue(TEXT("TagHelper open-link navigation data is valid"), NavigationData.IsValid());
	TestFalse(TEXT("TagHelper open-link has no switcher requirement"), NavigationData.RequiresSwitcher());
	if (!NavigationData.IsValid())
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/FigmaPrototypeTagHelperOpenLink");
	DeleteGeneratedAssetsAtPath(OutputPath);

	UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, OutputPath);
	TestTrue(TEXT("Build TagHelper open-link widget blueprint"), Blueprint != nullptr);
	if (!Blueprint)
	{
		return false;
	}

	FUMGGenerator::SaveWidgetBlueprint(Blueprint);
	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass);
	const UDesign2UMGPrototypeNavigationClassExtension* RuntimeExtension =
		GeneratedClass ? GeneratedClass->GetExtension<UDesign2UMGPrototypeNavigationClassExtension>() : nullptr;
	const UDesign2UMGPrototypeNavigationTagHelperClassExtension* TagMetadata =
		GeneratedClass ? GeneratedClass->GetExtension<UDesign2UMGPrototypeNavigationTagHelperClassExtension>() : nullptr;
	TestTrue(TEXT("TagHelper open-link generated class exists"), GeneratedClass != nullptr);
	TestNull(TEXT("TagHelper open-link does not attach runtime navigation extension"), RuntimeExtension);
	TestNull(TEXT("TagHelper open-link does not attach helper metadata"), TagMetadata);

	DeleteGeneratedAssetsAtPath(OutputPath);
	return GeneratedClass != nullptr;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeSpecialActionsRuntimeWorkflowTest,
	"Design2UMG.WidgetBlueprint.FigmaPrototypeSpecialActionsRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeSpecialActionsRuntimeWorkflowTest::RunTest(const FString& Parameters)
{
	FScopedPrototypeNavigationGenerationMode ScopedMode(
		EDesign2UMGPrototypeNavigationGenerationMode::RuntimeExtension);
	FScopedPrototypeOpenUrlCapture OpenUrlCapture;

	const FString OverlayButtonJson = MakeLeafNode(TEXT("overlay_button"), TEXT("Overlay"), TEXT("RECTANGLE"), 40, 60, 120, 44, FString::Printf(
		TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"2:1\",\"navigation\":\"OVERLAY\",\"transition\":{\"type\":\"DISSOLVE\",\"duration\":120,\"easing\":{\"type\":\"EASE_IN_AND_OUT\"}}}]}]"),
		*MakeSolidFillJson(0.1, 0.4, 0.9)));
	const FString HelpLinkButtonJson = MakeLeafNode(TEXT("link_button"), TEXT("Link"), TEXT("RECTANGLE"), 40, 120, 120, 44, FString::Printf(
		TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"URL\",\"url\":\"https://example.com/help\"}]}]"),
		*MakeSolidFillJson(0.2, 0.7, 0.5)));
	const FString Json = MakeFigmaDocJson(
		FString::Printf(TEXT("%s,%s"),
			*MakeContainerNode(TEXT("1:1"), TEXT("PageOne"), TEXT("FRAME"), 0, 0, 360, 240,
				FString::Printf(TEXT("%s,%s"), *OverlayButtonJson, *HelpLinkButtonJson)),
			*MakeContainerNode(TEXT("2:1"), TEXT("OverlayPage"), TEXT("FRAME"), 0, 0, 360, 240,
				MakeLeafNode(TEXT("back_button"), TEXT("Back"), TEXT("RECTANGLE"), 40, 60, 120, 44, FString::Printf(
					TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"BACK\"}]}]"),
					*MakeSolidFillJson(0.8, 0.3, 0.2))))),
		TEXT(",\"prototypeStartNodeID\":\"1:1\""));

	FDesignDocument Document;
	TestTrue(TEXT("Parse prototype special actions fixture"), FFigmaParser::ParseDocument(Json, Document));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	const FUMGPrototypeNavigationData& NavigationData = Data.PrototypeNavigation();
	TestTrue(TEXT("Prototype special action data available"), NavigationData.IsValid());
	if (!NavigationData.IsValid())
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/FigmaPrototypeSpecialActions");
	DeleteGeneratedAssetsAtPath(OutputPath);

	UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, OutputPath);
	TestTrue(TEXT("Build prototype special action widget blueprint"), Blueprint != nullptr);
	if (!Blueprint)
	{
		return false;
	}

	FUMGGenerator::SaveWidgetBlueprint(Blueprint);
	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass);
	TestTrue(TEXT("Generated class exists"), GeneratedClass != nullptr);
	if (!GeneratedClass)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UWidgetAnimation* OverlayOutgoingAnimation =
		FindGeneratedAnimation(GeneratedClass, TEXT("__d2u_nav_anim_000_out"));
	UWidgetAnimation* OverlayIncomingAnimation =
		FindGeneratedAnimation(GeneratedClass, TEXT("__d2u_nav_anim_000_in"));
	TestTrue(TEXT("Overlay outgoing animation generated"), OverlayOutgoingAnimation != nullptr);
	TestTrue(TEXT("Overlay incoming animation generated"), OverlayIncomingAnimation != nullptr);
	TestTrue(TEXT("Prototype special action generated class exports transition animations"),
		GeneratedClass->Animations.Num() >= 2);

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestTrue(TEXT("Editor world exists"), World != nullptr);
	UUserWidget* WidgetInstance = World ? CreateWidget<UUserWidget>(World, GeneratedClass) : nullptr;
	TestTrue(TEXT("Prototype special action widget instance created"), WidgetInstance != nullptr);
	if (!WidgetInstance || !WidgetInstance->WidgetTree)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	WidgetInstance->TakeWidget();
	WidgetInstance->AddToViewport();

	UWidgetSwitcher* Switcher = FindPrototypeSwitcherWidget(WidgetInstance, Data);
	UOverlay* OverlayHost = FindPrototypeOverlayHostWidget(WidgetInstance, Data);
	UButton* OverlayButton = Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("overlay_button")));
	UButton* BackButton = Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("back_button")));
	UButton* LinkButton = Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("link_button")));
	TestTrue(TEXT("Prototype special action switcher exists"), Switcher != nullptr);
	TestTrue(TEXT("Prototype overlay host exists"), OverlayHost != nullptr);
	TestTrue(TEXT("Prototype overlay trigger exists"), OverlayButton != nullptr);
	TestTrue(TEXT("Prototype back trigger exists"), BackButton != nullptr);
	TestTrue(TEXT("Prototype open link trigger exists"), LinkButton != nullptr);
	if (!Switcher || !OverlayHost || !OverlayButton || !BackButton || !LinkButton)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	TestEqual(TEXT("Prototype special action initial page index"), Switcher->GetActiveWidgetIndex(), 0);
	TestTrue(TEXT("Overlay host starts collapsed"), OverlayHost->GetVisibility() == ESlateVisibility::Collapsed);
	OverlayButton->OnClicked.Broadcast();
	TestEqual(TEXT("Overlay keeps base page active"), Switcher->GetActiveWidgetIndex(), 0);
	TestTrue(TEXT("Overlay host becomes visible"), OverlayHost->GetVisibility() == ESlateVisibility::SelfHitTestInvisible);
	TestTrue(TEXT("Overlay host receives overlay page"), OverlayHost->GetChildrenCount() == 1);
	const auto IsDescendantOf = [](const UWidget* Widget, const UPanelWidget* ExpectedParent)
	{
		const UWidget* Current = Widget;
		while (Current)
		{
			const UPanelWidget* Parent = Current->GetParent();
			if (!Parent)
			{
				return false;
			}
			if (Parent == ExpectedParent)
			{
				return true;
			}
			Current = Parent;
		}
		return false;
	};
	TestTrue(TEXT("Back button now lives under overlay host subtree"), IsDescendantOf(BackButton, OverlayHost));

	BackButton->OnClicked.Broadcast();
	TestEqual(TEXT("Back keeps base page active after closing overlay"), Switcher->GetActiveWidgetIndex(), 0);
	TestTrue(TEXT("Overlay host collapses after back"), OverlayHost->GetVisibility() == ESlateVisibility::Collapsed);
	TestTrue(TEXT("Overlay page removed from overlay host"), OverlayHost->GetChildrenCount() == 0);
	BackButton->OnClicked.Broadcast();
	TestEqual(TEXT("Back with empty history is a no-op"), Switcher->GetActiveWidgetIndex(), 0);

	LinkButton->OnClicked.Broadcast();
	TestEqual(TEXT("Open link keeps current page"), Switcher->GetActiveWidgetIndex(), 0);
	TestEqual(TEXT("Open link captures URL through runtime hook"),
		GDesign2UMGPrototypeNavigationLastOpenedUrl,
		TEXT("https://example.com/help"));

	WidgetInstance->RemoveFromParent();
	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeHoverAndPressRuntimeWorkflowTest,
	"Design2UMG.WidgetBlueprint.FigmaPrototypeHoverAndPressRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeHoverAndPressRuntimeWorkflowTest::RunTest(const FString& Parameters)
{
	FScopedPrototypeNavigationGenerationMode ScopedMode(
		EDesign2UMGPrototypeNavigationGenerationMode::RuntimeExtension);

	const FString Json = MakeFigmaDocJson(
		FString::Printf(TEXT("%s,%s"),
			*MakeContainerNode(TEXT("1:1"), TEXT("PageOne"), TEXT("FRAME"), 0, 0, 360, 240,
				MakeLeafNode(TEXT("hover_button"), TEXT("HoverCTA"), TEXT("RECTANGLE"), 40, 60, 120, 44, FString::Printf(
					TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_HOVER\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"2:1\",\"navigation\":\"NAVIGATE\"}]}]"),
					*MakeSolidFillJson(0.1, 0.4, 0.9)))),
			*MakeContainerNode(TEXT("2:1"), TEXT("PageTwo"), TEXT("FRAME"), 0, 0, 360, 240,
				MakeLeafNode(TEXT("press_button"), TEXT("PressBack"), TEXT("RECTANGLE"), 40, 60, 120, 44, FString::Printf(
					TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_PRESS\"},\"actions\":[{\"type\":\"BACK\"}]}]"),
					*MakeSolidFillJson(0.8, 0.3, 0.2))))),
		TEXT(",\"prototypeStartNodeID\":\"1:1\""));

	FDesignDocument Document;
	TestTrue(TEXT("Parse prototype hover/press fixture"), FFigmaParser::ParseDocument(Json, Document));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	const FUMGPrototypeNavigationData& NavigationData = Data.PrototypeNavigation();
	TestTrue(TEXT("Prototype hover/press data available"), NavigationData.IsValid());
	if (!NavigationData.IsValid())
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/FigmaPrototypeHoverAndPress");
	DeleteGeneratedAssetsAtPath(OutputPath);

	UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, OutputPath);
	TestTrue(TEXT("Build prototype hover/press widget blueprint"), Blueprint != nullptr);
	if (!Blueprint)
	{
		return false;
	}

	FUMGGenerator::SaveWidgetBlueprint(Blueprint);
	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass);
	TestTrue(TEXT("Generated hover/press class exists"), GeneratedClass != nullptr);
	if (!GeneratedClass)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestTrue(TEXT("Editor world exists"), World != nullptr);
	UUserWidget* WidgetInstance = World ? CreateWidget<UUserWidget>(World, GeneratedClass) : nullptr;
	TestTrue(TEXT("Prototype hover/press widget instance created"), WidgetInstance != nullptr);
	if (!WidgetInstance || !WidgetInstance->WidgetTree)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	WidgetInstance->TakeWidget();
	WidgetInstance->AddToViewport();

	UWidgetSwitcher* Switcher = FindPrototypeSwitcherWidget(WidgetInstance, Data);
	UButton* HoverButton = Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("hover_button")));
	UButton* PressButton = Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("press_button")));
	TestTrue(TEXT("Prototype hover/press switcher exists"), Switcher != nullptr);
	TestTrue(TEXT("Prototype hover trigger exists"), HoverButton != nullptr);
	TestTrue(TEXT("Prototype press trigger exists"), PressButton != nullptr);
	if (!Switcher || !HoverButton || !PressButton)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	TestEqual(TEXT("Prototype hover/press initial page index"), Switcher->GetActiveWidgetIndex(), 0);
	HoverButton->OnHovered.Broadcast();
	TestEqual(TEXT("Hover trigger switches to target page"), Switcher->GetActiveWidgetIndex(), 1);
	PressButton->OnPressed.Broadcast();
	TestEqual(TEXT("Press trigger walks back to previous page"), Switcher->GetActiveWidgetIndex(), 0);

	WidgetInstance->RemoveFromParent();
	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeAdvancedTriggersRuntimeWorkflowTest,
	"Design2UMG.WidgetBlueprint.FigmaPrototypeAdvancedTriggersRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeAdvancedTriggersRuntimeWorkflowTest::RunTest(const FString& Parameters)
{
	FScopedPrototypeNavigationGenerationMode ScopedMode(
		EDesign2UMGPrototypeNavigationGenerationMode::RuntimeExtension);

	const FString PageOneChildrenJson = FString::Printf(TEXT("%s,%s,%s"),
		*MakeLeafNode(TEXT("drag_button"), TEXT("DragCTA"), TEXT("RECTANGLE"), 32, 40, 120, 44, FString::Printf(
			TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_DRAG\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"2:1\",\"navigation\":\"NAVIGATE\"}]}]"),
			*MakeSolidFillJson(0.2, 0.7, 0.5))),
		*MakeLeafNode(TEXT("key_down_hotspot"), TEXT("KeyDownHotspot"), TEXT("RECTANGLE"), 32, 104, 120, 44, FString::Printf(
			TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_KEY_DOWN\",\"keyCodes\":[\"ENTER\"]},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"2:1\",\"navigation\":\"NAVIGATE\"}]}]"),
			*MakeSolidFillJson(0.3, 0.3, 0.8))),
		*MakeLeafNode(TEXT("conditional_false_button"), TEXT("ConditionalFalse"), TEXT("RECTANGLE"), 32, 168, 140, 44, FString::Printf(
			TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":["
				 "{\"type\":\"SET_VARIABLE\",\"variableId\":\"var:isPrimary\",\"variableValue\":{\"resolvedType\":\"BOOLEAN\",\"value\":false}},"
				 "{\"type\":\"CONDITIONAL\",\"conditionalBlocks\":["
				 "{\"condition\":{\"type\":\"VARIABLE_ALIAS\",\"id\":\"var:isPrimary\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"2:1\",\"navigation\":\"NAVIGATE\"}]},"
				 "{\"condition\":{\"resolvedType\":\"BOOLEAN\",\"value\":true},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"3:1\",\"navigation\":\"NAVIGATE\"}]}"
					 "]}]}]"),
			*MakeSolidFillJson(0.8, 0.4, 0.1))));
	const FString Json = MakeFigmaDocJson(
		FString::Printf(TEXT("%s,%s,%s"),
			*MakeContainerNode(TEXT("1:1"), TEXT("PageOne"), TEXT("FRAME"), 0, 0, 360, 240, PageOneChildrenJson),
			*MakeContainerNode(TEXT("2:1"), TEXT("PageTwo"), TEXT("FRAME"), 0, 0, 360, 240,
				MakeLeafNode(TEXT("key_up_hotspot"), TEXT("KeyUpHotspot"), TEXT("RECTANGLE"), 32, 40, 100, 40, FString::Printf(
					TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_KEY_UP\",\"keyCodes\":[\"ESCAPE\"]},\"actions\":[{\"type\":\"BACK\"}]}]"),
					*MakeSolidFillJson(0.2, 0.7, 0.4)))),
			*MakeContainerNode(TEXT("3:1"), TEXT("PageThree"), TEXT("FRAME"), 0, 0, 360, 240,
				MakeTextNode(TEXT("page_three_title"), TEXT("PageThreeTitle"), 24, 32, 140, 28, TEXT("Fallback"),
					TEXT("\"fontSize\":20")))),
		TEXT(",\"prototypeStartNodeID\":\"1:1\""));

	FDesignDocument Document;
	TestTrue(TEXT("Parse prototype advanced trigger fixture"), FFigmaParser::ParseDocument(Json, Document));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	const FUMGPrototypeNavigationData& NavigationData = Data.PrototypeNavigation();
	TestTrue(TEXT("Prototype advanced trigger data available"), NavigationData.IsValid());
	if (!NavigationData.IsValid())
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/FigmaPrototypeAdvancedTriggers");
	DeleteGeneratedAssetsAtPath(OutputPath);

	UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, OutputPath);
	TestTrue(TEXT("Build prototype advanced trigger widget blueprint"), Blueprint != nullptr);
	if (!Blueprint)
	{
		return false;
	}

	FUMGGenerator::SaveWidgetBlueprint(Blueprint);
	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass);
	TestTrue(TEXT("Generated advanced trigger class exists"), GeneratedClass != nullptr);
	if (!GeneratedClass)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestTrue(TEXT("Editor world exists"), World != nullptr);
	UUserWidget* WidgetInstance = World ? CreateWidget<UUserWidget>(World, GeneratedClass) : nullptr;
	TestTrue(TEXT("Prototype advanced trigger widget instance created"), WidgetInstance != nullptr);
	if (!WidgetInstance || !WidgetInstance->WidgetTree)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	WidgetInstance->TakeWidget();
	WidgetInstance->AddToViewport();

	UDesign2UMGPrototypeNavigationUserWidgetExtension* UserExtension =
		WidgetInstance->GetExtension<UDesign2UMGPrototypeNavigationUserWidgetExtension>();
	UWidgetSwitcher* Switcher = FindPrototypeSwitcherWidget(WidgetInstance, Data);
	UButton* DragButton = Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("drag_button")));
	UButton* ConditionalFalseButton = Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("conditional_false_button")));
	TestNotNull(TEXT("Prototype user extension exists"), UserExtension);
	TestTrue(TEXT("Prototype switcher exists"), Switcher != nullptr);
	TestTrue(TEXT("Prototype drag trigger exists"), DragButton != nullptr);
	TestTrue(TEXT("Prototype conditional trigger exists"), ConditionalFalseButton != nullptr);
	if (!UserExtension || !Switcher || !DragButton || !ConditionalFalseButton)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	auto FindBindingByTrigger = [&](const TCHAR* TriggerId) -> const FUMGPrototypeNavigationBinding*
	{
		return NavigationData.ClickBindings.FindByPredicate(
			[&](const FUMGPrototypeNavigationBinding& Binding)
			{
				return Binding.TriggerWidgetId == TriggerId;
			});
	};

	const FUMGPrototypeNavigationBinding* DragBinding = FindBindingByTrigger(TEXT("drag_button"));
	TestNotNull(TEXT("Drag binding available for runtime test"), DragBinding);
	if (!DragBinding)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	TestEqual(TEXT("Advanced trigger initial page index"), Switcher->GetActiveWidgetIndex(), 0);
	TestTrue(TEXT("Drag trigger binds native drag delegate"), DragButton->OnButtonDragDetected.IsBound());
	UserExtension->HandleNavigationRequest(*DragBinding);
	TestEqual(TEXT("Drag trigger navigates to second page"), Switcher->GetActiveWidgetIndex(), 1);

	TestTrue(TEXT("Keyboard key-up trigger handles Escape"), UserExtension->HandleKeyboardInput(EKeys::Escape, false));
	TestEqual(TEXT("Keyboard key-up trigger walks back to page one"), Switcher->GetActiveWidgetIndex(), 0);

	TestTrue(TEXT("Keyboard key-down trigger handles Enter"), UserExtension->HandleKeyboardInput(EKeys::Enter, true));
	TestEqual(TEXT("Keyboard key-down trigger navigates to page two"), Switcher->GetActiveWidgetIndex(), 1);
	TestTrue(TEXT("Keyboard key-up trigger handles Escape again"), UserExtension->HandleKeyboardInput(EKeys::Escape, false));
	TestEqual(TEXT("Keyboard key-up trigger returns to page one again"), Switcher->GetActiveWidgetIndex(), 0);

	ConditionalFalseButton->OnClicked.Broadcast();
	TestEqual(TEXT("Conditional false branch navigates to third page"), Switcher->GetActiveWidgetIndex(), 2);

	WidgetInstance->RemoveFromParent();
	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeSinglePageOpenLinkRuntimeWorkflowTest,
	"Design2UMG.WidgetBlueprint.FigmaPrototypeSinglePageOpenLinkRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeSinglePageOpenLinkRuntimeWorkflowTest::RunTest(const FString& Parameters)
{
	FScopedPrototypeNavigationGenerationMode ScopedMode(
		EDesign2UMGPrototypeNavigationGenerationMode::RuntimeExtension);
	FScopedPrototypeOpenUrlCapture OpenUrlCapture;

	const FString Json = MakeFigmaDocJson(
		MakeContainerNode(TEXT("1:1"), TEXT("PageOne"), TEXT("FRAME"), 0, 0, 360, 240,
			MakeLeafNode(TEXT("link_button"), TEXT("Link"), TEXT("RECTANGLE"), 40, 120, 120, 44, FString::Printf(
				TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"URL\",\"url\":\"https://example.com/single\"}]}]"),
				*MakeSolidFillJson(0.2, 0.7, 0.5)))));

	FDesignDocument Document;
	TestTrue(TEXT("Parse single-page open-link fixture"), FFigmaParser::ParseDocument(Json, Document));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	const FUMGPrototypeNavigationData& NavigationData = Data.PrototypeNavigation();
	TestTrue(TEXT("Single-page open-link navigation data available"), NavigationData.IsValid());
	TestFalse(TEXT("Single-page open-link does not require switcher"), NavigationData.RequiresSwitcher());
	if (!NavigationData.IsValid())
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/FigmaPrototypeSinglePageOpenLink");
	DeleteGeneratedAssetsAtPath(OutputPath);

	UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, OutputPath);
	TestTrue(TEXT("Build single-page open-link widget blueprint"), Blueprint != nullptr);
	if (!Blueprint)
	{
		return false;
	}

	FUMGGenerator::SaveWidgetBlueprint(Blueprint);
	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass);
	TestTrue(TEXT("Generated class exists"), GeneratedClass != nullptr);
	if (!GeneratedClass)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestTrue(TEXT("Editor world exists"), World != nullptr);
	UUserWidget* WidgetInstance = World ? CreateWidget<UUserWidget>(World, GeneratedClass) : nullptr;
	TestTrue(TEXT("Single-page open-link widget instance created"), WidgetInstance != nullptr);
	if (!WidgetInstance || !WidgetInstance->WidgetTree)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	WidgetInstance->TakeWidget();
	WidgetInstance->AddToViewport();

	UWidgetSwitcher* Switcher = FindPrototypeSwitcherWidget(WidgetInstance, Data);
	UButton* LinkButton = Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("link_button")));
	TestTrue(TEXT("Single-page open-link trigger exists"), LinkButton != nullptr);
	TestTrue(TEXT("Single-page open-link does not synthesize switcher"), Switcher == nullptr);
	if (!LinkButton)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	LinkButton->OnClicked.Broadcast();
	TestEqual(TEXT("Single-page open-link captures URL through runtime hook"),
		GDesign2UMGPrototypeNavigationLastOpenedUrl,
		TEXT("https://example.com/single"));

	WidgetInstance->RemoveFromParent();
	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeSlideTransitionRuntimeWorkflowTest,
	"Design2UMG.WidgetBlueprint.FigmaPrototypeSlideTransitionRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeSlideTransitionRuntimeWorkflowTest::RunTest(const FString& Parameters)
{
	FScopedPrototypeNavigationGenerationMode ScopedMode(
		EDesign2UMGPrototypeNavigationGenerationMode::RuntimeExtension);

	const FString CtaButtonJson = MakeLeafNode(TEXT("cta_button"), TEXT("CTA"), TEXT("RECTANGLE"), 40, 60, 120, 44, FString::Printf(
		TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"2:1\",\"navigation\":\"NAVIGATE\",\"transition\":{\"type\":\"PUSH\",\"direction\":\"LEFT\",\"duration\":160,\"easing\":{\"type\":\"EASE_OUT\"}}}]}]"),
		*MakeSolidFillJson(0.1, 0.4, 0.9)));
	const FString PageTwoJson = MakeContainerNode(TEXT("2:1"), TEXT("PageTwo"), TEXT("FRAME"), 0, 0, 360, 240,
		MakeTextNode(TEXT("page2_title"), TEXT("Done"), 32, 48, 120, 32, TEXT("Done"),
			TEXT("\"fontFamily\":\"Inter\",\"fontSize\":24,\"fontWeight\":600,\"textAlignHorizontal\":\"LEFT\",\"textAlignVertical\":\"TOP\",\"textDecoration\":\"NONE\",\"letterSpacing\":0,\"lineHeightUnit\":\"AUTO\",\"lineHeightPx\":0")));
	const FString Json = MakeFigmaDocJson(
		FString::Printf(TEXT("%s,%s"),
			*MakeContainerNode(TEXT("1:1"), TEXT("PageOne"), TEXT("FRAME"), 0, 0, 360, 240, CtaButtonJson),
			*PageTwoJson),
		TEXT(",\"prototypeStartNodeID\":\"1:1\""));

	FDesignDocument Document;
	TestTrue(TEXT("Parse prototype slide fixture"), FFigmaParser::ParseDocument(Json, Document));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	const FUMGPrototypeNavigationData& NavigationData = Data.PrototypeNavigation();
	TestTrue(TEXT("Prototype slide data available"), NavigationData.IsValid());
	if (!NavigationData.IsValid())
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/FigmaPrototypeSlideTransition");
	DeleteGeneratedAssetsAtPath(OutputPath);

	UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, OutputPath);
	TestTrue(TEXT("Build prototype slide widget blueprint"), Blueprint != nullptr);
	if (!Blueprint)
	{
		return false;
	}

	FUMGGenerator::SaveWidgetBlueprint(Blueprint);
	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass);
	TestTrue(TEXT("Generated slide class exists"), GeneratedClass != nullptr);
	if (!GeneratedClass)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UWidgetAnimation* SlideOutgoingAnimation =
		FindGeneratedAnimation(GeneratedClass, TEXT("__d2u_nav_anim_000_out"));
	UWidgetAnimation* SlideIncomingAnimation =
		FindGeneratedAnimation(GeneratedClass, TEXT("__d2u_nav_anim_000_in"));
	TestTrue(TEXT("Slide outgoing animation generated"), SlideOutgoingAnimation != nullptr);
	TestTrue(TEXT("Slide incoming animation generated"), SlideIncomingAnimation != nullptr);
	TestTrue(TEXT("Generated slide class exports transition animations"),
		GeneratedClass->Animations.Num() >= 2);

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestTrue(TEXT("Editor world exists"), World != nullptr);
	UUserWidget* WidgetInstance = World ? CreateWidget<UUserWidget>(World, GeneratedClass) : nullptr;
	TestTrue(TEXT("Prototype slide widget instance created"), WidgetInstance != nullptr);
	if (!WidgetInstance || !WidgetInstance->WidgetTree)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	WidgetInstance->TakeWidget();
	WidgetInstance->AddToViewport();

	UWidgetSwitcher* Switcher = FindPrototypeSwitcherWidget(WidgetInstance, Data);
	UButton* TriggerButton = Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("cta_button")));
	TestTrue(TEXT("Prototype slide switcher exists"), Switcher != nullptr);
	TestTrue(TEXT("Prototype slide trigger button exists"), TriggerButton != nullptr);
	if (!Switcher || !TriggerButton)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	TriggerButton->OnClicked.Broadcast();
	TickCoreTickerFor(0.04f);
	TestEqual(TEXT("Prototype slide keeps old page before halfway"), Switcher->GetActiveWidgetIndex(), 0);
	TestTrue(TEXT("Prototype slide updates translation during transition"),
		!FMath::IsNearlyZero(Switcher->GetRenderTransform().Translation.X, 0.5f));
	TickCoreTickerFor(0.06f);
	TestEqual(TEXT("Prototype slide switches to target page"), Switcher->GetActiveWidgetIndex(), 1);
	TickCoreTickerFor(0.10f);
	TestEqual(TEXT("Prototype slide finishes on target page"), Switcher->GetActiveWidgetIndex(), 1);
	TestTrue(TEXT("Prototype slide restores translation"), Switcher->GetRenderTransform().Translation.IsNearlyZero());
	TestTrue(TEXT("Prototype slide restores opacity"), FMath::IsNearlyEqual(Switcher->GetRenderOpacity(), 1.0f, 0.001f));

	WidgetInstance->RemoveFromParent();
	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

#endif
