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
	TestTrue(TEXT("Foreground blur retain rendering typed"), Data.RootWidget.Effect.bRetainRendering.IsSet() && Data.RootWidget.Effect.bRetainRendering.GetValue());
	TestEqual(TEXT("Foreground blur effect material ref exported"), Data.RootWidget.Effect.EffectMaterial.GetValue(), TEXT("d2u.retainer.foreground_blur"));
	TestEqual(TEXT("Foreground blur texture parameter exported"), Data.RootWidget.Effect.TextureParameter.GetValue(), TEXT("Texture"));

	TestTrue(TEXT("Foreground blur UV step is typed"), Data.RootWidget.Effect.EffectBlurUVStep.IsSet());
	const FVector2D UVStep = Data.RootWidget.Effect.EffectBlurUVStep.GetValue();
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
	TestTrue(TEXT("Inner shadow retain rendering typed"), Data.RootWidget.Effect.bRetainRendering.IsSet() && Data.RootWidget.Effect.bRetainRendering.GetValue());
	TestEqual(TEXT("Inner shadow effect material ref exported"), Data.RootWidget.Effect.EffectMaterial.GetValue(), TEXT("d2u.retainer.inner_shadow"));
	TestEqual(TEXT("Inner shadow texture parameter exported"), Data.RootWidget.Effect.TextureParameter.GetValue(), TEXT("Texture"));
	TestTrue(TEXT("Inner shadow color exported via typed effect"), Data.RootWidget.Effect.EffectColor.IsSet());
	TestTrue(TEXT("Inner shadow offset exported via typed effect"), Data.RootWidget.Effect.EffectOffset.IsSet());
	TestTrue(TEXT("Inner shadow blur step exported via typed effect"), Data.RootWidget.Effect.EffectBlurUVStep.IsSet());
	TestTrue(TEXT("Inner shadow normalized offset X preserved"), FMath::IsNearlyEqual(Data.RootWidget.Effect.EffectOffset.GetValue().X, 0.02f, 0.0001f));
	TestTrue(TEXT("Inner shadow normalized offset Y preserved"), FMath::IsNearlyEqual(Data.RootWidget.Effect.EffectOffset.GetValue().Y, 0.0375f, 0.0001f));
	TestTrue(TEXT("Inner shadow color alpha preserved"), FMath::IsNearlyEqual(Data.RootWidget.Effect.EffectColor.GetValue().A, 0.4f, 0.01f));
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
	TestTrue(TEXT("Background blur strength exported via typed effect"), Data.RootWidget.Effect.BlurStrength.IsSet());
	TestTrue(TEXT("Background blur strength preserved"), FMath::IsNearlyEqual(Data.RootWidget.Effect.BlurStrength.GetValue(), 18.0f));
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
	TestTrue(TEXT("Figma background blur keeps typed strength"), Data.RootWidget.Effect.BlurStrength.IsSet() && FMath::IsNearlyEqual(Data.RootWidget.Effect.BlurStrength.GetValue(), 16.0f, 0.001f));
	TestTrue(TEXT("Figma background blur keeps one child"), Data.RootWidget.Children.Num() == 1);
	if (Data.RootWidget.Children.Num() != 1)
	{
		return false;
	}

	const FUMGWidgetNode& InnerShadowWrapper = Data.RootWidget.Children[0];
	TestEqual(TEXT("Figma inner shadow becomes retainer child"), InnerShadowWrapper.Type, TEXT("RetainerBox"));
	TestTrue(TEXT("Figma inner shadow retain rendering typed"), InnerShadowWrapper.Effect.bRetainRendering.IsSet() && InnerShadowWrapper.Effect.bRetainRendering.GetValue());
	TestEqual(TEXT("Figma inner shadow effect material typed"), InnerShadowWrapper.Effect.EffectMaterial.GetValue(), TEXT("d2u.retainer.inner_shadow"));
	TestTrue(TEXT("Figma inner shadow offset typed"), InnerShadowWrapper.Effect.EffectOffset.IsSet());
	TestTrue(TEXT("Figma inner shadow blur typed"), InnerShadowWrapper.Effect.EffectBlurUVStep.IsSet());
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
	TestTrue(TEXT("Figma layer blur retain rendering typed"), Data.RootWidget.Effect.bRetainRendering.IsSet() && Data.RootWidget.Effect.bRetainRendering.GetValue());
	TestEqual(TEXT("Figma layer blur effect material typed"), Data.RootWidget.Effect.EffectMaterial.GetValue(), TEXT("d2u.retainer.foreground_blur"));
	TestTrue(TEXT("Figma layer blur UV step typed"), Data.RootWidget.Effect.EffectBlurUVStep.IsSet());
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
	TestTrue(TEXT("Figma outer shadow synthetic blur typed"), Data.RootWidget.Children[0].Shadow.ShadowBlur.IsSet() && FMath::IsNearlyEqual(Data.RootWidget.Children[0].Shadow.ShadowBlur.GetValue(), 10.0f, 0.001f));
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
	Node.Effect.EffectMaterial = TEXT("d2u.retainer.foreground_blur");
	Node.Effect.TextureParameter = TEXT("Texture");
	Node.Effect.EffectBlurUVStep = FVector2D(0.01f, 0.02f);
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
	Node.Effect.EffectMaterial = TEXT("d2u.retainer.inner_shadow");
	Node.Effect.TextureParameter = TEXT("Texture");
	Node.Effect.EffectColor = FLinearColor(0.1f, 0.2f, 0.3f, 0.4f);
	Node.Effect.EffectOffset = FVector2D(0.05f, -0.03f);
	Node.Effect.EffectBlurUVStep = FVector2D(0.01f, 0.02f);
	const FLinearColor NormalizedEffectColor = FUMGValueParser::ParseColor(
		Design2UMGWidgetNodeAccess::SerializeWidgetColor(Node.Effect.EffectColor.GetValue()));

	TestTrue(TEXT("Typed retainer payload keeps EffectColor directly"),
		Node.Effect.EffectColor.IsSet()
		&& Node.Effect.EffectColor.GetValue().Equals(FLinearColor(0.1f, 0.2f, 0.3f, 0.4f), 0.0001f));

	URetainerBox* PayloadWidget = NewObject<URetainerBox>(GetTransientPackageAsObject());
	TestTrue(TEXT("Typed payload RetainerBox created"), PayloadWidget != nullptr);
	if (!PayloadWidget)
	{
		return false;
	}

	FUMGWidgetUtils::ApplyWidgetProperties(PayloadWidget, Node);
	UMaterialInstanceDynamic* PayloadEffect = PayloadWidget->GetEffectMaterial();
	TestTrue(TEXT("Typed payload dynamic effect material created"), PayloadEffect != nullptr);
	if (!PayloadEffect)
	{
		return false;
	}

	const FLinearColor PayloadInnerShadowColor = PayloadEffect->K2_GetVectorParameterValue(TEXT("InnerShadowColor"));
	TestTrue(TEXT("Typed payload inner shadow color follows normalized typed payload color"),
		PayloadInnerShadowColor.Equals(NormalizedEffectColor, 0.0001f));

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
	TestTrue(TEXT("Inner shadow color R applied"), FMath::IsNearlyEqual(InnerShadowColor.R, NormalizedEffectColor.R, 0.0001f));
	TestTrue(TEXT("Inner shadow color A applied"), FMath::IsNearlyEqual(InnerShadowColor.A, NormalizedEffectColor.A, 0.0001f));
	TestTrue(TEXT("Inner shadow strength applied"), FMath::IsNearlyEqual(InnerShadowStrength, NormalizedEffectColor.A, 0.0001f));
	TestTrue(TEXT("Inner shadow offset X applied"), FMath::IsNearlyEqual(InnerShadowOffset.R, 0.05f, 0.0001f));
	TestTrue(TEXT("Inner shadow offset Y applied"), FMath::IsNearlyEqual(InnerShadowOffset.G, -0.03f, 0.0001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRetainerTypedPayloadExplicitFalseAndRenderingPhaseTest,
	"Design2UMG.PropertyApplier.RetainerExplicitFalseAndRenderingPhase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRetainerTypedPayloadExplicitFalseAndRenderingPhaseTest::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Node;
	Node.Type = TEXT("RetainerBox");
	Node.Effect.bRetainRendering = false;
	FUMGEffectData::FRenderingPhaseData RenderingPhase;
	RenderingPhase.Phase = 3;
	RenderingPhase.PhaseCount = 5;
	Node.Effect.RenderingPhase = RenderingPhase;

	TestTrue(TEXT("Typed retainer payload keeps explicit false RetainRendering"),
		Node.Effect.bRetainRendering.IsSet() && !Node.Effect.bRetainRendering.GetValue());
	TestTrue(TEXT("Typed retainer payload keeps RenderingPhase phase"),
		Node.Effect.RenderingPhase.IsSet() && Node.Effect.RenderingPhase.GetValue().Phase == 3);
	TestTrue(TEXT("Typed retainer payload keeps RenderingPhase phase count"),
		Node.Effect.RenderingPhase.IsSet() && Node.Effect.RenderingPhase.GetValue().PhaseCount == 5);

	URetainerBox* TypedWidget = NewObject<URetainerBox>(GetTransientPackageAsObject());
	URetainerBox* PayloadWidget = NewObject<URetainerBox>(GetTransientPackageAsObject());
	TestNotNull(TEXT("Typed retainer widget created"), TypedWidget);
	TestNotNull(TEXT("Payload retainer widget created"), PayloadWidget);
	if (!TypedWidget || !PayloadWidget)
	{
		return false;
	}

	TypedWidget->SetRetainRendering(true);
	TypedWidget->SetRenderingPhase(0, 1);
	FUMGWidgetUtils::ApplyWidgetProperties(TypedWidget, Node);
	TestFalse(TEXT("Typed retainer widget applies explicit false RetainRendering"), TypedWidget->IsRetainRendering());
	TestEqual(TEXT("Typed retainer widget applies RenderingPhase phase"), TypedWidget->GetPhase(), 3);
	TestEqual(TEXT("Typed retainer widget applies RenderingPhase phase count"), TypedWidget->GetPhaseCount(), 5);

	PayloadWidget->SetRetainRendering(true);
	PayloadWidget->SetRenderingPhase(0, 1);
	FUMGWidgetUtils::ApplyWidgetProperties(PayloadWidget, Node);
	TestFalse(TEXT("Payload retainer widget applies explicit false RetainRendering"), PayloadWidget->IsRetainRendering());
	TestEqual(TEXT("Payload retainer widget applies RenderingPhase phase"), PayloadWidget->GetPhase(), 3);
	TestEqual(TEXT("Payload retainer widget applies RenderingPhase phase count"), PayloadWidget->GetPhaseCount(), 5);
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
	Node.Text.FontFamily = TEXT("Geist");
	Node.Text.TypefaceFontName = TEXT("Regular");
	Node.Text.IconName = TEXT("glyph_download");
	Node.Text.Justification = TEXT("Center");
	Node.Text.bAutoWrapText = true;
	Node.Text.WrapTextAt = 144.0f;
	Node.Text.LineHeightPercentage = 1.35f;
	Node.Text.Color = FLinearColor(0.2f, 0.4f, 0.6f, 1.0f);
	Node.TextStyle.FontSize = 18.0f;
	Node.TextStyle.SourceFontSize = 18.0f;
	Node.TextStyle.LetterSpacing = 1.5f;
	Node.WidgetStyle.Padding = FMargin(7.0f, 8.0f, 9.0f, 10.0f);
	Node.WidgetStyle.ToolTipText = TEXT("Typed tooltip");
	Node.Shadow.ShadowOffset = FVector2D(2.0f, 3.0f);
	Node.Shadow.ShadowColorAndOpacity = FLinearColor(0.9f, 0.1f, 0.2f, 0.5f);

	FUMGWidgetUtils::ApplyWidgetProperties(TypedWidget, Node);
	FUMGWidgetUtils::ApplyWidgetProperties(LegacyWidget, Node);

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
	const FLinearColor ExpectedTextColor = UMGTextStyleApplication::NormalizeTypedTextColor(Node.Text.Color.GetValue());
	TestTrue(TEXT("Typed text block color follows normalized typed node color"),
		TypedWidget->GetColorAndOpacity().GetSpecifiedColor().Equals(ExpectedTextColor, 0.0001f));
	TestTrue(TEXT("Second text block color follows normalized typed node color"),
		LegacyWidget->GetColorAndOpacity().GetSpecifiedColor().Equals(ExpectedTextColor, 0.0001f));
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
	const FFloatProperty* EditableTextBoxMinimumDesiredWidthProperty = FindFProperty<FFloatProperty>(UEditableTextBox::StaticClass(), TEXT("MinimumDesiredWidth"));
	const FFloatProperty* EditableTextMinimumDesiredWidthProperty = FindFProperty<FFloatProperty>(UEditableText::StaticClass(), TEXT("MinimumDesiredWidth"));
	TestNotNull(TEXT("EditableTextBox MinimumDesiredWidth property exists"), EditableTextBoxMinimumDesiredWidthProperty);
	TestNotNull(TEXT("EditableText MinimumDesiredWidth property exists"), EditableTextMinimumDesiredWidthProperty);
	if (!EditableTextBoxMinimumDesiredWidthProperty || !EditableTextMinimumDesiredWidthProperty)
	{
		return false;
	}

	FUMGWidgetNode Node;
	Node.Text.Text = TEXT("Editable text");
	Node.Text.HintText = TEXT("Editable hint");
	Node.Text.FontFamily = TEXT("Geist");
	Node.Text.TypefaceFontName = TEXT("Regular");
	Node.Text.IconName = TEXT("glyph_edit");
	Node.Text.Justification = TEXT("Center");
	Node.Text.Color = FLinearColor(0.3f, 0.5f, 0.7f, 1.0f);
	Node.TextStyle.FontSize = 19.0f;
	Node.TextStyle.SourceFontSize = 19.0f;
	Node.TextStyle.LetterSpacing = 1.25f;
	Node.WidgetStyle.MinDesiredWidth = 123.5f;

	UEditableTextBox* TypedTextBox = NewObject<UEditableTextBox>(GetTransientPackageAsObject());
	UEditableTextBox* LegacyTextBox = NewObject<UEditableTextBox>(GetTransientPackageAsObject());
	TestNotNull(TEXT("Typed editable text box created"), TypedTextBox);
	TestNotNull(TEXT("Legacy editable text box created"), LegacyTextBox);
	if (!TypedTextBox || !LegacyTextBox)
	{
		return false;
	}

	FUMGWidgetUtils::ApplyWidgetProperties(TypedTextBox, Node);
	FUMGWidgetUtils::ApplyWidgetProperties(LegacyTextBox, Node);

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
	const FLinearColor ExpectedTextColor = UMGTextStyleApplication::NormalizeTypedTextColor(Node.Text.Color.GetValue());
	TestTrue(TEXT("EditableTextBox typed text color follows normalized typed node color"),
		TypedTextBoxStyle.TextStyle.ColorAndOpacity.GetSpecifiedColor().Equals(ExpectedTextColor, 0.0001f));
	TestTrue(TEXT("EditableTextBox typed foreground color follows normalized typed node color"),
		TypedTextBoxStyle.ForegroundColor.GetSpecifiedColor().Equals(ExpectedTextColor, 0.0001f));
	TestTrue(TEXT("Second EditableTextBox text color follows normalized typed node color"),
		LegacyTextBoxStyle.TextStyle.ColorAndOpacity.GetSpecifiedColor().Equals(ExpectedTextColor, 0.0001f));
	TestTrue(TEXT("Second EditableTextBox foreground color follows normalized typed node color"),
		LegacyTextBoxStyle.ForegroundColor.GetSpecifiedColor().Equals(ExpectedTextColor, 0.0001f));
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
	FUMGWidgetUtils::ApplyWidgetProperties(LegacyEditableText, Node);

	TestEqual(TEXT("EditableText typed and legacy text match"), TypedEditableText->GetText().ToString(), LegacyEditableText->GetText().ToString());
	TestEqual(TEXT("EditableText typed and legacy hint match"), TypedEditableText->GetHintText().ToString(), LegacyEditableText->GetHintText().ToString());
	TestEqual(TEXT("EditableText typed and legacy font size match"), TypedEditableText->GetFont().Size, LegacyEditableText->GetFont().Size);
	TestEqual(TEXT("EditableText typed and legacy typeface match"), TypedEditableText->GetFont().TypefaceFontName, LegacyEditableText->GetFont().TypefaceFontName);
	TestEqual(TEXT("EditableText typed and legacy letter spacing match"), TypedEditableText->GetFont().LetterSpacing, LegacyEditableText->GetFont().LetterSpacing);
	TestEqual(TEXT("EditableText typed and legacy font object path match"),
		TypedEditableText->GetFont().FontObject ? TypedEditableText->GetFont().FontObject->GetPathName() : FString(),
		LegacyEditableText->GetFont().FontObject ? LegacyEditableText->GetFont().FontObject->GetPathName() : FString());
	TestTrue(TEXT("EditableText typed color follows normalized typed node color"),
		TypedEditableText->WidgetStyle.ColorAndOpacity.GetSpecifiedColor().Equals(ExpectedTextColor, 0.0001f));
	TestTrue(TEXT("Second EditableText color follows normalized typed node color"),
		LegacyEditableText->WidgetStyle.ColorAndOpacity.GetSpecifiedColor().Equals(ExpectedTextColor, 0.0001f));
	TestEqual(TEXT("EditableText typed and legacy justification match"), TypedEditableText->GetJustification(), LegacyEditableText->GetJustification());
	TestTrue(TEXT("EditableText typed and legacy icon shaping match"),
		TypedEditableText->ShapedTextOptions == LegacyEditableText->ShapedTextOptions);

	UEditableTextBox* TypedBehaviorTextBox = NewObject<UEditableTextBox>(GetTransientPackageAsObject());
	UEditableText* TypedBehaviorEditableText = NewObject<UEditableText>(GetTransientPackageAsObject());
	TestNotNull(TEXT("Typed behavior editable text box created"), TypedBehaviorTextBox);
	TestNotNull(TEXT("Typed behavior editable text created"), TypedBehaviorEditableText);
	if (!TypedBehaviorTextBox || !TypedBehaviorEditableText)
	{
		return false;
	}

	FUMGWidgetUtils::ApplyWidgetProperties(TypedBehaviorTextBox, Node);
	FUMGWidgetUtils::ApplyWidgetProperties(TypedBehaviorEditableText, Node);

	TestTrue(TEXT("Typed editable text box applies MinimumDesiredWidth"),
		FMath::IsNearlyEqual(
			EditableTextBoxMinimumDesiredWidthProperty->GetPropertyValue_InContainer(TypedBehaviorTextBox),
			123.5f,
			0.001f));
	TestTrue(TEXT("Typed editable text applies MinimumDesiredWidth"),
		FMath::IsNearlyEqual(
			EditableTextMinimumDesiredWidthProperty->GetPropertyValue_InContainer(TypedBehaviorEditableText),
			123.5f,
			0.001f));

	FUMGWidgetNode EmptyTypedNode;
	EmptyTypedNode.Text.Text = TEXT("");
	EmptyTypedNode.Text.HintText = TEXT("");

	TestTrue(TEXT("Typed empty text is retained instead of dropped"),
		EmptyTypedNode.Text.Text.IsSet() && EmptyTypedNode.Text.Text.GetValue().IsEmpty());
	TestTrue(TEXT("Typed empty hint is retained instead of dropped"),
		EmptyTypedNode.Text.HintText.IsSet() && EmptyTypedNode.Text.HintText.GetValue().IsEmpty());

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

	FUMGWidgetUtils::ApplyWidgetProperties(EmptyTypedTextBox, EmptyTypedNode);
	FUMGWidgetUtils::ApplyWidgetProperties(EmptyTypedEditableText, EmptyTypedNode);

	TestTrue(TEXT("Typed empty text clears EditableTextBox text"), EmptyTypedTextBox->GetText().ToString().IsEmpty());
	TestTrue(TEXT("Typed empty hint clears EditableTextBox hint"), EmptyTypedTextBox->GetHintText().ToString().IsEmpty());
	TestTrue(TEXT("Typed empty text clears EditableText text"), EmptyTypedEditableText->GetText().ToString().IsEmpty());
	TestTrue(TEXT("Typed empty hint clears EditableText hint"), EmptyTypedEditableText->GetHintText().ToString().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextTypedPayloadExplicitFalseFormattingFlagsTest,
	"Design2UMG.PropertyApplier.TextExplicitFalseFormattingFlags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextTypedPayloadExplicitFalseFormattingFlagsTest::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Node;
	Node.Type = TEXT("TextBlock");
	Node.Text.bAutoWrapText = false;
	Node.Text.bUnderline = false;
	Node.Text.bStrikethrough = false;

	TestTrue(TEXT("Typed text payload keeps explicit false AutoWrapText"),
		Node.Text.bAutoWrapText.IsSet() && !Node.Text.bAutoWrapText.GetValue());
	TestTrue(TEXT("Typed text payload keeps explicit false Underline"),
		Node.Text.bUnderline.IsSet() && !Node.Text.bUnderline.GetValue());
	TestTrue(TEXT("Typed text payload keeps explicit false Strikethrough"),
		Node.Text.bStrikethrough.IsSet() && !Node.Text.bStrikethrough.GetValue());

	UTextBlock* TypedWidget = NewObject<UTextBlock>(GetTransientPackageAsObject());
	UTextBlock* PayloadWidget = NewObject<UTextBlock>(GetTransientPackageAsObject());
	TestNotNull(TEXT("Typed text widget created"), TypedWidget);
	TestNotNull(TEXT("Payload text widget created"), PayloadWidget);
	if (!TypedWidget || !PayloadWidget)
	{
		return false;
	}

	TypedWidget->SetAutoWrapText(true);
	FUMGWidgetUtils::ApplyWidgetProperties(TypedWidget, Node);
	TestFalse(TEXT("Typed text widget applies explicit false AutoWrapText"), TypedWidget->GetAutoWrapText());

	PayloadWidget->SetAutoWrapText(true);
	FUMGWidgetUtils::ApplyWidgetProperties(PayloadWidget, Node);
	TestFalse(TEXT("Payload text widget applies explicit false AutoWrapText"), PayloadWidget->GetAutoWrapText());
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
	Node.Text.HintText = TEXT("Multi-line hint");
	Node.Text.FontFamily = TEXT("Geist");
	Node.Text.TypefaceFontName = TEXT("Regular");
	Node.Text.IconName = TEXT("glyph_multiline");
	Node.Text.Justification = TEXT("Right");
	Node.Text.bAutoWrapText = true;
	Node.Text.WrapTextAt = 220.0f;
	Node.Text.LineHeightPercentage = 1.4f;
	Node.Text.Color = FLinearColor(0.15f, 0.55f, 0.35f, 1.0f);
	Node.TextStyle.FontSize = 17.0f;
	Node.TextStyle.SourceFontSize = 17.0f;
	Node.TextStyle.LetterSpacing = 0.75f;
	Node.WidgetStyle.Padding = FMargin(4.0f, 5.0f, 6.0f, 7.0f);

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
	FUMGWidgetUtils::ApplyWidgetProperties(LegacyTextBox, Node);

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
	const FLinearColor ExpectedTextColor = UMGTextStyleApplication::NormalizeTypedTextColor(Node.Text.Color.GetValue());
	TestTrue(TEXT("MultiLineEditableTextBox typed text color follows normalized typed node color"),
		TypedTextBox->WidgetStyle.TextStyle.ColorAndOpacity.GetSpecifiedColor().Equals(ExpectedTextColor, 0.0001f));
	TestTrue(TEXT("MultiLineEditableTextBox typed foreground color follows normalized typed node color"),
		TypedTextBox->WidgetStyle.ForegroundColor.GetSpecifiedColor().Equals(ExpectedTextColor, 0.0001f));
	TestTrue(TEXT("Second MultiLineEditableTextBox text color follows normalized typed node color"),
		LegacyTextBox->WidgetStyle.TextStyle.ColorAndOpacity.GetSpecifiedColor().Equals(ExpectedTextColor, 0.0001f));
	TestTrue(TEXT("Second MultiLineEditableTextBox foreground color follows normalized typed node color"),
		LegacyTextBox->WidgetStyle.ForegroundColor.GetSpecifiedColor().Equals(ExpectedTextColor, 0.0001f));
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
	FUMGWidgetUtils::ApplyWidgetProperties(LegacyEditableText, Node);

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
	TestTrue(TEXT("MultiLineEditableText typed color follows normalized typed node color"),
		TypedEditableText->WidgetStyle.ColorAndOpacity.GetSpecifiedColor().Equals(ExpectedTextColor, 0.0001f));
	TestTrue(TEXT("Second MultiLineEditableText color follows normalized typed node color"),
		LegacyEditableText->WidgetStyle.ColorAndOpacity.GetSpecifiedColor().Equals(ExpectedTextColor, 0.0001f));
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
	Node.Text.FontFamily = TEXT("Geist");
	Node.Text.TypefaceFontName = TEXT("Regular");
	Node.Text.Justification = TEXT("Center");
	Node.Text.bAutoWrapText = true;
	Node.Text.WrapTextAt = 180.0f;
	Node.Text.LineHeightPercentage = 1.25f;
	Node.Text.Color = FLinearColor(0.1f, 0.2f, 0.3f, 1.0f);
	Node.Text.RichTextStyleRowsJson = TEXT("{\"d2u_run_0\":{\"Color\":\"#112233FF\"},\"d2u_run_1\":{\"FontSize\":22.0,\"DesignFontSize\":22.0,\"Color\":\"#224466FF\",\"Underline\":true,\"Href\":\"https://docs.example.com\"}}");
	Node.TextStyle.FontSize = 18.0f;
	Node.TextStyle.SourceFontSize = 18.0f;
	Node.TextStyle.LetterSpacing = 1.5f;
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
	const FLinearColor ExpectedDefaultColor = UMGTextStyleApplication::NormalizeTypedTextColor(Node.Text.Color.GetValue());
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
	FUMGWidgetUtils::ApplyWidgetProperties(LegacyWidget, Node);

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
	TestTrue(TEXT("Typed rich text default color follows normalized typed node color"),
		DefaultStyle.ColorAndOpacity.GetSpecifiedColor().Equals(ExpectedDefaultColor, 0.0001f));
	TestTrue(TEXT("Second rich text default color follows normalized typed node color"),
		LegacyDefaultStyle.ColorAndOpacity.GetSpecifiedColor().Equals(ExpectedDefaultColor, 0.0001f));
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
	SourceNode.Text.HintText = TEXT("Round-trip hint");
	SourceNode.Text.FontFamily = TEXT("/Game/Design2UMG/Fonts/Geist.Geist");
	SourceNode.Text.TypefaceFontName = TEXT("Regular");
	SourceNode.Text.Justification = TEXT("Center");
	SourceNode.Text.bAutoWrapText = true;
	SourceNode.Text.WrapTextAt = 240.0f;
	SourceNode.Text.LineHeightPercentage = 1.3f;
	SourceNode.Text.Color = FLinearColor(0.25f, 0.45f, 0.65f, 1.0f);
	SourceNode.Text.IconName = TEXT("glyph_roundtrip");
	SourceNode.TextStyle.FontSize = 18.0f;
	SourceNode.TextStyle.SourceFontSize = 18.0f;
	SourceNode.TextStyle.LetterSpacing = 1.25f;
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
	TestTrue(TEXT("Parsed auto wrap flag preserved"), ParsedNode->Text.bAutoWrapText.IsSet() && ParsedNode->Text.bAutoWrapText.GetValue() == SourceNode.Text.bAutoWrapText.GetValue());
	TestTrue(TEXT("Parsed wrap width preserved"), ParsedNode->Text.WrapTextAt.IsSet() && FMath::IsNearlyEqual(ParsedNode->Text.WrapTextAt.GetValue(), SourceNode.Text.WrapTextAt.GetValue(), 0.001f));
	TestTrue(TEXT("Parsed line height preserved"), ParsedNode->Text.LineHeightPercentage.IsSet() && FMath::IsNearlyEqual(ParsedNode->Text.LineHeightPercentage.GetValue(), SourceNode.Text.LineHeightPercentage.GetValue(), 0.001f));
	TestTrue(TEXT("Parsed color preserved"), ParsedNode->Text.Color.IsSet() && ParsedNode->Text.Color.GetValue().Equals(SourceNode.Text.Color.GetValue(), 0.01f));
	TestTrue(TEXT("Parsed font size preserved"), FMath::IsNearlyEqual(ParsedNode->TextStyle.FontSize, SourceNode.TextStyle.FontSize, 0.001f));
	TestTrue(TEXT("Parsed source font size preserved"), FMath::IsNearlyEqual(ParsedNode->TextStyle.SourceFontSize, SourceNode.TextStyle.SourceFontSize, 0.001f));
	TestTrue(TEXT("Parsed letter spacing preserved"),
		ParsedNode->TextStyle.LetterSpacing.IsSet()
		&& FMath::IsNearlyEqual(ParsedNode->TextStyle.LetterSpacing.GetValue(), SourceNode.TextStyle.LetterSpacing.GetValue(), 0.001f));
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
	const FLinearColor ExpectedSourceTextColor = UMGTextStyleApplication::NormalizeTypedTextColor(SourceNode.Text.Color.GetValue());
	const FLinearColor ExpectedParsedTextColor = UMGTextStyleApplication::NormalizeTypedTextColor(ParsedNode->Text.Color.GetValue());
	TestTrue(TEXT("Source widget text color follows normalized source node color"),
		SourceWidget->WidgetStyle.TextStyle.ColorAndOpacity.GetSpecifiedColor().Equals(ExpectedSourceTextColor, 0.0001f));
	TestTrue(TEXT("Parsed widget text color follows normalized parsed node color"),
		ParsedWidget->WidgetStyle.TextStyle.ColorAndOpacity.GetSpecifiedColor().Equals(ExpectedParsedTextColor, 0.0001f));
	TestTrue(TEXT("Source widget foreground color follows normalized source node color"),
		SourceWidget->WidgetStyle.ForegroundColor.GetSpecifiedColor().Equals(ExpectedSourceTextColor, 0.0001f));
	TestTrue(TEXT("Parsed widget foreground color follows normalized parsed node color"),
		ParsedWidget->WidgetStyle.ForegroundColor.GetSpecifiedColor().Equals(ExpectedParsedTextColor, 0.0001f));
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextStyleLetterSpacingOptionalSemanticsTest,
	"Design2UMG.UMGJson.TextStyleLetterSpacingOptionalSemantics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextStyleLetterSpacingOptionalSemanticsTest::RunTest(const FString& Parameters)
{
	auto SerializeSingleNode = [](const FUMGWidgetNode& Node)
	{
		FUMGWidgetData Data;
		Data.WidgetName = TEXT("WBP_LetterSpacingOptionalSemantics");
		Data.RootWidget.Type = TEXT("CanvasPanel");
		Data.RootWidget.Name = TEXT("Root");
		Data.RootWidget.Children.Add(Node);
		return FUMGJsonSerializer::Serialize(Data);
	};

	auto TryReadSerializedLetterSpacing = [](const FString& Json, float& OutValue)
	{
		TSharedPtr<FJsonObject> RootObject;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
		{
			return false;
		}

		const TSharedPtr<FJsonObject>* RootWidgetObject = nullptr;
		if (!RootObject->TryGetObjectField(TEXT("RootWidget"), RootWidgetObject))
		{
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
		if (!(*RootWidgetObject)->TryGetArrayField(TEXT("Children"), Children) || Children->Num() != 1)
		{
			return false;
		}

		const TSharedPtr<FJsonObject> ChildObject = (*Children)[0]->AsObject();
		if (!ChildObject.IsValid())
		{
			return false;
		}

		const TSharedPtr<FJsonObject>* TextStyleObject = nullptr;
		if (!ChildObject->TryGetObjectField(TEXT("TextStyle"), TextStyleObject))
		{
			return false;
		}

		double NumberValue = 0.0;
		if (!(*TextStyleObject)->TryGetNumberField(UMGPropertyKeys::LetterSpacing, NumberValue))
		{
			return false;
		}

		OutValue = static_cast<float>(NumberValue);
		return true;
	};

	auto ParseSingleNode = [&](const FString& Json, const TCHAR* Id, FUMGWidgetData& OutParsedData)
	{
		FString ParseError;
		TestTrue(TEXT("Letter spacing JSON parses"), FUMGJsonParser::ParseJson(Json, OutParsedData, &ParseError));
		if (!ParseError.IsEmpty())
		{
			AddInfo(ParseError);
		}
		return FindWidgetById(OutParsedData.RootWidget, Id);
	};

	FUMGWidgetNode ExplicitZeroNode;
	ExplicitZeroNode.Id = TEXT("explicit_zero_spacing");
	ExplicitZeroNode.Type = TEXT("TextBlock");
	ExplicitZeroNode.Name = TEXT("ExplicitZeroSpacing");
	ExplicitZeroNode.TextStyle.FontSize = 20.0f;
	ExplicitZeroNode.TextStyle.SourceFontSize = 20.0f;
	ExplicitZeroNode.TextStyle.LetterSpacing = 0.0f;

	FUMGWidgetNode NonZeroNode = ExplicitZeroNode;
	NonZeroNode.Id = TEXT("non_zero_spacing");
	NonZeroNode.Name = TEXT("NonZeroSpacing");
	NonZeroNode.TextStyle.LetterSpacing = 1.25f;

	FUMGWidgetNode UnsetNode = ExplicitZeroNode;
	UnsetNode.Id = TEXT("unset_spacing");
	UnsetNode.Name = TEXT("UnsetSpacing");
	UnsetNode.TextStyle.LetterSpacing.Reset();

	const FString ExplicitZeroJson = SerializeSingleNode(ExplicitZeroNode);
	float SerializedLetterSpacing = -1.0f;
	TestTrue(TEXT("Explicit zero letter spacing serializes"), TryReadSerializedLetterSpacing(ExplicitZeroJson, SerializedLetterSpacing));
	TestTrue(TEXT("Explicit zero letter spacing value preserved in JSON"), FMath::IsNearlyZero(SerializedLetterSpacing));

	FUMGWidgetData ParsedExplicitZeroData;
	const FUMGWidgetNode* ParsedExplicitZeroNode = ParseSingleNode(ExplicitZeroJson, TEXT("explicit_zero_spacing"), ParsedExplicitZeroData);
	TestNotNull(TEXT("Parsed explicit zero node exists"), ParsedExplicitZeroNode);
	TestTrue(TEXT("Parsed explicit zero letter spacing remains set"),
		ParsedExplicitZeroNode
		&& ParsedExplicitZeroNode->TextStyle.LetterSpacing.IsSet()
		&& FMath::IsNearlyZero(ParsedExplicitZeroNode->TextStyle.LetterSpacing.GetValue()));

	TestTrue(TEXT("Typed payload keeps explicit zero letter spacing"),
		ExplicitZeroNode.TextStyle.LetterSpacing.IsSet()
		&& FMath::IsNearlyZero(ExplicitZeroNode.TextStyle.LetterSpacing.GetValue()));

	UTextBlock* ExplicitZeroWidget = NewObject<UTextBlock>(GetTransientPackageAsObject());
	TestNotNull(TEXT("Explicit zero widget created"), ExplicitZeroWidget);
	if (ExplicitZeroWidget)
	{
		FSlateFontInfo FontInfo = ExplicitZeroWidget->GetFont();
		FontInfo.LetterSpacing = 321;
		ExplicitZeroWidget->SetFont(FontInfo);
		FUMGWidgetUtils::ApplyWidgetProperties(ExplicitZeroWidget, ExplicitZeroNode);
		TestEqual(TEXT("Explicit zero letter spacing applies"), ExplicitZeroWidget->GetFont().LetterSpacing, 0);
	}

	const FString NonZeroJson = SerializeSingleNode(NonZeroNode);
	TestTrue(TEXT("Non-zero letter spacing serializes"), TryReadSerializedLetterSpacing(NonZeroJson, SerializedLetterSpacing));
	TestTrue(TEXT("Non-zero letter spacing value preserved in JSON"), FMath::IsNearlyEqual(SerializedLetterSpacing, 1.25f, 0.001f));

	FUMGWidgetData ParsedNonZeroData;
	const FUMGWidgetNode* ParsedNonZeroNode = ParseSingleNode(NonZeroJson, TEXT("non_zero_spacing"), ParsedNonZeroData);
	TestNotNull(TEXT("Parsed non-zero node exists"), ParsedNonZeroNode);
	TestTrue(TEXT("Parsed non-zero letter spacing remains set"),
		ParsedNonZeroNode
		&& ParsedNonZeroNode->TextStyle.LetterSpacing.IsSet()
		&& FMath::IsNearlyEqual(ParsedNonZeroNode->TextStyle.LetterSpacing.GetValue(), 1.25f, 0.001f));

	const FString UnsetJson = SerializeSingleNode(UnsetNode);
	TestFalse(TEXT("Unset letter spacing does not serialize"), TryReadSerializedLetterSpacing(UnsetJson, SerializedLetterSpacing));

	FUMGWidgetData ParsedUnsetData;
	const FUMGWidgetNode* ParsedUnsetNode = ParseSingleNode(UnsetJson, TEXT("unset_spacing"), ParsedUnsetData);
	TestNotNull(TEXT("Parsed unset node exists"), ParsedUnsetNode);
	TestTrue(TEXT("Parsed unset letter spacing remains unset"),
		ParsedUnsetNode
		&& !ParsedUnsetNode->TextStyle.LetterSpacing.IsSet());

	TestFalse(TEXT("Typed payload omits unset letter spacing"), UnsetNode.TextStyle.LetterSpacing.IsSet());

	UTextBlock* UnsetWidget = NewObject<UTextBlock>(GetTransientPackageAsObject());
	TestNotNull(TEXT("Unset widget created"), UnsetWidget);
	if (UnsetWidget)
	{
		FSlateFontInfo FontInfo = UnsetWidget->GetFont();
		FontInfo.LetterSpacing = 321;
		UnsetWidget->SetFont(FontInfo);
		FUMGWidgetUtils::ApplyWidgetProperties(UnsetWidget, UnsetNode);
		TestEqual(TEXT("Unset letter spacing does not apply"), UnsetWidget->GetFont().LetterSpacing, 321);
	}

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
	SourceNode.Text.RichTextStyleRowsJson = TEXT("{\"d2u_run_0\":{\"Color\":\"#112233FF\",\"DesignFontSize\":18.0,\"FontFamily\":\"/Game/Design2UMG/Fonts/Geist.Geist\",\"FontSize\":27.0,\"TypefaceFontName\":\"Regular\"},\"d2u_run_1\":{\"Color\":\"#224466FF\",\"DesignFontSize\":18.0,\"FontFamily\":\"/Game/Design2UMG/Fonts/Geist.Geist\",\"FontSize\":27.0,\"Href\":\"https://docs.example.com\",\"TypefaceFontName\":\"Bold\",\"Underline\":true}}");
	SourceNode.Text.FontFamily = TEXT("/Game/Design2UMG/Fonts/Geist.Geist");
	SourceNode.Text.TypefaceFontName = TEXT("Regular");
	SourceNode.Text.Justification = TEXT("Center");
	SourceNode.Text.bAutoWrapText = true;
	SourceNode.Text.WrapTextAt = 180.0f;
	SourceNode.Text.LineHeightPercentage = 1.25f;
	SourceNode.Text.Color = FLinearColor(0.1f, 0.2f, 0.3f, 1.0f);
	SourceNode.TextStyle.FontSize = 18.0f;
	SourceNode.TextStyle.SourceFontSize = 18.0f;
	SourceNode.TextStyle.LetterSpacing = 1.5f;
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
	TestTrue(TEXT("Parsed rich text wrap width preserved"), ParsedNode->Text.WrapTextAt.IsSet() && FMath::IsNearlyEqual(ParsedNode->Text.WrapTextAt.GetValue(), SourceNode.Text.WrapTextAt.GetValue(), 0.001f));
	TestTrue(TEXT("Parsed rich text line height preserved"), ParsedNode->Text.LineHeightPercentage.IsSet() && FMath::IsNearlyEqual(ParsedNode->Text.LineHeightPercentage.GetValue(), SourceNode.Text.LineHeightPercentage.GetValue(), 0.001f));
	TestTrue(TEXT("Parsed rich text font size preserved"), FMath::IsNearlyEqual(ParsedNode->TextStyle.FontSize, SourceNode.TextStyle.FontSize, 0.001f));
	TestTrue(TEXT("Parsed rich text source font size preserved"), FMath::IsNearlyEqual(ParsedNode->TextStyle.SourceFontSize, SourceNode.TextStyle.SourceFontSize, 0.001f));
	TestTrue(TEXT("Parsed rich text letter spacing preserved"),
		ParsedNode->TextStyle.LetterSpacing.IsSet()
		&& FMath::IsNearlyEqual(ParsedNode->TextStyle.LetterSpacing.GetValue(), SourceNode.TextStyle.LetterSpacing.GetValue(), 0.001f));

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTypedWidgetAccessEnumeratesLateTypedKeysTest,
	"Design2UMG.WidgetNodeAccess.TypedAccessEnumeratesLateTypedKeys",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTypedWidgetAccessEnumeratesLateTypedKeysTest::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Node;
	Node.WidgetState.ActiveWidgetIndex = 2;
	Node.Render.RenderTranslation = FVector2D(1.5f, -2.0f);

	int32 ActiveWidgetIndex = INDEX_NONE;
	TestTrue(TEXT("Typed access keeps active widget index"),
		Design2UMGWidgetNodeAccess::TryGetWidgetInt(Node, UMGPropertyKeys::ActiveWidgetIndex, ActiveWidgetIndex));
	TestEqual(TEXT("Typed access active widget index value"), ActiveWidgetIndex, 2);

	FVector2D RenderTranslation = FVector2D::ZeroVector;
	TestTrue(TEXT("Typed access keeps render translation"),
		Design2UMGWidgetNodeAccess::TryGetWidgetVector2D(Node, UMGPropertyKeys::RenderTranslation, RenderTranslation));
	TestTrue(TEXT("Typed access render translation value"), RenderTranslation.Equals(FVector2D(1.5f, -2.0f), 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTypedWidgetAccessEnumeratesDescriptorBackedBrushKeysTest,
	"Design2UMG.WidgetNodeAccess.TypedAccessEnumeratesDescriptorBackedBrushKeys",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTypedWidgetAccessEnumeratesDescriptorBackedBrushKeysTest::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Node;
	Node.Brush.Image.Mode = TEXT("Crop");
	FUMGVisualBrushData::FImageTransformData Transform;
	Transform.A = 1.0f;
	Transform.B = 0.25f;
	Transform.TX = 0.1f;
	Transform.C = -0.05f;
	Transform.D = 1.0f;
	Transform.TY = 0.2f;
	Node.Brush.Image.Transform = Transform;
	Node.Brush.Gradient.Type = TEXT("Angular");
	Node.Brush.Gradient.Rotation = 45.0f;
	Node.Brush.Gradient.Stops = {
		{ FLinearColor::Red, 0.0f },
		{ FLinearColor::Blue, 1.0f },
	};

	FString ImageMode;
	TestTrue(TEXT("Typed access keeps image mode"),
		Design2UMGWidgetNodeAccess::TryGetWidgetString(Node, UMGPropertyKeys::ImageMode, ImageMode));
	TestEqual(TEXT("Typed access image mode value"), ImageMode, FString(TEXT("Crop")));

	FString ImageTransform;
	TestTrue(TEXT("Typed access keeps image transform"),
		Design2UMGWidgetNodeAccess::TryGetWidgetString(Node, UMGPropertyKeys::ImageTransform, ImageTransform));
	TestEqual(
		TEXT("Typed access image transform value"),
		ImageTransform,
		Design2UMGWidgetNodeAccess::SerializeImageTransform(Node.Brush.Image.Transform.GetValue()));

	FString MaterialRef;
	TestTrue(TEXT("Typed access keeps gradient material ref"),
		Design2UMGWidgetNodeAccess::TryGetWidgetString(Node, UMGPropertyKeys::MaterialRef, MaterialRef));
	TestEqual(TEXT("Typed access gradient material ref value"), MaterialRef, FString(TEXT("angulargradient")));

	FString MaterialParams;
	TestTrue(TEXT("Typed access keeps gradient material params"),
		Design2UMGWidgetNodeAccess::TryGetWidgetString(Node, UMGPropertyKeys::MaterialParams, MaterialParams));
	TestTrue(TEXT("Typed access gradient material params carry rotation"), MaterialParams.Contains(TEXT("\"Rotation\":45.0000")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrushDescriptorPrecedenceContractTest,
	"Design2UMG.WidgetNodeAccess.BrushDescriptorPrecedenceContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBrushDescriptorPrecedenceContractTest::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Node;
	Node.Brush.MaterialRef = TEXT("roundedrect");
	Node.Brush.TexturePath = TEXT("/Game/UI/T_LegacyBrush.T_LegacyBrush");
	Node.Brush.Image.TexturePath = TEXT("/Game/UI/T_DescriptorBrush.T_DescriptorBrush");
	Node.Brush.Gradient.Type = TEXT("Angular");
	Node.Brush.Gradient.Rotation = 15.0f;
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

	Node.Brush.Gradient = FUMGVisualBrushData::FGradientData();
	TestTrue(TEXT("Explicit material ref remains after removing gradient"), Design2UMGWidgetNodeAccess::TryGetBrushTypedWidgetString(Node, UMGPropertyKeys::MaterialRef, MaterialRef));
	TestEqual(TEXT("Explicit material ref becomes fallback owner"), MaterialRef, FString(TEXT("roundedrect")));
	TestTrue(TEXT("Public getter falls back to explicit material ref once gradient is removed"), Design2UMGWidgetNodeAccess::TryGetWidgetString(Node, UMGPropertyKeys::MaterialRef, PublicMaterialRef));
	TestEqual(TEXT("Public getter explicit material ref matches typed fallback"), PublicMaterialRef, MaterialRef);

	FUMGVisualBrushData LegacyOnlyBrush;
	LegacyOnlyBrush.TexturePath = TEXT("/Game/UI/T_LegacyBrush.T_LegacyBrush");
	FString NormalizationDiagnostic;
	TestTrue(TEXT("Legacy/root texture path normalizes into canonical image owner"),
		Design2UMGWidgetNodeAccess::NormalizeBrushTexturePathOwner(LegacyOnlyBrush, &NormalizationDiagnostic));
	TestTrue(TEXT("Legacy/root normalization records no conflict diagnostic"), NormalizationDiagnostic.IsEmpty());
	TestTrue(TEXT("Normalized texture path moved into image owner"),
		LegacyOnlyBrush.Image.TexturePath.IsSet() && LegacyOnlyBrush.Image.TexturePath.GetValue() == TEXT("/Game/UI/T_LegacyBrush.T_LegacyBrush"));
	TestTrue(TEXT("Normalization clears legacy/root owner"), LegacyOnlyBrush.TexturePath.IsEmpty());

	TestTrue(TEXT("Typed access keeps explicit material ref after gradient removal"),
		Design2UMGWidgetNodeAccess::TryGetWidgetString(Node, UMGPropertyKeys::MaterialRef, PublicMaterialRef));
	TestEqual(TEXT("Typed access explicit material ref matches typed fallback"), PublicMaterialRef, MaterialRef);

	FUMGWidgetNode LegacyOnlyNode;
	LegacyOnlyNode.Brush = LegacyOnlyBrush;
	FString NormalizedTexturePath;
	TestTrue(TEXT("Typed access keeps normalized canonical texture path"),
		Design2UMGWidgetNodeAccess::TryGetWidgetString(LegacyOnlyNode, UMGPropertyKeys::TexturePath, NormalizedTexturePath));
	TestEqual(TEXT("Typed access normalized texture path matches canonical owner"), NormalizedTexturePath, FString(TEXT("/Game/UI/T_LegacyBrush.T_LegacyBrush")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrushDescriptorExplicitEmptyMaterialRefTest,
	"Design2UMG.WidgetNodeAccess.BrushDescriptorExplicitEmptyMaterialRef",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBrushDescriptorExplicitEmptyMaterialRefTest::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Node;
	Node.Type = TEXT("Image");
	Node.Name = TEXT("EmptyMaterialRef");
	Node.Brush.MaterialRef = TEXT("");

	FString MaterialRef;
	TestTrue(TEXT("Explicit empty material ref is queryable"),
		Design2UMGWidgetNodeAccess::TryGetBrushTypedWidgetString(Node, UMGPropertyKeys::MaterialRef, MaterialRef));
	TestTrue(TEXT("Explicit empty material ref stays empty"), MaterialRef.IsEmpty());

	FString PublicMaterialRef;
	TestTrue(TEXT("Public getter returns explicit empty material ref"),
		Design2UMGWidgetNodeAccess::TryGetWidgetString(Node, UMGPropertyKeys::MaterialRef, PublicMaterialRef));
	TestTrue(TEXT("Public getter keeps explicit empty material ref empty"), PublicMaterialRef.IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrushDescriptorExplicitEmptyImageTexturePathTest,
	"Design2UMG.WidgetNodeAccess.BrushDescriptorExplicitEmptyImageTexturePath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBrushDescriptorExplicitEmptyImageTexturePathTest::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Node;
	Node.Type = TEXT("Image");
	Node.Name = TEXT("EmptyImageTexturePath");
	Node.Brush.MaterialRef = TEXT("roundedrect");
	Node.Brush.Image.TexturePath = TEXT("");

	FString TexturePath;
	TestTrue(TEXT("Explicit empty image texture path is queryable"),
		Design2UMGWidgetNodeAccess::TryGetBrushTypedWidgetString(Node, UMGPropertyKeys::TexturePath, TexturePath));
	TestTrue(TEXT("Explicit empty image texture path stays empty"), TexturePath.IsEmpty());

	FString PublicTexturePath;
	TestTrue(TEXT("Public getter returns explicit empty image texture path"),
		Design2UMGWidgetNodeAccess::TryGetWidgetString(Node, UMGPropertyKeys::TexturePath, PublicTexturePath));
	TestTrue(TEXT("Public getter keeps explicit empty image texture path empty"), PublicTexturePath.IsEmpty());

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
	TestTrue(TEXT("Parser clears legacy/root texture owner after normalization"), ParsedData.RootWidget.Brush.TexturePath.IsEmpty());
	TestEqual(TEXT("Typed accessor exposes canonical image texture path"),
		GetWidgetProperty(ParsedData.RootWidget, TEXT("TexturePath")),
		FString(TEXT("/Game/UI/T_CanonicalBrush.T_CanonicalBrush")));
	TestTrue(TEXT("Conflict JSON preserves parsed crop transform"),
		ParsedData.RootWidget.Brush.Image.Transform.IsSet()
		&& FMath::IsNearlyEqual(ParsedData.RootWidget.Brush.Image.Transform.GetValue().A, 0.5f, 0.0001f));
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
	SourceData.RootWidget.Effect.EffectMaterial = TEXT("d2u.retainer.inner_shadow");
	SourceData.RootWidget.Effect.TextureParameter = TEXT("Texture");
	SourceData.RootWidget.Effect.EffectColor = FLinearColor(0.1f, 0.2f, 0.3f, 0.4f);
	SourceData.RootWidget.Effect.EffectOffset = FVector2D(0.05f, -0.03f);
	SourceData.RootWidget.Effect.EffectBlurUVStep = FVector2D(0.01f, 0.02f);

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

	TestEqual(TEXT("Parsed inner shadow effect material preserved"), ParsedData.RootWidget.Effect.EffectMaterial.GetValue(), TEXT("d2u.retainer.inner_shadow"));
	TestTrue(TEXT("Parsed inner shadow color preserved"), ParsedData.RootWidget.Effect.EffectColor.IsSet());
	TestTrue(TEXT("Parsed inner shadow offset preserved"), ParsedData.RootWidget.Effect.EffectOffset.IsSet());
	TestTrue(TEXT("Parsed inner shadow blur step preserved"), ParsedData.RootWidget.Effect.EffectBlurUVStep.IsSet());
	TestTrue(TEXT("Parsed inner shadow color alpha preserved"), FMath::IsNearlyEqual(ParsedData.RootWidget.Effect.EffectColor.GetValue().A, 0.4f, 0.0001f));
	TestTrue(TEXT("Parsed inner shadow offset X preserved"), FMath::IsNearlyEqual(ParsedData.RootWidget.Effect.EffectOffset.GetValue().X, 0.05f, 0.0001f));
	TestTrue(TEXT("Parsed inner shadow offset Y preserved"), FMath::IsNearlyEqual(ParsedData.RootWidget.Effect.EffectOffset.GetValue().Y, -0.03f, 0.0001f));
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

	UTexture2D* Texture = FProceduralShapeGenerator::GenerateFromMaterialRef(BuildProceduralRequestForTest(*this, *Widget),
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

	UTexture2D* Texture = FProceduralShapeGenerator::GenerateFromMaterialRef(BuildProceduralRequestForTest(*this, *Widget),
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
	TestTrue(TEXT("Figma radial center parsed"), GradientNode->Visual.Fill.Gradient.Center.IsSet()
		&& FMath::IsNearlyEqual(GradientNode->Visual.Fill.Gradient.Center.GetValue().X, 0.25f, 0.001f)
		&& FMath::IsNearlyEqual(GradientNode->Visual.Fill.Gradient.Center.GetValue().Y, 0.75f, 0.001f));
	TestTrue(TEXT("Figma radial size parsed"), GradientNode->Visual.Fill.Gradient.Size.IsSet()
		&& FMath::IsNearlyEqual(GradientNode->Visual.Fill.Gradient.Size.GetValue().X, 0.25f, 0.001f)
		&& FMath::IsNearlyEqual(GradientNode->Visual.Fill.Gradient.Size.GetValue().Y, 1.0f, 0.001f));
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

	UTexture2D* Texture = FProceduralShapeGenerator::GenerateFromMaterialRef(BuildProceduralRequestForTest(*this, *Widget),
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

	UTexture2D* Texture = FProceduralShapeGenerator::GenerateFromMaterialRef(BuildProceduralRequestForTest(*this, *ShadowWidget),
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
	FUMGProceduralResourceRequest LegacyShadowRequest;
	LegacyShadowRequest.MaterialRef = TEXT("boxshadow");
	LegacyShadowRequest.WidthOverride = 56.0f;
	LegacyShadowRequest.HeightOverride = 40.0f;
	LegacyShadowRequest.ShadowBlur = 8.0f;
	LegacyShadowRequest.MaterialParams = TEXT("{\"Color\":\"#000000FF\",\"CornerRadius\":0.0}");

	FUMGProceduralResourceRequest ExplicitPaddingShadowRequest = LegacyShadowRequest;
	ExplicitPaddingShadowRequest.MaterialParams = TEXT("{\"Color\":\"#000000FF\",\"CornerRadius\":0.0,\"BlurPadding\":8.0}");

	UTexture2D* LegacyTexture = FProceduralShapeGenerator::GenerateFromMaterialRef(
		LegacyShadowRequest,
		GetTransientPackageAsObject());
	UTexture2D* ExplicitPaddingTexture = FProceduralShapeGenerator::GenerateFromMaterialRef(
		ExplicitPaddingShadowRequest,
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
		Design2UMGWidgetNodeAccess::TryGetWidgetVector2D(*ShadowWidget, TEXT("RenderTranslation"), NegativeSpreadTranslation));
	TestTrue(TEXT("Negative spread translates back inward"), NegativeSpreadTranslation.Equals(FVector2D(6.0f, 6.0f), 0.001f));

	UTexture2D* Texture = FProceduralShapeGenerator::GenerateFromMaterialRef(BuildProceduralRequestForTest(*this, *ShadowWidget),
		GetTransientPackageAsObject());
	TestTrue(TEXT("Negative spread texture generated"), Texture != nullptr);
	if (!Texture)
	{
		return false;
	}

	TestEqual(TEXT("Negative spread opaque pixel area matches shrunken rect"), CountOpaqueTexturePixels(Texture), 28 * 28);
	return true;
}
