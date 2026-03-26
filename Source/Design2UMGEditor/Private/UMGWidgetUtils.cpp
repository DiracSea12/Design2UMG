// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGWidgetUtils.h"
#include "Design2UMGLog.h"
#include "Design2UMGSettings.h"
#include "LayoutCalculator.h"
#include "LayoutFixer.h"
#include "UMGPropertyKeys.h"
#include "UMGWidgetNodeAccess.h"
#include "UMGValueParser.h"

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
#include "Components/RetainerBox.h"
#include "Components/WidgetSwitcher.h"
#include "Components/NamedSlot.h"
#include "Components/NamedSlotInterface.h"
#include "Components/SafeZone.h"
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

FIntPoint FUMGWidgetUtils::EffectiveDesignSize = FIntPoint(1920, 1080);

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
		const bool bPadding = !FUMGValueParser::IsNearlyEqualMargin(Slot.Padding, Default.Padding);
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

// 前向声明：从 CreateWidgetRecursive 拆分出的子控件处理函数
static void HandleNamedSlotChildren(UWidgetTree* WidgetTree, INamedSlotInterface* NamedSlotHost, const FUMGWidgetNode& Node, const TMap<FString, UTexture2D*>* TextureMap);
static void HandleContentWidgetChild(UWidgetTree* WidgetTree, UContentWidget* Content, const FUMGWidgetNode& Node, const TMap<FString, UTexture2D*>* TextureMap);

// Slot 历史数据有效性判断
static bool HasValidSlotHistory(const FUMGSlotData& Slot);

UWidget* FUMGWidgetUtils::CreateWidgetRecursive(UWidgetTree* WidgetTree, const FUMGWidgetNode& Node, UPanelWidget* ParentWidget, const TMap<FString, UTexture2D*>* TextureMap)
{
	UClass* WidgetClass = ResolveWidgetClass(Node.Type);

	// Widget 类覆盖：按节点名称匹配用户配置的替换规则
	if (UClass* Override = UDesign2UMGSettings::Get()->FindClassOverride(Node.Name))
	{
		WidgetClass = Override;
	}

	if (!WidgetClass)
	{
		// v2.2: 增强警告，明确说明子树丢失
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

	// 去重：WidgetTree 中同名对象已存在时追加数字后缀
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

	// 添加到父容器并应用 Slot 属性
	if (ParentWidget)
	{
		if (UPanelSlot* Slot = ParentWidget->AddChild(NewWidget))
		{
			// v2.2: 只对CanvasPanel使用LayoutCalculator计算Slot
			// 其他容器（VerticalBox/HorizontalBox/Overlay）使用原始Slot数据
			FUMGSlotData SlotData = Node.Slot;
			UCanvasPanel* CanvasParent = Cast<UCanvasPanel>(ParentWidget);
			const bool bIsRootCanvasParent = CanvasParent && (WidgetTree && WidgetTree->RootWidget == ParentWidget);
			const bool bHasMeaningfulSlotData = HasMeaningfulSlotData(SlotData);
			const bool bEnableAdaptiveAnchors = UDesign2UMGSettings::Get()->bEnableAdaptiveRootCanvasAnchors;
			UE_LOG(LogDesign2UMG, Verbose, TEXT("CreateWidget: '%s' parent=%s slot_path=%s bounds=(%.1f,%.1f,%.1f,%.1f)"),
				*Node.Name, *ParentWidget->GetClass()->GetName(),
				(bIsRootCanvasParent && bEnableAdaptiveAnchors && !bHasMeaningfulSlotData) ? TEXT("LayoutCalc") : TEXT("Direct"),
				Node.Bounds.X, Node.Bounds.Y, Node.Bounds.W, Node.Bounds.H);
			if (bIsRootCanvasParent
				&& bEnableAdaptiveAnchors
				&& !bHasMeaningfulSlotData
				&& Node.Bounds.W > 0.0f
				&& Node.Bounds.H > 0.0f)
			{
				FLayoutContext Context;
				Context.DesignSize = FUMGWidgetUtils::EffectiveDesignSize;
				Context.Preset = FLayoutPreset::GetStandardHUDPreset();
				// v2.2: Hysteresis - 只有当Slot有有效历史数据时才设置OldAnchor
				if (HasValidSlotHistory(Node.Slot))
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
				// 像素Bounds归一化为0-1（Design2UMGConverter输出像素坐标，LayoutCalculator期望归一化）
				FBounds NormalizedBounds;
				NormalizedBounds.X = Node.Bounds.X / Context.DesignSize.X;
				NormalizedBounds.Y = Node.Bounds.Y / Context.DesignSize.Y;
				NormalizedBounds.W = Node.Bounds.W / Context.DesignSize.X;
				NormalizedBounds.H = Node.Bounds.H / Context.DesignSize.Y;
				const bool bOrigAutoSize = SlotData.bAutoSize;
				SlotData = FLayoutCalculator::CalculateSlot(NormalizedBounds, Context);
				SlotData.bAutoSize = bOrigAutoSize;
			}
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

	// Image 控件：优先 MaterialRef 程序化生成，回退 TexturePath 纹理绑定
	if (UImage* ImageWidget = Cast<UImage>(NewWidget))
	{
		if (!HandleImageMaterialRef(ImageWidget, Node))
		{
			HandleImageTextureRef(ImageWidget, Node, TextureMap);
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
		HandleNamedSlotChildren(WidgetTree, NamedSlotHost, Node, TextureMap);
	}
	else if (UContentWidget* Content = Cast<UContentWidget>(NewWidget))
	{
		HandleContentWidgetChild(WidgetTree, Content, Node, TextureMap);
	}

	return NewWidget;
}

static bool HasValidSlotHistory(const FUMGSlotData& Slot)
{
	return !FMath::IsNearlyEqual(Slot.Offsets.Left, 0.0f) ||
		!FMath::IsNearlyEqual(Slot.Offsets.Top, 0.0f) ||
		!FMath::IsNearlyEqual(Slot.Offsets.Right, 100.0f) ||
		!FMath::IsNearlyEqual(Slot.Offsets.Bottom, 100.0f) ||
		!FMath::IsNearlyEqual(Slot.Anchors.Min.X, 0.0f) ||
		!FMath::IsNearlyEqual(Slot.Anchors.Min.Y, 0.0f) ||
		!FMath::IsNearlyEqual(Slot.Anchors.Max.X, 0.0f) ||
		!FMath::IsNearlyEqual(Slot.Anchors.Max.Y, 0.0f) ||
		!FMath::IsNearlyEqual(Slot.Alignment.X, 0.0f) ||
		!FMath::IsNearlyEqual(Slot.Alignment.Y, 0.0f);
}

static void HandleNamedSlotChildren(UWidgetTree* WidgetTree, INamedSlotInterface* NamedSlotHost, const FUMGWidgetNode& Node, const TMap<FString, UTexture2D*>* TextureMap)
{
	TArray<FName> SlotNames;
	NamedSlotHost->GetSlotNames(SlotNames);

	if (SlotNames.Num() == 0)
	{
		if (Node.Children.Num() > 0)
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("NamedSlot 宿主 '%s' 没有槽位名称但有 %d 个子控件"),
				*Node.Name, Node.Children.Num());
		}
		return;
	}

	TSet<FName> UsedSlots;
	TMap<FName, UWidget*> SlotContent;
	int32 NextSlotIndex = 0;

	// 槽位追加：已有内容时自动包裹 VerticalBox
	auto AppendToSlot = [&](const FName& SlotName, UWidget* ChildWidget, const FUMGWidgetNode& ChildNode)
	{
		if (UWidget** ExistingPtr = SlotContent.Find(SlotName))
		{
			if (UPanelWidget* Panel = Cast<UPanelWidget>(*ExistingPtr))
			{
				if (UPanelSlot* NewSlot = Panel->AddChild(ChildWidget))
				{
					FUMGWidgetUtils::ApplySlotProperties(NewSlot, ChildNode.Slot);
				}
				return;
			}

			FName WrapName = FUMGWidgetUtils::SanitizeWidgetName(Node.Name + TEXT("_SlotWrap_") + SlotName.ToString(), TEXT("SlotWrap"));
			if (UVerticalBox* WrapBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), WrapName))
			{
				WrapBox->AddChild(*ExistingPtr);
				if (UPanelSlot* NewSlot = WrapBox->AddChild(ChildWidget))
				{
					FUMGWidgetUtils::ApplySlotProperties(NewSlot, ChildNode.Slot);
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
		UWidget* ChildWidget = FUMGWidgetUtils::CreateWidgetRecursive(WidgetTree, ChildNode, nullptr, TextureMap);
		if (!ChildWidget) continue;

		// 查找目标槽位：优先显式指定，其次按序分配，最后回退到末尾槽位
		FName TargetSlot = NAME_None;
		FString FallbackSlotName;
		Design2UMGWidgetNodeAccess::TryGetWidgetString(ChildNode, UMGPropertyKeys::NamedSlot, FallbackSlotName);
		FString RequestedSlotName;
		if (!Design2UMGWidgetNodeAccess::TryGetWidgetString(ChildNode, UMGPropertyKeys::SlotName, RequestedSlotName) || RequestedSlotName.IsEmpty())
		{
			RequestedSlotName = FallbackSlotName;
		}
		if (!RequestedSlotName.IsEmpty())
		{
			for (const FName& SlotName : SlotNames)
			{
				if (SlotName.ToString().Equals(RequestedSlotName, ESearchCase::IgnoreCase))
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
			UE_LOG(LogDesign2UMG, Warning, TEXT("NamedSlot 宿主 '%s' 没有可用槽位，子控件 '%s' 已跳过"),
				*Node.Name, *ChildNode.Name);
			continue;
		}

		if (HasMeaningfulSlotData(ChildNode.Slot))
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("NamedSlot 子控件 '%s' 的 Slot 不生效，已忽略（宿主 '%s'）"),
				*ChildNode.Name, *Node.Name);
		}

		if (bFallbackAppend)
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("NamedSlot 宿主 '%s' 槽位已满，子控件 '%s' 追加到 '%s'"),
				*Node.Name, *ChildNode.Name, *TargetSlot.ToString());
		}
		else if (SlotContent.Contains(TargetSlot))
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("NamedSlot '%s' 在 '%s' 已存在内容，合并子控件 '%s'"),
				*TargetSlot.ToString(), *Node.Name, *ChildNode.Name);
		}

		AppendToSlot(TargetSlot, ChildWidget, ChildNode);
		UsedSlots.Add(TargetSlot);
	}
}

static void HandleContentWidgetChild(UWidgetTree* WidgetTree, UContentWidget* Content, const FUMGWidgetNode& Node, const TMap<FString, UTexture2D*>* TextureMap)
{
	if (Node.Children.Num() == 0) return;

	const FUMGWidgetNode& ChildNode = Node.Children[0];
	UWidget* ChildWidget = FUMGWidgetUtils::CreateWidgetRecursive(WidgetTree, ChildNode, nullptr, TextureMap);
	if (ChildWidget)
	{
		if (UPanelSlot* ContentSlot = Content->SetContent(ChildWidget))
		{
			// SizeBox is a runtime wrapper used to preserve measured size.
			// Re-applying the wrapped child's original panel slot to USizeBoxSlot
			// shifts/stretchs the content and regresses visual parity.
			if (!Content->IsA<USizeBox>())
			{
				FUMGWidgetUtils::ApplySlotProperties(ContentSlot, ChildNode.Slot);
			}
		}

		// ContentWidget 通过自身属性控制子控件对齐
		if (UBorder* Border = Cast<UBorder>(Content))
		{
			// Keep Fill semantics for Border content. Mapping Fill+Alignment(0,0) to Left/Top
			// causes root canvases to collapse to desired size and drift to top-left.
			Border->SetHorizontalAlignment(FUMGValueParser::ParseHAlign(ChildNode.Slot.HorizontalAlignment));
			Border->SetVerticalAlignment(FUMGValueParser::ParseVAlign(ChildNode.Slot.VerticalAlignment));
		}
	}

	if (Node.Children.Num() > 1)
	{
		UE_LOG(LogDesign2UMG, Warning, TEXT("ContentWidget '%s' ignoring %d extra children"), *Node.Name, Node.Children.Num() - 1);
	}
}

namespace
{
	// 将FUMGWidgetNode转换为FFixerWidgetNode
	void ConvertToFixerNode(const FUMGWidgetNode& Src, FFixerWidgetNode& Dst)
	{
		Dst.Id = Src.Id;
		Dst.Type = Src.Type;
		Dst.Bounds = Src.Bounds;
		Dst.bFixExempt = false;

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

	// 递归归一化Bounds（像素→0-1）
	void NormalizeFixerBounds(FFixerWidgetNode& Node, float InvW, float InvH)
	{
		Node.Bounds.X *= InvW;
		Node.Bounds.Y *= InvH;
		Node.Bounds.W *= InvW;
		Node.Bounds.H *= InvH;
		for (auto& Child : Node.Children)
			NormalizeFixerBounds(Child, InvW, InvH);
	}

	// 递归反归一化Bounds（0-1→像素）
	void DenormalizeFixerBounds(FFixerWidgetNode& Node, float W, float H)
	{
		Node.Bounds.X *= W;
		Node.Bounds.Y *= H;
		Node.Bounds.W *= W;
		Node.Bounds.H *= H;
		for (auto& Child : Node.Children)
			DenormalizeFixerBounds(Child, W, H);
	}
}

void FUMGWidgetUtils::ApplyFixer(FUMGWidgetNode& RootNode, const FIntPoint& DesignSize)
{
	// 初始化Fixer预设
	FFixerPreset Preset;
	FLayoutFixer::Initialize(DesignSize, Preset);
	// 设计稿导入强调“还原优先”，禁止自动越界裁剪，否则会把本应越界显示的区域压扁。
	Preset.bEnableOverflowFix = false;

	// 转换为Fixer节点
	FFixerWidgetNode FixerRoot;
	ConvertToFixerNode(RootNode, FixerRoot);

	// 像素Bounds归一化为0-1（Fixer内部假设归一化坐标系）
	const float InvW = 1.0f / FMath::Max(1.0f, (float)DesignSize.X);
	const float InvH = 1.0f / FMath::Max(1.0f, (float)DesignSize.Y);
	NormalizeFixerBounds(FixerRoot, InvW, InvH);

	// 执行修正
	TArray<FFixerLogEntry> Log;
	FLayoutFixer::FixWidgetTree(FixerRoot, Preset, Log);

	// 反归一化回像素坐标
	DenormalizeFixerBounds(FixerRoot, (float)DesignSize.X, (float)DesignSize.Y);

	// 写回修正后的Bounds
	ApplyFixedBounds(RootNode, FixerRoot);

	// 输出修正日志
	for (const FFixerLogEntry& Entry : Log)
	{
		UE_LOG(LogDesign2UMG, Verbose, TEXT("Fixer [%s] %s: (%.4f,%.4f,%.4f,%.4f) -> (%.4f,%.4f,%.4f,%.4f)"),
			*Entry.WidgetId, *Entry.FixType,
			Entry.OldBounds.X, Entry.OldBounds.Y, Entry.OldBounds.W, Entry.OldBounds.H,
			Entry.NewBounds.X, Entry.NewBounds.Y, Entry.NewBounds.W, Entry.NewBounds.H);
	}
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

namespace
{

	/**
	 * 处理 Image 控件的 MaterialRef（程序化形状生成）
	 * @return true 如果成功生成并绑定纹理
	 */
	bool HandleImageMaterialRef(UImage* ImageWidget, const FUMGWidgetNode& Node)
	{
		FString MatRef;
		Design2UMGWidgetNodeAccess::TryGetWidgetString(Node, UMGPropertyKeys::MaterialRef, MatRef);
		if (MatRef.IsEmpty())
		{
			return false;
		}

		FUMGPropertyBag GenerationProps = Design2UMGWidgetNodeAccess::BuildTypedPropertyBag(Node);
		UTexture2D* Texture = FProceduralShapeGenerator::GenerateFromMaterialRef(MatRef, GenerationProps, ImageWidget);
		if (!Texture)
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("Image '%s' 未知形状类型: %s"), *Node.Name, *MatRef);
			return false;
		}

		ImageWidget->SetBrushFromTexture(Texture);
		FSlateBrush Brush = ImageWidget->GetBrush();
		Brush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
		Brush.Tiling = ESlateBrushTileType::NoTile;
		ImageWidget->SetBrush(Brush);
		ImageWidget->SetDesiredSizeOverride(Brush.ImageSize);
		ImageWidget->SynchronizeProperties();
		return true;
	}

	bool HandleImageTextureRef(UImage* ImageWidget, const FUMGWidgetNode& Node, const TMap<FString, UTexture2D*>* TextureMap)
	{
		FString TexPath;
		Design2UMGWidgetNodeAccess::TryGetWidgetString(Node, UMGPropertyKeys::TexturePath, TexPath);
		const FString TrimmedPath = TexPath.TrimStartAndEnd();
		if (TrimmedPath.IsEmpty())
		{
			return false;
		}
		UTexture2D* Texture = nullptr;

		// 优先从 TextureMap 查找（预览场景使用内存纹理）
		if (TextureMap)
		{
			if (UTexture2D* const* Found = TextureMap->Find(TrimmedPath))
			{
				Texture = *Found;
			}
		}

		// 回退：从资产路径加载
		if (!Texture)
		{
			Texture = LoadObject<UTexture2D>(nullptr, *TrimmedPath);
		}

		if (!Texture) return false;

		// 保留 ApplyWidgetProperties 已设置的 ImageSize 和 UVRegion
		const FSlateBrush OldBrush = ImageWidget->GetBrush();
		const FVector2D OldImageSize = OldBrush.ImageSize;
		const FBox2f OldUVRegion = OldBrush.GetUVRegion();

		ImageWidget->SetBrushFromTexture(Texture);
		FSlateBrush Brush = ImageWidget->GetBrush();
		Brush.Tiling = ESlateBrushTileType::NoTile;

		// WidthOverride/HeightOverride 优先（设计稿指定尺寸），否则用纹理原始尺寸
		// 注意：FSlateBrush 默认 ImageSize 是 32x32，不能用它判断是否有显式覆盖
		float SizeOverride = 0.0f;
		const bool bHadSizeOverride =
			Design2UMGWidgetNodeAccess::TryGetWidgetFloat(Node, UMGPropertyKeys::WidthOverride, SizeOverride) ||
			Design2UMGWidgetNodeAccess::TryGetWidgetFloat(Node, UMGPropertyKeys::HeightOverride, SizeOverride);
		Brush.ImageSize = bHadSizeOverride ? OldImageSize : FVector2D(Texture->GetSizeX(), Texture->GetSizeY());

		// 恢复 UVRegion（非默认值说明 Crop 已设置）
		if (OldUVRegion.GetSize().GetMax() > KINDA_SMALL_NUMBER)
			Brush.SetUVRegion(OldUVRegion);

		ImageWidget->SetBrush(Brush);
		ImageWidget->SetDesiredSizeOverride(Brush.ImageSize);
		ImageWidget->SynchronizeProperties();
		return true;
	}
}

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

			// Widget 尺寸（点锚点下 Right=Width, Bottom=Height）
			float Width = Slot.Offsets.Right;
			float Height = Slot.Offsets.Bottom;

			// 如果有 WidthOverride/HeightOverride，使用它们
			Design2UMGWidgetNodeAccess::TryGetWidgetFloat(Node, UMGPropertyKeys::WidthOverride, Width);
			Design2UMGWidgetNodeAccess::TryGetWidgetFloat(Node, UMGPropertyKeys::HeightOverride, Height);

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

