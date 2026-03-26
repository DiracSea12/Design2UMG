#include "Design2UMGLocalFigmaImport.h"

#include "Design2UMGConverter.h"
#include "Design2UMGLog.h"
#include "DesignImportPipeline.h"
#include "DesignImportUtils.h"
#include "FigmaParser.h"
#include "RefExpander.h"
#include "UMGValueParser.h"
#include "UMGSchema.h"

#include "HAL/FileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"

namespace
{
	FString MakeVecKey(const FString& RawId)
	{
		return TEXT("vec_")
			+ RawId
				.Replace(TEXT(":"), TEXT("_"))
				.Replace(TEXT("/"), TEXT("_"));
	}

	bool TryResolveVectorPngPath(const FString& NodeId, const FString& ImagesDir, FString& OutVecKey, FString& OutPngPath)
	{
		TArray<FString> CandidateIds;
		CandidateIds.Add(NodeId);

		int32 DelimIndex = INDEX_NONE;
		if (NodeId.FindLastChar(TEXT(';'), DelimIndex) && DelimIndex + 1 < NodeId.Len())
		{
			CandidateIds.Add(NodeId.Mid(DelimIndex + 1));
		}

		if (NodeId.FindLastChar(TEXT('/'), DelimIndex) && DelimIndex + 1 < NodeId.Len())
		{
			CandidateIds.Add(NodeId.Mid(DelimIndex + 1));
		}

		for (const FString& CandidateId : CandidateIds)
		{
			const FString VecKey = MakeVecKey(CandidateId);
			const FString PngPath = ImagesDir / (VecKey + TEXT(".png"));
			if (FPaths::FileExists(PngPath))
			{
				OutVecKey = VecKey;
				OutPngPath = PngPath;
				return true;
			}
		}
		return false;
	}

	bool ShouldSkipTinyVectorPng(const FString& PngPath, float NodeWidth, float NodeHeight)
	{
		const int64 FileSize = IFileManager::Get().FileSize(*PngPath);
		if (FileSize <= 0)
		{
			return true;
		}

		TArray<uint8> Header;
		if (!FFileHelper::LoadFileToArray(Header, *PngPath) || Header.Num() < 24)
		{
			return false;
		}

		const int32 W = (int32(Header[16]) << 24) | (int32(Header[17]) << 16) | (int32(Header[18]) << 8) | int32(Header[19]);
		const int32 H = (int32(Header[20]) << 24) | (int32(Header[21]) << 16) | (int32(Header[22]) << 8) | int32(Header[23]);

		const bool bDegenerateImage = (W <= 1 || H <= 1);
		const bool bTinyImage = (W <= 2 && H <= 2);
		const bool bSuspiciousPayload = FileSize <= 200;
		const bool bSizeMismatch = bTinyImage && (NodeWidth > 4.0f || NodeHeight > 4.0f);
		return bDegenerateImage || (bTinyImage && bSuspiciousPayload) || bSizeMismatch;
	}

	bool NeedsVectorRasterFallback(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return false;
		}

		if (Node->Metadata.Contains(TEXT("figma.vectorRasterFallback")))
		{
			return true;
		}

		if (Node->Geometry.IsEmpty())
		{
			return true;
		}

		if (Node->Fill.Type == EDesignFillType::Gradient || Node->Fill.Type == EDesignFillType::Image)
		{
			return true;
		}

		for (const FDesignFill& Fill : Node->Fills)
		{
			if (!Fill.bEnabled)
			{
				continue;
			}

			if (Fill.Type == EDesignFillType::Gradient || Fill.Type == EDesignFillType::Image)
			{
				return true;
			}
		}

		return false;
	}

	bool DecodeImageFileToBGRA8(const FString& ImagePath, int32& OutWidth, int32& OutHeight, TArray64<uint8>& OutRawData, FString& OutError)
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

	bool SaveBGRA8ToPng(const TArray64<uint8>& RawData, int32 Width, int32 Height, const FString& OutputPath, FString& OutError)
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

	FVector3f SetLuminosity(const FVector3f& BackdropColor, float TargetLuminosity)
	{
		const float Delta = TargetLuminosity - ComputeLuminosity(BackdropColor);
		return ClipLuminosityColor(BackdropColor + FVector3f(Delta, Delta, Delta));
	}

	uint8 FloatToByte(const float Value)
	{
		return static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Value, 0.0f, 1.0f) * 255.0f));
	}

	FString BuildLuminosityOverlayKey(const FString& ImageRef, const FString& BackdropColor, const FString& BlendMode, float Opacity, float ScalingFactor)
	{
		const FString Signature = FString::Printf(TEXT("lumfit|%s|%s|%s|%.4f|%.4f"), *ImageRef, *BackdropColor, *BlendMode.ToLower(), Opacity, ScalingFactor);
		FTCHARToUTF8 Utf8(*Signature);
		FMD5 Md5;
		Md5.Update(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		uint8 Digest[16];
		Md5.Final(Digest);
		return TEXT("lumfit_") + BytesToHex(Digest, UE_ARRAY_COUNT(Digest)).ToLower();
	}

	float GetNormalizedLuminosityBakeScalingFactor(const FDesignFill& Fill)
	{
		// FIT overlays are re-laid out by the generated ScaleBox at runtime.
		// The bake step only rewrites pixel colors, so scalingFactor does not
		// participate in the derived image identity for this import path.
		if (Fill.Image.Mode == EDesignImageMode::Fit)
		{
			return 1.0f;
		}

		if (Fill.Image.bHasScalingFactor && Fill.Image.ScalingFactor > KINDA_SMALL_NUMBER)
		{
			return Fill.Image.ScalingFactor;
		}

		return 1.0f;
	}

	FString BuildLayerBlurKey(const FString& ImageRef, const FString& NodeId, int32 FillIndex, float BlurRadius)
	{
		const FString Signature = FString::Printf(TEXT("layerblur|%s|%s|%d|%.4f"), *ImageRef, *NodeId, FillIndex, BlurRadius);
		FTCHARToUTF8 Utf8(*Signature);
		FMD5 Md5;
		Md5.Update(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		uint8 Digest[16];
		Md5.Final(Digest);
		return TEXT("layerblur_") + BytesToHex(Digest, UE_ARRAY_COUNT(Digest)).ToLower();
	}

	FString GetDerivedLocalFigmaImagesDir()
	{
		return FPaths::ProjectSavedDir() / TEXT("Design2UMG") / TEXT("DerivedFigmaImages");
	}

	bool TryFindBackdropSolidColor(const TArray<FDesignFill>& Fills, int32 ImageFillIndex, FString& OutColor)
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

	bool ShouldBakeLuminosityOverlay(const FDesignFill& Fill)
	{
		return Fill.bEnabled
			&& Fill.Type == EDesignFillType::Image
			&& Fill.Image.Mode == EDesignImageMode::Fit
			&& IsSupportedBakedOverlayBlendMode(Fill.BlendMode)
			&& !Fill.Image.Url.IsEmpty();
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
			auto OverlayChannel = [](float Source, float Backdrop)
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
		int32 Width,
		int32 Height,
		float X,
		float Y,
		int32 Channel)
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

		const auto Read = [&RawData, Width](int32 SX, int32 SY, int32 C) -> float
		{
			return RawData[(static_cast<int64>(SY) * Width + SX) * 4 + C] / 255.0f;
		};

		const float Top = FMath::Lerp(Read(X0, Y0, Channel), Read(X1, Y0, Channel), FracX);
		const float Bottom = FMath::Lerp(Read(X0, Y1, Channel), Read(X1, Y1, Channel), FracX);
		return FMath::Lerp(Top, Bottom, FracY);
	}

	bool GenerateLuminosityOverlayImage(
		const FString& SourceImagePath,
		const FString& BackdropColor,
		const FString& BlendMode,
		float FillOpacity,
		const FString& OutputPath,
		FString& OutError)
	{
		int32 Width = 0;
		int32 Height = 0;
		TArray64<uint8> SourceRawData;
		if (!DecodeImageFileToBGRA8(SourceImagePath, Width, Height, SourceRawData, OutError))
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

		return SaveBGRA8ToPng(RawData, Width, Height, OutputPath, OutError);
	}

	void ApplyBoxBlurPass(
		const TArray64<uint8>& SourceRawData,
		TArray64<uint8>& OutRawData,
		int32 Width,
		int32 Height,
		int32 Radius,
		bool bHorizontal)
	{
		OutRawData.SetNumUninitialized(SourceRawData.Num());
		if (Radius <= 0)
		{
			OutRawData = SourceRawData;
			return;
		}

		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				int32 SampleCount = 0;
				int32 ChannelSum[4] = { 0, 0, 0, 0 };
				for (int32 Offset = -Radius; Offset <= Radius; ++Offset)
				{
					const int32 SampleX = bHorizontal ? FMath::Clamp(X + Offset, 0, Width - 1) : X;
					const int32 SampleY = bHorizontal ? Y : FMath::Clamp(Y + Offset, 0, Height - 1);
					const int64 SampleOffset = (static_cast<int64>(SampleY) * Width + SampleX) * 4;
					for (int32 Channel = 0; Channel < 4; ++Channel)
					{
						ChannelSum[Channel] += SourceRawData[SampleOffset + Channel];
					}
					++SampleCount;
				}

				const int64 PixelOffset = (static_cast<int64>(Y) * Width + X) * 4;
				for (int32 Channel = 0; Channel < 4; ++Channel)
				{
					OutRawData[PixelOffset + Channel] = static_cast<uint8>(FMath::RoundToInt(static_cast<float>(ChannelSum[Channel]) / SampleCount));
				}
			}
		}
	}

	int32 ResolveBlurKernelRadius(const TSharedPtr<FDesignNode>& Node, int32 SourceWidth, int32 SourceHeight, float BlurRadius)
	{
		const float NodeExtent = FMath::Max(1.0f, FMath::Max(Node.IsValid() ? Node->Width.Value : 0.0f, Node.IsValid() ? Node->Height.Value : 0.0f));
		const float SourceExtent = static_cast<float>(FMath::Max(SourceWidth, SourceHeight));
		const float SourceScale = SourceExtent / NodeExtent;
		return FMath::Clamp(FMath::RoundToInt(BlurRadius * SourceScale * 0.35f), 1, 24);
	}

	bool GenerateBlurredImage(
		const TSharedPtr<FDesignNode>& Node,
		const FString& SourceImagePath,
		float BlurRadius,
		const FString& OutputPath,
		FString& OutError)
	{
		int32 Width = 0;
		int32 Height = 0;
		TArray64<uint8> SourceRawData;
		if (!DecodeImageFileToBGRA8(SourceImagePath, Width, Height, SourceRawData, OutError))
		{
			return false;
		}

		const int32 KernelRadius = ResolveBlurKernelRadius(Node, Width, Height, BlurRadius);
		TArray64<uint8> WorkingRawData = SourceRawData;
		TArray64<uint8> TempRawData;
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			ApplyBoxBlurPass(WorkingRawData, TempRawData, Width, Height, KernelRadius, true);
			ApplyBoxBlurPass(TempRawData, WorkingRawData, Width, Height, KernelRadius, false);
		}

		return SaveBGRA8ToPng(WorkingRawData, Width, Height, OutputPath, OutError);
	}

	bool TryGetForegroundBlurRadius(const TSharedPtr<FDesignNode>& Node, float& OutBlurRadius)
	{
		OutBlurRadius = 0.0f;
		if (!Node.IsValid() || Node->Children.Num() != 0)
		{
			return false;
		}

		for (const FDesignEffect& Effect : Node->Effects)
		{
			if (Effect.bEnabled && Effect.Type == EDesignEffectType::Blur && Effect.Radius > 0.0f)
			{
				OutBlurRadius = FMath::Max(OutBlurRadius, Effect.Radius);
			}
		}

		return OutBlurRadius > 0.0f;
	}

	bool SupportsLocalLayerBlurBake(const TSharedPtr<FDesignNode>& Node, float& OutBlurRadius)
	{
		if (!TryGetForegroundBlurRadius(Node, OutBlurRadius))
		{
			return false;
		}

		bool bHasEnabledImageFill = false;
		for (const FDesignFill& Fill : Node->Fills)
		{
			if (!Fill.bEnabled)
			{
				continue;
			}

			if (!Fill.IsImage() || Fill.Image.Url.IsEmpty())
			{
				return false;
			}

			bHasEnabledImageFill = true;
		}

		if (!bHasEnabledImageFill)
		{
			return Node->Fill.IsImage() && !Node->Fill.Image.Url.IsEmpty();
		}

		return true;
	}

	int32 BakeLuminosityOverlayImages(TArray<TSharedPtr<FDesignNode>>& Nodes, TMap<FString, FString>& InOutImageRefToLocalPath)
	{
		int32 BakedCount = 0;
		const FString DerivedDir = GetDerivedLocalFigmaImagesDir();
		IFileManager::Get().MakeDirectory(*DerivedDir, true);

		for (TSharedPtr<FDesignNode>& Node : Nodes)
		{
			if (!Node.IsValid())
			{
				continue;
			}

			bool bNodeChanged = false;
			for (int32 FillIndex = 0; FillIndex < Node->Fills.Num(); ++FillIndex)
			{
				FDesignFill& Fill = Node->Fills[FillIndex];
				if (!ShouldBakeLuminosityOverlay(Fill))
				{
					continue;
				}

				FString BackdropColor;
				if (!TryFindBackdropSolidColor(Node->Fills, FillIndex, BackdropColor))
				{
					continue;
				}

				const FString* SourceImagePath = InOutImageRefToLocalPath.Find(Fill.Image.Url);
				if (!SourceImagePath || !FPaths::FileExists(*SourceImagePath))
				{
					continue;
				}

				const float LuminosityScalingFactor = GetNormalizedLuminosityBakeScalingFactor(Fill);
				const FString DerivedKey = BuildLuminosityOverlayKey(
					Fill.Image.Url,
					BackdropColor,
					Fill.BlendMode,
					Fill.Image.Opacity,
					LuminosityScalingFactor);
				FString* ExistingDerivedPath = InOutImageRefToLocalPath.Find(DerivedKey);
				if (!ExistingDerivedPath)
				{
					const FString DerivedPngPath = DerivedDir / (DerivedKey + TEXT(".png"));
					if (!FPaths::FileExists(DerivedPngPath))
					{
						FString ErrorMessage;
						if (!GenerateLuminosityOverlayImage(
							*SourceImagePath,
							BackdropColor,
							Fill.BlendMode,
							Fill.Image.Opacity,
							DerivedPngPath,
							ErrorMessage))
						{
							UE_LOG(LogDesign2UMG, Warning,
								TEXT("LocalFigmaImport: failed to bake %s overlay '%s': %s"),
								*Fill.BlendMode,
								*Fill.Image.Url,
								*ErrorMessage);
							continue;
						}
					}

					InOutImageRefToLocalPath.Add(DerivedKey, DerivedPngPath);
					ExistingDerivedPath = InOutImageRefToLocalPath.Find(DerivedKey);
				}

				if (!ExistingDerivedPath)
				{
					continue;
				}

				Fill.Image.Url = DerivedKey;
				Fill.Image.Opacity = 1.0f;
				Fill.Opacity = 1.0f;
				Fill.BlendMode = TEXT("NORMAL");
				Fill.Image.ScalingFactor = 1.0f;
				Fill.Image.bHasScalingFactor = false;
				bNodeChanged = true;
				++BakedCount;
			}

			if (bNodeChanged && Node->Fills.Num() > 0)
			{
				Node->Fill = Node->Fills.Last();
			}

			BakedCount += BakeLuminosityOverlayImages(Node->Children, InOutImageRefToLocalPath);
		}

		return BakedCount;
	}

	int32 BakeLayerBlurImages(TArray<TSharedPtr<FDesignNode>>& Nodes, TMap<FString, FString>& InOutImageRefToLocalPath)
	{
		int32 BakedCount = 0;
		const FString DerivedDir = GetDerivedLocalFigmaImagesDir();
		IFileManager::Get().MakeDirectory(*DerivedDir, true);

		for (TSharedPtr<FDesignNode>& Node : Nodes)
		{
			if (!Node.IsValid())
			{
				continue;
			}

			float BlurRadius = 0.0f;
			if (SupportsLocalLayerBlurBake(Node, BlurRadius))
			{
				struct FPendingBake
				{
					FDesignFill* Fill = nullptr;
					FString DerivedKey;
				};

				TArray<FPendingBake> PendingBakes;
				bool bBakeSupported = true;
				for (int32 FillIndex = 0; FillIndex < Node->Fills.Num(); ++FillIndex)
				{
					FDesignFill& Fill = Node->Fills[FillIndex];
					if (!Fill.bEnabled)
					{
						continue;
					}

					const FString* SourceImagePath = InOutImageRefToLocalPath.Find(Fill.Image.Url);
					if (!SourceImagePath || !FPaths::FileExists(*SourceImagePath))
					{
						bBakeSupported = false;
						break;
					}

					const FString DerivedKey = BuildLayerBlurKey(Fill.Image.Url, Node->Id, FillIndex, BlurRadius);
					if (!InOutImageRefToLocalPath.Contains(DerivedKey))
					{
						const FString DerivedPngPath = DerivedDir / (DerivedKey + TEXT(".png"));
						if (!FPaths::FileExists(DerivedPngPath))
						{
							FString ErrorMessage;
							if (!GenerateBlurredImage(Node, *SourceImagePath, BlurRadius, DerivedPngPath, ErrorMessage))
							{
								UE_LOG(LogDesign2UMG, Warning,
									TEXT("LocalFigmaImport: failed to bake layer blur '%s': %s"),
									*Node->Id,
									*ErrorMessage);
								bBakeSupported = false;
								break;
							}
						}

						InOutImageRefToLocalPath.Add(DerivedKey, DerivedPngPath);
					}

					FPendingBake& PendingBake = PendingBakes.AddDefaulted_GetRef();
					PendingBake.Fill = &Fill;
					PendingBake.DerivedKey = DerivedKey;
				}

				if (bBakeSupported && PendingBakes.Num() > 0)
				{
					for (FPendingBake& PendingBake : PendingBakes)
					{
						PendingBake.Fill->Image.Url = PendingBake.DerivedKey;
					}
					Node->Effects.RemoveAll([](const FDesignEffect& Effect)
					{
						return Effect.Type == EDesignEffectType::Blur;
					});
					if (Node->Fills.Num() > 0)
					{
						Node->Fill = Node->Fills.Last();
					}
					BakedCount += PendingBakes.Num();
				}
			}

			BakedCount += BakeLayerBlurImages(Node->Children, InOutImageRefToLocalPath);
		}

		return BakedCount;
	}

	int32 InjectVectorImageFills(TArray<TSharedPtr<FDesignNode>>& Nodes, const FString& ImagesDir, TMap<FString, FString>& OutMapping)
	{
		int32 InjectedCount = 0;

		for (TSharedPtr<FDesignNode>& Node : Nodes)
		{
			if (!Node.IsValid())
			{
				continue;
			}

			const bool bIsVector =
				(Node->Type == EDesignNodeType::Path) ||
				(Node->Type == EDesignNodeType::Line) ||
				(Node->Type == EDesignNodeType::Ellipse) ||
				(Node->Type == EDesignNodeType::Polygon);

			if (bIsVector && !Node->Fill.IsImage() && NeedsVectorRasterFallback(Node))
			{
				FString VecKey;
				FString PngPath;
				if (TryResolveVectorPngPath(Node->Id, ImagesDir, VecKey, PngPath))
				{
					if (ShouldSkipTinyVectorPng(PngPath, Node->Width.Value, Node->Height.Value))
					{
						continue;
					}

					FDesignFill ImgFill;
					ImgFill.Type = EDesignFillType::Image;
					ImgFill.Image.Url = VecKey;
					ImgFill.Image.Mode = EDesignImageMode::Fill;
					Node->Fill = ImgFill;
					Node->Fills.Empty();
					Node->Fills.Add(ImgFill);
					if (!OutMapping.Contains(VecKey))
					{
						OutMapping.Add(VecKey, PngPath);
					}
					++InjectedCount;
				}
			}

			InjectedCount += InjectVectorImageFills(Node->Children, ImagesDir, OutMapping);
		}

		return InjectedCount;
	}

	TSharedPtr<FDesignNode> FindDesignNodeByIdRecursive(const TArray<TSharedPtr<FDesignNode>>& Nodes, const FString& TargetNodeId)
	{
		for (const TSharedPtr<FDesignNode>& Node : Nodes)
		{
			if (!Node.IsValid())
			{
				continue;
			}

			if (Node->Id == TargetNodeId)
			{
				return Node;
			}

			if (TSharedPtr<FDesignNode> ChildMatch = FindDesignNodeByIdRecursive(Node->Children, TargetNodeId))
			{
				return ChildMatch;
			}
		}

		return nullptr;
	}

	bool FilterDocumentToTargetNode(
		const FDesignDocument& InDocument,
		const FString& TargetNodeId,
		FDesignDocument& OutDocument,
		FString& OutError)
	{
		OutDocument = InDocument;
		if (TargetNodeId.IsEmpty())
		{
			return true;
		}

		const TSharedPtr<FDesignNode> Match = FindDesignNodeByIdRecursive(InDocument.Children, TargetNodeId);
		if (!Match.IsValid())
		{
			OutError = FString::Printf(TEXT("Failed to locate Figma target node '%s'"), *TargetNodeId);
			return false;
		}

		OutDocument.Children.Reset();
		OutDocument.Children.Add(FRefExpander::DeepCopyNode(Match));
		return true;
	}

	void NormalizeSingleRootDocumentToOrigin(FDesignDocument& Document)
	{
		if (Document.Children.Num() != 1 || !Document.Children[0].IsValid())
		{
			return;
		}

		Document.Children[0]->X = 0.0f;
		Document.Children[0]->Y = 0.0f;
		Document.Children[0]->bHasX = true;
		Document.Children[0]->bHasY = true;
	}
}

namespace Design2UMGLocalFigmaImport
{
	bool LoadLocalFigmaDocument(
		const FString& JsonFilePath,
		FDesignDocument& OutDocument,
		FString& OutError,
		const FString& TargetNodeId)
	{
		FDesignDocument ParsedDocument;
		FString JsonStr;
		if (!FFileHelper::LoadFileToString(JsonStr, *JsonFilePath))
		{
			OutError = FString::Printf(TEXT("Failed to read design.json: %s"), *JsonFilePath);
			return false;
		}

		if (!FFigmaParser::ParseDocument(JsonStr, ParsedDocument))
		{
			OutError = FString::Printf(TEXT("Failed to parse Figma JSON: %s"), *JsonFilePath);
			return false;
		}

		const FString ImagesDir = FPaths::GetPath(JsonFilePath) / TEXT("images");
		TMap<FString, FString> ImageRefToLocalPath;
		const int32 VecCount = InjectVectorImageFills(ParsedDocument.Children, ImagesDir, ImageRefToLocalPath);
		const int32 RenderFallbackCount = 0;

		TSet<FString> ImageRefs;
		FFigmaParser::CollectImageRefs(ParsedDocument, ImageRefs);
		for (const FString& Ref : ImageRefs)
		{
			if (ImageRefToLocalPath.Contains(Ref))
			{
				continue;
			}
			const FString PngPath = ImagesDir / (Ref + TEXT(".png"));
			if (FPaths::FileExists(PngPath))
			{
				ImageRefToLocalPath.Add(Ref, PngPath);
			}
		}
		const int32 LuminosityDerivedCount = BakeLuminosityOverlayImages(ParsedDocument.Children, ImageRefToLocalPath);
		const int32 LayerBlurDerivedCount = BakeLayerBlurImages(ParsedDocument.Children, ImageRefToLocalPath);

		UE_LOG(LogDesign2UMG, Log, TEXT("LocalFigmaImport: %d image mappings (%d vec_*, %d render fallbacks, %d image fills, %d baked blend overlays, %d baked layer blurs)"),
			ImageRefToLocalPath.Num(), VecCount, RenderFallbackCount, ImageRefToLocalPath.Num() - VecCount - RenderFallbackCount, LuminosityDerivedCount, LayerBlurDerivedCount);

		FFigmaParser::ApplyImageUrls(ParsedDocument, ImageRefToLocalPath);
		if (!FilterDocumentToTargetNode(ParsedDocument, TargetNodeId, OutDocument, OutError))
		{
			return false;
		}

		NormalizeSingleRootDocumentToOrigin(OutDocument);
		return true;
	}

	bool BuildLocalFigmaWidgetData(
		const FString& JsonFilePath,
		FUMGWidgetData& OutData,
		FString& OutError,
		const FString& TargetNodeId)
	{
		FDesignDocument ParsedDoc;
		if (!LoadLocalFigmaDocument(JsonFilePath, ParsedDoc, OutError, TargetNodeId))
		{
			return false;
		}

		DesignImportPipeline::ConvertDocument(ParsedDoc, OutData);
		DesignImportPipeline::ResolveImages(OutData);

		if (OutData.RootWidget.Children.Num() == 0)
		{
			OutError = FString::Printf(TEXT("No frames found in export: %s"), *JsonFilePath);
			return false;
		}

		return true;
	}
}
