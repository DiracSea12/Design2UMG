// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Automation/Design2UMGAutomationStructureSnapshot.h"

#include "DesignSchema.h"
#include "FontResolver.h"
#include "RefExpander.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ContentWidget.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Dom/JsonObject.h"

namespace Design2UMGAutomationStructureSnapshot
{
	namespace
	{
		struct FStructureSnapshotStats
		{
			int32 TotalNodes = 0;
			int32 LeafNodes = 0;
			int32 ContainerNodes = 0;
			int32 TextNodes = 0;
			int32 VisualNodes = 0;
			int32 ButtonNodes = 0;
			int32 PlaceholderNodes = 0;
			int32 MaxDepth = 0;
			int32 RootChildCount = 0;
			FString RootKind = TEXT("unknown");
			TArray<FString> VisibleTexts;
			TArray<FString> BorderWidgets;
			TMap<FString, int32> KindHistogram;
			TMap<FString, int32> ClassHistogram;

			void AddKind(const FString& Kind)
			{
				KindHistogram.FindOrAdd(Kind)++;
			}

			void AddClass(const UWidget* Widget)
			{
				if (!Widget)
				{
					return;
				}
				ClassHistogram.FindOrAdd(Widget->GetClass()->GetName())++;
			}

			TSharedPtr<FJsonObject> ToJson(const FString& SnapshotType) const
			{
				TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
				Root->SetStringField(TEXT("snapshot_type"), SnapshotType);
				Root->SetStringField(TEXT("root_kind"), RootKind);
				Root->SetNumberField(TEXT("root_child_count"), RootChildCount);
				Root->SetNumberField(TEXT("total_nodes"), TotalNodes);
				Root->SetNumberField(TEXT("leaf_nodes"), LeafNodes);
				Root->SetNumberField(TEXT("container_nodes"), ContainerNodes);
				Root->SetNumberField(TEXT("text_nodes"), TextNodes);
				Root->SetNumberField(TEXT("visual_nodes"), VisualNodes);
				Root->SetNumberField(TEXT("button_nodes"), ButtonNodes);
				Root->SetNumberField(TEXT("placeholder_nodes"), PlaceholderNodes);
				Root->SetNumberField(TEXT("max_depth"), MaxDepth);

				TArray<FString> SortedTexts = VisibleTexts;
				SortedTexts.Sort();
				TArray<TSharedPtr<FJsonValue>> TextValues;
				for (const FString& Text : SortedTexts)
				{
					TextValues.Add(MakeShared<FJsonValueString>(Text));
				}
				Root->SetArrayField(TEXT("visible_texts"), TextValues);

				TArray<FString> SortedBorders = BorderWidgets;
				SortedBorders.Sort();
				TArray<TSharedPtr<FJsonValue>> BorderValues;
				for (const FString& BorderName : SortedBorders)
				{
					BorderValues.Add(MakeShared<FJsonValueString>(BorderName));
				}
				Root->SetArrayField(TEXT("border_widgets"), BorderValues);

				TSharedPtr<FJsonObject> HistogramObject = MakeShared<FJsonObject>();
				for (const TPair<FString, int32>& Pair : KindHistogram)
				{
					HistogramObject->SetNumberField(Pair.Key, Pair.Value);
				}
				Root->SetObjectField(TEXT("kind_histogram"), HistogramObject);

				TSharedPtr<FJsonObject> ClassHistogramObject = MakeShared<FJsonObject>();
				for (const TPair<FString, int32>& Pair : ClassHistogram)
				{
					ClassHistogramObject->SetNumberField(Pair.Key, Pair.Value);
				}
				Root->SetObjectField(TEXT("class_histogram"), ClassHistogramObject);
				return Root;
			}
		};

		FString NormalizeStructureText(const FString& InText)
		{
			FString Normalized = InText;
			Normalized.ReplaceInline(TEXT("\r"), TEXT(" "));
			Normalized.ReplaceInline(TEXT("\n"), TEXT(" "));
			Normalized.ReplaceInline(TEXT("\t"), TEXT(" "));

			TArray<FString> Parts;
			Normalized.ParseIntoArrayWS(Parts);
			return FString::Join(Parts, TEXT(" "));
		}

		FString NormalizeCompactStructureKey(const FString& InValue)
		{
			FString Key = InValue.ToLower().TrimStartAndEnd();
			Key.ReplaceInline(TEXT(" "), TEXT(""));
			Key.ReplaceInline(TEXT("-"), TEXT(""));
			Key.ReplaceInline(TEXT("_"), TEXT(""));
			return Key;
		}

		bool IsIconLikeFontFamily(const FString& FontFamily)
		{
			const FString NormalizedFamily = NormalizeCompactStructureKey(FontFamily);
			return NormalizedFamily.Contains(TEXT("lucide"))
				|| NormalizedFamily.Contains(TEXT("materialsymbol"))
				|| NormalizedFamily.Contains(TEXT("materialicon"));
		}

		FDesignDocument CloneDocumentForSnapshot(const FDesignDocument& SourceDocument)
		{
			FDesignDocument SnapshotDocument;
			SnapshotDocument.Version = SourceDocument.Version;
			SnapshotDocument.Fonts = SourceDocument.Fonts;
			SnapshotDocument.Themes = SourceDocument.Themes;
			SnapshotDocument.Variables = SourceDocument.Variables;
			SnapshotDocument.Metadata = SourceDocument.Metadata;

			SnapshotDocument.Children.Reserve(SourceDocument.Children.Num());
			for (const TSharedPtr<FDesignNode>& Child : SourceDocument.Children)
			{
				SnapshotDocument.Children.Add(FRefExpander::DeepCopyNode(Child));
			}

			return SnapshotDocument;
		}

		bool ShouldSkipDesignNodeForStructure(
			const TSharedPtr<FDesignNode>& Node,
			const TFunction<bool(const TSharedPtr<FDesignNode>&)>& AdditionalSkipNodePredicate)
		{
			if (!Node.IsValid() || Node->IsCollapsed())
			{
				return true;
			}

			return AdditionalSkipNodePredicate && AdditionalSkipNodePredicate(Node);
		}

		TSharedPtr<FDesignNode> ResolveEffectiveDesignNodeForStructure(const TSharedPtr<FDesignNode>& Node, const FDesignDocument& Doc)
		{
			if (!Node.IsValid())
			{
				return nullptr;
			}

			if (Node->Type == EDesignNodeType::Ref)
			{
				if (TSharedPtr<FDesignNode> Expanded = FRefExpander::ExpandRef(Node, Doc))
				{
					return Expanded;
				}
			}

			return Node;
		}

		FString ClassifyDesignNodeKindForStructure(const TSharedPtr<FDesignNode>& Node)
		{
			if (!Node.IsValid())
			{
				return TEXT("unknown");
			}
			if (Node->LayoutData.bSemanticPlaceholderSurface)
			{
				return TEXT("placeholder");
			}

			switch (Node->Type)
			{
			case EDesignNodeType::Frame:
			case EDesignNodeType::Group:
				return TEXT("container");
			case EDesignNodeType::Text:
			case EDesignNodeType::IconFont:
				return TEXT("text");
			case EDesignNodeType::Rectangle:
			case EDesignNodeType::Ellipse:
			case EDesignNodeType::Line:
			case EDesignNodeType::Polygon:
			case EDesignNodeType::Path:
				return TEXT("visual");
			default:
				return TEXT("other");
			}
		}

		void GatherDesignStructureChildren(
			const TSharedPtr<FDesignNode>& Node,
			const FDesignDocument& Doc,
			const TFunction<bool(const TSharedPtr<FDesignNode>&)>& AdditionalSkipNodePredicate,
			TArray<TSharedPtr<FDesignNode>>& OutChildren)
		{
			OutChildren.Reset();
			if (!Node.IsValid())
			{
				return;
			}

			for (const TSharedPtr<FDesignNode>& Child : Node->Children)
			{
				TSharedPtr<FDesignNode> EffectiveChild = ResolveEffectiveDesignNodeForStructure(Child, Doc);
				if (!ShouldSkipDesignNodeForStructure(EffectiveChild, AdditionalSkipNodePredicate))
				{
					OutChildren.Add(EffectiveChild);
				}
			}
		}

		void AccumulateDesignStructureSnapshot(
			const TSharedPtr<FDesignNode>& Node,
			const FDesignDocument& Doc,
			const TFunction<bool(const TSharedPtr<FDesignNode>&)>& AdditionalSkipNodePredicate,
			const int32 Depth,
			FStructureSnapshotStats& Stats)
		{
			TSharedPtr<FDesignNode> EffectiveNode = ResolveEffectiveDesignNodeForStructure(Node, Doc);
			if (ShouldSkipDesignNodeForStructure(EffectiveNode, AdditionalSkipNodePredicate))
			{
				return;
			}

			Stats.TotalNodes++;
			Stats.MaxDepth = FMath::Max(Stats.MaxDepth, Depth);

			const FString Kind = ClassifyDesignNodeKindForStructure(EffectiveNode);
			Stats.AddKind(Kind);
			if (Kind == TEXT("container"))
			{
				Stats.ContainerNodes++;
			}
			else if (Kind == TEXT("text"))
			{
				Stats.TextNodes++;
				const FString Text = (EffectiveNode->Type == EDesignNodeType::IconFont
					|| IsIconLikeFontFamily(EffectiveNode->Text.FontFamily)
					|| IsIconLikeFontFamily(EffectiveNode->Text.IconFontFamily))
					? FString()
					: NormalizeStructureText(EffectiveNode->Text.Content);
				if (!Text.IsEmpty())
				{
					Stats.VisibleTexts.Add(Text);
				}
			}
			else if (Kind == TEXT("visual"))
			{
				Stats.VisualNodes++;
			}
			else if (Kind == TEXT("placeholder"))
			{
				Stats.PlaceholderNodes++;
				Stats.VisualNodes++;
			}

			TArray<TSharedPtr<FDesignNode>> Children;
			GatherDesignStructureChildren(EffectiveNode, Doc, AdditionalSkipNodePredicate, Children);
			if (Depth == 0)
			{
				Stats.RootChildCount = Children.Num();
			}
			if (Children.Num() == 0)
			{
				Stats.LeafNodes++;
				return;
			}

			for (const TSharedPtr<FDesignNode>& Child : Children)
			{
				AccumulateDesignStructureSnapshot(Child, Doc, AdditionalSkipNodePredicate, Depth + 1, Stats);
			}
		}

		TSharedPtr<FDesignNode> FindDesignNodeByIdRecursive(const TSharedPtr<FDesignNode>& Node, const FString& TargetId)
		{
			if (!Node.IsValid())
			{
				return nullptr;
			}
			if (Node->Id == TargetId)
			{
				return Node;
			}

			for (const TSharedPtr<FDesignNode>& Child : Node->Children)
			{
				if (TSharedPtr<FDesignNode> Match = FindDesignNodeByIdRecursive(Child, TargetId))
				{
					return Match;
				}
			}
			return nullptr;
		}

		bool IsWidgetVisibleForStructure(const UWidget* Widget)
		{
			if (!Widget)
			{
				return false;
			}

			const ESlateVisibility Visibility = Widget->GetVisibility();
			return Visibility != ESlateVisibility::Collapsed && Visibility != ESlateVisibility::Hidden;
		}

		void GatherWidgetStructureChildren(UWidget* Widget, TArray<UWidget*>& OutChildren)
		{
			OutChildren.Reset();
			if (!Widget)
			{
				return;
			}

			TSet<UWidget*> Seen;
			auto TryAddChild = [&OutChildren, &Seen](UWidget* Child)
			{
				if (!IsWidgetVisibleForStructure(Child) || Seen.Contains(Child))
				{
					return;
				}
				Seen.Add(Child);
				OutChildren.Add(Child);
			};

			if (UUserWidget* UserWidget = Cast<UUserWidget>(Widget))
			{
				if (UserWidget->WidgetTree && UserWidget->WidgetTree->RootWidget)
				{
					TryAddChild(UserWidget->WidgetTree->RootWidget);
				}
			}

			if (UPanelWidget* PanelWidget = Cast<UPanelWidget>(Widget))
			{
				for (int32 ChildIndex = 0; ChildIndex < PanelWidget->GetChildrenCount(); ++ChildIndex)
				{
					TryAddChild(PanelWidget->GetChildAt(ChildIndex));
				}
				return;
			}

			if (UContentWidget* ContentWidget = Cast<UContentWidget>(Widget))
			{
				TryAddChild(ContentWidget->GetContent());
			}
		}

		FString ClassifyWidgetKindForStructure(UWidget* Widget)
		{
			if (!Widget)
			{
				return TEXT("unknown");
			}
			if (Cast<UTextBlock>(Widget))
			{
				return TEXT("text");
			}
			if (Cast<UButton>(Widget))
			{
				return TEXT("button");
			}
			if (Cast<UImage>(Widget))
			{
				return TEXT("visual");
			}
			if (Cast<USpacer>(Widget))
			{
				return TEXT("spacer");
			}
			if (UBorder* Border = Cast<UBorder>(Widget))
			{
				TArray<UWidget*> Children;
				GatherWidgetStructureChildren(Border, Children);
				return Children.Num() > 0 ? TEXT("container") : TEXT("visual");
			}
			if (Cast<UPanelWidget>(Widget) || Cast<UContentWidget>(Widget))
			{
				return TEXT("container");
			}
			return TEXT("other");
		}

		FString ResolveWidgetStructureText(const UTextBlock* TextBlock)
		{
			if (!TextBlock)
			{
				return FString();
			}

			const FString RawText = NormalizeStructureText(TextBlock->GetText().ToString());
			if (RawText.IsEmpty())
			{
				return FString();
			}

			const FSlateFontInfo& FontInfo = TextBlock->GetFont();
			if (FontInfo.FontObject)
			{
				const FString FontObjectName = FontInfo.FontObject->GetName();
				const FString FontObjectPath = FontInfo.FontObject->GetPathName();
				if (IsIconLikeFontFamily(FontObjectName) || IsIconLikeFontFamily(FontObjectPath))
				{
					return FString();
				}

				const FString SemanticIconName = FFontResolver::FindKnownIconNameByGlyph(FontObjectName, RawText);
				if (!SemanticIconName.IsEmpty())
				{
					return FString();
				}
			}

			return RawText;
		}

		void AccumulateWidgetStructureSnapshot(UWidget* Widget, const int32 Depth, FStructureSnapshotStats& Stats)
		{
			if (!IsWidgetVisibleForStructure(Widget))
			{
				return;
			}

			Stats.TotalNodes++;
			Stats.MaxDepth = FMath::Max(Stats.MaxDepth, Depth);
			Stats.AddClass(Widget);
			if (Cast<UBorder>(Widget))
			{
				Stats.BorderWidgets.Add(Widget->GetName());
			}

			const FString Kind = ClassifyWidgetKindForStructure(Widget);
			Stats.AddKind(Kind);
			if (Kind == TEXT("container"))
			{
				Stats.ContainerNodes++;
			}
			else if (Kind == TEXT("text"))
			{
				Stats.TextNodes++;
				if (const UTextBlock* TextBlock = Cast<UTextBlock>(Widget))
				{
					const FString Text = ResolveWidgetStructureText(TextBlock);
					if (!Text.IsEmpty())
					{
						Stats.VisibleTexts.Add(Text);
					}
				}
			}
			else if (Kind == TEXT("visual"))
			{
				Stats.VisualNodes++;
			}
			else if (Kind == TEXT("button"))
			{
				Stats.ButtonNodes++;
			}

			TArray<UWidget*> Children;
			GatherWidgetStructureChildren(Widget, Children);
			if (Depth == 0)
			{
				Stats.RootChildCount = Children.Num();
			}
			if (Children.Num() == 0)
			{
				Stats.LeafNodes++;
				return;
			}

			for (UWidget* Child : Children)
			{
				AccumulateWidgetStructureSnapshot(Child, Depth + 1, Stats);
			}
		}
	}

	TSharedPtr<FJsonObject> BuildDesignStructureSnapshot(
		const FDesignDocument& Doc,
		const FString& PreferredRootId,
		TFunction<bool(const TSharedPtr<FDesignNode>&)> AdditionalSkipNodePredicate)
	{
		const FDesignDocument SnapshotDoc = CloneDocumentForSnapshot(Doc);
		FStructureSnapshotStats Stats;

		if (!PreferredRootId.IsEmpty())
		{
			for (const TSharedPtr<FDesignNode>& Child : SnapshotDoc.Children)
			{
				if (TSharedPtr<FDesignNode> Match = FindDesignNodeByIdRecursive(Child, PreferredRootId))
				{
					TSharedPtr<FDesignNode> RootNode = ResolveEffectiveDesignNodeForStructure(Match, SnapshotDoc);
					if (ShouldSkipDesignNodeForStructure(RootNode, AdditionalSkipNodePredicate))
					{
						return nullptr;
					}

					Stats.RootKind = ClassifyDesignNodeKindForStructure(RootNode);
					AccumulateDesignStructureSnapshot(RootNode, SnapshotDoc, AdditionalSkipNodePredicate, 0, Stats);
					return Stats.ToJson(TEXT("design_semantic"));
				}
			}
		}

		if (SnapshotDoc.Children.Num() == 1)
		{
			TSharedPtr<FDesignNode> RootNode = ResolveEffectiveDesignNodeForStructure(SnapshotDoc.Children[0], SnapshotDoc);
			if (ShouldSkipDesignNodeForStructure(RootNode, AdditionalSkipNodePredicate))
			{
				return nullptr;
			}

			Stats.RootKind = ClassifyDesignNodeKindForStructure(RootNode);
			AccumulateDesignStructureSnapshot(RootNode, SnapshotDoc, AdditionalSkipNodePredicate, 0, Stats);
			return Stats.ToJson(TEXT("design_semantic"));
		}

		Stats.RootKind = TEXT("document");
		for (const TSharedPtr<FDesignNode>& Child : SnapshotDoc.Children)
		{
			TSharedPtr<FDesignNode> EffectiveChild = ResolveEffectiveDesignNodeForStructure(Child, SnapshotDoc);
			if (ShouldSkipDesignNodeForStructure(EffectiveChild, AdditionalSkipNodePredicate))
			{
				continue;
			}

			Stats.RootChildCount++;
			AccumulateDesignStructureSnapshot(EffectiveChild, SnapshotDoc, AdditionalSkipNodePredicate, 1, Stats);
		}
		return Stats.ToJson(TEXT("design_semantic"));
	}

	TSharedPtr<FJsonObject> BuildWidgetTreeStructureSnapshot(UWidgetTree* WidgetTree)
	{
		if (!WidgetTree || !WidgetTree->RootWidget)
		{
			return nullptr;
		}

		FStructureSnapshotStats Stats;
		Stats.RootKind = ClassifyWidgetKindForStructure(WidgetTree->RootWidget);
		AccumulateWidgetStructureSnapshot(WidgetTree->RootWidget, 0, Stats);
		return Stats.ToJson(TEXT("widget_tree_semantic"));
	}
}
