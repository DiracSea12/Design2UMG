// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

// Figma luminosity image overlay coverage (wide/square/identity-transform variants and opacity cap).

#include "Misc/AutomationTest.h"
#include "Design2UMGTestUtils.h"
#include "Design2UMGConverter.h"
#include "Design2UMGSettings.h"
#include "FigmaParser.h"
#include "FigmaTestDocBuilder.h"

#if WITH_AUTOMATION_WORKER

using namespace Design2UMGCoreTests;
using namespace Design2UMGTestUtils;

namespace
{
	struct FLuminosityOverlayNodeSpec
	{
		const TCHAR* Id;
		const TCHAR* Name;
		double Width;
		double Height;
		const TCHAR* ImageRef;
		// Extra IMAGE-fill fields, e.g. ",\"scalingFactor\":0.5" or ",\"imageTransform\":[[1,0,0],[0,1,0]]".
		const TCHAR* ImageExtraFields;
	};

	FString MakeLuminosityOverlayDocJson(const FLuminosityOverlayNodeSpec& Spec)
	{
		return MakeFigmaDocJson(
			MakeContainerNode(Spec.Id, Spec.Name, TEXT("FRAME"), 0, 0, Spec.Width, Spec.Height, FString(),
				FString::Printf(
					TEXT(",\"absoluteRenderBounds\":%s,\"fills\":["
						 "{\"type\":\"SOLID\",\"visible\":true,\"blendMode\":\"NORMAL\",\"color\":{\"r\":0.9254902,\"g\":0.9019608,\"b\":0.94117647,\"a\":1}},"
						 "{\"type\":\"IMAGE\",\"visible\":true,\"blendMode\":\"LUMINOSITY\",\"imageRef\":\"%s\",\"scaleMode\":\"FIT\"%s}"
						 "]"),
					*MakeBoundsJson(0, 0, Spec.Width, Spec.Height), Spec.ImageRef, Spec.ImageExtraFields)));
	}

	struct FLuminosityOpacityCapScope
	{
		explicit FLuminosityOpacityCapScope(const float NewValue, const bool bEnableOverride = true)
		{
			Settings = UDesign2UMGSettings::Get();
			if (Settings)
			{
				bSavedUseCustomOverride = Settings->bUseCustomLuminosityFitOpacityCap;
				SavedValue = Settings->LuminosityFitOpacityCap;
				Settings->bUseCustomLuminosityFitOpacityCap = bEnableOverride;
				Settings->LuminosityFitOpacityCap = NewValue;
			}
		}

		~FLuminosityOpacityCapScope()
		{
			if (Settings)
			{
				Settings->bUseCustomLuminosityFitOpacityCap = bSavedUseCustomOverride;
				Settings->LuminosityFitOpacityCap = SavedValue;
			}
		}

		UDesign2UMGSettings* Settings = nullptr;
		bool bSavedUseCustomOverride = false;
		float SavedValue = 0.15f;
	};

}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaLuminosityImageOverlayTest,
	"Design2UMG.FigmaParser.LuminosityImageOverlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaLuminosityImageOverlayTest::RunTest(const FString& Parameters)
{
	FLuminosityOpacityCapScope LuminosityOpacityCapScope(0.15f);
	const FString Json = MakeLuminosityOverlayDocJson({
		TEXT("node_lum"), TEXT("LumCard"), 120, 80, TEXT("img_lum"), TEXT(",\"scalingFactor\":0.5")});

	FDesignDocument Doc;
	TestTrue(TEXT("Figma parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() == 0)
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& Node = Doc.Children[0];
	TestEqual(TEXT("Luminosity placeholder keeps both fills"), Node->Visual.Fills.Num(), 2);
	TestEqual(TEXT("Base fill remains color"), Node->Visual.Fills[0].Type, EDesignFillType::Color);
	TestEqual(TEXT("Overlay fill remains image"), Node->Visual.Fills[1].Type, EDesignFillType::Image);
	TestEqual(TEXT("Overlay image mode preserved"), Node->Visual.Fills[1].Image.Mode, EDesignImageMode::Fit);

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::FromSettings(UDesign2UMGSettings::Get()));
	const FUMGWidgetNode* Overlay = FindWidgetById(Data.RootWidget, TEXT("node_lum"));
	TestTrue(TEXT("Converted widget found"), Overlay != nullptr);
	if (!Overlay)
	{
		return false;
	}

	TestEqual(TEXT("Luminosity image converts to multi-fill overlay"), Overlay->Type, TEXT("Overlay"));
	TestEqual(TEXT("Leaf multi-fill overlay keeps only base + image layers"), Overlay->Children.Num(), 2);
	if (Overlay->Children.Num() != 2)
	{
		return false;
	}

	const FUMGWidgetNode& BaseLayer = Overlay->Children[0];
	const FUMGWidgetNode& FitLayer = Overlay->Children[1];
	TestEqual(TEXT("Base layer is border"), BaseLayer.Type, TEXT("Border"));
	TestEqual(TEXT("Fit layer is scale box"), FitLayer.Type, TEXT("ScaleBox"));
	TestTrue(TEXT("Fit layer has image child"), FitLayer.Children.Num() == 1);
	if (FitLayer.Children.Num() != 1)
	{
		return false;
	}

	const FUMGWidgetNode& ImageChild = FitLayer.Children[0];
	TestEqual(TEXT("Image child type"), ImageChild.Type, TEXT("Image"));
	TestEqual(TEXT("Image child keeps texture"), GetWidgetProperty(ImageChild, TEXT("TexturePath")), TEXT("img_lum"));
	TestEqual(TEXT("Fit layer stretch"), GetWidgetProperty(FitLayer, TEXT("Stretch")), TEXT("ScaleToFit"));
	TestFalse(TEXT("Luminosity fit layer should not pin synthetic pivot"), HasWidgetProperty(FitLayer, TEXT("RenderTransformPivot")));
	TestFalse(TEXT("Luminosity fit layer should not inject synthetic scale"), HasWidgetProperty(FitLayer, TEXT("RenderTransformScale")));
	TestFalse(TEXT("Luminosity fit layer should not inject synthetic translation"), HasWidgetProperty(FitLayer, TEXT("RenderTranslation")));
	TestTrue(TEXT("Luminosity fit image applies opacity fallback"),
		FMath::IsNearlyEqual(FCString::Atof(*GetWidgetProperty(ImageChild, TEXT("RenderOpacity"))), 0.15f, 0.0001f));
	TestFalse(TEXT("Non-square fit image child should not pin width override"), HasWidgetProperty(ImageChild, TEXT("WidthOverride")));
	TestFalse(TEXT("Non-square fit image child should not pin height override"), HasWidgetProperty(ImageChild, TEXT("HeightOverride")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaWideLuminosityImageOverlayTest,
	"Design2UMG.FigmaParser.WideLuminosityImageOverlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaWideLuminosityImageOverlayTest::RunTest(const FString& Parameters)
{
	FLuminosityOpacityCapScope LuminosityOpacityCapScope(0.15f);
	const FString Json = MakeLuminosityOverlayDocJson({
		TEXT("overlay_wide"), TEXT("OverlayWide"), 220, 120, TEXT("img_lum_wide"), TEXT(",\"scalingFactor\":0.5")});

	FDesignDocument Doc;
	TestTrue(TEXT("Wide luminosity parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() == 0)
	{
		return false;
	}

	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::FromSettings(UDesign2UMGSettings::Get()));
	const FUMGWidgetNode* Overlay = FindWidgetById(Data.RootWidget, TEXT("overlay_wide"));
	TestTrue(TEXT("Wide luminosity overlay widget exists"), Overlay != nullptr);
	if (!Overlay || Overlay->Children.Num() < 2)
	{
		return false;
	}

	const FUMGWidgetNode& FitLayer = Overlay->Children[1];
	TestEqual(TEXT("Wide fit layer is scale box"), FitLayer.Type, TEXT("ScaleBox"));
	TestTrue(TEXT("Wide fit layer has image child"), FitLayer.Children.Num() == 1);
	if (FitLayer.Children.Num() != 1)
	{
		return false;
	}

	const FUMGWidgetNode& ImageChild = FitLayer.Children[0];
	TestFalse(TEXT("Wide luminosity fit layer should not inject synthetic scale"), HasWidgetProperty(FitLayer, TEXT("RenderTransformScale")));
	TestFalse(TEXT("Wide luminosity fit layer should not inject synthetic translation"), HasWidgetProperty(FitLayer, TEXT("RenderTranslation")));
	TestTrue(TEXT("Wide luminosity image applies opacity fallback"),
		FMath::IsNearlyEqual(FCString::Atof(*GetWidgetProperty(ImageChild, TEXT("RenderOpacity"))), 0.15f, 0.0001f));
	TestFalse(TEXT("Wide luminosity image should not pin width override"), HasWidgetProperty(ImageChild, TEXT("WidthOverride")));
	TestFalse(TEXT("Wide luminosity image should not pin height override"), HasWidgetProperty(ImageChild, TEXT("HeightOverride")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaSquareLuminosityImageOverlayTest,
	"Design2UMG.FigmaParser.SquareLuminosityImageOverlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaSquareLuminosityImageOverlayTest::RunTest(const FString& Parameters)
{
	FLuminosityOpacityCapScope LuminosityOpacityCapScope(0.15f);
	const FString Json = MakeLuminosityOverlayDocJson({
		TEXT("overlay_square"), TEXT("OverlaySquare"), 120, 120, TEXT("img_lum_square"), TEXT(",\"imageTransform\":[[1,0,0],[0,1,0]]")});

	FDesignDocument Doc;
	TestTrue(TEXT("Square luminosity parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() == 0)
	{
		return false;
	}

	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::FromSettings(UDesign2UMGSettings::Get()));
	const FUMGWidgetNode* Overlay = FindWidgetById(Data.RootWidget, TEXT("overlay_square"));
	TestTrue(TEXT("Square luminosity overlay widget exists"), Overlay != nullptr);
	if (!Overlay)
	{
		return false;
	}

	TestEqual(TEXT("Square luminosity image converts to overlay"), Overlay->Type, TEXT("Overlay"));
	TestTrue(TEXT("Square luminosity overlay keeps image layer"), Overlay->Children.Num() >= 2);
	if (Overlay->Children.Num() < 2)
	{
		return false;
	}

	const FUMGWidgetNode& FitLayer = Overlay->Children[1];
	TestEqual(TEXT("Square fit layer is scale box"), FitLayer.Type, TEXT("ScaleBox"));
	TestTrue(TEXT("Square fit layer has image child"), FitLayer.Children.Num() == 1);
	if (FitLayer.Children.Num() != 1)
	{
		return false;
	}

	const FUMGWidgetNode& ImageChild = FitLayer.Children[0];
	TestEqual(TEXT("Square image child keeps texture"), GetWidgetProperty(ImageChild, TEXT("TexturePath")), TEXT("img_lum_square"));
	TestEqual(TEXT("Square image child pins width"), GetWidgetProperty(ImageChild, TEXT("WidthOverride")), TEXT("120.0"));
	TestEqual(TEXT("Square image child pins height"), GetWidgetProperty(ImageChild, TEXT("HeightOverride")), TEXT("120.0"));
	TestTrue(TEXT("Square image child applies opacity fallback"),
		FMath::IsNearlyEqual(FCString::Atof(*GetWidgetProperty(ImageChild, TEXT("RenderOpacity"))), 0.15f, 0.0001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaLuminosityIdentityTransformOverlayTest,
	"Design2UMG.FigmaParser.LuminosityIdentityTransformOverlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaLuminosityIdentityTransformOverlayTest::RunTest(const FString& Parameters)
{
	FLuminosityOpacityCapScope LuminosityOpacityCapScope(0.15f);
	const FString Json = MakeLuminosityOverlayDocJson({
		TEXT("overlay_id"), TEXT("OverlayIdentity"), 120, 80, TEXT("img_lum_id"), TEXT(",\"imageTransform\":[[1,0,0],[0,1,0]]")});

	FDesignDocument Doc;
	TestTrue(TEXT("Figma parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() == 0)
	{
		return false;
	}

	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::FromSettings(UDesign2UMGSettings::Get()));
	const FUMGWidgetNode* Overlay = FindWidgetById(Data.RootWidget, TEXT("overlay_id"));
	TestTrue(TEXT("Identity transform overlay widget exists"), Overlay != nullptr);
	if (!Overlay)
	{
		return false;
	}

	TestEqual(TEXT("Identity transform luminosity image converts to overlay"), Overlay->Type, TEXT("Overlay"));
	TestTrue(TEXT("Identity transform overlay keeps image layer"), Overlay->Children.Num() >= 2);
	if (Overlay->Children.Num() < 2 || Overlay->Children[1].Children.Num() != 1)
	{
		return false;
	}
	const FUMGWidgetNode& FitLayer = Overlay->Children[1];
	const FUMGWidgetNode& ImageChild = FitLayer.Children[0];
	TestFalse(TEXT("Identity transform overlay should not inject synthetic scale"), HasWidgetProperty(FitLayer, TEXT("RenderTransformScale")));
	TestFalse(TEXT("Identity transform overlay should not inject synthetic translation"), HasWidgetProperty(FitLayer, TEXT("RenderTranslation")));
	TestTrue(TEXT("Identity transform overlay applies opacity fallback"),
		FMath::IsNearlyEqual(FCString::Atof(*GetWidgetProperty(ImageChild, TEXT("RenderOpacity"))), 0.15f, 0.0001f));
	return true;
}

#endif

