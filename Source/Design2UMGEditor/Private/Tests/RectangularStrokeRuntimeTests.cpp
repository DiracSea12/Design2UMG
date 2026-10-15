// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Misc/AutomationTest.h"
#include "Design2UMGImageTestUtils.h"
#include "PenParser.h"
#include "Design2UMGConverter.h"
#include "UMGGenerator.h"
#include "UMGWidgetRenderer.h"
#include "WidgetBlueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ImageUtils.h"
#include "ImageCore.h"
#include "Blueprint/UserWidget.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Slate/WidgetRenderer.h"
#include "RenderingThread.h"
#include "UObject/StrongObjectPtr.h"
#include "Components/Border.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/Texture2D.h"
#include "UObject/UObjectGlobals.h"
#include "TextureResource.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCanvasStrokeContainerTest,
	"Design2UMG.Converter.CanvasStrokeContainer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCanvasStrokeContainerTest::RunTest(const FString&)
{
	for (const float Thickness : {1.f, 1.5f, 3.f})
	{
		FDesignDocument Document;
		const FString Json = FString::Printf(TEXT(R"JSON({"children":[{"id":"root","type":"frame","width":100,"height":80,"layout":"none","fill":"#182030","children":[{"id":"panel","type":"frame","x":12,"y":15,"width":60,"height":40,"layout":"none","stroke":"#FF804080","strokeWidth":%g,"strokeAlignment":"inner","children":[{"id":"content","type":"rectangle","x":10,"y":8,"width":30,"height":20,"fill":"#3050FF"},{"id":"overlap","type":"rectangle","x":0,"y":0,"width":20,"height":12,"fill":"#50FF8080"}]}]}]})JSON"), Thickness);
		if (!TestTrue(TEXT("Parse canvas stroke fixture"), FPenParser::ParseDocument(Json, Document))) return false;
		const FUMGWidgetData Compact = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
		FUMGWidgetData Oracle = Compact;
		bool bExpanded = false;
		TFunction<void(FUMGWidgetNode&)> Expand = [&](FUMGWidgetNode& Node)
		{
			if (Node.Id != TEXT("panel"))
			{
				for (FUMGWidgetNode& Child : Node.Children) Expand(Child);
				return;
			}
			TestEqual(TEXT("Fixed unfilled frame reuses its canvas"), Node.Type, FString(TEXT("CanvasPanel")));
			FUMGWidgetNode Content;
			Content.Type = TEXT("CanvasPanel");
			Content.Id = Content.Name = TEXT("IndependentContentCanvas");
			TArray<FUMGWidgetNode> Strokes;
			for (FUMGWidgetNode& Child : Node.Children)
			{
				if (!Child.Id.StartsWith(TEXT("panel_stroke")))
				{
					Content.Children.Add(MoveTemp(Child));
					continue;
				}
				Child.Slot = FUMGSlotData();
				if (Child.Id.EndsWith(TEXT("_top"))) Child.Slot.VerticalAlignment = EUMGSlotVerticalAlignment::Top;
				if (Child.Id.EndsWith(TEXT("_bottom"))) Child.Slot.VerticalAlignment = EUMGSlotVerticalAlignment::Bottom;
				if (Child.Id.EndsWith(TEXT("_left"))) Child.Slot.HorizontalAlignment = EUMGSlotHorizontalAlignment::Left;
				if (Child.Id.EndsWith(TEXT("_right"))) Child.Slot.HorizontalAlignment = EUMGSlotHorizontalAlignment::Right;
				Strokes.Add(MoveTemp(Child));
			}
			Node.Type = TEXT("Overlay");
			Node.Children.Reset();
			Node.Children.Add(MoveTemp(Content));
			Node.Children.Append(MoveTemp(Strokes));
			bExpanded = true;
		};
		Expand(Oracle.RootWidget);
		if (!TestTrue(TEXT("Independent two-container oracle constructed"), bExpanded)) return false;
		auto Build = [this](const FUMGWidgetData& Data) -> UWidgetBlueprint*
		{
			UWidgetBlueprint* BP = FUMGGenerator::BuildWidgetBlueprint(Data,
				TEXT("/Game/Design2UMG/Automation/CanvasStroke/WBP_") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
			if (TestNotNull(TEXT("Build canvas stroke asset"), BP)) FKismetEditorUtilities::CompileBlueprint(BP);
			return BP;
		};
		UWidgetBlueprint* ActualBP = Build(Compact);
		UWidgetBlueprint* OracleBP = Build(Oracle);
		if (!ActualBP || !OracleBP) return false;
		auto Render = [this](UWidgetBlueprint* BP, float Scale, float Opacity, TArray<FColor>& Pixels)
		{
			TStrongObjectPtr<UUserWidget> Instance(NewObject<UUserWidget>(GetTransientPackage(), BP->GeneratedClass));
			if (!TestTrue(TEXT("Initialize canvas stroke instance"), Instance->Initialize())) return false;
			Instance->SetRenderOpacity(Opacity);
			const FVector2D Size(100 * Scale, 80 * Scale);
			TStrongObjectPtr<UTextureRenderTarget2D> Target(FWidgetRenderer::CreateTargetFor(Size, TF_Nearest, true));
			FWidgetRenderer Renderer(true);
			Renderer.DrawWidget(Target.Get(), Instance->TakeWidget(), Scale, Size, 0.f);
			FlushRenderingCommands();
			const bool bRead = Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
			Instance->ReleaseSlateResources(true);
			return TestTrue(TEXT("Read canvas stroke pixels"), bRead);
		};
		for (const float Scale : {1.f, 1.25f, 1.5f, 2.f})
		{
			for (const float Opacity : {1.f, 0.5f})
			{
				TArray<FColor> Actual, Expected;
				if (!Render(ActualBP, Scale, Opacity, Actual) || !Render(OracleBP, Scale, Opacity, Expected)) return false;
				TestTrue(FString::Printf(TEXT("Container folding preserves pixels: stroke=%g scale=%g opacity=%g"), Thickness, Scale, Opacity), Actual == Expected);
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSingleFlowPositionElisionTest,
	"Design2UMG.Converter.SingleFlowPositionElision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSingleFlowPositionElisionTest::RunTest(const FString&)
{
	for (const FString Layout : {FString(TEXT("horizontal")), FString(TEXT("vertical"))})
	{
		FString Positioned = FString::Printf(TEXT(R"JSON({"children":[{"id":"root","type":"frame","width":100,"height":80,"layout":"none","fill":"#182030","children":[{"id":"panel","type":"frame","x":12,"y":15,"width":60,"height":40,"layout":"%s","fill":"#506070","children":[{"id":"leaf","type":"rectangle","x":9,"y":7,"width":20,"height":12,"fill":"#FF8050"}]}]}]})JSON"), *Layout);
		Positioned.ReplaceInline(Layout == TEXT("horizontal") ? TEXT("\"width\":20") : TEXT("\"height\":12"),
			Layout == TEXT("horizontal") ? TEXT("\"width\":\"fill_container\"") : TEXT("\"height\":\"fill_container\""));
		FString Unpositioned = Positioned;
		Unpositioned.ReplaceInline(TEXT("\"x\":9,\"y\":7,"), TEXT(""));
		TArray<FImage> Images;
		for (const FString& Json : {Positioned, Unpositioned})
		{
			FDesignDocument Document;
			if (!TestTrue(TEXT("Parse single flow fixture"), FPenParser::ParseDocument(Json, Document))) return false;
			const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
			int32 RedundantFlowPanels = 0;
			TFunction<void(const FUMGWidgetNode&)> Count = [&](const FUMGWidgetNode& Node)
			{
				if (Node.Id == TEXT("panel_inner") && (Node.Type == TEXT("HorizontalBox") || Node.Type == TEXT("VerticalBox"))) ++RedundantFlowPanels;
				for (const FUMGWidgetNode& Child : Node.Children) Count(Child);
			};
			Count(Data.RootWidget);
			TestEqual(TEXT("Ignored flow coordinates do not retain a redundant one-child panel"), RedundantFlowPanels, 0);
			UWidgetBlueprint* BP = FUMGGenerator::BuildWidgetBlueprint(Data,
				TEXT("/Game/Design2UMG/Automation/SingleFlow/WBP_") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
			if (!TestNotNull(TEXT("Build single flow blueprint"), BP)) return false;
			FKismetEditorUtilities::CompileBlueprint(BP);
			TArray<uint8> Png;
			FIntPoint Size;
			FString Error;
			if (!TestTrue(TEXT("Render single flow blueprint"), FUMGWidgetRenderer::RenderWidgetToPng(BP, Png, Size, &Error))) return false;
			FImage& Image = Images.AddDefaulted_GetRef();
			if (!TestTrue(TEXT("Decode single flow render"), Design2UMGImageTestUtils::DecodeToBGRA8(Png.GetData(), Png.Num(), Image))) return false;
		}
		TestTrue(TEXT("Ignored coordinates preserve every pixel after flow promotion"), Images[0].RawData == Images[1].RawData);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSingleAutoFlowPositionElisionTest,
	"Design2UMG.Converter.SingleAutoFlowPositionElision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSingleAutoFlowPositionElisionTest::RunTest(const FString&)
{
	for (const bool bHorizontal : {false, true})
	for (const TCHAR* Justify : {TEXT("start"), TEXT("center"), TEXT("end"), TEXT("space_around")})
	for (const bool bOverflow : {false, true})
	{
		// Compare panel elision at zero and nonzero coordinates, including fixed overflow.
		const FString Positioned = FString::Printf(TEXT(R"JSON({"children":[{"id":"root","type":"frame","width":120,"height":100,"layout":"none","fill":"#182030","children":[{"id":"panel","type":"frame","x":20,"y":20,"width":60,"height":40,"layout":"%s","justifyContent":"%s","alignItems":"center","fill":"#506070","children":[{"id":"leaf","type":"rectangle","x":9,"y":7,"width":%d,"height":%d,"fill":"#FF8050"}]}]}]})JSON"),
			bHorizontal ? TEXT("horizontal") : TEXT("vertical"), Justify,
			bHorizontal && bOverflow ? 80 : 20, !bHorizontal && bOverflow ? 60 : 12);
		FString ZeroPositioned = Positioned;
		ZeroPositioned.ReplaceInline(TEXT("\"x\":9,\"y\":7,"), TEXT("\"x\":0,\"y\":0,"));
		TArray<FImage> Images;
		for (const FString& Json : {Positioned, ZeroPositioned})
		{
			FDesignDocument Document;
			if (!TestTrue(TEXT("Parse fixed-child flow fixture"), FPenParser::ParseDocument(Json, Document))) return false;
			FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
			int32 RedundantPanels = 0;
			TFunction<void(const FUMGWidgetNode&)> Count = [&](const FUMGWidgetNode& Node)
			{
				if (Node.Id == TEXT("panel_inner") && (Node.Type == TEXT("HorizontalBox") || Node.Type == TEXT("VerticalBox"))) ++RedundantPanels;
				for (const FUMGWidgetNode& Child : Node.Children) Count(Child);
			};
			Count(Data.RootWidget);
			TestEqual(TEXT("Retain flow only when its overflow allocation is needed"), RedundantPanels, bOverflow ? 1 : 0);
			if (Images.Num() == 1)
			{
				// Independent pre-elision oracle: one Auto child in a box panel,
				// with the original weighted spacers, rather than Border alignment.
				TFunction<void(FUMGWidgetNode&)> RestoreFlow = [&](FUMGWidgetNode& Node)
				{
					if (Node.Id == TEXT("panel") && Node.Type == TEXT("Border") && Node.Children.Num() == 1
						&& Node.Children[0].Type != TEXT("HorizontalBox") && Node.Children[0].Type != TEXT("VerticalBox"))
					{
						FUMGWidgetNode Flow;
						Flow.Type = bHorizontal ? TEXT("HorizontalBox") : TEXT("VerticalBox");
						Flow.Name = TEXT("oracle_flow");
						Flow.Id = TEXT("panel_inner");
						Flow.Slot.HorizontalAlignment = EUMGSlotHorizontalAlignment::Fill;
						Flow.Slot.VerticalAlignment = EUMGSlotVerticalAlignment::Fill;
						FUMGWidgetNode Spacer;
						Spacer.Type = TEXT("Spacer");
						Spacer.Slot.SizeRule = EUMGSlotSizeRule::Fill;
						if (FCString::Strcmp(Justify, TEXT("start")) != 0) Flow.Children.Add(Spacer);
						Flow.Children.Add(MoveTemp(Node.Children[0]));
						if (FCString::Strcmp(Justify, TEXT("center")) == 0 || FCString::Strcmp(Justify, TEXT("space_around")) == 0) Flow.Children.Add(Spacer);
						Node.Children.Reset();
						Node.Children.Add(MoveTemp(Flow));
						return;
					}
					for (FUMGWidgetNode& Child : Node.Children) RestoreFlow(Child);
				};
				RestoreFlow(Data.RootWidget);
				RedundantPanels = 0;
				Count(Data.RootWidget);
				TestEqual(TEXT("Oracle retains exactly one original flow panel"), RedundantPanels, 1);
			}
			UWidgetBlueprint* BP = FUMGGenerator::BuildWidgetBlueprint(Data,
				TEXT("/Game/Design2UMG/Automation/SingleAutoFlow/WBP_") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
			if (!TestNotNull(TEXT("Build fixed-child flow blueprint"), BP)) return false;
			FKismetEditorUtilities::CompileBlueprint(BP);
			TArray<uint8> Png;
			FIntPoint Size;
			FString Error;
			if (!TestTrue(TEXT("Render fixed-child flow blueprint"), FUMGWidgetRenderer::RenderWidgetToPng(BP, Png, Size, &Error))) return false;
			FImage& Image = Images.AddDefaulted_GetRef();
			if (!TestTrue(TEXT("Decode fixed-child flow render"), Design2UMGImageTestUtils::DecodeToBGRA8(Png.GetData(), Png.Num(), Image))) return false;
		}
		TestTrue(TEXT("Elision preserves normal and overflowing pixels against the original flow"), Images[0].RawData == Images[1].RawData);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCanvasResolvedSizeElisionTest,
	"Design2UMG.Converter.CanvasResolvedSizeElision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCanvasResolvedSizeElisionTest::RunTest(const FString&)
{
	for (const bool bFitWidth : {false, true})
	for (const bool bWrappingText : {false, true})
	{
		const TCHAR* Content = bWrappingText
			? TEXT(R"JSON({"id":"leaf","type":"text","width":40,"textGrowth":"fixed-width","fontSize":12,"content":"Wrapping content on several lines","fill":"#FF8050"})JSON")
			: TEXT(R"JSON({"id":"leaf","type":"rectangle","width":20,"height":12,"fill":"#FF8050"})JSON");
		const FString Json = FString::Printf(TEXT(R"JSON({"children":[{"id":"root","type":"frame","width":200,"height":200,"layout":"none","fill":"#182030","children":[{"id":"panel","type":"frame","x":12,"y":15,"width":%s,"height":%s,"layout":"vertical","opacity":0.6,"clip":true,"fill":"#506070","children":[%s]}]}]})JSON"),
			bFitWidth ? TEXT("\"fit_content\"") : TEXT("64"), bFitWidth ? TEXT("80") : TEXT("\"fit_content\""), Content);
		FDesignDocument Document;
		if (!TestTrue(TEXT("Parse resolved canvas fixture"), FPenParser::ParseDocument(Json, Document))) return false;
		const FUMGWidgetData Compact = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
		int32 OuterSizeBoxes = 0;
		TFunction<void(const FUMGWidgetNode&)> Count = [&](const FUMGWidgetNode& Node)
		{
			if (Node.Id == TEXT("panel_sz") && Node.Type == TEXT("SizeBox")) ++OuterSizeBoxes;
			for (const FUMGWidgetNode& Child : Node.Children) Count(Child);
		};
		Count(Compact.RootWidget);
		TestEqual(TEXT("A fixed canvas allocation does not need another desired-size wrapper"), OuterSizeBoxes, 0);
		FUMGWidgetData Oracle = Compact;
		bool bRestored = false;
		TFunction<void(FUMGWidgetNode&)> RestoreSizeBox = [&](FUMGWidgetNode& Node)
		{
			if (Node.Id != TEXT("panel"))
			{
				for (FUMGWidgetNode& Child : Node.Children) RestoreSizeBox(Child);
				return;
			}
			TestFalse(TEXT("Canvas slot owns the resolved size"), Node.Slot.bAutoSize);
			FUMGWidgetNode Wrapper;
			Wrapper.Type = TEXT("SizeBox");
			Wrapper.Name = Wrapper.Id = TEXT("oracle_size");
			Wrapper.Bounds = Node.Bounds;
			Wrapper.Slot = Node.Slot;
			if (bFitWidth && bWrappingText) Wrapper.WidgetStyle.MinDesiredWidth = Node.Slot.Offsets.Right;
			else Wrapper.WidgetStyle.WidthOverride = Node.Slot.Offsets.Right;
			if (!bFitWidth && bWrappingText) Wrapper.WidgetStyle.MinDesiredHeight = Node.Slot.Offsets.Bottom;
			else Wrapper.WidgetStyle.HeightOverride = Node.Slot.Offsets.Bottom;
			Node.Slot = FUMGSlotData();
			Wrapper.Children.Add(MoveTemp(Node));
			Node = MoveTemp(Wrapper);
			bRestored = true;
		};
		RestoreSizeBox(Oracle.RootWidget);
		if (!TestTrue(TEXT("Restore an independent desired-size wrapper"), bRestored)) return false;
		TArray<TArray<FColor>> Images;
		const FUMGWidgetData* Variants[] = {&Compact, &Oracle};
		for (const FUMGWidgetData* Data : Variants)
		{
			UWidgetBlueprint* BP = FUMGGenerator::BuildWidgetBlueprint(*Data,
				TEXT("/Game/Design2UMG/Automation/CanvasResolvedSize/WBP_") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
			if (!TestNotNull(TEXT("Build resolved-size fixture"), BP)) return false;
			FKismetEditorUtilities::CompileBlueprint(BP);
			TStrongObjectPtr<UUserWidget> Instance(NewObject<UUserWidget>(GetTransientPackage(), BP->GeneratedClass));
			if (!TestTrue(TEXT("Initialize resolved-size fixture"), Instance->Initialize())) return false;
			for (const FVector2D Size : {FVector2D(200, 200), FVector2D(50, 30)})
			{
				TStrongObjectPtr<UTextureRenderTarget2D> Target(FWidgetRenderer::CreateTargetFor(Size, TF_Nearest, true));
				FWidgetRenderer Renderer(true);
				Renderer.DrawWidget(Target.Get(), Instance->TakeWidget(), Size, 0.f);
				FlushRenderingCommands();
				if (!TestTrue(TEXT("Read resolved-size pixels"), Target->GameThread_GetRenderTargetResource()->ReadPixels(Images.AddDefaulted_GetRef()))) return false;
			}
			Instance->ReleaseSlateResources(true);
		}
		TestTrue(TEXT("Resolved-size elision preserves all normal pixels"), Images[0] == Images[2]);
		TestTrue(TEXT("Resolved-size elision preserves all undersized pixels"), Images[1] == Images[3]);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrokeSizeConstraintPromotionTest,
	"Design2UMG.Converter.StrokeSizeConstraintPromotion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FStrokeSizeConstraintPromotionTest::RunTest(const FString&)
{
	const FString Json = TEXT(R"JSON({"children":[{"id":"root","type":"frame","width":100,"height":80,"layout":"none","fill":"#182030","children":[{"id":"panel","type":"frame","x":12,"y":15,"width":60,"height":40,"layout":"horizontal","stroke":"#FF804080","strokeWidth":2,"strokeAlignment":"inner","children":[{"id":"leaf","type":"rectangle","width":20,"height":12,"fill":"#3050FF"}]}]}]})JSON");
	FDesignDocument Document;
	if (!TestTrue(TEXT("Parse constrained stroke content"), FPenParser::ParseDocument(Json, Document))) return false;
	const FUMGWidgetData Compact = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	FUMGWidgetData Oracle = Compact;
	bool bFound = false;
	TFunction<void(FUMGWidgetNode&)> Expand = [&](FUMGWidgetNode& Node)
	{
		if (Node.Id != TEXT("panel"))
		{
			for (FUMGWidgetNode& Child : Node.Children) Expand(Child);
			return;
		}
		if (!TestTrue(TEXT("Stroke overlay has content"), Node.Children.Num() > 0)) return;
		FUMGWidgetNode& Content = Node.Children[0];
		if (!TestEqual(TEXT("Paint-free border does not wrap a size constraint"), Content.Type, FString(TEXT("SizeBox")))) return;
		TestEqual(TEXT("Promoted child retains fixed width"), Content.WidgetStyle.WidthOverride.Get(0.f), 20.f);
		TestEqual(TEXT("Promoted child retains fixed height"), Content.WidgetStyle.HeightOverride.Get(0.f), 12.f);
		FUMGWidgetNode Surface;
		Surface.Id = Surface.Name = TEXT("IndependentUnpaintedSurface");
		Surface.Type = TEXT("Border");
		Surface.Brush.BrushColor = FLinearColor::Transparent;
		Surface.Children.Add(MoveTemp(Content));
		Content = MoveTemp(Surface);
		bFound = true;
	};
	Expand(Oracle.RootWidget);
	if (!bFound) return false;
	TArray<FImage> Images;
	const FUMGWidgetData* Variants[] = {&Compact, &Oracle};
	for (const FUMGWidgetData* Data : Variants)
	{
		UWidgetBlueprint* BP = FUMGGenerator::BuildWidgetBlueprint(*Data,
			TEXT("/Game/Design2UMG/Automation/StrokeSize/WBP_") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
		if (!TestNotNull(TEXT("Build constrained stroke blueprint"), BP)) return false;
		FKismetEditorUtilities::CompileBlueprint(BP);
		TArray<uint8> Png;
		FIntPoint Size;
		FString Error;
		if (!TestTrue(TEXT("Render constrained stroke blueprint"), FUMGWidgetRenderer::RenderWidgetToPng(BP, Png, Size, &Error))) return false;
		FImage& Image = Images.AddDefaulted_GetRef();
		if (!TestTrue(TEXT("Decode constrained stroke render"), Design2UMGImageTestUtils::DecodeToBGRA8(Png.GetData(), Png.Num(), Image))) return false;
	}
	TestTrue(TEXT("Removing unpainted surface preserves every pixel"), Images[0].RawData == Images[1].RawData);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCompactRectangularStrokeTest,
	"Design2UMG.Converter.CompactRectangularStroke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCompactRectangularStrokeTest::RunTest(const FString&)
{
	struct FStrokeCase
	{
		float Thickness;
		int32 Width;
		int32 Height;
		bool bTranslucent;
		bool bUnequal = false;
	};
	for (const FStrokeCase& Fixture : {
		FStrokeCase{1.f, 60, 40, false}, {2.f, 60, 40, false}, {3.f, 60, 40, false},
		{2.f, 17, 13, true}, {3.f, 6, 6, true}, {1.5f, 60, 40, false},
		{3.f, 5, 4, true}, {3.f, 4, 10, true}, {2.f, 60, 40, false, true}})
	{
		const float Thickness = Fixture.Thickness;
		FDesignDocument Document;
		FString Json = TEXT(R"JSON({"children":[{"id":"root","type":"frame","width":100,"height":80,"layout":"none","fill":"#000000","children":[{"id":"panel","type":"frame","x":20,"y":20,"width":60,"height":40,"fill":"#FFFFFF","stroke":"#FF0000","strokeWidth":2,"strokeAlignment":"inner"}]}]})JSON");
		Json.ReplaceInline(TEXT("\"strokeWidth\":2"), *FString::Printf(TEXT("\"strokeWidth\":%g"), Thickness));
		Json.ReplaceInline(TEXT("\"width\":60,\"height\":40"), *FString::Printf(TEXT("\"width\":%d,\"height\":%d"), Fixture.Width, Fixture.Height));
		if (Fixture.bTranslucent) Json.ReplaceInline(TEXT("#FF0000"), TEXT("#FF000080"));
		AddInfo(FString::Printf(TEXT("Outline thickness %g, size %dx%d, translucent=%d, unequal=%d"), Thickness, Fixture.Width, Fixture.Height, Fixture.bTranslucent, Fixture.bUnequal));
		if (!TestTrue(TEXT("Parse stroke fixture"), FPenParser::ParseDocument(Json, Document))) return false;
		if (Fixture.bUnequal)
		{
			FDesignStrokeThickness& Stroke = Document.Children[0]->Children[0]->Visual.Stroke.Thickness;
			Stroke.bUniform = false;
			Stroke.Top = Stroke.Bottom = Stroke.Left = Thickness;
			Stroke.Right = Thickness + 1.f;
		}
		FUMGWidgetData Compact = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
		int32 StrokeImages = 0;
		TFunction<void(const FUMGWidgetNode&)> Count = [&](const FUMGWidgetNode& Node)
		{
			if ((Node.Type == TEXT("Image") || Node.Type == TEXT("Border")) && Node.Name.StartsWith(TEXT("panel_stroke"))) ++StrokeImages;
			for (const FUMGWidgetNode& Child : Node.Children) Count(Child);
		};
		Count(Compact.RootWidget);
		const bool bCompact = Thickness == FMath::RoundToFloat(Thickness) && Fixture.Height >= 2 * Thickness;
		if (!TestEqual(TEXT("Opposite integer horizontal strokes share one mask"), StrokeImages, bCompact ? 3 : 4)) return false;
		if (!bCompact) continue;
		const FLinearColor StrokeColor = Fixture.bTranslucent ? FLinearColor(1.f, 0.f, 0.f, 128.f / 255.f) : FLinearColor::Red;
		FUMGWidgetData FourSides = Compact;
		bool Replaced = false;
		TFunction<void(FUMGWidgetNode&)> Expand = [&](FUMGWidgetNode& Node)
		{
			const int32 Removed = Node.Children.RemoveAll([](const FUMGWidgetNode& Child)
			{
				return Child.Name.StartsWith(TEXT("panel_stroke"));
			});
			if (Removed == 3)
			{
				for (int32 Side = 0; Side < 4; ++Side)
				{
					FUMGWidgetNode Line;
					Line.Type = TEXT("Image");
					Line.Name = FString::Printf(TEXT("OracleSide%d"), Side);
					Line.Id = Line.Name;
					Line.Brush.Image.TexturePath = TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture");
					Line.Brush.BrushColor = StrokeColor;
					Line.WidgetStyle.WidthOverride = Side < 2 ? 1.f : Thickness + (Fixture.bUnequal && Side == 3 ? 1.f : 0.f);
					Line.WidgetStyle.HeightOverride = Side < 2 ? Thickness : 1.f;
					Line.Slot.HorizontalAlignment = Side < 2 ? EUMGSlotHorizontalAlignment::Fill
					: Side == 2 ? EUMGSlotHorizontalAlignment::Left : EUMGSlotHorizontalAlignment::Right;
					Line.Slot.VerticalAlignment = Side >= 2 ? EUMGSlotVerticalAlignment::Fill
					: Side == 0 ? EUMGSlotVerticalAlignment::Top : EUMGSlotVerticalAlignment::Bottom;
					Node.Children.Add(MoveTemp(Line));
				}
				Replaced = true;
				return;
			}
			for (FUMGWidgetNode& Child : Node.Children) Expand(Child);
		};
		Expand(FourSides.RootWidget);
		if (!TestTrue(TEXT("Construct independent four-strip rendering oracle"), Replaced)) return false;
		auto Render = [this](const FUMGWidgetData& Data, FImage& Image, float Scale = 1.0f, float Opacity = 1.0f)
		{
			UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data,
			TEXT("/Game/Design2UMG/Automation/CompactStroke/WBP_") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
			if (!TestNotNull(TEXT("Build stroke blueprint"), Blueprint)) return false;
			FKismetEditorUtilities::CompileBlueprint(Blueprint);
			Blueprint->WidgetTree->ForEachWidget([this](UWidget* Widget)
			{
				if (const UBorder* Border = Cast<UBorder>(Widget); Border && Border->GetName().Contains(TEXT("stroke_horizontal")))
				{
					const UTexture2D* Texture = Cast<UTexture2D>(Border->Background.GetResourceObject());
					TestEqual(TEXT("Decorative mask does not intercept content input"), Border->GetVisibility(), ESlateVisibility::HitTestInvisible);
					if (TestNotNull(TEXT("Outline texture is persisted"), Texture))
					{
						TestFalse(TEXT("Outline survives instance duplication"), Texture->HasAnyFlags(RF_DuplicateTransient));
						TestEqual(TEXT("Outline keeps nearest sampling"), Texture->Filter.GetValue(), TF_Nearest);
					}
				}
			});
			if (Scale != 1.0f || Opacity != 1.0f)
			{
				TStrongObjectPtr<UUserWidget> Instance(NewObject<UUserWidget>(GetTransientPackage(), Blueprint->GeneratedClass));
				if (!TestTrue(TEXT("Initialize scaled stroke instance"), Instance->Initialize())) return false;
				Instance->SetRenderOpacity(Opacity);
				Instance->WidgetTree->ForEachWidget([this](UWidget* Widget)
				{
					if (UBorder* Border = Cast<UBorder>(Widget); Border && Border->GetName().Contains(TEXT("stroke_horizontal")))
					TestNotNull(TEXT("Runtime outline retains texture"), Border->Background.GetResourceObject());
				});
				const FVector2D DrawSize(100 * Scale, 80 * Scale);
				TStrongObjectPtr<UTextureRenderTarget2D> Target(FWidgetRenderer::CreateTargetFor(DrawSize, TF_Nearest, true));
				FWidgetRenderer Renderer(true);
				Renderer.DrawWidget(Target.Get(), Instance->TakeWidget(), Scale, DrawSize, 0.f);
				FlushRenderingCommands();
				TArray<FColor> Pixels;
				if (!TestTrue(TEXT("Read scaled stroke pixels"), Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels))) return false;
				Image.Init(Target->SizeX, Target->SizeY, ERawImageFormat::BGRA8);
				FMemory::Memcpy(Image.RawData.GetData(), Pixels.GetData(), Pixels.Num() * sizeof(FColor));
				Instance->ReleaseSlateResources(true);
				return true;
			}
			TArray<uint8> Png;
			FIntPoint Size;
			FString Error;
			return TestTrue(TEXT("Render stroke"), FUMGWidgetRenderer::RenderWidgetToPng(Blueprint, Png, Size, &Error))
			&& TestTrue(TEXT("Decode stroke"), Design2UMGImageTestUtils::DecodeToBGRA8(Png.GetData(), Png.Num(), Image));
		};
		FImage Actual, Expected;
		if (!Render(Compact, Actual) || !Render(FourSides, Expected)) return false;
		TestEqual(TEXT("Same render width"), Actual.SizeX, Expected.SizeX);
		TestEqual(TEXT("Same render height"), Actual.SizeY, Expected.SizeY);
		TestTrue(TEXT("Compact outline preserves every rendered pixel"), Actual.RawData == Expected.RawData);
		if (Actual.RawData != Expected.RawData && Actual.SizeX == Expected.SizeX && Actual.SizeY == Expected.SizeY)
		{
			const FColor* A = reinterpret_cast<const FColor*>(Actual.RawData.GetData());
			const FColor* E = reinterpret_cast<const FColor*>(Expected.RawData.GetData());
			int32 Changed = 0;
			for (int32 I = 0; I < Actual.SizeX * Actual.SizeY; ++I) if (A[I] != E[I]) ++Changed;
			AddInfo(FString::Printf(TEXT("Changed=%d inner actual=%s expected=%s"), Changed,
			*A[30 * Actual.SizeX + 30].ToString(), *E[30 * Expected.SizeX + 30].ToString()));
		}
		if (!Render(Compact, Actual, 1.f, 0.5f) || !Render(FourSides, Expected, 1.f, 0.5f)) return false;
		TestTrue(TEXT("Outline preserves pixels under inherited runtime opacity"), Actual.RawData == Expected.RawData);
		for (const float Scale : {1.25f, 1.5f, 2.0f})
		{
			if (!Render(Compact, Actual, Scale) || !Render(FourSides, Expected, Scale)) return false;
			TestTrue(FString::Printf(TEXT("Outline preserves every pixel at layout scale %g"), Scale), Actual.RawData == Expected.RawData);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRectangularStrokeRuntimeTest,
	"Design2UMG.Converter.RectangularStrokeRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRectangularStrokeRuntimeTest::RunTest(const FString&)
{
	const FString Json = TEXT(R"JSON({"children":[{
		"id":"root","type":"frame","width":100,"height":80,"layout":"none","fill":"#000000",
		"children":[{"id":"panel","type":"frame","x":20,"y":20,"width":60,"height":40,
		"layout":"none","fill":"#FFFFFF","stroke":"#FF0000","strokeWidth":2,
		"strokeAlignment":"inner","children":[{"id":"content","type":"rectangle",
		"x":20,"y":10,"width":20,"height":20,"fill":"#0000FF"}]}]}]})JSON");
	auto CheckRuntime = [this](const FString& SourceJson, bool HasFill)
	{
		FDesignDocument Document;
		if (!TestTrue(TEXT("Source parses"), FPenParser::ParseDocument(SourceJson, Document))) return false;
		const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
		if (!HasFill)
		{
			const FUMGWidgetNode* Panel = nullptr;
			TFunction<void(const FUMGWidgetNode&)> FindPanel = [&](const FUMGWidgetNode& Candidate)
			{
				if (Candidate.Id == TEXT("panel")) Panel = &Candidate;
				for (const FUMGWidgetNode& Child : Candidate.Children) FindPanel(Child);
			};
			FindPanel(Data.RootWidget);
			if (!TestNotNull(TEXT("Outlined frame retains source identity"), Panel)) return false;
			TestEqual(TEXT("Unpainted absolute content owns its strokes without an extra overlay"), Panel->Type, FString(TEXT("CanvasPanel")));
			TestEqual(TEXT("Content plus three stroke surfaces remain"), Panel->Children.Num(), 4);
		}
		// Use a mounted content path so generated textures follow normal import persistence.
		UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data,
			TEXT("/Game/Design2UMG/Automation/CompactStroke/Runtime_") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
		if (!TestNotNull(TEXT("Widget blueprint built"), Blueprint)) return false;
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		TArray<uint8> Png;
		FIntPoint Size;
		FString Error;
		if (!TestTrue(TEXT("Compiled widget renders"), FUMGWidgetRenderer::RenderWidgetToPng(Blueprint, Png, Size, &Error))) return false;
		FImage Image;
		if (!TestTrue(TEXT("Decode runtime pixels"), Design2UMGImageTestUtils::DecodeToBGRA8(Png.GetData(), Png.Num(), Image))) return false;
		if (!TestTrue(TEXT("Expected BGRA8 image"), Image.Format == ERawImageFormat::BGRA8)) return false;
		if (!TestTrue(TEXT("Rendered image contains panel"), Image.SizeX > 50 && Image.SizeY > 40)) return false;
		const FColor* Pixels = reinterpret_cast<const FColor*>(Image.RawData.GetData());
		const FColor Top = Pixels[20 * Image.SizeX + 50];
		const FColor Left = Pixels[40 * Image.SizeX + 20];
		int32 RedPixels = 0;
		for (int32 Index = 0; Index < Image.SizeX * Image.SizeY; ++Index)
		{
			const FColor Pixel = Pixels[Index];
			if (Pixel.R > 240 && Pixel.G < 10 && Pixel.B < 10 && Pixel.A > 240) ++RedPixels;
		}
		TestTrue(TEXT("Square outline has opaque red pixels"), RedPixels > 100);
		TestTrue(TEXT("Integer-aligned square top edge preserves stroke color"), Top.R > 250 && Top.G < 5 && Top.B < 5);
		TestTrue(TEXT("Integer-aligned square left edge preserves stroke color"), Left.R > 250 && Left.G < 5 && Left.B < 5);
		const FColor Interior = Pixels[30 * Image.SizeX + 30];
		TestTrue(TEXT("Stroke preserves the authored interior fill"), HasFill
			? (Interior.R > 240 && Interior.G > 240 && Interior.B > 240)
			: (Interior.R < 10 && Interior.G < 10 && Interior.B < 10));
		return true;
	};
	TestTrue(TEXT("Solid-fill outlined frame renders"), CheckRuntime(Json, true));
	FString NoFill = Json;
	NoFill.ReplaceInline(TEXT("\"fill\":\"#FFFFFF\","), TEXT(""));
	TestTrue(TEXT("Unfilled outlined frame renders"), CheckRuntime(NoFill, false));
	return true;
}
#endif
