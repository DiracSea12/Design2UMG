IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaComplexParserTest,
	"Design2UMG.FigmaParser.ComplexDashboard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaComplexParserTest::RunTest(const FString& Parameters)
{
	FString Json;
	TestTrue(TEXT("Load FigmaComplex fixture"), LoadFixtureJson(TEXT("FigmaComplex.json"), Json));
	if (Json.IsEmpty())
		return false;
	FDesignDocument Doc;
	TestTrue(TEXT("Figma complex parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	TestEqual(TEXT("Top-level frame count"), Doc.Children.Num(), 1);
	if (Doc.Children.Num() == 0)
		return false;
	// Dashboard root: HORIZONTAL auto layout
	const auto& Dashboard = Doc.Children[0];
	TestEqual(TEXT("Dashboard type"), Dashboard->Type, EDesignNodeType::Frame);
	TestEqual(TEXT("Dashboard layout"), Dashboard->LayoutData.Layout, EDesignLayout::Horizontal);
	TestTrue(TEXT("Dashboard clip (Figma clipsContent mapped)"), Dashboard->LayoutData.ExplicitClip.Get(false));
	TestEqual(TEXT("Dashboard children"), Dashboard->Children.Num(), 2);
	if (Dashboard->Children.Num() < 2)
		return false;
	// Sidebar: VERTICAL + individualStrokeWeights + FILL height
	const auto& Sidebar = Dashboard->Children[0];
	TestEqual(TEXT("Sidebar layout"), Sidebar->LayoutData.Layout, EDesignLayout::Vertical);
	TestTrue(TEXT("Sidebar height FILL"), Sidebar->Transform.Height.IsFill());
	TestTrue(TEXT("Sidebar width FIXED"), Sidebar->Transform.Width.IsFixed());
	TestEqual(TEXT("Sidebar gap"), Sidebar->LayoutData.ExplicitGap.Get(0.0f), 8.0f);
	TestEqual(TEXT("Sidebar padding left"), Sidebar->LayoutData.ExplicitPadding.Get(FDesignPadding{}).Left, 16.0f);
	TestFalse(TEXT("Sidebar stroke non-uniform"), Sidebar->Visual.Stroke.Thickness.bUniform);
	TestEqual(TEXT("Sidebar stroke right"), Sidebar->Visual.Stroke.Thickness.Right, 1.0f);
	TestEqual(TEXT("Sidebar stroke top"), Sidebar->Visual.Stroke.Thickness.Top, 0.0f);
	TestEqual(TEXT("Sidebar children"), Sidebar->Children.Num(), 4);
	if (Sidebar->Children.Num() < 4)
		return false;
	// Logo text: fontWeight 700 preserved, textAutoResize→Auto, textAlignVertical CENTER
	const auto& Logo = Sidebar->Children[0];
	TestEqual(TEXT("Logo type"), Logo->Type, EDesignNodeType::Text);
	TestEqual(TEXT("Logo content"), Logo->Text.Content, TEXT("Acme Dashboard"));
	TestEqual(TEXT("Logo fontWeight"), Logo->Text.FontWeight, TEXT("700"));
	TestEqual(TEXT("Logo fontSize"), Logo->Text.FontSize, 20.0f);
	TestEqual(TEXT("Logo textGrowth"), Logo->Text.TextGrowth, EDesignTextGrowth::Auto);
	TestEqual(TEXT("Logo textAlignV"), Logo->Text.TextAlignVertical, EDesignTextAlignVertical::Middle);
	TestTrue(TEXT("Logo lineHeight > 0"), Logo->Text.LineHeight > 0.0f);
	// Divider: LINE type
	TestEqual(TEXT("Divider type"), Sidebar->Children[1]->Type, EDesignNodeType::Line);
	// NavItem: COMPONENT → bReusable
	const auto& NavItem = Sidebar->Children[2];
	TestTrue(TEXT("NavItem reusable"), NavItem->bReusable);
	TestEqual(TEXT("NavItem layout"), NavItem->LayoutData.Layout, EDesignLayout::Horizontal);
	TestEqual(TEXT("NavItem children"), NavItem->Children.Num(), 2);
	// NavItem child: VECTOR → Path
	if (NavItem->Children.Num() >= 1)
		TestEqual(TEXT("NavIcon type"), NavItem->Children[0]->Type, EDesignNodeType::Path);
	// NavItem Instance: INSTANCE → Ref = componentId
	const auto& NavInst = Sidebar->Children[3];
	TestEqual(TEXT("NavInst ref"), NavInst->Ref, TEXT("10:5"));
	// MainContent
	const auto& Main = Dashboard->Children[1];
	TestTrue(TEXT("Main width FILL"), Main->Transform.Width.IsFill());
	TestEqual(TEXT("Main padding"), Main->LayoutData.ExplicitPadding.Get(FDesignPadding{}).Top, 32.0f);
	TestEqual(TEXT("Main children"), Main->Children.Num(), 4);
	if (Main->Children.Num() < 4)
		return false;
	// Header: SPACE_BETWEEN
	const auto& Header = Main->Children[0];
	TestEqual(TEXT("Header justify"), Header->LayoutData.JustifyContent, EDesignJustifyContent::SpaceBetween);
	TestEqual(TEXT("Header align"), Header->LayoutData.AlignItems, EDesignAlignItems::Center);
	// Avatar: ELLIPSE + multi-fill (SOLID + IMAGE)
	if (Header->Children.Num() >= 2)
	{
		const auto& Avatar = Header->Children[1];
		TestEqual(TEXT("Avatar type"), Avatar->Type, EDesignNodeType::Ellipse);
		TestTrue(TEXT("Avatar has image fill"), Avatar->Visual.PrimaryFill().IsImage());
		TestTrue(TEXT("Avatar multi-fills >= 2"), Avatar->Visual.Fills.Num() >= 2);
	}
	// StatCard: GRADIENT_LINEAR + DROP_SHADOW
	const auto& StatsRow = Main->Children[1];
	if (StatsRow->Children.Num() >= 1)
	{
		const auto& Card = StatsRow->Children[0];
		TestEqual(TEXT("Card fill gradient"), Card->Visual.PrimaryFill().Type, EDesignFillType::Gradient);
		TestEqual(TEXT("Card gradient type"), Card->Visual.PrimaryFill().Gradient.GradientType, EDesignGradientType::Linear);
		TestTrue(TEXT("Card gradient stops >= 2"), Card->Visual.PrimaryFill().Gradient.Colors.Num() >= 2);
		TestTrue(TEXT("Card has effects"), Card->Visual.Effects.Num() > 0);
		TestEqual(TEXT("Card shadow type"), Card->Visual.Effects[0].Type, EDesignEffectType::Shadow);
		TestFalse(TEXT("Card shadow not inner"), Card->Visual.Effects[0].Shadow.bInner);
	}
	// ChartArea: INNER_SHADOW + BACKGROUND_BLUR + rectangleCornerRadii child
	const auto& Chart = Main->Children[2];
	TestTrue(TEXT("Chart effects >= 2"), Chart->Visual.Effects.Num() >= 2);
	bool bFoundInner = false, bFoundBgBlur = false;
	for (const auto& E : Chart->Visual.Effects)
	{
		if (E.Type == EDesignEffectType::Shadow && E.Shadow.bInner) bFoundInner = true;
		if (E.Type == EDesignEffectType::BackgroundBlur) bFoundBgBlur = true;
	}
	TestTrue(TEXT("Chart has inner shadow"), bFoundInner);
	TestTrue(TEXT("Chart has bg blur"), bFoundBgBlur);
	// ChartPlaceholder: non-uniform corner radius
	if (Chart->Children.Num() >= 2)
	{
		const auto& Placeholder = Chart->Children[1];
		TestEqual(TEXT("Placeholder type"), Placeholder->Type, EDesignNodeType::Rectangle);
		TestFalse(TEXT("Placeholder non-uniform radius"), Placeholder->Visual.CornerRadius.bUniform);
		TestEqual(TEXT("Placeholder TL"), Placeholder->Visual.CornerRadius.TopLeft, 12.0f);
		TestEqual(TEXT("Placeholder BL"), Placeholder->Visual.CornerRadius.BottomLeft, 0.0f);
	}
	// GROUP: absolute positioned children
	const auto& Overlay = Main->Children[3];
	TestEqual(TEXT("Overlay type"), Overlay->Type, EDesignNodeType::Group);
	TestTrue(TEXT("Overlay children >= 7"), Overlay->Children.Num() >= 7);
	if (Overlay->Children.Num() >= 7)
	{
		// LAYER_BLUR
		TestTrue(TEXT("Badge has effects"), Overlay->Children[0]->Visual.Effects.Num() > 0);
		TestEqual(TEXT("Badge blur type"), Overlay->Children[0]->Visual.Effects[0].Type, EDesignEffectType::Blur);
		// FlippedIcon: relativeTransform flip detection
		TestTrue(TEXT("FlippedIcon flipX"), Overlay->Children[1]->Transform.bFlipX);
		// STAR → Polygon
		TestEqual(TEXT("Star type"), Overlay->Children[2]->Type, EDesignNodeType::Polygon);
		// REGULAR_POLYGON → Polygon
		TestEqual(TEXT("Polygon type"), Overlay->Children[3]->Type, EDesignNodeType::Polygon);
		// BOOLEAN_OPERATION → Path
		TestEqual(TEXT("BoolOp type"), Overlay->Children[4]->Type, EDesignNodeType::Path);
		// IMAGE fill with imageTransform + opacity
		const auto& ImgCard = Overlay->Children[5];
		TestTrue(TEXT("ImgCard has image fill"), ImgCard->Visual.PrimaryFill().IsImage());
		TestTrue(TEXT("ImgCard opacity < 1"), ImgCard->Transform.Opacity < 1.0f);
		// GRADIENT_RADIAL
		const auto& GradBg = Overlay->Children[6];
		TestEqual(TEXT("GradBg gradient type"), GradBg->Visual.PrimaryFill().Gradient.GradientType, EDesignGradientType::Radial);
	}
	// Justified text + textAlignVertical BOTTOM + textAutoResize NONE
	if (Overlay->Children.Num() >= 8)
	{
		const auto& JustText = Overlay->Children[7];
		TestEqual(TEXT("JustText align"), JustText->Text.TextAlign, EDesignTextAlign::Justify);
		TestEqual(TEXT("JustText valign"), JustText->Text.TextAlignVertical, EDesignTextAlignVertical::Bottom);
		TestEqual(TEXT("JustText growth"), JustText->Text.TextGrowth, EDesignTextGrowth::FixedWidthHeight);
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaLineHeightMappingTest,
	"Design2UMG.FigmaParser.LineHeightMapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaLineHeightMappingTest::RunTest(const FString& Parameters)
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
					"absoluteBoundingBox": {"x":0,"y":0,"width":100,"height":100},
					"children": [
						{
							"id": "0:3",
							"name": "TextPx",
							"type": "TEXT",
							"visible": true,
							"absoluteBoundingBox": {"x":1,"y":1,"width":30,"height":12},
							"fills": [{"type":"SOLID","visible":true,"color":{"r":1,"g":1,"b":1,"a":1}}],
							"style": {
								"fontFamily": "Inter",
								"fontSize": 10,
								"fontWeight": 400,
								"lineHeightUnit": "PIXELS",
								"lineHeightPx": 15
							},
							"characters": "A",
							"textAutoResize": "HEIGHT"
						},
						{
							"id": "0:4",
							"name": "TextPercent",
							"type": "TEXT",
							"visible": true,
							"absoluteBoundingBox": {"x":1,"y":20,"width":30,"height":12},
							"fills": [{"type":"SOLID","visible":true,"color":{"r":1,"g":1,"b":1,"a":1}}],
							"style": {
								"fontFamily": "Inter",
								"fontSize": 10,
								"fontWeight": 400,
								"lineHeightUnit": "FONT_SIZE_%",
								"lineHeightPercentFontSize": 140
							},
							"characters": "B",
							"textAutoResize": "HEIGHT"
						},
						{
							"id": "0:5",
							"name": "TextAuto",
							"type": "TEXT",
							"visible": true,
							"absoluteBoundingBox": {"x":1,"y":40,"width":30,"height":12},
							"fills": [{"type":"SOLID","visible":true,"color":{"r":1,"g":1,"b":1,"a":1}}],
							"style": {
								"fontFamily": "Inter",
								"fontSize": 10,
								"fontWeight": 400,
								"lineHeightUnit": "AUTO"
							},
							"characters": "C",
							"textAutoResize": "HEIGHT"
						},
						{
							"id": "0:6",
							"name": "TextMissing",
							"type": "TEXT",
							"visible": true,
							"absoluteBoundingBox": {"x":1,"y":60,"width":30,"height":12},
							"fills": [{"type":"SOLID","visible":true,"color":{"r":1,"g":1,"b":1,"a":1}}],
							"style": {
								"fontFamily": "Inter",
								"fontSize": 10,
								"fontWeight": 400
							},
							"characters": "D",
							"textAutoResize": "HEIGHT"
						}
					]
				}]
			}]
		}
	})");
	FDesignDocument Doc;
	TestTrue(TEXT("Figma parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() == 0 || Doc.Children[0]->Children.Num() < 4)
		return false;
	const TSharedPtr<FDesignNode>& PxText = Doc.Children[0]->Children[0];
	const TSharedPtr<FDesignNode>& PercentText = Doc.Children[0]->Children[1];
	const TSharedPtr<FDesignNode>& AutoText = Doc.Children[0]->Children[2];
	const TSharedPtr<FDesignNode>& MissingText = Doc.Children[0]->Children[3];
	TestTrue(TEXT("PIXELS line height mapped as ratio"), FMath::IsNearlyEqual(PxText->Text.LineHeight, 1.5f, 0.001f));
	TestTrue(TEXT("FONT_SIZE_% line height mapped as ratio"), FMath::IsNearlyEqual(PercentText->Text.LineHeight, 1.4f, 0.001f));
	TestEqual(TEXT("AUTO line height remains zero"), AutoText->Text.LineHeight, 0.0f);
	TestEqual(TEXT("Missing line height remains zero"), MissingText->Text.LineHeight, 0.0f);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaStrokeVisibilityGateTest,
	"Design2UMG.FigmaParser.StrokeVisibilityGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaStrokeVisibilityGateTest::RunTest(const FString& Parameters)
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
					"absoluteBoundingBox": {"x":0,"y":0,"width":400,"height":300},
					"children": [
						{
							"id": "0:3",
							"name": "NoVisibleStroke",
							"type": "RECTANGLE",
							"visible": true,
							"absoluteBoundingBox": {"x":10,"y":10,"width":120,"height":40},
							"strokeWeight": 3,
							"strokeAlign": "CENTER",
							"strokes": [{"type":"SOLID","visible":false,"color":{"r":1,"g":0,"b":0,"a":1}}]
						},
						{
							"id": "0:4",
							"name": "VisibleStroke",
							"type": "RECTANGLE",
							"visible": true,
							"absoluteBoundingBox": {"x":10,"y":70,"width":120,"height":40},
							"strokeWeight": 2,
							"strokeAlign": "OUTSIDE",
							"strokes": [{"type":"SOLID","visible":true,"color":{"r":0,"g":1,"b":0,"a":1}}]
						}
					]
				}]
			}]
		}
	})");
	FDesignDocument Doc;
	TestTrue(TEXT("Figma parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() == 0 || Doc.Children[0]->Children.Num() < 2)
		return false;
	const TSharedPtr<FDesignNode>& NoVisibleStroke = Doc.Children[0]->Children[0];
	const TSharedPtr<FDesignNode>& VisibleStroke = Doc.Children[0]->Children[1];
	TestFalse(TEXT("No-visible-stroke node should not have stroke"), NoVisibleStroke->Visual.Stroke.HasStroke());
	TestEqual(TEXT("No-visible-stroke thickness remains 0"), NoVisibleStroke->Visual.Stroke.Thickness.Uniform, 0.0f);
	TestTrue(TEXT("Visible-stroke node keeps stroke"), VisibleStroke->Visual.Stroke.HasStroke());
	TestEqual(TEXT("Visible-stroke align parsed"), VisibleStroke->Visual.Stroke.Align, EDesignStrokeAlign::Outside);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaExpandedContainerRenderBoundsUseLayoutSpaceTest,
	"Design2UMG.FigmaParser.ExpandedContainerRenderBoundsUseLayoutSpace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaExpandedContainerRenderBoundsUseLayoutSpaceTest::RunTest(const FString& Parameters)
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
					"clipsContent": true,
					"absoluteBoundingBox": {"x":0,"y":0,"width":100,"height":80},
					"absoluteRenderBounds": {"x":-8,"y":-8,"width":116,"height":96},
					"effects": [{
						"type": "DROP_SHADOW",
						"visible": true,
						"blendMode": "NORMAL",
						"offset": {"x":0,"y":0},
						"radius": 8,
						"color": {"r":0,"g":0,"b":0,"a":0.25}
					}],
					"children": [{
						"id": "0:3",
						"name": "Child",
						"type": "RECTANGLE",
						"visible": true,
						"absoluteBoundingBox": {"x":10,"y":12,"width":40,"height":20},
						"absoluteRenderBounds": {"x":10,"y":12,"width":40,"height":20},
						"fills": [{"type":"SOLID","visible":true,"color":{"r":1,"g":1,"b":1,"a":1}}]
					}]
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Figma parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	TestEqual(TEXT("Top-level frame count"), Doc.Children.Num(), 1);
	if (Doc.Children.Num() != 1)
		return false;

	const TSharedPtr<FDesignNode>& Root = Doc.Children[0];
	TestTrue(TEXT("Root X present"), Root->Transform.HasExplicitX());
	TestTrue(TEXT("Root Y present"), Root->Transform.HasExplicitY());
	TestEqual(TEXT("Root X stays in layout space"), Root->Transform.X, 0.0f);
	TestEqual(TEXT("Root Y stays in layout space"), Root->Transform.Y, 0.0f);
	TestTrue(TEXT("Root width fixed"), Root->Transform.Width.IsFixed());
	TestTrue(TEXT("Root height fixed"), Root->Transform.Height.IsFixed());
	TestEqual(TEXT("Root width uses layout bounds"), Root->Transform.Width.Value, 100.0f);
	TestEqual(TEXT("Root height uses layout bounds"), Root->Transform.Height.Value, 80.0f);
	TestEqual(TEXT("Root child count"), Root->Children.Num(), 1);
	if (Root->Children.Num() != 1)
		return false;

	const TSharedPtr<FDesignNode>& Child = Root->Children[0];
	TestTrue(TEXT("Child X present"), Child->Transform.HasExplicitX());
	TestTrue(TEXT("Child Y present"), Child->Transform.HasExplicitY());
	TestEqual(TEXT("Child X remains relative to layout bounds"), Child->Transform.X, 10.0f);
	TestEqual(TEXT("Child Y remains relative to layout bounds"), Child->Transform.Y, 12.0f);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaClippedSyntheticVectorRasterUsesVisibleBoundsTest,
	"Design2UMG.FigmaParser.ClippedSyntheticVectorRasterUsesVisibleBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaClippedSyntheticVectorRasterUsesVisibleBoundsTest::RunTest(const FString& Parameters)
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
					"name": "Viewport",
					"type": "FRAME",
					"visible": true,
					"clipsContent": true,
					"absoluteBoundingBox": {"x":0,"y":0,"width":1600,"height":920},
					"absoluteRenderBounds": {"x":0,"y":0,"width":1600,"height":920},
					"children": [{
						"id": "0:3",
						"name": "ClippingFrame",
						"type": "FRAME",
						"visible": true,
						"clipsContent": true,
						"absoluteBoundingBox": {"x":0,"y":0,"width":1600,"height":1200},
						"absoluteRenderBounds": {"x":0,"y":0,"width":1600,"height":920},
						"children": [{
							"id": "0:4",
							"name": "BackgroundShape",
							"type": "ELLIPSE",
							"visible": true,
							"absoluteBoundingBox": {"x":-701,"y":-148,"width":1700,"height":1700},
							"absoluteRenderBounds": {"x":0,"y":0,"width":999,"height":920},
							"fills": [{"type":"SOLID","visible":true,"color":{"r":0.5,"g":0.2,"b":1,"a":1}}]
						}, {
							"id": "0:5",
							"name": "RasterizedBackgroundShape",
							"type": "ELLIPSE",
							"visible": true,
							"absoluteBoundingBox": {"x":-701,"y":-148,"width":1700,"height":1700},
							"absoluteRenderBounds": {"x":0,"y":0,"width":999,"height":920},
							"fills": [
								{"type":"GRADIENT_LINEAR","visible":true,"gradientStops":[
									{"position":0,"color":{"r":0.8,"g":0.2,"b":1,"a":1}},
									{"position":1,"color":{"r":0.5,"g":0.1,"b":1,"a":1}}
								]},
								{"type":"IMAGE","visible":true,"imageRef":"vec_clipped_shape","scaleMode":"FILL"}
							]
						}, {
							"id": "0:6",
							"name": "RasterizedStroke",
							"type": "VECTOR",
							"visible": true,
							"absoluteBoundingBox": {"x":0,"y":100,"width":599,"height":1},
							"absoluteRenderBounds": {"x":0,"y":100.15,"width":560,"height":0.7},
							"fills": [{"type":"IMAGE","visible":true,"imageRef":"vec_unclipped_stroke","scaleMode":"FILL"}]
						}]
					}]
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Clipped leaf parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() != 1
		|| Doc.Children[0]->Children.Num() != 1
		|| Doc.Children[0]->Children[0]->Children.Num() != 3)
	{
		AddError(TEXT("Expected viewport, clipping frame, and leaf shape"));
		return false;
	}

	const TSharedPtr<FDesignNode>& Viewport = Doc.Children[0];
	const TSharedPtr<FDesignNode>& ClippingFrame = Viewport->Children[0];
	const TSharedPtr<FDesignNode>& BackgroundShape = ClippingFrame->Children[0];
	const TSharedPtr<FDesignNode>& RasterizedBackgroundShape = ClippingFrame->Children[1];
	const TSharedPtr<FDesignNode>& RasterizedStroke = ClippingFrame->Children[2];
	TestTrue(TEXT("Viewport keeps clipping"), Viewport->LayoutData.ExplicitClip.Get(false));
	TestTrue(TEXT("Nested frame keeps clipping"), ClippingFrame->LayoutData.ExplicitClip.Get(false));
	TestEqual(TEXT("Ordinary clipped leaf keeps visible X"), BackgroundShape->Transform.X, 0.0f);
	TestEqual(TEXT("Ordinary clipped leaf keeps visible Y"), BackgroundShape->Transform.Y, 0.0f);
	TestTrue(TEXT("Clipped leaf width fixed"), BackgroundShape->Transform.Width.IsFixed());
	TestTrue(TEXT("Clipped leaf height fixed"), BackgroundShape->Transform.Height.IsFixed());
	TestEqual(TEXT("Ordinary clipped leaf keeps visible width"), BackgroundShape->Transform.Width.Value, 999.0f);
	TestEqual(TEXT("Ordinary clipped leaf keeps visible height"), BackgroundShape->Transform.Height.Value, 920.0f);
	TestEqual(TEXT("Pre-rasterized leaf keeps visible X"), RasterizedBackgroundShape->Transform.X, 0.0f);
	TestEqual(TEXT("Pre-rasterized leaf keeps visible Y"), RasterizedBackgroundShape->Transform.Y, 0.0f);
	TestEqual(TEXT("Pre-rasterized leaf keeps raster width"), RasterizedBackgroundShape->Transform.Width.Value, 999.0f);
	TestEqual(TEXT("Pre-rasterized leaf keeps raster height"), RasterizedBackgroundShape->Transform.Height.Value, 920.0f);
	TestEqual(TEXT("Unclipped vector raster keeps render X"), RasterizedStroke->Transform.X, 0.0f);
	TestEqual(TEXT("Unclipped vector raster keeps render Y"), RasterizedStroke->Transform.Y, 100.15f);
	TestEqual(TEXT("Unclipped vector raster keeps render width"), RasterizedStroke->Transform.Width.Value, 560.0f);
	TestEqual(TEXT("Unclipped vector raster keeps render height"), RasterizedStroke->Transform.Height.Value, 0.7f);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaClippedContainerOffsetsChildrenFromVisibleOriginTest,
	"Design2UMG.FigmaParser.ClippedContainerOffsetsChildrenFromVisibleOrigin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaClippedContainerOffsetsChildrenFromVisibleOriginTest::RunTest(const FString& Parameters)
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
					"id": "root",
					"name": "Viewport",
					"type": "FRAME",
					"clipsContent": true,
					"absoluteBoundingBox": {"x":100,"y":200,"width":300,"height":200},
					"absoluteRenderBounds": {"x":100,"y":200,"width":300,"height":200},
					"children": [{
						"id": "cropped",
						"name": "CroppedContainer",
						"type": "GROUP",
						"absoluteBoundingBox": {"x":20,"y":140,"width":500,"height":400},
						"absoluteRenderBounds": {"x":100,"y":200,"width":300,"height":200},
						"children": [{
							"id": "mask",
							"name": "Mask",
							"type": "VECTOR",
							"isMask": true,
							"absoluteBoundingBox": {"x":20,"y":140,"width":500,"height":400}
						}, {
							"id": "content",
							"name": "Content",
							"type": "INSTANCE",
							"absoluteBoundingBox": {"x":80,"y":180,"width":360,"height":240},
							"absoluteRenderBounds": {"x":100,"y":200,"width":300,"height":200},
							"children": [{
								"id": "tile",
								"name": "VisibleTile",
								"type": "RECTANGLE",
								"absoluteBoundingBox": {"x":100,"y":200,"width":50,"height":50},
								"absoluteRenderBounds": {"x":100,"y":200,"width":50,"height":50},
								"fills": [{"type":"SOLID","color":{"r":0.2,"g":0.3,"b":0.5,"a":1}}]
							}]
						}]
					}]
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Clipped container parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() != 1
		|| Doc.Children[0]->Children.Num() != 1
		|| Doc.Children[0]->Children[0]->Children.Num() != 1
		|| Doc.Children[0]->Children[0]->Children[0]->Children.Num() != 1)
	{
		AddError(TEXT("Expected viewport, cropped container, content, and tile"));
		return false;
	}

	const TSharedPtr<FDesignNode>& CroppedContainer = Doc.Children[0]->Children[0];
	const TSharedPtr<FDesignNode>& Content = CroppedContainer->Children[0];
	const TSharedPtr<FDesignNode>& Tile = Content->Children[0];
	TestEqual(TEXT("Cropped container starts at viewport origin"), CroppedContainer->Transform.X, 0.0f);
	TestEqual(TEXT("Cropped container visible width"), CroppedContainer->Transform.Width.Value, 300.0f);
	TestEqual(TEXT("Content offsets from visible X origin"), Content->Transform.X, -20.0f);
	TestEqual(TEXT("Content offsets from visible Y origin"), Content->Transform.Y, -20.0f);
	TestEqual(TEXT("Nested tile remains relative to content layout X"), Tile->Transform.X, 20.0f);
	TestEqual(TEXT("Nested tile remains relative to content layout Y"), Tile->Transform.Y, 20.0f);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaLeafForegroundBlurUsesLayoutBoundsTest,
	"Design2UMG.FigmaParser.LeafForegroundBlurUsesLayoutBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaLeafForegroundBlurUsesLayoutBoundsTest::RunTest(const FString& Parameters)
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
					"absoluteBoundingBox": {"x":0,"y":0,"width":160,"height":120},
					"absoluteRenderBounds": {"x":0,"y":0,"width":160,"height":120},
					"children": [{
						"id": "blur_avatar",
						"name": "BlurAvatar",
						"type": "ELLIPSE",
						"visible": true,
						"absoluteBoundingBox": {"x":24,"y":20,"width":26,"height":26},
						"absoluteRenderBounds": {"x":2.25,"y":-1.75,"width":69.5,"height":69.5},
						"fills": [
							{"type":"IMAGE","visible":true,"blendMode":"NORMAL","imageRef":"avatar","scaleMode":"FIT"},
							{"type":"IMAGE","visible":true,"blendMode":"NORMAL","imageRef":"avatar_mask","scaleMode":"FILL"}
						],
						"effects": [{
							"type":"LAYER_BLUR",
							"visible":true,
							"blendMode":"NORMAL",
							"radius":21.75
						}],
						"children": []
					}]
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Leaf blur parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	TestEqual(TEXT("Top-level frame count"), Doc.Children.Num(), 1);
	if (Doc.Children.Num() != 1 || Doc.Children[0]->Children.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& Avatar = Doc.Children[0]->Children[0];
	TestTrue(TEXT("Leaf blur X present"), Avatar->Transform.HasExplicitX());
	TestTrue(TEXT("Leaf blur Y present"), Avatar->Transform.HasExplicitY());
	TestEqual(TEXT("Leaf blur X uses layout bounds"), Avatar->Transform.X, 24.0f);
	TestEqual(TEXT("Leaf blur Y uses layout bounds"), Avatar->Transform.Y, 20.0f);
	TestTrue(TEXT("Leaf blur width fixed"), Avatar->Transform.Width.IsFixed());
	TestTrue(TEXT("Leaf blur height fixed"), Avatar->Transform.Height.IsFixed());
	TestEqual(TEXT("Leaf blur width uses layout bounds"), Avatar->Transform.Width.Value, 26.0f);
	TestEqual(TEXT("Leaf blur height uses layout bounds"), Avatar->Transform.Height.Value, 26.0f);
	TestTrue(TEXT("Leaf blur keeps render delta X"), Avatar->Metadata.Contains(TEXT("source.renderBoundsDeltaX")));
	TestTrue(TEXT("Leaf blur keeps render delta Y"), Avatar->Metadata.Contains(TEXT("source.renderBoundsDeltaY")));
	TestTrue(TEXT("Leaf blur keeps render delta W"), Avatar->Metadata.Contains(TEXT("source.renderBoundsDeltaW")));
	TestTrue(TEXT("Leaf blur keeps render delta H"), Avatar->Metadata.Contains(TEXT("source.renderBoundsDeltaH")));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaShrunkenFilledFrameUsesLayoutBoundsTest,
	"Design2UMG.FigmaParser.ShrunkenFilledFrameUsesLayoutBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaShrunkenFilledFrameUsesLayoutBoundsTest::RunTest(const FString& Parameters)
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
					"absoluteBoundingBox": {"x":0,"y":0,"width":320,"height":240},
					"absoluteRenderBounds": {"x":0,"y":0,"width":320,"height":240},
					"children": [{
						"id": "0:3",
						"name": "ImageCard",
						"type": "FRAME",
						"visible": true,
						"absoluteBoundingBox": {"x":24,"y":16,"width":160,"height":160},
						"absoluteRenderBounds": {"x":24,"y":16,"width":160,"height":100},
						"fills": [
							{"type":"SOLID","visible":true,"blendMode":"NORMAL","color":{"r":0.92,"g":0.90,"b":0.94,"a":1}},
							{"type":"IMAGE","visible":true,"blendMode":"LUMINOSITY","imageRef":"img_card","scaleMode":"FIT"}
						],
						"children": []
					}]
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Shrunken filled frame parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	TestEqual(TEXT("Top-level frame count"), Doc.Children.Num(), 1);
	if (Doc.Children.Num() != 1 || Doc.Children[0]->Children.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& Card = Doc.Children[0]->Children[0];
	TestTrue(TEXT("Filled frame X present"), Card->Transform.HasExplicitX());
	TestTrue(TEXT("Filled frame Y present"), Card->Transform.HasExplicitY());
	TestEqual(TEXT("Filled frame X uses layout bounds"), Card->Transform.X, 24.0f);
	TestEqual(TEXT("Filled frame Y uses layout bounds"), Card->Transform.Y, 16.0f);
	TestTrue(TEXT("Filled frame width fixed"), Card->Transform.Width.IsFixed());
	TestTrue(TEXT("Filled frame height fixed"), Card->Transform.Height.IsFixed());
	TestEqual(TEXT("Filled frame width uses layout bounds"), Card->Transform.Width.Value, 160.0f);
	TestEqual(TEXT("Filled frame height uses layout bounds"), Card->Transform.Height.Value, 160.0f);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaFilledAutoLayoutContainerUsesLayoutBoundsTest,
	"Design2UMG.FigmaParser.FilledAutoLayoutContainerUsesLayoutBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaFilledAutoLayoutContainerUsesLayoutBoundsTest::RunTest(const FString& Parameters)
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
					"absoluteBoundingBox": {"x":0,"y":0,"width":400,"height":300},
					"absoluteRenderBounds": {"x":0,"y":0,"width":400,"height":300},
					"children": [{
						"id":"content",
						"name":"Content",
						"type":"FRAME",
						"visible":true,
						"layoutMode":"VERTICAL",
						"absoluteBoundingBox":{"x":20,"y":20,"width":300,"height":200},
						"absoluteRenderBounds":{"x":20,"y":20,"width":300,"height":136},
						"fills":[{"type":"SOLID","visible":true,"blendMode":"NORMAL","color":{"r":1,"g":1,"b":1,"a":1}}],
						"children":[
							{
								"id":"headline",
								"name":"Headline",
								"type":"TEXT",
								"visible":true,
								"layoutSizingHorizontal":"FILL",
								"layoutSizingVertical":"HUG",
								"absoluteBoundingBox":{"x":36,"y":36,"width":268,"height":48},
								"absoluteRenderBounds":{"x":38,"y":41,"width":240,"height":33.27},
								"characters":"Label",
								"style":{
									"fontFamily":"Roboto",
									"fontWeight":700,
									"fontSize":32,
									"textAlignHorizontal":"LEFT",
									"textAlignVertical":"TOP",
									"textCase":"ORIGINAL",
									"textDecoration":"NONE",
									"letterSpacing":0,
									"lineHeightUnit":"PIXELS",
									"lineHeightPx":48
								}
							}
						]
					}]
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Filled auto-layout container parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	TestEqual(TEXT("Top-level frame count"), Doc.Children.Num(), 1);
	if (Doc.Children.Num() != 1 || Doc.Children[0]->Children.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& Content = Doc.Children[0]->Children[0];
	TestTrue(TEXT("Filled container X present"), Content->Transform.HasExplicitX());
	TestTrue(TEXT("Filled container Y present"), Content->Transform.HasExplicitY());
	TestEqual(TEXT("Filled container X uses layout bounds"), Content->Transform.X, 20.0f);
	TestEqual(TEXT("Filled container Y uses layout bounds"), Content->Transform.Y, 20.0f);
	TestTrue(TEXT("Filled container width fixed"), Content->Transform.Width.IsFixed());
	TestTrue(TEXT("Filled container height fixed"), Content->Transform.Height.IsFixed());
	TestEqual(TEXT("Filled container width uses layout bounds"), Content->Transform.Width.Value, 300.0f);
	TestEqual(TEXT("Filled container height uses layout bounds"), Content->Transform.Height.Value, 200.0f);
	TestEqual(TEXT("Filled container child count"), Content->Children.Num(), 1);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaExplicitGroupMaskLeafSkippedTest,
	"Design2UMG.FigmaParser.ExplicitGroupMaskLeafSkipped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaExplicitGroupMaskLeafSkippedTest::RunTest(const FString& Parameters)
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
					"id": "masked_group",
					"name": "MaskedGroup",
					"type": "GROUP",
					"visible": true,
					"absoluteBoundingBox": {"x": 0, "y": 0, "width": 120, "height": 120},
					"children": [
						{
							"id": "mask_rect",
							"name": "Mask Shape",
							"type": "RECTANGLE",
							"visible": true,
							"isMask": true,
							"absoluteBoundingBox": {"x": 0, "y": 0, "width": 120, "height": 120},
							"fills": [{
								"type": "SOLID",
								"visible": true,
								"color": {"r": 0.0, "g": 0.0, "b": 0.0, "a": 1.0}
							}]
						},
						{
							"id": "label",
							"name": "Label",
							"type": "TEXT",
							"visible": true,
							"absoluteBoundingBox": {"x": 10, "y": 10, "width": 80, "height": 20},
							"characters": "Visible",
							"style": {
								"fontFamily": "Inter",
								"fontSize": 16,
								"fontWeight": 400,
								"textAlignHorizontal": "LEFT",
								"textAlignVertical": "TOP"
							}
						}
					]
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Explicit group mask fixture parses"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& Group = Doc.Children[0];
	TestEqual(TEXT("Masked group keeps one runtime child"), Group->Children.Num(), 1);
	if (Group->Children.Num() != 1)
	{
		return false;
	}

	TestEqual(TEXT("Explicit mask leaf is skipped"), Group->Children[0]->Id, TEXT("label"));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaImplicitGroupMaskLeafStackSkippedTest,
	"Design2UMG.FigmaParser.ImplicitGroupMaskLeafStackSkipped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaImplicitGroupMaskLeafStackSkippedTest::RunTest(const FString& Parameters)
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
					"id": "masked_group",
					"name": "MaskedGroup",
					"type": "GROUP",
					"visible": true,
					"absoluteBoundingBox": {"x": 0, "y": 0, "width": 120, "height": 120},
					"children": [
						{
							"id": "implicit_mask",
							"name": "Shape",
							"type": "RECTANGLE",
							"visible": true,
							"absoluteBoundingBox": {"x": 0, "y": 0, "width": 120, "height": 120},
							"fills": [{
								"type": "SOLID",
								"visible": true,
								"color": {"r": 1.0, "g": 1.0, "b": 1.0, "a": 1.0}
							}]
						},
						{
							"id": "masked_image",
							"name": "Image",
							"type": "RECTANGLE",
							"visible": true,
							"absoluteBoundingBox": {"x": -10, "y": -10, "width": 140, "height": 140},
							"fills": [{
								"type": "IMAGE",
								"visible": true,
								"imageRef": "masked_image_ref",
								"scaleMode": "FILL"
							}]
						}
					]
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Implicit group mask stack fixture parses"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& Group = Doc.Children[0];
	TestEqual(TEXT("Implicit group mask stack keeps one runtime child"), Group->Children.Num(), 1);
	if (Group->Children.Num() != 1)
	{
		return false;
	}

	TestEqual(TEXT("Implicit mask leaf is skipped"), Group->Children[0]->Id, TEXT("masked_image"));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaImplicitGroupMaskGraphicalSubtreeSkippedTest,
	"Design2UMG.FigmaParser.ImplicitGroupMaskGraphicalSubtreeSkipped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaImplicitGroupMaskGraphicalSubtreeSkippedTest::RunTest(const FString& Parameters)
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
					"id": "masked_group",
					"name": "MaskedGroup",
					"type": "GROUP",
					"visible": true,
					"absoluteBoundingBox": {"x": 0, "y": 0, "width": 48, "height": 48},
					"children": [
						{
							"id": "implicit_mask",
							"name": "Shape",
							"type": "ELLIPSE",
							"visible": true,
							"absoluteBoundingBox": {"x": 0, "y": 0, "width": 48, "height": 48},
							"fills": [{
								"type": "SOLID",
								"visible": true,
								"color": {"r": 1.0, "g": 1.0, "b": 1.0, "a": 1.0}
							}]
						},
						{
							"id": "grouped_artwork",
							"name": "Artwork",
							"type": "GROUP",
							"visible": true,
							"absoluteBoundingBox": {"x": -4, "y": -4, "width": 56, "height": 56},
							"children": [
								{
									"id": "art_circle",
									"name": "Circle",
									"type": "ELLIPSE",
									"visible": true,
									"absoluteBoundingBox": {"x": -4, "y": -4, "width": 56, "height": 56}
								},
								{
									"id": "art_vector",
									"name": "Detail",
									"type": "VECTOR",
									"visible": true,
									"absoluteBoundingBox": {"x": 12, "y": 12, "width": 24, "height": 24}
								}
							]
						}
					]
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Implicit graphical subtree mask fixture parses"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& Group = Doc.Children[0];
	TestEqual(TEXT("Implicit graphical subtree keeps one runtime child"), Group->Children.Num(), 1);
	if (Group->Children.Num() != 1)
	{
		return false;
	}

	TestEqual(TEXT("Grouped artwork remains after implicit mask removal"), Group->Children[0]->Id, TEXT("grouped_artwork"));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaFullBoundsGraphicalBackgroundKeptWithoutClipEvidenceTest,
	"Design2UMG.FigmaParser.FullBoundsGraphicalBackgroundKeptWithoutClipEvidence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaFullBoundsGraphicalBackgroundKeptWithoutClipEvidenceTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"document": {
			"id": "0:0", "name": "Doc", "type": "DOCUMENT", "children": [{
				"id": "0:1", "name": "Page", "type": "CANVAS", "children": [{
					"id": "decorated_background", "name": "DecoratedBackground", "type": "GROUP",
					"absoluteBoundingBox": {"x": 0, "y": 0, "width": 120, "height": 120},
					"children": [
						{
							"id": "background", "name": "Background", "type": "RECTANGLE",
							"absoluteBoundingBox": {"x": 0, "y": 0, "width": 120, "height": 120},
							"fills": [{"type": "SOLID", "color": {"r": 0.1, "g": 0.2, "b": 0.3, "a": 1}}]
						},
						{
							"id": "dot_a", "name": "DotA", "type": "ELLIPSE",
							"absoluteBoundingBox": {"x": 16, "y": 16, "width": 12, "height": 12},
							"fills": [{"type": "SOLID", "color": {"r": 0.4, "g": 0.5, "b": 0.6, "a": 1}}]
						},
						{
							"id": "dot_b", "name": "DotB", "type": "ELLIPSE",
							"absoluteBoundingBox": {"x": 84, "y": 84, "width": 12, "height": 12},
							"fills": [{"type": "SOLID", "color": {"r": 0.4, "g": 0.5, "b": 0.6, "a": 1}}]
						}
					]
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Decorated graphical background fixture parses"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& Group = Doc.Children[0];
	TestEqual(TEXT("Contained graphical decoration keeps all children"), Group->Children.Num(), 3);
	return Group->Children.Num() == 3
		&& TestEqual(TEXT("Full-bounds graphical background remains first"), Group->Children[0]->Id, TEXT("background"));
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaFullBoundsGroupBackgroundKeptWithoutMaskFlagTest,
	"Design2UMG.FigmaParser.FullBoundsGroupBackgroundKeptWithoutMaskFlag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaFullBoundsGroupBackgroundKeptWithoutMaskFlagTest::RunTest(const FString& Parameters)
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
					"id": "background_group",
					"name": "BackgroundGroup",
					"type": "GROUP",
					"visible": true,
					"absoluteBoundingBox": {"x": 0, "y": 0, "width": 120, "height": 120},
					"children": [
						{
							"id": "background_rect",
							"name": "Background",
							"type": "RECTANGLE",
							"visible": true,
							"absoluteBoundingBox": {"x": 0, "y": 0, "width": 120, "height": 120},
							"fills": [{
								"type": "SOLID",
								"visible": true,
								"color": {"r": 0.1, "g": 0.2, "b": 0.3, "a": 1.0}
							}]
						},
						{
							"id": "label",
							"name": "Label",
							"type": "TEXT",
							"visible": true,
							"absoluteBoundingBox": {"x": 10, "y": 10, "width": 80, "height": 20},
							"characters": "Visible",
							"style": {
								"fontFamily": "Inter",
								"fontSize": 16,
								"fontWeight": 400,
								"textAlignHorizontal": "LEFT",
								"textAlignVertical": "TOP"
							}
						}
					]
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Full-bounds group background fixture parses"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& Group = Doc.Children[0];
	TestEqual(TEXT("Group keeps background and foreground"), Group->Children.Num(), 2);
	if (Group->Children.Num() != 2)
	{
		return false;
	}

	TestEqual(TEXT("Background remains the first child"), Group->Children[0]->Id, TEXT("background_rect"));
	TestEqual(TEXT("Foreground remains the second child"), Group->Children[1]->Id, TEXT("label"));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaImplicitMaskLeafKeepsMultiFillGeometryTest,
	"Design2UMG.FigmaParser.ImplicitMaskLeafKeepsMultiFillGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaImplicitMaskLeafKeepsMultiFillGeometryTest::RunTest(const FString& Parameters)
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
					"id": "logo_group",
					"name": "LogoGroup",
					"type": "GROUP",
					"visible": true,
					"absoluteBoundingBox": {"x": 0, "y": 0, "width": 48, "height": 48},
					"children": [
						{
							"id": "logo_backplate",
							"name": "Logo Backplate",
							"type": "ELLIPSE",
							"visible": true,
							"absoluteBoundingBox": {"x": 0, "y": 0, "width": 48, "height": 48},
							"fills": [
								{
									"type": "SOLID",
									"visible": true,
									"color": {"r": 0.15, "g": 0.2, "b": 0.7, "a": 1.0}
								},
								{
									"type": "IMAGE",
									"visible": true,
									"imageRef": "img_logo",
									"scaleMode": "FILL"
								}
							]
						},
						{
							"id": "logo_foreground",
							"name": "Foreground",
							"type": "VECTOR",
							"visible": true,
							"absoluteBoundingBox": {"x": 12, "y": 12, "width": 24, "height": 24},
							"fillGeometry": [{
								"path": "M 0 0 L 24 0 L 12 24 Z",
								"windingRule": "NONZERO"
							}],
							"fills": [{
								"type": "SOLID",
								"visible": true,
								"color": {"r": 1.0, "g": 1.0, "b": 1.0, "a": 1.0}
							}]
						}
					]
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Multi-fill logo fixture parses"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& Group = Doc.Children[0];
	TestEqual(TEXT("Group keeps both runtime children"), Group->Children.Num(), 2);
	if (Group->Children.Num() != 2)
	{
		return false;
	}

	TestEqual(TEXT("Multi-fill backplate is preserved"), Group->Children[0]->Id, TEXT("logo_backplate"));
	TestEqual(TEXT("Foreground stays after preserved backplate"), Group->Children[1]->Id, TEXT("logo_foreground"));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaFullBoundsFrameRectangleKeptWithoutMaskFlagTest,
	"Design2UMG.FigmaParser.FullBoundsFrameRectangleKeptWithoutMaskFlag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaFullBoundsFrameRectangleKeptWithoutMaskFlagTest::RunTest(const FString& Parameters)
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
					"id": "frame_root",
					"name": "FrameRoot",
					"type": "FRAME",
					"visible": true,
					"clipsContent": true,
					"absoluteBoundingBox": {"x": 0, "y": 0, "width": 120, "height": 120},
					"children": [
						{
							"id": "bg_rect",
							"name": "Rectangle",
							"type": "RECTANGLE",
							"visible": true,
							"absoluteBoundingBox": {"x": 0, "y": 0, "width": 120, "height": 120},
							"fills": [{
								"type": "SOLID",
								"visible": true,
								"color": {"r": 0.0, "g": 0.0, "b": 0.0, "a": 1.0}
							}]
						},
						{
							"id": "fg_label",
							"name": "Foreground",
							"type": "TEXT",
							"visible": true,
							"absoluteBoundingBox": {"x": 12, "y": 12, "width": 60, "height": 20},
							"characters": "Keep",
							"style": {
								"fontFamily": "Inter",
								"fontSize": 16,
								"fontWeight": 400,
								"textAlignHorizontal": "LEFT",
								"textAlignVertical": "TOP"
							}
						}
					]
				}]
			}]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Full-bounds frame rectangle fixture parses"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FDesignNode>& Frame = Doc.Children[0];
	TestEqual(TEXT("Full-bounds frame keeps both children"), Frame->Children.Num(), 2);
	if (Frame->Children.Num() != 2)
	{
		return false;
	}

	TestEqual(TEXT("Background rectangle is preserved"), Frame->Children[0]->Id, TEXT("bg_rect"));
	TestEqual(TEXT("Foreground label is preserved"), Frame->Children[1]->Id, TEXT("fg_label"));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaCornerRadiusNormalizationTest,
	"Design2UMG.FigmaParser.CornerRadiusNormalization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaCornerRadiusNormalizationTest::RunTest(const FString& Parameters)
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
					"absoluteBoundingBox": {"x":0,"y":0,"width":400,"height":300},
					"children": [
						{
							"id": "0:3",
							"name": "UniformRadius",
							"type": "RECTANGLE",
							"visible": true,
							"absoluteBoundingBox": {"x":10,"y":10,"width":120,"height":40},
							"rectangleCornerRadii": [100,100,100,100]
						},
						{
							"id": "0:4",
							"name": "NonUniformRadius",
							"type": "RECTANGLE",
							"visible": true,
							"absoluteBoundingBox": {"x":10,"y":70,"width":120,"height":40},
							"rectangleCornerRadii": [8,12,8,12]
						}
					]
				}]
			}]
		}
	})");
	FDesignDocument Doc;
	TestTrue(TEXT("Figma parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() == 0 || Doc.Children[0]->Children.Num() < 2)
		return false;
	const TSharedPtr<FDesignNode>& Uniform = Doc.Children[0]->Children[0];
	const TSharedPtr<FDesignNode>& NonUniform = Doc.Children[0]->Children[1];
	TestTrue(TEXT("Equal radii normalized to uniform"), Uniform->Visual.CornerRadius.bUniform);
	TestEqual(TEXT("Equal radii keeps numeric value"), Uniform->Visual.CornerRadius.Uniform, 100.0f);
	TestFalse(TEXT("Mixed radii remains non-uniform"), NonUniform->Visual.CornerRadius.bUniform);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaTextFitUsesAuthoredMinimumSizeTest,
	"Design2UMG.Converter.TextFitUsesAuthoredMinimumSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaTextFitUsesAuthoredMinimumSizeTest::RunTest(const FString& Parameters)
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
					"layoutMode": "VERTICAL",
					"absoluteBoundingBox": {"x":0,"y":0,"width":500,"height":200},
					"children": [
						{
							"id": "0:3",
							"name": "ButtonLabelInter",
							"type": "TEXT",
							"visible": true,
							"layoutSizingHorizontal": "HUG",
							"layoutSizingVertical": "HUG",
							"absoluteBoundingBox": {"x":20,"y":20,"width":63,"height":20},
							"fills": [{"type":"SOLID","visible":true,"color":{"r":1,"g":1,"b":1,"a":1}}],
							"style": {
								"fontFamily": "Inter",
								"fontSize": 18,
								"fontWeight": 600
							},
							"characters": "Download"
						},
						{
							"id": "0:4",
							"name": "ButtonLabelRoboto",
							"type": "TEXT",
							"visible": true,
							"layoutSizingHorizontal": "HUG",
							"layoutSizingVertical": "HUG",
							"absoluteBoundingBox": {"x":20,"y":56,"width":35,"height":20},
							"fills": [{"type":"SOLID","visible":true,"color":{"r":1,"g":1,"b":1,"a":1}}],
							"style": {
								"fontFamily": "Roboto",
								"fontSize": 14,
								"fontWeight": 400
							},
							"characters": "Label"
						}
					]
				}]
			}]
		}
	})");
	FDesignDocument Doc;
	TestTrue(TEXT("Figma parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() == 0)
		return false;

	TFunction<TSharedPtr<FDesignNode>(const TArray<TSharedPtr<FDesignNode>>&, const FString&)> FindTextNodeByContent;
	FindTextNodeByContent = [&](const TArray<TSharedPtr<FDesignNode>>& Nodes, const FString& ExpectedContent) -> TSharedPtr<FDesignNode>
	{
		for (const TSharedPtr<FDesignNode>& Node : Nodes)
		{
			if (!Node.IsValid())
			{
				continue;
			}

			if (Node->Type == EDesignNodeType::Text && Node->Text.Content == ExpectedContent)
			{
				return Node;
			}

			if (TSharedPtr<FDesignNode> FoundChild = FindTextNodeByContent(Node->Children, ExpectedContent))
			{
				return FoundChild;
			}
		}

		return nullptr;
	};

	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());

	bool bObservedRuntimeGrowth = false;
	const auto VerifySingleLineFitLabel = [&](const FString& ExpectedText, const float ExpectedWidth, const float ExpectedHeight, const float SourceFontSize)
	{
		const FUMGWidgetNode* Label = FindWidgetByProperty(Data.RootWidget, TEXT("Text"), &ExpectedText);
		TestTrue(*FString::Printf(TEXT("Find text widget '%s'"), *ExpectedText), Label != nullptr);
		if (!Label)
		{
			return false;
		}

		TSharedPtr<FDesignNode> SourceNode = FindTextNodeByContent(Doc.Children, ExpectedText);
		TestTrue(*FString::Printf(TEXT("Find source text node '%s'"), *ExpectedText), SourceNode.IsValid());
		if (!SourceNode.IsValid())
		{
			return false;
		}

		AddInfo(FString::Printf(
			TEXT("Text fit '%s': source=(%.1f,%.1f) sourceFlags=(%d,%d) widget=(%.1f,%.1f)"),
			*ExpectedText,
			SourceNode->Transform.Width.Value,
			SourceNode->Transform.Height.Value,
			SourceNode->Transform.bSourceHasWidth ? 1 : 0,
			SourceNode->Transform.bSourceHasHeight ? 1 : 0,
			Label->Bounds.W,
			Label->Bounds.H));

		TestTrue(*FString::Printf(TEXT("Parser should preserve the authored fit width for '%s'"), *ExpectedText),
			FMath::IsNearlyEqual(SourceNode->Transform.Width.Value, ExpectedWidth, 0.5f));
		TestTrue(*FString::Printf(TEXT("Parser should preserve the authored fit height for '%s'"), *ExpectedText),
			FMath::IsNearlyEqual(SourceNode->Transform.Height.Value, ExpectedHeight, 0.5f));
		TestTrue(*FString::Printf(TEXT("Runtime fit width should not shrink below the authored minimum for '%s'"), *ExpectedText),
			Label->Bounds.W + 0.5f >= ExpectedWidth);
		TestTrue(*FString::Printf(TEXT("Runtime fit height should not shrink below the authored minimum for '%s'"), *ExpectedText),
			Label->Bounds.H + 0.5f >= ExpectedHeight);
		bObservedRuntimeGrowth |= Label->Bounds.W > ExpectedWidth + 0.5f
			|| Label->Bounds.H > ExpectedHeight + 0.5f;
		TestTrue(*FString::Printf(TEXT("Source font size metadata should be preserved for '%s'"), *ExpectedText),
			FMath::IsNearlyEqual(Label->TextStyle.SourceFontSize, SourceFontSize, 0.01f));
		TestTrue(*FString::Printf(TEXT("Resolved widget font size should stay positive and never exceed the authored size for '%s'"), *ExpectedText),
			Label->TextStyle.FontSize > 0.0f
			&& Label->TextStyle.FontSize <= SourceFontSize + 0.01f);
		const float ExpectedCompiledFontSize =
			SourceFontSize * 72.0f / static_cast<float>(FontConstants::RenderDPI);
		TestTrue(*FString::Printf(TEXT("Single-line fit text '%s' should use the source px-to-point conversion"), *ExpectedText),
			FMath::IsNearlyEqual(Label->TextStyle.FontSize, ExpectedCompiledFontSize, 0.01f));
		return true;
	};

	if (!VerifySingleLineFitLabel(TEXT("Download"), 63.0f, 20.0f, 18.0f))
	{
		return false;
	}

	if (!VerifySingleLineFitLabel(TEXT("Label"), 35.0f, 20.0f, 14.0f))
	{
		return false;
	}

	TestTrue(TEXT("At least one fit label should grow when runtime glyph metrics exceed the authored minimum"), bObservedRuntimeGrowth);

	return true;
}
