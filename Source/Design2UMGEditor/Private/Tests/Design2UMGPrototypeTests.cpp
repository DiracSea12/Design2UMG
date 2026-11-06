// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"
#include "Design2UMGConverter.h"
#include "Design2UMGTestUtils.h"
#include "FigmaParser.h"
#include "FigmaTestDocBuilder.h"
#include "DesignSchemaSourceExtensions.h"
#include "UMGPropertyKeys.h"
#include "UMGSchemaSourceExtensions.h"
#include "UMGWidgetNodeAccess.h"

#if WITH_AUTOMATION_WORKER

using namespace Design2UMGTestUtils;
using namespace Design2UMGCoreTests;

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
		TEXT(",\"prototypeStartNodeID\":\"2:1\",\"design2umgPrototypeDurationUnit\":\"MILLISECONDS\""));

	FDesignDocument Doc;
	TestTrue(TEXT("Figma prototype navigation parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGPrototypeNavigationData& NavigationData = Data.PrototypeNavigation();
	TestTrue(TEXT("Prototype navigation plan exists"), NavigationData.IsValid());
	TestEqual(TEXT("Prototype root uses CanvasPanel"), Data.RootWidget.Type, TEXT("CanvasPanel"));
	TestEqual(TEXT("Prototype start page resolves to second page"), NavigationData.InitialPageIndex, 1);
	TestEqual(TEXT("Prototype page count"), NavigationData.PageIds.Num(), 2);
	TestEqual(TEXT("Hosted pages keep their design names"), NavigationData.PageWidgetNames.FindRef(TEXT("2:1")), FString(TEXT("PageTwo")));
	TestEqual(TEXT("Prototype click binding count"), NavigationData.ClickBindings.Num(), 1);
	if (!NavigationData.IsValid() || NavigationData.ClickBindings.Num() != 1)
	{
		return false;
	}

	const FUMGPrototypeNavigationBinding& Binding = NavigationData.ClickBindings[0];
	TestEqual(TEXT("Prototype trigger widget id"), Binding.TriggerWidgetId, TEXT("cta_button"));
	TestEqual(TEXT("Prototype trigger button keeps its design name"), Binding.TriggerWidgetName, TEXT("CTA"));
	TestTrue(TEXT("Prototype page host widget name is no longer fixed hardcode"),
		NavigationData.PageHostWidgetName != TEXT("__d2u_prototype_page_host"));
	TestEqual(TEXT("Prototype trigger type recorded"), Binding.TriggerType, TEXT("ON_CLICK"));
	TestEqual(TEXT("Prototype target page id"), Binding.PrimaryStep().TargetPageId, TEXT("2:1"));
	TestEqual(TEXT("Prototype target page index"), Binding.PrimaryStep().TargetPageIndex, 1);
	TestEqual(TEXT("Prototype transition type preserved"), Binding.PrimaryStep().Transition.Type, TEXT("DISSOLVE"));
	TestTrue(TEXT("Prototype transition duration preserved"), FMath::IsNearlyEqual(Binding.PrimaryStep().Transition.DurationSeconds, 0.18f, 0.0001f));
	TestEqual(TEXT("Prototype transition easing preserved"), Binding.PrimaryStep().Transition.EasingType, TEXT("EASE_IN_AND_OUT"));
	TestEqual(TEXT("Only the initial page is visible"), Data.RootWidget.Children[0].WidgetStyle.Visibility.Get(TEXT("Visible")), FString(TEXT("Collapsed")));
	TestEqual(TEXT("The selected page preserves its design visibility"), Data.RootWidget.Children[1].WidgetStyle.Visibility.Get(TEXT("Visible")), FString(TEXT("Visible")));

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeContestedNamesTest,
	"Design2UMG.Converter.FigmaPrototypeContestedNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeContestedNamesTest::RunTest(const FString& Parameters)
{
	const auto Trigger = [](const TCHAR* Id, const TCHAR* Destination)
	{
		return MakeLeafNode(Id, TEXT("Go"), TEXT("RECTANGLE"), 40, 60, 120, 44, FString::Printf(
			TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},"
				 "\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"%s\",\"navigation\":\"NAVIGATE\"}]}]"),
			*MakeSolidFillJson(0.1, 0.4, 0.9), Destination));
	};
	// Both pages are named "Home / S1" in the source; both triggers are named "Go".
	const FString Json = MakeFigmaDocJson(FString::Printf(TEXT("%s,%s"),
		*MakeContainerNode(TEXT("1:1"), TEXT("Home / S1"), TEXT("FRAME"), 0, 0, 360, 240, Trigger(TEXT("go_1"), TEXT("2:1"))),
		*MakeContainerNode(TEXT("2:1"), TEXT("Home S1"), TEXT("FRAME"), 0, 0, 360, 240, Trigger(TEXT("go_2"), TEXT("1:1")))),
		TEXT(",\"prototypeStartNodeID\":\"2:1\""));
	FDesignDocument Doc;
	TestTrue(TEXT("Figma prototype parse succeeds"), FFigmaParser::ParseDocument(Json, Doc));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGPrototypeNavigationData& NavigationData = Data.PrototypeNavigation();
	TestEqual(TEXT("The initial page keeps the contested design name"), NavigationData.PageWidgetNames.FindRef(TEXT("2:1")), FString(TEXT("Home S1")));
	TestEqual(TEXT("Another page with the same runtime name uses its id"), NavigationData.PageWidgetNames.FindRef(TEXT("1:1")), FString(TEXT("1:1")));
	TestEqual(TEXT("Both contested triggers are bound"), NavigationData.ClickBindings.Num(), 2);
	for (const FUMGPrototypeNavigationBinding& Binding : NavigationData.ClickBindings)
	{
		TestEqual(TEXT("Triggers sharing a runtime name use their ids"), Binding.TriggerWidgetName, Binding.TriggerWidgetId);
		const FUMGWidgetNode* Button = FindWidgetById(Data.RootWidget, Binding.TriggerWidgetId);
		TestTrue(TEXT("The trigger button carries its recorded name"), Button && Button->Name == Binding.TriggerWidgetName);
	}
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
					 "\"transition\":{\"type\":\"DISSOLVE\",\"duration\":150,\"easing\":{\"type\":\"LINEAR\"}}}]}]"),
					*MakeSolidFillJson(0.1, 0.4, 0.9))),
			*MakeLeafNode(TEXT("link_button"), TEXT("Link"), TEXT("RECTANGLE"), 40, 120, 120, 44,
				FString::Printf(TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},"
					 "\"actions\":[{\"type\":\"OPEN_URL\",\"url\":\"https://example.com/help\"}]}]"),
					*MakeSolidFillJson(0.2, 0.7, 0.5))))),
		*MakeContainerNode(TEXT("2:1"), TEXT("OverlayPage"), TEXT("FRAME"), 0, 0, 360, 240,
			MakeLeafNode(TEXT("back_button"), TEXT("Back"), TEXT("RECTANGLE"), 40, 60, 120, 44,
				FString::Printf(TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},"
					 "\"actions\":[{\"type\":\"BACK\",\"transition\":{\"type\":\"DISSOLVE\",\"duration\":90,\"easing\":{\"type\":\"LINEAR\"}}}]}]"),
					*MakeSolidFillJson(0.8, 0.3, 0.2))))),
		TEXT(",\"prototypeStartNodeID\":\"1:1\",\"design2umgPrototypeDurationUnit\":\"MILLISECONDS\""));

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

	TestEqual(TEXT("Overlay action typed"), OverlayBinding->PrimaryStep().Kind, EUMGPrototypeNavigationStepKind::Overlay);
	TestEqual(TEXT("Overlay trigger type preserved"), OverlayBinding->TriggerType, TEXT("ON_CLICK"));
	TestEqual(TEXT("Overlay target page id preserved"), OverlayBinding->PrimaryStep().TargetPageId, TEXT("2:1"));
	TestEqual(TEXT("Overlay target page index preserved"), OverlayBinding->PrimaryStep().TargetPageIndex, 1);
	TestEqual(TEXT("Overlay transition preserved"), OverlayBinding->PrimaryStep().Transition.Type, TEXT("DISSOLVE"));
	TestTrue(TEXT("Overlay duration preserved"), FMath::IsNearlyEqual(OverlayBinding->PrimaryStep().Transition.DurationSeconds, 0.15f, 0.0001f));
	TestTrue(TEXT("Prototype overlay host widget name is no longer fixed hardcode"),
		NavigationData.OverlayHostWidgetName != TEXT("__d2u_prototype_overlay_host"));

	TestEqual(TEXT("Back action typed"), BackBinding->PrimaryStep().Kind, EUMGPrototypeNavigationStepKind::Back);
	TestEqual(TEXT("Back trigger type preserved"), BackBinding->TriggerType, TEXT("ON_CLICK"));
	TestTrue(TEXT("Back no longer abuses target page id"), BackBinding->PrimaryStep().TargetPageId.IsEmpty());
	TestEqual(TEXT("Back no longer abuses placeholder target page index"), BackBinding->PrimaryStep().TargetPageIndex, INDEX_NONE);
	TestEqual(TEXT("Back transition preserved"), BackBinding->PrimaryStep().Transition.Type, TEXT("DISSOLVE"));

	TestEqual(TEXT("Open link action typed"), OpenLinkBinding->PrimaryStep().Kind, EUMGPrototypeNavigationStepKind::OpenLink);
	TestEqual(TEXT("Open link trigger type preserved"), OpenLinkBinding->TriggerType, TEXT("ON_CLICK"));
	TestTrue(TEXT("Open link target page id stays empty"), OpenLinkBinding->PrimaryStep().TargetPageId.IsEmpty());
	TestEqual(TEXT("Open link target page index stays empty"), OpenLinkBinding->PrimaryStep().TargetPageIndex, INDEX_NONE);
	TestEqual(TEXT("Open link URL preserved"), OpenLinkBinding->PrimaryStep().ExternalUrl, TEXT("https://example.com/help"));

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
	TestEqual(TEXT("Hover action typed"), HoverBinding->PrimaryStep().Kind, EUMGPrototypeNavigationStepKind::Navigate);
	TestEqual(TEXT("Hover target page id preserved"), HoverBinding->PrimaryStep().TargetPageId, TEXT("2:1"));
	TestEqual(TEXT("Hover target page index preserved"), HoverBinding->PrimaryStep().TargetPageIndex, 1);

	TestEqual(TEXT("Drag trigger type preserved"), DragBinding->TriggerType, TEXT("ON_DRAG"));
	TestEqual(TEXT("Drag action typed"), DragBinding->PrimaryStep().Kind, EUMGPrototypeNavigationStepKind::Navigate);
	TestEqual(TEXT("Drag target page id preserved"), DragBinding->PrimaryStep().TargetPageId, TEXT("2:1"));
	TestEqual(TEXT("Drag target page index preserved"), DragBinding->PrimaryStep().TargetPageIndex, 1);
	TestTrue(TEXT("Drag binding exports executable steps"), DragBinding->Steps.Num() > 0);

	TestEqual(TEXT("Press trigger type preserved"), PressBinding->TriggerType, TEXT("ON_PRESS"));
	TestEqual(TEXT("Press action typed"), PressBinding->PrimaryStep().Kind, EUMGPrototypeNavigationStepKind::Back);
	TestTrue(TEXT("Press back keeps target page empty"), PressBinding->PrimaryStep().TargetPageId.IsEmpty());
	TestEqual(TEXT("Press back keeps target page index empty"), PressBinding->PrimaryStep().TargetPageIndex, INDEX_NONE);

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
				FString::Printf(TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_KEY_DOWN\",\"device\":\"KEYBOARD\",\"keyCodes\":[\"ENTER\"]},"
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
				FString::Printf(TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_KEY_UP\",\"device\":\"KEYBOARD\",\"keyCodes\":[\"ESCAPE\"]},"
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
	TestEqual(TEXT("Key-down action summary preserved"), KeyDownBinding->PrimaryStep().Kind, EUMGPrototypeNavigationStepKind::Navigate);
	TestEqual(TEXT("Key-down target page id preserved"), KeyDownBinding->PrimaryStep().TargetPageId, TEXT("2:1"));

	TestEqual(TEXT("Key-up trigger type preserved"), KeyUpBinding->TriggerType, TEXT("ON_KEY_UP"));
	TestTrue(TEXT("Key-up trigger exports key code"), KeyUpBinding->TriggerKeyCodes.Num() == 1 && KeyUpBinding->TriggerKeyCodes[0] == TEXT("ESCAPE"));
	TestEqual(TEXT("Key-up action summary preserved"), KeyUpBinding->PrimaryStep().Kind, EUMGPrototypeNavigationStepKind::Back);

	TestEqual(TEXT("Conditional trigger keeps click type"), ConditionalBinding->TriggerType, TEXT("ON_CLICK"));
	TestEqual(TEXT("Conditional summary resolves to first terminal navigation"), ConditionalBinding->PrimaryStep().Kind, EUMGPrototypeNavigationStepKind::Navigate);
	TestEqual(TEXT("Conditional summary target page id resolves first true branch"), ConditionalBinding->PrimaryStep().TargetPageId, TEXT("2:1"));
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
	TestEqual(TEXT("ScrollBox scrollbar visibility exported"), GetWidgetProperty(*ScrollFrame, UMGPropertyKeys::ScrollBarVisibility), TEXT("Collapsed"));

	TestTrue(TEXT("Typed ScrollBox owner keeps orientation"), ScrollFrame->ScrollBox.Orientation.IsSet());
	TestEqual(TEXT("Typed ScrollBox orientation value"), ScrollFrame->ScrollBox.Orientation.GetValue(), FString(TEXT("Vertical")));
	TestTrue(TEXT("Typed ScrollBox owner keeps scrollbar visibility"), ScrollFrame->ScrollBox.ScrollBarVisibility.IsSet());
	TestEqual(TEXT("Typed ScrollBox scrollbar visibility value"), ScrollFrame->ScrollBox.ScrollBarVisibility.GetValue(), FString(TEXT("Collapsed")));
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
	// Grid slot semantics live on the SizeBox wrapper that carries the child's slot.
	const FUMGWidgetNode* TailWrapper = FindWidgetById(Data.RootWidget, TEXT("tail_card_sz"));
	const FUMGWidgetNode* HeroWrapper = FindWidgetById(Data.RootWidget, TEXT("hero_card_sz"));
	TestNotNull(TEXT("Grid frame converts"), GridFrame);
	TestNotNull(TEXT("Tail card widget converts"), TailCard);
	TestNotNull(TEXT("Hero card widget converts"), HeroCard);
	TestNotNull(TEXT("Tail card is wrapped in a SizeBox"), TailWrapper);
	TestNotNull(TEXT("Hero card is wrapped in a SizeBox"), HeroWrapper);
	if (!GridFrame || !TailCard || !HeroCard || !TailWrapper || !HeroWrapper)
	{
		return false;
	}

	TestEqual(TEXT("Grid frame upgrades to GridPanel when span is present"), GridFrame->Type, TEXT("GridPanel"));

	TestEqual(TEXT("Tail card row exported"), TailWrapper->Slot.Row, 0);
	TestEqual(TEXT("Tail card column exported"), TailWrapper->Slot.Column, 1);
	TestTrue(TEXT("Tail card horizontal align exported"), TailWrapper->Slot.HorizontalAlignment == EUMGSlotHorizontalAlignment::Center);
	TestTrue(TEXT("Tail card vertical align exported"), TailWrapper->Slot.VerticalAlignment == EUMGSlotVerticalAlignment::Bottom);
	TestTrue(TEXT("Tail card gets half horizontal gap on the inner edge"), FMath::IsNearlyEqual(TailWrapper->Slot.Padding.Left, 6.0f, 0.01f));
	TestTrue(TEXT("Tail card keeps zero right padding on the outer edge"), FMath::IsNearlyEqual(TailWrapper->Slot.Padding.Right, 0.0f, 0.01f));
	TestTrue(TEXT("Tail card gets half row gap before the next row"), FMath::IsNearlyEqual(TailWrapper->Slot.Padding.Bottom, 12.0f, 0.01f));

	TestEqual(TEXT("Hero card row exported"), HeroWrapper->Slot.Row, 1);
	TestEqual(TEXT("Hero card column exported"), HeroWrapper->Slot.Column, 0);
	TestEqual(TEXT("Hero card column span exported"), HeroWrapper->Slot.ColumnSpan, 2);
	TestTrue(TEXT("Hero card horizontal align exported"), HeroWrapper->Slot.HorizontalAlignment == EUMGSlotHorizontalAlignment::Center);
	TestTrue(TEXT("Hero card vertical align exported"), HeroWrapper->Slot.VerticalAlignment == EUMGSlotVerticalAlignment::Bottom);
	TestTrue(TEXT("Hero card gets zero left padding on the outer edge"), FMath::IsNearlyEqual(HeroWrapper->Slot.Padding.Left, 0.0f, 0.01f));
	TestTrue(TEXT("Hero card gets half row gap above the second row"), FMath::IsNearlyEqual(HeroWrapper->Slot.Padding.Top, 12.0f, 0.01f));
	TestTrue(TEXT("Hero card keeps zero right padding when spanning to the edge"), FMath::IsNearlyEqual(HeroWrapper->Slot.Padding.Right, 0.0f, 0.01f));
	TestTrue(TEXT("Hero card keeps zero bottom padding on the outer edge"), FMath::IsNearlyEqual(HeroWrapper->Slot.Padding.Bottom, 0.0f, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeKeysIndependentOfRawJsonTest,
	"Design2UMG.TypedPayload.PrototypeKeysIndependentOfRawJson",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPrototypeKeysIndependentOfRawJsonTest::RunTest(const FString&)
{
	const FString Triggers = TEXT(R"(,"interactions":[
		{"trigger":{"type":"ON_KEY_DOWN","device":"KEYBOARD","keyCodes":["enter","ENTER",null,{},[],"","escape"],"keyCode":"tab"},"actions":[{"type":"BACK"}]},
		{"trigger":{"type":"ON_KEY_UP","device":"KEYBOARD","keyCode":"escape"},"actions":[{"type":"BACK"}]},
		{"trigger":{"type":"ON_CLICK"},"actions":[{"type":"BACK"}]}
	])");
	const FString Json = MakeFigmaDocJson(MakeContainerNode(TEXT("page"), TEXT("Page"), TEXT("FRAME"), 0, 0, 320, 200,
		MakeLeafNode(TEXT("hotspot"), TEXT("Hotspot"), TEXT("RECTANGLE"), 10, 10, 60, 40, FString::Printf(TEXT(",\"fills\":[%s]"), *MakeSolidFillJson(0.2, 0.4, 0.6)) + Triggers))
		+ TEXT(",") + MakeContainerNode(TEXT("other_page"), TEXT("OtherPage"), TEXT("FRAME"), 0, 0, 320, 200, MakeLeafNode(TEXT("other_rect"), TEXT("OtherRect"), TEXT("RECTANGLE"), 10, 10, 60, 40,
			FString::Printf(TEXT(",\"fills\":[%s]"), *MakeSolidFillJson(0.2, 0.4, 0.6)))));
	FDesignDocument Document;
	if (!TestTrue(TEXT("Keyboard source parses"), FFigmaParser::ParseDocument(Json, Document))) return false;
	if (!TestEqual(TEXT("Two pages parsed"), Document.Children.Num(), 2)
		|| !TestEqual(TEXT("One hotspot parsed"), Document.Children[0]->Children.Num(), 1)) return false;
	auto& Interactions = Document.Children[0]->Children[0]->MutableSource().Prototype.Interactions;
	if (!TestEqual(TEXT("All interactions parsed"), Interactions.Num(), 3)) return false;
	const TArray<FString> DownKeys = { TEXT("ENTER"), TEXT("ESCAPE"), TEXT("TAB") };
	const TArray<FString> UpKeys = { TEXT("ESCAPE") };
	TestTrue(TEXT("Parser owns the normalized key list"), Interactions[0].TriggerKeyCodes == DownKeys);
	TestTrue(TEXT("Parser owns the single-key form"), Interactions[1].TriggerKeyCodes == UpKeys);
	TestTrue(TEXT("Key-free triggers stay key-free"), Interactions[2].TriggerKeyCodes.IsEmpty());
	const FUMGWidgetData Baseline = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	if (!TestEqual(TEXT("Fixture has working bindings before changing metadata"), Baseline.PrototypeNavigation().ClickBindings.Num(), 3)) return false;
	for (const TCHAR* Raw : { TEXT("not-json"), TEXT(R"({"keyCodes":["OTHER"]})") })
	{
		for (auto& Interaction : Interactions) Interaction.TriggerJson = Raw;
		const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
		const auto& Bindings = Data.PrototypeNavigation().ClickBindings;
		TestEqual(TEXT("Raw metadata cannot remove keyboard bindings"), Bindings.Num(), 3);
		for (const auto& Binding : Bindings)
		{
			TestEqual(TEXT("Raw metadata is preserved without interpreting it"), Binding.TriggerJson, FString(Raw));
			if (Binding.UsesKeyDownTrigger()) TestTrue(TEXT("Ordered, normalized unique keys survive without JSON"), Binding.TriggerKeyCodes == DownKeys);
			if (Binding.UsesKeyUpTrigger()) TestTrue(TEXT("Single key source survives without JSON"), Binding.TriggerKeyCodes == UpKeys);
			if (Binding.UsesClickTrigger()) TestTrue(TEXT("Raw metadata cannot synthesize click keys"), Binding.TriggerKeyCodes.IsEmpty());
		}
	}
	Interactions[0].TriggerKeyCodes.Reset();
	Interactions[0].TriggerJson = TEXT(R"({"keyCodes":["ENTER"]})");
	const FUMGWidgetData Cleared = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestFalse(TEXT("Raw JSON cannot refill an empty typed key list"),
		Cleared.PrototypeNavigation().ClickBindings.ContainsByPredicate([](const FUMGPrototypeNavigationBinding& Binding) { return Binding.UsesKeyDownTrigger(); }));
	TestTrue(TEXT("Missing typed keyboard keys retain the existing diagnostic"),
		Cleared.Warnings.ContainsByPredicate([](const FString& Warning) { return Warning.Contains(TEXT("missing keyCodes")); }));
	TestEqual(TEXT("Unrelated bindings survive an empty key list"), Cleared.PrototypeNavigation().ClickBindings.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaExportedPrototypePagesTest,
	"Design2UMG.Converter.FigmaExportedPrototypePages",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaExportedPrototypePagesTest::RunTest(const FString&)
{
	const auto Navigation = [](const TCHAR* Target, const TCHAR* Trigger)
	{
		return FString::Printf(TEXT(",\"interactions\":[{\"trigger\":{\"type\":\"%s\"},\"actions\":[{\"type\":\"NODE\",\"navigation\":\"NAVIGATE\",\"destinationId\":\"%s\"}]}]"), Trigger, Target);
	};
	const auto Page = [&](const TCHAR* Id, const TCHAR* Target, const TCHAR* Trigger, float Width)
	{
		const FString ChildId = FString(Id) + TEXT("_button");
		const TCHAR* Image = FCString::Strcmp(Id, TEXT("A")) == 0 ? TEXT("root-image") : TEXT("target-image");
		FString Children = MakeLeafNode(*ChildId, TEXT("Button"), TEXT("RECTANGLE"), 0, 0, 30, 20,
			FString::Printf(TEXT(",\"fills\":[{\"type\":\"IMAGE\",\"imageRef\":\"%s\"}]"), Image) + Navigation(Target, Trigger));
		if (FCString::Strcmp(Id, TEXT("A")) == 0)
		{
			Children += TEXT(",");
			Children += MakeLeafNode(TEXT("drag"), TEXT("Drag"), TEXT("RECTANGLE"), 40, 0, 30, 20, Navigation(TEXT("unused"), TEXT("MOUSE_LEAVE")));
			Children += TEXT(",");
			Children += MakeLeafNode(TEXT("hover"), TEXT("Hover"), TEXT("RECTANGLE"), 0, 25, 30, 20,
				TEXT(",\"interactions\":[{\"trigger\":{\"type\":\"ON_HOVER\"},\"actions\":[{\"type\":\"NODE\",\"navigation\":\"OPEN_OVERLAY\",\"destinationId\":\"tooltip\"}]}]"));
			Children += TEXT(",");
			Children += MakeLeafNode(TEXT("animated_overlay"), TEXT("Animated"), TEXT("RECTANGLE"), 40, 25, 30, 20,
				TEXT(",\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"navigation\":\"OPEN_OVERLAY\",\"destinationId\":\"animated_page\",\"transition\":{\"type\":\"SMART_ANIMATE\",\"duration\":300,\"easing\":{\"type\":\"LINEAR\"}}}]}]"));
		}
		if (FCString::Strcmp(Id, TEXT("unused")) == 0)
		{
			Children += TEXT(",");
			Children += MakeLeafNode(TEXT("unsupported_trigger"), TEXT("Unsupported"), TEXT("RECTANGLE"), 0, 25, 30, 20,
				Navigation(TEXT("A"), TEXT("ON_UNSUPPORTED")));
		}
		return MakeContainerNode(Id, Id, TEXT("FRAME"), 0, 0, Width, 80, Children,
			FString::Printf(TEXT(",\"absoluteRenderBounds\":%s"), *MakeBoundsJson(-17, -6, Width + 34, 114)));
	};
	const FString Json = MakeFigmaDocJson(Page(TEXT("A"), TEXT("B"), TEXT("ON_CLICK"), 100),
		FString::Printf(TEXT(",\"prototypeStartNodeID\":\"C\",\"design2umgPrototypeDurationUnit\":\"MILLISECONDS\",\"design2umgPrototypePages\":[%s,%s,%s,%s,%s]"),
			*Page(TEXT("B"), TEXT("C"), TEXT("ON_CLICK"), 200),
			*Page(TEXT("C"), TEXT("A"), TEXT("ON_CLICK"), 300),
			*Page(TEXT("unused"), TEXT("A"), TEXT("ON_DRAG"), 400),
			*Page(TEXT("tooltip"), TEXT("A"), TEXT("ON_DRAG"), 50),
			*Page(TEXT("animated_page"), TEXT("A"), TEXT("ON_DRAG"), 50)));
	FDesignDocument Document;
	if (!TestTrue(TEXT("Exported pages parse"), FFigmaParser::ParseDocument(Json, Document))) return false;
	if (!TestEqual(TEXT("Only selected page is a source root"), Document.Children.Num(), 1)
		|| !TestEqual(TEXT("Exported pages remain separate"), Document.SourceDocument().PrototypePages.Num(), 5)) return false;
	TSet<FString> ImageRefs;
	FFigmaParser::CollectImageRefs(Document, ImageRefs);
	TestTrue(TEXT("Images on prototype pages are collected"), ImageRefs.Contains(TEXT("target-image")));
	TMap<FString, FString> ImageMap;
	ImageMap.Add(TEXT("target-image"), TEXT("resolved-image"));
	FFigmaParser::ApplyImageUrls(Document, ImageMap);
	ImageRefs.Reset();
	FFigmaParser::CollectImageRefs(Document, ImageRefs);
	TestTrue(TEXT("Prototype page image mappings are applied"), ImageRefs.Contains(TEXT("resolved-image")));
	TestTrue(TEXT("Selected page image remains distinct"), ImageRefs.Contains(TEXT("root-image")));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	const auto& Plan = Data.PrototypeNavigation();
	TestTrue(TEXT("Reachable pages produce navigation"), Plan.IsValid());
	TestEqual(TEXT("Only reachable destinations are generated"), Plan.PageIds.Num(), 4);
	TestEqual(TEXT("Selected page remains initial"), Plan.InitialPageIndex, 0);
	TestEqual(TEXT("Selected width remains the canvas width"), Data.RootWidget.Bounds.W, 100.0f);
	TestEqual(TEXT("Selected height remains the canvas height"), Data.RootWidget.Bounds.H, 80.0f);
	// Container render bounds are layout aggregates; the initial page's own paint sets the export extent.
	TestTrue(TEXT("Only the initial page supplies the source export extent"), Data.ExportBounds.IsSet()
		&& Data.ExportBounds->Min.Equals(FVector2D::ZeroVector)
		&& Data.ExportBounds->GetSize().Equals(FVector2D(100, 80)));
	TestFalse(TEXT("Unreachable page is not generated"), Plan.PageIds.Contains(TEXT("unused")));
	TestTrue(TEXT("Ordinary hover overlay target is generated"), Plan.PageIds.Contains(TEXT("tooltip")) && Plan.HasOverlayBindings());
	TestNotNull(TEXT("Overlay target is hosted in the generated tree"), FindWidgetById(Data.RootWidget, TEXT("tooltip")));
	TestFalse(TEXT("Unsupported animated overlay is not generated"), Plan.PageIds.Contains(TEXT("animated_page")));
	TestTrue(TEXT("Unsupported animated overlay remains diagnosed"), Data.Warnings.ContainsByPredicate(
		[](const FString& Warning) { return Warning.Contains(TEXT("animated_overlay")) && Warning.Contains(TEXT("not executed")); }));
	TestTrue(TEXT("Unsupported trigger remains diagnosed"), Data.Warnings.ContainsByPredicate(
		[](const FString& Warning) { return Warning.Contains(TEXT("MOUSE_LEAVE")); }));
	TestTrue(TEXT("Candidate analysis warnings are retained"), Data.Warnings.ContainsByPredicate(
		[](const FString& Warning) { return Warning.Contains(TEXT("unsupported_trigger")) && Warning.Contains(TEXT("ON_UNSUPPORTED")); }));
	TestEqual(TEXT("Source roots remain unchanged"), Document.Children.Num(), 1);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaExportedPrototypePageTimeoutTest,
	"Design2UMG.Converter.FigmaExportedPrototypePageTimeout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaExportedPrototypePageTimeoutTest::RunTest(const FString&)
{
	const FString Fill = FString::Printf(TEXT(",\"fills\":[%s]"), *MakeSolidFillJson(0.2, 0.4, 0.6));
	const FString Action = TEXT(",\"interactions\":[{\"trigger\":{\"type\":\"AFTER_TIMEOUT\",\"timeout\":1},"
		"\"actions\":[{\"type\":\"NODE\",\"navigation\":\"NAVIGATE\",\"destinationId\":\"B\","
		"\"transition\":{\"type\":\"SMART_ANIMATE\",\"duration\":300,\"easing\":{\"type\":\"LINEAR\"}}}]}]");
	const FString A = MakeContainerNode(TEXT("A"), TEXT("First"), TEXT("FRAME"), 0, 0, 100, 80,
		MakeLeafNode(TEXT("A_shape"), TEXT("Shape"), TEXT("RECTANGLE"), 10, 10, 20, 20, Fill), Action);
	const FString B = MakeContainerNode(TEXT("B"), TEXT("Second"), TEXT("FRAME"), 0, 0, 100, 80,
		MakeLeafNode(TEXT("B_shape"), TEXT("Shape"), TEXT("RECTANGLE"), 30, 10, 20, 20, Fill));
	const FString Json = MakeFigmaDocJson(A, FString::Printf(
		TEXT(",\"design2umgPrototypeDurationUnit\":\"MILLISECONDS\",\"design2umgPrototypePages\":[%s]"), *B));
	FDesignDocument Document;
	if (!TestTrue(TEXT("Page timeout source parses"), FFigmaParser::ParseDocument(Json, Document))) return false;
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	const auto& Navigation = Data.PrototypeNavigation();
	if (!TestTrue(TEXT("Timeout creates page navigation"), Navigation.IsValid())
		|| !TestEqual(TEXT("Both pages are generated"), Navigation.PageIds.Num(), 2)
		|| !TestEqual(TEXT("One timeout binding exists"), Navigation.ClickBindings.Num(), 1)) return false;
	const auto& Binding = Navigation.ClickBindings[0];
	TestTrue(TEXT("Source timeout is retained"), Binding.UsesPageOpenTrigger());
	TestTrue(*FString::Printf(TEXT("Source transition is admitted: %s"), *Binding.PrimaryStep().Transition.Diagnostic), Binding.PrimaryStep().Transition.Diagnostic.IsEmpty());
	TestTrue(TEXT("Source transition has an actual target"), !Binding.PrimaryStep().Transition.WidgetTargets.IsEmpty());
	FDesignDocument DragDocument;
	if (!TestTrue(TEXT("Drag navigation with exported target parses"), FFigmaParser::ParseDocument(
		Json.Replace(TEXT("\"type\":\"AFTER_TIMEOUT\",\"timeout\":1"), TEXT("\"type\":\"ON_DRAG\"")), DragDocument))) return false;
	const FUMGWidgetData DragData = FDesign2UMGConverter::Convert(DragDocument, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Drag-only destination is included in the reachable page closure"),
		DragData.PrototypeNavigation().PageIds.Contains(TEXT("B"))
		&& DragData.PrototypeNavigation().ClickBindings.ContainsByPredicate([](const FUMGPrototypeNavigationBinding& Item)
			{ return Item.TriggerWidgetId == TEXT("A") && Item.UsesDragTrigger() && Item.PrimaryStep().TargetPageId == TEXT("B"); }));
	const FString RejectedJson = Json.Replace(TEXT("\"AFTER_TIMEOUT\""), TEXT("\"ON_CLICK\""))
		.Replace(TEXT("\"LINEAR\""), TEXT("\"CUSTOM_SPRING\""));
	FDesignDocument RejectedDocument;
	if (!TestTrue(TEXT("Rejected animation with exported page parses"),
		FFigmaParser::ParseDocument(RejectedJson, RejectedDocument))) return false;
	const FUMGWidgetData RejectedData = FDesign2UMGConverter::Convert(RejectedDocument, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Rejected animation retains its diagnostic"), RejectedData.Warnings.ContainsByPredicate(
		[](const FString& Warning) { return Warning.Contains(TEXT("unsupported easing")) && Warning.Contains(TEXT("action not executed")); }));
	TestFalse(TEXT("Rejected animation produces no executable navigation"), RejectedData.PrototypeNavigation().IsValid());
	TestNotNull(TEXT("Selected root remains visible"), FindWidgetById(RejectedData.RootWidget, TEXT("A")));
	TestNull(TEXT("Rejected target page is not rendered as a second root"), FindWidgetById(RejectedData.RootWidget, TEXT("B")));
	TestEqual(TEXT("Rejected target remains source dependency"), RejectedDocument.SourceDocument().PrototypePages.Num(), 1);
	return !HasAnyErrors();
}

#endif
