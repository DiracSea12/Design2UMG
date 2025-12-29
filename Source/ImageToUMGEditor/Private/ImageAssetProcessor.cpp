// Copyright Epic Games, Inc. All Rights Reserved.

#include "ImageAssetProcessor.h"
#include "ImageToUMGLog.h"
#include "ImageToUMGConstants.h"
#include "ImageEncoder.h"
#include "JsonExtractUtils.h"

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

	// 背景移除：将背景像素设为透明
	// BgColor: 背景色，智能模式下用于筛选起始点和作为扩散参考
	// Tolerance: 颜色容差 (0-255)
	// bEdgeOnly: true=智能模式（从边缘扩散），false=全局模式
	// MaxDepth: 最大扩散深度（像素），0=无限制
	void RemoveBackgroundColor(TArray<uint8>& RawData, int32 Width, int32 Height,
		const FString& BgColor, int32 Tolerance = 30, bool bEdgeOnly = false, int32 MaxDepth = 0)
	{
		// 获取像素颜色
		auto GetPixel = [&](int32 X, int32 Y, uint8& R, uint8& G, uint8& B) {
			const uint8* P = RawData.GetData() + (Y * Width + X) * 4;
			B = P[0]; G = P[1]; R = P[2];
		};

		// 颜色差异
		auto ColorDiff = [](uint8 R1, uint8 G1, uint8 B1, uint8 R2, uint8 G2, uint8 B2) -> int32 {
			return FMath::Abs(R1 - R2) + FMath::Abs(G1 - G2) + FMath::Abs(B1 - B2);
		};

		// 解析背景色
		uint8 BgR = 0, BgG = 0, BgB = 0;
		bool bHasBgColor = !BgColor.IsEmpty() && FJsonExtractUtils::ParseHexColor(BgColor, BgR, BgG, BgB);

		int32 RemovedCount = 0;

		if (bEdgeOnly)
		{
			// 智能模式：从边缘扩散，带深度限制
			// 使用 BgColor 作为参考色（如果指定）

			TArray<bool> Visited;
			Visited.SetNumZeroed(Width * Height);
			TArray<bool> ToRemove;
			ToRemove.SetNumZeroed(Width * Height);
			// 深度追踪：记录每个像素距离边缘的深度
			TArray<int32> PixelDepth;
			PixelDepth.SetNumZeroed(Width * Height);

			// 使用结构体存储位置和深度
			struct FQueueItem { int32 X, Y, Depth; };
			TArray<FQueueItem> Queue;
			Queue.Reserve(Width * Height / 4);

			// 从边缘添加匹配背景色的像素
			auto TryAddEdgePixel = [&](int32 X, int32 Y) {
				int32 Idx = Y * Width + X;
				if (Visited[Idx]) return;

				uint8 R, G, B;
				GetPixel(X, Y, R, G, B);

				// 必须与背景色匹配才能成为起始点
				if (bHasBgColor && ColorDiff(R, G, B, BgR, BgG, BgB) > Tolerance)
				{
					return;  // 不匹配，跳过（不标记为已访问，可能从其他方向到达）
				}

				Visited[Idx] = true;
				ToRemove[Idx] = true;
				PixelDepth[Idx] = 1;
				Queue.Add({X, Y, 1});
			};

			// 添加四边像素
			for (int32 X = 0; X < Width; ++X)
			{
				TryAddEdgePixel(X, 0);
				TryAddEdgePixel(X, Height - 1);
			}
			for (int32 Y = 1; Y < Height - 1; ++Y)
			{
				TryAddEdgePixel(0, Y);
				TryAddEdgePixel(Width - 1, Y);
			}

			UE_LOG(LogImageToUMG, Verbose, TEXT("智能背景移除: BgColor=#%02X%02X%02X, %d个起始点 (容差=%d, 最大深度=%d)"),
				BgR, BgG, BgB, Queue.Num(), Tolerance, MaxDepth);

			// 四邻域
			const int32 DX[] = {0, 0, -1, 1};
			const int32 DY[] = {-1, 1, 0, 0};

			// BFS 扩散 - 使用宽松条件扩散，让光晕区域也被访问
			// 实际透明度由颜色差异决定，而非扩散条件
			int32 Head = 0;
			while (Head < Queue.Num())
			{
				FQueueItem Cur = Queue[Head++];

				// 深度限制检查：如果超过最大深度则不再扩散
				if (MaxDepth > 0 && Cur.Depth >= MaxDepth)
				{
					continue;
				}

				for (int32 D = 0; D < 4; ++D)
				{
					int32 NX = Cur.X + DX[D];
					int32 NY = Cur.Y + DY[D];

					if (NX < 0 || NX >= Width || NY < 0 || NY >= Height)
						continue;

					int32 NIdx = NY * Width + NX;
					if (Visited[NIdx])
						continue;

					Visited[NIdx] = true;

					uint8 NR, NG, NB;
					GetPixel(NX, NY, NR, NG, NB);

					int32 Diff = ColorDiff(NR, NG, NB, BgR, BgG, BgB);

					// 使用 3 倍容差作为扩散边界，覆盖整个光晕渐变区域
					if (Diff <= Tolerance * 3)
					{
						ToRemove[NIdx] = true;
						PixelDepth[NIdx] = Cur.Depth + 1;
						Queue.Add({NX, NY, Cur.Depth + 1});
					}
				}
			}

			// 应用渐变透明度
			for (int32 i = 0; i < Width * Height; ++i)
			{
				if (ToRemove[i])
				{
					uint8* Pixel = RawData.GetData() + i * 4;
					int32 Diff = ColorDiff(Pixel[2], Pixel[1], Pixel[0], BgR, BgG, BgB);

					// 渐变 Alpha：差异越大越不透明，差异越小越透明
					// Diff <= Tolerance: 完全透明 (Alpha = 0)
					// Diff >= Tolerance * 3: 完全不透明 (保持原 Alpha)
					// 中间：线性渐变
					if (Diff <= Tolerance)
					{
						Pixel[3] = 0;
						RemovedCount++;
					}
					else if (Diff < Tolerance * 3)
					{
						// 渐变区域：保留部分透明度
						int32 GradientAlpha = (Diff - Tolerance) * 255 / (Tolerance * 2);
						Pixel[3] = FMath::Min(Pixel[3], (uint8)GradientAlpha);
						RemovedCount++;
					}
					// else: 保持原始 Alpha，不修改
				}
			}
		}
		else
		{
			// 全局模式：移除所有匹配 BgColor 的像素（带渐变）
			if (!bHasBgColor)
			{
				UE_LOG(LogImageToUMG, Warning, TEXT("全局模式需要指定 BgColor"));
				return;
			}

			UE_LOG(LogImageToUMG, Verbose, TEXT("全局背景移除: #%02X%02X%02X (容差=%d)"), BgR, BgG, BgB, Tolerance);

			const int32 PixelCount = Width * Height;
			for (int32 i = 0; i < PixelCount; ++i)
			{
				uint8* Pixel = RawData.GetData() + i * 4;
				int32 Diff = ColorDiff(Pixel[2], Pixel[1], Pixel[0], BgR, BgG, BgB);

				if (Diff <= Tolerance)
				{
					Pixel[3] = 0;
					RemovedCount++;
				}
				else if (Diff < Tolerance * 3)
				{
					int32 GradientAlpha = (Diff - Tolerance) * 255 / (Tolerance * 2);
					Pixel[3] = FMath::Min(Pixel[3], (uint8)GradientAlpha);
					RemovedCount++;
				}
			}
		}

		UE_LOG(LogImageToUMG, Verbose, TEXT("背景移除完成: %d/%d 像素 (%.1f%%)"),
			RemovedCount, Width * Height, 100.0f * RemovedCount / (Width * Height));
	}

	// 应用透明区域：将指定矩形区域设为透明
	void ApplyTransparentRegions(TArray<uint8>& RawData, int32 Width, int32 Height,
		const TArray<FUMGTransparentRegion>& Regions)
	{
		if (Regions.Num() == 0) return;

		int32 TotalRemoved = 0;
		for (const FUMGTransparentRegion& Region : Regions)
		{
			// 边界检查
			int32 X1 = FMath::Clamp(Region.X, 0, Width);
			int32 Y1 = FMath::Clamp(Region.Y, 0, Height);
			int32 X2 = FMath::Clamp(Region.X + Region.Width, 0, Width);
			int32 Y2 = FMath::Clamp(Region.Y + Region.Height, 0, Height);

			for (int32 Y = Y1; Y < Y2; ++Y)
			{
				for (int32 X = X1; X < X2; ++X)
				{
					RawData[(Y * Width + X) * 4 + 3] = 0;  // 设置 alpha = 0
					TotalRemoved++;
				}
			}
		}

		UE_LOG(LogImageToUMG, Verbose, TEXT("透明区域处理完成: %d 个区域, %d 像素"),
			Regions.Num(), TotalRemoved);
	}

	// 计算裁剪参数（消除重复代码，支持X/Y分别缩放）
	FCropParams CalculateCropParams(
		const FUMGImageAsset& Asset,
		int32 SourceWidth, int32 SourceHeight,
		FVector2D CoordScale)
	{
		FCropParams Params;
		if (Asset.X < 0 || Asset.Y < 0 || Asset.Width <= 0 || Asset.Height <= 0)
		{
			// 使用整个源图
			Params.CropX = 0;
			Params.CropY = 0;
			Params.CropW = SourceWidth;
			Params.CropH = SourceHeight;
			Params.TargetWidth = (Asset.SuggestedWidth > 0) ? Asset.SuggestedWidth : SourceWidth;
			Params.TargetHeight = (Asset.SuggestedHeight > 0) ? Asset.SuggestedHeight : SourceHeight;
		}
		else
		{
			// 坐标缩放：AI返回的坐标基于缩放后图像，需要转换到原始图像坐标
			// 先计算终点坐标再取整，避免分别取整导致的累计误差
			Params.CropX = FMath::RoundToInt(Asset.X * CoordScale.X);
			Params.CropY = FMath::RoundToInt(Asset.Y * CoordScale.Y);
			int32 CropRight = FMath::RoundToInt((Asset.X + Asset.Width) * CoordScale.X);
			int32 CropBottom = FMath::RoundToInt((Asset.Y + Asset.Height) * CoordScale.Y);
			Params.CropW = FMath::Max(1, CropRight - Params.CropX);
			Params.CropH = FMath::Max(1, CropBottom - Params.CropY);
			Params.TargetWidth = (Asset.SuggestedWidth > 0) ? Asset.SuggestedWidth : Params.CropW;
			Params.TargetHeight = (Asset.SuggestedHeight > 0) ? Asset.SuggestedHeight : Params.CropH;
		}
		return Params;
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
		FCropParams Params = CalculateCropParams(Asset, SourceWidth, SourceHeight, CoordScale);

		TArray<uint8> CroppedData;
		if (!CropAndResizeImageFromRaw(SourceRawData, SourceWidth, SourceHeight,
			Params.CropX, Params.CropY, Params.CropW, Params.CropH, Params.TargetWidth, Params.TargetHeight, CroppedData))
		{
			UE_LOG(LogImageToUMG, Warning, TEXT("Failed to crop image for asset '%s'"), *Asset.Id);
			continue;
		}

		// 背景移除或透明区域处理
		bool bNeedProcess = Asset.bRemoveBg || Asset.TransparentRegions.Num() > 0;
		if (bNeedProcess)
		{
			TArray<uint8> RawData;
			int32 Width, Height;
			if (FImageEncoder::DecompressImageData(CroppedData, RawData, Width, Height))
			{
				// 背景移除：EdgeOnly模式不需要BgColor，全局模式需要
				if (Asset.bRemoveBg && (Asset.bEdgeOnly || !Asset.BgColor.IsEmpty()))
				{
					UE_LOG(LogImageToUMG, Verbose, TEXT("Asset '%s' 需要背景移除, EdgeOnly=%d, Tolerance=%d"),
						*Asset.Id, Asset.bEdgeOnly ? 1 : 0, Asset.BgTolerance);
					RemoveBackgroundColor(RawData, Width, Height, Asset.BgColor, Asset.BgTolerance, Asset.bEdgeOnly, Asset.EdgeDepth);
				}

				// 应用内部透明区域
				ApplyTransparentRegions(RawData, Width, Height, Asset.TransparentRegions);

				// 重新压缩为 PNG
				IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
				TSharedPtr<IImageWrapper> Wrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
				if (Wrapper.IsValid() && Wrapper->SetRaw(RawData.GetData(), RawData.Num(), Width, Height, ERGBFormat::BGRA, 8))
				{
					CroppedData = Wrapper->GetCompressed();
				}
			}
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

			UE_LOG(LogImageToUMG, Verbose, TEXT("已创建纹理 '%s'"), *Result.TexturePath);
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
		FCropParams Params = CalculateCropParams(Asset, SourceWidth, SourceHeight, CoordScale);

		UE_LOG(LogImageToUMG, Verbose, TEXT("处理图像资产 '%s': AI坐标(%d,%d,%d,%d) -> 源图坐标(%d,%d,%d,%d), 源图尺寸=%dx%d"),
			*Asset.Id,
			Asset.X, Asset.Y, Asset.Width, Asset.Height,
			Params.CropX, Params.CropY, Params.CropW, Params.CropH,
			SourceWidth, SourceHeight);

		TArray<uint8> CroppedData;
		if (!CropAndResizeImageFromRaw(SourceRawData, SourceWidth, SourceHeight,
			Params.CropX, Params.CropY, Params.CropW, Params.CropH, Params.TargetWidth, Params.TargetHeight, CroppedData))
		{
			UE_LOG(LogImageToUMG, Warning, TEXT("裁剪失败: '%s'"), *Asset.Id);
			continue;
		}

		// 解压裁剪后的 PNG 数据
		TArray<uint8> RawData;
		int32 Width, Height;
		if (!FImageEncoder::DecompressImageData(CroppedData, RawData, Width, Height))
		{
			continue;
		}

		// 背景移除：EdgeOnly模式不需要BgColor，全局模式需要
		if (Asset.bRemoveBg && (Asset.bEdgeOnly || !Asset.BgColor.IsEmpty()))
		{
			UE_LOG(LogImageToUMG, Verbose, TEXT("Asset '%s' 需要背景移除, EdgeOnly=%d, Tolerance=%d"),
				*Asset.Id, Asset.bEdgeOnly ? 1 : 0, Asset.BgTolerance);
			RemoveBackgroundColor(RawData, Width, Height, Asset.BgColor, Asset.BgTolerance, Asset.bEdgeOnly, Asset.EdgeDepth);
		}

		// 应用内部透明区域（无论是否移除背景都应用）
		ApplyTransparentRegions(RawData, Width, Height, Asset.TransparentRegions);

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
		UE_LOG(LogImageToUMG, Verbose, TEXT("Image scaled to %dx%d for AI (original: %dx%d)"), NewW, NewH, SourceSize.X, SourceSize.Y);
	}
	else
	{
		// 缩放失败，使用原始数据
		OutImageData = SourceData;
		OutSize = SourceSize;
	}
}
