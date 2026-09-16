// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "FigmaComponentDependencies.h"
#include "FigmaParser.h"
#include "Design2UMGConverter.h"
#include "DesignJsonUtils.h"
#include "DesignSchemaSourceExtensions.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "UMGSchemaSourceExtensions.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaComponentDependenciesTest,
	"Design2UMG.Unit.FigmaParser.ComponentDependencies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaComponentDependenciesTest::RunTest(const FString&)
{
	const FString Bundle = TEXT(R"JSON({"schemaVersion":1,"errors":[],"instances":{},"definitions":[
		{"id":"p","key":"parent","document":{"id":"p","type":"COMPONENT","name":"Same","children":[
			{"id":"nested","type":"INSTANCE","componentId":"c","mainComponentKey":"child"}]}},
		{"id":"c","key":"child","componentSetId":"set","componentSetKey":"set-key",
		 "document":{"id":"c","type":"COMPONENT","name":"Same","children":[]}},
		{"id":"unused","key":"unused","document":{"id":"unused","type":"COMPONENT","children":[]}}
	]})JSON");
	FigmaComponentDependencies::FCatalog Catalog;
	FString Error;
	if (!TestTrue(TEXT("Valid component catalog"), FigmaComponentDependencies::Parse(Bundle, Catalog, Error))) return false;
	TArray<FString> Order;
	TestTrue(TEXT("Dependency order"), FigmaComponentDependencies::BuildOrder(Catalog,
		{ TEXT("parent"), TEXT("parent"), TEXT("child") }, {}, Order, Error));
	TestEqual(TEXT("Same component appears once; unused definition excluded"), Order.Num(), 2);
	if (Order.Num() == 2)
	{
		TestEqual(TEXT("Child before parent"), Order[0], FString(TEXT("child")));
		TestEqual(TEXT("Parent after child"), Order[1], FString(TEXT("parent")));
	}
	TestEqual(TEXT("Variant set retained"), Catalog.Definitions.FindChecked(TEXT("child")).ComponentSetKey, FString(TEXT("set-key")));
	TestTrue(TEXT("Existing WBP terminates dependency traversal"), FigmaComponentDependencies::BuildOrder(
		Catalog, { TEXT("parent") }, { TEXT("parent") }, Order, Error));
	TestEqual(TEXT("No automatic children below explicit mapping"), Order.Num(), 0);
	TestFalse(TEXT("Missing dependency is failure"), FigmaComponentDependencies::BuildOrder(
		Catalog, { TEXT("absent") }, {}, Order, Error));
	TestTrue(TEXT("Missing identity reported"), Error.Contains(TEXT("absent")));
	Catalog.Definitions.FindChecked(TEXT("parent")).References[0].Children.Add({ TEXT("swapped-missing"), {} });
	TestTrue(TEXT("Explicit child WBP also excludes resolved grandchildren"), FigmaComponentDependencies::BuildOrder(
		Catalog, { TEXT("parent") }, { TEXT("child") }, Order, Error));
	TestEqual(TEXT("Only automatic parent remains"), Order.Num(), 1);
	TestFalse(TEXT("Unmapped resolved swap is a dependency"), FigmaComponentDependencies::BuildOrder(
		Catalog, { TEXT("parent") }, {}, Order, Error));
	Catalog.Definitions.FindChecked(TEXT("parent")).References[0].Children.Reset();
	Catalog.Definitions.FindChecked(TEXT("child")).References.Add({ TEXT("parent"), {} });
	TestFalse(TEXT("Cycle is failure"), FigmaComponentDependencies::BuildOrder(Catalog, { TEXT("parent") }, {}, Order, Error));
	TestEqual(TEXT("Failed plan is not a partial success"), Order.Num(), 0);
	TestFalse(TEXT("Unsupported schema"), FigmaComponentDependencies::Parse(TEXT("{\"schemaVersion\":2}"), Catalog, Error));
	TestFalse(TEXT("Wrong field type"), FigmaComponentDependencies::Parse(
		TEXT("{\"schemaVersion\":1,\"definitions\":{},\"instances\":{},\"errors\":[]}"), Catalog, Error));
	const FString Duplicate = TEXT(R"JSON({"schemaVersion":1,"instances":{},"errors":[],"definitions":[
		{"id":"a","key":"same","document":{"id":"a","type":"COMPONENT"}},
		{"id":"b","key":"same","document":{"id":"b","type":"COMPONENT"}}
	]})JSON");
	TestFalse(TEXT("Duplicate identity rejected"), FigmaComponentDependencies::Parse(Duplicate, Catalog, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaComponentDependencyRootFilterTest,
	"Design2UMG.Unit.FigmaParser.ComponentDependencyRootFilter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaComponentDependencyRootFilterTest::RunTest(const FString&)
{
	const FString Prepared = TEXT(R"JSON({
		"document":{"id":"document","type":"DOCUMENT","children":[
			{"id":"page-a","type":"CANVAS","children":[{"id":"selected","type":"FRAME","children":[
				{"id":"selected-instance","type":"INSTANCE","mainComponentKey":"parent"}]}]},
			{"id":"page-b","type":"CANVAS","children":[{"id":"unrelated","type":"FRAME","children":[
				{"id":"unrelated-instance","type":"INSTANCE","mainComponentKey":"unused"}]}]}
		]},
		"design2umgComponentDependencies":{"schemaVersion":1,
			"instances":{"selected-instance":{"key":"parent"},"unrelated-instance":{"key":"unused"}},
			"errors":[{"kind":"component_source_unavailable","nodeId":"unrelated-instance","message":"not published"}],
			"definitions":[
				{"id":"parent","key":"parent","document":{"id":"parent","type":"COMPONENT","children":[
					{"id":"nested","type":"INSTANCE","mainComponentKey":"child"}]}},
				{"id":"child","key":"child","document":{"id":"child","type":"COMPONENT","children":[]}},
				{"id":"unused","key":"unused","document":{"id":"unused","type":"COMPONENT","children":[]}}
			]}
		})JSON");
	TSharedPtr<FJsonObject> Root;
	if (!TestTrue(TEXT("Fixture parses"), DesignJsonUtils::TryParseObject(Prepared, Root))) return false;
	FString Error;
	if (!TestTrue(TEXT("Selected root filters dependency evidence"),
		FigmaComponentDependencies::FilterRestFileToSelectedRoot(Root, TEXT("selected"), Error))) return false;

	TestEqual(TEXT("Selected frame remains the document root"),
		DesignJsonUtils::GetString(Root->GetObjectField(TEXT("document")), TEXT("id")), FString(TEXT("selected")));
	TestEqual(TEXT("Selected frame keeps its node type"),
		DesignJsonUtils::GetString(Root->GetObjectField(TEXT("document")), TEXT("type")), FString(TEXT("FRAME")));
	const TSharedPtr<FJsonObject> Bundle = Root->GetObjectField(TEXT("design2umgComponentDependencies"));
	FString BundleJson;
	if (!TestTrue(TEXT("Filtered bundle serializes"),
		FJsonSerializer::Serialize(Bundle.ToSharedRef(), TJsonWriterFactory<>::Create(&BundleJson)))) return false;
	FigmaComponentDependencies::FCatalog Catalog;
	if (!TestTrue(TEXT("Filtered bundle parses"), FigmaComponentDependencies::Parse(BundleJson, Catalog, Error))) return false;
	TestEqual(TEXT("Only selected subtree instance remains"), Catalog.InstanceKeys.Num(), 1);
	TestTrue(TEXT("Selected instance remains"), Catalog.InstanceKeys.Contains(TEXT("selected-instance")));
	TestFalse(TEXT("Unrelated instance removed"), Catalog.InstanceKeys.Contains(TEXT("unrelated-instance")));
	TestEqual(TEXT("Unrelated missing diagnostic removed"), Catalog.Errors.Num(), 0);

	TArray<FString> Order;
	TestTrue(TEXT("Nested selected component closure remains complete"),
		FigmaComponentDependencies::BuildOrder(Catalog, {TEXT("parent")}, {}, Order, Error));
	TestEqual(TEXT("Nested closure contains child and parent"), Order.Num(), 2);
	if (Order.Num() == 2)
	{
		TestEqual(TEXT("Nested child precedes parent"), Order[0], FString(TEXT("child")));
		TestEqual(TEXT("Selected parent follows child"), Order[1], FString(TEXT("parent")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaComponentDependenciesRetentionTest,
	"Design2UMG.Unit.FigmaParser.ComponentDependenciesRetention",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFigmaComponentDependenciesRetentionTest::RunTest(const FString&)
{
	const FString Json = TEXT(R"JSON({"fileKey":"file-key","document":{
		"id":"root","name":"Root","type":"FRAME","absoluteBoundingBox":{"x":0,"y":0,"width":320,"height":200}},
		"design2umgComponentDependencies":{"schemaVersion":1,"definitions":[],"instances":{},"errors":[]}})JSON");
	FDesignDocument Document;
	if (!TestTrue(TEXT("Figma parses"), FFigmaParser::ParseDocument(Json, Document))) return false;
	TestFalse(TEXT("Complete definitions retained outside selected layout"), Document.SourceDocument().ComponentDependenciesJson.IsEmpty());
	const FUMGWidgetData Converted = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestEqual(TEXT("Conversion retains component definitions"), Converted.SourceDocument().ComponentDependenciesJson,
		Document.SourceDocument().ComponentDependenciesJson);
	TestEqual(TEXT("Definitions do not become page children"), Document.Children.Num(), 1);
	return true;
}
#endif
