// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignPathRasterizer.h"
#include "PreBuildResourceNormalizer.h"
#include "Misc/ScopeExit.h"
#include "ProceduralShapeGenerator.h"
#include "ProceduralShapeGeneratorAssets.h"
#include "UMGImageCropUtils.h"
#include "UMGWidgetNodeAccess.h"
#include "UMGWidgetNodeProceduralResourceAccess.h"
#include "TextureResource.h"
#include "UObject/Package.h"
#include "Design2UMGEffectMaterialBuilder.h"
#include "UMGGenerator.h"
#include "UMGPropertyKeys.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Tests/Design2UMGTestAssetCleanup.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "UObject/UObjectIterator.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesignPathMalformedInputTest,
	"Design2UMG.ReviewSafety.PathMalformedInput",
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
		TestNull(*Geometry, FDesignPathRasterizer::Rasterize(Geometry, 8, 8, FLinearColor::White));
	}
	for (const FString& Geometry : {
		FString(TEXT("M0 0 10 0 10 10 0 10Z")),
		FString(TEXT("m0 0 10 0 0 10 -10 0z")),
		FString(TEXT("M.0,+0 L1e1 0 L10 1E+1 H0 V0z")),
		FString(TEXT("M0 0 C2 0 10 2 10 10 S2 10 0 0Z")),
		FString(TEXT("M0 0 Q10 0 10 10 T0 0Z")),
		FString(TEXT("M0 0 A10 10 0 01 10 10 L0 10Z"))})
	{
		TestNotNull(*Geometry, FDesignPathRasterizer::Rasterize(Geometry, 8, 8, FLinearColor::White));
	}
	FDesignPathRasterizer::FlushPendingTextureUpdates();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesignPathAllocationLimitTest,
	"Design2UMG.ReviewSafety.PathAllocationLimits",
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
		FDesignPathRasterizer::Rasterize(TEXT("M0 0L10 0L10 10Z"), MAX_int32, MAX_int32, FLinearColor::White));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPathOperationBudgetTest,
	"Design2UMG.ReviewSafety.PathOperationBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPathOperationBudgetTest::RunTest(const FString&)
{
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
	Path.WidgetStyle.WidthOverride = 4096;
	Path.WidgetStyle.HeightOverride = 4096;
	Data.RootWidget.Children = {Path, Path};
	Data.RootWidget.Children[1].Path.FillColor = FLinearColor::Red;
	FString Error;
	TestTrue(TEXT("Exactly the shared 512 MiB operation budget is accepted without allocating"),
		FPreBuildResourceNormalizer::ValidatePathTextureBudget(Data.RootWidget, Error));
	Path.Path.FillColor = FLinearColor::Blue;
	Data.RootWidget.Children.Add(Path);
	TestFalse(TEXT("Multiple individually legal paths cannot exceed the operation budget"),
		FPreBuildResourceNormalizer::ValidatePathTextureBudget(Data.RootWidget, Error));
	TestTrue(TEXT("Budget error identifies the node and reason"), Error.Contains(TEXT("LargePath")) && Error.Contains(TEXT("operation limit")));
	FUMGWidgetNode Repeated = Data.RootWidget;
	Repeated.Children = {Path, Path, Path};
	TestTrue(TEXT("Reused identical path textures consume one allocation budget"),
		FPreBuildResourceNormalizer::ValidatePathTextureBudget(Repeated, Error));
	FDesign2UMGGenerationOutput Output;
	AddExpectedError(TEXT("Total decoded image data exceeds the operation limit"), EAutomationExpectedErrorFlags::Contains, 1);
	TestNull(TEXT("Generation refuses the whole request before allocating textures"),
		FUMGGenerator::BuildWidgetBlueprint(Data, TEXT("/Temp/ExcessivePaths"), nullptr, &Output));
	TestTrue(TEXT("Budget failure is a structured generation error"), Output.MappingDiagnostics.ContainsByPredicate(
		[](const FDesign2UMGMappingDiagnostic& D) { return D.Kind == TEXT("resource_preparation_failed"); }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPathCacheBudgetTest,
	"Design2UMG.ReviewSafety.PathCacheBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPathCacheBudgetTest::RunTest(const FString&)
{
	FDesignPathRasterizer::ClearCache();
	ON_SCOPE_EXIT { FDesignPathRasterizer::FlushPendingTextureUpdates(); FDesignPathRasterizer::ClearCache(); };
	const FString Geometry = TEXT("M0 0L10 0L10 10L0 10Z");
	for (int32 Index = 0; Index < 17; ++Index)
	{
		const FLinearColor Color(Index / 16.0f, 0.5f, 0.5f, 1.0f);
		if (!TestNotNull(TEXT("Ordinary large path generates"),
			FDesignPathRasterizer::Rasterize(Geometry, 2048, 2048, Color, TEXT("evenodd")))) return false;
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
		if (!FDesignPathRasterizer::Rasterize(Geometry, 8, 8, FLinearColor(Index / 256.0f, 0, 0, 1), TEXT("evenodd"))) return false;
	}
	TestEqual(TEXT("Entry budget is enforced on insertion, not one request later"),
		FDesignPathRasterizer::GetCachedPixelCount(), 256ll * 8 * 8);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeometryMaskSaveFailurePropagationTest,
	"Design2UMG.ReviewSafety.GeometryMaskSaveFailurePropagation",
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
	Design2UMGEffectMaterialBuilder::ArmGeometryMaskSaveFailureOnce();
	AddExpectedError(TEXT("Failed to save geometry mask material instance"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("Failed to create or persist geometry mask material"), EAutomationExpectedErrorFlags::Contains, 1);
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
	FString Error;
	TArray<TStrongObjectPtr<UMaterialInterface>> Materials;
	TestTrue(TEXT("Retry succeeds rather than returning a failed cached object"),
		Design2UMGEffectMaterialBuilder::PrepareGeometryMaskMaterials(Data.RootWidget, Materials, Error));
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
	"Design2UMG.ReviewSafety.GeneratedTextureSaveFailure",
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
		FPreBuildResourceNormalizer::ArmTextureSaveFailureOnce();
		AddExpectedError(TEXT("Failed to persist generated texture"), EAutomationExpectedErrorFlags::Contains, 2);
		FDesign2UMGGenerationOutput Output;
		TestNull(TEXT("Unsaved texture prevents blueprint replacement"),
			FUMGGenerator::BuildWidgetBlueprint(Data, Folder, nullptr, &Output, nullptr, Existing));
		if (!bPath) TestEqual(TEXT("Failed procedural intermediate is released immediately"), RootedTextureCount(), RootsBefore);
		TestEqual(TEXT("Old tree survives the resource failure"), Existing->WidgetTree.Get(), OriginalTree);
		TestEqual(TEXT("Old root survives the resource failure"), Existing->WidgetTree->RootWidget.Get(), OriginalRoot);
		TestTrue(TEXT("Failure identifies the generated image"), Output.MappingDiagnostics.ContainsByPredicate(
			[](const FDesign2UMGMappingDiagnostic& D) { return D.Kind == TEXT("resource_preparation_failed") && D.Message.Contains(TEXT("GeneratedImage")); }));
		if (!TestNotNull(TEXT("Retry builds successfully"),
			FUMGGenerator::BuildWidgetBlueprint(Data, Folder, nullptr, nullptr, nullptr, Existing))) return false;
		UImage* Widget = Cast<UImage>(Existing->WidgetTree->FindWidget(TEXT("GeneratedImage")));
		if (!TestNotNull(TEXT("Retry creates image widget"), Widget)) return false;
		UTexture2D* Texture = Cast<UTexture2D>(Widget->GetBrush().GetResourceObject());
		if (!TestNotNull(TEXT("Retry uses a texture asset"), Texture)) return false;
		const FString Filename = FPackageName::LongPackageNameToFilename(Texture->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
		TestTrue(TEXT("Retry has a real persisted package, not the failed memory object"), IFileManager::Get().FileExists(*Filename));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesignTypedGradientRasterParityTest,
	"Design2UMG.ReviewSafety.TypedGradientRasterParity",
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
	"Design2UMG.ReviewSafety.GeneratedRasterAllocationLimits",
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
	Node.WidgetStyle.ImageMode = TEXT("Crop");
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
	"Design2UMG.ReviewSafety.GeneratedTextureCreationFailure",
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
		AddExpectedError(TEXT("Failed to create generated texture"), EAutomationExpectedErrorFlags::Contains, 2);
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
	"Design2UMG.ReviewSafety.PathCacheRequestIdentity",
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
	Absolute.WidgetStyle.WidthOverride = Absolute.WidgetStyle.HeightOverride = 4096;
	Relative.WidgetStyle.WidthOverride = Relative.WidgetStyle.HeightOverride = 4096;
	FUMGWidgetNode Third = Absolute;
	Third.Path.FillColor = FLinearColor::Red;
	FUMGWidgetNode Root;
	Root.Children = {Absolute, Relative, Third};
	FString Error;
	TestFalse(TEXT("Budget accounting uses complete equality too, not the colliding string hash"),
		FPreBuildResourceNormalizer::ValidatePathTextureBudget(Root, Error));
	// The lower-level raster cache must not round distinct alpha values into one key.
	const FString Geometry = Absolute.Path.Geometry.GetValue();
	UTexture2D* HalfAlpha = FDesignPathRasterizer::Rasterize(Geometry, 8, 8, FLinearColor(1, 1, 1, 0.5f));
	UTexture2D* BelowHalf = FDesignPathRasterizer::Rasterize(Geometry, 8, 8, FLinearColor(1, 1, 1, 0.49999f));
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShadowKernelBudgetTest,
	"Design2UMG.ReviewSafety.ShadowKernelBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FShadowKernelBudgetTest::RunTest(const FString&)
{
	using ProceduralShapeGeneratorAssets::TryGetShadowKernelRadius;
	int32 Radius = -1;
	TestTrue(TEXT("Zero blur is a valid no-filter request"), TryGetShadowKernelRadius(8, 8, 0, Radius));
	TestEqual(TEXT("No kernel for zero blur"), Radius, 0);
	TestTrue(TEXT("A normal large UI shadow remains admissible without allocating"), TryGetShadowKernelRadius(2048, 2048, 40, Radius));
	TestEqual(TEXT("Ordinary discrete support is unchanged"), Radius, 60);
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
	"Design2UMG.ReviewSafety.ExistingTextureFailureIsolation",
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
		return Widget ? Cast<UTexture2D>(Widget->GetBrush().GetResourceObject()) : nullptr;
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
	FPreBuildResourceNormalizer::ArmTextureSaveFailureOnce();
	AddExpectedError(TEXT("Failed to persist generated texture"), EAutomationExpectedErrorFlags::Contains, 2);
	FDesign2UMGGenerationOutput Output;
	TestNull(TEXT("A failed updated dependency fails the build"),
		FUMGGenerator::BuildWidgetBlueprint(Data, Folder, nullptr, &Output, nullptr, Existing));
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

#endif
