// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGWidgetPropertyAccess.h"
#include "Design2UMGAutomationTestAdapter.h"
#include "Design2UMGFontAccess.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "PreBuildResourceNormalizer.h"
#include "ProceduralShapeGenerator.h"
#include "Tests/Design2UMGTestAssetCleanup.h"
#include "UMGJsonParser.h"
#include "UMGJsonSerializer.h"
#include "UMGPropertyKeys.h"
#include "UMGTextStyleApplication.h"
#include "UMGValueParsing.h"
#include "UMGWidgetNodeProceduralResourceAccess.h"
#include "UMGWidgetTreeBuilder.h"
#include "Components/RichTextBlock.h"
#include "Dom/JsonObject.h"
#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"
#include "RHI.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TextureResource.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"

#if WITH_AUTOMATION_WORKER
namespace
{
	FUMGVisualBrushData::FShapeData MakeShape()
	{
		FUMGVisualBrushData::FShapeData Shape;
		Shape.Color = FUMGValueParser::ParseColor(TEXT("#33669980"));
		Shape.Thickness = 0.25f;
		Shape.CornerRadius = 0.25f;
		Shape.CornerRadii = FVector4(0.1f, 0.2f, 0.3f, 0.4f);
		Shape.BlurPadding = 4.125f;
		Shape.StripeColor = FUMGValueParser::ParseColor(TEXT("#9977BBCC"));
		Shape.StripeWidth = 2;
		Shape.StripeGap = 5;
		Shape.Inset = 0;
		return Shape;
	}

	TArray<uint8> ReadPixels(UTexture2D* Texture)
	{
		TArray<uint8> Pixels;
		if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.IsEmpty()) return Pixels;
		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		const uint8* Data = static_cast<const uint8*>(Mip.BulkData.LockReadOnly());
		if (Data) Pixels.Append(Data, Mip.SizeX * Mip.SizeY * sizeof(FColor));
		Mip.BulkData.Unlock();
		return Pixels;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTypedShapeRequestAndSerializationCycleTest,
	"Design2UMG.TypedPayload.ShapeRequestAndSerializationCycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTypedShapeRequestAndSerializationCycleTest::RunTest(const FString&)
{
	FUMGWidgetData Source;
	Source.WidgetName = TEXT("WBP_TypedShape");
	FUMGWidgetNode& Node = Source.RootWidget;
	Node.Type = TEXT("Image");
	Node.Name = TEXT("Shape");
	Node.Brush.MaterialRef = TEXT("placeholdersurface");
	Node.Brush.Shape = MakeShape();
	const FString UserParams = TEXT(R"({"UserDefined":{"Gain":0,"Labels":["a,b","x=y"]}})");
	Node.Brush.MaterialParams = UserParams;

	FUMGWidgetData Parsed;
	if (!TestTrue(TEXT("Canonical shape round trip parses"), FUMGJsonParser::ParseJson(FUMGJsonSerializer::Serialize(Source), Parsed))) return false;
	TestTrue(TEXT("All shape fields and their presence survive the boundary"), Parsed.RootWidget.Brush.Shape == Node.Brush.Shape);
	TestEqual(TEXT("Open user parameters remain opaque and unchanged"), Parsed.RootWidget.Brush.MaterialParams.Get(FString()), UserParams);
	FUMGProceduralResourceRequest Request;
	TestTrue(TEXT("Typed shape builds a resource request"), Design2UMGWidgetNodeProceduralResourceAccess::TryBuildProceduralResourceRequest(Parsed.RootWidget, Request));
	TestTrue(TEXT("The request keeps the typed shape"), Request.Shape == Node.Brush.Shape);
	TestFalse(TEXT("Typed shapes never fall back to user JSON"), Request.MaterialParams.IsSet());

	Node.Brush.Shape = FUMGVisualBrushData::FShapeData{};
	Parsed = FUMGWidgetData{};
	TestTrue(TEXT("Explicit empty shape round trip parses"), FUMGJsonParser::ParseJson(FUMGJsonSerializer::Serialize(Source), Parsed));
	TestTrue(TEXT("An empty shape remains present"), Parsed.RootWidget.Brush.Shape.IsSet());
	TestTrue(TEXT("An empty shape request builds"), Design2UMGWidgetNodeProceduralResourceAccess::TryBuildProceduralResourceRequest(Parsed.RootWidget, Request));
	TestFalse(TEXT("Empty typed shape is authoritative, not a partial JSON merge"), Request.MaterialParams.IsSet());

	Node.Brush.Shape.Reset();
	Parsed = FUMGWidgetData{};
	TestTrue(TEXT("External-only parameters round trip"), FUMGJsonParser::ParseJson(FUMGJsonSerializer::Serialize(Source), Parsed));
	TestFalse(TEXT("Absent shape is not synthesized"), Parsed.RootWidget.Brush.Shape.IsSet());
	TestTrue(TEXT("External-only request builds"), Design2UMGWidgetNodeProceduralResourceAccess::TryBuildProceduralResourceRequest(Parsed.RootWidget, Request));
	TestEqual(TEXT("Explicit external parameters reach their existing boundary"), Request.MaterialParams.Get(FString()), UserParams);

	Node.Brush.Shape = MakeShape();
	Node.Brush.Gradient.Type = TEXT("Linear");
	Node.Brush.Gradient.Stops = { { FLinearColor::Red, 0.0f }, { FLinearColor::Blue, 1.0f } };
	TestTrue(TEXT("Gradient request builds when other payloads are present"), Design2UMGWidgetNodeProceduralResourceAccess::TryBuildProceduralResourceRequest(Node, Request));
	TestEqual(TEXT("Existing gradient precedence is retained"), Request.MaterialRef, FString(TEXT("gradientlinear")));
	TestFalse(TEXT("Inactive shape is not copied into a gradient request"), Request.Shape.IsSet());
	TestFalse(TEXT("Inactive external JSON is not copied into a gradient request"), Request.MaterialParams.IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTypedShapeRasterParityTest,
	"Design2UMG.TypedPayload.ShapeRasterParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTypedShapeRasterParityTest::RunTest(const FString&)
{
	ON_SCOPE_EXIT { FProceduralShapeGenerator::ReleaseTransientTextures(); };
	FUMGProceduralResourceRequest Typed;
	Typed.Shape = MakeShape();
	Typed.WidthOverride = 48.0f;
	Typed.HeightOverride = 32.0f;
	Typed.ShadowBlur = 4.0f;
	FUMGProceduralResourceRequest External = Typed;
	External.Shape.Reset();
	// The existing external convention accepts percentages; the typed payload holds fractions.
	External.MaterialParams = TEXT(R"({"Color":"#33669980","Thickness":25,"CornerRadius":25,"CornerRadii":[10,20,30,40],"BlurPadding":4.125,"StripeColor":"#9977BBCC","StripeWidth":2,"StripeGap":5,"Inset":0})");
	for (const TCHAR* Material : { TEXT("roundedrect"), TEXT("boxshadow"), TEXT("ellipseshadow"), TEXT("circle"), TEXT("ring"), TEXT("horizontalstroke"), TEXT("placeholderhatch"), TEXT("placeholdersurface") })
	{
		Typed.MaterialRef = External.MaterialRef = Material;
		UTexture2D* TypedTexture = FProceduralShapeGenerator::GenerateFromMaterialRef(Typed, GetTransientPackage());
		UTexture2D* ExternalTexture = FProceduralShapeGenerator::GenerateFromMaterialRef(External, GetTransientPackage());
		if (!TestNotNull(TEXT("Typed resource generated"), TypedTexture) || !TestNotNull(TEXT("External resource generated"), ExternalTexture)) return false;
		TestEqual(TEXT("Typed input preserves the generator's raster width"), TypedTexture->GetSizeX(), ExternalTexture->GetSizeX());
		TestEqual(TEXT("Typed input preserves the generator's raster height"), TypedTexture->GetSizeY(), ExternalTexture->GetSizeY());
		const TArray<uint8> TypedPixels = ReadPixels(TypedTexture);
		const TArray<uint8> ExternalPixels = ReadPixels(ExternalTexture);
		TestEqual(FString::Printf(TEXT("%s exposes every generated pixel"), Material), TypedPixels.Num(), TypedTexture->GetSizeX() * TypedTexture->GetSizeY() * static_cast<int32>(sizeof(FColor)));
		TestTrue(FString::Printf(TEXT("%s pixels match the existing parameter input"), Material), TypedPixels == ExternalPixels);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTypedShapeCacheIsolationTest,
	"Design2UMG.TypedPayload.ShapeCacheIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTypedShapeCacheIsolationTest::RunTest(const FString&)
{
	if (GUsingNullRHI) return true;
	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/TypedShape_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	ON_SCOPE_EXIT { Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(OutputPath); FProceduralShapeGenerator::ReleaseTransientTextures(); };
	using FShape = FUMGVisualBrushData::FShapeData;
	struct FCase { const TCHAR* Field; const TCHAR* Material; TFunction<void(FShape&)> Change; };
	const FCase Cases[] = {
		{ TEXT("Color"), TEXT("roundedrect"), [](FShape& S) { S.Color = FLinearColor::Blue; } },
		{ TEXT("Thickness"), TEXT("ring"), [](FShape& S) { S.Thickness = 0.5f; } },
		{ TEXT("CornerRadius"), TEXT("roundedrect"), [](FShape& S) { S.CornerRadius = 0.8f; } },
		{ TEXT("CornerRadii"), TEXT("roundedrect"), [](FShape& S) { S.CornerRadii = FVector4(0.0f, 0.1f, 0.7f, 1.0f); } },
		{ TEXT("BlurPadding"), TEXT("boxshadow"), [](FShape& S) { S.BlurPadding = 8.0f; } },
		{ TEXT("StripeColor"), TEXT("placeholdersurface"), [](FShape& S) { S.StripeColor = FLinearColor::Green; } },
		{ TEXT("StripeWidth"), TEXT("placeholdersurface"), [](FShape& S) { S.StripeWidth = 6; } },
		{ TEXT("StripeGap"), TEXT("placeholderhatch"), [](FShape& S) { S.StripeGap = 13; } },
		{ TEXT("Inset"), TEXT("placeholdersurface"), [](FShape& S) { S.Inset = 0; } }
	};
	for (const FCase& Case : Cases)
	{
		FUMGWidgetNode Root;
		Root.Type = TEXT("Overlay");
		FUMGWidgetNode Base;
		Base.Type = TEXT("Image");
		Base.Id = Base.Name = Case.Field;
		Base.Brush.MaterialRef = Case.Material;
		Base.Brush.Shape = FShape{};
		Base.WidgetStyle.WidthOverride = 48.0f;
		Base.WidgetStyle.HeightOverride = 32.0f;
		Base.Shadow.ShadowBlur = 4.0f;
		Root.Children = { Base, Base, Base };
		Case.Change(Root.Children[1].Brush.Shape.GetValue());
		Root.Children[2].Brush.MaterialParams = TEXT(R"({"Color":"#000000"})");
		FString Error;
		const bool bNormalized = FPreBuildResourceNormalizer::NormalizeProceduralTextures(Root, OutputPath, Case.Field, &Error);
		if (!TestTrue(FString::Printf(TEXT("%s materializes: %s"), Case.Field, *Error), bNormalized)) return false;
		const FString BasePath = Root.Children[0].Brush.Image.TexturePath.Get(FString());
		TestFalse(TEXT("Materialized texture path is present"), BasePath.IsEmpty());
		TestNotEqual(FString::Printf(TEXT("%s participates in resource identity"), Case.Field), Root.Children[1].Brush.Image.TexturePath.Get(FString()), BasePath);
		TestEqual(TEXT("Identical active typed data reuses its resource regardless of ignored JSON"), Root.Children[2].Brush.Image.TexturePath.Get(FString()), BasePath);
		for (const FUMGWidgetNode& Node : Root.Children)
		{
			TestFalse(TEXT("Materialization consumes the shape payload"), Node.Brush.Shape.IsSet());
			TestFalse(TEXT("Materialization clears the resource selector"), Node.Brush.MaterialRef.IsSet());
			TestFalse(TEXT("Materialization clears obsolete external parameters"), Node.Brush.MaterialParams.IsSet());
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCanonicalImageInputBoundaryTest,
	"Design2UMG.TypedPayload.CanonicalImageInputBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCanonicalImageInputBoundaryTest::RunTest(const FString&)
{
	Design2UMGAutomationTestAdapter::FScopedExpectedMessages ExpectedLogMessages(*this);
	ExpectedLogMessages.Expect(TEXT("Invalid Brush.Image.Transform"), ELogVerbosity::Warning, Design2UMGAutomationTestAdapter::EMessageMatch::Contains, 1);
	const FString Input = TEXT(R"({"RootWidget":{"Type":"Overlay","Name":"Root","Children":[
		{"Type":"Image","Name":"Canonical","Brush":{"Image":{"TexturePath":"/Game/Tex","Mode":"Tile","Transform":".5,0,.25,0,.75,.1"}},"WidgetStyle":{"WidthOverride":120}},
		{"Type":"Image","Name":"RejectedCanonical","Brush":{"Image":{"Transform":"invalid"}}},
		{"Type":"Image","Name":"RemovedAliases","Brush":{"TexturePath":"/Game/Legacy"},"WidgetStyle":{"ImageMode":"Crop","ImageTransform":"1,0,0,0,1,0"}}
	]}})");
	FUMGWidgetData Data;
	if (!TestTrue(TEXT("Canonical nested image input parses"), FUMGJsonParser::ParseJson(Input, Data))) return false;
	const auto& Canonical = Data.RootWidget.Children[0].Brush.Image;
	TestTrue(TEXT("Canonical texture path is typed"), Canonical.TexturePath.IsSet() && Canonical.TexturePath->Equals(TEXT("/Game/Tex")));
	TestEqual(TEXT("Canonical mode is typed"), Canonical.Mode.Get(FString()), FString(TEXT("Tile")));
	TestTrue(TEXT("Canonical transform is typed"), Canonical.Transform.IsSet() && Canonical.Transform->A == 0.5f);
	const auto& Rejected = Data.RootWidget.Children[1].Brush.Image;
	TestFalse(TEXT("Invalid canonical transform stays unset"), Rejected.Transform.IsSet());
	const auto& Removed = Data.RootWidget.Children[2].Brush.Image;
	TestFalse(TEXT("Removed root texture alias is not read"), Removed.TexturePath.IsSet());
	TestFalse(TEXT("Removed style image-mode alias is not read"), Removed.Mode.IsSet());
	TestFalse(TEXT("Removed style image-transform alias is not read"), Removed.Transform.IsSet());
	const auto Serialized = FUMGJsonSerializer::SerializeWidgetNode(Data.RootWidget.Children[0]);
	TestFalse(TEXT("Output brush has a single texture owner"), Serialized->GetObjectField(TEXT("Brush"))->HasField(TEXT("TexturePath")));
	const auto Style = Serialized->GetObjectField(TEXT("WidgetStyle"));
	TestFalse(TEXT("Output style has no image mode"), Style->HasField(TEXT("ImageMode")));
	TestFalse(TEXT("Output style has no string transform owner"), Style->HasField(TEXT("ImageTransform")));
	FUMGWidgetData SerializationCycle;
	TestTrue(TEXT("Canonical output parses again without diagnostics"), FUMGJsonParser::ParseJson(FUMGJsonSerializer::Serialize(Data), SerializationCycle));
	TestTrue(TEXT("Transform remains typed across the boundary"), SerializationCycle.RootWidget.Children[0].Brush.Image.Transform.IsSet() && SerializationCycle.RootWidget.Children[0].Brush.Image.Transform->A == Canonical.Transform->A);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTypedRichTextRowsBoundaryTest,
	"Design2UMG.TypedPayload.RichTextRowsBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTypedRichTextRowsBoundaryTest::RunTest(const FString&)
{
	const auto ParseRows = [this](const FString& Rows)
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		TSharedRef<FJsonObject> Node = MakeShared<FJsonObject>();
		TSharedRef<FJsonObject> Text = MakeShared<FJsonObject>();
		Text->SetStringField(UMGPropertyKeys::RichTextStyleRows, Rows);
		Node->SetStringField(TEXT("Type"), TEXT("RichTextBlock"));
		Node->SetStringField(TEXT("Name"), TEXT("Rows"));
		Node->SetObjectField(TEXT("TextData"), Text);
		Root->SetObjectField(TEXT("RootWidget"), Node);
		FString Json;
		FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Json));
		FUMGWidgetData Data;
		TestTrue(TEXT("Style-row input does not reject its containing widget"), FUMGJsonParser::ParseJson(Json, Data));
		return Data.RootWidget;
	};
	const FUMGWidgetNode Node = ParseRows(TEXT(R"({"ignored":42,"row":{"FontSize":0,"LetterSpacing":0,"Color":"","Underline":false,"Strikethrough":true,"Href":"docs"}})"));
	if (!TestTrue(TEXT("Valid row object becomes a typed map"), Node.Text.RichTextStyleRows.IsSet())) return false;
	const auto& Rows = Node.Text.RichTextStyleRows.GetValue();
	TestEqual(TEXT("Non-object rows are ignored at input"), Rows.Num(), 1);
	const auto* Row = Rows.Find(TEXT("row"));
	if (!TestNotNull(TEXT("Named row survives"), Row)) return false;
	TestTrue(TEXT("Explicit zero font size and spacing retain presence"), Row->FontSize.IsSet() && Row->FontSize.GetValue() == 0.0f && Row->LetterSpacing.IsSet() && Row->LetterSpacing.GetValue() == 0.0f);
	TestFalse(TEXT("Empty color retains inheritance semantics"), Row->Color.IsSet());
	URichTextBlock* Widget = NewObject<URichTextBlock>(GetTransientPackage());
	FTextBlockStyle Default = FTextBlockStyle::GetDefault();
	Default.Font.Size = Design2UMGFontAccess::ToNativeFontSize(17.0f);
	Widget->SetDefaultTextStyle(Default);
	FUMGWidgetTreeBuilder::ApplyWidgetProperties(Widget, Node);
	UDataTable* Table = Design2UMGWidgetPropertyAccess::GetTextStyleSet(*Widget);
	if (!TestNotNull(TEXT("Typed rows materialize a style table"), Table)) return false;
	const FRichTextStyleRow* Applied = Table->FindRow<FRichTextStyleRow>(TEXT("row"), TEXT("TypedRows"));
	if (!TestNotNull(TEXT("Typed row is applied"), Applied)) return false;
	TestEqual(TEXT("Invalid zero size does not replace the inherited point size"), static_cast<float>(Applied->TextStyle.Font.Size), 17.0f);
	TestTrue(TEXT("A hyperlink still supplies underline decoration"), Applied->TextStyle.UnderlineBrush.DrawAs != ESlateBrushDrawType::NoDrawType);
	TestTrue(TEXT("Strikethrough remains independent"), Applied->TextStyle.StrikeBrush.DrawAs != ESlateBrushDrawType::NoDrawType);
	const FUMGWidgetNode Invalid = ParseRows(TEXT("not-json"));
	TestFalse(TEXT("Invalid external JSON is not carried through the IR"), Invalid.Text.RichTextStyleRows.IsSet());
	FUMGWidgetTreeBuilder::ApplyWidgetProperties(Widget, Invalid);
	TestEqual(TEXT("Invalid input does not clear an existing style table"), Table->GetRowMap().Num(), 1);
	const FUMGWidgetNode Empty = ParseRows(TEXT("{}"));
	TestTrue(TEXT("Explicit empty rows remain distinct from absence"), Empty.Text.RichTextStyleRows.IsSet() && Empty.Text.RichTextStyleRows->IsEmpty());
	FUMGWidgetTreeBuilder::ApplyWidgetProperties(Widget, Empty);
	TestEqual(TEXT("Explicit empty rows clear the table"), Table->GetRowMap().Num(), 0);
	TestTrue(TEXT("Reused table retains its schema and owner"), Table->RowStruct == FRichTextStyleRow::StaticStruct() && Table->GetOuter() == Widget);
	return true;
}

namespace
{
	void CheckShapeInput(FAutomationTestBase& Test, const FString& JsonShape,
		const TOptional<FUMGVisualBrushData::FShapeData>& Expected)
	{
		FUMGWidgetData Data;
		const FString Json = FString::Printf(TEXT(R"({"RootWidget":{"Type":"Image","Name":"ShapeInput","Brush":{"MaterialRef":"roundedrect","Shape":%s}}})"), *JsonShape);
		if (!Test.TestTrue(TEXT("Invalid optional shape fields do not reject the widget"), FUMGJsonParser::ParseJson(Json, Data))) return;
		Test.TestTrue(TEXT("Only valid shape fields retain presence"), Data.RootWidget.Brush.Shape == Expected);
		FUMGProceduralResourceRequest Request;
		Test.TestTrue(TEXT("Sanitized resource request builds"), Design2UMGWidgetNodeProceduralResourceAccess::TryBuildProceduralResourceRequest(Data.RootWidget, Request));
		Test.TestTrue(TEXT("Resource request contains only sanitized data"), Request.Shape == Expected);
		FUMGWidgetData SerializationCycle;
		Test.TestTrue(TEXT("Sanitized output is readable JSON"), FUMGJsonParser::ParseJson(FUMGJsonSerializer::Serialize(Data), SerializationCycle));
		Test.TestTrue(TEXT("Sanitized shape and presence survive the round trip"), SerializationCycle.RootWidget.Brush.Shape == Expected);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTypedShapeNumericInputTest,
	"Design2UMG.TypedPayload.ShapeNumericInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTypedShapeNumericInputTest::RunTest(const FString&)
{
	Design2UMGAutomationTestAdapter::FScopedExpectedMessages ExpectedLogMessages(*this);
	using FShape = FUMGVisualBrushData::FShapeData;
	const TCHAR* FloatFields[] = { TEXT("Thickness"), TEXT("CornerRadius"), TEXT("BlurPadding") };
	const TCHAR* InvalidFloats[] = { TEXT("1e39"), TEXT("-1e39"), TEXT("true"), TEXT("false"), TEXT("null"), TEXT("\"0.25\""), TEXT("[]"), TEXT("{}") };
	for (const TCHAR* Field : FloatFields)
	{
		ExpectedLogMessages.Expect(FString::Printf(TEXT("FUMGJsonParser: Invalid Brush.Shape.%s;"), Field),
			ELogVerbosity::Warning, Design2UMGAutomationTestAdapter::EMessageMatch::Contains, UE_ARRAY_COUNT(InvalidFloats));
		for (const TCHAR* Value : InvalidFloats)
		{
			FShape Expected;
			Expected.Inset = 2;
			CheckShapeInput(*this, FString::Printf(TEXT("{\"%s\":%s,\"Inset\":2}"), Field, Value), Expected);
		}
	}
	const TCHAR* IntFields[] = { TEXT("StripeWidth"), TEXT("StripeGap"), TEXT("Inset") };
	const TCHAR* InvalidInts[] = { TEXT("2147483648"), TEXT("-2147483649"), TEXT("1e39"), TEXT("true"), TEXT("false"), TEXT("null"), TEXT("\"2\""), TEXT("[]"), TEXT("{}") };
	for (const TCHAR* Field : IntFields)
	{
		ExpectedLogMessages.Expect(FString::Printf(TEXT("FUMGJsonParser: Invalid Brush.Shape.%s;"), Field),
			ELogVerbosity::Warning, Design2UMGAutomationTestAdapter::EMessageMatch::Contains, UE_ARRAY_COUNT(InvalidInts));
		for (const TCHAR* Value : InvalidInts)
		{
			FShape Expected;
			Expected.CornerRadius = 0.25f;
			CheckShapeInput(*this, FString::Printf(TEXT("{\"%s\":%s,\"CornerRadius\":0.25}"), Field, Value), Expected);
		}
	}
	FShape Valid;
	Valid.Thickness = -0.125f;
	Valid.CornerRadius = 0.0f;
	Valid.BlurPadding = 1.25e-3f;
	Valid.StripeWidth = 3;
	Valid.StripeGap = -3;
	Valid.Inset = 0;
	CheckShapeInput(*this, TEXT(R"({"Thickness":-0.125,"CornerRadius":0,"BlurPadding":1.25e-3,"StripeWidth":2.5,"StripeGap":-2.5,"Inset":0})"), Valid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTypedShapeAggregateInputTest,
	"Design2UMG.TypedPayload.ShapeAggregateInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTypedShapeAggregateInputTest::RunTest(const FString&)
{
	Design2UMGAutomationTestAdapter::FScopedExpectedMessages ExpectedLogMessages(*this);
	using FShape = FUMGVisualBrushData::FShapeData;
	const TCHAR* BadTypes[] = { TEXT("null"), TEXT("[]"), TEXT("true"), TEXT("false"), TEXT("42"), TEXT("\"text\"") };
	ExpectedLogMessages.Expect(TEXT("FUMGJsonParser: Invalid Brush.Shape;"), ELogVerbosity::Warning,
		Design2UMGAutomationTestAdapter::EMessageMatch::Contains, UE_ARRAY_COUNT(BadTypes));
	for (const TCHAR* Value : BadTypes) CheckShapeInput(*this, Value, {});
	CheckShapeInput(*this, TEXT("{}"), FShape{});

	const TCHAR* InvalidRadii[] = { TEXT("null"), TEXT("{}"), TEXT("false"), TEXT("1"), TEXT("\"0,0,0,0\""), TEXT("[]"), TEXT("[0,1,2]"), TEXT("[0,1,2,3,4]") };
	const TCHAR* InvalidComponents[] = { TEXT("true"), TEXT("false"), TEXT("null"), TEXT("\"0.5\""), TEXT("[]"), TEXT("{}"), TEXT("1e39"), TEXT("-1e39") };
	ExpectedLogMessages.Expect(TEXT("FUMGJsonParser: Invalid Brush.Shape.CornerRadii;"), ELogVerbosity::Warning,
		Design2UMGAutomationTestAdapter::EMessageMatch::Contains, UE_ARRAY_COUNT(InvalidRadii) + 4 * UE_ARRAY_COUNT(InvalidComponents));
	FShape Expected;
	Expected.CornerRadius = 0.5f;
	for (const TCHAR* Value : InvalidRadii)
		CheckShapeInput(*this, FString::Printf(TEXT("{\"CornerRadii\":%s,\"CornerRadius\":0.5}"), Value), Expected);
	for (int32 Index = 0; Index < 4; ++Index)
	{
		for (const TCHAR* Value : InvalidComponents)
		{
			TArray<FString> Components = { TEXT("0"), TEXT("0.25"), TEXT("0.5"), TEXT("1") };
			Components[Index] = Value;
			CheckShapeInput(*this, FString::Printf(TEXT("{\"CornerRadii\":[%s],\"CornerRadius\":0.5}"), *FString::Join(Components, TEXT(","))), Expected);
		}
	}
	for (const TCHAR* Field : { TEXT("Color"), TEXT("StripeColor") })
	{
		ExpectedLogMessages.Expect(FString::Printf(TEXT("FUMGJsonParser: Invalid Brush.Shape.%s;"), Field),
			ELogVerbosity::Warning, Design2UMGAutomationTestAdapter::EMessageMatch::Contains, UE_ARRAY_COUNT(BadTypes) - 1);
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(BadTypes) - 1; ++Index)
			CheckShapeInput(*this, FString::Printf(TEXT("{\"%s\":%s,\"CornerRadius\":0.5}"), Field, BadTypes[Index]), Expected);
	}
	Expected.CornerRadii = FVector4(0, 0.25, 0.5, 1);
	Expected.Color = FUMGValueParser::ParseColor(TEXT("#12345678"));
	Expected.StripeColor = FUMGValueParser::ParseColor(TEXT("#ABCDEF"));
	CheckShapeInput(*this, TEXT(R"({"CornerRadius":0.5,"CornerRadii":[0,0.25,0.5,1],"Color":"#12345678","StripeColor":"#ABCDEF"})"), Expected);
	return true;
}

#endif
