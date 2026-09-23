// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Tests/Design2UMGReimportTestFixtures.h"
#include "Design2UMGSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/Package.h"

#if WITH_AUTOMATION_WORKER
using namespace Design2UMGReimportFixtures;

namespace
{
	FString GradientDocument(int32 Size, float Red)
	{
		TArray<FString> Children;
		for (int32 Index = 0; Index < 2; ++Index)
		{
			Children.Add(FString::Printf(TEXT(
				"{\"id\":\"gradient_%d\",\"name\":\"Gradient%d\",\"type\":\"RECTANGLE\","
				"\"absoluteBoundingBox\":{\"x\":%d,\"y\":0,\"width\":%d,\"height\":%d},"
				"\"fills\":[{\"type\":\"GRADIENT_LINEAR\",\"visible\":true,"
				"\"gradientHandlePositions\":[{\"x\":0,\"y\":0},{\"x\":1,\"y\":0},{\"x\":0,\"y\":1}],"
				"\"gradientStops\":[{\"position\":0,\"color\":{\"r\":%f,\"g\":0,\"b\":0,\"a\":1}},"
				"{\"position\":1,\"color\":{\"r\":0,\"g\":%f,\"b\":1,\"a\":1}}]}]}"),
				Index, Index, Index * Size, Size, Size, Red, Index * 0.5f));
		}
		return MakeFixtureDoc(FString::Join(Children, TEXT(",")), FixtureReimportScope, Size * 2, Size);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReimportGeneratedResourcePreparationTest,
	"Design2UMG.SafeReimport.Resources.PrepareBeforeMutation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FReimportGeneratedResourcePreparationTest::RunTest(const FString&)
{
	const FString Folder = TEXT("/Game/Design2UMG/_TestOutput/ReimportResources_")
		+ FGuid::NewGuid().ToString(EGuidFormats::Digits);
	ON_SCOPE_EXIT { FixtureWipeOutput(Folder); };
	UWidgetBlueprint* Blueprint = FixtureImport(GradientDocument(64, 1.0f), nullptr, Folder);
	if (!TestNotNull(TEXT("Initial gradient asset saved"), Blueprint)) return false;
	const FString Package = Blueprint->GetOutermost()->GetName();
	const FString BeforeHash = Design2UMGGenerationManifest::ComputeSavedAssetHash(Package);
	const FString Sidecar = Design2UMGGenerationManifest::BuildSidecarPathForAsset(Package);
	FString BeforeManifest;
	if (!TestTrue(TEXT("Initial manifest readable"), FixtureReadFileString(Sidecar, BeforeManifest))) return false;
	UWidget* OriginalRoot = Blueprint->WidgetTree->RootWidget;
	TGuardValue<int32> RestoreBudget(UDesign2UMGSettings::Get()->GeneratedTextureBudgetMiB, 1);

	// Individually valid 400x400 textures exceed the cumulative one-MiB budget;
	// a single oversized request must also fail before touching the saved tree.
	for (int32 Size : {400, 20000})
	{
		const FDesignReimportResult Result = FixtureReimport(GradientDocument(Size, 0.5f), nullptr, Package);
		TestFalse(TEXT("Required resource failure is not a successful reimport"), Result.bSucceeded);
		TestTrue(TEXT("No asset commit"), Result.Commit.Status == EDesign2UMGCommitStatus::NotCommitted);
		TestTrue(TEXT("Resource preparation leaves memory unchanged"), Result.Commit.Memory == EDesign2UMGMemoryState::Unchanged);
		TestTrue(TEXT("Original root retained"), Blueprint->WidgetTree->RootWidget == OriginalRoot);
		TestEqual(TEXT("Asset bytes unchanged"), Design2UMGGenerationManifest::ComputeSavedAssetHash(Package), BeforeHash);
		FString AfterManifest;
		TestTrue(TEXT("Manifest remains readable"), FixtureReadFileString(Sidecar, AfterManifest));
		TestEqual(TEXT("Manifest bytes unchanged"), AfterManifest, BeforeManifest);
		TestTrue(TEXT("Failure explains resource preparation"), FixtureCountKind(Result, TEXT("reimport_prepare_failed")) > 0);
	}

	const FDesignReimportResult Retry = FixtureReimport(GradientDocument(300, 0.25f), nullptr, Package);
	TestTrue(TEXT("A corrected request succeeds with a fresh operation budget"), Retry.bSucceeded);
	TestTrue(TEXT("Successful retry saves the asset"), Retry.Commit.Asset == EDesign2UMGFileState::Saved);
	TestTrue(TEXT("Successful retry updates asset bytes"), Design2UMGGenerationManifest::ComputeSavedAssetHash(Package) != BeforeHash);
	return true;
}
#endif
