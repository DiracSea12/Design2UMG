// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGWidgetPropertyAccess.h"
#include "Design2UMGImageTestUtils.h"
#include "Tests/Design2UMGReimportTestFixtures.h"
#include "Design2UMGSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/Package.h"
#include "Components/RetainerBox.h"
#include "Materials/MaterialInterface.h"
#include "UMGWidgetRenderer.h"
#include "ImageUtils.h"
#include "ImageCore.h"

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReimportEffectPersistenceTest,
	"Design2UMG.SafeReimport.Resources.SameSourceEffects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FReimportEffectPersistenceTest::RunTest(const FString&)
{
	const FString Source = MakeFixtureDoc(TEXT(R"JSON({"id":"group","name":"ShadowGroup","type":"FRAME",
	 "absoluteBoundingBox":{"x":20,"y":20,"width":80,"height":80},"cornerRadius":8,
	 "effects":[{"type":"DROP_SHADOW","visible":true,"radius":4,"offset":{"x":10,"y":4},"color":{"r":0,"g":0,"b":0,"a":0.5}}],
	 "children":[{"id":"a","name":"TranslucentFill","type":"RECTANGLE",
	 "absoluteBoundingBox":{"x":20,"y":20,"width":60,"height":60},
	 "fills":[{"type":"SOLID","color":{"r":1,"g":0.5,"b":0.25,"a":0.5}}]},
	 {"id":"b","name":"OpaqueFill","type":"RECTANGLE",
	 "absoluteBoundingBox":{"x":80,"y":80,"width":20,"height":20},
	 "fills":[{"type":"SOLID","color":{"r":0.25,"g":0.5,"b":1,"a":1}}]}]})JSON"), FixtureReimportScope, 160, 120);
	const FString Folder = TEXT("/Game/Design2UMG/_TestOutput/ReimportEffects_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	ON_SCOPE_EXIT { FixtureWipeOutput(Folder); };
	UWidgetBlueprint* Blueprint = FixtureImport(Source, nullptr, Folder);
	if (!TestNotNull(TEXT("Initial effect asset saved"), Blueprint)) return false;
	const auto CaptureMaterials = [](UWidgetBlueprint* Asset)
	{
		TMap<FName, FString> Result;
		Asset->WidgetTree->ForEachWidget([&](UWidget* Widget)
		{
			if (const URetainerBox* Retainer = Cast<URetainerBox>(Widget))
			{
				const UMaterialInterface* Material = Design2UMGWidgetPropertyAccess::GetRetainerEffectMaterialInterface(*Retainer);
				Result.Add(Widget->GetFName(), Material ? Material->GetPathName() : FString());
			}
		});
		return Result;
	};
	const TMap<FName, FString> Materials = CaptureMaterials(Blueprint);
	bool bHasSubtreeShadow = false;
	for (const TPair<FName, FString>& Pair : Materials) bHasSubtreeShadow |= Pair.Value.Contains(TEXT("D2U_SubtreeShadow_"));
	if (!TestTrue(TEXT("Fixture includes a prepared subtree shadow"), bHasSubtreeShadow)) return false;
	TArray<uint8> BeforePng;
	FIntPoint BeforeSize;
	FString Error;
	if (!TestTrue(TEXT("Fresh effect asset renders"), FUMGWidgetRenderer::RenderWidgetToPng(Blueprint, BeforePng, BeforeSize, &Error))) return false;
	for (int32 Repeat = 0; Repeat < 2; ++Repeat)
	{
		const FDesignReimportResult Result = FixtureReimport(Source, nullptr, Blueprint->GetOutermost()->GetName());
		if (!TestTrue(TEXT("Same-source effect reimport succeeds"), Result.bSucceeded)) return false;
		TestTrue(TEXT("Same source retains every persistent effect material"), CaptureMaterials(Blueprint).OrderIndependentCompareEqual(Materials));
		TArray<uint8> AfterPng;
		FIntPoint AfterSize;
		if (!TestTrue(TEXT("Reimported effect asset renders"), FUMGWidgetRenderer::RenderWidgetToPng(Blueprint, AfterPng, AfterSize, &Error))) return false;
		TestEqual(TEXT("Same source retains render dimensions"), AfterSize, BeforeSize);
		FImage Before, After;
		if (!TestTrue(TEXT("Effect renders decode"), Design2UMGImageTestUtils::DecodeToBGRA8(BeforePng.GetData(), BeforePng.Num(), Before)
			&& Design2UMGImageTestUtils::DecodeToBGRA8(AfterPng.GetData(), AfterPng.Num(), After))) return false;
		TestTrue(TEXT("Same source retains every effect pixel"), Before.RawData == After.RawData);
	}
	return true;
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
