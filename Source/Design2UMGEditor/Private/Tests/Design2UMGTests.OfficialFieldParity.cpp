// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

// Official field parity: Pen and Figma parser coverage against the official field vocabulary.

#include "Misc/AutomationTest.h"
#include "Design2UMGTestUtils.h"
#include "Design2UMGConverter.h"
#include "PenParser.h"
#include "FigmaParser.h"
#include "FigmaTestDocBuilder.h"
#include "UMGSchemaSourceExtensions.h"

#if WITH_AUTOMATION_WORKER

using namespace Design2UMGCoreTests;
using namespace Design2UMGTestUtils;


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenOfficialFieldParityTest,
	"Design2UMG.Parity.PenOfficialFields",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPenOfficialFieldParityTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"variables": {
			"flagDisabled": {"type":"boolean","value":false},
			"alphaVar": {"type":"number","value":0.66},
			"underVar": {"type":"boolean","value":true},
			"segA": {"type":"string","value":"Hi"}
		},
		"children": [{
			"id": "frame1",
			"type": "frame",
			"layout": "horizontal",
			"layoutIncludeStroke": false,
			"stroke": "#112233",
			"strokeWidth": 2,
			"strokeAlignment": "inner",
			"strokeLinejoin": "miter",
			"strokeLinecap": "round",
			"children": [{
				"id": "text1",
				"type": "text",
				"content": "Spec",
				"fontStyle": "normal",
				"underline": false,
				"strikethrough": true,
				"href": "https://docs.pencil.dev",
				"fill": {
					"type": "mesh_gradient",
					"blendMode": "multiply",
					"opacity": 0.42,
					"rows": 2,
					"columns": 2,
					"points": [
						{"x": 0, "y": 0, "color": "#000000", "in": [-0.1, 0.0], "out": [0.1, 0.0]},
						{"position": {"x": 1, "y": 1}, "color": "#FFFFFF", "handleIn": [0.9, 1.0], "handleOut": [1.1, 1.0]}
					],
					"colors": [
						{"color": "#000000", "position": 0},
						{"color": "#FFFFFF", "position": 1}
					]
				}
			},{
				"id": "rect1",
				"type": "rectangle",
				"fill": {
					"type": "gradient",
					"gradientType": "radial",
					"center": {"x": 0.2, "y": 0.8},
					"size": {"width": "2.5", "height": 1.25},
					"colors": [
						{"color": "#000000", "position": 0},
						{"color": "#FFFFFF", "position": 1}
					]
				},
				"effect": [{
					"type": "shadow",
					"blendMode": "screen",
					"shadowType": "outer",
					"offset": {"x": 2, "y": 3},
					"blur": 4,
					"spread": 1,
					"color": "#01020340"
				}]
			},{
				"id": "rich1",
				"type": "text",
				"enabled": "$flagDisabled",
				"opacity": "$alphaVar",
				"underline": "$underVar",
				"content": [
					{"content": "$segA", "fontWeight": "700"},
					{"content": " there", "fontStyle": "italic"}
				]
			}]
		}]
	})");
	FDesignDocument Doc;
	TestTrue(TEXT("Pen official parity parse succeeds"), FPenParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() == 0 || Doc.Children[0]->Children.Num() < 3)
		return false;
	const TSharedPtr<FDesignNode>& FrameNode = Doc.Children[0];
	const TSharedPtr<FDesignNode>& TextNode = FrameNode->Children[0];
	const TSharedPtr<FDesignNode>& GradientNode = FrameNode->Children[1];
	const TSharedPtr<FDesignNode>& RichTextNode = FrameNode->Children[2];
	TestTrue(TEXT("layoutIncludeStroke false preserved"), FrameNode->Metadata.FindRef(TEXT("layoutIncludeStroke")) == TEXT("false"));
	TestTrue(TEXT("strokeLinejoin preserved"), FrameNode->Metadata.FindRef(TEXT("stroke.join")) == TEXT("miter"));
	TestTrue(TEXT("strokeLinecap preserved"), FrameNode->Metadata.FindRef(TEXT("stroke.cap")) == TEXT("round"));
	TestTrue(TEXT("fill blendMode preserved"), TextNode->Visual.TopmostFill().BlendMode == TEXT("multiply"));
	TestTrue(TEXT("fill opacity preserved"), FMath::IsNearlyEqual(TextNode->Visual.TopmostFill().Opacity, 0.42f, 0.001f));
	TestTrue(TEXT("text href preserved"), TextNode->Text.Href == TEXT("https://docs.pencil.dev"));
	TestTrue(TEXT("text strikethrough preserved"), TextNode->Text.bStrikethrough);
	TestTrue(TEXT("gradient center parsed"), GradientNode->Visual.TopmostFill().Gradient.Center.IsSet()
		&& FMath::IsNearlyEqual(GradientNode->Visual.TopmostFill().Gradient.Center.GetValue().X, 0.2f, 0.001f)
		&& FMath::IsNearlyEqual(GradientNode->Visual.TopmostFill().Gradient.Center.GetValue().Y, 0.8f, 0.001f));
	TestTrue(TEXT("gradient size parsed"), GradientNode->Visual.TopmostFill().Gradient.Size.IsSet()
		&& FMath::IsNearlyEqual(GradientNode->Visual.TopmostFill().Gradient.Size.GetValue().X, 2.5f, 0.001f)
		&& FMath::IsNearlyEqual(GradientNode->Visual.TopmostFill().Gradient.Size.GetValue().Y, 1.25f, 0.001f));
	TestTrue(TEXT("shadow blendMode preserved"), GradientNode->Visual.Effects.Num() > 0
		&& GradientNode->Visual.Effects[0].BlendMode == TEXT("screen")
		&& GradientNode->Visual.Effects[0].Shadow.BlendMode == TEXT("screen"));
	TestTrue(TEXT("text content array fallback"), RichTextNode->Text.Content == TEXT("Hi there"));
	TestTrue(TEXT("text content array raw preserved"), RichTextNode->Metadata.Contains(TEXT("text.content.raw")));
	TestTrue(TEXT("text runs parsed"), RichTextNode->Text.TextRuns.Num() == 2
		&& RichTextNode->Text.TextRuns[0].FontWeight == TEXT("700")
		&& RichTextNode->Text.TextRuns[1].FontStyle == TEXT("italic"));
	TestTrue(TEXT("boolean variable parsed"), !RichTextNode->Transform.bEnabled && RichTextNode->Text.bUnderline);
	TestTrue(TEXT("number variable parsed"), FMath::IsNearlyEqual(RichTextNode->Transform.Opacity, 0.66f, 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaOfficialFieldParityTest,
	"Design2UMG.Parity.FigmaOfficialFields",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaOfficialFieldParityTest::RunTest(const FString& Parameters)
{
	const FString Json = MakeFigmaDocJson(
		MakeContainerNode(TEXT("0:2"), TEXT("Root"), TEXT("FRAME"), 0, 0, 800, 600,
			FString::Printf(TEXT("%s,%s,%s"),
				*MakeContainerNode(TEXT("gridp"), TEXT("GridParity"), TEXT("FRAME"), 10, 10, 320, 240, FString(),
					TEXT(",\"layoutMode\":\"GRID\",\"layoutWrap\":\"WRAP\",\"itemSpacing\":8,"
						 "\"paddingLeft\":1,\"paddingRight\":2,\"paddingTop\":3,\"paddingBottom\":4,"
						 "\"counterAxisAlignContent\":\"SPACE_BETWEEN\",\"counterAxisSpacing\":6,"
						 "\"layoutPositioning\":\"ABSOLUTE\",\"layoutAlign\":\"STRETCH\","
						 "\"overflowDirection\":\"HORIZONTAL_SCROLLING\","
						 "\"transitionNodeID\":\"1:3\","
						 "\"transitionDuration\":420,"
						 "\"transitionEasing\":{\"type\":\"EASE_OUT\"},"
						 "\"primaryAxisSizingMode\":\"AUTO\",\"counterAxisSizingMode\":\"FIXED\","
						 "\"itemReverseZIndex\":true,\"strokesIncludedInLayout\":false,"
						 "\"minWidth\":100,\"maxWidth\":300,\"minHeight\":50,\"maxHeight\":200,"
						 "\"gridRowCount\":2,\"gridColumnCount\":3,\"gridRowGap\":10,\"gridColumnGap\":12,"
						 "\"gridRowsSizing\":\"AUTO\",\"gridColumnsSizing\":\"FIXED\","
						 "\"gridChildHorizontalAlign\":\"CENTER\",\"gridChildVerticalAlign\":\"MAX\","
						 "\"gridRowSpan\":2,\"gridColumnSpan\":1,\"gridColumnAnchorIndex\":0,\"gridRowAnchorIndex\":1,"
						 "\"layoutGrids\":[{\"pattern\":\"GRID\",\"sectionSize\":8,\"visible\":true}],"
						 "\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"1:2\",\"navigation\":\"NAVIGATE\",\"transition\":{\"type\":\"SMART_ANIMATE\",\"duration\":180,\"direction\":\"LEFT\",\"easing\":{\"type\":\"EASE_IN_AND_OUT\"},\"matchLayers\":true}}]}],"
						 "\"flowStartingPoints\":[{\"nodeId\":\"0:2\",\"name\":\"Flow 1\"}],"
						 "\"componentPropertyReferences\":{\"width\":\"123:1\"},"
						 "\"explicitVariableModes\":{\"1\":\"2:3\"},"
						 "\"resolvedVariableModes\":{\"1\":\"2:3\"},"
						 "\"annotations\":[{\"label\":\"todo\",\"properties\":{}}],"
						 "\"measurements\":[{\"label\":\"w\",\"value\":320}],"
						 "\"exportSettings\":[{\"suffix\":\"@2x\",\"format\":\"PNG\"}],"
						 "\"devStatus\":{\"type\":\"READY_FOR_DEV\"},"
						 "\"branchData\":{\"mainFileKey\":\"main1\",\"branchKey\":\"branch1\"},"
							 "\"boundVariables\":{\"itemSpacing\":{\"type\":\"VARIABLE_ALIAS\",\"id\":\"VariableID:1:2\"}}")),
				*MakeLeafNode(TEXT("paint1"), TEXT("PaintParity"), TEXT("RECTANGLE"), 400, 10, 120, 80,
					TEXT(",\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"opacity\":0.5,\"blendMode\":\"MULTIPLY\",\"color\":{\"r\":1,\"g\":0,\"b\":0}}],"
						 "\"effects\":[{\"type\":\"DROP_SHADOW\",\"visible\":true,\"blendMode\":\"SCREEN\",\"offset\":{\"x\":1,\"y\":2},\"radius\":3,\"spread\":0.5,\"opacity\":0.6,\"color\":{\"r\":0,\"g\":0,\"b\":0}}]")),
				*MakeContainerNode(TEXT("inst1"), TEXT("InstanceParity"), TEXT("INSTANCE"), 560, 10, 120, 80,
					MakeTextNode(TEXT("inst1_txt"), TEXT("Label"), 570, 20, 100, 24, TEXT("Instance text"),
						TEXT("\"fontSize\":16")),
					TEXT(",\"componentId\":\"12:34\",\"componentSetId\":\"56:78\",\"mainComponentKey\":\"component-key-1\","
						 "\"componentProperties\":{\"title\":{\"type\":\"TEXT\",\"value\":\"Hello\"}},"
						 "\"variantProperties\":{\"Active\":\"True\",\"Size\":\"Small\"},"
						 "\"overrides\":[{\"id\":\"inst1_txt\",\"path\":[\"characters\"],\"value\":\"Override\"}],"
						 "\"isExposedInstance\":true")))),
		TEXT(",\"components\":{\"12:34\":{\"key\":\"component-key-1\",\"name\":\"Sidebar Item Active?=True\",\"description\":\"\",\"remote\":true,\"componentSetId\":\"56:78\",\"componentPropertyDefinitions\":{\"title\":{\"type\":\"TEXT\",\"defaultValue\":\"Hello\"}},\"documentationLinks\":[{\"uri\":\"https://example.com/component\"}]}},"
			 "\"componentSets\":{\"56:78\":{\"key\":\"component-set-key-1\",\"name\":\"Sidebar Item\",\"description\":\"\",\"remote\":true,\"componentPropertyDefinitions\":{\"Active\":{\"type\":\"BOOLEAN\",\"defaultValue\":true}},\"variantGroupProperties\":{\"Active\":{\"values\":[\"True\",\"False\"]}}}},"
			 "\"prototypeStartNodeID\":\"0:2\","
			 "\"prototypeDevice\":{\"type\":\"PRESET\",\"size\":\"DESKTOP\"},"
			 "\"flowStartingPoints\":[{\"nodeId\":\"0:2\",\"name\":\"Doc Flow\"}]"));
	FDesignDocument Doc;
	TestTrue(TEXT("Figma official parity parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	if (Doc.Children.Num() == 0 || Doc.Children[0]->Children.Num() < 3)
		return false;
	const TSharedPtr<FDesignNode>& Node = Doc.Children[0]->Children[0];
	const TSharedPtr<FDesignNode>& PaintNode = Doc.Children[0]->Children[1];
	const TSharedPtr<FDesignNode>& InstanceNode = Doc.Children[0]->Children[2];
	TestTrue(TEXT("figma layoutPositioning"), Node->Metadata.FindRef(TEXT("layoutPositioning")) == TEXT("ABSOLUTE"));
	TestTrue(TEXT("figma counterAxisSpacing"), Node->Metadata.Contains(TEXT("counterAxisSpacing")));
	TestTrue(TEXT("figma layoutAlign"), Node->Metadata.FindRef(TEXT("layoutAlign")) == TEXT("STRETCH"));
	TestTrue(TEXT("figma sizing modes"), Node->Metadata.Contains(TEXT("primaryAxisSizingMode")) && Node->Metadata.Contains(TEXT("counterAxisSizingMode")));
	TestTrue(TEXT("figma min/max"), Node->Metadata.Contains(TEXT("minWidth")) && Node->Metadata.Contains(TEXT("maxHeight")));
	TestTrue(TEXT("figma grid keys"), Node->Metadata.Contains(TEXT("gridRowCount")) && Node->Metadata.Contains(TEXT("gridColumnsSizing")));
	TestTrue(TEXT("figma grid child align"), Node->Metadata.Contains(TEXT("gridChildHorizontalAlign")) && Node->Metadata.Contains(TEXT("gridChildVerticalAlign")));
	TestTrue(TEXT("figma grid span/anchor"), Node->Metadata.Contains(TEXT("gridRowSpan")) && Node->Metadata.Contains(TEXT("gridColumnAnchorIndex")));
	TestTrue(TEXT("figma bool keys"), Node->Metadata.FindRef(TEXT("itemReverseZIndex")) == TEXT("true"));
	TestTrue(TEXT("figma boundVariables"), Node->Metadata.Contains(TEXT("boundVariables.itemSpacing")));
	TestTrue(TEXT("figma overflowDirection"), Node->Metadata.FindRef(TEXT("overflowDirection")) == TEXT("HORIZONTAL_SCROLLING"));
	TestTrue(TEXT("figma layoutGrids"), Node->Metadata.Contains(TEXT("layoutGrids")));
	TestTrue(TEXT("figma interactions"), Node->Metadata.Contains(TEXT("interactions")));
	TestTrue(TEXT("figma flowStartingPoints"), Node->Metadata.Contains(TEXT("flowStartingPoints")));
	TestTrue(TEXT("figma componentPropertyReferences"), Node->Metadata.Contains(TEXT("componentPropertyReferences")));
	TestTrue(TEXT("figma explicitVariableModes"), Node->Metadata.Contains(TEXT("explicitVariableModes")));
	TestTrue(TEXT("figma resolvedVariableModes"), Node->Metadata.Contains(TEXT("resolvedVariableModes")));
	TestTrue(TEXT("figma annotations"), Node->Metadata.Contains(TEXT("annotations")));
	TestTrue(TEXT("figma measurements"), Node->Metadata.Contains(TEXT("measurements")));
	TestTrue(TEXT("figma exportSettings"), Node->Metadata.Contains(TEXT("exportSettings")));
	TestTrue(TEXT("figma devStatus"), Node->Metadata.Contains(TEXT("devStatus")));
	TestTrue(TEXT("figma branchData"), Node->Metadata.Contains(TEXT("branchData")));
	TestTrue(TEXT("figma components dictionary"), Doc.Metadata.Contains(TEXT("components")));
	TestTrue(TEXT("figma componentSets dictionary"), Doc.Metadata.Contains(TEXT("componentSets")));
	TestTrue(TEXT("figma components json field"), !Doc.SourceDocument().ComponentsJson.IsEmpty());
	TestTrue(TEXT("figma componentSets json field"), !Doc.SourceDocument().ComponentSetsJson.IsEmpty());
	TestTrue(TEXT("figma components json carries component defs"), Doc.SourceDocument().ComponentsJson.Contains(TEXT("componentPropertyDefinitions")));
	TestTrue(TEXT("figma componentSets json carries variant groups"), Doc.SourceDocument().ComponentSetsJson.Contains(TEXT("variantGroupProperties")));
	TestTrue(TEXT("figma doc prototypeStartNodeID"), Doc.Metadata.FindRef(TEXT("prototypeStartNodeID")) == TEXT("0:2"));
	TestTrue(TEXT("figma doc prototypeDevice"), Doc.Metadata.Contains(TEXT("prototypeDevice")));
	TestTrue(TEXT("figma absolute positioning semantic"), Node->LayoutData.bAbsoluteLayoutPositioning);
	TestTrue(TEXT("figma wrap cross-axis gap semantic"), Node->LayoutData.WrapCrossAxisGap.IsSet() && FMath::IsNearlyEqual(Node->LayoutData.WrapCrossAxisGap.GetValue(), 6.0f, 0.001f));
	TestTrue(TEXT("figma layoutWrap metadata"), Node->Metadata.FindRef(TEXT("layoutWrap")) == TEXT("WRAP"));
	TestTrue(TEXT("figma strokesIncludedInLayout metadata"), Node->Metadata.FindRef(TEXT("strokesIncludedInLayout")) == TEXT("false"));
	TestTrue(TEXT("figma itemReverseZIndex metadata"), Node->Metadata.FindRef(TEXT("itemReverseZIndex")) == TEXT("true"));
	TestTrue(TEXT("figma boundVariables metadata"), Node->Metadata.Contains(TEXT("boundVariables")));
	TestEqual(TEXT("figma overflowDirection semantic"), Node->LayoutData.Overflow, EDesignOverflow::HorizontalScrolling);
	TestTrue(TEXT("figma prototype transitionNodeID typed"), Node->Source().Prototype.TransitionNodeId == TEXT("1:3"));
	TestTrue(TEXT("figma prototype legacy transition typed"), Node->Source().Prototype.Transition.Duration.IsSet()
		&& FMath::IsNearlyEqual(Node->Source().Prototype.Transition.Duration.GetValue(), 420.0f, 0.001f)
		&& Node->Source().Prototype.Transition.EasingType == TEXT("EASE_OUT"));
	TestTrue(TEXT("figma prototype interactions typed"), Node->Source().Prototype.Interactions.Num() == 1
		&& Node->Source().Prototype.Interactions[0].TriggerType == TEXT("ON_CLICK")
		&& Node->Source().Prototype.Interactions[0].Actions.Num() == 1
		&& Node->Source().Prototype.Interactions[0].Actions[0].Type == TEXT("NODE")
		&& Node->Source().Prototype.Interactions[0].Actions[0].DestinationId == TEXT("1:2")
		&& Node->Source().Prototype.Interactions[0].Actions[0].Navigation == TEXT("NAVIGATE")
		&& Node->Source().Prototype.Interactions[0].Actions[0].Transition.Type == TEXT("SMART_ANIMATE")
		&& Node->Source().Prototype.Interactions[0].Actions[0].Transition.Direction == TEXT("LEFT")
		&& Node->Source().Prototype.Interactions[0].Actions[0].Transition.Duration.IsSet()
		&& FMath::IsNearlyEqual(Node->Source().Prototype.Interactions[0].Actions[0].Transition.Duration.GetValue(), 180.0f, 0.001f)
		&& Node->Source().Prototype.Interactions[0].Actions[0].Transition.EasingType == TEXT("EASE_IN_AND_OUT")
		&& Node->Source().Prototype.Interactions[0].Actions[0].Transition.MatchLayers.IsSet()
		&& Node->Source().Prototype.Interactions[0].Actions[0].Transition.MatchLayers.GetValue());
	TestTrue(TEXT("figma prototype flowStartingPoints typed"), Node->Source().Prototype.FlowStartingPoints.Num() == 1
		&& Node->Source().Prototype.FlowStartingPoints[0].NodeId == TEXT("0:2")
		&& Node->Source().Prototype.FlowStartingPoints[0].Name == TEXT("Flow 1"));
	TestTrue(TEXT("figma doc prototypeStartNodeID typed"), Doc.SourceDocument().PrototypeStartNodeId == TEXT("0:2"));
	TestTrue(TEXT("figma doc prototypeDevice metadata carries device type"), Doc.Metadata.FindRef(TEXT("prototypeDevice")).Contains(TEXT("DESKTOP")));
	TestTrue(TEXT("figma doc flowStartingPoints typed"), Doc.SourceDocument().FlowStartingPoints.Num() == 1
		&& Doc.SourceDocument().FlowStartingPoints[0].NodeId == TEXT("0:2")
		&& Doc.SourceDocument().FlowStartingPoints[0].Name == TEXT("Doc Flow"));
	TestTrue(TEXT("figma fill blendMode preserved"), PaintNode->Visual.TopmostFill().BlendMode == TEXT("MULTIPLY"));
	TestTrue(TEXT("figma fill opacity preserved"), FMath::IsNearlyEqual(PaintNode->Visual.TopmostFill().Opacity, 0.5f, 0.001f));
	TestTrue(TEXT("figma effect blendMode preserved"), PaintNode->Visual.Effects.Num() > 0
		&& PaintNode->Visual.Effects[0].BlendMode == TEXT("SCREEN")
		&& PaintNode->Visual.Effects[0].Shadow.BlendMode == TEXT("SCREEN"));
	TestTrue(TEXT("figma source node type preserved"), InstanceNode->Source().SourceNodeType == TEXT("INSTANCE"));
	TestTrue(TEXT("figma instance componentId preserved"), InstanceNode->Metadata.FindRef(TEXT("instanceComponentId")) == TEXT("12:34"));
	TestTrue(TEXT("figma instance ext flag"), InstanceNode->Source().ComponentInstance.bIsInstance);
	TestTrue(TEXT("figma instance ext componentId"), InstanceNode->Source().ComponentInstance.ComponentId == TEXT("12:34"));
	TestTrue(TEXT("figma instance ext componentSetId"), InstanceNode->Source().ComponentInstance.ComponentSetId == TEXT("56:78"));
	TestTrue(TEXT("figma instance ext mainComponentKey"), InstanceNode->Source().ComponentInstance.MainComponentKey == TEXT("component-key-1"));
	TestTrue(TEXT("figma instance ext componentProperties"), !InstanceNode->Source().ComponentInstance.ComponentPropertiesJson.IsEmpty());
	TestTrue(TEXT("figma instance ext variantProperties"), !InstanceNode->Source().ComponentInstance.VariantPropertiesJson.IsEmpty());
	TestTrue(TEXT("figma instance ext overrides"), !InstanceNode->Source().ComponentInstance.OverridesJson.IsEmpty());
	TestTrue(TEXT("figma instance ext isExposedInstance"), InstanceNode->Source().ComponentInstance.IsExposedInstance.Get(false));
	TestTrue(TEXT("figma instance componentSetId preserved"), InstanceNode->Metadata.FindRef(TEXT("componentSetId")) == TEXT("56:78"));
	TestTrue(TEXT("figma instance mainComponentKey preserved"), InstanceNode->Metadata.FindRef(TEXT("mainComponentKey")) == TEXT("component-key-1"));
TestTrue(TEXT("figma instance componentProperties preserved"), InstanceNode->Metadata.Contains(TEXT("componentProperties")));
TestTrue(TEXT("figma instance variantProperties preserved"), InstanceNode->Metadata.Contains(TEXT("variantProperties")));
TestTrue(TEXT("figma instance overrides preserved"), InstanceNode->Metadata.Contains(TEXT("overrides")));
TestTrue(TEXT("figma instance isExposedInstance preserved"), InstanceNode->Metadata.FindRef(TEXT("isExposedInstance")) == TEXT("true"));

	const FUMGWidgetData ConvertedData = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("figma document components catalog exported"), ConvertedData.SourceDocument().ComponentsJson.Contains(TEXT("component-key-1")));
	TestTrue(TEXT("figma document componentSets catalog exported"), ConvertedData.SourceDocument().ComponentSetsJson.Contains(TEXT("component-set-key-1")));

	const FUMGWidgetNode* ConvertedInstanceWidget = FindWidgetById(ConvertedData.RootWidget, TEXT("inst1"));
	TestNotNull(TEXT("figma instance widget exported to UMG tree"), ConvertedInstanceWidget);
	if (!ConvertedInstanceWidget)
	{
		return false;
	}

	TestTrue(TEXT("figma instance typed node data exported"), ConvertedInstanceWidget->Source().IsFigmaInstance());
	TestEqual(TEXT("figma instance source node type exported"), ConvertedInstanceWidget->Source().SourceNodeType, TEXT("INSTANCE"));
	TestEqual(TEXT("figma instance componentId exported"), ConvertedInstanceWidget->Source().ComponentId, TEXT("12:34"));
	TestEqual(TEXT("figma instance componentSetId exported"), ConvertedInstanceWidget->Source().ComponentSetId, TEXT("56:78"));
	TestEqual(TEXT("figma instance mainComponentKey exported"), ConvertedInstanceWidget->Source().MainComponentKey, TEXT("component-key-1"));
	TestTrue(TEXT("figma instance componentProperties exported"), !ConvertedInstanceWidget->Source().ComponentPropertiesJson.IsEmpty());
	TestTrue(TEXT("figma instance variantProperties exported"), !ConvertedInstanceWidget->Source().VariantPropertiesJson.IsEmpty());
	TestTrue(TEXT("figma instance overrides exported"), !ConvertedInstanceWidget->Source().OverridesJson.IsEmpty());
	TestTrue(TEXT("figma instance exposed flag exported"),
		ConvertedInstanceWidget->Source().HasIsExposedInstance.IsSet()
			&& ConvertedInstanceWidget->Source().HasIsExposedInstance.GetValue());
	return true;
}

#endif

