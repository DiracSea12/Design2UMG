// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignOptionalValues.h"
#include "UMGSchemaSourceExtensions.generated.h"

USTRUCT()
struct FUMGPrototypeWidgetAnimationTarget
{
	GENERATED_BODY()
	UPROPERTY() FString NodeId;
	UPROPERTY() FString DestinationNodeId;
	UPROPERTY() FString DestinationWidgetId;
	UPROPERTY() bool bFontSize = false;
	UPROPERTY() bool bLetterSpacing = false;
	UPROPERTY() bool bTextBoxChange = false;
	UPROPERTY() bool bTextPaintChange = false;
	UPROPERTY() bool bSurfacePaintChange = false;
	UPROPERTY() bool bStartPresent = true;
	UPROPERTY() bool bEndPresent = true;
	UPROPERTY() FVector2D Translation = FVector2D::ZeroVector;
	UPROPERTY() FVector2D Scale = FVector2D(1, 1);
	UPROPERTY() float Rotation = 0;
	UPROPERTY() float StartOpacity = 1;
	UPROPERTY() float EndOpacity = 1;
	UPROPERTY() FLinearColor StartColor = FLinearColor::White;
	UPROPERTY() FLinearColor EndColor = FLinearColor::White;
	UPROPERTY() bool bOpacity = false;
	UPROPERTY() bool bColor = false;
};

USTRUCT()
struct FUMGPrototypeLayerOrder
{
	GENERATED_BODY()
	UPROPERTY() FString ParentNodeId;
	UPROPERTY() FString NodeId;
	UPROPERTY() FString DestinationNodeId;
	UPROPERTY() FString ParentWidgetName;
	UPROPERTY() FString WidgetName;
	UPROPERTY() int32 StartIndex = INDEX_NONE;
	UPROPERTY() int32 EndIndex = INDEX_NONE;
};

USTRUCT()
struct FUMGPrototypeAddedLayer
{
	GENERATED_BODY()
	UPROPERTY() FString ParentNodeId;
	UPROPERTY() FString NodeId;
	UPROPERTY() FString DestinationNodeId;
	UPROPERTY() FString SourceNodeJson;
	UPROPERTY() FVector2D RelativePosition = FVector2D::ZeroVector;
	UPROPERTY() bool bReplaceSource = false;
	UPROPERTY() int32 DestinationChildIndex = INDEX_NONE;
	UPROPERTY() int32 EndIndex = INDEX_NONE;
	UPROPERTY() bool bBehindFlow = false;
	UPROPERTY() bool bFlowChild = false;
};

USTRUCT()
struct FUMGPrototypeLocalEndpoint
{
	GENERATED_BODY()
	UPROPERTY() FString NodeId;
	UPROPERTY() FString DestinationWidgetId;
	UPROPERTY() bool bText = false;
	UPROPERTY() bool bFixedPaintSize = false;
	UPROPERTY() FString SourceVisualWidgetId;
	UPROPERTY() FString TargetVisualWidgetId;
	UPROPERTY() FString SourcePresentationWidgetId;
	UPROPERTY() FString TargetPresentationWidgetId;
};

USTRUCT()
struct FUMGPrototypeSharedCapture
{
	GENERATED_BODY()
	UPROPERTY() FString NodeId;
	UPROPERTY() FString DestinationWidgetId;
	UPROPERTY() FString CaptureWidgetId;
	UPROPERTY() FString ShadowWidgetId;
	UPROPERTY() FString ContentWidgetId;
	UPROPERTY() FString PresentationCaptureWidgetId;
	UPROPERTY() FString PresentationShadowWidgetId;
	UPROPERTY() FString PresentationContentWidgetId;
};

USTRUCT()
struct FUMGPrototypeLayoutTarget
{
	GENERATED_BODY()
	UPROPERTY() FString NodeId;
	UPROPERTY() FString DestinationWidgetId;
	UPROPERTY() FString PresentationWidgetId;
};

USTRUCT()
struct FUMGPrototypeLivePresentation
{
	GENERATED_BODY()
	UPROPERTY() FString WidgetId;
	UPROPERTY() FString OriginalSocketWidgetId;
	UPROPERTY() FString PresentationSocketWidgetId;
	UPROPERTY() FString WidgetName;
	UPROPERTY() FString OriginalSocketWidgetName;
	UPROPERTY() FString PresentationSocketWidgetName;
};

USTRUCT()
struct FUMGPrototypeTransitionData
{
	GENERATED_BODY()

	UPROPERTY()
	FString Type;
	UPROPERTY() FString SourceTransitionType;
	UPROPERTY() FString DissolveReason;

	UPROPERTY()
	FString Direction;

	UPROPERTY() FVector2D DragMotionVector = FVector2D::ZeroVector;
	UPROPERTY() bool bHasDragMotionVector = false;

	UPROPERTY()
	float DurationSeconds = 0.0f;

	UPROPERTY()
	FString EasingType;

	UPROPERTY()
	FString EasingJson;

	UPROPERTY()
	FDesignOptionalBool HasMatchLayers;

	UPROPERTY() TArray<FUMGPrototypeWidgetAnimationTarget> WidgetTargets;
	UPROPERTY() bool bFixedViewportLayout = false;
	UPROPERTY() TArray<FUMGPrototypeLayoutTarget> LayoutTargets;
	UPROPERTY() TArray<FUMGPrototypeLocalEndpoint> LocalEndpoints;
	UPROPERTY() TArray<FUMGPrototypeSharedCapture> SharedCaptures;
	UPROPERTY() FString LayoutPresentationWidgetId;
	UPROPERTY() FString LayoutOriginalWidgetId;
	UPROPERTY() FString LayoutOriginalWidgetName;
	UPROPERTY() FString LayoutPresentationWidgetName;
	UPROPERTY() float LayoutOriginalOpacity = 1.0f;
	UPROPERTY() FString LayoutSourceWidgetId;
	UPROPERTY() FString LayoutDestinationWidgetId;
	UPROPERTY() FString LayoutSourcePageId;
	UPROPERTY() TArray<FUMGPrototypeLayerOrder> LayerOrders;
	UPROPERTY() TArray<FUMGPrototypeAddedLayer> AddedLayers;
	UPROPERTY() TArray<FUMGPrototypeLivePresentation> LivePresentations;
	UPROPERTY() FString SourceStateId;
	UPROPERTY() FString TargetStateId;
	UPROPERTY() FString InstanceScopeId;
	UPROPERTY() FString PageTransitionScopeId;
	UPROPERTY() FString StateHostWidgetId;
	UPROPERTY() FString SourceStateWidgetId;
	UPROPERTY() FString TargetStateWidgetId;
	UPROPERTY() bool bStateInstant = false;
	UPROPERTY() FString WidgetAnimationName;
	UPROPERTY() FString Diagnostic;
	UPROPERTY() bool bCubicBezier = false;
	UPROPERTY() FVector4 CubicBezier = FVector4(0, 0, 1, 1);
	UPROPERTY() bool bSpring = false;
	UPROPERTY() float SpringMass = 0;
	UPROPERTY() float SpringStiffness = 0;
	UPROPERTY() float SpringDamping = 0;
	UPROPERTY() float SpringInitialVelocity = 0;
	UPROPERTY() bool bSpringVelocityDefaulted = false;

	FString LayerScopeId() const
	{
		return !PageTransitionScopeId.IsEmpty() ? PageTransitionScopeId
			: (InstanceScopeId.IsEmpty() ? FString() : SourceStateWidgetId + TEXT("::transition::") + TargetStateId);
	}
	bool IsAnimated() const { return DurationSeconds > KINDA_SMALL_NUMBER; }

	bool UsesSlide() const
	{
		return Type.Equals(TEXT("PUSH"), ESearchCase::IgnoreCase)
			|| Type.Equals(TEXT("MOVE_IN"), ESearchCase::IgnoreCase)
			|| Type.Equals(TEXT("MOVE_OUT"), ESearchCase::IgnoreCase)
			|| Type.Equals(TEXT("SLIDE"), ESearchCase::IgnoreCase)
			|| Type.Equals(TEXT("SLIDE_IN"), ESearchCase::IgnoreCase)
			|| Type.Equals(TEXT("SLIDE_OUT"), ESearchCase::IgnoreCase);
	}

	bool UsesFadeOnly() const { return Type.Equals(TEXT("DISSOLVE"), ESearchCase::IgnoreCase); }

	bool UsesBidirectionalSlide() const
	{
		return Type.Equals(TEXT("PUSH"), ESearchCase::IgnoreCase)
			|| Type.Equals(TEXT("SLIDE"), ESearchCase::IgnoreCase);
	}

	bool UsesOutgoingSlideOnly() const { return Type.Equals(TEXT("MOVE_OUT"), ESearchCase::IgnoreCase) || Type.Equals(TEXT("SLIDE_OUT"), ESearchCase::IgnoreCase); }
	bool UsesIncomingSlideOnly() const { return Type.Equals(TEXT("MOVE_IN"), ESearchCase::IgnoreCase) || Type.Equals(TEXT("SLIDE_IN"), ESearchCase::IgnoreCase); }
};

USTRUCT()
struct FUMGSourceNodeData
{
	GENERATED_BODY()

	UPROPERTY() FString SourceNodeId;
	UPROPERTY() FString SourceNodeName;
	UPROPERTY() FString SourceNodeType;
	UPROPERTY() FString ComponentId;
	UPROPERTY() FString ComponentSetId;
	UPROPERTY() FString ComponentSetKey;
	UPROPERTY() FString MainComponentKey;
	UPROPERTY() FString ComponentPropertiesJson;
	UPROPERTY() FString VariantPropertiesJson;
	UPROPERTY() FString OverridesJson;

	UPROPERTY()
	FDesignOptionalBool HasIsExposedInstance;

	/** Conversion-time marker: this node carries a resolvable Figma component identity and is a Component Mapping candidate. */
	UPROPERTY()
	bool bMappedComponentCandidate = false;

	/** Editor generation-time marker: the hit project-level mapping key (Provider|DocumentScope|ComponentKey). Empty means no hit. */
	UPROPERTY()
	FString MappedComponentKey;

	// Figma component semantics: the uppercase SourceNodeType literals ("INSTANCE"/"COMPONENT"/"COMPONENT_SET") and the
	// component instance fields (ComponentId/ComponentPropertiesJson/...) are only ever written by the Figma parser,
	// so these predicates are false for every Pencil-sourced node data.
	bool IsFigmaInstance() const
	{
		return SourceNodeType.Equals(TEXT("INSTANCE"), ESearchCase::IgnoreCase)
			|| !ComponentId.IsEmpty()
			|| !ComponentPropertiesJson.IsEmpty()
			|| !VariantPropertiesJson.IsEmpty()
			|| !OverridesJson.IsEmpty();
	}

	bool IsFigmaComponentDefinition() const { return SourceNodeType.Equals(TEXT("COMPONENT"), ESearchCase::IgnoreCase); }
	bool IsFigmaComponentSet() const { return SourceNodeType.Equals(TEXT("COMPONENT_SET"), ESearchCase::IgnoreCase); }
	bool HasAnyFigmaComponentSemantics() const { return IsFigmaInstance() || IsFigmaComponentDefinition() || IsFigmaComponentSet(); }
};

USTRUCT()
struct FUMGSourceDocumentData
{
	GENERATED_BODY()

	UPROPERTY() FString ComponentsJson;
	UPROPERTY() FString ComponentSetsJson;
	UPROPERTY() FString ComponentDependenciesJson;
	UPROPERTY() TMap<FString, FString> ResolvedComponentImageUrls;
	UPROPERTY() FString SourceFilePath;

	/** Document-level design source identity: "figma" | "pencil" (canonical values in DesignComponentMapping::Provider*). Written by the Converter in StampDocumentComponentIdentity. */
	UPROPERTY() FString Provider;

	/** Unified DocumentScope resolution result (Figma: file key, falling back to the import source identifier), used for mapping matching and the manifest. */
	UPROPERTY() FString DocumentScope;
	/** Origin of the DocumentScope: "fileKey" | "importSource" | "unresolved". */
	UPROPERTY() FString DocumentScopeOrigin;

	bool HasAnyComponentCatalog() const { return !ComponentsJson.IsEmpty() || !ComponentSetsJson.IsEmpty(); }
};

UENUM()
enum class EUMGPrototypeNavigationAction : uint8
{
	Navigate, Overlay, Back, OpenLink, SetVariable, SetVariableMode, Conditional, AnimateWidget, CloseOverlay
};

UENUM()
enum class EUMGPrototypeNavigationStepKind : uint8
{
	Navigate, Overlay, Back, OpenLink, SetVariable, SetVariableMode, ConditionJump, Jump, AnimateWidget, CloseOverlay
};

USTRUCT()
struct FUMGPrototypeNavigationStep
{
	GENERATED_BODY()

	UPROPERTY() EUMGPrototypeNavigationStepKind Kind = EUMGPrototypeNavigationStepKind::Navigate;
	UPROPERTY() FString TargetPageId;
	UPROPERTY() int32 TargetPageIndex = INDEX_NONE;
	UPROPERTY() FString ExternalUrl;
	UPROPERTY() FUMGPrototypeTransitionData Transition;
	UPROPERTY() FString VariableId;
	UPROPERTY() FString VariableValueJson;
	UPROPERTY() FString VariableCollectionId;
	UPROPERTY() FString VariableModeId;
	UPROPERTY() FString ConditionJson;
	UPROPERTY() int32 JumpTargetIndex = INDEX_NONE;

	bool RequiresPageTarget() const { return Kind == EUMGPrototypeNavigationStepKind::Navigate || Kind == EUMGPrototypeNavigationStepKind::Overlay; }
	bool RequiresPageHost() const { return Kind == EUMGPrototypeNavigationStepKind::Navigate || Kind == EUMGPrototypeNavigationStepKind::Overlay || Kind == EUMGPrototypeNavigationStepKind::Back; }
	bool IsOverlayAction() const { return Kind == EUMGPrototypeNavigationStepKind::Overlay; }

	bool IsValid() const
	{
		switch (Kind)
		{
		case EUMGPrototypeNavigationStepKind::Navigate:
		case EUMGPrototypeNavigationStepKind::Overlay:
			return !TargetPageId.IsEmpty() && TargetPageIndex >= 0;
		case EUMGPrototypeNavigationStepKind::Back:
		case EUMGPrototypeNavigationStepKind::CloseOverlay: return true;
		case EUMGPrototypeNavigationStepKind::OpenLink: return !ExternalUrl.IsEmpty();
		case EUMGPrototypeNavigationStepKind::AnimateWidget: return !TargetPageId.IsEmpty();
		case EUMGPrototypeNavigationStepKind::SetVariable: return !VariableId.IsEmpty() && !VariableValueJson.IsEmpty();
		case EUMGPrototypeNavigationStepKind::SetVariableMode: return !VariableCollectionId.IsEmpty() && !VariableModeId.IsEmpty();
		case EUMGPrototypeNavigationStepKind::ConditionJump:
		case EUMGPrototypeNavigationStepKind::Jump: return JumpTargetIndex >= 0;
		default: return false;
		}
	}
};

USTRUCT()
struct FUMGPrototypeNavigationBinding
{
	GENERATED_BODY()

	UPROPERTY() FString TriggerWidgetId;
	UPROPERTY() FString TriggerWidgetName;
	/**
	 * Top-level action summary for binding-level consumers.
	 */
	UPROPERTY() FString SourcePageId;
	UPROPERTY() FString TriggerType;
	UPROPERTY() FString TriggerJson;
	UPROPERTY() FString TriggerDevice;
	UPROPERTY() TArray<FString> TriggerKeyCodes;
	UPROPERTY() float TriggerDelaySeconds = 0;
	UPROPERTY() FString TargetPageId;
	UPROPERTY() int32 TargetPageIndex = INDEX_NONE;
	UPROPERTY() EUMGPrototypeNavigationAction Action = EUMGPrototypeNavigationAction::Navigate;
	UPROPERTY() FString ExternalUrl;
	UPROPERTY() FUMGPrototypeTransitionData Transition;
	UPROPERTY() TArray<FUMGPrototypeNavigationStep> Steps;

	static bool IsClickTriggerType(const FString& T) { return T.IsEmpty() || T.Equals(TEXT("ON_CLICK"), ESearchCase::IgnoreCase); }
	static bool IsHoverTriggerType(const FString& T) { return T.Equals(TEXT("ON_HOVER"), ESearchCase::IgnoreCase); }
	static bool IsMouseEnterTriggerType(const FString& T) { return T.Equals(TEXT("MOUSE_ENTER"), ESearchCase::IgnoreCase); }
	static bool IsPressTriggerType(const FString& T) { return T.Equals(TEXT("ON_PRESS"), ESearchCase::IgnoreCase); }
	static bool IsDragTriggerType(const FString& T) { return T.Equals(TEXT("ON_DRAG"), ESearchCase::IgnoreCase); }
	static bool IsKeyDownTriggerType(const FString& T) { return T.Equals(TEXT("ON_KEY_DOWN"), ESearchCase::IgnoreCase); }
	static bool IsKeyUpTriggerType(const FString& T) { return T.Equals(TEXT("ON_KEY_UP"), ESearchCase::IgnoreCase); }
	static bool IsPageOpenTriggerType(const FString& T) { return T.Equals(TEXT("AFTER_TIMEOUT"), ESearchCase::IgnoreCase); }

	bool UsesClickTrigger() const { return IsClickTriggerType(TriggerType); }
	bool UsesHoverTrigger() const { return IsHoverTriggerType(TriggerType); }
	bool UsesMouseEnterTrigger() const { return IsMouseEnterTriggerType(TriggerType); }
	bool UsesPressTrigger() const { return IsPressTriggerType(TriggerType); }
	bool UsesDragTrigger() const { return IsDragTriggerType(TriggerType); }
	bool UsesKeyDownTrigger() const { return IsKeyDownTriggerType(TriggerType); }
	bool UsesKeyUpTrigger() const { return IsKeyUpTriggerType(TriggerType); }
	bool UsesKeyboardTrigger() const { return UsesKeyDownTrigger() || UsesKeyUpTrigger(); }
	bool UsesPageOpenTrigger() const { return IsPageOpenTriggerType(TriggerType); }

	bool RequiresPageTarget() const
	{
		if (Steps.Num() > 0) { for (const auto& S : Steps) { if (S.RequiresPageTarget()) return true; } return false; }
		return Action == EUMGPrototypeNavigationAction::Navigate || Action == EUMGPrototypeNavigationAction::Overlay;
	}

	bool RequiresPageHost() const
	{
		if (UsesDragTrigger()) return true;
		if (Steps.Num() > 0) { for (const auto& S : Steps) { if (S.RequiresPageHost()) return true; } return false; }
		return Action != EUMGPrototypeNavigationAction::OpenLink;
	}

	bool HasOverlayAction() const
	{
		if (Steps.Num() > 0) { for (const auto& S : Steps) { if (S.IsOverlayAction()) return true; } return false; }
		return Action == EUMGPrototypeNavigationAction::Overlay;
	}

	bool IsValid() const
	{
		if (TriggerWidgetId.IsEmpty() || TriggerWidgetName.IsEmpty()) return false;
		if (!UsesClickTrigger() && !UsesHoverTrigger() && !UsesMouseEnterTrigger() && !UsesPressTrigger() && !UsesDragTrigger() && !UsesKeyDownTrigger() && !UsesKeyUpTrigger() && !UsesPageOpenTrigger()) return false;
		if (UsesKeyboardTrigger() && (TriggerDevice != TEXT("KEYBOARD") || TriggerKeyCodes.Num() == 0)) return false;
		if (Steps.Num() > 0) { for (const auto& S : Steps) { if (!S.IsValid()) return false; } return true; }
		switch (Action)
		{
		case EUMGPrototypeNavigationAction::Navigate:
		case EUMGPrototypeNavigationAction::Overlay: return !TargetPageId.IsEmpty() && TargetPageIndex >= 0;
		case EUMGPrototypeNavigationAction::Back:
		case EUMGPrototypeNavigationAction::CloseOverlay: return true;
		case EUMGPrototypeNavigationAction::OpenLink: return !ExternalUrl.IsEmpty();
		default: return false;
		}
	}
};

USTRUCT()
struct FUMGPrototypeStateEntry
{
	GENERATED_BODY()
	UPROPERTY() FString StateId;
	UPROPERTY() FString WidgetId;
	UPROPERTY() FString WidgetName;
	UPROPERTY() FString SourceNodeJson;
	UPROPERTY() FString Visibility;
};

USTRUCT()
struct FUMGPrototypeStateHost
{
	GENERATED_BODY()
	UPROPERTY() FString HostWidgetId;
	UPROPERTY() FString InstanceWidgetId;
	UPROPERTY() FString InitialStateId;
	UPROPERTY() TArray<FUMGPrototypeStateEntry> States;
};

USTRUCT()
struct FUMGPrototypeNavigationData
{
	GENERATED_BODY()

	UPROPERTY() FString PageHostWidgetId;
	UPROPERTY() FString PageHostWidgetName;
	UPROPERTY() FString OverlayHostWidgetId;
	UPROPERTY() FString OverlayHostWidgetName;
	UPROPERTY() int32 InitialPageIndex = 0;
	UPROPERTY() TArray<FString> PageIds;
	/** Page id -> generated page widget name; graph wiring locates hosted pages by this name. */
	UPROPERTY() TMap<FString, FString> PageWidgetNames;
	UPROPERTY() TArray<FString> PageDisplayVisibility;
	UPROPERTY() TArray<FUMGPrototypeNavigationBinding> ClickBindings;
	UPROPERTY() TArray<FUMGPrototypeStateHost> StateHosts;

	bool HasOverlayBindings() const { for (const auto& B : ClickBindings) { if (B.HasOverlayAction()) return true; } return false; }
	bool RequiresPageHost() const { for (const auto& B : ClickBindings) { if (B.RequiresPageHost()) return true; } return false; }
	bool HasOverlayLayer() const { return !OverlayHostWidgetId.IsEmpty() && !OverlayHostWidgetName.IsEmpty(); }

	bool IsValid() const
	{
		if (ClickBindings.Num() == 0) return false;
		for (const auto& B : ClickBindings) { if (!B.IsValid()) return false; }
		if (!RequiresPageHost()) return true;
		if (PageHostWidgetId.IsEmpty() || PageHostWidgetName.IsEmpty() || PageIds.IsEmpty()) return false;
		return !HasOverlayBindings() || HasOverlayLayer();
	}
};

/**
 * Cross-module protocol constants for multi-page prototype composite tokens: Core (the assembly side) and
 * Editor (the reimport host-continuation side) share this single source - changing a literal on either end would silently break reconciliation, so constants turn drift into a compile error.
 */
namespace Design2UMGPrototypeTokens
{
	/** Common prefix of composite tokens (Design2UMGPrototypeRouting assembles <prefix><suffix>_<hash8>). */
	DESIGN2UMGCORE_API const TCHAR* TokenPrefix();
	/** Suffix of the page host (the assembly side passes it into MakeUniquePrototypeInternalToken). */
	DESIGN2UMGCORE_API const TCHAR* PageHostSuffix();
	/** Full prefix of the page host composite root id (TokenPrefix + PageHostSuffix + "_"). */
	DESIGN2UMGCORE_API const FString& PageHostRootPrefix();
	/** Overlay wrapper root id = page host root id + this suffix (assembled by Design2UMGPrototypeNavigation). */
	DESIGN2UMGCORE_API const TCHAR* OverlayRootSuffix();
	/** Non-numeric tail appended to every widget name/id of an overlay mirror copy (BuildRoot synthesizes one for a
	 *  dual-purpose/initial overlay-target page). MatchesWidgetBaseName only tolerates the generator's `_N`/`_wN`
	 *  tails, so mirrored names stay invisible to the wiring's base-name trigger/page resolution; the wiring finds
	 *  the mirror by sanitize(targetPageId + this suffix), which equals the generator's sanitize of the mirrored IR name. */
	DESIGN2UMGCORE_API const TCHAR* OverlayMirrorSuffix();
}
