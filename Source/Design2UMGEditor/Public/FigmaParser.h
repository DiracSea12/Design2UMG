// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "PenSchema.h"

/**
 * Figma REST API JSON → FPenDocument 解析器
 *
 * 将 Figma 节点树转换为与 .pen 相同的 FPenNode AST，
 * 从而复用整个下游管线（Converter → Layout → Generator）。
 */
class FFigmaParser
{
public:
	/** 从 Figma GET /v1/files/:key 返回的完整 JSON 解析文档 */
	static bool ParseDocument(const FString& FigmaJson, FPenDocument& OutDocument);

	/** 从 Figma file URL 提取 file key */
	static bool ExtractFileKey(const FString& UrlOrKey, FString& OutKey);

	/** 收集所有 imageRef（用于后续 images API 调用） */
	static void CollectImageRefs(const FPenDocument& Document, TSet<FString>& OutImageRefs);

	/** 将 images API 返回的 imageRef→URL 映射回填到节点树 */
	static void ApplyImageUrls(FPenDocument& Document, const TMap<FString, FString>& ImageRefToUrl);

private:
	/** 递归解析 Figma 节点 */
	static TSharedPtr<FPenNode> ParseNode(const TSharedPtr<FJsonObject>& NodeObj, const TSharedPtr<FJsonObject>& ParentObj);

	/** 映射 Figma 节点类型 */
	static EPenNodeType MapNodeType(const FString& FigmaType);

	/** 解析布局属性（Auto Layout） */
	static void ParseLayout(const TSharedPtr<FJsonObject>& NodeObj, FPenNode& OutNode);

	/** 解析尺寸（layoutSizing / absoluteBoundingBox） */
	static void ParseSize(const TSharedPtr<FJsonObject>& NodeObj, FPenNode& OutNode, bool bParentHasAutoLayout);

	/** 解析填充（返回最顶层可见填充，向后兼容） */
	static FPenFill ParseFill(const TArray<TSharedPtr<FJsonValue>>& Fills);

	/** 解析所有可见填充层（底→顶顺序） */
	static TArray<FPenFill> ParseFills(const TArray<TSharedPtr<FJsonValue>>& Fills);

	/** 解析单个填充对象 */
	static FPenFill ParseSingleFill(const TSharedPtr<FJsonObject>& FillObj);

	/** 解析描边 */
	static FPenStroke ParseStroke(const TSharedPtr<FJsonObject>& NodeObj);

	/** 解析圆角 */
	static FPenCornerRadius ParseCornerRadius(const TSharedPtr<FJsonObject>& NodeObj);

	/** 解析特效 */
	static TArray<FPenEffect> ParseEffects(const TArray<TSharedPtr<FJsonValue>>& Effects);

	/** 解析文本属性 */
	static void ParseTextProperties(const TSharedPtr<FJsonObject>& NodeObj, FPenNode& OutNode);

	/** 解析坐标（绝对→相对） */
	static void ParsePosition(const TSharedPtr<FJsonObject>& NodeObj, const TSharedPtr<FJsonObject>& ParentObj, FPenNode& OutNode, bool bParentHasAutoLayout);

	/** RGBA 0-1 → #RRGGBBAA hex */
	static FString ColorToHex(const TSharedPtr<FJsonObject>& ColorObj, float Opacity = 1.0f);

	/** 收集节点树中的 imageRef */
	static void CollectImageRefsRecursive(const TArray<TSharedPtr<FPenNode>>& Nodes, TSet<FString>& OutRefs);

	/** 回填 imageRef URL */
	static void ApplyImageUrlsRecursive(TArray<TSharedPtr<FPenNode>>& Nodes, const TMap<FString, FString>& Map);

	/** 检查节点是否有 Auto Layout */
	static bool HasAutoLayout(const TSharedPtr<FJsonObject>& NodeObj);
};
