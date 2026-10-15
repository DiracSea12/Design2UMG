// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Misc/AutomationTest.h"
#include "Design2UMGImageTestUtils.h"
#include "PenParser.h"
#include "Design2UMGConverter.h"
#include "UMGGenerator.h"
#include "UMGWidgetRenderer.h"
#include "UMGSchemaSourceExtensions.h"
#include "WidgetBlueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ImageUtils.h"
#include "ImageCore.h"
#include "Blueprint/UserWidget.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Slate/WidgetRenderer.h"
#include "RenderingThread.h"
#include "UObject/StrongObjectPtr.h"
#include "TextureResource.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSingleChildSizeBoxRuntimeTest,
	"Design2UMG.Converter.SingleChildSizeBoxRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSingleChildSizeBoxRuntimeTest::RunTest(const FString&)
{
	for (const bool bHorizontal : {true, false})
	{
		for (const int32 Mode : {0, 1, 2})
		{
			const bool bFill = Mode == 0;
			const bool bOverflow = Mode == 2;
			FDesignDocument Document;
			FString Json = FString::Printf(TEXT(R"JSON({"children":[{"id":"root","type":"frame","width":100,"height":80,"layout":"vertical","fill":"#182030","children":[{"id":"panel","name":"panel","type":"frame","width":60,"height":40,"layout":"%s","opacity":0.6,"children":[{"id":"leaf","type":"rectangle","width":%d,"height":%d,"fill":"#FF8050","stroke":"#FF0000","strokeWidth":1,"strokeAlignment":"inner"}]}]}]})JSON"),
				bHorizontal ? TEXT("horizontal") : TEXT("vertical"), bHorizontal ? (bOverflow ? 80 : 50) : 20, bHorizontal ? 12 : (bOverflow ? 60 : 35));
			if (bFill)
			{
				Json.ReplaceInline(bHorizontal ? TEXT("\"width\":50") : TEXT("\"height\":35"),
					bHorizontal ? TEXT("\"width\":\"fill_container\"") : TEXT("\"height\":\"fill_container\""));
			}
			if (!TestTrue(TEXT("Parse single-child sizing fixture"), FPenParser::ParseDocument(Json, Document))) return false;
			const FUMGWidgetData Compact = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
			FUMGWidgetData Oracle = Compact;
			bool bFound = false;
			TFunction<void(FUMGWidgetNode&)> Expand = [&](FUMGWidgetNode& Node)
			{
				const bool bOwnsPanel = Node.HasSourceData() && Node.Source().SourceNodeId == TEXT("panel");
				const bool bWrapsPanel = Node.Children.Num() == 1 && Node.Children[0].HasSourceData()
					&& Node.Children[0].Source().SourceNodeId == TEXT("panel");
				if (Node.Type != TEXT("SizeBox") || !(bFill ? bOwnsPanel : bWrapsPanel))
				{
					for (FUMGWidgetNode& Child : Node.Children) Expand(Child);
					return;
				}
				bFound = true;
				TestEqual(TEXT("Only a folded carrier owns the replaced panel's identity"), Node.HasSourceData(), bFill);
				if (!TestEqual(TEXT("Sizing carrier has one child"), Node.Children.Num(), 1)) return;
				const FString FlowType = bHorizontal ? TEXT("HorizontalBox") : TEXT("VerticalBox");
				if (!bFill)
				{
					TestEqual(TEXT("Auto sizing retains the flow panel's allocation behavior"), Node.Children[0].Type, FlowType);
					return;
				}
				if (!TestEqual(TEXT("Sizing carrier directly hosts its fill child"), Node.Children[0].Type, FString(TEXT("SizeBox")))) return;
				TestEqual(TEXT("Primary source widget id survives folding"), Node.Id, FString(TEXT("panel")));
				TestEqual(TEXT("Primary source widget name survives folding"), Node.Name, FString(TEXT("panel")));
				FUMGWidgetNode Flow;
				Flow.Id = Flow.Name = TEXT("IndependentFlowPanel");
				Flow.Type = FlowType;
				Node.Children[0].Slot.SizeRule = EUMGSlotSizeRule::Fill;
				Flow.Render = MoveTemp(Node.Render);
				Node.Render = FUMGRenderStyleData();
				Flow.Children.Add(MoveTemp(Node.Children[0]));
				Node.Children[0] = MoveTemp(Flow);
			};
			Expand(Oracle.RootWidget);
			if (!TestTrue(TEXT("Find sized source frame"), bFound)) return false;
			TArray<FImage> Images;
			TArray<TArray<FColor>> SmallImages;
			const FUMGWidgetData* Variants[] = {&Compact, &Oracle};
			for (const FUMGWidgetData* Data : Variants)
			{
				UWidgetBlueprint* BP = FUMGGenerator::BuildWidgetBlueprint(*Data,
					TEXT("/Game/Design2UMG/Automation/SingleChildSize/WBP_") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
				if (!TestNotNull(TEXT("Build single-child sizing blueprint"), BP)) return false;
				FKismetEditorUtilities::CompileBlueprint(BP);
				TArray<uint8> Png;
				FIntPoint Size;
				FString Error;
				if (!TestTrue(TEXT("Render single-child sizing blueprint"), FUMGWidgetRenderer::RenderWidgetToPng(BP, Png, Size, &Error))) return false;
				FImage& Image = Images.AddDefaulted_GetRef();
				if (!TestTrue(TEXT("Decode sizing render"), Design2UMGImageTestUtils::DecodeToBGRA8(Png.GetData(), Png.Num(), Image))) return false;
				TStrongObjectPtr<UUserWidget> Instance(NewObject<UUserWidget>(GetTransientPackage(), BP->GeneratedClass));
				if (!TestTrue(TEXT("Initialize undersized instance"), Instance->Initialize())) return false;
				const FVector2D SmallSize(40, 30);
				TStrongObjectPtr<UTextureRenderTarget2D> Target(FWidgetRenderer::CreateTargetFor(SmallSize, TF_Nearest, true));
				FWidgetRenderer Renderer(true);
				Renderer.DrawWidget(Target.Get(), Instance->TakeWidget(), 1.f, SmallSize, 0.f);
				FlushRenderingCommands();
				if (!TestTrue(TEXT("Read undersized rendering"), Target->GameThread_GetRenderTargetResource()->ReadPixels(SmallImages.AddDefaulted_GetRef()))) return false;
				Instance->ReleaseSlateResources(true);
			}
			TestTrue(TEXT("Sizing and inherited opacity match the independent flow panel"),
				Images[0].SizeX == Images[1].SizeX && Images[0].SizeY == Images[1].SizeY && Images[0].RawData == Images[1].RawData);
			TestTrue(TEXT("Actual undersized allocation preserves overflow pixels"), SmallImages[0] == SmallImages[1]);
		}
	}
	return true;
}
#endif
