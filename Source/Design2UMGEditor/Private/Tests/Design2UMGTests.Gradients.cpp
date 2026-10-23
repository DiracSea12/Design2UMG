// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

// Radial gradient fidelity: multi-stop payloads, transform handling, and Figma handle conversion.

#include "UObject/Package.h"
#include "Misc/AutomationTest.h"
#include "Design2UMGTestUtils.h"
#include "Design2UMGConverter.h"
#include "PenParser.h"
#include "FigmaParser.h"
#include "FigmaTestDocBuilder.h"
#include "ProceduralShapeGenerator.h"
#include "DesignPathRasterizer.h"
#include "UMGJsonParser.h"
#include "UMGJsonSerializer.h"
#include "UMGPropertyKeys.h"
#include "Materials/Material.h"
#include "Components/Widget.h"
#include "Engine/Texture2D.h"
#include "UObject/UObjectGlobals.h"

#if WITH_AUTOMATION_WORKER

using namespace Design2UMGCoreTests;
using namespace Design2UMGTestUtils;

using Design2UMGTestUtils::BuildProceduralRequestForTest;
using Design2UMGTestUtils::ReadTexturePixel;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAngularGradientBasisContractTest,
	"Design2UMG.Gradient.AngularBasisContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FAngularGradientBasisContractTest::RunTest(const FString&)
{
	const FString Fill = TEXT(R"({"type":"gradient","gradientType":"angular","rotation":35,"center":{"x":0.25,"y":0.75},"size":{"width":1,"height":1.2},"colors":[{"color":"#ff0000","position":0},{"color":"#0000ff","position":1}]})");
	FDesignDocument Frame;
	const FString FrameJson = FString::Printf(TEXT(R"({"version":"2.21","children":[{"id":"angular","type":"frame","width":120,"height":80,"fill":%s}]})"), *Fill);
	if (!TestTrue(TEXT("Angular source parses"), FPenParser::ParseDocument(FrameJson, Frame))) return false;
	if (!TestEqual(TEXT("One angular source node"), Frame.Children.Num(), 1)) return false;
	const auto& Source = Frame.Children[0]->Visual.TopmostFill().Gradient;
	const FVector4 Expected(-0.3441458618, -0.4914912266, 0.4095760221, -0.2867882182);
	if (!TestTrue(TEXT("Source records the complete normalized angular basis"), Source.AngularBasis.IsSet())) return false;
	TestTrue(TEXT("Official top and right axes reach the source payload"), Source.AngularBasis.GetValue().Equals(Expected, 0.000001));
	TestFalse(TEXT("Explicit axes do not duplicate size geometry"), Source.Size.IsSet());
	TestEqual(TEXT("Explicit axes do not duplicate rotation geometry"), Source.Rotation, 0.0f);
	FDesignDocument DefaultSize;
	const FString DefaultJson = FrameJson.Replace(TEXT(",\"size\":{\"width\":1,\"height\":1.2}"), TEXT(""));
	if (!TestTrue(TEXT("Angular default size parses"), FPenParser::ParseDocument(DefaultJson, DefaultSize))) return false;
	const auto& DefaultBasis = DefaultSize.Children[0]->Visual.TopmostFill().Gradient.AngularBasis;
	TestTrue(TEXT("Default size also records explicit axes"), DefaultBasis.IsSet()
		&& DefaultBasis.GetValue().Equals(FVector4(Expected.X / 1.2, Expected.Y / 1.2, Expected.Z, Expected.W), 0.000001));

	FUMGWidgetData Data;
	Data.RootWidget = FDesign2UMGConverter::Convert(Frame, FDesign2UMGConversionSettings::Defaults()).RootWidget;
	FUMGWidgetData Parsed;
	FString Error;
	if (!TestTrue(TEXT("Angular typed JSON parses"), FUMGJsonParser::ParseJson(FUMGJsonSerializer::Serialize(Data), Parsed, &Error))) return false;
	TestTrue(TEXT("Angular basis survives source mapping and JSON"), Parsed.RootWidget.Brush.Gradient.AngularBasis.IsSet()
		&& Parsed.RootWidget.Brush.Gradient.AngularBasis.GetValue().Equals(Expected, 0.000001));

	FDesignDocument Line;
	const FString LineJson = FString::Printf(TEXT(R"({"version":"2.21","children":[{"id":"line","type":"line","width":20,"height":4,"strokeWidth":4,"strokeLinecap":"round","stroke":%s}]})"), *Fill);
	if (!TestTrue(TEXT("Angular stroked line parses"), FPenParser::ParseDocument(LineJson, Line))) return false;
	if (!TestEqual(TEXT("The line source is present before conversion"), Line.Children.Num(), 1)) return false;
	const auto& SourceStroke = Line.Children[0]->Visual.Stroke;
	if (!TestTrue(TEXT("The line source has its declared stroke width"), SourceStroke.HasStroke())) return false;
	if (!TestTrue(TEXT("The line source stroke retains the angular paint before conversion"),
		SourceStroke.Fill.IsSet() && SourceStroke.Fill->Type == EDesignFillType::Gradient &&
		SourceStroke.Fill->Gradient.AngularBasis.IsSet() &&
		SourceStroke.Fill->Gradient.AngularBasis.GetValue().Equals(Expected, 0.000001))) return false;
	const auto LineData = FDesign2UMGConverter::Convert(Line, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Native line stroke owns the source angular axes"), LineData.RootWidget.Path.StrokeGradient.AngularBasis.IsSet());
	FUMGWidgetData ParsedLine;
	if (!TestTrue(TEXT("Native stroke JSON parses"), FUMGJsonParser::ParseJson(FUMGJsonSerializer::Serialize(LineData), ParsedLine, &Error))) return false;
	FDesignPathRasterRequest Request;
	if (!TestTrue(TEXT("Native line builds its production raster request"), FDesignPathRasterizer::TryBuildRequest(ParsedLine.RootWidget, Request))) return false;
	if (!TestTrue(TEXT("Native request retains the stroke paint"), Request.StrokePaint.IsSet())) return false;
	auto Gradient = Request.StrokePaint->Gradient;
	if (!TestTrue(TEXT("Angular source basis survives path JSON and request transfer"), Gradient.Type.Get(FString()) == TEXT("Angular")
		&& Gradient.AngularBasis.IsSet() && Gradient.AngularBasis.GetValue().Equals(Expected, 0.000001)
		&& Gradient.Center.Get(FVector2D::ZeroVector).Equals(FVector2D(0.25, 0.75), 0.000001))) return false;
	if (!TestTrue(TEXT("Round-cap native stroke expands its raster canvas"), Request.HasExpandedCanvas())) return false;
	AddInfo(FString::Printf(TEXT("Angular native stroke request: paint=%dx%d raster=%dx%d origin=%d,%d"),
		Request.PaintSize.X, Request.PaintSize.Y, Request.Size.X, Request.Size.Y, Request.PaintOffset.X, Request.PaintOffset.Y));
	const FVector2D OriginalSpan(FMath::Max(Request.PaintSize.X - 1, 1), FMath::Max(Request.PaintSize.Y - 1, 1));
	const FVector2D ExpandedSpan(FMath::Max(Request.Size.X - 1, 1), FMath::Max(Request.Size.Y - 1, 1));
	const FVector2D Scale = OriginalSpan / ExpandedSpan;
	const FVector2D Origin(Request.PaintOffset.X, Request.PaintOffset.Y);
	Gradient.TransformAngularPaintSpace(Scale, Origin / ExpandedSpan);
	const FVector4 Expanded(Expected.X * Scale.X, Expected.Y * Scale.Y, Expected.Z * Scale.X, Expected.W * Scale.Y);
	TestTrue(TEXT("Actual raster expansion preserves both source paint axes"), Gradient.AngularBasis.GetValue().Equals(Expanded, 0.000001));
	TestTrue(TEXT("Actual raster expansion preserves the source paint center"),
		Gradient.Center.Get(FVector2D::ZeroVector).Equals((FVector2D(0.25, 0.75) * OriginalSpan + Origin) / ExpandedSpan, 0.000001));
	UTexture2D* StrokeTexture = FDesignPathRasterizer::Rasterize(Request, &Error);
	FDesignPathRasterizer::FlushPendingTextureUpdates();
	if (!TestNotNull(FString::Printf(TEXT("Native angular stroke rasterizes: %s"), *Error), StrokeTexture)) return false;

	FUMGProceduralResourceRequest Circular;
	Circular.MaterialRef = TEXT("angulargradient");
	Circular.WidthOverride = 12;
	Circular.HeightOverride = 8;
	Circular.Gradient.Type = TEXT("Angular");
	Circular.Gradient.Rotation = 23;
	Circular.Gradient.Stops = Data.RootWidget.Brush.Gradient.Stops;
	TestNotNull(TEXT("Current angle-only gradient remains supported"), FProceduralShapeGenerator::GenerateFromMaterialRef(Circular, nullptr, &Error));
	Circular.WidthOverride = 1;
	Circular.Gradient.AngularBasis = Expected;
	TestNotNull(TEXT("Valid angular basis supports a single-pixel dimension"), FProceduralShapeGenerator::GenerateFromMaterialRef(Circular, nullptr, &Error));
	return true;
}

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
		GetTransientPackage());
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
		GetTransientPackage());
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
	const FString Json = MakeFigmaDocJson(
		MakeContainerNode(TEXT("0:2"), TEXT("Root"), TEXT("FRAME"), 0, 0, 64, 64,
			MakeLeafNode(TEXT("grad_figma"), TEXT("Figma Radial"), TEXT("RECTANGLE"), 0, 0, 64, 64,
				TEXT(",\"fills\":[{"
					 "\"type\":\"GRADIENT_RADIAL\",\"visible\":true,"
					 "\"gradientHandlePositions\":[{\"x\":0.25,\"y\":0.75},{\"x\":0.25,\"y\":0.875},{\"x\":0.75,\"y\":0.75}],"
					 "\"gradientStops\":["
					 "{\"position\":0.0,\"color\":{\"r\":1.0,\"g\":0.0,\"b\":0.0,\"a\":1.0}},"
					 "{\"position\":1.0,\"color\":{\"r\":0.0,\"g\":0.0,\"b\":1.0,\"a\":1.0}}"
					 "]}]"))));

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
		GetTransientPackage());
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
