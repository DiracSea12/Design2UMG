// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

// Box shadow fidelity: blur padding, explicit material params, and negative spread.

#include "Misc/AutomationTest.h"
#include "Design2UMGTestUtils.h"
#include "Design2UMGConverter.h"
#include "PenParser.h"
#include "ProceduralShapeGenerator.h"
#include "UMGPropertyKeys.h"
#include "Materials/Material.h"
#include "Components/Widget.h"
#include "Engine/Texture2D.h"
#include "UObject/UObjectGlobals.h"

#if WITH_AUTOMATION_WORKER

using namespace Design2UMGTestUtils;

using Design2UMGTestUtils::BuildProceduralRequestForTest;
using Design2UMGTestUtils::ReadTexturePixel;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxShadowBlurPaddingFidelityTest,
	"Design2UMG.Shadow.BoxShadowBlurPaddingFidelity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBoxShadowBlurPaddingFidelityTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"children": [
			{
				"id": "shadow_card",
				"type": "rectangle",
				"name": "Shadow Card",
				"width": 40,
				"height": 24,
				"clip": true,
				"fill": "#FFFFFFFF",
				"effect": {
					"type": "shadow",
					"shadowType": "outer",
					"color": "#000000FF",
					"offset": { "x": 0, "y": 0 },
					"blur": 8,
					"spread": 0
				}
			}
		]
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Box shadow blur-padding fixture parses"), FPenParser::ParseDocument(Json, Doc));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* Wrapper = FindWidgetById(Data.RootWidget, TEXT("shadow_card"));
	const FUMGWidgetNode* ShadowWidget = FindWidgetById(Data.RootWidget, TEXT("shadow_card_shadow0"));
	TestTrue(TEXT("Shadow wrapper exists"), Wrapper != nullptr);
	TestTrue(TEXT("Shadow image child exists"), ShadowWidget != nullptr);
	if (!Wrapper || !ShadowWidget)
	{
		return false;
	}

	TestEqual(TEXT("Box shadow keeps the content overlay wrapper"), Wrapper->Type, TEXT("Overlay"));
	TestEqual(TEXT("Box shadow wrapper owns clipping"), Wrapper->Render.Clipping.Get(FString()), TEXT("ClipToBounds"));
	TestEqual(TEXT("Box shadow material ref preserved"), GetWidgetProperty(*ShadowWidget, UMGPropertyKeys::MaterialRef), TEXT("boxshadow"));
	TestTrue(TEXT("Box shadow remains a fill child"),
		ShadowWidget->Slot.HorizontalAlignment == EUMGSlotHorizontalAlignment::Fill
		&& ShadowWidget->Slot.VerticalAlignment == EUMGSlotVerticalAlignment::Fill);
	// The shape keeps the source size (spread is 0) and a 1.5*blur 3-sigma
	// falloff margin is added per side: 40 + 24 = 64 wide, 24 + 24 = 48 tall.
	TestTrue(TEXT("Box shadow width includes blur padding"),
		FMath::IsNearlyEqual(FCString::Atof(*GetWidgetProperty(*ShadowWidget, TEXT("WidthOverride"))), 64.0f, 0.001f));
	TestTrue(TEXT("Box shadow height includes blur padding"),
		FMath::IsNearlyEqual(FCString::Atof(*GetWidgetProperty(*ShadowWidget, TEXT("HeightOverride"))), 48.0f, 0.001f));

	UTexture2D* Texture = FProceduralShapeGenerator::GenerateFromMaterialRef(BuildProceduralRequestForTest(*this, *ShadowWidget),
		GetTransientPackageAsObject());
	TestTrue(TEXT("Box shadow texture generated"), Texture != nullptr);
	if (!Texture)
	{
		return false;
	}

	FColor CornerPixel;
	FColor CenterPixel;
	TestTrue(TEXT("Box shadow corner pixel readable"), ReadTexturePixel(Texture, 0, 0, CornerPixel));
	TestTrue(TEXT("Box shadow center pixel readable"), ReadTexturePixel(Texture, 32, 24, CenterPixel));

	TestTrue(TEXT("Blur padding corner stays substantially softer than the core"), CornerPixel.A * 4 < CenterPixel.A);
	TestTrue(TEXT("Shadow core stays opaque"), CenterPixel.A > 200);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxShadowExplicitBlurPaddingMaterialParamTest,
	"Design2UMG.Shadow.ExplicitBlurPaddingMaterialParam",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBoxShadowExplicitBlurPaddingMaterialParamTest::RunTest(const FString& Parameters)
{
	FUMGProceduralResourceRequest DefaultPaddingRequest;
	DefaultPaddingRequest.MaterialRef = TEXT("boxshadow");
	DefaultPaddingRequest.WidthOverride = 56.0f;
	DefaultPaddingRequest.HeightOverride = 40.0f;
	DefaultPaddingRequest.ShadowBlur = 8.0f;
	DefaultPaddingRequest.MaterialParams = TEXT("{\"Color\":\"#000000FF\",\"CornerRadius\":0.0}");

	FUMGProceduralResourceRequest ExplicitPaddingShadowRequest = DefaultPaddingRequest;
	ExplicitPaddingShadowRequest.MaterialParams = TEXT("{\"Color\":\"#000000FF\",\"CornerRadius\":0.0,\"BlurPadding\":8.0}");

	UTexture2D* DefaultPaddingTexture = FProceduralShapeGenerator::GenerateFromMaterialRef(
		DefaultPaddingRequest,
		GetTransientPackageAsObject());
	UTexture2D* ExplicitPaddingTexture = FProceduralShapeGenerator::GenerateFromMaterialRef(
		ExplicitPaddingShadowRequest,
		GetTransientPackageAsObject());
	TestTrue(TEXT("Default-padding box shadow texture generated"), DefaultPaddingTexture != nullptr);
	TestTrue(TEXT("Explicit blur-padding texture generated"), ExplicitPaddingTexture != nullptr);
	if (!DefaultPaddingTexture || !ExplicitPaddingTexture)
	{
		return false;
	}

	FColor DefaultPaddingCornerPixel;
	FColor ExplicitCornerPixel;
	FColor ExplicitCenterPixel;
	TestTrue(TEXT("Default-padding corner pixel readable"), ReadTexturePixel(DefaultPaddingTexture, 0, 0, DefaultPaddingCornerPixel));
	TestTrue(TEXT("Explicit blur-padding corner pixel readable"), ReadTexturePixel(ExplicitPaddingTexture, 0, 0, ExplicitCornerPixel));
	TestTrue(TEXT("Explicit blur-padding center pixel readable"), ReadTexturePixel(ExplicitPaddingTexture, 28, 20, ExplicitCenterPixel));

	TestTrue(TEXT("Explicit blur-padding softens the padded corner"), ExplicitCornerPixel.A < DefaultPaddingCornerPixel.A);
	TestTrue(TEXT("Explicit blur-padding keeps the shadow core visible"), ExplicitCenterPixel.A > 150);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxShadowNegativeSpreadFidelityTest,
	"Design2UMG.Shadow.NegativeSpreadFidelity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBoxShadowNegativeSpreadFidelityTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"children": [
			{
				"id": "shadow_chip",
				"type": "rectangle",
				"name": "Shadow Chip",
				"width": 40,
				"height": 40,
				"clip": true,
				"fill": "#FFFFFFFF",
				"effect": {
					"type": "shadow",
					"shadowType": "outer",
					"color": "#000000FF",
					"offset": { "x": 0, "y": 0 },
					"blur": 0,
					"spread": -6
				}
			}
		]
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Negative spread shadow fixture parses"), FPenParser::ParseDocument(Json, Doc));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* Wrapper = FindWidgetById(Data.RootWidget, TEXT("shadow_chip"));
	const FUMGWidgetNode* ShadowWidget = FindWidgetById(Data.RootWidget, TEXT("shadow_chip_shadow0"));
	TestTrue(TEXT("Negative spread shadow wrapper exists"), Wrapper != nullptr);
	TestTrue(TEXT("Negative spread shadow image child exists"), ShadowWidget != nullptr);
	if (!Wrapper || !ShadowWidget)
	{
		return false;
	}

	TestEqual(TEXT("Negative spread keeps the layout overlay wrapper"), Wrapper->Type, TEXT("Overlay"));
	TestEqual(TEXT("Negative spread wrapper owns clipping"), Wrapper->Render.Clipping.Get(FString()), TEXT("ClipToBounds"));
	TestEqual(TEXT("Negative spread keeps rounded rect path when blur is zero"), GetWidgetProperty(*ShadowWidget, UMGPropertyKeys::MaterialRef), TEXT("roundedrect"));
	TestTrue(TEXT("Negative spread shrinks width"),
		FMath::IsNearlyEqual(FCString::Atof(*GetWidgetProperty(*ShadowWidget, TEXT("WidthOverride"))), 28.0f, 0.001f));
	TestTrue(TEXT("Negative spread shrinks height"),
		FMath::IsNearlyEqual(FCString::Atof(*GetWidgetProperty(*ShadowWidget, TEXT("HeightOverride"))), 28.0f, 0.001f));
	TestTrue(TEXT("Negative spread keeps its source translation"),
		ShadowWidget->Render.RenderTranslation.IsSet()
		&& ShadowWidget->Render.RenderTranslation.GetValue().Equals(FVector2D(0.0f, 0.0f), 0.001f));

	UTexture2D* Texture = FProceduralShapeGenerator::GenerateFromMaterialRef(BuildProceduralRequestForTest(*this, *ShadowWidget),
		GetTransientPackageAsObject());
	TestTrue(TEXT("Negative spread texture generated"), Texture != nullptr);
	if (!Texture)
	{
		return false;
	}

	TestEqual(TEXT("Negative spread opaque pixel area matches shrunken rect"), CountOpaqueTexturePixels(Texture), 28 * 28);
	return true;
}

#endif
