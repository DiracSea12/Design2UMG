// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGWidgetPropertyAccess.h"
#include "Design2UMGAutomationTestAdapter.h"
#include "Misc/AutomationTest.h"
#include "Algo/Count.h"
#include "UMGJsonParser.h"
#include "UMGImageCropUtils.h"
#include "UMGSchema.h"
#include "UMGValueParsing.h"
#include "UMGWidgetNodeAccess.h"
#include "UMGWidgetTreeBuilder.h"
#include "Components/TextBlock.h"
#include "UObject/Package.h"

#if WITH_AUTOMATION_WORKER

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImageTransformStrictParsingTest,
	"Design2UMG.NumericParsing.ImageTransform",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FImageTransformStrictParsingTest::RunTest(const FString& Parameters)
{
	using namespace Design2UMGWidgetNodeAccess;
	const FUMGVisualBrushData::FImageTransformData Expected{ 1.25f, 0.5f, -0.375f, 0.2f, 3.0f, -0.0f };
	FUMGVisualBrushData::FImageTransformData Parsed;
	TestTrue(TEXT("Signed decimal and exponent components parse"),
		TryParseImageTransform(TEXT(" 1.25e0, +0.5, -.375, 2E-1, 3., -0 "), Parsed));
	TestEqual(TEXT("All six components keep their order and values"), SerializeImageTransform(Parsed), SerializeImageTransform(Expected));
	TestTrue(TEXT("Serialized transform parses"), TryParseImageTransform(SerializeImageTransform(Expected), Parsed));
	TestEqual(TEXT("Transform round trip"), SerializeImageTransform(Parsed), SerializeImageTransform(Expected));
	TestTrue(TEXT("Zero is valid, not missing"), TryParseImageTransform(TEXT("0,0,0,0,0,0"), Parsed));
	TestEqual(TEXT("Zero transform is not replaced by identity"), Parsed.A, 0.0f);

	const TCHAR* InvalidComponents[] = {
		TEXT(""), TEXT(" "), TEXT("bogus"), TEXT("1junk"), TEXT("0junk"), TEXT("1 2"),
		TEXT("."), TEXT("+"), TEXT("--1"), TEXT("+-1"), TEXT("1e"), TEXT("1e+"),
		TEXT("NaN"), TEXT("nan"), TEXT("Inf"), TEXT("-Infinity"), TEXT("1e39"), TEXT("-1e400")
	};
	for (const TCHAR* Invalid : InvalidComponents)
	{
		for (int32 Index = 0; Index < 6; ++Index)
		{
			TArray<FString> Parts{ TEXT("2"), TEXT("3"), TEXT("5"), TEXT("7"), TEXT("11"), TEXT("13") };
			Parts[Index] = Invalid;
			const FString Input = FString::Join(Parts, TEXT(","));
			Parsed = Expected;
			TestFalse(FString::Printf(TEXT("Reject component %d in '%s'"), Index, *Input), TryParseImageTransform(Input, Parsed));
			TestEqual(TEXT("Failure leaves every output component unchanged"), SerializeImageTransform(Parsed), SerializeImageTransform(Expected));
		}
	}
	for (const TCHAR* Input : { TEXT("1,0,0,0,1"), TEXT("1,0,0,0,1,0,7"), TEXT(",1,0,0,0,1,0"), TEXT("1,,0,0,0,1,0"), TEXT("1,0,0,0,1,0,") })
	{
		Parsed = Expected;
		TestFalse(FString::Printf(TEXT("Reject wrong arity or empty component: '%s'"), Input), TryParseImageTransform(Input, Parsed));
		TestEqual(TEXT("Arity failure preserves output"), SerializeImageTransform(Parsed), SerializeImageTransform(Expected));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImageTransformJsonValidationTest,
	"Design2UMG.NumericParsing.ImageTransformJson",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FImageTransformJsonValidationTest::RunTest(const FString& Parameters)
{
	Design2UMGAutomationTestAdapter::FScopedExpectedMessages ExpectedLogMessages(*this);
	const TCHAR* Inputs[] = { TEXT("1,0,0,0,1,0"), TEXT("bad,0,0,0,1,0"), TEXT("1,0,0,0,1,NaN"), TEXT("1,0,1e39,0,1,0"), TEXT("1,,0,0,0,1,0"), TEXT("1,0,0,0,1,0junk") };
	ExpectedLogMessages.Expect(TEXT("FUMGJsonParser: Invalid Brush.Image.Transform"), ELogVerbosity::Warning, Design2UMGAutomationTestAdapter::EMessageMatch::Contains, UE_ARRAY_COUNT(Inputs) - 1);
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Inputs); ++Index)
	{
		const bool bValid = Index == 0;

		const FString Json = FString::Printf(TEXT(R"({"RootWidget":{"Type":"Image","Name":"NumericInput","Brush":{"Image":{"Transform":"%s"}}}})"), Inputs[Index]);
		FUMGWidgetData Data;
		TestTrue(TEXT("Malformed optional transform does not reject the widget"), FUMGJsonParser::ParseJson(Json, Data));
		TestEqual(TEXT("Typed transform presence reflects successful parsing"), Data.RootWidget.Brush.Image.Transform.IsSet(), bValid);

		FUMGWidgetData AliasData;
		TestTrue(TEXT("Removed alias-only widget still parses"), FUMGJsonParser::ParseJson(
			FString::Printf(TEXT(R"({"RootWidget":{"Type":"Image","Name":"AliasInput","WidgetStyle":{"ImageTransform":"%s"}}})"), Inputs[Index]), AliasData));
		TestFalse(TEXT("Removed style alias is not read for any input"), AliasData.RootWidget.Brush.Image.Transform.IsSet());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCornerRadiiStrictParsingTest,
	"Design2UMG.NumericParsing.CornerRadii",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCornerRadiiStrictParsingTest::RunTest(const FString& Parameters)
{
	Design2UMGAutomationTestAdapter::FScopedExpectedMessages ExpectedLogMessages(*this);
	const TCHAR* Inputs[] = {
		TEXT("1e1, +2.5, .75, 4."), TEXT("bad,2,3,4"), TEXT("1,NaN,3,4"), TEXT("1,2,Inf,4"),
		TEXT("1,2,3,1e39"), TEXT("1,2,3,4junk"), TEXT("1,2,3,0junk"), TEXT("1,2,3,1e+"),
		TEXT("1,,2,3,4"), TEXT(",1,2,3,4"), TEXT("1,2,3,4,"), TEXT("1,2,3"), TEXT("1,2,3,4,5")
	};
	ExpectedLogMessages.Expect(TEXT("FUMGJsonParser: Invalid Brush.CornerRadii"), ELogVerbosity::Warning, Design2UMGAutomationTestAdapter::EMessageMatch::Contains, UE_ARRAY_COUNT(Inputs) - 1);
	ExpectedLogMessages.Expect(TEXT("FUMGJsonParser: Invalid Effect.GeometryMaskCornerRadii"), ELogVerbosity::Warning, Design2UMGAutomationTestAdapter::EMessageMatch::Contains, UE_ARRAY_COUNT(Inputs) - 1);
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Inputs); ++Index)
	{
		const bool bValid = Index == 0;

		const FString Json = FString::Printf(TEXT(R"({"RootWidget":{"Type":"Image","Name":"NumericInput","Brush":{"CornerRadii":"%s"},"Effect":{"GeometryMaskCornerRadii":"%s"}}})"), Inputs[Index], Inputs[Index]);
		FUMGWidgetData Data;
		TestTrue(TEXT("Optional radii failure does not reject the widget"), FUMGJsonParser::ParseJson(Json, Data));
		TestEqual(TEXT("Brush radii are only set for valid input"), Data.RootWidget.Brush.CornerRadii.IsSet(), bValid);
		TestEqual(TEXT("Mask radii use the same strict numeric contract"), Data.RootWidget.Effect.GeometryMaskCornerRadii.IsSet(), bValid);
		if (bValid)
		{
			const FVector4 Expected(10, 2.5, 0.75, 4);
			TestTrue(TEXT("Brush radii preserve order"), Data.RootWidget.Brush.CornerRadii.IsSet() && Data.RootWidget.Brush.CornerRadii.GetValue().Equals(Expected));
			TestTrue(TEXT("Mask radii preserve order"), Data.RootWidget.Effect.GeometryMaskCornerRadii.IsSet() && Data.RootWidget.Effect.GeometryMaskCornerRadii.GetValue().Equals(Expected));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPaddingStrictParsingTest,
	"Design2UMG.NumericParsing.Padding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPaddingStrictParsingTest::RunTest(const FString& Parameters)
{
	Design2UMGAutomationTestAdapter::FScopedExpectedMessages ExpectedLogMessages(*this);
	struct FCase { const TCHAR* JsonValue; TOptional<FMargin> Expected; };
	const FCase Cases[] = {
		{ TEXT("\" +2.5e0 \""), FMargin(2.5f) }, { TEXT("\"-1, 2e0, .5, 4.\""), FMargin(-1, 2, 0.5f, 4) },
		{ TEXT("\"0\""), FMargin(0) }, { TEXT("\"bad\""), {} }, { TEXT("\"NaN\""), {} },
		{ TEXT("\"1e39\""), {} }, { TEXT("\"1,2,3,Inf\""), {} }, { TEXT("\"1,2,3,4junk\""), {} },
		{ TEXT("\"1,,2,3,4\""), {} }, { TEXT("\"1,2,3,4,\""), {} }, { TEXT("\"1,2\""), {} },
		{ TEXT("\"\""), {} }, { TEXT("null"), {} }, { TEXT("false"), {} }, { TEXT("10"), {} }
	};
	ExpectedLogMessages.Expect(TEXT("FUMGJsonParser: Invalid padding"), ELogVerbosity::Warning, Design2UMGAutomationTestAdapter::EMessageMatch::Contains, 6 * Algo::CountIf(Cases, [](const FCase& Case) { return !Case.Expected.IsSet(); }));
	for (const FCase& Case : Cases)
	{
		const FString Json = FString::Printf(TEXT(R"({"RootWidget":{"Type":"Image","Name":"NumericInput","WidgetStyle":{"Padding":%s,"SlotPadding":%s,"StrokePadding":%s,"StrokeMargin":%s},"Brush":{"BrushMargin":%s},"Slot":{"Padding":%s}}})"), Case.JsonValue, Case.JsonValue, Case.JsonValue, Case.JsonValue, Case.JsonValue, Case.JsonValue);
		FUMGWidgetData Data;
		TestTrue(TEXT("Optional padding failure does not reject the widget"), FUMGJsonParser::ParseJson(Json, Data));
		const FUMGWidgetStyleData& Style = Data.RootWidget.WidgetStyle;
		for (const TOptional<FMargin>& Actual : { Style.Padding, Style.SlotPadding, Style.StrokePadding, Style.StrokeMargin, Data.RootWidget.Brush.Margin })
		{
			TestEqual(TEXT("Bad padding is absent rather than silently zeroed"), Actual.IsSet(), Case.Expected.IsSet());
			if (Actual.IsSet() && Case.Expected.IsSet())
			{
				TestTrue(TEXT("All padding consumers preserve the parsed values"), FUMGValueParser::IsNearlyEqualMargin(Actual.GetValue(), Case.Expected.GetValue()));
			}
		}
		TestTrue(TEXT("Slot receives valid padding or keeps its default"), FUMGValueParser::IsNearlyEqualMargin(Data.RootWidget.Slot.Padding, Case.Expected.Get(FMargin(0))));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVector2DStrictParsingTest,
	"Design2UMG.NumericParsing.Vector2D",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FVector2DStrictParsingTest::RunTest(const FString& Parameters)
{
	Design2UMGAutomationTestAdapter::FScopedExpectedMessages ExpectedLogMessages(*this);
	TestTrue(TEXT("Vector accepts signed numbers and exponents"), FUMGValueParser::ParseVector2D(TEXT(" +1.25e0, -2.5E1 ")).Equals(FVector2D(1.25, -25)));
	const FVector2D Default(9, 11);
	const TCHAR* InvalidInputs[] = { TEXT("bad,2"), TEXT("1,NaN"), TEXT("Inf,2"), TEXT("1,1e39"), TEXT("1,2junk"), TEXT("0junk,2"), TEXT("1e+,2"), TEXT("1 2,3"), TEXT("1,,2"), TEXT(",1,2"), TEXT("1,2,"), TEXT("1"), TEXT("") };
	ExpectedLogMessages.Expect(TEXT("Invalid vector"), ELogVerbosity::Warning, Design2UMGAutomationTestAdapter::EMessageMatch::Contains, UE_ARRAY_COUNT(InvalidInputs));
	for (const TCHAR* Input : InvalidInputs)
	{
		TestTrue(FString::Printf(TEXT("Invalid vector '%s' retains caller default"), Input), FUMGValueParser::ParseVector2D(Input, Default).Equals(Default));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImageCropStrictParsingTest,
	"Design2UMG.NumericParsing.CropRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FImageCropStrictParsingTest::RunTest(const FString& Parameters)
{
	Design2UMGAutomationTestAdapter::FScopedExpectedMessages ExpectedLogMessages(*this);
	const auto ParseCropNode = [this](const FString& Transform)
	{
		FUMGWidgetData Data;
		TestTrue(TEXT("Crop input parses"), FUMGJsonParser::ParseJson(FString::Printf(
			TEXT(R"({"RootWidget":{"Type":"Image","Name":"CropInput","Brush":{"Image":{"Mode":"Crop","Transform":"%s"}},"WidgetStyle":{"WidthOverride":120,"HeightOverride":80}}})"),
			*Transform), Data));
		return Data.RootWidget;
	};
	FUMGWidgetNode Node;
	Node.Brush.Image.Mode = TEXT("Crop");
	UMGImageCropUtils::FImageCropRequest Request;
	TestFalse(TEXT("Missing transform does not create a crop request"), UMGImageCropUtils::TryBuildCropRequest(Node, Request));
	Node = ParseCropNode(TEXT(".5,0,.25,0,.75,.1"));
	TestTrue(TEXT("Validated input creates a crop request"), UMGImageCropUtils::TryBuildCropRequest(Node, Request));
	TestEqual(TEXT("Crop width is preserved"), Request.OutputWidth, 120);
	TestEqual(TEXT("Crop height is preserved"), Request.OutputHeight, 80);
	const FString Previous = Design2UMGWidgetNodeAccess::SerializeImageTransform(Request.Transform);
	const TCHAR* InvalidInputs[] = { TEXT("1,0,0,0,1,junk"), TEXT("1,0,0,0,1,NaN"), TEXT("1,0,0,0,1,1e39"), TEXT("1,,0,0,0,1,0") };
	ExpectedLogMessages.Expect(TEXT("FUMGJsonParser: Invalid Brush.Image.Transform"), ELogVerbosity::Warning, Design2UMGAutomationTestAdapter::EMessageMatch::Contains, UE_ARRAY_COUNT(InvalidInputs));
	for (const TCHAR* Input : InvalidInputs)
	{
		Node = ParseCropNode(Input);
		TestFalse(TEXT("Malformed input cannot reach cropping"), UMGImageCropUtils::TryBuildCropRequest(Node, Request));
		TestEqual(TEXT("Rejected transform leaves the previous request untouched"), Design2UMGWidgetNodeAccess::SerializeImageTransform(Request.Transform), Previous);
	}
	Node = ParseCropNode(TEXT("1,0,0,0,1,0"));
	TestTrue(TEXT("Typed identity reaches the crop request unchanged"), UMGImageCropUtils::TryBuildCropRequest(Node, Request));
	TestTrue(TEXT("Identity transform is preserved"), Request.Transform.IsIdentity());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVectorJsonFailurePresenceTest,
	"Design2UMG.NumericParsing.VectorJsonPresence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FVectorJsonFailurePresenceTest::RunTest(const FString& Parameters)
{
	Design2UMGAutomationTestAdapter::FScopedExpectedMessages ExpectedLogMessages(*this);
	struct FCase { const TCHAR* Value; TOptional<FVector2D> Expected; };
	const FCase Cases[] = {
		{ TEXT("\"1.25,2.5\""), FVector2D(1.25, 2.5) }, { TEXT("\"0,0\""), FVector2D::ZeroVector },
		{ TEXT("\"bad,2\""), {} }, { TEXT("\"1,,2\""), {} }, { TEXT("\"1,NaN\""), {} },
		{ TEXT("\"1,1e39\""), {} }, { TEXT("\"1,2junk\""), {} }, { TEXT("\"\""), {} },
		{ TEXT("[1,2]"), {} }, { TEXT("{}"), {} }, { TEXT("null"), {} }, { TEXT("false"), {} }, { TEXT("12"), {} }
	};
	ExpectedLogMessages.Expect(TEXT("FUMGJsonParser: Invalid vector"), ELogVerbosity::Warning, Design2UMGAutomationTestAdapter::EMessageMatch::Contains, 13 * Algo::CountIf(Cases, [](const FCase& Case) { return !Case.Expected.IsSet(); }));
	for (const FCase& Case : Cases)
	{
		const FString Json = FString::Printf(TEXT(R"({"RootWidget":{"Type":"TextBlock","Name":"VectorInput",
			"WidgetStyle":{"InnerSlotPadding":%s},
			"Brush":{"Gradient":{"StartPoint":%s,"EndPoint":%s,"Center":%s,"Size":%s}},
			"Effect":{"EffectBlurUVStep":%s,"EffectOffset":%s,"GeometryMaskSize":%s},
			"ShadowData":{"ShadowOffset":%s,"BoxShadows":[{"Offset":%s}]},
			"Render":{"RenderTransformPivot":%s,"RenderTransformScale":%s,"RenderTranslation":%s}}})"),
			Case.Value, Case.Value, Case.Value, Case.Value, Case.Value, Case.Value, Case.Value,
			Case.Value, Case.Value, Case.Value, Case.Value, Case.Value, Case.Value);
		FUMGWidgetData Data;
		TestTrue(TEXT("Invalid optional vectors do not reject the widget"), FUMGJsonParser::ParseJson(Json, Data));
		const FUMGWidgetNode& Node = Data.RootWidget;
		for (const TOptional<FVector2D>& Actual : { Node.WidgetStyle.InnerSlotPadding, Node.Brush.Gradient.StartPoint,
			Node.Brush.Gradient.EndPoint, Node.Brush.Gradient.Center, Node.Brush.Gradient.Size, Node.Effect.EffectBlurUVStep,
			Node.Effect.EffectOffset, Node.Effect.GeometryMaskSize, Node.Shadow.ShadowOffset,
			Node.Render.RenderTransformPivot, Node.Render.RenderTransformScale, Node.Render.RenderTranslation })
		{
			TestEqual(TEXT("Vector presence reflects parsing success, not fallback"), Actual.IsSet(), Case.Expected.IsSet());
			if (Actual.IsSet() && Case.Expected.IsSet())
			{
				TestTrue(TEXT("Valid vector components are preserved"), Actual->Equals(*Case.Expected));
			}
		}
		if (TestEqual(TEXT("Shadow layer remains present"), Node.Shadow.BoxShadows.Num(), 1))
		{
			TestTrue(TEXT("Non-optional offset retains its default only on failure"), Node.Shadow.BoxShadows[0].Offset.Equals(Case.Expected.Get(FVector2D::ZeroVector)));
		}
		UTextBlock* Widget = NewObject<UTextBlock>(GetTransientPackage());
		const FVector2D ExistingScale(3, 4);
		Widget->SetRenderScale(ExistingScale);
		FUMGWidgetTreeBuilder::ApplyWidgetProperties(Widget, Node);
		TestTrue(TEXT("Invalid scale does not change the real widget; explicit zero still applies"), Design2UMGWidgetPropertyAccess::GetRenderTransform(*Widget).Scale.Equals(Case.Expected.Get(ExistingScale)));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNumericJsonWrongTypeTest,
	"Design2UMG.NumericParsing.JsonWrongTypes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FNumericJsonWrongTypeTest::RunTest(const FString& Parameters)
{
	Design2UMGAutomationTestAdapter::FScopedExpectedMessages ExpectedLogMessages(*this);
	FUMGWidgetData Empty;
	TestTrue(TEXT("Missing numeric fields are allowed without warnings"), FUMGJsonParser::ParseJson(TEXT(R"({"RootWidget":{"Type":"Image","Name":"NoFields"}})"), Empty));
	const TCHAR* InvalidValues[] = { TEXT("null"), TEXT("[1,2,3,4]"), TEXT("{}"), TEXT("false"), TEXT("123"), TEXT("\"\"") };
	for (const TCHAR* Field : { TEXT("Brush.Image.Transform"), TEXT("Brush.CornerRadii"), TEXT("Effect.GeometryMaskCornerRadii") })
	{
		ExpectedLogMessages.Expect(FString::Printf(TEXT("FUMGJsonParser: Invalid %s"), Field), ELogVerbosity::Warning, Design2UMGAutomationTestAdapter::EMessageMatch::Contains, UE_ARRAY_COUNT(InvalidValues));
	}
	for (const TCHAR* Value : InvalidValues)
	{
		const FString Json = FString::Printf(TEXT(R"({"RootWidget":{"Type":"Image","Name":"WrongType",
			"Brush":{"Image":{"Transform":%s},"CornerRadii":%s},
			"Effect":{"GeometryMaskCornerRadii":%s}}})"), Value, Value, Value);
		FUMGWidgetData Data;
		TestTrue(TEXT("Bad optional field is diagnosed without losing the widget"), FUMGJsonParser::ParseJson(Json, Data));
		TestFalse(TEXT("Invalid canonical transform stays absent"), Data.RootWidget.Brush.Image.Transform.IsSet());
		TestFalse(TEXT("Invalid brush radii stay absent"), Data.RootWidget.Brush.CornerRadii.IsSet());
		TestFalse(TEXT("Invalid mask radii stay absent"), Data.RootWidget.Effect.GeometryMaskCornerRadii.IsSet());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNumericJsonFiniteScalarTest,
	"Design2UMG.NumericParsing.FiniteScalars",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FNumericJsonFiniteScalarTest::RunTest(const FString& Parameters)
{
	Design2UMGAutomationTestAdapter::FScopedExpectedMessages ExpectedLogMessages(*this);
	struct FCase { const TCHAR* Value; bool bValid; TOptional<float> Radius; float Angle; };
	const FCase Cases[] = {
		{ TEXT("2.5"), true, 2.5f, 2.5f }, { TEXT("0"), true, 0.0f, 0.0f },
		{ TEXT("-1"), true, {}, -1.0f }, { TEXT("-1e-50"), true, {}, 0.0f },
		{ TEXT("1e38"), true, 1e38f, 1e38f }, { TEXT("1e39"), false, {}, 0.0f },
		{ TEXT("-1e39"), false, {}, 0.0f }, { TEXT("\"NaN\""), false, {}, 0.0f },
		{ TEXT("null"), false, {}, 0.0f }, { TEXT("[]"), false, {}, 0.0f }
	};
	const int32 InvalidCount = Algo::CountIf(Cases, [](const FCase& Case) { return !Case.bValid; });
	ExpectedLogMessages.Expect(TEXT("FUMGJsonParser: Invalid OutlineCornerRadius"), ELogVerbosity::Warning, Design2UMGAutomationTestAdapter::EMessageMatch::Contains, InvalidCount);
	ExpectedLogMessages.Expect(TEXT("FUMGJsonParser: Invalid RenderAngle"), ELogVerbosity::Warning, Design2UMGAutomationTestAdapter::EMessageMatch::Contains, InvalidCount);
	for (const FCase& Case : Cases)
	{
		const FString Json = FString::Printf(TEXT(R"({"RootWidget":{"Type":"Image","Name":"ScalarInput","Brush":{"OutlineCornerRadius":%s},"Render":{"RenderAngle":%s}}})"), Case.Value, Case.Value);
		FUMGWidgetData Data;
		TestTrue(TEXT("Invalid optional scalars do not reject the widget"), FUMGJsonParser::ParseJson(Json, Data));
		TestEqual(TEXT("Radius validity and negative sentinel checked before narrowing"), Data.RootWidget.Brush.OutlineCornerRadius.IsSet(), Case.Radius.IsSet());
		TestEqual(TEXT("Invalid angle is not assigned"), Data.RootWidget.Render.RenderAngle.IsSet(), Case.bValid);
		if (Case.Radius.IsSet() && Data.RootWidget.Brush.OutlineCornerRadius.IsSet())
		{
			TestEqual(TEXT("Valid radius preserved"), *Data.RootWidget.Brush.OutlineCornerRadius, *Case.Radius);
		}
		if (Case.bValid && Data.RootWidget.Render.RenderAngle.IsSet())
		{
			TestEqual(TEXT("Valid angle preserved"), *Data.RootWidget.Render.RenderAngle, Case.Angle);
		}
		TestTrue(TEXT("Radius remains finite"), FMath::IsFinite(Data.RootWidget.Brush.OutlineCornerRadius.Get(0.0f)));
		TestTrue(TEXT("Angle remains finite"), FMath::IsFinite(Data.RootWidget.Render.RenderAngle.Get(0.0f)));
	}
	return true;
}

#endif
