// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGWidgetPropertyAccess.h"
#include "DesignPathRasterizer.h"
#include "PreBuildResourceNormalizer.h"
#include "Misc/ScopeExit.h"
#include "ProceduralShapeGenerator.h"
#include "ProceduralShapeGeneratorAssets.h"
#include "UMGImageCropUtils.h"
#include "UMGWidgetNodeAccess.h"
#include "UMGWidgetNodeProceduralResourceAccess.h"
#include "UMGWidgetTypes.h"
#include "UMGJsonParser.h"
#include "UMGJsonSerializer.h"
#include "WidgetResourceBinder.h"
#include "TextureResource.h"
#include "UObject/Package.h"
#include "Design2UMGEffectMaterialBuilder.h"
#include "UMGGenerator.h"
#include "UMGPropertyKeys.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "UMGWidgetTreeBuilder.h"
#include "Components/Image.h"
#include "Tests/Design2UMGTestAssetCleanup.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/AutomationTest.h"
#include "Tests/Design2UMGFileFailureFixture.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Design2UMGSettings.h"
#include "Templates/UnrealTemplate.h"
#include "HAL/FileManager.h"
#include "UObject/UObjectIterator.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// Real filesystem failure: a file occupies the directory needed by the saver.
	struct FScopedBlockedAssetDirectory
	{
		explicit FScopedBlockedAssetDirectory(const FString& ParentPackagePath)
			: PackagePath(ParentPackagePath / TEXT("BlockedDestination"))
			, Filename(FPackageName::LongPackageNameToFilename(PackagePath))
		{
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
			bCreated = FFileHelper::SaveStringToFile(TEXT("test directory blocker"), *Filename);
		}
		~FScopedBlockedAssetDirectory() { if (bCreated) IFileManager::Get().Delete(*Filename); }
		bool Remove()
		{
			if (!bCreated || !IFileManager::Get().Delete(*Filename)) return false;
			bCreated = false;
			return true;
		}
		FString PackagePath;
		FString Filename;
		bool bCreated = false;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesignPathMalformedInputTest,
	"Design2UMG.Resources.Rasterization.PathMalformedInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesignPathMalformedInputTest::RunTest(const FString&)
{
	const TArray<FString> Invalid = {
		TEXT("M 0 0 @"), TEXT("@"), TEXT("M 0"), TEXT("M 0 0 L"), TEXT("M 0 0 L 1"),
		TEXT("M 0 0 C 1 2 3 4 5"), TEXT("M 0 0 A 1 1 0 2 0 10 10"),
		TEXT("M 0 0 A 1 1 0 0 @ 10 10"), TEXT("M 0 0 L 1e+ 2"), TEXT("M + 0"),
		TEXT("M . 0"), TEXT("M 0 0 K 1 2"), TEXT("M 0 0 Z 1 2"),
		TEXT("M 0 0 L 1e999 2"), TEXT("M 0 0 L 1 2 3"), TEXT("L 0 0"), TEXT("M 0 0 L M 1 1")
	};
	AddExpectedError(TEXT("failed to parse path data"), EAutomationExpectedErrorFlags::Contains, Invalid.Num());
	for (const FString& Geometry : Invalid)
	{
		TestNull(*Geometry, FDesignPathRasterizer::Rasterize(FDesignPathRasterRequest(Geometry, FIntPoint(8, 8), FLinearColor::White)));
	}
	for (const FString& Geometry : {
		FString(TEXT("M0 0 10 0 10 10 0 10Z")),
		FString(TEXT("m0 0 10 0 0 10 -10 0z")),
		FString(TEXT("M.0,+0 L1e1 0 L10 1E+1 H0 V0z")),
		FString(TEXT("M0 0 C2 0 10 2 10 10 S2 10 0 0Z")),
		FString(TEXT("M0 0 Q10 0 10 10 T0 0Z")),
		FString(TEXT("M0 0 A10 10 0 01 10 10 L0 10Z"))})
	{
		TestNotNull(*Geometry, FDesignPathRasterizer::Rasterize(FDesignPathRasterRequest(Geometry, FIntPoint(8, 8), FLinearColor::White)));
	}
	FDesignPathRasterizer::FlushPendingTextureUpdates();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesignPathDegenerateBoundsTest,
	"Design2UMG.Resources.Rasterization.PathDegenerateBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FDesignPathDegenerateBoundsTest::RunTest(const FString&)
{
	FDesignPathRasterizer::ClearCache();
	ON_SCOPE_EXIT { FDesignPathRasterizer::ClearCache(); };
	for (const FString& Geometry : { FString(TEXT("M0 0L16 0")), FString(TEXT("M0 0L0 16")) })
	{
		FDesignPathRasterRequest Request(Geometry, FIntPoint(32, 32), {});
		Request.Path.FillRule = TEXT("nonzero");
		Request.Path.StrokeColor = FLinearColor::White;
		Request.Path.StrokeThickness = 1.0f;
		UTexture2D* Texture = FDesignPathRasterizer::Rasterize(Request);
		if (!TestNotNull(TEXT("A visible axis-aligned stroke produces a texture"), Texture)) continue;
		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		const FColor* Pixels = static_cast<const FColor*>(Mip.BulkData.LockReadOnly());
		int32 PaintedPixels = 0;
		if (Pixels) for (int32 Index = 0; Index < 32 * 32; ++Index) PaintedPixels += Pixels[Index].A > 0;
		Mip.BulkData.Unlock();
		TestTrue(TEXT("The line has actual visible pixels"), PaintedPixels > 0);
		TestTrue(TEXT("A thin line does not fill the whole image"), PaintedPixels < 32 * 32);
	}
	FDesignPathRasterRequest Square(TEXT("M10 10L30 10"), FIntPoint(40, 20), {});
	Square.Path.CanvasSize = FVector2D(40, 20);
	Square.Path.StrokeColor = FLinearColor::White;
	Square.Path.StrokeThickness = 10.0f;
	Square.Path.StrokeCap = TEXT("square");
	UTexture2D* SquareTexture = FDesignPathRasterizer::Rasterize(Square);
	if (TestNotNull(TEXT("A square-capped segment rasterizes"), SquareTexture))
	{
		const FColor* Pixels = static_cast<const FColor*>(SquareTexture->GetPlatformData()->Mips[0].BulkData.LockReadOnly());
		TestTrue(TEXT("Square caps include their rectangular corner"), Pixels[5 * 40 + 5].A > 0);
		TestEqual(TEXT("Square caps stop at half the stroke width, without an extra circular tip"), Pixels[10 * 40 + 4].A, uint8(0));
		SquareTexture->GetPlatformData()->Mips[0].BulkData.Unlock();
	}
	Square.Path.Geometry = TEXT("M10 10L30 30");
	Square.Path.CanvasSize = FVector2D(40, 40);
	Square.Size = FIntPoint(40, 40);
	UTexture2D* Diagonal = FDesignPathRasterizer::Rasterize(Square);
	if (TestNotNull(TEXT("A diagonal square-capped segment rasterizes"), Diagonal))
	{
		const FColor* Pixels = static_cast<const FColor*>(Diagonal->GetPlatformData()->Mips[0].BulkData.LockReadOnly());
		TestTrue(TEXT("The pixel loop includes rotated square corners"), Pixels[10 * 40 + 3].A > 0);
		Diagonal->GetPlatformData()->Mips[0].BulkData.Unlock();
	}
	UTexture2D* EmptyFill = FDesignPathRasterizer::Rasterize(FDesignPathRasterRequest(TEXT("M0 0L16 0"), FIntPoint(32, 2), FLinearColor::White));
	TestNotNull(TEXT("A zero-area fill is an empty raster, not a resource failure"), EmptyFill);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesignPathAllocationLimitTest,
	"Design2UMG.Resources.Rasterization.PathAllocationLimits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesignPathAllocationLimitTest::RunTest(const FString&)
{
	TestTrue(TEXT("Exact pixel limit is allowed without allocating it"), FDesignPathRasterizer::IsRasterSizeAllowed(8192, 8192));
	TestTrue(TEXT("Exact dimension limit is allowed"), FDesignPathRasterizer::IsRasterSizeAllowed(16384, 1));
	TestFalse(TEXT("Dimension limit enforced"), FDesignPathRasterizer::IsRasterSizeAllowed(16385, 1));
	TestFalse(TEXT("Pixel limit enforced independently of dimensions"), FDesignPathRasterizer::IsRasterSizeAllowed(8192, 8193));
	TestFalse(TEXT("Wide overflow input rejected before multiplication"), FDesignPathRasterizer::IsRasterSizeAllowed(MAX_int64, MAX_int64));
	TestFalse(TEXT("Zero dimension rejected"), FDesignPathRasterizer::IsRasterSizeAllowed(0, 1));
	FIntPoint Size;
	TestTrue(TEXT("Ordinary design size accepted"), FDesignPathRasterizer::TryGetRasterSize(20.0f, 10.0f, Size));
	TestEqual(TEXT("Supersampling retained"), Size, FIntPoint(40, 20));
	AddExpectedError(TEXT("invalid or excessive path texture size"), EAutomationExpectedErrorFlags::Contains, 4);
	for (float Width : {12000.0f, MAX_flt, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
	{
		TestFalse(TEXT("Invalid design dimension rejected before conversion"), FDesignPathRasterizer::TryGetRasterSize(Width, 10.0f, Size));
		TestEqual(TEXT("Rejected size clears output"), Size, FIntPoint::ZeroValue);
	}
	AddExpectedError(TEXT("pixel budget"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Supersampled total pixel limit enforced"), FDesignPathRasterizer::TryGetRasterSize(5000.0f, 5000.0f, Size));
	AddExpectedError(TEXT("invalid or excessive raster size"), EAutomationExpectedErrorFlags::Contains, 1);
	TestNull(TEXT("Direct rasterization cannot bypass limits"),
		FDesignPathRasterizer::Rasterize(FDesignPathRasterRequest(TEXT("M0 0L10 0L10 10Z"), FIntPoint(MAX_int32, MAX_int32), FLinearColor::White)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPathOperationBudgetTest,
	"Design2UMG.Resources.Rasterization.PathOperationBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPathOperationBudgetTest::RunTest(const FString&)
{
	TGuardValue<int32> RestoreBudget(UDesign2UMGSettings::Get()->GeneratedTextureBudgetMiB, 2);
	FDesignPathRasterizer::ClearCache();
	const FString Folder = TEXT("/Game/Design2UMG/Automation/PathBudget_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	ON_SCOPE_EXIT { FDesignPathRasterizer::ClearCache(); Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(Folder); };
	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_ExcessivePaths");
	Data.RootWidget.Type = TEXT("CanvasPanel");
	Data.RootWidget.Bounds.W = 100;
	Data.RootWidget.Bounds.H = 100;
	FUMGWidgetNode Path;
	Path.Type = TEXT("Image");
	Path.Name = TEXT("LargePath");
	Path.Path.Geometry = TEXT("M0 0L10 0L10 10L0 10Z");
	Path.Path.FillColor = FLinearColor::White;
	Path.WidgetStyle.WidthOverride = 256;
	Path.WidgetStyle.HeightOverride = 256;
	Data.RootWidget.Children = {Path, Path};
	Data.RootWidget.Children[1].Path.FillColor = FLinearColor::Red;
	FString Error;
	TestTrue(TEXT("Preflight does not allocate or bill path textures"),
		FPreBuildResourceNormalizer::ValidateGeneratedTextureRequests(Data.RootWidget, Error));
	Path.Path.FillColor = FLinearColor::Blue;
	Data.RootWidget.Children.Add(Path);
	// A path raster is allocated only on a cache miss inside FDesignPathRasterizer, and the
	// pre-check cannot know hit from miss, so it draws no path pixels at all. The three
	// paths are still accounted for: the generation below is refused at the allocation.
	TestTrue(TEXT("The pre-check draws no path pixels, because hit/miss is unknowable before materialization"),
		FPreBuildResourceNormalizer::ValidateGeneratedTextureRequests(Data.RootWidget, Error));
	FUMGWidgetNode Repeated = Data.RootWidget;
	Repeated.Children = {Path, Path, Path};
	TestTrue(TEXT("Reused identical path textures draw no extra pixels in the pre-check"),
		FPreBuildResourceNormalizer::ValidateGeneratedTextureRequests(Repeated, Error));
	FDesign2UMGGenerationOutput Output;
	// Once at the allocation point (the normalizer) and once at the generation entry.
	AddExpectedError(TEXT("Total decoded image data exceeds the operation limit"), EAutomationExpectedErrorFlags::Contains, 2);
	TestNull(TEXT("Generation refuses the whole request when a path raster overruns the budget"),
		FUMGGenerator::BuildWidgetBlueprint(Data, Folder, nullptr, &Output));
	TestTrue(TEXT("Budget failure is a structured generation error"), Output.MappingDiagnostics.ContainsByPredicate(
		[](const FDesign2UMGMappingDiagnostic& D) { return D.Kind == TEXT("resource_preparation_failed"); }));
	TestTrue(TEXT("Budget failure identifies the node and reason"), Output.MappingDiagnostics.ContainsByPredicate(
		[](const FDesign2UMGMappingDiagnostic& D) {
			return D.Message.Contains(TEXT("LargePath")) && D.Message.Contains(TEXT("operation limit")); }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPathCacheBudgetTest,
	"Design2UMG.Resources.Rasterization.PathCacheBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPathCacheBudgetTest::RunTest(const FString&)
{
	FDesignPathRasterizer::ClearCache();
	ON_SCOPE_EXIT { FDesignPathRasterizer::FlushPendingTextureUpdates(); FDesignPathRasterizer::ClearCache(); };
	const FString Geometry = TEXT("M0 0L10 0L10 10L0 10Z");
	for (int32 Index = 0; Index < 17; ++Index)
	{
		const FLinearColor Color(Index / 16.0f, 0.5f, 0.5f, 1.0f);
		FDesignPathRasterRequest Request(Geometry, FIntPoint(2048, 2048), Color);
		Request.Path.FillRule = TEXT("evenodd");
		if (!TestNotNull(TEXT("Ordinary large path generates"), FDesignPathRasterizer::Rasterize(Request))) return false;
		TestTrue(TEXT("Retained cache pixels never exceed one raster budget"),
			FDesignPathRasterizer::GetCachedPixelCount() <= FDesignPathRasterizer::MaxRasterPixels);
		FDesignPathRasterizer::FlushPendingTextureUpdates();
	}
	TestEqual(TEXT("The 17th texture evicts rather than retaining 272 MiB"),
		FDesignPathRasterizer::GetCachedPixelCount(), FDesignPathRasterizer::MaxRasterPixels);
	FDesignPathRasterizer::ClearCache();
	CollectGarbage(RF_NoFlags);
	for (int32 Index = 0; Index < 257; ++Index)
	{
		FDesignPathRasterRequest Request(Geometry, FIntPoint(8, 8), FLinearColor(Index / 256.0f, 0, 0, 1));
		Request.Path.FillRule = TEXT("evenodd");
		if (!FDesignPathRasterizer::Rasterize(Request)) return false;
	}
	TestEqual(TEXT("Entry budget is enforced on insertion, not one request later"),
		FDesignPathRasterizer::GetCachedPixelCount(), 256ll * 8 * 8);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeometryMaskSaveFailurePropagationTest,
	"Design2UMG.Resources.Rasterization.GeometryMaskSaveFailurePropagation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGeometryMaskSaveFailurePropagationTest::RunTest(const FString&)
{
	if (!TestNotNull(TEXT("Base material is ready before injecting instance failure"),
		Design2UMGEffectMaterialBuilder::GetOrCreateGeometryMaskRetainerMaterial())) return false;
	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_MaskSaveFailure");
	Data.RootWidget.Type = TEXT("CanvasPanel");
	Data.RootWidget.Name = TEXT("OriginalRoot");
	Data.RootWidget.Bounds.W = 200;
	Data.RootWidget.Bounds.H = 100;
	const FGuid Nonce = FGuid::NewGuid();
	const FString PackagePath = TEXT("/Temp/Design2UMGMaskFailure_") + Nonce.ToString(EGuidFormats::Digits);
	UWidgetBlueprint* Existing = FUMGGenerator::BuildWidgetBlueprint(Data, PackagePath);
	if (!TestNotNull(TEXT("Existing Blueprint is created"), Existing)) return false;
	UWidgetTree* OriginalTree = Existing->WidgetTree;
	UWidget* OriginalRoot = OriginalTree->RootWidget;
	Data.RootWidget.Type = TEXT("RetainerBox");
	Data.RootWidget.Effect.EffectMaterial = UMGPropertyKeys::RetainerGeometryMaskEffectRef;
	Data.RootWidget.Effect.GeometryMaskShape = EUMGGeometryMaskShape::Ellipse;
	Data.RootWidget.Effect.GeometryMaskSize = FVector2D(200 + (Nonce.A & 0xffff), 100 + (Nonce.B & 0xffff));
	Data.RootWidget.Effect.GeometryMaskCornerRadii = FVector4(0, 0, 0, 0);
	const auto CountRootedInstances = []()
	{
		int32 Count = 0;
		for (TObjectIterator<UMaterialInstanceConstant> It; It; ++It) if (It->IsRooted()) ++Count;
		return Count;
	};
	const int32 RootsBefore = CountRootedInstances();
	Design2UMGEditorTests::FScopedPackageSaveFailure SaveFailure(Design2UMGEditorTests::IsPackageSaveTargetFile);
	AddExpectedError(TEXT("Failed to save geometry mask material instance"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("Failed to create or persist retainer effect material"), EAutomationExpectedErrorFlags::Contains, 1);
	FDesign2UMGGenerationOutput Output;
	TestNull(TEXT("Unsaved dependency fails generation"),
		FUMGGenerator::BuildWidgetBlueprint(Data, PackagePath, nullptr, &Output, nullptr, Existing));
	TestEqual(TEXT("Failed dependency does not leak roots"), CountRootedInstances(), RootsBefore);
	TestEqual(TEXT("Existing WidgetTree preserved"), Existing->WidgetTree.Get(), OriginalTree);
	TestEqual(TEXT("Existing root preserved"), Existing->WidgetTree->RootWidget.Get(), OriginalRoot);
	TestTrue(TEXT("Failure reaches structured generation diagnostics"),
		Output.MappingDiagnostics.ContainsByPredicate([](const FDesign2UMGMappingDiagnostic& Diagnostic)
		{
			return Diagnostic.Kind == TEXT("resource_preparation_failed") && Diagnostic.Message.Contains(TEXT("OriginalRoot"));
		}));
	SaveFailure.AllowSaves();
	FString Error;
	TArray<TStrongObjectPtr<UMaterialInterface>> Materials;
	TestTrue(TEXT("Retry succeeds rather than returning a failed cached object"),
		Design2UMGEffectMaterialBuilder::PrepareRetainerEffectMaterials(Data.RootWidget, Materials, Error));
	UMaterialInterface* Retry = LoadObject<UMaterialInterface>(nullptr, *Data.RootWidget.Effect.EffectMaterial.GetValue());
	if (TestNotNull(TEXT("Retry resolves a persisted material"), Retry))
	{
		const FString FileName = FPackageName::LongPackageNameToFilename(Retry->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
		TestTrue(TEXT("Retry actually saved a package"), IFileManager::Get().FileExists(*FileName));
		TestFalse(TEXT("Successful dependency needs no global root"), Retry->IsRooted());
		if (!TestEqual(TEXT("The build owns the dependency until tree handoff"), Materials.Num(), 1)) return false;
		CollectGarbage(RF_NoFlags);
		TestTrue(TEXT("Scoped build owner keeps material valid across GC"), IsValid(Materials[0].Get()));
		// This exact, nonce-named file was created by this test, not a shared fixture.
		Materials.Reset();
		IFileManager::Get().Delete(*FileName);
		Retry->ClearFlags(RF_Public | RF_Standalone);
		Retry->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeneratedTextureSaveFailureTest,
	"Design2UMG.Resources.Rasterization.GeneratedTextureSaveFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FGeneratedTextureSaveFailureTest::RunTest(const FString&)
{
	const FString Folder = TEXT("/Game/Design2UMG/Automation/TextureSave_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	ON_SCOPE_EXIT { Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(Folder); };
	for (const bool bPath : {true, false})
	{
		FUMGWidgetData Data;
		Data.WidgetName = bPath ? TEXT("WBP_PathFailure") : TEXT("WBP_GradientFailure");
		Data.RootWidget.Type = TEXT("CanvasPanel");
		Data.RootWidget.Name = TEXT("OriginalRoot");
		Data.RootWidget.Bounds.W = 96;
		Data.RootWidget.Bounds.H = 64;
		UWidgetBlueprint* Existing = FUMGGenerator::BuildWidgetBlueprint(Data, Folder);
		if (!TestNotNull(TEXT("Original tree created before fault injection"), Existing)) return false;
		UWidgetTree* OriginalTree = Existing->WidgetTree;
		UWidget* OriginalRoot = OriginalTree->RootWidget;
		FUMGWidgetNode Image;
		Image.Type = TEXT("Image");
		Image.Name = TEXT("GeneratedImage");
		Image.WidgetStyle.WidthOverride = 16;
		Image.WidgetStyle.HeightOverride = 16;
		Image.Bounds.W = Image.Bounds.H = 16;
		if (bPath)
		{
			Image.Path.Geometry = TEXT("M0 0L10 0L10 10L0 10Z");
			Image.Path.FillColor = FLinearColor::White;
		}
		else
		{
			Image.Brush.Gradient.Type = TEXT("Linear");
			Image.Brush.Gradient.Stops = {{FLinearColor::Red, 0.0f}, {FLinearColor::Blue, 1.0f}};
		}
		Data.RootWidget.Children = {Image};
		const auto RootedTextureCount = []()
		{
			int32 Count = 0;
			for (TObjectIterator<UTexture2D> It; It; ++It) if (It->IsRooted()) ++Count;
			return Count;
		};
		const int32 RootsBefore = RootedTextureCount();
		FScopedBlockedAssetDirectory BlockedDestination(Folder);
		if (!TestTrue(TEXT("Real filesystem blocker created"), BlockedDestination.bCreated)) return false;
		AddExpectedError(TEXT("Failed to persist generated texture"), EAutomationExpectedErrorFlags::Contains, 2);
		FDesign2UMGGenerationOutput Output;
		TestNull(TEXT("Unsaved texture prevents blueprint replacement"),
			FUMGGenerator::BuildWidgetBlueprint(Data, BlockedDestination.PackagePath, nullptr, &Output, nullptr, Existing));
		if (!bPath) TestEqual(TEXT("Failed procedural intermediate is released immediately"), RootedTextureCount(), RootsBefore);
		TestEqual(TEXT("Old tree survives the resource failure"), Existing->WidgetTree.Get(), OriginalTree);
		TestEqual(TEXT("Old root survives the resource failure"), Existing->WidgetTree->RootWidget.Get(), OriginalRoot);
		TestTrue(TEXT("Failure identifies the generated image"), Output.MappingDiagnostics.ContainsByPredicate(
			[](const FDesign2UMGMappingDiagnostic& D) { return D.Kind == TEXT("resource_preparation_failed") && D.Message.Contains(TEXT("GeneratedImage")); }));
		TestTrue(TEXT("Filesystem blocker removed before retry"), BlockedDestination.Remove());
		if (!TestNotNull(TEXT("Retry builds successfully"),
			FUMGGenerator::BuildWidgetBlueprint(Data, Folder, nullptr, nullptr, nullptr, Existing))) return false;
		UImage* Widget = Cast<UImage>(Existing->WidgetTree->FindWidget(TEXT("GeneratedImage")));
		if (!TestNotNull(TEXT("Retry creates image widget"), Widget)) return false;
		UTexture2D* Texture = Cast<UTexture2D>(Design2UMGWidgetPropertyAccess::GetBrush(*Widget).GetResourceObject());
		if (!TestNotNull(TEXT("Retry uses a texture asset"), Texture)) return false;
		const FString Filename = FPackageName::LongPackageNameToFilename(Texture->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
		TestTrue(TEXT("Retry has a real persisted package, not the failed memory object"), IFileManager::Get().FileExists(*Filename));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesignTypedGradientRasterParityTest,
	"Design2UMG.Resources.Rasterization.TypedGradientRasterParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesignTypedGradientRasterParityTest::RunTest(const FString&)
{
	const auto Pixels = [](UTexture2D* Texture)
	{
		TArray<FColor> Result;
		if (Texture && Texture->GetPlatformData() && !Texture->GetPlatformData()->Mips.IsEmpty())
		{
			FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
			const FColor* Data = static_cast<const FColor*>(Mip.BulkData.LockReadOnly());
			if (Data) Result.Append(Data, Mip.SizeX * Mip.SizeY);
			Mip.BulkData.Unlock();
		}
		return Result;
	};
	for (const FString& Type : {FString(TEXT("Linear")), FString(TEXT("Radial")), FString(TEXT("Angular"))})
	{
		for (int32 StopCount : {0, 1, 2, 4})
		{
			FUMGWidgetNode Node;
			Node.Brush.Gradient.Type = Type;
			Node.Brush.Gradient.Rotation = 37.5123456f;
			Node.Brush.Gradient.StartPoint = FVector2D(0.123456789, 0.314159265);
			Node.Brush.Gradient.EndPoint = FVector2D(0.891234567, 0.765432198);
			Node.Brush.Gradient.Center = FVector2D(0.22005678, 0.73654321);
			Node.Brush.Gradient.Size = FVector2D(0.82345678, 1.35678912);
			Node.WidgetStyle.WidthOverride = 31.0f;
			Node.WidgetStyle.HeightOverride = 17.0f;
			for (int32 Index = 0; Index < StopCount; ++Index)
			{
				Node.Brush.Gradient.Stops.Add({FLinearColor(0.1111111f + Index * 0.2f, 0.2222222f, 0.7777777f - Index * 0.1f, 0.3333333f + Index * 0.2f),
					(StopCount - Index) / (StopCount + 1.0f)});
			}
			FUMGProceduralResourceRequest Typed;
			TestTrue(TEXT("Typed request builds"), Design2UMGWidgetNodeProceduralResourceAccess::TryBuildProceduralResourceRequest(Node, Typed));
			TestFalse(TEXT("Typed request carries no JSON"), Typed.MaterialParams.IsSet());
			TestEqual(TEXT("Request retains full coordinate precision"), Typed.Gradient.StartPoint.GetValue(), Node.Brush.Gradient.StartPoint.GetValue());
			FUMGProceduralResourceRequest Exported;
			Exported.MaterialRef = Typed.MaterialRef;
			Exported.WidthOverride = Typed.WidthOverride;
			Exported.HeightOverride = Typed.HeightOverride;
			Exported.MaterialParams = Design2UMGWidgetNodeAccess::SerializeGradientMaterialParams(Node.Brush.Gradient);
			UTexture2D* TypedTexture = FProceduralShapeGenerator::GenerateFromMaterialRef(Typed);
			UTexture2D* ExportedTexture = FProceduralShapeGenerator::GenerateFromMaterialRef(Exported);
			TestNotNull(TEXT("Typed texture generated"), TypedTexture);
			TestNotNull(TEXT("Explicit exported-input texture generated"), ExportedTexture);
			const TArray<FColor> TypedPixels = Pixels(TypedTexture);
			TestEqual(TEXT("Expected pixel count"), TypedPixels.Num(), 31 * 17);
			TestTrue(*FString::Printf(TEXT("%s with %d stops preserves every pixel"), *Type, StopCount), TypedPixels == Pixels(ExportedTexture));
		}
	}
	FProceduralShapeGenerator::FlushPendingTextureUpdates();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeneratedRasterAllocationLimitsTest,
	"Design2UMG.Resources.Rasterization.GeneratedRasterAllocationLimits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FGeneratedRasterAllocationLimitsTest::RunTest(const FString&)
{
	const DesignImageResources::FImageResourceLimits Limits;
	FIntPoint Size;
	TestTrue(TEXT("Subpixel dimensions produce one pixel"), Limits.TryGetPixelSize(0.2f, 0.4f, Size));
	TestEqual(TEXT("Positive subpixels are not treated as invalid zero sizes"), Size, FIntPoint(1, 1));
	FUMGProceduralResourceRequest Request;
	Request.MaterialRef = TEXT("gradientlinear");
	Request.HeightOverride = 8.0f;
	FString Error;
	for (float Width : {MAX_flt, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(), -1.0f, 0.0f, 16385.0f})
	{
		Request.WidthOverride = Width;
		TestNull(TEXT("Invalid source dimensions never reach a texture allocation"),
			FProceduralShapeGenerator::GenerateFromMaterialRef(Request, nullptr, &Error));
		TestTrue(TEXT("Rejected procedural request returns an error"), Error.Contains(TEXT("dimensions")));
	}
	Request.WidthOverride = 8192.0f;
	Request.HeightOverride = 8193.0f;
	TestNull(TEXT("Independent total pixel cap applies to gradients"),
		FProceduralShapeGenerator::GenerateFromMaterialRef(Request, nullptr, &Error));
	Request.MaterialRef = TEXT("circle");
	Request.WidthOverride = 10000.0f;
	Request.HeightOverride = 1.0f;
	AddExpectedError(TEXT("Invalid or excessive generated raster dimensions"), EAutomationExpectedErrorFlags::Contains, 1);
	TestNull(TEXT("Circle validates its square output, not just the narrow input"),
		FProceduralShapeGenerator::GenerateFromMaterialRef(Request, nullptr, &Error));
	TestFalse(TEXT("Known shape allocation failure is not silently treated as an unsupported material"), Error.IsEmpty());
	Request.MaterialRef = TEXT("placeholdersurface");
	Request.WidthOverride = Request.HeightOverride = 5000.0f;
	AddExpectedError(TEXT("Invalid or excessive supersampled placeholder dimensions"), EAutomationExpectedErrorFlags::Contains, 1);
	TestNull(TEXT("Placeholder validates its supersampled output"),
		FProceduralShapeGenerator::GenerateFromMaterialRef(Request, nullptr, &Error));
	AddExpectedError(TEXT("Invalid or excessive shadow mask dimensions"), EAutomationExpectedErrorFlags::Contains, 1);
	TestNull(TEXT("Direct shadow generator rejects before allocating the alpha mask"),
		ProceduralShapeGeneratorAssets::GenerateBoxShadow(MAX_int32, MAX_int32, FLinearColor::White, 0.0f, 0.0f, -1.0f, nullptr));
	Request.MaterialRef = TEXT("ExternalMaterialNotAProceduralShape");
	Request.WidthOverride = Request.HeightOverride = 8.0f;
	TestNull(TEXT("Nonprocedural references are not rasterized"), FProceduralShapeGenerator::GenerateFromMaterialRef(Request, nullptr, &Error));
	TestTrue(TEXT("An unrecognized material is distinct from a failed known generator"), Error.IsEmpty());
	Request.MaterialRef = TEXT("gradientlinear");
	UTexture2D* Source = FProceduralShapeGenerator::GenerateFromMaterialRef(Request, nullptr, &Error);
	if (!TestNotNull(TEXT("Ordinary source still generates"), Source)) return false;
	ON_SCOPE_EXIT { FProceduralShapeGenerator::FlushPendingTextureUpdates(); FProceduralShapeGenerator::ReleaseTransientTexture(Source); };
	TestTrue(TEXT("Success clears stale errors"), Error.IsEmpty());
	TestNull(TEXT("Crop output product is checked before reading the source or allocating"),
		FProceduralShapeGenerator::ApplyAffineCrop(Source, 1, 0, 0, 0, 1, 0, MAX_int32, MAX_int32, &Error));
	TestFalse(TEXT("Rejected crop returns an error"), Error.IsEmpty());
	TestNull(TEXT("Nonfinite crop coordinates never reach integer pixel indexing"),
		FProceduralShapeGenerator::ApplyAffineCrop(Source, std::numeric_limits<float>::quiet_NaN(), 0, 0, 0, 1, 0, 8, 8, &Error));
	UTexture2D* Crop = FProceduralShapeGenerator::ApplyAffineCrop(Source, 1, 0, 0, 0, 1, 0, 8, 8, &Error);
	TestNotNull(TEXT("Ordinary crop still generates"), Crop);
	TestTrue(TEXT("Successful crop clears errors"), Error.IsEmpty());
	FProceduralShapeGenerator::ReleaseTransientTexture(Crop);
	FUMGWidgetNode Node;
	Node.Brush.Image.Mode = TEXT("Crop");
	FUMGVisualBrushData::FImageTransformData Transform;
	Transform.B = 0.5f;
	Node.Brush.Image.Transform = Transform;
	Node.WidgetStyle.WidthOverride = MAX_flt;
	Node.WidgetStyle.HeightOverride = 8.0f;
	UMGImageCropUtils::FImageCropRequest CropRequest;
	TestTrue(TEXT("Crop mode remains identified even when its dimensions are invalid"), UMGImageCropUtils::TryBuildCropRequest(Node, CropRequest));
	TestEqual(TEXT("Invalid crop width is not converted into a spurious one-pixel allocation"), CropRequest.OutputWidth, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeneratedTextureCreationFailureTest,
	"Design2UMG.Resources.Rasterization.GeneratedTextureCreationFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FGeneratedTextureCreationFailureTest::RunTest(const FString&)
{
	const FString Folder = TEXT("/Game/Design2UMG/Automation/CreationFailure_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	ON_SCOPE_EXIT { Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(Folder); };
	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_CreationFailure");
	Data.RootWidget.Type = TEXT("CanvasPanel");
	Data.RootWidget.Bounds.W = 96;
	Data.RootWidget.Bounds.H = 64;
	UWidgetBlueprint* Existing = FUMGGenerator::BuildWidgetBlueprint(Data, Folder);
	if (!TestNotNull(TEXT("Existing tree prepared"), Existing)) return false;
	UWidgetTree* OriginalTree = Existing->WidgetTree;
	UWidget* OriginalRoot = OriginalTree->RootWidget;
	for (bool bPath : {true, false})
	{
		FUMGWidgetNode Image;
		Image.Type = TEXT("Image");
		Image.Name = TEXT("InvalidResource");
		Image.WidgetStyle.WidthOverride = Image.WidgetStyle.HeightOverride = 16.0f;
		if (bPath)
		{
			Image.Path.Geometry = TEXT("M0 0 @");
			Image.Path.FillColor = FLinearColor::White;
			AddExpectedError(TEXT("DesignPathRasterizer: failed to parse path data"), EAutomationExpectedErrorFlags::Contains, 1);
		}
		else
		{
			Image.Brush.Gradient.Type = TEXT("Linear");
			Image.Brush.Gradient.Stops = {{FLinearColor::Red, 0.0f}, {FLinearColor::Blue, 1.0f}};
			Image.WidgetStyle.WidthOverride = MAX_flt;
		}
		Data.RootWidget.Children = {Image};
		FDesign2UMGGenerationOutput Output;
		AddExpectedError(TEXT("Failed to create generated texture"), EAutomationExpectedErrorFlags::Contains, bPath ? 2 : 1);
		TestNull(TEXT("Known creation failures stop blueprint replacement"),
			FUMGGenerator::BuildWidgetBlueprint(Data, Folder, nullptr, &Output, nullptr, Existing));
		TestEqual(TEXT("Existing tree preserved"), Existing->WidgetTree.Get(), OriginalTree);
		TestEqual(TEXT("Existing root preserved"), Existing->WidgetTree->RootWidget.Get(), OriginalRoot);
		TestTrue(TEXT("Failure reaches structured generation diagnostics"), Output.MappingDiagnostics.ContainsByPredicate(
			[](const FDesign2UMGMappingDiagnostic& D) { return D.Kind == TEXT("resource_preparation_failed") && D.Message.Contains(TEXT("InvalidResource")); }));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPathCacheRequestIdentityTest,
	"Design2UMG.Resources.Rasterization.PathCacheRequestIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPathCacheRequestIdentityTest::RunTest(const FString&)
{
	const FString Folder = TEXT("/Game/Design2UMG/Automation/PathIdentity_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	ON_SCOPE_EXIT { Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(Folder); };
	FUMGWidgetNode Absolute;
	Absolute.Type = TEXT("Image");
	Absolute.Name = TEXT("SameName");
	Absolute.Path.Geometry = TEXT("M0 0 L10 0 L10 10 L0 10 Z");
	Absolute.Path.FillColor = FLinearColor::White;
	Absolute.WidgetStyle.WidthOverride = Absolute.WidgetStyle.HeightOverride = 32;
	FUMGWidgetNode Relative = Absolute;
	Relative.Path.Geometry = TEXT("m0 0 l10 0 l10 10 l0 10 z");
	TestEqual(TEXT("Fixture exercises the old case-insensitive hash collision"),
		GetTypeHash(Absolute.Path.Geometry.GetValue()), GetTypeHash(Relative.Path.Geometry.GetValue()));
	const auto Materialize = [&](TArray<FUMGWidgetNode> Nodes, const TCHAR* Case,
		TArray<TArray64<uint8>>& Pixels, TArray<FString>& Paths)
	{
		FUMGWidgetNode Root;
		Root.Children = MoveTemp(Nodes);
		FString Error;
		if (!TestTrue(TEXT("Paths materialize"), FPreBuildResourceNormalizer::NormalizePathTextures(Root, Folder / Case, TEXT("Probe"), &Error))) return false;
		for (const FUMGWidgetNode& Node : Root.Children)
		{
			const FString Path = Node.Brush.Image.TexturePath.Get(FString());
			UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *Path);
			if (!TestNotNull(TEXT("Persisted texture resolves"), Texture)) return false;
			TArray64<uint8> Raw;
			if (!TestTrue(TEXT("Persisted pixels readable"), Texture->Source.GetMipData(Raw, 0))) return false;
			TestEqual(TEXT("Complete pixel buffer"), Raw.Num(), 64ll * 64 * 4);
			Pixels.Add(MoveTemp(Raw));
			Paths.Add(Path);
		}
		return true;
	};
	TArray<TArray64<uint8>> A, B, AB, BA, AA;
	TArray<FString> PathsA, PathsB, PathsAB, PathsBA, PathsAA;
	if (!Materialize({Absolute}, TEXT("A"), A, PathsA)
		|| !Materialize({Relative}, TEXT("B"), B, PathsB)
		|| !Materialize({Absolute, Relative}, TEXT("AB"), AB, PathsAB)
		|| !Materialize({Relative, Absolute}, TEXT("BA"), BA, PathsBA)
		|| !Materialize({Absolute, Absolute}, TEXT("AA"), AA, PathsAA)) return false;
	TestTrue(TEXT("The two legal paths draw different shapes"), A[0] != B[0]);
	TestTrue(TEXT("AB does not reuse the first texture for the second shape"), AB[0] == A[0] && AB[1] == B[0]);
	TestTrue(TEXT("Swapping input order preserves both shapes"), BA[0] == B[0] && BA[1] == A[0]);
	TestTrue(TEXT("Different shapes with the same node name get distinct assets"), PathsAB[0] != PathsAB[1]);
	TestEqual(TEXT("Identical requests still reuse their asset"), PathsAA[0], PathsAA[1]);
	// Distinct geometry consumes separate raster allocations, even when string hashes collide.
	// A budget with room for one raster rejects the second distinct allocation.
	Absolute.WidgetStyle.WidthOverride = Absolute.WidgetStyle.HeightOverride = 2048;
	Relative.WidgetStyle.WidthOverride = Relative.WidgetStyle.HeightOverride = 2048;
	const int64 TotalPixelBudget =
		static_cast<int64>(DesignImageResources::FImageResourceLimits::DefaultMaxTotalDecodedBytes) / 4;
	DesignImageResources::FImageOperationBudget PathBudget((DesignImageResources::FImageResourceLimits()));
	FString Error;
	TestTrue(TEXT("The budget is left with exactly one 4096x4096 raster of headroom"),
		PathBudget.ConsumeDecodedPixels(TotalPixelBudget - 4096 * 4096, Error));
	FUMGWidgetNode Root;
	Root.Children = {Absolute, Relative};
	TestFalse(TEXT("Budget accounting uses complete equality too, not the colliding string hash"),
		FPreBuildResourceNormalizer::NormalizePathTextures(
			Root, Folder / TEXT("Budget"), TEXT("Probe"), &Error, &PathBudget));
	TestTrue(TEXT("The refusal names the cumulative operation limit"), Error.Contains(TEXT("operation limit")));
	// The lower-level raster cache must not round distinct alpha values into one key.
	const FString Geometry = Absolute.Path.Geometry.GetValue();
	UTexture2D* HalfAlpha = FDesignPathRasterizer::Rasterize(FDesignPathRasterRequest(Geometry, FIntPoint(8, 8), FLinearColor(1, 1, 1, 0.5f)));
	UTexture2D* BelowHalf = FDesignPathRasterizer::Rasterize(FDesignPathRasterRequest(Geometry, FIntPoint(8, 8), FLinearColor(1, 1, 1, 0.49999f)));
	if (!TestNotNull(TEXT("Half alpha raster"), HalfAlpha) || !TestNotNull(TEXT("Below-half alpha raster"), BelowHalf)) return false;
	const auto PixelAlpha = [](UTexture2D* Texture)
	{
		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		const FColor* Pixels = static_cast<const FColor*>(Mip.BulkData.LockReadOnly());
		const uint8 Alpha = Pixels[4 * 8 + 4].A;
		Mip.BulkData.Unlock();
		return Alpha;
	};
	TestEqual(TEXT("Half alpha preserves its quantized pixel value"), PixelAlpha(HalfAlpha), FLinearColor(1, 1, 1, 0.5f).ToFColor(true).A);
	TestEqual(TEXT("Sub-four-decimal alpha changes are not lost by the cache"), PixelAlpha(BelowHalf), FLinearColor(1, 1, 1, 0.49999f).ToFColor(true).A);
	TestTrue(TEXT("The alpha fixture distinguishes both requests"), PixelAlpha(HalfAlpha) != PixelAlpha(BelowHalf));
	FDesignPathRasterizer::FlushPendingTextureUpdates();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeneratedRasterActualAllocationBudgetTest,
	"Design2UMG.Resources.Rasterization.GeneratedRasterActualAllocationBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FGeneratedRasterActualAllocationBudgetTest::RunTest(const FString&)
{
	TGuardValue<int32> RestoreBudget(UDesign2UMGSettings::Get()->GeneratedTextureBudgetMiB, 1);
	const FString Folder = TEXT("/Game/Design2UMG/Automation/ActualRasterBudget_")
		+ FGuid::NewGuid().ToString(EGuidFormats::Digits);
	ON_SCOPE_EXIT { Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(Folder); };

	FUMGWidgetNode Node;
	Node.Type = UMGWidgetTypes::Image;
	Node.Id = TEXT("generated_then_cropped");
	Node.Name = TEXT("GeneratedThenCropped");
	Node.Bounds = FDesignPixelBounds{0.0f, 0.0f, 400.0f, 400.0f};
	Node.WidgetStyle.WidthOverride = 400.0f;
	Node.WidgetStyle.HeightOverride = 400.0f;
	Node.Brush.Gradient.Type = TEXT("Linear");
	Node.Brush.Gradient.Stops = {
		{FLinearColor::Red, 0.0f},
		{FLinearColor::Blue, 1.0f}
	};
	Node.Brush.Image.Mode = TEXT("Crop");
	Node.Brush.Image.Transform = FUMGVisualBrushData::FImageTransformData{1.0f, 0.25f, 0.0f, 0.0f, 1.0f, 0.0f};

	DesignImageResources::FImageOperationBudget Budget(DesignImageResources::GetGeneratedTextureLimits());
	const FString& RefusalError = Budget.GetRefusalError();
	FString Error;
	TestTrue(TEXT("The first real 400x400 procedural allocation fits one MiB"),
		FPreBuildResourceNormalizer::NormalizeProceduralTextures(
			Node, Folder / TEXT("Procedural"), TEXT("Actual"), &Error, &Budget));
	TestFalse(TEXT("Procedural normalization writes the source used by the following crop"),
		Node.Brush.Image.TexturePath.Get(FString()).IsEmpty());
	TestTrue(TEXT("The image transform survives procedural normalization"),
		Node.Brush.Image.Transform.IsSet());

	TestFalse(TEXT("The following real 400x400 affine allocation exceeds the same operation budget"),
		FPreBuildResourceNormalizer::NormalizeImageTransformTextures(
			Node, Folder / TEXT("Transform"), TEXT("Actual"), &Error, &Budget));
	TestTrue(TEXT("The allocation refusal reaches the normalizer result"),
		Error.Contains(TEXT("operation limit")) || RefusalError.Contains(TEXT("operation limit")));
	TestTrue(TEXT("A refused crop preserves the already materialized source path"),
		!Node.Brush.Image.TexturePath.Get(FString()).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShadowKernelBudgetTest,
	"Design2UMG.Resources.Rasterization.ShadowKernelBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FShadowKernelBudgetTest::RunTest(const FString&)
{
	using ProceduralShapeGeneratorAssets::TryGetShadowKernelRadius;
	int32 Radius = -1;
	TestTrue(TEXT("Zero blur is a valid no-filter request"), TryGetShadowKernelRadius(8, 8, 0, Radius));
	TestEqual(TEXT("No kernel for zero blur"), Radius, 0);
	TestTrue(TEXT("A normal large UI shadow remains admissible without allocating"), TryGetShadowKernelRadius(2048, 2048, 40, Radius));
	TestEqual(TEXT("Ordinary discrete support is unchanged"), Radius, 60);
	TestTrue(TEXT("Vectorized large shadow stays within the filter-operation budget"), TryGetShadowKernelRadius(1760, 1300, 160, Radius));
	TestEqual(TEXT("Large shadow keeps the full discrete support"), Radius, 240);
	TestTrue(TEXT("Coefficient boundary accepted without allocating"), TryGetShadowKernelRadius(8, 8, 2730, Radius));
	TestEqual(TEXT("Coefficient radius boundary"), Radius, 4095);
	TestFalse(TEXT("Coefficient limit is independent of texture dimensions"), TryGetShadowKernelRadius(8, 8, 2731, Radius));
	TestEqual(TEXT("Rejected request clears the radius"), Radius, 0);
	TestFalse(TEXT("Legal output area cannot trigger excessive two-pass convolution"), TryGetShadowKernelRadius(8192, 8192, 4, Radius));
	FUMGProceduralResourceRequest Request;
	Request.MaterialRef = TEXT("boxshadow");
	Request.WidthOverride = Request.HeightOverride = 8;
	const TArray<float> Invalid = {40000.0f, MAX_flt, std::numeric_limits<float>::infinity(),
		std::numeric_limits<float>::quiet_NaN(), -1.0f};
	AddExpectedError(TEXT("Invalid or excessive CPU shadow blur workload"), EAutomationExpectedErrorFlags::Contains, Invalid.Num());
	for (float Blur : Invalid)
	{
		TestFalse(TEXT("Reject before float-to-int conversion and kernel allocation"), TryGetShadowKernelRadius(8, 8, Blur, Radius));
		Request.ShadowBlur = Blur;
		FString Error;
		TestNull(TEXT("Invalid blur does not generate a texture"), FProceduralShapeGenerator::GenerateFromMaterialRef(Request, nullptr, &Error));
		TestFalse(TEXT("Failure reaches the procedural resource caller"), Error.IsEmpty());
	}
	Request.ShadowBlur = 2;
	UTexture2D* Texture = FProceduralShapeGenerator::GenerateFromMaterialRef(Request);
	TestNotNull(TEXT("Normal CPU shadow still renders"), Texture);
	FProceduralShapeGenerator::FlushPendingTextureUpdates();
	FProceduralShapeGenerator::ReleaseTransientTexture(Texture);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExistingTextureFailureIsolationTest,
	"Design2UMG.Resources.Rasterization.ExistingTextureFailureIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FExistingTextureFailureIsolationTest::RunTest(const FString&)
{
	const FString Folder = TEXT("/Game/Design2UMG/Automation/TextureIsolation_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	ON_SCOPE_EXIT { Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(Folder); };
	TStrongObjectPtr<UTexture2D> Source(NewObject<UTexture2D>(CreatePackage(*(Folder / TEXT("Source"))), TEXT("Source"), RF_Public | RF_Standalone));
	TArray<FColor> SourcePixels;
	SourcePixels.Init(FColor::Red, 16 * 16);
	Source->Source.Init(16, 16, 1, 1, TSF_BGRA8, reinterpret_cast<const uint8*>(SourcePixels.GetData()));
	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_TextureIsolation");
	Data.RootWidget.Type = TEXT("CanvasPanel");
	Data.RootWidget.Bounds.W = 96;
	Data.RootWidget.Bounds.H = 64;
	FUMGWidgetNode Image;
	Image.Type = TEXT("Image");
	Image.Name = TEXT("CroppedImage");
	Image.Brush.Image.TexturePath = Source->GetPathName();
	Image.Brush.Image.Mode = TEXT("Crop");
	Image.WidgetStyle.WidthOverride = Image.WidgetStyle.HeightOverride = 16;
	Image.Bounds.W = Image.Bounds.H = 16;
	FUMGVisualBrushData::FImageTransformData Transform;
	Transform.A = Transform.D = 0.5f;
	Transform.B = 0.25f; // An affine crop, not a UV-region-only request.
	Image.Brush.Image.Transform = Transform;
	Data.RootWidget.Children = {Image};
	UWidgetBlueprint* Existing = FUMGGenerator::BuildWidgetBlueprint(Data, Folder);
	if (!TestNotNull(TEXT("Initial blueprint with a persisted texture builds"), Existing)) return false;
	const auto TextureIn = [](UWidgetBlueprint* Blueprint) -> UTexture2D*
	{
		UImage* Widget = Cast<UImage>(Blueprint->WidgetTree->FindWidget(TEXT("CroppedImage")));
		return Widget ? Cast<UTexture2D>(Design2UMGWidgetPropertyAccess::GetBrush(*Widget).GetResourceObject()) : nullptr;
	};
	TStrongObjectPtr<UTexture2D> OldTexture(TextureIn(Existing));
	if (!TestNotNull(TEXT("Initial dependent texture exists"), OldTexture.Get())) return false;
	const FString OldPath = OldTexture->GetPathName();
	const FString OldFile = FPackageName::LongPackageNameToFilename(OldTexture->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
	TArray64<uint8> OldPixels;
	TArray<uint8> OldDisk;
	if (!TestTrue(TEXT("Initial dependent pixels read"), OldTexture->Source.GetMipData(OldPixels, 0))
		|| !TestTrue(TEXT("Initial dependent package read"), FFileHelper::LoadFileToArray(OldDisk, *OldFile))) return false;
	UWidgetTree* OldTree = Existing->WidgetTree;
	UWidget* OldRoot = OldTree->RootWidget;
	// Simulate a source image edit at the same path; size and transform stay fixed.
	SourcePixels.Init(FColor::Blue, 16 * 16);
	Source->Source.Init(16, 16, 1, 1, TSF_BGRA8, reinterpret_cast<const uint8*>(SourcePixels.GetData()));
	FScopedBlockedAssetDirectory BlockedDestination(Folder);
	if (!TestTrue(TEXT("Real filesystem blocker created"), BlockedDestination.bCreated)) return false;
	AddExpectedError(TEXT("Failed to persist generated texture"), EAutomationExpectedErrorFlags::Contains, 2);
	FDesign2UMGGenerationOutput Output;
	TestNull(TEXT("A failed updated dependency fails the build"),
		FUMGGenerator::BuildWidgetBlueprint(Data, BlockedDestination.PackagePath, nullptr, &Output, nullptr, Existing));
	TestEqual(TEXT("Failure keeps the original tree"), Existing->WidgetTree.Get(), OldTree);
	TestEqual(TEXT("Failure keeps the original root"), Existing->WidgetTree->RootWidget.Get(), OldRoot);
	TestEqual(TEXT("Failure keeps the original brush resource"), TextureIn(Existing), OldTexture.Get());
	TestTrue(TEXT("Structured failure is reported"), Output.MappingDiagnostics.ContainsByPredicate(
		[](const FDesign2UMGMappingDiagnostic& D) { return D.Kind == TEXT("resource_preparation_failed"); }));
	TArray64<uint8> AfterFailure;
	TArray<uint8> DiskAfterFailure;
	OldTexture->Source.GetMipData(AfterFailure, 0);
	FFileHelper::LoadFileToArray(DiskAfterFailure, *OldFile);
	TestTrue(TEXT("Old texture pixels, not just tree pointers, survive failure"), AfterFailure == OldPixels);
	TestTrue(TEXT("Old texture disk package survives failure byte for byte"), DiskAfterFailure == OldDisk);
	TestTrue(TEXT("Filesystem blocker removed before retry"), BlockedDestination.Remove());
	if (!TestNotNull(TEXT("Retry succeeds"), FUMGGenerator::BuildWidgetBlueprint(Data, Folder, nullptr, nullptr, nullptr, Existing))) return false;
	UTexture2D* NewTexture = TextureIn(Existing);
	if (!TestNotNull(TEXT("New brush resource exists"), NewTexture)) return false;
	TestTrue(TEXT("Changed output gets a new asset identity"), NewTexture->GetPathName() != OldPath);
	TArray64<uint8> NewPixels;
	NewTexture->Source.GetMipData(NewPixels, 0);
	TestTrue(TEXT("The source edit is represented in the new texture"), NewPixels.Num() == OldPixels.Num() && NewPixels != OldPixels);
	OldTexture->Source.GetMipData(AfterFailure, 0);
	TestTrue(TEXT("Successful retry still never mutates the old dependent asset"), AfterFailure == OldPixels);
	const FString NewPath = NewTexture->GetPathName();
	if (!TestNotNull(TEXT("Unchanged retry reuses the saved content"), FUMGGenerator::BuildWidgetBlueprint(Data, Folder, nullptr, nullptr, nullptr, Existing))) return false;
	TestEqual(TEXT("No new asset identity for unchanged output"), TextureIn(Existing)->GetPathName(), NewPath);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExplicitPathFillRuleTest,
	"Design2UMG.Resources.Rasterization.ExplicitPathFillRule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FExplicitPathFillRuleTest::RunTest(const FString&)
{
	FDesignPathRasterizer::ClearCache();
	ON_SCOPE_EXIT { FDesignPathRasterizer::FlushPendingTextureUpdates(); FDesignPathRasterizer::ClearCache(); };
	const FString Geometry = TEXT("M0 0L100 0L100 100L0 100Z M20 20L80 20L80 80L20 80Z");
	for (const TCHAR* Rule : {TEXT("nonzero"), TEXT("evenodd")})
	{
		FDesignPathRasterRequest Request(Geometry, FIntPoint(100, 100), FLinearColor::White);
		Request.Path.FillRule = Rule;
		UTexture2D* Texture = FDesignPathRasterizer::Rasterize(Request);
		if (!TestNotNull(TEXT("Compound path rasterized"), Texture)) return false;
		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		const FColor* Pixels = static_cast<const FColor*>(Mip.BulkData.LockReadOnly());
		const uint8 CenterAlpha = Pixels[50 * 100 + 50].A;
		Mip.BulkData.Unlock();
		TestEqual(TEXT("Explicit winding rule controls the nested contour"), CenterAlpha,
			FString(Rule) == TEXT("nonzero") ? uint8(255) : uint8(0));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeBorderGradientStrokeTest,
	"Design2UMG.Resources.Rasterization.NativeBorderGradientStroke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FNativeBorderGradientStrokeTest::RunTest(const FString&)
{
	const FString Folder = TEXT("/Game/Design2UMG/Automation/BorderPaint_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	ON_SCOPE_EXIT { Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(Folder); };
	FUMGWidgetData Data;
	FUMGWidgetNode& Node = Data.RootWidget;
	Node.Type = TEXT("Border");
	Node.Name = TEXT("PaintedBorder");
	Node.WidgetStyle.WidthOverride = Node.WidgetStyle.HeightOverride = 16.0f;
	Node.Path.CanvasSize = FVector2D(16, 16);
	Node.Path.Geometry = TEXT("M0 0 L16 0 L16 16 L0 16 Z");
	Node.Path.StrokeGeometry = TEXT("M1 1 L15 1 L15 15 L1 15 Z");
	Node.Path.FillColor = FLinearColor::Red;
	Node.Brush.Image.TexturePath = TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture");
	Node.Brush.Image.Mode = TEXT("Crop");
	FUMGVisualBrushData::FImageTransformData ImageTransform;
	ImageTransform.A = ImageTransform.D = 0.5f;
	ImageTransform.TX = ImageTransform.TY = 0.25f;
	Node.Brush.Image.Transform = ImageTransform;
	Node.Path.FillRule = TEXT("nonzero");
	Node.Path.StrokeColor = FLinearColor::White;
	Node.Path.StrokeThickness = 2.0f;
	Node.Path.StrokeGradient.Type = TEXT("Linear");
	Node.Path.StrokeGradient.Stops = {{FLinearColor::Blue, 0.0f}, {FLinearColor::Green, 1.0f}};
	FUMGWidgetData Parsed;
	FString Error;
	if (!TestTrue(TEXT("Compound paint round-trips through the existing JSON bridge"), FUMGJsonParser::ParseJson(FUMGJsonSerializer::Serialize(Data), Parsed, &Error))) return false;
	TestEqual(TEXT("Stroke geometry remains independent of fill geometry"), Parsed.RootWidget.Path.StrokeGeometry.Get(FString()), Node.Path.StrokeGeometry.GetValue());
	TestTrue(TEXT("The independent drawing canvas round-trips"), Parsed.RootWidget.Path.CanvasSize.IsSet() && Parsed.RootWidget.Path.CanvasSize.GetValue().Equals(FVector2D(16, 16)));
	TestEqual(TEXT("Both stroke stops survive"), Parsed.RootWidget.Path.StrokeGradient.Stops.Num(), 2);
	FUMGWidgetNode PreviewNode = Parsed.RootWidget;
	PreviewNode.Brush.Image.TexturePath = TEXT("preview-source");
	TMap<FString, UTexture2D*> PreviewImages;
	PreviewImages.Add(TEXT("preview-source"), LoadObject<UTexture2D>(nullptr, *Node.Brush.Image.TexturePath.GetValue()));
	UTexture2D* Preview = nullptr;
	TestTrue(TEXT("Native Border preview resolves the same compound resource"), FWidgetResourceBinder::TryResolveProceduralTexture(PreviewNode, GetTransientPackage(), Preview, nullptr, nullptr, &PreviewImages) == FWidgetResourceBinder::EProceduralTextureResult::Resolved);
	UBorder* Border = NewObject<UBorder>();
	FUMGWidgetTreeBuilder::ApplyWidgetProperties(Border, PreviewNode, &PreviewImages);
	TestTrue(TEXT("Native Border receives its bound preview image"), Border->Background.GetResourceObject() == Preview);
	UTexture2D* PreviousPreview = nullptr;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		UTexture2D* Source = UTexture2D::CreateTransient(2, 2, PF_B8G8R8A8);
		if (!TestNotNull(TEXT("Transient preview source exists"), Source)) return false;
		FColor* SourcePixels = static_cast<FColor*>(Source->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
		for (int32 Pixel = 0; Pixel < 4; ++Pixel) SourcePixels[Pixel] = FColor::White;
		Source->GetPlatformData()->Mips[0].BulkData.Unlock();
		Source->UpdateResource();
		PreviewImages[TEXT("preview-source")] = Source;
		TestTrue(TEXT("Updated preview source resolves"), FWidgetResourceBinder::TryResolveProceduralTexture(
			PreviewNode, GetTransientPackage(), Preview, nullptr, nullptr, &PreviewImages) == FWidgetResourceBinder::EProceduralTextureResult::Resolved);
		TestTrue(TEXT("Source identity is not just its unchanged URL or empty source GUID"), Preview != PreviousPreview);
		PreviousPreview = Preview;
	}
	if (!TestTrue(TEXT("Compound background persists"), FPreBuildResourceNormalizer::NormalizePathTextures(Parsed.RootWidget, Folder, TEXT("Border"), &Error))) return false;
	TestEqual(TEXT("Persistence retains the native control"), Parsed.RootWidget.Type, FString(TEXT("Border")));
	TestTrue(TEXT("Persistence does not reintroduce paint wrappers"), Parsed.RootWidget.Children.IsEmpty());
	TestTrue(TEXT("Border receives a materialized background"), Parsed.RootWidget.Brush.Image.TexturePath.IsSet());
	TestFalse(TEXT("Baked image transform is not applied twice"), Parsed.RootWidget.Brush.Image.Transform.IsSet());
	FUMGWidgetNode Variants;
	Variants.Children = {Node, Node};
	Variants.Children[1].Path.StrokeGradient.Stops[0].Color = FLinearColor::White;
	if (!TestTrue(TEXT("Distinct stroke paints materialize together"), FPreBuildResourceNormalizer::NormalizePathTextures(Variants, Folder / TEXT("Variants"), TEXT("Paint"), &Error))) return false;
	TestNotEqual(TEXT("Stroke paint participates in resource identity"), Variants.Children[0].Brush.Image.TexturePath.Get(FString()), Variants.Children[1].Brush.Image.TexturePath.Get(FString()));
	FUMGWidgetNode MissingImage = Node;
	MissingImage.Brush.Image.TexturePath = Folder / TEXT("MissingCompoundSource.MissingCompoundSource");
	TestFalse(TEXT("A compound background still requires its actual source image"),
		FPreBuildResourceNormalizer::NormalizePathTextures(MissingImage, Folder / TEXT("Missing"), TEXT("Paint"), &Error));
	TestTrue(TEXT("The required image failure comes from actual raster preparation"), Error.Contains(TEXT("Path fill image is not available")));

	return true;
}

#endif
