// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

// Typed widget state and brush JSON round trips (switcher state, widget state, scroll box, brush texture path normalization, gradient brush).

#include "Misc/AutomationTest.h"
#include "Design2UMGTestUtils.h"
#include "UMGJsonParser.h"
#include "UMGJsonSerializer.h"
#include "UMGPropertyKeys.h"
#include "UMGWidgetNodeAccess.h"
#include "UMGWidgetUtils.h"
#include "Materials/Material.h"
#include "Serialization/JsonSerializer.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Components/WidgetSwitcher.h"
#include "UObject/UObjectGlobals.h"

#if WITH_AUTOMATION_WORKER

using namespace Design2UMGTestUtils;


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetSwitcherUsesTypedStateTest,
	"Design2UMG.PropertyApplier.WidgetSwitcherUsesTypedState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FWidgetSwitcherUsesTypedStateTest::RunTest(const FString& Parameters)
{
	UWidgetSwitcher* Widget = NewObject<UWidgetSwitcher>(GetTransientPackageAsObject());
	TestTrue(TEXT("WidgetSwitcher created"), Widget != nullptr);
	if (!Widget)
	{
		return false;
	}

	Widget->AddChild(NewObject<UTextBlock>(Widget));
	Widget->AddChild(NewObject<UTextBlock>(Widget));
	Widget->AddChild(NewObject<UTextBlock>(Widget));

	FUMGWidgetNode Node;
	Node.Type = TEXT("WidgetSwitcher");
	Node.WidgetState.ActiveWidgetIndex = 2;

	FUMGWidgetUtils::ApplyWidgetProperties(Widget, Node);
	TestEqual(TEXT("Typed widget state applies active index"), Widget->GetActiveWidgetIndex(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetStateJsonRoundTripTest,
	"Design2UMG.UMGJson.WidgetStateRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FWidgetStateJsonRoundTripTest::RunTest(const FString& Parameters)
{
	FUMGWidgetData SourceData;
	SourceData.WidgetName = TEXT("WBP_WidgetStateRoundTrip");
	SourceData.RootWidget.Type = TEXT("WidgetSwitcher");
	SourceData.RootWidget.Id = TEXT("switcher_root");
	SourceData.RootWidget.Name = TEXT("SwitcherRoot");
	SourceData.RootWidget.WidgetState.ActiveWidgetIndex = 1;

	FUMGWidgetNode FirstPage;
	FirstPage.Type = TEXT("TextBlock");
	FirstPage.Id = TEXT("page_a");
	FirstPage.Name = TEXT("PageA");
	FirstPage.Text.Text = TEXT("A");

	FUMGWidgetNode SecondPage;
	SecondPage.Type = TEXT("TextBlock");
	SecondPage.Id = TEXT("page_b");
	SecondPage.Name = TEXT("PageB");
	SecondPage.Text.Text = TEXT("B");

	SourceData.RootWidget.Children.Add(MoveTemp(FirstPage));
	SourceData.RootWidget.Children.Add(MoveTemp(SecondPage));

	const FString Json = FUMGJsonSerializer::Serialize(SourceData);
	TestFalse(TEXT("Serialized widget state emits WidgetState payload"), Json.IsEmpty() || !Json.Contains(TEXT("\"WidgetState\"")));

	FUMGWidgetData ParsedData;
	FString ParseError;
	TestTrue(TEXT("Widget state JSON parses"), FUMGJsonParser::ParseJson(Json, ParsedData, &ParseError));
	if (!ParseError.IsEmpty())
	{
		AddInfo(ParseError);
	}

	TestTrue(TEXT("Parsed widget state keeps typed active index"), ParsedData.RootWidget.WidgetState.ActiveWidgetIndex.IsSet());
	TestEqual(TEXT("Parsed widget state active index preserved"), ParsedData.RootWidget.WidgetState.ActiveWidgetIndex.GetValue(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FScrollBoxJsonRoundTripTest,
	"Design2UMG.UMGJson.ScrollBoxRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FScrollBoxJsonRoundTripTest::RunTest(const FString& Parameters)
{
	FUMGWidgetData SourceData;
	SourceData.WidgetName = TEXT("WBP_ScrollBoxRoundTrip");
	SourceData.RootWidget.Type = TEXT("ScrollBox");
	SourceData.RootWidget.Id = TEXT("scroll_root");
	SourceData.RootWidget.Name = TEXT("ScrollRoot");
	SourceData.RootWidget.ScrollBox.Orientation = TEXT("Horizontal");
	SourceData.RootWidget.ScrollBox.ScrollBarVisibility = FString();

	FUMGWidgetNode Child;
	Child.Type = TEXT("TextBlock");
	Child.Id = TEXT("scroll_child");
	Child.Name = TEXT("ScrollChild");
	Child.Text.Text = TEXT("Child");
	SourceData.RootWidget.Children.Add(MoveTemp(Child));

	const FString Json = FUMGJsonSerializer::Serialize(SourceData);
	TestFalse(TEXT("Serialized scroll box emits ScrollBox payload"), Json.IsEmpty() || !Json.Contains(TEXT("\"ScrollBox\"")));
	TestTrue(TEXT("Serialized scroll box keeps orientation key"), Json.Contains(FString::Printf(TEXT("\"%s\""), UMGPropertyKeys::Orientation)));
	TestTrue(TEXT("Serialized scroll box keeps explicit empty scrollbar visibility key"), Json.Contains(FString::Printf(TEXT("\"%s\""), UMGPropertyKeys::ScrollBarVisibility)));

	FUMGWidgetData ParsedData;
	FString ParseError;
	TestTrue(TEXT("ScrollBox JSON parses"), FUMGJsonParser::ParseJson(Json, ParsedData, &ParseError));
	if (!ParseError.IsEmpty())
	{
		AddInfo(ParseError);
	}

	TestTrue(TEXT("Parsed scroll box keeps orientation presence"), ParsedData.RootWidget.ScrollBox.Orientation.IsSet());
	TestEqual(TEXT("Parsed scroll box orientation value"), ParsedData.RootWidget.ScrollBox.Orientation.GetValue(), FString(TEXT("Horizontal")));
	TestTrue(TEXT("Parsed scroll box keeps explicit empty scrollbar visibility presence"), ParsedData.RootWidget.ScrollBox.ScrollBarVisibility.IsSet());
	TestEqual(TEXT("Parsed scroll box explicit empty scrollbar visibility value"), ParsedData.RootWidget.ScrollBox.ScrollBarVisibility.GetValue(), FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrushLegacyTexturePathNormalizationTest,
	"Design2UMG.WidgetNodeAccess.BrushLegacyTexturePathNormalization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBrushLegacyTexturePathNormalizationTest::RunTest(const FString& Parameters)
{
	FUMGWidgetData Data;
	TestTrue(TEXT("Existing root texture input parses"), FUMGJsonParser::ParseJson(
		TEXT(R"({"RootWidget":{"Type":"Image","Name":"RootTexture","Brush":{"TexturePath":"/Game/UI/T_LegacyBrush.T_LegacyBrush"}}})"), Data));
	TestTrue(TEXT("Root input is normalized into the only image owner"),
		Data.RootWidget.Brush.Image.TexturePath.IsSet()
		&& Data.RootWidget.Brush.Image.TexturePath.GetValue() == TEXT("/Game/UI/T_LegacyBrush.T_LegacyBrush"));
	const auto SerializedBrush = FUMGJsonSerializer::SerializeWidgetNode(Data.RootWidget)->GetObjectField(TEXT("Brush"));
	TestFalse(TEXT("Serialization never emits the root alias"), SerializedBrush->HasField(TEXT("TexturePath")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrushTexturePathConflictNormalizationJsonTest,
	"Design2UMG.UMGJson.BrushTexturePathConflictNormalization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBrushTexturePathConflictNormalizationJsonTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("Brush.TexturePath conflicts with Brush.Image.TexturePath"), EAutomationExpectedErrorFlags::Contains, 1);

	const FString Json = TEXT(R"({
		"RootWidget": {
			"Type": "Image",
			"Id": "root",
			"Name": "ConflictImage",
			"Brush": {
				"TexturePath": "/Game/UI/T_LegacyBrush.T_LegacyBrush",
				"Image": {
					"TexturePath": "/Game/UI/T_CanonicalBrush.T_CanonicalBrush",
					"Mode": "Crop",
					"Transform": "0.5,0,0.25,0,0.75,0.1"
				}
			}
		},
		"WidgetName": "WBP_TextureConflict"
	})");

	FUMGWidgetData ParsedData;
	FString ParseError;
	TestTrue(TEXT("Conflict JSON parses"), FUMGJsonParser::ParseJson(Json, ParsedData, &ParseError));
	if (!ParseError.IsEmpty())
	{
		AddInfo(ParseError);
	}

	TestTrue(TEXT("Parser normalizes texture path into canonical image owner"),
		ParsedData.RootWidget.Brush.Image.TexturePath.IsSet()
		&& ParsedData.RootWidget.Brush.Image.TexturePath.GetValue() == TEXT("/Game/UI/T_CanonicalBrush.T_CanonicalBrush"));
	TestFalse(TEXT("Normalized output has no duplicate root owner"),
		FUMGJsonSerializer::SerializeWidgetNode(ParsedData.RootWidget)->GetObjectField(TEXT("Brush"))->HasField(TEXT("TexturePath")));
	TestEqual(TEXT("Typed accessor exposes canonical image texture path"),
		GetWidgetProperty(ParsedData.RootWidget, TEXT("TexturePath")),
		FString(TEXT("/Game/UI/T_CanonicalBrush.T_CanonicalBrush")));
	TestTrue(TEXT("Conflict JSON preserves parsed crop transform"),
		ParsedData.RootWidget.Brush.Image.Transform.IsSet()
		&& FMath::IsNearlyEqual(ParsedData.RootWidget.Brush.Image.Transform.GetValue().A, 0.5f, 0.0001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrushNegativeRadiiStayUnsetJsonTest,
	"Design2UMG.UMGJson.BrushNegativeRadiiStayUnset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBrushNegativeRadiiStayUnsetJsonTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"RootWidget": {
			"Type": "Image",
			"Id": "root",
			"Name": "NegativeRadii",
			"Brush": {
				"OutlineCornerRadius": -1,
				"CornerRadii": "-1,-1,-1,-1"
			}
		},
		"WidgetName": "WBP_NegativeRadii"
	})");

	FUMGWidgetData ParsedData;
	FString ParseError;
	TestTrue(TEXT("Negative-radii JSON parses"), FUMGJsonParser::ParseJson(Json, ParsedData, &ParseError));
	if (!ParseError.IsEmpty())
	{
		AddInfo(ParseError);
	}

	TestFalse(TEXT("Negative OutlineCornerRadius stays unset (legacy encoding)"),
		ParsedData.RootWidget.Brush.OutlineCornerRadius.IsSet());
	TestFalse(TEXT("Negative CornerRadii.X stays unset (legacy encoding)"),
		ParsedData.RootWidget.Brush.CornerRadii.IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTypedBrushImageGradientJsonRoundTripTest,
	"Design2UMG.UMGJson.TypedBrushImageGradientRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTypedBrushImageGradientJsonRoundTripTest::RunTest(const FString& Parameters)
{
	FUMGWidgetData SourceData;
	SourceData.WidgetName = TEXT("WBP_TypedBrushRoundTrip");
	SourceData.RootWidget.Type = TEXT("CanvasPanel");
	SourceData.RootWidget.Id = TEXT("root");
	SourceData.RootWidget.Name = TEXT("Root");

	FUMGWidgetNode ImageNode;
	ImageNode.Type = TEXT("Image");
	ImageNode.Id = TEXT("typed_image");
	ImageNode.Name = TEXT("TypedImage");
	ImageNode.Brush.Image.TexturePath = TEXT("/Game/UI/T_TypedImage.T_TypedImage");
	ImageNode.Brush.Image.Mode = TEXT("Crop");
	FUMGVisualBrushData::FImageTransformData ImageTransformData;
	ImageTransformData.A = 1.0f;
	ImageTransformData.B = 0.0f;
	ImageTransformData.TX = 0.125f;
	ImageTransformData.C = 0.0f;
	ImageTransformData.D = 1.0f;
	ImageTransformData.TY = -0.25f;
	ImageNode.Brush.Image.Transform = ImageTransformData;

	FUMGWidgetNode GradientNode;
	GradientNode.Type = TEXT("Image");
	GradientNode.Id = TEXT("typed_gradient");
	GradientNode.Name = TEXT("TypedGradient");
	GradientNode.Brush.Gradient.Type = TEXT("Angular");
	GradientNode.Brush.Gradient.Rotation = 135.0f;
	GradientNode.Brush.Gradient.StartPoint = FVector2D(0.15f, 0.25f);
	GradientNode.Brush.Gradient.EndPoint = FVector2D(0.85f, 0.75f);
	GradientNode.Brush.Gradient.Center = FVector2D(0.45f, 0.55f);
	GradientNode.Brush.Gradient.Size = FVector2D(0.8f, 1.1f);
	{
		FUMGVisualBrushData::FGradientStopData& Stop0 = GradientNode.Brush.Gradient.Stops.AddDefaulted_GetRef();
		Stop0.Color = FLinearColor::Red;
		Stop0.Position = 0.0f;
		FUMGVisualBrushData::FGradientStopData& Stop1 = GradientNode.Brush.Gradient.Stops.AddDefaulted_GetRef();
		Stop1.Color = FLinearColor::Green;
		Stop1.Position = 0.5f;
		FUMGVisualBrushData::FGradientStopData& Stop2 = GradientNode.Brush.Gradient.Stops.AddDefaulted_GetRef();
		Stop2.Color = FLinearColor::Blue;
		Stop2.Position = 1.0f;
	}

	SourceData.RootWidget.Children.Add(MoveTemp(ImageNode));
	SourceData.RootWidget.Children.Add(MoveTemp(GradientNode));

	const FString Json = FUMGJsonSerializer::Serialize(SourceData);
	TestTrue(TEXT("Typed brush round-trip emits nested image payload"), Json.Contains(TEXT("\"Image\"")));
	TestTrue(TEXT("Typed brush round-trip emits nested gradient payload"), Json.Contains(TEXT("\"Gradient\"")));

	FUMGWidgetData ParsedData;
	FString ParseError;
	TestTrue(TEXT("Typed brush round-trip parses"), FUMGJsonParser::ParseJson(Json, ParsedData, &ParseError));
	if (!ParseError.IsEmpty())
	{
		AddInfo(ParseError);
	}

	const FUMGWidgetNode* ParsedImageNode = FindWidgetById(ParsedData.RootWidget, TEXT("typed_image"));
	const FUMGWidgetNode* ParsedGradientNode = FindWidgetById(ParsedData.RootWidget, TEXT("typed_gradient"));
	TestTrue(TEXT("Parsed typed image node found"), ParsedImageNode != nullptr);
	TestTrue(TEXT("Parsed typed gradient node found"), ParsedGradientNode != nullptr);
	if (!ParsedImageNode || !ParsedGradientNode)
	{
		return false;
	}

	TestTrue(TEXT("Typed image texture path preserved"), ParsedImageNode->Brush.Image.TexturePath.IsSet() && ParsedImageNode->Brush.Image.TexturePath.GetValue() == TEXT("/Game/UI/T_TypedImage.T_TypedImage"));
	TestTrue(TEXT("Typed image mode preserved"), ParsedImageNode->Brush.Image.Mode.IsSet() && ParsedImageNode->Brush.Image.Mode.GetValue() == TEXT("Crop"));
	TestTrue(TEXT("Typed image transform preserved"), ParsedImageNode->Brush.Image.Transform.IsSet() && FMath::IsNearlyEqual(ParsedImageNode->Brush.Image.Transform.GetValue().TX, 0.125f, 0.0001f));
	TestEqual(TEXT("Typed image helper still exposes texture path"), GetWidgetProperty(*ParsedImageNode, TEXT("TexturePath")), TEXT("/Game/UI/T_TypedImage.T_TypedImage"));

	TestTrue(TEXT("Typed gradient type preserved"), ParsedGradientNode->Brush.Gradient.Type.IsSet() && ParsedGradientNode->Brush.Gradient.Type.GetValue() == TEXT("Angular"));
	TestTrue(TEXT("Typed gradient rotation preserved"), ParsedGradientNode->Brush.Gradient.Rotation.IsSet() && FMath::IsNearlyEqual(ParsedGradientNode->Brush.Gradient.Rotation.GetValue(), 135.0f, 0.001f));
	TestTrue(TEXT("Typed gradient start point preserved"), ParsedGradientNode->Brush.Gradient.StartPoint.IsSet()
		&& FMath::IsNearlyEqual(ParsedGradientNode->Brush.Gradient.StartPoint.GetValue().X, 0.15f, 0.001f)
		&& FMath::IsNearlyEqual(ParsedGradientNode->Brush.Gradient.StartPoint.GetValue().Y, 0.25f, 0.001f));
	TestTrue(TEXT("Typed gradient end point preserved"), ParsedGradientNode->Brush.Gradient.EndPoint.IsSet()
		&& FMath::IsNearlyEqual(ParsedGradientNode->Brush.Gradient.EndPoint.GetValue().X, 0.85f, 0.001f)
		&& FMath::IsNearlyEqual(ParsedGradientNode->Brush.Gradient.EndPoint.GetValue().Y, 0.75f, 0.001f));
	TestTrue(TEXT("Typed gradient stop count preserved"), ParsedGradientNode->Brush.Gradient.Stops.Num() == 3);
	TestEqual(TEXT("Typed gradient helper exposes material ref"), GetWidgetProperty(*ParsedGradientNode, TEXT("MaterialRef")), TEXT("angulargradient"));
	TestTrue(TEXT("Typed gradient helper exposes material params"), GetWidgetProperty(*ParsedGradientNode, TEXT("MaterialParams")).Contains(TEXT("\"Stops\"")));
	return true;
}

#endif

