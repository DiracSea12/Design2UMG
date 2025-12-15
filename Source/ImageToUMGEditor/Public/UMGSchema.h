// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UMGSchema.generated.h"

/**
 * 预设 Material 条目 - 用于 AI 引用
 */
USTRUCT(BlueprintType)
struct FUMGPresetMaterial
{
	GENERATED_BODY()

	/** AI 引用名称，如 "Circle", "Ring" */
	UPROPERTY(EditAnywhere, Category = "Material")
	FString Name;

	/** Material 资产路径 */
	UPROPERTY(EditAnywhere, Category = "Material", meta = (AllowedClasses = "/Script/Engine.MaterialInterface"))
	FSoftObjectPath MaterialPath;

	/** 给 AI 的描述，说明用途 */
	UPROPERTY(EditAnywhere, Category = "Material")
	FString Description;

	/** 支持的参数名称列表 */
	UPROPERTY(EditAnywhere, Category = "Material")
	TArray<FString> Parameters;
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
 * Widget 节点数据结构 - 描述单个控件
 */
USTRUCT()
struct FUMGWidgetNode
{
	GENERATED_BODY()

	/** 控件类型: CanvasPanel, VerticalBox, HorizontalBox, Button, TextBlock, Image, etc. */
	UPROPERTY()
	FString Type;

	/** 控件名称 */
	UPROPERTY()
	FString Name;

	/** 布局槽数据 */
	UPROPERTY()
	FUMGSlotData Slot;

	/** 属性键值对 */
	UPROPERTY()
	TMap<FString, FString> Properties;

	/** 子控件 (不可序列化，UHT 不支持递归 USTRUCT) */
	TArray<FUMGWidgetNode> Children;
};

/**
 * 图像资产数据结构 - 描述需要提取/生成的图像
 */
USTRUCT()
struct FUMGImageAsset
{
	GENERATED_BODY()

	/** 资产 ID（用于关联到 Widget 的 ImageRef 属性） */
	UPROPERTY()
	FString Id;

	/** 资产类型: Icon, Background, Avatar, Texture */
	UPROPERTY()
	FString Type;

	/** AI 描述（用于图像生成 API 的 prompt） */
	UPROPERTY()
	FString Description;

	/** 在原图中的边界框（像素坐标） */
	UPROPERTY()
	int32 X = 0;

	UPROPERTY()
	int32 Y = 0;

	UPROPERTY()
	int32 Width = 0;

	UPROPERTY()
	int32 Height = 0;

	/** 建议的输出尺寸（用于图像生成 API） */
	UPROPERTY()
	int32 SuggestedWidth = 256;

	UPROPERTY()
	int32 SuggestedHeight = 256;

	/** 是否需要移除背景 */
	UPROPERTY()
	bool bRemoveBg = false;

	/** 背景色（十六进制，如 "#000000"） */
	UPROPERTY()
	FString BgColor;

	/** 前景色（可选，用于优化抠图） */
	UPROPERTY()
	FString FgColor;
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

	/** 需要提取/生成的图像资产列表 */
	TArray<FUMGImageAsset> ImageAssets;
};
