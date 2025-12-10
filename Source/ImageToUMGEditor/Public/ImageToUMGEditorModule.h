// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class UTexture2D;

/**
 * ImageToUMG 编辑器模块
 * 通过AI分析图片生成Widget Blueprint
 */
class FImageToUMGEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** 打开预览窗口（带 AI 返回的 JSON） */
	void OpenPreviewWindow(const FString& Json, UTexture2D* Tex, const FString& Path, const FString& Name,
		const TArray<uint8>& ImageData, FIntPoint ImageSize, FIntPoint AIImageSize);

	/** 直接打开预览窗口（不调用 AI，用户可手动编辑或点击按钮调用 AI） */
	void OpenPreviewWindowDirect(UTexture2D* Tex, const FString& Path, const FString& Name,
		const TArray<uint8>& ImageData, FIntPoint ImageSize);

	/** 处理 AI 分析请求（从预览窗口调用） */
	void OnRequestAIAnalysis(const TArray<uint8>& ImageData, FIntPoint ImageSize,
		TFunction<void(const FString&)> OnSuccess, TFunction<void(const FString&)> OnError);

private:
	/** 从文件选择对话框生成（Tools菜单） */
	void OnGenerateFromFile();

	/** 预览确认后生成Widget Blueprint */
	void OnPreviewConfirmed(const FString& Json, const FString& OutputPath, const TArray<uint8>& SourceImageData, FIntPoint SourceImageSize, FVector2D CoordScale);

	TSharedPtr<class FUICommandList> PluginCommands;

	/** 持有从文件创建的临时纹理，防止GC */
	TStrongObjectPtr<UTexture2D> TempTextureHolder;
};
