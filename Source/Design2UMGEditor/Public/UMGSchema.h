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
	float FontSize = 0;              // 0 = unset
	float SourceFontSize = 0;        // Original design font size before px->UMG scaling
	float LetterSpacing = 0;
	bool bHasLetterSpacing = false;  // 鍖哄垎"鏄惧紡璁剧疆涓?"鍜?鏈缃?
};

/**
 * 绫诲瀷鍖栬瑙夌敾鍒峰睘鎬э紙娓愯繘寮忔浛浠?Properties 涓殑瑙嗚鐩稿叧 key锛? * WrapWithBorder 鏃舵暣浣撹浆绉诲埌 Border 鑺傜偣
 */
struct FUMGVisualBrushData
{
	float OutlineCornerRadius = -1;  // -1 = unset
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
 * Widget 鑺傜偣鏁版嵁缁撴瀯 - 鎻忚堪鍗曚釜鎺т欢锛坴2.4鏇存柊锛? */
USTRUCT()
struct FUMGWidgetNode
{
	GENERATED_BODY()

	/** 鎺т欢绫诲瀷: CanvasPanel, VerticalBox, HorizontalBox, Button, TextBlock, Image, etc. */
	UPROPERTY()
	FString Type;

	/** 绋冲畾鍞竴鏍囪瘑锛岃矾寰勫紡锛歿ParentId}/{Type}_{Index}锛坴2.2鏂板锛?*/
	UPROPERTY()
	FString Id;

	/** 鎺т欢鍚嶇О */
	UPROPERTY()
	FString Name;

	/** 鍍忕礌鍧愭爣Bounds锛堢敱LayoutCalculator浠嶴lot璁＄畻锛?*/
	FBounds Bounds;

	/** 甯冨眬妲芥暟鎹紙鐢盠ayoutCalculator浠嶣ounds璁＄畻锛?*/
	UPROPERTY()
	FUMGSlotData Slot;

	/** 
	 * 灞炴€ч敭鍊煎锛坙egacy锛岄€愭鐢?Brush 鏇夸唬锛?
	 * @note [TODO] 瀹屽叏閲嶆瀯涓哄己绫诲瀷鏁版嵁锛屾秷闄ゅ瓧绗︿覆 Map 甯︽潵鐨勭被鍨嬪畨鍏ㄥ拰缁存姢椋庨櫓銆?	 *       涓哄悜鍚庡吋瀹瑰綋鍓嶇増鏈繚鐣欙紝浣嗘柊灞炴€х郴缁熶笉搴斿啀娣诲姞鍒版 Map 涓€?	 */
	UPROPERTY()
	TMap<FString, FString> Properties;

	/** 绫诲瀷鍖栨枃鏈牱寮忥紙v2.4 鎷嗗垎鑷?Brush锛?*/
	FUMGTextStyleData TextStyle;

	/** 绫诲瀷鍖栬瑙夌敾鍒凤紙v2.4 鎷嗗垎鑷?Brush锛學rapWithBorder 鏃惰浆绉伙級 */
	FUMGVisualBrushData Brush;

	/** 瀛愭帶浠?(涓嶅彲搴忓垪鍖栵紝UHT 涓嶆敮鎸侀€掑綊 USTRUCT) */
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

