// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ImageToUMGSettings.generated.h"

/**
 * Image To UMG 插件设置
 * 保存在 Editor Preferences 中（用户级别，不会提交到版本控制）
 * 路径：Edit -> Editor Preferences -> Plugins -> Image To UMG
 */
UCLASS(config=EditorPerProjectUserSettings, meta=(DisplayName="Image To UMG"))
class UImageToUMGSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 获取单例实例 */
	static UImageToUMGSettings* Get();

	/** 获取设置类别名称 - 显示在 Editor Preferences 中 */
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	virtual FName GetSectionName() const override { return TEXT("Image To UMG"); }

	// ========== UMG 设计分辨率 ==========

	/** UMG设计分辨率宽度 */
	UPROPERTY(Config, EditAnywhere, Category = "Generation Settings", meta = (DisplayName = "Design Width", ClampMin = "320", ClampMax = "7680"))
	int32 DesignWidth = 1920;

	/** UMG设计分辨率高度 */
	UPROPERTY(Config, EditAnywhere, Category = "Generation Settings", meta = (DisplayName = "Design Height", ClampMin = "240", ClampMax = "4320"))
	int32 DesignHeight = 1080;

	/** 获取设计尺寸 */
	FIntPoint GetDesignSize() const { return FIntPoint(DesignWidth, DesignHeight); }

	/** 纵横比处理模式：Stretch=拉伸(默认), Fit=保持比例(letterbox) */
	UPROPERTY(Config, EditAnywhere, Category = "Generation Settings", meta = (DisplayName = "Aspect Ratio Mode"))
	bool bUseStretchMode = true;

	// ========== PenToUMG 设置 ==========

	/** .pen 文件默认导入路径 */
	UPROPERTY(Config, EditAnywhere, Category = "PenToUMG Settings", meta = (DisplayName = "Pen Files Path"))
	FString PenFilesPath = TEXT("/Game/UI/PenDesigns");

	/** 是否自动展开组件引用 */
	UPROPERTY(Config, EditAnywhere, Category = "PenToUMG Settings", meta = (DisplayName = "Auto Expand Refs"))
	bool bAutoExpandRefs = true;

};
