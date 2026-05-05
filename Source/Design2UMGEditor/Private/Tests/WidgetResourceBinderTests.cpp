// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"
#include "WidgetResourceBinder.h"
#include "UMGSchema.h"
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
	Node.Brush.bHasMaterialRef = true;
	Node.Brush.TexturePath = TEXT("/Engine/EngineMaterials/DefaultDiffuse");

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
	Node.Brush.bHasMaterialRef = true;

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
	Node.Brush.bHasMaterialRef = true;
	Node.Brush.TexturePath = TEXT("/Engine/EngineMaterials/DefaultDiffuse");

	UImage* ImageWidget = NewObject<UImage>();

	const bool bBound = FWidgetResourceBinder::BindImageResource(ImageWidget, Node, nullptr);

	// MaterialRef takes priority: when roundedrect succeeds, it should return true without using the texture path.
	TestTrue(TEXT("MaterialRef takes priority over TexturePath"), bBound);

	// Verify the brush comes from a procedural texture with a nonzero ImageSize.
	const FSlateBrush Brush = ImageWidget->GetBrush();
	TestTrue(TEXT("Brush ImageSize set by material path"), Brush.ImageSize.X > 0.0f && Brush.ImageSize.Y > 0.0f);

	return true;
}

#endif // WITH_AUTOMATION_WORKER
