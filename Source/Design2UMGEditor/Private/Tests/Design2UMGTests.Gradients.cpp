// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

// Radial gradient fidelity: multi-stop payloads, transform handling, and Figma handle conversion.

#include "Misc/AutomationTest.h"
#include "Design2UMGTestUtils.h"
#include "Design2UMGConverter.h"
#include "PenParser.h"
#include "FigmaParser.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRadialGradientMultiStopFidelityTest,
	"Design2UMG.Gradient.RadialMultiStopFidelity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRadialGradientMultiStopFidelityTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"children": [
			{
				"id": "root",
				"type": "frame",
				"name": "Root",
				"width": 64,
				"height": 64,
				"children": [
					{
						"id": "grad_card",
						"type": "rectangle",
						"name": "Gradient Card",
						"width": 64,
						"height": 64,
						"fill": {
							"type": "gradient",
							"gradientType": "radial",
							"colors": [
								{ "color": "#FF0000FF", "position": 0.0 },
								{ "color": "#00FF00FF", "position": 0.5 },
								{ "color": "#0000FFFF", "position": 1.0 }
							]
						}
					}
				]
			}
		]
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Radial multi-stop gradient fixture parses"), FPenParser::ParseDocument(Json, Doc));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* Widget = FindWidgetById(Data.RootWidget, TEXT("grad_card"));
	TestTrue(TEXT("Radial gradient widget exists"), Widget != nullptr);
	if (!Widget)
	{
		return false;
	}

	TestEqual(TEXT("Radial gradient material ref preserved"), GetWidgetProperty(*Widget, UMGPropertyKeys::MaterialRef), TEXT("radialgradient"));
	const FString MaterialParams = GetWidgetProperty(*Widget, UMGPropertyKeys::MaterialParams);
	TestTrue(TEXT("Radial gradient exports Stops array"), MaterialParams.Contains(TEXT("\"Stops\"")));
	TestFalse(TEXT("Radial gradient no longer falls back to center/edge pair"), MaterialParams.Contains(TEXT("ColorCenter")));

	UTexture2D* Texture = FProceduralShapeGenerator::GenerateFromMaterialRef(BuildProceduralRequestForTest(*this, *Widget),
		GetTransientPackageAsObject());
	TestTrue(TEXT("Radial gradient texture generated"), Texture != nullptr);
	if (!Texture)
	{
		return false;
	}

	FColor CenterPixel;
	FColor MidPixel;
	FColor EdgePixel;
	TestTrue(TEXT("Radial gradient center pixel readable"), ReadTexturePixel(Texture, 32, 32, CenterPixel));
	TestTrue(TEXT("Radial gradient mid pixel readable"), ReadTexturePixel(Texture, 32, 16, MidPixel));
	TestTrue(TEXT("Radial gradient edge pixel readable"), ReadTexturePixel(Texture, 0, 32, EdgePixel));

	TestTrue(TEXT("Radial gradient center keeps first stop red"), CenterPixel.R > 200 && CenterPixel.G < 80 && CenterPixel.B < 80);
	TestTrue(TEXT("Radial gradient midpoint reaches middle stop green"), MidPixel.G > MidPixel.R && MidPixel.G > MidPixel.B);
	TestTrue(TEXT("Radial gradient edge reaches final stop blue"), EdgePixel.B > 200 && EdgePixel.R < 80 && EdgePixel.G < 80);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRadialGradientTransformFidelityTest,
	"Design2UMG.Gradient.RadialTransformFidelity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRadialGradientTransformFidelityTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"children": [
			{
				"id": "root",
				"type": "frame",
				"name": "Root",
				"width": 64,
				"height": 64,
				"children": [
					{
						"id": "grad_transform",
						"type": "rectangle",
						"name": "Gradient Transform",
						"width": 64,
						"height": 64,
						"fill": {
							"type": "gradient",
							"gradientType": "radial",
							"rotation": 90,
							"center": {"x": 0.25, "y": 0.75},
							"size": {"width": 0.25, "height": 1.0},
							"colors": [
								{ "color": "#FF0000FF", "position": 0.0 },
								{ "color": "#0000FFFF", "position": 1.0 }
							]
						}
					}
				]
			}
		]
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Radial transform fixture parses"), FPenParser::ParseDocument(Json, Doc));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* Widget = FindWidgetById(Data.RootWidget, TEXT("grad_transform"));
	TestTrue(TEXT("Radial transform widget exists"), Widget != nullptr);
	if (!Widget)
	{
		return false;
	}

	const FString MaterialParams = GetWidgetProperty(*Widget, UMGPropertyKeys::MaterialParams);
	TestEqual(TEXT("Radial transform material ref preserved"), GetWidgetProperty(*Widget, UMGPropertyKeys::MaterialRef), TEXT("radialgradient"));
	TestTrue(TEXT("Radial transform exports center X"), MaterialParams.Contains(TEXT("\"CenterX\":0.2500")));
	TestTrue(TEXT("Radial transform exports center Y"), MaterialParams.Contains(TEXT("\"CenterY\":0.7500")));
	TestTrue(TEXT("Radial transform exports size width"), MaterialParams.Contains(TEXT("\"SizeWidth\":0.2500")));
	TestTrue(TEXT("Radial transform exports size height"), MaterialParams.Contains(TEXT("\"SizeHeight\":1.0000")));
	TestTrue(TEXT("Radial transform exports rotation"), MaterialParams.Contains(TEXT("\"Rotation\":90.0000")));

	UTexture2D* Texture = FProceduralShapeGenerator::GenerateFromMaterialRef(BuildProceduralRequestForTest(*this, *Widget),
		GetTransientPackageAsObject());
	TestTrue(TEXT("Radial transform texture generated"), Texture != nullptr);
	if (!Texture)
	{
		return false;
	}

	FColor ShiftedCenterPixel;
	FColor RotatedMajorAxisPixel;
	FColor RotatedMinorAxisPixel;
	FColor TextureCenterPixel;
	TestTrue(TEXT("Radial transform shifted center pixel readable"), ReadTexturePixel(Texture, 16, 47, ShiftedCenterPixel));
	TestTrue(TEXT("Radial transform major-axis pixel readable"), ReadTexturePixel(Texture, 27, 47, RotatedMajorAxisPixel));
	TestTrue(TEXT("Radial transform minor-axis pixel readable"), ReadTexturePixel(Texture, 16, 36, RotatedMinorAxisPixel));
	TestTrue(TEXT("Radial transform texture-center pixel readable"), ReadTexturePixel(Texture, 32, 32, TextureCenterPixel));

	TestTrue(TEXT("Shifted center keeps red core"), ShiftedCenterPixel.R > 200 && ShiftedCenterPixel.B < 80);
	TestTrue(TEXT("Rotated major axis stays inside elongated ellipse"), RotatedMajorAxisPixel.R > RotatedMajorAxisPixel.B);
	TestTrue(TEXT("Rotated minor axis reaches edge sooner"), RotatedMinorAxisPixel.B > 200 && RotatedMinorAxisPixel.R < 80);
	TestTrue(TEXT("Default center no longer acts as gradient center"), TextureCenterPixel.B > 180 && TextureCenterPixel.R < 120);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaRadialGradientHandleFidelityTest,
	"Design2UMG.FigmaParser.RadialGradientHandleFidelity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaRadialGradientHandleFidelityTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"document": {
			"id": "0:0",
			"name": "Doc",
			"type": "DOCUMENT",
			"children": [{
				"id": "0:1",
				"name": "Page 1",
				"type": "CANVAS",
				"children": [{
					"id": "0:2",
					"name": "Root",
					"type": "FRAME",
					"visible": true,
					"absoluteBoundingBox": {"x":0,"y":0,"width":64,"height":64},
					"children": [{
						"id": "grad_figma",
						"name": "Figma Radial",
						"type": "RECTANGLE",
						"visible": true,
						"absoluteBoundingBox": {"x":0,"y":0,"width":64,"height":64},
						"fills": [{
							"type": "GRADIENT_RADIAL",
							"visible": true,
							"gradientHandlePositions": [
								{"x":0.25,"y":0.75},
								{"x":0.25,"y":0.875},
								{"x":0.75,"y":0.75}
							],
							"gradientStops": [
								{"position":0.0,"color":{"r":1.0,"g":0.0,"b":0.0,"a":1.0}},
								{"position":1.0,"color":{"r":0.0,"g":0.0,"b":1.0,"a":1.0}}
							]
						}]
					}]
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Figma radial handle fixture parses"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() != 1 || Doc.Children[0]->Children.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& GradientNode = Doc.Children[0]->Children[0];
	TestEqual(TEXT("Figma radial gradient type preserved"), GradientNode->Visual.TopmostFill().Gradient.GradientType, EDesignGradientType::Radial);
	TestTrue(TEXT("Figma radial center parsed"), GradientNode->Visual.TopmostFill().Gradient.Center.IsSet()
		&& FMath::IsNearlyEqual(GradientNode->Visual.TopmostFill().Gradient.Center.GetValue().X, 0.25f, 0.001f)
		&& FMath::IsNearlyEqual(GradientNode->Visual.TopmostFill().Gradient.Center.GetValue().Y, 0.75f, 0.001f));
	TestTrue(TEXT("Figma radial size parsed"), GradientNode->Visual.TopmostFill().Gradient.Size.IsSet()
		&& FMath::IsNearlyEqual(GradientNode->Visual.TopmostFill().Gradient.Size.GetValue().X, 0.25f, 0.001f)
		&& FMath::IsNearlyEqual(GradientNode->Visual.TopmostFill().Gradient.Size.GetValue().Y, 1.0f, 0.001f));
	TestTrue(TEXT("Figma radial rotation parsed"), FMath::IsNearlyEqual(GradientNode->Visual.TopmostFill().Gradient.Rotation, 90.0f, 0.001f));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* Widget = FindWidgetById(Data.RootWidget, TEXT("grad_figma"));
	TestTrue(TEXT("Figma radial widget exists"), Widget != nullptr);
	if (!Widget)
	{
		return false;
	}

	const FString MaterialParams = GetWidgetProperty(*Widget, UMGPropertyKeys::MaterialParams);
	TestEqual(TEXT("Figma radial material ref preserved"), GetWidgetProperty(*Widget, UMGPropertyKeys::MaterialRef), TEXT("radialgradient"));
	TestTrue(TEXT("Figma radial exports center X"), MaterialParams.Contains(TEXT("\"CenterX\":0.2500")));
	TestTrue(TEXT("Figma radial exports center Y"), MaterialParams.Contains(TEXT("\"CenterY\":0.7500")));
	TestTrue(TEXT("Figma radial exports size width"), MaterialParams.Contains(TEXT("\"SizeWidth\":0.2500")));
	TestTrue(TEXT("Figma radial exports size height"), MaterialParams.Contains(TEXT("\"SizeHeight\":1.0000")));
	TestTrue(TEXT("Figma radial exports rotation"), MaterialParams.Contains(TEXT("\"Rotation\":90.0000")));

	UTexture2D* Texture = FProceduralShapeGenerator::GenerateFromMaterialRef(BuildProceduralRequestForTest(*this, *Widget),
		GetTransientPackageAsObject());
	TestTrue(TEXT("Figma radial texture generated"), Texture != nullptr);
	if (!Texture)
	{
		return false;
	}

	FColor ShiftedCenterPixel;
	FColor RotatedMajorAxisPixel;
	FColor RotatedMinorAxisPixel;
	TestTrue(TEXT("Figma radial shifted center pixel readable"), ReadTexturePixel(Texture, 16, 47, ShiftedCenterPixel));
	TestTrue(TEXT("Figma radial major-axis pixel readable"), ReadTexturePixel(Texture, 27, 47, RotatedMajorAxisPixel));
	TestTrue(TEXT("Figma radial minor-axis pixel readable"), ReadTexturePixel(Texture, 16, 36, RotatedMinorAxisPixel));

	TestTrue(TEXT("Figma radial shifted center keeps red core"), ShiftedCenterPixel.R > 200 && ShiftedCenterPixel.B < 80);
	TestTrue(TEXT("Figma radial rotated major axis stays inside ellipse"), RotatedMajorAxisPixel.R > RotatedMajorAxisPixel.B);
	TestTrue(TEXT("Figma radial rotated minor axis reaches edge sooner"), RotatedMinorAxisPixel.B > 200 && RotatedMinorAxisPixel.R < 80);
	return true;
}

#endif

