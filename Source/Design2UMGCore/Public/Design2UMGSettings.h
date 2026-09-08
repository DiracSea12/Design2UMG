// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/Font.h"
#include "Components/Widget.h"
#include "Design2UMGSettings.generated.h"

/**
 * Widget class override rule. When the node name contains the given pattern,
 * the generated widget class is replaced with the configured class.
 */
USTRUCT()
struct FWidgetClassOverride
{
	GENERATED_BODY()

	/** Node name match pattern using a contains-style check. */
	UPROPERTY(Config, EditAnywhere, Category = "Widget Class Overrides", meta = (DisplayName = "Name Contains"))
	FString NamePattern;

	/** Replacement widget class. Must derive from UWidget. */
	UPROPERTY(Config, EditAnywhere, Category = "Widget Class Overrides", meta = (DisplayName = "Widget Class", AllowAbstract = "false"))
	TSoftClassPtr<UWidget> WidgetClass;
};

UENUM()
enum class EDesign2UMGPrototypeNavigationGenerationMode : uint8
{
	RuntimeExtension,
	TagHelper,
	Disabled
};

/**
 * Design2UMG plugin settings.
 * Stored in Editor Preferences as user-scoped settings rather than source-controlled config.
 */
UCLASS(config=EditorPerProjectUserSettings, meta=(DisplayName="Design2UMG"))
class DESIGN2UMGCORE_API UDesign2UMGSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Return the settings singleton without migration or save/flush side effects. */
	static UDesign2UMGSettings* Get();
	static constexpr const TCHAR* CanonicalPlaceholderHatchBackgroundColor = TEXT("#D282FF33");
	static constexpr const TCHAR* CanonicalPlaceholderRefHatchBackgroundColor = TEXT("#9682FF33");
	static constexpr const TCHAR* CanonicalPlaceholderHatchStripeColor = TEXT("#D480FF");
	static constexpr const TCHAR* CanonicalPlaceholderRefHatchStripeColor = TEXT("#9580FF");
	static constexpr int32 CanonicalPlaceholderHatchStripeWidth = 1;
	static constexpr int32 CanonicalPlaceholderHatchStripeGap = 4;
	static constexpr float CanonicalLuminosityFitOpacityCap = 0.15f;

	struct FResolvedPlaceholderHatchStyle
	{
		FString BaseFillColor;
		FString RefFillColor;
		FString BaseStripeColor;
		FString RefStripeColor;
		int32 StripeWidthPx = CanonicalPlaceholderHatchStripeWidth;
		int32 StripeGapPx = CanonicalPlaceholderHatchStripeGap;
	};

	FResolvedPlaceholderHatchStyle ResolvePlaceholderHatchStyle() const;
	float ResolveLuminosityFitOpacityCap() const;

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	virtual FName GetSectionName() const override { return TEXT("Design2UMG"); }

	// ========== First Run ==========

	UPROPERTY(Config)
	bool bFirstRunCompleted = false;

	// ========== Remote Image Import ==========

	UPROPERTY(Config, EditAnywhere, Category = "Remote Image Import", meta = (DisplayName = "Enable Remote Image Import"))
	bool bEnableRemoteImageImport = false;

	UPROPERTY(Config, EditAnywhere, Category = "Remote Image Import", meta = (DisplayName = "Allow Insecure or Local Image URLs", ToolTip = "Allows HTTP, local-style hostnames, and IP-literal URLs. DNS resolution remains controlled by the operating system; keep disabled for untrusted design files."))
	bool bAllowUnsafeRemoteImageUrls = false;

	UPROPERTY(Config, EditAnywhere, Category = "Remote Image Import", meta = (DisplayName = "Remote Image Import Path"))
	FString RemoteImageImportPath = TEXT("/Game/Design2UMG/ImportedImages");

	UPROPERTY(Config, EditAnywhere, Category = "Remote Image Import", meta = (DisplayName = "Remote Image Timeout (Seconds)", ToolTip = "Timeout budget per download request. Each remote image gets the full window; a large batch does not shrink the budget of later requests.", ClampMin = "1", ClampMax = "120"))
	float RemoteImageTimeoutSeconds = 20.0f;

	// ========== Pencil MCP Connection ==========

	UPROPERTY(Config, EditAnywhere, Category = "Pencil Connection", meta = (DisplayName = "MCP Server Path (Optional)", ToolTip = "Override path to mcp-server binary. Leave empty for auto-discovery."))
	FString MCPServerPath;

	UPROPERTY(Config, EditAnywhere, Category = "Pencil Connection", meta = (DisplayName = "App Name (Optional)", ToolTip = "Override -app parameter. Leave empty to auto-detect from ~/.pencil/apps and running IDE processes (for example visual_studio_code / antigravity)."))
	FString PencilAppName;

	UPROPERTY(Config, EditAnywhere, Category = "Pencil Connection", meta = (DisplayName = "Pencil Extension Directory (Optional)", ToolTip = "Override path to an installed Pencil VS Code extension directory (the highagency.pencildev-<version> folder). Used to load extension resources such as the icon codepoint index. Leave empty for auto-discovery."))
	FString PencilExtensionDirectory;

	UPROPERTY(Config, EditAnywhere, Category = "Pencil Connection", meta = (DisplayName = "Suppress Missing Pencil Extension Resource Warning", ToolTip = "Hide the notification shown when a pen design uses icons or library variables but no Pencil extension resources were found. Uncheck to see the notification again."))
	bool bSuppressPencilExtensionResourceWarning = false;

	// ========== Font Resolution ==========

	UPROPERTY(Config, EditAnywhere, Category = "Font Resolution", meta = (DisplayName = "Font Family → Font Asset", ToolTip = "Map design font family or PostScript name to an existing /Game UFont asset."))
	TMap<FString, TSoftObjectPtr<UFont>> ExplicitFontMappings;

	UPROPERTY(Config, EditAnywhere, Category = "Font Resolution", meta = (DisplayName = "Default Font Asset", ToolTip = "Project UFont reported as the diagnostic fallback candidate when no design font matches. Leave empty to report the engine default font."))
	TSoftObjectPtr<UFont> DefaultFontAsset;

	// ========== Icon Font ==========

	/** Icon font family -> UE font asset mapping (for example "Material Symbols Sharp" -> UFont asset). */
	UPROPERTY(Config, EditAnywhere, Category = "Icon Font", meta = (DisplayName = "Font Family → Font Asset", ToolTip = "Map icon font family names to UE5 font assets."))
	TMap<FString, TSoftObjectPtr<UFont>> IconFontAssetMap;

	/** Optional fallback font asset path when the family is not present in IconFontAssetMap. */
	UPROPERTY(Config, EditAnywhere, Category = "Icon Font", meta = (DisplayName = "Default Icon Font Asset Path", ToolTip = "Optional fallback font asset path for icon fonts when no explicit family mapping is found."))
	FString DefaultIconFontAssetPath;

	/** Placeholder text to use when no font asset is available. Leave empty to keep IconName. */
	UPROPERTY(Config, EditAnywhere, Category = "Icon Font", meta = (DisplayName = "Missing Icon Placeholder", ToolTip = "When icon font asset is missing, use this placeholder text instead of IconName. Leave empty to keep IconName."))
	FString MissingIconFontPlaceholder;

	/** Find the font asset path for an icon font family, or return an empty string when not found. */
	FString FindIconFontAssetPath(const FString& FontFamily) const;

	// ========== Placeholder Hatch ==========

	/** Enable custom placeholder hatch overrides. Disable to force canonical plugin defaults for reproducible output. */
	UPROPERTY(Config, EditAnywhere, Category = "Placeholder Hatch", meta = (DisplayName = "Use Custom Hatch Style", ToolTip = "When disabled, placeholder hatch visuals always use the plugin's canonical defaults instead of user overrides."))
	bool bUseCustomPlaceholderHatchStyle = false;

	/** Fallback hatch background color when the node does not provide a usable solid fill. */
	UPROPERTY(Config, EditAnywhere, Category = "Placeholder Hatch", meta = (DisplayName = "Fallback Background Color", ToolTip = "Fallback hatch background color when node has no explicit solid fill color."))
	FString PlaceholderHatchBackgroundColor = TEXT("#D282FF33");

	/** Default background color for expanded reference-instance placeholders. */
	UPROPERTY(Config, EditAnywhere, Category = "Placeholder Hatch", meta = (DisplayName = "Expanded Ref Background Color", ToolTip = "Fallback hatch background color for expanded reference placeholders when node has no explicit solid fill color."))
	FString PlaceholderRefHatchBackgroundColor = TEXT("#9682FF33");

	/** Fallback hatch stripe color when the node does not provide a usable stroke color. */
	UPROPERTY(Config, EditAnywhere, Category = "Placeholder Hatch", meta = (DisplayName = "Fallback Stripe Color", ToolTip = "Fallback hatch stripe color when node has no explicit stroke color."))
	FString PlaceholderHatchStripeColor = TEXT("#D480FF");

	/** Default stripe color for expanded reference-instance placeholders. */
	UPROPERTY(Config, EditAnywhere, Category = "Placeholder Hatch", meta = (DisplayName = "Expanded Ref Stripe Color", ToolTip = "Fallback hatch stripe color for expanded reference placeholders when node has no explicit stroke color."))
	FString PlaceholderRefHatchStripeColor = TEXT("#9580FF");

	/** Placeholder hatch stripe width in pixels. */
	UPROPERTY(Config, EditAnywhere, Category = "Placeholder Hatch", meta = (DisplayName = "Stripe Width", ClampMin = "1", ClampMax = "32"))
	int32 PlaceholderHatchStripeWidth = 1;

	/** Placeholder hatch stripe gap in pixels. */
	UPROPERTY(Config, EditAnywhere, Category = "Placeholder Hatch", meta = (DisplayName = "Stripe Gap", ClampMin = "1", ClampMax = "64"))
	int32 PlaceholderHatchStripeGap = 4;

	/** Enable custom opacity-cap overrides for the FIT + LUMINOSITY runtime approximation. */
	UPROPERTY(Config, EditAnywhere, Category = "Image Blend Approximation", meta = (DisplayName = "Use Custom FIT Luminosity Opacity Cap", ToolTip = "When disabled, runtime approximation uses the plugin's canonical opacity cap instead of user overrides."))
	bool bUseCustomLuminosityFitOpacityCap = false;

	/** Maximum overlay opacity allowed by the FIT + LUMINOSITY runtime approximation. */
	UPROPERTY(Config, EditAnywhere, Category = "Image Blend Approximation", meta = (DisplayName = "FIT Luminosity Opacity Cap", ClampMin = "0.0", ClampMax = "1.0", ToolTip = "Upper bound used when approximating FIT + LUMINOSITY image overlays without a baked texture."))
	float LuminosityFitOpacityCap = 0.15f;

	// ========== Prototype Navigation ==========

	// Default is Disabled: RuntimeExtension bakes editor-module classes into generated
	// widgets, which cook strips from packaged builds. Enable it only for editor preview.
	UPROPERTY(Config, EditAnywhere, Category = "Prototype Navigation", meta = (DisplayName = "Generation Mode", ToolTip = "Disabled emits no prototype navigation binding. TagHelper writes stable helper metadata for an optional setup call. RuntimeExtension bakes editor-module navigation classes into the widget blueprint for editor preview only; packaged games cannot load them, so packaged widgets lose prototype navigation."))
	EDesign2UMGPrototypeNavigationGenerationMode PrototypeNavigationGenerationMode =
		EDesign2UMGPrototypeNavigationGenerationMode::Disabled;

	// ========== Widget Class Overrides ==========

	UPROPERTY(Config, EditAnywhere, Category = "Widget Class Overrides", meta = (DisplayName = "Class Override Rules", ToolTip = "When a node name contains the pattern, use the specified Widget class instead of the default."))
	TArray<FWidgetClassOverride> ClassOverrides;

	/** Resolve the class override for a node name, or return nullptr when no rule matches. */
	UClass* FindClassOverride(const FString& NodeName) const;

	/**
	 * Single shared class-override matcher used by settings and the generation settings snapshot.
	 * The optional cache memoizes node name -> resolved class (nullptr included) for the duration
	 * of one widget-tree build so rule loading runs once per tree instead of once per node.
	 */
	static UClass* ResolveClassOverride(
		const TArray<FWidgetClassOverride>& Rules,
		const FString& NodeName,
		TMap<FString, UClass*>* InOutNodeNameCache = nullptr);
};

