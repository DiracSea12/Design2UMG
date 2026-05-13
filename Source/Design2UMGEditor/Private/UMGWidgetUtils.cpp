// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGWidgetUtils.h"
#include "Design2UMGGenerationSettingsSnapshot.h"
#include "WidgetTreeChildBuilder.h"
#include "WidgetMetadataCollector.h"
#include "WidgetResourceBinder.h"
#include "Design2UMGLog.h"
#include "UMGLayoutProjection.h"
#include "WidgetLayoutAdapter.h"
#include "UMGPropertyKeys.h"
#include "UMGWidgetNodeAccess.h"
#include "UMGValueParsing.h"

#include "WidgetBlueprint.h"
#include "Blueprint/UserWidget.h"
#include "Editor.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"

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
		return Size.X > 1 && Size.Y > 1;
	}

	FIntPoint SanitizeEffectiveDesignSize(const FIntPoint& InSize)
	{
		return FIntPoint(
			FMath::Max(1, InSize.X),
			FMath::Max(1, InSize.Y));
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
			// List widgets.
			REG(ListView); REG(TileView); REG(TreeView);
			// Other widgets.
			REG(MenuAnchor); REG(EditableText); REG(MultiLineEditableText);
			#undef REG
			// Aliases.
			M.Add(TEXT("ComboBox"), UComboBoxString::StaticClass());
			M.Add(TEXT("ComboBoxString"), UComboBoxString::StaticClass());
			return M;
		}();
		return Map;
	}

}

UWidget* FUMGWidgetUtils::BuildWidgetTree(UWidgetTree* WidgetTree, const FUMGWidgetNode& Node, const TMap<FString, UTexture2D*>* TextureMap, TArray<FDesign2UMGFigmaWidgetMetadataRecord>* FigmaMetadataRecords, const FIntPoint* ExplicitDesignSize)
{
	UClass* WidgetClass = ResolveWidgetClass(Node.Type);
	if (const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings = Design2UMGGenerationSettingsContext::GetCurrent())
	{
		if (UClass* Override = GenerationSettings->FindClassOverride(Node.Name))
		{
			WidgetClass = Override;
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

	FName WidgetName = SanitizeWidgetName(Node.Name, Node.Type);
	if (FindObject<UObject>(WidgetTree, *WidgetName.ToString()))
	{
		for (int32 Suffix = 1; Suffix < 1000; ++Suffix)
		{
			FName Candidate = FName(*FString::Printf(TEXT("%s_%d"), *WidgetName.ToString(), Suffix));
			if (!FindObject<UObject>(WidgetTree, *Candidate.ToString()))
			{
				WidgetName = Candidate;
				break;
			}
		}
	}

	UWidget* NewWidget = WidgetTree->ConstructWidget<UWidget>(WidgetClass, WidgetName);
	if (!NewWidget)
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("Failed to construct widget: %s"), *Node.Name);
		return nullptr;
	}

	const FWidgetTreeChildBuildContext Context{
		TextureMap,
		FigmaMetadataRecords,
		ExplicitDesignSize
	};
	FWidgetTreeChildBuilder::BuildChildren(WidgetTree, NewWidget, Node, Context);

	return NewWidget;
}

UWidget* FUMGWidgetUtils::CreateWidgetRecursive(UWidgetTree* WidgetTree, const FUMGWidgetNode& Node, UPanelWidget* ParentWidget, const TMap<FString, UTexture2D*>* TextureMap, TArray<FDesign2UMGFigmaWidgetMetadataRecord>* FigmaMetadataRecords, const FIntPoint* ExplicitDesignSize)
{
	const FIntPoint EffectiveDesignSize = ResolveEffectiveDesignSize(Node, ExplicitDesignSize);

	UWidget* NewWidget = BuildWidgetTree(WidgetTree, Node, TextureMap, FigmaMetadataRecords, &EffectiveDesignSize);
	if (!NewWidget)
	{
		return nullptr;
	}

	// Add to the parent container and apply slot properties.
	if (ParentWidget)
	{
		if (UPanelSlot* Slot = ParentWidget->AddChild(NewWidget))
		{
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
	}

	ApplyWidgetProperties(NewWidget, Node);
	FWidgetMetadataCollector::TryAppend(NewWidget, Node, FigmaMetadataRecords);

	if (UImage* ImageWidget = Cast<UImage>(NewWidget))
	{
		FWidgetResourceBinder::BindImageResource(ImageWidget, Node, TextureMap);
	}

	return NewWidget;
}

UClass* FUMGWidgetUtils::ResolveWidgetClass(const FString& TypeName)
{
	if (UClass* const* Found = GetWidgetClassMap().Find(TypeName))
	{
		return *Found;
	}

	static TMap<FString, TWeakObjectPtr<UClass>> DynamicClassCache;
	if (TWeakObjectPtr<UClass>* Cached = DynamicClassCache.Find(TypeName))
	{
		if (Cached->IsValid())
			return Cached->Get();
		DynamicClassCache.Remove(TypeName);
	}

	FString ClassName = FString::Printf(TEXT("U%s"), *TypeName);
	UClass* FoundClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::EnsureIfAmbiguous);
	if (FoundClass && FoundClass->IsChildOf(UWidget::StaticClass()))
	{
		DynamicClassCache.Add(TypeName, FoundClass);
		return FoundClass;
	}

	return nullptr;
}

FName FUMGWidgetUtils::SanitizeWidgetName(const FString& InName, const FString& FallbackPrefix)
{
	FString Sanitized = SlugStringForValidName(InName);

	if (Sanitized.IsEmpty())
	{
		Sanitized = FallbackPrefix + TEXT("_Widget");
	}

	if (Sanitized.Len() > 0 && FChar::IsDigit(Sanitized[0]))
	{
		Sanitized = TEXT("_") + Sanitized;
	}

	return FName(*Sanitized);
}

