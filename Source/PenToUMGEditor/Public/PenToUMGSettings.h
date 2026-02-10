// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "PenToUMGSettings.generated.h"

/**
 * Pen To UMG 插件设置
 * 保存于 Editor Preferences（用户级，不提交版本库）
 */
UCLASS(config=EditorPerProjectUserSettings, meta=(DisplayName="Pen To UMG"))
class UPenToUMGSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 获取设置单例 */
	static UPenToUMGSettings* Get();

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	virtual FName GetSectionName() const override { return TEXT("Pen To UMG"); }

	// ========== UMG 设计分辨率 ==========

	UPROPERTY(Config, EditAnywhere, Category = "Generation Settings", meta = (DisplayName = "Design Width", ClampMin = "320", ClampMax = "7680"))
	int32 DesignWidth = 1920;

	UPROPERTY(Config, EditAnywhere, Category = "Generation Settings", meta = (DisplayName = "Design Height", ClampMin = "240", ClampMax = "4320"))
	int32 DesignHeight = 1080;

	FIntPoint GetDesignSize() const { return FIntPoint(DesignWidth, DesignHeight); }

	// ========== Remote Image Import ==========

	UPROPERTY(Config, EditAnywhere, Category = "Remote Image Import", meta = (DisplayName = "Enable Remote Image Import"))
	bool bEnableRemoteImageImport = true;

	UPROPERTY(Config, EditAnywhere, Category = "Remote Image Import", meta = (DisplayName = "Remote Image Import Path"))
	FString RemoteImageImportPath = TEXT("/Game/PenToUMG/ImportedImages");

	UPROPERTY(Config, EditAnywhere, Category = "Remote Image Import", meta = (DisplayName = "Remote Image Timeout (Seconds)", ClampMin = "1", ClampMax = "120"))
	float RemoteImageTimeoutSeconds = 20.0f;

	// ========== Pencil MCP Connection ==========

	UPROPERTY(Config, EditAnywhere, Category = "Pencil Connection", meta = (DisplayName = "MCP Server Path (Optional)", ToolTip = "Override path to mcp-server binary. Leave empty for auto-discovery."))
	FString MCPServerPath;

	UPROPERTY(Config, EditAnywhere, Category = "Pencil Connection", meta = (DisplayName = "App Name (Optional)", ToolTip = "Override -app parameter. Leave empty for auto-detection (antigravity/code)."))
	FString PencilAppName;

};
