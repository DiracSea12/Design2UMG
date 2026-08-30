// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

// Effect wrappers: blur / inner shadow / drop shadow / geometry mask conversion, retainer material application, and effect JSON round trips.

#include "Misc/AutomationTest.h"
#include "Design2UMGTestUtils.h"
#include "Design2UMGConverter.h"
#include "PenParser.h"
#include "FigmaParser.h"
#include "UMGJsonParser.h"
#include "UMGJsonSerializer.h"
#include "UMGValueParsing.h"
#include "UMGWidgetNodeAccess.h"
#include "UMGWidgetUtils.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Serialization/JsonSerializer.h"
#include "RHI.h"
#include "Components/RetainerBox.h"
#include "Components/Widget.h"
#include "UObject/UObjectGlobals.h"

#if WITH_AUTOMATION_WORKER

using namespace Design2UMGTestUtils;


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FForegroundBlurRetainerWrapperTest,
	"Design2UMG.Converter.ForegroundBlurRetainerWrapper",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FForegroundBlurRetainerWrapperTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"children": [
			{
				"id": "blur_card",
				"type": "frame",
				"name": "Blur Card",
				"width": 240,
				"height": 80,
				"effect": {
					"type": "blur",
					"radius": 12
				},
				"children": [
					{
						"id": "title",
						"type": "text",
						"content": "Blurred",
						"fontSize": 24
					}
				]
			}
		]
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Foreground blur fixture parses"), FPenParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() != 1)
	{
		return false;
	}

	TestTrue(TEXT("Foreground blur effect parsed into IR"),
		Doc.Children[0]->Visual.Effects.Num() == 1 && Doc.Children[0]->Visual.Effects[0].Type == EDesignEffectType::Blur);
	TestTrue(TEXT("Foreground blur radius preserved"),
		Doc.Children[0]->Visual.Effects.Num() == 1 && FMath::IsNearlyEqual(Doc.Children[0]->Visual.Effects[0].Radius, 12.0f));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	TestEqual(TEXT("Foreground blur uses RetainerBox wrapper"), Data.RootWidget.Type, TEXT("RetainerBox"));
	TestTrue(TEXT("Foreground blur retain rendering typed"), Data.RootWidget.Effect.bRetainRendering.IsSet() && Data.RootWidget.Effect.bRetainRendering.GetValue());
	TestEqual(TEXT("Foreground blur effect material ref exported"), Data.RootWidget.Effect.EffectMaterial.GetValue(), TEXT("d2u.retainer.foreground_blur"));
	TestEqual(TEXT("Foreground blur texture parameter exported"), Data.RootWidget.Effect.TextureParameter.GetValue(), TEXT("Texture"));

	TestTrue(TEXT("Foreground blur UV step is typed"), Data.RootWidget.Effect.EffectBlurUVStep.IsSet());
	const FVector2D UVStep = Data.RootWidget.Effect.EffectBlurUVStep.GetValue();
	TestTrue(TEXT("Foreground blur UV step X positive"), UVStep.X > 0.0f);
	TestTrue(TEXT("Foreground blur UV step Y positive"), UVStep.Y > 0.0f);
	TestTrue(TEXT("Foreground blur wrapper keeps child"), Data.RootWidget.Children.Num() == 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInnerShadowRetainerWrapperTest,
	"Design2UMG.Shadow.InnerShadowRetainerWrapper",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FInnerShadowRetainerWrapperTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"children": [
			{
				"id": "inner_shadow_card",
				"type": "frame",
				"name": "Inner Shadow Card",
				"width": 200,
				"height": 80,
				"fill": "#FFFFFFFF",
				"effect": {
					"type": "shadow",
					"shadowType": "inner",
					"color": "#00000066",
					"offset": { "x": 4, "y": 3 },
					"blur": 8
				},
				"children": [
					{
						"id": "title",
						"type": "text",
						"content": "Inset",
						"fontSize": 24
					}
				]
			}
		]
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Inner shadow fixture parses"), FPenParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() != 1)
	{
		return false;
	}

	TestTrue(TEXT("Inner shadow parsed into IR"),
		Doc.Children[0]->Visual.Effects.Num() == 1
		&& Doc.Children[0]->Visual.Effects[0].Type == EDesignEffectType::Shadow
		&& Doc.Children[0]->Visual.Effects[0].Shadow.bInner);

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	TestEqual(TEXT("Inner shadow uses RetainerBox wrapper"), Data.RootWidget.Type, TEXT("RetainerBox"));
	TestTrue(TEXT("Inner shadow retain rendering typed"), Data.RootWidget.Effect.bRetainRendering.IsSet() && Data.RootWidget.Effect.bRetainRendering.GetValue());
	TestEqual(TEXT("Inner shadow effect material ref exported"), Data.RootWidget.Effect.EffectMaterial.GetValue(), TEXT("d2u.retainer.inner_shadow"));
	TestEqual(TEXT("Inner shadow texture parameter exported"), Data.RootWidget.Effect.TextureParameter.GetValue(), TEXT("Texture"));
	TestTrue(TEXT("Inner shadow color exported via typed effect"), Data.RootWidget.Effect.EffectColor.IsSet());
	TestTrue(TEXT("Inner shadow offset exported via typed effect"), Data.RootWidget.Effect.EffectOffset.IsSet());
	TestTrue(TEXT("Inner shadow blur step exported via typed effect"), Data.RootWidget.Effect.EffectBlurUVStep.IsSet());
	TestTrue(TEXT("Inner shadow normalized offset X preserved"), FMath::IsNearlyEqual(Data.RootWidget.Effect.EffectOffset.GetValue().X, 0.02f, 0.0001f));
	TestTrue(TEXT("Inner shadow normalized offset Y preserved"), FMath::IsNearlyEqual(Data.RootWidget.Effect.EffectOffset.GetValue().Y, 0.0375f, 0.0001f));
	TestTrue(TEXT("Inner shadow color alpha preserved"), FMath::IsNearlyEqual(Data.RootWidget.Effect.EffectColor.GetValue().A, 0.4f, 0.01f));
	TestTrue(TEXT("Inner shadow wrapper keeps child"), Data.RootWidget.Children.Num() == 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBackgroundBlurUsesTypedEffectDataTest,
	"Design2UMG.Converter.BackgroundBlurUsesTypedEffectData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBackgroundBlurUsesTypedEffectDataTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"children": [
			{
				"id": "bg_blur_card",
				"type": "frame",
				"name": "Blur Layer",
				"width": 240,
				"height": 80,
				"effect": {
					"type": "background_blur",
					"radius": 18
				},
				"children": [
					{
						"id": "title",
						"type": "text",
						"content": "Backdrop",
						"fontSize": 24
					}
				]
			}
		]
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Background blur fixture parses"), FPenParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() != 1)
	{
		return false;
	}

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	TestEqual(TEXT("Background blur uses BackgroundBlur wrapper"), Data.RootWidget.Type, TEXT("BackgroundBlur"));
	TestTrue(TEXT("Background blur strength exported via typed effect"), Data.RootWidget.Effect.BlurStrength.IsSet());
	TestTrue(TEXT("Background blur strength preserved"), FMath::IsNearlyEqual(Data.RootWidget.Effect.BlurStrength.GetValue(), 18.0f));
	TestTrue(TEXT("Background blur wrapper keeps child"), Data.RootWidget.Children.Num() == 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaEffectsConvertToTypedWrappersTest,
	"Design2UMG.FigmaParser.EffectsConvertToTypedWrappers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaEffectsConvertToTypedWrappersTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"document": {
			"id": "0:0",
			"name": "Doc",
			"type": "DOCUMENT",
			"children": [{
				"id": "0:1",
				"name": "Page",
				"type": "CANVAS",
				"children": [{
					"id": "1:1",
					"name": "EffectCard",
					"type": "FRAME",
					"visible": true,
					"absoluteBoundingBox": {"x":0,"y":0,"width":180,"height":96},
					"absoluteRenderBounds": {"x":0,"y":0,"width":180,"height":96},
					"effects": [
						{
							"type": "INNER_SHADOW",
							"visible": true,
							"blendMode": "NORMAL",
							"offset": {"x":4,"y":-2},
							"radius": 12,
							"color": {"r":0,"g":0,"b":0,"a":0.3}
						},
						{
							"type": "BACKGROUND_BLUR",
							"visible": true,
							"radius": 16
						}
					],
					"children": [{
						"id": "1:2",
						"name": "Title",
						"type": "TEXT",
						"visible": true,
						"absoluteBoundingBox": {"x":16,"y":20,"width":80,"height":24},
						"characters": "Fx",
						"style": {"fontFamily":"Inter","fontSize":20,"fontWeight":500,"textAlignHorizontal":"LEFT","textAlignVertical":"TOP","textDecoration":"NONE","letterSpacing":0,"lineHeightUnit":"AUTO","lineHeightPx":0}
					}]
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Figma effect wrapper fixture parses"), FFigmaParser::ParseDocument(Json, Doc));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	TestEqual(TEXT("Figma background blur becomes outer wrapper"), Data.RootWidget.Type, TEXT("BackgroundBlur"));
	TestTrue(TEXT("Figma background blur keeps typed strength"), Data.RootWidget.Effect.BlurStrength.IsSet() && FMath::IsNearlyEqual(Data.RootWidget.Effect.BlurStrength.GetValue(), 16.0f, 0.001f));
	TestTrue(TEXT("Figma background blur keeps one child"), Data.RootWidget.Children.Num() == 1);
	if (Data.RootWidget.Children.Num() != 1)
	{
		return false;
	}

	const FUMGWidgetNode& InnerShadowWrapper = Data.RootWidget.Children[0];
	TestEqual(TEXT("Figma inner shadow becomes retainer child"), InnerShadowWrapper.Type, TEXT("RetainerBox"));
	TestTrue(TEXT("Figma inner shadow retain rendering typed"), InnerShadowWrapper.Effect.bRetainRendering.IsSet() && InnerShadowWrapper.Effect.bRetainRendering.GetValue());
	TestEqual(TEXT("Figma inner shadow effect material typed"), InnerShadowWrapper.Effect.EffectMaterial.GetValue(), TEXT("d2u.retainer.inner_shadow"));
	TestTrue(TEXT("Figma inner shadow offset typed"), InnerShadowWrapper.Effect.EffectOffset.IsSet());
	TestTrue(TEXT("Figma inner shadow blur typed"), InnerShadowWrapper.Effect.EffectBlurUVStep.IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaGeometryMaskConvertsBeforeInnerShadowTest,
	"Design2UMG.FigmaParser.GeometryMaskConvertsBeforeInnerShadow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaGeometryMaskConvertsBeforeInnerShadowTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"document": {
			"type": "DOCUMENT",
			"children": [{
				"type": "CANVAS",
				"children": [{
					"id": "shape-group",
					"name": "Shape Group",
					"type": "GROUP",
					"absoluteBoundingBox": {"x":0,"y":0,"width":100,"height":80},
					"effects": [{
						"type": "INNER_SHADOW",
						"visible": true,
						"offset": {"x":2,"y":2},
						"radius": 4,
						"color": {"r":0,"g":0,"b":0,"a":0.25}
					}],
					"children": [{
						"id": "shape-mask",
						"name": "Clip Shape",
						"type": "ELLIPSE",
						"isMask": true,
						"absoluteBoundingBox": {"x":0,"y":0,"width":100,"height":80}
					}, {
						"id": "shape-content",
						"name": "Color Layer",
						"type": "RECTANGLE",
						"absoluteBoundingBox": {"x":0,"y":0,"width":100,"height":80},
						"fills": [{"type":"SOLID","color":{"r":0.2,"g":0.4,"b":0.8,"a":1}}]
					}]
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Geometry mask fixture parses"), FFigmaParser::ParseDocument(Json, Doc));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	TestEqual(TEXT("Inner shadow remains the outer effect"), Data.RootWidget.Effect.EffectMaterial.GetValue(), TEXT("d2u.retainer.inner_shadow"));
	TestEqual(TEXT("Inner shadow keeps one masked child"), Data.RootWidget.Children.Num(), 1);
	if (Data.RootWidget.Children.Num() != 1)
	{
		return false;
	}

	const FUMGWidgetNode& MaskWrapper = Data.RootWidget.Children[0];
	TestEqual(TEXT("Geometry mask uses RetainerBox"), MaskWrapper.Type, TEXT("RetainerBox"));
	TestEqual(TEXT("Geometry mask material exported"), MaskWrapper.Effect.EffectMaterial.GetValue(), TEXT("d2u.retainer.geometry_mask"));
	TestTrue(TEXT("Ellipse mask shape is typed"),
		MaskWrapper.Effect.GeometryMaskShape.IsSet()
		&& MaskWrapper.Effect.GeometryMaskShape.GetValue() == EUMGGeometryMaskShape::Ellipse);
	TestTrue(TEXT("Geometry mask size is typed"),
		MaskWrapper.Effect.GeometryMaskSize.IsSet()
		&& MaskWrapper.Effect.GeometryMaskSize.GetValue().Equals(FVector2D(100.0f, 80.0f), 0.001f));
	TestEqual(TEXT("Geometry mask keeps converted content"), MaskWrapper.Children.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaLayerBlurConvertsToForegroundBlurWrapperTest,
	"Design2UMG.FigmaParser.LayerBlurConvertsToForegroundBlurWrapper",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaLayerBlurConvertsToForegroundBlurWrapperTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"document": {
			"id": "0:0",
			"name": "Doc",
			"type": "DOCUMENT",
			"children": [{
				"id": "0:1",
				"name": "Page",
				"type": "CANVAS",
				"children": [{
					"id": "1:1",
					"name": "BlurCard",
					"type": "FRAME",
					"visible": true,
					"absoluteBoundingBox": {"x":0,"y":0,"width":160,"height":80},
					"absoluteRenderBounds": {"x":0,"y":0,"width":160,"height":80},
					"effects": [{
						"type": "LAYER_BLUR",
						"visible": true,
						"radius": 14
					}],
					"children": []
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Figma layer blur fixture parses"), FFigmaParser::ParseDocument(Json, Doc));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	TestEqual(TEXT("Figma layer blur becomes foreground retainer wrapper"), Data.RootWidget.Type, TEXT("RetainerBox"));
	TestTrue(TEXT("Figma layer blur retain rendering typed"), Data.RootWidget.Effect.bRetainRendering.IsSet() && Data.RootWidget.Effect.bRetainRendering.GetValue());
	TestEqual(TEXT("Figma layer blur effect material typed"), Data.RootWidget.Effect.EffectMaterial.GetValue(), TEXT("d2u.retainer.foreground_blur"));
	TestTrue(TEXT("Figma layer blur UV step typed"), Data.RootWidget.Effect.EffectBlurUVStep.IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaDropShadowConvertsToBoxShadowWrapperTest,
	"Design2UMG.FigmaParser.DropShadowConvertsToBoxShadowWrapper",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaDropShadowConvertsToBoxShadowWrapperTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"document": {
			"id": "0:0",
			"name": "Doc",
			"type": "DOCUMENT",
			"children": [{
				"id": "0:1",
				"name": "Page",
				"type": "CANVAS",
				"children": [{
					"id": "1:1",
					"name": "ShadowCard",
					"type": "RECTANGLE",
					"visible": true,
					"clipsContent": true,
					"absoluteBoundingBox": {"x":0,"y":0,"width":160,"height":80},
					"absoluteRenderBounds": {"x":0,"y":0,"width":160,"height":80},
					"fills": [{"type":"SOLID","visible":true,"color":{"r":1,"g":1,"b":1,"a":1}}],
					"effects": [{
						"type": "DROP_SHADOW",
						"visible": true,
						"blendMode": "NORMAL",
						"offset": {"x":6,"y":4},
						"radius": 10,
						"spread": 2,
						"color": {"r":0,"g":0,"b":0,"a":0.2}
					}],
					"children": []
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Figma drop shadow fixture parses"), FFigmaParser::ParseDocument(Json, Doc));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	TestEqual(TEXT("Figma outer shadow keeps its overlay wrapper"), Data.RootWidget.Type, TEXT("Overlay"));
	TestTrue(TEXT("Figma outer shadow wrapper keeps shadow and content children"), Data.RootWidget.Children.Num() == 2);
	if (Data.RootWidget.Children.Num() != 2)
	{
		return false;
	}

	const FUMGWidgetNode& Shadow = Data.RootWidget.Children[0];
	TestEqual(TEXT("Figma outer shadow synthetic shadow node is image"), Shadow.Type, TEXT("Image"));
	TestTrue(TEXT("Figma outer shadow synthetic blur typed"), Shadow.Shadow.ShadowBlur.IsSet() && FMath::IsNearlyEqual(Shadow.Shadow.ShadowBlur.GetValue(), 10.0f, 0.001f));
	TestTrue(TEXT("Figma outer shadow keeps its source brush colour"),
		Shadow.Brush.BrushColor.IsSet()
		&& Shadow.Brush.BrushColor.GetValue().Equals(FLinearColor(0.0f, 0.0f, 0.0f, 0.2f)));
	TestTrue(TEXT("Figma outer shadow remains a fill child with its source offset"),
		Shadow.Slot.HorizontalAlignment == EUMGSlotHorizontalAlignment::Fill
		&& Shadow.Slot.VerticalAlignment == EUMGSlotVerticalAlignment::Fill
		&& Shadow.Render.RenderTranslation.IsSet()
		&& Shadow.Render.RenderTranslation.GetValue().Equals(FVector2D(-6.0f, -8.0f), 0.001f));
	TestEqual(TEXT("Figma outer shadow wrapper owns source clipping"),
		Data.RootWidget.Render.Clipping.Get(FString()), TEXT("ClipToBounds"));
	TestTrue(TEXT("Figma outer shadow content remains a fill child"),
		FMath::IsNearlyEqual(Data.RootWidget.Children[1].Bounds.X, 0.0f, 0.001f)
		&& FMath::IsNearlyEqual(Data.RootWidget.Children[1].Bounds.Y, 0.0f, 0.001f)
		&& FMath::IsNearlyEqual(Data.RootWidget.Children[1].Bounds.W, 0.0f, 0.001f)
		&& FMath::IsNearlyEqual(Data.RootWidget.Children[1].Bounds.H, 0.0f, 0.001f)
		&& Data.RootWidget.Children[1].Slot.HorizontalAlignment == EUMGSlotHorizontalAlignment::Fill
		&& Data.RootWidget.Children[1].Slot.VerticalAlignment == EUMGSlotVerticalAlignment::Fill);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRetainerEffectMaterialApplicationTest,
	"Design2UMG.PropertyApplier.RetainerEffectMaterialForegroundBlur",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRetainerEffectMaterialApplicationTest::RunTest(const FString& Parameters)
{
	URetainerBox* Widget = NewObject<URetainerBox>(GetTransientPackageAsObject());
	TestTrue(TEXT("RetainerBox created"), Widget != nullptr);
	if (!Widget)
	{
		return false;
	}

	FUMGWidgetNode Node;
	Node.Type = TEXT("RetainerBox");
	Node.Effect.bRetainRendering = true;
	Node.Effect.EffectMaterial = TEXT("d2u.retainer.foreground_blur");
	Node.Effect.TextureParameter = TEXT("Texture");
	Node.Effect.EffectBlurUVStep = FVector2D(0.01f, 0.02f);
	FUMGWidgetUtils::ApplyWidgetProperties(Widget, Node);

	UMaterialInstanceDynamic* DynamicEffect = Widget->GetEffectMaterial();
	TestTrue(TEXT("RetainerBox dynamic effect material created"), DynamicEffect != nullptr);
	if (!DynamicEffect)
	{
		return false;
	}

	TestEqual(TEXT("RetainerBox texture parameter kept"), Widget->GetTextureParameter(), FName(TEXT("Texture")));
	const FLinearColor BlurUVStep = DynamicEffect->K2_GetVectorParameterValue(TEXT("BlurUVStep"));
	TestTrue(TEXT("RetainerBox blur UV step X applied"), FMath::IsNearlyEqual(BlurUVStep.R, 0.01f, 0.0001f));
	TestTrue(TEXT("RetainerBox blur UV step Y applied"), FMath::IsNearlyEqual(BlurUVStep.G, 0.02f, 0.0001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeometryMaskRetainerMaterialApplicationTest,
	"Design2UMG.PropertyApplier.RetainerEffectMaterialGeometryMask",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FGeometryMaskRetainerMaterialApplicationTest::RunTest(const FString& Parameters)
{
	URetainerBox* Widget = NewObject<URetainerBox>(GetTransientPackageAsObject());
	TestTrue(TEXT("Geometry mask RetainerBox created"), Widget != nullptr);
	if (!Widget)
	{
		return false;
	}

	FUMGWidgetNode Node;
	Node.Type = TEXT("RetainerBox");
	Node.Effect.bRetainRendering = true;
	Node.Effect.EffectMaterial = TEXT("d2u.retainer.geometry_mask");
	Node.Effect.TextureParameter = TEXT("Texture");
	Node.Effect.GeometryMaskShape = EUMGGeometryMaskShape::RoundedRectangle;
	Node.Effect.GeometryMaskSize = FVector2D(100.0f, 80.0f);
	Node.Effect.GeometryMaskCornerRadii = FVector4(4.0f, 8.0f, 12.0f, 16.0f);
	FUMGWidgetUtils::ApplyWidgetProperties(Widget, Node);

	UMaterialInstanceDynamic* DynamicEffect = Widget->GetEffectMaterial();
	TestTrue(TEXT("Geometry mask dynamic material created"), DynamicEffect != nullptr);
	const UMaterialInterface* EffectMaterial = Widget->GetEffectMaterialInterface();
	TestTrue(TEXT("Geometry mask material instance survives generated asset reload"),
		EffectMaterial
		&& EffectMaterial->GetMaterial()
		&& EffectMaterial->GetMaterial()->GetPathName().Equals(
			TEXT("/Game/Design2UMG/GeneratedMaterials/D2U_GeometryMaskRetainerV15.D2U_GeometryMaskRetainerV15"),
			ESearchCase::CaseSensitive)
		&& !EffectMaterial->HasAnyFlags(RF_Transient));
	const FMaterialResource* MaterialResource = EffectMaterial
		? EffectMaterial->GetMaterialResource(GMaxRHIShaderPlatform)
		: nullptr;
	TestTrue(TEXT("Geometry mask material shader compilation is complete"),
		MaterialResource && MaterialResource->IsGameThreadShaderMapComplete());
	TestTrue(TEXT("Geometry mask material has no compile errors"),
		MaterialResource && MaterialResource->GetCompileErrors().IsEmpty());
	if (!DynamicEffect)
	{
		return false;
	}

	const float MaskShape = DynamicEffect->K2_GetScalarParameterValue(TEXT("MaskShape"));
	const FLinearColor MaskSize = DynamicEffect->K2_GetVectorParameterValue(TEXT("MaskSize"));
	const FLinearColor MaskTopRadii = DynamicEffect->K2_GetVectorParameterValue(TEXT("MaskCornerRadiiTop"));
	const FLinearColor MaskBottomRadii = DynamicEffect->K2_GetVectorParameterValue(TEXT("MaskCornerRadiiBottom"));
	TestTrue(TEXT("Rounded rectangle material mode applied"), FMath::IsNearlyEqual(MaskShape, 1.0f));
	TestTrue(TEXT("Geometry mask width applied"), FMath::IsNearlyEqual(MaskSize.R, 100.0f));
	TestTrue(TEXT("Geometry mask height applied"), FMath::IsNearlyEqual(MaskSize.G, 80.0f));
	TestTrue(TEXT("Geometry mask top-left radius applied"), FMath::IsNearlyEqual(MaskTopRadii.R, 4.0f));
	TestTrue(TEXT("Geometry mask bottom-left radius applied"), FMath::IsNearlyEqual(MaskBottomRadii.G, 16.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInnerShadowRetainerMaterialApplicationTest,
	"Design2UMG.Shadow.InnerShadowRetainerMaterialApplication",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FInnerShadowRetainerMaterialApplicationTest::RunTest(const FString& Parameters)
{
	URetainerBox* Widget = NewObject<URetainerBox>(GetTransientPackageAsObject());
	TestTrue(TEXT("Inner shadow RetainerBox created"), Widget != nullptr);
	if (!Widget)
	{
		return false;
	}

	FUMGWidgetNode Node;
	Node.Type = TEXT("RetainerBox");
	Node.Effect.bRetainRendering = true;
	Node.Effect.EffectMaterial = TEXT("d2u.retainer.inner_shadow");
	Node.Effect.TextureParameter = TEXT("Texture");
	Node.Effect.EffectColor = FLinearColor(0.1f, 0.2f, 0.3f, 0.4f);
	Node.Effect.EffectOffset = FVector2D(0.05f, -0.03f);
	Node.Effect.EffectBlurUVStep = FVector2D(0.01f, 0.02f);
	const FLinearColor NormalizedEffectColor = FUMGValueParser::ParseColor(
		Design2UMGWidgetNodeAccess::SerializeWidgetColor(Node.Effect.EffectColor.GetValue()));

	TestTrue(TEXT("Typed retainer payload keeps EffectColor directly"),
		Node.Effect.EffectColor.IsSet()
		&& Node.Effect.EffectColor.GetValue().Equals(FLinearColor(0.1f, 0.2f, 0.3f, 0.4f), 0.0001f));

	URetainerBox* PayloadWidget = NewObject<URetainerBox>(GetTransientPackageAsObject());
	TestTrue(TEXT("Typed payload RetainerBox created"), PayloadWidget != nullptr);
	if (!PayloadWidget)
	{
		return false;
	}

	FUMGWidgetUtils::ApplyWidgetProperties(PayloadWidget, Node);
	UMaterialInstanceDynamic* PayloadEffect = PayloadWidget->GetEffectMaterial();
	TestTrue(TEXT("Typed payload dynamic effect material created"), PayloadEffect != nullptr);
	if (!PayloadEffect)
	{
		return false;
	}

	const FLinearColor PayloadInnerShadowColor = PayloadEffect->K2_GetVectorParameterValue(TEXT("InnerShadowColor"));
	TestTrue(TEXT("Typed payload inner shadow color follows normalized typed payload color"),
		PayloadInnerShadowColor.Equals(NormalizedEffectColor, 0.0001f));

	FUMGWidgetUtils::ApplyWidgetProperties(Widget, Node);

	UMaterialInstanceDynamic* DynamicEffect = Widget->GetEffectMaterial();
	TestTrue(TEXT("Inner shadow dynamic effect material created"), DynamicEffect != nullptr);
	if (!DynamicEffect)
	{
		return false;
	}

	const FLinearColor BlurUVStep = DynamicEffect->K2_GetVectorParameterValue(TEXT("BlurUVStep"));
	const FLinearColor InnerShadowColor = DynamicEffect->K2_GetVectorParameterValue(TEXT("InnerShadowColor"));
	const FLinearColor InnerShadowOffset = DynamicEffect->K2_GetVectorParameterValue(TEXT("InnerShadowOffset"));
	const float InnerShadowStrength = DynamicEffect->K2_GetScalarParameterValue(TEXT("InnerShadowStrength"));
	TestTrue(TEXT("Inner shadow blur UV step X applied"), FMath::IsNearlyEqual(BlurUVStep.R, 0.01f, 0.0001f));
	TestTrue(TEXT("Inner shadow blur UV step Y applied"), FMath::IsNearlyEqual(BlurUVStep.G, 0.02f, 0.0001f));
	TestTrue(TEXT("Inner shadow color R applied"), FMath::IsNearlyEqual(InnerShadowColor.R, NormalizedEffectColor.R, 0.0001f));
	TestTrue(TEXT("Inner shadow color A applied"), FMath::IsNearlyEqual(InnerShadowColor.A, NormalizedEffectColor.A, 0.0001f));
	TestTrue(TEXT("Inner shadow strength applied"), FMath::IsNearlyEqual(InnerShadowStrength, NormalizedEffectColor.A, 0.0001f));
	TestTrue(TEXT("Inner shadow offset X applied"), FMath::IsNearlyEqual(InnerShadowOffset.R, 0.05f, 0.0001f));
	TestTrue(TEXT("Inner shadow offset Y applied"), FMath::IsNearlyEqual(InnerShadowOffset.G, -0.03f, 0.0001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRetainerTypedPayloadExplicitFalseAndRenderingPhaseTest,
	"Design2UMG.PropertyApplier.RetainerExplicitFalseAndRenderingPhase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRetainerTypedPayloadExplicitFalseAndRenderingPhaseTest::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Node;
	Node.Type = TEXT("RetainerBox");
	Node.Effect.bRetainRendering = false;
	FUMGEffectData::FRenderingPhaseData RenderingPhase;
	RenderingPhase.Phase = 3;
	RenderingPhase.PhaseCount = 5;
	Node.Effect.RenderingPhase = RenderingPhase;

	TestTrue(TEXT("Typed retainer payload keeps explicit false RetainRendering"),
		Node.Effect.bRetainRendering.IsSet() && !Node.Effect.bRetainRendering.GetValue());
	TestTrue(TEXT("Typed retainer payload keeps RenderingPhase phase"),
		Node.Effect.RenderingPhase.IsSet() && Node.Effect.RenderingPhase.GetValue().Phase == 3);
	TestTrue(TEXT("Typed retainer payload keeps RenderingPhase phase count"),
		Node.Effect.RenderingPhase.IsSet() && Node.Effect.RenderingPhase.GetValue().PhaseCount == 5);

	URetainerBox* TypedWidget = NewObject<URetainerBox>(GetTransientPackageAsObject());
	URetainerBox* PayloadWidget = NewObject<URetainerBox>(GetTransientPackageAsObject());
	TestNotNull(TEXT("Typed retainer widget created"), TypedWidget);
	TestNotNull(TEXT("Payload retainer widget created"), PayloadWidget);
	if (!TypedWidget || !PayloadWidget)
	{
		return false;
	}

	TypedWidget->SetRetainRendering(true);
	TypedWidget->SetRenderingPhase(0, 1);
	FUMGWidgetUtils::ApplyWidgetProperties(TypedWidget, Node);
	TestFalse(TEXT("Typed retainer widget applies explicit false RetainRendering"), TypedWidget->IsRetainRendering());
	TestEqual(TEXT("Typed retainer widget applies RenderingPhase phase"), TypedWidget->GetPhase(), 3);
	TestEqual(TEXT("Typed retainer widget applies RenderingPhase phase count"), TypedWidget->GetPhaseCount(), 5);

	PayloadWidget->SetRetainRendering(true);
	PayloadWidget->SetRenderingPhase(0, 1);
	FUMGWidgetUtils::ApplyWidgetProperties(PayloadWidget, Node);
	TestFalse(TEXT("Payload retainer widget applies explicit false RetainRendering"), PayloadWidget->IsRetainRendering());
	TestEqual(TEXT("Payload retainer widget applies RenderingPhase phase"), PayloadWidget->GetPhase(), 3);
	TestEqual(TEXT("Payload retainer widget applies RenderingPhase phase count"), PayloadWidget->GetPhaseCount(), 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeometryMaskEffectJsonRoundTripTest,
	"Design2UMG.UMGJson.GeometryMaskEffectRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FGeometryMaskEffectJsonRoundTripTest::RunTest(const FString& Parameters)
{
	FUMGWidgetData SourceData;
	SourceData.WidgetName = TEXT("WBP_GeometryMaskEffectRoundTrip");
	SourceData.RootWidget.Type = TEXT("RetainerBox");
	SourceData.RootWidget.Id = TEXT("geometry_mask_root");
	SourceData.RootWidget.Name = TEXT("GeometryMaskRoot");
	SourceData.RootWidget.Effect.bRetainRendering = true;
	SourceData.RootWidget.Effect.EffectMaterial = TEXT("d2u.retainer.geometry_mask");
	SourceData.RootWidget.Effect.TextureParameter = TEXT("Texture");
	SourceData.RootWidget.Effect.GeometryMaskShape = EUMGGeometryMaskShape::RoundedRectangle;
	SourceData.RootWidget.Effect.GeometryMaskSize = FVector2D(100.0f, 80.0f);
	SourceData.RootWidget.Effect.GeometryMaskCornerRadii = FVector4(4.0f, 8.0f, 12.0f, 16.0f);

	const FString Json = FUMGJsonSerializer::Serialize(SourceData);
	TestTrue(TEXT("Serialized geometry mask emits shape"), Json.Contains(TEXT("\"GeometryMaskShape\"")));
	TestTrue(TEXT("Serialized geometry mask emits size"), Json.Contains(TEXT("\"GeometryMaskSize\"")));
	TestTrue(TEXT("Serialized geometry mask emits corner radii"), Json.Contains(TEXT("\"GeometryMaskCornerRadii\"")));

	FUMGWidgetData ParsedData;
	FString ParseError;
	TestTrue(TEXT("Geometry mask effect JSON parses"), FUMGJsonParser::ParseJson(Json, ParsedData, &ParseError));
	TestTrue(TEXT("Parsed geometry mask shape preserved"),
		ParsedData.RootWidget.Effect.GeometryMaskShape.IsSet()
		&& ParsedData.RootWidget.Effect.GeometryMaskShape.GetValue() == EUMGGeometryMaskShape::RoundedRectangle);
	TestTrue(TEXT("Parsed geometry mask size preserved"),
		ParsedData.RootWidget.Effect.GeometryMaskSize.IsSet()
		&& ParsedData.RootWidget.Effect.GeometryMaskSize.GetValue().Equals(FVector2D(100.0f, 80.0f), 0.001f));
	TestTrue(TEXT("Parsed geometry mask radii preserved"),
		ParsedData.RootWidget.Effect.GeometryMaskCornerRadii.IsSet()
		&& ParsedData.RootWidget.Effect.GeometryMaskCornerRadii.GetValue().Equals(FVector4(4.0f, 8.0f, 12.0f, 16.0f), 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInnerShadowEffectJsonRoundTripTest,
	"Design2UMG.UMGJson.InnerShadowEffectRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FInnerShadowEffectJsonRoundTripTest::RunTest(const FString& Parameters)
{
	FUMGWidgetData SourceData;
	SourceData.WidgetName = TEXT("WBP_InnerShadowEffectRoundTrip");
	SourceData.RootWidget.Type = TEXT("RetainerBox");
	SourceData.RootWidget.Id = TEXT("inner_shadow_root");
	SourceData.RootWidget.Name = TEXT("InnerShadowRoot");
	SourceData.RootWidget.Effect.bRetainRendering = true;
	SourceData.RootWidget.Effect.EffectMaterial = TEXT("d2u.retainer.inner_shadow");
	SourceData.RootWidget.Effect.TextureParameter = TEXT("Texture");
	SourceData.RootWidget.Effect.EffectColor = FLinearColor(0.1f, 0.2f, 0.3f, 0.4f);
	SourceData.RootWidget.Effect.EffectOffset = FVector2D(0.05f, -0.03f);
	SourceData.RootWidget.Effect.EffectBlurUVStep = FVector2D(0.01f, 0.02f);

	const FString Json = FUMGJsonSerializer::Serialize(SourceData);
	TestFalse(TEXT("Serialized inner shadow emits Effect payload"), Json.IsEmpty() || !Json.Contains(TEXT("\"Effect\"")));
	TestTrue(TEXT("Serialized inner shadow emits effect color"), Json.Contains(TEXT("\"EffectColor\"")));
	TestTrue(TEXT("Serialized inner shadow emits effect offset"), Json.Contains(TEXT("\"EffectOffset\"")));

	FUMGWidgetData ParsedData;
	FString ParseError;
	TestTrue(TEXT("Inner shadow effect JSON parses"), FUMGJsonParser::ParseJson(Json, ParsedData, &ParseError));
	if (!ParseError.IsEmpty())
	{
		AddInfo(ParseError);
	}

	TestEqual(TEXT("Parsed inner shadow effect material preserved"), ParsedData.RootWidget.Effect.EffectMaterial.GetValue(), TEXT("d2u.retainer.inner_shadow"));
	TestTrue(TEXT("Parsed inner shadow color preserved"), ParsedData.RootWidget.Effect.EffectColor.IsSet());
	TestTrue(TEXT("Parsed inner shadow offset preserved"), ParsedData.RootWidget.Effect.EffectOffset.IsSet());
	TestTrue(TEXT("Parsed inner shadow blur step preserved"), ParsedData.RootWidget.Effect.EffectBlurUVStep.IsSet());
	TestTrue(TEXT("Parsed inner shadow color alpha preserved"), FMath::IsNearlyEqual(ParsedData.RootWidget.Effect.EffectColor.GetValue().A, 0.4f, 0.0001f));
	TestTrue(TEXT("Parsed inner shadow offset X preserved"), FMath::IsNearlyEqual(ParsedData.RootWidget.Effect.EffectOffset.GetValue().X, 0.05f, 0.0001f));
	TestTrue(TEXT("Parsed inner shadow offset Y preserved"), FMath::IsNearlyEqual(ParsedData.RootWidget.Effect.EffectOffset.GetValue().Y, -0.03f, 0.0001f));
	return true;
}

#endif

