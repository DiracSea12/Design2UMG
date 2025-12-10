// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UMGSchema.h"
#include "ImageAssetProcessor.h"

class UWidget;
class UPanelSlot;
class UPanelWidget;
class UWidgetTree;

/**
 * UMG Widget 工具类 - 共享的 Widget 创建和属性应用逻辑
 */
class FUMGWidgetUtils
{
public:
	// ========== Widget 创建 ==========

	/** 递归创建 Widget 树（统一入口，TextureMap 为空时不绑定纹理） */
	static UWidget* CreateWidgetRecursive(UWidgetTree* WidgetTree, const FUMGWidgetNode& Node, UPanelWidget* ParentWidget = nullptr, const TMap<FString, UTexture2D*>* TextureMap = nullptr);

	/** 绑定图像资产到 Widget 节点（将处理后的纹理路径写入 Properties） */
	static void BindImageAssets(FUMGWidgetNode& RootNode, const TArray<FProcessedImageAsset>& Assets);

	/** 构建 ImageRef -> Texture 的映射表（用于预览） */
	static TMap<FString, UTexture2D*> BuildTextureMap(const TArray<FProcessedImageAsset>& Assets);

	/** 从Widget节点的Slot信息生成ImageAssets（AI未返回时的兜底），同时给Widget添加ImageRef */
	static void GenerateImageAssetsFromWidgets(FUMGWidgetNode& Node, TArray<FUMGImageAsset>& OutAssets);

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

	// ========== Slot 属性应用 ==========

	/** 应用 Slot 属性到各种类型的 Slot */
	static void ApplySlotProperties(UPanelSlot* Slot, const FUMGSlotData& SlotData);

	// ========== Widget 属性应用 ==========

	/** 应用类型特定的 Widget 属性 */
	static void ApplyWidgetProperties(UWidget* Widget, const TMap<FString, FString>& Properties);

	// ========== Widget 名称处理 ==========

	/** 清理 Widget 名称，移除非法字符 */
	static FName SanitizeWidgetName(const FString& InName, const FString& FallbackPrefix);

private:
	/** 应用通用 Widget 属性（内部使用） */
	static void ApplyCommonProperties(UWidget* Widget, const TMap<FString, FString>& Properties);
};
