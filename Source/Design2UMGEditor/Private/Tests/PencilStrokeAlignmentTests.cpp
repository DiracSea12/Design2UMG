// Copyright (c) 2026, Diracsea12. SPDX-License-Identifier: MIT
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "PenParser.h"
#include "Design2UMGConverter.h"
#include "DesignPathRasterizer.h"
#include "UMGJsonParser.h"
#include "UMGJsonSerializer.h"
#include "Engine/Texture2D.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPencilStrokeAlignmentTest, "Design2UMG.Pencil.Stroke.Alignment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPencilStrokeAlignmentTest::RunTest(const FString& Parameters)
{
	FDesignDocument Document;
	TestTrue(TEXT("Pencil center default parses"), FPenParser::ParseDocument(TEXT(R"({"version":"2.21","children":[{"id":"path","type":"path","width":16,"height":16,"geometry":"M0 0L16 0L16 16L0 16Z","stroke":"#ffffff","strokeWidth":4}]})"), Document));
	if (Document.Children.Num() != 1) return false;
	TestEqual(TEXT("Pencil default is center"), Document.Children[0]->Visual.Stroke.Align, EDesignStrokeAlign::Center);
	TestEqual(TEXT("Shared/Figma default remains unchanged"), FDesignStroke().Align, EDesignStrokeAlign::Inside);
	const auto Converted = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestEqual(TEXT("Alignment reaches native path payload"), Converted.RootWidget.Path.StrokeAlignment.Get(FString()), FString(TEXT("center")));
	FUMGWidgetData Data;
	Data.RootWidget = Converted.RootWidget;
	Data.RootWidget.Path.StrokeAlignment = TEXT("outer");
	FUMGWidgetData RoundTrip; FString Error;
	if (!TestTrue(TEXT("Alignment JSON round-trip"), FUMGJsonParser::ParseJson(FUMGJsonSerializer::Serialize(Data), RoundTrip, &Error))) return false;
	TestEqual(TEXT("Outer survives JSON"), RoundTrip.RootWidget.Path.StrokeAlignment.Get(FString()), FString(TEXT("outer")));
	FDesignPathRasterRequest Outer;
	if (!TestTrue(TEXT("Outer request builds"), FDesignPathRasterizer::TryBuildRequest(RoundTrip.RootWidget, Outer))) return false;
	const int32 SS = DesignPathRasterDefaults::Supersample;
	TestEqual(TEXT("Original paint width retained"), Outer.PaintSize.X, 16 * SS);
	TestEqual(TEXT("Outer canvas expands by stroke width on both sides"), Outer.Size.X, 24 * SS);
	FUMGWidgetNode Painted = RoundTrip.RootWidget;
	Painted.Brush.Gradient.Type = TEXT("Linear");
	Painted.Brush.Gradient.Stops = {{FLinearColor::Blue, 0.0f}, {FLinearColor::Green, 1.0f}};
	Painted.Path.StrokeGradient.Type = TEXT("Linear");
	Painted.Path.StrokeGradient.Stops = {{FLinearColor::Red, 0.0f}, {FLinearColor::Yellow, 1.0f}};
	Painted.Path.FillColor = FLinearColor::Blue;
	FDesignPathRasterRequest PaintedRequest;
	TestTrue(TEXT("Separate gradient paints build"), FDesignPathRasterizer::TryBuildRequest(Painted, PaintedRequest));
	TestTrue(TEXT("Fill mask remains neutral when a stroke paint is also present"), PaintedRequest.Path.FillColor.IsSet() && PaintedRequest.Path.FillColor.GetValue() == FLinearColor::White);
	FUMGWidgetNode Expanded = RoundTrip.RootWidget;
	FDesignPathRasterizer::ApplyCanvasTransform(Expanded, Outer);
	TestTrue(TEXT("Expanded ink leaves the layout box untouched"), Expanded.Bounds.W == RoundTrip.RootWidget.Bounds.W && Expanded.WidgetStyle.WidthOverride == RoundTrip.RootWidget.WidgetStyle.WidthOverride);
	TestTrue(TEXT("Only rendering canvas grows"), Expanded.Render.RenderTransformScale.Get(FVector2D(1, 1)).Equals(FVector2D(1.5, 1.5)));
	FUMGWidgetNode Open = RoundTrip.RootWidget;
	Open.Path.Geometry = TEXT("M0 0L16 0L16 16Z M4 4L12 4");
	FDesignPathRasterRequest OpenRequest;
	TestTrue(TEXT("Mixed contour request builds"), FDesignPathRasterizer::TryBuildRequest(Open, OpenRequest));
	TestEqual(TEXT("Any open contour forces the whole stroke to center"), OpenRequest.Path.StrokeAlignment.Get(FString()), FString(TEXT("center")));
	Open.Path.Geometry = TEXT("M0 0L16 0L0 0");
	TestTrue(TEXT("Unclosed return to start parses"), FDesignPathRasterizer::TryBuildRequest(Open, OpenRequest));
	TestEqual(TEXT("Coincident endpoints are not an explicit close"), OpenRequest.Path.StrokeAlignment.Get(FString()), FString(TEXT("center")));
	FUMGWidgetNode InnerNode = RoundTrip.RootWidget; InnerNode.Path.StrokeAlignment = TEXT("inner");
	FDesignPathRasterRequest Inner;
	if (!TestTrue(TEXT("Inner request builds"), FDesignPathRasterizer::TryBuildRequest(InnerNode, Inner))) return false;
	TestEqual(TEXT("Inner does not expand the canvas"), Inner.Size.X, Inner.PaintSize.X);
	FDesignPathRasterizer::ClearCache();
	UTexture2D* InnerTexture = FDesignPathRasterizer::Rasterize(Inner, &Error);
	UTexture2D* OuterTexture = FDesignPathRasterizer::Rasterize(Outer, &Error);
	UTexture2D* SourceImage = UTexture2D::CreateTransient(2, 2, PF_B8G8R8A8);
	if (TestNotNull(TEXT("Own image fixture exists"), SourceImage))
	{
		auto* SourcePixels = static_cast<FColor*>(SourceImage->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
		for (int32 I = 0; I < 4; ++I) SourcePixels[I] = FColor::White;
		SourceImage->GetPlatformData()->Mips[0].BulkData.Unlock();
		FDesignPathRasterRequest ImageRequest = Outer;
		ImageRequest.Path.FillColor = FLinearColor::White;
		ImageRequest.FillImage = FUMGVisualBrushData::FImageData(); ImageRequest.FillTexture = SourceImage;
		UTexture2D* ExpandedImage = FDesignPathRasterizer::Rasterize(ImageRequest, &Error);
		if (TestNotNull(TEXT("Expanded image mask exists"), ExpandedImage))
			TestTrue(TEXT("Expanded output owns a matching mip, not the smaller source paint mip"), ExpandedImage->GetSizeX() == Outer.Size.X && ExpandedImage->GetSizeY() == Outer.Size.Y);
	}
	if (TestNotNull(TEXT("Inner raster exists"), InnerTexture) && TestNotNull(TEXT("Outer raster exists"), OuterTexture))
	{
		TestTrue(TEXT("Different alignment requests do not share a cached texture"), InnerTexture != OuterTexture);
		FDesignPathRasterizer::FlushPendingTextureUpdates();
		const auto* Inside = static_cast<const FColor*>(InnerTexture->GetPlatformData()->Mips[0].BulkData.LockReadOnly());
		TestTrue(TEXT("Inner stroke retains its interior band"), Inside[(8 * SS) * Inner.Size.X + 2 * SS].A > 0);
		InnerTexture->GetPlatformData()->Mips[0].BulkData.Unlock();
		const auto* Outside = static_cast<const FColor*>(OuterTexture->GetPlatformData()->Mips[0].BulkData.LockReadOnly());
		TestEqual(TEXT("Outer stroke excludes the interior band"), Outside[(12 * SS) * Outer.Size.X + 6 * SS].A, uint8(0));
		TestTrue(TEXT("Outer stroke retains ink beyond the original box"), Outside[(12 * SS) * Outer.Size.X + 2 * SS].A > 0);
		OuterTexture->GetPlatformData()->Mips[0].BulkData.Unlock();
	}
	FDesignPathRasterizer::ClearCache();
	// Ink coordinates below belong to the authored canvas, not fitted SVG bounds.
	FUMGWidgetNode Corner = RoundTrip.RootWidget;
	Corner.Path.FitGeometryToBounds = false;
	Corner.Path.StrokeAlignment = TEXT("center");
	Corner.Path.CanvasSize = FVector2D(16, 20);
	Corner.Path.Geometry = TEXT("M0 20L8 0L16 20Z");
	Corner.Path.StrokeJoin = TEXT("miter");
	FDesignPathRasterRequest Miter, Bevel;
	if (!TestTrue(TEXT("Acute miter request builds"), FDesignPathRasterizer::TryBuildRequest(Corner, Miter))) return false;
	TestFalse(TEXT("Authored-coordinate fixture keeps its original paint mapping"), Miter.Path.FitGeometryToBounds.Get(true));
	TestEqual(TEXT("Authored paint canvas remains 16 by 20"), Miter.PaintSize, FIntPoint(16 * SS, 20 * SS));
	Corner.Path.StrokeJoin = TEXT("bevel");
	if (!TestTrue(TEXT("Bevel request builds"), FDesignPathRasterizer::TryBuildRequest(Corner, Bevel))) return false;
	TestTrue(TEXT("Actual miter tip extends past half-width padding"), Miter.PaintOffset.Y > FMath::CeilToInt(Corner.Path.StrokeThickness.GetValue() * SS / 2));
	TestTrue(TEXT("Bevel does not reserve the removed miter tip"), Bevel.PaintOffset.Y < Miter.PaintOffset.Y);
	UTexture2D* MiterTexture = FDesignPathRasterizer::Rasterize(Miter, &Error);
	if (TestNotNull(TEXT("Miter raster exists"), MiterTexture))
	{
		FDesignPathRasterizer::FlushPendingTextureUpdates();
		const auto* Ink = static_cast<const FColor*>(MiterTexture->GetPlatformData()->Mips[0].BulkData.LockReadOnly());
		const int32 TipIndex = (Miter.PaintOffset.Y - 3 * SS) * Miter.Size.X + Miter.PaintOffset.X + 8 * SS;
		TestTrue(TEXT("Miter ink survives beyond the old half-width canvas"), Ink[TipIndex].A > 0);
		MiterTexture->GetPlatformData()->Mips[0].BulkData.Unlock();
	}
	FDesignPathRasterizer::ClearCache();
	Corner.Path.Geometry = TEXT("M6 20L8 0L10 20Z");
	Corner.Path.StrokeJoin = TEXT("miter");
	if (!TestTrue(TEXT("Over-limit miter request builds"), FDesignPathRasterizer::TryBuildRequest(Corner, Miter))) return false;
	Corner.Path.StrokeJoin = TEXT("bevel");
	if (!TestTrue(TEXT("Matching bevel request builds"), FDesignPathRasterizer::TryBuildRequest(Corner, Bevel))) return false;
	TestEqual(TEXT("Official default miter limit bevels an excessively sharp tip"), Miter.PaintOffset.Y, Bevel.PaintOffset.Y);
	Corner = RoundTrip.RootWidget;
	Corner.Path.FitGeometryToBounds = false;
	Corner.Path.StrokeAlignment = TEXT("center");
	Corner.Path.Geometry = TEXT("M4 4L15 4L15 15L4 15Z");
	FDesignPathRasterRequest Asymmetric;
	if (!TestTrue(TEXT("One-sided ink expansion builds"), FDesignPathRasterizer::TryBuildRequest(Corner, Asymmetric))) return false;
	TestEqual(TEXT("Right/bottom-only expansion keeps the drawing quad origin"), Asymmetric.CanvasBounds.Min, FIntPoint::ZeroValue);
	TestTrue(TEXT("Zero origin shift still has an expanded canvas"), Asymmetric.HasExpandedCanvas());
	FUMGWidgetNode ShaderInk = Corner;
	ShaderInk.Brush.Shader = FDesignShaderData();
	ShaderInk.Brush.Shader->Size = FVector2D(16, 16);
	FDesignPathRasterizer::ApplyCanvasTransform(ShaderInk, Asymmetric);
	const FVector2D SourceUnits = FVector2D(16, 16) / FVector2D(Asymmetric.PaintSize);
	TestTrue(TEXT("Expanded shader ink preserves its source resolution and asymmetric original paint coordinates"),
		ShaderInk.Brush.Shader->Size.Equals(FVector2D(16, 16)) &&
		ShaderInk.Brush.Shader->CaptureInsets.Equals(FVector4(0, 0,
			(Asymmetric.CanvasBounds.Width() - Asymmetric.PaintSize.X) * SourceUnits.X,
			(Asymmetric.CanvasBounds.Height() - Asymmetric.PaintSize.Y) * SourceUnits.Y)));

	FUMGWidgetNode ZeroOrigin = Corner;
	ZeroOrigin.Render.RenderTransformPivot = FVector2D::ZeroVector;
	ZeroOrigin.Render.RenderTransformScale = FVector2D(-1, 1);
	ZeroOrigin.Render.RenderAngle = 45.0f;
	FDesignPathRasterizer::ApplyCanvasTransform(ZeroOrigin, Asymmetric);
	TestTrue(TEXT("Expanded ink preserves a local-zero flip and rotation origin"), ZeroOrigin.Render.RenderTransformPivot.Get(FVector2D(0.5, 0.5)).IsNearlyZero() && ZeroOrigin.Render.RenderTranslation.Get(FVector2D::ZeroVector).IsNearlyZero());
	Corner.Render.RenderTransformPivot = FVector2D(0.25, 0.75);
	Corner.Render.RenderAngle = 90.0f;
	FDesignPathRasterizer::ApplyCanvasTransform(Corner, Asymmetric);
	const FVector2D Growth = FVector2D(Asymmetric.CanvasBounds.Size() - Asymmetric.PaintSize) / FVector2D(Asymmetric.PaintSize);
	const FVector2D LocalCorrection = Growth * FVector2D(0.25, 0.75) * FVector2D(Corner.Bounds.W, Corner.Bounds.H);
	TestTrue(TEXT("Asymmetric expansion preserves the authored paint origin through pivot and rotation"), Corner.Render.RenderTranslation.Get(FVector2D::ZeroVector).Equals(FVector2D(-LocalCorrection.Y, LocalCorrection.X), 0.001));
	TestEqual(TEXT("Ink expansion does not resize the layout slot"), Corner.Bounds.W, RoundTrip.RootWidget.Bounds.W);
	return true;
}
#endif
