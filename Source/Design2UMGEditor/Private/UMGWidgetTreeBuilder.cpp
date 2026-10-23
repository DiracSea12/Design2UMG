// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGWidgetTreeBuilder.h"
#include "DesignBrowserWidget.h"
#include "DesignImageResourceLimits.h"
#include "Design2UMGComponentBlueprints.h"
#include "Design2UMGGenerationSettingsSnapshot.h"
#include "Design2UMGComponentMappingGeneration.h"
#include "WidgetTreeChildBuilder.h"
#include "WidgetResourceBinder.h"
#include "Design2UMGLog.h"
#include "UMGLayoutProjection.h"
#include "UMGPropertyApplierInternal.h"
#include "UMGSchemaSourceExtensions.h"
#include "WidgetLayoutAdapter.h"
#include "UMGPropertyKeys.h"
#include "UMGWidgetNodeAccess.h"
#include "UMGWidgetTypes.h"
#include "UMGValueParsing.h"

#include "WidgetBlueprint.h"
#include "Blueprint/UserWidget.h"
#include "Editor.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/EngineVersionComparison.h"
#include "UObject/UObjectGlobals.h"

#include "Components/Widget.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/EditableTextBox.h"
#include "Components/CheckBox.h"
#include "Components/Slider.h"
#include "Components/ProgressBar.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/ScrollBox.h"
#include "Components/GridPanel.h"
#include "Components/UniformGridPanel.h"
#include "Components/WrapBox.h"
#include "Components/ScaleBox.h"
#include "Components/RichTextBlock.h"
#include "Components/CircularThrobber.h"
#include "Components/Throbber.h"
#include "Components/ExpandableArea.h"
#include "Components/ComboBoxString.h"
#include "Components/SpinBox.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/BackgroundBlur.h"
#include "Components/InvalidationBox.h"
#include "Components/NamedSlot.h"
#include "Components/RetainerBox.h"
#include "Components/WidgetSwitcher.h"
#include "Components/SafeZone.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralShapeGenerator.h"
#include "Components/ListView.h"
#include "Components/TileView.h"
#include "Components/TreeView.h"
#include "Components/MenuAnchor.h"
#include "Components/EditableText.h"
#include "Components/MultiLineEditableText.h"
#include "Blueprint/WidgetTree.h"

namespace
{
	bool IsValidEffectiveDesignSize(const FIntPoint& Size)
	{
		return Size.X > 0 && Size.Y > 0;
	}

	FIntPoint SanitizeEffectiveDesignSize(const FIntPoint& InSize)
	{
		return FIntPoint(
			FMath::Max(1, InSize.X),
			FMath::Max(1, InSize.Y));
	}

	/**
	 * Rank generated names for deterministic reimport pairing:
	 * Base -> 0, Base_<N> -> 2N-1, Base_w<N> -> 2N.
	 * Reject other tails, including overlay mirrors; the two numeric forms remain distinct.
	 */
	bool TryParseGeneratedWidgetSuffix(const FString& WidgetName, const FString& BaseName, int32& OutRank)
	{
		OutRank = 0;
		if (WidgetName == BaseName)
		{
			return true;
		}
		if (!WidgetName.StartsWith(BaseName + TEXT("_")))
		{
			return false;
		}

		const FString Tail = WidgetName.RightChop(BaseName.Len() + 1);
		if (Tail.IsNumeric())
		{
			const int32 SuffixOrdinal = FCString::Atoi(*Tail);
			if (SuffixOrdinal <= 0)
			{
				return false;
			}
			OutRank = 2 * SuffixOrdinal - 1;
			return true;
		}

		if (Tail.Len() > 1 && FChar::ToLower(Tail[0]) == TCHAR('w'))
		{
			const FString Digits = Tail.RightChop(1);
			if (Digits.IsNumeric())
			{
				const int32 SuffixOrdinal = FCString::Atoi(*Digits);
				if (SuffixOrdinal <= 0)
				{
					return false;
				}
				OutRank = 2 * SuffixOrdinal;
				return true;
			}
		}
		return false;
	}

	FIntPoint ResolveEffectiveDesignSize(const FUMGWidgetNode& SourceNode, const FIntPoint* ExplicitDesignSize)
	{
		if (ExplicitDesignSize && IsValidEffectiveDesignSize(*ExplicitDesignSize))
		{
			return SanitizeEffectiveDesignSize(*ExplicitDesignSize);
		}

		const FIntPoint RootBoundsSize(
			FMath::CeilToInt(SourceNode.Bounds.W),
			FMath::CeilToInt(SourceNode.Bounds.H));
		if (IsValidEffectiveDesignSize(RootBoundsSize))
		{
			return RootBoundsSize;
		}

		UE_LOG(LogDesign2UMG, Error,
			TEXT("Widget build design size is unavailable for '%s'; source data must define the layout size"),
			*SourceNode.Name);
		return FIntPoint(1, 1);
	}

	// Widget type mapping table.
	const TMap<FString, UClass*>& GetWidgetClassMap()
	{
		static const TMap<FString, UClass*> Map = []() {
			TMap<FString, UClass*> M;
			#define REG(Name) M.Add(TEXT(#Name), U##Name::StaticClass())
			// Containers.
			REG(CanvasPanel); REG(VerticalBox); REG(HorizontalBox); REG(Overlay);
			REG(ScrollBox); REG(GridPanel); REG(UniformGridPanel); REG(WrapBox);
			REG(WidgetSwitcher); REG(SafeZone);
			// Wrappers.
			REG(Border); REG(SizeBox); REG(ScaleBox); REG(BackgroundBlur);
			REG(InvalidationBox); REG(RetainerBox); REG(ExpandableArea); REG(NamedSlot);
			// Interactive widgets.
			REG(Button); REG(CheckBox); REG(Slider); REG(SpinBox);
			// Text widgets.
			REG(TextBlock); REG(RichTextBlock); REG(EditableTextBox); REG(MultiLineEditableTextBox);
			// Display widgets.
			REG(Image); REG(ProgressBar); REG(Throbber); REG(CircularThrobber); REG(Spacer);
			REG(WebBrowser);
			// List widgets.
			REG(ListView); REG(TileView); REG(TreeView);
			// Other widgets.
			REG(MenuAnchor); REG(EditableText); REG(MultiLineEditableText);
			#undef REG
			// Aliases.
			M.Add(UMGWidgetTypes::ComboBox, UComboBoxString::StaticClass());
			M.Add(UMGWidgetTypes::ComboBoxString, UComboBoxString::StaticClass());
			return M;
		}();
		return Map;
	}

}

UWidget* FUMGWidgetTreeBuilder::BuildWidgetTree(UWidgetTree* WidgetTree, const FUMGWidgetNode& Node, const TMap<FString, UTexture2D*>* TextureMap, const FIntPoint* ExplicitDesignSize, FDesign2UMGComponentMappingBuildState* ComponentMappingState, TMap<FString, UClass*>* ClassOverrideCache, const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings, DesignImageResources::FImageOperationBudget* Budget)
{
	DesignImageResources::FImageOperationBudget LocalBudget(DesignImageResources::GetGeneratedTextureLimits());
	if (!Budget) Budget = &LocalBudget;
	// Class-override lookups are cached per tree: each root call owns the cache; recursive
	// BuildWidgetTree/CreateWidgetRecursive entries reuse the same map so rule loading
	// happens at most once per node name during one build.
	TMap<FString, UClass*> LocalClassOverrideCache;
	if (!ClassOverrideCache)
	{
		ClassOverrideCache = &LocalClassOverrideCache;
	}

	FName WidgetName = SanitizeWidgetName(Node.Name, Node.Type);
	if (FindObject<UObject>(WidgetTree, *WidgetName.ToString()))
	{
		const FName OriginalWidgetName = WidgetName;
		for (int32 Suffix = 1; Suffix < 1000; ++Suffix)
		{
			FName Candidate = FName(*FString::Printf(TEXT("%s_%d"), *WidgetName.ToString(), Suffix));
			if (!FindObject<UObject>(WidgetTree, *Candidate.ToString()))
			{
				WidgetName = Candidate;
				break;
			}
		}
		if (WidgetName == OriginalWidgetName)
		{
			// All 999 suffixed candidates are taken; the widget keeps the colliding name and the
			// engine auto-renames on ConstructWidget, which can break reimport same-name matching.
			UE_LOG(LogDesign2UMG, Error,
				TEXT("Widget name '%s' has more than 999 duplicates in the widget tree; leaving the collision to engine auto-naming, reimport same-name matching may be affected."),
				*WidgetName.ToString());
		}
	}

	// Component Mapping hits take priority: attach as a project WBP class instance; the component's internal subtree is not expanded;
	// on class-missing/load-failure TryCreateMappedWidget has already logged a diagnostic and returned empty, and the plain path continues.
	TOptional<FDesign2UMGGenerationSettingsSnapshot> MappingFallbackSettings;
	if (ComponentMappingState
		&& Node.HasSourceData()
		&& !Node.Source().MappedComponentKey.IsEmpty())
	{
		if (UUserWidget* MappedInstance = Design2UMGComponentMappingGeneration::TryCreateMappedWidget(
			WidgetTree, Node, WidgetName, *ComponentMappingState))
		{
			ComponentMappingState->OnWidgetConstructed(MappedInstance);
			return MappedInstance;
		}
		if (GenerationSettings && GenerationSettings->ComponentBlueprints)
		{
			// Mapping cuts off automatic dependency planning. On mapping failure, render
			// the supplied subtree normally, including its descendants, as diagnosed above.
			MappingFallbackSettings = *GenerationSettings;
			MappingFallbackSettings->ComponentBlueprints.Reset();
			GenerationSettings = &MappingFallbackSettings.GetValue();
		}
	}

	if (GenerationSettings && GenerationSettings->ComponentBlueprints)
	{
		auto& Context = *GenerationSettings->ComponentBlueprints;
		const FString Key = Context.ResolveKey(Node);
		if (!Key.IsEmpty())
		{
			const auto* Component = Context.Components.Find(Key);
			UClass* Class = Component ? LoadObject<UClass>(nullptr, *Component->ClassPath) : nullptr;
			if (!Class)
			{
				Context.Error = FString::Printf(TEXT("Component '%s' for node '%s' has no generated class"), *Key, *Node.Id);
				UE_LOG(LogDesign2UMG, Error, TEXT("%s"), *Context.Error);
				return nullptr;
			}
			// Blueprint templates must not initialize a transient runtime tree before slot bindings are authored.
			UUserWidget* Instance = CastChecked<UUserWidget>(WidgetTree->ConstructWidget<UWidget>(Class, WidgetName));
			if (!Context.ApplyInstance(Instance, WidgetTree, Node, *GenerationSettings, TextureMap, ExplicitDesignSize, ComponentMappingState, Budget)) return nullptr;
			if (ComponentMappingState) ComponentMappingState->OnWidgetConstructed(Instance);
			return Instance;
		}
	}
	UClass* WidgetClass = ResolveWidgetClass(Node.Type);
	if (GenerationSettings && (!Node.HasSourceData() || Node.Source().MappedComponentKey.IsEmpty()))
	{
		// Name-based ClassOverride applies only to ordinary nodes that did not hit a Component Mapping.
		if (UClass* Override = GenerationSettings->FindClassOverride(Node.Name, ClassOverrideCache))
		{
			WidgetClass = Override;
		}
	}

	// Avoid case-insensitive UWidget property collisions before creating Blueprint variables.
	// Use _wN for property collisions and _N for sibling duplicates so reimport can rank both.
	const FName CollidingName = WidgetName;
	const auto IsNameFree = [&WidgetTree](const FName& Candidate)
	{
		return !UWidget::StaticClass()->FindPropertyByName(Candidate)
			&& !FindObject<UObject>(WidgetTree, *Candidate.ToString());
	};
	if (WidgetClass && UWidget::StaticClass()->FindPropertyByName(WidgetName))
	{
		for (int32 Suffix = 1; Suffix < 1000; ++Suffix)
		{
			const FName Candidate = FName(*FString::Printf(TEXT("%s_w%d"), *CollidingName.ToString(), Suffix));
			if (IsNameFree(Candidate))
			{
				WidgetName = Candidate;
				break;
			}
		}
	}

	if (!WidgetClass)
	{
		int32 ChildCount = Node.Children.Num();
		if (ChildCount > 0)
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("Unknown widget type '%s' (Id: %s), skipping with %d children"),
				*Node.Type, *Node.Id, ChildCount);
		}
		else
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("Unknown widget type '%s' (Id: %s), skipping"),
				*Node.Type, *Node.Id);
		}
		return nullptr;
	}

	UWidget* NewWidget = WidgetTree->ConstructWidget<UWidget>(WidgetClass, WidgetName);
	if (GenerationSettings && GenerationSettings->ComponentBlueprints)
	{
#if !UE_VERSION_OLDER_THAN(5, 1, 0)
		if (UNamedSlot* NamedSlot = Cast<UNamedSlot>(NewWidget)) NamedSlot->bExposeOnInstanceOnly = true;
#endif
	}
	if (!NewWidget)
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("Failed to construct widget: %s"), *Node.Name);
		return nullptr;
	}

	if (ComponentMappingState)
	{
		// The root-widget capture must happen before children are built (BuildChildren runs right below).
		ComponentMappingState->OnWidgetConstructed(NewWidget);
	}

	const FWidgetTreeChildBuildContext Context{
		TextureMap,
		ExplicitDesignSize,
		ComponentMappingState,
		ClassOverrideCache,
		GenerationSettings,
		Budget
	};
	if (!FWidgetTreeChildBuilder::BuildChildren(WidgetTree, NewWidget, Node, Context))
	{
		return nullptr;
	}

	return NewWidget;
}

UWidget* FUMGWidgetTreeBuilder::CreateWidgetRecursive(UWidgetTree* WidgetTree, const FUMGWidgetNode& Node, UPanelWidget* ParentWidget, const TMap<FString, UTexture2D*>* TextureMap, const FIntPoint* ExplicitDesignSize, FDesign2UMGComponentMappingBuildState* ComponentMappingState, TMap<FString, UClass*>* ClassOverrideCache, const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings, DesignImageResources::FImageOperationBudget* Budget)
{
	DesignImageResources::FImageOperationBudget LocalBudget(DesignImageResources::GetGeneratedTextureLimits());
	if (!Budget) Budget = &LocalBudget;
	const FIntPoint EffectiveDesignSize = ResolveEffectiveDesignSize(Node, ExplicitDesignSize);

	UWidget* NewWidget = BuildWidgetTree(WidgetTree, Node, TextureMap, &EffectiveDesignSize, ComponentMappingState, ClassOverrideCache, GenerationSettings, Budget);
	if (!NewWidget)
	{
		return nullptr;
	}

	// Add to the parent container and apply slot properties.
	if (ParentWidget)
	{
		UPanelSlot* Slot = ParentWidget->AddChild(NewWidget);
		if (!Slot)
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("Could not attach widget '%s' to '%s'"), *Node.Name, *ParentWidget->GetName());
			return nullptr;
		}
		const FUMGSlotData SlotData = FWidgetLayoutAdapter::Resolve(
			Node, ParentWidget, WidgetTree, EffectiveDesignSize, false);
		UE_LOG(LogDesign2UMG, Verbose,
			TEXT("CreateWidgetSlot: '%s' anc=(%.2f,%.2f)-(%.2f,%.2f) off=(%.1f,%.1f,%.1f,%.1f) align=(%.2f,%.2f) auto=%d"),
			*Node.Name,
			SlotData.Anchors.Min.X, SlotData.Anchors.Min.Y,
			SlotData.Anchors.Max.X, SlotData.Anchors.Max.Y,
			SlotData.Offsets.Left, SlotData.Offsets.Top, SlotData.Offsets.Right, SlotData.Offsets.Bottom,
			SlotData.Alignment.X, SlotData.Alignment.Y,
			SlotData.bAutoSize ? 1 : 0);
		ApplySlotProperties(Slot, SlotData);
	}

	const bool AutomaticComponent = GenerationSettings && GenerationSettings->ComponentBlueprints
		&& NewWidget->IsA<UUserWidget>() && Node.HasSourceData() && Node.Source().MappedComponentKey.IsEmpty()
		&& !GenerationSettings->ComponentBlueprints->ResolveKey(Node).IsEmpty();
	// The child blueprint root (or its instance override) owns source visuals. Applying them
	// again to the UserWidget wrapper would multiply opacity and repeat render transforms.
	if (!AutomaticComponent) ApplyWidgetProperties(NewWidget, Node, TextureMap, Budget);
	if (UWebBrowser* Browser = Cast<UWebBrowser>(NewWidget))
	{
		if (!DesignBrowserWidget::ApplyTemplate(*Browser, Node.Browser))
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("Browser '%s': initial scroll graph generation failed"), *Browser->GetName());
			return nullptr;
		}
	}
	if (!Budget->GetRefusalError().IsEmpty()) return nullptr;

	// mapped instances: explicit design-facing inputs are written after generic properties (declared values win over design defaults).
	if (ComponentMappingState
		&& Node.HasSourceData()
		&& !Node.Source().MappedComponentKey.IsEmpty())
	{
		if (UUserWidget* MappedInstance = Cast<UUserWidget>(NewWidget))
		{
			Design2UMGComponentMappingGeneration::ApplyMappedComponentInputs(
				MappedInstance, Node, *ComponentMappingState);
		}
	}

	// Record top-level generated nodes (mapped instances were registered at creation), for manifest assembly.
	if (ComponentMappingState)
	{
		ComponentMappingState->RecordCreatedWidget(Node, NewWidget, ParentWidget);
	}

	return NewWidget;
}

UClass* FUMGWidgetTreeBuilder::ResolveWidgetClass(const FString& TypeName)
{
	if (UClass* const* Found = GetWidgetClassMap().Find(TypeName))
	{
		return *Found;
	}

	// Editor-main-thread-only cache (widget class resolution is called during
	// generation and preview materialization, never off-thread). Weak pointers
	// let classes unloaded by asset GC drop out of the cache naturally.
	static TMap<FString, TWeakObjectPtr<UClass>> DynamicClassCache;
	if (TWeakObjectPtr<UClass>* Cached = DynamicClassCache.Find(TypeName))
	{
		if (Cached->IsValid())
			return Cached->Get();
		DynamicClassCache.Remove(TypeName);
	}

	FString ClassName = FString::Printf(TEXT("U%s"), *TypeName);
#if UE_VERSION_OLDER_THAN(5, 1, 0)
	UClass* FoundClass = FindObject<UClass>(ANY_PACKAGE, *ClassName);
#else
	UClass* FoundClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::EnsureIfAmbiguous);
#endif
	if (FoundClass && FoundClass->IsChildOf(UWidget::StaticClass()))
	{
		DynamicClassCache.Add(TypeName, FoundClass);
		return FoundClass;
	}

	return nullptr;
}

void FUMGWidgetTreeBuilder::ApplyWidgetProperties(UWidget* Widget, const FUMGWidgetNode& Node, const TMap<FString, UTexture2D*>* TextureMap, DesignImageResources::FImageOperationBudget* Budget)
{
	DesignImageResources::FImageOperationBudget LocalBudget(DesignImageResources::GetGeneratedTextureLimits());
	if (!Budget) Budget = &LocalBudget;
	if (!Widget)
	{
		return;
	}

	UMGPropertyApplierInternal::ApplyWidgetProperties(Widget, Node, Budget);
	if (UImage* Image = Cast<UImage>(Widget))
	{
		FWidgetResourceBinder::BindImageResource(Image, Node, TextureMap, Budget);
	}
}

FName FUMGWidgetTreeBuilder::SanitizeWidgetName(const FString& InName, const FString& FallbackPrefix)
{
	FString Sanitized = SlugStringForValidName(InName);

	if (Sanitized.IsEmpty())
	{
		Sanitized = FallbackPrefix + TEXT("_Widget");
	}

	// FName parses the "single underscore + digits" shape (e.g. "_127") into NAME_None; digit-leading names
	// must keep determinism with an alphabetic prefix (otherwise NewObject falls back to auto-naming and reimport's same-name matching breaks).
	if (Sanitized.Len() > 0 && FChar::IsDigit(Sanitized[0]))
	{
		Sanitized = TEXT("W") + Sanitized;
	}

	return FName(*Sanitized);
}

bool FUMGWidgetTreeBuilder::MatchesWidgetBaseName(const FString& WidgetName, const FString& BaseName)
{
	int32 Rank = 0;
	return TryParseGeneratedWidgetSuffix(WidgetName, BaseName, Rank);
}

int32 FUMGWidgetTreeBuilder::GetWidgetNameSuffixRank(const FString& WidgetName, const FString& BaseName)
{
	int32 Rank = 0;
	return TryParseGeneratedWidgetSuffix(WidgetName, BaseName, Rank) ? Rank : MAX_int32;
}
