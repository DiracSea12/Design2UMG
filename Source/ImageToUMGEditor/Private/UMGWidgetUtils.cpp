// Copyright Epic Games, Inc. All Rights Reserved.

#include "UMGWidgetUtils.h"
#include "ImageToUMGLog.h"
#include "ImageToUMGSettings.h"
#include "LayoutCalculator.h"

#include "WidgetBlueprint.h"
#include "Blueprint/UserWidget.h"
#include "Editor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Application/SlateApplication.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Input/HittestGrid.h"
#include "Misc/ConfigCacheIni.h"
#include "Slate/WidgetRenderer.h"
#include "WidgetBlueprintEditorUtils.h"
#include "Widgets/SVirtualWindow.h"

#include "Components/Widget.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/EditableTextBox.h"
#include "Components/CheckBox.h"
#include "Components/Slider.h"
#include "Components/ProgressBar.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/Spacer.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/GridPanel.h"
#include "Components/GridSlot.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/RichTextBlock.h"
#include "Components/CircularThrobber.h"
#include "Components/Throbber.h"
#include "Components/ExpandableArea.h"
#include "Components/ComboBoxString.h"
#include "Components/SpinBox.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/BackgroundBlur.h"
#include "Components/BackgroundBlurSlot.h"
#include "Components/InvalidationBox.h"
#include "Components/RetainerBox.h"
#include "Components/WidgetSwitcher.h"
#include "Components/NamedSlot.h"
#include "Components/NamedSlotInterface.h"
#include "Components/SafeZone.h"
#include "Components/SafeZoneSlot.h"
#include "Components/WidgetSwitcherSlot.h"
#include "Components/ButtonSlot.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralShapeGenerator.h"
#include "Components/ListView.h"
#include "Components/TileView.h"
#include "Components/TreeView.h"
#include "Components/MenuAnchor.h"
#include "Components/EditableText.h"
#include "Components/MultiLineEditableText.h"
#include "Components/ContentWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// ============================================================================
// 静态映射表
// ============================================================================

namespace
{
	// Widget 类型映射表
	const TMap<FString, UClass*>& GetWidgetClassMap()
	{
		static const TMap<FString, UClass*> Map = []() {
			TMap<FString, UClass*> M;
			#define REG(Name) M.Add(TEXT(#Name), U##Name::StaticClass())
			// 容器类
			REG(CanvasPanel); REG(VerticalBox); REG(HorizontalBox); REG(Overlay);
			REG(ScrollBox); REG(GridPanel); REG(UniformGridPanel); REG(WrapBox);
			REG(WidgetSwitcher); REG(SafeZone);
			// 包装类
			REG(Border); REG(SizeBox); REG(ScaleBox); REG(BackgroundBlur);
			REG(InvalidationBox); REG(RetainerBox); REG(ExpandableArea); REG(NamedSlot);
			// 交互类
			REG(Button); REG(CheckBox); REG(Slider); REG(SpinBox);
			// 文本类
			REG(TextBlock); REG(RichTextBlock); REG(EditableTextBox); REG(MultiLineEditableTextBox);
			// 显示类
			REG(Image); REG(ProgressBar); REG(Throbber); REG(CircularThrobber); REG(Spacer);
			// 列表类
			REG(ListView); REG(TileView); REG(TreeView);
			// 其他
			REG(MenuAnchor); REG(EditableText); REG(MultiLineEditableText);
			#undef REG
			// 别名
			M.Add(TEXT("ComboBox"), UComboBoxString::StaticClass());
			M.Add(TEXT("ComboBoxString"), UComboBoxString::StaticClass());
			return M;
		}();
		return Map;
	}

	// 对齐映射表
	const TMap<FString, EHorizontalAlignment>& GetHAlignMap()
	{
		static const TMap<FString, EHorizontalAlignment> Map = {
			{TEXT("Left"), HAlign_Left}, {TEXT("Center"), HAlign_Center},
			{TEXT("Right"), HAlign_Right}, {TEXT("Fill"), HAlign_Fill}
		};
		return Map;
	}

	const TMap<FString, EVerticalAlignment>& GetVAlignMap()
	{
		static const TMap<FString, EVerticalAlignment> Map = {
			{TEXT("Top"), VAlign_Top}, {TEXT("Center"), VAlign_Center},
			{TEXT("Bottom"), VAlign_Bottom}, {TEXT("Fill"), VAlign_Fill}
		};
		return Map;
	}

	const TMap<FString, ESlateVisibility>& GetVisibilityMap()
	{
		static const TMap<FString, ESlateVisibility> Map = {
			{TEXT("Visible"), ESlateVisibility::Visible},
			{TEXT("Hidden"), ESlateVisibility::Hidden},
			{TEXT("Collapsed"), ESlateVisibility::Collapsed},
			{TEXT("HitTestInvisible"), ESlateVisibility::HitTestInvisible},
			{TEXT("SelfHitTestInvisible"), ESlateVisibility::SelfHitTestInvisible}
		};
		return Map;
	}

	// 颜色名称映射
	const TMap<FString, FLinearColor>& GetNamedColors()
	{
		static const TMap<FString, FLinearColor> Map = {
			{TEXT("white"), FLinearColor::White},
			{TEXT("black"), FLinearColor::Black},
			{TEXT("red"), FLinearColor::Red},
			{TEXT("green"), FLinearColor::Green},
			{TEXT("blue"), FLinearColor::Blue},
			{TEXT("yellow"), FLinearColor::Yellow},
			{TEXT("gray"), FLinearColor::Gray},
			{TEXT("grey"), FLinearColor::Gray},
			{TEXT("transparent"), FLinearColor::Transparent}
		};
		return Map;
	}

	bool IsNearlyEqualMargin(const FMargin& A, const FMargin& B)
	{
		return FMath::IsNearlyEqual(A.Left, B.Left) &&
			FMath::IsNearlyEqual(A.Top, B.Top) &&
			FMath::IsNearlyEqual(A.Right, B.Right) &&
			FMath::IsNearlyEqual(A.Bottom, B.Bottom);
	}

	bool HasMeaningfulSlotData(const FUMGSlotData& Slot)
	{
		const FUMGSlotData Default;
		const bool bAnchors =
			!FMath::IsNearlyEqual(Slot.Anchors.Min.X, Default.Anchors.Min.X) ||
			!FMath::IsNearlyEqual(Slot.Anchors.Min.Y, Default.Anchors.Min.Y) ||
			!FMath::IsNearlyEqual(Slot.Anchors.Max.X, Default.Anchors.Max.X) ||
			!FMath::IsNearlyEqual(Slot.Anchors.Max.Y, Default.Anchors.Max.Y);
		const bool bAlignment =
			!FMath::IsNearlyEqual(Slot.Alignment.X, Default.Alignment.X) ||
			!FMath::IsNearlyEqual(Slot.Alignment.Y, Default.Alignment.Y);
		const bool bOffsets =
			!FMath::IsNearlyEqual(Slot.Offsets.Left, Default.Offsets.Left) ||
			!FMath::IsNearlyEqual(Slot.Offsets.Top, Default.Offsets.Top) ||
			!FMath::IsNearlyEqual(Slot.Offsets.Right, Default.Offsets.Right) ||
			!FMath::IsNearlyEqual(Slot.Offsets.Bottom, Default.Offsets.Bottom);
		const bool bPadding = !IsNearlyEqualMargin(Slot.Padding, Default.Padding);
		const bool bBox =
			Slot.HorizontalAlignment != Default.HorizontalAlignment ||
			Slot.VerticalAlignment != Default.VerticalAlignment ||
			Slot.SizeRule != Default.SizeRule ||
			!FMath::IsNearlyEqual(Slot.FillWeight, Default.FillWeight);
		const bool bGrid =
			Slot.Row != Default.Row ||
			Slot.Column != Default.Column ||
			Slot.RowSpan != Default.RowSpan ||
			Slot.ColumnSpan != Default.ColumnSpan;
		return bAnchors || bAlignment || bOffsets || bPadding || bBox || bGrid;
	}

	bool HasExplicitGridData(const FUMGSlotData& Slot)
	{
		const FUMGSlotData Default;
		return Slot.Row != Default.Row ||
			Slot.Column != Default.Column ||
			Slot.RowSpan != Default.RowSpan ||
			Slot.ColumnSpan != Default.ColumnSpan;
	}

	bool HasValidBounds(const FBounds& Bounds)
	{
		return Bounds.W > 0.0f && Bounds.H > 0.0f;
	}

	struct FAutoGridItem
	{
		int32 Index = INDEX_NONE;
		float CenterX = 0.0f;
		float CenterY = 0.0f;
		float Height = 0.0f;
	};

	bool ShouldAutoAssignGridSlots(const TArray<FUMGWidgetNode>& Children)
	{
		for (const FUMGWidgetNode& Child : Children)
		{
			if (!HasExplicitGridData(Child.Slot) && HasValidBounds(Child.Bounds))
			{
				return true;
			}
		}
		return false;
	}

	void BuildAutoGridSlots(const TArray<FUMGWidgetNode>& Children, TMap<int32, FIntPoint>& OutSlots)
	{
		OutSlots.Reset();

		TSet<int32> ExplicitIndices;
		TSet<FIntPoint> OccupiedCells;
		for (int32 Index = 0; Index < Children.Num(); ++Index)
		{
			const FUMGWidgetNode& Child = Children[Index];
			if (HasExplicitGridData(Child.Slot))
			{
				const FIntPoint Cell(Child.Slot.Row, Child.Slot.Column);
				OutSlots.Add(Index, Cell);
				OccupiedCells.Add(Cell);
				ExplicitIndices.Add(Index);
			}
		}

		TArray<FAutoGridItem> Items;
		Items.Reserve(Children.Num());

		float HeightSum = 0.0f;
		for (int32 Index = 0; Index < Children.Num(); ++Index)
		{
			const FUMGWidgetNode& Child = Children[Index];
			if (!HasValidBounds(Child.Bounds))
			{
				continue;
			}

			FAutoGridItem Item;
			Item.Index = Index;
			Item.CenterX = Child.Bounds.GetCenterX();
			Item.CenterY = Child.Bounds.GetCenterY();
			Item.Height = Child.Bounds.H;
			Items.Add(Item);
			HeightSum += Child.Bounds.H;
		}

		if (Items.Num() == 0)
		{
			return;
		}

		const float AvgHeight = HeightSum / Items.Num();
		const float RowThreshold = FMath::Max(AvgHeight * 0.5f, 0.01f);

		Items.Sort([](const FAutoGridItem& A, const FAutoGridItem& B)
		{
			if (FMath::IsNearlyEqual(A.CenterY, B.CenterY))
			{
				return A.CenterX < B.CenterX;
			}
			return A.CenterY < B.CenterY;
		});

		TArray<TArray<FAutoGridItem>> Rows;
		float CurrentRowCenter = 0.0f;
		int32 CurrentRowCount = 0;

		for (const FAutoGridItem& Item : Items)
		{
			if (Rows.Num() == 0)
			{
				Rows.Add({ Item });
				CurrentRowCenter = Item.CenterY;
				CurrentRowCount = 1;
				continue;
			}

			if (FMath::Abs(Item.CenterY - CurrentRowCenter) <= RowThreshold)
			{
				Rows.Last().Add(Item);
				CurrentRowCenter = (CurrentRowCenter * CurrentRowCount + Item.CenterY) / (CurrentRowCount + 1);
				++CurrentRowCount;
				continue;
			}

			Rows.Add({ Item });
			CurrentRowCenter = Item.CenterY;
			CurrentRowCount = 1;
		}

		for (int32 RowIndex = 0; RowIndex < Rows.Num(); ++RowIndex)
		{
			TArray<FAutoGridItem>& RowItems = Rows[RowIndex];
			RowItems.Sort([](const FAutoGridItem& A, const FAutoGridItem& B)
			{
				return A.CenterX < B.CenterX;
			});

			for (int32 ColIndex = 0; ColIndex < RowItems.Num(); ++ColIndex)
			{
				const FAutoGridItem& Item = RowItems[ColIndex];
				if (ExplicitIndices.Contains(Item.Index))
				{
					continue;
				}

				FIntPoint Cell(RowIndex, ColIndex);
				while (OccupiedCells.Contains(Cell))
				{
					Cell.Y++;
				}
				OutSlots.Add(Item.Index, Cell);
				OccupiedCells.Add(Cell);
			}
		}
	}

	// 前向声明 Image 控件辅助函数（定义在后面）
	bool HandleImageMaterialRef(UImage* ImageWidget, const FUMGWidgetNode& Node);
	bool HandleImageTextureRef(UImage* ImageWidget, const FUMGWidgetNode& Node, const TMap<FString, UTexture2D*>* TextureMap);
}

// ============================================================================
// Widget 创建（统一入口）
// ============================================================================

UWidget* FUMGWidgetUtils::CreateWidgetRecursive(UWidgetTree* WidgetTree, const FUMGWidgetNode& Node, UPanelWidget* ParentWidget, const TMap<FString, UTexture2D*>* TextureMap)
{
	UClass* WidgetClass = ResolveWidgetClass(Node.Type);
	if (!WidgetClass)
	{
		// v2.2: 增强警告，明确说明子树丢失
		int32 ChildCount = Node.Children.Num();
		if (ChildCount > 0)
		{
			UE_LOG(LogImageToUMG, Warning, TEXT("Unknown widget type '%s' (Id: %s), skipping with %d children"),
				*Node.Type, *Node.Id, ChildCount);
		}
		else
		{
			UE_LOG(LogImageToUMG, Warning, TEXT("Unknown widget type '%s' (Id: %s), skipping"),
				*Node.Type, *Node.Id);
		}
		return nullptr;
	}

	FName WidgetName = SanitizeWidgetName(Node.Name, Node.Type);
	UWidget* NewWidget = WidgetTree->ConstructWidget<UWidget>(WidgetClass, WidgetName);
	if (!NewWidget)
	{
		UE_LOG(LogImageToUMG, Error, TEXT("Failed to construct widget: %s"), *Node.Name);
		return nullptr;
	}

	// 添加到父容器并应用 Slot 属性
	if (ParentWidget)
	{
		if (UPanelSlot* Slot = ParentWidget->AddChild(NewWidget))
		{
			// v2.2: 只对CanvasPanel使用LayoutCalculator计算Slot
			// 其他容器（VerticalBox/HorizontalBox/Overlay）使用原始Slot数据
			FUMGSlotData SlotData = Node.Slot;
			UCanvasPanel* CanvasParent = Cast<UCanvasPanel>(ParentWidget);
			if (CanvasParent && Node.Bounds.W > 0.0f && Node.Bounds.H > 0.0f)
			{
				FLayoutContext Context;
				Context.DesignSize = UImageToUMGSettings::Get()->GetDesignSize();
				Context.Preset = FLayoutPreset::GetStandardHUDPreset();
				// v2.2: Hysteresis - 只有当Slot有有效历史数据时才设置OldAnchor
				const bool bHasValidSlot =
					!FMath::IsNearlyEqual(Node.Slot.Offsets.Left, 0.0f) ||
					!FMath::IsNearlyEqual(Node.Slot.Offsets.Top, 0.0f) ||
					!FMath::IsNearlyEqual(Node.Slot.Offsets.Right, 100.0f) ||
					!FMath::IsNearlyEqual(Node.Slot.Offsets.Bottom, 100.0f) ||
					!FMath::IsNearlyEqual(Node.Slot.Anchors.Min.X, 0.0f) ||
					!FMath::IsNearlyEqual(Node.Slot.Anchors.Min.Y, 0.0f) ||
					!FMath::IsNearlyEqual(Node.Slot.Anchors.Max.X, 0.0f) ||
					!FMath::IsNearlyEqual(Node.Slot.Anchors.Max.Y, 0.0f) ||
					!FMath::IsNearlyEqual(Node.Slot.Alignment.X, 0.0f) ||
					!FMath::IsNearlyEqual(Node.Slot.Alignment.Y, 0.0f);
				if (bHasValidSlot)
				{
					if (Node.Slot.Anchors.Min.X == Node.Slot.Anchors.Max.X)
					{
						Context.OldAnchorX = Node.Slot.Anchors.Min.X;
					}
					if (Node.Slot.Anchors.Min.Y == Node.Slot.Anchors.Max.Y)
					{
						Context.OldAnchorY = Node.Slot.Anchors.Min.Y;
					}
				}
				SlotData = FLayoutCalculator::CalculateSlot(Node.Bounds, Context);
			}
			ApplySlotProperties(Slot, SlotData);
		}
	}

	ApplyWidgetProperties(NewWidget, Node.Properties);

	// Image 控件：优先 MaterialRef，其次 TextureMap
	if (UImage* ImageWidget = Cast<UImage>(NewWidget))
	{
		bool bBound = HandleImageMaterialRef(ImageWidget, Node) || HandleImageTextureRef(ImageWidget, Node, TextureMap);

		if (!bBound && !Node.Properties.Contains(TEXT("MaterialRef")) && !Node.Properties.Contains(TEXT("ImageRef")))
		{
			UE_LOG(LogImageToUMG, Warning, TEXT("Image '%s' 没有 MaterialRef 或 ImageRef 属性"), *Node.Name);
		}
	}

	// 递归处理子控件
		if (UPanelWidget* AsPanel = Cast<UPanelWidget>(NewWidget))
	{
		TMap<int32, FIntPoint> AutoGridSlots;
		if ((AsPanel->IsA<UGridPanel>() || AsPanel->IsA<UUniformGridPanel>()) && ShouldAutoAssignGridSlots(Node.Children))
		{
			BuildAutoGridSlots(Node.Children, AutoGridSlots);
		}

		for (int32 ChildIndex = 0; ChildIndex < Node.Children.Num(); ++ChildIndex)
		{
			const FUMGWidgetNode& Child = Node.Children[ChildIndex];
			if (const FIntPoint* SlotIndex = AutoGridSlots.Find(ChildIndex))
			{
				FUMGWidgetNode ChildCopy = Child;
				ChildCopy.Slot.Row = SlotIndex->X;
				ChildCopy.Slot.Column = SlotIndex->Y;
				CreateWidgetRecursive(WidgetTree, ChildCopy, AsPanel, TextureMap);
			}
			else
			{
				CreateWidgetRecursive(WidgetTree, Child, AsPanel, TextureMap);
			}
		}
	}
	else if (INamedSlotInterface* NamedSlotHost = Cast<INamedSlotInterface>(NewWidget))
	{
		TArray<FName> SlotNames;
		NamedSlotHost->GetSlotNames(SlotNames);

		if (SlotNames.Num() == 0)
		{
			if (Node.Children.Num() > 0)
			{
				UE_LOG(LogImageToUMG, Warning, TEXT("NamedSlot 宿主 '%s' 没有槽位名称但有 %d 个子控件"),
					*Node.Name, Node.Children.Num());
			}
		}
		else
		{
			TSet<FName> UsedSlots;
			TMap<FName, UWidget*> SlotContent;
			int32 NextSlotIndex = 0;

			auto AppendToSlot = [&](const FName& SlotName, UWidget* ChildWidget, const FUMGWidgetNode& ChildNode)
			{
				if (UWidget** ExistingPtr = SlotContent.Find(SlotName))
				{
					if (UPanelWidget* Panel = Cast<UPanelWidget>(*ExistingPtr))
					{
						if (UPanelSlot* NewSlot = Panel->AddChild(ChildWidget))
						{
							ApplySlotProperties(NewSlot, ChildNode.Slot);
						}
						return;
					}

					FName WrapName = SanitizeWidgetName(Node.Name + TEXT("_SlotWrap_") + SlotName.ToString(), TEXT("SlotWrap"));
					if (UVerticalBox* WrapBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), WrapName))
					{
						WrapBox->AddChild(*ExistingPtr);
						if (UPanelSlot* NewSlot = WrapBox->AddChild(ChildWidget))
						{
							ApplySlotProperties(NewSlot, ChildNode.Slot);
						}
						NamedSlotHost->SetContentForSlot(SlotName, WrapBox);
						SlotContent[SlotName] = WrapBox;
					}
					return;
				}

				NamedSlotHost->SetContentForSlot(SlotName, ChildWidget);
				SlotContent.Add(SlotName, ChildWidget);
			};

			for (const FUMGWidgetNode& ChildNode : Node.Children)
			{
				UWidget* ChildWidget = CreateWidgetRecursive(WidgetTree, ChildNode, nullptr, TextureMap);
				if (!ChildWidget)
				{
					continue;
				}

				FName TargetSlot = NAME_None;
				const FString* SlotNameProp = ChildNode.Properties.Find(TEXT("SlotName"));
				if (!SlotNameProp) SlotNameProp = ChildNode.Properties.Find(TEXT("NamedSlot"));
				if (SlotNameProp && !SlotNameProp->IsEmpty())
				{
					for (const FName& SlotName : SlotNames)
					{
						if (SlotName.ToString().Equals(*SlotNameProp, ESearchCase::IgnoreCase))
						{
							TargetSlot = SlotName;
							break;
						}
					}
				}

				if (TargetSlot.IsNone())
				{
					while (NextSlotIndex < SlotNames.Num() && UsedSlots.Contains(SlotNames[NextSlotIndex]))
					{
						++NextSlotIndex;
					}
					if (NextSlotIndex < SlotNames.Num())
					{
						TargetSlot = SlotNames[NextSlotIndex];
						++NextSlotIndex;
					}
				}

				bool bFallbackAppend = false;
				if (TargetSlot.IsNone() && SlotNames.Num() > 0)
				{
					TargetSlot = SlotNames.Last();
					bFallbackAppend = true;
				}

				if (TargetSlot.IsNone())
				{
					UE_LOG(LogImageToUMG, Warning, TEXT("NamedSlot 宿主 '%s' 没有可用槽位，子控件 '%s' 已跳过"),
						*Node.Name, *ChildNode.Name);
					continue;
				}

				if (HasMeaningfulSlotData(ChildNode.Slot))
				{
					UE_LOG(LogImageToUMG, Warning, TEXT("NamedSlot 子控件 '%s' 的 Slot 不生效，已忽略（宿主 '%s'）"),
						*ChildNode.Name, *Node.Name);
				}

				if (bFallbackAppend)
				{
					UE_LOG(LogImageToUMG, Warning, TEXT("NamedSlot 宿主 '%s' 槽位已满，子控件 '%s' 追加到 '%s'"),
						*Node.Name, *ChildNode.Name, *TargetSlot.ToString());
				}
				else if (SlotContent.Contains(TargetSlot))
				{
					UE_LOG(LogImageToUMG, Warning, TEXT("NamedSlot '%s' 在 '%s' 已存在内容，合并子控件 '%s'"),
						*TargetSlot.ToString(), *Node.Name, *ChildNode.Name);
				}

				AppendToSlot(TargetSlot, ChildWidget, ChildNode);
				UsedSlots.Add(TargetSlot);
			}
		}
	}
	else if (UContentWidget* Content = Cast<UContentWidget>(NewWidget))
	{
		if (Node.Children.Num() > 0)
		{
			const FUMGWidgetNode& ChildNode = Node.Children[0];
			if (UWidget* ChildWidget = CreateWidgetRecursive(WidgetTree, ChildNode, nullptr, TextureMap))
			{
				if (UPanelSlot* ContentSlot = Content->SetContent(ChildWidget))
				{
					ApplySlotProperties(ContentSlot, ChildNode.Slot);
				}

				// v2.2: 将子控件的Alignment应用到ContentWidget
				// ContentWidget不使用CanvasSlot，而是通过自身属性控制子控件布局
				if (UBorder* Border = Cast<UBorder>(Content))
				{
					EHorizontalAlignment HAlign = FUMGWidgetUtils::ParseHAlign(ChildNode.Slot.HorizontalAlignment);
					EVerticalAlignment VAlign = FUMGWidgetUtils::ParseVAlign(ChildNode.Slot.VerticalAlignment);
					if (ChildNode.Slot.HorizontalAlignment == TEXT("Fill") && ChildNode.Slot.VerticalAlignment == TEXT("Fill"))
					{
						const int32 HIndex = FMath::RoundToInt(ChildNode.Slot.Alignment.X * 2.0f);
						const int32 VIndex = FMath::RoundToInt(ChildNode.Slot.Alignment.Y * 2.0f);
						static const EHorizontalAlignment HMap[] = { HAlign_Left, HAlign_Center, HAlign_Right };
						static const EVerticalAlignment VMap[] = { VAlign_Top, VAlign_Center, VAlign_Bottom };
						if (HIndex >= 0 && HIndex < 3) HAlign = HMap[HIndex];
						if (VIndex >= 0 && VIndex < 3) VAlign = VMap[VIndex];
					}
					Border->SetHorizontalAlignment(HAlign);
					Border->SetVerticalAlignment(VAlign);
				}
			}
			if (Node.Children.Num() > 1)
			{
				UE_LOG(LogImageToUMG, Warning, TEXT("ContentWidget '%s' ignoring %d extra children"), *Node.Name, Node.Children.Num() - 1);
			}
		}
	}

	return NewWidget;
}

// ============================================================================
// Fixer 应用
// ============================================================================

namespace
{
	// 将FUMGWidgetNode转换为FFixerWidgetNode
	void ConvertToFixerNode(const FUMGWidgetNode& Src, FFixerWidgetNode& Dst)
	{
		Dst.Id = Src.Id;
		Dst.Type = Src.Type;
		Dst.Bounds = Src.Bounds;
		Dst.Confidence = Src.Meta.Confidence;
		Dst.bFixExempt = Src.Meta.bFixExempt;

		for (const FUMGWidgetNode& Child : Src.Children)
		{
			FFixerWidgetNode ChildDst;
			ConvertToFixerNode(Child, ChildDst);
			Dst.Children.Add(ChildDst);
		}
	}

	// 将修正后的Bounds写回FUMGWidgetNode
	void ApplyFixedBounds(FUMGWidgetNode& Dst, const FFixerWidgetNode& Src)
	{
		Dst.Bounds = Src.Bounds;

		for (int32 i = 0; i < Dst.Children.Num() && i < Src.Children.Num(); ++i)
		{
			ApplyFixedBounds(Dst.Children[i], Src.Children[i]);
		}
	}
}

void FUMGWidgetUtils::ApplyFixer(FUMGWidgetNode& RootNode, const FIntPoint& SourceSize, const FIntPoint& DesignSize)
{
	// 初始化Fixer预设
	FFixerPreset Preset;
	FLayoutFixer::Initialize(DesignSize, Preset);

	// 转换为Fixer节点
	FFixerWidgetNode FixerRoot;
	ConvertToFixerNode(RootNode, FixerRoot);

	// 执行修正
	TArray<FFixerLogEntry> Log;
	FLayoutFixer::FixWidgetTree(FixerRoot, Preset, Log);

	// 写回修正后的Bounds
	ApplyFixedBounds(RootNode, FixerRoot);

	// 输出修正日志
	for (const FFixerLogEntry& Entry : Log)
	{
		UE_LOG(LogImageToUMG, Verbose, TEXT("Fixer [%s] %s: (%.4f,%.4f,%.4f,%.4f) -> (%.4f,%.4f,%.4f,%.4f)"),
			*Entry.WidgetId, *Entry.FixType,
			Entry.OldBounds.X, Entry.OldBounds.Y, Entry.OldBounds.W, Entry.OldBounds.H,
			Entry.NewBounds.X, Entry.NewBounds.Y, Entry.NewBounds.W, Entry.NewBounds.H);
	}
}

// ============================================================================
// Widget 类型解析
// ============================================================================

UClass* FUMGWidgetUtils::ResolveWidgetClass(const FString& TypeName)
{
	if (UClass* const* Found = GetWidgetClassMap().Find(TypeName))
	{
		return *Found;
	}

	static TMap<FString, UClass*> DynamicClassCache;
	if (UClass* const* Cached = DynamicClassCache.Find(TypeName))
	{
		return *Cached;
	}

	FString ClassName = FString::Printf(TEXT("U%s"), *TypeName);
	UClass* FoundClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::EnsureIfAmbiguous);
	if (FoundClass && FoundClass->IsChildOf(UWidget::StaticClass()))
	{
		DynamicClassCache.Add(TypeName, FoundClass);
		return FoundClass;
	}

	DynamicClassCache.Add(TypeName, nullptr);
	return nullptr;
}


// ============================================================================
// 颜色解析（简化版：只支持 #RRGGBB 和颜色名称）
// ============================================================================

FLinearColor FUMGWidgetUtils::ParseColor(const FString& ColorStr)
{
	FString Trimmed = ColorStr.TrimStartAndEnd();

	if (Trimmed.StartsWith(TEXT("#")))
	{
		return FColor::FromHex(Trimmed);
	}

	if (const FLinearColor* Named = GetNamedColors().Find(Trimmed.ToLower()))
	{
		return *Named;
	}

	return FLinearColor::White;
}

// ============================================================================
// 对齐方式解析
// ============================================================================

EHorizontalAlignment FUMGWidgetUtils::ParseHAlign(const FString& Str)
{
	const EHorizontalAlignment* Found = GetHAlignMap().Find(Str);
	return Found ? *Found : HAlign_Fill;
}

EVerticalAlignment FUMGWidgetUtils::ParseVAlign(const FString& Str)
{
	const EVerticalAlignment* Found = GetVAlignMap().Find(Str);
	return Found ? *Found : VAlign_Fill;
}

ESlateVisibility FUMGWidgetUtils::ParseVisibility(const FString& Str)
{
	const ESlateVisibility* Found = GetVisibilityMap().Find(Str);
	return Found ? *Found : ESlateVisibility::Visible;
}

// ============================================================================
// 几何解析
// ============================================================================

FVector2D FUMGWidgetUtils::ParseVector2D(const FString& Str, FVector2D Default)
{
	TArray<FString> Parts;
	Str.ParseIntoArray(Parts, TEXT(","));
	if (Parts.Num() == 2)
	{
		return FVector2D(
			FCString::Atof(*Parts[0].TrimStartAndEnd()),
			FCString::Atof(*Parts[1].TrimStartAndEnd())
		);
	}
	return Default;
}

FMargin FUMGWidgetUtils::ParseMargin(const FString& Str)
{
	TArray<FString> Parts;
	Str.ParseIntoArray(Parts, TEXT(","));
	if (Parts.Num() == 1)
	{
		return FMargin(FCString::Atof(*Parts[0]));
	}
	if (Parts.Num() == 4)
	{
		return FMargin(
			FCString::Atof(*Parts[0].TrimStartAndEnd()),
			FCString::Atof(*Parts[1].TrimStartAndEnd()),
			FCString::Atof(*Parts[2].TrimStartAndEnd()),
			FCString::Atof(*Parts[3].TrimStartAndEnd())
		);
	}
	return FMargin(0);
}

// ============================================================================
// Widget 名称处理
// ============================================================================

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

// ============================================================================
// Slot 属性应用（函数指针映射表模式）
// ============================================================================

namespace
{
	using FApplySlotFunc = void(*)(UPanelSlot*, const FUMGSlotData&);

	void ApplyCanvasPanelSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		UCanvasPanelSlot* CS = CastChecked<UCanvasPanelSlot>(Slot);
		FAnchors Anchors;
		Anchors.Minimum = FVector2D(S.Anchors.Min.X, S.Anchors.Min.Y);
		Anchors.Maximum = FVector2D(S.Anchors.Max.X, S.Anchors.Max.Y);
		CS->SetAnchors(Anchors);
		CS->SetAlignment(FVector2D(S.Alignment.X, S.Alignment.Y));
		CS->SetOffsets(FMargin(S.Offsets.Left, S.Offsets.Top, S.Offsets.Right, S.Offsets.Bottom));
	}

	// VBox/HBox Slot 共用逻辑
	template<typename TSlot>
	void ApplyBoxSlotCommon(TSlot* BoxSlot, const FUMGSlotData& S)
	{
		BoxSlot->SetPadding(S.Padding);
		BoxSlot->SetHorizontalAlignment(FUMGWidgetUtils::ParseHAlign(S.HorizontalAlignment));
		BoxSlot->SetVerticalAlignment(FUMGWidgetUtils::ParseVAlign(S.VerticalAlignment));
		if (S.SizeRule == TEXT("Fill"))
		{
			FSlateChildSize Size;
			Size.SizeRule = ESlateSizeRule::Fill;
			Size.Value = S.FillWeight;
			BoxSlot->SetSize(Size);
		}
	}

	void ApplyVerticalBoxSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		ApplyBoxSlotCommon(CastChecked<UVerticalBoxSlot>(Slot), S);
	}

	void ApplyHorizontalBoxSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		ApplyBoxSlotCommon(CastChecked<UHorizontalBoxSlot>(Slot), S);
	}

	void ApplyOverlaySlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		UOverlaySlot* OS = CastChecked<UOverlaySlot>(Slot);
		OS->SetPadding(S.Padding);
		OS->SetHorizontalAlignment(FUMGWidgetUtils::ParseHAlign(S.HorizontalAlignment));
		OS->SetVerticalAlignment(FUMGWidgetUtils::ParseVAlign(S.VerticalAlignment));
	}

	void ApplyGridSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		UGridSlot* GS = CastChecked<UGridSlot>(Slot);
		GS->SetPadding(S.Padding);
		GS->SetRow(S.Row);
		GS->SetColumn(S.Column);
		GS->SetRowSpan(S.RowSpan);
		GS->SetColumnSpan(S.ColumnSpan);
		GS->SetHorizontalAlignment(FUMGWidgetUtils::ParseHAlign(S.HorizontalAlignment));
		GS->SetVerticalAlignment(FUMGWidgetUtils::ParseVAlign(S.VerticalAlignment));
	}

	void ApplyUniformGridSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		UUniformGridSlot* UGS = CastChecked<UUniformGridSlot>(Slot);
		UGS->SetRow(S.Row);
		UGS->SetColumn(S.Column);
		UGS->SetHorizontalAlignment(FUMGWidgetUtils::ParseHAlign(S.HorizontalAlignment));
		UGS->SetVerticalAlignment(FUMGWidgetUtils::ParseVAlign(S.VerticalAlignment));
	}

	void ApplyWrapBoxSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		UWrapBoxSlot* WS = CastChecked<UWrapBoxSlot>(Slot);
		WS->SetPadding(S.Padding);
		WS->SetHorizontalAlignment(FUMGWidgetUtils::ParseHAlign(S.HorizontalAlignment));
		WS->SetVerticalAlignment(FUMGWidgetUtils::ParseVAlign(S.VerticalAlignment));
		WS->SetFillEmptySpace(S.SizeRule == TEXT("Fill"));
	}

	void ApplyScrollBoxSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		UScrollBoxSlot* SS = CastChecked<UScrollBoxSlot>(Slot);
		SS->SetPadding(S.Padding);
		SS->SetHorizontalAlignment(FUMGWidgetUtils::ParseHAlign(S.HorizontalAlignment));
		SS->SetVerticalAlignment(FUMGWidgetUtils::ParseVAlign(S.VerticalAlignment));
		if (S.SizeRule == TEXT("Fill"))
		{
			FSlateChildSize Size;
			Size.SizeRule = ESlateSizeRule::Fill;
			Size.Value = S.FillWeight;
			SS->SetSize(Size);
		}
	}

	void ApplyBorderSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		UBorderSlot* BS = CastChecked<UBorderSlot>(Slot);
		BS->SetPadding(S.Padding);
		BS->SetHorizontalAlignment(FUMGWidgetUtils::ParseHAlign(S.HorizontalAlignment));
		BS->SetVerticalAlignment(FUMGWidgetUtils::ParseVAlign(S.VerticalAlignment));
	}

	void ApplyButtonSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		UButtonSlot* BS = CastChecked<UButtonSlot>(Slot);
		BS->SetPadding(S.Padding);
		BS->SetHorizontalAlignment(FUMGWidgetUtils::ParseHAlign(S.HorizontalAlignment));
		BS->SetVerticalAlignment(FUMGWidgetUtils::ParseVAlign(S.VerticalAlignment));
	}

	void ApplySizeBoxSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		USizeBoxSlot* SS = CastChecked<USizeBoxSlot>(Slot);
		SS->SetPadding(S.Padding);
		SS->SetHorizontalAlignment(FUMGWidgetUtils::ParseHAlign(S.HorizontalAlignment));
		SS->SetVerticalAlignment(FUMGWidgetUtils::ParseVAlign(S.VerticalAlignment));
	}

	void ApplyScaleBoxSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		UScaleBoxSlot* SS = CastChecked<UScaleBoxSlot>(Slot);
		SS->SetHorizontalAlignment(FUMGWidgetUtils::ParseHAlign(S.HorizontalAlignment));
		SS->SetVerticalAlignment(FUMGWidgetUtils::ParseVAlign(S.VerticalAlignment));
	}

	void ApplyBackgroundBlurSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		UBackgroundBlurSlot* BS = CastChecked<UBackgroundBlurSlot>(Slot);
		BS->SetPadding(S.Padding);
		BS->SetHorizontalAlignment(FUMGWidgetUtils::ParseHAlign(S.HorizontalAlignment));
		BS->SetVerticalAlignment(FUMGWidgetUtils::ParseVAlign(S.VerticalAlignment));
	}

	void ApplyWidgetSwitcherSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		UWidgetSwitcherSlot* WS = CastChecked<UWidgetSwitcherSlot>(Slot);
		WS->SetPadding(S.Padding);
		WS->SetHorizontalAlignment(FUMGWidgetUtils::ParseHAlign(S.HorizontalAlignment));
		WS->SetVerticalAlignment(FUMGWidgetUtils::ParseVAlign(S.VerticalAlignment));
	}

	void ApplySafeZoneSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		USafeZoneSlot* SZ = CastChecked<USafeZoneSlot>(Slot);
		SZ->SetPadding(S.Padding);
		SZ->SetHorizontalAlignment(FUMGWidgetUtils::ParseHAlign(S.HorizontalAlignment));
		SZ->SetVerticalAlignment(FUMGWidgetUtils::ParseVAlign(S.VerticalAlignment));
	}

	const TMap<UClass*, FApplySlotFunc>& GetSlotApplierMap()
	{
		static const TMap<UClass*, FApplySlotFunc> Map = {
			{UCanvasPanelSlot::StaticClass(), &ApplyCanvasPanelSlot},
			{UVerticalBoxSlot::StaticClass(), &ApplyVerticalBoxSlot},
			{UHorizontalBoxSlot::StaticClass(), &ApplyHorizontalBoxSlot},
			{UOverlaySlot::StaticClass(), &ApplyOverlaySlot},
			{UGridSlot::StaticClass(), &ApplyGridSlot},
			{UUniformGridSlot::StaticClass(), &ApplyUniformGridSlot},
			{UWrapBoxSlot::StaticClass(), &ApplyWrapBoxSlot},
			{UScrollBoxSlot::StaticClass(), &ApplyScrollBoxSlot},
			{UBorderSlot::StaticClass(), &ApplyBorderSlot},
			{UButtonSlot::StaticClass(), &ApplyButtonSlot},
			{USizeBoxSlot::StaticClass(), &ApplySizeBoxSlot},
			{UScaleBoxSlot::StaticClass(), &ApplyScaleBoxSlot},
			{UBackgroundBlurSlot::StaticClass(), &ApplyBackgroundBlurSlot},
			{UWidgetSwitcherSlot::StaticClass(), &ApplyWidgetSwitcherSlot},
			{USafeZoneSlot::StaticClass(), &ApplySafeZoneSlot},
		};
		return Map;
	}

	// ============================================================================
	// Image 控件辅助函数
	// ============================================================================

	/**
	 * 处理 Image 控件的 MaterialRef（程序化形状生成）
	 * @return true 如果成功生成并绑定纹理
	 */
	bool HandleImageMaterialRef(UImage* ImageWidget, const FUMGWidgetNode& Node)
	{
		const FString* MaterialRef = Node.Properties.Find(TEXT("MaterialRef"));
		if (!MaterialRef) return false;

		UE_LOG(LogImageToUMG, Verbose, TEXT("Image '%s' 程序化生成 MaterialRef='%s'"), *Node.Name, **MaterialRef);

		// 解析参数
		FLinearColor Color = FLinearColor::White;
		FLinearColor ColorStart = FLinearColor::White;
		FLinearColor ColorEnd = FLinearColor::Black;
		float Thickness = 0.15f;
		float CornerRadius = 0.2f;

		// 解析 MaterialParams
		if (const FString* ParamsStr = Node.Properties.Find(TEXT("MaterialParams")))
		{
			TSharedPtr<FJsonObject> ParamsJson;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(*ParamsStr);
			if (FJsonSerializer::Deserialize(Reader, ParamsJson) && ParamsJson.IsValid())
			{
				FString ColorStr;
				if (ParamsJson->TryGetStringField(TEXT("Color"), ColorStr))
				{
					Color = FProceduralShapeGenerator::ParseHexColor(ColorStr);
				}
				if (ParamsJson->TryGetStringField(TEXT("ColorStart"), ColorStr) ||
					ParamsJson->TryGetStringField(TEXT("ColorCenter"), ColorStr))
				{
					ColorStart = FProceduralShapeGenerator::ParseHexColor(ColorStr);
				}
				if (ParamsJson->TryGetStringField(TEXT("ColorEnd"), ColorStr) ||
					ParamsJson->TryGetStringField(TEXT("ColorEdge"), ColorStr))
				{
					ColorEnd = FProceduralShapeGenerator::ParseHexColor(ColorStr);
				}

				double NumValue;
				if (ParamsJson->TryGetNumberField(TEXT("Thickness"), NumValue))
				{
					Thickness = (NumValue > 1.0) ? static_cast<float>(NumValue) / 100.0f : static_cast<float>(NumValue);
				}
				if (ParamsJson->TryGetNumberField(TEXT("CornerRadius"), NumValue))
				{
					CornerRadius = (NumValue > 1.0) ? static_cast<float>(NumValue) / 100.0f : static_cast<float>(NumValue);
				}
			}
		}

		// 获取尺寸
		int32 Width = 64, Height = 64;
		if (const FString* W = Node.Properties.Find(TEXT("WidthOverride"))) Width = FCString::Atoi(**W);
		if (const FString* H = Node.Properties.Find(TEXT("HeightOverride"))) Height = FCString::Atoi(**H);

		// 根据形状类型生成纹理
		UTexture2D* Texture = nullptr;
		FString ShapeName = (*MaterialRef).ToLower();

		if (ShapeName == TEXT("circle"))
		{
			Texture = FProceduralShapeGenerator::GenerateCircle(FMath::Max(Width, Height), Color, ImageWidget);
		}
		else if (ShapeName == TEXT("ring"))
		{
			Texture = FProceduralShapeGenerator::GenerateRing(FMath::Max(Width, Height), Color, Thickness, ImageWidget);
		}
		else if (ShapeName == TEXT("roundedrect"))
		{
			Texture = FProceduralShapeGenerator::GenerateRoundedRect(Width, Height, Color, CornerRadius, ImageWidget);
		}
		else if (ShapeName == TEXT("gradienth"))
		{
			Texture = FProceduralShapeGenerator::GenerateGradientH(Width, Height, ColorStart, ColorEnd, ImageWidget);
		}
		else if (ShapeName == TEXT("gradientv"))
		{
			Texture = FProceduralShapeGenerator::GenerateGradientV(Width, Height, ColorStart, ColorEnd, ImageWidget);
		}
		else if (ShapeName == TEXT("radialgradient"))
		{
			Texture = FProceduralShapeGenerator::GenerateRadialGradient(FMath::Max(Width, Height), ColorStart, ColorEnd, ImageWidget);
		}

		if (!Texture)
		{
			UE_LOG(LogImageToUMG, Warning, TEXT("  -> 未知形状类型: %s"), **MaterialRef);
			return false;
		}

		ImageWidget->SetBrushFromTexture(Texture);
		FSlateBrush Brush = ImageWidget->GetBrush();
		Brush.ImageSize = FVector2D(Width, Height);
		Brush.Tiling = ESlateBrushTileType::NoTile;
		ImageWidget->SetBrush(Brush);
		ImageWidget->SetDesiredSizeOverride(Brush.ImageSize);
		ImageWidget->SynchronizeProperties();

		UE_LOG(LogImageToUMG, Verbose, TEXT("  -> 程序化生成成功: %s (%dx%d)"), **MaterialRef, Width, Height);
		return true;
	}

	/**
	 * 处理 Image 控件的 ImageRef（纹理映射绑定）
	 * @return true 如果成功绑定纹理
	 */
	bool HandleImageTextureRef(UImage* ImageWidget, const FUMGWidgetNode& Node, const TMap<FString, UTexture2D*>* TextureMap)
	{
		if (!TextureMap) return false;

		const FString* ImageRef = Node.Properties.Find(TEXT("ImageRef"));
		if (!ImageRef) return false;

		UE_LOG(LogImageToUMG, Verbose, TEXT("Image '%s' 查找纹理 ImageRef='%s'"), *Node.Name, **ImageRef);

		UTexture2D* const* FoundTexture = TextureMap->Find(*ImageRef);
		if (!FoundTexture || !*FoundTexture)
		{
			UE_LOG(LogImageToUMG, Warning, TEXT("  -> 未找到纹理 '%s', TextureMap 有 %d 条目"),
				**ImageRef, TextureMap->Num());
			return false;
		}

		ImageWidget->SetBrushFromTexture(*FoundTexture);

		// 绑定后重新应用尺寸（SetBrushFromTexture 会重置 Brush）
		const FString* WidthStr = Node.Properties.Find(TEXT("WidthOverride"));
		const FString* HeightStr = Node.Properties.Find(TEXT("HeightOverride"));
		if (WidthStr || HeightStr)
		{
			FSlateBrush Brush = ImageWidget->GetBrush();
			if (WidthStr) Brush.ImageSize.X = FCString::Atof(**WidthStr);
			if (HeightStr) Brush.ImageSize.Y = FCString::Atof(**HeightStr);
			Brush.Tiling = ESlateBrushTileType::NoTile;
			ImageWidget->SetBrush(Brush);
			ImageWidget->SetDesiredSizeOverride(Brush.ImageSize);
		}

		ImageWidget->SynchronizeProperties();
		UE_LOG(LogImageToUMG, Verbose, TEXT("  -> 绑定成功: %s (%dx%d)"),
			*(*FoundTexture)->GetName(), (*FoundTexture)->GetSizeX(), (*FoundTexture)->GetSizeY());
		return true;
	}
}

void FUMGWidgetUtils::ApplySlotProperties(UPanelSlot* Slot, const FUMGSlotData& S)
{
	if (!Slot) return;
	const FUMGSlotData Default;
	const bool bHasNonDefaultCanvas =
		!FMath::IsNearlyEqual(S.Anchors.Min.X, Default.Anchors.Min.X) ||
		!FMath::IsNearlyEqual(S.Anchors.Min.Y, Default.Anchors.Min.Y) ||
		!FMath::IsNearlyEqual(S.Anchors.Max.X, Default.Anchors.Max.X) ||
		!FMath::IsNearlyEqual(S.Anchors.Max.Y, Default.Anchors.Max.Y) ||
		!FMath::IsNearlyEqual(S.Alignment.X, Default.Alignment.X) ||
		!FMath::IsNearlyEqual(S.Alignment.Y, Default.Alignment.Y) ||
		!FMath::IsNearlyEqual(S.Offsets.Left, Default.Offsets.Left) ||
		!FMath::IsNearlyEqual(S.Offsets.Top, Default.Offsets.Top) ||
		!FMath::IsNearlyEqual(S.Offsets.Right, Default.Offsets.Right) ||
		!FMath::IsNearlyEqual(S.Offsets.Bottom, Default.Offsets.Bottom);
	const bool bHasNonDefaultPadding = !IsNearlyEqualMargin(S.Padding, Default.Padding);
	const bool bHasNonDefaultBox =
		S.HorizontalAlignment != Default.HorizontalAlignment ||
		S.VerticalAlignment != Default.VerticalAlignment ||
		S.SizeRule != Default.SizeRule ||
		!FMath::IsNearlyEqual(S.FillWeight, Default.FillWeight);
	const bool bHasNonDefaultGrid =
		S.Row != Default.Row ||
		S.Column != Default.Column ||
		S.RowSpan != Default.RowSpan ||
		S.ColumnSpan != Default.ColumnSpan;

	const bool bIsCanvas = Slot->IsA<UCanvasPanelSlot>();
	const bool bIsGrid = Slot->IsA<UGridSlot>() || Slot->IsA<UUniformGridSlot>();
	if (bIsCanvas)
	{
		if (bHasNonDefaultPadding || bHasNonDefaultBox || bHasNonDefaultGrid)
		{
			UE_LOG(LogImageToUMG, Warning, TEXT("CanvasPanelSlot 忽略非Canvas字段（Padding/HAlign/VAlign/SizeRule/FillWeight/Row/Column等）"));
		}
	}
	else
	{
		if (bHasNonDefaultCanvas)
		{
			UE_LOG(LogImageToUMG, Warning, TEXT("%s 忽略 Anchors/Offsets/Alignment"), *Slot->GetClass()->GetName());
		}
		if (!bIsGrid && bHasNonDefaultGrid)
		{
			UE_LOG(LogImageToUMG, Warning, TEXT("%s 忽略 Row/Column/RowSpan/ColumnSpan"), *Slot->GetClass()->GetName());
		}
		if (Slot->IsA<UScaleBoxSlot>() && bHasNonDefaultPadding)
		{
			UE_LOG(LogImageToUMG, Warning, TEXT("ScaleBoxSlot 不支持 Padding"));
		}
	}

	if (const FApplySlotFunc* Func = GetSlotApplierMap().Find(Slot->GetClass()))
	{
		(*Func)(Slot, S);
	}
	else
	{
		UE_LOG(LogImageToUMG, Warning, TEXT("未支持的Slot类型: %s"), *Slot->GetClass()->GetName());
	}
}

// ============================================================================
// Widget 属性应用（拆分为独立函数，提高可读性）
// ============================================================================

namespace
{
	#define GET_PROP(Key) P.Find(TEXT(Key))

	void ApplyTextBlockProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UTextBlock* T = CastChecked<UTextBlock>(W);
		if (const FString* V = GET_PROP("Text")) T->SetText(FText::FromString(*V));
		if (const FString* V = GET_PROP("Color")) T->SetColorAndOpacity(FSlateColor(FUMGWidgetUtils::ParseColor(*V)));
		if (const FString* V = GET_PROP("FontSize"))
		{
			FSlateFontInfo F = T->GetFont();
			F.Size = FCString::Atoi(**V);
			T->SetFont(F);
		}
		if (const FString* V = GET_PROP("Justification"))
		{
			static const TMap<FString, ETextJustify::Type> JustifyMap = {
				{TEXT("Left"), ETextJustify::Left}, {TEXT("Center"), ETextJustify::Center}, {TEXT("Right"), ETextJustify::Right}
			};
			if (const ETextJustify::Type* J = JustifyMap.Find(*V)) T->SetJustification(*J);
		}
		if (const FString* V = GET_PROP("LineHeightPercentage"))
			T->SetLineHeightPercentage(FCString::Atof(**V));
	}

	void ApplyButtonProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UButton* B = CastChecked<UButton>(W);
		FButtonStyle S = B->GetStyle();
		if (const FString* V = GET_PROP("BackgroundColor"))
		{
			FLinearColor Color = FUMGWidgetUtils::ParseColor(*V);
			S.Normal.TintColor = FSlateColor(Color);
			S.Hovered.TintColor = FSlateColor(Color * 1.1f);
			S.Pressed.TintColor = FSlateColor(Color * 0.9f);
		}
		if (const FString* V = GET_PROP("HoveredColor")) S.Hovered.TintColor = FSlateColor(FUMGWidgetUtils::ParseColor(*V));
		if (const FString* V = GET_PROP("PressedColor")) S.Pressed.TintColor = FSlateColor(FUMGWidgetUtils::ParseColor(*V));
		B->SetStyle(S);
	}

	void ApplyImageProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UImage* I = CastChecked<UImage>(W);
		// 支持 Color 和 ColorAndOpacity 两种属性名
		const FString* ColorVal = GET_PROP("Color");
		if (!ColorVal) ColorVal = GET_PROP("ColorAndOpacity");
		if (ColorVal) I->SetColorAndOpacity(FUMGWidgetUtils::ParseColor(*ColorVal));

		if (const FString* V = GET_PROP("BrushColor")) I->SetBrushTintColor(FSlateColor(FUMGWidgetUtils::ParseColor(*V)));
		if (const FString* V = GET_PROP("TexturePath"))
		{
			UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, **V);
			if (!Tex) Tex = FindObject<UTexture2D>(nullptr, **V);
			if (Tex) I->SetBrushFromTexture(Tex);
		}

		// 处理 WidthOverride/HeightOverride - 设置 Brush 的 ImageSize
		// UImage 本身没有这些属性，但 AI 经常返回它们
		FSlateBrush Brush = I->GetBrush();
		bool bBrushModified = false;
		if (const FString* V = GET_PROP("WidthOverride"))
		{
			Brush.ImageSize.X = FCString::Atof(**V);
			bBrushModified = true;
		}
		if (const FString* V = GET_PROP("HeightOverride"))
		{
			Brush.ImageSize.Y = FCString::Atof(**V);
			bBrushModified = true;
		}
		if (bBrushModified)
		{
			// 设置 SizeBox Stretch 为 UserSpecifiedWithClipping 以使用自定义尺寸
			Brush.Tiling = ESlateBrushTileType::NoTile;
			I->SetBrush(Brush);
			// 设置 DesiredSizeOverride
			I->SetDesiredSizeOverride(Brush.ImageSize);
		}
	}

	void ApplyProgressBarProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UProgressBar* PB = CastChecked<UProgressBar>(W);
		if (const FString* V = GET_PROP("Percent")) PB->SetPercent(FCString::Atof(**V));
		if (const FString* V = GET_PROP("FillColor")) PB->SetFillColorAndOpacity(FUMGWidgetUtils::ParseColor(*V));
	}

	void ApplyThrobberProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UThrobber* T = CastChecked<UThrobber>(W);
		if (const FString* V = GET_PROP("NumberOfPieces")) T->SetNumberOfPieces(FCString::Atoi(**V));
		if (const FString* V = GET_PROP("Animate"))
		{
			const bool bAnimate = V->ToBool();
			T->SetAnimateHorizontally(bAnimate);
			T->SetAnimateVertically(bAnimate);
			T->SetAnimateOpacity(bAnimate);
		}
		if (const FString* V = GET_PROP("AnimateHorizontally")) T->SetAnimateHorizontally(V->ToBool());
		if (const FString* V = GET_PROP("AnimateVertically")) T->SetAnimateVertically(V->ToBool());
		if (const FString* V = GET_PROP("AnimateOpacity")) T->SetAnimateOpacity(V->ToBool());
	}

	void ApplyCircularThrobberProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UCircularThrobber* T = CastChecked<UCircularThrobber>(W);
		if (const FString* V = GET_PROP("NumberOfPieces")) T->SetNumberOfPieces(FCString::Atoi(**V));
		if (const FString* V = GET_PROP("Period")) T->SetPeriod(FCString::Atof(**V));
		if (const FString* V = GET_PROP("Radius")) T->SetRadius(FCString::Atof(**V));
	}

	void ApplySliderProps(UWidget* W, const TMap<FString, FString>& P)
	{
		if (const FString* V = GET_PROP("Value")) CastChecked<USlider>(W)->SetValue(FCString::Atof(**V));
	}

	void ApplyCheckBoxProps(UWidget* W, const TMap<FString, FString>& P)
	{
		if (const FString* V = GET_PROP("IsChecked")) CastChecked<UCheckBox>(W)->SetIsChecked(V->ToBool());
	}

	void ApplyEditableTextBoxProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UEditableTextBox* E = CastChecked<UEditableTextBox>(W);
		if (const FString* V = GET_PROP("Text")) E->SetText(FText::FromString(*V));
		if (const FString* V = GET_PROP("HintText")) E->SetHintText(FText::FromString(*V));
	}

	void ApplyMultiLineEditableTextBoxProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UMultiLineEditableTextBox* M = CastChecked<UMultiLineEditableTextBox>(W);
		if (const FString* V = GET_PROP("Text")) M->SetText(FText::FromString(*V));
		if (const FString* V = GET_PROP("HintText")) M->SetHintText(FText::FromString(*V));
	}

	void ApplyEditableTextProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UEditableText* E = CastChecked<UEditableText>(W);
		if (const FString* V = GET_PROP("Text")) E->SetText(FText::FromString(*V));
		if (const FString* V = GET_PROP("HintText")) E->SetHintText(FText::FromString(*V));
		if (const FString* V = GET_PROP("IsReadOnly")) E->SetIsReadOnly(V->ToBool());
		if (const FString* V = GET_PROP("IsPassword")) E->SetIsPassword(V->ToBool());
		if (const FString* V = GET_PROP("MinimumDesiredWidth")) E->SetMinimumDesiredWidth(FCString::Atof(**V));
	}

	void ApplyMultiLineEditableTextProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UMultiLineEditableText* M = CastChecked<UMultiLineEditableText>(W);
		if (const FString* V = GET_PROP("Text")) M->SetText(FText::FromString(*V));
		if (const FString* V = GET_PROP("HintText")) M->SetHintText(FText::FromString(*V));
		if (const FString* V = GET_PROP("IsReadOnly")) M->SetIsReadOnly(V->ToBool());
	}

	void ApplyBorderProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UBorder* Bd = CastChecked<UBorder>(W);
		if (const FString* V = GET_PROP("BrushColor"))
		{
			FLinearColor Color = FUMGWidgetUtils::ParseColor(*V);
			// 用引擎白色纹理做 brush 基底，再用 BrushColor 着色
			UTexture2D* WhiteTex = LoadObject<UTexture2D>(nullptr,
				TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
			if (WhiteTex)
			{
				FSlateBrush Brush;
				Brush.DrawAs = ESlateBrushDrawType::Image;
				Brush.SetResourceObject(WhiteTex);
				Brush.TintColor = FSlateColor(FLinearColor::White);
				Brush.ImageSize = FVector2D(1.f, 1.f);
				Bd->SetBrush(Brush);
			}
			Bd->SetBrushColor(Color);
		}
		if (const FString* V = GET_PROP("ContentColor")) Bd->SetContentColorAndOpacity(FUMGWidgetUtils::ParseColor(*V));
		if (const FString* V = GET_PROP("Padding")) Bd->SetPadding(FUMGWidgetUtils::ParseMargin(*V));
	}

	void ApplySizeBoxProps(UWidget* W, const TMap<FString, FString>& P)
	{
		USizeBox* SB = CastChecked<USizeBox>(W);
		if (const FString* V = GET_PROP("WidthOverride")) SB->SetWidthOverride(FCString::Atof(**V));
		if (const FString* V = GET_PROP("HeightOverride")) SB->SetHeightOverride(FCString::Atof(**V));
		if (const FString* V = GET_PROP("MinDesiredWidth")) SB->SetMinDesiredWidth(FCString::Atof(**V));
		if (const FString* V = GET_PROP("MinDesiredHeight")) SB->SetMinDesiredHeight(FCString::Atof(**V));
		if (const FString* V = GET_PROP("MaxDesiredWidth")) SB->SetMaxDesiredWidth(FCString::Atof(**V));
		if (const FString* V = GET_PROP("MaxDesiredHeight")) SB->SetMaxDesiredHeight(FCString::Atof(**V));
	}

	void ApplyScrollBoxProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UScrollBox* Sc = CastChecked<UScrollBox>(W);
		if (const FString* V = GET_PROP("Orientation"))
			Sc->SetOrientation(*V == TEXT("Horizontal") ? Orient_Horizontal : Orient_Vertical);
		if (const FString* V = GET_PROP("ScrollBarVisibility"))
		{
			static const TMap<FString, ESlateVisibility> VisMap = {
				{TEXT("Visible"), ESlateVisibility::Visible}, {TEXT("Collapsed"), ESlateVisibility::Collapsed}, {TEXT("Hidden"), ESlateVisibility::Hidden}
			};
			if (const ESlateVisibility* Vis = VisMap.Find(*V)) Sc->SetScrollBarVisibility(*Vis);
		}
	}

	void ApplySpacerProps(UWidget* W, const TMap<FString, FString>& P)
	{
		FVector2D Size(0, 0);
		if (const FString* V = GET_PROP("Width")) Size.X = FCString::Atof(**V);
		if (const FString* V = GET_PROP("Height")) Size.Y = FCString::Atof(**V);
		CastChecked<USpacer>(W)->SetSize(Size);
	}

	void ApplyRichTextBlockProps(UWidget* W, const TMap<FString, FString>& P)
	{
		if (const FString* V = GET_PROP("Text")) CastChecked<URichTextBlock>(W)->SetText(FText::FromString(*V));
	}

	void ApplyComboBoxStringProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UComboBoxString* CB = CastChecked<UComboBoxString>(W);
		if (const FString* V = GET_PROP("Options"))
		{
			TArray<FString> Opts;
			V->ParseIntoArray(Opts, TEXT(","));
			for (const FString& O : Opts) CB->AddOption(O.TrimStartAndEnd());
		}
		if (const FString* V = GET_PROP("SelectedOption")) CB->SetSelectedOption(*V);
	}

	void ApplySpinBoxProps(UWidget* W, const TMap<FString, FString>& P)
	{
		USpinBox* SpB = CastChecked<USpinBox>(W);
		if (const FString* V = GET_PROP("Value")) SpB->SetValue(FCString::Atof(**V));
		if (const FString* V = GET_PROP("MinValue")) SpB->SetMinValue(FCString::Atof(**V));
		if (const FString* V = GET_PROP("MaxValue")) SpB->SetMaxValue(FCString::Atof(**V));
	}

	void ApplyBackgroundBlurProps(UWidget* W, const TMap<FString, FString>& P)
	{
		if (const FString* V = GET_PROP("BlurStrength")) CastChecked<UBackgroundBlur>(W)->SetBlurStrength(FCString::Atof(**V));
	}

	void ApplyRetainerBoxProps(UWidget* W, const TMap<FString, FString>& P)
	{
		URetainerBox* RB = CastChecked<URetainerBox>(W);
		if (const FString* V = GET_PROP("RetainRendering")) RB->SetRetainRendering(V->ToBool());
		int32 Phase = RB->GetPhase();
		int32 PhaseCount = RB->GetPhaseCount();
		bool bHasPhase = false;
		if (const FString* V = GET_PROP("Phase"))
		{
			Phase = FCString::Atoi(**V);
			bHasPhase = true;
		}
		if (const FString* V = GET_PROP("PhaseCount"))
		{
			PhaseCount = FMath::Max(1, FCString::Atoi(**V));
			bHasPhase = true;
		}
		if (bHasPhase) RB->SetRenderingPhase(Phase, PhaseCount);
	}

	void ApplyInvalidationBoxProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UInvalidationBox* IB = CastChecked<UInvalidationBox>(W);
		if (const FString* V = GET_PROP("CanCache")) IB->SetCanCache(V->ToBool());
	}

	void ApplyGridPanelProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UGridPanel* G = CastChecked<UGridPanel>(W);
		if (const FString* V = GET_PROP("ColumnFill"))
		{
			TArray<FString> Fills;
			V->ParseIntoArray(Fills, TEXT(","));
			for (int32 i = 0; i < Fills.Num(); ++i)
			{
				G->SetColumnFill(i, FCString::Atof(*Fills[i].TrimStartAndEnd()));
			}
		}
		if (const FString* V = GET_PROP("RowFill"))
		{
			TArray<FString> Fills;
			V->ParseIntoArray(Fills, TEXT(","));
			for (int32 i = 0; i < Fills.Num(); ++i)
			{
				G->SetRowFill(i, FCString::Atof(*Fills[i].TrimStartAndEnd()));
			}
		}
	}

	void ApplyUniformGridPanelProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UUniformGridPanel* UG = CastChecked<UUniformGridPanel>(W);
		if (const FString* V = GET_PROP("SlotPadding")) UG->SetSlotPadding(FUMGWidgetUtils::ParseMargin(*V));
		if (const FString* V = GET_PROP("MinDesiredSlotWidth")) UG->SetMinDesiredSlotWidth(FCString::Atof(**V));
		if (const FString* V = GET_PROP("MinDesiredSlotHeight")) UG->SetMinDesiredSlotHeight(FCString::Atof(**V));
	}

	void ApplyWrapBoxProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UWrapBox* WB = CastChecked<UWrapBox>(W);
		if (const FString* V = GET_PROP("InnerSlotPadding")) WB->SetInnerSlotPadding(FUMGWidgetUtils::ParseVector2D(*V));
		if (const FString* V = GET_PROP("Orientation"))
			WB->SetOrientation(*V == TEXT("Vertical") ? Orient_Vertical : Orient_Horizontal);
	}

	void ApplyScaleBoxProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UScaleBox* SB = CastChecked<UScaleBox>(W);
		if (const FString* V = GET_PROP("Stretch"))
		{
			static const TMap<FString, EStretch::Type> StretchMap = {
				{TEXT("None"), EStretch::None}, {TEXT("Fill"), EStretch::Fill},
				{TEXT("ScaleToFit"), EStretch::ScaleToFit}, {TEXT("ScaleToFitX"), EStretch::ScaleToFitX},
				{TEXT("ScaleToFitY"), EStretch::ScaleToFitY}, {TEXT("ScaleToFill"), EStretch::ScaleToFill},
				{TEXT("UserSpecified"), EStretch::UserSpecified},
				{TEXT("UserSpecifiedWithClipping"), EStretch::UserSpecifiedWithClipping}
			};
			if (const EStretch::Type* S = StretchMap.Find(*V)) SB->SetStretch(*S);
		}
		if (const FString* V = GET_PROP("StretchDirection"))
		{
			static const TMap<FString, EStretchDirection::Type> DirMap = {
				{TEXT("Both"), EStretchDirection::Both}, {TEXT("DownOnly"), EStretchDirection::DownOnly}, {TEXT("UpOnly"), EStretchDirection::UpOnly}
			};
			if (const EStretchDirection::Type* D = DirMap.Find(*V)) SB->SetStretchDirection(*D);
		}
		if (const FString* V = GET_PROP("UserSpecifiedScale")) SB->SetUserSpecifiedScale(FCString::Atof(**V));
	}

	void ApplyWidgetSwitcherProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UWidgetSwitcher* WS = CastChecked<UWidgetSwitcher>(W);
		if (const FString* V = GET_PROP("ActiveWidgetIndex")) WS->SetActiveWidgetIndex(FCString::Atoi(**V));
	}

	void ApplyExpandableAreaProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UExpandableArea* EA = CastChecked<UExpandableArea>(W);
		if (const FString* V = GET_PROP("IsExpanded")) EA->SetIsExpanded(V->ToBool());
	}

	void ApplyListViewProps(UWidget* W, const TMap<FString, FString>& P)
	{
		// UListView 在 UE5.7 中没有 SetOrientation/SetEntrySpacing 公开方法
		// 这些属性需要在蓝图中设置或通过继承类配置
	}

	void ApplyTileViewProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UTileView* TV = CastChecked<UTileView>(W);
		if (const FString* V = GET_PROP("EntryWidth")) TV->SetEntryWidth(FCString::Atof(**V));
		if (const FString* V = GET_PROP("EntryHeight")) TV->SetEntryHeight(FCString::Atof(**V));
	}

	void ApplyTreeViewProps(UWidget* W, const TMap<FString, FString>& P)
	{
		// UTreeView 在 UE5.7 中没有 SetOrientation/SetEntrySpacing 公开方法
		// 这些属性需要在蓝图中设置或通过继承类配置
	}

	void ApplyMenuAnchorProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UMenuAnchor* MA = CastChecked<UMenuAnchor>(W);
		if (const FString* V = GET_PROP("Placement"))
		{
			static const TMap<FString, EMenuPlacement> PlacementMap = {
				{TEXT("MenuPlacement_BelowAnchor"), MenuPlacement_BelowAnchor},
				{TEXT("MenuPlacement_AboveAnchor"), MenuPlacement_AboveAnchor},
				{TEXT("MenuPlacement_ComboBox"), MenuPlacement_ComboBox},
				{TEXT("MenuPlacement_ComboBoxRight"), MenuPlacement_ComboBoxRight},
				{TEXT("MenuPlacement_MenuRight"), MenuPlacement_MenuRight},
				{TEXT("MenuPlacement_MenuLeft"), MenuPlacement_MenuLeft},
				{TEXT("BelowAnchor"), MenuPlacement_BelowAnchor},
				{TEXT("AboveAnchor"), MenuPlacement_AboveAnchor},
			};
			if (const EMenuPlacement* Pl = PlacementMap.Find(*V)) MA->SetPlacement(*Pl);
		}
	}

	void ApplySafeZoneProps(UWidget* W, const TMap<FString, FString>& P)
	{
		USafeZone* SZ = CastChecked<USafeZone>(W);
		// 默认全部填充，根据属性覆盖
		bool bLeft = true, bRight = true, bTop = true, bBottom = true;
		if (const FString* V = GET_PROP("PadLeft")) bLeft = V->ToBool();
		if (const FString* V = GET_PROP("PadRight")) bRight = V->ToBool();
		if (const FString* V = GET_PROP("PadTop")) bTop = V->ToBool();
		if (const FString* V = GET_PROP("PadBottom")) bBottom = V->ToBool();
		SZ->SetSidesToPad(bLeft, bRight, bTop, bBottom);
	}

	#undef GET_PROP

	// 属性应用函数映射表
	using FApplyPropsFunc = void(*)(UWidget*, const TMap<FString, FString>&);
	const TMap<UClass*, FApplyPropsFunc>& GetPropsApplierMap()
	{
		static const TMap<UClass*, FApplyPropsFunc> Map = {
			// 文本/显示类
			{UTextBlock::StaticClass(), &ApplyTextBlockProps},
			{URichTextBlock::StaticClass(), &ApplyRichTextBlockProps},
			{UImage::StaticClass(), &ApplyImageProps},
			{UProgressBar::StaticClass(), &ApplyProgressBarProps},
			{UThrobber::StaticClass(), &ApplyThrobberProps},
			{UCircularThrobber::StaticClass(), &ApplyCircularThrobberProps},
			{USpacer::StaticClass(), &ApplySpacerProps},
			// 交互类
			{UButton::StaticClass(), &ApplyButtonProps},
			{USlider::StaticClass(), &ApplySliderProps},
			{UCheckBox::StaticClass(), &ApplyCheckBoxProps},
			{UEditableTextBox::StaticClass(), &ApplyEditableTextBoxProps},
			{UMultiLineEditableTextBox::StaticClass(), &ApplyMultiLineEditableTextBoxProps},
			{UEditableText::StaticClass(), &ApplyEditableTextProps},
			{UMultiLineEditableText::StaticClass(), &ApplyMultiLineEditableTextProps},
			{UComboBoxString::StaticClass(), &ApplyComboBoxStringProps},
			{USpinBox::StaticClass(), &ApplySpinBoxProps},
			// 容器类（CanvasPanel/Overlay 无特殊属性，子控件通过 Slot 配置）
			{UGridPanel::StaticClass(), &ApplyGridPanelProps},
			{UUniformGridPanel::StaticClass(), &ApplyUniformGridPanelProps},
			{UWrapBox::StaticClass(), &ApplyWrapBoxProps},
			{UScrollBox::StaticClass(), &ApplyScrollBoxProps},
			{UWidgetSwitcher::StaticClass(), &ApplyWidgetSwitcherProps},
			// 包装类
			{UBorder::StaticClass(), &ApplyBorderProps},
			{USizeBox::StaticClass(), &ApplySizeBoxProps},
			{UScaleBox::StaticClass(), &ApplyScaleBoxProps},
			{UBackgroundBlur::StaticClass(), &ApplyBackgroundBlurProps},
			{URetainerBox::StaticClass(), &ApplyRetainerBoxProps},
			{UInvalidationBox::StaticClass(), &ApplyInvalidationBoxProps},
			{UExpandableArea::StaticClass(), &ApplyExpandableAreaProps},
			// 列表类
			{UListView::StaticClass(), &ApplyListViewProps},
			{UTileView::StaticClass(), &ApplyTileViewProps},
			{UTreeView::StaticClass(), &ApplyTreeViewProps},
			// 其他
			{UMenuAnchor::StaticClass(), &ApplyMenuAnchorProps},
			{USafeZone::StaticClass(), &ApplySafeZoneProps},
		};
		return Map;
	}
}

void FUMGWidgetUtils::ApplyCommonProperties(UWidget* W, const TMap<FString, FString>& Properties)
{
	#define GET_PROP(Key) Properties.Find(TEXT(Key))
	if (const FString* V = GET_PROP("Visibility")) W->SetVisibility(ParseVisibility(*V));
	if (const FString* V = GET_PROP("ToolTipText")) W->SetToolTipText(FText::FromString(*V));
	if (const FString* V = GET_PROP("IsEnabled")) W->SetIsEnabled(V->ToBool());
	if (const FString* V = GET_PROP("RenderOpacity")) W->SetRenderOpacity(FCString::Atof(**V));
	if (const FString* V = GET_PROP("RenderAngle")) W->SetRenderTransformAngle(FCString::Atof(**V));
	if (const FString* V = GET_PROP("RenderScale")) W->SetRenderScale(ParseVector2D(*V, FVector2D::UnitVector));
	if (const FString* V = GET_PROP("RenderTranslation")) W->SetRenderTranslation(ParseVector2D(*V));
	#undef GET_PROP
}

void FUMGWidgetUtils::ApplyWidgetProperties(UWidget* Widget, const TMap<FString, FString>& Properties)
{
	if (!Widget || Properties.Num() == 0) return;

	// 查找并调用类型特定的属性应用函数
	if (const FApplyPropsFunc* Func = GetPropsApplierMap().Find(Widget->GetClass()))
	{
		(*Func)(Widget, Properties);
	}

	ApplyCommonProperties(Widget, Properties);
}

// ============================================================================

namespace
{
	// 递归遍历节点计算最大边界
	void TraverseNodeForDesignSize(const FUMGWidgetNode& Node, FVector2D& MaxBounds)
	{
		const FUMGSlotData& Slot = Node.Slot;

		// 检查是否是点锚点（Min == Max）
		bool bIsPointAnchor = FMath::IsNearlyEqual(Slot.Anchors.Min.X, Slot.Anchors.Max.X) &&
		                      FMath::IsNearlyEqual(Slot.Anchors.Min.Y, Slot.Anchors.Max.Y);

		if (bIsPointAnchor)
		{
			// 位置（Left/Top 是相对于锚点的偏移）
			float PosX = Slot.Offsets.Left;
			float PosY = Slot.Offsets.Top;

			// Widget 尺寸
			float Width = Slot.Offsets.Right - Slot.Offsets.Left;
			float Height = Slot.Offsets.Bottom - Slot.Offsets.Top;

			// 如果有 WidthOverride/HeightOverride，使用它们
			if (const FString* W = Node.Properties.Find(TEXT("WidthOverride")))
				Width = FCString::Atof(**W);
			if (const FString* H = Node.Properties.Find(TEXT("HeightOverride")))
				Height = FCString::Atof(**H);

			// 计算右下角边界 = 位置 + 尺寸
			MaxBounds.X = FMath::Max(MaxBounds.X, FMath::Abs(PosX) + Width);
			MaxBounds.Y = FMath::Max(MaxBounds.Y, FMath::Abs(PosY) + Height);
		}

		// 递归处理子节点
		for (const FUMGWidgetNode& Child : Node.Children)
		{
			TraverseNodeForDesignSize(Child, MaxBounds);
		}
	}
}

FVector2D FUMGWidgetUtils::CalculateDesignSize(const FUMGWidgetNode& RootNode, FVector2D DefaultSize)
{
	FVector2D MaxBounds = DefaultSize;
	TraverseNodeForDesignSize(RootNode, MaxBounds);
	return MaxBounds;
}

void FUMGWidgetUtils::ConvertRatioToPixels(FUMGWidgetNode& Node, FIntPoint CanvasSize)
{
	// 转换 WidthRatio -> WidthOverride
	if (const FString* WidthRatioStr = Node.Properties.Find(TEXT("WidthRatio")))
	{
		float Ratio = FCString::Atof(**WidthRatioStr);
		int32 Pixels = FMath::RoundToInt(CanvasSize.X * Ratio);
		Node.Properties.Add(TEXT("WidthOverride"), FString::FromInt(Pixels));
		Node.Properties.Remove(TEXT("WidthRatio"));
	}

	// 转换 HeightRatio -> HeightOverride
	if (const FString* HeightRatioStr = Node.Properties.Find(TEXT("HeightRatio")))
	{
		float Ratio = FCString::Atof(**HeightRatioStr);
		int32 Pixels = FMath::RoundToInt(CanvasSize.Y * Ratio);
		Node.Properties.Add(TEXT("HeightOverride"), FString::FromInt(Pixels));
		Node.Properties.Remove(TEXT("HeightRatio"));
	}

	// 转换 Offsets 中的比例字段（如果有）
	// LeftRatio, TopRatio -> Left, Top (相对画布尺寸)
	FUMGSlotData& Slot = Node.Slot;

	// 注意: Offsets.Right/Bottom 在点锚点时表示尺寸，拉伸锚点时表示边距
	// 这里只处理显式的 Ratio 字段，不改变现有的 Offsets 逻辑

	// 递归处理子节点
	for (FUMGWidgetNode& Child : Node.Children)
	{
		ConvertRatioToPixels(Child, CanvasSize);
	}
}

// ============================================================================
// Widget 渲染
// ============================================================================

namespace
{
	FIntPoint GetDesignerPreviewWindowSize(const FIntPoint& FallbackSize)
	{
		int32 PreviewWidth = 0;
		int32 PreviewHeight = 0;
		if (GConfig)
		{
			GConfig->GetInt(TEXT("UMGEditor.Designer"), TEXT("PreviewWidth"), PreviewWidth, GEditorPerProjectIni);
			GConfig->GetInt(TEXT("UMGEditor.Designer"), TEXT("PreviewHeight"), PreviewHeight, GEditorPerProjectIni);
		}
		if (PreviewWidth > 0 && PreviewHeight > 0)
		{
			return FIntPoint(PreviewWidth, PreviewHeight);
		}
		return FallbackSize;
	}

	FIntPoint ComputePreviewRenderSize(UUserWidget* Widget, const FIntPoint& FallbackSize, const TSharedRef<SVirtualWindow>& Window)
	{
		FIntPoint Result = FallbackSize;
		if (!Widget) return Result;

		const FIntPoint WindowSize = GetDesignerPreviewWindowSize(FallbackSize);
		Window->Resize(FVector2D(WindowSize.X, WindowSize.Y));
		Window->SlatePrepass(1.0f);

		FVector2D DesiredSize = Window->GetDesiredSize();
		if (DesiredSize.X <= SMALL_NUMBER || DesiredSize.Y <= SMALL_NUMBER)
			DesiredSize = FVector2D(WindowSize.X, WindowSize.Y);

		FVector2D UnscaledSize = FWidgetBlueprintEditorUtils::GetWidgetPreviewUnScaledCustomSize(
			DesiredSize, Widget, TOptional<FVector2D>(), EThumbnailPreviewSizeMode::MatchDesignerMode);
		if (UnscaledSize.X <= SMALL_NUMBER || UnscaledSize.Y <= SMALL_NUMBER)
			UnscaledSize = DesiredSize;

		Result.X = FMath::Max(1, FMath::CeilToInt(UnscaledSize.X));
		Result.Y = FMath::Max(1, FMath::CeilToInt(UnscaledSize.Y));
		return Result;
	}
}

bool FUMGWidgetUtils::RenderWidgetToPng(UWidgetBlueprint* WidgetBP, const FIntPoint& RenderSize, TArray<uint8>& OutPngData, FIntPoint& OutSize)
{
	if (!WidgetBP || !WidgetBP->GeneratedClass)
	{
		UE_LOG(LogImageToUMG, Warning, TEXT("RenderWidgetToPng: invalid WidgetBP"));
		return false;
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) return false;

	UUserWidget* Widget = CreateWidget<UUserWidget>(World, TSubclassOf<UUserWidget>(WidgetBP->GeneratedClass));
	if (!Widget) return false;

	Widget->SetDesignerFlags(EWidgetDesignFlags::Designing | EWidgetDesignFlags::ExecutePreConstruct);
	Widget->PreConstruct(true);
	Widget->SynchronizeProperties();
	Widget->ForceLayoutPrepass();
	Widget->SetFlags(RF_Transient);
	Widget->AddToRoot();

	TSharedRef<SWidget> SlateWidget = Widget->TakeWidget();
	TSharedRef<SVirtualWindow> Window = SNew(SVirtualWindow);
	Window->SetContent(SlateWidget);
	const FIntPoint EffectiveSize = ComputePreviewRenderSize(Widget, RenderSize, Window);

	UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>();
	RT->Filter = TF_Bilinear;
	RT->ClearColor = FLinearColor::Transparent;
	RT->SRGB = true;
	RT->RenderTargetFormat = RTF_RGBA8;
	const EPixelFormat Fmt = FSlateApplication::Get().GetRenderer()->GetSlateRecommendedColorFormat();
	RT->InitCustomFormat(EffectiveSize.X, EffectiveSize.Y, Fmt, false);

	FWidgetRenderer Renderer(true);
	Renderer.SetIsPrepassNeeded(false);
	TUniquePtr<FHittestGrid> HitTestGrid = MakeUnique<FHittestGrid>();
	Renderer.DrawWindow(RT, *HitTestGrid, Window, 1.0f, FVector2D(EffectiveSize.X, EffectiveSize.Y), 0.1f);

	FTextureRenderTargetResource* Resource = RT->GameThread_GetRenderTargetResource();
	if (!Resource)
	{
		Widget->RemoveFromRoot();
		RT->ReleaseResource();
		RT->MarkAsGarbage();
		return false;
	}

	TArray<FColor> Pixels;
	Resource->ReadPixels(Pixels);
	RT->ReleaseResource();
	RT->MarkAsGarbage();
	Widget->RemoveFromRoot();

	if (Pixels.Num() == 0) return false;

	IImageWrapperModule& WrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> Wrapper = WrapperModule.CreateImageWrapper(EImageFormat::PNG);
	if (!Wrapper.IsValid()) return false;

	Wrapper->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), EffectiveSize.X, EffectiveSize.Y, ERGBFormat::BGRA, 8);
	OutPngData = Wrapper->GetCompressed();
	OutSize = EffectiveSize;
	return OutPngData.Num() > 0;
}
