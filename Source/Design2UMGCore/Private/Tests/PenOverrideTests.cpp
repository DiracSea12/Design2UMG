// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "PenParser.h"
#include "RefExpander.h"
#include "DesignSchema.h"

namespace
{
	TSharedPtr<FDesignNode> ExpandPatchedNode(
		FAutomationTestBase& Test, const FString& Base, const FString& Patch, bool bDescendant)
	{
		const FString Target = FString::Printf(TEXT("{\"id\":\"target\",\"reusable\":true,%s}"), *Base);
		const FString Json = bDescendant
			? FString::Printf(TEXT("{\"version\":\"2.14\",\"children\":[{\"id\":\"base\",\"type\":\"frame\",\"reusable\":true,\"children\":[%s]},{\"id\":\"instance\",\"type\":\"ref\",\"ref\":\"base\",\"descendants\":{\"target\":{%s}}}]}"), *Target, *Patch)
			: FString::Printf(TEXT("{\"version\":\"2.14\",\"children\":[%s,{\"id\":\"instance\",\"type\":\"ref\",\"ref\":\"target\"%s%s}]}"), *Target, Patch.IsEmpty() ? TEXT("") : TEXT(","), *Patch);
		FDesignDocument Doc;
		if (!Test.TestTrue(TEXT("Patch fixture parses"), FPenParser::ParseDocument(Json, Doc))) return nullptr;
		TSharedPtr<FDesignNode> Result = RefExpander::ExpandRef(Doc.Children.Last(), Doc).ExpandedNode;
		if (!Test.TestTrue(TEXT("Patch fixture expands"), Result.IsValid())) return nullptr;
		return bDescendant ? Result->Children[0] : Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenOverrideTextDefaultsTest,
	"Design2UMG.Unit.PenParser.Override.TextDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPenOverrideTextDefaultsTest::RunTest(const FString&)
{
	const FString Base = TEXT(R"JSON("type":"text","content":[{"content":"Before","fontWeight":"bold"}],"fontSize":28,"fontWeight":"bold","fontStyle":"italic","textAlign":"center","textAlignVertical":"bottom","textGrowth":"fixed-width-height","underline":true,"strikethrough":true,"href":"https://example.test","lineHeight":1.5,"letterSpacing":2)JSON");
	const FString Patch = TEXT(R"JSON("content":"","textAlign":"left","textAlignVertical":"top","textGrowth":"auto","underline":false,"strikethrough":false,"href":"","lineHeight":0,"letterSpacing":0)JSON");
	for (const bool bDescendant : {false, true})
	{
		const TSharedPtr<FDesignNode> Node = ExpandPatchedNode(*this, Base, Patch, bDescendant);
		if (!Node.IsValid()) return false;
		TestTrue(TEXT("Content clears"), Node->Text.Content.IsEmpty());
		TestTrue(TEXT("Replacing rich content with plain content clears old runs"), Node->Text.TextRuns.IsEmpty());
		TestFalse(TEXT("Old rich-content metadata does not survive"), Node->Metadata.Contains(TEXT("text.content.raw")));
		TestEqual(TEXT("Center to Left"), Node->Text.TextAlign, EDesignTextAlign::Left);
		TestEqual(TEXT("Bottom to Top"), Node->Text.TextAlignVertical, EDesignTextAlignVertical::Top);
		TestEqual(TEXT("Fixed growth to Auto"), Node->Text.TextGrowth, EDesignTextGrowth::Auto);
		TestFalse(TEXT("Underline true to false"), Node->Text.bUnderline);
		TestFalse(TEXT("Strikethrough true to false"), Node->Text.bStrikethrough);
		TestTrue(TEXT("Href clears"), Node->Text.Href.IsEmpty());
		TestEqual(TEXT("Line height clears to zero"), Node->Text.LineHeight, 0.0f);
		TestEqual(TEXT("Letter spacing explicitly zero"), Node->Text.LetterSpacing.Get(-1.0f), 0.0f);
		TestEqual(TEXT("Missing font size is inherited"), Node->Text.FontSize, 28.0f);
		TestEqual(TEXT("Missing weight preserves bold"), Node->Text.FontWeight, FString(TEXT("bold")));
		TestEqual(TEXT("Missing style preserves italic"), Node->Text.FontStyle, FString(TEXT("italic")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenOverrideContentOnlyTest,
	"Design2UMG.Unit.PenParser.Override.ContentOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPenOverrideContentOnlyTest::RunTest(const FString&)
{
	const FString Base = TEXT(R"JSON("type":"text","content":"Before","fontFamily":"Inter","fontPostScriptName":"Inter-BoldItalic","fontSize":28,"fontWeight":"bold","fontStyle":"italic","underline":true,"strikethrough":true,"textAlign":"center","href":"https://example.test")JSON");
	for (const bool bDescendant : {false, true})
	{
		const TSharedPtr<FDesignNode> Node = ExpandPatchedNode(*this, Base, TEXT("\"content\":\"After\""), bDescendant);
		if (!Node.IsValid()) return false;
		TestEqual(TEXT("Content changed"), Node->Text.Content, FString(TEXT("After")));
		TestEqual(TEXT("Bold survives content-only patch"), Node->Text.FontWeight, FString(TEXT("bold")));
		TestEqual(TEXT("Italic survives content-only patch"), Node->Text.FontStyle, FString(TEXT("italic")));
		TestEqual(TEXT("Font size survives content-only patch"), Node->Text.FontSize, 28.0f);
		TestEqual(TEXT("PostScript name survives content-only patch"), Node->Text.FontPostScriptName, FString(TEXT("Inter-BoldItalic")));
		TestTrue(TEXT("Missing underline preserves true"), Node->Text.bUnderline);
		TestTrue(TEXT("Missing strikethrough preserves true"), Node->Text.bStrikethrough);
		TestEqual(TEXT("Missing alignment preserves center"), Node->Text.TextAlign, EDesignTextAlign::Center);
		TestEqual(TEXT("Missing href preserves link"), Node->Text.Href, FString(TEXT("https://example.test")));
		const TSharedPtr<FDesignNode> Normal = ExpandPatchedNode(*this, Base, TEXT("\"fontWeight\":\"normal\",\"fontStyle\":\"normal\",\"fontFamily\":\"\",\"fontPostScriptName\":\"\""), bDescendant);
		if (!Normal.IsValid()) return false;
		TestEqual(TEXT("Explicit normal weight applies"), Normal->Text.FontWeight, FString(TEXT("normal")));
		TestEqual(TEXT("Explicit normal style applies"), Normal->Text.FontStyle, FString(TEXT("normal")));
		TestTrue(TEXT("Font family clears"), Normal->Text.FontFamily.IsEmpty());
		TestTrue(TEXT("PostScript name clears"), Normal->Text.FontPostScriptName.IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenOverrideLayoutTransformTest,
	"Design2UMG.Unit.PenParser.Override.LayoutTransform",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPenOverrideLayoutTransformTest::RunTest(const FString&)
{
	const FString Base = TEXT(R"JSON("type":"frame","name":"Before","layout":"vertical","gap":12,"padding":8,"justifyContent":"end","alignItems":"end","clip":true,"enabled":false,"rotation":45,"flipX":true,"flipY":true,"opacity":0.4,"x":12,"y":16,"width":100,"height":80)JSON");
	const FString Patch = TEXT(R"JSON("name":"","layout":"horizontal","gap":0,"padding":0,"justifyContent":"start","alignItems":"start","clip":false,"enabled":true,"rotation":0,"flipX":false,"flipY":false,"opacity":1,"x":0,"y":0,"width":"fit_content","height":"fill_container")JSON");
	for (const bool bDescendant : {false, true})
	{
		const TSharedPtr<FDesignNode> Node = ExpandPatchedNode(*this, Base, Patch, bDescendant);
		if (!Node.IsValid()) return false;
		const TSharedPtr<FDesignNode> Unchanged = ExpandPatchedNode(*this, Base, TEXT(""), bDescendant);
		if (!Unchanged.IsValid()) return false;
		TestEqual(TEXT("Missing name preserves component name"), Unchanged->Name, FString(TEXT("Before")));
		TestEqual(TEXT("Missing layout preserves vertical"), Unchanged->LayoutData.Layout, EDesignLayout::Vertical);
		TestFalse(TEXT("Missing enabled preserves false"), Unchanged->Transform.bEnabled);
		TestEqual(TEXT("Missing rotation preserves authored angle"), Unchanged->Transform.Rotation, 45.0f);
		TestTrue(TEXT("Missing flip preserves true"), Unchanged->Transform.bFlipX);
		TestTrue(TEXT("Missing clip preserves true"), Unchanged->LayoutData.ExplicitClip.Get(false));
		TestEqual(TEXT("Missing gap preserves authored gap"), Unchanged->LayoutData.ExplicitGap.Get(-1.0f), 12.0f);
		TestTrue(TEXT("Name clears"), Node->Name.IsEmpty());
		TestEqual(TEXT("Vertical to Horizontal"), Node->LayoutData.Layout, EDesignLayout::Horizontal);
		TestEqual(TEXT("Gap becomes explicit zero"), Node->LayoutData.ExplicitGap.Get(-1.0f), 0.0f);
		TestEqual(TEXT("Padding becomes zero"), Node->LayoutData.ExplicitPadding.GetValue().Left, 0.0f);
		TestEqual(TEXT("Justify End to Start"), Node->LayoutData.JustifyContent, EDesignJustifyContent::Start);
		TestEqual(TEXT("Align End to Start"), Node->LayoutData.AlignItems, EDesignAlignItems::Start);
		TestFalse(TEXT("Clip true to false"), Node->LayoutData.ExplicitClip.Get(true));
		TestTrue(TEXT("Enabled false to true"), Node->Transform.bEnabled);
		TestEqual(TEXT("Rotation resets"), Node->Transform.Rotation, 0.0f);
		TestFalse(TEXT("FlipX resets"), Node->Transform.bFlipX);
		TestFalse(TEXT("FlipY resets"), Node->Transform.bFlipY);
		TestEqual(TEXT("Opacity resets"), Node->Transform.Opacity, 1.0f);
		TestEqual(TEXT("X explicitly zero"), Node->Transform.ExplicitX.Get(-1.0f), 0.0f);
		TestEqual(TEXT("Y explicitly zero"), Node->Transform.ExplicitY.Get(-1.0f), 0.0f);
		TestTrue(TEXT("Authored fit width applies"), Node->Transform.Width.IsFit() && Node->Transform.bHasWidth && Node->Transform.bSourceHasWidth);
		TestTrue(TEXT("Authored fill height applies"), Node->Transform.Height.IsFill() && Node->Transform.bHasHeight && Node->Transform.bSourceHasHeight);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenOverrideChildrenTest,
	"Design2UMG.Unit.PenParser.Override.Children",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPenOverrideChildrenTest::RunTest(const FString&)
{
	const FString Base = TEXT(R"JSON("type":"frame","children":[{"id":"old","type":"text","content":"Before"}])JSON");
	for (const bool bDescendant : {false, true})
	{
		const TSharedPtr<FDesignNode> Absent = ExpandPatchedNode(*this, Base, TEXT(""), bDescendant);
		const TSharedPtr<FDesignNode> Clear = ExpandPatchedNode(*this, Base, TEXT("\"children\":[]"), bDescendant);
		const TSharedPtr<FDesignNode> Replace = ExpandPatchedNode(*this, Base, TEXT(R"JSON("children":[{"id":"new","type":"text","content":"After"}])JSON"), bDescendant);
		if (!Absent.IsValid() || !Clear.IsValid() || !Replace.IsValid()) return false;
		TestEqual(TEXT("Absent children preserve subtree"), Absent->Children.Num(), 1);
		TestEqual(TEXT("Empty children clear subtree"), Clear->Children.Num(), 0);
		TestEqual(TEXT("Provided children replace rather than append"), Replace->Children.Num(), 1);
		TestEqual(TEXT("Replacement child is used"), Replace->Children[0]->Id, FString(TEXT("new")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenOverrideVisualTest,
	"Design2UMG.Unit.PenParser.Override.Visual",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPenOverrideVisualTest::RunTest(const FString&)
{
	const FString Base = TEXT(R"JSON("type":"frame","fill":"#ff0000","stroke":"#000000","strokeWidth":3,"cornerRadius":8,"effect":[{"type":"shadow","blur":4}])JSON");
	for (const bool bDescendant : {false, true})
	{
		const TSharedPtr<FDesignNode> Clear = ExpandPatchedNode(*this, Base, TEXT(R"JSON("fill":[],"strokeWidth":0,"cornerRadius":0,"effect":[])JSON"), bDescendant);
		if (!Clear.IsValid()) return false;
		TestTrue(TEXT("Fills clear"), Clear->Visual.Fills.IsEmpty() && Clear->Visual.bFillExplicitClear);
		TestFalse(TEXT("Zero stroke thickness applies"), Clear->Visual.Stroke.HasStroke());
		TestEqual(TEXT("Corner radius resets"), Clear->Visual.CornerRadius.Uniform, 0.0f);
		TestTrue(TEXT("Effects clear"), Clear->Visual.Effects.IsEmpty() && Clear->Visual.bEffectsExplicitClear);
		const TSharedPtr<FDesignNode> WidthOnly = ExpandPatchedNode(*this, Base, TEXT("\"strokeWidth\":1"), bDescendant);
		if (!WidthOnly.IsValid()) return false;
		TestEqual(TEXT("Stroke thickness changes"), WidthOnly->Visual.Stroke.Thickness.Uniform, 1.0f);
		TestTrue(TEXT("Stroke width alone preserves explicit paint"), WidthOnly->Visual.Stroke.Fill.IsSet());
		TestEqual(TEXT("Absent effects preserve shadow"), WidthOnly->Visual.Effects.Num(), 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenOverrideNestedPrecedenceTest,
	"Design2UMG.Unit.PenParser.Override.NestedPrecedence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPenOverrideNestedPrecedenceTest::RunTest(const FString&)
{
	const FString Json = TEXT(R"JSON({"version":"2.14","children":[
		{"id":"base","type":"frame","reusable":true,"gap":12,"clip":true,"children":[
			{"id":"label","type":"text","content":"Base","fontWeight":"bold","fontStyle":"italic","textAlign":"center","href":"https://example.test"}]},
		{"id":"middle","type":"ref","ref":"base","reusable":true,"gap":8,"descendants":{
			"label":{"content":[{"content":"Middle","fontWeight":"bold"}],"underline":true,"fontSize":24}}},
		{"id":"outer","type":"ref","ref":"middle","gap":0,"clip":false,"descendants":{
			"label":{"content":"","underline":false,"href":"","textAlign":"left"}}}
	]})JSON");
	FDesignDocument Doc;
	if (!TestTrue(TEXT("Nested fixture parses"), FPenParser::ParseDocument(Json, Doc))) return false;
	const TSharedPtr<FDesignNode> Node = RefExpander::ExpandRef(Doc.Children[2], Doc).ExpandedNode;
	if (!TestTrue(TEXT("Nested ref expands"), Node.IsValid())) return false;
	TestEqual(TEXT("Outer gap wins including zero"), Node->LayoutData.ExplicitGap.Get(-1.0f), 0.0f);
	TestFalse(TEXT("Outer false clip wins"), Node->LayoutData.ExplicitClip.Get(true));
	const FDesignNode& Label = *Node->Children[0];
	TestTrue(TEXT("Outer empty content wins"), Label.Text.Content.IsEmpty());
	TestTrue(TEXT("Outer plain text clears inner rich runs"), Label.Text.TextRuns.IsEmpty());
	TestFalse(TEXT("Outer plain text clears inner raw metadata"), Label.Metadata.Contains(TEXT("text.content.raw")));
	TestFalse(TEXT("Outer false beats inner true"), Label.Text.bUnderline);
	TestTrue(TEXT("Outer href clear wins"), Label.Text.Href.IsEmpty());
	TestEqual(TEXT("Outer default align wins"), Label.Text.TextAlign, EDesignTextAlign::Left);
	TestEqual(TEXT("Inner font size remains when outer omits it"), Label.Text.FontSize, 24.0f);
	TestEqual(TEXT("Base bold survives both layers"), Label.Text.FontWeight, FString(TEXT("bold")));
	TestEqual(TEXT("Base italic survives both layers"), Label.Text.FontStyle, FString(TEXT("italic")));
	TestEqual(TEXT("Source component remains unchanged"), Doc.Children[0]->Children[0]->Text.Content, FString(TEXT("Base")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenOverrideDeferredRefTest,
	"Design2UMG.Unit.PenParser.Override.DeferredRefPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPenOverrideDeferredRefTest::RunTest(const FString&)
{
	const FString Json = TEXT(R"JSON({"version":"2.14","children":[
		{"id":"leaf","type":"frame","reusable":true,"gap":9,"enabled":false,"children":[
			{"id":"label","type":"text","content":"Base","fontWeight":"bold","fontStyle":"italic","underline":true}]},
		{"id":"wrapper","type":"frame","reusable":true,"children":[
			{"id":"nested","type":"ref","ref":"leaf","descendants":{"label":{"content":"Inner","fontSize":24}}}]},
		{"id":"outer","type":"ref","ref":"wrapper","descendants":{
			"nested":{"gap":0,"enabled":true},"nested/label":{"content":"","underline":false}}}
	]})JSON");
	FDesignDocument Doc;
	if (!TestTrue(TEXT("Deferred fixture parses"), FPenParser::ParseDocument(Json, Doc))) return false;
	const TSharedPtr<FDesignNode> Wrapper = RefExpander::ExpandRef(Doc.Children[2], Doc).ExpandedNode;
	if (!TestTrue(TEXT("Wrapper expands"), Wrapper.IsValid())) return false;
	const TSharedPtr<FDesignNode> Nested = RefExpander::ExpandRef(Wrapper->Children[0], Doc).ExpandedNode;
	if (!TestTrue(TEXT("Nested ref expands"), Nested.IsValid())) return false;
	TestEqual(TEXT("Patch on ref root survives later expansion"), Nested->LayoutData.ExplicitGap.Get(-1.0f), 0.0f);
	TestTrue(TEXT("Ref root can be re-enabled"), Nested->Transform.bEnabled);
	const FDesignNode& Label = *Nested->Children[0];
	TestTrue(TEXT("Deferred outer path clears content"), Label.Text.Content.IsEmpty());
	TestFalse(TEXT("Deferred outer path clears underline"), Label.Text.bUnderline);
	TestEqual(TEXT("Deferred path keeps inner font size"), Label.Text.FontSize, 24.0f);
	TestEqual(TEXT("Deferred path keeps base bold"), Label.Text.FontWeight, FString(TEXT("bold")));
	TestEqual(TEXT("Deferred path keeps base italic"), Label.Text.FontStyle, FString(TEXT("italic")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenOverrideReplacementIsolationTest,
	"Design2UMG.Unit.PenParser.Override.ReplacementIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPenOverrideReplacementIsolationTest::RunTest(const FString&)
{
	const FString Json = TEXT(R"JSON({"version":"2.14","children":[
		{"id":"base","type":"frame","reusable":true,"children":[{"id":"slot","type":"frame"}]},
		{"id":"middle","type":"ref","ref":"base","reusable":true,"descendants":{
			"slot":{"id":"replacement","type":"frame","gap":9,"children":[{"id":"old","type":"text","content":"Old"}]}}},
		{"id":"outer","type":"ref","ref":"middle","descendants":{
			"slot":{"gap":0,"children":[{"id":"fresh","type":"text","content":"New"}]}}}
	]})JSON");
	FDesignDocument Doc;
	if (!TestTrue(TEXT("Replacement fixture parses"), FPenParser::ParseDocument(Json, Doc))) return false;
	const TSharedPtr<FDesignNode> First = RefExpander::ExpandRef(Doc.Children[2], Doc).ExpandedNode;
	if (!TestTrue(TEXT("Replacement expands"), First.IsValid())) return false;
	TestEqual(TEXT("Typed descendant performs full replacement"), First->Children[0]->Id, FString(TEXT("replacement")));
	TestEqual(TEXT("Outer partial patch applies on replacement"), First->Children[0]->LayoutData.ExplicitGap.Get(-1.0f), 0.0f);
	TestEqual(TEXT("Outer children replace inner children"), First->Children[0]->Children[0]->Id, FString(TEXT("fresh")));
	First->Children[0]->Children[0]->Text.Content = TEXT("Mutated");
	const TSharedPtr<FDesignNode> Second = RefExpander::ExpandRef(Doc.Children[2], Doc).ExpandedNode;
	if (!TestTrue(TEXT("Second instance expands"), Second.IsValid())) return false;
	TestEqual(TEXT("Instances do not share patched children"), Second->Children[0]->Children[0]->Text.Content, FString(TEXT("New")));
	TestEqual(TEXT("Source replacement is unchanged"), Doc.Children[1]->Descendants.FindChecked(TEXT("slot")).Replacement->Children[0]->Text.Content, FString(TEXT("Old")));
	const TSharedPtr<FDesignNode> Copy = RefExpander::DeepCopyNode(Doc.Children[2]);
	Copy->Descendants.FindChecked(TEXT("slot")).Patch.Children.GetValue()[0]->Text.Content = TEXT("Copy only");
	TestEqual(TEXT("Deep copy isolates patch children"), Doc.Children[2]->Descendants.FindChecked(TEXT("slot")).Patch.Children.GetValue()[0]->Text.Content, FString(TEXT("New")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenOverridePaintPrecedenceTest,
	"Design2UMG.Unit.PenParser.Override.PaintPrecedence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPenOverridePaintPrecedenceTest::RunTest(const FString&)
{
	const FString Json = TEXT(R"JSON({"version":"2.14","children":[
		{"id":"base","type":"frame","reusable":true,"fill":"#ff0000","stroke":"#000000","strokeWidth":3},
		{"id":"painted","type":"ref","ref":"base","reusable":true,"stroke":"#ffffff"},
		{"id":"clear","type":"ref","ref":"painted","reusable":true,"fill":[]},
		{"id":"repaint","type":"ref","ref":"clear","fill":"#00ff00"},
		{"id":"explicit-stroke","type":"ref","ref":"clear","fill":[],"stroke":"#ffffff"}
	]})JSON");
	FDesignDocument Doc;
	if (!TestTrue(TEXT("Paint fixture parses"), FPenParser::ParseDocument(Json, Doc))) return false;
	const TSharedPtr<FDesignNode> Clear = RefExpander::ExpandRef(Doc.Children[2], Doc).ExpandedNode;
	const TSharedPtr<FDesignNode> Repaint = RefExpander::ExpandRef(Doc.Children[3], Doc).ExpandedNode;
	const TSharedPtr<FDesignNode> ExplicitStroke = RefExpander::ExpandRef(Doc.Children[4], Doc).ExpandedNode;
	if (!Clear.IsValid() || !Repaint.IsValid() || !ExplicitStroke.IsValid()) return false;
	TestFalse(TEXT("Outer fill clear removes inner stroke paint"), Clear->Visual.Stroke.Fill.IsSet());
	TestFalse(TEXT("Later fill does not resurrect previously cleared stroke"), Repaint->Visual.Stroke.Fill.IsSet());
	TestEqual(TEXT("Later nonempty fill applies"), Repaint->Visual.Fills.Num(), 1);
	TestTrue(TEXT("Authored stroke paint survives fill clear in same patch"), ExplicitStroke->Visual.Stroke.Fill.IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenOverrideStrokePartsTest,
	"Design2UMG.Unit.PenParser.Override.StrokeParts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPenOverrideStrokePartsTest::RunTest(const FString&)
{
	const FString Base = TEXT(R"JSON("type":"path","width":80,"height":50,"geometry":"M5 40 L35 5 L70 40","stroke":"#009944","strokeWidth":3,"strokeLinecap":"square","strokeLinejoin":"bevel")JSON");
	for (const bool bDescendant : {false, true})
	{
		for (const bool bCap : {false, true})
		{
			const FString Patch = bCap ? TEXT("\"strokeLinecap\":\"round\"") : TEXT("\"strokeLinejoin\":\"round\"");
			const TSharedPtr<FDesignNode> Node = ExpandPatchedNode(*this, Base, Patch, bDescendant);
			if (!Node.IsValid()) return false;
			TestEqual(TEXT("Only the authored cap changes"), Node->Visual.Stroke.Cap, bCap ? EDesignStrokeCap::Circular : EDesignStrokeCap::Square);
			TestEqual(TEXT("Only the authored join changes"), Node->Visual.Stroke.Join, bCap ? EDesignStrokeJoin::Bevel : EDesignStrokeJoin::Circular);
			TestEqual(TEXT("Unspecified stroke width is inherited"), Node->Visual.Stroke.Thickness.Uniform, 3.0f);
			TestTrue(TEXT("Unspecified stroke paint is inherited"), Node->Visual.Stroke.Fill.IsSet());
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenOverrideParentChildPrecedenceTest,
	"Design2UMG.Unit.PenParser.Override.ParentChildPrecedence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPenOverrideParentChildPrecedenceTest::RunTest(const FString&)
{
	auto ExpandContent = [this](const FString& MiddleDescendants, const FString& OuterDescendants)
	{
		const FString Json = FString::Printf(TEXT(R"JSON({"version":"2.14","children":[
			{"id":"base","type":"frame","reusable":true,"children":[
				{"id":"slot","type":"frame","children":[{"id":"label","type":"text","content":"BASE","fontSize":16}]}]},
			{"id":"middle","type":"ref","ref":"base","reusable":true,"descendants":{%s}},
			{"id":"outer","type":"ref","ref":"middle","descendants":{%s}}
		]})JSON"), *MiddleDescendants, *OuterDescendants);
		FDesignDocument Doc;
		if (!TestTrue(TEXT("Precedence fixture parses"), FPenParser::ParseDocument(Json, Doc))) return FString();
		const TSharedPtr<FDesignNode> Node = RefExpander::ExpandRef(Doc.Children.Last(), Doc).ExpandedNode;
		if (!TestTrue(TEXT("Precedence fixture expands"), Node.IsValid())
			|| !Node.IsValid() || Node->Children.IsEmpty() || Node->Children[0]->Children.IsEmpty())
		{
			return FString();
		}
		return Node->Children[0]->Children[0]->Text.Content;
	};

	const FString OuterParent = TEXT(R"JSON("slot":{"children":[{"id":"label","type":"text","content":"OUTER","fontSize":16}]})JSON");
	const FString ChildFirst = TEXT(R"JSON("slot/label":{"content":"MIDDLE"},"slot":{"gap":4})JSON");
	const FString ParentFirst = TEXT(R"JSON("slot":{"gap":4},"slot/label":{"content":"MIDDLE"})JSON");
	TestEqual(TEXT("Outer parent replacement wins regardless of inner key order"), ExpandContent(ChildFirst, OuterParent), FString(TEXT("OUTER")));
	TestEqual(TEXT("Reordered inner object has the same result"), ExpandContent(ParentFirst, OuterParent), FString(TEXT("OUTER")));

	const FString InnerParent = TEXT(R"JSON("slot":{"children":[{"id":"label","type":"text","content":"MIDDLE","fontSize":16}]})JSON");
	const FString OuterChild = TEXT(R"JSON("slot/label":{"content":"OUTER"})JSON");
	TestEqual(TEXT("Outer child patch applies after inner parent replacement"), ExpandContent(InnerParent, OuterChild), FString(TEXT("OUTER")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenOverrideChildrenPaddingPolicyTest,
	"Design2UMG.Unit.PenParser.Override.ChildrenPaddingPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPenOverrideChildrenPaddingPolicyTest::RunTest(const FString&)
{
	const FString Base = TEXT(R"JSON("type":"frame","layout":"vertical","width":60,"height":60,"padding":5,"children":[
		{"id":"old","type":"rectangle","x":10,"y":10,"width":10,"height":10}])JSON");
	const FString Patch = TEXT(R"JSON("children":[{"id":"fresh","type":"rectangle","width":10,"height":10}])JSON");
	for (const bool bDescendant : {false, true})
	{
		const TSharedPtr<FDesignNode> Before = ExpandPatchedNode(*this, Base, TEXT(""), bDescendant);
		const TSharedPtr<FDesignNode> After = ExpandPatchedNode(*this, Base, Patch, bDescendant);
		if (!Before.IsValid() || !After.IsValid()) return false;
		TestTrue(TEXT("Base padding policy is present"), Before->LayoutData.PaddingRequiresRuntimeWrap.IsSet());
		TestFalse(TEXT("Explicit source child position owns the base inset"), Before->LayoutData.PaddingRequiresRuntimeWrap.Get(true));
		TestTrue(TEXT("Replacement keeps padding"), After->LayoutData.ExplicitPadding.IsSet());
		TestTrue(TEXT("Replacement without positions restores runtime padding ownership"), After->LayoutData.PaddingRequiresRuntimeWrap.Get(false));
		TestFalse(TEXT("Replacement child has no explicit position"), After->Children[0]->Transform.HasExplicitPosition());
	}
	const FString RuntimeBase = TEXT(R"JSON("type":"frame","layout":"vertical","padding":5,"children":[
		{"id":"old","type":"rectangle","width":10,"height":10}])JSON");
	const FString PositionedChildren = TEXT(R"JSON("children":[{"id":"fresh","type":"rectangle","x":10,"y":10,"width":10,"height":10}])JSON");
	for (const bool bDescendant : {false, true})
	{
		const auto Inherited = ExpandPatchedNode(*this, RuntimeBase, PositionedChildren, bDescendant);
		const auto Authored = ExpandPatchedNode(*this, RuntimeBase, TEXT("\"padding\":5,") + PositionedChildren, bDescendant);
		if (!Inherited.IsValid() || !Authored.IsValid()) return false;
		TestTrue(TEXT("Child coordinates do not cancel inherited runtime padding"), Inherited->LayoutData.PaddingRequiresRuntimeWrap.Get(false));
		TestFalse(TEXT("Padding authored with positioned children keeps its baked ownership"), Authored->LayoutData.PaddingRequiresRuntimeWrap.Get(true));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenOverrideReplacementScopeTest,
	"Design2UMG.Unit.PenParser.Override.ReplacementScope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPenOverrideReplacementScopeTest::RunTest(const FString&)
{
	for (const bool bRootChildren : {false, true})
	{
		const FString Outer = bRootChildren
			? TEXT(R"JSON("children":[{"id":"slot","type":"frame","children":[{"id":"label","type":"text","content":"NEW","fontSize":16}]}],"descendants":{"slot/label":{"content":"FINAL"}})JSON")
			: TEXT(R"JSON("descendants":{"slot/label":{"content":"FINAL"},"slot":{"children":[{"id":"label","type":"text","content":"NEW","fontSize":16}]}})JSON");
		const FString Json = FString::Printf(TEXT(R"JSON({"version":"2.14","children":[
			{"id":"base","type":"frame","reusable":true,"children":[{"id":"slot","type":"frame","children":[{"id":"label","type":"text","content":"BASE"}]}]},
			{"id":"middle","type":"ref","ref":"base","reusable":true,"descendants":{"slot/label":{"content":"MIDDLE","underline":true,"fontSize":32}}},
			{"id":"outer","type":"ref","ref":"middle",%s}]})JSON"), *Outer);
		FDesignDocument Doc;
		if (!TestTrue(TEXT("Replacement scope parses"), FPenParser::ParseDocument(Json, Doc))) return false;
		const TSharedPtr<FDesignNode> Expanded = RefExpander::ExpandRef(Doc.Children.Last(), Doc).ExpandedNode;
		if (!TestTrue(TEXT("Replacement scope expands"), Expanded.IsValid())) return false;
		const auto& Label = *Expanded->Children[0]->Children[0];
		TestEqual(TEXT("New outer descendant patch still applies"), Label.Text.Content, FString(TEXT("FINAL")));
		TestEqual(TEXT("Inner font size cannot leak into replaced subtree"), Label.Text.FontSize, 16.f);
		TestFalse(TEXT("Inner underline cannot leak into replaced subtree"), Label.Text.bUnderline);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenOverrideDeferredSubtreeTest,
	"Design2UMG.Unit.PenParser.Override.DeferredSubtree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPenOverrideDeferredSubtreeTest::RunTest(const FString&)
{
	const FString Json = TEXT(R"JSON({"version":"2.14","children":[
		{"id":"leaf","type":"frame","reusable":true,"children":[{"id":"slot","type":"frame","children":[{"id":"label","type":"text","content":"BASE"}]}]},
		{"id":"wrapper","type":"frame","reusable":true,"children":[{"id":"nested","type":"ref","ref":"leaf","descendants":{"slot/label":{"content":"INNER","underline":true}}}]},
		{"id":"outer","type":"ref","ref":"wrapper","descendants":{
			"nested/slot/label":{"fontSize":20},
			"nested/slot":{"children":[{"id":"label","type":"text","content":"OUTER"}]}}}
	]})JSON");
	FDesignDocument Doc;
	if (!TestTrue(TEXT("Deferred subtree parses"), FPenParser::ParseDocument(Json, Doc))) return false;
	const TSharedPtr<FDesignNode> Wrapper = RefExpander::ExpandRef(Doc.Children.Last(), Doc).ExpandedNode;
	if (!TestTrue(TEXT("Outer wrapper expands"), Wrapper.IsValid())) return false;
	const TSharedPtr<FDesignNode> Nested = RefExpander::ExpandRef(Wrapper->Children[0], Doc).ExpandedNode;
	if (!TestTrue(TEXT("Nested ref expands"), Nested.IsValid())) return false;
	const auto& Label = *Nested->Children[0]->Children[0];
	TestEqual(TEXT("Deferred replacement discards old content patch"), Label.Text.Content, FString(TEXT("OUTER")));
	TestFalse(TEXT("Deferred replacement discards old decoration patch"), Label.Text.bUnderline);
	TestEqual(TEXT("New same-layer child patch survives"), Label.Text.FontSize, 20.f);
	return true;
}

#endif
