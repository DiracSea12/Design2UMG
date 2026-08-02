IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTypedNodeEmptyStringStillAppliesTest,
	"Design2UMG.PropertyApplier.TypedEmptyStringApplies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTypedNodeEmptyStringStillAppliesTest::RunTest(const FString& Parameters)
{
	UEditableTextBox* Widget = NewObject<UEditableTextBox>(GetTransientPackageAsObject());
	TestTrue(TEXT("EditableTextBox created"), Widget != nullptr);
	if (!Widget)
	{
		return false;
	}

	Widget->SetText(FText::FromString(TEXT("Existing Text")));
	Widget->SetHintText(FText::FromString(TEXT("Existing Hint")));

	FUMGWidgetNode Node;
	Node.Text.Text = FString();
	Node.Text.HintText = FString();
	FUMGWidgetUtils::ApplyWidgetProperties(Widget, Node);

	TestTrue(TEXT("Explicit empty Text still clears widget text"), Widget->GetText().ToString().IsEmpty());
	TestTrue(TEXT("Explicit empty HintText still clears widget hint"), Widget->GetHintText().ToString().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRenderTransformScaleJsonContractTest,
	"Design2UMG.Json.RenderTransformScaleContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRenderTransformScaleJsonContractTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"RootWidget": {
			"Type": "TextBlock",
			"Name": "ScaledText",
			"Bounds": { "X": 0, "Y": 0, "Width": 100, "Height": 20 },
			"Render": {
				"RenderTransformScale": "1.250000,0.750000"
			}
		}
	})");

	FUMGWidgetData Parsed;
	FString ParseError;
	TestTrue(TEXT("UMGJsonParser parses RenderTransformScale-only payload"), FUMGJsonParser::ParseJson(Json, Parsed, &ParseError));
	if (!ParseError.IsEmpty())
	{
		AddError(ParseError);
	}
	TestFalse(TEXT("Root widget type parsed"), Parsed.RootWidget.Type.IsEmpty());
	if (Parsed.RootWidget.Type.IsEmpty())
	{
		return false;
	}

	const FUMGWidgetNode& Root = Parsed.RootWidget;
	TestTrue(TEXT("RenderTransformScale is marked present"), Root.Render.RenderTransformScale.IsSet());
	TestTrue(TEXT("RenderTransformScale X preserved"), FMath::IsNearlyEqual(Root.Render.RenderTransformScale.GetValue().X, 1.25, KINDA_SMALL_NUMBER));
	TestTrue(TEXT("RenderTransformScale Y preserved"), FMath::IsNearlyEqual(Root.Render.RenderTransformScale.GetValue().Y, 0.75, KINDA_SMALL_NUMBER));

	const FString RoundTripJson = FUMGJsonSerializer::Serialize(Parsed);
	TestTrue(TEXT("Serializer keeps RenderTransformScale"), RoundTripJson.Contains(TEXT("\"RenderTransformScale\"")));
	TestFalse(TEXT("Serializer no longer emits RenderScale alias"), RoundTripJson.Contains(TEXT("\"RenderScale\"")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlotJsonTypedCompatibilityTest,
	"Design2UMG.Json.SlotTypedCompatibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSlotJsonTypedCompatibilityTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"RootWidget": {
			"Type": "TextBlock",
			"Name": "SlotText",
			"Slot": {
				"HorizontalAlignment": "left",
				"HAlign": "center",
				"VerticalAlignment": "top",
				"VAlign": "bottom",
				"SizeRule": "fill"
			}
		}
	})");

	FUMGWidgetData Parsed;
	FString ParseError;
	TestTrue(TEXT("UMGJsonParser parses slot aliases"), FUMGJsonParser::ParseJson(Json, Parsed, &ParseError));
	if (!ParseError.IsEmpty())
	{
		AddError(ParseError);
	}

	const FUMGSlotData& Slot = Parsed.RootWidget.Slot;
	TestTrue(TEXT("HAlign alias wins over HorizontalAlignment"), Slot.HorizontalAlignment == EUMGSlotHorizontalAlignment::Center);
	TestTrue(TEXT("VAlign alias wins over VerticalAlignment"), Slot.VerticalAlignment == EUMGSlotVerticalAlignment::Bottom);
	TestTrue(TEXT("Lowercase SizeRule parses as Fill"), Slot.SizeRule == EUMGSlotSizeRule::Fill);

	const FString RoundTripJson = FUMGJsonSerializer::Serialize(Parsed);
	TestTrue(TEXT("Serializer keeps canonical horizontal alignment"), RoundTripJson.Contains(TEXT("\"HorizontalAlignment\": \"Center\"")));
	TestTrue(TEXT("Serializer keeps canonical vertical alignment"), RoundTripJson.Contains(TEXT("\"VerticalAlignment\": \"Bottom\"")));
	TestTrue(TEXT("Serializer keeps canonical size rule"), RoundTripJson.Contains(TEXT("\"SizeRule\": \"Fill\"")));

	FUMGSlotData UnknownAlignmentSlot;
	UnknownAlignmentSlot.HorizontalAlignment = FUMGValueParser::ParseSlotHorizontalAlignment(TEXT("Diagonal"));
	UnknownAlignmentSlot.VerticalAlignment = FUMGValueParser::ParseSlotVerticalAlignment(TEXT("Outside"));
	TestTrue(TEXT("Unknown horizontal alignment falls back to Fill"), UnknownAlignmentSlot.HorizontalAlignment == EUMGSlotHorizontalAlignment::Fill);
	TestTrue(TEXT("Unknown vertical alignment falls back to Fill"), UnknownAlignmentSlot.VerticalAlignment == EUMGSlotVerticalAlignment::Fill);
	TestTrue(TEXT("Unknown SizeRule falls back to Auto"), FUMGValueParser::ParseSlotSizeRule(TEXT("Unexpected")) == EUMGSlotSizeRule::Auto);
	TestTrue(TEXT("Lowercase Auto size rule parses as Auto"), FUMGValueParser::ParseSlotSizeRule(TEXT("auto")) == EUMGSlotSizeRule::Auto);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParserNumberStringFlexibleTest,
	"Design2UMG.Unit.NumberStringFlexible",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FParserNumberStringFlexibleTest::RunTest(const FString& Parameters)
{
	FString Json;
	TestTrue(TEXT("Load fixture ParserNumberStringFlexible"), LoadFixtureJson(TEXT("ParserNumberStringFlexible.json"), Json));
	if (Json.IsEmpty())
		return false;
	FDesignDocument Doc;
	TestTrue(TEXT("String number parse succeeds"), FPenParser::ParseDocument(Json, Doc));
	TestEqual(TEXT("Top-level child count"), Doc.Children.Num(), 1);
	if (Doc.Children.Num() == 0)
		return false;
	const TSharedPtr<FDesignNode>& Root = Doc.Children[0];
	TestTrue(TEXT("Root has parsed X"), Root->Transform.HasExplicitX());
	TestTrue(TEXT("Root has parsed Y"), Root->Transform.HasExplicitY());
	TestEqual(TEXT("Root gap parsed from string"), Root->LayoutData.ExplicitGap.Get(0.0f), 8.0f);
	TestEqual(TEXT("Root opacity parsed from string"), Root->Transform.Opacity, 0.75f);
	TestEqual(TEXT("Root padding top parsed from string"), Root->LayoutData.ExplicitPadding.Get(FDesignPadding{}).Top, 1.0f);
	TestEqual(TEXT("Root padding right parsed from string"), Root->LayoutData.ExplicitPadding.Get(FDesignPadding{}).Right, 2.0f);
	TestEqual(TEXT("Root padding bottom parsed from string"), Root->LayoutData.ExplicitPadding.Get(FDesignPadding{}).Bottom, 3.0f);
	TestEqual(TEXT("Root padding left parsed from string"), Root->LayoutData.ExplicitPadding.Get(FDesignPadding{}).Left, 4.0f);
	TestTrue(TEXT("Root cornerRadius parsed as per-corner"), !Root->Visual.CornerRadius.bUniform);
	TestEqual(TEXT("Root cornerRadius TL parsed from string"), Root->Visual.CornerRadius.TopLeft, 5.0f);
	TestEqual(TEXT("Root cornerRadius TR parsed from string"), Root->Visual.CornerRadius.TopRight, 6.0f);
	TestEqual(TEXT("Root cornerRadius BR parsed from string"), Root->Visual.CornerRadius.BottomRight, 7.0f);
	TestEqual(TEXT("Root cornerRadius BL parsed from string"), Root->Visual.CornerRadius.BottomLeft, 8.0f);
	TestTrue(TEXT("Root stroke thickness parsed as uniform"), Root->Visual.Stroke.Thickness.bUniform);
	TestEqual(TEXT("Root stroke thickness parsed from string"), Root->Visual.Stroke.Thickness.Uniform, 2.5f);
	TestEqual(TEXT("Root image transform A parsed from string"), Root->Visual.PrimaryFill().Image.Transform.A, 1.0f);
	TestEqual(TEXT("Root image transform D parsed from string"), Root->Visual.PrimaryFill().Image.Transform.D, 1.0f);
	TestEqual(TEXT("Root child count"), Root->Children.Num(), 2);
	if (Root->Children.Num() < 2)
		return false;
	TestEqual(TEXT("Text fontSize parsed from string"), Root->Children[0]->Text.FontSize, 16.0f);
	TestEqual(TEXT("Text lineHeight parsed from string"), Root->Children[0]->Text.LineHeight, 1.5f);
	TestEqual(TEXT("Text letterSpacing parsed from string"), Root->Children[0]->Text.LetterSpacing, 0.2f);
	TestEqual(TEXT("Icon weight parsed from string"), Root->Children[1]->Text.IconWeight, 500);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenPaddingTwoValueAxisOrderTest,
	"Design2UMG.Parser.PenPaddingTwoValueAxisOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPenPaddingTwoValueAxisOrderTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"children": [{
			"id": "panel",
			"type": "frame",
			"padding": [6, 12]
		}]
	})");
	FDesignDocument Doc;
	TestTrue(TEXT("ParseDocument succeeds"), FPenParser::ParseDocument(Json, Doc));
	TestEqual(TEXT("Child count"), Doc.Children.Num(), 1);
	if (Doc.Children.Num() == 0)
	{
		return false;
	}
	const TSharedPtr<FDesignNode>& Panel = Doc.Children[0];
	TestEqual(TEXT("Two-value padding top"), Panel->LayoutData.ExplicitPadding.Get(FDesignPadding{}).Top, 6.0f);
	TestEqual(TEXT("Two-value padding bottom"), Panel->LayoutData.ExplicitPadding.Get(FDesignPadding{}).Bottom, 6.0f);
	TestEqual(TEXT("Two-value padding left"), Panel->LayoutData.ExplicitPadding.Get(FDesignPadding{}).Left, 12.0f);
	TestEqual(TEXT("Two-value padding right"), Panel->LayoutData.ExplicitPadding.Get(FDesignPadding{}).Right, 12.0f);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesignSchemaSourceExtensionsRemainOptInTest,
	"Design2UMG.Schema.SourceExtensionsRemainOptIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FDesignSchemaSourceExtensionsRemainOptInTest::RunTest(const FString& Parameters)
{
	FDesignNode Node;
	TestFalse(TEXT("Fresh node has no source extension"), Node.HasSourceData());
	const FDesignSourceNodeData& EmptyNodeSource = Node.Source();
	TestFalse(TEXT("Read-only node source access stays opt-in"), Node.HasSourceData());
	TestTrue(TEXT("Read-only node source returns empty source type"), EmptyNodeSource.SourceNodeType.IsEmpty());

	Node.MutableSource().SourceNodeType = TEXT("FRAME");
	TestTrue(TEXT("Explicit node source mutation materializes extension"), Node.HasSourceData());
	TestEqual(TEXT("Explicit node source mutation persists"), Node.Source().SourceNodeType, FString(TEXT("FRAME")));

	FDesignDocument Document;
	TestFalse(TEXT("Fresh document has no source document extension"), Document.HasSourceDocumentData());
	TestFalse(TEXT("Fresh document has no parse diagnostics extension"), Document.HasParseDiagnostics());

	const FDesignSourceDocumentData& EmptyDocumentSource = Document.SourceDocument();
	const FDesignParseDiagnostics& EmptyDiagnostics = Document.ParseDiagnostics();
	TestFalse(TEXT("Read-only document source access stays opt-in"), Document.HasSourceDocumentData());
	TestFalse(TEXT("Read-only diagnostics access stays opt-in"), Document.HasParseDiagnostics());
	TestTrue(TEXT("Read-only document source returns empty prototype start id"), EmptyDocumentSource.PrototypeStartNodeId.IsEmpty());
	TestEqual(TEXT("Read-only diagnostics returns empty skipped-node list"), EmptyDiagnostics.SkippedDesignerNodes.Num(), 0);

	Document.MutableSourceDocument().PrototypeStartNodeId = TEXT("root");
	FDesignSkippedNodeReport& AddedReport = Document.MutableParseDiagnostics().SkippedDesignerNodes.AddDefaulted_GetRef();
	AddedReport.NodeId = TEXT("skip-1");

	TestTrue(TEXT("Explicit document source mutation materializes source document"), Document.HasSourceDocumentData());
	TestTrue(TEXT("Explicit diagnostics mutation materializes diagnostics"), Document.HasParseDiagnostics());
	TestEqual(TEXT("Explicit document source mutation persists"), Document.SourceDocument().PrototypeStartNodeId, FString(TEXT("root")));
	TestEqual(TEXT("Explicit diagnostics mutation persists"), Document.ParseDiagnostics().SkippedDesignerNodes.Num(), 1);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUMGSchemaSourceExtensionsRemainOptInTest,
	"Design2UMG.Schema.UMGSourceExtensionsRemainOptIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FUMGSchemaSourceExtensionsRemainOptInTest::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Node;
	TestFalse(TEXT("Fresh UMG node has no source extension"), Node.HasSourceData());
	const FUMGSourceNodeData& EmptyNodeSource = Node.Source();
	TestFalse(TEXT("Read-only UMG node source access stays opt-in"), Node.HasSourceData());
	TestTrue(TEXT("Read-only UMG node source starts empty"), EmptyNodeSource.SourceNodeType.IsEmpty());

	Node.MutableSource().SourceNodeType = TEXT("INSTANCE");
	Node.MutableSource().ComponentId = TEXT("12:34");
	TestTrue(TEXT("Explicit UMG node source mutation materializes extension"), Node.HasSourceData());
	TestEqual(TEXT("Explicit UMG node source mutation persists"), Node.Source().SourceNodeType, FString(TEXT("INSTANCE")));
	TestEqual(TEXT("Explicit UMG node component id persists"), Node.Source().ComponentId, FString(TEXT("12:34")));

	FUMGWidgetData Data;
	TestFalse(TEXT("Fresh UMG widget data has no source document extension"), Data.HasSourceDocumentData());
	TestFalse(TEXT("Fresh UMG widget data has no prototype navigation extension"), Data.HasPrototypeNavigationData());

	const FUMGSourceDocumentData& EmptySourceDocument = Data.SourceDocument();
	const FUMGPrototypeNavigationData& EmptyPrototypeNavigation = Data.PrototypeNavigation();
	TestFalse(TEXT("Read-only UMG source document access stays opt-in"), Data.HasSourceDocumentData());
	TestFalse(TEXT("Read-only UMG prototype access stays opt-in"), Data.HasPrototypeNavigationData());
	TestTrue(TEXT("Read-only UMG source document starts empty"), EmptySourceDocument.ComponentsJson.IsEmpty());
	TestEqual(TEXT("Read-only UMG prototype starts empty"), EmptyPrototypeNavigation.ClickBindings.Num(), 0);

	Data.MutableSourceDocument().ComponentsJson = TEXT("{\"12:34\":{\"key\":\"button\"}}");
	FUMGPrototypeNavigationBinding& Binding = Data.MutablePrototypeNavigation().ClickBindings.AddDefaulted_GetRef();
	Binding.TriggerWidgetId = TEXT("cta_button");
	Binding.TriggerWidgetName = TEXT("cta_button");
	Binding.Action = EUMGPrototypeNavigationAction::Back;

	TestTrue(TEXT("Explicit UMG source document mutation materializes extension"), Data.HasSourceDocumentData());
	TestTrue(TEXT("Explicit UMG prototype mutation materializes extension"), Data.HasPrototypeNavigationData());
	TestTrue(TEXT("Explicit UMG source document mutation persists"), Data.SourceDocument().ComponentsJson.Contains(TEXT("button")));
	TestEqual(TEXT("Explicit UMG prototype mutation persists"), Data.PrototypeNavigation().ClickBindings.Num(), 1);
	return true;
}
