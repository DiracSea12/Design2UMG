// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"
#include "Design2UMGConverter.h"
#include "Design2UMGPrototypeRuntimeValueResolver.h"
#include "Design2UMGTestUtils.h"
#include "FigmaParser.h"
#include "FigmaTestDocBuilder.h"
#include "UMGPropertyKeys.h"
#include "UMGSchemaSourceExtensions.h"
#include "UMGWidgetNodeAccess.h"

#if WITH_AUTOMATION_WORKER

using namespace Design2UMGTestUtils;
using namespace Design2UMGCoreTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeRuntimeValueResolverTypeDomainCompatibilityTest,
	"Design2UMG.Unit.PrototypeRuntimeValueResolver.TypeDomainCompatibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPrototypeRuntimeValueResolverTypeDomainCompatibilityTest::RunTest(const FString& Parameters)
{
	const TMap<FString, FString> EmptyModes;
	TMap<FString, FString> VariableValues;
	VariableValues.Add(TEXT("var:isPrimary"), TEXT(R"({"resolvedType":"BOOLEAN","value":true})"));

	TestTrue(TEXT("resolvedType stays case-insensitive"),
		Design2UMGPrototypeRuntimeValueResolver::EvaluateConditionJson(
			TEXT(R"({"resolvedType":"boolean","value":true})"),
			VariableValues,
			EmptyModes));
	TestTrue(TEXT("resolvedType still wins over raw type"),
		Design2UMGPrototypeRuntimeValueResolver::EvaluateConditionJson(
			TEXT(R"({"resolvedType":"BoOlEaN","type":"string","value":true})"),
			VariableValues,
			EmptyModes));
	TestTrue(TEXT("raw float type still resolves"),
		Design2UMGPrototypeRuntimeValueResolver::EvaluateConditionJson(
			TEXT(R"({"type":"FlOaT","value":1.25})"),
			VariableValues,
			EmptyModes));
	TestTrue(TEXT("raw string type still resolves"),
		Design2UMGPrototypeRuntimeValueResolver::EvaluateConditionJson(
			TEXT(R"({"type":"sTrInG","value":"ready"})"),
			VariableValues,
			EmptyModes));
	TestTrue(TEXT("raw color type still resolves"),
		Design2UMGPrototypeRuntimeValueResolver::EvaluateConditionJson(
			TEXT(R"({"type":"CoLoR","value":{"r":0.1,"g":0.2,"b":0.3,"a":1.0}})"),
			VariableValues,
			EmptyModes));
	TestTrue(TEXT("variable alias type still resolves nested id payload"),
		Design2UMGPrototypeRuntimeValueResolver::EvaluateConditionJson(
			TEXT(R"({"type":"variable_alias","value":{"id":"var:isPrimary"}})"),
			VariableValues,
			EmptyModes));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeRuntimeValueResolverExpressionFunctionCompatibilityTest,
	"Design2UMG.Unit.PrototypeRuntimeValueResolver.ExpressionFunctionCompatibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPrototypeRuntimeValueResolverExpressionFunctionCompatibilityTest::RunTest(const FString& Parameters)
{
	TMap<FString, FString> VariableModes;
	VariableModes.Add(TEXT("collection:theme"), TEXT("mode:dark"));

	TestTrue(TEXT("NOT_EQUALS stays case-insensitive"),
		Design2UMGPrototypeRuntimeValueResolver::EvaluateConditionJson(
			TEXT(R"({"type":"expression","value":{"expressionFunction":"nOt_eQuAlS","expressionArguments":[1,2]}})"),
			TMap<FString, FString>(),
			VariableModes));
	TestTrue(TEXT("LESS_THAN_OR_EQUAL still resolves numeric comparison"),
		Design2UMGPrototypeRuntimeValueResolver::EvaluateConditionJson(
			TEXT(R"({"type":"expression","value":{"expressionFunction":"less_than_or_equal","expressionArguments":[3,3]}})"),
			TMap<FString, FString>(),
			VariableModes));
	TestTrue(TEXT("VAR_MODE_LOOKUP still resolves active mode match"),
		Design2UMGPrototypeRuntimeValueResolver::EvaluateConditionJson(
			TEXT(R"({"type":"expression","value":{"expressionFunction":"vAr_mOdE_lOoKuP","expressionArguments":["collection:theme","mode:dark"]}})"),
			TMap<FString, FString>(),
			VariableModes));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeNavigationConverterTest,
	"Design2UMG.Converter.FigmaPrototypeNavigation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeNavigationConverterTest::RunTest(const FString& Parameters)
{
	const FString Json = MakeFigmaDocJson(FString::Printf(
		TEXT("%s,%s"),
		*MakeContainerNode(TEXT("1:1"), TEXT("PageOne"), TEXT("FRAME"), 0, 0, 360, 240,
			MakeLeafNode(TEXT("cta_button"), TEXT("CTA"), TEXT("RECTANGLE"), 40, 60, 120, 44,
				FString::Printf(TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},"
					 "\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"2:1\",\"navigation\":\"NAVIGATE\","
					 "\"transition\":{\"type\":\"DISSOLVE\",\"duration\":180,\"easing\":{\"type\":\"EASE_IN_AND_OUT\"}}}]}]"),
					*MakeSolidFillJson(0.1, 0.4, 0.9)))),
		*MakeContainerNode(TEXT("2:1"), TEXT("PageTwo"), TEXT("FRAME"), 0, 0, 360, 240,
			MakeTextNode(TEXT("page2_title"), TEXT("Done"), 32, 48, 120, 32, TEXT("Done"),
				TEXT("\"fontFamily\":\"Inter\",\"fontSize\":24,\"fontWeight\":600,\"textAlignHorizontal\":\"LEFT\","
					 "\"textAlignVertical\":\"TOP\",\"textDecoration\":\"NONE\",\"letterSpacing\":0,"
					 "\"lineHeightUnit\":\"AUTO\",\"lineHeightPx\":0")))),
		TEXT(",\"prototypeStartNodeID\":\"2:1\""));

	FDesignDocument Doc;
	TestTrue(TEXT("Figma prototype navigation parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGPrototypeNavigationData& NavigationData = Data.PrototypeNavigation();
	TestTrue(TEXT("Prototype navigation plan exists"), NavigationData.IsValid());
	TestEqual(TEXT("Prototype root uses WidgetSwitcher"), Data.RootWidget.Type, TEXT("WidgetSwitcher"));
	TestEqual(TEXT("Prototype start page resolves to second page"), NavigationData.InitialPageIndex, 1);
	TestEqual(TEXT("Prototype page count"), NavigationData.PageIds.Num(), 2);
	TestEqual(TEXT("Prototype click binding count"), NavigationData.ClickBindings.Num(), 1);
	if (!NavigationData.IsValid() || NavigationData.ClickBindings.Num() != 1)
	{
		return false;
	}

	const FUMGPrototypeNavigationBinding& Binding = NavigationData.ClickBindings[0];
	TestEqual(TEXT("Prototype trigger widget id"), Binding.TriggerWidgetId, TEXT("cta_button"));
	TestEqual(TEXT("Prototype trigger widget runtime name"), Binding.TriggerWidgetName, TEXT("cta_button"));
	TestTrue(TEXT("Prototype switcher widget name is no longer fixed hardcode"),
		NavigationData.SwitcherWidgetName != TEXT("__d2u_prototype_switcher"));
	TestEqual(TEXT("Prototype trigger type recorded"), Binding.TriggerType, TEXT("ON_CLICK"));
	TestEqual(TEXT("Prototype target page id"), Binding.TargetPageId, TEXT("2:1"));
	TestEqual(TEXT("Prototype target page index"), Binding.TargetPageIndex, 1);
	TestEqual(TEXT("Prototype transition type preserved"), Binding.Transition.Type, TEXT("DISSOLVE"));
	TestTrue(TEXT("Prototype transition duration preserved"), FMath::IsNearlyEqual(Binding.Transition.DurationSeconds, 0.18f, 0.0001f));
	TestEqual(TEXT("Prototype transition easing preserved"), Binding.Transition.EasingType, TEXT("EASE_IN_AND_OUT"));
	TestTrue(TEXT("Prototype switcher active index exported via typed state"), Data.RootWidget.WidgetState.ActiveWidgetIndex.IsSet());
	TestEqual(TEXT("Prototype switcher typed active index preserved"), Data.RootWidget.WidgetState.ActiveWidgetIndex.GetValue(), 1);

	const FUMGWidgetNode* TriggerWidget = FindWidgetById(Data.RootWidget, TEXT("cta_button"));
	TestTrue(TEXT("Prototype trigger widget exists"), TriggerWidget != nullptr);
	if (!TriggerWidget)
	{
		return false;
	}

	TestEqual(TEXT("Prototype trigger converts to transparent button wrapper"), TriggerWidget->Type, TEXT("Button"));
	TestTrue(TEXT("Prototype trigger keeps one visual child"), TriggerWidget->Children.Num() == 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeSpecialActionsConverterTest,
	"Design2UMG.Converter.FigmaPrototypeSpecialActions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeSpecialActionsConverterTest::RunTest(const FString& Parameters)
{
	const FString Json = MakeFigmaDocJson(FString::Printf(
		TEXT("%s,%s"),
		*MakeContainerNode(TEXT("1:1"), TEXT("PageOne"), TEXT("FRAME"), 0, 0, 360, 240, FString::Printf(
			TEXT("%s,%s"),
			*MakeLeafNode(TEXT("overlay_button"), TEXT("Overlay"), TEXT("RECTANGLE"), 40, 60, 120, 44,
				FString::Printf(TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},"
					 "\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"2:1\",\"navigation\":\"OPEN_OVERLAY\","
					 "\"transition\":{\"type\":\"DISSOLVE\",\"duration\":150}}]}]"),
					*MakeSolidFillJson(0.1, 0.4, 0.9))),
			*MakeLeafNode(TEXT("link_button"), TEXT("Link"), TEXT("RECTANGLE"), 40, 120, 120, 44,
				FString::Printf(TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},"
					 "\"actions\":[{\"type\":\"OPEN_URL\",\"url\":\"https://example.com/help\"}]}]"),
					*MakeSolidFillJson(0.2, 0.7, 0.5))))),
		*MakeContainerNode(TEXT("2:1"), TEXT("OverlayPage"), TEXT("FRAME"), 0, 0, 360, 240,
			MakeLeafNode(TEXT("back_button"), TEXT("Back"), TEXT("RECTANGLE"), 40, 60, 120, 44,
				FString::Printf(TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},"
					 "\"actions\":[{\"type\":\"BACK\",\"transition\":{\"type\":\"DISSOLVE\",\"duration\":90}}]}]"),
					*MakeSolidFillJson(0.8, 0.3, 0.2))))),
		TEXT(",\"prototypeStartNodeID\":\"1:1\""));

	FDesignDocument Document;
	TestTrue(TEXT("Figma prototype special actions parse succeeds"), FFigmaParser::ParseDocument(Json, Document));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	const FUMGPrototypeNavigationData& NavigationData = Data.PrototypeNavigation();
	TestTrue(TEXT("Prototype special action plan exists"), NavigationData.IsValid());
	TestEqual(TEXT("Prototype special action binding count"), NavigationData.ClickBindings.Num(), 3);
	if (!NavigationData.IsValid() || NavigationData.ClickBindings.Num() != 3)
	{
		return false;
	}

	auto FindBindingByTrigger = [&](const TCHAR* TriggerId) -> const FUMGPrototypeNavigationBinding*
	{
		return NavigationData.ClickBindings.FindByPredicate(
			[&](const FUMGPrototypeNavigationBinding& Binding)
			{
				return Binding.TriggerWidgetId == TriggerId;
			});
	};

	const FUMGPrototypeNavigationBinding* OverlayBinding = FindBindingByTrigger(TEXT("overlay_button"));
	const FUMGPrototypeNavigationBinding* BackBinding = FindBindingByTrigger(TEXT("back_button"));
	const FUMGPrototypeNavigationBinding* OpenLinkBinding = FindBindingByTrigger(TEXT("link_button"));
	TestNotNull(TEXT("Overlay binding exported"), OverlayBinding);
	TestNotNull(TEXT("Back binding exported"), BackBinding);
	TestNotNull(TEXT("Open link binding exported"), OpenLinkBinding);
	if (!OverlayBinding || !BackBinding || !OpenLinkBinding)
	{
		return false;
	}

	TestEqual(TEXT("Overlay action typed"), OverlayBinding->Action, EUMGPrototypeNavigationAction::Overlay);
	TestEqual(TEXT("Overlay trigger type preserved"), OverlayBinding->TriggerType, TEXT("ON_CLICK"));
	TestEqual(TEXT("Overlay target page id preserved"), OverlayBinding->TargetPageId, TEXT("2:1"));
	TestEqual(TEXT("Overlay target page index preserved"), OverlayBinding->TargetPageIndex, 1);
	TestEqual(TEXT("Overlay transition preserved"), OverlayBinding->Transition.Type, TEXT("DISSOLVE"));
	TestTrue(TEXT("Overlay duration preserved"), FMath::IsNearlyEqual(OverlayBinding->Transition.DurationSeconds, 0.15f, 0.0001f));
	TestTrue(TEXT("Prototype overlay host widget name is no longer fixed hardcode"),
		NavigationData.OverlayHostWidgetName != TEXT("__d2u_prototype_overlay_host"));

	TestEqual(TEXT("Back action typed"), BackBinding->Action, EUMGPrototypeNavigationAction::Back);
	TestEqual(TEXT("Back trigger type preserved"), BackBinding->TriggerType, TEXT("ON_CLICK"));
	TestTrue(TEXT("Back no longer abuses target page id"), BackBinding->TargetPageId.IsEmpty());
	TestEqual(TEXT("Back no longer abuses placeholder target page index"), BackBinding->TargetPageIndex, INDEX_NONE);
	TestEqual(TEXT("Back transition preserved"), BackBinding->Transition.Type, TEXT("DISSOLVE"));

	TestEqual(TEXT("Open link action typed"), OpenLinkBinding->Action, EUMGPrototypeNavigationAction::OpenLink);
	TestEqual(TEXT("Open link trigger type preserved"), OpenLinkBinding->TriggerType, TEXT("ON_CLICK"));
	TestTrue(TEXT("Open link target page id stays empty"), OpenLinkBinding->TargetPageId.IsEmpty());
	TestEqual(TEXT("Open link target page index stays empty"), OpenLinkBinding->TargetPageIndex, INDEX_NONE);
	TestEqual(TEXT("Open link URL preserved"), OpenLinkBinding->ExternalUrl, TEXT("https://example.com/help"));

	const FUMGWidgetNode* OverlayTriggerWidget = FindWidgetById(Data.RootWidget, TEXT("overlay_button"));
	const FUMGWidgetNode* BackTriggerWidget = FindWidgetById(Data.RootWidget, TEXT("back_button"));
	const FUMGWidgetNode* OpenLinkTriggerWidget = FindWidgetById(Data.RootWidget, TEXT("link_button"));
	TestEqual(TEXT("Overlay trigger wraps into button"), OverlayTriggerWidget ? OverlayTriggerWidget->Type : FString(), TEXT("Button"));
	TestEqual(TEXT("Back trigger wraps into button"), BackTriggerWidget ? BackTriggerWidget->Type : FString(), TEXT("Button"));
	TestEqual(TEXT("Open link trigger wraps into button"), OpenLinkTriggerWidget ? OpenLinkTriggerWidget->Type : FString(), TEXT("Button"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeHoverAndPressConverterTest,
	"Design2UMG.Converter.FigmaPrototypeHoverAndPress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeHoverAndPressConverterTest::RunTest(const FString& Parameters)
{
	const FString Json = MakeFigmaDocJson(FString::Printf(
		TEXT("%s,%s"),
		*MakeContainerNode(TEXT("1:1"), TEXT("PageOne"), TEXT("FRAME"), 0, 0, 360, 240, FString::Printf(
			TEXT("%s,%s"),
			*MakeLeafNode(TEXT("hover_button"), TEXT("HoverCTA"), TEXT("RECTANGLE"), 40, 60, 120, 44,
				FString::Printf(TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_HOVER\"},"
					 "\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"2:1\",\"navigation\":\"NAVIGATE\"}]}]"),
					*MakeSolidFillJson(0.1, 0.4, 0.9))),
			*MakeLeafNode(TEXT("drag_button"), TEXT("DragCTA"), TEXT("RECTANGLE"), 40, 120, 120, 44,
				FString::Printf(TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_DRAG\"},"
					 "\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"2:1\",\"navigation\":\"NAVIGATE\"}]}]"),
					*MakeSolidFillJson(0.2, 0.7, 0.5))))),
		*MakeContainerNode(TEXT("2:1"), TEXT("PageTwo"), TEXT("FRAME"), 0, 0, 360, 240,
			MakeLeafNode(TEXT("press_button"), TEXT("PressBack"), TEXT("RECTANGLE"), 40, 60, 120, 44,
				FString::Printf(TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_PRESS\"},"
					 "\"actions\":[{\"type\":\"BACK\"}]}]"),
					*MakeSolidFillJson(0.8, 0.3, 0.2))))),
		TEXT(",\"prototypeStartNodeID\":\"1:1\""));

	FDesignDocument Document;
	TestTrue(TEXT("Figma prototype hover/press parse succeeds"), FFigmaParser::ParseDocument(Json, Document));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	const FUMGPrototypeNavigationData& NavigationData = Data.PrototypeNavigation();
	TestTrue(TEXT("Prototype hover/press plan exists"), NavigationData.IsValid());
	TestEqual(TEXT("Prototype hover/press binding count"), NavigationData.ClickBindings.Num(), 3);
	if (!NavigationData.IsValid() || NavigationData.ClickBindings.Num() != 3)
	{
		return false;
	}

	auto FindBindingByTrigger = [&](const TCHAR* TriggerId) -> const FUMGPrototypeNavigationBinding*
	{
		return NavigationData.ClickBindings.FindByPredicate(
			[&](const FUMGPrototypeNavigationBinding& Binding)
			{
				return Binding.TriggerWidgetId == TriggerId;
			});
	};

	const FUMGPrototypeNavigationBinding* HoverBinding = FindBindingByTrigger(TEXT("hover_button"));
	const FUMGPrototypeNavigationBinding* DragBinding = FindBindingByTrigger(TEXT("drag_button"));
	const FUMGPrototypeNavigationBinding* PressBinding = FindBindingByTrigger(TEXT("press_button"));
	TestNotNull(TEXT("Hover binding exported"), HoverBinding);
	TestNotNull(TEXT("Drag binding exported"), DragBinding);
	TestNotNull(TEXT("Press binding exported"), PressBinding);
	if (!HoverBinding || !DragBinding || !PressBinding)
	{
		return false;
	}

	TestEqual(TEXT("Hover trigger type preserved"), HoverBinding->TriggerType, TEXT("ON_HOVER"));
	TestEqual(TEXT("Hover action typed"), HoverBinding->Action, EUMGPrototypeNavigationAction::Navigate);
	TestEqual(TEXT("Hover target page id preserved"), HoverBinding->TargetPageId, TEXT("2:1"));
	TestEqual(TEXT("Hover target page index preserved"), HoverBinding->TargetPageIndex, 1);

	TestEqual(TEXT("Drag trigger type preserved"), DragBinding->TriggerType, TEXT("ON_DRAG"));
	TestEqual(TEXT("Drag action typed"), DragBinding->Action, EUMGPrototypeNavigationAction::Navigate);
	TestEqual(TEXT("Drag target page id preserved"), DragBinding->TargetPageId, TEXT("2:1"));
	TestEqual(TEXT("Drag target page index preserved"), DragBinding->TargetPageIndex, 1);
	TestTrue(TEXT("Drag binding exports executable steps"), DragBinding->Steps.Num() > 0);

	TestEqual(TEXT("Press trigger type preserved"), PressBinding->TriggerType, TEXT("ON_PRESS"));
	TestEqual(TEXT("Press action typed"), PressBinding->Action, EUMGPrototypeNavigationAction::Back);
	TestTrue(TEXT("Press back keeps target page empty"), PressBinding->TargetPageId.IsEmpty());
	TestEqual(TEXT("Press back keeps target page index empty"), PressBinding->TargetPageIndex, INDEX_NONE);

	TestFalse(TEXT("Drag trigger no longer warns as unsupported"),
		Data.Warnings.ContainsByPredicate(
			[](const FString& Warning)
			{
				return Warning.Contains(TEXT("ON_DRAG")) && Warning.Contains(TEXT("drag_button"));
			}));

	const FUMGWidgetNode* HoverTriggerWidget = FindWidgetById(Data.RootWidget, TEXT("hover_button"));
	const FUMGWidgetNode* PressTriggerWidget = FindWidgetById(Data.RootWidget, TEXT("press_button"));
	const FUMGWidgetNode* DragTriggerWidget = FindWidgetById(Data.RootWidget, TEXT("drag_button"));
	TestEqual(TEXT("Hover trigger wraps into button"), HoverTriggerWidget ? HoverTriggerWidget->Type : FString(), TEXT("Button"));
	TestEqual(TEXT("Press trigger wraps into button"), PressTriggerWidget ? PressTriggerWidget->Type : FString(), TEXT("Button"));
	TestEqual(TEXT("Drag trigger now wraps into button"), DragTriggerWidget ? DragTriggerWidget->Type : FString(), TEXT("Button"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeKeyboardAndConditionalConverterTest,
	"Design2UMG.Converter.FigmaPrototypeKeyboardAndConditional",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeKeyboardAndConditionalConverterTest::RunTest(const FString& Parameters)
{
	const FString Json = MakeFigmaDocJson(FString::Printf(
		TEXT("%s,%s,%s"),
		*MakeContainerNode(TEXT("1:1"), TEXT("PageOne"), TEXT("FRAME"), 0, 0, 360, 240, FString::Printf(
			TEXT("%s,%s"),
			*MakeLeafNode(TEXT("key_down_hotspot"), TEXT("KeyDownHotspot"), TEXT("RECTANGLE"), 32, 40, 100, 40,
				FString::Printf(TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_KEY_DOWN\",\"keyCodes\":[\"ENTER\"]},"
					 "\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"2:1\",\"navigation\":\"NAVIGATE\"}]}]"),
					*MakeSolidFillJson(0.3, 0.3, 0.8))),
			*MakeLeafNode(TEXT("conditional_button"), TEXT("ConditionalCTA"), TEXT("RECTANGLE"), 32, 104, 120, 44,
				FString::Printf(TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":["
					 "{\"type\":\"SET_VARIABLE\",\"variableId\":\"var:isPrimary\","
					 "\"variableValue\":{\"resolvedType\":\"BOOLEAN\",\"value\":true}},"
					 "{\"type\":\"CONDITIONAL\",\"conditionalBlocks\":["
					 "{\"condition\":{\"type\":\"VARIABLE_ALIAS\",\"id\":\"var:isPrimary\"},"
					 "\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"2:1\",\"navigation\":\"NAVIGATE\"}]},"
					 "{\"condition\":{\"resolvedType\":\"BOOLEAN\",\"value\":true},"
					 "\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"3:1\",\"navigation\":\"NAVIGATE\"}]}]}]}]"),
					*MakeSolidFillJson(0.8, 0.4, 0.1))))),
		*MakeContainerNode(TEXT("2:1"), TEXT("PageTwo"), TEXT("FRAME"), 0, 0, 360, 240,
			MakeLeafNode(TEXT("key_up_hotspot"), TEXT("KeyUpHotspot"), TEXT("RECTANGLE"), 32, 40, 100, 40,
				FString::Printf(TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_KEY_UP\",\"keyCodes\":[\"ESCAPE\"]},"
					 "\"actions\":[{\"type\":\"BACK\"}]}]"),
					*MakeSolidFillJson(0.2, 0.7, 0.4)))),
		*MakeContainerNode(TEXT("3:1"), TEXT("PageThree"), TEXT("FRAME"), 0, 0, 360, 240,
			MakeTextNode(TEXT("page_three_title"), TEXT("Fallback"), 24, 32, 120, 28, TEXT("Fallback"),
				TEXT("\"fontSize\":20")))),
		TEXT(",\"prototypeStartNodeID\":\"1:1\""));

	FDesignDocument Document;
	TestTrue(TEXT("Figma prototype keyboard/conditional parse succeeds"), FFigmaParser::ParseDocument(Json, Document));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	const FUMGPrototypeNavigationData& NavigationData = Data.PrototypeNavigation();
	TestTrue(TEXT("Prototype keyboard/conditional plan exists"), NavigationData.IsValid());
	TestEqual(TEXT("Prototype keyboard/conditional binding count"), NavigationData.ClickBindings.Num(), 3);
	if (!NavigationData.IsValid() || NavigationData.ClickBindings.Num() != 3)
	{
		return false;
	}

	auto FindBindingByTrigger = [&](const TCHAR* TriggerId) -> const FUMGPrototypeNavigationBinding*
	{
		return NavigationData.ClickBindings.FindByPredicate(
			[&](const FUMGPrototypeNavigationBinding& Binding)
			{
				return Binding.TriggerWidgetId == TriggerId;
			});
	};

	const FUMGPrototypeNavigationBinding* KeyDownBinding = FindBindingByTrigger(TEXT("key_down_hotspot"));
	const FUMGPrototypeNavigationBinding* ConditionalBinding = FindBindingByTrigger(TEXT("conditional_button"));
	const FUMGPrototypeNavigationBinding* KeyUpBinding = FindBindingByTrigger(TEXT("key_up_hotspot"));
	TestNotNull(TEXT("Key-down binding exported"), KeyDownBinding);
	TestNotNull(TEXT("Conditional binding exported"), ConditionalBinding);
	TestNotNull(TEXT("Key-up binding exported"), KeyUpBinding);
	if (!KeyDownBinding || !ConditionalBinding || !KeyUpBinding)
	{
		return false;
	}

	TestEqual(TEXT("Key-down trigger type preserved"), KeyDownBinding->TriggerType, TEXT("ON_KEY_DOWN"));
	TestTrue(TEXT("Key-down trigger exports key code"), KeyDownBinding->TriggerKeyCodes.Num() == 1 && KeyDownBinding->TriggerKeyCodes[0] == TEXT("ENTER"));
	TestEqual(TEXT("Key-down action summary preserved"), KeyDownBinding->Action, EUMGPrototypeNavigationAction::Navigate);
	TestEqual(TEXT("Key-down target page id preserved"), KeyDownBinding->TargetPageId, TEXT("2:1"));

	TestEqual(TEXT("Key-up trigger type preserved"), KeyUpBinding->TriggerType, TEXT("ON_KEY_UP"));
	TestTrue(TEXT("Key-up trigger exports key code"), KeyUpBinding->TriggerKeyCodes.Num() == 1 && KeyUpBinding->TriggerKeyCodes[0] == TEXT("ESCAPE"));
	TestEqual(TEXT("Key-up action summary preserved"), KeyUpBinding->Action, EUMGPrototypeNavigationAction::Back);

	TestEqual(TEXT("Conditional trigger keeps click type"), ConditionalBinding->TriggerType, TEXT("ON_CLICK"));
	TestEqual(TEXT("Conditional summary resolves to first terminal navigation"), ConditionalBinding->Action, EUMGPrototypeNavigationAction::Navigate);
	TestEqual(TEXT("Conditional summary target page id resolves first true branch"), ConditionalBinding->TargetPageId, TEXT("2:1"));
	TestTrue(TEXT("Conditional binding exports step program"), ConditionalBinding->Steps.Num() >= 6);
	if (ConditionalBinding->Steps.Num() < 6)
	{
		return false;
	}

	TestEqual(TEXT("Conditional first step is set-variable"), ConditionalBinding->Steps[0].Kind, EUMGPrototypeNavigationStepKind::SetVariable);
	TestEqual(TEXT("Conditional second step is condition jump"), ConditionalBinding->Steps[1].Kind, EUMGPrototypeNavigationStepKind::ConditionJump);
	TestEqual(TEXT("Conditional first branch navigates to page two"), ConditionalBinding->Steps[2].TargetPageId, TEXT("2:1"));
	TestEqual(TEXT("Conditional final branch navigates to page three"), ConditionalBinding->Steps[5].TargetPageId, TEXT("3:1"));
	TestTrue(TEXT("Conditional step graph has no unsupported warnings"),
		!Data.Warnings.ContainsByPredicate(
			[](const FString& Warning)
			{
				return Warning.Contains(TEXT("conditional_button"))
					&& (Warning.Contains(TEXT("CONDITIONAL")) || Warning.Contains(TEXT("SET_VARIABLE")));
			}));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaOverflowDirectionMapsToScrollBoxTest,
	"Design2UMG.Converter.FigmaOverflowDirectionMapsToScrollBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaOverflowDirectionMapsToScrollBoxTest::RunTest(const FString& Parameters)
{
	const FString Json = MakeFigmaDocJson(MakeContainerNode(TEXT("scroll_frame"), TEXT("ScrollFrame"), TEXT("FRAME"), 0, 0, 320, 240, FString::Printf(
		TEXT("%s,%s"),
		*MakeLeafNode(TEXT("row1"), TEXT("Row1"), TEXT("RECTANGLE"), 0, 0, 320, 80,
			FString::Printf(TEXT(",\"fills\":[%s]"), *MakeSolidFillJson(0.2, 0.3, 0.4))),
		*MakeLeafNode(TEXT("row2"), TEXT("Row2"), TEXT("RECTANGLE"), 0, 92, 320, 80,
			FString::Printf(TEXT(",\"fills\":[%s]"), *MakeSolidFillJson(0.4, 0.3, 0.2)))),
		TEXT(",\"layoutMode\":\"VERTICAL\",\"itemSpacing\":12,\"overflowDirection\":\"VERTICAL_SCROLLING\"")));

	FDesignDocument Document;
	TestTrue(TEXT("Parse scrollable figma fixture"), FFigmaParser::ParseDocument(Json, Document));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* ScrollFrame = FindWidgetById(Data.RootWidget, TEXT("scroll_frame"));
	TestNotNull(TEXT("Scrollable frame converts"), ScrollFrame);
	if (!ScrollFrame)
	{
		return false;
	}

	TestEqual(TEXT("overflowDirection frame maps to ScrollBox"), ScrollFrame->Type, TEXT("ScrollBox"));
	TestEqual(TEXT("ScrollBox orientation exported"), GetWidgetProperty(*ScrollFrame, UMGPropertyKeys::Orientation), TEXT("Vertical"));
	TestEqual(TEXT("ScrollBox scrollbar visibility exported"), GetWidgetProperty(*ScrollFrame, UMGPropertyKeys::ScrollBarVisibility), TEXT("Hidden"));

	TestTrue(TEXT("Typed ScrollBox owner keeps orientation"), ScrollFrame->ScrollBox.Orientation.IsSet());
	TestEqual(TEXT("Typed ScrollBox orientation value"), ScrollFrame->ScrollBox.Orientation.GetValue(), FString(TEXT("Vertical")));
	TestTrue(TEXT("Typed ScrollBox owner keeps scrollbar visibility"), ScrollFrame->ScrollBox.ScrollBarVisibility.IsSet());
	TestEqual(TEXT("Typed ScrollBox scrollbar visibility value"), ScrollFrame->ScrollBox.ScrollBarVisibility.GetValue(), FString(TEXT("Hidden")));
	TestTrue(TEXT("ScrollBox keeps both children"), ScrollFrame->Children.Num() == 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaGridSlotSemanticsConverterTest,
	"Design2UMG.Converter.FigmaGridSlotSemantics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaGridSlotSemanticsConverterTest::RunTest(const FString& Parameters)
{
	const FString Json = MakeFigmaDocJson(MakeContainerNode(TEXT("grid_frame"), TEXT("grid_frame"), TEXT("FRAME"), 0, 0, 320, 240, FString::Printf(
		TEXT("%s,%s,%s"),
		*MakeLeafNode(TEXT("head_card"), TEXT("head_card"), TEXT("RECTANGLE"), 0, 0, 100, 40,
			FString::Printf(TEXT(",\"gridRowAnchorIndex\":0,\"gridColumnAnchorIndex\":0,\"fills\":[%s]"),
				*MakeSolidFillJson(0.2, 0.3, 0.4))),
		*MakeLeafNode(TEXT("tail_card"), TEXT("tail_card"), TEXT("RECTANGLE"), 116, 0, 100, 40,
			FString::Printf(TEXT(",\"gridRowAnchorIndex\":0,\"gridColumnAnchorIndex\":1,\"fills\":[%s]"),
				*MakeSolidFillJson(0.4, 0.3, 0.2))),
		*MakeLeafNode(TEXT("hero_card"), TEXT("hero_card"), TEXT("RECTANGLE"), 0, 64, 216, 40,
			FString::Printf(TEXT(",\"gridRowAnchorIndex\":1,\"gridColumnAnchorIndex\":0,\"gridColumnSpan\":2,\"fills\":[%s]"),
				*MakeSolidFillJson(0.1, 0.6, 0.5)))),
		TEXT(",\"layoutMode\":\"GRID\",\"gridRowCount\":2,\"gridColumnCount\":2,\"gridRowGap\":24,\"gridColumnGap\":12,"
			 "\"gridChildHorizontalAlign\":\"CENTER\",\"gridChildVerticalAlign\":\"MAX\"")));

	FDesignDocument Document;
	TestTrue(TEXT("Parse figma grid slot semantics fixture"), FFigmaParser::ParseDocument(Json, Document));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());

	const FUMGWidgetNode* GridFrame = FindWidgetById(Data.RootWidget, TEXT("grid_frame"));
	const FUMGWidgetNode* TailCard = FindWidgetById(Data.RootWidget, TEXT("tail_card"));
	const FUMGWidgetNode* HeroCard = FindWidgetById(Data.RootWidget, TEXT("hero_card"));
	TestNotNull(TEXT("Grid frame converts"), GridFrame);
	TestNotNull(TEXT("Tail card fused widget converts"), TailCard);
	TestNotNull(TEXT("Hero card fused widget converts"), HeroCard);
	if (!GridFrame || !TailCard || !HeroCard)
	{
		return false;
	}

	TestEqual(TEXT("Grid frame upgrades to GridPanel when span is present"), GridFrame->Type, TEXT("GridPanel"));

	TestEqual(TEXT("Tail card row exported"), TailCard->Slot.Row, 0);
	TestEqual(TEXT("Tail card column exported"), TailCard->Slot.Column, 1);
	TestTrue(TEXT("Tail card horizontal align exported"), TailCard->Slot.HorizontalAlignment == EUMGSlotHorizontalAlignment::Center);
	TestTrue(TEXT("Tail card vertical align exported"), TailCard->Slot.VerticalAlignment == EUMGSlotVerticalAlignment::Bottom);
	TestTrue(TEXT("Tail card gets half horizontal gap on the inner edge"), FMath::IsNearlyEqual(TailCard->Slot.Padding.Left, 6.0f, 0.01f));
	TestTrue(TEXT("Tail card keeps zero right padding on the outer edge"), FMath::IsNearlyEqual(TailCard->Slot.Padding.Right, 0.0f, 0.01f));
	TestTrue(TEXT("Tail card gets half row gap before the next row"), FMath::IsNearlyEqual(TailCard->Slot.Padding.Bottom, 12.0f, 0.01f));

	TestEqual(TEXT("Hero card row exported"), HeroCard->Slot.Row, 1);
	TestEqual(TEXT("Hero card column exported"), HeroCard->Slot.Column, 0);
	TestEqual(TEXT("Hero card column span exported"), HeroCard->Slot.ColumnSpan, 2);
	TestTrue(TEXT("Hero card horizontal align exported"), HeroCard->Slot.HorizontalAlignment == EUMGSlotHorizontalAlignment::Center);
	TestTrue(TEXT("Hero card vertical align exported"), HeroCard->Slot.VerticalAlignment == EUMGSlotVerticalAlignment::Bottom);
	TestTrue(TEXT("Hero card gets zero left padding on the outer edge"), FMath::IsNearlyEqual(HeroCard->Slot.Padding.Left, 0.0f, 0.01f));
	TestTrue(TEXT("Hero card gets half row gap above the second row"), FMath::IsNearlyEqual(HeroCard->Slot.Padding.Top, 12.0f, 0.01f));
	TestTrue(TEXT("Hero card keeps zero right padding when spanning to the edge"), FMath::IsNearlyEqual(HeroCard->Slot.Padding.Right, 0.0f, 0.01f));
	TestTrue(TEXT("Hero card keeps zero bottom padding on the outer edge"), FMath::IsNearlyEqual(HeroCard->Slot.Padding.Bottom, 0.0f, 0.01f));
	return true;
}

#endif
