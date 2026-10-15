// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGWidgetPropertyAccess.h"
#include "Misc/AutomationTest.h"
#include "ProceduralShapeGenerator.h"
#include "WidgetResourceBinder.h"
#include "UMGSchema.h"
#include "UMGPropertyKeys.h"
#include "UMGValueParsing.h"
#include "UMGWidgetNodeAccess.h"
#include "UMGWidgetNodeProceduralResourceAccess.h"
#include "UMGWidgetTreeBuilder.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "TextureResource.h"
#include "Engine/Texture2D.h"

#if WITH_AUTOMATION_WORKER

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAffineCropReadsTextureSourcePixels,
	"Design2UMG.ImageCrop.ReadsTextureSourcePixels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FAffineCropReadsTextureSourcePixels::RunTest(const FString& Parameters)
{
	const TArray<FColor> SourcePixels = {
		FColor::Red,
		FColor::Green,
		FColor::Blue,
		FColor::White,
	};
	UTexture2D* SourceTexture = NewObject<UTexture2D>();
	SourceTexture->Source.Init(2, 2, 1, 1, TSF_BGRA8, reinterpret_cast<const uint8*>(SourcePixels.GetData()));

	UTexture2D* Result = FProceduralShapeGenerator::ApplyAffineCrop(
		SourceTexture,
		1.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f,
		2, 2);
	if (!TestNotNull(TEXT("Affine crop produces a texture from TextureSource pixels"), Result))
	{
		return false;
	}

	FTexturePlatformData* PlatformData = Result->GetPlatformData();
	if (!TestTrue(TEXT("Affine crop result exposes a readable mip"), PlatformData && PlatformData->Mips.Num() > 0))
	{
		return false;
	}

	const FColor* ResultPixels = static_cast<const FColor*>(PlatformData->Mips[0].BulkData.LockReadOnly());
	if (!TestNotNull(TEXT("Affine crop result mip is readable"), ResultPixels))
	{
		PlatformData->Mips[0].BulkData.Unlock();
		return false;
	}

	for (int32 Index = 0; Index < SourcePixels.Num(); ++Index)
	{
		TestEqual(*FString::Printf(TEXT("Affine crop preserves source pixel %d"), Index), ResultPixels[Index], SourcePixels[Index]);
	}
	PlatformData->Mips[0].BulkData.Unlock();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAffineCropUsesFigmaImageTransformDirection,
	"Design2UMG.ImageCrop.UsesFigmaImageTransformDirection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FAffineCropUsesFigmaImageTransformDirection::RunTest(const FString& Parameters)
{
	const TArray<FColor> SourcePixels = {
		FColor(1, 2, 3, 255), FColor(11, 12, 13, 255), FColor(21, 22, 23, 255),
		FColor(31, 32, 33, 255), FColor(41, 42, 43, 255), FColor(51, 52, 53, 255),
		FColor(61, 62, 63, 255), FColor(71, 72, 73, 255), FColor(81, 82, 83, 255),
	};
	UTexture2D* SourceTexture = NewObject<UTexture2D>();
	SourceTexture->Source.Init(3, 3, 1, 1, TSF_BGRA8, reinterpret_cast<const uint8*>(SourcePixels.GetData()));

	UTexture2D* Result = FProceduralShapeGenerator::ApplyAffineCrop(
		SourceTexture,
		0.5f, 0.5f, 0.0f,
		0.5f, -0.5f, 0.5f,
		2, 2);
	if (!TestNotNull(TEXT("Affine crop produces a transformed texture"), Result))
	{
		return false;
	}

	FTexturePlatformData* PlatformData = Result->GetPlatformData();
	if (!TestTrue(TEXT("Transformed texture exposes a readable mip"), PlatformData && PlatformData->Mips.Num() > 0))
	{
		return false;
	}

	const FColor* ResultPixels = static_cast<const FColor*>(PlatformData->Mips[0].BulkData.LockReadOnly());
	if (!TestNotNull(TEXT("Transformed texture mip is readable"), ResultPixels))
	{
		PlatformData->Mips[0].BulkData.Unlock();
		return false;
	}

	const int32 ExpectedSourceIndices[] = {3, 7, 1, 5};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(ExpectedSourceIndices); ++Index)
	{
		TestEqual(
			*FString::Printf(TEXT("Destination pixel %d samples the expected source UV"), Index),
			ResultPixels[Index],
			SourcePixels[ExpectedSourceIndices[Index]]);
	}
	PlatformData->Mips[0].BulkData.Unlock();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetResourceBinderTexturePath,
	"Design2UMG.WidgetResourceBinder.TexturePath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FWidgetResourceBinderTexturePath::RunTest(const FString& Parameters)
{
	// Invalid MaterialRef plus valid TexturePath: binder should fall back to the texture path and bind successfully.
	FUMGWidgetNode Node;
	Node.Type = TEXT("Image");
	Node.Name = TEXT("TestImage");
	Node.Brush.MaterialRef = TEXT("__invalid_shape_that_does_not_exist__");
	Node.Brush.Image.TexturePath = TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture");

	UImage* ImageWidget = NewObject<UImage>();
	TMap<FString, UTexture2D*> TextureMap;

	const bool bBound = FWidgetResourceBinder::BindImageResource(ImageWidget, Node, &TextureMap);

	// After MaterialRef generation fails, it should fall back to TexturePath and return true.
	TestTrue(TEXT("Fallback to TexturePath when MaterialRef invalid"), bBound);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetResourceBinderMaterialRefSuccess,
	"Design2UMG.WidgetResourceBinder.MaterialRefSuccess",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FWidgetResourceBinderMaterialRefSuccess::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Node;
	Node.Type = TEXT("Image");
	Node.Name = TEXT("TestImage");
	// Use a known procedural shape type.
	Node.Brush.MaterialRef = TEXT("roundedrect");

	UImage* ImageWidget = NewObject<UImage>();

	const bool bBound = FWidgetResourceBinder::BindImageResource(ImageWidget, Node, nullptr);

	// roundedrect is a valid MaterialRef and should bind successfully, returning true.
	TestTrue(TEXT("MaterialRef 'roundedrect' binds successfully"), bBound);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetResourceBinderGradientDescriptorMaterialPath,
	"Design2UMG.WidgetResourceBinder.GradientDescriptorMaterialPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FWidgetResourceBinderGradientDescriptorMaterialPath::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Node;
	Node.Type = TEXT("Image");
	Node.Name = TEXT("GradientImage");
	Node.Brush.Gradient.Type = TEXT("Radial");
	Node.Brush.Gradient.Center = FVector2D(0.5f, 0.5f);
	Node.Brush.Gradient.Size = FVector2D(0.8f, 0.6f);
	Node.Brush.Gradient.Stops = {
		{ FLinearColor::White, 0.0f },
		{ FLinearColor::Black, 1.0f }
	};
	Node.WidgetStyle.WidthOverride = 48.0f;
	Node.WidgetStyle.HeightOverride = 24.0f;

	FUMGProceduralResourceRequest Request;
	TestTrue(TEXT("Gradient brush builds a typed-first procedural request"),
		Design2UMGWidgetNodeProceduralResourceAccess::TryBuildProceduralResourceRequest(Node, Request));
	TestEqual(TEXT("Gradient brush resolves radial material ref"), Request.MaterialRef, FString(TEXT("radialgradient")));

	UImage* ImageWidget = NewObject<UImage>();
	const bool bBound = FWidgetResourceBinder::BindImageResource(ImageWidget, Node, nullptr);
	TestTrue(TEXT("Descriptor-backed gradient material path still binds"), bBound);
	TestTrue(TEXT("Gradient material path produces a brush resource"), Design2UMGWidgetPropertyAccess::GetBrush(*ImageWidget).GetResourceObject() != nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetResourceBinderMaterialRefPriority,
	"Design2UMG.WidgetResourceBinder.MaterialRefPriority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FWidgetResourceBinderMaterialRefPriority::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Node;
	Node.Type = TEXT("Image");
	Node.Name = TEXT("TestImage");
	// Set both MaterialRef and TexturePath.
	Node.Brush.MaterialRef = TEXT("roundedrect");
	Node.Brush.Image.TexturePath = TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture");

	UImage* ImageWidget = NewObject<UImage>();

	const bool bBound = FWidgetResourceBinder::BindImageResource(ImageWidget, Node, nullptr);

	// MaterialRef takes priority: when roundedrect succeeds, it should return true without using the texture path.
	TestTrue(TEXT("MaterialRef takes priority over TexturePath"), bBound);

	// Verify the brush comes from a procedural texture with a nonzero ImageSize.
	const FSlateBrush Brush = Design2UMGWidgetPropertyAccess::GetBrush(*ImageWidget);
	TestTrue(TEXT("Brush ImageSize set by material path"), Brush.ImageSize.X > 0.0f && Brush.ImageSize.Y > 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetResourceBinderExplicitEmptyMaterialRefBlocksTextureFallback,
	"Design2UMG.WidgetResourceBinder.ExplicitEmptyMaterialRefBlocksTextureFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FWidgetResourceBinderExplicitEmptyMaterialRefBlocksTextureFallback::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Node;
	Node.Type = TEXT("Image");
	Node.Name = TEXT("TestImage");
	Node.Brush.MaterialRef = TEXT("");
	Node.Brush.Image.TexturePath = TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture");

	UImage* ImageWidget = NewObject<UImage>();

	const bool bBound = FWidgetResourceBinder::BindImageResource(ImageWidget, Node, nullptr);

	TestFalse(TEXT("Explicit empty MaterialRef blocks TexturePath fallback"), bBound);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPropertyApplierExplicitEmptyMaterialRefBlocksTextureFallback,
	"Design2UMG.PropertyApplier.TypedEmptyMaterialRefBlocksTextureFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPropertyApplierExplicitEmptyMaterialRefBlocksTextureFallback::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Node;
	Node.Brush.MaterialRef = FString();
	Node.Brush.Image.TexturePath = TEXT("/Engine/EngineMaterials/DefaultDiffuse");

	UImage* ImageWidget = NewObject<UImage>();
	FUMGWidgetTreeBuilder::ApplyWidgetProperties(ImageWidget, Node);
	TestTrue(TEXT("Image explicit empty MaterialRef blocks TexturePath fallback"),
		Design2UMGWidgetPropertyAccess::GetBrush(*ImageWidget).GetResourceObject() == nullptr);

	UBorder* BorderWidget = NewObject<UBorder>();
	FUMGWidgetTreeBuilder::ApplyWidgetProperties(BorderWidget, Node);
	TestTrue(TEXT("Border explicit empty MaterialRef blocks TexturePath fallback"),
		BorderWidget->Background.GetResourceObject() == nullptr);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPropertyApplierBorderBrushColorSurvivesBrushMutation,
	"Design2UMG.PropertyApplier.BorderBrushColorSurvivesBrushMutation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPropertyApplierBorderBrushColorSurvivesBrushMutation::RunTest(const FString& Parameters)
{
	const FLinearColor RawColor(0.1234f, 0.5678f, 0.9012f, 0.6543f);
	const FLinearColor ExpectedColor = FUMGValueParser::ParseColor(
		Design2UMGWidgetNodeAccess::SerializeWidgetColor(RawColor));

	FUMGWidgetNode Node;
	Node.Brush.BrushColor = RawColor;
	Node.Brush.DrawAs = ESlateBrushDrawType::Image;

	UBorder* BorderWidget = NewObject<UBorder>();
	FUMGWidgetTreeBuilder::ApplyWidgetProperties(BorderWidget, Node);

	TestTrue(TEXT("Border brush color survives final brush assignment"),
		Design2UMGWidgetPropertyAccess::GetBrushColor(*BorderWidget).Equals(ExpectedColor, 0.0001f));
	TestTrue(TEXT("Border brush receives a resource when color needs an image brush"),
		BorderWidget->Background.GetResourceObject() != nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPropertyApplierUsesSharedAxisAlignedCropSemantics,
	"Design2UMG.PropertyApplier.SharedAxisAlignedCropSemantics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPropertyApplierUsesSharedAxisAlignedCropSemantics::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Node;
	Node.Type = TEXT("Image");
	Node.Name = TEXT("AxisCropPropertyApply");
	Node.Brush.Image.TexturePath = TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture");
	Node.Brush.Image.Mode = TEXT("Crop");
	Node.Brush.Image.Transform = FUMGVisualBrushData::FImageTransformData{ 0.5f, 0.0f, 0.25f, 0.0f, 0.75f, 0.1f };
	UImage* ImageWidget = NewObject<UImage>();
	FUMGWidgetTreeBuilder::ApplyWidgetProperties(ImageWidget, Node);

	const FBox2f UVRegion = Design2UMGWidgetPropertyAccess::GetUVRegion(
		Design2UMGWidgetPropertyAccess::GetBrush(*ImageWidget));
	TestTrue(TEXT("Property applier axis crop preserves UV min X"), FMath::IsNearlyEqual(UVRegion.Min.X, 0.25f, 0.0001f));
	TestTrue(TEXT("Property applier axis crop preserves UV min Y"), FMath::IsNearlyEqual(UVRegion.Min.Y, 0.10f, 0.0001f));
	TestTrue(TEXT("Property applier axis crop preserves UV max X"), FMath::IsNearlyEqual(UVRegion.Max.X, 0.75f, 0.0001f));
	TestTrue(TEXT("Property applier axis crop preserves UV max Y"), FMath::IsNearlyEqual(UVRegion.Max.Y, 0.85f, 0.0001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPropertyApplierNormalizesTypedAxisAlignedCropTransform,
	"Design2UMG.PropertyApplier.NormalizesTypedAxisAlignedCropTransform",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPropertyApplierNormalizesTypedAxisAlignedCropTransform::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Node;
	Node.Type = TEXT("Image");
	Node.Name = TEXT("TypedAxisCropPropertyApply");
	Node.Brush.Image.TexturePath = TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture");
	Node.Brush.Image.Mode = TEXT("Crop");
	FUMGVisualBrushData::FImageTransformData Transform;
	Transform.A = 0.50000049f;
	Transform.B = 0.0f;
	Transform.TX = 0.25000049f;
	Transform.C = 0.0f;
	Transform.D = 0.75000049f;
	Transform.TY = 0.10000049f;
	Node.Brush.Image.Transform = Transform;
	UImage* ImageWidget = NewObject<UImage>();
	FUMGWidgetTreeBuilder::ApplyWidgetProperties(ImageWidget, Node);

	// Compute UV bounds directly from payload floats: (TX, TY) to (TX + A, TY + D).
	const FBox2f UVRegion = Design2UMGWidgetPropertyAccess::GetUVRegion(
		Design2UMGWidgetPropertyAccess::GetBrush(*ImageWidget));
	TestTrue(TEXT("Typed crop transform reaches brush min X at full precision"), FMath::IsNearlyEqual(UVRegion.Min.X, 0.2500004768371582f, 0.0000001f));
	TestTrue(TEXT("Typed crop transform reaches brush min Y at full precision"), FMath::IsNearlyEqual(UVRegion.Min.Y, 0.10000049322843552f, 0.0000001f));
	TestTrue(TEXT("Typed crop transform reaches brush max X at full precision"), FMath::IsNearlyEqual(UVRegion.Max.X, 0.75000095367431641f, 0.0000001f));
	TestTrue(TEXT("Typed crop transform reaches brush max Y at full precision"), FMath::IsNearlyEqual(UVRegion.Max.Y, 0.85000097751617432f, 0.0000001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPropertyApplierUsesSharedAffineCropSemantics,
	"Design2UMG.PropertyApplier.SharedAffineCropSemantics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPropertyApplierUsesSharedAffineCropSemantics::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Node;
	Node.Type = TEXT("Image");
	Node.Name = TEXT("AffineCropPropertyApply");
	Node.Brush.Image.TexturePath = TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture");
	Node.Brush.Image.Mode = TEXT("Crop");
	Node.Brush.Image.Transform = FUMGVisualBrushData::FImageTransformData{ 1.0f, 0.25f, 0.0f, 0.1f, 1.0f, 0.0f };
	Node.WidgetStyle.WidthOverride = 8.0f;
	Node.WidgetStyle.HeightOverride = 6.0f;

	UImage* ImageWidget = NewObject<UImage>();
	FUMGWidgetTreeBuilder::ApplyWidgetProperties(ImageWidget, Node);

	UTexture2D* BoundTexture = Cast<UTexture2D>(Design2UMGWidgetPropertyAccess::GetBrush(*ImageWidget).GetResourceObject());
	if (!TestNotNull(TEXT("Property applier affine crop produces a texture"), BoundTexture))
	{
		return false;
	}

	TestEqual(TEXT("Property applier affine crop output width"), BoundTexture->GetSizeX(), 8);
	TestEqual(TEXT("Property applier affine crop output height"), BoundTexture->GetSizeY(), 6);
	const auto BrushImageSize = Design2UMGWidgetPropertyAccess::GetBrush(*ImageWidget).ImageSize;
	TestEqual(TEXT("Property applier affine crop brush width"), BrushImageSize.X, static_cast<decltype(BrushImageSize.X)>(8.0));
	TestEqual(TEXT("Property applier affine crop brush height"), BrushImageSize.Y, static_cast<decltype(BrushImageSize.Y)>(6.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetResourceBinderPreservesAffineCropAfterBinding,
	"Design2UMG.WidgetResourceBinder.PreservesAffineCropAfterBinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FWidgetResourceBinderPreservesAffineCropAfterBinding::RunTest(const FString& Parameters)
{
	const FString TextureKey = TEXT("test://affine-crop-source");
	UTexture2D* SourceTexture = UTexture2D::CreateTransient(16, 16);
	if (!TestNotNull(TEXT("Source texture created"), SourceTexture))
	{
		return false;
	}

	FUMGWidgetNode Node;
	Node.Type = TEXT("Image");
	Node.Name = TEXT("AffineCropImage");
	Node.Brush.Image.TexturePath = TextureKey;
	Node.Brush.Image.Mode = TEXT("Crop");
	Node.Brush.Image.Transform = FUMGVisualBrushData::FImageTransformData{ 1.0f, 0.25f, 0.0f, 0.1f, 1.0f, 0.0f };
	Node.WidgetStyle.WidthOverride = 8.0f;
	Node.WidgetStyle.HeightOverride = 6.0f;

	UImage* ImageWidget = NewObject<UImage>();
	FUMGWidgetTreeBuilder::ApplyWidgetProperties(ImageWidget, Node);

	TMap<FString, UTexture2D*> TextureMap;
	TextureMap.Add(TextureKey, SourceTexture);
	const bool bBound = FWidgetResourceBinder::BindImageResource(ImageWidget, Node, &TextureMap);

	TestTrue(TEXT("Texture map binding succeeds"), bBound);
	UTexture2D* BoundTexture = Cast<UTexture2D>(Design2UMGWidgetPropertyAccess::GetBrush(*ImageWidget).GetResourceObject());
	if (!TestNotNull(TEXT("Bound brush texture exists"), BoundTexture))
	{
		return false;
	}

	TestTrue(TEXT("Binder keeps affine-cropped texture instead of original source"), BoundTexture != SourceTexture);
	TestEqual(TEXT("Affine crop output width"), BoundTexture->GetSizeX(), 8);
	TestEqual(TEXT("Affine crop output height"), BoundTexture->GetSizeY(), 6);
	const auto BrushImageSize = Design2UMGWidgetPropertyAccess::GetBrush(*ImageWidget).ImageSize;
	TestEqual(TEXT("Brush image size follows affine crop width"), BrushImageSize.X, static_cast<decltype(BrushImageSize.X)>(8.0));
	TestEqual(TEXT("Brush image size follows affine crop height"), BrushImageSize.Y, static_cast<decltype(BrushImageSize.Y)>(6.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetResourceBinderPreservesAxisAlignedCropAfterBinding,
	"Design2UMG.WidgetResourceBinder.PreservesAxisAlignedCropAfterBinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FWidgetResourceBinderPreservesAxisAlignedCropAfterBinding::RunTest(const FString& Parameters)
{
	const FString TextureKey = TEXT("test://axis-crop-source");
	UTexture2D* SourceTexture = UTexture2D::CreateTransient(32, 24);
	if (!TestNotNull(TEXT("Source texture created"), SourceTexture))
	{
		return false;
	}

	FUMGWidgetNode Node;
	Node.Type = TEXT("Image");
	Node.Name = TEXT("AxisCropImage");
	Node.Brush.Image.TexturePath = TextureKey;
	Node.Brush.Image.Mode = TEXT("Crop");
	Node.Brush.Image.Transform = FUMGVisualBrushData::FImageTransformData{ 0.5f, 0.0f, 0.25f, 0.0f, 0.75f, 0.1f };
	UImage* ImageWidget = NewObject<UImage>();
	TMap<FString, UTexture2D*> TextureMap;
	TextureMap.Add(TextureKey, SourceTexture);
	const bool bBound = FWidgetResourceBinder::BindImageResource(ImageWidget, Node, &TextureMap);

	TestTrue(TEXT("Texture map binding succeeds"), bBound);
	UTexture2D* BoundTexture = Cast<UTexture2D>(Design2UMGWidgetPropertyAccess::GetBrush(*ImageWidget).GetResourceObject());
	if (!TestNotNull(TEXT("Bound brush texture exists"), BoundTexture))
	{
		return false;
	}

	TestTrue(TEXT("Binder keeps source texture for axis-aligned crop"), BoundTexture == SourceTexture);
	const FBox2f UVRegion = Design2UMGWidgetPropertyAccess::GetUVRegion(
		Design2UMGWidgetPropertyAccess::GetBrush(*ImageWidget));
	TestTrue(TEXT("Axis crop preserves UV min X"), FMath::IsNearlyEqual(UVRegion.Min.X, 0.25f, 0.0001f));
	TestTrue(TEXT("Axis crop preserves UV min Y"), FMath::IsNearlyEqual(UVRegion.Min.Y, 0.10f, 0.0001f));
	TestTrue(TEXT("Axis crop preserves UV max X"), FMath::IsNearlyEqual(UVRegion.Max.X, 0.75f, 0.0001f));
	TestTrue(TEXT("Axis crop preserves UV max Y"), FMath::IsNearlyEqual(UVRegion.Max.Y, 0.85f, 0.0001f));
	return true;
}

#endif // WITH_AUTOMATION_WORKER
