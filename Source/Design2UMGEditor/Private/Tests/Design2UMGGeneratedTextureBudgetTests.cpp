// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/Build.h"

#if WITH_AUTOMATION_WORKER

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Templates/UnrealTemplate.h"

#include "Design2UMGSettings.h"
#include "DesignImageResourceLimits.h"
#include "DesignPathRasterizer.h"
#include "PreBuildResourceNormalizer.h"
#include "ProceduralShapeGenerator.h"
#include "Tests/Design2UMGTestAssetCleanup.h"
#include "UMGGenerator.h"
#include "UMGSchema.h"
#include "UMGWidgetNodeProceduralResourceAccess.h"
#include "UMGWidgetTypes.h"

namespace
{
	FUMGWidgetNode MakeRoot()
	{
		FUMGWidgetNode Root;
		Root.Type = UMGWidgetTypes::CanvasPanel;
		Root.Id = TEXT("generated_texture_root");
		Root.Name = TEXT("GeneratedTextureBudget_Root");
		Root.Bounds = FDesignPixelBounds{0.0f, 0.0f, 800.0f, 800.0f};
		return Root;
	}

	FUMGWidgetNode MakeGradient(const FString& Id, const float Size, const float Blue)
	{
		FUMGWidgetNode Node;
		Node.Type = UMGWidgetTypes::Image;
		Node.Id = Id;
		Node.Name = Id;
		Node.Bounds = FDesignPixelBounds{0.0f, 0.0f, Size, Size};
		Node.WidgetStyle.WidthOverride = Size;
		Node.WidgetStyle.HeightOverride = Size;
		Node.Brush.Gradient.Type = TEXT("Linear");
		FUMGVisualBrushData::FGradientStopData First;
		First.Position = 0.0f;
		First.Color = FLinearColor::Red;
		FUMGVisualBrushData::FGradientStopData Second;
		Second.Position = 1.0f;
		Second.Color = FLinearColor(0.0f, 0.0f, Blue, 1.0f);
		Node.Brush.Gradient.Stops = {First, Second};
		return Node;
	}

	FUMGProceduralResourceRequest MakeGradientRequest(const int32 Size, const float Blue)
	{
		const FUMGWidgetNode Node = MakeGradient(TEXT("request"), static_cast<float>(Size), Blue);
		FUMGProceduralResourceRequest Request;
		Design2UMGWidgetNodeProceduralResourceAccess::TryBuildProceduralResourceRequest(Node, Request);
		return Request;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeneratedTextureBudgetActualProceduralAllocationBudgetTest,
	"Design2UMG.Resources.GeneratedTextureBudget.ActualProceduralAllocations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FGeneratedTextureBudgetActualProceduralAllocationBudgetTest::RunTest(const FString&)
{
	TGuardValue<int32> RestoreBudget(UDesign2UMGSettings::Get()->GeneratedTextureBudgetMiB, 1);
	DesignImageResources::FImageOperationBudget Budget(DesignImageResources::GetGeneratedTextureLimits());
	const FString& Refusal = Budget.GetRefusalError();
	FString Error;
	UTexture2D* First = FProceduralShapeGenerator::GenerateFromMaterialRef(
		MakeGradientRequest(400, 1.0f), nullptr, &Error, &Budget);
	ON_SCOPE_EXIT { FProceduralShapeGenerator::ReleaseTransientTexture(First); };
	if (!TestNotNull(TEXT("The first 400x400 allocation fits"), First)) return false;
	UTexture2D* Second = FProceduralShapeGenerator::GenerateFromMaterialRef(
		MakeGradientRequest(400, 0.5f), nullptr, &Error, &Budget);
	TestNull(TEXT("A second real 400x400 allocation exceeds one MiB"), Second);
	TestTrue(TEXT("The actual allocation reports the cumulative limit"),
		Error.Contains(TEXT("operation limit")) && Refusal.Contains(TEXT("operation limit")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeneratedTextureBudgetIndependentOperationsTest,
	"Design2UMG.Resources.GeneratedTextureBudget.InterleavedOperationsRemainIndependent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FGeneratedTextureBudgetIndependentOperationsTest::RunTest(const FString&)
{
	DesignImageResources::FImageResourceLimits Limits;
	Limits.MaxTotalDecodedBytes = 1024 * 1024;
	DesignImageResources::FImageOperationBudget FirstBudget(Limits);
	DesignImageResources::FImageOperationBudget SecondBudget(Limits);
	FString Error;
	TArray<UTexture2D*> Textures;
	ON_SCOPE_EXIT
	{
		for (UTexture2D* Texture : Textures) FProceduralShapeGenerator::ReleaseTransientTexture(Texture);
	};
	const auto Allocate = [&](DesignImageResources::FImageOperationBudget& Budget, int32 Size)
	{
		UTexture2D* Texture = FProceduralShapeGenerator::GenerateFromMaterialRef(
			MakeGradientRequest(Size, 1.0f), nullptr, &Error, &Budget);
		Textures.Add(Texture);
		return Texture;
	};
	TestNotNull(TEXT("The first operation allocates independently"), Allocate(FirstBudget, 400));
	TestNotNull(TEXT("An interleaved operation has its own allowance"), Allocate(SecondBudget, 400));
	TestNull(TEXT("The first operation retains its accumulated allocation"), Allocate(FirstBudget, 400));
	const FString Refusal = FirstBudget.GetRefusalError();
	TestTrue(TEXT("The failing operation records its refusal"), Refusal.Contains(TEXT("operation limit")));
	TestNotNull(TEXT("A smaller request still fits the first operation"), Allocate(FirstBudget, 32));
	TestEqual(TEXT("A later successful allocation preserves the first refusal"), FirstBudget.GetRefusalError(), Refusal);
	TestNotNull(TEXT("A refusal does not poison the other operation"), Allocate(SecondBudget, 32));
	TestTrue(TEXT("The other operation has no refusal"), SecondBudget.GetRefusalError().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeneratedTextureBudgetPathCacheHitBudgetTest,
	"Design2UMG.Resources.GeneratedTextureBudget.PathCacheHitIsNotBilledTwice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FGeneratedTextureBudgetPathCacheHitBudgetTest::RunTest(const FString&)
{
	TGuardValue<int32> RestoreBudget(UDesign2UMGSettings::Get()->GeneratedTextureBudgetMiB, 1);
	FDesignPathRasterizer::ClearCache();
	ON_SCOPE_EXIT { FDesignPathRasterizer::ClearCache(); };
	DesignImageResources::FImageOperationBudget Budget(DesignImageResources::GetGeneratedTextureLimits());
	const FString& Refusal = Budget.GetRefusalError();
	FString Error;
	const FString A = TEXT("M0 0 L10 0 L10 10 L0 10 Z");
	const FString B = TEXT("M0 0 L12 0 L12 10 L0 10 Z");
	UTexture2D* First = FDesignPathRasterizer::Rasterize(A, 400, 400, FLinearColor::White,
		TEXT("nonzero"), {}, 0.0f, TEXT("butt"), TEXT("miter"), &Error, &Budget);
	UTexture2D* Hit = FDesignPathRasterizer::Rasterize(A, 400, 400, FLinearColor::White,
		TEXT("nonzero"), {}, 0.0f, TEXT("butt"), TEXT("miter"), &Error, &Budget);
	TestNotNull(TEXT("Initial path raster allocates"), First);
	TestTrue(TEXT("A byte-identical path is a real cache hit"), Hit == First);
	UTexture2D* Miss = FDesignPathRasterizer::Rasterize(B, 400, 400, FLinearColor::White,
		TEXT("nonzero"), {}, 0.0f, TEXT("butt"), TEXT("miter"), &Error, &Budget);
	TestNull(TEXT("A distinct cache miss is a second allocation and is refused"), Miss);
	TestTrue(TEXT("The refused miss names the operation limit"), Error.Contains(TEXT("operation limit")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeneratedTextureBudgetPathCacheEvictionRebillsTest,
	"Design2UMG.Resources.GeneratedTextureBudget.PathCacheEvictionRebillsAllocation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FGeneratedTextureBudgetPathCacheEvictionRebillsTest::RunTest(const FString&)
{
	TGuardValue<int32> RestoreBudget(UDesign2UMGSettings::Get()->GeneratedTextureBudgetMiB, 1);
	FDesignPathRasterizer::ClearCache();
	ON_SCOPE_EXIT { FDesignPathRasterizer::ClearCache(); };
	DesignImageResources::FImageOperationBudget Budget(DesignImageResources::GetGeneratedTextureLimits());
	const FString& Refusal = Budget.GetRefusalError();
	FString Error;
	const FString Geometry = TEXT("M0 0 L10 0 L10 10 L0 10 Z");
	UTexture2D* First = FDesignPathRasterizer::Rasterize(
		Geometry, 400, 400, FLinearColor::White,
		TEXT("nonzero"), {}, 0.0f, TEXT("butt"), TEXT("miter"), &Error, &Budget);
	if (!TestNotNull(TEXT("The first cache miss allocates and is billed"), First)) return false;
	TestTrue(TEXT("The identical request is a cache hit before eviction"),
		FDesignPathRasterizer::Rasterize(
			Geometry, 400, 400, FLinearColor::White,
			TEXT("nonzero"), {}, 0.0f, TEXT("butt"), TEXT("miter"), &Error, &Budget) == First);

	FDesignPathRasterizer::ClearCache();
	UTexture2D* AfterEviction = FDesignPathRasterizer::Rasterize(
		Geometry, 400, 400, FLinearColor::White,
		TEXT("nonzero"), {}, 0.0f, TEXT("butt"), TEXT("miter"), &Error, &Budget);
	TestNull(TEXT("The same request is billed again after its cached allocation is evicted"), AfterEviction);
	TestTrue(TEXT("The re-billed allocation reaches the cumulative limit"),
		Error.Contains(TEXT("operation limit")) && Refusal.Contains(TEXT("operation limit")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeneratedTextureBudgetMixedKindsShareActualBudgetTest,
	"Design2UMG.Resources.GeneratedTextureBudget.MixedKindsShareActualBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FGeneratedTextureBudgetMixedKindsShareActualBudgetTest::RunTest(const FString&)
{
	TGuardValue<int32> RestoreBudget(UDesign2UMGSettings::Get()->GeneratedTextureBudgetMiB, 1);
	FDesignPathRasterizer::ClearCache();
	ON_SCOPE_EXIT { FDesignPathRasterizer::ClearCache(); };
	DesignImageResources::FImageOperationBudget Budget(DesignImageResources::GetGeneratedTextureLimits());
	const FString& Refusal = Budget.GetRefusalError();
	FString Error;
	UTexture2D* Procedural = FProceduralShapeGenerator::GenerateFromMaterialRef(
		MakeGradientRequest(400, 1.0f), nullptr, &Error, &Budget);
	ON_SCOPE_EXIT { FProceduralShapeGenerator::ReleaseTransientTexture(Procedural); };
	if (!TestNotNull(TEXT("Procedural allocation fits"), Procedural)) return false;
	UTexture2D* Path = FDesignPathRasterizer::Rasterize(
		TEXT("M0 0 L10 0 L10 10 L0 10 Z"), 318, 318, FLinearColor::White,
		TEXT("nonzero"), {}, 0.0f, TEXT("butt"), TEXT("miter"), &Error, &Budget);
	if (!TestNotNull(TEXT("A path allocation can consume the remaining shared budget"), Path)) return false;
	UTexture2D* Extra = FProceduralShapeGenerator::GenerateFromMaterialRef(
		MakeGradientRequest(32, 0.25f), nullptr, &Error, &Budget);
	TestNull(TEXT("A later allocation of another kind sees the same exhausted budget"), Extra);
	TestTrue(TEXT("The mixed-kind refusal is cumulative"), Error.Contains(TEXT("operation limit")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeneratedTextureBudgetPersistentNormalizationUsesActualCacheTest,
	"Design2UMG.Resources.GeneratedTextureBudget.PersistentNormalizationUsesActualCache",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FGeneratedTextureBudgetPersistentNormalizationUsesActualCacheTest::RunTest(const FString&)
{
	TGuardValue<int32> RestoreBudget(UDesign2UMGSettings::Get()->GeneratedTextureBudgetMiB, 1);
	const FString Folder = TEXT("/Game/Design2UMG/Automation/GeneratedTextureBudgetActualCache_")
		+ FGuid::NewGuid().ToString(EGuidFormats::Digits);
	ON_SCOPE_EXIT { Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(Folder); };
	DesignImageResources::FImageOperationBudget Budget(DesignImageResources::GetGeneratedTextureLimits());
	const FString& Refusal = Budget.GetRefusalError();

	FUMGWidgetNode Root = MakeRoot();
	const FUMGWidgetNode Repeated = MakeGradient(TEXT("same"), 400.0f, 1.0f);
	Root.Children = {Repeated, Repeated};
	FString Error;
	TestTrue(TEXT("A per-pass normalizer cache creates one texture for identical requests"),
		FPreBuildResourceNormalizer::NormalizeProceduralTextures(
			Root, Folder / TEXT("Repeated"), TEXT("GeneratedTextureBudget"), &Error, &Budget));
	TestEqual(TEXT("Both nodes reuse the same persisted result"),
		Root.Children[0].Brush.Image.TexturePath.Get(FString()),
		Root.Children[1].Brush.Image.TexturePath.Get(FString()));

	FUMGWidgetNode Distinct = MakeRoot();
	Distinct.Children = {
		MakeGradient(TEXT("first"), 400.0f, 1.0f),
		MakeGradient(TEXT("second"), 400.0f, 0.5f)
	};
	DesignImageResources::FImageOperationBudget FreshBudget(DesignImageResources::GetGeneratedTextureLimits());
	const FString& FreshRefusal = FreshBudget.GetRefusalError();
	TestFalse(TEXT("Distinct requests perform distinct allocations and exceed one MiB"),
		FPreBuildResourceNormalizer::NormalizeProceduralTextures(
			Distinct, Folder / TEXT("Distinct"), TEXT("GeneratedTextureBudget"), &Error, &FreshBudget));
	TestTrue(TEXT("The real second allocation propagates its refusal"),
		Error.Contains(TEXT("operation limit")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeneratedTextureBudgetUvOnlyCropDoesNotConsumeBudgetTest,
	"Design2UMG.Resources.GeneratedTextureBudget.UvOnlyCropDoesNotConsumeAllocationBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FGeneratedTextureBudgetUvOnlyCropDoesNotConsumeBudgetTest::RunTest(const FString&)
{
	TGuardValue<int32> RestoreBudget(UDesign2UMGSettings::Get()->GeneratedTextureBudgetMiB, 1);
	FDesignPathRasterizer::ClearCache();
	const FString Folder = TEXT("/Game/Design2UMG/Automation/GeneratedTextureBudgetUvOnly_")
		+ FGuid::NewGuid().ToString(EGuidFormats::Digits);
	ON_SCOPE_EXIT
	{
		FDesignPathRasterizer::ClearCache();
		Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(Folder);
	};
	DesignImageResources::FImageOperationBudget Budget(DesignImageResources::GetGeneratedTextureLimits());
	const FString& Refusal = Budget.GetRefusalError();

	FUMGWidgetNode Node = MakeGradient(TEXT("uv_only_crop"), 400.0f, 1.0f);
	Node.Brush.Image.Mode = TEXT("Crop");
	Node.Brush.Image.Transform =
		FUMGVisualBrushData::FImageTransformData{0.5f, 0.0f, 0.1f, 0.0f, 0.5f, 0.2f};
	FString Error;
	if (!TestTrue(TEXT("The source gradient consumes its one real allocation"),
		FPreBuildResourceNormalizer::NormalizeProceduralTextures(
			Node, Folder / TEXT("Procedural"), TEXT("GeneratedTextureBudget"), &Error, &Budget))) return false;
	const FString SourcePath = Node.Brush.Image.TexturePath.Get(FString());
	TestFalse(TEXT("The source gradient was persisted"), SourcePath.IsEmpty());
	TestTrue(TEXT("UV-only crop normalization needs no generated texture"),
		FPreBuildResourceNormalizer::NormalizeImageTransformTextures(
			Node, Folder / TEXT("Crop"), TEXT("GeneratedTextureBudget"), &Error, &Budget));
	TestEqual(TEXT("UV-only crop keeps the original texture path"),
		Node.Brush.Image.TexturePath.Get(FString()), SourcePath);

	UTexture2D* RemainingAllocation = FDesignPathRasterizer::Rasterize(
		TEXT("M0 0 L10 0 L10 10 L0 10 Z"), 318, 318, FLinearColor::White,
		TEXT("nonzero"), {}, 0.0f, TEXT("butt"), TEXT("miter"), &Error, &Budget);
	TestNotNull(TEXT("The remaining one-MiB headroom is still available after the UV-only crop"),
		RemainingAllocation);
	TestTrue(TEXT("No allocation refusal was recorded"), Refusal.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeneratedTextureBudgetValidationDoesNotSimulateCumulativeAllocationTest,
	"Design2UMG.Resources.GeneratedTextureBudget.ValidationDoesNotSimulateAllocation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FGeneratedTextureBudgetValidationDoesNotSimulateCumulativeAllocationTest::RunTest(const FString&)
{
	FUMGWidgetNode Root = MakeRoot();
	Root.Children = {
		MakeGradient(TEXT("first"), 400.0f, 1.0f),
		MakeGradient(TEXT("second"), 400.0f, 0.5f)
	};
	FString Error;
	TestTrue(TEXT("Preflight validates request shape but leaves cumulative accounting to allocations"),
		FPreBuildResourceNormalizer::ValidateGeneratedTextureRequests(Root, Error));
	Root.Children[1].WidgetStyle.WidthOverride = MAX_flt;
	TestFalse(TEXT("Preflight still rejects an individually invalid request"),
		FPreBuildResourceNormalizer::ValidateGeneratedTextureRequests(Root, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeneratedTextureBudgetBlueprintBuildActualBudgetTest,
	"Design2UMG.Resources.GeneratedTextureBudget.BlueprintBuildActualBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FGeneratedTextureBudgetBlueprintBuildActualBudgetTest::RunTest(const FString&)
{
	TGuardValue<int32> RestoreBudget(UDesign2UMGSettings::Get()->GeneratedTextureBudgetMiB, 1);
	const FString Folder = TEXT("/Game/Design2UMG/Automation/GeneratedTextureBudgetBlueprintBuild_")
		+ FGuid::NewGuid().ToString(EGuidFormats::Digits);
	ON_SCOPE_EXIT { Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(Folder); };
	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_GeneratedTextureBudgetBlueprintBuild");
	Data.RootWidget = MakeRoot();
	Data.RootWidget.Children = {
		MakeGradient(TEXT("first"), 400.0f, 1.0f),
		MakeGradient(TEXT("second"), 400.0f, 0.5f)
	};
	FDesign2UMGGenerationOutput Output;
	AddExpectedError(
		TEXT("Total decoded image data exceeds the operation limit"),
		EAutomationExpectedErrorFlags::Contains,
		2);
	TestNull(TEXT("Formal generation rejects the second real allocation"),
		FUMGGenerator::BuildWidgetBlueprint(Data, Folder, nullptr, &Output));
	TestTrue(TEXT("Formal generation exposes a resource-preparation diagnostic"),
		Output.MappingDiagnostics.ContainsByPredicate([](const FDesign2UMGMappingDiagnostic& Diagnostic)
		{
			return Diagnostic.Kind == TEXT("resource_preparation_failed")
				&& Diagnostic.Message.Contains(TEXT("operation limit"));
		}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeneratedTextureBudgetSharedBlueprintBuildsTest,
	"Design2UMG.Resources.GeneratedTextureBudget.SharedBudgetSpansBlueprintBuilds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FGeneratedTextureBudgetSharedBlueprintBuildsTest::RunTest(const FString&)
{
	const FString Folder = TEXT("/Game/Design2UMG/Automation/SharedBlueprintBudget_")
		+ FGuid::NewGuid().ToString(EGuidFormats::Digits);
	ON_SCOPE_EXIT { Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(Folder); };
	DesignImageResources::FImageResourceLimits Limits;
	Limits.MaxTotalDecodedBytes = 1024 * 1024;
	DesignImageResources::FImageOperationBudget Budget(Limits);
	FUMGWidgetData Data;
	Data.RootWidget = MakeRoot();
	Data.RootWidget.Children = {MakeGradient(TEXT("gradient"), 400.0f, 1.0f)};
	Data.WidgetName = TEXT("WBP_FirstBudgetedBuild");
	if (!TestNotNull(TEXT("The first blueprint draws from the supplied budget"),
		FUMGGenerator::BuildWidgetBlueprint(Data, Folder, nullptr, nullptr, nullptr, nullptr, &Budget))) return false;
	Data.WidgetName = TEXT("WBP_SecondBudgetedBuild");
	FDesign2UMGGenerationOutput Output;
	AddExpectedError(TEXT("Total decoded image data exceeds the operation limit"), EAutomationExpectedErrorFlags::Contains, 2);
	TestNull(TEXT("The next blueprint shares the accumulated allocation"),
		FUMGGenerator::BuildWidgetBlueprint(Data, Folder, nullptr, &Output, nullptr, nullptr, &Budget));
	TestTrue(TEXT("Shared-budget rejection keeps the preparation diagnostic"),
		Output.MappingDiagnostics.ContainsByPredicate([](const FDesign2UMGMappingDiagnostic& Diagnostic)
		{
			return Diagnostic.Kind == TEXT("resource_preparation_failed")
				&& Diagnostic.Message.Contains(TEXT("operation limit"));
		}));
	Data.WidgetName = TEXT("WBP_IndependentBudgetedBuild");
	TestNotNull(TEXT("An independent blueprint starts a fresh operation"),
		FUMGGenerator::BuildWidgetBlueprint(Data, Folder));
	return true;
}

#endif // WITH_AUTOMATION_WORKER
