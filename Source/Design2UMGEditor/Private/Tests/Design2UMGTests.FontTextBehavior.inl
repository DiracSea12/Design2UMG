IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIconFontNoMappingFallbackTest,
	"Design2UMG.IconFont.NoMappingFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FIconFontNoMappingFallbackTest::RunTest(const FString& Parameters)
{
	UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	TestTrue(TEXT("Settings available"), Settings != nullptr);
	if (!Settings)
	{
		return false;
	}
	const TMap<FString, TSoftObjectPtr<UFont>> SavedMap = Settings->IconFontAssetMap;
	const FString SavedDefaultPath = Settings->DefaultIconFontAssetPath;
	const FString SavedPlaceholder = Settings->MissingIconFontPlaceholder;
	Settings->IconFontAssetMap.Empty();
	Settings->DefaultIconFontAssetPath.Empty();
	Settings->MissingIconFontPlaceholder = TEXT("[missing-icon]");
	FString Json;
	TestTrue(TEXT("Load fixture IconFontNoMappingFallback"), LoadFixtureJson(TEXT("IconFontNoMappingFallback.json"), Json));
	if (Json.IsEmpty())
	{
		Settings->IconFontAssetMap = SavedMap;
		Settings->DefaultIconFontAssetPath = SavedDefaultPath;
		Settings->MissingIconFontPlaceholder = SavedPlaceholder;
		return false;
	}
	FDesignDocument Doc;
	TestTrue(TEXT("Icon fallback parse succeeds"), FPenParser::ParseDocument(Json, Doc));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::FromSettings(Settings));
	const FUMGWidgetNode* IconNode = FindWidgetByProperty(Data.RootWidget, TEXT("IconName"));
	TestTrue(TEXT("Icon widget exists"), IconNode != nullptr);
	if (IconNode)
	{
		TestFalse(TEXT("Missing mapping should not inject invalid FontFamily string as Typeface"),
			HasWidgetProperty(*IconNode, TEXT("FontFamily")));
		TestEqual(TEXT("Missing mapping uses configured placeholder text"),
			GetWidgetProperty(*IconNode, TEXT("Text")),
			TEXT("[missing-icon]"));
	}
	Settings->IconFontAssetMap = SavedMap;
	Settings->DefaultIconFontAssetPath = SavedDefaultPath;
	Settings->MissingIconFontPlaceholder = SavedPlaceholder;
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIconFontNoMappingKeepIconNameTest,
	"Design2UMG.IconFont.NoMappingKeepsIconName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FIconFontNoMappingKeepIconNameTest::RunTest(const FString& Parameters)
{
	UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	const TMap<FString, TSoftObjectPtr<UFont>> SavedMap = Settings->IconFontAssetMap;
	const FString SavedDefaultPath = Settings->DefaultIconFontAssetPath;
	const FString SavedPlaceholder = Settings->MissingIconFontPlaceholder;
	Settings->IconFontAssetMap.Empty();
	Settings->DefaultIconFontAssetPath.Empty();
	Settings->MissingIconFontPlaceholder.Empty();
	FString Json;
	TestTrue(TEXT("Load fixture IconFontNoMappingFallback"), LoadFixtureJson(TEXT("IconFontNoMappingFallback.json"), Json));
	if (Json.IsEmpty())
	{
		Settings->IconFontAssetMap = SavedMap;
		Settings->DefaultIconFontAssetPath = SavedDefaultPath;
		Settings->MissingIconFontPlaceholder = SavedPlaceholder;
		return false;
	}
	FDesignDocument Doc;
	TestTrue(TEXT("Icon fallback parse succeeds"), FPenParser::ParseDocument(Json, Doc));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::FromSettings(Settings));
	const FString ExpectedIconName = TEXT("dashboard");
	const FUMGWidgetNode* IconNode = FindWidgetByProperty(Data.RootWidget, TEXT("IconName"), &ExpectedIconName);
	TestTrue(TEXT("Icon widget exists"), IconNode != nullptr);
	if (IconNode)
	{
		TestFalse(TEXT("Missing mapping should not inject invalid FontFamily string as Typeface"),
			HasWidgetProperty(*IconNode, TEXT("FontFamily")));
		TestEqual(TEXT("Empty placeholder should keep original IconName text"),
			GetWidgetProperty(*IconNode, TEXT("Text")),
			ExpectedIconName);
	}
	Settings->IconFontAssetMap = SavedMap;
	Settings->DefaultIconFontAssetPath = SavedDefaultPath;
	Settings->MissingIconFontPlaceholder = SavedPlaceholder;
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIconFontLigatureFallbackUsesRegularWeightTest,
	"Design2UMG.IconFont.LigatureFallbackUsesRegularWeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FIconFontLigatureFallbackUsesRegularWeightTest::RunTest(const FString& Parameters)
{
	UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	const TMap<FString, TSoftObjectPtr<UFont>> SavedMap = Settings->IconFontAssetMap;
	const FString SavedDefaultPath = Settings->DefaultIconFontAssetPath;
	const FString SavedPlaceholder = Settings->MissingIconFontPlaceholder;
	Settings->IconFontAssetMap.Empty();
	Settings->DefaultIconFontAssetPath = TEXT("/Engine/EngineFonts/Roboto.Roboto");
	Settings->MissingIconFontPlaceholder.Empty();

	const FString Json = TEXT(R"({
		"children": [{
			"id": "root",
			"type": "frame",
			"children": [{
				"id": "icon1",
				"type": "icon_font",
				"iconFontFamily": "MissingIconFamily_UnitTest",
				"iconFontName": "dashboard",
				"fontSize": 20,
				"weight": 100
			}]
		}]
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Icon weight fixture parse succeeds"), FPenParser::ParseDocument(Json, Doc));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::FromSettings(Settings));
	const FString ExpectedIconName = TEXT("dashboard");
	const FUMGWidgetNode* IconNode = FindWidgetByProperty(Data.RootWidget, TEXT("IconName"), &ExpectedIconName);
	TestTrue(TEXT("Icon widget exists"), IconNode != nullptr);
	if (IconNode)
	{
		TestEqual(TEXT("Icon widget uses configured default font asset"),
			GetWidgetProperty(*IconNode, TEXT("FontFamily")),
			TEXT("/Engine/EngineFonts/Roboto.Roboto"));
		TestEqual(TEXT("Ligature fallback icon keeps regular typeface without explicit fontWeight"),
			GetWidgetProperty(*IconNode, TEXT("TypefaceFontName")),
			TEXT("Regular"));
	}

	Settings->IconFontAssetMap = SavedMap;
	Settings->DefaultIconFontAssetPath = SavedDefaultPath;
	Settings->MissingIconFontPlaceholder = SavedPlaceholder;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJustifyAutoTextWrapParityTest,
	"Design2UMG.Text.JustifyAutoWrapParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFontResolverDefaultFontAliasLookupTest,
	"Design2UMG.Text.FontResolverDefaultFontAliasLookup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFontResolverDefaultFontAliasLookupTest::RunTest(const FString& Parameters)
{
	const UObject* DefaultFontObject = FTextBlockStyle::GetDefault().Font.FontObject;
	if (!DefaultFontObject)
	{
		AddWarning(TEXT("Default Slate text font is unavailable, skipping font alias lookup test."));
		return true;
	}

	const FString DefaultFontPath = DefaultFontObject->GetPathName();
	const FString DefaultFontName = DefaultFontObject->GetName();
	const FString StyleQualifiedAlias = DefaultFontName + TEXT(" Regular");

	TestEqual(TEXT("Default font family resolves through FontResolver"),
		FFontResolver::FindFontAssetPath(DefaultFontName),
		DefaultFontPath);
	TestEqual(TEXT("Style-qualified alias resolves to the same font asset"),
		FFontResolver::FindFontAssetPath(StyleQualifiedAlias),
		DefaultFontPath);
	return true;
}

bool FJustifyAutoTextWrapParityTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"children": [
			{
				"id": "justify_auto",
				"type": "text",
				"name": "Justify Auto",
				"content": "This sentence should wrap against the source width.",
				"width": 120,
				"textAlign": "justify",
				"textGrowth": "auto",
				"fontFamily": "Roboto",
				"fontSize": 16
			}
		]
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Justify auto text fixture parses"), FPenParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() != 1)
	{
		return false;
	}

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* TextWidget = FindWidgetById(Data.RootWidget, TEXT("justify_auto"));
	TestTrue(TEXT("Justify auto text widget exists"), TextWidget != nullptr);
	if (!TextWidget)
	{
		return false;
	}

	TestEqual(TEXT("Justify auto text converts to TextBlock"), TextWidget->Type, TEXT("TextBlock"));
	TestEqual(TEXT("Justify auto text enables runtime wrapping"), GetWidgetProperty(*TextWidget, TEXT("AutoWrapText")), TEXT("true"));
	TestEqual(TEXT("Justify auto text keeps source wrap width"), GetWidgetProperty(*TextWidget, TEXT("WrapTextAt")), TEXT("120.0"));
	TestTrue(TEXT("Justify auto text should not clip like fixed-height text"), GetWidgetProperty(*TextWidget, TEXT("Clipping")).IsEmpty());
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKnownWidthFillTextWrapParityTest,
	"Design2UMG.Text.KnownWidthFillWrapParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FKnownWidthFillTextWrapParityTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"JSON({
		"children": [
			{
				"id": "fill_known_wrap",
				"type": "text",
				"name": "Fill Known Wrap",
				"content": "This fill-width text should keep the known fallback width at runtime.",
				"width": "fill_container(300)",
				"textGrowth": "auto",
				"fontFamily": "Roboto",
				"fontSize": 16
			}
		]
	})JSON");

	FDesignDocument Doc;
	TestTrue(TEXT("Known-width fill text fixture parses"), FPenParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() != 1)
	{
		return false;
	}

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* TextWidget = FindWidgetById(Data.RootWidget, TEXT("fill_known_wrap"));
	TestTrue(TEXT("Known-width fill text widget exists"), TextWidget != nullptr);
	if (!TextWidget)
	{
		return false;
	}

	TestEqual(TEXT("Known-width fill text enables runtime wrapping"), GetWidgetProperty(*TextWidget, TEXT("AutoWrapText")), TEXT("true"));
	TestEqual(TEXT("Known-width fill text keeps fallback wrap width"), GetWidgetProperty(*TextWidget, TEXT("WrapTextAt")), TEXT("300.0"));
	TestTrue(TEXT("Known-width fill text should not clip"), GetWidgetProperty(*TextWidget, TEXT("Clipping")).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMultiParagraphFixedWidthTextKeepsDeclaredFontSizeTest,
	"Design2UMG.Text.MultiParagraphFixedWidthKeepsDeclaredFontSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMultiParagraphFixedWidthTextKeepsDeclaredFontSizeTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"JSON({
		"document": {
			"id": "0:0",
			"name": "Doc",
			"type": "DOCUMENT",
			"children": [{
				"id": "0:1",
				"name": "Page 1",
				"type": "CANVAS",
				"children": [{
					"id": "0:2",
					"name": "Root",
					"type": "FRAME",
					"visible": true,
					"absoluteBoundingBox": {"x":0,"y":0,"width":1400,"height":200},
					"children": [{
						"id": "body",
						"name": "Supporting text",
						"type": "TEXT",
						"visible": true,
						"absoluteBoundingBox": {"x":0,"y":0,"width":1325,"height":80},
						"style": {
							"fontFamily": "Roboto",
							"fontWeight": 400,
							"fontSize": 14,
							"textAlignHorizontal": "LEFT",
							"textAlignVertical": "TOP",
							"letterSpacing": 0.25,
							"lineHeightUnit": "PIXELS",
							"lineHeightPx": 20
						},
						"layoutSizingHorizontal": "FILL",
						"layoutSizingVertical": "HUG",
						"layoutPositioning": "AUTO",
						"textAutoResize": "HEIGHT",
						"characters": "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat.\n\nDuis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum."
					}]
				}]
			}]
		}
	})JSON");

	FDesignDocument Doc;
	TestTrue(TEXT("Figma multi-paragraph fixture parses"), FFigmaParser::ParseDocument(Json, Doc));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* TextWidget = FindWidgetById(Data.RootWidget, TEXT("body"));
	TestNotNull(TEXT("Converted body text exists"), TextWidget);
	if (!TextWidget)
	{
		return false;
	}

	TestEqual(TEXT("Body text keeps declared runtime content"), TextWidget->Text.Text.GetValue(), FString(TEXT("Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat.\n\nDuis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum.")));
	TestEqual(TEXT("Body text enables runtime wrap"), GetWidgetProperty(*TextWidget, TEXT("AutoWrapText")), TEXT("true"));
	TestEqual(TEXT("Body text keeps authored wrap width"), GetWidgetProperty(*TextWidget, TEXT("WrapTextAt")), TEXT("1325.0"));
	TestTrue(TEXT("Body text should not clip after conversion"), GetWidgetProperty(*TextWidget, TEXT("Clipping")).IsEmpty());
	return true;
}
