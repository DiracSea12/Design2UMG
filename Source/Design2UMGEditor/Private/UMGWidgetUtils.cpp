// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGWidgetUtils.h"
#include "WidgetTreeChildBuilder.h"
#include "WidgetMetadataCollector.h"
#include "WidgetResourceBinder.h"
#include "Design2UMGLog.h"
#include "Design2UMGSettings.h"
#include "LayoutCalculator.h"
#include "LayoutFixer.h"
#include "WidgetLayoutAdapter.h"
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
	thread_local int32 GWidgetBuildDepth = 0;
	thread_local FIntPoint GWidgetBuildDesignSizeSnapshot = FIntPoint(1920, 1080);

	FIntPoint SanitizeEffectiveDesignSize(const FIntPoint& InSize)
	{
		return FIntPoint(
			FMath::Max(1, InSize.X),
			FMath::Max(1, InSize.Y));
	}

	class FScopedWidgetBuildDesignSize
	{
	public:
		explicit FScopedWidgetBuildDesignSize(const FIntPoint* ExplicitDesignSize)
		{
			if (GWidgetBuildDepth == 0)
			{
				GWidgetBuildDesignSizeSnapshot = SanitizeEffectiveDesignSize(
					ExplicitDesignSize ? *ExplicitDesignSize : FIntPoint(1920, 1080));
				bOwnsSnapshot = true;
			}
			++GWidgetBuildDepth;
		}

		~FScopedWidgetBuildDesignSize()
		{
			GWidgetBuildDepth = FMath::Max(0, GWidgetBuildDepth - 1);
			if (bOwnsSnapshot && GWidgetBuildDepth == 0)
			{
				GWidgetBuildDesignSizeSnapshot = FIntPoint(1920, 1080);
			}
		}

		static FIntPoint Current()
		{
			return SanitizeEffectiveDesignSize(
				GWidgetBuildDepth > 0
					? GWidgetBuildDesignSizeSnapshot
					: FIntPoint(1920, 1080));
		}

	private:
		bool bOwnsSnapshot = false;
	};

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

}

UWidget* FUMGWidgetUtils::BuildWidgetTree(UWidgetTree* WidgetTree, const FUMGWidgetNode& Node, const TMap<FString, UTexture2D*>* TextureMap, TArray<FDesign2UMGFigmaWidgetMetadataRecord>* FigmaMetadataRecords, const FIntPoint* ExplicitDesignSize)
{
	UClass* WidgetClass = ResolveWidgetClass(Node.Type);
	if (UClass* Override = UDesign2UMGSettings::Get()->FindClassOverride(Node.Name))
	{
		WidgetClass = Override;
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
	const FScopedWidgetBuildDesignSize DesignSizeScope(ExplicitDesignSize);

	UWidget* NewWidget = BuildWidgetTree(WidgetTree, Node, TextureMap, FigmaMetadataRecords, ExplicitDesignSize);
	if (!NewWidget)
	{
		return nullptr;
	}

	// 添加到父容器并应用 Slot 属性
	if (ParentWidget)
	{
		if (UPanelSlot* Slot = ParentWidget->AddChild(NewWidget))
		{
			const bool bEnableAdaptiveAnchors = UDesign2UMGSettings::Get()->bEnableAdaptiveRootCanvasAnchors;
			const FUMGSlotData SlotData = FWidgetLayoutAdapter::Resolve(
				Node, ParentWidget, WidgetTree, FScopedWidgetBuildDesignSize::Current(), bEnableAdaptiveAnchors);
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

