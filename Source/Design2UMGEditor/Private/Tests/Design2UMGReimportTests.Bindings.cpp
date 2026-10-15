// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

// Safe reimport: editor event-binding lifecycle across reimports and binding function-name collision.

#include "Misc/AutomationTest.h"
#include "Misc/EngineVersionComparison.h"

#include "Design2UMGGenerationManifest.h"
#include "Design2UMGWidgetBindingTools.h"
#include "DesignImportPipeline.h"
#include "DesignImportReimportInternal.h"
#include "Animation/WidgetAnimation.h"
#include "Animation/WidgetAnimationBinding.h"
#include "MovieScene.h"
#include "Components/NamedSlot.h"
#include "UMGGenerator.h"
#include "Tests/ComponentPropertyFixtures.h"

#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/Widget.h"
#include "EdGraph/EdGraph.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"
#include "Design2UMGReimportTestFixtures.h"

#if WITH_AUTOMATION_WORKER

using namespace Design2UMGReimportFixtures;

// Repeated reimport updates design properties while preserving project children, event
// bindings and widget identity. Project bindings take precedence on shared triggers.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportContinuousLifecycleBindingsTest,
	"Design2UMG.SafeReimport.ContinuousLifecycleWithBindings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportContinuousLifecycleBindingsTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/FixtureReimportLifecycleBindings");

	const auto MakeLifeDoc = [](const TCHAR* ButtonLabel, const TCHAR* HeadText)
	{
		return MakeFixtureDoc(FString::Printf(
			TEXT("%s,%s,%s"),
			*MakeFixtureButton(TEXT("fixture_btn_1"), TEXT("HeroButton"), 24, 24, ButtonLabel),
			*MakeFixturePanel(TEXT("fixture_panel"), TEXT("InfoPanel"), 180, 24,
				MakeFixtureText(TEXT("fixture_head"), TEXT("Head"), 188, 32, HeadText)),
			*MakeFixtureInteractive(TEXT("fixture_link"), TEXT("JumpLink"), 24, TEXT("ON_CLICK"), TEXT("https://fixture.example/jump"))));
	};

	UWidgetBlueprint* WBP = FixtureImport(MakeLifeDoc(TEXT("life-v1"), TEXT("head-v1")), FixtureMappedClassPath, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	UWidget* LinkButton = WBP->WidgetTree->FindWidget(TEXT("fixture_link"));
	UUserWidget* MappedBefore = FixtureFindMappedInstance(WBP);
	UTextBlock* HeadBefore = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Head")));
	if (!TestNotNull(TEXT("trigger button widget present"), LinkButton)
		|| !TestNotNull(TEXT("mapped instance present"), MappedBefore)
		|| !TestNotNull(TEXT("Head text present"), HeadBefore))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	const TWeakObjectPtr<UWidget> LinkWeak = LinkButton;
	const TWeakObjectPtr<UUserWidget> MappedWeak = MappedBefore;
	const TWeakObjectPtr<UTextBlock> HeadWeak = HeadBefore;

	// Add a project event binding through WidgetBindings, including its function graph.
	FString BindingError;
	if (!TestTrue(TEXT("WidgetBindings adds the event binding"),
			Design2UMGWidgetBindingTools::AddEventBinding(
				WBP, TEXT("fixture_link"), TEXT("OnClicked"), TEXT("DevHandler"), BindingError)))
	{
		AddError(FString::Printf(TEXT("AddEventBinding failed: %s"), *BindingError));
		FixtureWipeOutput(OutputDir);
		return false;
	}
	// Idempotent: adding the same binding twice does not create a second one.
	TestTrue(TEXT("AddEventBinding is idempotent"),
		Design2UMGWidgetBindingTools::AddEventBinding(
			WBP, TEXT("fixture_link"), TEXT("OnClicked"), TEXT("DevHandler"), BindingError));
	int32 BindingCount = 0;
	for (const FDelegateEditorBinding& Binding : WBP->Bindings)
	{
		BindingCount += (Binding.ObjectName == TEXT("fixture_link") && Binding.PropertyName == TEXT("OnClicked")) ? 1 : 0;
	}
	TestEqual(TEXT("exactly one editor binding for the trigger"), BindingCount, 1);

	UTextBlock* ProjectNote = WBP->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ProjectNote"));
	if (UPanelWidget* InfoPanel = Cast<UPanelWidget>(WBP->WidgetTree->FindWidget(TEXT("InfoPanel"))))
	{
		InfoPanel->AddChild(ProjectNote);
	}

	// v2 reimport: design values updated + project logic kept + the project event binding keeps
	// exclusive ownership of the trigger (no D2U runtime binding competes for it anymore).
	const FDesignReimportResult ResultV2 = FixtureReimport(MakeLifeDoc(TEXT("life-v2"), TEXT("head-v2")), FixtureMappedClassPath, TargetPackage);
	TestTrue(TEXT("v2 reimport succeeds"), ResultV2.bSucceeded);

	const bool bV2HasDevBinding = WBP->Bindings.ContainsByPredicate([](const FDelegateEditorBinding& Binding)
	{
		return Binding.ObjectName == TEXT("fixture_link")
			&& Binding.PropertyName == TEXT("OnClicked")
			&& Binding.FunctionName == TEXT("DevHandler");
	});
	TestTrue(TEXT("project event binding preserved across v2 reimport"), bV2HasDevBinding);
	TestNotNull(TEXT("DevHandler function still resolvable"),
		WBP->SkeletonGeneratedClass ? WBP->SkeletonGeneratedClass->FindFunctionByName(TEXT("DevHandler")) : nullptr);
	TestNotNull(TEXT("project-added child preserved"), WBP->WidgetTree->FindWidget(TEXT("ProjectNote")));
	if (UUserWidget* MappedV2 = ComponentFixtures::CastInput(MappedWeak.Get()))
	{
		TestTrue(TEXT("mapped identity preserved across v2"), MappedV2 == MappedBefore);
		TestEqual(TEXT("mapped input refreshed in v2"), ComponentFixtures::Read<FTextProperty>(MappedV2, TEXT("TextValue")).ToString(), FString(TEXT("life-v2")));
	}
	if (UTextBlock* HeadV2 = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Head"))))
	{
		TestEqual(TEXT("D2U text refreshed in v2"), HeadV2->GetText().ToString(), FString(TEXT("head-v2")));
	}

	// Reimport with another property change must preserve relations, identity, and bindings.
	const FDesignReimportResult ResultV3 = FixtureReimport(MakeLifeDoc(TEXT("life-v3"), TEXT("head-v3")), FixtureMappedClassPath, TargetPackage);
	TestTrue(TEXT("v3 reimport succeeds"), ResultV3.bSucceeded);

	UWidget* LinkV3 = WBP->WidgetTree->FindWidget(TEXT("fixture_link"));
	if (TestNotNull(TEXT("trigger button still present after v3"), LinkV3))
	{
		TestTrue(TEXT("trigger button identity stable across both reimports"), LinkV3 == LinkWeak.Get());
	}
	if (UUserWidget* MappedV3 = ComponentFixtures::CastInput(MappedWeak.Get()))
	{
		TestTrue(TEXT("mapped identity stable across both reimports"), MappedV3 == MappedBefore);
		TestEqual(TEXT("mapped input refreshed in v3"), ComponentFixtures::Read<FTextProperty>(MappedV3, TEXT("TextValue")).ToString(), FString(TEXT("life-v3")));
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
		return Binding.ObjectName == TEXT("fixture_link")
			&& Binding.PropertyName == TEXT("OnClicked")
			&& Binding.FunctionName == TEXT("DevHandler");
	});
	TestTrue(TEXT("project event binding preserved across v3 reimport"), bV3HasDevBinding);
	TestNull(TEXT("no duplicate Head created"), WBP->WidgetTree->FindWidget(TEXT("Head_1")));
	TestNull(TEXT("no duplicate trigger created"), WBP->WidgetTree->FindWidget(TEXT("fixture_link_1")));

	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads after v3"), FixtureLoadSidecar(TargetPackage, Sidecar)))
	{
		TestNotNull(TEXT("mapped instance still recorded"), Sidecar.FindInstanceByDesignNodeId(TEXT("fixture_btn_1")));
		// The host root of an interactive document is the synthesized switcher: the page root fixture_root is a top-level instance;
		// InfoPanel and friends are nested nodes inside the page and do not enter the top-level instance records.
		TestNotNull(TEXT("page instance still recorded"), Sidecar.FindInstanceByDesignNodeId(TEXT("fixture_root")));
		TestFalse(TEXT("project child stays outside d2uOwned"), Sidecar.D2UOwned.Contains(TEXT("ProjectNote")));
	}

	FixtureWipeOutput(OutputDir);
	return true;
}

// A function-name collision creates a uniquely named graph. The binding must resolve
// that same function after compilation and saving.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportBindingFunctionNameCollisionTest,
	"Design2UMG.SafeReimport.BindingSurvivesFunctionNameCollision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportBindingFunctionNameCollisionTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/FixtureReimportBindingNameCollision");

	const FString V1 = MakeFixtureDoc(MakeFixtureInteractive(TEXT("fixture_link"), TEXT("JumpLink"), 24, TEXT("ON_CLICK"), TEXT("https://fixture.example/jump")));
	UWidgetBlueprint* WBP = FixtureImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}

	// Create the collision: a member variable shares the requested function name (that name is not a function, but it occupies the Kismet namespace).
	FEdGraphPinType BoolPin;
	BoolPin.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	TestTrue(TEXT("colliding member variable added"),
		FBlueprintEditorUtils::AddMemberVariable(WBP, TEXT("ClashHandler"), BoolPin));

	FString BindingError;
	if (!TestTrue(TEXT("binding added despite the name clash"),
			Design2UMGWidgetBindingTools::AddEventBinding(
				WBP, TEXT("fixture_link"), TEXT("OnClicked"), TEXT("ClashHandler"), BindingError)))
	{
		AddError(FString::Printf(TEXT("AddEventBinding failed: %s"), *BindingError));
		FixtureWipeOutput(OutputDir);
		return false;
	}

	const FDelegateEditorBinding* Binding = nullptr;
	for (const FDelegateEditorBinding& Candidate : WBP->Bindings)
	{
		if (Candidate.ObjectName == TEXT("fixture_link") && Candidate.PropertyName == TEXT("OnClicked"))
		{
			Binding = &Candidate;
		}
	}
	if (!TestNotNull(TEXT("editor binding recorded"), Binding))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	// The binding must point at the actually created uniquely-named function (under collision the requested name ClashHandler is taken by the member variable).
	TestTrue(TEXT("binding points at the unique function name, not the clashing requested name"),
		Binding->FunctionName != TEXT("ClashHandler"));
	bool bGraphExists = false;
	for (const UEdGraph* Graph : WBP->FunctionGraphs)
	{
		bGraphExists = bGraphExists || (Graph && Graph->GetFName() == Binding->FunctionName);
	}
	TestTrue(TEXT("function graph exists under the bound name"), bGraphExists);

	// After compiling and saving (the same path as the generation chain): the function resolves on the generated class and the binding was not stripped.
	if (!TestTrue(TEXT("compile + save succeeds with the collision binding"),
			FUMGGenerator::SaveWidgetBlueprint(WBP, nullptr).IsSuccess()))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	const bool bBindingSurvivesCompile = WBP->Bindings.ContainsByPredicate(
		[Binding](const FDelegateEditorBinding& Candidate)
		{
			return Candidate.ObjectName == TEXT("fixture_link")
				&& Candidate.PropertyName == TEXT("OnClicked")
				&& Candidate.FunctionName == Binding->FunctionName;
		});
	TestTrue(TEXT("binding survives compile with the unique function name"), bBindingSurvivesCompile);
	TestNotNull(TEXT("bound function resolvable on the generated class"),
		WBP->GeneratedClass ? WBP->GeneratedClass->FindFunctionByName(Binding->FunctionName) : nullptr);

	FixtureWipeOutput(OutputDir);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportNamedSlotOwnershipTest,
	"Design2UMG.SafeReimport.NamedSlotOwnership", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportNamedSlotOwnershipTest::RunTest(const FString&)
{
	// Blueprint compilation collects garbage; prepare the class before unrooted test objects.
	UClass* const HostClass = ComponentFixtures::NamedSlotHostClass();
	UWidgetBlueprint* Blueprint = NewObject<UWidgetBlueprint>(GetTransientPackage());
	Blueprint->WidgetTree = NewObject<UWidgetTree>(Blueprint);
	UUserWidget* Host = Blueprint->WidgetTree->ConstructWidget<UUserWidget>(HostClass, TEXT("SlotHost"));
	Blueprint->WidgetTree->RootWidget = Host;
	Host->WidgetTree = NewObject<UWidgetTree>(Host);
	UNamedSlot* Slot = Host->WidgetTree->ConstructWidget<UNamedSlot>(UNamedSlot::StaticClass(), TEXT("Content"));
	Host->WidgetTree->RootWidget = Slot;
	UTextBlock* Manual = Blueprint->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Manual"));
	Host->SetContentForSlot(TEXT("Content"), Manual);
	TestTrue(TEXT("Fixture contains real named-slot binding"), Host->GetContentForSlot(TEXT("Content")) == Manual);
	TSet<FString> Owned{TEXT("SlotHost")};
	FString Reason;
	TestFalse(TEXT("Handmade slot content prevents deletion"), DesignImportPipeline::IsSubtreePurelyD2UOwned(Blueprint, Host, Owned, Reason));
	TestTrue(TEXT("Conflict identifies hidden content"), Reason.Contains(TEXT("Manual")));
	Owned.Add(TEXT("Manual"));
	TestTrue(TEXT("Wholly generated slot content permits deletion"), DesignImportPipeline::IsSubtreePurelyD2UOwned(Blueprint, Host, Owned, Reason));
	FDelegateEditorBinding Binding;
	Binding.ObjectName = TEXT("Manual");
	Binding.PropertyName = TEXT("Text");
	Blueprint->Bindings.Add(Binding);
	TestFalse(TEXT("Bound content in slot prevents deletion"), DesignImportPipeline::IsSubtreePurelyD2UOwned(Blueprint, Host, Owned, Reason));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportAnimationOwnershipTest,
	"Design2UMG.SafeReimport.AnimationOwnership", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportAnimationOwnershipTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/FixtureAnimationOwnership");
	const FString Keep = MakeFixtureText(TEXT("keep"), TEXT("Keeper"), 250, 24, TEXT("Keep"));
	const FString Animated = MakeFixtureText(TEXT("animated"), TEXT("Animated"), 24, 24, TEXT("Animate"));
	UWidgetBlueprint* Blueprint = FixtureImport(MakeFixtureDoc(Animated + TEXT(",") + Keep), nullptr, OutputDir);
	if (!TestNotNull(TEXT("Animation fixture imported"), Blueprint)) return false;
	UWidget* Target = Blueprint->WidgetTree->FindWidget(TEXT("Animated"));
	if (!TestNotNull(TEXT("Animation target exists"), Target)) return false;
	UWidgetAnimation* Animation = NewObject<UWidgetAnimation>(Blueprint, TEXT("UserAnimation"), RF_Transactional);
	Animation->MovieScene = NewObject<UMovieScene>(Animation);
	const FGuid Guid = Animation->MovieScene->AddPossessable(TEXT("Animated"), Target->GetClass());
	FWidgetAnimationBinding Binding;
	Binding.WidgetName = Target->GetFName();
	Binding.AnimationGuid = Guid;
	Binding.bIsRootWidget = false;
	Animation->AnimationBindings.Add(Binding);
	Blueprint->Animations.Add(Animation);
#if !UE_VERSION_OLDER_THAN(5, 6, 0)
	Blueprint->WidgetVariableNameToGuidMap.Add(Animation->GetFName(), FGuid::NewGuid());
#endif
	const FString Package = Blueprint->GetOutermost()->GetName();
	const FDesignReimportResult Result = FixtureReimport(MakeFixtureDoc(Keep), nullptr, Package);
	TestTrue(TEXT("Per-node animation conflict permits remaining import"), Result.bSucceeded);
	TestTrue(TEXT("Animated target preserved"), Blueprint->WidgetTree->FindWidget(TEXT("Animated")) == Target);
	TestTrue(TEXT("Animation retained"), Blueprint->Animations.Contains(Animation));
	TestEqual(TEXT("Animation binding intact"), Animation->GetBindings().Num(), 1);
	TestTrue(TEXT("Animation prevents node removal"), FixtureCountKind(Result, TEXT("reimport_remove_conflict")) > 0);
	FixtureWipeOutput(OutputDir);
	return true;
}

#endif
