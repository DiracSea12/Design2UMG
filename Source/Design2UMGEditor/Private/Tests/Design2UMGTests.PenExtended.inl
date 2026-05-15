IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenExtendedFieldsAndMeshGradientTest,
	"Design2UMG.PenParser.ExtendedFieldsAndMeshGradient",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPenExtendedFieldsAndMeshGradientTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"children": [
			{
				"id": "txt1",
				"type": "text",
				"content": "Hello",
				"fontStyle": "italic",
				"underline": true,
				"strikethrough": true,
				"href": "https://example.com",
				"fill": {
					"type": "mesh_gradient",
					"columns": 2,
					"rows": 3,
					"colors": [
						{"color": "#FF0000", "position": 0.0},
						{"color": "#00FF00", "position": 1.0}
					],
					"points": [
						{"x": 0.25, "y": 0.75, "color": "#FF0000", "in": [0.1, 0.2], "out": {"x": 0.3, "y": 0.4}}
					]
				}
			},
			{
				"id": "comp1",
				"type": "frame",
				"reusable": true,
				"layoutIncludeStroke": true,
				"stroke": {
					"align": "inside",
					"thickness": 1,
					"fill": "#000000",
					"join": "round",
					"miterAngle": 45,
					"cap": "square",
					"dashPattern": [2, 4]
				},
				"children": [
					{
						"id": "label",
						"type": "text",
						"content": "Default"
					}
				]
			},
			{
				"id": "inst1",
				"type": "ref",
				"ref": "comp1",
				"descendants": {
					"label": {
						"content": "Go",
						"fontStyle": "italic",
						"underline": true,
						"href": "https://override.example"
					}
				}
			}
		]
	})");
	FDesignDocument Doc;
	TestTrue(TEXT("Pen parse succeeds"), FPenParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() < 2)
		return false;
	const TSharedPtr<FDesignNode>& TextNode = Doc.Children[0];
	TestEqual(TEXT("Text node type"), TextNode->Type, EDesignNodeType::Text);
	TestEqual(TEXT("fontStyle parsed"), TextNode->Text.FontStyle, TEXT("italic"));
	TestTrue(TEXT("underline parsed"), TextNode->Text.bUnderline);
	TestTrue(TEXT("strikethrough parsed"), TextNode->Text.bStrikethrough);
	TestEqual(TEXT("href parsed"), TextNode->Text.Href, TEXT("https://example.com"));
	TestEqual(TEXT("mesh_gradient maps to gradient"), TextNode->Visual.Fill.Type, EDesignFillType::Gradient);
	TestTrue(TEXT("mesh_gradient marker set"), TextNode->Visual.Fill.bFromMeshGradient);
	TestTrue(TEXT("mesh_gradient raw payload kept"), !TextNode->Visual.Fill.MeshGradientRaw.IsEmpty());
	TestTrue(TEXT("mesh_gradient columns present"), TextNode->Visual.Fill.MeshGradient.Columns.IsSet());
	TestEqual(TEXT("mesh_gradient columns value"), TextNode->Visual.Fill.MeshGradient.Columns.Get(0), 2);
	TestTrue(TEXT("mesh_gradient rows present"), TextNode->Visual.Fill.MeshGradient.Rows.IsSet());
	TestEqual(TEXT("mesh_gradient rows value"), TextNode->Visual.Fill.MeshGradient.Rows.Get(0), 3);
	TestEqual(TEXT("mesh_gradient point count"), TextNode->Visual.Fill.MeshGradient.Points.Num(), 1);
	if (TextNode->Visual.Fill.MeshGradient.Points.Num() == 1)
	{
		const FDesignMeshGradientPoint& MeshPoint = TextNode->Visual.Fill.MeshGradient.Points[0];
		TestTrue(TEXT("mesh_gradient point position present"), MeshPoint.Position.IsSet());
		TestTrue(TEXT("mesh_gradient point position value"), MeshPoint.Position.IsSet() && MeshPoint.Position.GetValue().Equals(FVector2D(0.25f, 0.75f), 0.0001f));
		TestTrue(TEXT("mesh_gradient in handle present"), MeshPoint.InHandle.Value.IsSet());
		TestTrue(TEXT("mesh_gradient out handle present"), MeshPoint.OutHandle.Value.IsSet());
	}
	const TSharedPtr<FDesignNode>& ComponentNode = Doc.Children[1];
	TestTrue(TEXT("stroke explicit fill present"), ComponentNode->Visual.Stroke.Fill.IsSet());
	TestTrue(TEXT("stroke explicit fill is color"), ComponentNode->Visual.Stroke.Fill.IsSet() && ComponentNode->Visual.Stroke.Fill.GetValue().Type == EDesignFillType::Color);
	const FString* LayoutIncludeStroke = ComponentNode->Metadata.Find(TEXT("layoutIncludeStroke"));
	TestTrue(TEXT("layoutIncludeStroke preserved"), LayoutIncludeStroke && *LayoutIncludeStroke == TEXT("true"));
	const FString* StrokeJoin = ComponentNode->Metadata.Find(TEXT("stroke.join"));
	TestTrue(TEXT("stroke join preserved"), StrokeJoin && *StrokeJoin == TEXT("round"));
	const FString* StrokeCap = ComponentNode->Metadata.Find(TEXT("stroke.cap"));
	TestTrue(TEXT("stroke cap preserved"), StrokeCap && *StrokeCap == TEXT("square"));
	const FString* StrokeMiterAngle = ComponentNode->Metadata.Find(TEXT("stroke.miterAngle"));
	TestTrue(TEXT("stroke miterAngle preserved"), StrokeMiterAngle && !StrokeMiterAngle->IsEmpty());
	const FString* StrokeDashPattern = ComponentNode->Metadata.Find(TEXT("stroke.dashPattern"));
	TestTrue(TEXT("stroke dashPattern preserved"), StrokeDashPattern && StrokeDashPattern->Contains(TEXT("2")) && StrokeDashPattern->Contains(TEXT("4")));
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* OverrideLabel = FindWidgetById(Data.RootWidget, TEXT("label"));
	TestTrue(TEXT("Find override text widget"), OverrideLabel != nullptr);
	if (!OverrideLabel)
		return false;
	TestEqual(TEXT("Override text with href upgrades to rich text"), OverrideLabel->Type, TEXT("RichTextBlock"));
	const FString OverrideMarkup = GetWidgetProperty(*OverrideLabel, TEXT("Text"));
	TestTrue(TEXT("Override text content propagated"), OverrideMarkup.Contains(TEXT("Go")));
	TestTrue(TEXT("Override href serialized into markup"), OverrideMarkup.Contains(TEXT("href=\"https://override.example\"")));
	TestEqual(TEXT("Override fontStyle propagated"), GetWidgetProperty(*OverrideLabel, TEXT("FontStyle")), TEXT("italic"));
	TestEqual(TEXT("Override underline propagated"), GetWidgetProperty(*OverrideLabel, TEXT("Underline")), TEXT("true"));
	TestEqual(TEXT("Override href propagated"), GetWidgetProperty(*OverrideLabel, TEXT("Href")), TEXT("https://override.example"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenRichTextConverterUsesRichTextBlockTest,
	"Design2UMG.Converter.PenRichTextUsesRichTextBlock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPenRichTextConverterUsesRichTextBlockTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"children": [
			{
				"id": "root",
				"type": "frame",
				"width": 320,
				"height": 120,
				"children": [
					{
						"id": "rich_label",
						"type": "text",
						"width": 220,
						"height": "fit_content",
						"fontFamily": "Geist",
						"fontSize": 18,
						"letterSpacing": 0,
						"fill": "#112233",
						"content": [
							{ "content": "Open " },
							{ "content": "Docs", "fontWeight": "700", "fontSize": 0, "letterSpacing": 0, "underline": true, "strikethrough": false, "href": "https://docs.example.com" }
						]
					}
				]
			}
		]
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Pen rich text fixture parses"), FPenParser::ParseDocument(Json, Doc));
	const TSharedPtr<FDesignNode>& ParsedRichTextNode = Doc.Children[0]->Children[0];
	TestTrue(TEXT("Node-level explicit zero letter spacing remains present"), ParsedRichTextNode->Text.HasLetterSpacingValue());
	TestEqual(TEXT("Node-level explicit zero letter spacing value"), ParsedRichTextNode->Text.LetterSpacing, 0.0f);
	TestEqual(TEXT("Rich text run count"), ParsedRichTextNode->Text.TextRuns.Num(), 2);
	if (ParsedRichTextNode->Text.TextRuns.Num() == 2)
	{
		const FDesignTextRun& LinkRun = ParsedRichTextNode->Text.TextRuns[1];
		TestTrue(TEXT("Explicit zero run font size remains present"), LinkRun.FontSize.IsSet());
		TestEqual(TEXT("Explicit zero run font size value"), LinkRun.FontSize.Get(1.0f), 0.0f);
		TestTrue(TEXT("Explicit zero run letter spacing remains present"), LinkRun.LetterSpacing.IsSet());
		TestEqual(TEXT("Explicit zero run letter spacing value"), LinkRun.LetterSpacing.Get(1.0f), 0.0f);
		TestTrue(TEXT("Explicit true underline remains present"), LinkRun.bUnderline.IsSet());
		TestTrue(TEXT("Explicit true underline value"), LinkRun.bUnderline.Get(false));
		TestTrue(TEXT("Explicit false strikethrough remains present"), LinkRun.bStrikethrough.IsSet());
		TestFalse(TEXT("Explicit false strikethrough value"), LinkRun.bStrikethrough.Get(true));
	}
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* RichLabel = FindWidgetById(Data.RootWidget, TEXT("rich_label"));
	TestNotNull(TEXT("Rich text widget exists"), RichLabel);
	if (!RichLabel)
	{
		return false;
	}

	TestEqual(TEXT("Rich text converts to RichTextBlock"), RichLabel->Type, TEXT("RichTextBlock"));

	const FString Markup = GetWidgetProperty(*RichLabel, TEXT("Text"));
	TestTrue(TEXT("Rich text markup contains first run tag"), Markup.Contains(TEXT("<d2u_run_0>")));
	TestTrue(TEXT("Rich text markup contains href attribute"), Markup.Contains(TEXT("href=\"https://docs.example.com\"")));
	TestTrue(TEXT("Rich text markup keeps link text"), Markup.Contains(TEXT("Docs")));
	TestEqual(TEXT("Single rich hyperlink becomes tooltip"), GetWidgetProperty(*RichLabel, TEXT("ToolTipText")), TEXT("https://docs.example.com"));

	TestTrue(TEXT("Rich text style rows exported via typed text payload"), RichLabel->Text.RichTextStyleRowsJson.IsSet());
	if (!RichLabel->Text.RichTextStyleRowsJson.IsSet())
	{
		return false;
	}

	// Parse JSON → stable snapshot: rowName{key=value;...}|... (rows sorted by name, keys in fixed order)
	TSharedPtr<FJsonObject> StyleRows;
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RichLabel->Text.RichTextStyleRowsJson.GetValue());
		TestTrue(TEXT("Style rows JSON is valid"), FJsonSerializer::Deserialize(Reader, StyleRows) && StyleRows.IsValid());
		if (!StyleRows.IsValid())
		{
			return false;
		}
	}

	// Fixed key order matching the serializer's runtime-property set
	static const TCHAR* const KeyOrder[] = {
		TEXT("Color"), TEXT("DesignFontSize"), TEXT("FontFamily"), TEXT("FontSize"),
		TEXT("Href"), TEXT("LetterSpacing"), TEXT("Strikethrough"), TEXT("TypefaceFontName"), TEXT("Underline")
	};

	TArray<FString> RowNames;
	for (const auto& Pair : StyleRows->Values)
	{
		RowNames.Add(Pair.Key);
	}
	RowNames.Sort();

	FString Snapshot;
	for (const FString& RowName : RowNames)
	{
		if (!Snapshot.IsEmpty()) Snapshot += TEXT("|");
		Snapshot += RowName + TEXT("{");
		const TSharedPtr<FJsonObject>* RowObj = nullptr;
		StyleRows->TryGetObjectField(RowName, RowObj);
		bool bFirstKey = true;
		for (const TCHAR* Key : KeyOrder)
		{
			if (!RowObj || !(*RowObj)->HasField(Key)) continue;
			if (!bFirstKey) Snapshot += TEXT(";");
			bFirstKey = false;
			const TSharedPtr<FJsonValue>& Val = (*RowObj)->Values[Key];
			FString ValStr;
			if (Val->Type == EJson::String)
			{
				ValStr = Val->AsString();
			}
			else if (Val->Type == EJson::Number)
			{
				ValStr = FString::Printf(TEXT("%.1f"), Val->AsNumber());
			}
			else if (Val->Type == EJson::Boolean)
			{
				ValStr = Val->AsBool() ? TEXT("true") : TEXT("false");
			}
			Snapshot += FString(Key) + TEXT("=") + ValStr;
		}
		Snapshot += TEXT("}");
	}

	const FString ExpectedSnapshot =
		TEXT("d2u_run_0{Color=#112233FF;DesignFontSize=18.0;FontFamily=/Game/Design2UMG/Fonts/Geist.Geist;FontSize=27.0;LetterSpacing=0.0;TypefaceFontName=Regular}")
		TEXT("|d2u_run_1{Color=#112233FF;FontFamily=/Game/Design2UMG/Fonts/Geist.Geist;Href=https://docs.example.com;LetterSpacing=0.0;TypefaceFontName=Bold;Underline=true}");

	TestEqual(TEXT("Rich text style rows snapshot matches"), Snapshot, ExpectedSnapshot);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPathStrokeOnlyIconRasterizationTest,
	"Design2UMG.Path.StrokeOnlyIconRasterization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPathStrokeOnlyIconRasterizationTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"children": [
			{
				"id": "root",
				"type": "frame",
				"name": "Root",
				"width": 24,
				"height": 24,
				"children": [
					{
						"id": "icon_plus",
						"type": "path",
						"name": "plus",
						"x": 7,
						"y": 7,
						"width": 10,
						"height": 10,
						"geometry": "M7.07104 3.53555l-3.53552 0-3.53552 0m3.53552-3.53555l0 3.53555 0 3.53549",
						"stroke": {
							"align": "center",
							"thickness": 1.5,
							"fill": "#111111",
							"join": "round",
							"cap": "round"
						}
					}
				]
			}
		]
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Stroke-only path parses"), FPenParser::ParseDocument(Json, Doc));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* Widget = FindWidgetById(Data.RootWidget, TEXT("icon_plus"));
	TestTrue(TEXT("Converted widget keeps stroke-only icon"), Widget != nullptr);
	if (!Widget)
	{
		return false;
	}

	TestEqual(TEXT("Stroke-only path converts to image widget"), Widget->Type, TEXT("Image"));
	TestTrue(
		TEXT("Stroke color propagated"),
		FUMGValueParser::ParseColor(GetWidgetProperty(*Widget, UMGPropertyKeys::PathStrokeColor))
			.Equals(FUMGValueParser::ParseColor(TEXT("#111111")), 0.0001f));
	TestEqual(TEXT("Stroke thickness propagated"), GetWidgetProperty(*Widget, UMGPropertyKeys::PathStrokeThickness), TEXT("1.5"));
	TestEqual(TEXT("Stroke cap propagated"), GetWidgetProperty(*Widget, UMGPropertyKeys::PathStrokeCap), TEXT("round"));
	TestEqual(TEXT("Stroke join propagated"), GetWidgetProperty(*Widget, UMGPropertyKeys::PathStrokeJoin), TEXT("round"));

	const FString Geometry = GetWidgetProperty(*Widget, UMGPropertyKeys::PathGeometry);
	const FString FillRule = GetWidgetProperty(*Widget, UMGPropertyKeys::PathFillRule);
	const FString StrokeColorValue = GetWidgetProperty(*Widget, UMGPropertyKeys::PathStrokeColor);
	const float StrokeThickness = FCString::Atof(*GetWidgetProperty(*Widget, UMGPropertyKeys::PathStrokeThickness));
	UTexture2D* Texture = FDesignPathRasterizer::Rasterize(
		Geometry,
		20,
		20,
		TOptional<FLinearColor>(),
		FillRule,
		TOptional<FLinearColor>(FUMGValueParser::ParseColor(StrokeColorValue)),
		StrokeThickness,
		GetWidgetProperty(*Widget, UMGPropertyKeys::PathStrokeCap),
		GetWidgetProperty(*Widget, UMGPropertyKeys::PathStrokeJoin));
	TestTrue(TEXT("Stroke-only path rasterizes to texture"), Texture != nullptr);
	if (!Texture)
	{
		return false;
	}

	TestTrue(TEXT("Stroke-only path produces visible pixels"), CountOpaqueTexturePixels(Texture) >= 24);
	return true;
}
