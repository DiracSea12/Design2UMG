// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "UMGSchema.generated.h"

/**
 * 鍍忕礌鍧愭爣Bounds锛堝畾涔夊湪UMGSchema閬垮厤寰幆渚濊禆锛? */
struct FBounds
{
	float X = 0.0f;  // 宸︿笂瑙扻锛堝儚绱狅級
	float Y = 0.0f;  // 宸︿笂瑙扽锛堝儚绱狅級
	float W = 0.0f;  // 瀹藉害锛堝儚绱狅級
	float H = 0.0f;  // 楂樺害锛堝儚绱狅級

	float GetRight() const { return X + W; }
	float GetBottom() const { return Y + H; }
	float GetCenterX() const { return X + W / 2.0f; }
	float GetCenterY() const { return Y + H / 2.0f; }
};

/**
 * Anchor 鏁版嵁缁撴瀯
 */
USTRUCT()
struct FUMGAnchorData
{
	GENERATED_BODY()

	UPROPERTY()
	float X = 0.0f;

	UPROPERTY()
	float Y = 0.0f;
};

/**
 * Anchors 鏁版嵁缁撴瀯 (Min/Max)
 */
USTRUCT()
struct FUMGAnchorsData
{
	GENERATED_BODY()

	UPROPERTY()
	FUMGAnchorData Min;

	UPROPERTY()
	FUMGAnchorData Max;
};

/**
 * Offsets 鏁版嵁缁撴瀯
 */
USTRUCT()
struct FUMGOffsetsData
{
	GENERATED_BODY()

	UPROPERTY()
	float Left = 0.0f;

	UPROPERTY()
	float Top = 0.0f;

	UPROPERTY()
	float Right = 100.0f;

	UPROPERTY()
	float Bottom = 100.0f;
};

/**
 * Slot 鏁版嵁缁撴瀯 - 鎻忚堪鎺т欢鍦ㄧ埗瀹瑰櫒涓殑甯冨眬淇℃伅
 */
USTRUCT()
struct FUMGSlotData
{
	GENERATED_BODY()

	// CanvasPanel 涓撶敤
	UPROPERTY()
	FUMGAnchorsData Anchors;

	UPROPERTY()
	FUMGAnchorData Alignment;

	UPROPERTY()
	FUMGOffsetsData Offsets;

	// 閫氱敤
	UPROPERTY()
	FMargin Padding = FMargin(0.0f);

	// VerticalBox/HorizontalBox/Overlay 涓撶敤
	UPROPERTY()
	FString HorizontalAlignment = TEXT("Fill"); // Left, Center, Right, Fill

	UPROPERTY()
	FString VerticalAlignment = TEXT("Fill"); // Top, Center, Bottom, Fill

	UPROPERTY()
	FString SizeRule = TEXT("Auto"); // Auto, Fill

	UPROPERTY()
	float FillWeight = 1.0f;

	// CanvasPanel: Size To Content
	UPROPERTY()
	bool bAutoSize = false;

	// GridPanel 涓撶敤
	UPROPERTY()
	int32 Row = 0;

	UPROPERTY()
	int32 Column = 0;

	UPROPERTY()
	int32 RowSpan = 1;

	UPROPERTY()
	int32 ColumnSpan = 1;
};

/**
 * 绫诲瀷鍖栨枃鏈牱寮忓睘鎬э紙FontSize, LetterSpacing 绛夋枃鏈笓灞炲瓧娈碉級
 * WrapWithBorder 鏃朵笉闅忚瑙夊睘鎬ц浆绉伙紝鐣欏湪鍘?Widget 涓? */
struct FUMGTextStyleData
{
	float FontSize = 0;              // 0 = 鏈缃?
	float LetterSpacing = 0;
	bool bHasLetterSpacing = false;  // 鍖哄垎"鏄惧紡璁剧疆涓?"鍜?鏈缃?
};

/**
 * 绫诲瀷鍖栬瑙夌敾鍒峰睘鎬э紙娓愯繘寮忔浛浠?Properties 涓殑瑙嗚鐩稿叧 key锛? * WrapWithBorder 鏃舵暣浣撹浆绉诲埌 Border 鑺傜偣
 */
struct FUMGVisualBrushData
{
	float OutlineCornerRadius = -1;  // -1 = 鏈缃?
	FVector4 CornerRadii = FVector4(-1, -1, -1, -1);
	float OutlineThickness = 0;

	ESlateBrushDrawType::Type DrawAs = ESlateBrushDrawType::NoDrawType;
	bool bHasDrawAs = false;         // 鍖哄垎"鏄惧紡NoDrawType"鍜?鏈缃?
	FLinearColor BrushColor = FLinearColor::Transparent;
	bool bHasBrushColor = false;
	FLinearColor OutlineColor = FLinearColor::Transparent;
	bool bHasOutlineColor = false;

	FString MaterialRef;
	FString MaterialParams;
	FString TexturePath;

	bool HasCornerRadii() const { return CornerRadii.X >= 0; }
};

/**
 * Widget 节点数据结构 - 描述单个控件（v2.4 更新）
 */
USTRUCT()
struct FUMGWidgetNode
{
	GENERATED_BODY()

	/** 控件类型: CanvasPanel, VerticalBox, HorizontalBox, Button, TextBlock, Image 等 */
	UPROPERTY()
	FString Type;

	/** 稳定唯一标识，路径格式：{ParentId}/{Type}_{Index}（v2.2 新增） */
	UPROPERTY()
	FString Id;

	/** 控件名称 */
	UPROPERTY()
	FString Name;

	/** 像素坐标 Bounds（由 LayoutCalculator 从 Slot 反推） */
	FBounds Bounds;

	/** 布局槽数据（由 LayoutCalculator 从 Bounds 计算） */
	UPROPERTY()
	FUMGSlotData Slot;

	/**
	 * 属性键值对（legacy，逐步由 Brush 替代）
	 * @note [TODO] 后续重构为强类型数据，减少 string map 带来的类型安全和维护风险。
	 *       当前为向后兼容保留，新属性系统不应继续写入此 Map。
	 */
	UPROPERTY()
	TMap<FString, FString> Properties;

	/** 类型化文本样式（v2.4 从 Brush 中拆分） */
	FUMGTextStyleData TextStyle;

	/** 类型化视觉画刷（v2.4 从 Brush 中拆分；WrapWithBorder 时会转移） */
	FUMGVisualBrushData Brush;

	/** 子控件（不可序列化，UHT 不支持递归 USTRUCT） */
	TArray<FUMGWidgetNode> Children;
};

/**
 * Widget 钃濆浘鏁版嵁缁撴瀯 - 鎻忚堪鏁翠釜 Widget Blueprint
 */
USTRUCT()
struct FUMGWidgetData
{
	GENERATED_BODY()

	/** 鐢熸垚鐨?Widget Blueprint 鍚嶇О */
	UPROPERTY()
	FString WidgetName = TEXT("WBP_Generated");

	/** 鏍规帶浠?*/
	UPROPERTY()
	FUMGWidgetNode RootWidget;

	/** 杞崲杩囩▼涓殑闄嶇骇璀﹀憡锛堝 Ellipse鈫払order锛?*/
	TArray<FString> Warnings;
};
