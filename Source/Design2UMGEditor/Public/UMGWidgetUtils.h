// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UMGSchema.h"

class UWidget;
class UPanelSlot;
class UPanelWidget;
class UWidgetTree;
class UWidgetBlueprint;

/**
 * UMG Widget 工具类 - 共享的 Widget 创建和属性应用逻辑
 */
class FUMGWidgetUtils
{
public:
	// ========== Widget 创建 ==========

	/** 递归创建 Widget 树（TextureMap 非空时将匹配的纹理绑定到 Image 控件） */
	static UWidget* CreateWidgetRecursive(UWidgetTree* WidgetTree, const FUMGWidgetNode& Node, UPanelWidget* ParentWidget = nullptr, const TMap<FString, UTexture2D*>* TextureMap = nullptr);

	/** 应用Fixer修正Bounds（在CreateWidgetRecursive之前调用）
	 * @param RootNode Widget树根节点
	 * @param DesignSize 设计尺寸（UMG目标）
	 */
	static void ApplyFixer(FUMGWidgetNode& RootNode, const FIntPoint& DesignSize);

	/** 从Widget树计算所需的设计尺寸（遍历所有子节点，找出最大边界） */
	static FVector2D CalculateDesignSize(const FUMGWidgetNode& RootNode, FVector2D DefaultSize = FVector2D(1920, 1080));

	// ========== Widget 类型解析 ==========

	/** 根据类型名称获取 Widget 类 */
	static UClass* ResolveWidgetClass(const FString& TypeName);

	// ========== 颜色解析 ==========

	/** 解析颜色字符串 - 支持 #RRGGBB 和颜色名称 */
	static FLinearColor ParseColor(const FString& ColorStr);

	// ========== 对齐方式解析 ==========

	static EHorizontalAlignment ParseHAlign(const FString& Str);
	static EVerticalAlignment ParseVAlign(const FString& Str);
	static ESlateVisibility ParseVisibility(const FString& Str);

	// ========== 几何解析 ==========

	static FVector2D ParseVector2D(const FString& Str, FVector2D Default = FVector2D::ZeroVector);
	static FMargin ParseMargin(const FString& Str);

	/** FMargin 近似相等比较 */
	static bool IsNearlyEqualMargin(const FMargin& A, const FMargin& B)
	{
		return FMath::IsNearlyEqual(A.Left, B.Left) &&
			FMath::IsNearlyEqual(A.Top, B.Top) &&
			FMath::IsNearlyEqual(A.Right, B.Right) &&
			FMath::IsNearlyEqual(A.Bottom, B.Bottom);
	}

	// ========== Slot 属性应用 ==========

	/** 应用 Slot 属性到各种类型的 Slot */
	static void ApplySlotProperties(UPanelSlot* Slot, const FUMGSlotData& SlotData);

	// ========== Widget 属性应用 ==========

	/** 应用类型特定的 Widget 属性（legacy string map） */
	static void ApplyWidgetProperties(UWidget* Widget, const TMap<FString, FString>& Properties);

	/** 应用类型特定的 Widget 属性（typed 优先，legacy 回退） */
	static void ApplyWidgetProperties(UWidget* Widget, const FUMGWidgetNode& Node);

	// ========== Widget 渲染 ==========

	/** 将 WidgetBlueprint 渲染为 PNG 图片数据（需要已编译的 GeneratedClass） */
	static bool RenderWidgetToPng(UWidgetBlueprint* WidgetBP, const FIntPoint& RenderSize, TArray<uint8>& OutPngData, FIntPoint& OutSize);

	/** 直接从 WidgetTree 渲染为 PNG（编译前调用，transient 纹理数据仍有效） */
	static bool RenderWidgetTreeToPng(UWidgetBlueprint* WidgetBP, const FIntPoint& RenderSize, TArray<uint8>& OutPngData, FIntPoint& OutSize);

	// ========== Widget 名称处理 ==========

	/** 清理 Widget 名称，移除非法字符 */
	static FName SanitizeWidgetName(const FString& InName, const FString& FallbackPrefix);

private:
	/** 应用通用 Widget 属性（内部使用） */
	static void ApplyCommonProperties(UWidget* Widget, const TMap<FString, FString>& Properties);
};
