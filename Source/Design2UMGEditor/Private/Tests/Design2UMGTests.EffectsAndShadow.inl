IMPLEMENT_SIMPLE_AUTOMATION_TEST(FForegroundBlurRetainerWrapperTest,
	"Design2UMG.Converter.ForegroundBlurRetainerWrapper",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FForegroundBlurRetainerWrapperTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"children": [
			{
				"id": "blur_card",
				"type": "frame",
				"name": "Blur Card",
				"width": 240,
				"height": 80,
				"effect": {
					"type": "blur",
					"radius": 12
				},
				"children": [
					{
						"id": "title",
						"type": "text",
						"content": "Blurred",
						"fontSize": 24
					}
				]
			}
		]
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Foreground blur fixture parses"), FPenParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() != 1)
	{
		return false;
	}

	TestTrue(TEXT("Foreground blur effect parsed into IR"),
		Doc.Children[0]->Visual.Effects.Num() == 1 && Doc.Children[0]->Visual.Effects[0].Type == EDesignEffectType::Blur);
	TestTrue(TEXT("Foreground blur radius preserved"),
		Doc.Children[0]->Visual.Effects.Num() == 1 && FMath::IsNearlyEqual(Doc.Children[0]->Visual.Effects[0].Radius, 12.0f));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	TestEqual(TEXT("Foreground blur uses RetainerBox wrapper"), Data.RootWidget.Type, TEXT("RetainerBox"));
	TestTrue(TEXT("Foreground blur retain rendering typed"), Data.RootWidget.Effect.bHasRetainRendering && Data.RootWidget.Effect.bRetainRendering);
	TestEqual(TEXT("Foreground blur effect material ref exported"), Data.RootWidget.Effect.EffectMaterial, TEXT("d2u.retainer.foreground_blur"));
	TestEqual(TEXT("Foreground blur texture parameter exported"), Data.RootWidget.Effect.TextureParameter, TEXT("Texture"));

	const FVector2D UVStep = Data.RootWidget.Effect.EffectBlurUVStep;
	TestTrue(TEXT("Foreground blur UV step is typed"), Data.RootWidget.Effect.bHasEffectBlurUVStep);
	TestTrue(TEXT("Foreground blur UV step X positive"), UVStep.X > 0.0f);
	TestTrue(TEXT("Foreground blur UV step Y positive"), UVStep.Y > 0.0f);
	TestTrue(TEXT("Foreground blur wrapper keeps child"), Data.RootWidget.Children.Num() == 1);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInnerShadowRetainerWrapperTest,
	"Design2UMG.Shadow.InnerShadowRetainerWrapper",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FInnerShadowRetainerWrapperTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"children": [
			{
				"id": "inner_shadow_card",
				"type": "frame",
				"name": "Inner Shadow Card",
				"width": 200,
				"height": 80,
				"fill": "#FFFFFFFF",
				"effect": {
					"type": "shadow",
					"shadowType": "inner",
					"color": "#00000066",
					"offset": { "x": 4, "y": 3 },
					"blur": 8
				},
				"children": [
					{
						"id": "title",
						"type": "text",
						"content": "Inset",
						"fontSize": 24
					}
				]
			}
		]
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Inner shadow fixture parses"), FPenParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() != 1)
	{
		return false;
	}

	TestTrue(TEXT("Inner shadow parsed into IR"),
		Doc.Children[0]->Visual.Effects.Num() == 1
		&& Doc.Children[0]->Visual.Effects[0].Type == EDesignEffectType::Shadow
		&& Doc.Children[0]->Visual.Effects[0].Shadow.bInner);

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	TestEqual(TEXT("Inner shadow uses RetainerBox wrapper"), Data.RootWidget.Type, TEXT("RetainerBox"));
	TestTrue(TEXT("Inner shadow retain rendering typed"), Data.RootWidget.Effect.bHasRetainRendering && Data.RootWidget.Effect.bRetainRendering);
	TestEqual(TEXT("Inner shadow effect material ref exported"), Data.RootWidget.Effect.EffectMaterial, TEXT("d2u.retainer.inner_shadow"));
	TestEqual(TEXT("Inner shadow texture parameter exported"), Data.RootWidget.Effect.TextureParameter, TEXT("Texture"));
	TestTrue(TEXT("Inner shadow color exported via typed effect"), Data.RootWidget.Effect.bHasEffectColor);
	TestTrue(TEXT("Inner shadow offset exported via typed effect"), Data.RootWidget.Effect.bHasEffectOffset);
	TestTrue(TEXT("Inner shadow blur step exported via typed effect"), Data.RootWidget.Effect.bHasEffectBlurUVStep);
	TestTrue(TEXT("Inner shadow normalized offset X preserved"), FMath::IsNearlyEqual(Data.RootWidget.Effect.EffectOffset.X, 0.02f, 0.0001f));
	TestTrue(TEXT("Inner shadow normalized offset Y preserved"), FMath::IsNearlyEqual(Data.RootWidget.Effect.EffectOffset.Y, 0.0375f, 0.0001f));
	TestTrue(TEXT("Inner shadow color alpha preserved"), FMath::IsNearlyEqual(Data.RootWidget.Effect.EffectColor.A, 0.4f, 0.01f));
	TestTrue(TEXT("Inner shadow wrapper keeps child"), Data.RootWidget.Children.Num() == 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBackgroundBlurUsesTypedEffectDataTest,
	"Design2UMG.Converter.BackgroundBlurUsesTypedEffectData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBackgroundBlurUsesTypedEffectDataTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"children": [
			{
				"id": "bg_blur_card",
				"type": "frame",
				"name": "Blur Layer",
				"width": 240,
				"height": 80,
				"effect": {
					"type": "background_blur",
					"radius": 18
				},
				"children": [
					{
						"id": "title",
						"type": "text",
						"content": "Backdrop",
						"fontSize": 24
					}
				]
			}
		]
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Background blur fixture parses"), FPenParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() != 1)
	{
		return false;
	}

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	TestEqual(TEXT("Background blur uses BackgroundBlur wrapper"), Data.RootWidget.Type, TEXT("BackgroundBlur"));
	TestTrue(TEXT("Background blur strength exported via typed effect"), Data.RootWidget.Effect.bHasBlurStrength);
	TestTrue(TEXT("Background blur strength preserved"), FMath::IsNearlyEqual(Data.RootWidget.Effect.BlurStrength, 18.0f));
	TestTrue(TEXT("Background blur wrapper keeps child"), Data.RootWidget.Children.Num() == 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaEffectsConvertToTypedWrappersTest,
	"Design2UMG.FigmaParser.EffectsConvertToTypedWrappers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaEffectsConvertToTypedWrappersTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"document": {
			"id": "0:0",
			"name": "Doc",
			"type": "DOCUMENT",
			"children": [{
				"id": "0:1",
				"name": "Page",
				"type": "CANVAS",
				"children": [{
					"id": "1:1",
					"name": "EffectCard",
					"type": "FRAME",
					"visible": true,
					"absoluteBoundingBox": {"x":0,"y":0,"width":180,"height":96},
					"absoluteRenderBounds": {"x":0,"y":0,"width":180,"height":96},
					"effects": [
						{
							"type": "INNER_SHADOW",
							"visible": true,
							"blendMode": "NORMAL",
							"offset": {"x":4,"y":-2},
							"radius": 12,
							"color": {"r":0,"g":0,"b":0,"a":0.3}
						},
						{
							"type": "BACKGROUND_BLUR",
							"visible": true,
							"radius": 16
						}
					],
					"children": [{
						"id": "1:2",
						"name": "Title",
						"type": "TEXT",
						"visible": true,
						"absoluteBoundingBox": {"x":16,"y":20,"width":80,"height":24},
						"characters": "Fx",
						"style": {"fontFamily":"Inter","fontSize":20,"fontWeight":500,"textAlignHorizontal":"LEFT","textAlignVertical":"TOP","textDecoration":"NONE","letterSpacing":0,"lineHeightUnit":"AUTO","lineHeightPx":0}
					}]
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Figma effect wrapper fixture parses"), FFigmaParser::ParseDocument(Json, Doc));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	TestEqual(TEXT("Figma background blur becomes outer wrapper"), Data.RootWidget.Type, TEXT("BackgroundBlur"));
	TestTrue(TEXT("Figma background blur keeps typed strength"), Data.RootWidget.Effect.bHasBlurStrength && FMath::IsNearlyEqual(Data.RootWidget.Effect.BlurStrength, 16.0f, 0.001f));
	TestTrue(TEXT("Figma background blur keeps one child"), Data.RootWidget.Children.Num() == 1);
	if (Data.RootWidget.Children.Num() != 1)
	{
		return false;
	}

	const FUMGWidgetNode& InnerShadowWrapper = Data.RootWidget.Children[0];
	TestEqual(TEXT("Figma inner shadow becomes retainer child"), InnerShadowWrapper.Type, TEXT("RetainerBox"));
	TestTrue(TEXT("Figma inner shadow retain rendering typed"), InnerShadowWrapper.Effect.bHasRetainRendering && InnerShadowWrapper.Effect.bRetainRendering);
	TestEqual(TEXT("Figma inner shadow effect material typed"), InnerShadowWrapper.Effect.EffectMaterial, TEXT("d2u.retainer.inner_shadow"));
	TestTrue(TEXT("Figma inner shadow offset typed"), InnerShadowWrapper.Effect.bHasEffectOffset);
	TestTrue(TEXT("Figma inner shadow blur typed"), InnerShadowWrapper.Effect.bHasEffectBlurUVStep);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaLayerBlurConvertsToForegroundBlurWrapperTest,
	"Design2UMG.FigmaParser.LayerBlurConvertsToForegroundBlurWrapper",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaLayerBlurConvertsToForegroundBlurWrapperTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"document": {
			"id": "0:0",
			"name": "Doc",
			"type": "DOCUMENT",
			"children": [{
				"id": "0:1",
				"name": "Page",
				"type": "CANVAS",
				"children": [{
					"id": "1:1",
					"name": "BlurCard",
					"type": "FRAME",
					"visible": true,
					"absoluteBoundingBox": {"x":0,"y":0,"width":160,"height":80},
					"absoluteRenderBounds": {"x":0,"y":0,"width":160,"height":80},
					"effects": [{
						"type": "LAYER_BLUR",
						"visible": true,
						"radius": 14
					}],
					"children": []
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Figma layer blur fixture parses"), FFigmaParser::ParseDocument(Json, Doc));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	TestEqual(TEXT("Figma layer blur becomes foreground retainer wrapper"), Data.RootWidget.Type, TEXT("RetainerBox"));
	TestTrue(TEXT("Figma layer blur retain rendering typed"), Data.RootWidget.Effect.bHasRetainRendering && Data.RootWidget.Effect.bRetainRendering);
	TestEqual(TEXT("Figma layer blur effect material typed"), Data.RootWidget.Effect.EffectMaterial, TEXT("d2u.retainer.foreground_blur"));
	TestTrue(TEXT("Figma layer blur UV step typed"), Data.RootWidget.Effect.bHasEffectBlurUVStep);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaDropShadowConvertsToBoxShadowWrapperTest,
	"Design2UMG.FigmaParser.DropShadowConvertsToBoxShadowWrapper",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaDropShadowConvertsToBoxShadowWrapperTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"document": {
			"id": "0:0",
			"name": "Doc",
			"type": "DOCUMENT",
			"children": [{
				"id": "0:1",
				"name": "Page",
				"type": "CANVAS",
				"children": [{
					"id": "1:1",
					"name": "ShadowCard",
					"type": "RECTANGLE",
					"visible": true,
					"absoluteBoundingBox": {"x":0,"y":0,"width":160,"height":80},
					"absoluteRenderBounds": {"x":0,"y":0,"width":160,"height":80},
					"fills": [{"type":"SOLID","visible":true,"color":{"r":1,"g":1,"b":1,"a":1}}],
					"effects": [{
						"type": "DROP_SHADOW",
						"visible": true,
						"blendMode": "NORMAL",
						"offset": {"x":6,"y":4},
						"radius": 10,
						"spread": 2,
						"color": {"r":0,"g":0,"b":0,"a":0.2}
					}],
					"children": []
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Figma drop shadow fixture parses"), FFigmaParser::ParseDocument(Json, Doc));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	TestEqual(TEXT("Figma outer shadow becomes overlay wrapper"), Data.RootWidget.Type, TEXT("Overlay"));
	TestTrue(TEXT("Figma outer shadow wrapper keeps shadow and content children"), Data.RootWidget.Children.Num() == 2);
	if (Data.RootWidget.Children.Num() != 2)
	{
		return false;
	}

	TestEqual(TEXT("Figma outer shadow synthetic shadow node is image"), Data.RootWidget.Children[0].Type, TEXT("Image"));
	TestTrue(TEXT("Figma outer shadow synthetic blur typed"), Data.RootWidget.Children[0].Shadow.bHasShadowBlur && FMath::IsNearlyEqual(Data.RootWidget.Children[0].Shadow.ShadowBlur, 10.0f, 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRetainerEffectMaterialApplicationTest,
	"Design2UMG.PropertyApplier.RetainerEffectMaterialForegroundBlur",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRetainerEffectMaterialApplicationTest::RunTest(const FString& Parameters)
{
	URetainerBox* Widget = NewObject<URetainerBox>(GetTransientPackageAsObject());
	TestTrue(TEXT("RetainerBox created"), Widget != nullptr);
	if (!Widget)
	{
		return false;
	}

	FUMGWidgetNode Node;
	Node.Type = TEXT("RetainerBox");
	Node.Effect.bRetainRendering = true;
	Node.Effect.bHasRetainRendering = true;
	Node.Effect.EffectMaterial = TEXT("d2u.retainer.foreground_blur");
	Node.Effect.TextureParameter = TEXT("Texture");
	Node.Effect.EffectBlurUVStep = FVector2D(0.01f, 0.02f);
	Node.Effect.bHasEffectBlurUVStep = true;
	FUMGWidgetUtils::ApplyWidgetProperties(Widget, Node);

	UMaterialInstanceDynamic* DynamicEffect = Widget->GetEffectMaterial();
	TestTrue(TEXT("RetainerBox dynamic effect material created"), DynamicEffect != nullptr);
	if (!DynamicEffect)
	{
		return false;
	}

	TestEqual(TEXT("RetainerBox texture parameter kept"), Widget->GetTextureParameter(), FName(TEXT("Texture")));
	const FLinearColor BlurUVStep = DynamicEffect->K2_GetVectorParameterValue(TEXT("BlurUVStep"));
	TestTrue(TEXT("RetainerBox blur UV step X applied"), FMath::IsNearlyEqual(BlurUVStep.R, 0.01f, 0.0001f));
	TestTrue(TEXT("RetainerBox blur UV step Y applied"), FMath::IsNearlyEqual(BlurUVStep.G, 0.02f, 0.0001f));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInnerShadowRetainerMaterialApplicationTest,
	"Design2UMG.Shadow.InnerShadowRetainerMaterialApplication",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FInnerShadowRetainerMaterialApplicationTest::RunTest(const FString& Parameters)
{
	URetainerBox* Widget = NewObject<URetainerBox>(GetTransientPackageAsObject());
	TestTrue(TEXT("Inner shadow RetainerBox created"), Widget != nullptr);
	if (!Widget)
	{
		return false;
	}

	FUMGWidgetNode Node;
	Node.Type = TEXT("RetainerBox");
	Node.Effect.bRetainRendering = true;
	Node.Effect.bHasRetainRendering = true;
	Node.Effect.EffectMaterial = TEXT("d2u.retainer.inner_shadow");
	Node.Effect.TextureParameter = TEXT("Texture");
	Node.Effect.EffectColor = FLinearColor(0.1f, 0.2f, 0.3f, 0.4f);
	Node.Effect.bHasEffectColor = true;
	Node.Effect.EffectOffset = FVector2D(0.05f, -0.03f);
	Node.Effect.bHasEffectOffset = true;
	Node.Effect.EffectBlurUVStep = FVector2D(0.01f, 0.02f);
	Node.Effect.bHasEffectBlurUVStep = true;
	FUMGWidgetUtils::ApplyWidgetProperties(Widget, Node);

	UMaterialInstanceDynamic* DynamicEffect = Widget->GetEffectMaterial();
	TestTrue(TEXT("Inner shadow dynamic effect material created"), DynamicEffect != nullptr);
	if (!DynamicEffect)
	{
		return false;
	}

	const FLinearColor BlurUVStep = DynamicEffect->K2_GetVectorParameterValue(TEXT("BlurUVStep"));
	const FLinearColor InnerShadowColor = DynamicEffect->K2_GetVectorParameterValue(TEXT("InnerShadowColor"));
	const FLinearColor InnerShadowOffset = DynamicEffect->K2_GetVectorParameterValue(TEXT("InnerShadowOffset"));
	const float InnerShadowStrength = DynamicEffect->K2_GetScalarParameterValue(TEXT("InnerShadowStrength"));
	TestTrue(TEXT("Inner shadow blur UV step X applied"), FMath::IsNearlyEqual(BlurUVStep.R, 0.01f, 0.0001f));
	TestTrue(TEXT("Inner shadow blur UV step Y applied"), FMath::IsNearlyEqual(BlurUVStep.G, 0.02f, 0.0001f));
	TestTrue(TEXT("Inner shadow color R applied"), FMath::IsNearlyEqual(InnerShadowColor.R, 0.1f, 0.0001f));
	TestTrue(TEXT("Inner shadow color A applied"), FMath::IsNearlyEqual(InnerShadowColor.A, 0.4f, 0.0001f));
	TestTrue(TEXT("Inner shadow strength applied"), FMath::IsNearlyEqual(InnerShadowStrength, 0.4f, 0.0001f));
	TestTrue(TEXT("Inner shadow offset X applied"), FMath::IsNearlyEqual(InnerShadowOffset.R, 0.05f, 0.0001f));
	TestTrue(TEXT("Inner shadow offset Y applied"), FMath::IsNearlyEqual(InnerShadowOffset.G, -0.03f, 0.0001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRichTextBlockStyleRowsApplicationTest,
	"Design2UMG.PropertyApplier.RichTextBlockStyleRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRichTextBlockStyleRowsApplicationTest::RunTest(const FString& Parameters)
{
	URichTextBlock* Widget = NewObject<URichTextBlock>(GetTransientPackageAsObject());
	TestTrue(TEXT("RichTextBlock created"), Widget != nullptr);
	if (!Widget)
	{
		return false;
	}

	FTextBlockStyle PreconfiguredStyle = FTextBlockStyle::GetDefault();
	PreconfiguredStyle.ShadowColorAndOpacity = FLinearColor(0.8f, 0.1f, 0.2f, 0.5f);
	PreconfiguredStyle.ShadowOffset = FVector2D(2.0f, 3.0f);
	Widget->SetDefaultTextStyle(PreconfiguredStyle);

	FUMGWidgetNode Node;
	Node.Type = TEXT("RichTextBlock");
	Node.Text.Text = TEXT("<d2u_run_0>Hello </><d2u_run_1 href=\"https://docs.example.com\">Docs</>");
	Node.Text.bHasText = true;
	Node.Text.FontFamily = TEXT("Geist");
	Node.Text.bHasFontFamily = true;
	Node.Text.TypefaceFontName = TEXT("Regular");
	Node.Text.bHasTypefaceFontName = true;
	Node.Text.Justification = TEXT("Center");
	Node.Text.bHasJustification = true;
	Node.Text.bAutoWrapText = true;
	Node.Text.bHasAutoWrapText = true;
	Node.Text.WrapTextAt = 180.0f;
	Node.Text.bHasWrapTextAt = true;
	Node.Text.LineHeightPercentage = 1.25f;
	Node.Text.bHasLineHeightPercentage = true;
	Node.Text.Color = FLinearColor(0.1f, 0.2f, 0.3f, 1.0f);
	Node.Text.bHasColor = true;
	Node.Text.RichTextStyleRowsJson = TEXT("{\"d2u_run_0\":{\"Color\":\"#112233FF\"},\"d2u_run_1\":{\"FontSize\":22.0,\"DesignFontSize\":22.0,\"Color\":\"#224466FF\",\"Underline\":true,\"Href\":\"https://docs.example.com\"}}");
	Node.Text.bHasRichTextStyleRows = true;
	Node.TextStyle.FontSize = 18.0f;
	Node.TextStyle.SourceFontSize = 18.0f;
	Node.TextStyle.LetterSpacing = 1.5f;
	Node.TextStyle.bHasLetterSpacing = true;
	Node.WidgetStyle.Padding = FMargin(3.0f, 4.0f, 5.0f, 6.0f);
	Node.WidgetStyle.ToolTipText = TEXT("https://docs.example.com");

	FUMGWidgetUtils::ApplyWidgetProperties(Widget, Node);

	TestEqual(TEXT("Rich text source text applied"), Widget->GetText().ToString(), TEXT("<d2u_run_0>Hello </><d2u_run_1 href=\"https://docs.example.com\">Docs</>"));
	TestEqual(TEXT("Rich text tooltip applied"), Widget->GetToolTipText().ToString(), TEXT("https://docs.example.com"));
	TestTrue(TEXT("Rich text auto wrap applied"), Widget->GetAutoWrapText());
	TestTrue(TEXT("Rich text wrap width applied"), FMath::IsNearlyEqual(Widget->GetWrapTextAt(), 180.0f, 0.001f));
	const FStructProperty* MarginProperty = FindFProperty<FStructProperty>(UTextLayoutWidget::StaticClass(), TEXT("Margin"));
	TestTrue(TEXT("Rich text margin property found"), MarginProperty != nullptr);
	if (MarginProperty)
	{
		const FMargin* MarginValue = MarginProperty->ContainerPtrToValuePtr<FMargin>(Widget);
		TestTrue(TEXT("Rich text padding applied as internal margin"),
			MarginValue
			&& FMath::IsNearlyEqual(MarginValue->Left, 3.0f, 0.001f)
			&& FMath::IsNearlyEqual(MarginValue->Top, 4.0f, 0.001f)
			&& FMath::IsNearlyEqual(MarginValue->Right, 5.0f, 0.001f)
			&& FMath::IsNearlyEqual(MarginValue->Bottom, 6.0f, 0.001f));
	}

	const FTextBlockStyle& DefaultStyle = Widget->GetCurrentDefaultTextStyle();
	TestEqual(TEXT("Rich text default font size applied"), DefaultStyle.Font.Size, 18.0f);
	const FLinearColor ExpectedDefaultColor = FUMGValueParser::ParseColor(
		Design2UMGWidgetNodeAccess::SerializeWidgetColor(Node.Text.Color));
	TestTrue(TEXT("Rich text default color applied"),
		DefaultStyle.ColorAndOpacity.GetSpecifiedColor().Equals(ExpectedDefaultColor, 0.0001f));
	TestTrue(TEXT("Rich text default style preserves preconfigured shadow color"),
		DefaultStyle.ShadowColorAndOpacity.Equals(PreconfiguredStyle.ShadowColorAndOpacity, 0.0001f));
	TestTrue(TEXT("Rich text default style preserves preconfigured shadow offset"),
		DefaultStyle.ShadowOffset == PreconfiguredStyle.ShadowOffset);

	UDataTable* StyleSet = Widget->GetTextStyleSet();
	TestTrue(TEXT("Rich text style set created"), StyleSet != nullptr);
	if (!StyleSet)
	{
		return false;
	}
	TestTrue(TEXT("Rich text generated style set is widget-owned"), StyleSet->GetOuter() == Widget);
	TestFalse(TEXT("Rich text generated style set is not transient"), StyleSet->HasAnyFlags(RF_Transient));

	const FRichTextStyleRow* BaseRow = StyleSet->FindRow<FRichTextStyleRow>(TEXT("d2u_run_0"), TEXT("RichTextBlockStyleRowsApplication"));
	TestNotNull(TEXT("Base row exists"), BaseRow);
	if (!BaseRow)
	{
		return false;
	}
	TestEqual(TEXT("Sparse base row inherits default font size"), BaseRow->TextStyle.Font.Size, 18.0f);
	TestTrue(TEXT("Sparse base row preserves base shadow color"),
		BaseRow->TextStyle.ShadowColorAndOpacity.Equals(PreconfiguredStyle.ShadowColorAndOpacity, 0.0001f));

	const FRichTextStyleRow* LinkRow = StyleSet->FindRow<FRichTextStyleRow>(TEXT("d2u_run_1"), TEXT("RichTextBlockStyleRowsApplication"));
	TestNotNull(TEXT("Link row exists"), LinkRow);
	if (!LinkRow)
	{
		return false;
	}

	TestEqual(TEXT("Link row font size applied"), LinkRow->TextStyle.Font.Size, 22.0f);
	TestTrue(TEXT("Link row underline brush created"), LinkRow->TextStyle.UnderlineBrush.DrawAs != ESlateBrushDrawType::NoDrawType);
	TestTrue(TEXT("Link row color applied"),
		LinkRow->TextStyle.ColorAndOpacity.GetSpecifiedColor().Equals(
			FUMGValueParser::ParseColor(TEXT("#224466FF")),
			0.0001f));
	TestTrue(TEXT("Link row preserves base shadow color"),
		LinkRow->TextStyle.ShadowColorAndOpacity.Equals(PreconfiguredStyle.ShadowColorAndOpacity, 0.0001f));
	TestTrue(TEXT("Link row preserves base shadow offset"),
		LinkRow->TextStyle.ShadowOffset == PreconfiguredStyle.ShadowOffset);
	return true;
}

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
	Node.WidgetState.bHasActiveWidgetIndex = true;

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
	SourceData.RootWidget.WidgetState.bHasActiveWidgetIndex = true;

	FUMGWidgetNode FirstPage;
	FirstPage.Type = TEXT("TextBlock");
	FirstPage.Id = TEXT("page_a");
	FirstPage.Name = TEXT("PageA");
	FirstPage.Text.Text = TEXT("A");
	FirstPage.Text.bHasText = true;

	FUMGWidgetNode SecondPage;
	SecondPage.Type = TEXT("TextBlock");
	SecondPage.Id = TEXT("page_b");
	SecondPage.Name = TEXT("PageB");
	SecondPage.Text.Text = TEXT("B");
	SecondPage.Text.bHasText = true;

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

	TestTrue(TEXT("Parsed widget state keeps typed active index"), ParsedData.RootWidget.WidgetState.bHasActiveWidgetIndex);
	TestEqual(TEXT("Parsed widget state active index preserved"), ParsedData.RootWidget.WidgetState.ActiveWidgetIndex, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTypedPropertyBagEnumeratesLateTypedKeysTest,
	"Design2UMG.WidgetNodeAccess.TypedPropertyBagEnumeratesLateTypedKeys",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTypedPropertyBagEnumeratesLateTypedKeysTest::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Node;
	Node.WidgetState.ActiveWidgetIndex = 2;
	Node.WidgetState.bHasActiveWidgetIndex = true;
	Node.Render.RenderTranslation = FVector2D(1.5f, -2.0f);
	Node.Render.bHasRenderTranslation = true;

	const FUMGPropertyBag TypedProperties = Design2UMGWidgetNodeAccess::BuildTypedPropertyBag(Node);

	int32 ActiveWidgetIndex = INDEX_NONE;
	TestTrue(TEXT("Typed property bag keeps active widget index"), TypedProperties.TryGetInt(UMGPropertyKeys::ActiveWidgetIndex, ActiveWidgetIndex));
	TestEqual(TEXT("Typed property bag active widget index value"), ActiveWidgetIndex, 2);

	FVector2D RenderTranslation = FVector2D::ZeroVector;
	TestTrue(TEXT("Typed property bag keeps render translation"), TypedProperties.TryGetVector2D(UMGPropertyKeys::RenderTranslation, RenderTranslation));
	TestTrue(TEXT("Typed property bag render translation value"), RenderTranslation.Equals(FVector2D(1.5f, -2.0f), 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTypedPropertyBagEnumeratesDescriptorBackedBrushKeysTest,
	"Design2UMG.WidgetNodeAccess.TypedPropertyBagEnumeratesDescriptorBackedBrushKeys",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTypedPropertyBagEnumeratesDescriptorBackedBrushKeysTest::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Node;
	Node.Brush.Image.Mode = TEXT("Crop");
	Node.Brush.Image.bHasMode = true;
	Node.Brush.Image.Transform.A = 1.0f;
	Node.Brush.Image.Transform.B = 0.25f;
	Node.Brush.Image.Transform.TX = 0.1f;
	Node.Brush.Image.Transform.C = -0.05f;
	Node.Brush.Image.Transform.D = 1.0f;
	Node.Brush.Image.Transform.TY = 0.2f;
	Node.Brush.Image.bHasTransform = true;
	Node.Brush.Gradient.Type = TEXT("Angular");
	Node.Brush.Gradient.bHasType = true;
	Node.Brush.Gradient.Rotation = 45.0f;
	Node.Brush.Gradient.bHasRotation = true;
	Node.Brush.Gradient.Stops = {
		{ FLinearColor::Red, 0.0f },
		{ FLinearColor::Blue, 1.0f },
	};

	const FUMGPropertyBag TypedProperties = Design2UMGWidgetNodeAccess::BuildTypedPropertyBag(Node);

	FString ImageMode;
	TestTrue(TEXT("Typed property bag keeps image mode"), TypedProperties.TryGetString(UMGPropertyKeys::ImageMode, ImageMode));
	TestEqual(TEXT("Typed property bag image mode value"), ImageMode, FString(TEXT("Crop")));

	FString ImageTransform;
	TestTrue(TEXT("Typed property bag keeps image transform"), TypedProperties.TryGetString(UMGPropertyKeys::ImageTransform, ImageTransform));
	TestEqual(
		TEXT("Typed property bag image transform value"),
		ImageTransform,
		Design2UMGWidgetNodeAccess::SerializeImageTransform(Node.Brush.Image.Transform));

	FString MaterialRef;
	TestTrue(TEXT("Typed property bag keeps gradient material ref"), TypedProperties.TryGetString(UMGPropertyKeys::MaterialRef, MaterialRef));
	TestEqual(TEXT("Typed property bag gradient material ref value"), MaterialRef, FString(TEXT("angulargradient")));

	FString MaterialParams;
	TestTrue(TEXT("Typed property bag keeps gradient material params"), TypedProperties.TryGetString(UMGPropertyKeys::MaterialParams, MaterialParams));
	TestTrue(TEXT("Typed property bag gradient material params carry rotation"), MaterialParams.Contains(TEXT("\"Rotation\":45.0000")));
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
	ImageNode.Brush.Image.bHasTexturePath = true;
	ImageNode.Brush.Image.Mode = TEXT("Crop");
	ImageNode.Brush.Image.bHasMode = true;
	ImageNode.Brush.Image.Transform.A = 1.0f;
	ImageNode.Brush.Image.Transform.B = 0.0f;
	ImageNode.Brush.Image.Transform.TX = 0.125f;
	ImageNode.Brush.Image.Transform.C = 0.0f;
	ImageNode.Brush.Image.Transform.D = 1.0f;
	ImageNode.Brush.Image.Transform.TY = -0.25f;
	ImageNode.Brush.Image.bHasTransform = true;

	FUMGWidgetNode GradientNode;
	GradientNode.Type = TEXT("Image");
	GradientNode.Id = TEXT("typed_gradient");
	GradientNode.Name = TEXT("TypedGradient");
	GradientNode.Brush.Gradient.Type = TEXT("Angular");
	GradientNode.Brush.Gradient.bHasType = true;
	GradientNode.Brush.Gradient.Rotation = 135.0f;
	GradientNode.Brush.Gradient.bHasRotation = true;
	GradientNode.Brush.Gradient.Center = FVector2D(0.45f, 0.55f);
	GradientNode.Brush.Gradient.bHasCenter = true;
	GradientNode.Brush.Gradient.Size = FVector2D(0.8f, 1.1f);
	GradientNode.Brush.Gradient.bHasSize = true;
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

	TestTrue(TEXT("Typed image texture path preserved"), ParsedImageNode->Brush.Image.bHasTexturePath && ParsedImageNode->Brush.Image.TexturePath == TEXT("/Game/UI/T_TypedImage.T_TypedImage"));
	TestTrue(TEXT("Typed image mode preserved"), ParsedImageNode->Brush.Image.bHasMode && ParsedImageNode->Brush.Image.Mode == TEXT("Crop"));
	TestTrue(TEXT("Typed image transform preserved"), ParsedImageNode->Brush.Image.bHasTransform && FMath::IsNearlyEqual(ParsedImageNode->Brush.Image.Transform.TX, 0.125f, 0.0001f));
	TestEqual(TEXT("Typed image helper still exposes texture path"), GetWidgetProperty(*ParsedImageNode, TEXT("TexturePath")), TEXT("/Game/UI/T_TypedImage.T_TypedImage"));

	TestTrue(TEXT("Typed gradient type preserved"), ParsedGradientNode->Brush.Gradient.bHasType && ParsedGradientNode->Brush.Gradient.Type == TEXT("Angular"));
	TestTrue(TEXT("Typed gradient rotation preserved"), ParsedGradientNode->Brush.Gradient.bHasRotation && FMath::IsNearlyEqual(ParsedGradientNode->Brush.Gradient.Rotation, 135.0f, 0.001f));
	TestTrue(TEXT("Typed gradient stop count preserved"), ParsedGradientNode->Brush.Gradient.Stops.Num() == 3);
	TestEqual(TEXT("Typed gradient helper exposes material ref"), GetWidgetProperty(*ParsedGradientNode, TEXT("MaterialRef")), TEXT("angulargradient"));
	TestTrue(TEXT("Typed gradient helper exposes material params"), GetWidgetProperty(*ParsedGradientNode, TEXT("MaterialParams")).Contains(TEXT("\"Stops\"")));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInnerShadowEffectJsonRoundTripTest,
	"Design2UMG.UMGJson.InnerShadowEffectRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FInnerShadowEffectJsonRoundTripTest::RunTest(const FString& Parameters)
{
	FUMGWidgetData SourceData;
	SourceData.WidgetName = TEXT("WBP_InnerShadowEffectRoundTrip");
	SourceData.RootWidget.Type = TEXT("RetainerBox");
	SourceData.RootWidget.Id = TEXT("inner_shadow_root");
	SourceData.RootWidget.Name = TEXT("InnerShadowRoot");
	SourceData.RootWidget.Effect.bRetainRendering = true;
	SourceData.RootWidget.Effect.bHasRetainRendering = true;
	SourceData.RootWidget.Effect.EffectMaterial = TEXT("d2u.retainer.inner_shadow");
	SourceData.RootWidget.Effect.TextureParameter = TEXT("Texture");
	SourceData.RootWidget.Effect.EffectColor = FLinearColor(0.1f, 0.2f, 0.3f, 0.4f);
	SourceData.RootWidget.Effect.bHasEffectColor = true;
	SourceData.RootWidget.Effect.EffectOffset = FVector2D(0.05f, -0.03f);
	SourceData.RootWidget.Effect.bHasEffectOffset = true;
	SourceData.RootWidget.Effect.EffectBlurUVStep = FVector2D(0.01f, 0.02f);
	SourceData.RootWidget.Effect.bHasEffectBlurUVStep = true;

	const FString Json = FUMGJsonSerializer::Serialize(SourceData);
	TestFalse(TEXT("Serialized inner shadow emits Effect payload"), Json.IsEmpty() || !Json.Contains(TEXT("\"Effect\"")));
	TestTrue(TEXT("Serialized inner shadow emits effect color"), Json.Contains(TEXT("\"EffectColor\"")));
	TestTrue(TEXT("Serialized inner shadow emits effect offset"), Json.Contains(TEXT("\"EffectOffset\"")));

	FUMGWidgetData ParsedData;
	FString ParseError;
	TestTrue(TEXT("Inner shadow effect JSON parses"), FUMGJsonParser::ParseJson(Json, ParsedData, &ParseError));
	if (!ParseError.IsEmpty())
	{
		AddInfo(ParseError);
	}

	TestEqual(TEXT("Parsed inner shadow effect material preserved"), ParsedData.RootWidget.Effect.EffectMaterial, TEXT("d2u.retainer.inner_shadow"));
	TestTrue(TEXT("Parsed inner shadow color preserved"), ParsedData.RootWidget.Effect.bHasEffectColor);
	TestTrue(TEXT("Parsed inner shadow offset preserved"), ParsedData.RootWidget.Effect.bHasEffectOffset);
	TestTrue(TEXT("Parsed inner shadow blur step preserved"), ParsedData.RootWidget.Effect.bHasEffectBlurUVStep);
	TestTrue(TEXT("Parsed inner shadow color alpha preserved"), FMath::IsNearlyEqual(ParsedData.RootWidget.Effect.EffectColor.A, 0.4f, 0.0001f));
	TestTrue(TEXT("Parsed inner shadow offset X preserved"), FMath::IsNearlyEqual(ParsedData.RootWidget.Effect.EffectOffset.X, 0.05f, 0.0001f));
	TestTrue(TEXT("Parsed inner shadow offset Y preserved"), FMath::IsNearlyEqual(ParsedData.RootWidget.Effect.EffectOffset.Y, -0.03f, 0.0001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRadialGradientMultiStopFidelityTest,
	"Design2UMG.Gradient.RadialMultiStopFidelity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRadialGradientMultiStopFidelityTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"children": [
			{
				"id": "root",
				"type": "frame",
				"name": "Root",
				"width": 64,
				"height": 64,
				"children": [
					{
						"id": "grad_card",
						"type": "rectangle",
						"name": "Gradient Card",
						"width": 64,
						"height": 64,
						"fill": {
							"type": "gradient",
							"gradientType": "radial",
							"colors": [
								{ "color": "#FF0000FF", "position": 0.0 },
								{ "color": "#00FF00FF", "position": 0.5 },
								{ "color": "#0000FFFF", "position": 1.0 }
							]
						}
					}
				]
			}
		]
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Radial multi-stop gradient fixture parses"), FPenParser::ParseDocument(Json, Doc));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	const FUMGWidgetNode* Widget = FindWidgetById(Data.RootWidget, TEXT("grad_card"));
	TestTrue(TEXT("Radial gradient widget exists"), Widget != nullptr);
	if (!Widget)
	{
		return false;
	}

	TestEqual(TEXT("Radial gradient material ref preserved"), GetWidgetProperty(*Widget, UMGPropertyKeys::MaterialRef), TEXT("radialgradient"));
	const FString MaterialParams = GetWidgetProperty(*Widget, UMGPropertyKeys::MaterialParams);
	TestTrue(TEXT("Radial gradient exports Stops array"), MaterialParams.Contains(TEXT("\"Stops\"")));
	TestFalse(TEXT("Radial gradient no longer falls back to center/edge pair"), MaterialParams.Contains(TEXT("ColorCenter")));

	UTexture2D* Texture = FProceduralShapeGenerator::GenerateFromMaterialRef(
		TEXT("radialgradient"),
		Design2UMGWidgetNodeAccess::BuildTypedPropertyBag(*Widget),
		GetTransientPackageAsObject());
	TestTrue(TEXT("Radial gradient texture generated"), Texture != nullptr);
	if (!Texture)
	{
		return false;
	}

	FColor CenterPixel;
	FColor MidPixel;
	FColor EdgePixel;
	TestTrue(TEXT("Radial gradient center pixel readable"), ReadTexturePixel(Texture, 32, 32, CenterPixel));
	TestTrue(TEXT("Radial gradient mid pixel readable"), ReadTexturePixel(Texture, 32, 16, MidPixel));
	TestTrue(TEXT("Radial gradient edge pixel readable"), ReadTexturePixel(Texture, 0, 32, EdgePixel));

	TestTrue(TEXT("Radial gradient center keeps first stop red"), CenterPixel.R > 200 && CenterPixel.G < 80 && CenterPixel.B < 80);
	TestTrue(TEXT("Radial gradient midpoint reaches middle stop green"), MidPixel.G > MidPixel.R && MidPixel.G > MidPixel.B);
	TestTrue(TEXT("Radial gradient edge reaches final stop blue"), EdgePixel.B > 200 && EdgePixel.R < 80 && EdgePixel.G < 80);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRadialGradientTransformFidelityTest,
	"Design2UMG.Gradient.RadialTransformFidelity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRadialGradientTransformFidelityTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"children": [
			{
				"id": "root",
				"type": "frame",
				"name": "Root",
				"width": 64,
				"height": 64,
				"children": [
					{
						"id": "grad_transform",
						"type": "rectangle",
						"name": "Gradient Transform",
						"width": 64,
						"height": 64,
						"fill": {
							"type": "gradient",
							"gradientType": "radial",
							"rotation": 90,
							"center": {"x": 0.25, "y": 0.75},
							"size": {"width": 0.25, "height": 1.0},
							"colors": [
								{ "color": "#FF0000FF", "position": 0.0 },
								{ "color": "#0000FFFF", "position": 1.0 }
							]
						}
					}
				]
			}
		]
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Radial transform fixture parses"), FPenParser::ParseDocument(Json, Doc));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	const FUMGWidgetNode* Widget = FindWidgetById(Data.RootWidget, TEXT("grad_transform"));
	TestTrue(TEXT("Radial transform widget exists"), Widget != nullptr);
	if (!Widget)
	{
		return false;
	}

	const FString MaterialParams = GetWidgetProperty(*Widget, UMGPropertyKeys::MaterialParams);
	TestEqual(TEXT("Radial transform material ref preserved"), GetWidgetProperty(*Widget, UMGPropertyKeys::MaterialRef), TEXT("radialgradient"));
	TestTrue(TEXT("Radial transform exports center X"), MaterialParams.Contains(TEXT("\"CenterX\":0.2500")));
	TestTrue(TEXT("Radial transform exports center Y"), MaterialParams.Contains(TEXT("\"CenterY\":0.7500")));
	TestTrue(TEXT("Radial transform exports size width"), MaterialParams.Contains(TEXT("\"SizeWidth\":0.2500")));
	TestTrue(TEXT("Radial transform exports size height"), MaterialParams.Contains(TEXT("\"SizeHeight\":1.0000")));
	TestTrue(TEXT("Radial transform exports rotation"), MaterialParams.Contains(TEXT("\"Rotation\":90.0000")));

	UTexture2D* Texture = FProceduralShapeGenerator::GenerateFromMaterialRef(
		TEXT("radialgradient"),
		Design2UMGWidgetNodeAccess::BuildTypedPropertyBag(*Widget),
		GetTransientPackageAsObject());
	TestTrue(TEXT("Radial transform texture generated"), Texture != nullptr);
	if (!Texture)
	{
		return false;
	}

	FColor ShiftedCenterPixel;
	FColor RotatedMajorAxisPixel;
	FColor RotatedMinorAxisPixel;
	FColor TextureCenterPixel;
	TestTrue(TEXT("Radial transform shifted center pixel readable"), ReadTexturePixel(Texture, 16, 47, ShiftedCenterPixel));
	TestTrue(TEXT("Radial transform major-axis pixel readable"), ReadTexturePixel(Texture, 27, 47, RotatedMajorAxisPixel));
	TestTrue(TEXT("Radial transform minor-axis pixel readable"), ReadTexturePixel(Texture, 16, 36, RotatedMinorAxisPixel));
	TestTrue(TEXT("Radial transform texture-center pixel readable"), ReadTexturePixel(Texture, 32, 32, TextureCenterPixel));

	TestTrue(TEXT("Shifted center keeps red core"), ShiftedCenterPixel.R > 200 && ShiftedCenterPixel.B < 80);
	TestTrue(TEXT("Rotated major axis stays inside elongated ellipse"), RotatedMajorAxisPixel.R > RotatedMajorAxisPixel.B);
	TestTrue(TEXT("Rotated minor axis reaches edge sooner"), RotatedMinorAxisPixel.B > 200 && RotatedMinorAxisPixel.R < 80);
	TestTrue(TEXT("Default center no longer acts as gradient center"), TextureCenterPixel.B > 180 && TextureCenterPixel.R < 120);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaRadialGradientHandleFidelityTest,
	"Design2UMG.FigmaParser.RadialGradientHandleFidelity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaRadialGradientHandleFidelityTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
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
					"absoluteBoundingBox": {"x":0,"y":0,"width":64,"height":64},
					"children": [{
						"id": "grad_figma",
						"name": "Figma Radial",
						"type": "RECTANGLE",
						"visible": true,
						"absoluteBoundingBox": {"x":0,"y":0,"width":64,"height":64},
						"fills": [{
							"type": "GRADIENT_RADIAL",
							"visible": true,
							"gradientHandlePositions": [
								{"x":0.25,"y":0.75},
								{"x":0.25,"y":0.875},
								{"x":0.75,"y":0.75}
							],
							"gradientStops": [
								{"position":0.0,"color":{"r":1.0,"g":0.0,"b":0.0,"a":1.0}},
								{"position":1.0,"color":{"r":0.0,"g":0.0,"b":1.0,"a":1.0}}
							]
						}]
					}]
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Figma radial handle fixture parses"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() != 1 || Doc.Children[0]->Children.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& GradientNode = Doc.Children[0]->Children[0];
	TestEqual(TEXT("Figma radial gradient type preserved"), GradientNode->Visual.Fill.Gradient.GradientType, EDesignGradientType::Radial);
	TestTrue(TEXT("Figma radial center parsed"), GradientNode->Visual.Fill.Gradient.bHasCenter
		&& FMath::IsNearlyEqual(GradientNode->Visual.Fill.Gradient.CenterX, 0.25f, 0.001f)
		&& FMath::IsNearlyEqual(GradientNode->Visual.Fill.Gradient.CenterY, 0.75f, 0.001f));
	TestTrue(TEXT("Figma radial size parsed"), GradientNode->Visual.Fill.Gradient.bHasSize
		&& FMath::IsNearlyEqual(GradientNode->Visual.Fill.Gradient.SizeWidth, 0.25f, 0.001f)
		&& FMath::IsNearlyEqual(GradientNode->Visual.Fill.Gradient.SizeHeight, 1.0f, 0.001f));
	TestTrue(TEXT("Figma radial rotation parsed"), FMath::IsNearlyEqual(GradientNode->Visual.Fill.Gradient.Rotation, 90.0f, 0.001f));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	const FUMGWidgetNode* Widget = FindWidgetById(Data.RootWidget, TEXT("grad_figma"));
	TestTrue(TEXT("Figma radial widget exists"), Widget != nullptr);
	if (!Widget)
	{
		return false;
	}

	const FString MaterialParams = GetWidgetProperty(*Widget, UMGPropertyKeys::MaterialParams);
	TestEqual(TEXT("Figma radial material ref preserved"), GetWidgetProperty(*Widget, UMGPropertyKeys::MaterialRef), TEXT("radialgradient"));
	TestTrue(TEXT("Figma radial exports center X"), MaterialParams.Contains(TEXT("\"CenterX\":0.2500")));
	TestTrue(TEXT("Figma radial exports center Y"), MaterialParams.Contains(TEXT("\"CenterY\":0.7500")));
	TestTrue(TEXT("Figma radial exports size width"), MaterialParams.Contains(TEXT("\"SizeWidth\":0.2500")));
	TestTrue(TEXT("Figma radial exports size height"), MaterialParams.Contains(TEXT("\"SizeHeight\":1.0000")));
	TestTrue(TEXT("Figma radial exports rotation"), MaterialParams.Contains(TEXT("\"Rotation\":90.0000")));

	UTexture2D* Texture = FProceduralShapeGenerator::GenerateFromMaterialRef(
		TEXT("radialgradient"),
		Design2UMGWidgetNodeAccess::BuildTypedPropertyBag(*Widget),
		GetTransientPackageAsObject());
	TestTrue(TEXT("Figma radial texture generated"), Texture != nullptr);
	if (!Texture)
	{
		return false;
	}

	FColor ShiftedCenterPixel;
	FColor RotatedMajorAxisPixel;
	FColor RotatedMinorAxisPixel;
	TestTrue(TEXT("Figma radial shifted center pixel readable"), ReadTexturePixel(Texture, 16, 47, ShiftedCenterPixel));
	TestTrue(TEXT("Figma radial major-axis pixel readable"), ReadTexturePixel(Texture, 27, 47, RotatedMajorAxisPixel));
	TestTrue(TEXT("Figma radial minor-axis pixel readable"), ReadTexturePixel(Texture, 16, 36, RotatedMinorAxisPixel));

	TestTrue(TEXT("Figma radial shifted center keeps red core"), ShiftedCenterPixel.R > 200 && ShiftedCenterPixel.B < 80);
	TestTrue(TEXT("Figma radial rotated major axis stays inside ellipse"), RotatedMajorAxisPixel.R > RotatedMajorAxisPixel.B);
	TestTrue(TEXT("Figma radial rotated minor axis reaches edge sooner"), RotatedMinorAxisPixel.B > 200 && RotatedMinorAxisPixel.R < 80);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxShadowBlurPaddingFidelityTest,
	"Design2UMG.Shadow.BoxShadowBlurPaddingFidelity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBoxShadowBlurPaddingFidelityTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"children": [
			{
				"id": "shadow_card",
				"type": "rectangle",
				"name": "Shadow Card",
				"width": 40,
				"height": 24,
				"fill": "#FFFFFFFF",
				"effect": {
					"type": "shadow",
					"shadowType": "outer",
					"color": "#000000FF",
					"offset": { "x": 0, "y": 0 },
					"blur": 8,
					"spread": 0
				}
			}
		]
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Box shadow blur-padding fixture parses"), FPenParser::ParseDocument(Json, Doc));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	const FUMGWidgetNode* Wrapper = FindWidgetById(Data.RootWidget, TEXT("shadow_card"));
	const FUMGWidgetNode* ShadowWidget = FindWidgetById(Data.RootWidget, TEXT("shadow_card_shadow0"));
	TestTrue(TEXT("Shadow wrapper exists"), Wrapper != nullptr);
	TestTrue(TEXT("Shadow image child exists"), ShadowWidget != nullptr);
	if (!Wrapper || !ShadowWidget)
	{
		return false;
	}

	TestEqual(TEXT("Box shadow wraps widget with overlay"), Wrapper->Type, TEXT("Overlay"));
	TestEqual(TEXT("Box shadow material ref preserved"), GetWidgetProperty(*ShadowWidget, UMGPropertyKeys::MaterialRef), TEXT("boxshadow"));
	TestTrue(TEXT("Box shadow width includes blur padding"),
		FMath::IsNearlyEqual(FCString::Atof(*GetWidgetProperty(*ShadowWidget, TEXT("WidthOverride"))), 56.0f, 0.001f));
	TestTrue(TEXT("Box shadow height includes blur padding"),
		FMath::IsNearlyEqual(FCString::Atof(*GetWidgetProperty(*ShadowWidget, TEXT("HeightOverride"))), 40.0f, 0.001f));

	UTexture2D* Texture = FProceduralShapeGenerator::GenerateFromMaterialRef(
		TEXT("boxshadow"),
		Design2UMGWidgetNodeAccess::BuildTypedPropertyBag(*ShadowWidget),
		GetTransientPackageAsObject());
	TestTrue(TEXT("Box shadow texture generated"), Texture != nullptr);
	if (!Texture)
	{
		return false;
	}

	FColor CornerPixel;
	FColor CenterPixel;
	TestTrue(TEXT("Box shadow corner pixel readable"), ReadTexturePixel(Texture, 0, 0, CornerPixel));
	TestTrue(TEXT("Box shadow center pixel readable"), ReadTexturePixel(Texture, 28, 20, CenterPixel));

	TestTrue(TEXT("Blur padding corner stays substantially softer than the core"), CornerPixel.A * 4 < CenterPixel.A);
	TestTrue(TEXT("Shadow core stays opaque"), CenterPixel.A > 200);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxShadowExplicitBlurPaddingMaterialParamTest,
	"Design2UMG.Shadow.ExplicitBlurPaddingMaterialParam",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBoxShadowExplicitBlurPaddingMaterialParamTest::RunTest(const FString& Parameters)
{
	FUMGPropertyBag LegacyShadowProperties;
	LegacyShadowProperties.Add(TEXT("WidthOverride"), TEXT("56"));
	LegacyShadowProperties.Add(TEXT("HeightOverride"), TEXT("40"));
	LegacyShadowProperties.Add(UMGPropertyKeys::ShadowBlur, TEXT("8"));
	LegacyShadowProperties.Add(TEXT("MaterialParams"), TEXT("{\"Color\":\"#000000FF\",\"CornerRadius\":0.0}"));

	FUMGPropertyBag ExplicitPaddingShadowProperties;
	ExplicitPaddingShadowProperties.Add(TEXT("WidthOverride"), TEXT("56"));
	ExplicitPaddingShadowProperties.Add(TEXT("HeightOverride"), TEXT("40"));
	ExplicitPaddingShadowProperties.Add(UMGPropertyKeys::ShadowBlur, TEXT("8"));
	ExplicitPaddingShadowProperties.Add(TEXT("MaterialParams"), TEXT("{\"Color\":\"#000000FF\",\"CornerRadius\":0.0,\"BlurPadding\":8.0}"));

	UTexture2D* LegacyTexture = FProceduralShapeGenerator::GenerateFromMaterialRef(
		TEXT("boxshadow"),
		LegacyShadowProperties,
		GetTransientPackageAsObject());
	UTexture2D* ExplicitPaddingTexture = FProceduralShapeGenerator::GenerateFromMaterialRef(
		TEXT("boxshadow"),
		ExplicitPaddingShadowProperties,
		GetTransientPackageAsObject());
	TestTrue(TEXT("Legacy box shadow texture generated"), LegacyTexture != nullptr);
	TestTrue(TEXT("Explicit blur-padding texture generated"), ExplicitPaddingTexture != nullptr);
	if (!LegacyTexture || !ExplicitPaddingTexture)
	{
		return false;
	}

	FColor LegacyCornerPixel;
	FColor ExplicitCornerPixel;
	FColor ExplicitCenterPixel;
	TestTrue(TEXT("Legacy corner pixel readable"), ReadTexturePixel(LegacyTexture, 0, 0, LegacyCornerPixel));
	TestTrue(TEXT("Explicit blur-padding corner pixel readable"), ReadTexturePixel(ExplicitPaddingTexture, 0, 0, ExplicitCornerPixel));
	TestTrue(TEXT("Explicit blur-padding center pixel readable"), ReadTexturePixel(ExplicitPaddingTexture, 28, 20, ExplicitCenterPixel));

	TestTrue(TEXT("Explicit blur-padding softens the padded corner"), ExplicitCornerPixel.A < LegacyCornerPixel.A);
	TestTrue(TEXT("Explicit blur-padding keeps the shadow core visible"), ExplicitCenterPixel.A > 150);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoxShadowNegativeSpreadFidelityTest,
	"Design2UMG.Shadow.NegativeSpreadFidelity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBoxShadowNegativeSpreadFidelityTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"children": [
			{
				"id": "shadow_chip",
				"type": "rectangle",
				"name": "Shadow Chip",
				"width": 40,
				"height": 40,
				"fill": "#FFFFFFFF",
				"effect": {
					"type": "shadow",
					"shadowType": "outer",
					"color": "#000000FF",
					"offset": { "x": 0, "y": 0 },
					"blur": 0,
					"spread": -6
				}
			}
		]
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Negative spread shadow fixture parses"), FPenParser::ParseDocument(Json, Doc));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	const FUMGWidgetNode* ShadowWidget = FindWidgetById(Data.RootWidget, TEXT("shadow_chip_shadow0"));
	TestTrue(TEXT("Negative spread shadow image child exists"), ShadowWidget != nullptr);
	if (!ShadowWidget)
	{
		return false;
	}

	TestEqual(TEXT("Negative spread keeps rounded rect path when blur is zero"), GetWidgetProperty(*ShadowWidget, UMGPropertyKeys::MaterialRef), TEXT("roundedrect"));
	TestTrue(TEXT("Negative spread shrinks width"),
		FMath::IsNearlyEqual(FCString::Atof(*GetWidgetProperty(*ShadowWidget, TEXT("WidthOverride"))), 28.0f, 0.001f));
	TestTrue(TEXT("Negative spread shrinks height"),
		FMath::IsNearlyEqual(FCString::Atof(*GetWidgetProperty(*ShadowWidget, TEXT("HeightOverride"))), 28.0f, 0.001f));
	FVector2D NegativeSpreadTranslation = FVector2D::ZeroVector;
	TestTrue(TEXT("Negative spread exposes render translation"),
		Design2UMGWidgetNodeAccess::BuildTypedPropertyBag(*ShadowWidget).TryGetVector2D(TEXT("RenderTranslation"), NegativeSpreadTranslation));
	TestTrue(TEXT("Negative spread translates back inward"), NegativeSpreadTranslation.Equals(FVector2D(6.0f, 6.0f), 0.001f));

	UTexture2D* Texture = FProceduralShapeGenerator::GenerateFromMaterialRef(
		TEXT("roundedrect"),
		Design2UMGWidgetNodeAccess::BuildTypedPropertyBag(*ShadowWidget),
		GetTransientPackageAsObject());
	TestTrue(TEXT("Negative spread texture generated"), Texture != nullptr);
	if (!Texture)
	{
		return false;
	}

	TestEqual(TEXT("Negative spread opaque pixel area matches shrunken rect"), CountOpaqueTexturePixels(Texture), 28 * 28);
	return true;
}
