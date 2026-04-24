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

	FUMGPropertyBag TypedProperties = Design2UMGWidgetNodeAccess::BuildTypedPropertyBag(Node);
	FLinearColor TypedEffectColor = FLinearColor::Transparent;
	const FLinearColor SerializedEffectColor =
		FUMGValueParser::ParseColor(Design2UMGWidgetNodeAccess::SerializeWidgetColor(Node.Effect.EffectColor));
	TestTrue(TEXT("Typed retainer payload keeps EffectColor without serialization drift"),
		TypedProperties.TryGetColor(UMGPropertyKeys::EffectColor, TypedEffectColor)
		&& TypedEffectColor.Equals(SerializedEffectColor, 0.0001f));

	URetainerBox* PayloadWidget = NewObject<URetainerBox>(GetTransientPackageAsObject());
	TestTrue(TEXT("Typed payload RetainerBox created"), PayloadWidget != nullptr);
	if (!PayloadWidget)
	{
		return false;
	}

	FUMGWidgetUtils::ApplyWidgetProperties(PayloadWidget, TypedProperties);
	UMaterialInstanceDynamic* PayloadEffect = PayloadWidget->GetEffectMaterial();
	TestTrue(TEXT("Typed payload dynamic effect material created"), PayloadEffect != nullptr);
	if (!PayloadEffect)
	{
		return false;
	}

	const FLinearColor PayloadInnerShadowColor = PayloadEffect->K2_GetVectorParameterValue(TEXT("InnerShadowColor"));
	TestTrue(TEXT("Typed payload inner shadow color follows serialized payload color"),
		PayloadInnerShadowColor.Equals(SerializedEffectColor, 0.0001f));

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextBlockUsesTypedTextPayloadParityTest,
	"Design2UMG.PropertyApplier.TextBlockUsesTypedTextPayload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextBlockUsesTypedTextPayloadParityTest::RunTest(const FString& Parameters)
{
	UTextBlock* TypedWidget = NewObject<UTextBlock>(GetTransientPackageAsObject());
	UTextBlock* LegacyWidget = NewObject<UTextBlock>(GetTransientPackageAsObject());
	TestTrue(TEXT("Typed TextBlock created"), TypedWidget != nullptr);
	TestTrue(TEXT("Legacy TextBlock created"), LegacyWidget != nullptr);
	if (!TypedWidget || !LegacyWidget)
	{
		return false;
	}

	FUMGWidgetNode Node;
	Node.Type = TEXT("TextBlock");
	Node.Text.Text = TEXT("Glyph label");
	Node.Text.bHasText = true;
	Node.Text.FontFamily = TEXT("Geist");
	Node.Text.bHasFontFamily = true;
	Node.Text.TypefaceFontName = TEXT("Regular");
	Node.Text.bHasTypefaceFontName = true;
	Node.Text.IconName = TEXT("glyph_download");
	Node.Text.bHasIconName = true;
	Node.Text.Justification = TEXT("Center");
	Node.Text.bHasJustification = true;
	Node.Text.bAutoWrapText = true;
	Node.Text.bHasAutoWrapText = true;
	Node.Text.WrapTextAt = 144.0f;
	Node.Text.bHasWrapTextAt = true;
	Node.Text.LineHeightPercentage = 1.35f;
	Node.Text.bHasLineHeightPercentage = true;
	Node.Text.Color = FLinearColor(0.2f, 0.4f, 0.6f, 1.0f);
	Node.Text.bHasColor = true;
	Node.TextStyle.FontSize = 18.0f;
	Node.TextStyle.SourceFontSize = 18.0f;
	Node.TextStyle.LetterSpacing = 1.5f;
	Node.TextStyle.bHasLetterSpacing = true;
	Node.WidgetStyle.Padding = FMargin(7.0f, 8.0f, 9.0f, 10.0f);
	Node.WidgetStyle.ToolTipText = TEXT("Typed tooltip");
	Node.Shadow.ShadowOffset = FVector2D(2.0f, 3.0f);
	Node.Shadow.bHasShadowOffset = true;
	Node.Shadow.ShadowColorAndOpacity = FLinearColor(0.9f, 0.1f, 0.2f, 0.5f);
	Node.Shadow.bHasShadowColorAndOpacity = true;

	const FUMGPropertyBag TypedProperties = Design2UMGWidgetNodeAccess::BuildTypedPropertyBag(Node);
	const FLinearColor SerializedNodeColor =
		FUMGValueParser::ParseColor(Design2UMGWidgetNodeAccess::SerializeWidgetColor(Node.Text.Color));
	FUMGWidgetUtils::ApplyWidgetProperties(TypedWidget, Node);
	FUMGWidgetUtils::ApplyWidgetProperties(LegacyWidget, TypedProperties);

	const FStructProperty* MarginProperty = FindFProperty<FStructProperty>(UTextLayoutWidget::StaticClass(), TEXT("Margin"));
	const FFloatProperty* LineHeightProperty = FindFProperty<FFloatProperty>(UTextLayoutWidget::StaticClass(), TEXT("LineHeightPercentage"));
	const FByteProperty* JustificationProperty = FindFProperty<FByteProperty>(UTextLayoutWidget::StaticClass(), TEXT("Justification"));
	const FStructProperty* ShapedTextOptionsProperty = FindFProperty<FStructProperty>(UTextLayoutWidget::StaticClass(), TEXT("ShapedTextOptions"));
	TestNotNull(TEXT("Text layout margin property exists"), MarginProperty);
	TestNotNull(TEXT("Text layout line-height property exists"), LineHeightProperty);
	TestNotNull(TEXT("Text layout justification property exists"), JustificationProperty);
	TestNotNull(TEXT("Text layout shaped-text property exists"), ShapedTextOptionsProperty);
	if (!MarginProperty || !LineHeightProperty || !JustificationProperty || !ShapedTextOptionsProperty)
	{
		return false;
	}

	const FMargin* TypedMargin = MarginProperty->ContainerPtrToValuePtr<FMargin>(TypedWidget);
	const FMargin* LegacyMargin = MarginProperty->ContainerPtrToValuePtr<FMargin>(LegacyWidget);
	const float TypedLineHeight = LineHeightProperty->GetPropertyValue_InContainer(TypedWidget);
	const float LegacyLineHeight = LineHeightProperty->GetPropertyValue_InContainer(LegacyWidget);
	const ETextJustify::Type TypedJustification = static_cast<ETextJustify::Type>(
		JustificationProperty->GetUnsignedIntPropertyValue(JustificationProperty->ContainerPtrToValuePtr<void>(TypedWidget)));
	const ETextJustify::Type LegacyJustification = static_cast<ETextJustify::Type>(
		JustificationProperty->GetUnsignedIntPropertyValue(JustificationProperty->ContainerPtrToValuePtr<void>(LegacyWidget)));
	const FShapedTextOptions* TypedShaping = ShapedTextOptionsProperty->ContainerPtrToValuePtr<FShapedTextOptions>(TypedWidget);
	const FShapedTextOptions* LegacyShaping = ShapedTextOptionsProperty->ContainerPtrToValuePtr<FShapedTextOptions>(LegacyWidget);

	TestEqual(TEXT("Typed node path keeps tooltip"), TypedWidget->GetToolTipText().ToString(), TEXT("Typed tooltip"));
	TestEqual(TEXT("Typed and legacy text match"), TypedWidget->GetText().ToString(), LegacyWidget->GetText().ToString());
	TestTrue(TEXT("Typed text block color follows typed node color directly"),
		TypedWidget->GetColorAndOpacity().GetSpecifiedColor().Equals(Node.Text.Color, 0.0001f));
	TestTrue(TEXT("Legacy text block color follows serialized payload color"),
		LegacyWidget->GetColorAndOpacity().GetSpecifiedColor().Equals(SerializedNodeColor, 0.0001f));
	TestEqual(TEXT("Typed and legacy font size match"), TypedWidget->GetFont().Size, LegacyWidget->GetFont().Size);
	TestEqual(TEXT("Typed and legacy typeface match"), TypedWidget->GetFont().TypefaceFontName, LegacyWidget->GetFont().TypefaceFontName);
	TestEqual(TEXT("Typed and legacy letter spacing match"), TypedWidget->GetFont().LetterSpacing, LegacyWidget->GetFont().LetterSpacing);
	TestEqual(TEXT("Typed and legacy font object path match"),
		TypedWidget->GetFont().FontObject ? TypedWidget->GetFont().FontObject->GetPathName() : FString(),
		LegacyWidget->GetFont().FontObject ? LegacyWidget->GetFont().FontObject->GetPathName() : FString());
	TestEqual(TEXT("Typed and legacy justification match"), TypedJustification, LegacyJustification);
	TestTrue(TEXT("Typed and legacy auto-wrap match"), TypedWidget->GetAutoWrapText() == LegacyWidget->GetAutoWrapText());
	TestTrue(TEXT("Typed and legacy wrap width match"), FMath::IsNearlyEqual(TypedWidget->GetWrapTextAt(), LegacyWidget->GetWrapTextAt(), 0.001f));
	TestTrue(TEXT("Typed and legacy line height match"), FMath::IsNearlyEqual(TypedLineHeight, LegacyLineHeight, 0.001f));
	TestTrue(TEXT("Typed and legacy margin match"),
		TypedMargin
		&& LegacyMargin
		&& FMath::IsNearlyEqual(TypedMargin->Left, LegacyMargin->Left, 0.001f)
		&& FMath::IsNearlyEqual(TypedMargin->Top, LegacyMargin->Top, 0.001f)
		&& FMath::IsNearlyEqual(TypedMargin->Right, LegacyMargin->Right, 0.001f)
		&& FMath::IsNearlyEqual(TypedMargin->Bottom, LegacyMargin->Bottom, 0.001f));
	TestTrue(TEXT("Typed and legacy shadow offset match"), TypedWidget->GetShadowOffset().Equals(LegacyWidget->GetShadowOffset(), 0.001f));
	TestTrue(TEXT("Typed and legacy shadow color match"),
		TypedWidget->GetShadowColorAndOpacity().Equals(LegacyWidget->GetShadowColorAndOpacity(), 0.0001f));
	TestTrue(TEXT("Typed and legacy icon shaping match"),
		TypedShaping
		&& LegacyShaping
		&& TypedShaping->bOverride_TextShapingMethod == LegacyShaping->bOverride_TextShapingMethod
		&& TypedShaping->TextShapingMethod == LegacyShaping->TextShapingMethod);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditableTextFamilyUsesTypedTextPayloadParityTest,
	"Design2UMG.PropertyApplier.EditableTextFamilyUsesTypedTextPayload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FEditableTextFamilyUsesTypedTextPayloadParityTest::RunTest(const FString& Parameters)
{
	const FBoolProperty* EditableTextBoxReadOnlyProperty = FindFProperty<FBoolProperty>(UEditableTextBox::StaticClass(), TEXT("IsReadOnly"));
	const FBoolProperty* EditableTextBoxPasswordProperty = FindFProperty<FBoolProperty>(UEditableTextBox::StaticClass(), TEXT("IsPassword"));
	const FFloatProperty* EditableTextBoxMinimumDesiredWidthProperty = FindFProperty<FFloatProperty>(UEditableTextBox::StaticClass(), TEXT("MinimumDesiredWidth"));
	const FBoolProperty* EditableTextReadOnlyProperty = FindFProperty<FBoolProperty>(UEditableText::StaticClass(), TEXT("IsReadOnly"));
	const FBoolProperty* EditableTextPasswordProperty = FindFProperty<FBoolProperty>(UEditableText::StaticClass(), TEXT("IsPassword"));
	const FFloatProperty* EditableTextMinimumDesiredWidthProperty = FindFProperty<FFloatProperty>(UEditableText::StaticClass(), TEXT("MinimumDesiredWidth"));
	TestNotNull(TEXT("EditableTextBox IsReadOnly property exists"), EditableTextBoxReadOnlyProperty);
	TestNotNull(TEXT("EditableTextBox IsPassword property exists"), EditableTextBoxPasswordProperty);
	TestNotNull(TEXT("EditableTextBox MinimumDesiredWidth property exists"), EditableTextBoxMinimumDesiredWidthProperty);
	TestNotNull(TEXT("EditableText IsReadOnly property exists"), EditableTextReadOnlyProperty);
	TestNotNull(TEXT("EditableText IsPassword property exists"), EditableTextPasswordProperty);
	TestNotNull(TEXT("EditableText MinimumDesiredWidth property exists"), EditableTextMinimumDesiredWidthProperty);
	if (!EditableTextBoxReadOnlyProperty || !EditableTextBoxPasswordProperty || !EditableTextBoxMinimumDesiredWidthProperty
		|| !EditableTextReadOnlyProperty || !EditableTextPasswordProperty || !EditableTextMinimumDesiredWidthProperty)
	{
		return false;
	}

	FUMGWidgetNode Node;
	Node.Text.Text = TEXT("Editable text");
	Node.Text.bHasText = true;
	Node.Text.HintText = TEXT("Editable hint");
	Node.Text.bHasHintText = true;
	Node.Text.FontFamily = TEXT("Geist");
	Node.Text.bHasFontFamily = true;
	Node.Text.TypefaceFontName = TEXT("Regular");
	Node.Text.bHasTypefaceFontName = true;
	Node.Text.IconName = TEXT("glyph_edit");
	Node.Text.bHasIconName = true;
	Node.Text.Justification = TEXT("Center");
	Node.Text.bHasJustification = true;
	Node.Text.Color = FLinearColor(0.3f, 0.5f, 0.7f, 1.0f);
	Node.Text.bHasColor = true;
	Node.TextStyle.FontSize = 19.0f;
	Node.TextStyle.SourceFontSize = 19.0f;
	Node.TextStyle.LetterSpacing = 1.25f;
	Node.TextStyle.bHasLetterSpacing = true;

	const FUMGPropertyBag TypedProperties = Design2UMGWidgetNodeAccess::BuildTypedPropertyBag(Node);
	const FLinearColor SerializedNodeColor =
		FUMGValueParser::ParseColor(Design2UMGWidgetNodeAccess::SerializeWidgetColor(Node.Text.Color));

	UEditableTextBox* TypedTextBox = NewObject<UEditableTextBox>(GetTransientPackageAsObject());
	UEditableTextBox* LegacyTextBox = NewObject<UEditableTextBox>(GetTransientPackageAsObject());
	TestNotNull(TEXT("Typed editable text box created"), TypedTextBox);
	TestNotNull(TEXT("Legacy editable text box created"), LegacyTextBox);
	if (!TypedTextBox || !LegacyTextBox)
	{
		return false;
	}

	FUMGWidgetUtils::ApplyWidgetProperties(TypedTextBox, Node);
	FUMGWidgetUtils::ApplyWidgetProperties(LegacyTextBox, TypedProperties);

	const FEditableTextBoxStyle& TypedTextBoxStyle = TypedTextBox->GetWidgetStyle();
	const FEditableTextBoxStyle& LegacyTextBoxStyle = LegacyTextBox->GetWidgetStyle();
	TestEqual(TEXT("EditableTextBox typed and legacy text match"), TypedTextBox->GetText().ToString(), LegacyTextBox->GetText().ToString());
	TestEqual(TEXT("EditableTextBox typed and legacy hint match"), TypedTextBox->GetHintText().ToString(), LegacyTextBox->GetHintText().ToString());
	TestEqual(TEXT("EditableTextBox typed and legacy font size match"), TypedTextBoxStyle.TextStyle.Font.Size, LegacyTextBoxStyle.TextStyle.Font.Size);
	TestEqual(TEXT("EditableTextBox typed and legacy typeface match"), TypedTextBoxStyle.TextStyle.Font.TypefaceFontName, LegacyTextBoxStyle.TextStyle.Font.TypefaceFontName);
	TestEqual(TEXT("EditableTextBox typed and legacy letter spacing match"), TypedTextBoxStyle.TextStyle.Font.LetterSpacing, LegacyTextBoxStyle.TextStyle.Font.LetterSpacing);
	TestEqual(TEXT("EditableTextBox typed and legacy font object path match"),
		TypedTextBoxStyle.TextStyle.Font.FontObject ? TypedTextBoxStyle.TextStyle.Font.FontObject->GetPathName() : FString(),
		LegacyTextBoxStyle.TextStyle.Font.FontObject ? LegacyTextBoxStyle.TextStyle.Font.FontObject->GetPathName() : FString());
	TestTrue(TEXT("EditableTextBox typed text color follows typed node color directly"),
		TypedTextBoxStyle.TextStyle.ColorAndOpacity.GetSpecifiedColor().Equals(Node.Text.Color, 0.0001f));
	TestTrue(TEXT("EditableTextBox typed foreground color follows typed node color directly"),
		TypedTextBoxStyle.ForegroundColor.GetSpecifiedColor().Equals(Node.Text.Color, 0.0001f));
	TestTrue(TEXT("EditableTextBox legacy text color follows serialized payload color"),
		LegacyTextBoxStyle.TextStyle.ColorAndOpacity.GetSpecifiedColor().Equals(SerializedNodeColor, 0.0001f));
	TestTrue(TEXT("EditableTextBox legacy foreground color follows serialized payload color"),
		LegacyTextBoxStyle.ForegroundColor.GetSpecifiedColor().Equals(SerializedNodeColor, 0.0001f));
	TestEqual(TEXT("EditableTextBox typed and legacy justification match"), TypedTextBox->GetJustification(), LegacyTextBox->GetJustification());
	TestTrue(TEXT("EditableTextBox typed and legacy icon shaping match"),
		TypedTextBox->ShapedTextOptions == LegacyTextBox->ShapedTextOptions);

	UEditableText* TypedEditableText = NewObject<UEditableText>(GetTransientPackageAsObject());
	UEditableText* LegacyEditableText = NewObject<UEditableText>(GetTransientPackageAsObject());
	TestNotNull(TEXT("Typed editable text created"), TypedEditableText);
	TestNotNull(TEXT("Legacy editable text created"), LegacyEditableText);
	if (!TypedEditableText || !LegacyEditableText)
	{
		return false;
	}

	FUMGWidgetUtils::ApplyWidgetProperties(TypedEditableText, Node);
	FUMGWidgetUtils::ApplyWidgetProperties(LegacyEditableText, TypedProperties);

	TestEqual(TEXT("EditableText typed and legacy text match"), TypedEditableText->GetText().ToString(), LegacyEditableText->GetText().ToString());
	TestEqual(TEXT("EditableText typed and legacy hint match"), TypedEditableText->GetHintText().ToString(), LegacyEditableText->GetHintText().ToString());
	TestEqual(TEXT("EditableText typed and legacy font size match"), TypedEditableText->GetFont().Size, LegacyEditableText->GetFont().Size);
	TestEqual(TEXT("EditableText typed and legacy typeface match"), TypedEditableText->GetFont().TypefaceFontName, LegacyEditableText->GetFont().TypefaceFontName);
	TestEqual(TEXT("EditableText typed and legacy letter spacing match"), TypedEditableText->GetFont().LetterSpacing, LegacyEditableText->GetFont().LetterSpacing);
	TestEqual(TEXT("EditableText typed and legacy font object path match"),
		TypedEditableText->GetFont().FontObject ? TypedEditableText->GetFont().FontObject->GetPathName() : FString(),
		LegacyEditableText->GetFont().FontObject ? LegacyEditableText->GetFont().FontObject->GetPathName() : FString());
	TestTrue(TEXT("EditableText typed color follows typed node color directly"),
		TypedEditableText->WidgetStyle.ColorAndOpacity.GetSpecifiedColor().Equals(Node.Text.Color, 0.0001f));
	TestTrue(TEXT("EditableText legacy color follows serialized payload color"),
		LegacyEditableText->WidgetStyle.ColorAndOpacity.GetSpecifiedColor().Equals(SerializedNodeColor, 0.0001f));
	TestEqual(TEXT("EditableText typed and legacy justification match"), TypedEditableText->GetJustification(), LegacyEditableText->GetJustification());
	TestTrue(TEXT("EditableText typed and legacy icon shaping match"),
		TypedEditableText->ShapedTextOptions == LegacyEditableText->ShapedTextOptions);

	FUMGPropertyBag TypedBehaviorProperties = Design2UMGWidgetNodeAccess::BuildTypedPropertyBag(Node);
	TypedBehaviorProperties.Add(UMGPropertyKeys::IsReadOnly, TEXT("true"));
	TypedBehaviorProperties.Add(UMGPropertyKeys::IsPassword, TEXT("true"));
	TypedBehaviorProperties.Add(UMGPropertyKeys::MinimumDesiredWidth, TEXT("123.5"));

	bool bTypedPayloadReadOnly = false;
	bool bTypedPayloadPassword = false;
	float TypedPayloadMinimumDesiredWidth = 0.0f;
	TestTrue(TEXT("Typed editable payload keeps IsReadOnly"), TypedBehaviorProperties.TryGetBool(UMGPropertyKeys::IsReadOnly, bTypedPayloadReadOnly) && bTypedPayloadReadOnly);
	TestTrue(TEXT("Typed editable payload keeps IsPassword"), TypedBehaviorProperties.TryGetBool(UMGPropertyKeys::IsPassword, bTypedPayloadPassword) && bTypedPayloadPassword);
	TestTrue(TEXT("Typed editable payload keeps MinimumDesiredWidth"),
		TypedBehaviorProperties.TryGetFloat(UMGPropertyKeys::MinimumDesiredWidth, TypedPayloadMinimumDesiredWidth)
		&& FMath::IsNearlyEqual(TypedPayloadMinimumDesiredWidth, 123.5f, 0.001f));

	UEditableTextBox* TypedBehaviorTextBox = NewObject<UEditableTextBox>(GetTransientPackageAsObject());
	UEditableText* TypedBehaviorEditableText = NewObject<UEditableText>(GetTransientPackageAsObject());
	TestNotNull(TEXT("Typed behavior editable text box created"), TypedBehaviorTextBox);
	TestNotNull(TEXT("Typed behavior editable text created"), TypedBehaviorEditableText);
	if (!TypedBehaviorTextBox || !TypedBehaviorEditableText)
	{
		return false;
	}

	FUMGWidgetUtils::ApplyWidgetProperties(TypedBehaviorTextBox, TypedBehaviorProperties);
	FUMGWidgetUtils::ApplyWidgetProperties(TypedBehaviorEditableText, TypedBehaviorProperties);

	TestTrue(TEXT("Typed editable text box applies IsReadOnly"),
		EditableTextBoxReadOnlyProperty->GetPropertyValue_InContainer(TypedBehaviorTextBox));
	TestTrue(TEXT("Typed editable text box applies IsPassword"),
		EditableTextBoxPasswordProperty->GetPropertyValue_InContainer(TypedBehaviorTextBox));
	TestTrue(TEXT("Typed editable text box applies MinimumDesiredWidth"),
		FMath::IsNearlyEqual(
			EditableTextBoxMinimumDesiredWidthProperty->GetPropertyValue_InContainer(TypedBehaviorTextBox),
			123.5f,
			0.001f));
	TestTrue(TEXT("Typed editable text applies IsReadOnly"),
		EditableTextReadOnlyProperty->GetPropertyValue_InContainer(TypedBehaviorEditableText));
	TestTrue(TEXT("Typed editable text applies IsPassword"),
		EditableTextPasswordProperty->GetPropertyValue_InContainer(TypedBehaviorEditableText));
	TestTrue(TEXT("Typed editable text applies MinimumDesiredWidth"),
		FMath::IsNearlyEqual(
			EditableTextMinimumDesiredWidthProperty->GetPropertyValue_InContainer(TypedBehaviorEditableText),
			123.5f,
			0.001f));

	FUMGWidgetNode EmptyTypedNode;
	EmptyTypedNode.Text.Text = TEXT("");
	EmptyTypedNode.Text.bHasText = true;
	EmptyTypedNode.Text.HintText = TEXT("");
	EmptyTypedNode.Text.bHasHintText = true;

	FUMGPropertyBag EmptyTypedProperties = Design2UMGWidgetNodeAccess::BuildTypedPropertyBag(EmptyTypedNode);
	FString EmptyTypedTextValue;
	FString EmptyTypedHintValue;
	TestTrue(TEXT("Typed empty text is serialized instead of dropped"),
		EmptyTypedProperties.TryGetString(UMGPropertyKeys::Text, EmptyTypedTextValue) && EmptyTypedTextValue.IsEmpty());
	TestTrue(TEXT("Typed empty hint is serialized instead of dropped"),
		EmptyTypedProperties.TryGetString(UMGPropertyKeys::HintText, EmptyTypedHintValue) && EmptyTypedHintValue.IsEmpty());

	UEditableTextBox* EmptyTypedTextBox = NewObject<UEditableTextBox>(GetTransientPackageAsObject());
	UEditableText* EmptyTypedEditableText = NewObject<UEditableText>(GetTransientPackageAsObject());
	TestNotNull(TEXT("Typed empty editable text box created"), EmptyTypedTextBox);
	TestNotNull(TEXT("Typed empty editable text created"), EmptyTypedEditableText);
	if (!EmptyTypedTextBox || !EmptyTypedEditableText)
	{
		return false;
	}

	EmptyTypedTextBox->SetText(FText::FromString(TEXT("Existing Text")));
	EmptyTypedTextBox->SetHintText(FText::FromString(TEXT("Existing Hint")));
	EmptyTypedEditableText->SetText(FText::FromString(TEXT("Existing Text")));
	EmptyTypedEditableText->SetHintText(FText::FromString(TEXT("Existing Hint")));

	FUMGWidgetUtils::ApplyWidgetProperties(EmptyTypedTextBox, EmptyTypedProperties);
	FUMGWidgetUtils::ApplyWidgetProperties(EmptyTypedEditableText, EmptyTypedProperties);

	TestTrue(TEXT("Typed empty text clears EditableTextBox text"), EmptyTypedTextBox->GetText().ToString().IsEmpty());
	TestTrue(TEXT("Typed empty hint clears EditableTextBox hint"), EmptyTypedTextBox->GetHintText().ToString().IsEmpty());
	TestTrue(TEXT("Typed empty text clears EditableText text"), EmptyTypedEditableText->GetText().ToString().IsEmpty());
	TestTrue(TEXT("Typed empty hint clears EditableText hint"), EmptyTypedEditableText->GetHintText().ToString().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMultiLineEditableTextFamilyUsesTypedTextPayloadParityTest,
	"Design2UMG.PropertyApplier.MultiLineEditableTextFamilyUsesTypedTextPayload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMultiLineEditableTextFamilyUsesTypedTextPayloadParityTest::RunTest(const FString& Parameters)
{
	const FStructProperty* MarginProperty = FindFProperty<FStructProperty>(UTextLayoutWidget::StaticClass(), TEXT("Margin"));
	const FFloatProperty* LineHeightProperty = FindFProperty<FFloatProperty>(UTextLayoutWidget::StaticClass(), TEXT("LineHeightPercentage"));
	const FByteProperty* JustificationProperty = FindFProperty<FByteProperty>(UTextLayoutWidget::StaticClass(), TEXT("Justification"));
	const FStructProperty* ShapedTextOptionsProperty = FindFProperty<FStructProperty>(UTextLayoutWidget::StaticClass(), TEXT("ShapedTextOptions"));
	TestNotNull(TEXT("Text layout margin property exists"), MarginProperty);
	TestNotNull(TEXT("Text layout line-height property exists"), LineHeightProperty);
	TestNotNull(TEXT("Text layout justification property exists"), JustificationProperty);
	TestNotNull(TEXT("Text layout shaped-text property exists"), ShapedTextOptionsProperty);
	if (!MarginProperty || !LineHeightProperty || !JustificationProperty || !ShapedTextOptionsProperty)
	{
		return false;
	}

	FUMGWidgetNode Node;
	Node.Text.Text = TEXT("Multi-line editable text");
	Node.Text.bHasText = true;
	Node.Text.HintText = TEXT("Multi-line hint");
	Node.Text.bHasHintText = true;
	Node.Text.FontFamily = TEXT("Geist");
	Node.Text.bHasFontFamily = true;
	Node.Text.TypefaceFontName = TEXT("Regular");
	Node.Text.bHasTypefaceFontName = true;
	Node.Text.IconName = TEXT("glyph_multiline");
	Node.Text.bHasIconName = true;
	Node.Text.Justification = TEXT("Right");
	Node.Text.bHasJustification = true;
	Node.Text.bAutoWrapText = true;
	Node.Text.bHasAutoWrapText = true;
	Node.Text.WrapTextAt = 220.0f;
	Node.Text.bHasWrapTextAt = true;
	Node.Text.LineHeightPercentage = 1.4f;
	Node.Text.bHasLineHeightPercentage = true;
	Node.Text.Color = FLinearColor(0.15f, 0.55f, 0.35f, 1.0f);
	Node.Text.bHasColor = true;
	Node.TextStyle.FontSize = 17.0f;
	Node.TextStyle.SourceFontSize = 17.0f;
	Node.TextStyle.LetterSpacing = 0.75f;
	Node.TextStyle.bHasLetterSpacing = true;
	Node.WidgetStyle.Padding = FMargin(4.0f, 5.0f, 6.0f, 7.0f);

	const FUMGPropertyBag TypedProperties = Design2UMGWidgetNodeAccess::BuildTypedPropertyBag(Node);
	const FLinearColor SerializedNodeColor =
		FUMGValueParser::ParseColor(Design2UMGWidgetNodeAccess::SerializeWidgetColor(Node.Text.Color));

	auto ReadLayoutState = [&](UTextLayoutWidget* Widget, FMargin& OutMargin, float& OutLineHeight, ETextJustify::Type& OutJustification, FShapedTextOptions& OutShaping)
	{
		const FMargin* MarginValue = MarginProperty->ContainerPtrToValuePtr<FMargin>(Widget);
		OutMargin = MarginValue ? *MarginValue : FMargin();
		OutLineHeight = LineHeightProperty->GetPropertyValue_InContainer(Widget);
		OutJustification = static_cast<ETextJustify::Type>(
			JustificationProperty->GetUnsignedIntPropertyValue(JustificationProperty->ContainerPtrToValuePtr<void>(Widget)));
		const FShapedTextOptions* ShapingValue = ShapedTextOptionsProperty->ContainerPtrToValuePtr<FShapedTextOptions>(Widget);
		OutShaping = ShapingValue ? *ShapingValue : FShapedTextOptions();
	};

	UMultiLineEditableTextBox* TypedTextBox = NewObject<UMultiLineEditableTextBox>(GetTransientPackageAsObject());
	UMultiLineEditableTextBox* LegacyTextBox = NewObject<UMultiLineEditableTextBox>(GetTransientPackageAsObject());
	TestNotNull(TEXT("Typed multi-line editable text box created"), TypedTextBox);
	TestNotNull(TEXT("Legacy multi-line editable text box created"), LegacyTextBox);
	if (!TypedTextBox || !LegacyTextBox)
	{
		return false;
	}

	FUMGWidgetUtils::ApplyWidgetProperties(TypedTextBox, Node);
	FUMGWidgetUtils::ApplyWidgetProperties(LegacyTextBox, TypedProperties);

	FMargin TypedTextBoxMargin;
	FMargin LegacyTextBoxMargin;
	float TypedTextBoxLineHeight = 0.0f;
	float LegacyTextBoxLineHeight = 0.0f;
	ETextJustify::Type TypedTextBoxJustification = ETextJustify::Left;
	ETextJustify::Type LegacyTextBoxJustification = ETextJustify::Left;
	FShapedTextOptions TypedTextBoxShaping;
	FShapedTextOptions LegacyTextBoxShaping;
	ReadLayoutState(TypedTextBox, TypedTextBoxMargin, TypedTextBoxLineHeight, TypedTextBoxJustification, TypedTextBoxShaping);
	ReadLayoutState(LegacyTextBox, LegacyTextBoxMargin, LegacyTextBoxLineHeight, LegacyTextBoxJustification, LegacyTextBoxShaping);

	TestEqual(TEXT("MultiLineEditableTextBox typed and legacy text match"), TypedTextBox->GetText().ToString(), LegacyTextBox->GetText().ToString());
	TestEqual(TEXT("MultiLineEditableTextBox typed and legacy hint match"), TypedTextBox->GetHintText().ToString(), LegacyTextBox->GetHintText().ToString());
	TestEqual(TEXT("MultiLineEditableTextBox typed and legacy font size match"), TypedTextBox->WidgetStyle.TextStyle.Font.Size, LegacyTextBox->WidgetStyle.TextStyle.Font.Size);
	TestEqual(TEXT("MultiLineEditableTextBox typed and legacy typeface match"), TypedTextBox->WidgetStyle.TextStyle.Font.TypefaceFontName, LegacyTextBox->WidgetStyle.TextStyle.Font.TypefaceFontName);
	TestEqual(TEXT("MultiLineEditableTextBox typed and legacy letter spacing match"), TypedTextBox->WidgetStyle.TextStyle.Font.LetterSpacing, LegacyTextBox->WidgetStyle.TextStyle.Font.LetterSpacing);
	TestEqual(TEXT("MultiLineEditableTextBox typed and legacy font object path match"),
		TypedTextBox->WidgetStyle.TextStyle.Font.FontObject ? TypedTextBox->WidgetStyle.TextStyle.Font.FontObject->GetPathName() : FString(),
		LegacyTextBox->WidgetStyle.TextStyle.Font.FontObject ? LegacyTextBox->WidgetStyle.TextStyle.Font.FontObject->GetPathName() : FString());
	TestTrue(TEXT("MultiLineEditableTextBox typed text color follows typed node color directly"),
		TypedTextBox->WidgetStyle.TextStyle.ColorAndOpacity.GetSpecifiedColor().Equals(Node.Text.Color, 0.0001f));
	TestTrue(TEXT("MultiLineEditableTextBox typed foreground color follows typed node color directly"),
		TypedTextBox->WidgetStyle.ForegroundColor.GetSpecifiedColor().Equals(Node.Text.Color, 0.0001f));
	TestTrue(TEXT("MultiLineEditableTextBox legacy text color follows serialized payload color"),
		LegacyTextBox->WidgetStyle.TextStyle.ColorAndOpacity.GetSpecifiedColor().Equals(SerializedNodeColor, 0.0001f));
	TestTrue(TEXT("MultiLineEditableTextBox legacy foreground color follows serialized payload color"),
		LegacyTextBox->WidgetStyle.ForegroundColor.GetSpecifiedColor().Equals(SerializedNodeColor, 0.0001f));
	TestTrue(TEXT("MultiLineEditableTextBox typed and legacy auto-wrap match"), TypedTextBox->GetAutoWrapText() == LegacyTextBox->GetAutoWrapText());
	TestTrue(TEXT("MultiLineEditableTextBox typed and legacy wrap width match"), FMath::IsNearlyEqual(TypedTextBox->GetWrapTextAt(), LegacyTextBox->GetWrapTextAt(), 0.001f));
	TestTrue(TEXT("MultiLineEditableTextBox typed and legacy line height match"), FMath::IsNearlyEqual(TypedTextBoxLineHeight, LegacyTextBoxLineHeight, 0.001f));
	TestEqual(TEXT("MultiLineEditableTextBox typed and legacy justification match"), TypedTextBoxJustification, LegacyTextBoxJustification);
	TestTrue(TEXT("MultiLineEditableTextBox typed and legacy margin match"),
		FMath::IsNearlyEqual(TypedTextBoxMargin.Left, LegacyTextBoxMargin.Left, 0.001f)
		&& FMath::IsNearlyEqual(TypedTextBoxMargin.Top, LegacyTextBoxMargin.Top, 0.001f)
		&& FMath::IsNearlyEqual(TypedTextBoxMargin.Right, LegacyTextBoxMargin.Right, 0.001f)
		&& FMath::IsNearlyEqual(TypedTextBoxMargin.Bottom, LegacyTextBoxMargin.Bottom, 0.001f));
	TestTrue(TEXT("MultiLineEditableTextBox typed and legacy icon shaping match"), TypedTextBoxShaping == LegacyTextBoxShaping);

	UMultiLineEditableText* TypedEditableText = NewObject<UMultiLineEditableText>(GetTransientPackageAsObject());
	UMultiLineEditableText* LegacyEditableText = NewObject<UMultiLineEditableText>(GetTransientPackageAsObject());
	TestNotNull(TEXT("Typed multi-line editable text created"), TypedEditableText);
	TestNotNull(TEXT("Legacy multi-line editable text created"), LegacyEditableText);
	if (!TypedEditableText || !LegacyEditableText)
	{
		return false;
	}

	FUMGWidgetUtils::ApplyWidgetProperties(TypedEditableText, Node);
	FUMGWidgetUtils::ApplyWidgetProperties(LegacyEditableText, TypedProperties);

	FMargin TypedEditableTextMargin;
	FMargin LegacyEditableTextMargin;
	float TypedEditableTextLineHeight = 0.0f;
	float LegacyEditableTextLineHeight = 0.0f;
	ETextJustify::Type TypedEditableTextJustification = ETextJustify::Left;
	ETextJustify::Type LegacyEditableTextJustification = ETextJustify::Left;
	FShapedTextOptions TypedEditableTextShaping;
	FShapedTextOptions LegacyEditableTextShaping;
	ReadLayoutState(TypedEditableText, TypedEditableTextMargin, TypedEditableTextLineHeight, TypedEditableTextJustification, TypedEditableTextShaping);
	ReadLayoutState(LegacyEditableText, LegacyEditableTextMargin, LegacyEditableTextLineHeight, LegacyEditableTextJustification, LegacyEditableTextShaping);

	TestEqual(TEXT("MultiLineEditableText typed and legacy text match"), TypedEditableText->GetText().ToString(), LegacyEditableText->GetText().ToString());
	TestEqual(TEXT("MultiLineEditableText typed and legacy hint match"), TypedEditableText->GetHintText().ToString(), LegacyEditableText->GetHintText().ToString());
	TestEqual(TEXT("MultiLineEditableText typed and legacy font size match"), TypedEditableText->GetFont().Size, LegacyEditableText->GetFont().Size);
	TestEqual(TEXT("MultiLineEditableText typed and legacy typeface match"), TypedEditableText->GetFont().TypefaceFontName, LegacyEditableText->GetFont().TypefaceFontName);
	TestEqual(TEXT("MultiLineEditableText typed and legacy letter spacing match"), TypedEditableText->GetFont().LetterSpacing, LegacyEditableText->GetFont().LetterSpacing);
	TestEqual(TEXT("MultiLineEditableText typed and legacy font object path match"),
		TypedEditableText->GetFont().FontObject ? TypedEditableText->GetFont().FontObject->GetPathName() : FString(),
		LegacyEditableText->GetFont().FontObject ? LegacyEditableText->GetFont().FontObject->GetPathName() : FString());
	TestTrue(TEXT("MultiLineEditableText typed color follows typed node color directly"),
		TypedEditableText->WidgetStyle.ColorAndOpacity.GetSpecifiedColor().Equals(Node.Text.Color, 0.0001f));
	TestTrue(TEXT("MultiLineEditableText legacy color follows serialized payload color"),
		LegacyEditableText->WidgetStyle.ColorAndOpacity.GetSpecifiedColor().Equals(SerializedNodeColor, 0.0001f));
	TestTrue(TEXT("MultiLineEditableText typed and legacy auto-wrap match"), TypedEditableText->GetAutoWrapText() == LegacyEditableText->GetAutoWrapText());
	TestTrue(TEXT("MultiLineEditableText typed and legacy wrap width match"), FMath::IsNearlyEqual(TypedEditableText->GetWrapTextAt(), LegacyEditableText->GetWrapTextAt(), 0.001f));
	TestTrue(TEXT("MultiLineEditableText typed and legacy line height match"), FMath::IsNearlyEqual(TypedEditableTextLineHeight, LegacyEditableTextLineHeight, 0.001f));
	TestEqual(TEXT("MultiLineEditableText typed and legacy justification match"), TypedEditableTextJustification, LegacyEditableTextJustification);
	TestTrue(TEXT("MultiLineEditableText typed and legacy margin match"),
		FMath::IsNearlyEqual(TypedEditableTextMargin.Left, LegacyEditableTextMargin.Left, 0.001f)
		&& FMath::IsNearlyEqual(TypedEditableTextMargin.Top, LegacyEditableTextMargin.Top, 0.001f)
		&& FMath::IsNearlyEqual(TypedEditableTextMargin.Right, LegacyEditableTextMargin.Right, 0.001f)
		&& FMath::IsNearlyEqual(TypedEditableTextMargin.Bottom, LegacyEditableTextMargin.Bottom, 0.001f));
	TestTrue(TEXT("MultiLineEditableText typed and legacy icon shaping match"), TypedEditableTextShaping == LegacyEditableTextShaping);
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
	const FLinearColor ExpectedDefaultColor = Node.Text.Color;
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

	URichTextBlock* LegacyWidget = NewObject<URichTextBlock>(GetTransientPackageAsObject());
	TestTrue(TEXT("Legacy RichTextBlock created"), LegacyWidget != nullptr);
	if (!LegacyWidget)
	{
		return false;
	}
	LegacyWidget->SetDefaultTextStyle(PreconfiguredStyle);
	FUMGWidgetUtils::ApplyWidgetProperties(LegacyWidget, Design2UMGWidgetNodeAccess::BuildTypedPropertyBag(Node));
	const FLinearColor SerializedDefaultColor =
		FUMGValueParser::ParseColor(Design2UMGWidgetNodeAccess::SerializeWidgetColor(Node.Text.Color));

	TestEqual(TEXT("Typed and legacy rich text markup match"),
		Widget->GetText().ToString(),
		LegacyWidget->GetText().ToString());
	TestEqual(TEXT("Typed and legacy rich text tooltip match"),
		Widget->GetToolTipText().ToString(),
		LegacyWidget->GetToolTipText().ToString());
	TestTrue(TEXT("Typed and legacy rich text wrap width match"),
		FMath::IsNearlyEqual(Widget->GetWrapTextAt(), LegacyWidget->GetWrapTextAt(), 0.001f));

	const FTextBlockStyle& LegacyDefaultStyle = LegacyWidget->GetCurrentDefaultTextStyle();
	TestEqual(TEXT("Typed and legacy rich text default font size match"),
		DefaultStyle.Font.Size,
		LegacyDefaultStyle.Font.Size);
	TestTrue(TEXT("Typed rich text default color follows typed node color directly"),
		DefaultStyle.ColorAndOpacity.GetSpecifiedColor().Equals(Node.Text.Color, 0.0001f));
	TestTrue(TEXT("Legacy rich text default color follows serialized payload color"),
		LegacyDefaultStyle.ColorAndOpacity.GetSpecifiedColor().Equals(SerializedDefaultColor, 0.0001f));
	TestTrue(TEXT("Typed and legacy rich text default shadow color match"),
		DefaultStyle.ShadowColorAndOpacity.Equals(LegacyDefaultStyle.ShadowColorAndOpacity, 0.0001f));
	TestTrue(TEXT("Typed and legacy rich text default shadow offset match"),
		DefaultStyle.ShadowOffset == LegacyDefaultStyle.ShadowOffset);

	UDataTable* LegacyStyleSet = LegacyWidget->GetTextStyleSet();
	TestNotNull(TEXT("Legacy rich text style set exists"), LegacyStyleSet);
	if (!LegacyStyleSet)
	{
		return false;
	}

	const FRichTextStyleRow* LegacyLinkRow = LegacyStyleSet->FindRow<FRichTextStyleRow>(TEXT("d2u_run_1"), TEXT("RichTextBlockStyleRowsParity"));
	TestNotNull(TEXT("Legacy link row exists"), LegacyLinkRow);
	if (!LegacyLinkRow)
	{
		return false;
	}

	TestEqual(TEXT("Typed and legacy rich text row font size match"),
		LinkRow->TextStyle.Font.Size,
		LegacyLinkRow->TextStyle.Font.Size);
	TestTrue(TEXT("Typed and legacy rich text row color match"),
		LinkRow->TextStyle.ColorAndOpacity.GetSpecifiedColor().Equals(
			LegacyLinkRow->TextStyle.ColorAndOpacity.GetSpecifiedColor(),
			0.0001f));
	TestEqual(TEXT("Typed and legacy rich text row underline draw-as match"),
		LinkRow->TextStyle.UnderlineBrush.DrawAs,
		LegacyLinkRow->TextStyle.UnderlineBrush.DrawAs);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextFamilyTypedJsonRoundTripTest,
	"Design2UMG.UMGJson.TextFamilyTypedPayloadRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextFamilyTypedJsonRoundTripTest::RunTest(const FString& Parameters)
{
	const FStructProperty* MarginProperty = FindFProperty<FStructProperty>(UTextLayoutWidget::StaticClass(), TEXT("Margin"));
	const FFloatProperty* LineHeightProperty = FindFProperty<FFloatProperty>(UTextLayoutWidget::StaticClass(), TEXT("LineHeightPercentage"));
	const FByteProperty* JustificationProperty = FindFProperty<FByteProperty>(UTextLayoutWidget::StaticClass(), TEXT("Justification"));
	const FStructProperty* ShapedTextOptionsProperty = FindFProperty<FStructProperty>(UTextLayoutWidget::StaticClass(), TEXT("ShapedTextOptions"));
	TestNotNull(TEXT("Text layout margin property exists"), MarginProperty);
	TestNotNull(TEXT("Text layout line-height property exists"), LineHeightProperty);
	TestNotNull(TEXT("Text layout justification property exists"), JustificationProperty);
	TestNotNull(TEXT("Text layout shaped-text property exists"), ShapedTextOptionsProperty);
	if (!MarginProperty || !LineHeightProperty || !JustificationProperty || !ShapedTextOptionsProperty)
	{
		return false;
	}

	FUMGWidgetData SourceData;
	SourceData.WidgetName = TEXT("WBP_TextFamilyJsonRoundTrip");
	SourceData.RootWidget.Type = TEXT("CanvasPanel");
	SourceData.RootWidget.Id = TEXT("root");
	SourceData.RootWidget.Name = TEXT("Root");

	FUMGWidgetNode SourceNode;
	SourceNode.Type = TEXT("MultiLineEditableTextBox");
	SourceNode.Id = TEXT("typed_text_family");
	SourceNode.Name = TEXT("TypedTextFamily");
	SourceNode.Text.Text = TEXT("Round-trip text payload");
	SourceNode.Text.bHasText = true;
	SourceNode.Text.HintText = TEXT("Round-trip hint");
	SourceNode.Text.bHasHintText = true;
	SourceNode.Text.FontFamily = TEXT("/Game/Design2UMG/Fonts/Geist.Geist");
	SourceNode.Text.bHasFontFamily = true;
	SourceNode.Text.TypefaceFontName = TEXT("Regular");
	SourceNode.Text.bHasTypefaceFontName = true;
	SourceNode.Text.Justification = TEXT("Center");
	SourceNode.Text.bHasJustification = true;
	SourceNode.Text.bAutoWrapText = true;
	SourceNode.Text.bHasAutoWrapText = true;
	SourceNode.Text.WrapTextAt = 240.0f;
	SourceNode.Text.bHasWrapTextAt = true;
	SourceNode.Text.LineHeightPercentage = 1.3f;
	SourceNode.Text.bHasLineHeightPercentage = true;
	SourceNode.Text.Color = FLinearColor(0.25f, 0.45f, 0.65f, 1.0f);
	SourceNode.Text.bHasColor = true;
	SourceNode.Text.IconName = TEXT("glyph_roundtrip");
	SourceNode.Text.bHasIconName = true;
	SourceNode.TextStyle.FontSize = 18.0f;
	SourceNode.TextStyle.SourceFontSize = 18.0f;
	SourceNode.TextStyle.LetterSpacing = 1.25f;
	SourceNode.TextStyle.bHasLetterSpacing = true;
	SourceNode.WidgetStyle.Padding = FMargin(3.0f, 4.0f, 5.0f, 6.0f);
	SourceData.RootWidget.Children.Add(SourceNode);

	const FString Json = FUMGJsonSerializer::Serialize(SourceData);
	TestTrue(TEXT("Serialized text family emits TextStyle payload"), Json.Contains(TEXT("\"TextStyle\"")));
	TestTrue(TEXT("Serialized text family emits TextData payload"), Json.Contains(TEXT("\"TextData\"")));
	TestTrue(TEXT("Serialized text family emits rich typed hint field"), Json.Contains(TEXT("\"HintText\"")));

	FUMGWidgetData ParsedData;
	FString ParseError;
	TestTrue(TEXT("Text family JSON parses"), FUMGJsonParser::ParseJson(Json, ParsedData, &ParseError));
	if (!ParseError.IsEmpty())
	{
		AddInfo(ParseError);
	}

	const FUMGWidgetNode* ParsedNode = FindWidgetById(ParsedData.RootWidget, TEXT("typed_text_family"));
	TestNotNull(TEXT("Parsed text family node exists"), ParsedNode);
	if (!ParsedNode)
	{
		return false;
	}

	TestEqual(TEXT("Parsed text preserved"), ParsedNode->Text.Text, SourceNode.Text.Text);
	TestEqual(TEXT("Parsed hint preserved"), ParsedNode->Text.HintText, SourceNode.Text.HintText);
	TestEqual(TEXT("Parsed font family preserved"), ParsedNode->Text.FontFamily, SourceNode.Text.FontFamily);
	TestEqual(TEXT("Parsed typeface preserved"), ParsedNode->Text.TypefaceFontName, SourceNode.Text.TypefaceFontName);
	TestEqual(TEXT("Parsed justification preserved"), ParsedNode->Text.Justification, SourceNode.Text.Justification);
	TestTrue(TEXT("Parsed auto wrap flag preserved"), ParsedNode->Text.bHasAutoWrapText && ParsedNode->Text.bAutoWrapText == SourceNode.Text.bAutoWrapText);
	TestTrue(TEXT("Parsed wrap width preserved"), ParsedNode->Text.bHasWrapTextAt && FMath::IsNearlyEqual(ParsedNode->Text.WrapTextAt, SourceNode.Text.WrapTextAt, 0.001f));
	TestTrue(TEXT("Parsed line height preserved"), ParsedNode->Text.bHasLineHeightPercentage && FMath::IsNearlyEqual(ParsedNode->Text.LineHeightPercentage, SourceNode.Text.LineHeightPercentage, 0.001f));
	TestTrue(TEXT("Parsed color preserved"), ParsedNode->Text.bHasColor && ParsedNode->Text.Color.Equals(SourceNode.Text.Color, 0.01f));
	TestTrue(TEXT("Parsed font size preserved"), FMath::IsNearlyEqual(ParsedNode->TextStyle.FontSize, SourceNode.TextStyle.FontSize, 0.001f));
	TestTrue(TEXT("Parsed source font size preserved"), FMath::IsNearlyEqual(ParsedNode->TextStyle.SourceFontSize, SourceNode.TextStyle.SourceFontSize, 0.001f));
	TestTrue(TEXT("Parsed letter spacing preserved"), ParsedNode->TextStyle.bHasLetterSpacing && FMath::IsNearlyEqual(ParsedNode->TextStyle.LetterSpacing, SourceNode.TextStyle.LetterSpacing, 0.001f));
	TestTrue(TEXT("Parsed padding preserved"),
		ParsedNode->WidgetStyle.Padding.IsSet()
		&& FMath::IsNearlyEqual(ParsedNode->WidgetStyle.Padding->Left, SourceNode.WidgetStyle.Padding->Left, 0.001f)
		&& FMath::IsNearlyEqual(ParsedNode->WidgetStyle.Padding->Top, SourceNode.WidgetStyle.Padding->Top, 0.001f)
		&& FMath::IsNearlyEqual(ParsedNode->WidgetStyle.Padding->Right, SourceNode.WidgetStyle.Padding->Right, 0.001f)
		&& FMath::IsNearlyEqual(ParsedNode->WidgetStyle.Padding->Bottom, SourceNode.WidgetStyle.Padding->Bottom, 0.001f));

	auto ReadLayoutState = [&](UTextLayoutWidget* Widget, FMargin& OutMargin, float& OutLineHeight, ETextJustify::Type& OutJustification, FShapedTextOptions& OutShaping)
	{
		const FMargin* MarginValue = MarginProperty->ContainerPtrToValuePtr<FMargin>(Widget);
		OutMargin = MarginValue ? *MarginValue : FMargin();
		OutLineHeight = LineHeightProperty->GetPropertyValue_InContainer(Widget);
		OutJustification = static_cast<ETextJustify::Type>(
			JustificationProperty->GetUnsignedIntPropertyValue(JustificationProperty->ContainerPtrToValuePtr<void>(Widget)));
		const FShapedTextOptions* ShapingValue = ShapedTextOptionsProperty->ContainerPtrToValuePtr<FShapedTextOptions>(Widget);
		OutShaping = ShapingValue ? *ShapingValue : FShapedTextOptions();
	};

	UMultiLineEditableTextBox* SourceWidget = NewObject<UMultiLineEditableTextBox>(GetTransientPackageAsObject());
	UMultiLineEditableTextBox* ParsedWidget = NewObject<UMultiLineEditableTextBox>(GetTransientPackageAsObject());
	TestNotNull(TEXT("Source text family widget created"), SourceWidget);
	TestNotNull(TEXT("Parsed text family widget created"), ParsedWidget);
	if (!SourceWidget || !ParsedWidget)
	{
		return false;
	}

	FUMGWidgetUtils::ApplyWidgetProperties(SourceWidget, SourceNode);
	FUMGWidgetUtils::ApplyWidgetProperties(ParsedWidget, *ParsedNode);

	FMargin SourceMargin;
	FMargin ParsedMargin;
	float SourceLineHeight = 0.0f;
	float ParsedLineHeight = 0.0f;
	ETextJustify::Type SourceJustification = ETextJustify::Left;
	ETextJustify::Type ParsedJustification = ETextJustify::Left;
	FShapedTextOptions SourceShaping;
	FShapedTextOptions ParsedShaping;
	ReadLayoutState(SourceWidget, SourceMargin, SourceLineHeight, SourceJustification, SourceShaping);
	ReadLayoutState(ParsedWidget, ParsedMargin, ParsedLineHeight, ParsedJustification, ParsedShaping);

	TestEqual(TEXT("Round-trip widget text matches"), ParsedWidget->GetText().ToString(), SourceWidget->GetText().ToString());
	TestEqual(TEXT("Round-trip widget hint matches"), ParsedWidget->GetHintText().ToString(), SourceWidget->GetHintText().ToString());
	TestEqual(TEXT("Round-trip widget font size matches"), ParsedWidget->WidgetStyle.TextStyle.Font.Size, SourceWidget->WidgetStyle.TextStyle.Font.Size);
	TestEqual(TEXT("Round-trip widget typeface matches"), ParsedWidget->WidgetStyle.TextStyle.Font.TypefaceFontName, SourceWidget->WidgetStyle.TextStyle.Font.TypefaceFontName);
	TestEqual(TEXT("Round-trip widget letter spacing matches"), ParsedWidget->WidgetStyle.TextStyle.Font.LetterSpacing, SourceWidget->WidgetStyle.TextStyle.Font.LetterSpacing);
	TestEqual(TEXT("Round-trip widget font object path matches"),
		ParsedWidget->WidgetStyle.TextStyle.Font.FontObject ? ParsedWidget->WidgetStyle.TextStyle.Font.FontObject->GetPathName() : FString(),
		SourceWidget->WidgetStyle.TextStyle.Font.FontObject ? SourceWidget->WidgetStyle.TextStyle.Font.FontObject->GetPathName() : FString());
	TestTrue(TEXT("Source widget text color follows source node color directly"),
		SourceWidget->WidgetStyle.TextStyle.ColorAndOpacity.GetSpecifiedColor().Equals(SourceNode.Text.Color, 0.0001f));
	TestTrue(TEXT("Parsed widget text color follows parsed node color directly"),
		ParsedWidget->WidgetStyle.TextStyle.ColorAndOpacity.GetSpecifiedColor().Equals(ParsedNode->Text.Color, 0.0001f));
	TestTrue(TEXT("Source widget foreground color follows source node color directly"),
		SourceWidget->WidgetStyle.ForegroundColor.GetSpecifiedColor().Equals(SourceNode.Text.Color, 0.0001f));
	TestTrue(TEXT("Parsed widget foreground color follows parsed node color directly"),
		ParsedWidget->WidgetStyle.ForegroundColor.GetSpecifiedColor().Equals(ParsedNode->Text.Color, 0.0001f));
	TestTrue(TEXT("Round-trip widget auto wrap matches"), ParsedWidget->GetAutoWrapText() == SourceWidget->GetAutoWrapText());
	TestTrue(TEXT("Round-trip widget wrap width matches"), FMath::IsNearlyEqual(ParsedWidget->GetWrapTextAt(), SourceWidget->GetWrapTextAt(), 0.001f));
	TestTrue(TEXT("Round-trip widget line height matches"), FMath::IsNearlyEqual(ParsedLineHeight, SourceLineHeight, 0.001f));
	TestEqual(TEXT("Round-trip widget justification matches"), ParsedJustification, SourceJustification);
	TestTrue(TEXT("Round-trip widget margin matches"),
		FMath::IsNearlyEqual(ParsedMargin.Left, SourceMargin.Left, 0.001f)
		&& FMath::IsNearlyEqual(ParsedMargin.Top, SourceMargin.Top, 0.001f)
		&& FMath::IsNearlyEqual(ParsedMargin.Right, SourceMargin.Right, 0.001f)
		&& FMath::IsNearlyEqual(ParsedMargin.Bottom, SourceMargin.Bottom, 0.001f));
	TestTrue(TEXT("Round-trip widget shaping matches"), ParsedShaping == SourceShaping);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRichTextTypedJsonRoundTripTest,
	"Design2UMG.UMGJson.RichTextTypedPayloadRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRichTextTypedJsonRoundTripTest::RunTest(const FString& Parameters)
{
	FUMGWidgetData SourceData;
	SourceData.WidgetName = TEXT("WBP_RichTextJsonRoundTrip");
	SourceData.RootWidget.Type = TEXT("CanvasPanel");
	SourceData.RootWidget.Id = TEXT("root");
	SourceData.RootWidget.Name = TEXT("Root");

	FUMGWidgetNode SourceNode;
	SourceNode.Type = TEXT("RichTextBlock");
	SourceNode.Id = TEXT("typed_rich_text");
	SourceNode.Name = TEXT("TypedRichText");
	SourceNode.Text.Text = TEXT("<d2u_run_0>Hello </><d2u_run_1 href=\"https://docs.example.com\">Docs</>");
	SourceNode.Text.bHasText = true;
	SourceNode.Text.RichTextStyleRowsJson = TEXT("{\"d2u_run_0\":{\"Color\":\"#112233FF\",\"DesignFontSize\":18.0,\"FontFamily\":\"/Game/Design2UMG/Fonts/Geist.Geist\",\"FontSize\":27.0,\"TypefaceFontName\":\"Regular\"},\"d2u_run_1\":{\"Color\":\"#224466FF\",\"DesignFontSize\":18.0,\"FontFamily\":\"/Game/Design2UMG/Fonts/Geist.Geist\",\"FontSize\":27.0,\"Href\":\"https://docs.example.com\",\"TypefaceFontName\":\"Bold\",\"Underline\":true}}");
	SourceNode.Text.bHasRichTextStyleRows = true;
	SourceNode.Text.FontFamily = TEXT("/Game/Design2UMG/Fonts/Geist.Geist");
	SourceNode.Text.bHasFontFamily = true;
	SourceNode.Text.TypefaceFontName = TEXT("Regular");
	SourceNode.Text.bHasTypefaceFontName = true;
	SourceNode.Text.Justification = TEXT("Center");
	SourceNode.Text.bHasJustification = true;
	SourceNode.Text.bAutoWrapText = true;
	SourceNode.Text.bHasAutoWrapText = true;
	SourceNode.Text.WrapTextAt = 180.0f;
	SourceNode.Text.bHasWrapTextAt = true;
	SourceNode.Text.LineHeightPercentage = 1.25f;
	SourceNode.Text.bHasLineHeightPercentage = true;
	SourceNode.Text.Color = FLinearColor(0.1f, 0.2f, 0.3f, 1.0f);
	SourceNode.Text.bHasColor = true;
	SourceNode.TextStyle.FontSize = 18.0f;
	SourceNode.TextStyle.SourceFontSize = 18.0f;
	SourceNode.TextStyle.LetterSpacing = 1.5f;
	SourceNode.TextStyle.bHasLetterSpacing = true;
	SourceNode.WidgetStyle.Padding = FMargin(3.0f, 4.0f, 5.0f, 6.0f);
	SourceData.RootWidget.Children.Add(SourceNode);

	const FString Json = FUMGJsonSerializer::Serialize(SourceData);
	TestTrue(TEXT("Serialized rich text emits rich style row payload"), Json.Contains(TEXT("d2u_run_0")) && Json.Contains(TEXT("d2u_run_1")));
	TestTrue(TEXT("Serialized rich text emits text style payload"), Json.Contains(TEXT("\"TextStyle\"")));

	FUMGWidgetData ParsedData;
	FString ParseError;
	TestTrue(TEXT("Rich text JSON parses"), FUMGJsonParser::ParseJson(Json, ParsedData, &ParseError));
	if (!ParseError.IsEmpty())
	{
		AddInfo(ParseError);
	}

	const FUMGWidgetNode* ParsedNode = FindWidgetById(ParsedData.RootWidget, TEXT("typed_rich_text"));
	TestNotNull(TEXT("Parsed rich text node exists"), ParsedNode);
	if (!ParsedNode)
	{
		return false;
	}

	TestEqual(TEXT("Parsed rich text markup preserved"), ParsedNode->Text.Text, SourceNode.Text.Text);
	TestEqual(TEXT("Parsed rich text style rows preserved"), ParsedNode->Text.RichTextStyleRowsJson, SourceNode.Text.RichTextStyleRowsJson);
	TestEqual(TEXT("Parsed rich text font family preserved"), ParsedNode->Text.FontFamily, SourceNode.Text.FontFamily);
	TestEqual(TEXT("Parsed rich text typeface preserved"), ParsedNode->Text.TypefaceFontName, SourceNode.Text.TypefaceFontName);
	TestTrue(TEXT("Parsed rich text wrap width preserved"), ParsedNode->Text.bHasWrapTextAt && FMath::IsNearlyEqual(ParsedNode->Text.WrapTextAt, SourceNode.Text.WrapTextAt, 0.001f));
	TestTrue(TEXT("Parsed rich text line height preserved"), ParsedNode->Text.bHasLineHeightPercentage && FMath::IsNearlyEqual(ParsedNode->Text.LineHeightPercentage, SourceNode.Text.LineHeightPercentage, 0.001f));
	TestTrue(TEXT("Parsed rich text font size preserved"), FMath::IsNearlyEqual(ParsedNode->TextStyle.FontSize, SourceNode.TextStyle.FontSize, 0.001f));
	TestTrue(TEXT("Parsed rich text source font size preserved"), FMath::IsNearlyEqual(ParsedNode->TextStyle.SourceFontSize, SourceNode.TextStyle.SourceFontSize, 0.001f));
	TestTrue(TEXT("Parsed rich text letter spacing preserved"), ParsedNode->TextStyle.bHasLetterSpacing && FMath::IsNearlyEqual(ParsedNode->TextStyle.LetterSpacing, SourceNode.TextStyle.LetterSpacing, 0.001f));

	auto BuildStyleSnapshot = [&](URichTextBlock* Widget, const TCHAR* Context) -> FString
	{
		UDataTable* StyleSet = Widget->GetTextStyleSet();
		if (!StyleSet)
		{
			return FString();
		}

		const FRichTextStyleRow* BaseRow = StyleSet->FindRow<FRichTextStyleRow>(TEXT("d2u_run_0"), Context);
		const FRichTextStyleRow* LinkRow = StyleSet->FindRow<FRichTextStyleRow>(TEXT("d2u_run_1"), Context);
		if (!BaseRow || !LinkRow)
		{
			return FString();
		}

		return FString::Printf(
			TEXT("base:%.0f:%s|link:%.0f:%s:%d"),
			BaseRow->TextStyle.Font.Size,
			*BaseRow->TextStyle.ColorAndOpacity.GetSpecifiedColor().ToFColor(true).ToHex(),
			LinkRow->TextStyle.Font.Size,
			*LinkRow->TextStyle.ColorAndOpacity.GetSpecifiedColor().ToFColor(true).ToHex(),
			static_cast<int32>(LinkRow->TextStyle.UnderlineBrush.DrawAs));
	};

	URichTextBlock* SourceWidget = NewObject<URichTextBlock>(GetTransientPackageAsObject());
	URichTextBlock* ParsedWidget = NewObject<URichTextBlock>(GetTransientPackageAsObject());
	TestNotNull(TEXT("Source rich text widget created"), SourceWidget);
	TestNotNull(TEXT("Parsed rich text widget created"), ParsedWidget);
	if (!SourceWidget || !ParsedWidget)
	{
		return false;
	}

	const FTextBlockStyle PreconfiguredStyle = FTextBlockStyle::GetDefault();
	SourceWidget->SetDefaultTextStyle(PreconfiguredStyle);
	ParsedWidget->SetDefaultTextStyle(PreconfiguredStyle);

	FUMGWidgetUtils::ApplyWidgetProperties(SourceWidget, SourceNode);
	FUMGWidgetUtils::ApplyWidgetProperties(ParsedWidget, *ParsedNode);

	TestEqual(TEXT("Round-trip rich text markup matches"), ParsedWidget->GetText().ToString(), SourceWidget->GetText().ToString());
	TestTrue(TEXT("Round-trip rich text wrap width matches"), FMath::IsNearlyEqual(ParsedWidget->GetWrapTextAt(), SourceWidget->GetWrapTextAt(), 0.001f));
	TestEqual(TEXT("Round-trip rich text style snapshot matches"),
		BuildStyleSnapshot(ParsedWidget, TEXT("ParsedRichTextRoundTrip")),
		BuildStyleSnapshot(SourceWidget, TEXT("SourceRichTextRoundTrip")));
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrushDescriptorPrecedenceContractTest,
	"Design2UMG.WidgetNodeAccess.BrushDescriptorPrecedenceContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBrushDescriptorPrecedenceContractTest::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Node;
	Node.Brush.MaterialRef = TEXT("roundedrect");
	Node.Brush.bHasMaterialRef = true;
	Node.Brush.TexturePath = TEXT("/Game/UI/T_LegacyBrush.T_LegacyBrush");
	Node.Brush.Image.TexturePath = TEXT("/Game/UI/T_DescriptorBrush.T_DescriptorBrush");
	Node.Brush.Image.bHasTexturePath = true;
	Node.Brush.Gradient.Type = TEXT("Angular");
	Node.Brush.Gradient.bHasType = true;
	Node.Brush.Gradient.Rotation = 15.0f;
	Node.Brush.Gradient.bHasRotation = true;
	Node.Brush.Gradient.Stops = {
		{ FLinearColor::Red, 0.0f },
		{ FLinearColor::Blue, 1.0f },
	};

	FString MaterialRef;
	TestTrue(TEXT("Brush typed string resolves material ref"), Design2UMGWidgetNodeAccess::TryGetBrushTypedWidgetString(Node, UMGPropertyKeys::MaterialRef, MaterialRef));
	TestEqual(TEXT("Gradient-derived material ref takes precedence"), MaterialRef, FString(TEXT("angulargradient")));
	FString PublicMaterialRef;
	TestTrue(TEXT("Public getter resolves material ref through the same precedence"), Design2UMGWidgetNodeAccess::TryGetWidgetString(Node, UMGPropertyKeys::MaterialRef, PublicMaterialRef));
	TestEqual(TEXT("Public getter material ref matches typed precedence"), PublicMaterialRef, MaterialRef);

	FString TexturePath;
	TestTrue(TEXT("Brush typed string resolves texture path"), Design2UMGWidgetNodeAccess::TryGetBrushTypedWidgetString(Node, UMGPropertyKeys::TexturePath, TexturePath));
	TestEqual(TEXT("Descriptor-backed image texture path takes precedence"), TexturePath, FString(TEXT("/Game/UI/T_DescriptorBrush.T_DescriptorBrush")));
	FString PublicTexturePath;
	TestTrue(TEXT("Public getter resolves texture path through the same precedence"), Design2UMGWidgetNodeAccess::TryGetWidgetString(Node, UMGPropertyKeys::TexturePath, PublicTexturePath));
	TestEqual(TEXT("Public getter texture path matches typed precedence"), PublicTexturePath, TexturePath);

	FUMGPropertyBag TypedProperties = Design2UMGWidgetNodeAccess::BuildTypedPropertyBag(Node);
	FString BagMaterialRef;
	TestTrue(TEXT("Typed property bag keeps precedence-selected material ref"), TypedProperties.TryGetString(UMGPropertyKeys::MaterialRef, BagMaterialRef));
	TestEqual(TEXT("Typed property bag material ref matches precedence contract"), BagMaterialRef, MaterialRef);
	FString BagTexturePath;
	TestTrue(TEXT("Typed property bag keeps precedence-selected texture path"), TypedProperties.TryGetString(UMGPropertyKeys::TexturePath, BagTexturePath));
	TestEqual(TEXT("Typed property bag texture path matches precedence contract"), BagTexturePath, TexturePath);

	Node.Brush.Gradient = FUMGVisualBrushData::FGradientData();
	TestTrue(TEXT("Explicit material ref remains after removing gradient"), Design2UMGWidgetNodeAccess::TryGetBrushTypedWidgetString(Node, UMGPropertyKeys::MaterialRef, MaterialRef));
	TestEqual(TEXT("Explicit material ref becomes fallback owner"), MaterialRef, FString(TEXT("roundedrect")));
	TestTrue(TEXT("Public getter falls back to explicit material ref once gradient is removed"), Design2UMGWidgetNodeAccess::TryGetWidgetString(Node, UMGPropertyKeys::MaterialRef, PublicMaterialRef));
	TestEqual(TEXT("Public getter explicit material ref matches typed fallback"), PublicMaterialRef, MaterialRef);

	Node.Brush.Image.TexturePath.Empty();
	Node.Brush.Image.bHasTexturePath = false;
	TestTrue(TEXT("Legacy texture path remains after removing descriptor texture"), Design2UMGWidgetNodeAccess::TryGetBrushTypedWidgetString(Node, UMGPropertyKeys::TexturePath, TexturePath));
	TestEqual(TEXT("Legacy texture path becomes fallback owner"), TexturePath, FString(TEXT("/Game/UI/T_LegacyBrush.T_LegacyBrush")));
	TestTrue(TEXT("Public getter falls back to legacy texture path once descriptor texture is removed"), Design2UMGWidgetNodeAccess::TryGetWidgetString(Node, UMGPropertyKeys::TexturePath, PublicTexturePath));
	TestEqual(TEXT("Public getter legacy texture path matches typed fallback"), PublicTexturePath, TexturePath);

	TypedProperties = Design2UMGWidgetNodeAccess::BuildTypedPropertyBag(Node);
	TestTrue(TEXT("Typed property bag keeps explicit material ref after gradient removal"), TypedProperties.TryGetString(UMGPropertyKeys::MaterialRef, BagMaterialRef));
	TestEqual(TEXT("Typed property bag explicit material ref matches typed fallback"), BagMaterialRef, MaterialRef);
	TestTrue(TEXT("Typed property bag keeps legacy texture path after descriptor removal"), TypedProperties.TryGetString(UMGPropertyKeys::TexturePath, BagTexturePath));
	TestEqual(TEXT("Typed property bag legacy texture path matches typed fallback"), BagTexturePath, TexturePath);
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
