// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "UMGSchema.generated.h"

/**
 * 像素坐标Bounds（定义在UMGSchema避免循环依赖）
 */
struct FBounds
{
	float X = 0.0f;  // 左上角X（像素）
	float Y = 0.0f;  // 左上角Y（像素）
	float W = 0.0f;  // 宽度（像素）
	float H = 0.0f;  // 高度（像素）

	float GetRight() const { return X + W; }
	float GetBottom() const { return Y + H; }
	float GetCenterX() const { return X + W / 2.0f; }
	float GetCenterY() const { return Y + H / 2.0f; }
};

/**
 * Anchor 数据结构
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
 * Anchors 数据结构 (Min/Max)
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
 * Offsets 数据结构
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
 * Slot 数据结构 - 描述控件在父容器中的布局信息
 */
USTRUCT()
struct FUMGSlotData
{
	GENERATED_BODY()

	// CanvasPanel 专用
	UPROPERTY()
	FUMGAnchorsData Anchors;

	UPROPERTY()
	FUMGAnchorData Alignment;

	UPROPERTY()
	FUMGOffsetsData Offsets;

	// 通用
	UPROPERTY()
	FMargin Padding = FMargin(0.0f);

	// VerticalBox/HorizontalBox/Overlay 专用
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

	// GridPanel 专用
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
 * 类型化文本样式属性（FontSize, LetterSpacing 等文本专属字段）
 * WrapWithBorder 时不随视觉属性转移，留在原 Widget 上
 */
struct FUMGTextStyleData
{
	float FontSize = 0;              // 0 = 未设置
	float LetterSpacing = 0;
	bool bHasLetterSpacing = false;  // 区分"显式设置为0"和"未设置"
};

/**
 * 类型化视觉画刷属性（渐进式替代 Properties 中的视觉相关 key）
 * WrapWithBorder 时整体转移到 Border 节点
 */
struct FUMGVisualBrushData
{
	float OutlineCornerRadius = -1;  // -1 = 未设置
	FVector4 CornerRadii = FVector4(-1, -1, -1, -1);
	float OutlineThickness = 0;

	ESlateBrushDrawType::Type DrawAs = ESlateBrushDrawType::NoDrawType;
	bool bHasDrawAs = false;         // 区分"显式NoDrawType"和"未设置"
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
 * Widget 节点数据结构 - 描述单个控件（v2.4更新）
 */
USTRUCT()
struct FUMGWidgetNode
{
	GENERATED_BODY()

	/** 控件类型: CanvasPanel, VerticalBox, HorizontalBox, Button, TextBlock, Image, etc. */
	UPROPERTY()
	FString Type;

	/** 稳定唯一标识，路径式：{ParentId}/{Type}_{Index}（v2.2新增） */
	UPROPERTY()
	FString Id;

	/** 控件名称 */
	UPROPERTY()
	FString Name;

	/** 像素坐标Bounds（由LayoutCalculator从Slot计算） */
	FBounds Bounds;

	/** 布局槽数据（由LayoutCalculator从Bounds计算） */
	UPROPERTY()
	FUMGSlotData Slot;

	/** 属性键值对（legacy，逐步由 Brush 替代） */
	UPROPERTY()
	TMap<FString, FString> Properties;

	/** 类型化文本样式（v2.4 拆分自 Brush） */
	FUMGTextStyleData TextStyle;

	/** 类型化视觉画刷（v2.4 拆分自 Brush，WrapWithBorder 时转移） */
	FUMGVisualBrushData Brush;

	/** 子控件 (不可序列化，UHT 不支持递归 USTRUCT) */
	TArray<FUMGWidgetNode> Children;
};

/**
 * Widget 蓝图数据结构 - 描述整个 Widget Blueprint
 */
USTRUCT()
struct FUMGWidgetData
{
	GENERATED_BODY()

	/** 生成的 Widget Blueprint 名称 */
	UPROPERTY()
	FString WidgetName = TEXT("WBP_Generated");

	/** 根控件 */
	UPROPERTY()
	FUMGWidgetNode RootWidget;
};
