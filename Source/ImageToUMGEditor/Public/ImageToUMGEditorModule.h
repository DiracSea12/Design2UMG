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

	/** 打开预览窗口 */
	void OpenPreviewWindow(const FString& Json, UTexture2D* Tex, const FString& Path, const FString& Name,
		const TArray<uint8>& ImageData, FIntPoint ImageSize, FIntPoint AIImageSize);

private:
	/** 从文件选择对话框生成（Tools菜单） */
	void OnGenerateFromFile();

	/** 预览确认后生成Widget Blueprint */
	void OnPreviewConfirmed(const FString& Json, const FString& OutputPath, const TArray<uint8>& SourceImageData, FIntPoint SourceImageSize, float CoordScale);

	TSharedPtr<class FUICommandList> PluginCommands;
};
