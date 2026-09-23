// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

// Figma render-fallback coverage: vector raster fallback, metadata round trip, leaf/structured fallback widgets, child-tree preservation.

#include "Misc/AutomationTest.h"
#include "Design2UMGTestUtils.h"
#include "Design2UMGConverter.h"
#include "FigmaParser.h"
#include "FigmaTestDocBuilder.h"

#if WITH_AUTOMATION_WORKER

using namespace Design2UMGCoreTests;
using namespace Design2UMGTestUtils;


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaVectorRasterFallbackTest,
	"Design2UMG.FigmaParser.VectorRasterFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaVectorRasterFallbackTest::RunTest(const FString& Parameters)
{
	const FString Json = MakeFigmaDocJson(
		MakeContainerNode(TEXT("0:2"), TEXT("Root"), TEXT("FRAME"), 0, 0, 400, 400,
			MakeLeafNode(TEXT("ellipse_grad"), TEXT("Ellipse Gradient"), TEXT("ELLIPSE"), 20, 20, 320, 320,
				TEXT(",\"fillGeometry\":[{\"path\":\"M0 160C0 71.6 71.6 0 160 0C248.4 0 320 71.6 320 160C320 248.4 248.4 320 160 320C71.6 320 0 248.4 0 160Z\",\"windingRule\":\"NONZERO\"}],"
					 "\"fills\":["
					 "{\"type\":\"GRADIENT_LINEAR\",\"visible\":true,\"gradientStops\":["
					 "{\"position\":0.0,\"color\":{\"r\":1,\"g\":0,\"b\":0,\"a\":1}},"
					 "{\"position\":1.0,\"color\":{\"r\":0,\"g\":0,\"b\":1,\"a\":1}}"
					 "],\"gradientHandlePositions\":[{\"x\":0,\"y\":0.5},{\"x\":1,\"y\":0.5}]},"
					 "{\"type\":\"IMAGE\",\"visible\":true,\"imageRef\":\"vec_grad_1\",\"scaleMode\":\"FILL\"}"
					 "]"))));

	FDesignDocument Doc;
	TestTrue(TEXT("Figma vector fallback parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() == 0 || Doc.Children[0]->Children.Num() == 0)
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& EllipseNode = Doc.Children[0]->Children[0];
	TestTrue(TEXT("Vector raster fallback captured"), EllipseNode->Visual.VectorRasterFallbackImageRef == TEXT("vec_grad_1"));

	TSet<FString> ImageRefs;
	FFigmaParser::CollectImageRefs(Doc, ImageRefs);
	TestTrue(TEXT("CollectImageRefs includes vector fallback"), ImageRefs.Contains(TEXT("vec_grad_1")));

	TMap<FString, FString> ImageMap;
	ImageMap.Add(TEXT("vec_grad_1"), TEXT("/Game/Transient/T_vec_grad_1.T_vec_grad_1"));
	FFigmaParser::ApplyImageUrls(Doc, ImageMap);
	TestTrue(TEXT("ApplyImageUrls remaps vector fallback"), EllipseNode->Visual.VectorRasterFallbackImageRef == TEXT("/Game/Transient/T_vec_grad_1.T_vec_grad_1"));

	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* Widget = FindWidgetById(Data.RootWidget, TEXT("ellipse_grad"));
	TestTrue(TEXT("Gradient ellipse widget exists"), Widget != nullptr);
	if (!Widget)
	{
		return false;
	}

	TestEqual(TEXT("Gradient ellipse uses image fallback"), Widget->Type, TEXT("Image"));
	TestEqual(TEXT("Gradient ellipse texture path preserved"), GetWidgetProperty(*Widget, TEXT("TexturePath")), TEXT("/Game/Transient/T_vec_grad_1.T_vec_grad_1"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaRenderFallbackMetadataSerializationCycleTest,
	"Design2UMG.FigmaParser.RenderFallbackMetadataSerializationCycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaRenderFallbackMetadataSerializationCycleTest::RunTest(const FString& Parameters)
{
	const FString Json = MakeFigmaDocJson(
		MakeContainerNode(TEXT("0:2"), TEXT("Root"), TEXT("FRAME"), 0, 0, 300, 200,
			MakeContainerNode(TEXT("render_leaf"), TEXT("Image Tile"), TEXT("FRAME"), 24, 16, 160, 96, FString(),
				FString::Printf(
					TEXT(",\"absoluteRenderBounds\":%s,\"fills\":["
						 "{\"type\":\"SOLID\",\"visible\":true,\"blendMode\":\"NORMAL\",\"color\":{\"r\":0.2,\"g\":0.2,\"b\":0.2,\"a\":1}},"
						 "{\"type\":\"IMAGE\",\"visible\":true,\"blendMode\":\"LUMINOSITY\",\"imageRef\":\"img_orig\",\"scaleMode\":\"FIT\",\"filters\":{\"contrast\":0.25}}"
						 "],"
						 "\"design2umgRenderFallbackImageRef\":\"render_202_5356\","
						 "\"design2umgRenderFallbackScope\":\"full\","
						 "\"design2umgRenderFallbackSource\":\"figma_render_png\","
						 "\"design2umgRenderFallbackReason\":\"image_filters+image_blend_luminosity\","
						 "\"interactions\":{\"unexpected\":\"object\"}"),
					*MakeBoundsJson(24, 16, 160, 96)))));

	FDesignDocument Doc;
	TestTrue(TEXT("Render fallback parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() == 0 || Doc.Children[0]->Children.Num() == 0)
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& RenderNode = Doc.Children[0]->Children[0];
	TestEqual(TEXT("Render fallback image ref normalized"), RenderNode->Visual.RenderFallback.ImageRef, TEXT("render_202_5356"));
	TestEqual(TEXT("Render fallback scope normalized"), RenderNode->Visual.RenderFallback.Scope, EDesignRenderFallbackScope::Leaf);
	TestEqual(TEXT("Render fallback source preserved"), RenderNode->Metadata.FindRef(TEXT("design2umgRenderFallbackSource")), TEXT("figma_render_png"));
	TestEqual(TEXT("Render fallback reason preserved"), RenderNode->Metadata.FindRef(TEXT("design2umgRenderFallbackReason")), TEXT("image_filters+image_blend_luminosity"));
	TestTrue(TEXT("Non-array interactions remain metadata"), RenderNode->Metadata.Contains(TEXT("interactions")));
	TestFalse(TEXT("Non-array interactions do not suppress render fallback"), RenderNode->Visual.RenderFallback.bSuppressedBySourceSemantics);

	TSet<FString> ImageRefs;
	FFigmaParser::CollectImageRefs(Doc, ImageRefs);
	TestTrue(TEXT("CollectImageRefs includes original image"), ImageRefs.Contains(TEXT("img_orig")));
	TestTrue(TEXT("CollectImageRefs includes render fallback image"), ImageRefs.Contains(TEXT("render_202_5356")));

	TMap<FString, FString> ImageMap;
	ImageMap.Add(TEXT("img_orig"), TEXT("/Game/Transient/T_img_orig.T_img_orig"));
	ImageMap.Add(TEXT("render_202_5356"), TEXT("/Game/Transient/T_render_202_5356.T_render_202_5356"));
	FFigmaParser::ApplyImageUrls(Doc, ImageMap);
	TestEqual(TEXT("ApplyImageUrls remaps render fallback image"), RenderNode->Visual.RenderFallback.ImageRef, TEXT("/Game/Transient/T_render_202_5356.T_render_202_5356"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaStructuredLeafRenderFallbackWidgetTest,
	"Design2UMG.FigmaParser.StructuredLeafRenderFallbackWidget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaStructuredLeafRenderFallbackWidgetTest::RunTest(const FString& Parameters)
{
	FDesignDocument Doc;
	TSharedPtr<FDesignNode> Root = MakeShared<FDesignNode>();
	Root->Id = TEXT("root");
	Root->Name = TEXT("Root");
	Root->Type = EDesignNodeType::Frame;
	Root->LayoutData.Layout = EDesignLayout::None;
	Root->Transform.ExplicitX = 0.0f;
	Root->Transform.ExplicitY = 0.0f;
	Root->Transform.X = 0.0f;
	Root->Transform.Y = 0.0f;
	Root->Transform.Width = FDesignSize::Fixed(100.0f);
	Root->Transform.Height = FDesignSize::Fixed(80.0f);

	TSharedPtr<FDesignNode> Leaf = MakeShared<FDesignNode>();
	Leaf->Id = TEXT("render_leaf_structured");
	Leaf->Name = TEXT("Image Tile Structured");
	Leaf->Type = EDesignNodeType::Frame;
	Leaf->LayoutData.Layout = EDesignLayout::None;
	Leaf->Transform.ExplicitX = 10.0f;
	Leaf->Transform.ExplicitY = 12.0f;
	Leaf->Transform.X = 10.0f;
	Leaf->Transform.Y = 12.0f;
	Leaf->Transform.Width = FDesignSize::Fixed(26.0f);
	Leaf->Transform.Height = FDesignSize::Fixed(26.0f);
	Leaf->MutableSource().SourceNodeType = TEXT("FRAME");
	FDesignFill& LeafFill = Leaf->Visual.Fills.AddDefaulted_GetRef();
	LeafFill.Type = EDesignFillType::Image;
	LeafFill.bEnabled = true;
	LeafFill.Image.Url = TEXT("img_avatar");
	LeafFill.Image.Mode = EDesignImageMode::Fit;
	Leaf->Visual.RenderFallback.ImageRef = TEXT("/Game/Transient/T_render_202_5356.T_render_202_5356");
	Root->Children.Add(Leaf);
	Doc.Children.Add(Root);

	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* Widget = FindWidgetById(Data.RootWidget, TEXT("render_leaf_structured"));
	TestTrue(TEXT("Structured render fallback widget exists"), Widget != nullptr);
	if (!Widget)
	{
		return false;
	}

	TestEqual(TEXT("Structured render fallback converts directly to image"), Widget->Type, TEXT("Image"));
	TestEqual(TEXT("Structured render fallback keeps texture path"), GetWidgetProperty(*Widget, TEXT("TexturePath")), TEXT("/Game/Transient/T_render_202_5356.T_render_202_5356"));
	TestTrue(TEXT("Structured render fallback keeps design X"), FMath::IsNearlyEqual(Widget->Bounds.X, 10.0f, 0.001f));
	TestTrue(TEXT("Structured render fallback keeps design Y"), FMath::IsNearlyEqual(Widget->Bounds.Y, 12.0f, 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaLeafRenderFallbackWidgetTest,
	"Design2UMG.FigmaParser.LeafRenderFallbackWidget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaLeafRenderFallbackWidgetTest::RunTest(const FString& Parameters)
{
	FDesignDocument Doc;
	TSharedPtr<FDesignNode> Root = MakeShared<FDesignNode>();
	Root->Id = TEXT("root");
	Root->Name = TEXT("Root");
	Root->Type = EDesignNodeType::Frame;
	Root->LayoutData.Layout = EDesignLayout::None;
	Root->Transform.ExplicitX = 0.0f;
	Root->Transform.ExplicitY = 0.0f;
	Root->Transform.X = 0.0f;
	Root->Transform.Y = 0.0f;
	Root->Transform.Width = FDesignSize::Fixed(100.0f);
	Root->Transform.Height = FDesignSize::Fixed(80.0f);

	TSharedPtr<FDesignNode> Leaf = MakeShared<FDesignNode>();
	Leaf->Id = TEXT("render_leaf");
	Leaf->Name = TEXT("Image Tile");
	Leaf->Type = EDesignNodeType::Frame;
	Leaf->LayoutData.Layout = EDesignLayout::None;
	Leaf->Transform.ExplicitX = 10.0f;
	Leaf->Transform.ExplicitY = 12.0f;
	Leaf->Transform.X = 10.0f;
	Leaf->Transform.Y = 12.0f;
	Leaf->Transform.Width = FDesignSize::Fixed(26.0f);
	Leaf->Transform.Height = FDesignSize::Fixed(26.0f);
	FDesignFill& LeafFill = Leaf->Visual.Fills.AddDefaulted_GetRef();
	LeafFill.Type = EDesignFillType::Image;
	LeafFill.bEnabled = true;
	LeafFill.Image.Url = TEXT("img_avatar");
	LeafFill.Image.Mode = EDesignImageMode::Fit;
	Leaf->Visual.RenderFallback.ImageRef = TEXT("/Game/Transient/T_render_202_5356.T_render_202_5356");
	Root->Children.Add(Leaf);
	Doc.Children.Add(Root);

	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* Widget = FindWidgetById(Data.RootWidget, TEXT("render_leaf"));
	TestTrue(TEXT("Leaf render fallback widget exists"), Widget != nullptr);
	if (!Widget)
	{
		return false;
	}

	TestEqual(TEXT("Leaf render fallback converts directly to image"), Widget->Type, TEXT("Image"));
	TestEqual(TEXT("Leaf render fallback keeps texture path"), GetWidgetProperty(*Widget, TEXT("TexturePath")), TEXT("/Game/Transient/T_render_202_5356.T_render_202_5356"));
	const FString WidthOverrideValue = GetWidgetProperty(*Widget, TEXT("WidthOverride"));
	const FString HeightOverrideValue = GetWidgetProperty(*Widget, TEXT("HeightOverride"));
	const float WidthOverride = FCString::Atof(*WidthOverrideValue);
	const float HeightOverride = FCString::Atof(*HeightOverrideValue);
	TestFalse(TEXT("Leaf render fallback exposes typed width override"), WidthOverrideValue.IsEmpty());
	TestFalse(TEXT("Leaf render fallback exposes typed height override"), HeightOverrideValue.IsEmpty());
	TestTrue(TEXT("Leaf render fallback keeps design width override"), FMath::IsNearlyEqual(WidthOverride, 26.0f, 0.001f));
	TestTrue(TEXT("Leaf render fallback keeps design height override"), FMath::IsNearlyEqual(HeightOverride, 26.0f, 0.001f));
	TestTrue(TEXT("Leaf render fallback keeps design X"), FMath::IsNearlyEqual(Widget->Bounds.X, 10.0f, 0.001f));
	TestTrue(TEXT("Leaf render fallback keeps design Y"), FMath::IsNearlyEqual(Widget->Bounds.Y, 12.0f, 0.001f));
	TestTrue(TEXT("Leaf render fallback widget width uses design bounds"), FMath::IsNearlyEqual(Widget->Bounds.W, 26.0f, 0.001f));
	TestTrue(TEXT("Leaf render fallback widget height uses design bounds"), FMath::IsNearlyEqual(Widget->Bounds.H, 26.0f, 0.001f));
	TestTrue(TEXT("Leaf render fallback keeps no structural children"), Widget->Children.Num() == 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaRenderFallbackKeepsChildTreeTest,
	"Design2UMG.FigmaParser.RenderFallbackKeepsChildTree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaRenderFallbackKeepsChildTreeTest::RunTest(const FString& Parameters)
{
	const FString Json = MakeFigmaDocJson(
		MakeContainerNode(TEXT("render_parent"), TEXT("Card"), TEXT("FRAME"), 0, 0, 260, 160,
			MakeTextNode(TEXT("render_child"), TEXT("Title"), 20, 18, 80, 24, TEXT("Hello"),
				TEXT("\"fontFamily\":\"Inter\",\"fontSize\":16,\"fontWeight\":400,\"textAlignHorizontal\":\"LEFT\",\"textAlignVertical\":\"TOP\","
					 "\"textCase\":\"ORIGINAL\",\"textDecoration\":\"NONE\",\"letterSpacing\":0,\"lineHeightUnit\":\"AUTO\",\"lineHeightPx\":0")),
			FString::Printf(
				TEXT(",\"absoluteRenderBounds\":%s,\"fills\":["
					 "{\"type\":\"SOLID\",\"visible\":true,\"blendMode\":\"NORMAL\",\"color\":{\"r\":0.2,\"g\":0.2,\"b\":0.2,\"a\":1}},"
					 "{\"type\":\"IMAGE\",\"visible\":true,\"blendMode\":\"LUMINOSITY\",\"imageRef\":\"img_orig\",\"scaleMode\":\"FIT\",\"filters\":{\"contrast\":0.25}}"
					 "],"
					 "\"design2umgRenderFallbackImageRef\":\"/Game/Transient/T_render_parent.T_render_parent\","
					 "\"design2umgRenderFallbackScope\":\"full\","
					 "\"design2umgRenderFallbackSource\":\"figma_render_png\","
					 "\"design2umgRenderFallbackReason\":\"image_filters+image_blend_luminosity\""),
				*MakeBoundsJson(0, 0, 260, 160))));

	FDesignDocument Doc;
	TestTrue(TEXT("Render fallback parent parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* ParentWidget = FindWidgetById(Data.RootWidget, TEXT("render_parent"));
	const FUMGWidgetNode* ChildWidget = FindWidgetById(Data.RootWidget, TEXT("render_child"));
	TestTrue(TEXT("Render fallback parent widget exists"), ParentWidget != nullptr);
	TestTrue(TEXT("Render fallback child widget exists"), ChildWidget != nullptr);
	if (!ParentWidget || !ChildWidget)
	{
		return false;
	}

	TestFalse(TEXT("Render fallback parent with children must not flatten to image"), ParentWidget->Type == TEXT("Image"));
	TestEqual(TEXT("Child text preserved"), GetWidgetProperty(*ChildWidget, TEXT("Text")), TEXT("Hello"));
	return true;
}

#endif

