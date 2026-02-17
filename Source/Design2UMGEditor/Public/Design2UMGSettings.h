// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/Font.h"
#include "Components/Widget.h"
#include "Design2UMGSettings.generated.h"

/**
 * Widget 类覆盖规则：节点名称包含指定字符串时，替换为指定 Widget 类
 */
USTRUCT()
struct FWidgetClassOverride
{
	GENERATED_BODY()

	/** 节点名称匹配模式（包含匹配） */
	UPROPERTY(Config, EditAnywhere, Category = "Widget Class Overrides", meta = (DisplayName = "Name Contains"))
	FString NamePattern;

	/** 替换为的 Widget 类（必须继承自 UWidget） */
	UPROPERTY(Config, EditAnywhere, Category = "Widget Class Overrides", meta = (DisplayName = "Widget Class", AllowAbstract = "false"))
	TSoftClassPtr<UWidget> WidgetClass;
};

/**
 * Design To UMG 插件设置
 * 保存于 Editor Preferences（用户级，不提交版本库）
 */
UCLASS(config=EditorPerProjectUserSettings, meta=(DisplayName="Pen To UMG"))
class UDesign2UMGSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 获取设置单例 */
	static UDesign2UMGSettings* Get();

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
	FString RemoteImageImportPath = TEXT("/Game/Design2UMG/ImportedImages");

	UPROPERTY(Config, EditAnywhere, Category = "Remote Image Import", meta = (DisplayName = "Remote Image Timeout (Seconds)", ClampMin = "1", ClampMax = "120"))
	float RemoteImageTimeoutSeconds = 20.0f;

	// ========== Pencil MCP Connection ==========

	UPROPERTY(Config, EditAnywhere, Category = "Pencil Connection", meta = (DisplayName = "MCP Server Path (Optional)", ToolTip = "Override path to mcp-server binary. Leave empty for auto-discovery."))
	FString MCPServerPath;

	UPROPERTY(Config, EditAnywhere, Category = "Pencil Connection", meta = (DisplayName = "App Name (Optional)", ToolTip = "Override -app parameter. Leave empty for auto-detection (antigravity/code)."))
	FString PencilAppName;

	// ========== Font Import ==========

	UPROPERTY(Config, EditAnywhere, Category = "Font Import", meta = (DisplayName = "Auto Import Fonts", ToolTip = "Automatically download and import fonts from .pen file URLs during import."))
	bool bAutoImportFonts = false;

	// ========== Icon Font ==========

	/** 图标字体族名 → UE5 字体资产映射（如 "Material Symbols Sharp" → 对应的 UFont 资产） */
	UPROPERTY(Config, EditAnywhere, Category = "Icon Font", meta = (DisplayName = "Font Family → Font Asset", ToolTip = "Map icon font family names to UE5 font assets."))
	TMap<FString, TSoftObjectPtr<UFont>> IconFontAssetMap;

	/** 当字体族未命中 IconFontAssetMap 时，使用该默认字体资产路径（可选） */
	UPROPERTY(Config, EditAnywhere, Category = "Icon Font", meta = (DisplayName = "Default Icon Font Asset Path", ToolTip = "Optional fallback font asset path for icon fonts when no explicit family mapping is found."))
	FString DefaultIconFontAssetPath;

	/** 无可用字体资产时的占位文本；留空则回退为 IconName 原文 */
	UPROPERTY(Config, EditAnywhere, Category = "Icon Font", meta = (DisplayName = "Missing Icon Placeholder", ToolTip = "When icon font asset is missing, use this placeholder text instead of IconName. Leave empty to keep IconName."))
	FString MissingIconFontPlaceholder;

	/** 查找图标字体的资产路径，无匹配返回空字符串 */
	FString FindIconFontAssetPath(const FString& FontFamily) const;

	// ========== Widget Class Overrides ==========

	UPROPERTY(Config, EditAnywhere, Category = "Widget Class Overrides", meta = (DisplayName = "Class Override Rules", ToolTip = "When a node name contains the pattern, use the specified Widget class instead of the default."))
	TArray<FWidgetClassOverride> ClassOverrides;

	/** 根据节点名称查找覆盖类，无匹配返回 nullptr */
	UClass* FindClassOverride(const FString& NodeName) const;
};
