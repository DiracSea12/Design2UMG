// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PixelFormat.h"

class UTexture2D;

/**
 * 图片编码工具类
 */
class FImageEncoder
{
public:
	/**
	 * 从 Texture2D 获取原始图片数据
	 * @param Texture 输入纹理
	 * @param OutImageData 输出图片数据 (PNG 格式)
	 * @param OutFormat 输出格式字符串 ("png" 或 "jpeg")
	 * @param OutWidth 输出图片宽度
	 * @param OutHeight 输出图片高度
	 * @return 是否成功
	 */
	static bool GetImageDataFromTexture(UTexture2D* Texture, TArray<uint8>& OutImageData, FString& OutFormat, int32& OutWidth, int32& OutHeight);

	/**
	 * 从文件路径读取图片数据
	 * @param FilePath 图片文件路径
	 * @param OutImageData 输出图片数据
	 * @param OutFormat 输出格式字符串
	 * @return 是否成功
	 */
	static bool GetImageDataFromFile(const FString& FilePath, TArray<uint8>& OutImageData, FString& OutFormat);

	/**
	 * 解压 PNG/JPEG 图像数据为 BGRA 原始像素
	 * @param CompressedData 压缩的图像数据
	 * @param OutRawData 输出的 BGRA 原始数据
	 * @param OutWidth 输出宽度
	 * @param OutHeight 输出高度
	 * @return 是否成功
	 */
	static bool DecompressImageData(const TArray<uint8>& CompressedData, TArray<uint8>& OutRawData, int32& OutWidth, int32& OutHeight);

private:
	/**
	 * 检查是否是压缩格式
	 */
	static bool IsCompressedFormat(EPixelFormat Format);

	/**
	 * 解压缩纹理数据
	 */
	static bool DecompressTexture(UTexture2D* Texture, TArray<uint8>& OutRGBAData, int32& OutWidth, int32& OutHeight);
};
