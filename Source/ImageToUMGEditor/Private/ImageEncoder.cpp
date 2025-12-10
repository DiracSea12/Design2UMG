// Copyright Epic Games, Inc. All Rights Reserved.

#include "ImageEncoder.h"
#include "ImageToUMGLog.h"
#include "Engine/Texture2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorFramework/AssetImportData.h"

// ============================================================================
// RAII 锁管理器
// ============================================================================

struct FBulkDataReadLock
{
	FByteBulkData& BulkData;
	const void* Data;

	explicit FBulkDataReadLock(FByteBulkData& InBulkData)
		: BulkData(InBulkData)
		, Data(InBulkData.LockReadOnly())
	{
	}

	~FBulkDataReadLock()
	{
		BulkData.Unlock();
	}

	const void* Get() const { return Data; }
	bool IsValid() const { return Data != nullptr; }

	// 禁止拷贝
	FBulkDataReadLock(const FBulkDataReadLock&) = delete;
	FBulkDataReadLock& operator=(const FBulkDataReadLock&) = delete;
};

// ============================================================================
// 像素格式转换辅助函数
// ============================================================================

static void ConvertBGRAToRGBA(const uint8* Src, uint8* Dst, int32 PixelCount)
{
	for (int32 i = 0; i < PixelCount; ++i)
	{
		Dst[i * 4 + 0] = Src[i * 4 + 2]; // R
		Dst[i * 4 + 1] = Src[i * 4 + 1]; // G
		Dst[i * 4 + 2] = Src[i * 4 + 0]; // B
		Dst[i * 4 + 3] = Src[i * 4 + 3]; // A
	}
}

static void ConvertGrayToRGBA(const uint8* Src, uint8* Dst, int32 PixelCount)
{
	for (int32 i = 0; i < PixelCount; ++i)
	{
		uint8 Gray = Src[i];
		Dst[i * 4 + 0] = Gray;
		Dst[i * 4 + 1] = Gray;
		Dst[i * 4 + 2] = Gray;
		Dst[i * 4 + 3] = 255;
	}
}

// Reinhard tone mapping + 转换为 uint8
FORCEINLINE void ToneMapAndWrite(float R, float G, float B, float A, uint8* Dst)
{
	R = R / (1.0f + R);
	G = G / (1.0f + G);
	B = B / (1.0f + B);
	Dst[0] = FMath::Clamp(static_cast<int32>(R * 255.0f), 0, 255);
	Dst[1] = FMath::Clamp(static_cast<int32>(G * 255.0f), 0, 255);
	Dst[2] = FMath::Clamp(static_cast<int32>(B * 255.0f), 0, 255);
	Dst[3] = FMath::Clamp(static_cast<int32>(A * 255.0f), 0, 255);
}

static void ConvertHDRToRGBA(const float* Src, uint8* Dst, int32 PixelCount)
{
	for (int32 i = 0; i < PixelCount; ++i)
	{
		ToneMapAndWrite(Src[i * 4], Src[i * 4 + 1], Src[i * 4 + 2], Src[i * 4 + 3], &Dst[i * 4]);
	}
}

static void ConvertFloat16ToRGBA(const FFloat16* Src, uint8* Dst, int32 PixelCount)
{
	for (int32 i = 0; i < PixelCount; ++i)
	{
		ToneMapAndWrite(
			Src[i * 4].GetFloat(), Src[i * 4 + 1].GetFloat(),
			Src[i * 4 + 2].GetFloat(), Src[i * 4 + 3].GetFloat(),
			&Dst[i * 4]
		);
	}
}

static void Convert16BitToRGBA(const uint16* Src, uint8* Dst, int32 PixelCount)
{
	for (int32 i = 0; i < PixelCount; ++i)
	{
		Dst[i * 4 + 0] = Src[i * 4 + 0] >> 8;
		Dst[i * 4 + 1] = Src[i * 4 + 1] >> 8;
		Dst[i * 4 + 2] = Src[i * 4 + 2] >> 8;
		Dst[i * 4 + 3] = Src[i * 4 + 3] >> 8;
	}
}

// ============================================================================
// 主要实现
// ============================================================================

bool FImageEncoder::GetImageDataFromTexture(UTexture2D* Texture, TArray<uint8>& OutImageData, FString& OutFormat, int32& OutWidth, int32& OutHeight)
{
	if (!Texture)
	{
		UE_LOG(LogImageToUMG, Error, TEXT("ImageEncoder: Texture is null"));
		return false;
	}

	OutWidth = Texture->GetSizeX();
	OutHeight = Texture->GetSizeY();

	// 图片尺寸限制（防止内存溢出，4096x4096 = 16M 像素）
	constexpr int32 MaxDim = 4096;
	if (OutWidth > MaxDim || OutHeight > MaxDim)
	{
		UE_LOG(LogImageToUMG, Error, TEXT("ImageEncoder: Image too large (%dx%d), max %d"), OutWidth, OutHeight, MaxDim);
		return false;
	}

	// 优先使用原始文件
	if (Texture->AssetImportData)
	{
		FString SourcePath = Texture->AssetImportData->GetFirstFilename();
		if (!SourcePath.IsEmpty() && FPaths::FileExists(SourcePath))
		{
			if (GetImageDataFromFile(SourcePath, OutImageData, OutFormat))
			{
				// 从文件数据中解析实际尺寸（关键修复！）
				IImageWrapperModule& Mod = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
				EImageFormat Fmt = Mod.DetectImageFormat(OutImageData.GetData(), OutImageData.Num());
				TSharedPtr<IImageWrapper> Wrap = Mod.CreateImageWrapper(Fmt);
				if (Wrap.IsValid() && Wrap->SetCompressed(OutImageData.GetData(), OutImageData.Num()))
				{
					OutWidth = Wrap->GetWidth();
					OutHeight = Wrap->GetHeight();
					UE_LOG(LogImageToUMG, Log, TEXT("ImageEncoder: 从原始文件读取尺寸: %dx%d (纹理报告: %dx%d)"),
						OutWidth, OutHeight, Texture->GetSizeX(), Texture->GetSizeY());
				}
				return true;
			}
		}
	}

	// 从纹理数据导出
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

	FTexturePlatformData* PlatformData = Texture->GetPlatformData();
	if (!PlatformData || PlatformData->Mips.Num() == 0)
	{
		UE_LOG(LogImageToUMG, Error, TEXT("ImageEncoder: No platform data available"));
		return false;
	}

	FTexture2DMipMap& Mip = PlatformData->Mips[0];
	EPixelFormat PixelFormat = PlatformData->PixelFormat;
	int32 Width = Mip.SizeX;
	int32 Height = Mip.SizeY;
	int32 PixelCount = Width * Height;

	// 压缩格式需要解压
	if (IsCompressedFormat(PixelFormat))
	{
		TArray<uint8> DecompressedData;
		if (DecompressTexture(Texture, DecompressedData, Width, Height))
		{
			if (ImageWrapper->SetRaw(DecompressedData.GetData(), DecompressedData.Num(), Width, Height, ERGBFormat::RGBA, 8))
			{
				OutImageData = ImageWrapper->GetCompressed(100);
				OutFormat = TEXT("png");
				OutWidth = Width;
				OutHeight = Height;
				return OutImageData.Num() > 0;
			}
		}
		UE_LOG(LogImageToUMG, Warning, TEXT("ImageEncoder: Failed to decompress texture, format: %d"), (int32)PixelFormat);
		return false;
	}

	// 使用 RAII 锁定纹理数据
	FBulkDataReadLock DataLock(Mip.BulkData);
	if (!DataLock.IsValid())
	{
		UE_LOG(LogImageToUMG, Error, TEXT("ImageEncoder: Failed to lock texture data"));
		return false;
	}

	const void* Data = DataLock.Get();

	// 转换像素格式
	TArray<uint8> RawData;
	RawData.SetNumUninitialized(PixelCount * 4);
	bool bSuccess = false;

	switch (PixelFormat)
	{
	case PF_B8G8R8A8:
		ConvertBGRAToRGBA(static_cast<const uint8*>(Data), RawData.GetData(), PixelCount);
		bSuccess = true;
		break;
	case PF_R8G8B8A8:
		FMemory::Memcpy(RawData.GetData(), Data, PixelCount * 4);
		bSuccess = true;
		break;
	case PF_G8:
		ConvertGrayToRGBA(static_cast<const uint8*>(Data), RawData.GetData(), PixelCount);
		bSuccess = true;
		break;
	case PF_FloatRGBA:
	case PF_A32B32G32R32F:
		ConvertHDRToRGBA(static_cast<const float*>(Data), RawData.GetData(), PixelCount);
		bSuccess = true;
		break;
	case PF_R16G16B16A16_UNORM:
	case PF_R16G16B16A16_SNORM:
		Convert16BitToRGBA(static_cast<const uint16*>(Data), RawData.GetData(), PixelCount);
		bSuccess = true;
		break;
	default:
		break;
	}

	if (!bSuccess)
	{
		UE_LOG(LogImageToUMG, Error, TEXT("ImageEncoder: Unsupported pixel format: %d"), (int32)PixelFormat);
		return false;
	}

	// 编码为 PNG
	if (ImageWrapper->SetRaw(RawData.GetData(), RawData.Num(), Width, Height, ERGBFormat::RGBA, 8))
	{
		OutImageData = ImageWrapper->GetCompressed(100);
		OutFormat = TEXT("png");
		OutWidth = Width;
		OutHeight = Height;
		return OutImageData.Num() > 0;
	}

	UE_LOG(LogImageToUMG, Error, TEXT("ImageEncoder: Failed to encode image"));
	return false;
}

bool FImageEncoder::IsCompressedFormat(EPixelFormat Format)
{
	switch (Format)
	{
	case PF_DXT1:
	case PF_DXT3:
	case PF_DXT5:
	case PF_BC4:
	case PF_BC5:
	case PF_BC6H:
	case PF_BC7:
	case PF_ASTC_4x4:
	case PF_ASTC_6x6:
	case PF_ASTC_8x8:
	case PF_ASTC_10x10:
	case PF_ASTC_12x12:
		return true;
	default:
		return false;
	}
}

bool FImageEncoder::DecompressTexture(UTexture2D* Texture, TArray<uint8>& OutRGBAData, int32& OutWidth, int32& OutHeight)
{
	if (!Texture) return false;

	FTextureSource& Source = Texture->Source;
	if (!Source.IsValid())
	{
		UE_LOG(LogImageToUMG, Warning, TEXT("ImageEncoder: Texture source is not valid"));
		return false;
	}

	OutWidth = Source.GetSizeX();
	OutHeight = Source.GetSizeY();
	int32 PixelCount = OutWidth * OutHeight;

	TArray64<uint8> SourceData;
	if (!Source.GetMipData(SourceData, 0))
	{
		UE_LOG(LogImageToUMG, Warning, TEXT("ImageEncoder: Failed to get source mip data"));
		return false;
	}

	ETextureSourceFormat SourceFormat = Source.GetFormat();
	OutRGBAData.SetNumUninitialized(PixelCount * 4);

	if (SourceFormat == TSF_BGRA8)
	{
		ConvertBGRAToRGBA(SourceData.GetData(), OutRGBAData.GetData(), PixelCount);
		return true;
	}
	else if (SourceFormat == TSF_RGBA8_DEPRECATED)
	{
		FMemory::Memcpy(OutRGBAData.GetData(), SourceData.GetData(), PixelCount * 4);
		return true;
	}
	else if (SourceFormat == TSF_G8)
	{
		ConvertGrayToRGBA(SourceData.GetData(), OutRGBAData.GetData(), PixelCount);
		return true;
	}
	else if (SourceFormat == TSF_RGBA16F)
	{
		ConvertFloat16ToRGBA(reinterpret_cast<const FFloat16*>(SourceData.GetData()), OutRGBAData.GetData(), PixelCount);
		return true;
	}
	else if (SourceFormat == TSF_RGBA16)
	{
		Convert16BitToRGBA(reinterpret_cast<const uint16*>(SourceData.GetData()), OutRGBAData.GetData(), PixelCount);
		return true;
	}

	UE_LOG(LogImageToUMG, Warning, TEXT("ImageEncoder: Unsupported source format: %d"), (int32)SourceFormat);
	return false;
}

bool FImageEncoder::DecompressImageData(const TArray<uint8>& CompressedData, TArray<uint8>& OutRawData, int32& OutWidth, int32& OutHeight)
{
	IImageWrapperModule& Mod = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	EImageFormat Fmt = Mod.DetectImageFormat(CompressedData.GetData(), CompressedData.Num());
	TSharedPtr<IImageWrapper> Wrap = Mod.CreateImageWrapper(Fmt);

	if (!Wrap.IsValid() || !Wrap->SetCompressed(CompressedData.GetData(), CompressedData.Num()))
	{
		return false;
	}

	TArray64<uint8> RawData64;
	if (!Wrap->GetRaw(ERGBFormat::BGRA, 8, RawData64))
	{
		return false;
	}

	OutWidth = Wrap->GetWidth();
	OutHeight = Wrap->GetHeight();

	// 转换 TArray64 到 TArray
	OutRawData.SetNumUninitialized(RawData64.Num());
	FMemory::Memcpy(OutRawData.GetData(), RawData64.GetData(), RawData64.Num());
	return true;
}

bool FImageEncoder::GetImageDataFromFile(const FString& FilePath, TArray<uint8>& OutImageData, FString& OutFormat)
{
	if (!FPaths::FileExists(FilePath))
	{
		UE_LOG(LogImageToUMG, Error, TEXT("ImageEncoder: File does not exist: %s"), *FilePath);
		return false;
	}

	if (!FFileHelper::LoadFileToArray(OutImageData, *FilePath))
	{
		UE_LOG(LogImageToUMG, Error, TEXT("ImageEncoder: Failed to read file: %s"), *FilePath);
		return false;
	}

	FString Extension = FPaths::GetExtension(FilePath).ToLower();
	if (Extension == TEXT("png"))
	{
		OutFormat = TEXT("png");
	}
	else if (Extension == TEXT("jpg") || Extension == TEXT("jpeg"))
	{
		OutFormat = TEXT("jpeg");
	}
	else if (Extension == TEXT("bmp"))
	{
		// BMP 转换为 PNG
		IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		TSharedPtr<IImageWrapper> BmpWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::BMP);

		if (BmpWrapper->SetCompressed(OutImageData.GetData(), OutImageData.Num()))
		{
			TArray64<uint8> RawData;
			if (BmpWrapper->GetRaw(ERGBFormat::RGBA, 8, RawData))
			{
				TSharedPtr<IImageWrapper> PngWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
				if (PngWrapper->SetRaw(RawData.GetData(), RawData.Num(), BmpWrapper->GetWidth(), BmpWrapper->GetHeight(), ERGBFormat::RGBA, 8))
				{
					OutImageData = PngWrapper->GetCompressed(100);
					OutFormat = TEXT("png");
					return true;
				}
			}
		}
		return false;
	}
	else
	{
		OutFormat = TEXT("png");
	}

	return true;
}
