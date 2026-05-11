// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"
#include "WidgetResourceBinder.h"
#include "UMGSchema.h"
#include "UMGPropertyKeys.h"
#include "UMGWidgetUtils.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"

#if WITH_AUTOMATION_WORKER

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
	const FSlateBrush Brush = ImageWidget->GetBrush();
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
	"Design2UMG.PropertyApplier.ExplicitEmptyMaterialRefBlocksTextureFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPropertyApplierExplicitEmptyMaterialRefBlocksTextureFallback::RunTest(const FString& Parameters)
{
	FUMGPropertyBag Properties;
	Properties.Add(UMGPropertyKeys::MaterialRef, FString());
	Properties.Add(UMGPropertyKeys::TexturePath, TEXT("/Engine/EngineMaterials/DefaultDiffuse"));

	UImage* ImageWidget = NewObject<UImage>();
	FUMGWidgetUtils::ApplyWidgetProperties(ImageWidget, Properties);
	TestTrue(TEXT("Image explicit empty MaterialRef blocks TexturePath fallback"),
		ImageWidget->GetBrush().GetResourceObject() == nullptr);

	UBorder* BorderWidget = NewObject<UBorder>();
	FUMGWidgetUtils::ApplyWidgetProperties(BorderWidget, Properties);
	TestTrue(TEXT("Border explicit empty MaterialRef blocks TexturePath fallback"),
		BorderWidget->Background.GetResourceObject() == nullptr);

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
	Node.WidgetStyle.ImageMode = TEXT("Crop");
	Node.WidgetStyle.ImageTransform = TEXT("0.5,0,0.25,0,0.75,0.1");

	UImage* ImageWidget = NewObject<UImage>();
	FUMGWidgetUtils::ApplyWidgetProperties(ImageWidget, Node);

	const FBox2f UVRegion = ImageWidget->GetBrush().GetUVRegion();
	TestTrue(TEXT("Property applier axis crop preserves UV min X"), FMath::IsNearlyEqual(UVRegion.Min.X, 0.25f, 0.0001f));
	TestTrue(TEXT("Property applier axis crop preserves UV min Y"), FMath::IsNearlyEqual(UVRegion.Min.Y, 0.10f, 0.0001f));
	TestTrue(TEXT("Property applier axis crop preserves UV max X"), FMath::IsNearlyEqual(UVRegion.Max.X, 0.75f, 0.0001f));
	TestTrue(TEXT("Property applier axis crop preserves UV max Y"), FMath::IsNearlyEqual(UVRegion.Max.Y, 0.85f, 0.0001f));
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
	Node.WidgetStyle.ImageMode = TEXT("Crop");
	Node.WidgetStyle.ImageTransform = TEXT("1,0.25,0,0.10,1,0");
	Node.WidgetStyle.WidthOverride = 8.0f;
	Node.WidgetStyle.HeightOverride = 6.0f;

	UImage* ImageWidget = NewObject<UImage>();
	FUMGWidgetUtils::ApplyWidgetProperties(ImageWidget, Node);

	UTexture2D* BoundTexture = Cast<UTexture2D>(ImageWidget->GetBrush().GetResourceObject());
	if (!TestNotNull(TEXT("Property applier affine crop produces a texture"), BoundTexture))
	{
		return false;
	}

	TestEqual(TEXT("Property applier affine crop output width"), BoundTexture->GetSizeX(), 8);
	TestEqual(TEXT("Property applier affine crop output height"), BoundTexture->GetSizeY(), 6);
	TestEqual(TEXT("Property applier affine crop brush width"), ImageWidget->GetBrush().ImageSize.X, 8.0f);
	TestEqual(TEXT("Property applier affine crop brush height"), ImageWidget->GetBrush().ImageSize.Y, 6.0f);
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
	Node.WidgetStyle.ImageMode = TEXT("Crop");
	Node.WidgetStyle.ImageTransform = TEXT("1,0.25,0,0.10,1,0");
	Node.WidgetStyle.WidthOverride = 8.0f;
	Node.WidgetStyle.HeightOverride = 6.0f;

	UImage* ImageWidget = NewObject<UImage>();
	FUMGWidgetUtils::ApplyWidgetProperties(ImageWidget, Node);

	TMap<FString, UTexture2D*> TextureMap;
	TextureMap.Add(TextureKey, SourceTexture);
	const bool bBound = FWidgetResourceBinder::BindImageResource(ImageWidget, Node, &TextureMap);

	TestTrue(TEXT("Texture map binding succeeds"), bBound);
	UTexture2D* BoundTexture = Cast<UTexture2D>(ImageWidget->GetBrush().GetResourceObject());
	if (!TestNotNull(TEXT("Bound brush texture exists"), BoundTexture))
	{
		return false;
	}

	TestTrue(TEXT("Binder keeps affine-cropped texture instead of original source"), BoundTexture != SourceTexture);
	TestEqual(TEXT("Affine crop output width"), BoundTexture->GetSizeX(), 8);
	TestEqual(TEXT("Affine crop output height"), BoundTexture->GetSizeY(), 6);
	TestEqual(TEXT("Brush image size follows affine crop width"), ImageWidget->GetBrush().ImageSize.X, 8.0f);
	TestEqual(TEXT("Brush image size follows affine crop height"), ImageWidget->GetBrush().ImageSize.Y, 6.0f);
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
	Node.WidgetStyle.ImageMode = TEXT("Crop");
	Node.WidgetStyle.ImageTransform = TEXT("0.5,0,0.25,0,0.75,0.1");

	UImage* ImageWidget = NewObject<UImage>();
	TMap<FString, UTexture2D*> TextureMap;
	TextureMap.Add(TextureKey, SourceTexture);
	const bool bBound = FWidgetResourceBinder::BindImageResource(ImageWidget, Node, &TextureMap);

	TestTrue(TEXT("Texture map binding succeeds"), bBound);
	UTexture2D* BoundTexture = Cast<UTexture2D>(ImageWidget->GetBrush().GetResourceObject());
	if (!TestNotNull(TEXT("Bound brush texture exists"), BoundTexture))
	{
		return false;
	}

	TestTrue(TEXT("Binder keeps source texture for axis-aligned crop"), BoundTexture == SourceTexture);
	const FBox2f UVRegion = ImageWidget->GetBrush().GetUVRegion();
	TestTrue(TEXT("Axis crop preserves UV min X"), FMath::IsNearlyEqual(UVRegion.Min.X, 0.25f, 0.0001f));
	TestTrue(TEXT("Axis crop preserves UV min Y"), FMath::IsNearlyEqual(UVRegion.Min.Y, 0.10f, 0.0001f));
	TestTrue(TEXT("Axis crop preserves UV max X"), FMath::IsNearlyEqual(UVRegion.Max.X, 0.75f, 0.0001f));
	TestTrue(TEXT("Axis crop preserves UV max Y"), FMath::IsNearlyEqual(UVRegion.Max.Y, 0.85f, 0.0001f));
	return true;
}

#endif // WITH_AUTOMATION_WORKER
