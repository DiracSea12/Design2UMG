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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFontResolverProjectFontOnlyLookupTest,
	"Design2UMG.Text.FontResolverProjectFontOnlyLookup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFontResolverProjectFontOnlyLookupTest::RunTest(const FString& Parameters)
{
	FFontResolver::ClearCache();

	UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	TestTrue(TEXT("Settings available"), Settings != nullptr);
	if (!Settings)
	{
		return false;
	}
	const TMap<FString, TSoftObjectPtr<UFont>> SavedMappings = Settings->ExplicitFontMappings;
	const TSoftObjectPtr<UFont> SavedDefaultFont = Settings->DefaultFontAsset;
	Settings->ExplicitFontMappings.Empty();
	Settings->DefaultFontAsset.Reset();

	const FString ProjectInterPath = TEXT("/Game/Design2UMG/Fonts/Inter.Inter");
	if (!LoadObject<UFont>(nullptr, *ProjectInterPath))
	{
		AddWarning(TEXT("Project Inter font asset is unavailable in this test project; skipping project exact-match check."));
		Settings->ExplicitFontMappings = SavedMappings;
		Settings->DefaultFontAsset = SavedDefaultFont;
		return true;
	}

	TestEqual(TEXT("Exact normalized /Game UFont asset name resolves"),
		FFontResolver::FindFontAssetPath(TEXT("Inter")),
		ProjectInterPath);
	TestTrue(TEXT("Style-qualified fuzzy alias must not resolve"),
		FFontResolver::FindFontAssetPath(TEXT("Inter Regular")).IsEmpty());

	Settings->ExplicitFontMappings.Add(TEXT("InterPS-Regular"), TSoftObjectPtr<UFont>(FSoftObjectPath(ProjectInterPath)));
	TestEqual(TEXT("PostScript exact matching only works through explicit mapping"),
		FFontResolver::FindFontAssetPath(FString(), TEXT("InterPS-Regular")),
		ProjectInterPath);
	TestTrue(TEXT("PostScript name must not trigger automatic project asset matching"),
		FFontResolver::FindFontAssetPath(FString(), TEXT("Inter")).IsEmpty());

	const FString NonProjectOnlyFamily = TEXT("D2UNonProjectOnlyFont");
	FFontResolver::RegisterFontAssetPath(NonProjectOnlyFamily, TEXT("/Engine/EngineFonts/Roboto.Roboto"));
	TestTrue(TEXT("Explicit engine font registration must not become a project match"),
		FFontResolver::FindFontAssetPath(NonProjectOnlyFamily).IsEmpty());
	TestEqual(TEXT("Missing project font falls back to matching engine UFont asset"),
		FFontResolver::FindFontAssetPath(TEXT("Roboto")),
		TEXT("/Engine/EngineFonts/Roboto.Roboto"));
	const FString ExpectedEngineTypeface = FFontResolver::FindBestTypefaceForAssetPath(TEXT("/Engine/EngineFonts/Roboto.Roboto"), TEXT("500"));
	TestFalse(TEXT("Engine fallback test asset exposes a selectable typeface"),
		ExpectedEngineTypeface.IsEmpty());
	TestEqual(TEXT("Engine fallback uses requested typeface selection for the resolved asset"),
		FFontResolver::FindBestTypeface(TEXT("Roboto"), TEXT("500")),
		ExpectedEngineTypeface);

	const FString AutomationOnlyFamily = TEXT("D2UAutomationOnlyFont");
	FFontResolver::RegisterFontAssetPath(
		AutomationOnlyFamily,
		TEXT("/Game/Design2UMG/Automation/figma-figma-export-source/ImportedFonts/Oxygen.Oxygen"));
	TestTrue(TEXT("Automation imported font registration must not become a project match"),
		FFontResolver::FindFontAssetPath(AutomationOnlyFamily).IsEmpty());

	Settings->ExplicitFontMappings = SavedMappings;
	Settings->DefaultFontAsset = SavedDefaultFont;
	FFontResolver::ClearCache();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFontResolverRejectsEmptyCompositeProjectFontTest,
	"Design2UMG.Text.FontResolverRejectsEmptyCompositeProjectFont",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFontResolverRejectsEmptyCompositeProjectFontTest::RunTest(const FString& Parameters)
{
	FFontResolver::ClearCache();

	UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	TestTrue(TEXT("Settings available"), Settings != nullptr);
	if (!Settings)
	{
		return false;
	}

	const TMap<FString, TSoftObjectPtr<UFont>> SavedMappings = Settings->ExplicitFontMappings;
	const TSoftObjectPtr<UFont> SavedDefaultFont = Settings->DefaultFontAsset;
	Settings->ExplicitFontMappings.Empty();
	Settings->DefaultFontAsset.Reset();

	UPackage* EmptyFontPackage = CreatePackage(TEXT("/Game/Design2UMG/Fonts/D2U_EmptyCompositeFontForResolverTest"));
	UFont* EmptyCompositeFont = NewObject<UFont>(
		EmptyFontPackage,
		TEXT("D2U_EmptyCompositeFontForResolverTest"),
		RF_Public | RF_Standalone);
	TestTrue(TEXT("Project-path empty composite font created"), EmptyCompositeFont != nullptr);
	if (EmptyCompositeFont)
	{
		Settings->ExplicitFontMappings.Add(
			TEXT("D2UEmptyCompositeFont"),
			TSoftObjectPtr<UFont>(FSoftObjectPath(EmptyCompositeFont)));
		TestTrue(TEXT("Loadable but empty composite UFont must not resolve"),
			FFontResolver::FindFontAssetPath(TEXT("D2UEmptyCompositeFont")).IsEmpty());

		const FString ProjectInterPath = TEXT("/Game/Design2UMG/Fonts/Inter.Inter");
		if (LoadObject<UFont>(nullptr, *ProjectInterPath))
		{
			FFontResolver::ClearCache();
			Settings->ExplicitFontMappings.Add(
				TEXT("Inter"),
				TSoftObjectPtr<UFont>(FSoftObjectPath(EmptyCompositeFont)));
			TestTrue(TEXT("Invalid explicit mapping must not fall through to automatic project exact match"),
				FFontResolver::FindFontAssetPath(TEXT("Inter")).IsEmpty());
		}
	}

	Settings->ExplicitFontMappings = SavedMappings;
	Settings->DefaultFontAsset = SavedDefaultFont;
	FFontResolver::ClearCache();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExplicitFontMappingMissingTypefaceDoesNotAutoSubstituteTest,
	"Design2UMG.Text.ExplicitFontMappingMissingTypefaceDoesNotAutoSubstitute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FExplicitFontMappingMissingTypefaceDoesNotAutoSubstituteTest::RunTest(const FString& Parameters)
{
	FFontResolver::ClearCache();

	UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	TestTrue(TEXT("Settings available"), Settings != nullptr);
	if (!Settings)
	{
		return false;
	}

	const TMap<FString, TSoftObjectPtr<UFont>> SavedMappings = Settings->ExplicitFontMappings;
	const TSoftObjectPtr<UFont> SavedDefaultFont = Settings->DefaultFontAsset;
	Settings->ExplicitFontMappings.Empty();
	Settings->DefaultFontAsset.Reset();

	UPackage* RegularOnlyPackage = CreatePackage(TEXT("/Game/Design2UMG/Fonts/D2U_RegularOnlyExplicitFontForResolverTest"));
	UFont* RegularOnlyFont = NewObject<UFont>(
		RegularOnlyPackage,
		TEXT("D2U_RegularOnlyExplicitFontForResolverTest"),
		RF_Public | RF_Standalone);
	TestTrue(TEXT("Regular-only explicit font fixture created"), RegularOnlyFont != nullptr);
	if (!RegularOnlyFont)
	{
		Settings->ExplicitFontMappings = SavedMappings;
		Settings->DefaultFontAsset = SavedDefaultFont;
		return false;
	}

	RegularOnlyFont->FontCacheType = EFontCacheType::Runtime;
	RegularOnlyFont->GetMutableInternalCompositeFont().DefaultTypeface.Fonts.Add(FTypefaceEntry(
		FName(TEXT("Regular")),
		FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts/Roboto-Regular.ttf")),
		EFontHinting::Default,
		EFontLoadingPolicy::LazyLoad));
	const FString RegularOnlyPath = RegularOnlyFont->GetPathName();

	Settings->ExplicitFontMappings.Add(
		TEXT("D2UExplicitRegularOnlyPS"),
		TSoftObjectPtr<UFont>(FSoftObjectPath(RegularOnlyFont)));
	FFontResolver::ClearCache();

	const FString ExactBoldTypeface = FFontResolver::FindExactTypefaceForAssetPath(RegularOnlyPath, TEXT("700"));
	const FString NearestTypeface = FFontResolver::FindBestTypefaceForAssetPath(RegularOnlyPath, TEXT("700"));
	TestFalse(TEXT("Nearest-weight lookup has a substitutable typeface"), NearestTypeface.IsEmpty());
	TestEqual(TEXT("Explicit mapping still resolves the project font asset"),
		FFontResolver::FindFontAssetPath(TEXT("D2UExplicitRegularOnly"), TEXT("D2UExplicitRegularOnlyPS")),
		RegularOnlyPath);
	TestEqual(TEXT("Regular-only asset has no exact bold typeface"),
		ExactBoldTypeface,
		FString());

	const FString Json = TEXT(R"({
		"children": [{
			"id": "explicit_missing_typeface",
			"type": "text",
			"content": "No substitute",
			"fontFamily": "D2UExplicitRegularOnly",
			"fontPostScriptName": "D2UExplicitRegularOnlyPS",
			"fontWeight": "700",
			"fontSize": 16
		}]
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Explicit missing typeface fixture parses"), FPenParser::ParseDocument(Json, Doc));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* TextWidget = FindWidgetById(Data.RootWidget, TEXT("explicit_missing_typeface"));
	TestNotNull(TEXT("Converted explicit-mapping text exists"), TextWidget);
	if (TextWidget)
	{
		TestFalse(TEXT("Missing explicit typeface must not write the mapped font asset"),
			HasWidgetProperty(*TextWidget, TEXT("FontFamily")));
		TestFalse(TEXT("Missing explicit typeface must not auto-substitute nearest regular or mapped bold token"),
			HasWidgetProperty(*TextWidget, TEXT("TypefaceFontName")));
	}

	Settings->ExplicitFontMappings = SavedMappings;
	Settings->DefaultFontAsset = SavedDefaultFont;
	FFontResolver::ClearCache();
	return TextWidget != nullptr;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextRunPostScriptNameTriggersRichTextBlockTest,
	"Design2UMG.Text.TextRunPostScriptNameTriggersRichTextBlock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextRunPostScriptNameTriggersRichTextBlockTest::RunTest(const FString& Parameters)
{
	FFontResolver::ClearCache();

	UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	TestTrue(TEXT("Settings available"), Settings != nullptr);
	if (!Settings)
	{
		return false;
	}

	const TMap<FString, TSoftObjectPtr<UFont>> SavedMappings = Settings->ExplicitFontMappings;
	const TSoftObjectPtr<UFont> SavedDefaultFont = Settings->DefaultFontAsset;
	Settings->ExplicitFontMappings.Empty();
	Settings->DefaultFontAsset.Reset();

	UPackage* RunFontPackage = CreatePackage(TEXT("/Game/Design2UMG/Fonts/D2U_RunPostScriptFontForResolverTest"));
	UFont* RunFont = NewObject<UFont>(
		RunFontPackage,
		TEXT("D2U_RunPostScriptFontForResolverTest"),
		RF_Public | RF_Standalone);
	TestTrue(TEXT("Run-level PostScript font fixture created"), RunFont != nullptr);
	if (!RunFont)
	{
		Settings->ExplicitFontMappings = SavedMappings;
		Settings->DefaultFontAsset = SavedDefaultFont;
		return false;
	}

	RunFont->FontCacheType = EFontCacheType::Runtime;
	RunFont->GetMutableInternalCompositeFont().DefaultTypeface.Fonts.Add(FTypefaceEntry(
		FName(TEXT("Regular")),
		FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts/Roboto-Regular.ttf")),
		EFontHinting::Default,
		EFontLoadingPolicy::LazyLoad));
	const FString RunFontPath = RunFont->GetPathName();
	const FString RunRegularTypeface = FFontResolver::FindExactTypefaceForAssetPath(RunFontPath, TEXT("400"));
	TestFalse(TEXT("Run-level PostScript fixture has a usable regular typeface"), RunRegularTypeface.IsEmpty());

	Settings->ExplicitFontMappings.Add(
		TEXT("D2URunOnlyPS-Regular"),
		TSoftObjectPtr<UFont>(FSoftObjectPath(RunFont)));
	FFontResolver::ClearCache();

	const FString Json = TEXT(R"({
		"children": [{
			"id": "postscript_run_text",
			"type": "text",
			"content": [
				{ "content": "Base " },
				{ "content": "Mapped", "fontPostScriptName": "D2URunOnlyPS-Regular" }
			],
			"fontFamily": "D2UParentFamily",
			"fontSize": 16
		}]
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Run-level PostScript fixture parses"), FPenParser::ParseDocument(Json, Doc));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* TextWidget = FindWidgetById(Data.RootWidget, TEXT("postscript_run_text"));
	TestNotNull(TEXT("Converted run-level PostScript text exists"), TextWidget);
	if (TextWidget)
	{
		TestEqual(TEXT("Run-level PostScript-only styling converts to RichTextBlock"), TextWidget->Type, TEXT("RichTextBlock"));
		TestTrue(TEXT("Rich text markup includes mapped run tag"), GetWidgetProperty(*TextWidget, TEXT("Text")).Contains(TEXT("<d2u_run_1>Mapped</>")));
		TestTrue(TEXT("Run-level PostScript style rows exported"), TextWidget->Text.RichTextStyleRowsJson.IsSet());
		if (TextWidget->Text.RichTextStyleRowsJson.IsSet())
		{
			TSharedPtr<FJsonObject> StyleRows;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(TextWidget->Text.RichTextStyleRowsJson.GetValue());
			TestTrue(TEXT("Run-level PostScript style rows JSON is valid"), FJsonSerializer::Deserialize(Reader, StyleRows) && StyleRows.IsValid());
			if (StyleRows.IsValid())
			{
				const TSharedPtr<FJsonObject>* MappedRunRow = nullptr;
				TestTrue(TEXT("Mapped run style row exists"), StyleRows->TryGetObjectField(TEXT("d2u_run_1"), MappedRunRow));
				if (MappedRunRow && MappedRunRow->IsValid())
				{
					TestEqual(TEXT("Mapped run resolves explicit PostScript font asset"), (*MappedRunRow)->GetStringField(TEXT("FontFamily")), RunFontPath);
					TestEqual(TEXT("Mapped run keeps exact regular typeface"), (*MappedRunRow)->GetStringField(TEXT("TypefaceFontName")), RunRegularTypeface);
				}
			}
		}
	}

	Settings->ExplicitFontMappings = SavedMappings;
	Settings->DefaultFontAsset = SavedDefaultFont;
	FFontResolver::ClearCache();
	return TextWidget != nullptr;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMissingDesignFontSkipsGeneratedFontFamilyTest,
	"Design2UMG.Text.MissingDesignFontSkipsGeneratedFontFamily",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMissingDesignFontSkipsGeneratedFontFamilyTest::RunTest(const FString& Parameters)
{
	UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	TestTrue(TEXT("Settings available"), Settings != nullptr);
	if (!Settings)
	{
		return false;
	}
	const TMap<FString, TSoftObjectPtr<UFont>> SavedMappings = Settings->ExplicitFontMappings;
	const TSoftObjectPtr<UFont> SavedDefaultFont = Settings->DefaultFontAsset;
	Settings->ExplicitFontMappings.Empty();
	const FString ProjectInterPath = TEXT("/Game/Design2UMG/Fonts/Inter.Inter");
	Settings->DefaultFontAsset = LoadObject<UFont>(nullptr, *ProjectInterPath)
		? TSoftObjectPtr<UFont>(FSoftObjectPath(ProjectInterPath))
		: TSoftObjectPtr<UFont>();

	const FString Json = TEXT(R"({
		"children": [{
			"id": "missing_font_text",
			"type": "text",
			"content": "Fallback",
			"fontFamily": "DefinitelyMissingProjectFont",
			"fontWeight": "700",
			"fontSize": 16
		}]
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Missing font fixture parses"), FPenParser::ParseDocument(Json, Doc));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* TextWidget = FindWidgetById(Data.RootWidget, TEXT("missing_font_text"));
	TestNotNull(TEXT("Converted missing-font text exists"), TextWidget);
	if (TextWidget)
	{
		TestFalse(TEXT("Missing design font must not write an explicit default FontFamily into generated UMG data"),
			HasWidgetProperty(*TextWidget, TEXT("FontFamily")));
		TestEqual(TEXT("Missing design font keeps a stable fallback typeface token"),
			GetWidgetProperty(*TextWidget, TEXT("TypefaceFontName")),
			TEXT("Bold"));
	}
	Settings->ExplicitFontMappings = SavedMappings;
	Settings->DefaultFontAsset = SavedDefaultFont;
	FFontResolver::ClearCache();
	return TextWidget != nullptr;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInvalidProjectFontWidthFitUsesDeterministicFallbackTest,
	"Design2UMG.Text.InvalidProjectFontWidthFitUsesDeterministicFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FInvalidProjectFontWidthFitUsesDeterministicFallbackTest::RunTest(const FString& Parameters)
{
	const FString InvalidFamily = TEXT("D2U_InvalidWidthFitFontForResolverTest");
	UPackage* InvalidFontPackage = CreatePackage(TEXT("/Game/Design2UMG/Fonts/D2U_InvalidWidthFitFontForResolverTest"));
	UFont* InvalidFont = NewObject<UFont>(
		InvalidFontPackage,
		TEXT("D2U_InvalidWidthFitFontForResolverTest"),
		RF_Public | RF_Standalone);
	TestNotNull(TEXT("Invalid project font fixture created"), InvalidFont);
	if (!InvalidFont)
	{
		return false;
	}
	InvalidFont->FontCacheType = EFontCacheType::Runtime;
	FAssetRegistryModule::AssetCreated(InvalidFont);
	InvalidFontPackage->MarkPackageDirty();
	FFontResolver::ClearCache();

	TSharedPtr<FDesignNode> TextNode = MakeShared<FDesignNode>();
	TextNode->Id = TEXT("invalid_width_fit");
	TextNode->Name = TEXT("Invalid Width Fit");
	TextNode->Type = EDesignNodeType::Text;
	TextNode->Text.Content = TEXT("Free Admin Dashboard Template");
	TextNode->Text.FontFamily = InvalidFamily;
	TextNode->Text.FontWeight = TEXT("700");
	TextNode->Text.FontSize = 55.0f;
	TextNode->Text.TextGrowth = EDesignTextGrowth::FixedWidth;
	TextNode->Text.TextAlignVertical = EDesignTextAlignVertical::Top;
	TextNode->Transform.Width = FDesignSize::Fixed(565.7f);
	TextNode->Transform.bSourceHasWidth = true;
	TextNode->Transform.Height = FDesignSize::Fixed(130.4f);

	const float ResolvedFontSize = TextMeasurement::ResolveTextFontSizeForUMG(TextNode);
	TestTrue(TEXT("Invalid project font width fit stays below broad fallback size"), ResolvedFontSize < 20.0f);
	TestEqual(TEXT("Invalid project font width fit uses deterministic heuristic width"), ResolvedFontSize, 18.0f);
	FFontResolver::ClearCache();
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
