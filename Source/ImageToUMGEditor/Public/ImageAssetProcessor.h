// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UMGSchema.h"

class UTexture2D;

/**
 * 图像资产处理结果
 */
struct FProcessedImageAsset
{
	/** 资产 ID（对应 FUMGImageAsset::Id） */
	FString Id;

	/** 生成的纹理资产路径 */
	FString TexturePath;

	/** 生成的纹理对象 */
	UTexture2D* Texture = nullptr;
};

/**
 * 图像资产处理器 - 从截图裁剪图像资产
 */
class FImageAssetProcessor
{
public:
	/**
	 * 处理图像资产
	 * @param ImageAssets 需要处理的图像资产列表
	 * @param SourceImageData 原始截图数据（PNG/JPEG）
	 * @param OutputPath 输出路径（UE 资产路径，如 /Game/UI/Textures）
	 * @param OutResults 处理结果
	 * @param CoordScale 坐标缩放因子（AI坐标 * CoordScale = 原始坐标，默认1.0无缩放）
	 * @return 成功处理的资产数量
	 */
	static int32 ProcessImageAssets(
		const TArray<FUMGImageAsset>& ImageAssets,
		const TArray<uint8>& SourceImageData,
		const FString& OutputPath,
		TArray<FProcessedImageAsset>& OutResults,
		float CoordScale = 1.0f
	);

	/**
	 * 为预览创建临时纹理（不保存到磁盘）
	 * @param ImageAssets 需要处理的图像资产列表
	 * @param SourceImageData 原始截图数据（PNG/JPEG）
	 * @param OutResults 处理结果（TexturePath 为空，Texture 为临时对象）
	 * @param OutTextures 输出的纹理强引用（调用者需持有以防GC）
	 * @param CoordScale 坐标缩放因子（AI坐标 * CoordScale = 原始坐标，默认1.0无缩放）
	 * @return 成功处理的资产数量
	 */
	static int32 ProcessImageAssetsForPreview(
		const TArray<FUMGImageAsset>& ImageAssets,
		const TArray<uint8>& SourceImageData,
		TArray<FProcessedImageAsset>& OutResults,
		TArray<TStrongObjectPtr<UTexture2D>>& OutTextures,
		float CoordScale = 1.0f
	);

	/**
	 * 缩放图像数据（用于 AI API 发送前压缩大图）
	 * @param SourceData 原始图像数据（PNG/JPEG）
	 * @param SourceWidth 原始宽度
	 * @param SourceHeight 原始高度
	 * @param TargetWidth 目标宽度
	 * @param TargetHeight 目标高度
	 * @param OutScaledData 输出的缩放后图像数据（PNG）
	 * @return 是否成功
	 */
	static bool ScaleImageData(
		const TArray<uint8>& SourceData,
		int32 SourceWidth,
		int32 SourceHeight,
		int32 TargetWidth,
		int32 TargetHeight,
		TArray<uint8>& OutScaledData
	);

private:
	/**
	 * 从原始图像裁剪并缩放指定区域
	 * @param SourceData 原始图像数据（PNG/JPEG）
	 * @param CropX 裁剪起点 X
	 * @param CropY 裁剪起点 Y
	 * @param CropWidth 裁剪宽度
	 * @param CropHeight 裁剪高度
	 * @param TargetWidth 目标宽度（0 表示使用裁剪宽度）
	 * @param TargetHeight 目标高度（0 表示使用裁剪高度）
	 * @param OutCroppedData 输出的裁剪后图像数据（PNG）
	 * @return 是否成功
	 */
	static bool CropAndResizeImage(
		const TArray<uint8>& SourceData,
		int32 CropX,
		int32 CropY,
		int32 CropWidth,
		int32 CropHeight,
		int32 TargetWidth,
		int32 TargetHeight,
		TArray<uint8>& OutCroppedData
	);

	/**
	 * 导入图像数据为 UTexture2D 资产
	 * @param ImageData PNG 图像数据
	 * @param AssetPath 资产路径
	 * @param AssetName 资产名称
	 * @return 创建的纹理对象（失败返回 nullptr）
	 */
	static UTexture2D* ImportAsTexture(
		const TArray<uint8>& ImageData,
		const FString& AssetPath,
		const FString& AssetName
	);
};
