// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignImageBakeUtils.h"

#include "Design2UMGLog.h"
#include "DesignSchema.h"
#include "DesignSchemaSourceExtensions.h"

#include "HAL/FileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"

namespace
{
	FString NormalizeCacheIdentityPath(const FString& InPath)
	{
		FString FullPath = FPaths::ConvertRelativePathToFull(InPath);
		FPaths::NormalizeFilename(FullPath);
		FullPath.ToLowerInline();
		return FullPath;
	}

	float ComputeLuminosity(const FVector3f& Color)
	{
		return 0.3f * Color.X + 0.59f * Color.Y + 0.11f * Color.Z;
	}

	FVector3f ClipLuminosityColor(const FVector3f& Color)
	{
		FVector3f Result = Color;
		const float L = ComputeLuminosity(Result);
		const float N = FMath::Min3(Result.X, Result.Y, Result.Z);
		const float X = FMath::Max3(Result.X, Result.Y, Result.Z);

		if (N < 0.0f)
		{
			const float Denominator = L - N;
			if (!FMath::IsNearlyZero(Denominator))
			{
				Result.X = L + ((Result.X - L) * L) / Denominator;
				Result.Y = L + ((Result.Y - L) * L) / Denominator;
				Result.Z = L + ((Result.Z - L) * L) / Denominator;
			}
			else
			{
				Result = FVector3f(FMath::Clamp(L, 0.0f, 1.0f));
			}
		}

		if (X > 1.0f)
		{
			const float Denominator = X - L;
			if (!FMath::IsNearlyZero(Denominator))
			{
				Result.X = L + ((Result.X - L) * (1.0f - L)) / Denominator;
				Result.Y = L + ((Result.Y - L) * (1.0f - L)) / Denominator;
				Result.Z = L + ((Result.Z - L) * (1.0f - L)) / Denominator;
			}
			else
			{
				Result = FVector3f(FMath::Clamp(L, 0.0f, 1.0f));
			}
		}

		Result.X = FMath::Clamp(Result.X, 0.0f, 1.0f);
		Result.Y = FMath::Clamp(Result.Y, 0.0f, 1.0f);
		Result.Z = FMath::Clamp(Result.Z, 0.0f, 1.0f);
		return Result;
	}

	FVector3f SetLuminosity(const FVector3f& BackdropColor, const float TargetLuminosity)
	{
		const float Delta = TargetLuminosity - ComputeLuminosity(BackdropColor);
		return ClipLuminosityColor(BackdropColor + FVector3f(Delta, Delta, Delta));
	}

	uint8 FloatToByte(const float Value)
	{
		return static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Value, 0.0f, 1.0f) * 255.0f));
	}

	bool TryFindBackdropSolidColor(const TArray<FDesignFill>& Fills, const int32 ImageFillIndex, FString& OutColor)
	{
		for (int32 Index = ImageFillIndex - 1; Index >= 0; --Index)
		{
			const FDesignFill& Candidate = Fills[Index];
			if (!Candidate.bEnabled)
			{
				continue;
			}

			if (Candidate.Type == EDesignFillType::Color && !Candidate.SolidColor.IsEmpty())
			{
				OutColor = Candidate.SolidColor;
				return true;
			}
		}

		return false;
	}

	bool IsSupportedBakedOverlayBlendMode(const FString& BlendMode)
	{
		return BlendMode.Equals(TEXT("LUMINOSITY"), ESearchCase::IgnoreCase)
			|| BlendMode.Equals(TEXT("MULTIPLY"), ESearchCase::IgnoreCase)
			|| BlendMode.Equals(TEXT("SCREEN"), ESearchCase::IgnoreCase)
			|| BlendMode.Equals(TEXT("OVERLAY"), ESearchCase::IgnoreCase);
	}

	bool ShouldBakeImageBlendOverlay(const FDesignFill& Fill)
	{
		return Fill.bEnabled
			&& Fill.IsImage()
			&& Fill.Image.Mode == EDesignImageMode::Fit
			&& IsSupportedBakedOverlayBlendMode(Fill.BlendMode)
			&& !Fill.Image.Url.IsEmpty();
	}

	float GetNormalizedBlendBakeScalingFactor(const FDesignFill& Fill)
	{
		if (Fill.Image.Mode == EDesignImageMode::Fit)
		{
			return 1.0f;
		}

		if (Fill.Image.ScalingFactor.IsSet() && Fill.Image.ScalingFactor.GetValue() > KINDA_SMALL_NUMBER)
		{
			return Fill.Image.ScalingFactor.GetValue();
		}

		return 1.0f;
	}

	FString BuildBlendOverlayKey(
		const FString& SourceDocumentIdentity,
		const FString& SourceImageIdentity,
		const FString& SourceImageKey,
		const FString& BackdropColor,
		const FString& BlendMode,
		const float Opacity,
		const float ScalingFactor)
	{
		const FString Signature = FString::Printf(
			TEXT("blendfit|%s|%s|%s|%s|%s|%.4f|%.4f"),
			*SourceDocumentIdentity,
			*SourceImageIdentity,
			*SourceImageKey,
			*BackdropColor,
			*BlendMode.ToLower(),
			Opacity,
			ScalingFactor);
		return DesignImageBakeUtils::HashDerivedImageSignature(TEXT("blendfit"), Signature);
	}

	FVector3f ApplyOverlayBlendMode(const FVector3f& SourceColor, const FVector3f& BackdropColor, const FString& BlendMode)
	{
		if (BlendMode.Equals(TEXT("LUMINOSITY"), ESearchCase::IgnoreCase))
		{
			return SetLuminosity(BackdropColor, ComputeLuminosity(SourceColor));
		}

		if (BlendMode.Equals(TEXT("MULTIPLY"), ESearchCase::IgnoreCase))
		{
			return FVector3f(
				SourceColor.X * BackdropColor.X,
				SourceColor.Y * BackdropColor.Y,
				SourceColor.Z * BackdropColor.Z);
		}

		if (BlendMode.Equals(TEXT("SCREEN"), ESearchCase::IgnoreCase))
		{
			return FVector3f(
				1.0f - (1.0f - SourceColor.X) * (1.0f - BackdropColor.X),
				1.0f - (1.0f - SourceColor.Y) * (1.0f - BackdropColor.Y),
				1.0f - (1.0f - SourceColor.Z) * (1.0f - BackdropColor.Z));
		}

		if (BlendMode.Equals(TEXT("OVERLAY"), ESearchCase::IgnoreCase))
		{
			const auto OverlayChannel = [](const float Source, const float Backdrop)
			{
				return (Backdrop < 0.5f)
					? (2.0f * Source * Backdrop)
					: (1.0f - 2.0f * (1.0f - Source) * (1.0f - Backdrop));
			};

			return FVector3f(
				OverlayChannel(SourceColor.X, BackdropColor.X),
				OverlayChannel(SourceColor.Y, BackdropColor.Y),
				OverlayChannel(SourceColor.Z, BackdropColor.Z));
		}

		return SourceColor;
	}

	FVector3f ClampColor01(const FVector3f& Color)
	{
		return FVector3f(
			FMath::Clamp(Color.X, 0.0f, 1.0f),
			FMath::Clamp(Color.Y, 0.0f, 1.0f),
			FMath::Clamp(Color.Z, 0.0f, 1.0f));
	}

	float SampleDecodedImageChannel(
		const TArray64<uint8>& RawData,
		const int32 Width,
		const int32 Height,
		const float X,
		const float Y,
		const int32 Channel)
	{
		if (X < 0.0f || Y < 0.0f || X > Width - 1.0f || Y > Height - 1.0f)
		{
			return 0.0f;
		}

		const int32 X0 = FMath::Clamp(FMath::FloorToInt(X), 0, Width - 1);
		const int32 Y0 = FMath::Clamp(FMath::FloorToInt(Y), 0, Height - 1);
		const int32 X1 = FMath::Clamp(X0 + 1, 0, Width - 1);
		const int32 Y1 = FMath::Clamp(Y0 + 1, 0, Height - 1);
		const float FracX = FMath::Clamp(X - X0, 0.0f, 1.0f);
		const float FracY = FMath::Clamp(Y - Y0, 0.0f, 1.0f);

		const auto Read = [&RawData, Width](const int32 SX, const int32 SY, const int32 C) -> float
		{
			return RawData[(static_cast<int64>(SY) * Width + SX) * 4 + C] / 255.0f;
		};

		const float Top = FMath::Lerp(Read(X0, Y0, Channel), Read(X1, Y0, Channel), FracX);
		const float Bottom = FMath::Lerp(Read(X0, Y1, Channel), Read(X1, Y1, Channel), FracX);
		return FMath::Lerp(Top, Bottom, FracY);
	}

	bool GenerateBlendOverlayImage(
		const FString& SourceImagePath,
		const FString& BackdropColor,
		const FString& BlendMode,
		const float FillOpacity,
		const FString& OutputPath,
		FString& OutError)
	{
		int32 Width = 0;
		int32 Height = 0;
		TArray64<uint8> SourceRawData;
		if (!DesignImageBakeUtils::DecodeImageFileToBGRA8(SourceImagePath, Width, Height, SourceRawData, OutError))
		{
			return false;
		}

		TArray64<uint8> RawData;
		RawData.SetNumZeroed(SourceRawData.Num());

		const FColor Backdrop = FColor::FromHex(BackdropColor);
		const FVector3f BackdropRgb(
			Backdrop.R / 255.0f,
			Backdrop.G / 255.0f,
			Backdrop.B / 255.0f);
		const float EffectiveFillOpacity = FMath::Clamp(FillOpacity, 0.0f, 1.0f);

		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const float SampleX = static_cast<float>(X);
				const float SampleY = static_cast<float>(Y);
				const float SourceB = SampleDecodedImageChannel(SourceRawData, Width, Height, SampleX, SampleY, 0);
				const float SourceG = SampleDecodedImageChannel(SourceRawData, Width, Height, SampleX, SampleY, 1);
				const float SourceR = SampleDecodedImageChannel(SourceRawData, Width, Height, SampleX, SampleY, 2);
				const float SourceA = SampleDecodedImageChannel(SourceRawData, Width, Height, SampleX, SampleY, 3);
				const FVector3f SourceRgb(SourceR, SourceG, SourceB);
				const FVector3f Blended = ClampColor01(ApplyOverlayBlendMode(SourceRgb, BackdropRgb, BlendMode));
				const int64 PixelOffset = (static_cast<int64>(Y) * Width + X) * 4;
				RawData[PixelOffset + 0] = FloatToByte(Blended.Z);
				RawData[PixelOffset + 1] = FloatToByte(Blended.Y);
				RawData[PixelOffset + 2] = FloatToByte(Blended.X);
				RawData[PixelOffset + 3] = FloatToByte(SourceA * EffectiveFillOpacity);
			}
		}

		return DesignImageBakeUtils::SaveBGRA8ToPng(RawData, Width, Height, OutputPath, OutError);
	}

	void ApplyImagePathMapRecursive(
		TArray<TSharedPtr<FDesignNode>>& Nodes,
		const TMap<FString, FString>& ImageKeyToPath)
	{
		for (TSharedPtr<FDesignNode>& Node : Nodes)
		{
			if (!Node.IsValid())
			{
				continue;
			}

			if (Node->Visual.Fill.IsImage() && !Node->Visual.Fill.Image.Url.IsEmpty())
			{
				if (const FString* ResolvedPath = ImageKeyToPath.Find(Node->Visual.Fill.Image.Url))
				{
					Node->Visual.Fill.Image.Url = *ResolvedPath;
				}
			}

			for (FDesignFill& Fill : Node->Visual.Fills)
			{
				if (Fill.IsImage() && !Fill.Image.Url.IsEmpty())
				{
					if (const FString* ResolvedPath = ImageKeyToPath.Find(Fill.Image.Url))
					{
						Fill.Image.Url = *ResolvedPath;
					}
				}
			}

			ApplyImagePathMapRecursive(Node->Children, ImageKeyToPath);
		}
	}

	void CollectLocalImagePathMapRecursive(
		const TArray<TSharedPtr<FDesignNode>>& Nodes,
		TMap<FString, FString>& OutImageKeyToPath)
	{
		for (const TSharedPtr<FDesignNode>& Node : Nodes)
		{
			if (!Node.IsValid())
			{
				continue;
			}

			const auto TryAddImagePath = [&OutImageKeyToPath](const FDesignFill& Fill)
			{
				if (Fill.IsImage() && !Fill.Image.Url.IsEmpty() && FPaths::FileExists(Fill.Image.Url))
				{
					OutImageKeyToPath.Add(Fill.Image.Url, Fill.Image.Url);
				}
			};

			TryAddImagePath(Node->Visual.Fill);
			for (const FDesignFill& Fill : Node->Visual.Fills)
			{
				TryAddImagePath(Fill);
			}

			CollectLocalImagePathMapRecursive(Node->Children, OutImageKeyToPath);
		}
	}
}

FString DesignImageBakeUtils::BuildSourceDocumentIdentity(const FString& SourcePathOrIdentity)
{
	return NormalizeCacheIdentityPath(SourcePathOrIdentity);
}

FString DesignImageBakeUtils::HashDerivedImageSignature(const FString& Prefix, const FString& Signature)
{
	const FTCHARToUTF8 Utf8(*Signature);
	FMD5 Md5;
	Md5.Update(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	uint8 Digest[16];
	Md5.Final(Digest);
	return Prefix + TEXT("_") + BytesToHex(Digest, UE_ARRAY_COUNT(Digest)).ToLower();
}

FString DesignImageBakeUtils::BuildSourceFileIdentity(const FString& FilePath)
{
	const FString NormalizedPath = NormalizeCacheIdentityPath(FilePath);
	if (!FPaths::FileExists(NormalizedPath))
	{
		return NormalizedPath;
	}

	const FMD5Hash FileHash = FMD5Hash::HashFile(*NormalizedPath);
	return FString::Printf(TEXT("%s|%s"), *NormalizedPath, *LexToString(FileHash));
}

FString DesignImageBakeUtils::BuildSourceScopedDerivedDir(const FString& SourceDocumentIdentity)
{
	return FPaths::ProjectSavedDir()
		/ TEXT("Design2UMG")
		/ TEXT("DerivedImageBakes")
		/ HashDerivedImageSignature(TEXT("doc"), SourceDocumentIdentity);
}

bool DesignImageBakeUtils::DecodeImageFileToBGRA8(
	const FString& ImagePath,
	int32& OutWidth,
	int32& OutHeight,
	TArray64<uint8>& OutRawData,
	FString& OutError)
{
	TArray<uint8> CompressedData;
	if (!FFileHelper::LoadFileToArray(CompressedData, *ImagePath) || CompressedData.Num() == 0)
	{
		OutError = FString::Printf(TEXT("Failed to read image data: %s"), *ImagePath);
		return false;
	}

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const EImageFormat Format = ImageWrapperModule.DetectImageFormat(CompressedData.GetData(), CompressedData.Num());
	if (Format == EImageFormat::Invalid)
	{
		OutError = FString::Printf(TEXT("Unsupported image format: %s"), *ImagePath);
		return false;
	}

	TSharedPtr<IImageWrapper> Wrapper = ImageWrapperModule.CreateImageWrapper(Format);
	if (!Wrapper.IsValid() || !Wrapper->SetCompressed(CompressedData.GetData(), CompressedData.Num()))
	{
		OutError = FString::Printf(TEXT("Failed to parse image data: %s"), *ImagePath);
		return false;
	}

	OutWidth = Wrapper->GetWidth();
	OutHeight = Wrapper->GetHeight();
	if (OutWidth <= 0 || OutHeight <= 0)
	{
		OutError = FString::Printf(TEXT("Invalid image dimensions: %s"), *ImagePath);
		return false;
	}

	if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, OutRawData))
	{
		OutError = FString::Printf(TEXT("Failed to decode image pixels: %s"), *ImagePath);
		return false;
	}

	return true;
}

bool DesignImageBakeUtils::SaveBGRA8ToPng(
	const TArray64<uint8>& RawData,
	const int32 Width,
	const int32 Height,
	const FString& OutputPath,
	FString& OutError)
{
	if (Width <= 0 || Height <= 0 || RawData.Num() < static_cast<int64>(Width) * static_cast<int64>(Height) * 4)
	{
		OutError = FString::Printf(TEXT("Invalid raw image payload for '%s'"), *OutputPath);
		return false;
	}

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> Wrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
	if (!Wrapper.IsValid() || !Wrapper->SetRaw(RawData.GetData(), RawData.Num(), Width, Height, ERGBFormat::BGRA, 8))
	{
		OutError = FString::Printf(TEXT("Failed to encode image pixels: %s"), *OutputPath);
		return false;
	}

	const TArray64<uint8>& Compressed = Wrapper->GetCompressed(100);
	if (Compressed.Num() == 0)
	{
		OutError = FString::Printf(TEXT("Encoded PNG payload is empty: %s"), *OutputPath);
		return false;
	}

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputPath), true);
	TArray<uint8> Bytes;
	Bytes.Append(Compressed.GetData(), Compressed.Num());
	if (!FFileHelper::SaveArrayToFile(Bytes, *OutputPath))
	{
		OutError = FString::Printf(TEXT("Failed to save PNG: %s"), *OutputPath);
		return false;
	}

	return true;
}

void DesignImageBakeUtils::ApplyImagePathMap(FDesignDocument& Document, const TMap<FString, FString>& ImageKeyToPath)
{
	ApplyImagePathMapRecursive(Document.Children, ImageKeyToPath);
}

FString DesignImageBakeUtils::ResolveDocumentImageBakeIdentity(const FDesignDocument& Document, const FString& ExplicitSourcePath)
{
	if (!ExplicitSourcePath.IsEmpty())
	{
		return BuildSourceDocumentIdentity(ExplicitSourcePath);
	}

	if (!Document.SourceDocument().SourceFilePath.IsEmpty())
	{
		return BuildSourceDocumentIdentity(Document.SourceDocument().SourceFilePath);
	}

	if (const FString* SourceFilePath = Document.Metadata.Find(TEXT("design.sourceFilePath")))
	{
		if (!SourceFilePath->IsEmpty())
		{
			return BuildSourceDocumentIdentity(*SourceFilePath);
		}
	}

	TMap<FString, FString> LocalImagePathMap;
	CollectLocalImagePathMapRecursive(Document.Children, LocalImagePathMap);
	if (LocalImagePathMap.Num() > 0)
	{
		TArray<FString> SourcePaths;
		LocalImagePathMap.GenerateValueArray(SourcePaths);
		SourcePaths.Sort();
		return FString::Printf(
			TEXT("inline|%s|%s|%d"),
			*Document.Version,
			*FString::Join(SourcePaths, TEXT("|")),
			Document.Children.Num());
	}

	return FString::Printf(TEXT("inline|%s|children:%d"), *Document.Version, Document.Children.Num());
}

int32 DesignImageBakeUtils::BakeMappedImageBlendOverlays(
	TArray<TSharedPtr<FDesignNode>>& Nodes,
	TMap<FString, FString>& InOutImageKeyToLocalPath,
	const FString& SourceDocumentIdentity,
	const TCHAR* LogContext)
{
	int32 BakedCount = 0;
	const FString DerivedDir = BuildSourceScopedDerivedDir(SourceDocumentIdentity);
	IFileManager::Get().MakeDirectory(*DerivedDir, true);

	TArray<TSharedPtr<FDesignNode>> PendingNodes = Nodes;
	while (PendingNodes.Num() > 0)
	{
		TSharedPtr<FDesignNode> Node = PendingNodes.Pop(EAllowShrinking::No);
		if (!Node.IsValid())
		{
			continue;
		}

		bool bNodeChanged = false;
		for (int32 FillIndex = 0; FillIndex < Node->Visual.Fills.Num(); ++FillIndex)
		{
			FDesignFill& Fill = Node->Visual.Fills[FillIndex];
			if (!ShouldBakeImageBlendOverlay(Fill))
			{
				continue;
			}

			FString BackdropColor;
			if (!TryFindBackdropSolidColor(Node->Visual.Fills, FillIndex, BackdropColor))
			{
				continue;
			}

			const FString* SourceImagePath = InOutImageKeyToLocalPath.Find(Fill.Image.Url);
			if (!SourceImagePath || !FPaths::FileExists(*SourceImagePath))
			{
				continue;
			}

			const FString SourceImageIdentity = BuildSourceFileIdentity(*SourceImagePath);
			const float NormalizedScalingFactor = GetNormalizedBlendBakeScalingFactor(Fill);
			const FString DerivedKey = BuildBlendOverlayKey(
				SourceDocumentIdentity,
				SourceImageIdentity,
				Fill.Image.Url,
				BackdropColor,
				Fill.BlendMode,
				Fill.Image.Opacity,
				NormalizedScalingFactor);

			FString* ExistingDerivedPath = InOutImageKeyToLocalPath.Find(DerivedKey);
			if (!ExistingDerivedPath)
			{
				const FString DerivedPngPath = DerivedDir / (DerivedKey + TEXT(".png"));
				if (!FPaths::FileExists(DerivedPngPath))
				{
					FString ErrorMessage;
					if (!GenerateBlendOverlayImage(
						*SourceImagePath,
						BackdropColor,
						Fill.BlendMode,
						Fill.Image.Opacity,
						DerivedPngPath,
						ErrorMessage))
					{
						UE_LOG(LogDesign2UMG, Warning,
							TEXT("%s: failed to bake %s image blend '%s': %s"),
							LogContext ? LogContext : TEXT("DesignImageBake"),
							*Fill.BlendMode,
							*Fill.Image.Url,
							*ErrorMessage);
						continue;
					}
				}

				InOutImageKeyToLocalPath.Add(DerivedKey, DerivedPngPath);
				ExistingDerivedPath = InOutImageKeyToLocalPath.Find(DerivedKey);
			}

			if (!ExistingDerivedPath)
			{
				continue;
			}

			Fill.Image.Url = DerivedKey;
			Fill.Image.Opacity = 1.0f;
			Fill.Opacity = 1.0f;
			Fill.BlendMode = TEXT("NORMAL");
			Fill.Image.ScalingFactor.Reset();
			bNodeChanged = true;
			++BakedCount;
		}

		if (bNodeChanged && Node->Visual.Fills.Num() > 0)
		{
			Node->Visual.Fill = Node->Visual.Fills.Last();
		}

		for (const TSharedPtr<FDesignNode>& Child : Node->Children)
		{
			PendingNodes.Add(Child);
		}
	}

	return BakedCount;
}

int32 DesignImageBakeUtils::BakeDocumentLocalImageBlendOverlays(
	FDesignDocument& Document,
	const FString& ExplicitSourcePath,
	const TCHAR* LogContext)
{
	TMap<FString, FString> LocalImagePathMap;
	CollectLocalImagePathMapRecursive(Document.Children, LocalImagePathMap);
	if (LocalImagePathMap.Num() == 0)
	{
		return 0;
	}

	const FString SourceDocumentIdentity = ResolveDocumentImageBakeIdentity(Document, ExplicitSourcePath);
	const int32 BakedCount = BakeMappedImageBlendOverlays(
		Document.Children,
		LocalImagePathMap,
		SourceDocumentIdentity,
		LogContext);
	if (BakedCount > 0)
	{
		ApplyImagePathMap(Document, LocalImagePathMap);
		UE_LOG(LogDesign2UMG, Log,
			TEXT("%s: baked %d local image blend overlays"),
			LogContext ? LogContext : TEXT("DesignImageBake"),
			BakedCount);
	}

	return BakedCount;
}
