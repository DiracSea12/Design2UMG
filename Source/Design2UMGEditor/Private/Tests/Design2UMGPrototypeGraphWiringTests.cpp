// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGWidgetPropertyAccess.h"
#include "Misc/AutomationTest.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/ScopeExit.h"
#include "Misc/CommandLine.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Automation/Design2UMGAutomationRender.h"
#include "Debugging/SlateDebugging.h"
#include "Rendering/DrawElements.h"
#include "Components/RetainerBox.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "TextureResource.h"
#include "Misc/SecureHash.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "DynamicRHI.h"
#include "UObject/UObjectIterator.h"
#include "Misc/Parse.h"
#include "Design2UMGConverter.h"
#include "DesignImportPipeline.h"
#include "Design2UMGGenerationSettingsSnapshot.h"
#include "Design2UMGPrototypeGraphWiring.h"
#include "Design2UMGPrototypeGraphWiringInternal.h"
#include "Design2UMGAnimationPresets.h"
#include "NativeTextAnimation.h"
#include "DesignWidgetBlueprintUtils.h"
#include "Design2UMGTestUtils.h"
#include "Tests/ComponentPropertyFixtures.h"
#include "Tests/Design2UMGTestAssetCleanup.h"
#include "FigmaParser.h"
#include "FigmaTestDocBuilder.h"
#include "UMGGenerator.h"
#include "UMGPrototypeAnimationBuilder.h"
#include "UMGSchemaSourceExtensions.h"
#include "UMGWidgetTreeBuilder.h"
#include "UObject/StrongObjectPtr.h"
#include "RenderingThread.h"
#include "Slate/WidgetRenderer.h"

#include "Animation/MovieScene2DTransformSection.h"
#include "Animation/MovieScene2DTransformTrack.h"
#include "Animation/UMGSequenceTickManager.h"
#include "Animation/WidgetAnimation.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/SlateWrapperTypes.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/ComponentDelegateBinding.h"
#include "Engine/DynamicBlueprintBinding.h"
#include "Engine/LatentActionManager.h"
#include "Engine/MemberReference.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Input/Events.h"
#include "Layout/WidgetPath.h"
#include "Widgets/SWindow.h"
#include "Widgets/Layout/SBox.h"
#include "InputCoreTypes.h"
#include "K2Node_CallArrayFunction.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "K2Node_EnumEquality.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_SwitchInteger.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet/KismetArrayLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "MovieScene.h"
#include "MovieSceneBinding.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieSceneColorTrack.h"
#include "TimerManager.h"
#include "CoreGlobals.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace Design2UMGTestUtils;
using namespace Design2UMGCoreTests;
using Design2UMGEditorTests::DeleteGeneratedAssetsAtPath;

namespace
{
	/** Trigger button resolved as graph wiring resolves it: by base name, tolerating the tree builder's dedup suffix. */
	/** Generated button for a source trigger id, resolved by its recorded runtime name as graph wiring resolves it. */
	UButton* FindTriggerButton(const UWidgetTree* Tree, const FUMGPrototypeNavigationData& Navigation, const FString& TriggerId, const TCHAR* Suffix = TEXT(""))
	{
		const FUMGPrototypeNavigationBinding* Binding = Navigation.ClickBindings.FindByPredicate(
			[&](const FUMGPrototypeNavigationBinding& Item) { return Item.TriggerWidgetId == TriggerId; });
		return Binding && Tree ? FUMGWidgetTreeBuilder::FindUniqueTreeWidget<UButton>(Tree,
			FUMGWidgetTreeBuilder::SanitizeWidgetName(Binding->TriggerWidgetName + Suffix, TEXT("Button")).ToString()) : nullptr;
	}

	FName TriggerButtonName(const UWidgetTree* Tree, const FString& BaseName)
	{
		const UButton* Button = FUMGWidgetTreeBuilder::FindUniqueTreeWidget<UButton>(Tree, BaseName);
		return Button ? Button->GetFName() : NAME_None;
	}

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

	const FString OverlayBackPrototypeJson(bool bIncludeClose = false)
	{
		const FString OverlayButtonJson = MakeLeafNode(TEXT("overlay_button"), TEXT("Overlay"), TEXT("RECTANGLE"), 40, 60, 120, 44, FString::Printf(
			TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"2:1\",\"navigation\":\"OPEN_OVERLAY\"}]}]"),
			*MakeSolidFillJson(0.1, 0.4, 0.9)));
		FString OverlayChildren = MakeLeafNode(TEXT("back_button"), TEXT("Back"), TEXT("RECTANGLE"), 40, 60, 120, 44, FString::Printf(
			TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"BACK\"}]}]"),
			*MakeSolidFillJson(0.8, 0.3, 0.2)));
		if (bIncludeClose)
			OverlayChildren += TEXT(",") + MakeLeafNode(TEXT("close_button"), TEXT("Close"), TEXT("RECTANGLE"), 180, 60, 120, 44, FString::Printf(
				TEXT(",\"fills\":[%s],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"CLOSE\"}]}]"),
				*MakeSolidFillJson(0.4, 0.3, 0.2)));
		if (bIncludeClose)
			OverlayChildren += TEXT(",") + MakeLeafNode(TEXT("next_overlay"), TEXT("Open next"), TEXT("RECTANGLE"), 40, 120, 120, 44,
				TEXT(",\"fills\":[{\"type\":\"SOLID\",\"color\":{\"r\":0.2,\"g\":0.4,\"b\":0.8}}],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"3:1\",\"navigation\":\"OPEN_OVERLAY\"}]}]"));
		const FString PageTwoJson = MakeContainerNode(TEXT("2:1"), TEXT("PageTwo"), TEXT("FRAME"), 0, 0, 360, 240, OverlayChildren);
		FString Pages = MakeContainerNode(TEXT("1:1"), TEXT("PageOne"), TEXT("FRAME"), 0, 0, 360, 240, OverlayButtonJson) + TEXT(",") + PageTwoJson;
		if (bIncludeClose)
		{
			const FString TopClose = MakeLeafNode(TEXT("top_close_button"), TEXT("Close top"), TEXT("RECTANGLE"), 40, 60, 120, 44,
				TEXT(",\"fills\":[{\"type\":\"SOLID\",\"color\":{\"r\":0.8,\"g\":0.4,\"b\":0.2}}],\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"CLOSE\"}]}]"));
			Pages += TEXT(",") + MakeContainerNode(TEXT("3:1"), TEXT("PageThree"), TEXT("FRAME"), 0, 0, 360, 240, TopClose);
		}
		return MakeFigmaDocJson(Pages, TEXT(",\"prototypeStartNodeID\":\"1:1\""));
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
	// buttons on page one. The page must stay a page host child (the page host owns the page flow),
	// so BuildRoot synthesizes a mirror copy of it under the overlay host: the overlay binding
	// toggles the mirror (layered display), never the page host or the history. The mirror's own
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
	// second page. The initial page must stay a page host child (InitialPageIndex keeps pointing at
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
				"{\"trigger\":{\"type\":\"ON_KEY_DOWN\",\"device\":\"KEYBOARD\",\"keyCodes\":[\"n\"]},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"2:1\",\"navigation\":\"NAVIGATE\"}]},"
				"{\"trigger\":{\"type\":\"ON_KEY_UP\",\"device\":\"KEYBOARD\",\"keyCodes\":[\"m\"]},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"1:1\",\"navigation\":\"NAVIGATE\"}]}]"),
			*MakeSolidFillJson(0.1, 0.4, 0.9));
		const FString KeyNodeJson = MakeLeafNode(TEXT("key_pad"), TEXT("KeyPad"), TEXT("RECTANGLE"), 40, 60, 120, 44, KeyTriggerJson);
		const FString PageTwoKeyTriggerJson = FString::Printf(
			TEXT(",\"fills\":[%s],\"interactions\":["
				"{\"trigger\":{\"type\":\"ON_KEY_DOWN\",\"device\":\"KEYBOARD\",\"keyCodes\":[\"n\"]},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"1:1\",\"navigation\":\"NAVIGATE\"}]}]"),
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
		OutDocument.MutableSourceDocument().PrototypeDurationUnit = TEXT("MILLISECONDS");
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

	UK2Node_CallFunction* FindExecChainCall(const UEdGraphNode* Node)
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

	// A bound event only enters its generated trigger function; the actions start at that function's entry.
	const UEdGraphNode* TriggerBody(const UWidgetBlueprint* WBP, const UEdGraphNode* Event)
	{
		const UK2Node_CallFunction* Call = FindExecChainCall(Event);
		if (!Call || !Call->FunctionReference.IsSelfContext()) return nullptr;
		for (const UEdGraph* Graph : WBP->FunctionGraphs)
			if (Graph->GetFName() == Call->FunctionReference.GetMemberName())
				for (const UEdGraphNode* Node : Graph->Nodes)
					if (Node->IsA<UK2Node_FunctionEntry>()) return Node;
		return nullptr;
	}

	TArray<const UEdGraphNode*> GeneratedGraphNodes(const UWidgetBlueprint* WBP)
	{
		TArray<const UEdGraphNode*> Nodes;
		for (const TArray<TObjectPtr<UEdGraph>>* Graphs : {&WBP->UbergraphPages, &WBP->FunctionGraphs})
			for (const UEdGraph* Graph : *Graphs)
				Nodes.Append(Graph->Nodes);
		return Nodes;
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
#if UE_VERSION_OLDER_THAN(5, 2, 0)
		Tracks.Append(MovieScene->GetMasterTracks());
#else
		Tracks.Append(MovieScene->GetTracks());
#endif
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

	const FMovieSceneFloatChannel* FindOpacityChannelForWidget(const UWidgetAnimation* Animation, FName WidgetName)
	{
		if (!Animation || !Animation->MovieScene) return nullptr;
		const UMovieScene* Scene = Animation->MovieScene;
		for (const auto& WidgetBinding : Animation->AnimationBindings)
		{
			if (WidgetBinding.WidgetName != WidgetName) continue;
			for (const FMovieSceneBinding& Binding : Scene->GetBindings())
			{
				if (Binding.GetObjectGuid() != WidgetBinding.AnimationGuid) continue;
				for (const UMovieSceneTrack* Track : Binding.GetTracks())
				{
					if (const auto* FloatTrack = Cast<UMovieSceneFloatTrack>(Track))
						for (const UMovieSceneSection* Section : FloatTrack->GetAllSections())
							if (const auto* FloatSection = Cast<UMovieSceneFloatSection>(Section)) return &FloatSection->GetChannel();
				}
			}
		}
		return nullptr;
	}

	bool HasColorTrackForWidget(const UWidgetAnimation* Animation, FName WidgetName)
	{
		if (!Animation || !Animation->MovieScene) return false;
		const UMovieScene* Scene = Animation->MovieScene;
		for (const auto& WidgetBinding : Animation->AnimationBindings)
		{
			if (WidgetBinding.WidgetName != WidgetName) continue;
			for (const FMovieSceneBinding& Binding : Scene->GetBindings())
				if (Binding.GetObjectGuid() == WidgetBinding.AnimationGuid)
					for (const UMovieSceneTrack* Track : Binding.GetTracks())
						if (Cast<UMovieSceneColorTrack>(Track)) return true;
		}
		return false;
	}

	const UMovieScene2DTransformSection* FindTransformForWidget(const UWidgetAnimation* Animation, FName WidgetName)
	{
		if (!Animation || !Animation->MovieScene) return nullptr;
		const UMovieScene* Scene = Animation->MovieScene;
		for (const auto& WidgetBinding : Animation->AnimationBindings)
		{
			if (WidgetBinding.WidgetName != WidgetName) continue;
			for (const FMovieSceneBinding& Binding : Scene->GetBindings())
			{
				if (Binding.GetObjectGuid() != WidgetBinding.AnimationGuid) continue;
				for (const UMovieSceneTrack* Track : Binding.GetTracks())
					if (const auto* TransformTrack = Cast<UMovieScene2DTransformTrack>(Track))
						for (const UMovieSceneSection* Section : TransformTrack->GetAllSections())
							if (const auto* TransformSection = Cast<UMovieScene2DTransformSection>(Section)) return TransformSection;
			}
		}
		return nullptr;
	}


	int32 CurrentPageIndex(const UUserWidget* Instance)
	{
		const auto* Property = Instance ? FindFProperty<FIntProperty>(Instance->GetClass(), TEXT("__d2u_current_page")) : nullptr;
		return Property ? Property->GetPropertyValue_InContainer(Instance) : INDEX_NONE;
	}

	UWidget* CurrentPageWidget(const UUserWidget* Instance, UCanvasPanel* PageHost)
	{
		const int32 Index = CurrentPageIndex(Instance);
		return PageHost && Index >= 0 && Index < PageHost->GetChildrenCount() ? PageHost->GetChildAt(Index) : nullptr;
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

	UWidgetAnimation* Animation = FindGeneratedAnimation(WBP, TEXT("__d2u_nav_anim_000_pages"));
	if (!TestNotNull(TEXT("One native page transition is generated"), Animation)
		|| !TestEqual(TEXT("One transition animation is retained"), WBP->Animations.Num(), 1))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	const auto* PageHost = Cast<UCanvasPanel>(WBP->WidgetTree ? WBP->WidgetTree->RootWidget : nullptr);
	if (!TestNotNull(TEXT("The page host is a native canvas"), PageHost)
		|| !TestEqual(TEXT("Both real pages are retained"), PageHost->GetChildrenCount(), 2)
		|| !TestEqual(TEXT("The shared animation binds both pages"), Animation->AnimationBindings.Num(), 2))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	const FMovieSceneFloatChannel* SourceOpacity = FindOpacityChannelForWidget(Animation, PageHost->GetChildAt(0)->GetFName());
	const FMovieSceneFloatChannel* TargetOpacity = FindOpacityChannelForWidget(Animation, PageHost->GetChildAt(1)->GetFName());
	if (!TestNotNull(TEXT("Source page opacity track exists"), SourceOpacity)
		|| !TestNotNull(TEXT("Target page opacity track exists"), TargetOpacity))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestTrue(TEXT("Both fade tracks retain their native curve endpoints"), SourceOpacity->GetNumKeys() >= 2 && TargetOpacity->GetNumKeys() >= 2
		&& FMath::IsNearlyEqual(SourceOpacity->GetValues()[0].Value, 1.0f)
		&& FMath::IsNearlyEqual(SourceOpacity->GetValues()[SourceOpacity->GetNumKeys() - 1].Value, 0.0f)
		&& FMath::IsNearlyEqual(TargetOpacity->GetValues()[0].Value, 0.0f)
		&& FMath::IsNearlyEqual(TargetOpacity->GetValues()[TargetOpacity->GetNumKeys() - 1].Value, 1.0f));
	const TArray<UK2Node_ComponentBoundEvent*> Events = CollectBoundEvents(WBP);
	if (!TestEqual(TEXT("Exactly one click event is authored"), Events.Num(), 1))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestEqual(TEXT("Click remains bound to the source button"), Events[0]->ComponentPropertyName.ToString(), FString(TEXT("CTA")));
	TestEqual(TEXT("The original click delegate is used"), Events[0]->DelegatePropertyName.ToString(), FString(TEXT("OnClicked")));
	bool bPlay = false, bSetVisibility = false, bPushHistory = false;
	for (const UEdGraphNode* Node : GeneratedGraphNodes(WBP))
		if (const auto* Call = Cast<UK2Node_CallFunction>(Node))
		{
			const FName Name = Call->FunctionReference.GetMemberName();
			bPlay |= Name == TEXT("PlayAnimation");
			bSetVisibility |= Name == TEXT("SetVisibility");
			bPushHistory |= Name == TEXT("Array_Add");
		}
	TestTrue(TEXT("Generated event plays the shared animation"), bPlay);
	TestTrue(TEXT("Generated navigation changes page visibility"), bSetVisibility);
	TestTrue(TEXT("Generated navigation records page history"), bPushHistory);
	const auto* Generated = Cast<UWidgetBlueprintGeneratedClass>(WBP->GeneratedClass);
	const auto* History = Generated ? FindFProperty<FArrayProperty>(Generated, FName(Design2UMGPrototypeGraphWiring::NavHistoryVariableName)) : nullptr;
	TestTrue(TEXT("Compiled class retains integer page history"), History && CastField<FIntProperty>(History->Inner));

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
	TestEqual(TEXT("Bound event targets the link button widget"), EventNode->ComponentPropertyName.ToString(), TEXT("Link"));
	TestEqual(TEXT("Bound event uses the click delegate"), EventNode->DelegatePropertyName.ToString(), TEXT("OnClicked"));

	UK2Node_CallFunction* CallNode = FindExecChainCall(TriggerBody(WBP, EventNode));
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
	bool bFoundPageVisibilityCall = false;
	for (const UEdGraphNode* Node : GeneratedGraphNodes(WBP))
	{
		bFoundVariableGet |= Cast<UK2Node_VariableGet>(Node) != nullptr;
		if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
		{
			bFoundPageVisibilityCall |= Call->FunctionReference.GetMemberName() == TEXT("SetVisibility");
		}
	}
	TestFalse(TEXT("Open-link wiring authors no variable get"), bFoundVariableGet);
	TestFalse(TEXT("Open-link wiring authors no page visibility call"), bFoundPageVisibilityCall);

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
	const int32 MarkerCountAfterSave = CountMarkerNodes();
	TestTrue(TEXT("Initial wiring authors generated nodes"), MarkerCountAfterSave > 0);
	TestEqual(TEXT("Initial wiring authors one shared transition animation"), WBP->Animations.Num(), 1);
	if (!TestEqual(TEXT("Initial wiring authors one bound event"), CollectBoundEvents(WBP).Num(), 1))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	// Regenerate: re-run the animation builder (name-prefix purge inside) and the wiring over the
	// same blueprint; neither animations, nodes nor the generated member variable may accumulate.
	UMGPrototypeAnimationBuilder::GenerateNavigationAnimations(
		WBP, Data.RootWidget, NavigationData);
	TestEqual(TEXT("Regenerated shared animation stays unique"), WBP->Animations.Num(), 1);

	TArray<FString> WiringWarnings;
	Design2UMGPrototypeGraphWiring::WireSimplePrototypeBindings(WBP, NavigationData, WiringWarnings);
	TestEqual(TEXT("Fully eligible fixture re-wires without skips"), WiringWarnings.Num(), 0);
	TestEqual(TEXT("Re-wiring keeps the node count stable"), Ubergraph->Nodes.Num(), NodeCountAfterSave);
	TestEqual(TEXT("Re-wiring keeps the generated node count"), CountMarkerNodes(), MarkerCountAfterSave);
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
	TestEqual(TEXT("Re-wired event still targets the button"), Events[0]->ComponentPropertyName.ToString(), TEXT("CTA"));

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
			|| NodeClass == UK2Node_VariableSet::StaticClass()
			|| NodeClass == UK2Node_CustomEvent::StaticClass()
			|| NodeClass == UK2Node_EnumEquality::StaticClass()
			|| NodeClass == UK2Node_ExecutionSequence::StaticClass()
			|| NodeClass == UK2Node_SwitchInteger::StaticClass()
			|| NodeClass == UK2Node_IfThenElse::StaticClass()
			|| NodeClass == UK2Node_FunctionEntry::StaticClass()
			|| NodeClass == UK2Node_FunctionResult::StaticClass()
			|| NodeClass == UK2Node_Event::StaticClass());
	}

	// The generated history variable remains unique and resolves to an engine array of int32.
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

	UWidgetAnimation* Animation = FindGeneratedAnimation(WBP, TEXT("__d2u_nav_anim_000_pages"));
	const auto* PageHost = Cast<UCanvasPanel>(WBP->WidgetTree ? WBP->WidgetTree->RootWidget : nullptr);
	if (!TestNotNull(TEXT("One native page animation exists"), Animation)
		|| !TestNotNull(TEXT("Native page host exists"), PageHost)
		|| !TestEqual(TEXT("The animation binds both real pages"), Animation->AnimationBindings.Num(), 2)
		|| !TestEqual(TEXT("The host contains both pages"), PageHost->GetChildrenCount(), 2))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestNull(TEXT("PUSH has no opacity track"), FindOpacityChannel(Animation));
	const auto* SourceTransform = FindTransformForWidget(Animation, PageHost->GetChildAt(0)->GetFName());
	const auto* TargetTransform = FindTransformForWidget(Animation, PageHost->GetChildAt(1)->GetFName());
	const auto* HostNode = FindIrNodeById(Data.RootWidget, NavigationData.PageHostWidgetId);
	if (!TestNotNull(TEXT("Source page transform exists"), SourceTransform)
		|| !TestNotNull(TEXT("Target page transform exists"), TargetTransform)
		|| !TestNotNull(TEXT("Page host bounds are available"), HostNode))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	const float Width = HostNode->Bounds.W;
	const auto SourceX = SourceTransform->Translation[0].GetValues();
	const auto TargetX = TargetTransform->Translation[0].GetValues();
	if (!TestTrue(TEXT("Both native curves have endpoints"), SourceX.Num() >= 2 && TargetX.Num() >= 2))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestTrue(TEXT("LEFT PUSH moves source left and brings target from the right"),
		FMath::IsNearlyEqual(SourceX[0].Value, 0.0f) && FMath::IsNearlyEqual(SourceX[SourceX.Num()-1].Value, -Width)
		&& FMath::IsNearlyEqual(TargetX[0].Value, Width) && FMath::IsNearlyEqual(TargetX[TargetX.Num()-1].Value, 0.0f));
	TestTrue(TEXT("PUSH preserves the vertical axis"),
		FMath::IsNearlyZero(SourceTransform->Translation[1].GetValues()[0].Value)
		&& FMath::IsNearlyZero(TargetTransform->Translation[1].GetValues()[0].Value));

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

	TestEqual(TEXT("No transition animations are generated"), WBP->Animations.Num(), 0);
	TestTrue(TEXT("No transition animation name is written into the IR"),
		NavigationData.ClickBindings[0].PrimaryStep().Transition.WidgetAnimationName.IsEmpty());
	const auto Events = CollectBoundEvents(WBP);
	if (!TestEqual(TEXT("Exactly one click event is authored"), Events.Num(), 1))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	bool bPushHistory = false, bSetVisibility = false, bPlayAnimation = false;
	for (const UEdGraphNode* Node : GeneratedGraphNodes(WBP))
		if (const auto* Call = Cast<UK2Node_CallFunction>(Node))
		{
			const FName Name = Call->FunctionReference.GetMemberName();
			bPushHistory |= Name == TEXT("Array_Add");
			bSetVisibility |= Name == TEXT("SetVisibility");
			bPlayAnimation |= Name == TEXT("PlayAnimation");
		}
	TestTrue(TEXT("Direct navigation records history"), bPushHistory);
	TestTrue(TEXT("Direct navigation changes native page visibility"), bSetVisibility);
	TestFalse(TEXT("Direct navigation does not play an animation"), bPlayAnimation);

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
	UCanvasPanel* OverlayHost = WBP->WidgetTree
		? Cast<UCanvasPanel>(WBP->WidgetTree->FindWidget(FName(*NavigationData.OverlayHostWidgetName)))
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
		if (Event && Event->ComponentPropertyName == FName(TEXT("Overlay")))
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
	bool bHostVisibleCall = false;
	for (const UEdGraphNode* Node : GeneratedGraphNodes(WBP))
	{
		const auto* Call = Cast<UK2Node_CallFunction>(Node);
		if (!Call || Call->FunctionReference.GetMemberName() != TEXT("SetVisibility")) continue;
		const UEdGraphPin* Visibility = Call->FindPin(TEXT("InVisibility"), EGPD_Input);
		const UEdGraphPin* Self = Call->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input);
		const auto* Get = Self && Self->LinkedTo.Num() == 1 ? Cast<UK2Node_VariableGet>(Self->LinkedTo[0]->GetOwningNode()) : nullptr;
		bHostVisibleCall |= Get && Get->GetVarName() == OverlayHost->GetFName() && Visibility
			&& Visibility->DefaultValue == StaticEnum<ESlateVisibility>()->GetNameStringByValue(static_cast<int64>(ESlateVisibility::SelfHitTestInvisible));
	}
	TestTrue(TEXT("Opening uses native SetVisibility on the real overlay host"), bHostVisibleCall);

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

// Back wiring reads the last history value, selects its page, and removes that entry.
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
	const UK2Node_ComponentBoundEvent* BackEvent = Events[0]->ComponentPropertyName == FName(TEXT("Back"))
		? Events[0]
		: (Events[1]->ComponentPropertyName == FName(TEXT("Back")) ? Events[1] : nullptr);
	if (!TestNotNull(TEXT("Back bound event found"), BackEvent))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	const UEdGraphNode* BackBody = TriggerBody(WBP, BackEvent);
	const UEdGraphPin* Then = BackBody ? BackBody->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output) : nullptr;
	const auto* Branch = Then && Then->LinkedTo.Num() == 1 ? Cast<UK2Node_IfThenElse>(Then->LinkedTo[0]->GetOwningNode()) : nullptr;
	if (!TestNotNull(TEXT("Back checks whether history is empty"), Branch))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	const UEdGraphPin* Condition = Branch->FindPin(UEdGraphSchema_K2::PN_Condition, EGPD_Input);
	const auto* Empty = Condition && Condition->LinkedTo.Num() == 1 ? Cast<UK2Node_CallFunction>(Condition->LinkedTo[0]->GetOwningNode()) : nullptr;
	TestTrue(TEXT("The branch tests Array_IsEmpty"), Empty && Empty->FunctionReference.GetMemberName() == TEXT("Array_IsEmpty"));
	const UEdGraphPin* Else = Branch->FindPin(UEdGraphSchema_K2::PN_Else, EGPD_Output);
	const auto* PageSwitch = Else && Else->LinkedTo.Num() == 1 ? Cast<UK2Node_SwitchInteger>(Else->LinkedTo[0]->GetOwningNode()) : nullptr;
	if (!TestNotNull(TEXT("Nonempty history selects the stored page"), PageSwitch))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	const UEdGraphPin* Selection = PageSwitch->GetSelectionPin();
	const auto* Get = Selection && Selection->LinkedTo.Num() == 1 ? Cast<UK2Node_CallFunction>(Selection->LinkedTo[0]->GetOwningNode()) : nullptr;
	TestTrue(TEXT("Page selection reads the last history value"), Get && Get->FunctionReference.GetMemberName() == TEXT("Array_Get"));
	const UEdGraphPin* Index = Get ? Get->FindPin(TEXT("Index"), EGPD_Input) : nullptr;
	const auto* Last = Index && Index->LinkedTo.Num() == 1 ? Cast<UK2Node_CallFunction>(Index->LinkedTo[0]->GetOwningNode()) : nullptr;
	TestTrue(TEXT("Array_Get uses the last history index"), Last && Last->FunctionReference.GetMemberName() == TEXT("Array_LastIndex"));
	bool bRemovesHistory = false, bChangesVisibility = false;
	for (const UEdGraphNode* Node : GeneratedGraphNodes(WBP))
		if (const auto* Call = Cast<UK2Node_CallFunction>(Node))
		{
			bRemovesHistory |= Call->FunctionReference.GetMemberName() == TEXT("Array_Remove");
			bChangesVisibility |= Call->FunctionReference.GetMemberName() == TEXT("SetVisibility");
		}
	TestTrue(TEXT("Back removes the history entry"), bRemovesHistory);
	TestTrue(TEXT("Back changes native page visibility"), bChangesVisibility);

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

	UCanvasPanel* PageHost = Cast<UCanvasPanel>(
		WidgetInstance->WidgetTree ? WidgetInstance->WidgetTree->FindWidget(FName(*NavigationData.PageHostWidgetName)) : nullptr);
	UButton* NavButton = WidgetInstance->WidgetTree ? Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("Next"))) : nullptr;
	UButton* BackButton = WidgetInstance->WidgetTree ? Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("Back"))) : nullptr;
	if (!TestTrue(TEXT("PageHost exists"), PageHost != nullptr)
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
	TestEqual(TEXT("Initial active page"), CurrentPageIndex(WidgetInstance), 0);

	NavButton->OnClicked.Broadcast();
	TestEqual(TEXT("Navigate switches to page 1 immediately (no transition)"), CurrentPageIndex(WidgetInstance), 1);
	TestEqual(TEXT("Navigate pushed the left page index"), HistoryNum(), 1);
	if (HistoryProperty)
	{
		FScriptArrayHelper Helper(HistoryProperty, HistoryProperty->ContainerPtrToValuePtr<void>(WidgetInstance));
		TestEqual(TEXT("Pushed value is the page that was left"), Helper.Num() == 1 ? *reinterpret_cast<int32*>(Helper.GetRawPtr(0)) : INDEX_NONE, 0);
	}

	BackButton->OnClicked.Broadcast();
	TestEqual(TEXT("Back returns to page 0"), CurrentPageIndex(WidgetInstance), 0);
	TestEqual(TEXT("Back popped the history entry"), HistoryNum(), 0);

	// Empty-history guard: a second BACK must not move the page.
	BackButton->OnClicked.Broadcast();
	TestEqual(TEXT("Second back is a no-op on an empty history"), CurrentPageIndex(WidgetInstance), 0);
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
				|| NodeClass == UK2Node_VariableSet::StaticClass()
				|| NodeClass == UK2Node_EnumEquality::StaticClass()
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
	TestTrue(TEXT("Generated class CDO is focusable"), WidgetCDO && Design2UMGWidgetPropertyAccess::IsUserWidgetFocusable(*WidgetCDO));

	// Runtime: drive both overrides through ProcessEvent with the FKeyEvent parms layout.
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	UUserWidget* WidgetInstance = GeneratedClass && World ? CreateWidget<UUserWidget>(World, GeneratedClass) : nullptr;
	if (!TestTrue(TEXT("Widget instance created"), WidgetInstance != nullptr))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	WidgetInstance->TakeWidget();
	UCanvasPanel* PageHost = Cast<UCanvasPanel>(
		WidgetInstance->WidgetTree ? WidgetInstance->WidgetTree->FindWidget(FName(*NavigationData.PageHostWidgetName)) : nullptr);
	if (!TestNotNull(TEXT("PageHost exists"), PageHost))
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
	TestEqual(TEXT("Page one N switches to page two"), CurrentPageIndex(WidgetInstance), 1);
	ProcessKeyEvent(EKeys::M, TEXT("OnKeyUp"));
	TestEqual(TEXT("Page one M is gated out while page two is active"), CurrentPageIndex(WidgetInstance), 1);
	ProcessKeyEvent(EKeys::N, TEXT("OnKeyDown"));
	TestEqual(TEXT("Page two N switches back to page one"), CurrentPageIndex(WidgetInstance), 0);

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
			return Warning.Contains(TEXT("OnKeyDown")) && Warning.Contains(TEXT("user connections"))
				&& Warning.Contains(TEXT("preserved"));
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

	UCanvasPanel* PageHost = Cast<UCanvasPanel>(
		WidgetInstance->WidgetTree ? WidgetInstance->WidgetTree->FindWidget(FName(*NavigationData.PageHostWidgetName)) : nullptr);
	UButton* FirstNavButton = FindTriggerButton(WidgetInstance->WidgetTree, NavigationData, TEXT("nav_first"));
	UButton* SecondNavButton = FindTriggerButton(WidgetInstance->WidgetTree, NavigationData, TEXT("nav_second"));
	UButton* BackButton = FindTriggerButton(WidgetInstance->WidgetTree, NavigationData, TEXT("back_button"));
	if (!TestTrue(TEXT("PageHost exists"), PageHost != nullptr)
		|| !TestTrue(TEXT("First navigate button exists"), FirstNavButton != nullptr)
		|| !TestTrue(TEXT("Second navigate button exists"), SecondNavButton != nullptr)
		|| !TestTrue(TEXT("Back button exists"), BackButton != nullptr))
	{
		WidgetInstance->RemoveFromParent();
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	TestEqual(TEXT("Initial active page"), CurrentPageIndex(WidgetInstance), 0);

	// 0 -> 2 -> 1 builds the history [0, 2]: page indices do not equal their own positions.
	FirstNavButton->OnClicked.Broadcast();
	TestEqual(TEXT("First navigate switches to page 2"), CurrentPageIndex(WidgetInstance), 2);
	SecondNavButton->OnClicked.Broadcast();
	TestEqual(TEXT("Second navigate switches to page 1"), CurrentPageIndex(WidgetInstance), 1);

	// History [0, 2] must return page value 2, not array index 1.
	BackButton->OnClicked.Broadcast();
	TestEqual(TEXT("Back returns to the last recorded page value (2), not the last index (1)"),
		CurrentPageIndex(WidgetInstance), 2);
	BackButton->OnClicked.Broadcast();
	TestEqual(TEXT("Second back unwinds to the first recorded page"), CurrentPageIndex(WidgetInstance), 0);

	// Empty history: Back must not move the page.
	BackButton->OnClicked.Broadcast();
	TestEqual(TEXT("Back on empty history is a no-op"), CurrentPageIndex(WidgetInstance), 0);

	WidgetInstance->RemoveFromParent();
	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaHoverOverlayBindingTest,
	"Design2UMG.WidgetBlueprint.PrototypeGraphWiring.HoverOverlayBinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaHoverOverlayBindingTest::RunTest(const FString&)
{
	const FString Source = OverlayBackPrototypeJson().Replace(
		TEXT("\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"2:1\",\"navigation\":\"OPEN_OVERLAY\"}"),
		TEXT("\"type\":\"ON_HOVER\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"2:1\",\"navigation\":\"OPEN_OVERLAY\"}"));
	FDesignDocument Document;
	FUMGWidgetData Data;
	if (!TestTrue(TEXT("Hover overlay source converts"), ConvertPrototype(Source, Document, Data))) return false;
	const FString Folder = TEXT("/Game/Design2UMG/_TestOutput/HoverOverlayBinding");
	DeleteGeneratedAssetsAtPath(Folder);
	FUMGPrototypeNavigationData Navigation;
	auto* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, Folder, nullptr, nullptr, &Navigation);
	if (!TestNotNull(TEXT("Hover overlay blueprint built"), Blueprint)
		|| !TestTrue(TEXT("Hover overlay blueprint saves"), FUMGGenerator::SaveWidgetBlueprint(Blueprint, nullptr, &Navigation).IsSuccess())) return false;
	TestNotNull(TEXT("Hover opens through the native button event"), FKismetEditorUtilities::FindBoundEventForComponent(Blueprint, TEXT("OnHovered"), TEXT("Overlay")));
	TestNotNull(TEXT("Hover ownership state exists"), FindFProperty<FIntProperty>(Blueprint->GeneratedClass, TEXT("__d2u_overlay_owner")));
	TestNotNull(TEXT("Hover monitor is compiled"), Blueprint->GeneratedClass->FindFunctionByName(TEXT("__d2u_overlay_000_monitor")));
	TestNull(TEXT("No mirror monitor exists without a resolved mirror trigger"),
		Blueprint->GeneratedClass->FindFunctionByName(TEXT("__d2u_overlay_000_mirror_monitor")));
	bool bClearsMissingMirror = false;
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	for (const UEdGraph* Graph : Graphs)
	{
		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			const auto* Call = Cast<UK2Node_CallFunction>(Node);
			if (!Call || Call->FunctionReference.GetMemberName() != TEXT("K2_ClearTimer")) continue;
			const auto* FunctionName = Call->FindPin(TEXT("FunctionName"));
			bClearsMissingMirror |= FunctionName && FunctionName->DefaultValue == TEXT("__d2u_overlay_000_mirror_monitor");
		}
	}
	TestFalse(TEXT("Cleanup never clears a nonexistent mirror monitor"), bClearsMissingMirror);
	DeleteGeneratedAssetsAtPath(Folder);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaTimedOverlayRuntimeTest,
	"Design2UMG.WidgetBlueprint.PrototypeGraphWiring.TimedOverlayRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaTimedOverlayRuntimeTest::RunTest(const FString&)
{
	const FString Source = OverlayBackPrototypeJson(true).Replace(
		TEXT("\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"2:1\",\"navigation\":\"OPEN_OVERLAY\"}]}"),
		TEXT("\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"2:1\",\"navigation\":\"OPEN_OVERLAY\"}]},{\"trigger\":{\"type\":\"AFTER_TIMEOUT\",\"timeout\":100},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"2:1\",\"navigation\":\"OPEN_OVERLAY\"}]}")).Replace(
		TEXT("\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"BACK\"}"),
		TEXT("\"type\":\"AFTER_TIMEOUT\",\"timeout\":1000},\"actions\":[{\"type\":\"CLOSE\"}"));
	FDesignDocument Document;
	FUMGWidgetData Data;
	if (!TestTrue(TEXT("Timed overlay source converts"), ConvertPrototype(Source, Document, Data))) return false;
	const FString Folder = TEXT("/Game/Design2UMG/_TestOutput/TimedOverlayRuntime");
	DeleteGeneratedAssetsAtPath(Folder);
	FUMGPrototypeNavigationData Navigation;
	auto* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, Folder, nullptr, nullptr, &Navigation);
	if (!TestNotNull(TEXT("Timed overlay blueprint built"), Blueprint)
		|| !TestTrue(TEXT("Timed overlay blueprint saves"), FUMGGenerator::SaveWidgetBlueprint(Blueprint, nullptr, &Navigation).IsSuccess())) return false;
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	auto* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass);
	UUserWidget* Instance = World && GeneratedClass ? CreateWidget<UUserWidget>(World, GeneratedClass) : nullptr;
	if (!TestNotNull(TEXT("Timed overlay instance created"), Instance)) return false;
	Instance->AddToRoot();
	TSharedRef<SWidget> Slate = Instance->TakeWidget();
	auto* Host = Cast<UCanvasPanel>(Instance->WidgetTree->FindWidget(FName(*Navigation.OverlayHostWidgetName)));
	if (!TestNotNull(TEXT("Timed overlay host found"), Host)) { Instance->RemoveFromRoot(); return false; }
	TestEqual(TEXT("Overlay initially collapsed"), Host->GetVisibility(), ESlateVisibility::Collapsed);
	auto Tick = [&](float Seconds)
	{
		++GFrameCounter;
		UUMGSequenceTickManager::Get(Instance)->OnWidgetTicked(Instance);
		FSlateApplication::Get().OnPreTick().Broadcast(Seconds);
		World->GetTimerManager().Tick(Seconds);
		FSlateApplication::Get().OnPostTick().Broadcast(Seconds);
	};
	Tick(0.01f);
	Tick(0.11f);
	TestEqual(TEXT("Timer opens overlay without pointer input"), Host->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
	auto* Open = Cast<UButton>(Instance->WidgetTree->FindWidget(TEXT("Overlay")));
	auto* Close = Cast<UButton>(Instance->WidgetTree->FindWidget(TEXT("Close")));
	if (!TestNotNull(TEXT("Overlay open control exists"), Open) || !TestNotNull(TEXT("Overlay close control exists"), Close))
	{
		Instance->RemoveFromRoot();
		return false;
	}
	const int32 TimedCloseIndex = Navigation.ClickBindings.IndexOfByPredicate([](const FUMGPrototypeNavigationBinding& Binding)
	{
		return Binding.UsesPageOpenTrigger() && Binding.Steps.Num() == 1
			&& Binding.Steps[0].Kind == EUMGPrototypeNavigationStepKind::CloseOverlay;
	});
	if (!TestTrue(TEXT("Source supplies a timed close binding"), TimedCloseIndex != INDEX_NONE))
	{
		Instance->RemoveFromRoot();
		return false;
	}
	const FString CloseTimer = FString::Printf(TEXT("__d2u_anim_%03d_play"), TimedCloseIndex);
	TestTrue(TEXT("Opening starts the overlay close timer"), UKismetSystemLibrary::K2_IsTimerActive(Instance, CloseTimer));
	Tick(0.4f);
	TestEqual(TEXT("Overlay remains visible before its close delay"), Host->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
	Close->OnClicked.Broadcast();
	TestEqual(TEXT("Early close hides overlay"), Host->GetVisibility(), ESlateVisibility::Collapsed);
	TestFalse(TEXT("Early close cancels its timer"), UKismetSystemLibrary::K2_IsTimerActive(Instance, CloseTimer));
	Open->OnClicked.Broadcast();
	Tick(0.7f);
	TestEqual(TEXT("Reopened overlay survives the old deadline"), Host->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
	Tick(0.4f);
	TestEqual(TEXT("Reopened overlay closes after its complete delay"), Host->GetVisibility(), ESlateVisibility::Collapsed);
	Instance->RemoveFromParent();
	Instance->RemoveFromRoot();
	DeleteGeneratedAssetsAtPath(Folder);
	return true;
}

// Overlay runtime smoke: the overlay-target page is parented under the host at build time
// (Collapsed), the open chain shows host + page while the page host page stays active, and Back
// collapses both layers again.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPrototypeOverlayRuntimeSmokeTest,
	"Design2UMG.WidgetBlueprint.PrototypeGraphWiring.OverlayRuntimeSmoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPrototypeOverlayRuntimeSmokeTest::RunTest(const FString& Parameters)
{
	FDesignDocument Document;
	FUMGWidgetData Data;
	if (!TestTrue(TEXT("Convert overlay prototype fixture"), ConvertPrototype(OverlayBackPrototypeJson(true), Document, Data)))
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
	// (Collapsed by default) and is NOT a page host child; the flow page stays in the page host.
	UCanvasPanel* HostTemplate = WBP->WidgetTree
		? Cast<UCanvasPanel>(WBP->WidgetTree->FindWidget(FName(*NavigationData.OverlayHostWidgetName)))
		: nullptr;
	UCanvasPanel* PageHostTemplate = WBP->WidgetTree
		? Cast<UCanvasPanel>(WBP->WidgetTree->FindWidget(FName(*NavigationData.PageHostWidgetName)))
		: nullptr;
	if (!TestNotNull(TEXT("Overlay host exists in the built tree"), HostTemplate)
		|| !TestNotNull(TEXT("PageHost exists in the built tree"), PageHostTemplate))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestEqual(TEXT("Overlay host starts collapsed"), HostTemplate->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Both overlay pages are parented under the host"), HostTemplate->GetChildrenCount(), 2);
	UWidget* HostedPageTemplate = HostTemplate->GetChildAt(0);
	TestTrue(TEXT("Hosted page is the overlay target (2:1)"),
		HostedPageTemplate && FUMGWidgetTreeBuilder::MatchesWidgetBaseName(
			HostedPageTemplate->GetName(), FUMGWidgetTreeBuilder::SanitizeWidgetName(Data.PrototypeNavigation().PageWidgetNames.FindRef(TEXT("2:1")), TEXT("CanvasPanel")).ToString()));
	TestEqual(TEXT("Hosted page defaults to collapsed"), HostedPageTemplate->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("PageHost keeps only the non-overlay flow page"), PageHostTemplate->GetChildrenCount(), 1);

	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(WBP->GeneratedClass);
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	UUserWidget* WidgetInstance = GeneratedClass && World ? CreateWidget<UUserWidget>(World, GeneratedClass) : nullptr;
	if (!TestTrue(TEXT("Widget instance created"), WidgetInstance != nullptr))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	WidgetInstance->TakeWidget();

	UCanvasPanel* PageHost = Cast<UCanvasPanel>(
		WidgetInstance->WidgetTree ? WidgetInstance->WidgetTree->FindWidget(FName(*NavigationData.PageHostWidgetName)) : nullptr);
	UCanvasPanel* Host = WidgetInstance->WidgetTree
		? Cast<UCanvasPanel>(WidgetInstance->WidgetTree->FindWidget(FName(*NavigationData.OverlayHostWidgetName)))
		: nullptr;
	UButton* OverlayButton = FindTriggerButton(WidgetInstance->WidgetTree, NavigationData, TEXT("overlay_button"));
	UButton* BackButton = FindTriggerButton(WidgetInstance->WidgetTree, NavigationData, TEXT("back_button"));
	UButton* CloseButton = FindTriggerButton(WidgetInstance->WidgetTree, NavigationData, TEXT("close_button"));
	UButton* NextOverlayButton = FindTriggerButton(WidgetInstance->WidgetTree, NavigationData, TEXT("next_overlay"));
	UButton* TopCloseButton = FindTriggerButton(WidgetInstance->WidgetTree, NavigationData, TEXT("top_close_button"));
	UWidget* TopPage = Host && Host->GetChildrenCount() > 1 ? Host->GetChildAt(1) : nullptr;
	UWidget* HostedPage = Host ? Host->GetChildAt(0) : nullptr;
	if (!TestTrue(TEXT("Runtime page host exists"), PageHost != nullptr)
		|| !TestTrue(TEXT("Runtime overlay host exists"), Host != nullptr)
		|| !TestTrue(TEXT("Runtime hosted page exists"), HostedPage != nullptr)
		|| !TestTrue(TEXT("Overlay button exists"), OverlayButton != nullptr)
		|| !TestTrue(TEXT("Back button exists"), BackButton != nullptr)
		|| !TestTrue(TEXT("Close button exists"), CloseButton != nullptr)
		|| !TestTrue(TEXT("Nested overlay controls exist"), NextOverlayButton && TopCloseButton && TopPage))
	{
		WidgetInstance->RemoveFromParent();
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	TestEqual(TEXT("Overlay content starts hidden"), HostedPage->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Initial active page"), CurrentPageIndex(WidgetInstance), 0);

	// Open: both the host and the hosted page turn visible; the page host page stays active
	// (an overlay rides on top, it does not navigate).
	OverlayButton->OnClicked.Broadcast();
	TestEqual(TEXT("Overlay open shows the host"), Host->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
	TestEqual(TEXT("Overlay open shows the hosted page"), HostedPage->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
	TestEqual(TEXT("Overlay keeps the active page host page unchanged"), CurrentPageIndex(WidgetInstance), 0);

	NextOverlayButton->OnClicked.Broadcast();
	TestTrue(TEXT("Opening a second overlay keeps the first visible"), HostedPage->GetVisibility() != ESlateVisibility::Collapsed);
	TestTrue(TEXT("Opening a second overlay displays it"), TopPage->GetVisibility() != ESlateVisibility::Collapsed);
	const auto* LowerSlot = Cast<UCanvasPanelSlot>(HostedPage->Slot);
	const auto* TopSlot = Cast<UCanvasPanelSlot>(TopPage->Slot);
	TestTrue(TEXT("The newest overlay is painted above the earlier one"), LowerSlot && TopSlot && TopSlot->GetZOrder() > LowerSlot->GetZOrder());
	TopCloseButton->OnClicked.Broadcast();
	TestEqual(TEXT("CLOSE hides only the topmost overlay"), TopPage->GetVisibility(), ESlateVisibility::Collapsed);
	TestTrue(TEXT("CLOSE retains the lower overlay"), HostedPage->GetVisibility() != ESlateVisibility::Collapsed);
	TestTrue(TEXT("CLOSE retains the overlay host while another layer remains"), Host->GetVisibility() != ESlateVisibility::Collapsed);
	TestEqual(TEXT("Closing the top overlay leaves the current page unchanged"), CurrentPageIndex(WidgetInstance), 0);
	CloseButton->OnClicked.Broadcast();
	TestEqual(TEXT("CLOSE collapses the active overlay host"), Host->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("CLOSE collapses the active overlay page"), HostedPage->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("CLOSE leaves the current page unchanged"), CurrentPageIndex(WidgetInstance), 0);
	CloseButton->OnClicked.Broadcast();
	TestEqual(TEXT("CLOSE with no active overlay is a no-op"), Host->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Repeated CLOSE leaves the page unchanged"), CurrentPageIndex(WidgetInstance), 0);
	OverlayButton->OnClicked.Broadcast();
	TestEqual(TEXT("Overlay can reopen after CLOSE"), Host->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);

	// Back: both layers collapse again (the page reset keeps a later different overlay clean).
	BackButton->OnClicked.Broadcast();
	TestEqual(TEXT("Back collapses the host"), Host->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Back collapses the hosted page"), HostedPage->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Back leaves the empty-history page alone"), CurrentPageIndex(WidgetInstance), 0);

	WidgetInstance->RemoveFromParent();
	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

// Plain overlay-only page runtime smoke: its Navigate button lives in the visible hosted page, not in
// a mirror copy. Clicking it must exit the overlay (host + hosted page collapse) before switching the
// underlying page host; otherwise the overlay keeps covering the destination page.
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

	UCanvasPanel* PageHost = Cast<UCanvasPanel>(
		WidgetInstance->WidgetTree ? WidgetInstance->WidgetTree->FindWidget(FName(*NavigationData.PageHostWidgetName)) : nullptr);
	UCanvasPanel* Host = WidgetInstance->WidgetTree
		? Cast<UCanvasPanel>(WidgetInstance->WidgetTree->FindWidget(FName(*NavigationData.OverlayHostWidgetName)))
		: nullptr;
	UButton* OverlayButton = WidgetInstance->WidgetTree ? Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("Overlay"))) : nullptr;
	UButton* NavButton = WidgetInstance->WidgetTree ? Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("Next"))) : nullptr;
	UWidget* HostedPage = Host ? Host->GetChildAt(0) : nullptr;
	if (!TestTrue(TEXT("PageHost exists"), PageHost != nullptr)
		|| !TestTrue(TEXT("Overlay host exists"), Host != nullptr)
		|| !TestTrue(TEXT("Hosted overlay-only page exists"), HostedPage != nullptr)
		|| !TestTrue(TEXT("Overlay button exists"), OverlayButton != nullptr)
		|| !TestTrue(TEXT("Hosted navigate button exists"), NavButton != nullptr))
	{
		WidgetInstance->RemoveFromParent();
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	TestEqual(TEXT("Initial active page"), CurrentPageIndex(WidgetInstance), 0);
	TestEqual(TEXT("Overlay host starts collapsed"), Host->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Hosted page starts collapsed"), HostedPage->GetVisibility(), ESlateVisibility::Collapsed);

	OverlayButton->OnClicked.Broadcast();
	TestEqual(TEXT("Overlay open shows the host"), Host->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
	TestEqual(TEXT("Overlay open shows the hosted page"), HostedPage->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
	TestEqual(TEXT("Overlay keeps the active page unchanged"), CurrentPageIndex(WidgetInstance), 0);

	NavButton->OnClicked.Broadcast();
	TestEqual(TEXT("Navigate collapses the overlay host"), Host->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Navigate collapses the hosted page"), HostedPage->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Navigate switches to the target page"), CurrentPageIndex(WidgetInstance), 1);

	WidgetInstance->RemoveFromParent();
	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

// Dual-target overlay runtime smoke: a page that is both a Navigate target and an Overlay target
// stays a page host child AND gets a synthesized mirror copy under the overlay host. The Navigate
// binding switches to the page host instance; the overlay binding opens the mirror as a layered
// display (page host active index and history untouched); the MIRROR's buttons carry their own
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
	// no design identity anywhere in its subtree (root overlay -> [page host, overlay host]).
	const FUMGWidgetNode* MirrorRootNode = nullptr;
	if (Data.RootWidget.Children.Num() == 2)
	{
		const FString MirrorName = Data.PrototypeNavigation().PageWidgetNames.FindRef(TEXT("2:1")) + Design2UMGPrototypeTokens::OverlayMirrorSuffix();
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
	const FString MirrorPageWidgetName = FUMGWidgetTreeBuilder::SanitizeWidgetName(
		Data.PrototypeNavigation().PageWidgetNames.FindRef(TEXT("2:1")) + Design2UMGPrototypeTokens::OverlayMirrorSuffix(), TEXT("CanvasPanel")).ToString();
	const FString MirrorBackButtonName = FUMGWidgetTreeBuilder::SanitizeWidgetName(
		FString(TEXT("Back")) + Design2UMGPrototypeTokens::OverlayMirrorSuffix(), TEXT("Button")).ToString();
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

	UCanvasPanel* PageHost = Cast<UCanvasPanel>(
		WidgetInstance->WidgetTree ? WidgetInstance->WidgetTree->FindWidget(FName(*NavigationData.PageHostWidgetName)) : nullptr);
	UCanvasPanel* Host = WidgetInstance->WidgetTree
		? Cast<UCanvasPanel>(WidgetInstance->WidgetTree->FindWidget(FName(*NavigationData.OverlayHostWidgetName)))
		: nullptr;
	UButton* NavButton = FindTriggerButton(WidgetInstance->WidgetTree, NavigationData, TEXT("nav_button"));
	UButton* OverlayButton = FindTriggerButton(WidgetInstance->WidgetTree, NavigationData, TEXT("overlay_button"));
	UButton* BackButton = FindTriggerButton(WidgetInstance->WidgetTree, NavigationData, TEXT("back_button"));
		UButton* MirrorBackButton = WidgetInstance->WidgetTree
		? Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(FName(*MirrorBackButtonName))) : nullptr;
	UButton* MirrorNavButton = FindTriggerButton(WidgetInstance->WidgetTree, NavigationData, TEXT("mirror_nav_button"), Design2UMGPrototypeTokens::OverlayMirrorSuffix());
	UWidget* MirrorPage = Host && Host->GetChildrenCount() == 1 ? Host->GetChildAt(0) : nullptr;
	if (!TestTrue(TEXT("PageHost exists"), PageHost != nullptr)
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

	// The dual-target page stays in the page host alongside the flow page (placement rule); the
	// host carries only the synthesized mirror.
	TestEqual(TEXT("Dual-target page stays a page host child"), PageHost->GetChildrenCount(), 2);
	TestEqual(TEXT("Host carries only the mirror copy"), Host->GetChildrenCount(), 1);
	TestEqual(TEXT("Initial active page"), CurrentPageIndex(WidgetInstance), 0);
	TestEqual(TEXT("History starts empty"), HistoryNum(), 0);
	TestEqual(TEXT("Mirror starts collapsed"), MirrorPage->GetVisibility(), ESlateVisibility::Collapsed);

	// (a) The Navigate binding switches to the page host instance of the dual-target page.
	NavButton->OnClicked.Broadcast();
	TestEqual(TEXT("Navigate switches to the dual-target page"), CurrentPageIndex(WidgetInstance), 1);
	TestEqual(TEXT("Navigate pushed the left page onto the history"), HistoryNum(), 1);

	// (b) The overlay binding opens the mirror layered on top: the mirror and the host turn
	// visible while the active page and the history stay untouched.
	OverlayButton->OnClicked.Broadcast();
	TestEqual(TEXT("Overlay open shows the host"), Host->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
	TestEqual(TEXT("Overlay open shows the mirror page"), MirrorPage->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
	TestEqual(TEXT("Overlay keeps the active page host page unchanged"), CurrentPageIndex(WidgetInstance), 1);
	TestEqual(TEXT("Overlay keeps the history unchanged"), HistoryNum(), 1);

	// (c) The MIRROR's back button is the visible button inside the open overlay: its chain
	// collapses the mirror and the host only - the page flow and the history are exactly as the
	// overlay left them.
	MirrorBackButton->OnClicked.Broadcast();
	TestEqual(TEXT("Mirror back collapses the host"), Host->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Mirror back collapses the mirror page"), MirrorPage->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Mirror back keeps the active page unchanged"), CurrentPageIndex(WidgetInstance), 1);
	TestEqual(TEXT("Mirror back keeps the history unchanged"), HistoryNum(), 1);

	// (d) Re-open the overlay, then trigger the MIRROR's navigate button (visible only inside the
	// overlay): its chain closes the overlay first, then runs the standard navigate tail - the
	// page host navigates to "1:1" and the history gains exactly one entry.
	OverlayButton->OnClicked.Broadcast();
	TestEqual(TEXT("Overlay re-open shows the host"), Host->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
	TestEqual(TEXT("Overlay re-open shows the mirror page"), MirrorPage->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
	MirrorNavButton->OnClicked.Broadcast();
	TestEqual(TEXT("Mirror navigate closes the overlay host"), Host->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Mirror navigate collapses the mirror page"), MirrorPage->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Mirror navigate switches to page 1:1"), CurrentPageIndex(WidgetInstance), 0);
	TestEqual(TEXT("Mirror navigate pushed exactly one history entry"), HistoryNum(), 2);

	WidgetInstance->RemoveFromParent();
	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

// Initial-page overlay runtime smoke: when the INITIAL page is an overlay target it stays a
// page host child - the startup active index keeps pointing at it (no remap, no fallback) - and
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

	UCanvasPanel* PageHost = Cast<UCanvasPanel>(
		WidgetInstance->WidgetTree ? WidgetInstance->WidgetTree->FindWidget(FName(*NavigationData.PageHostWidgetName)) : nullptr);
	UCanvasPanel* Host = WidgetInstance->WidgetTree
		? Cast<UCanvasPanel>(WidgetInstance->WidgetTree->FindWidget(FName(*NavigationData.OverlayHostWidgetName)))
		: nullptr;
	UButton* NavButton = FindTriggerButton(WidgetInstance->WidgetTree, NavigationData, TEXT("nav_button"));
	UButton* OverlayButton = FindTriggerButton(WidgetInstance->WidgetTree, NavigationData, TEXT("open_overlay_button"));
	UButton* BackButton = FindTriggerButton(WidgetInstance->WidgetTree, NavigationData, TEXT("back_button"));
	const FString MirrorPageWidgetName = FUMGWidgetTreeBuilder::SanitizeWidgetName(
		Data.PrototypeNavigation().PageWidgetNames.FindRef(TEXT("1:1")) + Design2UMGPrototypeTokens::OverlayMirrorSuffix(), TEXT("CanvasPanel")).ToString();
	UButton* MirrorNavButton = FindTriggerButton(WidgetInstance->WidgetTree, NavigationData, TEXT("nav_button"), Design2UMGPrototypeTokens::OverlayMirrorSuffix());
	UWidget* MirrorPage = Host && Host->GetChildrenCount() == 1 ? Host->GetChildAt(0) : nullptr;
	if (!TestTrue(TEXT("PageHost exists"), PageHost != nullptr)
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

	// The startup page IS the overlay target and it must still be the active page host child: the
	// index stays 0 and the shown widget is the initial page itself, not a remapped fallback. The
	// host carries only the synthesized mirror copy.
	TestEqual(TEXT("Initial page stays a page host child"), PageHost->GetChildrenCount(), 2);
	TestEqual(TEXT("Host carries only the mirror copy"), Host->GetChildrenCount(), 1);
	TestEqual(TEXT("Startup active index is unchanged"), CurrentPageIndex(WidgetInstance), 0);
	TestTrue(TEXT("Startup page shown is the initial page"),
		CurrentPageWidget(WidgetInstance, PageHost) && FUMGWidgetTreeBuilder::MatchesWidgetBaseName(
			CurrentPageWidget(WidgetInstance, PageHost)->GetName(),
			FUMGWidgetTreeBuilder::SanitizeWidgetName(Data.PrototypeNavigation().PageWidgetNames.FindRef(TEXT("1:1")), TEXT("CanvasPanel")).ToString()));
	TestEqual(TEXT("Mirror starts collapsed"), MirrorPage->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("History starts empty"), HistoryNum(), 0);

	// Right at startup (before any navigate): opening the overlay targeting the initial page
	// shows the mirror layered on top - the active page and the history stay untouched.
	OverlayButton->OnClicked.Broadcast();
	TestEqual(TEXT("Startup overlay open shows the host"), Host->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
	TestEqual(TEXT("Startup overlay open shows the initial-page mirror"), MirrorPage->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
	TestEqual(TEXT("Startup overlay keeps the active page unchanged"), CurrentPageIndex(WidgetInstance), 0);
	TestEqual(TEXT("Startup overlay keeps the history empty"), HistoryNum(), 0);

	// The MIRROR's nav button (nav_button lives on the initial page, so its mirror is the visible
	// one inside the overlay): its chain closes the overlay, then runs the standard navigate
	// tail - the history gains the left page and the page host moves to page two.
	MirrorNavButton->OnClicked.Broadcast();
	TestEqual(TEXT("Mirror navigate closes the overlay host"), Host->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Mirror navigate collapses the mirror"), MirrorPage->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Mirror navigate switches to page two"), CurrentPageIndex(WidgetInstance), 1);
	TestEqual(TEXT("Mirror navigate pushed exactly one history entry"), HistoryNum(), 1);

	// Re-open the overlay on top of page two: layered display, page flow untouched.
	OverlayButton->OnClicked.Broadcast();
	TestEqual(TEXT("Overlay re-open shows the host"), Host->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
	TestEqual(TEXT("Overlay re-open shows the initial-page mirror"), MirrorPage->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
	TestEqual(TEXT("Overlay re-open keeps the active page unchanged"), CurrentPageIndex(WidgetInstance), 1);
	TestEqual(TEXT("Overlay re-open keeps the history unchanged"), HistoryNum(), 1);

	// The ORIGINAL back button closes the overlay without popping the history; the recorded
	// history still returns to the initial page on a second Back.
	BackButton->OnClicked.Broadcast();
	TestEqual(TEXT("Back collapses the host"), Host->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Back collapses the mirror"), MirrorPage->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Back keeps the active page unchanged"), CurrentPageIndex(WidgetInstance), 1);
	TestEqual(TEXT("Back keeps the history unchanged"), HistoryNum(), 1);

	BackButton->OnClicked.Broadcast();
	TestEqual(TEXT("Second back returns to the initial page"), CurrentPageIndex(WidgetInstance), 0);
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
	Entry.WidgetClass = ComponentFixtures::MappedClass()->GetPathName();

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

	// Built tree: BOTH the original (inside the page host page) and the mirror (inside the hosted
	// mirror page) resolved to the mapped project widget class.
	UUserWidget* OriginalInstance = WBP->WidgetTree
		? ComponentFixtures::CastMapped(WBP->WidgetTree->FindWidget(TEXT("MappedButton"))) : nullptr;
	UUserWidget* MirrorInstance = WBP->WidgetTree
		? ComponentFixtures::CastMapped(WBP->WidgetTree->FindWidget(FName(*MirrorInstanceName))) : nullptr;
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

	UCanvasPanel* PageHost = Cast<UCanvasPanel>(
		WidgetInstance->WidgetTree ? WidgetInstance->WidgetTree->FindWidget(FName(*NavigationData.PageHostWidgetName)) : nullptr);
	UCanvasPanel* Host = WidgetInstance->WidgetTree
		? Cast<UCanvasPanel>(WidgetInstance->WidgetTree->FindWidget(FName(*NavigationData.OverlayHostWidgetName)))
		: nullptr;
	UButton* NavButton = WidgetInstance->WidgetTree ? Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("Next"))) : nullptr;
	UButton* OverlayButton = WidgetInstance->WidgetTree ? Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("Overlay"))) : nullptr;
	UButton* BackButton = WidgetInstance->WidgetTree ? Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("Back"))) : nullptr;
	UWidget* HostedPage = Host && Host->GetChildrenCount() > 0 ? Host->GetChildAt(0) : nullptr;
	if (!TestTrue(TEXT("PageHost exists"), PageHost != nullptr)
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
	TestEqual(TEXT("Navigate switches to page B"), CurrentPageIndex(WidgetInstance), 1);
	TestEqual(TEXT("History holds the left page"), HistoryNum(), 1);

	// Open the overlay on top of B.
	OverlayButton->OnClicked.Broadcast();
	TestEqual(TEXT("Overlay open shows the host"), Host->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
	TestEqual(TEXT("Overlay open shows the hosted page"), HostedPage->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
	TestEqual(TEXT("Overlay keeps page B active"), CurrentPageIndex(WidgetInstance), 1);

	// Back closes the overlay while preserving active page B and its navigation history.
	BackButton->OnClicked.Broadcast();
	TestEqual(TEXT("Back closes the overlay host"), Host->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Back collapses the hosted page"), HostedPage->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Back with an open overlay stays on page B"), CurrentPageIndex(WidgetInstance), 1);
	TestEqual(TEXT("Back with an open overlay keeps the history"), HistoryNum(), 1);

	// Back again (overlay closed): pops the history and returns to A.
	BackButton->OnClicked.Broadcast();
	TestEqual(TEXT("Second back returns to page A"), CurrentPageIndex(WidgetInstance), 0);
	TestEqual(TEXT("Second back popped the history"), HistoryNum(), 0);

	WidgetInstance->RemoveFromParent();
	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

// Exercise the generated animation and completion timer through the native frame path,
// then check page visibility and the restored source transform after further frames.
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

	// MOVE_OUT uses one shared native page transition.
	TestEqual(TEXT("MOVE_OUT generates one shared animation"), WBP->Animations.Num(), 1);
	TestNotNull(TEXT("Page animation generated"), FindGeneratedAnimation(WBP, TEXT("__d2u_nav_anim_000_pages")));

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

	UCanvasPanel* PageHost = Cast<UCanvasPanel>(
		WidgetInstance->WidgetTree ? WidgetInstance->WidgetTree->FindWidget(FName(*NavigationData.PageHostWidgetName)) : nullptr);
	UButton* TriggerButton = WidgetInstance->WidgetTree ? Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("CTA"))) : nullptr;
	if (!TestTrue(TEXT("PageHost exists"), PageHost != nullptr)
		|| !TestTrue(TEXT("Trigger button exists"), TriggerButton != nullptr))
	{
		WidgetInstance->RemoveFromParent();
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	TestTrue(TEXT("PageHost translation starts at the origin"),
		Design2UMGWidgetPropertyAccess::GetRenderTransform(*PageHost).Translation.Equals(FVector2D::ZeroVector));

	TriggerButton->OnClicked.Broadcast();
	TestEqual(TEXT("Page switch waits for the outgoing delay"), CurrentPageIndex(WidgetInstance), 0);
	const FObjectProperty* OutgoingAnimationProperty = FindFProperty<FObjectProperty>(
		GeneratedClass, FName(TEXT("__d2u_nav_anim_000_pages")));
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
	// A nonintegral frame cadence exercises completion and animation evaluation on adjacent frames.
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
		++GFrameCounter;
		World->GetLatentActionManager().BeginFrame();
		World->GetLatentActionManager().ProcessLatentActions(WidgetInstance, FrameSeconds);
		World->GetTimerManager().Tick(FrameSeconds);
		FSlateApplication::Get().OnPostTick().Broadcast(FrameSeconds);
	};

	// Begin with a timer-only frame, then drive both timer and animation to completion.
	bool bChainCompleted = false;
	for (int32 FrameIndex = 0; FrameIndex < 60 && !bChainCompleted; ++FrameIndex)
	{
		RunFrame(FrameIndex > 0);
		bChainCompleted = CurrentPageIndex(WidgetInstance) == 1;
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

	TestEqual(TEXT("MOVE_OUT completes the page switch"), CurrentPageIndex(WidgetInstance), 1);
	TestTrue(TEXT("MOVE_OUT restores the source page after residual animation frames"),
		PageHost->GetChildrenCount() == 2 && Design2UMGWidgetPropertyAccess::GetRenderTransform(*PageHost->GetChildAt(0)).Translation.Equals(FVector2D::ZeroVector));
	TestEqual(TEXT("MOVE_OUT hides the source page"), PageHost->GetChildAt(0)->GetVisibility(), ESlateVisibility::Collapsed);
	TestTrue(TEXT("MOVE_OUT displays the target page"), PageHost->GetChildAt(1)->GetVisibility() != ESlateVisibility::Collapsed);
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

	// MOVE_IN uses one shared native page transition.
	TestEqual(TEXT("MOVE_IN generates one shared animation"), WBP->Animations.Num(), 1);
	TestNotNull(TEXT("Page animation generated"), FindGeneratedAnimation(WBP, TEXT("__d2u_nav_anim_000_pages")));

	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(WBP->GeneratedClass);
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	UUserWidget* WidgetInstance = GeneratedClass && World ? CreateWidget<UUserWidget>(World, GeneratedClass) : nullptr;
	if (!TestTrue(TEXT("Widget instance created"), WidgetInstance != nullptr))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	WidgetInstance->TakeWidget();

	UCanvasPanel* PageHost = Cast<UCanvasPanel>(
		WidgetInstance->WidgetTree ? WidgetInstance->WidgetTree->FindWidget(FName(*NavigationData.PageHostWidgetName)) : nullptr);
	UButton* TriggerButton = WidgetInstance->WidgetTree ? Cast<UButton>(WidgetInstance->WidgetTree->FindWidget(TEXT("CTA"))) : nullptr;
	if (!TestTrue(TEXT("PageHost exists"), PageHost != nullptr)
		|| !TestTrue(TEXT("Trigger button exists"), TriggerButton != nullptr))
	{
		WidgetInstance->RemoveFromParent();
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	// Both real pages are present during the single transition; commit occurs after playback.
	TriggerButton->OnClicked.Broadcast();
	TestEqual(TEXT("MOVE_IN keeps the source as current during playback"), CurrentPageIndex(WidgetInstance), 0);
	TestTrue(TEXT("MOVE_IN makes the target page visible during playback"), PageHost->GetChildrenCount() == 2
		&& PageHost->GetChildAt(1)->GetVisibility() != ESlateVisibility::Collapsed);
	const FObjectProperty* IncomingAnimationProperty = FindFProperty<FObjectProperty>(
		GeneratedClass, FName(TEXT("__d2u_nav_anim_000_pages")));
	UWidgetAnimation* IncomingAnimation = IncomingAnimationProperty
		? Cast<UWidgetAnimation>(IncomingAnimationProperty->GetObjectPropertyValue_InContainer(WidgetInstance))
		: nullptr;
	TestTrue(TEXT("Incoming slide animation is playing after the switch"),
		IncomingAnimation && WidgetInstance->IsAnimationPlaying(IncomingAnimation));

	WidgetInstance->RemoveFromParent();
	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaPresetEventReuseTest,
	"Design2UMG.WidgetBlueprint.PrototypeGraphWiring.PresetEventReuse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaPresetEventReuseTest::RunTest(const FString&)
{
	FDesignDocument Document;
	FUMGWidgetData Data;
	if (!TestTrue(TEXT("Source animation fixture converts"), ConvertPrototype(MakeNativeWidgetAnimationDocument(), Document, Data))) return false;
	Data.WidgetName = TEXT("WBP_PresetEventReuse");
	const FString Folder = TEXT("/Game/Design2UMG/_TestOutput/PresetEventReuse");
	DeleteGeneratedAssetsAtPath(Folder);
	FUMGPrototypeNavigationData Navigation;
	auto* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, Folder, nullptr, nullptr, &Navigation);
	if (!TestNotNull(TEXT("Source blueprint built"), Blueprint)
		|| !TestTrue(TEXT("Source blueprint saves"), FUMGGenerator::SaveWidgetBlueprint(Blueprint, nullptr, &Navigation).IsSuccess())) return false;
	const auto* OriginalClick = FKismetEditorUtilities::FindBoundEventForComponent(Blueprint, TEXT("OnClicked"), TriggerButtonName(Blueprint->WidgetTree, TEXT("instance_0")));
	if (!TestNotNull(TEXT("Existing source event exists"), OriginalClick)) return false;
	Design2UMGAnimationPresets::FSettings Settings;
	Settings.Preset = Design2UMGAnimationPresets::EPreset::ButtonScale;
	Settings.TargetWidgetName = TriggerButtonName(Blueprint->WidgetTree, TEXT("instance_0"));
	TArray<FString> Warnings;
	if (!TestTrue(TEXT("Native feedback preset applies"), Design2UMGAnimationPresets::Apply(Blueprint, Settings, Warnings))) return false;
	TestEqual(TEXT("Source click event is retained"), FKismetEditorUtilities::FindBoundEventForComponent(Blueprint, TEXT("OnClicked"), TriggerButtonName(Blueprint->WidgetTree, TEXT("instance_0"))), OriginalClick);
	for (const FName Delegate : {FName(TEXT("OnHovered")), FName(TEXT("OnUnhovered")), FName(TEXT("OnPressed")), FName(TEXT("OnReleased"))})
		TestNotNull(*FString::Printf(TEXT("Native event %s exists"), *Delegate.ToString()), FKismetEditorUtilities::FindBoundEventForComponent(Blueprint, Delegate, TriggerButtonName(Blueprint->WidgetTree, TEXT("instance_0"))));
	Warnings.Reset();
	TestTrue(TEXT("Applying the same preset again succeeds"), Design2UMGAnimationPresets::Apply(Blueprint, Settings, Warnings));
	TestEqual(TEXT("Reapplying retains the source event"), FKismetEditorUtilities::FindBoundEventForComponent(Blueprint, TEXT("OnClicked"), TriggerButtonName(Blueprint->WidgetTree, TEXT("instance_0"))), OriginalClick);
	DeleteGeneratedAssetsAtPath(Folder);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaTemporaryPressNavigateRuntimeTest,
	"Design2UMG.WidgetBlueprint.PrototypeGraphWiring.TemporaryPressNavigateRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaTemporaryPressNavigateRuntimeTest::RunTest(const FString&)
{
	FDesignDocument Document;
	FUMGWidgetData Data;
	const FString Source = NavigatePrototypeJson().Replace(TEXT("\"ON_CLICK\""), TEXT("\"ON_PRESS\""));
	if (!TestTrue(TEXT("Temporary navigation source converts"), ConvertPrototype(Source, Document, Data))) return false;
	const FString Folder = TEXT("/Game/Design2UMG/_TestOutput/TemporaryPressNavigate");
	DeleteGeneratedAssetsAtPath(Folder);
	FUMGPrototypeNavigationData Navigation;
	auto* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, Folder, nullptr, nullptr, &Navigation);
	FDesign2UMGGenerationOutput Output;
	if (!TestNotNull(TEXT("Temporary navigation blueprint builds"), Blueprint)
		|| !TestTrue(TEXT("Temporary navigation blueprint saves"), FUMGGenerator::SaveWidgetBlueprint(Blueprint, &Output, &Navigation).IsSuccess())
		|| !TestFalse(TEXT("Temporary navigation has no skipped action"), Output.bHasDegradation)) return false;
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Editor world exists"), World) || !TestNotNull(TEXT("Blueprint class exists"), Blueprint->GeneratedClass.Get())
		|| !TestTrue(TEXT("Slate is initialized"), FSlateApplication::IsInitialized())) return false;
	auto* Instance = CreateWidget<UUserWidget>(World, Blueprint->GeneratedClass.Get());
	if (!TestNotNull(TEXT("Saved class instantiates"), Instance)) return false;
	Instance->AddToRoot();
	const auto Window = SNew(SWindow).Title(FText::FromString(TEXT("Temporary press navigation")))
		.ClientSize(FVector2D(360, 240)).FocusWhenFirstShown(false).SupportsMaximize(false).SupportsMinimize(false)
		[SNew(SBox)[Instance->TakeWidget()]];
	FSlateApplication::Get().AddWindow(Window, true);
	ON_SCOPE_EXIT { FSlateApplication::Get().RequestDestroyWindow(Window); Instance->RemoveFromRoot(); DeleteGeneratedAssetsAtPath(Folder); };
	Window->SlatePrepass();
	FSlateApplication::Get().Tick();
	FSlateApplication::Get().Tick();
	auto* Host = Cast<UCanvasPanel>(Instance->WidgetTree->FindWidget(FName(*Navigation.PageHostWidgetName)));
	auto* Button = Cast<UButton>(Instance->WidgetTree->FindWidget(TEXT("CTA")));
	if (!TestNotNull(TEXT("Native page host exists"), Host) || !TestEqual(TEXT("Host retains both pages"), Host->GetChildrenCount(), 2)
		|| !TestNotNull(TEXT("Source button exists"), Button)) return false;
	const auto Control = Button->GetCachedWidget();
	if (!TestTrue(TEXT("Source button has real geometry"), Control.IsValid())) return false;
	const auto* History = FindFProperty<FArrayProperty>(Instance->GetClass(), FName(Design2UMGPrototypeGraphWiring::NavHistoryVariableName));
	const auto HistoryCount = [&]() { return History ? FScriptArrayHelper(History, History->ContainerPtrToValuePtr<void>(Instance)).Num() : 0; };
	const auto Tick = [&](float Seconds)
	{
		++GFrameCounter;
		UUMGSequenceTickManager::Get(Instance)->OnWidgetTicked(Instance);
		FSlateApplication::Get().OnPreTick().Broadcast(Seconds);
		World->GetLatentActionManager().BeginFrame();
		World->GetLatentActionManager().ProcessLatentActions(Instance, Seconds);
		World->GetTimerManager().Tick(Seconds);
		FSlateApplication::Get().OnPostTick().Broadcast(Seconds);
	};
	TSharedPtr<FSlateVirtualUserHandle> UserHandle;
	int32 VirtualIndex = 0;
	do { UserHandle = FSlateApplication::Get().FindOrCreateVirtualUser(VirtualIndex++); } while (!UserHandle.IsUnique());
	const int32 UserIndex = UserHandle->GetUserIndex();
	const FVector2D Center = Control->GetCachedGeometry().LocalToAbsolute(Control->GetCachedGeometry().GetLocalSize() * 0.5);
	const TArray<TSharedRef<SWindow>> Windows{Window};
	const FWidgetPath Path = FSlateApplication::Get().LocateWindowUnderMouse(Center, Windows, false, UserIndex);
	if (!TestTrue(TEXT("Real hit test reaches source button"), Path.IsValid() && Path.ContainsWidget(Control.Get()))) return false;
	const FPointerEvent Move(UserIndex, FSlateApplication::CursorPointerIndex, Center, Center, {}, FKey(), 0, FModifierKeysState());
	FSlateApplication::Get().RoutePointerMoveEvent(Path, Move, true);
	const TSet<FKey> Pressed{EKeys::LeftMouseButton};
	const FPointerEvent Down(UserIndex, FSlateApplication::CursorPointerIndex, Center, Center, Pressed, EKeys::LeftMouseButton, 0, FModifierKeysState());
	const FPointerEvent Up(UserIndex, FSlateApplication::CursorPointerIndex, Center, Center, {}, EKeys::LeftMouseButton, 0, FModifierKeysState());
	if (!TestTrue(TEXT("Real press is handled"), FSlateApplication::Get().RoutePointerDownEvent(Path, Down).IsEventHandled())) return false;
	Tick(0.03f);
	TestTrue(TEXT("Press displays the temporary destination"), Host->GetChildAt(1)->GetVisibility() != ESlateVisibility::Collapsed);
	TestEqual(TEXT("Press retains the current source page"), CurrentPageIndex(Instance), 0);
	TestEqual(TEXT("Press does not push global history"), HistoryCount(), 0);
	if (!TestTrue(TEXT("Real release is handled"), FSlateApplication::Get().RoutePointerUpEvent(Path, Up).IsEventHandled())) return false;
	for (int32 Frame = 0; Frame < 12; ++Frame) Tick(0.03f);
	TestTrue(TEXT("Short press restores the original page"), Host->GetChildAt(0)->GetVisibility() != ESlateVisibility::Collapsed);
	TestEqual(TEXT("Short press hides the temporary destination"), Host->GetChildAt(1)->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Release keeps the current source page"), CurrentPageIndex(Instance), 0);
	TestEqual(TEXT("Release leaves history unchanged"), HistoryCount(), 0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaNativePressAnimationBindingTest,
	"Design2UMG.WidgetBlueprint.PrototypeGraphWiring.NativePressAnimationBinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaNativePressAnimationBindingTest::RunTest(const FString&)
{
	FDesignDocument Document;
	FUMGWidgetData Data;
	const FString Source = MakeNativeWidgetAnimationDocument().Replace(TEXT("\"ON_CLICK\""), TEXT("\"ON_PRESS\""));
	if (!TestTrue(TEXT("Press animation source converts"), ConvertPrototype(Source, Document, Data))) return false;
	Data.WidgetName = TEXT("WBP_NativePressAnimationContract");
	const FString Folder = TEXT("/Game/Design2UMG/_TestOutput/NativePressAnimationContract");
	DeleteGeneratedAssetsAtPath(Folder);
	FUMGPrototypeNavigationData Navigation;
	auto* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, Folder, nullptr, nullptr, &Navigation);
	if (!TestNotNull(TEXT("Press blueprint built"), Blueprint)) return false;
	FDesign2UMGGenerationOutput Output;
	if (!TestTrue(TEXT("Press blueprint compiles and saves"), FUMGGenerator::SaveWidgetBlueprint(Blueprint, &Output, &Navigation).IsSuccess())) return false;
	TestNotNull(TEXT("Press starts the animation"), FKismetEditorUtilities::FindBoundEventForComponent(Blueprint, TEXT("OnPressed"), TriggerButtonName(Blueprint->WidgetTree, TEXT("instance_0"))));
	TestNotNull(TEXT("Release reverses the animation"), FKismetEditorUtilities::FindBoundEventForComponent(Blueprint, TEXT("OnReleased"), TriggerButtonName(Blueprint->WidgetTree, TEXT("instance_0"))));
	DeleteGeneratedAssetsAtPath(Folder);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaNativeWidgetAnimationBuildTest,
	"Design2UMG.WidgetBlueprint.PrototypeGraphWiring.NativeWidgetAnimationBuild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaNativeWidgetAnimationBuildTest::RunTest(const FString&)
{
	FDesignDocument Document;
	FUMGWidgetData Data;
	if (!TestTrue(TEXT("Canonical basic animation source converts"), ConvertPrototype(MakeNativeWidgetAnimationDocument(), Document, Data))) return false;
	Data.WidgetName = TEXT("WBP_NativeWidgetAnimationContract");
	const FString Folder = TEXT("/Game/Design2UMG/_TestOutput/NativeWidgetAnimationContract");
	DeleteGeneratedAssetsAtPath(Folder);
	FUMGPrototypeNavigationData Navigation;
	auto* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, Folder, nullptr, nullptr, &Navigation);
	if (!TestNotNull(TEXT("Native animation blueprint built"), Blueprint)) return false;
	FDesign2UMGGenerationOutput SaveOutput;
	if (!TestTrue(TEXT("Native animation blueprint compiles and saves"), FUMGGenerator::SaveWidgetBlueprint(Blueprint, &SaveOutput, &Navigation).IsSuccess())) return false;
	for (const auto& Diagnostic : SaveOutput.MappingDiagnostics) AddInfo(Diagnostic.Message);
	TestFalse(TEXT("All four native trigger contracts wire without silent degradation"), SaveOutput.bHasDegradation);
	if (!TestEqual(TEXT("One animation per source interaction"), Blueprint->Animations.Num(), 4)) return false;
	TSet<FName> AnimatedWidgets;
	for (const UWidgetAnimation* Animation : Blueprint->Animations)
	{
		TestEqual(TEXT("Only engine animation assets"), Animation->GetClass(), UWidgetAnimation::StaticClass());
		const float TickTolerance = Animation->MovieScene
			? static_cast<float>(Animation->MovieScene->GetTickResolution().AsInterval()) + KINDA_SMALL_NUMBER : 0.0f;
		TestTrue(TEXT("Full source action duration, not two half transitions"),
			FMath::IsNearlyEqual(Animation->GetEndTime() - Animation->GetStartTime(), 0.4f, TickTolerance));
		TArray<FName> PaintOwners;
		for (const auto& WidgetBinding : Animation->AnimationBindings)
			if (HasColorTrackForWidget(Animation, WidgetBinding.WidgetName)
				&& FindTransformForWidget(Animation, WidgetBinding.WidgetName)
				&& FindOpacityChannelForWidget(Animation, WidgetBinding.WidgetName))
				PaintOwners.Add(WidgetBinding.WidgetName);
		if (!TestEqual(TEXT("Each leaf target has one concrete native property owner"), PaintOwners.Num(), 1)) return false;
		AnimatedWidgets.Add(PaintOwners[0]);
		const auto Tracks = CollectAnimationTracks(Animation);
		TestEqual(TEXT("Transform, opacity and color tracks are native and editable"), Tracks.Num(), 3);
		TestTrue(TEXT("Native solid color track exists"), Tracks.ContainsByPredicate([](const UMovieSceneTrack* Track)
		{
			const auto* Color = Cast<UMovieSceneColorTrack>(Track);
			return Color && Color->GetPropertyPath() == TEXT("BrushColor");
		}));
	}
	TestEqual(TEXT("Same-named children in four instances remain independent"), AnimatedWidgets.Num(), 4);
	TestNotNull(TEXT("Native click event exists"), FKismetEditorUtilities::FindBoundEventForComponent(Blueprint, TEXT("OnClicked"), TriggerButtonName(Blueprint->WidgetTree, TEXT("instance_0"))));
	TestNotNull(TEXT("Native mouse-enter event exists"), FKismetEditorUtilities::FindBoundEventForComponent(Blueprint, TEXT("OnHovered"), TriggerButtonName(Blueprint->WidgetTree, TEXT("instance_1"))));
	TestNull(TEXT("MOUSE_ENTER never gains hover-revert behavior"), FKismetEditorUtilities::FindBoundEventForComponent(Blueprint, TEXT("OnUnhovered"), TriggerButtonName(Blueprint->WidgetTree, TEXT("instance_1"))));
	TestNotNull(TEXT("ON_HOVER retains native leave reversal"), FKismetEditorUtilities::FindBoundEventForComponent(Blueprint, TEXT("OnUnhovered"), TriggerButtonName(Blueprint->WidgetTree, TEXT("instance_3"))));
	const auto ReachableCall = [Blueprint](const UEdGraphNode* Start, const FName Name, const FString& TimerName = FString())
	{
		TArray<const UEdGraphNode*> Pending{Start};
		TSet<const UEdGraphNode*> Seen;
		while (!Pending.IsEmpty())
		{
			const UEdGraphNode* Node = Pending.Pop();
			if (!Node || Seen.Contains(Node)) continue;
			Seen.Add(Node);
            if (const auto* Call = Cast<UK2Node_CallFunction>(Node))
            {
                if (Call->FunctionReference.IsSelfContext())
                    for (const UEdGraph* Function : Blueprint->FunctionGraphs)
                        if (Function->GetFName() == Call->FunctionReference.GetMemberName())
                            for (const UEdGraphNode* Entry : Function->Nodes)
                                if (Entry->IsA<UK2Node_FunctionEntry>()) Pending.Add(Entry);
                if (Call->FunctionReference.GetMemberName() == Name
					&& (TimerName.IsEmpty() || (Call->FindPin(TEXT("FunctionName"))
						&& Call->FindPin(TEXT("FunctionName"))->DefaultValue == TimerName))) return true;
            }
			for (const UEdGraphPin* Pin : Node->Pins)
				if (Pin && Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
					for (const UEdGraphPin* Link : Pin->LinkedTo) if (Link) Pending.Add(Link->GetOwningNode());
		}
		return false;
	};
	const UK2Node_Event* Construct = nullptr;
	const UK2Node_Event* Destruct = nullptr;
	const UK2Node_FunctionEntry* CleanupEvent = nullptr;
	for (const UEdGraph* Graph : Blueprint->UbergraphPages)
		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			if (const auto* Event = Cast<UK2Node_Event>(Node))
			{
				if (Event->EventReference.GetMemberName() == TEXT("Construct")) Construct = Event;
				if (Event->EventReference.GetMemberName() == TEXT("Destruct")) Destruct = Event;
			}

		}
	for (const UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
        if (!Graph->Nodes.ContainsByPredicate([](const UEdGraphNode* Node)
            { return Node->NodeComment == Design2UMGPrototypeGraphWiring::NodeMarkerComment; })) continue;
        for (const UEdGraphNode* Node : Graph->Nodes)
		{
			TestEqual(TEXT("Synchronous lifecycle functions retain generated ownership"), Node->NodeComment, FString(Design2UMGPrototypeGraphWiring::NodeMarkerComment));
			if (const auto* Entry = Cast<UK2Node_FunctionEntry>(Node))
				if (Graph->GetFName() == TEXT("__d2u_cancel_animation_actions")) CleanupEvent = Entry;
			for (const UEdGraphPin* Pin : Node->Pins)
				for (const UEdGraphPin* Link : Pin->LinkedTo)
					TestTrue(TEXT("Lifecycle function pins never cross graph boundaries"), Link->GetOwningNode()->GetGraph() == Graph);
		}
	}
	TestNotNull(TEXT("Cleanup is a synchronous function entry"), CleanupEvent);
	TestTrue(TEXT("Native destruction invokes animation cleanup"), ReachableCall(Destruct, TEXT("__d2u_cancel_animation_actions")));
	TestTrue(TEXT("Native cleanup clears the automatic timer and stops animations"),
		ReachableCall(CleanupEvent, TEXT("K2_ClearTimer"), TEXT("__d2u_anim_002_play"))
		&& ReachableCall(CleanupEvent, TEXT("StopAnimation")));
	TestTrue(TEXT("Native construction invokes automatic page playback"), ReachableCall(Construct, TEXT("__d2u_page_open")));
	const UEdGraph* StateTimers = nullptr;
	for (const UEdGraph* Graph : Blueprint->FunctionGraphs)
		if (Graph->GetFName() == Design2UMGPrototypeGraphWiring::BindingFunctionName(TEXT("state_timeouts"), 0)) StateTimers = Graph;
	if (!TestNotNull(TEXT("State timers have an owned synchronous activation dispatcher"), StateTimers)) return false;
	TestTrue(TEXT("Automatic playback checks the actual native state ancestry before scheduling"),
		ReachableCall(Construct, StateTimers->GetFName()) && StateTimers->Nodes.ContainsByPredicate([](const UEdGraphNode* Node)
		{
			const auto* Call = Cast<UK2Node_CallFunction>(Node);
			return Call && Call->FunctionReference.GetMemberName() == Design2UMGPrototypeGraphWiring::BindingFunctionName(TEXT("state_visible"), 0)
				&& Call->FindPin(TEXT("Widget")) && !Call->FindPin(TEXT("Widget"))->LinkedTo.IsEmpty();
		}));
    const auto* Trigger = FKismetEditorUtilities::FindBoundEventForComponent(Blueprint, TEXT("OnClicked"), TriggerButtonName(Blueprint->WidgetTree, TEXT("instance_0")));
    TestTrue(TEXT("Native trigger enters its own synchronous body"), ReachableCall(Trigger, TEXT("__d2u_trigger_000")));
    TestTrue(TEXT("Cancellation invokes a separate state restore function"),
        Blueprint->FunctionGraphs.ContainsByPredicate([](const UEdGraph* Graph) { return Graph->GetName().StartsWith(TEXT("__d2u_restore_host_")); }));
    const int32 GeneratedFunctionCount = Blueprint->FunctionGraphs.Num();
    TArray<FString> RewireWarnings;
    Design2UMGPrototypeGraphWiring::WireSimplePrototypeBindings(Blueprint, Navigation, RewireWarnings);
    TestEqual(TEXT("Regeneration replaces synchronous functions without accumulation"), Blueprint->FunctionGraphs.Num(), GeneratedFunctionCount);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    TestTrue(TEXT("Regenerated synchronous bodies compile"), Blueprint->Status != BS_Error);
	const FString OpacitySource = MakeNativeWidgetAnimationDocument()
		.Replace(TEXT("\"componentId\":\"base\""), TEXT("\"componentId\":\"base\",\"opacity\":0.5"))
		.Replace(TEXT("\"name\":\"On\""), TEXT("\"name\":\"On\",\"opacity\":0.25"));
	FDesignDocument OpacityDocument;
	FUMGWidgetData OpacityData;
	if (!TestTrue(TEXT("Root-opacity animation source converts"), ConvertPrototype(OpacitySource, OpacityDocument, OpacityData))) return false;
	OpacityData.WidgetName = TEXT("WBP_NativeRootOpacityContract");
	const FString OpacityFolder = Folder / TEXT("RootOpacity");
	FUMGPrototypeNavigationData OpacityNavigation;
	auto* OpacityBlueprint = FUMGGenerator::BuildWidgetBlueprint(OpacityData, OpacityFolder, nullptr, nullptr, &OpacityNavigation);
	if (!TestNotNull(TEXT("Root-opacity blueprint builds"), OpacityBlueprint)
		|| !TestTrue(TEXT("Root-opacity blueprint saves"), FUMGGenerator::SaveWidgetBlueprint(OpacityBlueprint, nullptr, &OpacityNavigation).IsSuccess())) return false;
	auto* RootButton = Cast<UButton>(OpacityBlueprint->WidgetTree->FindWidget(TriggerButtonName(OpacityBlueprint->WidgetTree, TEXT("instance_0"))));
	if (!TestTrue(TEXT("Animated opacity has one native root carrier"), RootButton && RootButton->GetContent())) return false;
	const FButtonStyle& TriggerStyle = Design2UMGWidgetPropertyAccess::GetButtonStyle(*RootButton);
	TestTrue(TEXT("Prototype trigger adds no normal or pressed layout padding"),
		TriggerStyle.NormalPadding == FMargin(0.0f) && TriggerStyle.PressedPadding == FMargin(0.0f));
	const TSharedRef<SWidget> TriggerSlate = RootButton->TakeWidget();
	TriggerSlate->SlatePrepass();
	TestTrue(TEXT("Prototype trigger keeps its content's desired size"),
		RootButton->GetDesiredSize().Equals(RootButton->GetContent()->GetDesiredSize(), 0.01));
	RootButton->ReleaseSlateResources(true);
	TestTrue(TEXT("Root opacity starts at 0.5, not 0.5 multiplied twice"), FMath::IsNearlyEqual(RootButton->GetRenderOpacity(), 0.5f)
		&& FMath::IsNearlyEqual(RootButton->GetContent()->GetRenderOpacity(), 1.f));
	bool bRootOpacityKeys = false;
	for (const UWidgetAnimation* Animation : OpacityBlueprint->Animations)
		for (const auto& Binding : Animation->AnimationBindings)
		{
			if (Binding.WidgetName != RootButton->GetFName()) continue;
			const FMovieSceneBinding* MovieBinding = Animation->MovieScene->FindBinding(Binding.AnimationGuid);
			if (!TestNotNull(TEXT("Root opacity animation binding is persisted"), MovieBinding)) return false;
			for (const UMovieSceneTrack* Track : MovieBinding->GetTracks())
				if (const auto* Float = Cast<UMovieSceneFloatTrack>(Track))
					for (UMovieSceneSection* Section : Float->GetAllSections())
					{
						const auto* Opacity = CastChecked<UMovieSceneFloatSection>(Section);
						float Start = 0, End = 0;
						bRootOpacityKeys |= Opacity->GetChannel().Evaluate(FFrameTime(0), Start)
							&& Opacity->GetChannel().Evaluate(FFrameTime(Animation->MovieScene->GetPlaybackRange().GetUpperBoundValue()), End)
							&& FMath::IsNearlyEqual(Start, 0.5f) && FMath::IsNearlyEqual(End, 0.25f);
					}
		}
	TestTrue(TEXT("Root native opacity keys retain exact 0.5 to 0.25 values without alpha division"), bRootOpacityKeys);
	auto* UserExit = FKismetEditorUtilities::FindBoundEventForComponent(OpacityBlueprint, TEXT("OnUnhovered"), TriggerButtonName(OpacityBlueprint->WidgetTree, TEXT("instance_3")));
	if (!TestNotNull(TEXT("Hover exit exists before user-content conflict"), UserExit)) return false;
	if (!TestEqual(TEXT("Existing hover exit has its native reverse body"), UserExit->GetThenPin()->LinkedTo.Num(), 1)) return false;
	const auto* OriginalForward = FKismetEditorUtilities::FindBoundEventForComponent(OpacityBlueprint, TEXT("OnHovered"), TriggerButtonName(OpacityBlueprint->WidgetTree, TEXT("instance_3")));
	const_cast<UK2Node_ComponentBoundEvent*>(UserExit)->NodeComment = TEXT("User-authored hover exit");
	UEdGraphNode* UserExitBody = UserExit->GetThenPin()->LinkedTo[0]->GetOwningNode();
	UserExitBody->NodeComment = TEXT("User-authored hover exit body");
	UEdGraphPin* UserAnimationPin = nullptr;
	TArray<UEdGraphNode*> Pending{UserExitBody};
	TSet<UEdGraphNode*> Visited;
	while (!Pending.IsEmpty() && !UserAnimationPin)
	{
		UEdGraphNode* Node = Pending.Pop();
		if (!Node || Visited.Contains(Node)) continue;
		Visited.Add(Node);
        if (const auto* Call = Cast<UK2Node_CallFunction>(Node))
            if (Call->FunctionReference.IsSelfContext())
                for (const UEdGraph* Graph : OpacityBlueprint->FunctionGraphs)
                    if (Graph->GetFName() == Call->FunctionReference.GetMemberName())
                        for (UEdGraphNode* Entry : Graph->Nodes)
                            if (Entry->IsA<UK2Node_FunctionEntry>()) Pending.Add(Entry);
		UEdGraphPin* Candidate = Node->FindPin(TEXT("InAnimation"), EGPD_Input);
		if (Candidate && Candidate->LinkedTo.Num() == 1) { UserAnimationPin = Candidate; break; }
		for (UEdGraphPin* Pin : Node->Pins)
			if (Pin && Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
				for (UEdGraphPin* Linked : Pin->LinkedTo) if (Linked) Pending.Add(Linked->GetOwningNode());
	}
	if (!TestNotNull(TEXT("Hover exit reaches a native animation reference"), UserAnimationPin)) return false;
	UEdGraphNode* UserAnimationGet = UserAnimationPin->LinkedTo[0]->GetOwningNode();
	UserAnimationGet->NodeComment = TEXT("User-authored hover animation reference");
	TArray<FString> ConflictWarnings;
	Design2UMGPrototypeGraphWiring::WireSimplePrototypeBindings(OpacityBlueprint, OpacityNavigation, ConflictWarnings);
	TestEqual(TEXT("User-authored hover exit is preserved"), FKismetEditorUtilities::FindBoundEventForComponent(OpacityBlueprint, TEXT("OnUnhovered"), TriggerButtonName(OpacityBlueprint->WidgetTree, TEXT("instance_3"))), UserExit);
	TestTrue(TEXT("User-authored exit logic stays connected"), UserExit->GetThenPin()->LinkedTo.Num() == 1
		&& UserExit->GetThenPin()->LinkedTo[0]->GetOwningNode() == UserExitBody
		&& UserAnimationPin->LinkedTo.Num() == 1 && UserAnimationPin->LinkedTo[0]->GetOwningNode() == UserAnimationGet);
	TestTrue(TEXT("Mixed graph preserves the existing forward event"), OriginalForward
		&& FKismetEditorUtilities::FindBoundEventForComponent(OpacityBlueprint, TEXT("OnHovered"), TriggerButtonName(OpacityBlueprint->WidgetTree, TEXT("instance_3"))) == OriginalForward);
	TestTrue(TEXT("Mixed graph preservation is explicitly diagnosed"), ConflictWarnings.ContainsByPredicate([](const FString& Warning)
	{
		return Warning.Contains(TEXT("user connections")) && Warning.Contains(TEXT("preserved"));
	}));
	OpacityData.WidgetName = TEXT("WBP_NativeLifecycleIsolation");
	FUMGPrototypeNavigationData IsolatedNavigation;
	auto* IsolatedBlueprint = FUMGGenerator::BuildWidgetBlueprint(OpacityData, Folder / TEXT("LifecycleIsolation"), nullptr, nullptr, &IsolatedNavigation);
	if (!TestNotNull(TEXT("Independent lifecycle blueprint builds"), IsolatedBlueprint)
		|| !TestTrue(TEXT("Independent lifecycle blueprint saves"), FUMGGenerator::SaveWidgetBlueprint(IsolatedBlueprint, nullptr, &IsolatedNavigation).IsSuccess())) return false;
	OpacityBlueprint = IsolatedBlueprint;
	OpacityNavigation = MoveTemp(IsolatedNavigation);
	Design2UMGPrototypeGraphWiring::ClearGeneratedBindings(OpacityBlueprint);
	auto OverlayNavigation = OpacityNavigation;
	OverlayNavigation.ClickBindings[0].Steps[0].Kind = EUMGPrototypeNavigationStepKind::Overlay;
	ConflictWarnings.Reset();
	Design2UMGPrototypeGraphWiring::WireSimplePrototypeBindings(OpacityBlueprint, OverlayNavigation, ConflictWarnings);
	TestNull(TEXT("Overlay cannot consume a property animation as an instant action"), FKismetEditorUtilities::FindBoundEventForComponent(OpacityBlueprint, TEXT("OnClicked"), TriggerButtonName(OpacityBlueprint->WidgetTree, TEXT("instance_0"))));
	TestTrue(TEXT("Unsupported property action is diagnosed by shared binding admission"), ConflictWarnings.ContainsByPredicate([](const FString& Warning)
	{
		return Warning.Contains(TEXT("instance_0")) && Warning.Contains(TEXT("Overlay and other actions not executed"));
	}));

	Design2UMGPrototypeGraphWiring::ClearGeneratedBindings(OpacityBlueprint);
	TArray<UK2Node_Event*> EmptyLifecycleEvents;
	for (UEdGraph* Graph : OpacityBlueprint->UbergraphPages)
		for (UEdGraphNode* Node : Graph->Nodes)
			if (auto* Event = Cast<UK2Node_Event>(Node))
				if (Event->EventReference.GetMemberName() == TEXT("Construct") || Event->EventReference.GetMemberName() == TEXT("Destruct"))
					EmptyLifecycleEvents.Add(Event);
	for (auto* Event : EmptyLifecycleEvents)
	{
		if (!TestTrue(TEXT("Default lifecycle event is empty before relocating it"), Event->GetThenPin()->LinkedTo.IsEmpty())) return false;
		FBlueprintEditorUtils::RemoveNode(OpacityBlueprint, Event, true);
	}
	UEdGraph* OtherGraph = FBlueprintEditorUtils::CreateNewGraph(OpacityBlueprint, FName(TEXT("UserLifecycle")), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	FBlueprintEditorUtils::AddUbergraphPage(OpacityBlueprint, OtherGraph);
	const auto AddLifecycle = [&](const TCHAR* Name)
	{
		FGraphNodeCreator<UK2Node_Event> Creator(*OtherGraph);
		auto* Event = Creator.CreateNode();
		Event->EventReference.SetExternalMember(Name, UUserWidget::StaticClass());
		Event->bOverrideFunction = true;
		Creator.Finalize();
		return Event;
	};
	auto* OtherConstruct = AddLifecycle(TEXT("Construct"));
	auto* OtherDestruct = AddLifecycle(TEXT("Destruct"));
	const auto CountLifecycle = [&]()
	{
		int32 Count = 0;
		for (const UEdGraph* Graph : OpacityBlueprint->UbergraphPages)
			for (const UEdGraphNode* Node : Graph->Nodes)
				if (const auto* Event = Cast<UK2Node_Event>(Node))
					Count += Event->EventReference.GetMemberName() == TEXT("Construct") || Event->EventReference.GetMemberName() == TEXT("Destruct") ? 1 : 0;
		return Count;
	};
	ConflictWarnings.Reset();
	Design2UMGPrototypeGraphWiring::WireSimplePrototypeBindings(OpacityBlueprint, OpacityNavigation, ConflictWarnings);
	TestEqual(TEXT("Empty lifecycle events in another graph are reused without duplicates"), CountLifecycle(), 2);
	for (auto* Event : {OtherConstruct, OtherDestruct})
	{
		if (!TestEqual(TEXT("Reused lifecycle has one owned call"), Event->GetThenPin()->LinkedTo.Num(), 1)) return false;
		TestEqual(TEXT("Lifecycle call stays in its event's graph"), Event->GetThenPin()->LinkedTo[0]->GetOwningNode()->GetGraph(), OtherGraph);
	}
	FKismetEditorUtilities::CompileBlueprint(OpacityBlueprint);
	TestTrue(TEXT("Reused cross-graph lifecycle compiles"), OpacityBlueprint->Status == BS_UpToDate || OpacityBlueprint->Status == BS_UpToDateWithWarnings);
	Design2UMGPrototypeGraphWiring::ClearGeneratedBindings(OpacityBlueprint);
	TArray<UK2Node_CallFunction*> UserBodies;
	for (auto* Event : {OtherConstruct, OtherDestruct})
	{
		Event->NodeComment = TEXT("User lifecycle");
		Event->GetThenPin()->BreakAllPinLinks();
		FGraphNodeCreator<UK2Node_CallFunction> Creator(*OtherGraph);
		auto* Body = Creator.CreateNode();
		Body->SetFromFunction(UWidget::StaticClass()->FindFunctionByName(TEXT("InvalidateLayoutAndVolatility")));
		Creator.Finalize();
		Event->GetThenPin()->MakeLinkTo(Body->GetExecPin());
		UserBodies.Add(Body);
	}
	ConflictWarnings.Reset();
	Design2UMGPrototypeGraphWiring::WireSimplePrototypeBindings(OpacityBlueprint, OpacityNavigation, ConflictWarnings);
	TestEqual(TEXT("Connected user lifecycle events are not duplicated"), CountLifecycle(), 2);
	for (int32 Index = 0; Index < UserBodies.Num(); ++Index)
	{
		auto* Event = Index == 0 ? OtherConstruct : OtherDestruct;
		TestTrue(TEXT("Other-graph user lifecycle body remains reachable"), ReachableCall(Event, TEXT("InvalidateLayoutAndVolatility")));
		TestTrue(TEXT("Generated lifecycle dispatcher shares the native sequence"), ReachableCall(Event,
			Index == 0 ? TEXT("__d2u_page_open") : TEXT("__d2u_cancel_animation_actions")));
	}
	TestNotNull(TEXT("Compatible lifecycle retains mouse property actions"), FKismetEditorUtilities::FindBoundEventForComponent(OpacityBlueprint, TEXT("OnClicked"), TriggerButtonName(OpacityBlueprint->WidgetTree, TEXT("instance_0"))));
	TestFalse(TEXT("Compatible lifecycle is not incorrectly diagnosed as a conflict"), ConflictWarnings.ContainsByPredicate([](const FString& Warning)
	{
		return Warning.Contains(TEXT("lifecycle conflicts with user content"));
	}));
	Design2UMGPrototypeGraphWiring::ClearGeneratedBindings(OpacityBlueprint);
	for (int32 Index = 0; Index < UserBodies.Num(); ++Index)
	{
		auto* Event = Index == 0 ? OtherConstruct : OtherDestruct;
		TestTrue(TEXT("Cleanup restores the original user lifecycle connection"), Event->GetThenPin()->LinkedTo.Num() == 1
			&& Event->GetThenPin()->LinkedTo[0]->GetOwningNode() == UserBodies[Index]);
	}
	FBlueprintEditorUtils::RemoveNode(OpacityBlueprint, OtherConstruct, true);
	FBlueprintEditorUtils::RemoveNode(OpacityBlueprint, OtherDestruct, true);
	for (auto* Body : UserBodies) FBlueprintEditorUtils::RemoveNode(OpacityBlueprint, Body, true);
	TArray<UK2Node_CustomEvent*> ReservedEvents;
	for (const TCHAR* Name : {TEXT("__d2u_page_open"), TEXT("__d2u_cancel_animation_actions")})
	{
		FGraphNodeCreator<UK2Node_CustomEvent> Creator(*OtherGraph);
		auto* Event = Creator.CreateNode();
		Event->CustomFunctionName = Name;
		Creator.Finalize();
		ReservedEvents.Add(Event);
	}
	ConflictWarnings.Reset();
	Design2UMGPrototypeGraphWiring::WireSimplePrototypeBindings(OpacityBlueprint, OpacityNavigation, ConflictWarnings);
	for (auto* Event : ReservedEvents) TestTrue(TEXT("Other-graph reserved user event is preserved"), OtherGraph->Nodes.Contains(Event));
	TestNull(TEXT("Reserved event conflict refuses mouse property actions"), FKismetEditorUtilities::FindBoundEventForComponent(OpacityBlueprint, TEXT("OnClicked"), TriggerButtonName(OpacityBlueprint->WidgetTree, TEXT("instance_0"))));
	TestTrue(TEXT("Other-graph reserved event conflict is diagnosed"), ConflictWarnings.ContainsByPredicate([](const FString& Warning)
	{
		return Warning.Contains(TEXT("lifecycle conflicts with user content"));
	}));
	for (auto* Event : ReservedEvents) FBlueprintEditorUtils::RemoveNode(OpacityBlueprint, Event, true);
	Design2UMGPrototypeGraphWiring::ClearGeneratedBindings(OpacityBlueprint);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(OpacityBlueprint);
	const auto CheckDispatcherConflict = [&]()
	{
		ConflictWarnings.Reset();
		Design2UMGPrototypeGraphWiring::WireSimplePrototypeBindings(OpacityBlueprint, OpacityNavigation, ConflictWarnings);
		TestTrue(TEXT("Reserved function or member name conflict is diagnosed"), ConflictWarnings.ContainsByPredicate([](const FString& Warning)
		{
			return Warning.Contains(TEXT("lifecycle conflicts with user content"));
		}));
		for (const UEdGraph* Graph : OpacityBlueprint->UbergraphPages)
			TestFalse(TEXT("Dispatcher name conflict refuses all generated property animation wiring"), Graph->Nodes.ContainsByPredicate([](const UEdGraphNode* Node)
			{
				return Node && Node->NodeComment == Design2UMGPrototypeGraphWiring::NodeMarkerComment;
			}));
	};
	UEdGraph* ReservedFunction = FBlueprintEditorUtils::CreateNewGraph(OpacityBlueprint, FName(TEXT("__d2u_page_open")), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	FBlueprintEditorUtils::AddFunctionGraph<UClass>(OpacityBlueprint, ReservedFunction, true, nullptr);
	const auto UserFunctionNodes = ReservedFunction->Nodes;
	CheckDispatcherConflict();
	TestTrue(TEXT("Reserved ordinary user function and its nodes are preserved"), OpacityBlueprint->FunctionGraphs.Contains(ReservedFunction)
		&& ReservedFunction->Nodes == UserFunctionNodes);
	TestNotNull(TEXT("Reserved ordinary user function remains reflected"), OpacityBlueprint->SkeletonGeneratedClass->FindFunctionByName(TEXT("__d2u_page_open")));
	FBlueprintEditorUtils::RemoveGraph(OpacityBlueprint, ReservedFunction, EGraphRemoveFlags::MarkTransient);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(OpacityBlueprint);
	FEdGraphPinType UserVariableType;
	UserVariableType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	if (!TestTrue(TEXT("User member occupies cleanup dispatcher name"), FBlueprintEditorUtils::AddMemberVariable(
		OpacityBlueprint, TEXT("__d2u_cancel_animation_actions"), UserVariableType, TEXT("true")))) return false;
	const FGuid UserVariableGuid = OpacityBlueprint->NewVariables.Last().VarGuid;
	CheckDispatcherConflict();
	TestTrue(TEXT("Reserved user member keeps its identity and default value"), OpacityBlueprint->NewVariables.ContainsByPredicate([&](const FBPVariableDescription& Variable)
	{
		return Variable.VarName == TEXT("__d2u_cancel_animation_actions") && Variable.VarGuid == UserVariableGuid && Variable.DefaultValue == TEXT("true");
	}));
	TestNotNull(TEXT("Reserved user member remains reflected"), FindFProperty<FBoolProperty>(OpacityBlueprint->SkeletonGeneratedClass, TEXT("__d2u_cancel_animation_actions")));
	FBlueprintEditorUtils::RemoveMemberVariable(OpacityBlueprint, TEXT("__d2u_cancel_animation_actions"));
	ConflictWarnings.Reset();
	Design2UMGPrototypeGraphWiring::WireSimplePrototypeBindings(OpacityBlueprint, OpacityNavigation, ConflictWarnings);
	TestTrue(TEXT("Removing user name conflicts restores normal property animation admission"), ConflictWarnings.IsEmpty());
	ConflictWarnings.Reset();
	Design2UMGPrototypeGraphWiring::WireSimplePrototypeBindings(OpacityBlueprint, OpacityNavigation, ConflictWarnings);
	TestTrue(TEXT("Regeneration does not mistake its previous dispatchers for user content"), ConflictWarnings.IsEmpty());
	TestNotNull(TEXT("Regeneration restores native click playback"), FKismetEditorUtilities::FindBoundEventForComponent(OpacityBlueprint, TEXT("OnClicked"), TriggerButtonName(OpacityBlueprint->WidgetTree, TEXT("instance_0"))));
	FKismetEditorUtilities::CompileBlueprint(OpacityBlueprint);
	TestTrue(TEXT("Regenerated lifecycle dispatchers compile without name collisions"), OpacityBlueprint->Status == BS_UpToDate || OpacityBlueprint->Status == BS_UpToDateWithWarnings);
	auto* RefreshText = OpacityBlueprint->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("RefreshLabel"));
	auto* RefreshParent = Cast<UPanelWidget>(OpacityBlueprint->WidgetTree->RootWidget);
	if (!TestNotNull(TEXT("Font refresh fixture has a native panel root"), RefreshParent)) return false;
	RefreshParent->AddChild(RefreshText);
	DesignWidgetBlueprintUtils::EnsureWidgetVariableGuids(OpacityBlueprint);
	auto* DestinationText = NewObject<UTextBlock>();
	FSlateFontInfo DestinationFont = RefreshText->GetFont();
	DestinationFont.Size += 12;
	DestinationText->SetFont(DestinationFont);
	UWidgetAnimation* RefreshAnimation = OpacityBlueprint->Animations[0];
	const auto WriteRefreshKeys = [](FMovieSceneFloatChannel& Channel, float Start, float End)
	{
		Channel.AddLinearKey(FFrameNumber(0), Start);
		Channel.AddLinearKey(FFrameNumber(10), End);
	};
	FString RefreshError;
	NativeTextAnimation::FRefreshBuilder Refresh(OpacityBlueprint);
	if (!TestTrue(TEXT("Real native font refresh endpoint builds"), Refresh.AddFontTracks(
		RefreshAnimation, RefreshText, DestinationText, true, false, WriteRefreshKeys, RefreshError)))
	{
		AddError(RefreshError);
		return false;
	}
	Refresh.Build([&](UWidgetAnimation*, const FString& Error) { AddError(Error); });
	ConflictWarnings.Reset();
	Design2UMGPrototypeGraphWiring::WireSimplePrototypeBindings(OpacityBlueprint, OpacityNavigation, ConflictWarnings);
	TestTrue(TEXT("Native refresh delegates wire without degradation"), ConflictWarnings.IsEmpty());
	for (const FName Function : {FName(TEXT("BindToAnimationFinished")), FName(TEXT("UnbindFromAnimationFinished"))})
	{
		int32 Calls = 0;
		for (const UEdGraph* Graph : OpacityBlueprint->UbergraphPages)
			for (const UEdGraphNode* Node : Graph->Nodes)
				if (const auto* Call = Cast<UK2Node_CallFunction>(Node))
					if (Call->FunctionReference.GetMemberName() == Function)
					{
						++Calls;
						const auto* AnimationPin = Call->FindPin(TEXT("Animation"));
						const auto* DelegatePin = Call->FindPin(TEXT("Delegate"));
						TestTrue(TEXT("Native finished callback receives its animation and delegate"), AnimationPin && AnimationPin->LinkedTo.Num() == 1
							&& DelegatePin && DelegatePin->LinkedTo.Num() == 1);
					}
		TestTrue(TEXT("Native refresh lifecycle contains the bind and unbind calls"), Calls > 0);
	}
	FKismetEditorUtilities::CompileBlueprint(OpacityBlueprint);
	TestTrue(TEXT("Native finished refresh delegates compile"), OpacityBlueprint->Status == BS_UpToDate || OpacityBlueprint->Status == BS_UpToDateWithWarnings);
	DeleteGeneratedAssetsAtPath(OpacityFolder);
	DeleteGeneratedAssetsAtPath(Folder / TEXT("LifecycleIsolation"));

	{
		const FString LeafShadowJson = MakeNativeWidgetAnimationDocument().Replace(TEXT("\"id\":\"target_surface\""),
			TEXT("\"id\":\"target_surface\",\"styles\":{\"fill\":\"resolved-fill\"}"))
			.Replace(TEXT("\"rotation\":1.5707963267948966"),
				TEXT("\"effects\":[{\"type\":\"DROP_SHADOW\",\"visible\":true,\"color\":{\"r\":0,\"g\":0,\"b\":0,\"a\":0.3},\"offset\":{\"x\":0,\"y\":2},\"radius\":4,\"spread\":0}],\"rotation\":1.5707963267948966"));
		FDesignDocument LeafShadowDocument;
		FUMGWidgetData LeafShadowData;
		if (!TestTrue(TEXT("The original unclipped rotated leaf shadow source converts"),
			ConvertPrototype(LeafShadowJson, LeafShadowDocument, LeafShadowData))) return false;
		LeafShadowData.WidgetName = TEXT("WBP_NativeLeafShadow");
		const FString LeafShadowFolder = Folder / TEXT("LeafShadow");
		DeleteGeneratedAssetsAtPath(LeafShadowFolder);
		ON_SCOPE_EXIT { DeleteGeneratedAssetsAtPath(LeafShadowFolder); };
		FUMGPrototypeNavigationData LeafShadowNavigation;
		auto* LeafShadowBlueprint = FUMGGenerator::BuildWidgetBlueprint(LeafShadowData, LeafShadowFolder, nullptr, nullptr, &LeafShadowNavigation);
		FDesign2UMGGenerationOutput LeafShadowOutput;
		if (!TestNotNull(TEXT("The original leaf shadow builds a native blueprint"), LeafShadowBlueprint)
			|| !TestTrue(TEXT("The original leaf shadow saves through the real generation chain"),
				FUMGGenerator::SaveWidgetBlueprint(LeafShadowBlueprint, &LeafShadowOutput, &LeafShadowNavigation).IsSuccess())) return false;
		TestFalse(TEXT("The original leaf shadow has no degraded or skipped native action"), LeafShadowOutput.bHasDegradation);
		const auto* LeafShadowBinding = LeafShadowNavigation.ClickBindings.FindByPredicate(
			[](const FUMGPrototypeNavigationBinding& Binding) { return Binding.TriggerWidgetId == TEXT("instance_0"); });
		TestTrue(TEXT("The original leaf shadow retains its executable native SMART animation after saving"), LeafShadowBinding
			&& LeafShadowBinding->Steps.Num() == 1 && LeafShadowBinding->PrimaryStep().Transition.Type == TEXT("SMART_ANIMATE")
			&& LeafShadowBinding->PrimaryStep().Transition.Diagnostic.IsEmpty() && !LeafShadowBinding->PrimaryStep().Transition.WidgetAnimationName.IsEmpty()
			&& FindObject<UWidgetAnimation>(LeafShadowBlueprint, *LeafShadowBinding->PrimaryStep().Transition.WidgetAnimationName) != nullptr);
	}

	{
		const FString StaticClipFill = TEXT(",\"fills\":[{\"type\":\"SOLID\",\"color\":{\"r\":0.2,\"g\":0.3,\"b\":0.4,\"a\":1}}]");
		const FString StaticClipAction = TEXT(",\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"navigation\":\"NAVIGATE\",\"destinationId\":\"clip_page_b\",\"transition\":{\"type\":\"SMART_ANIMATE\",\"duration\":200,\"easing\":{\"type\":\"LINEAR\"}}}]}]");
		const auto BuildStaticClipOwner = [&](const TCHAR* Id, bool bClip)
		{
			return MakeContainerNode(Id, TEXT("Owner"), TEXT("FRAME"), bClip ? 12 : 120, 14, 40, 40,
				MakeLeafNode(bClip ? TEXT("clip_child_b") : TEXT("clip_child_a"), TEXT("Child"), TEXT("RECTANGLE"), 2, 3, 8, 8, StaticClipFill),
				StaticClipFill + (bClip ? TEXT(",\"clipsContent\":true") : TEXT(",\"clipsContent\":false")));
		};
		const FString StaticClipJson = MakeFigmaDocJson(
			MakeContainerNode(TEXT("clip_page_a"), TEXT("Source"), TEXT("FRAME"), 0, 0, 100, 100,
				BuildStaticClipOwner(TEXT("clip_owner_a"), false), StaticClipAction + TEXT(",\"clipsContent\":true")) + TEXT(",")
			+ MakeContainerNode(TEXT("clip_page_b"), TEXT("Target"), TEXT("FRAME"), 0, 0, 100, 100,
				BuildStaticClipOwner(TEXT("clip_owner_b"), true), TEXT(",\"clipsContent\":true")),
			TEXT(",\"design2umgPrototypeDurationUnit\":\"MILLISECONDS\""));
		FDesignDocument StaticClipDocument;
		FUMGWidgetData StaticClipData;
		if (!TestTrue(TEXT("An unclipped native owner inside a clipped page converts"), ConvertPrototype(StaticClipJson, StaticClipDocument, StaticClipData))) return false;
		if (!TestEqual(TEXT("The clip owner keeps its source action"), StaticClipData.PrototypeNavigation().ClickBindings.Num(), 1)) return false;
		StaticClipData.WidgetName = TEXT("WBP_ArrangedNativeClipOwner");
		const FString ArrangedClipFolder = Folder / TEXT("ArrangedNativeClipOwner");
		DeleteGeneratedAssetsAtPath(ArrangedClipFolder);
		ON_SCOPE_EXIT { DeleteGeneratedAssetsAtPath(ArrangedClipFolder); };
		FUMGPrototypeNavigationData ArrangedNavigation;
		auto* ArrangedBlueprint = FUMGGenerator::BuildWidgetBlueprint(StaticClipData, ArrangedClipFolder, nullptr, nullptr, &ArrangedNavigation);
		FDesign2UMGGenerationOutput ArrangedOutput;
		if (!TestNotNull(TEXT("An offscreen source keeps real native arranged endpoint geometry"), ArrangedBlueprint)
			|| !TestTrue(TEXT("The clipped-offscreen to onscreen native action builds and saves"),
				FUMGGenerator::SaveWidgetBlueprint(ArrangedBlueprint, &ArrangedOutput, &ArrangedNavigation).IsSuccess())) return false;
		TestFalse(TEXT("Offscreen painting does not remove the native layout action"), ArrangedOutput.bHasDegradation);
		TestTrue(TEXT("The offscreen source keeps its positive native endpoint guard and saved animation"),
			ArrangedNavigation.ClickBindings.Num() == 1 && ArrangedNavigation.ClickBindings[0].PrimaryStep().Transition.Diagnostic.IsEmpty()
			&& !ArrangedNavigation.ClickBindings[0].PrimaryStep().Transition.WidgetAnimationName.IsEmpty()
			&& FindObject<UWidgetAnimation>(ArrangedBlueprint, *ArrangedNavigation.ClickBindings[0].PrimaryStep().Transition.WidgetAnimationName) != nullptr);
		// The resource owner also needs its bounded native frame when its page action is instantaneous.
		auto& StaticBinding = StaticClipData.MutablePrototypeNavigation().ClickBindings[0];
		StaticBinding.Steps[0].Transition = FUMGPrototypeTransitionData();
		StaticClipData.WidgetName = TEXT("WBP_StaticNativeClipOwner");
		const FString StaticClipFolder = Folder / TEXT("StaticNativeClipOwner");
		DeleteGeneratedAssetsAtPath(StaticClipFolder);
		ON_SCOPE_EXIT { DeleteGeneratedAssetsAtPath(StaticClipFolder); };
		FUMGPrototypeNavigationData StaticNavigation;
		auto* StaticBlueprint = FUMGGenerator::BuildWidgetBlueprint(StaticClipData, StaticClipFolder, nullptr, nullptr, &StaticNavigation);
		FDesign2UMGGenerationOutput StaticOutput;
		if (!TestNotNull(TEXT("The static shared owner measures its real native ancestor clip"), StaticBlueprint)
			|| !TestTrue(TEXT("The measured static capture defaults survive native save"), FUMGGenerator::SaveWidgetBlueprint(StaticBlueprint, &StaticOutput, &StaticNavigation).IsSuccess())) return false;
		TestFalse(TEXT("The static ancestor clip does not discard the native page action"), StaticOutput.bHasDegradation);
		TArray<UWidget*> StaticWidgets;
		StaticBlueprint->WidgetTree->GetAllWidgets(StaticWidgets);
		bool bSavedAncestorFrame = false;
		for (UWidget* StaticCaptureWidget : StaticWidgets)
			if (const auto* StaticCaptureRetainer = Cast<URetainerBox>(StaticCaptureWidget))
				if (const auto* StaticCaptureMaterial = Design2UMGWidgetPropertyAccess::GetRetainerEffectMaterialInterface(*StaticCaptureRetainer))
				{
					FLinearColor StaticCaptureFrame;
					if (StaticCaptureMaterial->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("CaptureRect")), StaticCaptureFrame)
						&& StaticCaptureFrame.R < 0.f && StaticCaptureFrame.B == 100.f && StaticCaptureFrame.A == 100.f) bSavedAncestorFrame = true;
				}
		TestTrue(TEXT("The persisted capture frame uses its native ancestor's size rather than the unclipped owner's size"), bSavedAncestorFrame);
	}

	{
		const FString LiveFontAction = TEXT(",\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"navigation\":\"NAVIGATE\",\"destinationId\":\"font_page_b\",\"transition\":{\"type\":\"SMART_ANIMATE\",\"duration\":200,\"easing\":{\"type\":\"LINEAR\"}}}]}]");
		const FString LiveFontFill = TEXT(",\"fills\":[{\"type\":\"GRADIENT_LINEAR\",\"gradientHandlePositions\":[{\"x\":0,\"y\":0},{\"x\":1,\"y\":0},{\"x\":0,\"y\":1}],\"gradientStops\":[{\"position\":0,\"color\":{\"r\":1,\"g\":0,\"b\":0,\"a\":1}},{\"position\":1,\"color\":{\"r\":0,\"g\":0,\"b\":1,\"a\":1}}]}]");
		const FString LiveFontSolid = TEXT(",\"fills\":[{\"type\":\"SOLID\",\"color\":{\"r\":1,\"g\":1,\"b\":1,\"a\":1}}]");
		const FString LiveFontSourceStyle = TEXT("\"fontFamily\":\"Roboto\",\"fontSize\":18,\"lineHeightPx\":20,\"lineHeightUnit\":\"PIXELS\",\"letterSpacing\":0");
		const FString LiveFontTargetStyle = TEXT("\"fontFamily\":\"Roboto\",\"fontSize\":22,\"lineHeightPx\":24,\"lineHeightUnit\":\"PIXELS\",\"letterSpacing\":20");
		const FString LiveFontJson = MakeFigmaDocJson(
			MakeContainerNode(TEXT("font_page_a"), TEXT("Source"), TEXT("FRAME"), 0, 0, 100, 40,
				MakeTextNode(TEXT("font_first_a"), TEXT("First"), 0, 0, 40, 32, TEXT("42"), LiveFontSourceStyle, LiveFontSolid + LiveFontAction)
				+ TEXT(",") + MakeTextNode(TEXT("font_second_a"), TEXT("Second"), 50, 0, 40, 32, TEXT("42"), LiveFontSourceStyle, LiveFontFill + LiveFontAction))
			+ TEXT(",") + MakeContainerNode(TEXT("font_page_b"), TEXT("Target"), TEXT("FRAME"), 0, 0, 100, 40,
				MakeTextNode(TEXT("font_first_b"), TEXT("First"), 0, 0, 40, 32, TEXT("42"), LiveFontTargetStyle, LiveFontSolid)
				+ TEXT(",") + MakeTextNode(TEXT("font_second_b"), TEXT("Second"), 50, 0, 40, 32, TEXT("42"), LiveFontTargetStyle, LiveFontFill)),
			TEXT(",\"design2umgPrototypeDurationUnit\":\"MILLISECONDS\""));
		FDesignDocument LiveFontDocument;
		FUMGWidgetData LiveFontData;
		if (!TestTrue(TEXT("Two interactive text endpoints convert"), ConvertPrototype(LiveFontJson, LiveFontDocument, LiveFontData))) return false;
		if (!TestEqual(TEXT("Both interactive text actions are retained"), LiveFontData.PrototypeNavigation().ClickBindings.Num(), 2)) return false;
		for (const auto& LiveFontBinding : LiveFontData.PrototypeNavigation().ClickBindings)
			TestTrue(TEXT("Interactive text layout contains the other live content and real font changes"),
				LiveFontBinding.PrimaryStep().Transition.bFixedViewportLayout && !LiveFontBinding.PrimaryStep().Transition.LivePresentations.IsEmpty()
				&& LiveFontBinding.PrimaryStep().Transition.WidgetTargets.ContainsByPredicate([](const FUMGPrototypeWidgetAnimationTarget& Target)
					{ return Target.bFontSize && Target.bLetterSpacing; }));
		LiveFontData.WidgetName = TEXT("WBP_LiveTextPropertyOwners");
		const FString LiveFontFolder = Folder / TEXT("LiveTextPropertyOwners");
		DeleteGeneratedAssetsAtPath(LiveFontFolder);
		ON_SCOPE_EXIT { DeleteGeneratedAssetsAtPath(LiveFontFolder); };
		FUMGPrototypeNavigationData LiveFontNavigation;
		auto* LiveFontBlueprint = FUMGGenerator::BuildWidgetBlueprint(LiveFontData, LiveFontFolder, nullptr, nullptr, &LiveFontNavigation);
		FDesign2UMGGenerationOutput LiveFontOutput;
		if (!TestNotNull(TEXT("Live text content resolves real glyphs while sockets keep layout"), LiveFontBlueprint)
			|| !TestTrue(TEXT("Both live text actions compile and save"), FUMGGenerator::SaveWidgetBlueprint(LiveFontBlueprint, &LiveFontOutput, &LiveFontNavigation).IsSuccess())) return false;
		TestFalse(TEXT("Live glyph resolution does not skip native actions"), LiveFontOutput.bHasDegradation);
		for (const auto& LiveFontBinding : LiveFontNavigation.ClickBindings)
		{
			const auto* LiveFontAnimation = FindObject<UWidgetAnimation>(LiveFontBlueprint, *LiveFontBinding.PrimaryStep().Transition.WidgetAnimationName);
			if (!TestNotNull(TEXT("Each live text action saves its native animation"), LiveFontAnimation)) return false;
			TSet<FName> LiveFontSockets;
			for (const auto& Presentation : LiveFontBinding.PrimaryStep().Transition.LivePresentations) LiveFontSockets.Add(FName(*Presentation.PresentationSocketWidgetName));
			int32 LiveGlyphTracks = 0;
			const UMovieScene& MovieScene = *LiveFontAnimation->MovieScene;
			for (const FMovieSceneBinding& TrackBinding : MovieScene.GetBindings())
				for (const UMovieSceneTrack* Track : TrackBinding.GetTracks())
					if (const auto* FloatTrack = Cast<UMovieSceneFloatTrack>(Track); FloatTrack && FloatTrack->GetPropertyPath() == TEXT("Font.Size"))
					{
						const auto* GlyphBinding = LiveFontAnimation->AnimationBindings.FindByPredicate([&](const FWidgetAnimationBinding& Item)
							{ return Item.AnimationGuid == TrackBinding.GetObjectGuid(); });
						const auto* GlyphProperty = GlyphBinding ? FindFProperty<FObjectProperty>(LiveFontBlueprint->GeneratedClass, GlyphBinding->WidgetName) : nullptr;
						TestTrue(TEXT("Font curves bind actual TextBlocks rather than the layout socket"), GlyphProperty
							&& GlyphProperty->PropertyClass->IsChildOf(UTextBlock::StaticClass()) && !LiveFontSockets.Contains(GlyphBinding->WidgetName));
						++LiveGlyphTracks;
					}
			TestTrue(TEXT("The actual native font curves remain present"), LiveGlyphTracks > 0);
		}
	}

	const FString LocalAction = TEXT(",\"interactions\":[{\"trigger\":{\"type\":\"ON_HOVER\"},\"actions\":[{\"type\":\"NODE\",\"navigation\":\"CHANGE_TO\",\"destinationId\":\"local_b\",\"transition\":{\"type\":\"SMART_ANIMATE\",\"duration\":400,\"easing\":{\"type\":\"LINEAR\"}}}]}]");
	const auto Navigate = [](const TCHAR* Destination)
	{ return FString::Printf(TEXT(",\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"navigation\":\"NAVIGATE\",\"destinationId\":\"%s\"}]}]"), Destination); };
	const FString LocalFill = TEXT(",\"fills\":[{\"type\":\"SOLID\",\"color\":{\"r\":1,\"g\":0,\"b\":0,\"a\":1}}]");
	const auto LocalVector = [&](const TCHAR* Id, int32 X, int32 Size, const TCHAR* Fill, const TCHAR* Stroke, float Opacity)
	{
		return MakeLeafNode(Id, TEXT("LocalGlyph"), TEXT("VECTOR"), X, 5, Size, Size, LocalFill
			+ FString::Printf(TEXT(",\"opacity\":%g,\"strokeWeight\":1,\"strokeAlign\":\"CENTER\",\"strokes\":[{\"type\":\"SOLID\",\"color\":{\"r\":0,\"g\":0,\"b\":0,\"a\":1}}],\"fillGeometry\":[{\"windingRule\":\"NONZERO\",\"path\":\"%s\"}],\"strokeGeometry\":[{\"windingRule\":\"EVENODD\",\"path\":\"%s\"}]"), Opacity, Fill, Stroke));
	};
	const FString LocalChildrenA = LocalVector(TEXT("local_vector_a"), 10, 20, TEXT("M0 0L20 0L20 20L0 20Z"), TEXT("M-0.5 -0.5L20.5 -0.5L20.5 20.5L-0.5 20.5Z M0.5 0.5L0.5 19.5L19.5 19.5L19.5 0.5Z"), 0.5f)
		+ TEXT(",") + MakeTextNode(TEXT("local_text_a"), TEXT("LocalLabel"), 10, 60, 80, 40, TEXT("MOVE"),
			TEXT("\"fontFamily\":\"Roboto\",\"fontStyle\":\"Regular\",\"fontWeight\":400,\"fontSize\":24,\"letterSpacing\":0,\"lineHeightPx\":36,\"textAlignHorizontal\":\"LEFT\""), LocalFill)
		+ TEXT(",") + MakeLeafNode(TEXT("local_leave_a"), TEXT("Leave"), TEXT("RECTANGLE"), 150, 5, 20, 20, LocalFill + Navigate(TEXT("local_c")));
	const FString LocalChildrenB = LocalVector(TEXT("local_vector_b"), 60, 40, TEXT("M0 0L40 0L40 40L0 40Z"), TEXT("M-0.5 -0.5L40.5 -0.5L40.5 40.5L-0.5 40.5Z M0.5 0.5L0.5 39.5L39.5 39.5L39.5 0.5Z"), 0.25f)
		+ TEXT(",") + MakeTextNode(TEXT("local_text_b"), TEXT("LocalLabel"), 60, 60, 100, 48, TEXT("MOVE"),
			TEXT("\"fontFamily\":\"Roboto\",\"fontStyle\":\"Bold\",\"fontWeight\":700,\"fontSize\":32,\"letterSpacing\":1,\"lineHeightPx\":48,\"textAlignHorizontal\":\"CENTER\""), LocalFill)
		+ TEXT(",") + MakeLeafNode(TEXT("local_leave_b"), TEXT("Leave"), TEXT("RECTANGLE"), 150, 5, 20, 20, LocalFill + Navigate(TEXT("local_c")));
	FDesignDocument LocalDocument;
	FUMGWidgetData LocalData;
	const FString LocalBase = MakeContainerNode(TEXT("local_base"), TEXT("LocalSource"), TEXT("COMPONENT"), 0, 0, 200, 120, LocalChildrenA);
	const FString LocalDestination = MakeContainerNode(TEXT("local_b"), TEXT("LocalTarget"), TEXT("COMPONENT"), 0, 0, 200, 120, LocalChildrenB);
	const FString LocalSource = MakeContainerNode(TEXT("local_a"), TEXT("LocalSource"), TEXT("INSTANCE"), 0, 0, 200, 120, LocalChildrenA,
		TEXT(",\"componentId\":\"local_base\"") + LocalAction);
	const FString LocalJson = MakeFigmaDocJson(
		MakeContainerNode(TEXT("local_page"), TEXT("LocalPage"), TEXT("FRAME"), 0, 0, 200, 120, LocalSource)
		+ TEXT(",") + MakeContainerNode(TEXT("local_c"), TEXT("LocalReturn"), TEXT("FRAME"), 0, 0, 200, 120,
			MakeLeafNode(TEXT("local_return_surface"), TEXT("ReturnSurface"), TEXT("RECTANGLE"), 0, 0, 200, 120, LocalFill), Navigate(TEXT("local_page"))),
		FString::Printf(TEXT(",\"design2umgPrototypeDurationUnit\":\"MILLISECONDS\",\"design2umgComponentDependencies\":{\"schemaVersion\":1,\"definitions\":[{\"id\":\"local_base\",\"key\":\"local_base\",\"document\":%s},{\"id\":\"local_b\",\"key\":\"local_b\",\"document\":%s}],\"instances\":{},\"errors\":[]}"), *LocalBase, *LocalDestination));
	const bool bLocalParsed = FFigmaParser::ParseDocument(LocalJson, LocalDocument);
	if (bLocalParsed)
	{
		LocalDocument.MutableSourceDocument().PrototypeDurationUnit = TEXT("MILLISECONDS");
		LocalData = FDesign2UMGConverter::Convert(LocalDocument, FDesign2UMGConversionSettings::Defaults());
	}
	if (!TestTrue(TEXT("One mixed local endpoint SMART source converts"), bLocalParsed && LocalData.PrototypeNavigation().ClickBindings.Num() > 0)) return false;
	LocalData.WidgetName = TEXT("WBP_LocalNativeEndpoints");
	FUMGPrototypeNavigationData LocalNavigation;
	auto* LocalBlueprint = FUMGGenerator::BuildWidgetBlueprint(LocalData, Folder / TEXT("LocalEndpoints"), nullptr, nullptr, &LocalNavigation);
	FDesign2UMGGenerationOutput LocalOutput;
	if (!TestNotNull(TEXT("Mixed native endpoint blueprint builds"), LocalBlueprint)
		|| !TestTrue(TEXT("Mixed native endpoint blueprint saves"), FUMGGenerator::SaveWidgetBlueprint(LocalBlueprint, &LocalOutput, &LocalNavigation).IsSuccess())
		|| !TestFalse(TEXT("Mixed endpoint actions have no skipped wiring"), LocalOutput.bHasDegradation)) return false;
	const auto* LocalBinding = LocalNavigation.ClickBindings.FindByPredicate([](const FUMGPrototypeNavigationBinding& Binding)
		{ return Binding.TriggerWidgetId == TEXT("local_a"); });
	if (!TestTrue(TEXT("One SMART retains its vector and native text endpoints"), LocalBinding && LocalBinding->PrimaryStep().Transition.Type == TEXT("SMART_ANIMATE")
		&& LocalBinding->PrimaryStep().Transition.LocalEndpoints.Num() == 2 && LocalNavigation.StateHosts.Num() == 1)) return false;
	UWorld* LocalWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Local native playback world exists"), LocalWorld)) return false;
	TStrongObjectPtr<UUserWidget> LocalInstance(CreateWidget<UUserWidget>(LocalWorld, LocalBlueprint->GeneratedClass.Get()));
	if (!TestNotNull(TEXT("Local native playback instance creates"), LocalInstance.Get())) return false;
	TSharedPtr<SWidget> LocalSlate = LocalInstance->TakeWidget();
	TFunction<UWidget*(const FUMGWidgetNode&, UWidget*, const FString&)> Resolve = [&](const FUMGWidgetNode& Model, UWidget* Widget, const FString& Id) -> UWidget*
	{
		if (Model.Id == Id) return Widget;
		auto* Panel = Cast<UPanelWidget>(Widget);
		if (!Panel || Panel->GetChildrenCount() != Model.Children.Num()) return nullptr;
		for (int32 Index = 0; Index < Model.Children.Num(); ++Index)
			if (auto* Found = Resolve(Model.Children[Index], Panel->GetChildAt(Index), Id)) return Found;
		return nullptr;
	};
	const auto RuntimeWidget = [&](const FString& Id) { return Resolve(LocalData.RootWidget, LocalInstance->WidgetTree->RootWidget, Id); };
	const auto* VectorEndpoint = LocalBinding->PrimaryStep().Transition.LocalEndpoints.FindByPredicate([](const FUMGPrototypeLocalEndpoint& Endpoint) { return Endpoint.bFixedPaintSize; });
	const auto* TextEndpoint = LocalBinding->PrimaryStep().Transition.LocalEndpoints.FindByPredicate([](const FUMGPrototypeLocalEndpoint& Endpoint) { return Endpoint.bText; });
	if (!TestTrue(TEXT("Both typed local roles exist"), VectorEndpoint && TextEndpoint)) return false;
	const auto* VectorPaintModel = FindIrNodeById(LocalData.RootWidget, VectorEndpoint->SourceVisualWidgetId);
	TestTrue(TEXT("Static vector paint inputs do not bake the original node opacity"), VectorPaintModel
		&& VectorPaintModel->Children.Num() == 2
		&& VectorPaintModel->Children[0].Path.FillColor.Get(FLinearColor::Transparent).A == 1.f
		&& VectorPaintModel->Children[1].Brush.BrushColor.Get(FLinearColor::Transparent).A == 1.f);
	auto* Outgoing = RuntimeWidget(VectorEndpoint->SourcePresentationWidgetId);
	auto* Incoming = RuntimeWidget(VectorEndpoint->TargetPresentationWidgetId);
	auto* OutgoingText = Cast<UTextBlock>(RuntimeWidget(TextEndpoint->SourcePresentationWidgetId));
	auto* IncomingText = Cast<UTextBlock>(RuntimeWidget(TextEndpoint->TargetPresentationWidgetId));
	auto* CanonicalTextA = Cast<UTextBlock>(RuntimeWidget(TextEndpoint->SourceVisualWidgetId));
	auto* CanonicalTextB = Cast<UTextBlock>(RuntimeWidget(TextEndpoint->DestinationWidgetId));
	const auto* Placement = LocalBinding->PrimaryStep().Transition.LayoutTargets.FindByPredicate([](const FUMGPrototypeLayoutTarget& Item) { return Item.NodeId == TEXT("local_vector_a"); });
	auto* Painted = Placement ? RuntimeWidget(Placement->PresentationWidgetId) : nullptr;
	auto* PaintSlot = Painted ? Cast<UCanvasPanelSlot>(Painted->Slot) : nullptr;
	auto* Hover = Cast<UButton>(RuntimeWidget(TEXT("local_a")));
	auto* Leave = Cast<UButton>(RuntimeWidget(TEXT("local_leave_a")));
	auto* Return = Cast<UButton>(RuntimeWidget(TEXT("local_c")));
	const auto* AnimationProperty = FindFProperty<FObjectProperty>(LocalInstance->GetClass(), FName(*LocalBinding->PrimaryStep().Transition.WidgetAnimationName));
	auto* Runtime = AnimationProperty ? Cast<UWidgetAnimation>(AnimationProperty->GetObjectPropertyValue_InContainer(LocalInstance.Get())) : nullptr;
	if (!TestTrue(TEXT("Native local paint, glyph, slot, input and animation resolve"), Outgoing && Incoming && OutgoingText && IncomingText && CanonicalTextA && CanonicalTextB && PaintSlot && Hover && Leave && Return && Runtime)) return false;
	const FVector2D Viewport(200, 120);
	TStrongObjectPtr<UTextureRenderTarget2D> LocalTarget(FWidgetRenderer::CreateTargetFor(Viewport, TF_Nearest, true));
	FWidgetRenderer LocalRenderer(false);
	ON_SCOPE_EXIT
	{
		LocalInstance->ReleaseSlateResources(true); LocalSlate.Reset(); LocalInstance.Reset();
		DeleteGeneratedAssetsAtPath(Folder / TEXT("LocalEndpoints"));
	};
	const auto Draw = [&]() { LocalInstance->ForceLayoutPrepass(); LocalRenderer.DrawWidget(LocalTarget.Get(), LocalSlate.ToSharedRef(), 1, Viewport, 0); FlushRenderingCommands(); };
	const auto TickLocal = [&](float Seconds)
	{
		++GFrameCounter;
		UUMGSequenceTickManager::Get(LocalInstance.Get())->OnWidgetTicked(LocalInstance.Get());
		FSlateApplication::Get().OnPreTick().Broadcast(Seconds);
		LocalWorld->GetLatentActionManager().BeginFrame();
		LocalWorld->GetLatentActionManager().ProcessLatentActions(LocalInstance.Get(), Seconds);
		LocalWorld->GetTimerManager().Tick(Seconds);
		FSlateApplication::Get().OnPostTick().Broadcast(Seconds);
	};
	TickLocal(0); Draw();
	Hover->OnHovered.Broadcast(); TickLocal(0); TickLocal(0.1f); Draw();
	const float Progress = LocalInstance->GetAnimationCurrentTime(Runtime) / LocalBinding->PrimaryStep().Transition.DurationSeconds;
	AddInfo(FString::Printf(TEXT("Local native playback time=%g progress=%g sourceOpacity=%g targetOpacity=%g slotX=%g width=%g paintWidths=%g/%g"),
		LocalInstance->GetAnimationCurrentTime(Runtime), Progress, Outgoing->GetRenderOpacity(), Incoming->GetRenderOpacity(), PaintSlot->GetOffsets().Left,
		PaintSlot->GetOffsets().Right, Outgoing->GetCachedGeometry().GetLocalSize().X, Incoming->GetCachedGeometry().GetLocalSize().X));
	TestTrue(TEXT("Local endpoints actually advance on the native clock"), LocalInstance->IsAnimationPlaying(Runtime) && Progress > 0.05f && Progress < 0.95f);
	TestTrue(TEXT("Both native paint opacities evaluate the same source progress"), FMath::IsNearlyEqual(Outgoing->GetRenderOpacity(), 1-Progress, 0.01f)
		&& FMath::IsNearlyEqual(Incoming->GetRenderOpacity(), Progress, 0.01f)
		&& FMath::IsNearlyEqual(OutgoingText->GetRenderOpacity(), 1-Progress, 0.01f) && FMath::IsNearlyEqual(IncomingText->GetRenderOpacity(), Progress, 0.01f));
	TestTrue(TEXT("Common slot moves while static vector paints keep physical logical dimensions"),
		FMath::IsNearlyEqual(PaintSlot->GetOffsets().Left, FMath::Lerp(9.5f,59.5f,Progress), 0.05f)
		&& FMath::IsNearlyEqual(Outgoing->GetCachedGeometry().GetLocalSize().X, 21.f, 0.05f)
		&& FMath::IsNearlyEqual(Incoming->GetCachedGeometry().GetLocalSize().X, 41.f, 0.05f));
	const float ExpectedFont = FMath::Lerp(CanonicalTextA->GetFont().Size, CanonicalTextB->GetFont().Size, Progress);
	TestTrue(TEXT("Both native text faces keep the size curve and their own typeface"), FMath::IsNearlyEqual(OutgoingText->GetFont().Size, ExpectedFont, 0.05f)
		&& FMath::IsNearlyEqual(IncomingText->GetFont().Size, ExpectedFont, 0.05f)
		&& OutgoingText->GetFont().TypefaceFontName == CanonicalTextA->GetFont().TypefaceFontName
		&& IncomingText->GetFont().TypefaceFontName == CanonicalTextB->GetFont().TypefaceFontName);
	TestTrue(TEXT("Original vector node opacity is applied once on the common owner"), FMath::IsNearlyEqual(Painted->GetRenderOpacity(), FMath::Lerp(0.5f,0.25f,Progress),0.01f));
	LocalInstance->SetAnimationCurrentTime(Runtime,0); TickLocal(0); LocalInstance->FlushAnimations(); Draw();
	TestTrue(TEXT("Native seek zero restores source and collapses incoming paint"), FMath::IsNearlyZero(LocalInstance->GetAnimationCurrentTime(Runtime))
		&& FMath::IsNearlyEqual(Outgoing->GetRenderOpacity(),1.f)
		&& FMath::IsNearlyZero(Incoming->GetRenderOpacity()) && Incoming->GetVisibility() == ESlateVisibility::Collapsed);
	Leave->OnClicked.Broadcast(); TickLocal(0); Draw();
	TestEqual(TEXT("Normal navigation cancels the local native playback"), CurrentPageIndex(LocalInstance.Get()), 1);
	Return->OnClicked.Broadcast(); TickLocal(0); Draw();
	TestEqual(TEXT("Returning restores the real source page"), CurrentPageIndex(LocalInstance.Get()), 0);
	TestTrue(TEXT("Cancel restores local paint and common node opacity before reentry"), FMath::IsNearlyEqual(Outgoing->GetRenderOpacity(),1.f)
		&& FMath::IsNearlyZero(Incoming->GetRenderOpacity()) && FMath::IsNearlyEqual(Painted->GetRenderOpacity(),0.5f));
	Hover->OnHovered.Broadcast(); TickLocal(0); TickLocal(0.1f); Draw();
	TestTrue(TEXT("Reentry starts another real local endpoint playback"), LocalInstance->IsAnimationPlaying(Runtime)
		&& Outgoing->GetRenderOpacity() > 0 && Outgoing->GetRenderOpacity() < 1 && Incoming->GetRenderOpacity() > 0);
	Hover->OnUnhovered.Broadcast();
	for (int32 Frame = 0; Frame < 12; ++Frame) TickLocal(0.05f);
	Draw();
	TestTrue(TEXT("Reverse return restores source glyph and paint without a second node opacity"), !LocalInstance->IsAnimationPlaying(Runtime)
		&& FMath::IsNearlyEqual(Outgoing->GetRenderOpacity(),1.f) && FMath::IsNearlyZero(Incoming->GetRenderOpacity())
		&& FMath::IsNearlyEqual(OutgoingText->GetRenderOpacity(),1.f) && FMath::IsNearlyZero(IncomingText->GetRenderOpacity())
		&& FMath::IsNearlyEqual(Painted->GetRenderOpacity(),0.5f));

	AddInfo(TEXT("Retained one native WBP plus its generation sidecar for NativeWidgetAnimationReload in a fresh editor process"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaNativeWidgetAnimationNavigationLifecycleTest,
	"Design2UMG.WidgetBlueprint.PrototypeGraphWiring.NativeWidgetAnimationNavigationLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaNativeWidgetAnimationNavigationLifecycleTest::RunTest(const FString&)
{
	for (int32 PlaybackKind = 0; PlaybackKind < 3; ++PlaybackKind)
	{
		const bool bAutomatic = PlaybackKind == 1;
		const bool bPropertyOnly = PlaybackKind == 2;
		const FString SmartAction = FString::Printf(TEXT("{\"type\":\"NODE\",\"navigation\":\"%s\",\"destinationId\":\"%s\",\"transition\":{\"type\":\"SMART_ANIMATE\",\"duration\":400,\"easing\":{\"type\":\"LINEAR\"}}}"),
			bPropertyOnly ? TEXT("CHANGE_TO") : TEXT("NAVIGATE"), bPropertyOnly ? TEXT("target") : TEXT("B"));
		const auto Page = [&](const TCHAR* Id, const TCHAR* Exit, bool bSource, float Opacity)
		{
			const FString Paint = FString::Printf(TEXT(",\"fills\":[%s]"), *MakeSolidFillJson(0.2, 0.4, 0.6));
			const FString Smart = MakeLeafNode(*(FString(Id) + TEXT("_smart")), TEXT("Smart"), bPropertyOnly && bSource ? TEXT("INSTANCE") : TEXT("RECTANGLE"), 0, 0, 40, 20,
				Paint + FString::Printf(TEXT(",\"opacity\":%s"), *FString::SanitizeFloat(Opacity))
				+ (bPropertyOnly && bSource ? TEXT(",\"componentId\":\"base\"") : TEXT(""))
				+ (bSource ? FString::Printf(TEXT(",\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[%s]}]"), *SmartAction) : FString()));
			const FString Leave = MakeLeafNode(*(FString(Id) + TEXT("_leave")), TEXT("Leave"), TEXT("RECTANGLE"), 50, 0, 40, 20,
				Paint + FString::Printf(TEXT(",\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"navigation\":\"NAVIGATE\",\"destinationId\":\"%s\"}]}]"), Exit));
			FString Extra;
			if (bSource && bAutomatic) Extra += FString::Printf(TEXT(",\"interactions\":[{\"trigger\":{\"type\":\"AFTER_TIMEOUT\",\"timeout\":100},\"actions\":[%s]}]"), *SmartAction);
			return MakeContainerNode(Id, Id, TEXT("FRAME"), 0, 0, 100, 40, Smart + TEXT(",") + Leave, Extra);
		};
		FString DocumentExtra = TEXT(",\"fileKey\":\"native-animation-lifecycle\",\"design2umgPrototypeDurationUnit\":\"MILLISECONDS\"");
		if (bPropertyOnly)
		{
			const FString Target = MakeLeafNode(TEXT("target"), TEXT("Smart"), TEXT("COMPONENT"), 0, 0, 40, 20,
				FString::Printf(TEXT(",\"opacity\":0.5,\"fills\":[%s]"), *MakeSolidFillJson(0.2, 0.4, 0.6)));
			DocumentExtra += FString::Printf(TEXT(",\"design2umgComponentDependencies\":{\"schemaVersion\":1,\"definitions\":[{\"id\":\"target\",\"key\":\"target\",\"document\":%s}],\"instances\":{},\"errors\":[]}"), *Target);
		}
		const FString Json = MakeFigmaDocJson(Page(TEXT("A"), TEXT("C"), true, 1) + TEXT(",")
			+ Page(TEXT("B"), TEXT("C"), false, 0.5f) + TEXT(",") + Page(TEXT("C"), TEXT("A"), false, 1),
			DocumentExtra);
		FDesignDocument Document;
		FUMGWidgetData Data;
		if (!TestTrue(TEXT("Lifecycle source converts"), ConvertPrototype(Json, Document, Data))) return false;
		Data.WidgetName = bPropertyOnly ? TEXT("WBP_PropertyAnimationLifecycle")
			: bAutomatic ? TEXT("WBP_AutomaticAnimationLifecycle") : TEXT("WBP_ClickAnimationLifecycle");
		const FString Folder = TEXT("/Game/Design2UMG/_TestOutput/NativeAnimationLifecycle") / Data.WidgetName;
		DeleteGeneratedAssetsAtPath(Folder);
		FUMGPrototypeNavigationData Navigation;
		auto* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, Folder, nullptr, nullptr, &Navigation);
		if (!TestNotNull(TEXT("Lifecycle blueprint builds"), Blueprint)) return false;
		FDesign2UMGGenerationOutput Output;
		if (!TestTrue(TEXT("Lifecycle blueprint compiles and saves"), FUMGGenerator::SaveWidgetBlueprint(Blueprint, &Output, &Navigation).IsSuccess())) return false;
		for (const auto& Diagnostic : Output.MappingDiagnostics) AddInfo(Diagnostic.Message);
		if (!TestFalse(TEXT("Lifecycle source has no skipped native actions"), Output.bHasDegradation)) return false;
		if (bPropertyOnly)
		{
			bool bSchedulesStateCompletion = false;
			bool bClearsStateCompletion = false;
			TArray<UEdGraph*> Graphs;
            Blueprint->GetAllGraphs(Graphs);
            for (const UEdGraph* Graph : Graphs)
				for (const UEdGraphNode* Node : Graph->Nodes)
					if (const auto* Call = Cast<UK2Node_CallFunction>(Node))
					{
						const UEdGraphPin* FunctionName = Call->FindPin(TEXT("FunctionName"));
						if (!FunctionName || !FunctionName->DefaultValue.StartsWith(TEXT("__d2u_state_"))) continue;
						bSchedulesStateCompletion |= Call->FunctionReference.GetMemberName() == TEXT("K2_SetTimer");
						bClearsStateCompletion |= Call->FunctionReference.GetMemberName() == TEXT("K2_ClearTimer");
					}
			TestTrue(TEXT("Property-only transition schedules and cancels its own state completion"),
				bSchedulesStateCompletion && bClearsStateCompletion);
		}
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!TestNotNull(TEXT("Lifecycle contract has an editor world"), World) || !FSlateApplication::IsInitialized()) return false;
		UUserWidget* Instance = CreateWidget<UUserWidget>(World, Blueprint->GeneratedClass.Get());
		if (!TestNotNull(TEXT("Lifecycle instance creates"), Instance)) return false;
		Instance->AddToRoot();
		TSharedPtr<SWidget> SlateWidget = Instance->TakeWidget();
		FTimerHandle UserTimer = UKismetSystemLibrary::K2_SetTimer(Instance, TEXT("InvalidateLayoutAndVolatility"), 10, false);
		ON_SCOPE_EXIT
		{
			World->GetTimerManager().ClearTimer(UserTimer);
			SlateWidget.Reset();
			Instance->RemoveFromRoot();
			DeleteGeneratedAssetsAtPath(Folder);
		};
		auto* PageHost = Cast<UCanvasPanel>(Instance->WidgetTree->FindWidget(FName(*Navigation.PageHostWidgetName)));
		if (!TestNotNull(TEXT("Lifecycle uses the generated native page host"), PageHost)) return false;
		const auto Click = [&](const TCHAR* Name)
		{
			auto* Button = FindTriggerButton(Instance->WidgetTree, Navigation, Name);
			if (!TestNotNull(TEXT("Generated native navigation button resolves"), Button)) return false;
			Button->OnClicked.Broadcast();
			return true;
		};
		const auto Tick = [&](float Seconds)
		{
			++GFrameCounter;
			UUMGSequenceTickManager::Get(Instance)->OnWidgetTicked(Instance);
			FSlateApplication::Get().OnPreTick().Broadcast(Seconds);
			World->GetTimerManager().Tick(Seconds);
			FSlateApplication::Get().OnPostTick().Broadcast(Seconds);
		};
		Tick(0.01f);
		if (bAutomatic) Tick(0.11f);
		else if (!Click(TEXT("A_smart"))) return false;
		const int32 AnimationIndex = 0;
		const auto* Property = FindFProperty<FObjectProperty>(Blueprint->GeneratedClass, FName(*Navigation.ClickBindings[AnimationIndex].PrimaryStep().Transition.WidgetAnimationName));
		auto* Animation = Property ? Cast<UWidgetAnimation>(Property->GetObjectPropertyValue_InContainer(Instance)) : nullptr;
		TestTrue(TEXT("Current entry starts native property playback"), Animation && Instance->IsAnimationPlaying(Animation));
		Tick(0.1f);
		if (!Click(TEXT("A_leave"))) return false;
		TestEqual(TEXT("Another normal navigation leaves the source page"), CurrentPageIndex(Instance), 2);
		TestTrue(TEXT("Leaving stops only the pending generated animation"), Animation && !Instance->IsAnimationPlaying(Animation));
		Tick(0.11f);
		if (!Click(TEXT("C_leave"))) return false;
		if (!bAutomatic && !Click(TEXT("A_smart"))) return false;
		if (bPropertyOnly) TestTrue(TEXT("Reentry starts a new property-only playback"), Animation && Instance->IsAnimationPlaying(Animation));
		Tick(0.11f);
		Tick(0.11f);
		TestEqual(TEXT("Old completion stays invalid after leaving and reentering the same page"), CurrentPageIndex(Instance), 0);
		for (int32 Frame = 0; Frame < 8; ++Frame) Tick(0.05f);
		if (bPropertyOnly)
		{
			TestEqual(TEXT("Property-only playback never navigates"), CurrentPageIndex(Instance), 0);
			TestTrue(TEXT("Reentered property-only playback completes"), Animation && !Instance->IsAnimationPlaying(Animation));
		}
		else TestEqual(TEXT("Only the newly entered action eventually navigates"), CurrentPageIndex(Instance), 1);
		TestTrue(TEXT("Page cancellation preserves an unrelated user timer"), World->GetTimerManager().IsTimerActive(UserTimer));
		if (!Click(bPropertyOnly ? TEXT("A_leave") : TEXT("B_leave")) || !Click(TEXT("C_leave")) || !Click(TEXT("A_smart"))) return false;
		SlateWidget.Reset();
		TestTrue(TEXT("Real native destruction stops generated animation playback"), Animation && !Instance->IsAnimationPlaying(Animation));
		for (int32 Index = 0; Index < Navigation.ClickBindings.Num(); ++Index)
		{
			const auto& Binding = Navigation.ClickBindings[Index];
			if (Binding.PrimaryStep().Transition.WidgetAnimationName.IsEmpty()) continue;
			const auto* AnimationProperty = FindFProperty<FObjectProperty>(Blueprint->GeneratedClass, FName(*Binding.PrimaryStep().Transition.WidgetAnimationName));
			auto* OwnedAnimation = AnimationProperty ? Cast<UWidgetAnimation>(AnimationProperty->GetObjectPropertyValue_InContainer(Instance)) : nullptr;
			TestTrue(TEXT("Destruction stops every generated property animation"), OwnedAnimation && !Instance->IsAnimationPlaying(OwnedAnimation));
			if (Binding.UsesPageOpenTrigger()) TestFalse(TEXT("Destruction clears owned automatic start timer"),
				UKismetSystemLibrary::K2_IsTimerActive(Instance, FString::Printf(TEXT("__d2u_anim_%03d_play"), Index)));
			if (Binding.Steps[0].Kind == EUMGPrototypeNavigationStepKind::Navigate) TestFalse(TEXT("Destruction clears owned completion timer"),
				UKismetSystemLibrary::K2_IsTimerActive(Instance, FString::Printf(TEXT("__d2u_anim_%03d_navigate"), Index)));
		}
		TestTrue(TEXT("Native destruction does not cancel unrelated user logic"), World->GetTimerManager().IsTimerActive(UserTimer));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaNativeWidgetAnimationReloadTest,
	"Design2UMG.Manual.NativeWidgetAnimationReload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaNativeWidgetAnimationReloadTest::RunTest(const FString&)
{
	const TCHAR* Asset = TEXT("/Game/Design2UMG/_TestOutput/NativeWidgetAnimationContract/WBP_NativeWidgetAnimationContract.WBP_NativeWidgetAnimationContract");
	if (!TestNull(TEXT("Reload contract requires a fresh editor process after NativeWidgetAnimationBuild"), FindObject<UWidgetBlueprint>(nullptr, Asset))) return false;
	auto* Blueprint = LoadObject<UWidgetBlueprint>(nullptr, Asset);
	if (!TestNotNull(TEXT("Saved native animation asset reloads from disk"), Blueprint)) return false;
	TestEqual(TEXT("All four timelines survive disk reload"), Blueprint->Animations.Num(), 4);
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	UUserWidget* Instance = World ? CreateWidget<UUserWidget>(World, Blueprint->GeneratedClass.Get()) : nullptr;
	if (!TestNotNull(TEXT("Reloaded generated class instantiates"), Instance) || !FSlateApplication::IsInitialized()) return false;
	Instance->AddToRoot();
	const auto Window = SNew(SWindow).Title(FText::FromString(TEXT("Native widget animation input contract")))
		.ClientSize(FVector2D(600, 160)).FocusWhenFirstShown(false).SupportsMaximize(false).SupportsMinimize(false)
		[SNew(SBox).WidthOverride(580).HeightOverride(120)[Instance->TakeWidget()]];
	FSlateApplication::Get().AddWindow(Window, true);
	ON_SCOPE_EXIT
	{
		FSlateApplication::Get().RequestDestroyWindow(Window);
		Instance->RemoveFromRoot();
	};
	Window->SlatePrepass();
	FSlateApplication::Get().Tick();
	FSlateApplication::Get().Tick();
	TSharedPtr<FSlateVirtualUserHandle> UserHandle;
	int32 VirtualIndex = 0;
	do { UserHandle = FSlateApplication::Get().FindOrCreateVirtualUser(VirtualIndex++); } while (!UserHandle.IsUnique());
	const int32 UserIndex = UserHandle->GetUserIndex();
	const auto User = FSlateApplication::Get().GetUser(UserIndex);
	const auto AnimationAt = [&](int32 Index) -> UWidgetAnimation*
	{
		const FName Name(*FString::Printf(TEXT("__d2u_nav_anim_%03d_properties"), Index));
		const auto* Property = FindFProperty<FObjectProperty>(Blueprint->GeneratedClass, Name);
		return Property ? Cast<UWidgetAnimation>(Property->GetObjectPropertyValue_InContainer(Instance)) : nullptr;
	};
	const auto SurfaceAt = [&](int32 Index) -> UBorder*
	{
		auto* Animation = AnimationAt(Index);
		if (!Animation) return nullptr;
		UBorder* Result = nullptr;
		for (const auto& Binding : Animation->AnimationBindings)
		{
			auto* Candidate = Cast<UBorder>(Instance->WidgetTree->FindWidget(Binding.WidgetName));
			if (!Candidate || !HasColorTrackForWidget(Animation, Binding.WidgetName)
				|| !FindTransformForWidget(Animation, Binding.WidgetName)
				|| !FindOpacityChannelForWidget(Animation, Binding.WidgetName)) continue;
			if (Result) return nullptr;
			Result = Candidate;
		}
		return Result;
	};
	const auto Route = [&](int32 Index, bool bClick)
	{
		auto* Button = Cast<UButton>(Instance->WidgetTree->FindWidget(TriggerButtonName(Instance->WidgetTree, FString::Printf(TEXT("instance_%d"), Index))));
		const auto Control = Button ? Button->GetCachedWidget() : TSharedPtr<SWidget>();
		if (!TestTrue(TEXT("Actual native button has live Slate geometry"), Control.IsValid())) return false;
		const auto& Geometry = Control->GetCachedGeometry();
		const FVector2D Center = Geometry.LocalToAbsolute(Geometry.GetLocalSize() * 0.5);
		const TArray<TSharedRef<SWindow>> Windows{Window};
		const FWidgetPath Path = FSlateApplication::Get().LocateWindowUnderMouse(Center, Windows, false, UserIndex);
		if (!TestTrue(TEXT("Native hit test reaches the exact button in this test window"), Path.IsValid()
			&& Path.GetWindow() == Window && Path.ContainsWidget(Control.Get()))) return false;
		const FPointerEvent Move(UserIndex, FSlateApplication::CursorPointerIndex, Center, Center, {}, FKey(), 0, FModifierKeysState());
		FSlateApplication::Get().RoutePointerMoveEvent(Path, Move, true);
		if (!bClick) return true;
		const TSet<FKey> Pressed{EKeys::LeftMouseButton};
		const FPointerEvent Down(UserIndex, FSlateApplication::CursorPointerIndex, Center, Center, Pressed, EKeys::LeftMouseButton, 0, FModifierKeysState());
		const FPointerEvent Up(UserIndex, FSlateApplication::CursorPointerIndex, Center, Center, {}, EKeys::LeftMouseButton, 0, FModifierKeysState());
		const bool bDown = FSlateApplication::Get().RoutePointerDownEvent(Path, Down).IsEventHandled();
		TestTrue(TEXT("Native input captures only the target window"), User->GetCaptorPath(FSlateApplication::CursorPointerIndex).GetWindow() == Window);
		const bool bUp = FSlateApplication::Get().RoutePointerUpEvent(Path, Up).IsEventHandled();
		return bDown && bUp && !User->HasCapture(FSlateApplication::CursorPointerIndex);
	};
	const auto Tick = [&](float Seconds)
	{
		++GFrameCounter;
		UUMGSequenceTickManager::Get(Instance)->OnWidgetTicked(Instance);
		FSlateApplication::Get().OnPreTick().Broadcast(Seconds);
		World->GetLatentActionManager().BeginFrame();
		World->GetLatentActionManager().ProcessLatentActions(Instance, Seconds);
		World->GetTimerManager().Tick(Seconds);
		FSlateApplication::Get().OnPostTick().Broadcast(Seconds);
	};
	TestTrue(TEXT("Real Slate click is handled"), Route(0, true));
	TestTrue(TEXT("Native click starts its reloaded animation"), AnimationAt(0) && Instance->IsAnimationPlaying(AnimationAt(0)));
	TestTrue(TEXT("Real Slate move enters the second instance"), Route(1, false));
	TestTrue(TEXT("MOUSE_ENTER starts its reloaded animation"), AnimationAt(1) && Instance->IsAnimationPlaying(AnimationAt(1)));
	Tick(0.1f);
	Tick(0.1f);
	auto* Surface = SurfaceAt(0);
	if (!TestNotNull(TEXT("Animated native Border resolved from persisted binding"), Surface)) return false;
	const FLinearColor EffectiveColor = Surface->GetBrushColor() * Surface->Background.TintColor.GetSpecifiedColor();
	TestTrue(TEXT("Actual native color changes without multiplying by the original red tint"), EffectiveColor.B > 0 && EffectiveColor.R < 1);
	TestTrue(TEXT("Actual native transform has an intermediate value"), Surface->GetRenderTransform().Angle > 0 && Surface->GetRenderTransform().Angle < 90);
	TestTrue(TEXT("Source AFTER_TIMEOUT starts automatically without any pointer input"), AnimationAt(2) && Instance->IsAnimationPlaying(AnimationAt(2)));
	TestTrue(TEXT("Real Slate move enters reverting ON_HOVER"), Route(3, false));
	Tick(0.1f);
	Tick(0.1f);
	TestTrue(TEXT("Leaving MOUSE_ENTER does not reverse it"), SurfaceAt(1) && SurfaceAt(1)->GetRenderTransform().Angle > 0);
	TestTrue(TEXT("Real Slate move leaves the hovering instance"), Route(0, false));
	for (int32 Frame = 0; Frame < 10; ++Frame) Tick(0.1f);
	auto* SourceState = Instance->WidgetTree->FindWidget(TriggerButtonName(Instance->WidgetTree, TEXT("instance_0")));
	auto* StateHost = SourceState ? SourceState->GetParent() : nullptr;
	if (!TestNotNull(TEXT("Completed source state has its native state host"), StateHost)
		|| !TestEqual(TEXT("Click fixture retains both concrete states"), StateHost->GetChildrenCount(), 2)) return false;
	UWidget* TargetState = StateHost->GetChildAt(0) == SourceState ? StateHost->GetChildAt(1) : StateHost->GetChildAt(0);
	TestEqual(TEXT("Completed transition hides its source state"), SourceState->GetVisibility(), ESlateVisibility::Collapsed);
	TestTrue(TEXT("Completed transition displays the concrete target state"), TargetState->GetVisibility() != ESlateVisibility::Collapsed
		&& TargetState->GetVisibility() != ESlateVisibility::Hidden);
	const FName StatePropertyName(*FString::Printf(TEXT("__d2u_state_%08x"), FCrc::StrCrc32(TEXT("instance_0__state"))));
	const auto* StateProperty = FindFProperty<FIntProperty>(Instance->GetClass(), StatePropertyName);
	if (!TestNotNull(TEXT("Persisted current-state property exists"), StateProperty)) return false;
	TestEqual(TEXT("Click commits the target state"), StateProperty->GetPropertyValue_InContainer(Instance), 1);
	TestTrue(TEXT("Hidden source opacity restores after the transition"), FMath::IsNearlyEqual(Surface->GetRenderOpacity(), 1.0f, 0.01f));
	TestTrue(TEXT("Hidden source color restores after the transition"), (Surface->GetBrushColor() * Surface->Background.TintColor.GetSpecifiedColor()).Equals(FLinearColor::Red, 0.01f));
	TestTrue(TEXT("Hidden source transform restores after the transition"), Surface->GetRenderTransform().Scale.Equals(FVector2D(1, 1), 0.01)
		&& FMath::IsNearlyZero(Surface->GetRenderTransform().Angle, 0.01f));
	TArray<UBorder*> TargetSurfaces;
	UWidgetTree::ForWidgetAndChildren(TargetState, [&](UWidget* Widget)
	{
		if (auto* Border = Cast<UBorder>(Widget)) TargetSurfaces.Add(Border);
	});
	if (!TestEqual(TEXT("Concrete target contains one native paint surface"), TargetSurfaces.Num(), 1)) return false;
	const auto* TargetSurface = TargetSurfaces[0];
	TestTrue(TEXT("Visible target retains the endpoint opacity"), FMath::IsNearlyEqual(TargetSurface->GetRenderOpacity(), 0.4f, 0.01f));
	TestTrue(TEXT("Visible target retains the blue endpoint"), (TargetSurface->GetBrushColor() * TargetSurface->Background.TintColor.GetSpecifiedColor()).Equals(FLinearColor::Blue, 0.01f));
	const auto* TargetSlot = Cast<UCanvasPanelSlot>(TargetSurface->Slot);
	if (!TestNotNull(TEXT("Visible target surface has its native layout slot"), TargetSlot)) return false;
	TestTrue(TEXT("Visible target uses the declared endpoint dimensions"), TargetSlot->GetSize().Equals(FVector2D(80, 40), 0.01));
	TestTrue(TEXT("Visible target retains its endpoint rotation without transition scaling"), FMath::IsNearlyEqual(TargetSurface->GetRenderTransform().Angle, 90.0f, 0.01f)
		&& TargetSurface->GetRenderTransform().Scale.Equals(FVector2D(1, 1), 0.01));
	TestTrue(TEXT("ON_HOVER reverses to source state after leaving"), SurfaceAt(3) && FMath::IsNearlyZero(SurfaceAt(3)->GetRenderTransform().Angle, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSavedPrototypeGraphReferenceDiagnostic,
    "Design2UMG.Manual.SavedPrototypeGraphReferences",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSavedPrototypeGraphReferenceDiagnostic::RunTest(const FString& Parameters)
{
    FString AssetPath;
    if (!FParse::Value(FCommandLine::Get(), TEXT("D2UNativeAnimationAsset="), AssetPath))
    {
        AddError(TEXT("Graph reference diagnosis requires -D2UNativeAnimationAsset=<saved WBP object path>"));
        return false;
    }
    auto* Blueprint = LoadObject<UWidgetBlueprint>(nullptr, *AssetPath);
    if (!TestNotNull(TEXT("Load requested saved WidgetBlueprint"), Blueprint)) return false;
    UClass* NativeParent = Blueprint->ParentClass;
    while (NativeParent && !NativeParent->HasAnyClassFlags(CLASS_Native)) NativeParent = NativeParent->GetSuperClass();
    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    TSet<FName> CustomEvents;
    for (const UEdGraph* Graph : Graphs)
        for (const UEdGraphNode* Node : Graph->Nodes)
            if (const auto* Event = Cast<UK2Node_CustomEvent>(Node)) CustomEvents.Add(Event->CustomFunctionName);
    const auto DescribeNode = [](const UEdGraphNode* Node)
    {
        auto Value = MakeShared<FJsonObject>();
        Value->SetStringField(TEXT("node"), Node->GetPathName());
        Value->SetStringField(TEXT("guid"), Node->NodeGuid.ToString());
        Value->SetStringField(TEXT("class"), Node->GetClass()->GetPathName());
        if (const auto* Call = Cast<UK2Node_CallFunction>(Node))
        {
            Value->SetStringField(TEXT("function"), Call->FunctionReference.GetMemberName().ToString());
            Value->SetStringField(TEXT("member_guid"), Call->FunctionReference.GetMemberGuid().ToString());
            Value->SetBoolField(TEXT("self_context"), Call->FunctionReference.IsSelfContext());
            Value->SetStringField(TEXT("member_parent"), GetPathNameSafe(Call->FunctionReference.GetMemberParentClass()));
        }
        if (const auto* Variable = Cast<UK2Node_Variable>(Node))
            Value->SetStringField(TEXT("variable"), Variable->GetVarName().ToString());
        return Value;
    };
    const auto ReadPins = [&DescribeNode](const UEdGraphNode* Node)
    {
        TArray<TSharedPtr<FJsonValue>> Pins;
        for (const UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin->Direction != EGPD_Input && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec) continue;
            auto Value = MakeShared<FJsonObject>();
            Value->SetStringField(TEXT("pin"), Pin->PinName.ToString());
            Value->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());
            Value->SetBoolField(TEXT("input"), Pin->Direction == EGPD_Input);
            Value->SetStringField(TEXT("default_value"), Pin->DefaultValue);
            Value->SetStringField(TEXT("default_object"), GetPathNameSafe(Pin->DefaultObject));
            TArray<TSharedPtr<FJsonValue>> Links;
            for (const UEdGraphPin* Linked : Pin->LinkedTo)
            {
                auto Link = DescribeNode(Linked->GetOwningNode());
                Link->SetStringField(TEXT("pin"), Linked->PinName.ToString());
                Links.Add(MakeShared<FJsonValueObject>(Link));
            }
            Value->SetArrayField(TEXT("links"), Links);
            Pins.Add(MakeShared<FJsonValueObject>(Value));
        }
        return Pins;
    };
    const auto Emit = [this](const TCHAR* Kind, const TSharedRef<FJsonObject>& Value)
    {
        FString Json;
        const TSharedPtr<FJsonObject> Object = Value;
        FJsonSerializer::Serialize(Object, TJsonWriterFactory<>::Create(&Json));
        AddInfo(FString(Kind) + TEXT(" ") + Json);
    };
    TSet<const UEdGraph*> TriggerGraphs;
    for (const UEdGraph* Graph : Graphs)
        for (const UEdGraphNode* Node : Graph->Nodes)
            if (const auto* Event = Cast<UK2Node_ComponentBoundEvent>(Node))
            {
                auto Value = DescribeNode(Event);
                Value->SetStringField(TEXT("component"), Event->GetComponentPropertyName().ToString());
                Value->SetStringField(TEXT("delegate"), Event->DelegatePropertyName.ToString());
                Value->SetStringField(TEXT("event_function"), Event->GetFunctionName().ToString());
                Value->SetArrayField(TEXT("pins"), ReadPins(Event));
                Emit(TEXT("GraphBoundEvent"), Value);
                for (const UEdGraphPin* Pin : Event->Pins)
                    if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
                        for (const UEdGraphPin* Linked : Pin->LinkedTo)
                            if (const auto* Call = Cast<UK2Node_CallFunction>(Linked->GetOwningNode()))
                                if (Call->FunctionReference.IsSelfContext())
                                    for (const UEdGraph* FunctionGraph : Blueprint->FunctionGraphs)
                                        if (FunctionGraph->GetFName() == Call->FunctionReference.GetMemberName()
                                            && FunctionGraph->GraphGuid == Call->FunctionReference.GetMemberGuid()
                                            && !FunctionGraph->Nodes.IsEmpty()
                                            && !FunctionGraph->Nodes.ContainsByPredicate([](const UEdGraphNode* Member)
                                                { return Member->NodeComment != Design2UMGPrototypeGraphWiring::NodeMarkerComment; }))
                                            TriggerGraphs.Add(FunctionGraph);
            }
    for (const UEdGraph* Graph : TriggerGraphs)
        for (const UEdGraphNode* Node : Graph->Nodes)
            {
                auto Value = DescribeNode(Node);
                Value->SetStringField(TEXT("graph"), Graph->GetPathName());
                Value->SetArrayField(TEXT("pins"), ReadPins(Node));
                Emit(TEXT("GraphTriggerOperation"), Value);
            }
    if (const auto* Generated = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass))
        for (const auto& BindingObject : Generated->DynamicBindingObjects)
            if (const auto* Binding = Cast<UComponentDelegateBinding>(BindingObject))
                for (const auto& Entry : Binding->ComponentDelegateBindings)
                {
                    auto Value = MakeShared<FJsonObject>();
                    Value->SetStringField(TEXT("component"), Entry.ComponentPropertyName.ToString());
                    Value->SetStringField(TEXT("delegate"), Entry.DelegatePropertyName.ToString());
                    Value->SetStringField(TEXT("function"), Entry.FunctionNameToBind.ToString());
                    Emit(TEXT("GraphCompiledDelegate"), Value);
                }
    TMap<FString, int32> Totals;
    TMap<FString, int32> Samples;
    int32 TotalNodes = 0;
    int32 TotalCalls = 0;
    AddInfo(FString::Printf(TEXT("GraphReferenceAsset asset=%s native_parent=%s"), *AssetPath, *GetPathNameSafe(NativeParent)));
    for (const UEdGraph* Graph : Graphs)
    {
        TMap<FString, int32> Counts;
        int32 Calls = 0;
        TotalNodes += Graph->Nodes.Num();
        for (const UEdGraphNode* Node : Graph->Nodes)
        {
            const auto* Call = Cast<UK2Node_CallFunction>(Node);
            if (!Call) continue;
            ++Calls;
            const FMemberReference& Reference = Call->FunctionReference;
            const FName Name = Reference.GetMemberName();
            const UClass* Owner = Reference.GetMemberParentClass();
            const bool bSelf = Reference.IsSelfContext();
            const UFunction* NativeFunction = bSelf && NativeParent ? NativeParent->FindFunctionByName(Name) : nullptr;
            const FString Category = bSelf
                ? (CustomEvents.Contains(Name) ? TEXT("custom_self") : NativeFunction ? TEXT("native_self") : TEXT("other_self"))
                : (Owner && Owner->HasAnyClassFlags(CLASS_Native) ? TEXT("native_external") : TEXT("other_external"));
            ++Counts.FindOrAdd(Category);
            ++Totals.FindOrAdd(Category);
            if (Samples.FindOrAdd(Category)++ < 3)
                AddInfo(FString::Printf(TEXT("GraphReferenceSample category=%s node=%s function=%s parent=%s self=%d member_guid=%s node_guid=%s native_declaration=%s"),
                    *Category, *Node->GetPathName(), *Name.ToString(), *GetPathNameSafe(Owner), bSelf,
                    *Reference.GetMemberGuid().ToString(), *Node->NodeGuid.ToString(), *GetPathNameSafe(NativeFunction)));
        }
        TotalCalls += Calls;
        AddInfo(FString::Printf(TEXT("GraphReferenceGraph graph=%s nodes=%d calls=%d native_self=%d native_external=%d custom_self=%d other_self=%d other_external=%d"),
            *Graph->GetPathName(), Graph->Nodes.Num(), Calls, Counts.FindRef(TEXT("native_self")), Counts.FindRef(TEXT("native_external")),
            Counts.FindRef(TEXT("custom_self")), Counts.FindRef(TEXT("other_self")), Counts.FindRef(TEXT("other_external"))));
    }
    AddInfo(FString::Printf(TEXT("GraphReferenceTotals graphs=%d nodes=%d calls=%d native_self=%d native_external=%d custom_self=%d other_self=%d other_external=%d"),
        Graphs.Num(), TotalNodes, TotalCalls, Totals.FindRef(TEXT("native_self")), Totals.FindRef(TEXT("native_external")),
        Totals.FindRef(TEXT("custom_self")), Totals.FindRef(TEXT("other_self")), Totals.FindRef(TEXT("other_external"))));
    return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRenderSessionStateDiagnostic,
    "Design2UMG.Manual.RenderSessionState",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRenderSessionStateDiagnostic::RunTest(const FString& Parameters)
{
#if WITH_SLATE_DEBUGGING
    FString AssetPath, SpecPaths[2], OutputPath;
    if (!FParse::Value(FCommandLine::Get(), TEXT("D2UNativeAnimationAsset="), AssetPath)
        || !FParse::Value(FCommandLine::Get(), TEXT("D2UDiagnosticRenderSpecA="), SpecPaths[0])
        || !FParse::Value(FCommandLine::Get(), TEXT("D2UDiagnosticRenderSpecB="), SpecPaths[1])
        || !FParse::Value(FCommandLine::Get(), TEXT("D2UDiagnosticObservation="), OutputPath))
    {
        AddError(TEXT("Render observation requires explicit asset, D2UDiagnosticRenderSpecA/B and D2UDiagnosticObservation paths"));
        return false;
    }
    TSharedPtr<FJsonObject> Specs[2];
    for (int32 Index = 0; Index < 2; ++Index)
    {
        FString Text;
        if (!FFileHelper::LoadFileToString(Text, *SpecPaths[Index])
            || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Specs[Index]) || !Specs[Index])
        {
            AddError(TEXT("Cannot read the explicit render spec"));
            return false;
        }
        const FString Mode = Specs[Index]->GetStringField(TEXT("mode"));
        const bool IsAsset = Mode == TEXT("asset")
            && Specs[Index]->GetStringField(TEXT("source_path")) == AssetPath
            && Specs[Index]->GetBoolField(TEXT("force_reload_package"));
        const bool IsGeneration = Index == 0 && Mode == TEXT("local_figma")
            && !Specs[Index]->GetStringField(TEXT("source_path")).IsEmpty()
            && !Specs[Index]->GetStringField(TEXT("output_path")).IsEmpty();
        if (!IsAsset && !IsGeneration)
        {
            AddError(TEXT("Spec A must be local_figma generation or exact asset reload; spec B must reload the explicit asset"));
            return false;
        }
    }
    const FString RHI = GDynamicRHI ? GDynamicRHI->GetName() : TEXT("unavailable");
    const FString FeatureLevel = LexToString(GMaxRHIFeatureLevel);
    AddInfo(FString::Printf(TEXT("RenderObservation RHI=%s feature_level=%s"), *RHI, *FeatureLevel));
    if (!TestEqual(TEXT("Observe the required D3D12 backend"), RHI, FString(TEXT("D3D12")))
        || !TestEqual(TEXT("Observe the required SM6 feature level"), FeatureLevel, FString(TEXT("SM6")))) return false;
    auto Evidence = MakeShared<FJsonObject>();
    Evidence->SetStringField(TEXT("asset"), AssetPath);
    Evidence->SetStringField(TEXT("spec_a"), SpecPaths[0]);
    Evidence->SetStringField(TEXT("spec_b"), SpecPaths[1]);
    Evidence->SetStringField(TEXT("rhi_backend"), RHI);
    Evidence->SetStringField(TEXT("rhi_feature_level"), FeatureLevel);
    TArray<TSharedPtr<FJsonValue>> Observations;
    int32 RenderIndex = 0;
    const auto ColorValues = [](const FLinearColor& Color)
    {
        TArray<TSharedPtr<FJsonValue>> Values;
        for (float Channel : {Color.R, Color.G, Color.B, Color.A}) Values.Add(MakeShared<FJsonValueNumber>(Channel));
        return Values;
    };
    TMap<TWeakObjectPtr<UTexture2D>, TSharedPtr<FJsonObject>> SourceTextureStates;
    const auto ReadSourceTextureState = [&](UTexture2D* Texture)
    {
        TSharedPtr<FJsonObject>& Data = SourceTextureStates.FindOrAdd(TWeakObjectPtr<UTexture2D>(Texture));
        if (Data) return Data;
        Data = MakeShared<FJsonObject>();
        Data->SetStringField(TEXT("identity"), Texture->GetPathName());
        Data->SetNumberField(TEXT("source_width"), Texture->Source.GetSizeX());
        Data->SetNumberField(TEXT("source_height"), Texture->Source.GetSizeY());
        Data->SetNumberField(TEXT("source_format"), static_cast<int32>(Texture->Source.GetFormat()));
        FTextureFormatSettings LayerSettings;
        Texture->GetLayerFormatSettings(0, LayerSettings);
        Data->SetBoolField(TEXT("layer_settings_srgb"), LayerSettings.SRGB);
        Data->SetBoolField(TEXT("use_legacy_gamma"), Texture->bUseLegacyGamma);
        Data->SetNumberField(TEXT("source_mip_count"), Texture->Source.GetNumMips());
        Data->SetNumberField(TEXT("source_encoding_override"), static_cast<int32>(Texture->SourceColorSettings.EncodingOverride));
        Data->SetStringField(TEXT("snapshot_scope"), TEXT("First read of this actual texture object; per-paint RHI state is recorded separately"));
        Data->SetNumberField(TEXT("filter"), static_cast<int32>(Texture->Filter));
        Data->SetNumberField(TEXT("address_x"), static_cast<int32>(Texture->AddressX));
        Data->SetNumberField(TEXT("address_y"), static_cast<int32>(Texture->AddressY));
        const FTextureResource* Resource = Texture->GetResource();
        if (Resource && Resource->TextureRHI.IsValid())
        {
            const FRHITextureDesc& Desc = Resource->TextureRHI->GetDesc();
            Data->SetNumberField(TEXT("resident_pixel_format"), static_cast<int32>(Desc.Format));
            Data->SetStringField(TEXT("resident_pixel_format_name"), GPixelFormats[Desc.Format].Name);
            Data->SetNumberField(TEXT("resident_width"), Desc.Extent.X);
            Data->SetNumberField(TEXT("resident_height"), Desc.Extent.Y);
        }
        else Data->SetStringField(TEXT("pixel_format_unavailable"), TEXT("No resident texture RHI; platform-data completion is not requested"));
        TArray64<uint8> Bytes;
        const bool HasSource = Texture->Source.IsValid() && Texture->Source.GetMipData(Bytes, 0, 0, 0);
        Data->SetBoolField(TEXT("source_mip_available"), HasSource);
        if (HasSource)
        {
            FSHA1 Hash;
            Hash.Update(Bytes.GetData(), Bytes.Num());
            Hash.Final();
            uint8 Digest[20];
            Hash.GetHash(Digest);
            Data->SetStringField(TEXT("source_mip_sha1"), BytesToHex(Digest, 20));
            Data->SetNumberField(TEXT("source_mip_bytes"), Bytes.Num());
            if (Texture->Source.GetFormat() == TSF_BGRA8)
            {
                uint8 MinAlpha = 255, MaxAlpha = 0;
                int64 Transparent = 0, NonOpaque = 0;
                constexpr int32 MaxPeakColors = 4;
                FColor PeakColors[MaxPeakColors];
                int64 PeakCounts[MaxPeakColors] = {};
                int32 PeakColorCount = 0;
                int64 PeakPixels = 0, UnlistedPeakPixels = 0;
                for (int64 Offset = 3; Offset < Bytes.Num(); Offset += 4)
                {
                    const uint8 Alpha = Bytes[Offset];
                    MinAlpha = FMath::Min(MinAlpha, Alpha);
                    if (Alpha > MaxAlpha)
                    {
                        PeakColorCount = 0;
                        PeakPixels = 0;
                        UnlistedPeakPixels = 0;
                    }
                    MaxAlpha = FMath::Max(MaxAlpha, Alpha);
                    if (Alpha != 0 && Alpha == MaxAlpha)
                    {
                        ++PeakPixels;
                        const FColor RGB(Bytes[Offset - 1], Bytes[Offset - 2], Bytes[Offset - 3], 255);
                        int32 ColorIndex = 0;
                        while (ColorIndex < PeakColorCount && PeakColors[ColorIndex] != RGB) ++ColorIndex;
                        if (ColorIndex < PeakColorCount) ++PeakCounts[ColorIndex];
                        else if (PeakColorCount < MaxPeakColors)
                        {
                            PeakColors[PeakColorCount] = RGB;
                            PeakCounts[PeakColorCount++] = 1;
                        }
                        else ++UnlistedPeakPixels;
                    }
                    Transparent += Alpha == 0;
                    NonOpaque += Alpha != 255;
                }
                Data->SetNumberField(TEXT("source_alpha_min_u8"), MinAlpha);
                Data->SetNumberField(TEXT("source_alpha_max_u8"), MaxAlpha);
                Data->SetNumberField(TEXT("source_alpha_zero_pixels"), Transparent);
                Data->SetNumberField(TEXT("source_alpha_nonopaque_pixels"), NonOpaque);
                Data->SetBoolField(TEXT("source_alpha_max_rgb_available"), PeakPixels > 0);
                Data->SetNumberField(TEXT("source_alpha_max_pixel_count"), PeakPixels);
                Data->SetNumberField(TEXT("source_alpha_max_rgb_unlisted_pixels"), UnlistedPeakPixels);
                TArray<TSharedPtr<FJsonValue>> Palette;
                for (int32 Index = 0; Index < PeakColorCount; ++Index)
                {
                    auto Entry = MakeShared<FJsonObject>();
                    TArray<TSharedPtr<FJsonValue>> RGB;
                    for (uint8 Channel : {PeakColors[Index].R, PeakColors[Index].G, PeakColors[Index].B})
                        RGB.Add(MakeShared<FJsonValueNumber>(Channel));
                    Entry->SetArrayField(TEXT("rgb_u8"), RGB);
                    Entry->SetNumberField(TEXT("pixels"), PeakCounts[Index]);
                    Palette.Add(MakeShared<FJsonValueObject>(Entry));
                }
                Data->SetArrayField(TEXT("source_alpha_max_rgb_palette"), Palette);
            }
            else Data->SetStringField(TEXT("source_alpha_unavailable"), TEXT("Raw alpha diagnostic supports the actual BGRA8 source format only"));
        }
        else Data->SetStringField(TEXT("source_unavailable"), TEXT("Actual texture has no readable source mip"));
        return Data;
    };
    const auto ReadTextureState = [&](UTexture* Texture)
    {
        auto Value = MakeShared<FJsonObject>();
        Value->SetBoolField(TEXT("available"), Texture != nullptr);
        if (!Texture) return Value;
        Value->SetStringField(TEXT("identity"), Texture->GetPathName());
        Value->SetBoolField(TEXT("srgb"), Texture->SRGB);
        if (auto* Texture2D = Cast<UTexture2D>(Texture))
            Value->SetObjectField(TEXT("source_state"), ReadSourceTextureState(Texture2D));
        Value->SetNumberField(TEXT("surface_width"), Texture->GetSurfaceWidth());
        Value->SetNumberField(TEXT("surface_height"), Texture->GetSurfaceHeight());
        const FTextureResource* Resource = Texture->GetResource();
        const bool Resident = Resource && Resource->TextureRHI.IsValid();
        Value->SetBoolField(TEXT("rhi_available"), Resident);
        if (Resident)
        {
            const FRHITextureDesc& Desc = Resource->TextureRHI->GetDesc();
            Value->SetNumberField(TEXT("rhi_format"), static_cast<int32>(Desc.Format));
            Value->SetStringField(TEXT("rhi_format_name"), GPixelFormats[Desc.Format].Name);
            Value->SetStringField(TEXT("rhi_flags"), LexToString(static_cast<uint64>(Desc.Flags)));
            Value->SetNumberField(TEXT("rhi_width"), Desc.Extent.X);
            Value->SetNumberField(TEXT("rhi_height"), Desc.Extent.Y);
            Value->SetNumberField(TEXT("rhi_mip_count"), Desc.NumMips);
        }
        const auto* Target = Cast<UTextureRenderTarget2D>(Texture);
        Value->SetBoolField(TEXT("render_target_gamma_available"), Target != nullptr);
        if (Target)
        {
            Value->SetBoolField(TEXT("force_linear_gamma"), Target->bForceLinearGamma);
            Value->SetNumberField(TEXT("target_gamma"), Target->TargetGamma);
        }
        return Value;
    };
    const auto ReadMaterial = [&](UMaterialInterface* Material)
    {
        auto Value = MakeShared<FJsonObject>();
        Value->SetBoolField(TEXT("available"), Material != nullptr);
        Value->SetBoolField(TEXT("mid_available"), Cast<UMaterialInstanceDynamic>(Material) != nullptr);
        if (!Material) return Value;
        Value->SetStringField(TEXT("identity"), Material->GetPathName());
        if (Cast<UMaterialInstanceDynamic>(Material)) Value->SetStringField(TEXT("mid"), Material->GetPathName());
        Value->SetNumberField(TEXT("material_blend_mode"), static_cast<int32>(Material->GetBlendMode()));
        float Opacity = 0;
        const bool Found = Material->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("D2UGroupOpacity")), Opacity);
        Value->SetBoolField(TEXT("group_opacity_available"), Found);
        if (Found) Value->SetNumberField(TEXT("group_opacity"), Opacity);
        TArray<TSharedPtr<FJsonValue>> Scalars, Vectors, Textures;
        TArray<FMaterialParameterInfo> Infos;
        TArray<FGuid> Ids;
        const auto ParameterIdentity = [](const FMaterialParameterInfo& Info)
        {
            auto Parameter = MakeShared<FJsonObject>();
            Parameter->SetStringField(TEXT("name"), Info.Name.ToString());
            Parameter->SetNumberField(TEXT("association"), static_cast<uint8>(Info.Association));
            Parameter->SetNumberField(TEXT("index"), Info.Index);
            return Parameter;
        };
        Material->GetAllScalarParameterInfo(Infos, Ids);
        for (const auto& Info : Infos)
        {
            auto Parameter = ParameterIdentity(Info);
            float Scalar = 0;
            const bool Available = Material->GetScalarParameterValue(FHashedMaterialParameterInfo(Info), Scalar);
            Parameter->SetBoolField(TEXT("available"), Available);
            if (Available) Parameter->SetNumberField(TEXT("value"), Scalar);
            Scalars.Add(MakeShared<FJsonValueObject>(Parameter));
        }
        Infos.Reset(); Ids.Reset();
        Material->GetAllVectorParameterInfo(Infos, Ids);
        for (const auto& Info : Infos)
        {
            auto Parameter = ParameterIdentity(Info);
            FLinearColor Vector;
            const bool Available = Material->GetVectorParameterValue(FHashedMaterialParameterInfo(Info), Vector);
            Parameter->SetBoolField(TEXT("available"), Available);
            if (Available) Parameter->SetArrayField(TEXT("value"), ColorValues(Vector));
            Vectors.Add(MakeShared<FJsonValueObject>(Parameter));
        }
        Infos.Reset(); Ids.Reset();
        Material->GetAllTextureParameterInfo(Infos, Ids);
        for (const auto& Info : Infos)
        {
            auto Parameter = ParameterIdentity(Info);
            UTexture* Texture = nullptr;
            const bool Available = Material->GetTextureParameterValue(FHashedMaterialParameterInfo(Info), Texture);
            Parameter->SetBoolField(TEXT("available"), Available);
            Parameter->SetObjectField(TEXT("texture"), ReadTextureState(Available ? Texture : nullptr));
            Textures.Add(MakeShared<FJsonValueObject>(Parameter));
        }
        TArray<TSharedPtr<FJsonValue>> Required;
        for (const TCHAR* Name : {TEXT("D2UBlendMode"), TEXT("D2USourceU"), TEXT("D2USourceV"),
            TEXT("D2UCompositionU"), TEXT("D2UCompositionV")})
        {
            auto Parameter = MakeShared<FJsonObject>();
            Parameter->SetStringField(TEXT("name"), Name);
            bool Available = false;
            for (const auto* Group : {&Scalars, &Vectors, &Textures})
                for (const auto& Entry : *Group)
                    if (Entry->AsObject()->GetStringField(TEXT("name")) == Name)
                        Available |= Entry->AsObject()->GetBoolField(TEXT("available"));
            Parameter->SetBoolField(TEXT("available"), Available);
            if (!Available) Parameter->SetStringField(TEXT("unavailable"), TEXT("Not declared or public getter returned false"));
            Required.Add(MakeShared<FJsonValueObject>(Parameter));
        }
        Value->SetArrayField(TEXT("required_parameter_availability"), Required);
        Value->SetArrayField(TEXT("scalar_parameters"), Scalars);
        Value->SetArrayField(TEXT("vector_parameters"), Vectors);
        Value->SetArrayField(TEXT("texture_parameters"), Textures);
        Value->SetStringField(TEXT("parameter_scope"), TEXT("Public material parameter metadata and getters; missing names are not declared, failed getters have available=false"));
        return Value;
    };
    const auto ReadGeometry = [](UWidget* Widget)
    {
        auto Value = MakeShared<FJsonObject>();
        const FGeometry& Geometry = Widget->GetCachedGeometry();
        const auto Vector = [](const FVector2D& V)
        {
            TArray<TSharedPtr<FJsonValue>> Result;
            Result.Add(MakeShared<FJsonValueNumber>(V.X));
            Result.Add(MakeShared<FJsonValueNumber>(V.Y));
            return Result;
        };
        Value->SetArrayField(TEXT("local_size"), Vector(Geometry.GetLocalSize()));
        Value->SetArrayField(TEXT("absolute_origin"), Vector(Geometry.LocalToAbsolute(FVector2D::ZeroVector)));
        Value->SetArrayField(TEXT("absolute_unit_x"), Vector(Geometry.LocalToAbsolute(FVector2D(1, 0))));
        Value->SetArrayField(TEXT("absolute_unit_y"), Vector(Geometry.LocalToAbsolute(FVector2D(0, 1))));
        const FWidgetTransform& Transform = Widget->GetRenderTransform();
        Value->SetArrayField(TEXT("render_translation"), Vector(Transform.Translation));
        Value->SetArrayField(TEXT("render_scale"), Vector(Transform.Scale));
        Value->SetArrayField(TEXT("render_shear"), Vector(Transform.Shear));
        Value->SetNumberField(TEXT("render_angle"), Transform.Angle);
        Value->SetArrayField(TEXT("render_pivot"), Vector(Widget->GetRenderTransformPivot()));
        Value->SetNumberField(TEXT("render_opacity"), Widget->GetRenderOpacity());
        Value->SetNumberField(TEXT("visibility"), static_cast<uint8>(Widget->GetVisibility()));
        return Value;
    };
    const auto Observe = [&](const TCHAR* Stage, const SWidget* PaintedWidget)
    {
        auto Observation = MakeShared<FJsonObject>();
        Observation->SetNumberField(TEXT("render"), RenderIndex);
        Observation->SetStringField(TEXT("stage"), Stage);
        Observation->SetStringField(TEXT("frame"), LexToString(GFrameCounter));
        TArray<TSharedPtr<FJsonValue>> Instances;
        for (TObjectIterator<UUserWidget> It; It; ++It)
        {
            UUserWidget* Root = *It;
            if (Root->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject)
                || Root->GetClass()->GetPathName() != AssetPath + TEXT("_C")
                || Root->GetCachedWidget().Get() != PaintedWidget || !Root->WidgetTree) continue;
            auto Instance = MakeShared<FJsonObject>();
            Instance->SetStringField(TEXT("instance"), Root->GetPathName());
            TArray<TSharedPtr<FJsonValue>> Producers, Retainers, Brushes;
            TSet<UWidget*> VisitedWidgets;
            TSet<UWidgetTree*> VisitedTrees;
            TFunction<void(UWidgetTree*)> VisitTree;
            VisitTree = [&](UWidgetTree* Tree)
            {
                if (!Tree || VisitedTrees.Contains(Tree)) return;
                VisitedTrees.Add(Tree);
                Tree->ForEachWidget([&](UWidget* Widget)
                {
                    if (VisitedWidgets.Contains(Widget)) return;
                    VisitedWidgets.Add(Widget);
                    if (const auto* Role = FindFProperty<FObjectPropertyBase>(Widget->GetClass(), TEXT("D2UNativeProducer")))
                    {
                        auto Producer = MakeShared<FJsonObject>();
                        Producer->SetStringField(TEXT("widget"), Widget->GetPathName());
                        Producer->SetObjectField(TEXT("geometry"), ReadGeometry(Widget));
                        Producer->SetStringField(TEXT("source"), GetPathNameSafe(Role->GetObjectPropertyValue_InContainer(Widget)));
                        const auto* Opacity = FindFProperty<FFloatProperty>(Widget->GetClass(), TEXT("SourceOpacity"));
                        Producer->SetBoolField(TEXT("source_opacity_available"), Opacity != nullptr);
                        if (Opacity) Producer->SetNumberField(TEXT("source_opacity"), Opacity->GetPropertyValue_InContainer(Widget));
                        const auto* State = FindFProperty<FObjectPropertyBase>(Widget->GetClass(), TEXT("PaintState"));
                        Producer->SetObjectField(TEXT("paint_state"), ReadMaterial(State ? Cast<UMaterialInstanceDynamic>(State->GetObjectPropertyValue_InContainer(Widget)) : nullptr));
                        Producers.Add(MakeShared<FJsonValueObject>(Producer));
                    }
                    if (auto* Retainer = Cast<URetainerBox>(Widget))
                    {
                        auto Value = MakeShared<FJsonObject>();
                        Value->SetStringField(TEXT("widget"), Widget->GetPathName());
                        Value->SetBoolField(TEXT("retain_rendering"), Retainer->IsRetainRendering());
                        Value->SetBoolField(TEXT("render_on_phase"), Retainer->IsRenderOnPhase());
                        Value->SetBoolField(TEXT("render_on_invalidation"), Retainer->IsRenderOnInvalidation());
                        Value->SetNumberField(TEXT("phase"), Retainer->GetPhase());
                        Value->SetNumberField(TEXT("phase_count"), Retainer->GetPhaseCount());
                        const FVector2D Size = Retainer->GetCachedAllottedGeometry().GetLocalSize();
                        Value->SetNumberField(TEXT("width"), Size.X);
                        Value->SetNumberField(TEXT("height"), Size.Y);
                        const auto Slate = Retainer->GetCachedWidget();
                        Value->SetBoolField(TEXT("cached_slate_available"), Slate.IsValid());
                        if (Slate.IsValid()) Value->SetStringField(TEXT("cached_slate_type"), Slate->GetType().ToString());
                        Value->SetStringField(TEXT("render_state_unavailable"), TEXT("SRetainerWidget offscreen/visibility state getters are protected; only public configuration and capture data are observed"));
                        UMaterialInstanceDynamic* MID = Retainer->GetEffectMaterial();
                        Value->SetObjectField(TEXT("effect_material"), ReadMaterial(MID));
                        UTexture* Texture = nullptr;
                        const bool HasTexture = MID && MID->GetTextureParameterValue(FHashedMaterialParameterInfo(Retainer->GetTextureParameter()), Texture) && Texture;
                        Value->SetBoolField(TEXT("capture_texture_available"), HasTexture);
                        if (HasTexture)
                        {
                            Value->SetStringField(TEXT("capture_texture"), Texture->GetPathName());
                            Value->SetObjectField(TEXT("capture_texture_state"), ReadTextureState(Texture));
                            Value->SetNumberField(TEXT("capture_width"), Texture->GetSurfaceWidth());
                            Value->SetNumberField(TEXT("capture_height"), Texture->GetSurfaceHeight());
                        }
                        Retainers.Add(MakeShared<FJsonValueObject>(Value));
                    }
                    const FSlateBrush* Brush = nullptr;
                    if (const auto* Image = Cast<UImage>(Widget)) Brush = &Design2UMGWidgetPropertyAccess::GetBrush(*Image);
                    else if (const auto* Border = Cast<UBorder>(Widget)) Brush = &Border->Background;
                    if (Brush)
                    {
                        auto Value = MakeShared<FJsonObject>();
                        Value->SetStringField(TEXT("widget"), Widget->GetPathName());
                        Value->SetObjectField(TEXT("geometry"), ReadGeometry(Widget));
                        Value->SetNumberField(TEXT("clipping"), static_cast<uint8>(Widget->GetClipping()));
                        TArray<TSharedPtr<FJsonValue>> Ancestors;
                        TSet<const UWidget*> VisitedAncestors;
                        VisitedAncestors.Add(Widget);
                        UWidget* Current = Widget;
                        while (Current != Root)
                        {
                            UWidget* Parent = Current->GetParent();
                            bool bWidgetTreeOwner = false;
                            if (!Parent)
                                if (const auto* Tree = Current->GetTypedOuter<UWidgetTree>())
                                {
                                    Parent = Cast<UUserWidget>(Tree->GetOuter());
                                    bWidgetTreeOwner = Parent != nullptr;
                                }
                            if (!Parent || VisitedAncestors.Contains(Parent)) break;
                            VisitedAncestors.Add(Parent);
                            auto Ancestor = MakeShared<FJsonObject>();
                            Ancestor->SetStringField(TEXT("widget"), Parent->GetPathName());
                            Ancestor->SetStringField(TEXT("class"), Parent->GetClass()->GetPathName());
                            Ancestor->SetBoolField(TEXT("widget_tree_owner_link"), bWidgetTreeOwner);
                            Ancestor->SetObjectField(TEXT("geometry"), ReadGeometry(Parent));
                            Ancestor->SetNumberField(TEXT("clipping"), static_cast<uint8>(Parent->GetClipping()));
                            Ancestors.Add(MakeShared<FJsonValueObject>(Ancestor));
                            Current = Parent;
                        }
                        Value->SetArrayField(TEXT("ancestors"), Ancestors);
                        Value->SetBoolField(TEXT("ancestor_chain_reaches_render_root"), Current == Root);
                        Value->SetStringField(TEXT("ancestor_scope"), TEXT("UMG panel parents and owning UserWidget across WidgetTrees; Slate-only wrappers and effective clip rectangles are not observed"));
                        Value->SetStringField(TEXT("resource"), GetPathNameSafe(Brush->GetResourceObject()));
                        Value->SetNumberField(TEXT("image_width"), Brush->ImageSize.X);
                        Value->SetNumberField(TEXT("image_height"), Brush->ImageSize.Y);
                        Value->SetNumberField(TEXT("draw_as"), static_cast<uint8>(Brush->GetDrawType()));
                        const FLinearColor Tint = Brush->TintColor.GetSpecifiedColor();
                        TArray<TSharedPtr<FJsonValue>> TintValues;
                        for (float Channel : {Tint.R, Tint.G, Tint.B, Tint.A}) TintValues.Add(MakeShared<FJsonValueNumber>(Channel));
                        Value->SetArrayField(TEXT("specified_tint"), TintValues);
                        if (const auto* Image = Cast<UImage>(Widget))
                            Value->SetArrayField(TEXT("image_color_and_opacity"), ColorValues(Image->GetColorAndOpacity()));
                        if (const auto* Border = Cast<UBorder>(Widget))
                            Value->SetArrayField(TEXT("border_brush_color"), ColorValues(Border->GetBrushColor()));
                        const FBox2f UV = Brush->GetUVRegion();
                        Value->SetBoolField(TEXT("uv_region_valid"), UV.bIsValid != 0);
                        if (UV.bIsValid)
                        {
                            TArray<TSharedPtr<FJsonValue>> Region;
                            for (double Coordinate : {double(UV.Min.X), double(UV.Min.Y), double(UV.Max.X), double(UV.Max.Y)})
                                Region.Add(MakeShared<FJsonValueNumber>(Coordinate));
                            Value->SetArrayField(TEXT("uv_region"), Region);
                        }
                        const FMargin& Margin = Brush->GetMargin();
                        TArray<TSharedPtr<FJsonValue>> Margins;
                        for (float Side : {Margin.Left, Margin.Top, Margin.Right, Margin.Bottom}) Margins.Add(MakeShared<FJsonValueNumber>(Side));
                        Value->SetArrayField(TEXT("margin_ltrb"), Margins);
                        Value->SetNumberField(TEXT("tiling"), static_cast<uint8>(Brush->GetTiling()));
                        Value->SetNumberField(TEXT("mirroring"), static_cast<uint8>(Brush->GetMirroring()));
                        Value->SetObjectField(TEXT("material"), ReadMaterial(Cast<UMaterialInterface>(Brush->GetResourceObject())));
                        Value->SetObjectField(TEXT("resource_texture_state"), ReadTextureState(Cast<UTexture>(Brush->GetResourceObject())));
                        if (auto* Texture = Cast<UTexture2D>(Brush->GetResourceObject()))
                        {
                            Value->SetObjectField(TEXT("texture"), ReadSourceTextureState(Texture));
                        }
                        Brushes.Add(MakeShared<FJsonValueObject>(Value));
                    }
                    if (const auto* Nested = Cast<UUserWidget>(Widget)) VisitTree(Nested->WidgetTree);
                });
            };
            VisitTree(Root->WidgetTree);
            Instance->SetObjectField(TEXT("geometry"), ReadGeometry(Root));
            Instance->SetArrayField(TEXT("brushes"), Brushes);
            Instance->SetArrayField(TEXT("producers"), Producers);
            Instance->SetArrayField(TEXT("retainers"), Retainers);
            Instances.Add(MakeShared<FJsonValueObject>(Instance));
        }
        if (Instances.IsEmpty()) return;
        Observation->SetArrayField(TEXT("live_render_instances"), Instances);
        Observations.Add(MakeShared<FJsonValueObject>(Observation));
    };
    const FDelegateHandle Begin = FSlateDebugging::BeginWidgetPaint.AddLambda(
        [&](const SWidget* Widget, const FPaintArgs&, const FGeometry&, const FSlateRect&, const FSlateWindowElementList& Elements, int32)
        { if (Widget->GetType() == TEXT("SObjectWidget")) Observe(TEXT("before_root_paint"), Widget); });
    const FDelegateHandle End = FSlateDebugging::EndWidgetPaint.AddLambda(
        [&](const SWidget* Widget, const FSlateWindowElementList& Elements, int32)
        { if (Widget->GetType() == TEXT("SObjectWidget")) Observe(TEXT("after_root_paint"), Widget); });
    ON_SCOPE_EXIT
    {
        FSlateDebugging::BeginWidgetPaint.Remove(Begin);
        FSlateDebugging::EndWidgetPaint.Remove(End);
    };
    for (RenderIndex = 0; RenderIndex < 2; ++RenderIndex)
    {
        const int32 BeforeCount = Observations.Num();
        Design2UMGAutomationRender::HandleConsoleCommand({SpecPaths[RenderIndex], TEXT("--no-quit")});
        TestTrue(*FString::Printf(TEXT("Render %d produced actual root-paint observations"), RenderIndex), Observations.Num() > BeforeCount);
        FString SpecText, ResultText;
        TSharedPtr<FJsonObject> Spec, Result;
        FFileHelper::LoadFileToString(SpecText, *SpecPaths[RenderIndex]);
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(SpecText), Spec);
        const bool HasResult = FFileHelper::LoadFileToString(ResultText, *Spec->GetStringField(TEXT("result_json_path")))
            && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ResultText), Result) && Result;
        TestTrue(*FString::Printf(TEXT("Render %d completed through the existing render entry"), RenderIndex), HasResult && Result->GetBoolField(TEXT("success")));
        if (HasResult) TestEqual(TEXT("Rendered asset matches explicit observation identity"), Result->GetStringField(TEXT("generated_asset_path")), AssetPath);
    }
    Evidence->SetArrayField(TEXT("observations"), Observations);
    FString Json;
    FJsonSerializer::Serialize(Evidence, TJsonWriterFactory<>::Create(&Json));
    TestTrue(TEXT("Write actual paint-time observations"), FFileHelper::SaveStringToFile(Json, *OutputPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    TestTrue(TEXT("Actual offscreen paint callbacks were observed"), !Observations.IsEmpty());
    return !HasAnyErrors();
#else
    AddError(TEXT("This diagnostic requires the existing WITH_SLATE_DEBUGGING delegates"));
    return false;
#endif
}

#endif
