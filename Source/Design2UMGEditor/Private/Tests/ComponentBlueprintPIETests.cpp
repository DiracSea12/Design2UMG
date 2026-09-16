// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved.
// SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Tests/AutomationCommon.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"
#include "UnrealClient.h"
#include "WidgetBlueprint.h"

namespace
{
	class FCheckComponentBlueprintInPIE final : public IAutomationLatentCommand
	{
	public:
		FCheckComponentBlueprintInPIE(FAutomationTestBase& InTest, UWidgetBlueprint* InBlueprint,
			int32 InExpectedChildren, bool bInCheckFixtureText)
			: Test(InTest), Blueprint(InBlueprint), ExpectedChildren(InExpectedChildren),
			bCheckFixtureText(bInCheckFixtureText), StartedAt(FPlatformTime::Seconds())
		{
			Screenshot = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() /
				TEXT("Design2UMG/ComponentBlueprintPIE") / (FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".png")));
		}

		virtual bool Update() override
		{
			if (FPlatformTime::Seconds() - StartedAt > 60.0)
			{
				Test.AddError(TEXT("Timed out waiting for PIE world, widget layout, or viewport screenshot."));
				ReleaseWidget();
				return true;
			}
			if (!Runtime.IsValid())
			{
				UWorld* World = GEditor->PlayWorld;
				if (!World || !World->HasBegunPlay() || !World->GetFirstPlayerController()) return false;
				Test.TestTrue(TEXT("Verification runs in a playing game world"), World->IsGameWorld());
				Runtime.Reset(CreateWidget<UUserWidget>(World->GetFirstPlayerController(), Blueprint->GeneratedClass.Get()));
				if (!Test.TestNotNull(TEXT("Create saved parent in PIE"), Runtime.Get())) return true;
				Runtime->AddToViewport();
				AddedAt = FPlatformTime::Seconds();
				return false;
			}
			if (!bScreenshotRequested)
			{
				// Let the actual game viewport arrange and draw the widget before inspecting it.
				if (FPlatformTime::Seconds() - AddedAt < 2.0) return false;
				Test.TestTrue(TEXT("Parent belongs to game viewport"), Runtime->IsInViewport());
				Test.TestTrue(TEXT("Parent has arranged viewport geometry"), Runtime->GetCachedGeometry().GetLocalSize().GetMin() > 0.0);
				TArray<UUserWidget*> Children;
				Runtime->WidgetTree->ForEachWidget([&Children](UWidget* Widget)
				{
					if (UUserWidget* Child = Cast<UUserWidget>(Widget)) Children.Add(Child);
				});
				Test.TestEqual(TEXT("PIE retains separate child widget instances"), Children.Num(), ExpectedChildren);
				for (UUserWidget* Child : Children)
				{
					Test.TestTrue(TEXT("Child belongs to the same game world"), Child->GetWorld() == Runtime->GetWorld());
					Test.TestTrue(TEXT("Child class directly derives from native UUserWidget"), Child->GetClass()->GetSuperClass() == UUserWidget::StaticClass());
				}
				if (bCheckFixtureText)
				{
					TArray<FString> Texts;
					Runtime->WidgetTree->ForEachWidgetAndDescendants([&Texts](UWidget* Widget)
					{
						if (const UTextBlock* Text = Cast<UTextBlock>(Widget)) Texts.Add(Text->GetText().ToString());
					});
					Test.TestTrue(TEXT("First instance keeps its override in PIE"), Texts.Contains(TEXT("Confirm")));
					Test.TestTrue(TEXT("Second instance keeps its updated override in PIE"), Texts.Contains(TEXT("Updated")));
					Test.TestFalse(TEXT("Component default does not replace PIE overrides"), Texts.Contains(TEXT("Default")));
					if (Children.Num() == 2) Test.TestTrue(TEXT("PIE instances share one saved class"), Children[0]->GetClass() == Children[1]->GetClass());
				}
				IFileManager::Get().MakeDirectory(*FPaths::GetPath(Screenshot), true);
				FScreenshotRequest::RequestScreenshot(Screenshot, true, false);
				bScreenshotRequested = true;
				return false;
			}
			if (IFileManager::Get().FileSize(*Screenshot) <= 0) return false;
			Test.AddInfo(FString::Printf(TEXT("Actual PIE viewport screenshot: %s"), *Screenshot));
			ReleaseWidget();
			return true;
		}

	private:
		void ReleaseWidget()
		{
			if (Runtime.IsValid()) Runtime->RemoveFromParent();
			Runtime.Reset();
		}

		FAutomationTestBase& Test;
		TStrongObjectPtr<UWidgetBlueprint> Blueprint;
		TStrongObjectPtr<UUserWidget> Runtime;
		int32 ExpectedChildren;
		bool bCheckFixtureText;
		double StartedAt;
		double AddedAt = 0.0;
		bool bScreenshotRequested = false;
		FString Screenshot;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComponentBlueprintPIETest,
	"Design2UMG.ComponentBlueprintPersistence.GameplayPIE", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FComponentBlueprintPIETest::RunTest(const FString&)
{
	FString AssetPath;
	const bool bExternalAsset = FParse::Value(FCommandLine::Get(), TEXT("D2UComponentPIEAsset="), AssetPath);
	int32 ExpectedChildren = 2;
	if (bExternalAsset)
	{
		if (!FParse::Value(FCommandLine::Get(), TEXT("D2UComponentPIEExpectedChildren="), ExpectedChildren) || ExpectedChildren <= 0)
		{
			AddError(TEXT("D2UComponentPIEAsset requires a positive D2UComponentPIEExpectedChildren assertion."));
			return false;
		}
	}
	else if (!TestTrue(TEXT("SharedInstances supplied a saved asset for a fresh PIE process"), FFileHelper::LoadFileToString(AssetPath,
		*(FPaths::ProjectSavedDir() / TEXT("Design2UMG/ComponentBlueprintReload.txt"))))) return false;
	UWidgetBlueprint* Blueprint = LoadObject<UWidgetBlueprint>(nullptr, *AssetPath);
	if (!TestNotNull(TEXT("Load previously saved blueprint"), Blueprint) ||
		!TestNotNull(TEXT("Saved blueprint has a generated class"), Blueprint->GeneratedClass.Get())) return false;
	if (!TestNull(TEXT("Test starts outside an existing play session"), GEditor->PlayWorld)) return false;
	// Unsaved map inside the existing project; no project map or game logic is modified.
	if (!TestNotNull(TEXT("Create isolated unsaved map"), FAutomationEditorCommonUtils::CreateNewMap())) return false;
	// A separate PIE viewport keeps its screenshot free of editor panels. Include Slate UI,
	// because a scene-only screenshot omits the UMG layer we are testing.
	FRequestPlaySessionParams PlayParams;
	GEditor->RequestPlaySession(PlayParams);
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FCheckComponentBlueprintInPIE>(*this, Blueprint, ExpectedChildren, !bExternalAsset));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.0f));
	return true;
}

#endif
