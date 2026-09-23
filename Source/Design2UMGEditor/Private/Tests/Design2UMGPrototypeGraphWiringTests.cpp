// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"
#include "Design2UMGConverter.h"
#include "DesignImportPipeline.h"
#include "Design2UMGGenerationSettingsSnapshot.h"
#include "Design2UMGPrototypeGraphWiring.h"
#include "Design2UMGTestUtils.h"
#include "Tests/Design2UMGMappedComponentTestWidget.h"
#include "Tests/Design2UMGTestAssetCleanup.h"
#include "FigmaParser.h"
#include "FigmaTestDocBuilder.h"
#include "UMGGenerator.h"
#include "UMGPrototypeAnimationBuilder.h"
#include "UMGSchemaSourceExtensions.h"
#include "UMGWidgetUtils.h"

#include "Animation/MovieScene2DTransformSection.h"
#include "Animation/MovieScene2DTransformTrack.h"
#include "Animation/UMGSequenceTickManager.h"
#include "Animation/WidgetAnimation.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Components/Button.h"
#include "Components/Overlay.h"
#include "Components/SlateWrapperTypes.h"
#include "Components/WidgetSwitcher.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/ComponentDelegateBinding.h"
#include "Engine/DynamicBlueprintBinding.h"
#include "Engine/LatentActionManager.h"
#include "Engine/MemberReference.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "K2Node_CallArrayFunction.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_VariableGet.h"
#include "Kismet/KismetArrayLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "MovieScene.h"
#include "MovieSceneBinding.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace Design2UMGTestUtils;
using namespace Design2UMGCoreTests;
using Design2UMGEditorTests::DeleteGeneratedAssetsAtPath;

namespace
{
	const FString NavigatePrototypeJson()
	{
		const FString CtaButtonJson = MakeLeafNode(TEXT("cta_button"), TEXT("CTA"), TEXT("RECTANGLE"), 40, 60, 120, 44, FString::Printf(
			TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"2:1\",\"navigation\":\"NAVIGATE\",\"transition\":{\"type\":\"DISSOLVE\",\"duration\":120,\"easing\":{\"type\":\"EASE_IN_AND_OUT\"}}}]}]"),
			*MakeSolidFillJson(0.1, 0.4, 0.9)));
		const FString PageTwoJson = MakeContainerNode(TEXT("2:1"), TEXT("PageTwo"), TEXT("FRAME"), 0, 0, 360, 240,
			MakeTextNode(TEXT("page2_title"), TEXT("Done"), 32, 48, 120, 32, TEXT("Done"),
				TEXT("\"fontFamily\":\"Inter\",\"fontSize\":24,\"fontWeight\":600,\"textAlignHorizontal\":\"LEFT\",\"textAlignVertical\":\"TOP\",\"textDecoration\":\"NONE\",\"letterSpacing\":0,\"lineHeightUnit\":\"AUTO\",\"lineHeightPx\":0")));
		return MakeFigmaDocJson(
			FString::Printf(TEXT("%s,%s"),
				*MakeContainerNode(TEXT("1:1"), TEXT("PageOne"), TEXT("FRAME"), 0, 0, 360, 240, CtaButtonJson),
				*PageTwoJson),
			TEXT(",\"prototypeStartNodeID\":\"1:1\""));
	}

	const FString SlideNavigatePrototypeJson()
	{
		const FString CtaButtonJson = MakeLeafNode(TEXT("cta_button"), TEXT("CTA"), TEXT("RECTANGLE"), 40, 60, 120, 44, FString::Printf(
			TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"2:1\",\"navigation\":\"NAVIGATE\",\"transition\":{\"type\":\"PUSH\",\"direction\":\"LEFT\",\"duration\":200,\"easing\":{\"type\":\"EASE_IN_AND_OUT\"}}}]}]"),
			*MakeSolidFillJson(0.1, 0.4, 0.9)));
		const FString PageTwoJson = MakeContainerNode(TEXT("2:1"), TEXT("PageTwo"), TEXT("FRAME"), 0, 0, 360, 240,
			MakeTextNode(TEXT("page2_title"), TEXT("Done"), 32, 48, 120, 32, TEXT("Done"),
				TEXT("\"fontFamily\":\"Inter\",\"fontSize\":24,\"fontWeight\":600,\"textAlignHorizontal\":\"LEFT\",\"textAlignVertical\":\"TOP\",\"textDecoration\":\"NONE\",\"letterSpacing\":0,\"lineHeightUnit\":\"AUTO\",\"lineHeightPx\":0")));
		return MakeFigmaDocJson(
			FString::Printf(TEXT("%s,%s"),
				*MakeContainerNode(TEXT("1:1"), TEXT("PageOne"), TEXT("FRAME"), 0, 0, 360, 240, CtaButtonJson),
				*PageTwoJson),
			TEXT(",\"prototypeStartNodeID\":\"1:1\""));
	}

	const FString NavigateNoTransitionJson()
	{
		const FString CtaButtonJson = MakeLeafNode(TEXT("cta_button"), TEXT("CTA"), TEXT("RECTANGLE"), 40, 60, 120, 44, FString::Printf(
			TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"2:1\",\"navigation\":\"NAVIGATE\"}]}]"),
			*MakeSolidFillJson(0.1, 0.4, 0.9)));
		const FString PageTwoJson = MakeContainerNode(TEXT("2:1"), TEXT("PageTwo"), TEXT("FRAME"), 0, 0, 360, 240,
			MakeTextNode(TEXT("page2_title"), TEXT("Done"), 32, 48, 120, 32, TEXT("Done"),
				TEXT("\"fontFamily\":\"Inter\",\"fontSize\":24,\"fontWeight\":600,\"textAlignHorizontal\":\"LEFT\",\"textAlignVertical\":\"TOP\",\"textDecoration\":\"NONE\",\"letterSpacing\":0,\"lineHeightUnit\":\"AUTO\",\"lineHeightPx\":0")));
		return MakeFigmaDocJson(
			FString::Printf(TEXT("%s,%s"),
				*MakeContainerNode(TEXT("1:1"), TEXT("PageOne"), TEXT("FRAME"), 0, 0, 360, 240, CtaButtonJson),
				*PageTwoJson),
			TEXT(",\"prototypeStartNodeID\":\"1:1\""));
	}

	const FString OpenLinkPrototypeJson()
	{
		const FString LinkButtonJson = MakeLeafNode(TEXT("link_button"), TEXT("Link"), TEXT("RECTANGLE"), 40, 60, 120, 44, FString::Printf(
			TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"OPEN_URL\",\"url\":\"https://example.com/help\"}]}]"),
			*MakeSolidFillJson(0.2, 0.7, 0.5)));
		return MakeFigmaDocJson(
			MakeContainerNode(TEXT("1:1"), TEXT("PageOne"), TEXT("FRAME"), 0, 0, 360, 240, LinkButtonJson),
			TEXT(",\"prototypeStartNodeID\":\"1:1\""));
	}

	const FString OverlayBackPrototypeJson()
	{
		const FString OverlayButtonJson = MakeLeafNode(TEXT("overlay_button"), TEXT("Overlay"), TEXT("RECTANGLE"), 40, 60, 120, 44, FString::Printf(
			TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"2:1\",\"navigation\":\"OPEN_OVERLAY\"}]}]"),
			*MakeSolidFillJson(0.1, 0.4, 0.9)));
		const FString PageTwoJson = MakeContainerNode(TEXT("2:1"), TEXT("PageTwo"), TEXT("FRAME"), 0, 0, 360, 240,
			MakeLeafNode(TEXT("back_button"), TEXT("Back"), TEXT("RECTANGLE"), 40, 60, 120, 44, FString::Printf(
				TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"BACK\"}]}]"),
				*MakeSolidFillJson(0.8, 0.3, 0.2))));
		return MakeFigmaDocJson(
			FString::Printf(TEXT("%s,%s"),
				*MakeContainerNode(TEXT("1:1"), TEXT("PageOne"), TEXT("FRAME"), 0, 0, 360, 240, OverlayButtonJson),
				*PageTwoJson),
			TEXT(",\"prototypeStartNodeID\":\"1:1\""));
	}

	const FString NavigateBackPrototypeJson()
	{
		const FString NavButtonJson = MakeLeafNode(TEXT("nav_button"), TEXT("Next"), TEXT("RECTANGLE"), 40, 60, 120, 44, FString::Printf(
			TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"2:1\",\"navigation\":\"NAVIGATE\"}]}]"),
			*MakeSolidFillJson(0.1, 0.4, 0.9)));
		const FString PageTwoJson = MakeContainerNode(TEXT("2:1"), TEXT("PageTwo"), TEXT("FRAME"), 0, 0, 360, 240,
			MakeLeafNode(TEXT("back_button"), TEXT("Back"), TEXT("RECTANGLE"), 40, 60, 120, 44, FString::Printf(
				TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"BACK\"}]}]"),
				*MakeSolidFillJson(0.8, 0.3, 0.2))));
		return MakeFigmaDocJson(
			FString::Printf(TEXT("%s,%s"),
				*MakeContainerNode(TEXT("1:1"), TEXT("PageOne"), TEXT("FRAME"), 0, 0, 360, 240, NavButtonJson),
				*PageTwoJson),
			TEXT(",\"prototypeStartNodeID\":\"1:1\""));
	}

	// Three pages in document order (indices 0,1,2): 0 navigates to 2, 2 navigates to 1, 1 has Back.
	// The multi-step history [0,2] pins the pop semantics: Back must return to page 2 (the last
	// VALUE), not page 1 (the last INDEX).
	const FString MultiStepNavigateBackPrototypeJson()
	{
		const FString FirstNavJson = MakeLeafNode(TEXT("nav_first"), TEXT("ToThree"), TEXT("RECTANGLE"), 40, 40, 120, 44, FString::Printf(
			TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"3:1\",\"navigation\":\"NAVIGATE\"}]}]"),
			*MakeSolidFillJson(0.1, 0.4, 0.9)));
		const FString SecondNavJson = MakeLeafNode(TEXT("nav_second"), TEXT("ToTwo"), TEXT("RECTANGLE"), 40, 40, 120, 44, FString::Printf(
			TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"2:1\",\"navigation\":\"NAVIGATE\"}]}]"),
			*MakeSolidFillJson(0.2, 0.6, 0.8)));
		const FString BackJson = MakeLeafNode(TEXT("back_button"), TEXT("Back"), TEXT("RECTANGLE"), 40, 40, 120, 44, FString::Printf(
			TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"BACK\"}]}]"),
			*MakeSolidFillJson(0.8, 0.3, 0.2)));
		return MakeFigmaDocJson(
			FString::Printf(TEXT("%s,%s,%s"),
				*MakeContainerNode(TEXT("1:1"), TEXT("PageOne"), TEXT("FRAME"), 0, 0, 360, 240, FirstNavJson),
				*MakeContainerNode(TEXT("2:1"), TEXT("PageTwo"), TEXT("FRAME"), 0, 0, 360, 240, BackJson),
				*MakeContainerNode(TEXT("3:1"), TEXT("PageThree"), TEXT("FRAME"), 0, 0, 360, 240, SecondNavJson)),
			TEXT(",\"prototypeStartNodeID\":\"1:1\""));
	}

	const FString MoveOutNavigatePrototypeJson()
	{
		const FString CtaButtonJson = MakeLeafNode(TEXT("cta_button"), TEXT("CTA"), TEXT("RECTANGLE"), 40, 60, 120, 44, FString::Printf(
			TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"2:1\",\"navigation\":\"NAVIGATE\",\"transition\":{\"type\":\"MOVE_OUT\",\"direction\":\"LEFT\",\"duration\":200,\"easing\":{\"type\":\"EASE_IN_AND_OUT\"}}}]}]"),
			*MakeSolidFillJson(0.1, 0.4, 0.9)));
		const FString PageTwoJson = MakeContainerNode(TEXT("2:1"), TEXT("PageTwo"), TEXT("FRAME"), 0, 0, 360, 240,
			MakeTextNode(TEXT("page2_title"), TEXT("Done"), 32, 48, 120, 32, TEXT("Done"),
				TEXT("\"fontFamily\":\"Inter\",\"fontSize\":24,\"fontWeight\":600,\"textAlignHorizontal\":\"LEFT\",\"textAlignVertical\":\"TOP\",\"textDecoration\":\"NONE\",\"letterSpacing\":0,\"lineHeightUnit\":\"AUTO\",\"lineHeightPx\":0")));
		return MakeFigmaDocJson(
			FString::Printf(TEXT("%s,%s"),
				*MakeContainerNode(TEXT("1:1"), TEXT("PageOne"), TEXT("FRAME"), 0, 0, 360, 240, CtaButtonJson),
				*PageTwoJson),
			TEXT(",\"prototypeStartNodeID\":\"1:1\""));
	}

	const FString MoveInNavigatePrototypeJson()
	{
		const FString CtaButtonJson = MakeLeafNode(TEXT("cta_button"), TEXT("CTA"), TEXT("RECTANGLE"), 40, 60, 120, 44, FString::Printf(
			TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"2:1\",\"navigation\":\"NAVIGATE\",\"transition\":{\"type\":\"MOVE_IN\",\"direction\":\"LEFT\",\"duration\":200,\"easing\":{\"type\":\"EASE_IN_AND_OUT\"}}}]}]"),
			*MakeSolidFillJson(0.1, 0.4, 0.9)));
		const FString PageTwoJson = MakeContainerNode(TEXT("2:1"), TEXT("PageTwo"), TEXT("FRAME"), 0, 0, 360, 240,
			MakeTextNode(TEXT("page2_title"), TEXT("Done"), 32, 48, 120, 32, TEXT("Done"),
				TEXT("\"fontFamily\":\"Inter\",\"fontSize\":24,\"fontWeight\":600,\"textAlignHorizontal\":\"LEFT\",\"textAlignVertical\":\"TOP\",\"textDecoration\":\"NONE\",\"letterSpacing\":0,\"lineHeightUnit\":\"AUTO\",\"lineHeightPx\":0")));
		return MakeFigmaDocJson(
			FString::Printf(TEXT("%s,%s"),
				*MakeContainerNode(TEXT("1:1"), TEXT("PageOne"), TEXT("FRAME"), 0, 0, 360, 240, CtaButtonJson),
				*PageTwoJson),
			TEXT(",\"prototypeStartNodeID\":\"1:1\""));
	}

	// Dual-target page: "2:1" is BOTH the Navigate target and the Overlay target of different
	// buttons on page one. The page must stay a switcher child (the switcher owns the page flow),
	// so BuildRoot synthesizes a mirror copy of it under the overlay host: the overlay binding
	// toggles the mirror (layered display), never the switcher or the history. The mirror's own
	// buttons (back_button, mirror_nav_button) are the VISIBLE ones while the overlay is open.
	const FString DualTargetOverlayPrototypeJson()
	{
		const FString NavButtonJson = MakeLeafNode(TEXT("nav_button"), TEXT("Next"), TEXT("RECTANGLE"), 20, 60, 120, 44, FString::Printf(
			TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"2:1\",\"navigation\":\"NAVIGATE\"}]}]"),
			*MakeSolidFillJson(0.1, 0.4, 0.9)));
		const FString OverlayButtonJson = MakeLeafNode(TEXT("overlay_button"), TEXT("Overlay"), TEXT("RECTANGLE"), 20, 120, 120, 44, FString::Printf(
			TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"2:1\",\"navigation\":\"OPEN_OVERLAY\"}]}]"),
			*MakeSolidFillJson(0.2, 0.7, 0.5)));
		const FString BackButtonJson = MakeLeafNode(TEXT("back_button"), TEXT("Back"), TEXT("RECTANGLE"), 40, 60, 120, 44, FString::Printf(
			TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"BACK\"}]}]"),
			*MakeSolidFillJson(0.8, 0.3, 0.2)));
		const FString MirrorNavButtonJson = MakeLeafNode(TEXT("mirror_nav_button"), TEXT("Home"), TEXT("RECTANGLE"), 40, 120, 120, 44, FString::Printf(
			TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"1:1\",\"navigation\":\"NAVIGATE\"}]}]"),
			*MakeSolidFillJson(0.5, 0.5, 0.8)));
		const FString PageTwoJson = MakeContainerNode(TEXT("2:1"), TEXT("PageTwo"), TEXT("FRAME"), 0, 0, 360, 240,
			FString::Printf(TEXT("%s,%s"), *BackButtonJson, *MirrorNavButtonJson));
		return MakeFigmaDocJson(
			FString::Printf(TEXT("%s,%s"),
				*MakeContainerNode(TEXT("1:1"), TEXT("PageOne"), TEXT("FRAME"), 0, 0, 360, 240,
					FString::Printf(TEXT("%s,%s"), *NavButtonJson, *OverlayButtonJson)),
				*PageTwoJson),
			TEXT(",\"prototypeStartNodeID\":\"1:1\""));
	}

	// Dual-target page carrying a mapped Figma component instance: the mirror copy under the
	// overlay host must keep the component-mapping sidecar fields (candidate marker + component
	// identity) while its design identity stays cleared, so the mirror resolves the SAME mapped
	// project widget class as the original.
	const FString DualTargetOverlayMappedComponentPrototypeJson()
	{
		const FString NavButtonJson = MakeLeafNode(TEXT("nav_button"), TEXT("Next"), TEXT("RECTANGLE"), 20, 60, 120, 44, FString::Printf(
			TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"2:1\",\"navigation\":\"NAVIGATE\"}]}]"),
			*MakeSolidFillJson(0.1, 0.4, 0.9)));
		const FString OverlayButtonJson = MakeLeafNode(TEXT("overlay_button"), TEXT("Overlay"), TEXT("RECTANGLE"), 20, 120, 120, 44, FString::Printf(
			TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"2:1\",\"navigation\":\"OPEN_OVERLAY\"}]}]"),
			*MakeSolidFillJson(0.2, 0.7, 0.5)));
		const FString ButtonStyleJson =
			TEXT("\"fontFamily\":\"Inter\",\"fontSize\":16,\"fontWeight\":500,\"textAlignHorizontal\":\"LEFT\",\"textAlignVertical\":\"TOP\",\"textDecoration\":\"NONE\",\"letterSpacing\":0,\"lineHeightUnit\":\"AUTO\",\"lineHeightPx\":0");
		const FString InstanceJson = MakeContainerNode(TEXT("mapped_btn"), TEXT("MappedButton"), TEXT("INSTANCE"), 160, 60, 120, 44,
			MakeTextNode(TEXT("mapped_btn_label"), TEXT("Label"), 168, 70, 80, 20, TEXT("Checkout"), ButtonStyleJson),
			FString::Printf(
				TEXT(",\"componentId\":\"12:34\",\"mainComponentKey\":\"component-key-primary\","
					 "\"componentProperties\":{\"label\":{\"type\":\"TEXT\",\"value\":\"Checkout\"}},\"fills\":[%s]"),
				*MakeSolidFillJson(0.1, 0.4, 0.9)));
		const FString PageTwoJson = MakeContainerNode(TEXT("2:1"), TEXT("PageTwo"), TEXT("FRAME"), 0, 0, 360, 240, InstanceJson);
		return MakeFigmaDocJson(
			FString::Printf(TEXT("%s,%s"),
				*MakeContainerNode(TEXT("1:1"), TEXT("PageOne"), TEXT("FRAME"), 0, 0, 360, 240,
					FString::Printf(TEXT("%s,%s"), *NavButtonJson, *OverlayButtonJson)),
				*PageTwoJson),
			TEXT(",\"fileKey\":\"mirror-mapping-file-key\",\"prototypeStartNodeID\":\"1:1\""));
	}

	// Initial-page overlay: the startup page "1:1" is the OPEN_OVERLAY target of a button on the
	// second page. The initial page must stay a switcher child (InitialPageIndex keeps pointing at
	// it, no remap); BuildRoot synthesizes a mirror copy under the overlay host so the binding is a
	// layered display, like any dual-target page.
	const FString InitialPageOverlayPrototypeJson()
	{
		const FString NavButtonJson = MakeLeafNode(TEXT("nav_button"), TEXT("Next"), TEXT("RECTANGLE"), 40, 60, 120, 44, FString::Printf(
			TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"2:1\",\"navigation\":\"NAVIGATE\"}]}]"),
			*MakeSolidFillJson(0.1, 0.4, 0.9)));
		const FString OverlayButtonJson = MakeLeafNode(TEXT("open_overlay_button"), TEXT("HomeOverlay"), TEXT("RECTANGLE"), 40, 120, 120, 44, FString::Printf(
			TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"1:1\",\"navigation\":\"OPEN_OVERLAY\"}]}]"),
			*MakeSolidFillJson(0.2, 0.7, 0.5)));
		const FString BackButtonJson = MakeLeafNode(TEXT("back_button"), TEXT("Back"), TEXT("RECTANGLE"), 40, 180, 120, 44, FString::Printf(
			TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"BACK\"}]}]"),
			*MakeSolidFillJson(0.8, 0.3, 0.2)));
		return MakeFigmaDocJson(
			FString::Printf(TEXT("%s,%s"),
				*MakeContainerNode(TEXT("1:1"), TEXT("PageOne"), TEXT("FRAME"), 0, 0, 360, 240, NavButtonJson),
				*MakeContainerNode(TEXT("2:1"), TEXT("PageTwo"), TEXT("FRAME"), 0, 0, 360, 240,
					FString::Printf(TEXT("%s,%s"), *OverlayButtonJson, *BackButtonJson))),
			TEXT(",\"prototypeStartNodeID\":\"1:1\""));
	}

	// Navigate + overlay + back: page one navigates to page two and opens page three as a pure
	// overlay (layered on top, the active page stays). Back must close the open overlay first
	// WITHOUT popping the history; only a second Back returns to page one.
	const FString NavigateOverlayBackPrototypeJson()
	{
		const FString NavButtonJson = MakeLeafNode(TEXT("nav_button"), TEXT("Next"), TEXT("RECTANGLE"), 20, 60, 120, 44, FString::Printf(
			TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"2:1\",\"navigation\":\"NAVIGATE\"}]}]"),
			*MakeSolidFillJson(0.1, 0.4, 0.9)));
		const FString OverlayButtonJson = MakeLeafNode(TEXT("overlay_button"), TEXT("Overlay"), TEXT("RECTANGLE"), 20, 120, 120, 44, FString::Printf(
			TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"3:1\",\"navigation\":\"OPEN_OVERLAY\"}]}]"),
			*MakeSolidFillJson(0.2, 0.7, 0.5)));
		const FString BackButtonJson = MakeLeafNode(TEXT("back_button"), TEXT("Back"), TEXT("RECTANGLE"), 40, 60, 120, 44, FString::Printf(
			TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"BACK\"}]}]"),
			*MakeSolidFillJson(0.8, 0.3, 0.2)));
		const FString PageThreeJson = MakeContainerNode(TEXT("3:1"), TEXT("PageThree"), TEXT("FRAME"), 0, 0, 360, 240,
			MakeTextNode(TEXT("page3_title"), TEXT("Layer"), 32, 48, 120, 32, TEXT("Layer"),
				TEXT("\"fontFamily\":\"Inter\",\"fontSize\":24,\"fontWeight\":600,\"textAlignHorizontal\":\"LEFT\",\"textAlignVertical\":\"TOP\",\"textDecoration\":\"NONE\",\"letterSpacing\":0,\"lineHeightUnit\":\"AUTO\",\"lineHeightPx\":0")));
		return MakeFigmaDocJson(
			FString::Printf(TEXT("%s,%s,%s"),
				*MakeContainerNode(TEXT("1:1"), TEXT("PageOne"), TEXT("FRAME"), 0, 0, 360, 240,
					FString::Printf(TEXT("%s,%s"), *NavButtonJson, *OverlayButtonJson)),
				*MakeContainerNode(TEXT("2:1"), TEXT("PageTwo"), TEXT("FRAME"), 0, 0, 360, 240, BackButtonJson),
				*PageThreeJson),
			TEXT(",\"prototypeStartNodeID\":\"1:1\""));
	}

	const FString KeyboardNavigatePrototypeJson()
	{
		// Page one binds N down (forward) and M up (back); page two binds the SAME N key
		// (back). Keyboard branches must be gated by the source page: hidden-page M must
		// not fire while page two is active, and the two N actions must not steal from
		// each other when the pages switch.
		const FString KeyTriggerJson = FString::Printf(
			TEXT(",\"fills\":[%s],\"interactions\":["
				"{\"trigger\":{\"type\":\"ON_KEY_DOWN\",\"keyCodes\":[\"n\"]},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"2:1\",\"navigation\":\"NAVIGATE\"}]},"
				"{\"trigger\":{\"type\":\"ON_KEY_UP\",\"keyCodes\":[\"m\"]},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"1:1\",\"navigation\":\"NAVIGATE\"}]}]"),
			*MakeSolidFillJson(0.1, 0.4, 0.9));
		const FString KeyNodeJson = MakeLeafNode(TEXT("key_pad"), TEXT("KeyPad"), TEXT("RECTANGLE"), 40, 60, 120, 44, KeyTriggerJson);
		const FString PageTwoKeyTriggerJson = FString::Printf(
			TEXT(",\"fills\":[%s],\"interactions\":["
				"{\"trigger\":{\"type\":\"ON_KEY_DOWN\",\"keyCodes\":[\"n\"]},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"1:1\",\"navigation\":\"NAVIGATE\"}]}]"),
			*MakeSolidFillJson(0.7, 0.4, 0.2));
		const FString PageTwoKeyNodeJson = MakeLeafNode(
			TEXT("page2_key"), TEXT("PageTwoKey"), TEXT("RECTANGLE"), 40, 60, 120, 44, PageTwoKeyTriggerJson);
		const FString PageTwoJson = MakeContainerNode(TEXT("2:1"), TEXT("PageTwo"), TEXT("FRAME"), 0, 0, 360, 240,
			FString::Printf(TEXT("%s,%s"), *PageTwoKeyNodeJson,
				*MakeTextNode(TEXT("page2_title"), TEXT("Done"), 32, 120, 120, 32, TEXT("Done"),
					TEXT("\"fontFamily\":\"Inter\",\"fontSize\":24,\"fontWeight\":600,\"textAlignHorizontal\":\"LEFT\",\"textAlignVertical\":\"TOP\",\"textDecoration\":\"NONE\",\"letterSpacing\":0,\"lineHeightUnit\":\"AUTO\",\"lineHeightPx\":0"))));
		return MakeFigmaDocJson(
			FString::Printf(TEXT("%s,%s"),
				*MakeContainerNode(TEXT("1:1"), TEXT("PageOne"), TEXT("FRAME"), 0, 0, 360, 240, KeyNodeJson),
				*PageTwoJson),
			TEXT(",\"prototypeStartNodeID\":\"1:1\",\"fileKey\":\"proto-nav-wiring-key\""));
	}

	// Plain overlay-only page (not a Navigate target, not the initial page): its content is
	// parented directly under the overlay host (no synthesized mirror), so its visible Navigate
	// button must still close the overlay before switching the page underneath.
	const FString OverlayOnlyPageNavigatePrototypeJson()
	{
		const FString OverlayButtonJson = MakeLeafNode(TEXT("overlay_button"), TEXT("Overlay"), TEXT("RECTANGLE"), 20, 60, 120, 44, FString::Printf(
			TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"2:1\",\"navigation\":\"OPEN_OVERLAY\"}]}]"),
			*MakeSolidFillJson(0.2, 0.7, 0.5)));
		const FString NavButtonJson = MakeLeafNode(TEXT("nav_button"), TEXT("Next"), TEXT("RECTANGLE"), 20, 60, 120, 44, FString::Printf(
			TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"3:1\",\"navigation\":\"NAVIGATE\"}]}]"),
			*MakeSolidFillJson(0.1, 0.4, 0.9)));
		const FString PageOneJson = MakeContainerNode(TEXT("1:1"), TEXT("PageOne"), TEXT("FRAME"), 0, 0, 360, 240, OverlayButtonJson);
		const FString PageTwoJson = MakeContainerNode(TEXT("2:1"), TEXT("PageTwo"), TEXT("FRAME"), 0, 0, 360, 240, NavButtonJson);
		const FString PageThreeJson = MakeContainerNode(TEXT("3:1"), TEXT("PageThree"), TEXT("FRAME"), 0, 0, 360, 240,
			MakeTextNode(TEXT("page3_title"), TEXT("PageThreeTitle"), 32, 48, 120, 32, TEXT("Page three"),
				TEXT("\"fontFamily\":\"Inter\",\"fontSize\":24,\"fontWeight\":600,\"textAlignHorizontal\":\"LEFT\",\"textAlignVertical\":\"TOP\",\"textDecoration\":\"NONE\",\"letterSpacing\":0,\"lineHeightUnit\":\"AUTO\",\"lineHeightPx\":0")));
		return MakeFigmaDocJson(
			FString::Printf(TEXT("%s,%s,%s"), *PageOneJson, *PageTwoJson, *PageThreeJson),
			TEXT(",\"prototypeStartNodeID\":\"1:1\""));
	}

	bool ConvertPrototype(const FString& Json, FDesignDocument& OutDocument, FUMGWidgetData& OutData)
	{
		if (!FFigmaParser::ParseDocument(Json, OutDocument))
		{
			return false;
		}
		OutData = FDesign2UMGConverter::Convert(OutDocument, FDesign2UMGConversionSettings::Defaults());
		return OutData.PrototypeNavigation().ClickBindings.Num() > 0;
	}

	// Mirror purity contract: every node of a synthesized overlay mirror subtree must carry NO design
	// identity (empty SourceNodeId; reimport must never manifest-match a mirror) while the rest of
	// the sidecar may survive (component-mapping fields included), and must carry the
	// OverlayMirrorSuffix on its name (synthesized chrome, not design nodes).
	void CollectOverlayMirrorPurityViolations(const FUMGWidgetNode& Node, TArray<FString>& OutViolations)
	{
		if ((Node.HasSourceData() && !Node.Source().SourceNodeId.IsEmpty())
			|| !Node.Name.EndsWith(Design2UMGPrototypeTokens::OverlayMirrorSuffix()))
		{
			OutViolations.Add(Node.Name);
		}
		for (const FUMGWidgetNode& Child : Node.Children)
		{
			CollectOverlayMirrorPurityViolations(Child, OutViolations);
		}
	}

	// Depth-first IR node lookup by widget name (the wiring tests address page content by name;
	// mirror names are the original names + OverlayMirrorSuffix by construction).
	const FUMGWidgetNode* FindWidgetNodeByName(const FUMGWidgetNode& Node, const FString& TargetName)
	{
		if (Node.Name == TargetName)
		{
			return &Node;
		}
		for (const FUMGWidgetNode& Child : Node.Children)
		{
			if (const FUMGWidgetNode* Found = FindWidgetNodeByName(Child, TargetName))
			{
				return Found;
			}
		}
		return nullptr;
	}

	TArray<UK2Node_ComponentBoundEvent*> CollectBoundEvents(const UWidgetBlueprint* WBP)
	{
		TArray<UK2Node_ComponentBoundEvent*> Events;
		if (WBP && WBP->UbergraphPages.Num() > 0 && WBP->UbergraphPages[0])
		{
			for (UEdGraphNode* Node : WBP->UbergraphPages[0]->Nodes)
			{
				if (UK2Node_ComponentBoundEvent* Event = Cast<UK2Node_ComponentBoundEvent>(Node))
				{
					Events.Add(Event);
				}
			}
		}
		return Events;
	}

	UK2Node_CallFunction* FindExecChainCall(const UK2Node_ComponentBoundEvent* EventNode)
	{
		if (!EventNode)
		{
			return nullptr;
		}
		const UEdGraphPin* ThenPin = EventNode->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
		if (!ThenPin || ThenPin->LinkedTo.Num() == 0)
		{
			return nullptr;
		}
		return Cast<UK2Node_CallFunction>(ThenPin->LinkedTo[0]->GetOwningNode());
	}

	UK2Node_CallFunction* NextExecCall(const UEdGraphNode* Node)
	{
		if (!Node)
		{
			return nullptr;
		}
		const UEdGraphPin* ThenPin = Node->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
		if (!ThenPin || ThenPin->LinkedTo.Num() == 0)
		{
			return nullptr;
		}
		return Cast<UK2Node_CallFunction>(ThenPin->LinkedTo[0]->GetOwningNode());
	}

	// Possessable-bound tracks live on the movie scene's object bindings, not the flat track list.
	UWidgetAnimation* FindGeneratedAnimation(const UWidgetBlueprint* WBP, const TCHAR* AnimationName)
	{
		if (!WBP)
		{
			return nullptr;
		}
		for (UWidgetAnimation* Animation : WBP->Animations)
		{
			if (Animation && Animation->GetFName() == FName(AnimationName))
			{
				return Animation;
			}
		}
		return nullptr;
	}

	TArray<const UMovieSceneTrack*> CollectAnimationTracks(const UWidgetAnimation* Animation)
	{
		TArray<const UMovieSceneTrack*> Tracks;
		if (!Animation || !Animation->MovieScene)
		{
			return Tracks;
		}
		const UMovieScene* MovieScene = Animation->MovieScene;
		Tracks.Append(MovieScene->GetTracks());
		for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
		{
			for (const UMovieSceneTrack* Track : Binding.GetTracks())
			{
				Tracks.Add(Track);
			}
		}
		return Tracks;
	}

	const FMovieSceneFloatChannel* FindOpacityChannel(const UWidgetAnimation* Animation)
	{
		for (const UMovieSceneTrack* Track : CollectAnimationTracks(Animation))
		{
			const UMovieSceneFloatTrack* FloatTrack = Cast<UMovieSceneFloatTrack>(Track);
			if (!FloatTrack)
			{
				continue;
			}
			for (UMovieSceneSection* Section : FloatTrack->GetAllSections())
			{
				if (const UMovieSceneFloatSection* FloatSection = Cast<UMovieSceneFloatSection>(Section))
				{
					return &FloatSection->GetChannel();
				}
			}
		}
		return nullptr;
	}

	const UMovieScene2DTransformSection* FindTransformSection(const UWidgetAnimation* Animation)
	{
		for (const UMovieSceneTrack* Track : CollectAnimationTracks(Animation))
		{
			const UMovieScene2DTransformTrack* TransformTrack = Cast<UMovieScene2DTransformTrack>(Track);
			if (!TransformTrack)
			{
				continue;
			}
			for (UMovieSceneSection* Section : TransformTrack->GetAllSections())
			{
				if (const UMovieScene2DTransformSection* TransformSection = Cast<UMovieScene2DTransformSection>(Section))
				{
					return TransformSection;
				}
			}
		}
		return nullptr;
	}

	const FUMGWidgetNode* FindIrNodeById(const FUMGWidgetNode& Node, const FString& TargetId)
	{
		if (Node.Id == TargetId)
		{
			return &Node;
		}
		for (const FUMGWidgetNode& Child : Node.Children)
		{
			if (const FUMGWidgetNode* Found = FindIrNodeById(Child, TargetId))
			{
				return Found;
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeNavigateGraphWiringTest,
	"Design2UMG.WidgetBlueprint.PrototypeGraphWiring.Navigate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeNavigateGraphWiringTest::RunTest(const FString& Parameters)
{
	FDesignDocument Document;
	FUMGWidgetData Data;
	if (!TestTrue(TEXT("Convert navigate prototype fixture"), ConvertPrototype(NavigatePrototypeJson(), Document, Data)))
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ProtoNavWiringNavigate");
	DeleteGeneratedAssetsAtPath(OutputPath);

	FUMGPrototypeNavigationData NavigationData;
	UWidgetBlueprint* WBP = FUMGGenerator::BuildWidgetBlueprint(
		Data, OutputPath, nullptr, nullptr, &NavigationData);
	if (!TestTrue(TEXT("Build navigate widget blueprint"), WBP != nullptr)
		|| !TestTrue(TEXT("Initial save with transition wiring succeeds"),
			FUMGGenerator::SaveWidgetBlueprint(WBP, nullptr, &NavigationData).IsSuccess()))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	// Transition animations: the DISSOLVE fade produced one out (1->0) and one in (0->1) animation,
	// possessable-bound to the switcher, each sampled with 5 opacity keys.
	UWidgetAnimation* OutgoingAnimation = FindGeneratedAnimation(WBP, TEXT("__d2u_nav_anim_000_out"));
	UWidgetAnimation* IncomingAnimation = FindGeneratedAnimation(WBP, TEXT("__d2u_nav_anim_000_in"));
	if (!TestNotNull(TEXT("Outgoing fade animation generated"), OutgoingAnimation)
		|| !TestNotNull(TEXT("Incoming fade animation generated"), IncomingAnimation))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestEqual(TEXT("Only the two transition animations exist on the blueprint"), WBP->Animations.Num(), 2);

	const UWidgetSwitcher* Switcher = Cast<UWidgetSwitcher>(
		WBP->WidgetTree ? WBP->WidgetTree->RootWidget : nullptr);
	if (!TestNotNull(TEXT("Switcher is the built tree root"), Switcher))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestTrue(TEXT("Outgoing animation is possessable-bound to the switcher"),
		OutgoingAnimation->AnimationBindings.Num() == 1
		&& OutgoingAnimation->AnimationBindings[0].WidgetName == Switcher->GetFName());
	TestTrue(TEXT("Incoming animation is possessable-bound to the switcher"),
		IncomingAnimation->AnimationBindings.Num() == 1
		&& IncomingAnimation->AnimationBindings[0].WidgetName == Switcher->GetFName());

	const FMovieSceneFloatChannel* OutgoingOpacity = FindOpacityChannel(OutgoingAnimation);
	const FMovieSceneFloatChannel* IncomingOpacity = FindOpacityChannel(IncomingAnimation);
	if (!TestNotNull(TEXT("Outgoing opacity track exists"), OutgoingOpacity)
		|| !TestNotNull(TEXT("Incoming opacity track exists"), IncomingOpacity))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestEqual(TEXT("Outgoing fade carries 5 sampled keys"), OutgoingOpacity->GetNumKeys(), 5);
	TestEqual(TEXT("Incoming fade carries 5 sampled keys"), IncomingOpacity->GetNumKeys(), 5);
	TestEqual(TEXT("Outgoing fade starts fully opaque"), OutgoingOpacity->GetValues()[0].Value, 1.0f);
	TestEqual(TEXT("Outgoing fade ends fully transparent"), OutgoingOpacity->GetValues()[4].Value, 0.0f);
	TestEqual(TEXT("Incoming fade starts fully transparent"), IncomingOpacity->GetValues()[0].Value, 0.0f);
	TestEqual(TEXT("Incoming fade ends fully opaque"), IncomingOpacity->GetValues()[4].Value, 1.0f);

	// Wired chain: bound event -> PlayAnimation(out) -> Delay(phase) -> SetActiveWidgetIndex -> PlayAnimation(in).
	const TArray<UK2Node_ComponentBoundEvent*> Events = CollectBoundEvents(WBP);
	if (!TestEqual(TEXT("Exactly one component-bound event is authored"), Events.Num(), 1))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	const UK2Node_ComponentBoundEvent* EventNode = Events[0];
	TestEqual(TEXT("Bound event targets the button widget"), EventNode->ComponentPropertyName.ToString(), TEXT("cta_button"));
	TestEqual(TEXT("Bound event uses the click delegate"), EventNode->DelegatePropertyName.ToString(), TEXT("OnClicked"));

	const auto AssertPlayAnimationNode = [&](const UK2Node_CallFunction* PlayNode, const TCHAR* ExpectedAnimationName)
	{
		if (!TestNotNull(TEXT("Chain reaches a PlayAnimation call"), PlayNode))
		{
			return false;
		}
		TestEqual(TEXT("Call target is PlayAnimation"), PlayNode->FunctionReference.GetMemberName().ToString(), TEXT("PlayAnimation"));
		// Self-context call inside the widget's own ubergraph: the member reference stores no parent
		// class (FMemberReference::SetFromField clears it), so prove the engine target instead.
		TestTrue(TEXT("PlayAnimation call is self-context and UUserWidget exposes the function"),
			PlayNode->FunctionReference.IsSelfContext()
			&& UUserWidget::StaticClass()->FindFunctionByName(TEXT("PlayAnimation")) != nullptr);
		const UEdGraphPin* AnimationPin = PlayNode->FindPin(TEXT("InAnimation"), EGPD_Input);
		if (!TestNotNull(TEXT("PlayAnimation has an InAnimation pin"), AnimationPin)
			|| !TestTrue(TEXT("InAnimation pin is linked"), AnimationPin->LinkedTo.Num() == 1))
		{
			return false;
		}
		const UK2Node_VariableGet* AnimationGet = Cast<UK2Node_VariableGet>(AnimationPin->LinkedTo[0]->GetOwningNode());
		return TestTrue(TEXT("Animation variable get references the generated animation property"),
			AnimationGet && AnimationGet->GetVarName() == FName(ExpectedAnimationName));
	};

	UK2Node_CallFunction* OutgoingPlayNode = FindExecChainCall(EventNode);
	if (!AssertPlayAnimationNode(OutgoingPlayNode, TEXT("__d2u_nav_anim_000_out")))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UK2Node_CallFunction* DelayNode = NextExecCall(OutgoingPlayNode);
	if (!TestNotNull(TEXT("Chain reaches the Delay node"), DelayNode))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestEqual(TEXT("Call target is Delay"), DelayNode->FunctionReference.GetMemberName().ToString(), TEXT("Delay"));
	TestTrue(TEXT("Delay resolves on UKismetSystemLibrary"),
		DelayNode->FunctionReference.GetMemberParentClass() == UKismetSystemLibrary::StaticClass());
	const UEdGraphPin* DurationPin = DelayNode->FindPin(TEXT("Duration"), EGPD_Input);
	if (!TestNotNull(TEXT("Delay Duration pin exists"), DurationPin))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestEqual(TEXT("Delay Duration equals one transition phase"),
		DurationPin->DefaultValue, FString::SanitizeFloat(FMath::Max(0.12f * 0.5f, 1.0f / 120.0f)));

	UK2Node_CallFunction* CallNode = NextExecCall(DelayNode);
	if (!TestNotNull(TEXT("Chain reaches the history push after the delay"), CallNode))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestEqual(TEXT("Call after the delay is the history push Array_Add"), CallNode->FunctionReference.GetMemberName().ToString(), TEXT("Array_Add"));
	CallNode = NextExecCall(CallNode);
	if (!TestNotNull(TEXT("Chain reaches the page switch call"), CallNode))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestEqual(TEXT("Call target is SetActiveWidgetIndex"), CallNode->FunctionReference.GetMemberName().ToString(), TEXT("SetActiveWidgetIndex"));
	TestTrue(TEXT("SetActiveWidgetIndex resolves on UWidgetSwitcher"),
		CallNode->FunctionReference.GetMemberParentClass() == UWidgetSwitcher::StaticClass());

	const UEdGraphPin* IndexPin = CallNode->FindPin(TEXT("Index"), EGPD_Input);
	if (!TestNotNull(TEXT("Index pin exists"), IndexPin))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestEqual(TEXT("Index pin carries the resolved page index"), IndexPin->DefaultValue, TEXT("1"));

	const UEdGraphPin* SelfPin = CallNode->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input);
	TestTrue(TEXT("Switcher variable get feeds the call target"),
		SelfPin && SelfPin->LinkedTo.Num() == 1
		&& Cast<UK2Node_VariableGet>(SelfPin->LinkedTo[0]->GetOwningNode())
		&& Cast<UK2Node_VariableGet>(SelfPin->LinkedTo[0]->GetOwningNode())->GetVarName() == Switcher->GetFName());

	// the switch call is preceded by the history push segment. The chain between the
	// Delay and the switch is GetActiveWidgetIndex -> Array_Add(history, index).
	UK2Node_CallFunction* PushNode = nullptr;
	for (UEdGraphNode* Node : WBP->UbergraphPages[0]->Nodes)
	{
		if (UK2Node_CallFunction* Candidate = Cast<UK2Node_CallFunction>(Node))
		{
			if (Candidate->FunctionReference.GetMemberName() == TEXT("Array_Add"))
			{
				PushNode = Candidate;
			}
		}
	}
	if (!TestNotNull(TEXT("History push Array_Add node authored"), PushNode))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestTrue(TEXT("Array_Add resolves on UKismetArrayLibrary"),
		PushNode->FunctionReference.GetMemberParentClass() == UKismetArrayLibrary::StaticClass());
	const UEdGraphPin* PushTargetArrayPin = PushNode->FindPin(TEXT("TargetArray"), EGPD_Input);
	TestTrue(TEXT("Array_Add reads the generated history variable"),
		PushTargetArrayPin && PushTargetArrayPin->LinkedTo.Num() == 1
		&& Cast<UK2Node_VariableGet>(PushTargetArrayPin->LinkedTo[0]->GetOwningNode())
		&& Cast<UK2Node_VariableGet>(PushTargetArrayPin->LinkedTo[0]->GetOwningNode())->GetVarName()
			== FName(Design2UMGPrototypeGraphWiring::NavHistoryVariableName));
	const UEdGraphPin* PushNewItemPin = PushNode->FindPin(TEXT("NewItem"), EGPD_Input);
	TestTrue(TEXT("Array_Add item comes from GetActiveWidgetIndex"),
		PushNewItemPin && PushNewItemPin->LinkedTo.Num() == 1
		&& PushNewItemPin->LinkedTo[0]->GetOwningNode()->GetClass() == UK2Node_CallFunction::StaticClass()
		&& static_cast<UK2Node_CallFunction*>(PushNewItemPin->LinkedTo[0]->GetOwningNode())
			->FunctionReference.GetMemberName() == TEXT("GetActiveWidgetIndex"));

	// The compiled class carries the generated member variable with the expected type.
	const UWidgetBlueprintGeneratedClass* NavigateGeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(WBP->GeneratedClass);
	const FArrayProperty* HistoryProperty = NavigateGeneratedClass
		? FindFProperty<FArrayProperty>(NavigateGeneratedClass, FName(Design2UMGPrototypeGraphWiring::NavHistoryVariableName))
		: nullptr;
	TestTrue(TEXT("Compiled class exposes the history array variable"),
		HistoryProperty && CastField<FIntProperty>(HistoryProperty->Inner));

	if (!AssertPlayAnimationNode(NextExecCall(CallNode), TEXT("__d2u_nav_anim_000_in")))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeOpenLinkGraphWiringTest,
	"Design2UMG.WidgetBlueprint.PrototypeGraphWiring.OpenLink",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeOpenLinkGraphWiringTest::RunTest(const FString& Parameters)
{
	FDesignDocument Document;
	FUMGWidgetData Data;
	if (!TestTrue(TEXT("Convert open-link prototype fixture"), ConvertPrototype(OpenLinkPrototypeJson(), Document, Data)))
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ProtoNavWiringOpenLink");
	DeleteGeneratedAssetsAtPath(OutputPath);

	UWidgetBlueprint* WBP = FUMGGenerator::GenerateWidgetBlueprint(Data, OutputPath);
	if (!TestTrue(TEXT("Generate open-link widget blueprint"), WBP != nullptr))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	const TArray<UK2Node_ComponentBoundEvent*> Events = CollectBoundEvents(WBP);
	if (!TestEqual(TEXT("Exactly one component-bound event is authored"), Events.Num(), 1))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	const UK2Node_ComponentBoundEvent* EventNode = Events[0];
	TestEqual(TEXT("Bound event targets the link button widget"), EventNode->ComponentPropertyName.ToString(), TEXT("link_button"));
	TestEqual(TEXT("Bound event uses the click delegate"), EventNode->DelegatePropertyName.ToString(), TEXT("OnClicked"));

	UK2Node_CallFunction* CallNode = FindExecChainCall(EventNode);
	if (!TestNotNull(TEXT("Event exec chain reaches a call node"), CallNode))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestEqual(TEXT("Call target is LaunchURL"), CallNode->FunctionReference.GetMemberName().ToString(), TEXT("LaunchURL"));
	TestTrue(TEXT("LaunchURL resolves on UKismetSystemLibrary"),
		CallNode->FunctionReference.GetMemberParentClass() == UKismetSystemLibrary::StaticClass());

	const UEdGraphPin* UrlPin = CallNode->FindPin(TEXT("URL"), EGPD_Input);
	if (!TestNotNull(TEXT("URL pin exists"), UrlPin))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestEqual(TEXT("URL pin carries the literal URL"), UrlPin->DefaultValue, TEXT("https://example.com/help"));

	bool bFoundVariableGet = false;
	bool bFoundSetActiveCall = false;
	for (UEdGraphNode* Node : WBP->UbergraphPages[0]->Nodes)
	{
		bFoundVariableGet |= Cast<UK2Node_VariableGet>(Node) != nullptr;
		if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
		{
			bFoundSetActiveCall |= Call->FunctionReference.GetMemberName() == TEXT("SetActiveWidgetIndex");
		}
	}
	TestFalse(TEXT("Open-link wiring authors no variable get"), bFoundVariableGet);
	TestFalse(TEXT("Open-link wiring authors no switcher call"), bFoundSetActiveCall);

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeWiringRegistersDelegateBindingTest,
	"Design2UMG.WidgetBlueprint.PrototypeGraphWiring.DelegateBindingRegistered",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeWiringRegistersDelegateBindingTest::RunTest(const FString& Parameters)
{
	FDesignDocument Document;
	FUMGWidgetData Data;
	if (!TestTrue(TEXT("Convert navigate prototype fixture"), ConvertPrototype(NavigatePrototypeJson(), Document, Data)))
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ProtoNavWiringBinding");
	DeleteGeneratedAssetsAtPath(OutputPath);

	UWidgetBlueprint* WBP = FUMGGenerator::GenerateWidgetBlueprint(Data, OutputPath);
	if (!TestTrue(TEXT("Generate navigate widget blueprint"), WBP != nullptr))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(WBP->GeneratedClass);
	if (!TestNotNull(TEXT("Generated class exists"), GeneratedClass))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UComponentDelegateBinding* ComponentBinding = nullptr;
	for (UDynamicBlueprintBinding* BindingObject : GeneratedClass->DynamicBindingObjects)
	{
		if (UComponentDelegateBinding* Candidate = Cast<UComponentDelegateBinding>(BindingObject))
		{
			ComponentBinding = Candidate;
		}
	}
	if (!TestNotNull(TEXT("Compiled class carries a component delegate binding"), ComponentBinding))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	const TArray<UK2Node_ComponentBoundEvent*> Events = CollectBoundEvents(WBP);
	if (!TestEqual(TEXT("One bound event node in the graph"), Events.Num(), 1) || !Events[0])
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	const UK2Node_ComponentBoundEvent* EventNode = Events[0];

	const FBlueprintComponentDelegateBinding* RegisteredBinding = nullptr;
	for (const FBlueprintComponentDelegateBinding& Entry : ComponentBinding->ComponentDelegateBindings)
	{
		if (Entry.ComponentPropertyName == EventNode->ComponentPropertyName)
		{
			RegisteredBinding = &Entry;
		}
	}
	if (!TestNotNull(TEXT("Delegate binding entry registered for the button"), RegisteredBinding))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestEqual(TEXT("Registered delegate is OnClicked"), RegisteredBinding->DelegatePropertyName.ToString(), TEXT("OnClicked"));
	TestTrue(TEXT("Registered function matches the authored event"),
		RegisteredBinding->FunctionNameToBind == EventNode->CustomFunctionName);
	TestNotNull(TEXT("Bound function exists on the generated class"),
		GeneratedClass->FindFunctionByName(EventNode->CustomFunctionName));

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeWiringRegenerateIsIdempotentTest,
	"Design2UMG.WidgetBlueprint.PrototypeGraphWiring.RegenerateIdempotent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeWiringRegenerateIsIdempotentTest::RunTest(const FString& Parameters)
{
	FDesignDocument Document;
	FUMGWidgetData Data;
	if (!TestTrue(TEXT("Convert navigate prototype fixture"), ConvertPrototype(NavigatePrototypeJson(), Document, Data)))
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ProtoNavWiringIdempotent");
	DeleteGeneratedAssetsAtPath(OutputPath);

	FUMGPrototypeNavigationData NavigationData;
	UWidgetBlueprint* WBP = FUMGGenerator::BuildWidgetBlueprint(
		Data, OutputPath, nullptr, nullptr, &NavigationData);
	if (!TestTrue(TEXT("Build navigate widget blueprint"), WBP != nullptr))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	if (!TestTrue(TEXT("Initial save with wiring succeeds"),
		FUMGGenerator::SaveWidgetBlueprint(WBP, nullptr, &NavigationData).IsSuccess()))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UEdGraph* Ubergraph = WBP->UbergraphPages[0];
	const auto CountMarkerNodes = [Ubergraph]()
	{
		int32 Count = 0;
		for (const UEdGraphNode* Node : Ubergraph->Nodes)
		{
			Count += Node && Node->NodeComment.Equals(Design2UMGPrototypeGraphWiring::NodeMarkerComment, ESearchCase::CaseSensitive) ? 1 : 0;
		}
		return Count;
	};
	const int32 NodeCountAfterSave = Ubergraph->Nodes.Num();
	// The fade transition chain with the history push authors twelve marker nodes (event,
	// out get, PlayAnimation, Delay, switcher get, GetActiveWidgetIndex, history get, Array_Add,
	// SetActiveWidgetIndex, switcher get, in get, PlayAnimation); the total count also pins the
	// baseline the regenerate below must hold.
	TestEqual(TEXT("Initial wiring authors the marker transition chain"), CountMarkerNodes(), 12);
	TestEqual(TEXT("Initial wiring authors two transition animations"), WBP->Animations.Num(), 2);
	if (!TestEqual(TEXT("Initial wiring authors one bound event"), CollectBoundEvents(WBP).Num(), 1))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	// Regenerate: re-run the animation builder (name-prefix purge inside) and the wiring over the
	// same blueprint; neither animations, nodes nor the generated member variable may accumulate.
	UMGPrototypeAnimationBuilder::GenerateNavigationAnimations(
		WBP, Data.RootWidget, NavigationData);
	TestEqual(TEXT("Regenerated animations stay at two"), WBP->Animations.Num(), 2);

	TArray<FString> WiringWarnings;
	Design2UMGPrototypeGraphWiring::WireSimplePrototypeBindings(WBP, NavigationData, WiringWarnings);
	TestEqual(TEXT("Fully eligible fixture re-wires without skips"), WiringWarnings.Num(), 0);
	TestEqual(TEXT("Re-wiring keeps the node count stable"), Ubergraph->Nodes.Num(), NodeCountAfterSave);
	TestEqual(TEXT("Re-wiring keeps exactly the marker transition chain"), CountMarkerNodes(), 12);
	int32 HistoryVariableDescriptions = 0;
	for (const FBPVariableDescription& Variable : WBP->NewVariables)
	{
		HistoryVariableDescriptions += Variable.VarName == FName(Design2UMGPrototypeGraphWiring::NavHistoryVariableName) ? 1 : 0;
	}
	TestEqual(TEXT("Re-wiring does not duplicate the history member variable"), HistoryVariableDescriptions, 1);
	const TArray<UK2Node_ComponentBoundEvent*> Events = CollectBoundEvents(WBP);
	if (!TestEqual(TEXT("Re-wiring still has exactly one bound event"), Events.Num(), 1))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestEqual(TEXT("Re-wired event still targets the button"), Events[0]->ComponentPropertyName.ToString(), TEXT("cta_button"));

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeWiringGraphPurityTest,
	"Design2UMG.WidgetBlueprint.PrototypeGraphWiring.EngineNodePurity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeWiringGraphPurityTest::RunTest(const FString& Parameters)
{
	FDesignDocument Document;
	FUMGWidgetData Data;
	if (!TestTrue(TEXT("Convert navigate prototype fixture"), ConvertPrototype(NavigatePrototypeJson(), Document, Data)))
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ProtoNavWiringPurity");
	DeleteGeneratedAssetsAtPath(OutputPath);

	UWidgetBlueprint* WBP = FUMGGenerator::GenerateWidgetBlueprint(Data, OutputPath);
	if (!TestTrue(TEXT("Generate navigate widget blueprint"), WBP != nullptr)
		|| !TestTrue(TEXT("Ubergraph page exists"), WBP->UbergraphPages.Num() > 0 && WBP->UbergraphPages[0] != nullptr))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	for (UEdGraphNode* Node : WBP->UbergraphPages[0]->Nodes)
	{
		if (!Node)
		{
			continue;
		}
		const UClass* NodeClass = Node->GetClass();
		TestTrue(
			*FString::Printf(TEXT("Node class %s is a native engine class"), *NodeClass->GetName()),
			NodeClass->GetOutermost()->GetName().StartsWith(TEXT("/Script/")));
		// Engine-standard graph content only: the wiring's node kinds plus the engine-config auto-spawned
		// UserWidget ghost events (PreConstruct/Construct/Tick) CreateBlueprint seeds in every WBP.
		TestTrue(
			*FString::Printf(TEXT("Node class %s is an engine-standard node kind"), *NodeClass->GetName()),
			NodeClass == UK2Node_ComponentBoundEvent::StaticClass()
			|| NodeClass == UK2Node_CallFunction::StaticClass()
			|| NodeClass == UK2Node_CallArrayFunction::StaticClass()
			|| NodeClass == UK2Node_VariableGet::StaticClass()
			|| NodeClass == UK2Node_IfThenElse::StaticClass()
			|| NodeClass == UK2Node_FunctionEntry::StaticClass()
			|| NodeClass == UK2Node_FunctionResult::StaticClass()
			|| NodeClass == UK2Node_Event::StaticClass());
	}

	// Engine-standard variable content only: the generated history variable must be the single
	// plugin-owned member variable and resolve to an engine FArrayProperty of int32.
	int32 HistoryVariableCount = 0;
	for (const FBPVariableDescription& Variable : WBP->NewVariables)
	{
		HistoryVariableCount += Variable.VarName == FName(Design2UMGPrototypeGraphWiring::NavHistoryVariableName) ? 1 : 0;
	}
	TestEqual(TEXT("Exactly one generated history member variable"), HistoryVariableCount, 1);
	const UWidgetBlueprintGeneratedClass* PurityGeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(WBP->GeneratedClass);
	const FArrayProperty* PurityHistoryProperty = PurityGeneratedClass
		? FindFProperty<FArrayProperty>(PurityGeneratedClass, FName(Design2UMGPrototypeGraphWiring::NavHistoryVariableName))
		: nullptr;
	TestTrue(TEXT("History member variable compiles to an engine FArrayProperty of int32"),
		PurityHistoryProperty && CastField<FIntProperty>(PurityHistoryProperty->Inner));

	// Engine-standard animation content only: the animation, its MovieScene and its tracks must all
	// be native engine classes (no plugin UObjects baked into the generated asset).
	for (UWidgetAnimation* Animation : WBP->Animations)
	{
		if (!TestTrue(TEXT("Animation entry is a UWidgetAnimation"), Animation != nullptr))
		{
			continue;
		}
		const UClass* AnimationClass = Animation->GetClass();
		TestTrue(
			*FString::Printf(TEXT("Animation class %s is a native engine class"), *AnimationClass->GetName()),
			AnimationClass == UWidgetAnimation::StaticClass()
			&& AnimationClass->GetOutermost()->GetName().StartsWith(TEXT("/Script/")));
		if (const UMovieScene* MovieScene = Animation->MovieScene)
		{
			TestTrue(TEXT("MovieScene is a native engine class"),
				MovieScene->GetClass()->GetOutermost()->GetName().StartsWith(TEXT("/Script/")));
			for (const UMovieSceneTrack* Track : CollectAnimationTracks(Animation))
			{
				TestTrue(
					*FString::Printf(TEXT("Track class %s is a native engine class"), *Track->GetClass()->GetName()),
					Track && Track->GetClass()->GetOutermost()->GetName().StartsWith(TEXT("/Script/")));
			}
		}
	}

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeNavigateSlideTransitionTest,
	"Design2UMG.WidgetBlueprint.PrototypeGraphWiring.SlideTransition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeNavigateSlideTransitionTest::RunTest(const FString& Parameters)
{
	FDesignDocument Document;
	FUMGWidgetData Data;
	if (!TestTrue(TEXT("Convert slide navigate prototype fixture"), ConvertPrototype(SlideNavigatePrototypeJson(), Document, Data)))
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ProtoNavWiringSlide");
	DeleteGeneratedAssetsAtPath(OutputPath);

	FUMGPrototypeNavigationData NavigationData;
	UWidgetBlueprint* WBP = FUMGGenerator::BuildWidgetBlueprint(
		Data, OutputPath, nullptr, nullptr, &NavigationData);
	if (!TestTrue(TEXT("Build slide navigate widget blueprint"), WBP != nullptr)
		|| !TestTrue(TEXT("Save with slide transition wiring succeeds"),
			FUMGGenerator::SaveWidgetBlueprint(WBP, nullptr, &NavigationData).IsSuccess()))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UWidgetAnimation* OutgoingAnimation = FindGeneratedAnimation(WBP, TEXT("__d2u_nav_anim_000_out"));
	UWidgetAnimation* IncomingAnimation = FindGeneratedAnimation(WBP, TEXT("__d2u_nav_anim_000_in"));
	if (!TestNotNull(TEXT("Outgoing slide animation generated"), OutgoingAnimation)
		|| !TestNotNull(TEXT("Incoming slide animation generated"), IncomingAnimation))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	// PUSH is slide-only: no opacity track, one 2D transform track with a translation-only mask.
	TestNull(TEXT("Outgoing slide has no opacity track"), FindOpacityChannel(OutgoingAnimation));
	TestNull(TEXT("Incoming slide has no opacity track"), FindOpacityChannel(IncomingAnimation));

	const UMovieScene2DTransformSection* OutgoingTransform = FindTransformSection(OutgoingAnimation);
	const UMovieScene2DTransformSection* IncomingTransform = FindTransformSection(IncomingAnimation);
	if (!TestNotNull(TEXT("Outgoing transform section exists"), OutgoingTransform)
		|| !TestNotNull(TEXT("Incoming transform section exists"), IncomingTransform))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	for (const UMovieScene2DTransformSection* Section : { OutgoingTransform, IncomingTransform })
	{
		if (!Section)
		{
			continue;
		}
		const EMovieScene2DTransformChannel Channels = Section->GetMask().GetChannels();
		TestTrue(TEXT("Slide masks translation only"),
			EnumHasAllFlags(Channels, EMovieScene2DTransformChannel::Translation)
			&& !EnumHasAnyFlags(Channels, EMovieScene2DTransformChannel::Rotation)
			&& !EnumHasAnyFlags(Channels, EMovieScene2DTransformChannel::Scale));
	}

	// Direction contract for a LEFT PUSH (bidirectional): the switcher (360px wide here, read from
	// the IR switcher node bounds) slides out to +W and in from -W.
	const FUMGWidgetNode* SwitcherNode = FindIrNodeById(
		Data.RootWidget, NavigationData.SwitcherWidgetId);
	if (!TestNotNull(TEXT("IR switcher node found"), SwitcherNode))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	const float SlideWidth = SwitcherNode->Bounds.W > KINDA_SMALL_NUMBER ? SwitcherNode->Bounds.W : 120.0f;

	TestEqual(TEXT("Outgoing slide starts at the origin"), OutgoingTransform->Translation[0].GetValues()[0].Value, 0.0f);
	TestEqual(TEXT("Outgoing slide ends at +width (LEFT push out)"), OutgoingTransform->Translation[0].GetValues()[4].Value, SlideWidth);
	TestEqual(TEXT("Incoming slide starts at -width (LEFT push in)"), IncomingTransform->Translation[0].GetValues()[0].Value, -SlideWidth);
	TestEqual(TEXT("Incoming slide ends at the origin"), IncomingTransform->Translation[0].GetValues()[4].Value, 0.0f);
	TestEqual(TEXT("Slide stays on the horizontal axis"), OutgoingTransform->Translation[1].GetValues()[4].Value, 0.0f);
	TestEqual(TEXT("Outgoing slide carries 5 sampled keys"), OutgoingTransform->Translation[0].GetNumKeys(), 5);

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeNavigateNoTransitionDirectWiringTest,
	"Design2UMG.WidgetBlueprint.PrototypeGraphWiring.NavigateNoTransition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeNavigateNoTransitionDirectWiringTest::RunTest(const FString& Parameters)
{
	FDesignDocument Document;
	FUMGWidgetData Data;
	if (!TestTrue(TEXT("Convert no-transition navigate fixture"), ConvertPrototype(NavigateNoTransitionJson(), Document, Data)))
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ProtoNavWiringNoTransition");
	DeleteGeneratedAssetsAtPath(OutputPath);

	FUMGPrototypeNavigationData NavigationData;
	UWidgetBlueprint* WBP = FUMGGenerator::BuildWidgetBlueprint(
		Data, OutputPath, nullptr, nullptr, &NavigationData);
	if (!TestTrue(TEXT("Build no-transition widget blueprint"), WBP != nullptr)
		|| !TestTrue(TEXT("Save with direct wiring succeeds"),
			FUMGGenerator::SaveWidgetBlueprint(WBP, nullptr, &NavigationData).IsSuccess()))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	// No transition: no animations are authored and the binding keeps the direct wiring with the
	// history push (bound event -> GetActiveWidgetIndex -> Array_Add -> SetActiveWidgetIndex).
	TestEqual(TEXT("No transition animations are generated"), WBP->Animations.Num(), 0);
	TestFalse(TEXT("No transition animation names are written into the IR"),
		NavigationData.ClickBindings[0].Transition.HasGeneratedAnimations());

	const TArray<UK2Node_ComponentBoundEvent*> Events = CollectBoundEvents(WBP);
	if (!TestEqual(TEXT("Exactly one component-bound event is authored"), Events.Num(), 1))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	// Direct chain: event -> Array_Add(history, pure GetActiveWidgetIndex) -> SetActiveWidgetIndex.
	UK2Node_CallFunction* PushNode = FindExecChainCall(Events[0]);
	if (!TestNotNull(TEXT("Event exec chain reaches the history push"), PushNode))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestEqual(TEXT("First call target is Array_Add"), PushNode->FunctionReference.GetMemberName().ToString(), TEXT("Array_Add"));

	UK2Node_CallFunction* CallNode = NextExecCall(PushNode);
	if (!TestNotNull(TEXT("Chain reaches the page switch call"), CallNode))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestEqual(TEXT("Direct call target is SetActiveWidgetIndex"), CallNode->FunctionReference.GetMemberName().ToString(), TEXT("SetActiveWidgetIndex"));
	TestNull(TEXT("No further node sits in the chain"), NextExecCall(CallNode));

	int32 MarkerNodeCount = 0;
	for (const UEdGraphNode* Node : WBP->UbergraphPages[0]->Nodes)
	{
		MarkerNodeCount += Node && Node->NodeComment.Equals(
			Design2UMGPrototypeGraphWiring::NodeMarkerComment, ESearchCase::CaseSensitive) ? 1 : 0;
	}
	TestEqual(TEXT("Direct wiring authors the marker set (event, two gets, three calls, history get)"), MarkerNodeCount, 7);

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

// An Overlay click makes the generated overlay host SelfHitTestInvisible.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeOverlayGraphWiringTest,
	"Design2UMG.WidgetBlueprint.PrototypeGraphWiring.Overlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeOverlayGraphWiringTest::RunTest(const FString& Parameters)
{
	FDesignDocument Document;
	FUMGWidgetData Data;
	if (!TestTrue(TEXT("Convert overlay prototype fixture"), ConvertPrototype(OverlayBackPrototypeJson(), Document, Data)))
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ProtoNavWiringOverlay");
	DeleteGeneratedAssetsAtPath(OutputPath);

	FUMGPrototypeNavigationData NavigationData;
	UWidgetBlueprint* WBP = FUMGGenerator::BuildWidgetBlueprint(
		Data, OutputPath, nullptr, nullptr, &NavigationData);
	if (!TestTrue(TEXT("Build overlay widget blueprint"), WBP != nullptr)
		|| !TestTrue(TEXT("Save overlay widget blueprint"),
			FUMGGenerator::SaveWidgetBlueprint(WBP, nullptr, &NavigationData).IsSuccess()))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	// BuildRoot synthesized the host as a Collapsed overlay child of the wrapping root overlay.
	UOverlay* OverlayHost = WBP->WidgetTree
		? Cast<UOverlay>(WBP->WidgetTree->FindWidget(FName(*NavigationData.OverlayHostWidgetName)))
		: nullptr;
	if (!TestNotNull(TEXT("Overlay host widget exists in the tree"), OverlayHost))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestEqual(TEXT("Overlay host starts collapsed"), OverlayHost->GetVisibility(), ESlateVisibility::Collapsed);

	// The fixture wires two bindings (overlay open + back); isolate the overlay trigger's event.
	const TArray<UK2Node_ComponentBoundEvent*> Events = CollectBoundEvents(WBP);
	const UK2Node_ComponentBoundEvent* OverlayEvent = nullptr;
	for (const UK2Node_ComponentBoundEvent* Event : Events)
	{
		if (Event && Event->ComponentPropertyName == FName(TEXT("overlay_button")))
		{
			OverlayEvent = Event;
		}
	}
	if (!TestEqual(TEXT("Both bindings wired (overlay + back)"), Events.Num(), 2)
		|| !TestNotNull(TEXT("Overlay bound event found"), OverlayEvent))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	UK2Node_CallFunction* CallNode = FindExecChainCall(OverlayEvent);
	if (!TestNotNull(TEXT("Overlay chain reaches a call node"), CallNode))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestEqual(TEXT("Call target is SetVisibility"), CallNode->FunctionReference.GetMemberName().ToString(), TEXT("SetVisibility"));
	// Self-context call inside the widget's own ubergraph (UUserWidget inherits UWidget::SetVisibility):
	// the member reference stores no parent class, so prove the engine target instead.
	TestTrue(TEXT("SetVisibility call is self-context and UWidget exposes the function"),
		CallNode->FunctionReference.IsSelfContext()
		&& UWidget::StaticClass()->FindFunctionByName(TEXT("SetVisibility")) != nullptr);

	const UEdGraphPin* VisibilityPin = CallNode->FindPin(TEXT("InVisibility"), EGPD_Input);
	if (!TestNotNull(TEXT("Visibility pin exists"), VisibilityPin))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestEqual(TEXT("Visibility literal is SelfHitTestInvisible (enumerant-name default)"),
		VisibilityPin->DefaultValue,
		StaticEnum<ESlateVisibility>()->GetNameStringByValue(static_cast<int64>(ESlateVisibility::SelfHitTestInvisible)));

	const UEdGraphPin* SelfPin = CallNode->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input);
	TestTrue(TEXT("Overlay host variable get feeds the visibility target"),
		SelfPin && SelfPin->LinkedTo.Num() == 1
		&& Cast<UK2Node_VariableGet>(SelfPin->LinkedTo[0]->GetOwningNode())
		&& Cast<UK2Node_VariableGet>(SelfPin->LinkedTo[0]->GetOwningNode())->GetVarName() == OverlayHost->GetFName());

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

// Back wiring: Branch on Array_IsEmpty(history) guarding SetActiveWidgetIndex(Array_Get at
// Array_LastIndex) -> Array_Remove (still at Array_LastIndex: history stores values, not positions),
// with the overlay host and every hosted page collapsed first when they exist.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeBackGraphWiringTest,
	"Design2UMG.WidgetBlueprint.PrototypeGraphWiring.Back",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeBackGraphWiringTest::RunTest(const FString& Parameters)
{
	FDesignDocument Document;
	FUMGWidgetData Data;
	if (!TestTrue(TEXT("Convert navigate+back prototype fixture"), ConvertPrototype(NavigateBackPrototypeJson(), Document, Data)))
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ProtoNavWiringBack");
	DeleteGeneratedAssetsAtPath(OutputPath);

	FUMGPrototypeNavigationData NavigationData;
	UWidgetBlueprint* WBP = FUMGGenerator::BuildWidgetBlueprint(
		Data, OutputPath, nullptr, nullptr, &NavigationData);
	if (!TestTrue(TEXT("Build navigate+back widget blueprint"), WBP != nullptr)
		|| !TestTrue(TEXT("Save navigate+back widget blueprint"),
			FUMGGenerator::SaveWidgetBlueprint(WBP, nullptr, &NavigationData).IsSuccess()))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	const TArray<UK2Node_ComponentBoundEvent*> Events = CollectBoundEvents(WBP);
	if (!TestEqual(TEXT("Two component-bound events are authored (navigate + back)"), Events.Num(), 2))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	const UK2Node_ComponentBoundEvent* BackEvent = Events[0]->ComponentPropertyName == FName(TEXT("back_button"))
		? Events[0]
		: (Events[1]->ComponentPropertyName == FName(TEXT("back_button")) ? Events[1] : nullptr);
	if (!TestNotNull(TEXT("Back bound event found"), BackEvent))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	// Event -> Branch; condition is the pure empty-history guard.
	const UEdGraphPin* BackThenPin = BackEvent->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
	const UK2Node_IfThenElse* BranchNode = BackThenPin && BackThenPin->LinkedTo.Num() == 1
		? Cast<UK2Node_IfThenElse>(BackThenPin->LinkedTo[0]->GetOwningNode())
		: nullptr;
	if (!TestNotNull(TEXT("Back chain guards with a Branch node"), BranchNode))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	const UEdGraphPin* ConditionPin = BranchNode->FindPin(UEdGraphSchema_K2::PN_Condition, EGPD_Input);
	TestTrue(TEXT("Branch condition comes from Array_IsEmpty"),
		ConditionPin && ConditionPin->LinkedTo.Num() == 1
		&& ConditionPin->LinkedTo[0]->GetOwningNode()->GetClass() == UK2Node_CallArrayFunction::StaticClass()
		&& static_cast<const UK2Node_CallFunction*>(ConditionPin->LinkedTo[0]->GetOwningNode())
			->FunctionReference.GetMemberName() == TEXT("Array_IsEmpty"));

	// False path: SetActiveWidgetIndex fed by Array_LastIndex, then Array_Remove.
	const UEdGraphPin* ElsePin = BranchNode->FindPin(UEdGraphSchema_K2::PN_Else, EGPD_Output);
	const UK2Node_CallFunction* SwitchNode = ElsePin && ElsePin->LinkedTo.Num() == 1
		? Cast<UK2Node_CallFunction>(ElsePin->LinkedTo[0]->GetOwningNode())
		: nullptr;
	if (!TestNotNull(TEXT("Branch false path reaches SetActiveWidgetIndex"), SwitchNode)
		|| !TestTrue(TEXT("Pop target is SetActiveWidgetIndex"),
			SwitchNode && SwitchNode->FunctionReference.GetMemberName() == TEXT("SetActiveWidgetIndex")))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	const UEdGraphPin* IndexPin = SwitchNode->FindPin(TEXT("Index"), EGPD_Input);
	TestTrue(TEXT("Pop index comes from Array_Get"),
		IndexPin && IndexPin->LinkedTo.Num() == 1
		&& IndexPin->LinkedTo[0]->GetOwningNode()->GetClass() == UK2Node_CallArrayFunction::StaticClass()
		&& static_cast<const UK2Node_CallFunction*>(IndexPin->LinkedTo[0]->GetOwningNode())
			->FunctionReference.GetMemberName() == TEXT("Array_Get"));
	TestTrue(TEXT("Array_Get reads the history at Array_LastIndex"),
		IndexPin && [&]()
		{
			const UEdGraphPin* GetIndexPin = IndexPin->LinkedTo.Num() == 1
				? IndexPin->LinkedTo[0]->GetOwningNode()->FindPin(TEXT("Index"), EGPD_Input)
				: nullptr;
			return GetIndexPin && GetIndexPin->LinkedTo.Num() == 1
				&& GetIndexPin->LinkedTo[0]->GetOwningNode()->GetClass() == UK2Node_CallArrayFunction::StaticClass()
				&& static_cast<const UK2Node_CallFunction*>(GetIndexPin->LinkedTo[0]->GetOwningNode())
					->FunctionReference.GetMemberName() == TEXT("Array_LastIndex");
		}());

	const UK2Node_CallFunction* RemoveNode = NextExecCall(SwitchNode);
	TestTrue(TEXT("Pop continues into Array_Remove"),
		RemoveNode && RemoveNode->FunctionReference.GetMemberName() == TEXT("Array_Remove"));
	const UEdGraphPin* RemoveArrayPin = RemoveNode ? RemoveNode->FindPin(TEXT("TargetArray"), EGPD_Input) : nullptr;
	TestTrue(TEXT("Array_Remove edits the history variable"),
		RemoveArrayPin && RemoveArrayPin->LinkedTo.Num() == 1
		&& Cast<UK2Node_VariableGet>(RemoveArrayPin->LinkedTo[0]->GetOwningNode())
		&& Cast<UK2Node_VariableGet>(RemoveArrayPin->LinkedTo[0]->GetOwningNode())->GetVarName()
			== FName(Design2UMGPrototypeGraphWiring::NavHistoryVariableName));

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

// runtime smoke (latent-pump pattern; this fixture has no transition, so switches are
// immediate): navigate once pushes the current index 0, BACK pops it and returns to page 0.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeBackRuntimeSmokeTest,
	"Design2UMG.WidgetBlueprint.PrototypeGraphWiring.BackRuntimeSmoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeBackRuntimeSmokeTest::RunTest(const FString& Parameters)
{
	FDesignDocument Document;
	FUMGWidgetData Data;
	if (!TestTrue(TEXT("Convert navigate+back prototype fixture"), ConvertPrototype(NavigateBackPrototypeJson(), Document, Data)))
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ProtoNavWiringBackSmoke");
	DeleteGeneratedAssetsAtPath(OutputPath);

	FUMGPrototypeNavigationData NavigationData;
	UWidgetBlueprint* WBP = FUMGGenerator::BuildWidgetBlueprint(
		Data, OutputPath, nullptr, nullptr, &NavigationData);
	if (!TestTrue(TEXT("Build navigate+back widget blueprint"), WBP != nullptr)
		|| !TestTrue(TEXT("Save navigate+back widget blueprint"),
			FUMGGenerator::SaveWidgetBlueprint(WBP, nullptr, &NavigationData).IsSuccess()))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(WBP->GeneratedClass);
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	UUserWidget* WidgetInstance = GeneratedClass && World ? CreateWidget<UUserWidget>(World, GeneratedClass) : nullptr;
	if (!TestTrue(TEXT("Widget instance created"), WidgetInstance != nullptr))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	WidgetInstance->TakeWidget();

	UWidgetSwitcher* Switcher = Cast<UWidgetSwitcher>(
		WidgetInstance->WidgetTree ? WidgetInstance->WidgetTree->FindWidget(FName(*NavigationData.SwitcherWidgetName)) : nullptr);
	UButton* NavButton = WidgetInstance->WidgetTree ? Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("nav_button"))) : nullptr;
	UButton* BackButton = WidgetInstance->WidgetTree ? Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("back_button"))) : nullptr;
	if (!TestTrue(TEXT("Switcher exists"), Switcher != nullptr)
		|| !TestTrue(TEXT("Navigate button exists"), NavButton != nullptr)
		|| !TestTrue(TEXT("Back button exists"), BackButton != nullptr))
	{
		WidgetInstance->RemoveFromParent();
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	FArrayProperty* HistoryProperty = FindFProperty<FArrayProperty>(
		GeneratedClass, FName(Design2UMGPrototypeGraphWiring::NavHistoryVariableName));
	const auto HistoryNum = [HistoryProperty, WidgetInstance]()
	{
		return HistoryProperty ? FScriptArrayHelper(HistoryProperty, HistoryProperty->ContainerPtrToValuePtr<void>(WidgetInstance)).Num() : -1;
	};

	TestEqual(TEXT("History starts empty"), HistoryNum(), 0);
	TestEqual(TEXT("Initial active page"), Switcher->GetActiveWidgetIndex(), 0);

	NavButton->OnClicked.Broadcast();
	TestEqual(TEXT("Navigate switches to page 1 immediately (no transition)"), Switcher->GetActiveWidgetIndex(), 1);
	TestEqual(TEXT("Navigate pushed the left page index"), HistoryNum(), 1);
	if (HistoryProperty)
	{
		FScriptArrayHelper Helper(HistoryProperty, HistoryProperty->ContainerPtrToValuePtr<void>(WidgetInstance));
		TestEqual(TEXT("Pushed value is the page that was left"), Helper.Num() == 1 ? *reinterpret_cast<int32*>(Helper.GetRawPtr(0)) : INDEX_NONE, 0);
	}

	BackButton->OnClicked.Broadcast();
	TestEqual(TEXT("Back returns to page 0"), Switcher->GetActiveWidgetIndex(), 0);
	TestEqual(TEXT("Back popped the history entry"), HistoryNum(), 0);

	// Empty-history guard: a second BACK must not move the page.
	BackButton->OnClicked.Broadcast();
	TestEqual(TEXT("Second back is a no-op on an empty history"), Switcher->GetActiveWidgetIndex(), 0);
	TestEqual(TEXT("History stays empty"), HistoryNum(), 0);

	WidgetInstance->RemoveFromParent();
	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

// keyboard wiring: generated OnKeyDown/OnKeyUp override function graphs, one key-equality
// Branch per key code guarding the Navigate chain, CDO flagged focusable, and the compiled
// overrides callable via ProcessEvent.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeKeyboardGraphWiringTest,
	"Design2UMG.WidgetBlueprint.PrototypeGraphWiring.Keyboard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeKeyboardGraphWiringTest::RunTest(const FString& Parameters)
{
	FDesignDocument Document;
	FUMGWidgetData Data;
	if (!TestTrue(TEXT("Convert keyboard prototype fixture"), ConvertPrototype(KeyboardNavigatePrototypeJson(), Document, Data)))
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ProtoNavWiringKeyboard");
	DeleteGeneratedAssetsAtPath(OutputPath);

	FUMGPrototypeNavigationData NavigationData;
	UWidgetBlueprint* WBP = FUMGGenerator::BuildWidgetBlueprint(
		Data, OutputPath, nullptr, nullptr, &NavigationData);
	if (!TestTrue(TEXT("Build keyboard widget blueprint"), WBP != nullptr)
		|| !TestTrue(TEXT("Save keyboard widget blueprint"),
			FUMGGenerator::SaveWidgetBlueprint(WBP, nullptr, &NavigationData).IsSuccess()))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	// One generated override graph per event kind, all engine-standard node classes inside.
	for (const TCHAR* EventName : { TEXT("OnKeyDown"), TEXT("OnKeyUp") })
	{
		const UEdGraph* KeyGraph = nullptr;
		for (const UEdGraph* Graph : WBP->FunctionGraphs)
		{
			if (Graph && Graph->GetFName() == FName(EventName))
			{
				KeyGraph = Graph;
			}
		}
		if (!TestNotNull(FString::Printf(TEXT("Generated %s override graph exists"), EventName), KeyGraph))
		{
			DeleteGeneratedAssetsAtPath(OutputPath);
			return false;
		}
		bool bFoundEntry = false;
		bool bFoundResult = false;
		for (const UEdGraphNode* Node : KeyGraph->Nodes)
		{
			if (!Node)
			{
				continue;
			}
			const UClass* NodeClass = Node->GetClass();
			TestTrue(
				*FString::Printf(TEXT("Keyboard node class %s is an engine-standard node kind"), *NodeClass->GetName()),
				NodeClass == UK2Node_FunctionEntry::StaticClass()
				|| NodeClass == UK2Node_FunctionResult::StaticClass()
				|| NodeClass == UK2Node_CallFunction::StaticClass()
				|| NodeClass == UK2Node_CallArrayFunction::StaticClass()
				|| NodeClass == UK2Node_VariableGet::StaticClass()
				|| NodeClass == UK2Node_IfThenElse::StaticClass());
			bFoundEntry |= Cast<UK2Node_FunctionEntry>(Node) != nullptr;
			bFoundResult |= Cast<UK2Node_FunctionResult>(Node) != nullptr;
		}
		TestTrue(FString::Printf(TEXT("%s graph has entry and result terminators"), EventName), bFoundEntry && bFoundResult);
	}

	// The OnKeyDown branch: entry -> Branch, key literal "N", tail reaches the switch call.
	const UEdGraph* KeyDownGraph = nullptr;
	for (const UEdGraph* Graph : WBP->FunctionGraphs)
	{
		if (Graph && Graph->GetFName() == FName(TEXT("OnKeyDown")))
		{
			KeyDownGraph = Graph;
		}
	}
	const UK2Node_FunctionEntry* EntryNode = nullptr;
	for (const UEdGraphNode* Node : KeyDownGraph->Nodes)
	{
		if (const UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node))
		{
			EntryNode = Entry;
		}
	}
	const UEdGraphPin* EntryThenPin = EntryNode ? EntryNode->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output) : nullptr;
	const UK2Node_IfThenElse* BranchNode = EntryThenPin && EntryThenPin->LinkedTo.Num() == 1
		? Cast<UK2Node_IfThenElse>(EntryThenPin->LinkedTo[0]->GetOwningNode())
		: nullptr;
	if (!TestNotNull(TEXT("OnKeyDown entry feeds a key Branch"), BranchNode))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	const UK2Node_CallFunction* KeyEqualsNode = nullptr;
	for (const UEdGraphNode* Node : KeyDownGraph->Nodes)
	{
		if (const UK2Node_CallFunction* Candidate = Cast<UK2Node_CallFunction>(Node))
		{
			if (Candidate->FunctionReference.GetMemberName() == TEXT("EqualEqual_KeyKey"))
			{
				KeyEqualsNode = Candidate;
			}
		}
	}
	if (!TestNotNull(TEXT("Key equality call authored"), KeyEqualsNode))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	const UEdGraphPin* BPin = KeyEqualsNode->FindPin(TEXT("B"), EGPD_Input);
	TestTrue(TEXT("Key literal pin carries the resolved FKey name"),
		BPin && BPin->DefaultValue == TEXT("N"));

	// Compiled overrides exist and the class defaults are focusable so keys can reach the widget.
	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(WBP->GeneratedClass);
	TestNotNull(TEXT("Compiled class carries the OnKeyDown override"),
		GeneratedClass ? GeneratedClass->FindFunctionByName(TEXT("OnKeyDown")) : nullptr);
	TestNotNull(TEXT("Compiled class carries the OnKeyUp override"),
		GeneratedClass ? GeneratedClass->FindFunctionByName(TEXT("OnKeyUp")) : nullptr);
	const UUserWidget* WidgetCDO = GeneratedClass ? GeneratedClass->GetDefaultObject<UUserWidget>() : nullptr;
	TestTrue(TEXT("Generated class CDO is focusable"), WidgetCDO && WidgetCDO->IsFocusable());

	// Runtime: drive both overrides through ProcessEvent with the FKeyEvent parms layout.
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	UUserWidget* WidgetInstance = GeneratedClass && World ? CreateWidget<UUserWidget>(World, GeneratedClass) : nullptr;
	if (!TestTrue(TEXT("Widget instance created"), WidgetInstance != nullptr))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	WidgetInstance->TakeWidget();
	UWidgetSwitcher* Switcher = Cast<UWidgetSwitcher>(
		WidgetInstance->WidgetTree ? WidgetInstance->WidgetTree->FindWidget(FName(*NavigationData.SwitcherWidgetName)) : nullptr);
	if (!TestNotNull(TEXT("Switcher exists"), Switcher))
	{
		WidgetInstance->RemoveFromParent();
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	struct FOnKeyDownParms { FGeometry MyGeometry; FKeyEvent InKeyEvent; FEventReply ReturnValue; };
	const auto ProcessKeyEvent = [WidgetInstance, GeneratedClass](const FKey Key, const TCHAR* FunctionName)
	{
		FOnKeyDownParms Parms;
		Parms.InKeyEvent = FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0);
		if (UFunction* Function = GeneratedClass->FindFunctionByName(FunctionName))
		{
			WidgetInstance->ProcessEvent(Function, &Parms);
		}
	};
	ProcessKeyEvent(EKeys::N, TEXT("OnKeyDown"));
	TestEqual(TEXT("Page one N switches to page two"), Switcher->GetActiveWidgetIndex(), 1);
	ProcessKeyEvent(EKeys::M, TEXT("OnKeyUp"));
	TestEqual(TEXT("Page one M is gated out while page two is active"), Switcher->GetActiveWidgetIndex(), 1);
	ProcessKeyEvent(EKeys::N, TEXT("OnKeyDown"));
	TestEqual(TEXT("Page two N switches back to page one"), Switcher->GetActiveWidgetIndex(), 0);

	WidgetInstance->RemoveFromParent();
	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

// Mixed generated/user keyboard override: once a user node sits in the generated OnKeyDown graph,
// ClearGeneratedBindings must preserve the whole graph (and its generated nodes) instead of deleting
// the user logic with it; re-authoring must skip that event graph rather than mask it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeKeyboardMixedGraphPreservedTest,
	"Design2UMG.WidgetBlueprint.PrototypeGraphWiring.KeyboardMixedGraphPreserved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeKeyboardMixedGraphPreservedTest::RunTest(const FString& Parameters)
{
	FDesignDocument Document;
	FUMGWidgetData Data;
	if (!TestTrue(TEXT("Convert keyboard prototype fixture"), ConvertPrototype(KeyboardNavigatePrototypeJson(), Document, Data)))
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ProtoNavWiringKeyboardMixedGraph");
	DeleteGeneratedAssetsAtPath(OutputPath);

	FUMGPrototypeNavigationData NavigationData;
	UWidgetBlueprint* WBP = FUMGGenerator::BuildWidgetBlueprint(
		Data, OutputPath, nullptr, nullptr, &NavigationData);
	if (!TestTrue(TEXT("Build keyboard widget blueprint"), WBP != nullptr)
		|| !TestEqual(TEXT("Supported keyboard save is not degraded"),
			FUMGGenerator::SaveWidgetBlueprint(WBP, nullptr, &NavigationData).Status,
			EDesign2UMGCommitStatus::Succeeded))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UEdGraph* KeyDownGraph = nullptr;
	for (UEdGraph* Graph : WBP->FunctionGraphs)
	{
		if (Graph && Graph->GetFName() == FName(TEXT("OnKeyDown")))
		{
			KeyDownGraph = Graph;
		}
	}
	if (!TestNotNull(TEXT("Generated OnKeyDown graph exists"), KeyDownGraph))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UFunction* PrintFunction = UKismetSystemLibrary::StaticClass()->FindFunctionByName(
		GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString));
	if (!TestNotNull(TEXT("PrintString exists for the user node"), PrintFunction))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	UK2Node_CallFunction* UserNode = NewObject<UK2Node_CallFunction>(KeyDownGraph);
	UserNode->SetFromFunction(PrintFunction);
	KeyDownGraph->AddNode(UserNode, /*bUserAction=*/true, /*bNotify=*/false);
	UserNode->CreateNewGuid();
	UserNode->PostPlacedNewNode();
	UserNode->AllocateDefaultPins();

	TArray<FString> PreservedMixedGraphs;
	Design2UMGPrototypeGraphWiring::ClearGeneratedBindings(WBP, &PreservedMixedGraphs);
	TestTrue(TEXT("Mixed OnKeyDown graph reported as preserved"),
		PreservedMixedGraphs.Contains(TEXT("OnKeyDown")));
	TestTrue(TEXT("Mixed graph itself survives the clear"),
		WBP->FunctionGraphs.Contains(KeyDownGraph));
	TestTrue(TEXT("User node inside the mixed graph survives the clear"),
		KeyDownGraph->Nodes.Contains(UserNode));

	TArray<FString> WiringWarnings;
	Design2UMGPrototypeGraphWiring::WireSimplePrototypeBindings(WBP, NavigationData, WiringWarnings);
	TestTrue(TEXT("Re-authoring preserves the mixed graph"),
		WBP->FunctionGraphs.Contains(KeyDownGraph));
	TestTrue(TEXT("Re-authoring preserves the user node"),
		KeyDownGraph->Nodes.Contains(UserNode));
	TestTrue(TEXT("Re-authoring reports the skipped OnKeyDown override"),
		WiringWarnings.ContainsByPredicate([](const FString& Warning)
		{
			return Warning.Contains(TEXT("OnKeyDown")) && Warning.Contains(TEXT("already exists"));
		}));

	const FDesign2UMGCommitResult SaveWithoutDiagnostics =
		FUMGGenerator::SaveWidgetBlueprint(WBP, nullptr, &NavigationData);
	TestEqual(TEXT("Wiring degradation does not require a diagnostic output"), SaveWithoutDiagnostics.Status,
		EDesign2UMGCommitStatus::SucceededWithDegradation);
	TestEqual(TEXT("Degraded asset-only save still commits the asset"), SaveWithoutDiagnostics.Asset,
		EDesign2UMGFileState::Saved);
	TestEqual(TEXT("Asset-only save does not request a manifest"), SaveWithoutDiagnostics.Manifest,
		EDesign2UMGFileState::Unchanged);

	FDesign2UMGGenerationOutput SaveOutput;
	const FDesign2UMGCommitResult SaveResult = FUMGGenerator::SaveWidgetBlueprint(WBP, &SaveOutput, &NavigationData);
	TestEqual(TEXT("Save promotes a wiring skip to degradation"), SaveResult.Status,
		EDesign2UMGCommitStatus::SucceededWithDegradation);
	TestTrue(TEXT("Save output carries the wiring degradation signal"), SaveOutput.bHasDegradation);
	TestTrue(TEXT("Save output surfaces the wiring skip diagnostic"),
		SaveOutput.MappingDiagnostics.ContainsByPredicate([](const FDesign2UMGMappingDiagnostic& Diagnostic)
		{
			return Diagnostic.Kind == TEXT("prototype_wiring_skipped")
				&& Diagnostic.Message.Contains(TEXT("OnKeyDown"));
		}));

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

// Reimport preserves a mixed keyboard graph and reports its user-content conflict.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeKeyboardMixedGraphReimportPreservedTest,
	"Design2UMG.WidgetBlueprint.PrototypeGraphWiring.KeyboardMixedGraphReimportPreserved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeKeyboardMixedGraphReimportPreservedTest::RunTest(const FString& Parameters)
{
	FDesignDocument Document;
	FUMGWidgetData Data;
	if (!TestTrue(TEXT("Convert keyboard prototype fixture"), ConvertPrototype(KeyboardNavigatePrototypeJson(), Document, Data)))
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ProtoNavWiringKeyboardMixedGraphReimport");
	DeleteGeneratedAssetsAtPath(OutputPath);

	FDesign2UMGGenerationOutput GenerationOutput;
	UWidgetBlueprint* WBP = FUMGGenerator::GenerateWidgetBlueprint(Data, OutputPath, nullptr, &GenerationOutput);
	if (!TestTrue(TEXT("Generate keyboard widget blueprint with manifest"), WBP != nullptr))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	UEdGraph* KeyDownGraph = nullptr;
	for (UEdGraph* Graph : WBP->FunctionGraphs)
	{
		if (Graph && Graph->GetFName() == FName(TEXT("OnKeyDown")))
		{
			KeyDownGraph = Graph;
		}
	}
	UFunction* PrintFunction = UKismetSystemLibrary::StaticClass()->FindFunctionByName(
		GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString));
	if (!TestNotNull(TEXT("Generated OnKeyDown graph exists"), KeyDownGraph)
		|| !TestNotNull(TEXT("PrintString exists for the user node"), PrintFunction))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	UK2Node_CallFunction* UserNode = NewObject<UK2Node_CallFunction>(KeyDownGraph);
	UserNode->SetFromFunction(PrintFunction);
	KeyDownGraph->AddNode(UserNode, /*bUserAction=*/true, /*bNotify=*/false);
	UserNode->CreateNewGuid();
	UserNode->PostPlacedNewNode();
	UserNode->AllocateDefaultPins();

	FDesignReimportRequest Request;
	Request.TargetAssetPath = TargetPackage;
	const FDesignReimportResult Result = DesignImportPipeline::Reimport(Document, Request);
	TestTrue(TEXT("Reimport with a mixed keyboard graph succeeds"), Result.bSucceeded);
	TestTrue(TEXT("Preserved mixed graph raises explicit degradation"), Result.bHasDegradation);
	TestEqual(TEXT("Preserved mixed graph is not plain success"), Result.Commit.Status,
		EDesign2UMGCommitStatus::SucceededWithDegradation);

	UEdGraph* KeyDownGraphAfter = nullptr;
	for (UEdGraph* Graph : WBP->FunctionGraphs)
	{
		if (Graph && Graph->GetFName() == FName(TEXT("OnKeyDown")))
		{
			KeyDownGraphAfter = Graph;
		}
	}
	TestNotNull(TEXT("Mixed OnKeyDown graph survives the full reimport"), KeyDownGraphAfter);
	TestTrue(TEXT("User node survives the full reimport"),
		KeyDownGraphAfter && KeyDownGraphAfter->Nodes.Contains(UserNode));

	bool bConflictDiagnosed = false;
	for (const FDesign2UMGMappingDiagnostic& Diagnostic : Result.Diagnostics)
	{
		if (Diagnostic.Kind == TEXT("reimport_prototype_graph_conflict")
			&& Diagnostic.Message.Contains(TEXT("OnKeyDown")))
		{
			bConflictDiagnosed = true;
		}
	}
	TestTrue(TEXT("Reimport reports the preserved mixed graph conflict"), bConflictDiagnosed);

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

// Verify that a UUserWidget::OnKeyDown override (BlueprintImplementableEvent with an
// FEventReply return value, hence NOT placeable as an ubergraph event node) is expressible as a
// generated function graph that compiles and lands on the generated class. Keyboard wiring is only
// implemented if this passes.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeKeyDownOverrideTest,
	"Design2UMG.WidgetBlueprint.PrototypeGraphWiring.KeyDownOverride",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeKeyDownOverrideTest::RunTest(const FString& Parameters)
{
	FDesignDocument Document;
	FUMGWidgetData Data;
	if (!TestTrue(TEXT("Convert navigate prototype fixture"), ConvertPrototype(NavigateNoTransitionJson(), Document, Data)))
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/PrototypeKeyboardNavigation");
	DeleteGeneratedAssetsAtPath(OutputPath);

	FUMGPrototypeNavigationData NavigationData;
	UWidgetBlueprint* WBP = FUMGGenerator::BuildWidgetBlueprint(
		Data, OutputPath, nullptr, nullptr, &NavigationData);
	if (!TestTrue(TEXT("Build keyboard widget blueprint"), WBP != nullptr)
		|| !TestTrue(TEXT("Save keyboard widget blueprint"),
			FUMGGenerator::SaveWidgetBlueprint(WBP, nullptr, &NavigationData).IsSuccess()))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	// Engine-canonical override authoring (SMyBlueprint::ImplementFunction path for functions that
	// cannot be placed as events): CreateNewGraph + AddFunctionGraph seeds entry/result terminators.
	UEdGraph* KeyGraph = FBlueprintEditorUtils::CreateNewGraph(
		WBP, FName(TEXT("OnKeyDown")), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	FBlueprintEditorUtils::AddFunctionGraph(
		WBP, KeyGraph, /*bIsUserCreated=*/false, UUserWidget::StaticClass());

	bool bFoundEntry = false;
	bool bFoundResult = false;
	for (const UEdGraphNode* Node : KeyGraph->Nodes)
	{
		bFoundEntry |= Cast<UK2Node_FunctionEntry>(Node) != nullptr;
		bFoundResult |= Cast<UK2Node_FunctionResult>(Node) != nullptr;
	}
	TestTrue(TEXT("OnKeyDown graph seeds a function entry node"), bFoundEntry);
	TestTrue(TEXT("OnKeyDown graph seeds a function result node for the FEventReply return"), bFoundResult);

	const EBlueprintCompileOptions CompileOptions =
		EBlueprintCompileOptions::SkipDefaultObjectValidation |
		EBlueprintCompileOptions::SkipFiBSearchMetaUpdate;
	FKismetEditorUtilities::CompileBlueprint(WBP, CompileOptions);
	if (!TestTrue(TEXT("OnKeyDown override graph compiles"),
			WBP->Status == BS_UpToDate || WBP->Status == BS_UpToDateWithWarnings))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestNotNull(TEXT("Generated class carries the compiled OnKeyDown override"),
		Cast<UWidgetBlueprintGeneratedClass>(WBP->GeneratedClass)
			? Cast<UWidgetBlueprintGeneratedClass>(WBP->GeneratedClass)->FindFunctionByName(TEXT("OnKeyDown"))
			: nullptr);

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

// Multi-step Back runtime smoke: history stores the VALUES of the left pages, so popping [0,2]
// must land on page 2 (the last value), not page 1 (the last index). Empty history stays a no-op.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeBackMultiStepRuntimeSmokeTest,
	"Design2UMG.WidgetBlueprint.PrototypeGraphWiring.BackMultiStepRuntimeSmoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeBackMultiStepRuntimeSmokeTest::RunTest(const FString& Parameters)
{
	FDesignDocument Document;
	FUMGWidgetData Data;
	if (!TestTrue(TEXT("Convert multi-step navigate+back prototype fixture"), ConvertPrototype(MultiStepNavigateBackPrototypeJson(), Document, Data)))
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ProtoNavWiringBackMultiStep");
	DeleteGeneratedAssetsAtPath(OutputPath);

	FUMGPrototypeNavigationData NavigationData;
	UWidgetBlueprint* WBP = FUMGGenerator::BuildWidgetBlueprint(
		Data, OutputPath, nullptr, nullptr, &NavigationData);
	if (!TestTrue(TEXT("Build multi-step navigate+back widget blueprint"), WBP != nullptr)
		|| !TestTrue(TEXT("Save multi-step navigate+back widget blueprint"),
			FUMGGenerator::SaveWidgetBlueprint(WBP, nullptr, &NavigationData).IsSuccess()))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(WBP->GeneratedClass);
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	UUserWidget* WidgetInstance = GeneratedClass && World ? CreateWidget<UUserWidget>(World, GeneratedClass) : nullptr;
	if (!TestTrue(TEXT("Widget instance created"), WidgetInstance != nullptr))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	WidgetInstance->TakeWidget();

	UWidgetSwitcher* Switcher = Cast<UWidgetSwitcher>(
		WidgetInstance->WidgetTree ? WidgetInstance->WidgetTree->FindWidget(FName(*NavigationData.SwitcherWidgetName)) : nullptr);
	UButton* FirstNavButton = WidgetInstance->WidgetTree ? Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("nav_first"))) : nullptr;
	UButton* SecondNavButton = WidgetInstance->WidgetTree ? Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("nav_second"))) : nullptr;
	UButton* BackButton = WidgetInstance->WidgetTree ? Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("back_button"))) : nullptr;
	if (!TestTrue(TEXT("Switcher exists"), Switcher != nullptr)
		|| !TestTrue(TEXT("First navigate button exists"), FirstNavButton != nullptr)
		|| !TestTrue(TEXT("Second navigate button exists"), SecondNavButton != nullptr)
		|| !TestTrue(TEXT("Back button exists"), BackButton != nullptr))
	{
		WidgetInstance->RemoveFromParent();
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	TestEqual(TEXT("Initial active page"), Switcher->GetActiveWidgetIndex(), 0);

	// 0 -> 2 -> 1 builds the history [0, 2]: page indices do not equal their own positions.
	FirstNavButton->OnClicked.Broadcast();
	TestEqual(TEXT("First navigate switches to page 2"), Switcher->GetActiveWidgetIndex(), 2);
	SecondNavButton->OnClicked.Broadcast();
	TestEqual(TEXT("Second navigate switches to page 1"), Switcher->GetActiveWidgetIndex(), 1);

	// History [0, 2] must return page value 2, not array index 1.
	BackButton->OnClicked.Broadcast();
	TestEqual(TEXT("Back returns to the last recorded page value (2), not the last index (1)"),
		Switcher->GetActiveWidgetIndex(), 2);
	BackButton->OnClicked.Broadcast();
	TestEqual(TEXT("Second back unwinds to the first recorded page"), Switcher->GetActiveWidgetIndex(), 0);

	// Empty history: Back must not move the page.
	BackButton->OnClicked.Broadcast();
	TestEqual(TEXT("Back on empty history is a no-op"), Switcher->GetActiveWidgetIndex(), 0);

	WidgetInstance->RemoveFromParent();
	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

// Overlay runtime smoke: the overlay-target page is parented under the host at build time
// (Collapsed), the open chain shows host + page while the switcher page stays active, and Back
// collapses both layers again.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeOverlayRuntimeSmokeTest,
	"Design2UMG.WidgetBlueprint.PrototypeGraphWiring.OverlayRuntimeSmoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeOverlayRuntimeSmokeTest::RunTest(const FString& Parameters)
{
	FDesignDocument Document;
	FUMGWidgetData Data;
	if (!TestTrue(TEXT("Convert overlay prototype fixture"), ConvertPrototype(OverlayBackPrototypeJson(), Document, Data)))
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ProtoNavWiringOverlaySmoke");
	DeleteGeneratedAssetsAtPath(OutputPath);

	FUMGPrototypeNavigationData NavigationData;
	UWidgetBlueprint* WBP = FUMGGenerator::BuildWidgetBlueprint(
		Data, OutputPath, nullptr, nullptr, &NavigationData);
	if (!TestTrue(TEXT("Build overlay widget blueprint"), WBP != nullptr)
		|| !TestTrue(TEXT("Save overlay widget blueprint"),
			FUMGGenerator::SaveWidgetBlueprint(WBP, nullptr, &NavigationData).IsSuccess()))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	// Structural contract of the built tree: the overlay-target page lives under the host
	// (Collapsed by default) and is NOT a switcher child; the flow page stays in the switcher.
	UOverlay* HostTemplate = WBP->WidgetTree
		? Cast<UOverlay>(WBP->WidgetTree->FindWidget(FName(*NavigationData.OverlayHostWidgetName)))
		: nullptr;
	UWidgetSwitcher* SwitcherTemplate = WBP->WidgetTree
		? Cast<UWidgetSwitcher>(WBP->WidgetTree->FindWidget(FName(*NavigationData.SwitcherWidgetName)))
		: nullptr;
	if (!TestNotNull(TEXT("Overlay host exists in the built tree"), HostTemplate)
		|| !TestNotNull(TEXT("Switcher exists in the built tree"), SwitcherTemplate))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestEqual(TEXT("Overlay host starts collapsed"), HostTemplate->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Overlay-target page is parented under the host"), HostTemplate->GetChildrenCount(), 1);
	UWidget* HostedPageTemplate = HostTemplate->GetChildAt(0);
	TestTrue(TEXT("Hosted page is the overlay target (2:1)"),
		HostedPageTemplate && FUMGWidgetUtils::MatchesWidgetBaseName(
			HostedPageTemplate->GetName(), FUMGWidgetUtils::SanitizeWidgetName(TEXT("2:1"), TEXT("CanvasPanel")).ToString()));
	TestEqual(TEXT("Hosted page defaults to collapsed"), HostedPageTemplate->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Switcher keeps only the non-overlay flow page"), SwitcherTemplate->GetChildrenCount(), 1);

	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(WBP->GeneratedClass);
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	UUserWidget* WidgetInstance = GeneratedClass && World ? CreateWidget<UUserWidget>(World, GeneratedClass) : nullptr;
	if (!TestTrue(TEXT("Widget instance created"), WidgetInstance != nullptr))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	WidgetInstance->TakeWidget();

	UWidgetSwitcher* Switcher = Cast<UWidgetSwitcher>(
		WidgetInstance->WidgetTree ? WidgetInstance->WidgetTree->FindWidget(FName(*NavigationData.SwitcherWidgetName)) : nullptr);
	UOverlay* Host = WidgetInstance->WidgetTree
		? Cast<UOverlay>(WidgetInstance->WidgetTree->FindWidget(FName(*NavigationData.OverlayHostWidgetName)))
		: nullptr;
	UButton* OverlayButton = WidgetInstance->WidgetTree ? Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("overlay_button"))) : nullptr;
	UButton* BackButton = WidgetInstance->WidgetTree ? Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("back_button"))) : nullptr;
	UWidget* HostedPage = Host ? Host->GetChildAt(0) : nullptr;
	if (!TestTrue(TEXT("Runtime switcher exists"), Switcher != nullptr)
		|| !TestTrue(TEXT("Runtime overlay host exists"), Host != nullptr)
		|| !TestTrue(TEXT("Runtime hosted page exists"), HostedPage != nullptr)
		|| !TestTrue(TEXT("Overlay button exists"), OverlayButton != nullptr)
		|| !TestTrue(TEXT("Back button exists"), BackButton != nullptr))
	{
		WidgetInstance->RemoveFromParent();
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	TestEqual(TEXT("Overlay content starts hidden"), HostedPage->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Initial active page"), Switcher->GetActiveWidgetIndex(), 0);

	// Open: both the host and the hosted page turn visible; the switcher page stays active
	// (an overlay rides on top, it does not navigate).
	OverlayButton->OnClicked.Broadcast();
	TestEqual(TEXT("Overlay open shows the host"), Host->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
	TestEqual(TEXT("Overlay open shows the hosted page"), HostedPage->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
	TestEqual(TEXT("Overlay keeps the active switcher page unchanged"), Switcher->GetActiveWidgetIndex(), 0);

	// Back: both layers collapse again (the page reset keeps a later different overlay clean).
	BackButton->OnClicked.Broadcast();
	TestEqual(TEXT("Back collapses the host"), Host->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Back collapses the hosted page"), HostedPage->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Back leaves the empty-history page alone"), Switcher->GetActiveWidgetIndex(), 0);

	WidgetInstance->RemoveFromParent();
	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

// Plain overlay-only page runtime smoke: its Navigate button lives in the visible hosted page, not in
// a mirror copy. Clicking it must exit the overlay (host + hosted page collapse) before switching the
// underlying switcher; otherwise the overlay keeps covering the destination page.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeOverlayOnlyPageNavigateRuntimeSmokeTest,
	"Design2UMG.WidgetBlueprint.PrototypeGraphWiring.OverlayOnlyPageNavigateRuntimeSmoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeOverlayOnlyPageNavigateRuntimeSmokeTest::RunTest(const FString& Parameters)
{
	FDesignDocument Document;
	FUMGWidgetData Data;
	if (!TestTrue(TEXT("Convert overlay-only navigate prototype fixture"), ConvertPrototype(OverlayOnlyPageNavigatePrototypeJson(), Document, Data)))
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ProtoNavWiringOverlayOnlyNavigate");
	DeleteGeneratedAssetsAtPath(OutputPath);

	FUMGPrototypeNavigationData NavigationData;
	UWidgetBlueprint* WBP = FUMGGenerator::BuildWidgetBlueprint(
		Data, OutputPath, nullptr, nullptr, &NavigationData);
	if (!TestTrue(TEXT("Build overlay-only navigate widget blueprint"), WBP != nullptr)
		|| !TestTrue(TEXT("Save overlay-only navigate widget blueprint"),
			FUMGGenerator::SaveWidgetBlueprint(WBP, nullptr, &NavigationData).IsSuccess()))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(WBP->GeneratedClass);
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	UUserWidget* WidgetInstance = GeneratedClass && World ? CreateWidget<UUserWidget>(World, GeneratedClass) : nullptr;
	if (!TestTrue(TEXT("Widget instance created"), WidgetInstance != nullptr))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	WidgetInstance->TakeWidget();

	UWidgetSwitcher* Switcher = Cast<UWidgetSwitcher>(
		WidgetInstance->WidgetTree ? WidgetInstance->WidgetTree->FindWidget(FName(*NavigationData.SwitcherWidgetName)) : nullptr);
	UOverlay* Host = WidgetInstance->WidgetTree
		? Cast<UOverlay>(WidgetInstance->WidgetTree->FindWidget(FName(*NavigationData.OverlayHostWidgetName)))
		: nullptr;
	UButton* OverlayButton = WidgetInstance->WidgetTree ? Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("overlay_button"))) : nullptr;
	UButton* NavButton = WidgetInstance->WidgetTree ? Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("nav_button"))) : nullptr;
	UWidget* HostedPage = Host ? Host->GetChildAt(0) : nullptr;
	if (!TestTrue(TEXT("Switcher exists"), Switcher != nullptr)
		|| !TestTrue(TEXT("Overlay host exists"), Host != nullptr)
		|| !TestTrue(TEXT("Hosted overlay-only page exists"), HostedPage != nullptr)
		|| !TestTrue(TEXT("Overlay button exists"), OverlayButton != nullptr)
		|| !TestTrue(TEXT("Hosted navigate button exists"), NavButton != nullptr))
	{
		WidgetInstance->RemoveFromParent();
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	TestEqual(TEXT("Initial active page"), Switcher->GetActiveWidgetIndex(), 0);
	TestEqual(TEXT("Overlay host starts collapsed"), Host->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Hosted page starts collapsed"), HostedPage->GetVisibility(), ESlateVisibility::Collapsed);

	OverlayButton->OnClicked.Broadcast();
	TestEqual(TEXT("Overlay open shows the host"), Host->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
	TestEqual(TEXT("Overlay open shows the hosted page"), HostedPage->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
	TestEqual(TEXT("Overlay keeps the active page unchanged"), Switcher->GetActiveWidgetIndex(), 0);

	NavButton->OnClicked.Broadcast();
	TestEqual(TEXT("Navigate collapses the overlay host"), Host->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Navigate collapses the hosted page"), HostedPage->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Navigate switches to the target page"), Switcher->GetActiveWidgetIndex(), 1);

	WidgetInstance->RemoveFromParent();
	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

// Dual-target overlay runtime smoke: a page that is both a Navigate target and an Overlay target
// stays a switcher child AND gets a synthesized mirror copy under the overlay host. The Navigate
// binding switches to the switcher instance; the overlay binding opens the mirror as a layered
// display (switcher active index and history untouched); the MIRROR's buttons carry their own
// bindings (the visible buttons inside the overlay): mirror Back collapses the mirror and the
// host without touching the page flow, mirror Navigate closes the overlay then navigates. Also
// locks the mirror purity contract: the mirror subtree carries no design identity (synthesized
// chrome, not design nodes) and its widget names land in the manifest's D2U-owned set.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeDualTargetOverlayMirrorRuntimeSmokeTest,
	"Design2UMG.WidgetBlueprint.PrototypeGraphWiring.DualTargetOverlayMirrorRuntimeSmoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeDualTargetOverlayMirrorRuntimeSmokeTest::RunTest(const FString& Parameters)
{
	FDesignDocument Document;
	FUMGWidgetData Data;
	if (!TestTrue(TEXT("Convert dual-target overlay prototype fixture"), ConvertPrototype(DualTargetOverlayPrototypeJson(), Document, Data)))
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ProtoNavWiringDualTargetOverlay");
	DeleteGeneratedAssetsAtPath(OutputPath);

	FUMGPrototypeNavigationData NavigationData;
	FDesign2UMGGenerationOutput GenerationOutput;
	UWidgetBlueprint* WBP = FUMGGenerator::BuildWidgetBlueprint(
		Data, OutputPath, nullptr, &GenerationOutput, &NavigationData);
	if (!TestTrue(TEXT("Build dual-target overlay widget blueprint"), WBP != nullptr)
		|| !TestTrue(TEXT("Save dual-target overlay widget blueprint"),
			FUMGGenerator::SaveWidgetBlueprint(WBP, nullptr, &NavigationData).IsSuccess()))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	// IR purity: the hosted mirror of the dual-target page has the suffixed name on every node and
	// no design identity anywhere in its subtree (root overlay -> [switcher, overlay host]).
	const FUMGWidgetNode* MirrorRootNode = nullptr;
	if (Data.RootWidget.Children.Num() == 2)
	{
		const FString MirrorName = FString(TEXT("2:1")) + Design2UMGPrototypeTokens::OverlayMirrorSuffix();
		for (const FUMGWidgetNode& HostChild : Data.RootWidget.Children[1].Children)
		{
			if (HostChild.Name == MirrorName)
			{
				MirrorRootNode = &HostChild;
			}
		}
	}
	if (!TestNotNull(TEXT("Mirror copy of the dual-target page exists under the host in the IR"), MirrorRootNode))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TArray<FString> PurityViolations;
	CollectOverlayMirrorPurityViolations(*MirrorRootNode, PurityViolations);
	TestEqual(TEXT("Mirror subtree carries no design identity and is fully suffixed"), PurityViolations.Num(), 0);

	// Manifest ownership: the mirror widgets are D2U-owned regenerated chrome, recorded by their
	// constructed (sanitized) names.
	const FString MirrorPageWidgetName = FUMGWidgetUtils::SanitizeWidgetName(
		FString(TEXT("2:1")) + Design2UMGPrototypeTokens::OverlayMirrorSuffix(), TEXT("CanvasPanel")).ToString();
	const FString MirrorBackButtonName = FUMGWidgetUtils::SanitizeWidgetName(
		FString(TEXT("back_button")) + Design2UMGPrototypeTokens::OverlayMirrorSuffix(), TEXT("Button")).ToString();
	TestTrue(TEXT("Mirror page name lands in the manifest D2U-owned set"),
		GenerationOutput.Manifest.D2UOwnedNames.Contains(MirrorPageWidgetName));
	TestTrue(TEXT("Mirror child name lands in the manifest D2U-owned set"),
		GenerationOutput.Manifest.D2UOwnedNames.Contains(MirrorBackButtonName));

	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(WBP->GeneratedClass);
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	UUserWidget* WidgetInstance = GeneratedClass && World ? CreateWidget<UUserWidget>(World, GeneratedClass) : nullptr;
	if (!TestTrue(TEXT("Widget instance created"), WidgetInstance != nullptr))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	WidgetInstance->TakeWidget();

	UWidgetSwitcher* Switcher = Cast<UWidgetSwitcher>(
		WidgetInstance->WidgetTree ? WidgetInstance->WidgetTree->FindWidget(FName(*NavigationData.SwitcherWidgetName)) : nullptr);
	UOverlay* Host = WidgetInstance->WidgetTree
		? Cast<UOverlay>(WidgetInstance->WidgetTree->FindWidget(FName(*NavigationData.OverlayHostWidgetName)))
		: nullptr;
	UButton* NavButton = WidgetInstance->WidgetTree ? Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("nav_button"))) : nullptr;
	UButton* OverlayButton = WidgetInstance->WidgetTree ? Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("overlay_button"))) : nullptr;
	UButton* BackButton = WidgetInstance->WidgetTree ? Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("back_button"))) : nullptr;
	const FString MirrorNavButtonName = FUMGWidgetUtils::SanitizeWidgetName(
		FString(TEXT("mirror_nav_button")) + Design2UMGPrototypeTokens::OverlayMirrorSuffix(), TEXT("Button")).ToString();
	UButton* MirrorBackButton = WidgetInstance->WidgetTree
		? Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(FName(*MirrorBackButtonName))) : nullptr;
	UButton* MirrorNavButton = WidgetInstance->WidgetTree
		? Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(FName(*MirrorNavButtonName))) : nullptr;
	UWidget* MirrorPage = Host && Host->GetChildrenCount() == 1 ? Host->GetChildAt(0) : nullptr;
	if (!TestTrue(TEXT("Switcher exists"), Switcher != nullptr)
		|| !TestTrue(TEXT("Overlay host exists"), Host != nullptr)
		|| !TestTrue(TEXT("Hosted mirror page exists"), MirrorPage != nullptr)
		|| !TestTrue(TEXT("Hosted page carries the mirror name"),
			MirrorPage && MirrorPage->GetName() == MirrorPageWidgetName)
		|| !TestTrue(TEXT("Navigate button exists"), NavButton != nullptr)
		|| !TestTrue(TEXT("Overlay button exists"), OverlayButton != nullptr)
		|| !TestTrue(TEXT("Back button exists"), BackButton != nullptr)
		|| !TestTrue(TEXT("Mirror back button exists"), MirrorBackButton != nullptr)
		|| !TestTrue(TEXT("Mirror navigate button exists"), MirrorNavButton != nullptr))
	{
		WidgetInstance->RemoveFromParent();
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	FArrayProperty* HistoryProperty = FindFProperty<FArrayProperty>(
		GeneratedClass, FName(Design2UMGPrototypeGraphWiring::NavHistoryVariableName));
	const auto HistoryNum = [HistoryProperty, WidgetInstance]()
	{
		return HistoryProperty ? FScriptArrayHelper(HistoryProperty, HistoryProperty->ContainerPtrToValuePtr<void>(WidgetInstance)).Num() : -1;
	};

	// The dual-target page stays in the switcher alongside the flow page (placement rule); the
	// host carries only the synthesized mirror.
	TestEqual(TEXT("Dual-target page stays a switcher child"), Switcher->GetChildrenCount(), 2);
	TestEqual(TEXT("Host carries only the mirror copy"), Host->GetChildrenCount(), 1);
	TestEqual(TEXT("Initial active page"), Switcher->GetActiveWidgetIndex(), 0);
	TestEqual(TEXT("History starts empty"), HistoryNum(), 0);
	TestEqual(TEXT("Mirror starts collapsed"), MirrorPage->GetVisibility(), ESlateVisibility::Collapsed);

	// (a) The Navigate binding switches to the switcher instance of the dual-target page.
	NavButton->OnClicked.Broadcast();
	TestEqual(TEXT("Navigate switches to the dual-target page"), Switcher->GetActiveWidgetIndex(), 1);
	TestEqual(TEXT("Navigate pushed the left page onto the history"), HistoryNum(), 1);

	// (b) The overlay binding opens the mirror layered on top: the mirror and the host turn
	// visible while the active page and the history stay untouched.
	OverlayButton->OnClicked.Broadcast();
	TestEqual(TEXT("Overlay open shows the host"), Host->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
	TestEqual(TEXT("Overlay open shows the mirror page"), MirrorPage->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
	TestEqual(TEXT("Overlay keeps the active switcher page unchanged"), Switcher->GetActiveWidgetIndex(), 1);
	TestEqual(TEXT("Overlay keeps the history unchanged"), HistoryNum(), 1);

	// (c) The MIRROR's back button is the visible button inside the open overlay: its chain
	// collapses the mirror and the host only - the page flow and the history are exactly as the
	// overlay left them.
	MirrorBackButton->OnClicked.Broadcast();
	TestEqual(TEXT("Mirror back collapses the host"), Host->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Mirror back collapses the mirror page"), MirrorPage->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Mirror back keeps the active page unchanged"), Switcher->GetActiveWidgetIndex(), 1);
	TestEqual(TEXT("Mirror back keeps the history unchanged"), HistoryNum(), 1);

	// (d) Re-open the overlay, then trigger the MIRROR's navigate button (visible only inside the
	// overlay): its chain closes the overlay first, then runs the standard navigate tail - the
	// switcher navigates to "1:1" and the history gains exactly one entry.
	OverlayButton->OnClicked.Broadcast();
	TestEqual(TEXT("Overlay re-open shows the host"), Host->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
	TestEqual(TEXT("Overlay re-open shows the mirror page"), MirrorPage->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
	MirrorNavButton->OnClicked.Broadcast();
	TestEqual(TEXT("Mirror navigate closes the overlay host"), Host->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Mirror navigate collapses the mirror page"), MirrorPage->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Mirror navigate switches to page 1:1"), Switcher->GetActiveWidgetIndex(), 0);
	TestEqual(TEXT("Mirror navigate pushed exactly one history entry"), HistoryNum(), 2);

	WidgetInstance->RemoveFromParent();
	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

// Initial-page overlay runtime smoke: when the INITIAL page is an overlay target it stays a
// switcher child - the startup active index keeps pointing at it (no remap, no fallback) - and
// BuildRoot synthesizes a mirror copy under the overlay host. Right at startup the overlay opens
// the mirror as a layered display (active page and history untouched); the MIRROR's nav button
// (the visible one inside the overlay) closes the overlay and navigates; the ORIGINAL back button
// closes a reopened overlay without popping the history, and a second Back pops it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeInitialPageOverlayMirrorRuntimeSmokeTest,
	"Design2UMG.WidgetBlueprint.PrototypeGraphWiring.InitialPageOverlayMirrorRuntimeSmoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeInitialPageOverlayMirrorRuntimeSmokeTest::RunTest(const FString& Parameters)
{
	FDesignDocument Document;
	FUMGWidgetData Data;
	if (!TestTrue(TEXT("Convert initial-page overlay prototype fixture"), ConvertPrototype(InitialPageOverlayPrototypeJson(), Document, Data)))
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ProtoNavWiringInitialPageOverlay");
	DeleteGeneratedAssetsAtPath(OutputPath);

	FUMGPrototypeNavigationData NavigationData;
	UWidgetBlueprint* WBP = FUMGGenerator::BuildWidgetBlueprint(
		Data, OutputPath, nullptr, nullptr, &NavigationData);
	if (!TestTrue(TEXT("Build initial-page overlay widget blueprint"), WBP != nullptr)
		|| !TestTrue(TEXT("Save initial-page overlay widget blueprint"),
			FUMGGenerator::SaveWidgetBlueprint(WBP, nullptr, &NavigationData).IsSuccess()))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(WBP->GeneratedClass);
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	UUserWidget* WidgetInstance = GeneratedClass && World ? CreateWidget<UUserWidget>(World, GeneratedClass) : nullptr;
	if (!TestTrue(TEXT("Widget instance created"), WidgetInstance != nullptr))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	WidgetInstance->TakeWidget();

	UWidgetSwitcher* Switcher = Cast<UWidgetSwitcher>(
		WidgetInstance->WidgetTree ? WidgetInstance->WidgetTree->FindWidget(FName(*NavigationData.SwitcherWidgetName)) : nullptr);
	UOverlay* Host = WidgetInstance->WidgetTree
		? Cast<UOverlay>(WidgetInstance->WidgetTree->FindWidget(FName(*NavigationData.OverlayHostWidgetName)))
		: nullptr;
	UButton* NavButton = WidgetInstance->WidgetTree ? Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("nav_button"))) : nullptr;
	UButton* OverlayButton = WidgetInstance->WidgetTree ? Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("open_overlay_button"))) : nullptr;
	UButton* BackButton = WidgetInstance->WidgetTree ? Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("back_button"))) : nullptr;
	const FString MirrorPageWidgetName = FUMGWidgetUtils::SanitizeWidgetName(
		FString(TEXT("1:1")) + Design2UMGPrototypeTokens::OverlayMirrorSuffix(), TEXT("CanvasPanel")).ToString();
	const FString MirrorNavButtonName = FUMGWidgetUtils::SanitizeWidgetName(
		FString(TEXT("nav_button")) + Design2UMGPrototypeTokens::OverlayMirrorSuffix(), TEXT("Button")).ToString();
	UButton* MirrorNavButton = WidgetInstance->WidgetTree
		? Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(FName(*MirrorNavButtonName))) : nullptr;
	UWidget* MirrorPage = Host && Host->GetChildrenCount() == 1 ? Host->GetChildAt(0) : nullptr;
	if (!TestTrue(TEXT("Switcher exists"), Switcher != nullptr)
		|| !TestTrue(TEXT("Overlay host exists"), Host != nullptr)
		|| !TestTrue(TEXT("Hosted mirror of the initial page exists"), MirrorPage != nullptr)
		|| !TestTrue(TEXT("Hosted page carries the mirror name"),
			MirrorPage && MirrorPage->GetName() == MirrorPageWidgetName)
		|| !TestTrue(TEXT("Navigate button exists"), NavButton != nullptr)
		|| !TestTrue(TEXT("Overlay button exists"), OverlayButton != nullptr)
		|| !TestTrue(TEXT("Back button exists"), BackButton != nullptr)
		|| !TestTrue(TEXT("Mirror navigate button exists"), MirrorNavButton != nullptr))
	{
		WidgetInstance->RemoveFromParent();
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	FArrayProperty* HistoryProperty = FindFProperty<FArrayProperty>(
		GeneratedClass, FName(Design2UMGPrototypeGraphWiring::NavHistoryVariableName));
	const auto HistoryNum = [HistoryProperty, WidgetInstance]()
	{
		return HistoryProperty ? FScriptArrayHelper(HistoryProperty, HistoryProperty->ContainerPtrToValuePtr<void>(WidgetInstance)).Num() : -1;
	};

	// The startup page IS the overlay target and it must still be the active switcher child: the
	// index stays 0 and the shown widget is the initial page itself, not a remapped fallback. The
	// host carries only the synthesized mirror copy.
	TestEqual(TEXT("Initial page stays a switcher child"), Switcher->GetChildrenCount(), 2);
	TestEqual(TEXT("Host carries only the mirror copy"), Host->GetChildrenCount(), 1);
	TestEqual(TEXT("Startup active index is unchanged"), Switcher->GetActiveWidgetIndex(), 0);
	TestTrue(TEXT("Startup page shown is the initial page"),
		Switcher->GetActiveWidget() && FUMGWidgetUtils::MatchesWidgetBaseName(
			Switcher->GetActiveWidget()->GetName(),
			FUMGWidgetUtils::SanitizeWidgetName(TEXT("1:1"), TEXT("CanvasPanel")).ToString()));
	TestEqual(TEXT("Mirror starts collapsed"), MirrorPage->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("History starts empty"), HistoryNum(), 0);

	// Right at startup (before any navigate): opening the overlay targeting the initial page
	// shows the mirror layered on top - the active page and the history stay untouched.
	OverlayButton->OnClicked.Broadcast();
	TestEqual(TEXT("Startup overlay open shows the host"), Host->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
	TestEqual(TEXT("Startup overlay open shows the initial-page mirror"), MirrorPage->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
	TestEqual(TEXT("Startup overlay keeps the active page unchanged"), Switcher->GetActiveWidgetIndex(), 0);
	TestEqual(TEXT("Startup overlay keeps the history empty"), HistoryNum(), 0);

	// The MIRROR's nav button (nav_button lives on the initial page, so its mirror is the visible
	// one inside the overlay): its chain closes the overlay, then runs the standard navigate
	// tail - the history gains the left page and the switcher moves to page two.
	MirrorNavButton->OnClicked.Broadcast();
	TestEqual(TEXT("Mirror navigate closes the overlay host"), Host->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Mirror navigate collapses the mirror"), MirrorPage->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Mirror navigate switches to page two"), Switcher->GetActiveWidgetIndex(), 1);
	TestEqual(TEXT("Mirror navigate pushed exactly one history entry"), HistoryNum(), 1);

	// Re-open the overlay on top of page two: layered display, page flow untouched.
	OverlayButton->OnClicked.Broadcast();
	TestEqual(TEXT("Overlay re-open shows the host"), Host->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
	TestEqual(TEXT("Overlay re-open shows the initial-page mirror"), MirrorPage->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
	TestEqual(TEXT("Overlay re-open keeps the active page unchanged"), Switcher->GetActiveWidgetIndex(), 1);
	TestEqual(TEXT("Overlay re-open keeps the history unchanged"), HistoryNum(), 1);

	// The ORIGINAL back button closes the overlay without popping the history; the recorded
	// history still returns to the initial page on a second Back.
	BackButton->OnClicked.Broadcast();
	TestEqual(TEXT("Back collapses the host"), Host->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Back collapses the mirror"), MirrorPage->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Back keeps the active page unchanged"), Switcher->GetActiveWidgetIndex(), 1);
	TestEqual(TEXT("Back keeps the history unchanged"), HistoryNum(), 1);

	BackButton->OnClicked.Broadcast();
	TestEqual(TEXT("Second back returns to the initial page"), Switcher->GetActiveWidgetIndex(), 0);
	TestEqual(TEXT("Second back popped the history"), HistoryNum(), 0);

	WidgetInstance->RemoveFromParent();
	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

// Mapped-component overlay test: the dual-target page carries a mapped Figma component instance.
// The mirror copy under the overlay host keeps the component-mapping sidecar fields while its
// design identity stays cleared, and generation resolves the mirror's node to the SAME mapped
// project widget class as the original - mirrors do not degrade mapped components to plain widgets.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeDualTargetOverlayMappedComponentTest,
	"Design2UMG.WidgetBlueprint.PrototypeGraphWiring.DualTargetOverlayMappedComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeDualTargetOverlayMappedComponentTest::RunTest(const FString& Parameters)
{
	FDesignDocument Document;
	FUMGWidgetData Data;
	if (!TestTrue(TEXT("Convert dual-target mapped-component overlay fixture"),
		ConvertPrototype(DualTargetOverlayMappedComponentPrototypeJson(), Document, Data)))
	{
		return false;
	}

	// IR contract: the mirror node kept the mapping sidecar (candidate marker; component identity
	// fields feed the match) with SourceNodeId cleared, while the original node keeps its design
	// identity too.
	const FString MirrorInstanceName = FString(TEXT("MappedButton")) + Design2UMGPrototypeTokens::OverlayMirrorSuffix();
	const FUMGWidgetNode* OriginalInstanceNode = FindWidgetNodeByName(Data.RootWidget, TEXT("MappedButton"));
	const FUMGWidgetNode* MirrorInstanceNode = FindWidgetNodeByName(Data.RootWidget, MirrorInstanceName);
	if (!TestNotNull(TEXT("Original instance node exists in the IR"), OriginalInstanceNode)
		|| !TestNotNull(TEXT("Mirror instance node exists in the IR"), MirrorInstanceNode))
	{
		return false;
	}
	TestTrue(TEXT("Original instance keeps its design identity"),
		OriginalInstanceNode->HasSourceData() && !OriginalInstanceNode->Source().SourceNodeId.IsEmpty());
	TestTrue(TEXT("Mirror instance cleared its design identity"),
		MirrorInstanceNode->HasSourceData() && MirrorInstanceNode->Source().SourceNodeId.IsEmpty());
	TestTrue(TEXT("Original instance is a mapped candidate"),
		OriginalInstanceNode->Source().bMappedComponentCandidate);
	TestTrue(TEXT("Mirror instance kept the mapped-candidate marker"),
		MirrorInstanceNode->Source().bMappedComponentCandidate);

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ProtoNavWiringDualTargetOverlayMapped");
	DeleteGeneratedAssetsAtPath(OutputPath);

	FDesign2UMGGenerationSettingsSnapshot Settings;
	DesignComponentMapping::FComponentMappingEntry& Entry =
		Settings.ComponentMapping.Table.Entries.AddDefaulted_GetRef();
	Entry.Key = TEXT("figma|mirror-mapping-file-key|component-key-primary");
	Entry.Provider = TEXT("figma");
	Entry.DocumentScope = TEXT("mirror-mapping-file-key");
	Entry.ComponentKey = TEXT("component-key-primary");
	Entry.WidgetClass = TEXT("/Script/Design2UMGEditor.Design2UMGMappedComponentTestWidget");

	FUMGPrototypeNavigationData NavigationData;
	UWidgetBlueprint* WBP = FUMGGenerator::BuildWidgetBlueprint(
		Data, OutputPath, &Settings, nullptr, &NavigationData);
	if (!TestTrue(TEXT("Build dual-target mapped-component widget blueprint"), WBP != nullptr)
		|| !TestTrue(TEXT("Save dual-target mapped-component widget blueprint"),
			FUMGGenerator::SaveWidgetBlueprint(WBP, nullptr, &NavigationData).IsSuccess()))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	// Built tree: BOTH the original (inside the switcher page) and the mirror (inside the hosted
	// mirror page) resolved to the mapped project widget class.
	UDesign2UMGMappedComponentTestWidget* OriginalInstance = WBP->WidgetTree
		? Cast<UDesign2UMGMappedComponentTestWidget>(WBP->WidgetTree->FindWidget(TEXT("MappedButton"))) : nullptr;
	UDesign2UMGMappedComponentTestWidget* MirrorInstance = WBP->WidgetTree
		? Cast<UDesign2UMGMappedComponentTestWidget>(WBP->WidgetTree->FindWidget(FName(*MirrorInstanceName))) : nullptr;
	if (!TestTrue(TEXT("Original instance resolved to the mapped project class"), OriginalInstance != nullptr)
		|| !TestTrue(TEXT("Mirror instance resolved to the mapped project class"), MirrorInstance != nullptr))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestTrue(TEXT("Mirror maps to the same widget class as the original"),
		MirrorInstance->GetClass() == OriginalInstance->GetClass());

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

// Back-closes-overlay runtime smoke: A -> B -> open overlay -> Back must stay on B (the overlay
// host and its hosted page collapse, the history is NOT popped); only a second Back pops the
// history and returns to A.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeBackClosesOverlayRuntimeSmokeTest,
	"Design2UMG.WidgetBlueprint.PrototypeGraphWiring.BackClosesOverlayRuntimeSmoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeBackClosesOverlayRuntimeSmokeTest::RunTest(const FString& Parameters)
{
	FDesignDocument Document;
	FUMGWidgetData Data;
	if (!TestTrue(TEXT("Convert navigate+overlay+back prototype fixture"), ConvertPrototype(NavigateOverlayBackPrototypeJson(), Document, Data)))
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ProtoNavWiringBackClosesOverlay");
	DeleteGeneratedAssetsAtPath(OutputPath);

	FUMGPrototypeNavigationData NavigationData;
	UWidgetBlueprint* WBP = FUMGGenerator::BuildWidgetBlueprint(
		Data, OutputPath, nullptr, nullptr, &NavigationData);
	if (!TestTrue(TEXT("Build navigate+overlay+back widget blueprint"), WBP != nullptr)
		|| !TestTrue(TEXT("Save navigate+overlay+back widget blueprint"),
			FUMGGenerator::SaveWidgetBlueprint(WBP, nullptr, &NavigationData).IsSuccess()))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(WBP->GeneratedClass);
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	UUserWidget* WidgetInstance = GeneratedClass && World ? CreateWidget<UUserWidget>(World, GeneratedClass) : nullptr;
	if (!TestTrue(TEXT("Widget instance created"), WidgetInstance != nullptr))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	WidgetInstance->TakeWidget();

	UWidgetSwitcher* Switcher = Cast<UWidgetSwitcher>(
		WidgetInstance->WidgetTree ? WidgetInstance->WidgetTree->FindWidget(FName(*NavigationData.SwitcherWidgetName)) : nullptr);
	UOverlay* Host = WidgetInstance->WidgetTree
		? Cast<UOverlay>(WidgetInstance->WidgetTree->FindWidget(FName(*NavigationData.OverlayHostWidgetName)))
		: nullptr;
	UButton* NavButton = WidgetInstance->WidgetTree ? Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("nav_button"))) : nullptr;
	UButton* OverlayButton = WidgetInstance->WidgetTree ? Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("overlay_button"))) : nullptr;
	UButton* BackButton = WidgetInstance->WidgetTree ? Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("back_button"))) : nullptr;
	UWidget* HostedPage = Host && Host->GetChildrenCount() > 0 ? Host->GetChildAt(0) : nullptr;
	if (!TestTrue(TEXT("Switcher exists"), Switcher != nullptr)
		|| !TestTrue(TEXT("Overlay host exists"), Host != nullptr)
		|| !TestTrue(TEXT("Hosted overlay page exists"), HostedPage != nullptr)
		|| !TestTrue(TEXT("Navigate button exists"), NavButton != nullptr)
		|| !TestTrue(TEXT("Overlay button exists"), OverlayButton != nullptr)
		|| !TestTrue(TEXT("Back button exists"), BackButton != nullptr))
	{
		WidgetInstance->RemoveFromParent();
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	FArrayProperty* HistoryProperty = FindFProperty<FArrayProperty>(
		GeneratedClass, FName(Design2UMGPrototypeGraphWiring::NavHistoryVariableName));
	const auto HistoryNum = [HistoryProperty, WidgetInstance]()
	{
		return HistoryProperty ? FScriptArrayHelper(HistoryProperty, HistoryProperty->ContainerPtrToValuePtr<void>(WidgetInstance)).Num() : -1;
	};

	// A -> B.
	NavButton->OnClicked.Broadcast();
	TestEqual(TEXT("Navigate switches to page B"), Switcher->GetActiveWidgetIndex(), 1);
	TestEqual(TEXT("History holds the left page"), HistoryNum(), 1);

	// Open the overlay on top of B.
	OverlayButton->OnClicked.Broadcast();
	TestEqual(TEXT("Overlay open shows the host"), Host->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
	TestEqual(TEXT("Overlay open shows the hosted page"), HostedPage->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
	TestEqual(TEXT("Overlay keeps page B active"), Switcher->GetActiveWidgetIndex(), 1);

	// Back closes the overlay while preserving active page B and its navigation history.
	BackButton->OnClicked.Broadcast();
	TestEqual(TEXT("Back closes the overlay host"), Host->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Back collapses the hosted page"), HostedPage->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Back with an open overlay stays on page B"), Switcher->GetActiveWidgetIndex(), 1);
	TestEqual(TEXT("Back with an open overlay keeps the history"), HistoryNum(), 1);

	// Back again (overlay closed): pops the history and returns to A.
	BackButton->OnClicked.Broadcast();
	TestEqual(TEXT("Second back returns to page A"), Switcher->GetActiveWidgetIndex(), 0);
	TestEqual(TEXT("Second back popped the history"), HistoryNum(), 0);

	WidgetInstance->RemoveFromParent();
	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

// MOVE_OUT runtime smoke with the REAL generated animation playing. The test broadcasts the global
// Slate PreTick/PostTick delegates, which drives UUMGSequenceTickManager::TickWidgetAnimations (the
// production per-frame tick) and the end-of-frame MovieScene flush; the latent manager is pumped in
// between exactly like NativeTick. The click frame advances only the latent Delay, so with the 59 Hz
// frame delta the delay resumes while the outgoing animation is still playing - the ordering in
// which an unprotected chain lets the final off-screen key re-overwrite the reset during the
// following frames. Removing the StopAnimation node makes this test fail on the final translation.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeMoveOutTransitionRuntimeSmokeTest,
	"Design2UMG.WidgetBlueprint.PrototypeGraphWiring.MoveOutRuntimeSmoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeMoveOutTransitionRuntimeSmokeTest::RunTest(const FString& Parameters)
{
	FDesignDocument Document;
	FUMGWidgetData Data;
	if (!TestTrue(TEXT("Convert MOVE_OUT navigate prototype fixture"), ConvertPrototype(MoveOutNavigatePrototypeJson(), Document, Data)))
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ProtoNavWiringMoveOut");
	DeleteGeneratedAssetsAtPath(OutputPath);

	FUMGPrototypeNavigationData NavigationData;
	UWidgetBlueprint* WBP = FUMGGenerator::BuildWidgetBlueprint(
		Data, OutputPath, nullptr, nullptr, &NavigationData);
	if (!TestTrue(TEXT("Build MOVE_OUT widget blueprint"), WBP != nullptr)
		|| !TestTrue(TEXT("Save MOVE_OUT widget blueprint"),
			FUMGGenerator::SaveWidgetBlueprint(WBP, nullptr, &NavigationData).IsSuccess()))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	// MOVE_OUT generates exactly the outgoing slide; no incoming animation exists.
	TestEqual(TEXT("MOVE_OUT generates only the outgoing animation"), WBP->Animations.Num(), 1);
	TestNotNull(TEXT("Outgoing animation generated"), FindGeneratedAnimation(WBP, TEXT("__d2u_nav_anim_000_out")));
	TestNull(TEXT("No incoming animation for MOVE_OUT"), FindGeneratedAnimation(WBP, TEXT("__d2u_nav_anim_000_in")));

	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(WBP->GeneratedClass);
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	UUserWidget* WidgetInstance = GeneratedClass && World ? CreateWidget<UUserWidget>(World, GeneratedClass) : nullptr;
	if (!TestTrue(TEXT("Widget instance created"), WidgetInstance != nullptr))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	// Keep the Slate widget alive: a discarded TakeWidget() result lets the weak MyGCWidget expire,
	// which makes UUserWidget::IsVisible() false and stops the sequence tick manager from ticking.
	const TSharedRef<SWidget> WidgetSlate = WidgetInstance->TakeWidget();
	(void)WidgetSlate; // Holds the Slate subtree alive for the whole test (a discarded result lets MyGCWidget expire).

	UWidgetSwitcher* Switcher = Cast<UWidgetSwitcher>(
		WidgetInstance->WidgetTree ? WidgetInstance->WidgetTree->FindWidget(FName(*NavigationData.SwitcherWidgetName)) : nullptr);
	UButton* TriggerButton = WidgetInstance->WidgetTree ? Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("cta_button"))) : nullptr;
	if (!TestTrue(TEXT("Switcher exists"), Switcher != nullptr)
		|| !TestTrue(TEXT("Trigger button exists"), TriggerButton != nullptr))
	{
		WidgetInstance->RemoveFromParent();
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	TestTrue(TEXT("Switcher translation starts at the origin"),
		Switcher->GetRenderTransform().Translation.Equals(FVector2D::ZeroVector));

	TriggerButton->OnClicked.Broadcast();
	TestEqual(TEXT("Page switch waits for the outgoing delay"), Switcher->GetActiveWidgetIndex(), 0);
	const FObjectProperty* OutgoingAnimationProperty = FindFProperty<FObjectProperty>(
		GeneratedClass, FName(TEXT("__d2u_nav_anim_000_out")));
	UWidgetAnimation* OutgoingAnimation = OutgoingAnimationProperty
		? Cast<UWidgetAnimation>(OutgoingAnimationProperty->GetObjectPropertyValue_InContainer(WidgetInstance))
		: nullptr;
	TestTrue(TEXT("Outgoing slide animation is playing"), OutgoingAnimation && WidgetInstance->IsAnimationPlaying(OutgoingAnimation));
	if (!OutgoingAnimation || !World)
	{
		WidgetInstance->RemoveFromParent();
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	// Drive the real generated animation through the engine's own frame path: Slate PreTick ticks
	// the active UMG animation and force-flushes queued movie-scene evaluation, then the widget's
	// latent Delay pump runs, then the post-tick flush drains anything the chain queued. The public
	// test surface cannot call UUserWidget::TickActionsAndAnimation directly, so the test drives the
	// registered UUMGSequenceTickManager delegates exactly as Slate does in a running game.
	UUMGSequenceTickManager* AnimationTickManager = UUMGSequenceTickManager::Get(WidgetInstance);
	const bool bCanDriveRealAnimationTicks = FSlateApplication::IsInitialized() && AnimationTickManager != nullptr;
	if (!TestTrue(TEXT("Slate/UMG sequence tick path is available"), bCanDriveRealAnimationTicks))
	{
		WidgetInstance->RemoveFromParent();
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	// 59 Hz is a deterministic, plausible frame delta: it keeps the latent Delay one frame ahead
	// of the animation's Slate PreTick, so the delay resumes while the outgoing animation is still
	// playing. At exactly 60 Hz the float alignment can make both finish on the same frame, which
	// would let a broken chain pass because the final key lands before the reset.
	constexpr float FrameSeconds = 1.0f / 59.0f;

	auto RunFrame = [&](bool bAdvanceAnimation)
	{
		if (bAdvanceAnimation)
		{
			// NativeTick marks the widget ticking each frame; TickWidgetAnimations skips widgets
			// that did not announce themselves in the previous frame.
			AnimationTickManager->OnWidgetTicked(WidgetInstance);
			FSlateApplication::Get().OnPreTick().Broadcast(FrameSeconds);
		}
		// A real world ticks BeginFrame once per frame; FLatentActionManager processes each object
		// at most once per frame, so the harness must mirror that or only frame 0 advances.
		World->GetLatentActionManager().BeginFrame();
		World->GetLatentActionManager().ProcessLatentActions(WidgetInstance, FrameSeconds);
		FSlateApplication::Get().OnPostTick().Broadcast(FrameSeconds);
	};

	// The click frame (FrameIndex 0) advances the latent delay only: the animation has not had its
	// first Slate PreTick yet, which is exactly the ordering that can leave the outgoing
	// animation's final evaluation pending when the delay resumes.
	bool bChainCompleted = false;
	for (int32 FrameIndex = 0; FrameIndex < 60 && !bChainCompleted; ++FrameIndex)
	{
		RunFrame(FrameIndex > 0);
		bChainCompleted = Switcher->GetActiveWidgetIndex() == 1;
	}
	if (!TestTrue(TEXT("MOVE_OUT chain completes within the frame loop"), bChainCompleted))
	{
		WidgetInstance->RemoveFromParent();
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	// Continue several full frames after the chain completed, so the animation must play through
	// its natural end and any late evaluation cannot overwrite the reset translation.
	for (int32 ExtraFrame = 0; ExtraFrame < 6; ++ExtraFrame)
	{
		RunFrame(true);
	}

	TestEqual(TEXT("MOVE_OUT completes the page switch"), Switcher->GetActiveWidgetIndex(), 1);
	TestTrue(TEXT("MOVE_OUT keeps the switcher translation at the origin after residual animation frames"),
		Switcher->GetRenderTransform().Translation.Equals(FVector2D::ZeroVector));
	TestFalse(TEXT("Outgoing animation is no longer playing at the end"),
		WidgetInstance->IsAnimationPlaying(OutgoingAnimation));

	WidgetInstance->RemoveFromParent();
	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

// Incoming-only transitions switch pages immediately, then play the incoming animation.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeMoveInTransitionRuntimeSmokeTest,
	"Design2UMG.WidgetBlueprint.PrototypeGraphWiring.MoveInRuntimeSmoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeMoveInTransitionRuntimeSmokeTest::RunTest(const FString& Parameters)
{
	FDesignDocument Document;
	FUMGWidgetData Data;
	if (!TestTrue(TEXT("Convert MOVE_IN navigate prototype fixture"), ConvertPrototype(MoveInNavigatePrototypeJson(), Document, Data)))
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ProtoNavWiringMoveIn");
	DeleteGeneratedAssetsAtPath(OutputPath);

	FUMGPrototypeNavigationData NavigationData;
	UWidgetBlueprint* WBP = FUMGGenerator::BuildWidgetBlueprint(
		Data, OutputPath, nullptr, nullptr, &NavigationData);
	if (!TestTrue(TEXT("Build MOVE_IN widget blueprint"), WBP != nullptr)
		|| !TestTrue(TEXT("Save MOVE_IN widget blueprint"),
			FUMGGenerator::SaveWidgetBlueprint(WBP, nullptr, &NavigationData).IsSuccess()))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	// MOVE_IN generates exactly the incoming slide; no outgoing animation exists.
	TestEqual(TEXT("MOVE_IN generates only the incoming animation"), WBP->Animations.Num(), 1);
	TestNull(TEXT("No outgoing animation for MOVE_IN"), FindGeneratedAnimation(WBP, TEXT("__d2u_nav_anim_000_out")));
	TestNotNull(TEXT("Incoming animation generated"), FindGeneratedAnimation(WBP, TEXT("__d2u_nav_anim_000_in")));

	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(WBP->GeneratedClass);
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	UUserWidget* WidgetInstance = GeneratedClass && World ? CreateWidget<UUserWidget>(World, GeneratedClass) : nullptr;
	if (!TestTrue(TEXT("Widget instance created"), WidgetInstance != nullptr))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	WidgetInstance->TakeWidget();

	UWidgetSwitcher* Switcher = Cast<UWidgetSwitcher>(
		WidgetInstance->WidgetTree ? WidgetInstance->WidgetTree->FindWidget(FName(*NavigationData.SwitcherWidgetName)) : nullptr);
	UButton* TriggerButton = WidgetInstance->WidgetTree ? Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("cta_button"))) : nullptr;
	if (!TestTrue(TEXT("Switcher exists"), Switcher != nullptr)
		|| !TestTrue(TEXT("Trigger button exists"), TriggerButton != nullptr))
	{
		WidgetInstance->RemoveFromParent();
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	// Switch-first chain: the page changes immediately on the click (no pre-switch dead delay).
	TriggerButton->OnClicked.Broadcast();
	TestEqual(TEXT("MOVE_IN switches immediately (no pre-switch delay)"), Switcher->GetActiveWidgetIndex(), 1);
	const FObjectProperty* IncomingAnimationProperty = FindFProperty<FObjectProperty>(
		GeneratedClass, FName(TEXT("__d2u_nav_anim_000_in")));
	UWidgetAnimation* IncomingAnimation = IncomingAnimationProperty
		? Cast<UWidgetAnimation>(IncomingAnimationProperty->GetObjectPropertyValue_InContainer(WidgetInstance))
		: nullptr;
	TestTrue(TEXT("Incoming slide animation is playing after the switch"),
		IncomingAnimation && WidgetInstance->IsAnimationPlaying(IncomingAnimation));

	WidgetInstance->RemoveFromParent();
	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

#endif
