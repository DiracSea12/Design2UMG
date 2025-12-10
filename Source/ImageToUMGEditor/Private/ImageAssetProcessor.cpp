// Copyright Epic Games, Inc. All Rights Reserved.

#include "ImageAssetProcessor.h"
#include "ImageToUMGLog.h"
#include "ImageToUMGConstants.h"
#include "ImageEncoder.h"

#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Engine/Texture2D.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "EditorFramework/AssetImportData.h"

// ============================================================================
// 辅助函数
// ============================================================================

namespace
{
	// 裁剪参数结构
	struct FCropParams
	{
		int32 CropX, CropY, CropW, CropH;
		int32 TargetWidth, TargetHeight;
	};

	// 计算裁剪参数（消除重复代码，支持X/Y分别缩放）
	FCropParams CalculateCropParams(
		const FUMGImageAsset& Asset,
		int32 SourceWidth, int32 SourceHeight,
		FVector2D CoordScale)
	{
		FCropParams P;
		if (Asset.X < 0 || Asset.Y < 0 || Asset.Width <= 0 || Asset.Height <= 0)
		{
			// 使用整个源图
			P.CropX = 0;
			P.CropY = 0;
			P.CropW = SourceWidth;
			P.CropH = SourceHeight;
			P.TargetWidth = (Asset.SuggestedWidth > 0) ? Asset.SuggestedWidth : SourceWidth;
			P.TargetHeight = (Asset.SuggestedHeight > 0) ? Asset.SuggestedHeight : SourceHeight;
		}
		else
		{
			// 坐标缩放：AI返回的坐标基于缩放后图像，需要转换到原始图像坐标（X/Y分别缩放）
			P.CropX = FMath::RoundToInt(Asset.X * CoordScale.X);
			P.CropY = FMath::RoundToInt(Asset.Y * CoordScale.Y);
			P.CropW = FMath::RoundToInt(Asset.Width * CoordScale.X);
			P.CropH = FMath::RoundToInt(Asset.Height * CoordScale.Y);
			P.TargetWidth = (Asset.SuggestedWidth > 0) ? Asset.SuggestedWidth : P.CropW;
			P.TargetHeight = (Asset.SuggestedHeight > 0) ? Asset.SuggestedHeight : P.CropH;
		}
		return P;
	}

	// 双线性采样单个像素
	FORCEINLINE void BilinearSample(
		const uint8* Src, int32 SrcWidth, int32 SrcHeight,
		float SrcX, float SrcY, uint8* Dst)
	{
		int32 X0 = FMath::Clamp(FMath::FloorToInt(SrcX), 0, SrcWidth - 1);
		int32 Y0 = FMath::Clamp(FMath::FloorToInt(SrcY), 0, SrcHeight - 1);
		int32 X1 = FMath::Clamp(X0 + 1, 0, SrcWidth - 1);
		int32 Y1 = FMath::Clamp(Y0 + 1, 0, SrcHeight - 1);

		float Fx = FMath::Clamp(SrcX - X0, 0.0f, 1.0f);
		float Fy = FMath::Clamp(SrcY - Y0, 0.0f, 1.0f);

		const uint8* P00 = Src + (Y0 * SrcWidth + X0) * 4;
		const uint8* P10 = Src + (Y0 * SrcWidth + X1) * 4;
		const uint8* P01 = Src + (Y1 * SrcWidth + X0) * 4;
		const uint8* P11 = Src + (Y1 * SrcWidth + X1) * 4;

		for (int32 C = 0; C < 4; ++C)
		{
			float V = P00[C] * (1 - Fx) * (1 - Fy) + P10[C] * Fx * (1 - Fy) +
			          P01[C] * (1 - Fx) * Fy + P11[C] * Fx * Fy;
			Dst[C] = FMath::Clamp(FMath::RoundToInt(V), 0, 255);
		}
	}

	// 双线性插值缩放图像（并行优化）
	void ResizeImageBilinear(
		const uint8* Src, int32 SrcW, int32 SrcH,
		uint8* Dst, int32 DstW, int32 DstH)
	{
		// 小图像使用串行处理，大图像使用并行处理
		if (DstH < ImageToUMGConstants::ParallelProcessingThreshold)
		{
			// 串行处理小图像
			for (int32 Y = 0; Y < DstH; ++Y)
			{
				float SrcY = FMath::Max(0.0f, (Y + 0.5f) * SrcH / DstH - 0.5f);
				for (int32 X = 0; X < DstW; ++X)
				{
					float SrcX = FMath::Max(0.0f, (X + 0.5f) * SrcW / DstW - 0.5f);
					BilinearSample(Src, SrcW, SrcH, SrcX, SrcY, Dst + (Y * DstW + X) * 4);
				}
			}
		}
		else
		{
			// 并行处理大图像（按行并行）
			ParallelFor(DstH, [Src, SrcW, SrcH, Dst, DstW, DstH](int32 Y)
			{
				float SrcY = FMath::Max(0.0f, (Y + 0.5f) * SrcH / DstH - 0.5f);
				for (int32 X = 0; X < DstW; ++X)
				{
					float SrcX = FMath::Max(0.0f, (X + 0.5f) * SrcW / DstW - 0.5f);
					BilinearSample(Src, SrcW, SrcH, SrcX, SrcY, Dst + (Y * DstW + X) * 4);
				}
			});
		}
	}

	// 配置 UI 纹理属性
	void ConfigureUITexture(UTexture2D* Texture)
	{
		Texture->SRGB = true;
		Texture->CompressionSettings = TC_EditorIcon;
		Texture->MipGenSettings = TMGS_NoMipmaps;
		Texture->LODGroup = TEXTUREGROUP_UI;
		Texture->Filter = TF_Bilinear;
		Texture->NeverStream = true;
	}
}

int32 FImageAssetProcessor::ProcessImageAssets(
	const TArray<FUMGImageAsset>& ImageAssets,
	const TArray<uint8>& SourceImageData,
	const FString& OutputPath,
	TArray<FProcessedImageAsset>& OutResults,
	FVector2D CoordScale)
{
	if (ImageAssets.Num() == 0 || SourceImageData.Num() == 0)
	{
		return 0;
	}

	// 解压源图一次，后续复用
	TArray<uint8> SourceRawData;
	int32 SourceWidth, SourceHeight;
	if (!FImageEncoder::DecompressImageData(SourceImageData, SourceRawData, SourceWidth, SourceHeight))
	{
		UE_LOG(LogImageToUMG, Error, TEXT("ProcessImageAssets: Failed to decompress source image"));
		return 0;
	}

	int32 SuccessCount = 0;

	for (const FUMGImageAsset& Asset : ImageAssets)
	{
		FCropParams P = CalculateCropParams(Asset, SourceWidth, SourceHeight, CoordScale);

		TArray<uint8> CroppedData;
		if (!CropAndResizeImageFromRaw(SourceRawData, SourceWidth, SourceHeight,
			P.CropX, P.CropY, P.CropW, P.CropH, P.TargetWidth, P.TargetHeight, CroppedData))
		{
			UE_LOG(LogImageToUMG, Warning, TEXT("Failed to crop image for asset '%s'"), *Asset.Id);
			continue;
		}

		// 生成安全的资产名称
		FString SafeName = SlugStringForValidName(Asset.Id);
		if (SafeName.IsEmpty()) SafeName = TEXT("Asset");

		// 检查资产冲突
		FString AssetName = FString::Printf(TEXT("T_%s"), *SafeName);
		FString FullPath = OutputPath / AssetName;
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

		FString TestPath = FullPath + TEXT(".") + AssetName;
		if (ARM.Get().GetAssetByObjectPath(FSoftObjectPath(TestPath)).IsValid())
		{
			int32 Suffix = 1;
			while (ARM.Get().GetAssetByObjectPath(FSoftObjectPath(OutputPath / FString::Printf(TEXT("T_%s_%d.T_%s_%d"), *SafeName, Suffix, *SafeName, Suffix))).IsValid() && Suffix < ImageToUMGConstants::MaxAssetNameSuffix)
			{
				Suffix++;
			}
			AssetName = FString::Printf(TEXT("T_%s_%d"), *SafeName, Suffix);
		}

		UTexture2D* Texture = ImportAsTexture(CroppedData, OutputPath, AssetName);
		if (Texture)
		{
			FProcessedImageAsset Result;
			Result.Id = Asset.Id;
			Result.Texture = Texture;
			Result.TexturePath = Texture->GetPathName();
			OutResults.Add(Result);
			SuccessCount++;

			UE_LOG(LogImageToUMG, Log, TEXT("Created texture '%s'"), *Result.TexturePath);
		}
	}

	return SuccessCount;
}

bool FImageAssetProcessor::CropAndResizeImageFromRaw(
	const TArray<uint8>& RawData,
	int32 SourceWidth,
	int32 SourceHeight,
	int32 CropX,
	int32 CropY,
	int32 CropWidth,
	int32 CropHeight,
	int32 TargetWidth,
	int32 TargetHeight,
	TArray<uint8>& OutCroppedData)
{
	// 边界检查和调整
	CropX = FMath::Clamp(CropX, 0, SourceWidth - 1);
	CropY = FMath::Clamp(CropY, 0, SourceHeight - 1);
	CropWidth = FMath::Clamp(CropWidth, 1, SourceWidth - CropX);
	CropHeight = FMath::Clamp(CropHeight, 1, SourceHeight - CropY);

	// 裁剪像素数据
	TArray<uint8> CroppedRaw;
	CroppedRaw.SetNumUninitialized(CropWidth * CropHeight * 4);

	const int32 SrcStride = SourceWidth * 4;
	const int32 CropStride = CropWidth * 4;

	for (int32 Y = 0; Y < CropHeight; ++Y)
	{
		const uint8* SrcRow = RawData.GetData() + (CropY + Y) * SrcStride + CropX * 4;
		uint8* DstRow = CroppedRaw.GetData() + Y * CropStride;
		FMemory::Memcpy(DstRow, SrcRow, CropStride);
	}

	// 如果需要缩放
	TArray<uint8>* FinalData = &CroppedRaw;
	TArray<uint8> ResizedRaw;
	int32 FinalWidth = CropWidth;
	int32 FinalHeight = CropHeight;

	if (TargetWidth != CropWidth || TargetHeight != CropHeight)
	{
		ResizedRaw.SetNumUninitialized(TargetWidth * TargetHeight * 4);
		ResizeImageBilinear(CroppedRaw.GetData(), CropWidth, CropHeight,
			ResizedRaw.GetData(), TargetWidth, TargetHeight);
		FinalData = &ResizedRaw;
		FinalWidth = TargetWidth;
		FinalHeight = TargetHeight;
	}

	// 压缩为 PNG
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> OutputWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
	if (!OutputWrapper.IsValid())
	{
		return false;
	}

	if (!OutputWrapper->SetRaw(FinalData->GetData(), FinalData->Num(), FinalWidth, FinalHeight, ERGBFormat::BGRA, 8))
	{
		return false;
	}

	OutCroppedData = OutputWrapper->GetCompressed();
	return OutCroppedData.Num() > 0;
}

bool FImageAssetProcessor::CropAndResizeImage(
	const TArray<uint8>& SourceData,
	int32 CropX,
	int32 CropY,
	int32 CropWidth,
	int32 CropHeight,
	int32 TargetWidth,
	int32 TargetHeight,
	TArray<uint8>& OutCroppedData)
{
	TArray<uint8> RawData;
	int32 SourceWidth, SourceHeight;
	if (!FImageEncoder::DecompressImageData(SourceData, RawData, SourceWidth, SourceHeight))
	{
		UE_LOG(LogImageToUMG, Error, TEXT("Failed to decompress source image"));
		return false;
	}

	return CropAndResizeImageFromRaw(RawData, SourceWidth, SourceHeight,
		CropX, CropY, CropWidth, CropHeight, TargetWidth, TargetHeight, OutCroppedData);
}

UTexture2D* FImageAssetProcessor::ImportAsTexture(
	const TArray<uint8>& ImageData,
	const FString& AssetPath,
	const FString& AssetName)
{
	if (ImageData.Num() == 0)
	{
		return nullptr;
	}

	TArray<uint8> RawData;
	int32 Width, Height;
	if (!FImageEncoder::DecompressImageData(ImageData, RawData, Width, Height))
	{
		return nullptr;
	}

	// 创建包
	FString FullPath = AssetPath / AssetName;
	UPackage* Package = CreatePackage(*FullPath);
	if (!Package)
	{
		UE_LOG(LogImageToUMG, Error, TEXT("Failed to create package at %s"), *FullPath);
		return nullptr;
	}

	// 创建纹理
	UTexture2D* Texture = NewObject<UTexture2D>(Package, FName(*AssetName), RF_Public | RF_Standalone);
	Texture->SetPlatformData(new FTexturePlatformData());
	Texture->GetPlatformData()->SizeX = Width;
	Texture->GetPlatformData()->SizeY = Height;
	Texture->GetPlatformData()->PixelFormat = PF_B8G8R8A8;

	ConfigureUITexture(Texture);

	// 创建 Mip
	FTexture2DMipMap* Mip = new FTexture2DMipMap();
	Texture->GetPlatformData()->Mips.Add(Mip);
	Mip->SizeX = Width;
	Mip->SizeY = Height;

	// 复制数据
	Mip->BulkData.Lock(LOCK_READ_WRITE);
	void* TextureData = Mip->BulkData.Realloc(RawData.Num());
	FMemory::Memcpy(TextureData, RawData.GetData(), RawData.Num());
	Mip->BulkData.Unlock();

	// 更新纹理
	Texture->Source.Init(Width, Height, 1, 1, TSF_BGRA8, RawData.GetData());
	Texture->UpdateResource();

	// 保存资产
	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Texture);

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	FString FileName = FPackageName::LongPackageNameToFilename(FullPath, FPackageName::GetAssetPackageExtension());
	UPackage::SavePackage(Package, Texture, *FileName, SaveArgs);

	return Texture;
}

int32 FImageAssetProcessor::ProcessImageAssetsForPreview(
	const TArray<FUMGImageAsset>& ImageAssets,
	const TArray<uint8>& SourceImageData,
	TArray<FProcessedImageAsset>& OutResults,
	TArray<TStrongObjectPtr<UTexture2D>>& OutTextures,
	FVector2D CoordScale)
{
	if (ImageAssets.Num() == 0 || SourceImageData.Num() == 0)
	{
		return 0;
	}

	// 清理旧的预览纹理
	OutTextures.Empty();
	OutResults.Empty();

	// 解压源图一次，后续复用
	TArray<uint8> SourceRawData;
	int32 SourceWidth, SourceHeight;
	if (!FImageEncoder::DecompressImageData(SourceImageData, SourceRawData, SourceWidth, SourceHeight))
	{
		UE_LOG(LogImageToUMG, Error, TEXT("ProcessImageAssetsForPreview: Failed to decompress source image"));
		return 0;
	}

	int32 SuccessCount = 0;

	for (const FUMGImageAsset& Asset : ImageAssets)
	{
		FCropParams P = CalculateCropParams(Asset, SourceWidth, SourceHeight, CoordScale);

		TArray<uint8> CroppedData;
		if (!CropAndResizeImageFromRaw(SourceRawData, SourceWidth, SourceHeight,
			P.CropX, P.CropY, P.CropW, P.CropH, P.TargetWidth, P.TargetHeight, CroppedData))
		{
			continue;
		}

		// 解压裁剪后的 PNG 数据
		TArray<uint8> RawData;
		int32 Width, Height;
		if (!FImageEncoder::DecompressImageData(CroppedData, RawData, Width, Height))
		{
			continue;
		}

		// 创建临时纹理（不保存到磁盘）
		UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
		if (!Texture)
		{
			continue;
		}

		ConfigureUITexture(Texture);

		// 填充数据
		void* TextureData = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(TextureData, RawData.GetData(), RawData.Num());
		Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
		Texture->UpdateResource();

		// 保存强引用防止GC
		OutTextures.Add(TStrongObjectPtr<UTexture2D>(Texture));

		FProcessedImageAsset Result;
		Result.Id = Asset.Id;
		Result.Texture = Texture;
		Result.TexturePath = Texture->GetPathName();  // 临时路径
		OutResults.Add(Result);
		SuccessCount++;
	}

	return SuccessCount;
}

bool FImageAssetProcessor::ScaleImageData(
	const TArray<uint8>& SourceData,
	int32 SourceWidth,
	int32 SourceHeight,
	int32 TargetWidth,
	int32 TargetHeight,
	TArray<uint8>& OutScaledData)
{
	if (SourceData.Num() == 0 || TargetWidth <= 0 || TargetHeight <= 0)
	{
		return false;
	}

	// 解压源图像
	TArray<uint8> RawData;
	int32 ActualWidth, ActualHeight;
	if (!FImageEncoder::DecompressImageData(SourceData, RawData, ActualWidth, ActualHeight))
	{
		UE_LOG(LogImageToUMG, Error, TEXT("ScaleImageData: Failed to decompress source image"));
		return false;
	}

	// 验证尺寸
	if (ActualWidth != SourceWidth || ActualHeight != SourceHeight)
	{
		UE_LOG(LogImageToUMG, Warning, TEXT("ScaleImageData: Size mismatch (expected %dx%d, got %dx%d)"),
			SourceWidth, SourceHeight, ActualWidth, ActualHeight);
	}

	// 缩放
	TArray<uint8> ScaledRaw;
	ScaledRaw.SetNumUninitialized(TargetWidth * TargetHeight * 4);
	ResizeImageBilinear(RawData.GetData(), ActualWidth, ActualHeight,
		ScaledRaw.GetData(), TargetWidth, TargetHeight);

	// 压缩为 PNG
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> OutputWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
	if (!OutputWrapper.IsValid())
	{
		return false;
	}

	if (!OutputWrapper->SetRaw(ScaledRaw.GetData(), ScaledRaw.Num(), TargetWidth, TargetHeight, ERGBFormat::BGRA, 8))
	{
		return false;
	}

	OutScaledData = OutputWrapper->GetCompressed();
	return OutScaledData.Num() > 0;
}

void FImageAssetProcessor::PrepareImageForAI(
	const TArray<uint8>& SourceData,
	FIntPoint SourceSize,
	int32 MaxDimension,
	TArray<uint8>& OutImageData,
	FIntPoint& OutSize)
{
	// 如果图像尺寸在限制内，直接使用原始数据
	if (SourceSize.X <= MaxDimension && SourceSize.Y <= MaxDimension)
	{
		OutImageData = SourceData;
		OutSize = SourceSize;
		return;
	}

	// 计算缩放比例，保持宽高比
	float Scale = FMath::Min(
		static_cast<float>(MaxDimension) / SourceSize.X,
		static_cast<float>(MaxDimension) / SourceSize.Y
	);
	int32 NewW = FMath::RoundToInt(SourceSize.X * Scale);
	int32 NewH = FMath::RoundToInt(SourceSize.Y * Scale);

	TArray<uint8> ScaledData;
	if (ScaleImageData(SourceData, SourceSize.X, SourceSize.Y, NewW, NewH, ScaledData))
	{
		OutImageData = MoveTemp(ScaledData);
		OutSize = FIntPoint(NewW, NewH);
		UE_LOG(LogImageToUMG, Log, TEXT("Image scaled to %dx%d for AI (original: %dx%d)"), NewW, NewH, SourceSize.X, SourceSize.Y);
	}
	else
	{
		// 缩放失败，使用原始数据
		OutImageData = SourceData;
		OutSize = SourceSize;
	}
}
