// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"
#include "Design2UMGConverter.h"
#include "Design2UMGTestUtils.h"
#include "FigmaParser.h"
#include "UMGPropertyKeys.h"
#include "UMGWidgetNodeAccess.h"

#if WITH_AUTOMATION_WORKER

using namespace Design2UMGTestUtils;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeNavigationConverterTest,
	"Design2UMG.Converter.FigmaPrototypeNavigation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeNavigationConverterTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"document": {
			"id":"0:0",
			"name":"Doc",
			"type":"DOCUMENT",
			"prototypeStartNodeID":"2:1",
			"children":[
				{"id":"0:1","name":"Page","type":"CANVAS","children":[
					{"id":"1:1","name":"PageOne","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":0,"y":0,"width":360,"height":240},"children":[
						{"id":"cta_button","name":"CTA","type":"RECTANGLE","visible":true,
						 "absoluteBoundingBox":{"x":40,"y":60,"width":120,"height":44},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.1,"g":0.4,"b":0.9,"a":1}}],
						 "interactions":[{"trigger":{"type":"ON_CLICK"},"actions":[{"type":"NODE","destinationId":"2:1","navigation":"NAVIGATE","transition":{"type":"DISSOLVE","duration":180,"easing":{"type":"EASE_IN_AND_OUT"}}}]}],
						 "children":[]}
					]},
					{"id":"2:1","name":"PageTwo","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":0,"y":0,"width":360,"height":240},"children":[
						{"id":"page2_title","name":"Done","type":"TEXT","visible":true,
						 "absoluteBoundingBox":{"x":32,"y":48,"width":120,"height":32},
						 "characters":"Done",
						 "style":{"fontFamily":"Inter","fontSize":24,"fontWeight":600,"textAlignHorizontal":"LEFT","textAlignVertical":"TOP","textDecoration":"NONE","letterSpacing":0,"lineHeightUnit":"AUTO","lineHeightPx":0}}
					]}
				]}
			]
		}
	})");

	FDesignDocument Doc;
	TestTrue(TEXT("Figma prototype navigation parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	TestTrue(TEXT("Prototype navigation plan exists"), Data.PrototypeNavigation.IsValid());
	TestEqual(TEXT("Prototype root uses WidgetSwitcher"), Data.RootWidget.Type, TEXT("WidgetSwitcher"));
	TestEqual(TEXT("Prototype start page resolves to second page"), Data.PrototypeNavigation.InitialPageIndex, 1);
	TestEqual(TEXT("Prototype page count"), Data.PrototypeNavigation.PageIds.Num(), 2);
	TestEqual(TEXT("Prototype click binding count"), Data.PrototypeNavigation.ClickBindings.Num(), 1);
	if (!Data.PrototypeNavigation.IsValid() || Data.PrototypeNavigation.ClickBindings.Num() != 1)
	{
		return false;
	}

	const FUMGPrototypeNavigationBinding& Binding = Data.PrototypeNavigation.ClickBindings[0];
	TestEqual(TEXT("Prototype trigger widget id"), Binding.TriggerWidgetId, TEXT("cta_button"));
	TestEqual(TEXT("Prototype trigger widget runtime name"), Binding.TriggerWidgetName, TEXT("cta_button"));
	TestTrue(TEXT("Prototype switcher widget name is no longer fixed hardcode"),
		Data.PrototypeNavigation.SwitcherWidgetName != TEXT("__d2u_prototype_switcher"));
	TestEqual(TEXT("Prototype trigger type recorded"), Binding.TriggerType, TEXT("ON_CLICK"));
	TestEqual(TEXT("Prototype target page id"), Binding.TargetPageId, TEXT("2:1"));
	TestEqual(TEXT("Prototype target page index"), Binding.TargetPageIndex, 1);
	TestEqual(TEXT("Prototype transition type preserved"), Binding.Transition.Type, TEXT("DISSOLVE"));
	TestTrue(TEXT("Prototype transition duration preserved"), FMath::IsNearlyEqual(Binding.Transition.DurationSeconds, 0.18f, 0.0001f));
	TestEqual(TEXT("Prototype transition easing preserved"), Binding.Transition.EasingType, TEXT("EASE_IN_AND_OUT"));
	TestTrue(TEXT("Prototype switcher active index exported via typed state"), Data.RootWidget.WidgetState.bHasActiveWidgetIndex);
	TestEqual(TEXT("Prototype switcher typed active index preserved"), Data.RootWidget.WidgetState.ActiveWidgetIndex, 1);

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
	const FString Json = TEXT(R"({
		"document": {
			"id":"0:0",
			"name":"Doc",
			"type":"DOCUMENT",
			"prototypeStartNodeID":"1:1",
			"children":[
				{"id":"0:1","name":"Page","type":"CANVAS","children":[
					{"id":"1:1","name":"PageOne","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":0,"y":0,"width":360,"height":240},"children":[
						{"id":"overlay_button","name":"Overlay","type":"RECTANGLE","visible":true,
						 "absoluteBoundingBox":{"x":40,"y":60,"width":120,"height":44},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.1,"g":0.4,"b":0.9,"a":1}}],
						 "interactions":[{"trigger":{"type":"ON_CLICK"},"actions":[{"type":"NODE","destinationId":"2:1","navigation":"OPEN_OVERLAY","transition":{"type":"DISSOLVE","duration":150}}]}],
						 "children":[]},
						{"id":"link_button","name":"Link","type":"RECTANGLE","visible":true,
						 "absoluteBoundingBox":{"x":40,"y":120,"width":120,"height":44},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.2,"g":0.7,"b":0.5,"a":1}}],
						 "interactions":[{"trigger":{"type":"ON_CLICK"},"actions":[{"type":"OPEN_URL","url":"https://example.com/help"}]}],
						 "children":[]}
					]},
					{"id":"2:1","name":"OverlayPage","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":0,"y":0,"width":360,"height":240},"children":[
						{"id":"back_button","name":"Back","type":"RECTANGLE","visible":true,
						 "absoluteBoundingBox":{"x":40,"y":60,"width":120,"height":44},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.8,"g":0.3,"b":0.2,"a":1}}],
						 "interactions":[{"trigger":{"type":"ON_CLICK"},"actions":[{"type":"BACK","transition":{"type":"DISSOLVE","duration":90}}]}],
						 "children":[]}
					]}
				]}
			]
		}
	})");

	FDesignDocument Document;
	TestTrue(TEXT("Figma prototype special actions parse succeeds"), FFigmaParser::ParseDocument(Json, Document));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document);
	TestTrue(TEXT("Prototype special action plan exists"), Data.PrototypeNavigation.IsValid());
	TestEqual(TEXT("Prototype special action binding count"), Data.PrototypeNavigation.ClickBindings.Num(), 3);
	if (!Data.PrototypeNavigation.IsValid() || Data.PrototypeNavigation.ClickBindings.Num() != 3)
	{
		return false;
	}

	auto FindBindingByTrigger = [&](const TCHAR* TriggerId) -> const FUMGPrototypeNavigationBinding*
	{
		return Data.PrototypeNavigation.ClickBindings.FindByPredicate(
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
		Data.PrototypeNavigation.OverlayHostWidgetName != TEXT("__d2u_prototype_overlay_host"));

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
	const FString Json = TEXT(R"({
		"document": {
			"id":"0:0",
			"name":"Doc",
			"type":"DOCUMENT",
			"prototypeStartNodeID":"1:1",
			"children":[
				{"id":"0:1","name":"Page","type":"CANVAS","children":[
					{"id":"1:1","name":"PageOne","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":0,"y":0,"width":360,"height":240},"children":[
						{"id":"hover_button","name":"HoverCTA","type":"RECTANGLE","visible":true,
						 "absoluteBoundingBox":{"x":40,"y":60,"width":120,"height":44},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.1,"g":0.4,"b":0.9,"a":1}}],
						 "interactions":[{"trigger":{"type":"ON_HOVER"},"actions":[{"type":"NODE","destinationId":"2:1","navigation":"NAVIGATE"}]}],
						 "children":[]},
						{"id":"drag_button","name":"DragCTA","type":"RECTANGLE","visible":true,
						 "absoluteBoundingBox":{"x":40,"y":120,"width":120,"height":44},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.2,"g":0.7,"b":0.5,"a":1}}],
						 "interactions":[{"trigger":{"type":"ON_DRAG"},"actions":[{"type":"NODE","destinationId":"2:1","navigation":"NAVIGATE"}]}],
						 "children":[]}
					]},
					{"id":"2:1","name":"PageTwo","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":0,"y":0,"width":360,"height":240},"children":[
						{"id":"press_button","name":"PressBack","type":"RECTANGLE","visible":true,
						 "absoluteBoundingBox":{"x":40,"y":60,"width":120,"height":44},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.8,"g":0.3,"b":0.2,"a":1}}],
						 "interactions":[{"trigger":{"type":"ON_PRESS"},"actions":[{"type":"BACK"}]}],
						 "children":[]}
					]}
				]}
			]
		}
	})");

	FDesignDocument Document;
	TestTrue(TEXT("Figma prototype hover/press parse succeeds"), FFigmaParser::ParseDocument(Json, Document));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document);
	TestTrue(TEXT("Prototype hover/press plan exists"), Data.PrototypeNavigation.IsValid());
	TestEqual(TEXT("Prototype hover/press binding count"), Data.PrototypeNavigation.ClickBindings.Num(), 3);
	if (!Data.PrototypeNavigation.IsValid() || Data.PrototypeNavigation.ClickBindings.Num() != 3)
	{
		return false;
	}

	auto FindBindingByTrigger = [&](const TCHAR* TriggerId) -> const FUMGPrototypeNavigationBinding*
	{
		return Data.PrototypeNavigation.ClickBindings.FindByPredicate(
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
	const FString Json = TEXT(R"({
		"document": {
			"id":"0:0",
			"name":"Doc",
			"type":"DOCUMENT",
			"prototypeStartNodeID":"1:1",
			"children":[
				{"id":"0:1","name":"Page","type":"CANVAS","children":[
					{"id":"1:1","name":"PageOne","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":0,"y":0,"width":360,"height":240},"children":[
						{"id":"key_down_hotspot","name":"KeyDownHotspot","type":"RECTANGLE","visible":true,
						 "absoluteBoundingBox":{"x":32,"y":40,"width":100,"height":40},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.3,"g":0.3,"b":0.8,"a":1}}],
						 "interactions":[{"trigger":{"type":"ON_KEY_DOWN","keyCodes":["ENTER"]},"actions":[{"type":"NODE","destinationId":"2:1","navigation":"NAVIGATE"}]}],
						 "children":[]},
						{"id":"conditional_button","name":"ConditionalCTA","type":"RECTANGLE","visible":true,
						 "absoluteBoundingBox":{"x":32,"y":104,"width":120,"height":44},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.8,"g":0.4,"b":0.1,"a":1}}],
						 "interactions":[{"trigger":{"type":"ON_CLICK"},"actions":[
							{"type":"SET_VARIABLE","variableId":"var:isPrimary","variableValue":{"resolvedType":"BOOLEAN","value":true}},
							{"type":"CONDITIONAL","conditionalBlocks":[
								{"condition":{"type":"VARIABLE_ALIAS","id":"var:isPrimary"},"actions":[{"type":"NODE","destinationId":"2:1","navigation":"NAVIGATE"}]},
								{"condition":{"resolvedType":"BOOLEAN","value":true},"actions":[{"type":"NODE","destinationId":"3:1","navigation":"NAVIGATE"}]}
							]}
						 ]}],
						 "children":[]}
					]},
					{"id":"2:1","name":"PageTwo","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":0,"y":0,"width":360,"height":240},"children":[
						{"id":"key_up_hotspot","name":"KeyUpHotspot","type":"RECTANGLE","visible":true,
						 "absoluteBoundingBox":{"x":32,"y":40,"width":100,"height":40},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.2,"g":0.7,"b":0.4,"a":1}}],
						 "interactions":[{"trigger":{"type":"ON_KEY_UP","keyCodes":["ESCAPE"]},"actions":[{"type":"BACK"}]}],
						 "children":[]}
					]},
					{"id":"3:1","name":"PageThree","type":"FRAME","visible":true,"absoluteBoundingBox":{"x":0,"y":0,"width":360,"height":240},"children":[
						{"id":"page_three_title","name":"Fallback","type":"TEXT","visible":true,
						 "absoluteBoundingBox":{"x":24,"y":32,"width":120,"height":28},
						 "characters":"Fallback","style":{"fontSize":20}}
					]}
				]}
			]
		}
	})");

	FDesignDocument Document;
	TestTrue(TEXT("Figma prototype keyboard/conditional parse succeeds"), FFigmaParser::ParseDocument(Json, Document));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document);
	TestTrue(TEXT("Prototype keyboard/conditional plan exists"), Data.PrototypeNavigation.IsValid());
	TestEqual(TEXT("Prototype keyboard/conditional binding count"), Data.PrototypeNavigation.ClickBindings.Num(), 3);
	if (!Data.PrototypeNavigation.IsValid() || Data.PrototypeNavigation.ClickBindings.Num() != 3)
	{
		return false;
	}

	auto FindBindingByTrigger = [&](const TCHAR* TriggerId) -> const FUMGPrototypeNavigationBinding*
	{
		return Data.PrototypeNavigation.ClickBindings.FindByPredicate(
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
	const FString Json = TEXT(R"({
		"document": {
			"id":"0:0",
			"name":"Doc",
			"type":"DOCUMENT",
			"children":[
				{"id":"0:1","name":"Page","type":"CANVAS","children":[
					{"id":"scroll_frame","name":"ScrollFrame","type":"FRAME","visible":true,
					 "layoutMode":"VERTICAL","itemSpacing":12,"overflowDirection":"VERTICAL_SCROLLING",
					 "absoluteBoundingBox":{"x":0,"y":0,"width":320,"height":240},
					 "children":[
						{"id":"row1","name":"Row1","type":"RECTANGLE","visible":true,"absoluteBoundingBox":{"x":0,"y":0,"width":320,"height":80},"fills":[{"type":"SOLID","visible":true,"color":{"r":0.2,"g":0.3,"b":0.4,"a":1}}]},
						{"id":"row2","name":"Row2","type":"RECTANGLE","visible":true,"absoluteBoundingBox":{"x":0,"y":92,"width":320,"height":80},"fills":[{"type":"SOLID","visible":true,"color":{"r":0.4,"g":0.3,"b":0.2,"a":1}}]}
					 ]}
				]}
			]
		}
	})");

	FDesignDocument Document;
	TestTrue(TEXT("Parse scrollable figma fixture"), FFigmaParser::ParseDocument(Json, Document));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document);
	const FUMGWidgetNode* ScrollFrame = FindWidgetById(Data.RootWidget, TEXT("scroll_frame"));
	TestNotNull(TEXT("Scrollable frame converts"), ScrollFrame);
	if (!ScrollFrame)
	{
		return false;
	}

	TestEqual(TEXT("overflowDirection frame maps to ScrollBox"), ScrollFrame->Type, TEXT("ScrollBox"));
	TestEqual(TEXT("ScrollBox orientation exported"), GetWidgetProperty(*ScrollFrame, UMGPropertyKeys::Orientation), TEXT("Vertical"));
	TestEqual(TEXT("ScrollBox scrollbar visibility exported"), GetWidgetProperty(*ScrollFrame, UMGPropertyKeys::ScrollBarVisibility), TEXT("Hidden"));

	const FUMGPropertyBag TypedProperties = Design2UMGWidgetNodeAccess::BuildTypedPropertyBag(*ScrollFrame);
	FString Orientation;
	TestTrue(TEXT("Typed property bag keeps ScrollBox orientation"), TypedProperties.TryGetString(UMGPropertyKeys::Orientation, Orientation));
	TestEqual(TEXT("Typed property bag ScrollBox orientation value"), Orientation, FString(TEXT("Vertical")));
	FString ScrollBarVisibility;
	TestTrue(TEXT("Typed property bag keeps ScrollBox scrollbar visibility"), TypedProperties.TryGetString(UMGPropertyKeys::ScrollBarVisibility, ScrollBarVisibility));
	TestEqual(TEXT("Typed property bag ScrollBox scrollbar visibility value"), ScrollBarVisibility, FString(TEXT("Hidden")));
	TestTrue(TEXT("ScrollBox keeps both children"), ScrollFrame->Children.Num() == 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaGridSlotSemanticsConverterTest,
	"Design2UMG.Converter.FigmaGridSlotSemantics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaGridSlotSemanticsConverterTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"document": {
			"id":"0:0",
			"name":"Doc",
			"type":"DOCUMENT",
			"children":[
				{"id":"0:1","name":"Page","type":"CANVAS","children":[
					{"id":"grid_frame","name":"grid_frame","type":"FRAME","visible":true,
					 "layoutMode":"GRID","gridRowCount":2,"gridColumnCount":2,"gridRowGap":24,"gridColumnGap":12,
					 "gridChildHorizontalAlign":"CENTER","gridChildVerticalAlign":"MAX",
					 "absoluteBoundingBox":{"x":0,"y":0,"width":320,"height":240},
					 "children":[
						{"id":"head_card","name":"head_card","type":"RECTANGLE","visible":true,
						 "gridRowAnchorIndex":0,"gridColumnAnchorIndex":0,
						 "absoluteBoundingBox":{"x":0,"y":0,"width":100,"height":40},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.2,"g":0.3,"b":0.4,"a":1}}]},
						{"id":"tail_card","name":"tail_card","type":"RECTANGLE","visible":true,
						 "gridRowAnchorIndex":0,"gridColumnAnchorIndex":1,
						 "absoluteBoundingBox":{"x":116,"y":0,"width":100,"height":40},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.4,"g":0.3,"b":0.2,"a":1}}]},
						{"id":"hero_card","name":"hero_card","type":"RECTANGLE","visible":true,
						 "gridRowAnchorIndex":1,"gridColumnAnchorIndex":0,"gridColumnSpan":2,
						 "absoluteBoundingBox":{"x":0,"y":64,"width":216,"height":40},
						 "fills":[{"type":"SOLID","visible":true,"color":{"r":0.1,"g":0.6,"b":0.5,"a":1}}]}
					 ]}
				]}
			]
		}
	})");

	FDesignDocument Document;
	TestTrue(TEXT("Parse figma grid slot semantics fixture"), FFigmaParser::ParseDocument(Json, Document));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document);

	const FUMGWidgetNode* GridFrame = FindWidgetById(Data.RootWidget, TEXT("grid_frame"));
	const FUMGWidgetNode* TailCard = FindWidgetById(Data.RootWidget, TEXT("tail_card_sz"));
	const FUMGWidgetNode* HeroCard = FindWidgetById(Data.RootWidget, TEXT("hero_card_sz"));
	TestNotNull(TEXT("Grid frame converts"), GridFrame);
	TestNotNull(TEXT("Tail card wrapper converts"), TailCard);
	TestNotNull(TEXT("Hero card wrapper converts"), HeroCard);
	if (!GridFrame || !TailCard || !HeroCard)
	{
		return false;
	}

	TestEqual(TEXT("Grid frame upgrades to GridPanel when span is present"), GridFrame->Type, TEXT("GridPanel"));

	TestEqual(TEXT("Tail card row exported"), TailCard->Slot.Row, 0);
	TestEqual(TEXT("Tail card column exported"), TailCard->Slot.Column, 1);
	TestEqual(TEXT("Tail card horizontal align exported"), TailCard->Slot.HorizontalAlignment, TEXT("Center"));
	TestEqual(TEXT("Tail card vertical align exported"), TailCard->Slot.VerticalAlignment, TEXT("Bottom"));
	TestTrue(TEXT("Tail card gets half horizontal gap on the inner edge"), FMath::IsNearlyEqual(TailCard->Slot.Padding.Left, 6.0f, 0.01f));
	TestTrue(TEXT("Tail card keeps zero right padding on the outer edge"), FMath::IsNearlyEqual(TailCard->Slot.Padding.Right, 0.0f, 0.01f));
	TestTrue(TEXT("Tail card gets half row gap before the next row"), FMath::IsNearlyEqual(TailCard->Slot.Padding.Bottom, 12.0f, 0.01f));

	TestEqual(TEXT("Hero card row exported"), HeroCard->Slot.Row, 1);
	TestEqual(TEXT("Hero card column exported"), HeroCard->Slot.Column, 0);
	TestEqual(TEXT("Hero card column span exported"), HeroCard->Slot.ColumnSpan, 2);
	TestEqual(TEXT("Hero card horizontal align exported"), HeroCard->Slot.HorizontalAlignment, TEXT("Center"));
	TestEqual(TEXT("Hero card vertical align exported"), HeroCard->Slot.VerticalAlignment, TEXT("Bottom"));
	TestTrue(TEXT("Hero card gets zero left padding on the outer edge"), FMath::IsNearlyEqual(HeroCard->Slot.Padding.Left, 0.0f, 0.01f));
	TestTrue(TEXT("Hero card gets half row gap above the second row"), FMath::IsNearlyEqual(HeroCard->Slot.Padding.Top, 12.0f, 0.01f));
	TestTrue(TEXT("Hero card keeps zero right padding when spanning to the edge"), FMath::IsNearlyEqual(HeroCard->Slot.Padding.Right, 0.0f, 0.01f));
	TestTrue(TEXT("Hero card keeps zero bottom padding on the outer edge"), FMath::IsNearlyEqual(HeroCard->Slot.Padding.Bottom, 0.0f, 0.01f));
	return true;
}

#endif
