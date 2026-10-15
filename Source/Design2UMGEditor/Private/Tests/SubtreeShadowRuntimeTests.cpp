// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved.
// SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"
#include "Design2UMGImageTestUtils.h"
#include "FigmaParser.h"
#include "Design2UMGConverter.h"
#include "UMGGenerator.h"
#include "UMGWidgetRenderer.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ImageUtils.h"
#include "ImageCore.h"
#include "PreBuildResourceNormalizer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FSubtreeShadowRuntimeTest,
	"Design2UMG.Converter.SubtreeShadowRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

void FSubtreeShadowRuntimeTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	Names.Add(TEXT("OpaqueChildren"));
	Commands.Add(TEXT("opaque"));
	Names.Add(TEXT("OverlappingTranslucentChildren"));
	Commands.Add(TEXT("overlap"));
	Names.Add(TEXT("BlackBlurredShadow"));
	Commands.Add(TEXT("blur"));
	Names.Add(TEXT("WideBlur32")); Commands.Add(TEXT("wide32"));
	Names.Add(TEXT("WideBlur100")); Commands.Add(TEXT("wide100"));
	Names.Add(TEXT("TranslatedBlurredShadow"));
	Commands.Add(TEXT("translated"));
	Names.Add(TEXT("RoundedTransparentContainer"));
	Commands.Add(TEXT("rounded"));
	Names.Add(TEXT("SiblingGeometryMask"));
	Commands.Add(TEXT("sibling_mask"));
	Names.Add(TEXT("NestedUserWidget"));
	Commands.Add(TEXT("nested"));
	Names.Add(TEXT("DescendantShadowOverflow"));
	Commands.Add(TEXT("child_overflow"));
}

bool FSubtreeShadowRuntimeTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"JSON({"document":{"id":"root","type":"FRAME",
		"absoluteBoundingBox":{"x":0,"y":0,"width":240,"height":120},
		"children":[{"id":"group","type":"FRAME",
		"absoluteBoundingBox":{"x":20,"y":20,"width":80,"height":80},
		"effects":[{"type":"DROP_SHADOW","visible":true,"radius":0,
		"offset":{"x":100,"y":0},"color":{"r":0,"g":1,"b":1,"a":0.5}}],
		"children":[{"id":"a","type":"RECTANGLE",
		"absoluteBoundingBox":{"x":20,"y":20,"width":20,"height":20},
		"fills":[{"type":"SOLID","color":{"r":1,"g":1,"b":1,"a":1}}]},
		{"id":"b","type":"RECTANGLE",
		"absoluteBoundingBox":{"x":80,"y":80,"width":20,"height":20},
		"fills":[{"type":"SOLID","color":{"r":1,"g":1,"b":1,"a":1}}]}]}]}})JSON");
	const bool bOverlapping = Parameters == TEXT("overlap");
	const bool bChildOverflow = Parameters == TEXT("child_overflow");
	const int32 TranslateY = Parameters == TEXT("translated") ? 500 : 0;
	const bool bWide = Parameters == TEXT("wide32") || Parameters == TEXT("wide100");
	const float Blur = Parameters == TEXT("wide100") ? 100.0f : (bWide ? 32.0f : 4.0f);
	const bool bBlurred = Parameters == TEXT("blur") || TranslateY != 0 || bWide;
	FString SourceJson = Json;
	if (bChildOverflow)
	{
		SourceJson.ReplaceInline(TEXT("\"height\":120"), TEXT("\"height\":160"));
		SourceJson.ReplaceInline(TEXT("\"id\":\"b\",\"type\":\"RECTANGLE\""),
			TEXT(R"JSON("id":"b","type":"RECTANGLE","effects":[{"type":"DROP_SHADOW","visible":true,"radius":0,"offset":{"x":0,"y":20},"color":{"r":0,"g":1,"b":1,"a":0.5}}])JSON"));
	}
	if (Parameters == TEXT("sibling_mask"))
	{
		SourceJson.ReplaceInline(TEXT("\"children\":[{\"id\":\"group\""),
			TEXT(R"JSON("children":[{"id":"masked","type":"FRAME",
"absoluteBoundingBox":{"x":0,"y":0,"width":10,"height":10},"children":[
{"id":"mask","type":"ELLIPSE","isMask":true,"absoluteBoundingBox":{"x":0,"y":0,"width":10,"height":10},
"fills":[{"type":"SOLID","color":{"r":1,"g":1,"b":1,"a":1}}]},
{"id":"maskedfill","type":"RECTANGLE","absoluteBoundingBox":{"x":0,"y":0,"width":10,"height":10},
"fills":[{"type":"SOLID","color":{"r":1,"g":0,"b":0,"a":1}}]}]}, {"id":"group")JSON"));
	}
	if (TranslateY != 0)
	{
		SourceJson.ReplaceInline(TEXT("\"height\":120"), TEXT("\"height\":620"));
		SourceJson.ReplaceInline(TEXT("\"y\":20"), TEXT("\"y\":520"));
		SourceJson.ReplaceInline(TEXT("\"y\":80"), TEXT("\"y\":580"));
	}
	if (Parameters == TEXT("rounded"))
	{
		SourceJson.ReplaceInline(TEXT("\"id\":\"group\",\"type\":\"FRAME\""),
			TEXT("\"id\":\"group\",\"type\":\"FRAME\",\"cornerRadius\":8"));
	}
	if (bBlurred)
	{
		SourceJson.ReplaceInline(TEXT("\"radius\":0"), *FString::Printf(TEXT("\"radius\":%.1f"), Blur));
		SourceJson.ReplaceInline(TEXT("\"r\":0,\"g\":1,\"b\":1"), TEXT("\"r\":0,\"g\":0,\"b\":0"));
	}
	if (bOverlapping)
	{
		SourceJson.ReplaceInline(TEXT("\"x\":80,\"y\":80"), TEXT("\"x\":20,\"y\":20"));
		SourceJson.ReplaceInline(TEXT("\"a\":1"), TEXT("\"a\":0.5"));
	}
	FDesignDocument Document;
	if (!TestTrue(TEXT("Transparent group parses"), FFigmaParser::ParseDocument(SourceJson, Document))) return false;
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	FPreBuildResourceNormalizer::NormalizeProceduralTextures(Data.RootWidget,
		TEXT("/Game/Design2UMG/Automation/SubtreeShadowFixture"), TEXT("SubtreeShadowProbe"));
	UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data,
		TEXT("/Temp/Design2UMGSubtreeShadow_") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
	if (!TestNotNull(TEXT("Compiled native group asset exists"), Blueprint)) return false;
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	if (Parameters == TEXT("nested"))
	{
		FUMGWidgetData ParentData;
		ParentData.WidgetName = TEXT("WBP_SubtreeShadowParent");
		ParentData.RootWidget.Type = TEXT("CanvasPanel");
		ParentData.RootWidget.Name = TEXT("ParentCanvas");
		ParentData.RootWidget.Bounds.W = 240;
		ParentData.RootWidget.Bounds.H = 120;
		UWidgetBlueprint* Parent = FUMGGenerator::BuildWidgetBlueprint(ParentData,
			TEXT("/Temp/Design2UMGShadowParent_") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
		if (!TestNotNull(TEXT("Parent blueprint exists"), Parent)) return false;
		UUserWidget* Child = NewObject<UUserWidget>(Parent->WidgetTree, Blueprint->GeneratedClass, TEXT("NestedShadowChild"));
		UCanvasPanel* Canvas = CastChecked<UCanvasPanel>(Parent->WidgetTree->RootWidget);
		Canvas->AddChildToCanvas(Child)->SetSize(FVector2D(240, 120));
		FKismetEditorUtilities::CompileBlueprint(Parent);
		Blueprint = Parent;
	}
	TArray<uint8> Png;
	FIntPoint Size;
	FString Error;
	if (!TestTrue(TEXT("Compiled group renders"), FUMGWidgetRenderer::RenderWidgetToPng(Blueprint, Png, Size, &Error)))
	{
		AddError(Error);
		return false;
	}
	FImage Image;
	FFileHelper::SaveArrayToFile(Png, *(FPaths::ProjectSavedDir() / (TEXT("SubtreeShadow-") + Parameters + TEXT(".png"))));
	if (!TestTrue(TEXT("PNG decodes"), Design2UMGImageTestUtils::DecodeToBGRA8(Png.GetData(), Png.Num(), Image))) return false;
	if (!TestTrue(TEXT("Expected output dimensions and pixel format"),
		Image.SizeX == 240 && Image.SizeY == 120 + TranslateY + (bChildOverflow ? 40 : 0) && Image.Format == ERawImageFormat::BGRA8)) return false;
	const FColor* Pixels = reinterpret_cast<const FColor*>(Image.RawData.GetData());
	const FColor FirstShadow = Pixels[(30 + TranslateY) * Image.SizeX + 130];
	const FColor GapShadow = Pixels[(60 + TranslateY) * Image.SizeX + 160];
	const FColor LastShadow = Pixels[(90 + TranslateY) * Image.SizeX + 190];
	int32 CyanCount = 0;
	int32 CyanMinX = Image.SizeX;
	int32 CyanMinY = Image.SizeY;
	for (int32 Y = 0; Y < Image.SizeY; ++Y)
	{
		for (int32 X = 0; X < Image.SizeX; ++X)
		{
			const FColor Pixel = Pixels[Y * Image.SizeX + X];
			if (Pixel.A > 2 && Pixel.R < 3 && Pixel.G > 252 && Pixel.B > 252)
			{
				++CyanCount;
				CyanMinX = FMath::Min(CyanMinX, X);
				CyanMinY = FMath::Min(CyanMinY, Y);
			}
		}
	}
	AddInfo(FString::Printf(TEXT("Overlap=%d, source alpha=%d, cyan pixels=%d min=%d,%d"),
		bOverlapping, Pixels[(30 + TranslateY) * Image.SizeX + 30].A, CyanCount, CyanMinX, CyanMinY));
	TestTrue(TEXT("Original subtree remains visible"),
		FMath::Abs(static_cast<int32>(Pixels[(30 + TranslateY) * Image.SizeX + 30].A) - (bOverlapping ? 192 : 255)) <= 2);
	AddInfo(FString::Printf(TEXT("Shadow first RGBA=%d,%d,%d,%d; gap alpha=%d; last alpha=%d"),
		FirstShadow.R, FirstShadow.G, FirstShadow.B, FirstShadow.A, GapShadow.A, LastShadow.A));
	int32 ExpectedAlpha = bOverlapping ? 96 : 128;
	if (bWide)
	{
		const double Sigma = Blur * 0.5;
		const int32 Radius = FMath::CeilToInt(Sigma * 3.0);
		auto Interval = [&](int32 Position, int32 Start)
		{
			double Sum = 0.0, Inside = 0.0;
			for (int32 Offset = -Radius; Offset <= Radius; ++Offset)
			{
				const double Weight = FMath::Exp(-double(Offset * Offset) / (2.0 * Sigma * Sigma));
				Sum += Weight;
				if (Position + Offset >= Start && Position + Offset < Start + 20) Inside += Weight;
			}
			return Inside / Sum;
		};
		int32 MaxError = 0;
		for (int32 Y = 0; Y < Image.SizeY; Y += 7)
		{
			for (int32 X = 110; X < Image.SizeX; X += 7)
			{
				const int32 Expected = FMath::RoundToInt(128.0 * (
					Interval(X - 100, 20) * Interval(Y, 20) + Interval(X - 100, 80) * Interval(Y, 80)));
				MaxError = FMath::Max(MaxError, FMath::Abs(int32(Pixels[Y * Image.SizeX + X].A) - Expected));
			}
		}
		TestTrue(FString::Printf(TEXT("Wide shadow stays within 2 alpha levels of dense Gaussian, max=%d"), MaxError), MaxError <= 2);
		return true;
	}
	if (bBlurred)
	{
		// Source blur radius 4 has sigma 2, matching the existing shape-shadow
		// rasterizer (GaussianBlurAlpha receives BlurRadius * 0.5).
		double WeightSum = 0;
		double InteriorWeight = 0;
		double EdgeWeight = 0;
		for (int32 Sample = -6; Sample <= 6; ++Sample)
		{
			const double Weight = FMath::Exp(-Sample * Sample / 8.0);
			WeightSum += Weight;
			if (Sample >= -10 && Sample <= 9) InteriorWeight += Weight;
			if (Sample <= -3) EdgeWeight += Weight;
		}
		ExpectedAlpha = FMath::RoundToInt(128.0 * FMath::Square(InteriorWeight / WeightSum));
		const int32 ExpectedEdgeAlpha = FMath::RoundToInt(128.0 * EdgeWeight * InteriorWeight / FMath::Square(WeightSum));
		const int32 ActualEdgeAlpha = Pixels[(30 + TranslateY) * Image.SizeX + 142].A;
		TestTrue(FString::Printf(TEXT("Blur falloff follows source radius: expected %d, actual %d"), ExpectedEdgeAlpha, ActualEdgeAlpha),
			FMath::Abs(ActualEdgeAlpha - ExpectedEdgeAlpha) <= 2);
	}
	TestTrue(TEXT("Shadow uses the composited child alpha once"), FMath::Abs(static_cast<int32>(FirstShadow.A) - ExpectedAlpha) <= 2);
	TestTrue(TEXT("Shadow follows the second child's actual position"), FMath::Abs(static_cast<int32>(LastShadow.A) - (bOverlapping ? 0 : ExpectedAlpha)) <= 2);
	TestEqual(TEXT("Transparent gap casts no shadow"), static_cast<int32>(GapShadow.A), 0);
	if (bChildOverflow)
	{
		TestEqual(TEXT("Unclipped descendant shadow outside the group remains visible"),
			static_cast<int32>(Pixels[115 * Image.SizeX + 90].A), 128);
		TestEqual(TEXT("Parent shadow includes descendant overflow"),
			static_cast<int32>(Pixels[115 * Image.SizeX + 190].A), 64);
	}
	if (Parameters == TEXT("sibling_mask"))
	{
		TestTrue(TEXT("Sibling masked content remains visible"), Pixels[5 * Image.SizeX + 5].R > 250);
		TestEqual(TEXT("Sibling geometry mask clips its corner"), static_cast<int32>(Pixels[0].A), 0);
	}
	TestTrue(TEXT("Shadow retains its declared color"), FirstShadow.R < 3 &&
		(bBlurred ? FirstShadow.G < 3 && FirstShadow.B < 3 : FirstShadow.G > 252 && FirstShadow.B > 252));
	TArray<uint8> RepeatedPng;
	FIntPoint RepeatedSize;
	if (!TestTrue(TEXT("Repeated rendering succeeds"),
		FUMGWidgetRenderer::RenderWidgetToPng(Blueprint, RepeatedPng, RepeatedSize, &Error)))
	{
		AddError(Error);
		return false;
	}
	FImage RepeatedImage;
	if (!TestTrue(TEXT("Repeated PNG decodes"),
		Design2UMGImageTestUtils::DecodeToBGRA8(RepeatedPng.GetData(), RepeatedPng.Num(), RepeatedImage))) return false;
	TestEqual(TEXT("Repeated rendering preserves dimensions"), RepeatedSize, Size);
	TestTrue(TEXT("Repeated rendering preserves every pixel"), RepeatedImage.RawData == Image.RawData);
	return true;
}
#endif
