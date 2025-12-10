// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

/**
 * ImageToUMG 插件常量定义
 */
namespace ImageToUMGConstants
{
	// ============================================================================
	// AI 相关
	// ============================================================================

	/** AI API 推荐最大图像尺寸 */
	constexpr int32 MaxAIDimension = 2048;

	/** 默认最大输出 Token 数 */
	constexpr int32 DefaultMaxTokens = 4096;

	/** 默认 HTTP 请求超时时间（秒） */
	constexpr float DefaultTimeoutSeconds = 120.0f;

	/** 错误响应预览长度 */
	constexpr int32 ErrorResponsePreviewLen = 500;

	/** 错误响应详细长度 */
	constexpr int32 ErrorResponseDetailLen = 800;

	// ============================================================================
	// UI 相关
	// ============================================================================

	/** 通知显示时长（秒） */
	constexpr float NotificationDuration = 5.0f;

	/** 预览窗口默认宽度 */
	constexpr int32 PreviewWindowWidth = 1400;

	/** 预览窗口默认高度 */
	constexpr int32 PreviewWindowHeight = 900;

	/** JSON 编辑防抖延迟（秒） */
	constexpr float JsonEditDebounceSeconds = 0.5f;

	// ============================================================================
	// 图像处理
	// ============================================================================

	/** 纹理最大尺寸限制 */
	constexpr int32 MaxTextureDimension = 4096;

	/** 资产名称后缀最大尝试次数 */
	constexpr int32 MaxAssetNameSuffix = 100;

	/** Widget Blueprint 名称后缀最大尝试次数 */
	constexpr int32 MaxWidgetNameSuffix = 1000;

	/** 并行处理阈值（图像高度超过此值时使用并行处理） */
	constexpr int32 ParallelProcessingThreshold = 256;

	/** HTTP 请求体大小警告阈值（MB） */
	constexpr int32 MaxRequestBodySizeMB = 20;
}
