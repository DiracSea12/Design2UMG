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
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	FString MakeVecKey(const FString& RawId)
	{
		return TEXT("vec_")
			+ RawId
				.Replace(TEXT(":"), TEXT("_"))
				.Replace(TEXT("/"), TEXT("_"));
	}

	FString MakeRenderFallbackKey(const FString& RawId)
	{
		return TEXT("render_")
			+ RawId
				.Replace(TEXT(":"), TEXT("_"))
				.Replace(TEXT("/"), TEXT("_"))
				.Replace(TEXT(";"), TEXT("_"));
	}

	struct FRawBounds
	{
		double X = 0.0;
		double Y = 0.0;
		double W = 0.0;
		double H = 0.0;

		bool IsValid() const
		{
			return W > 0.0 && H > 0.0;
		}
	};

	struct FLocalRenderFallbackSpec
	{
		FString NodeId;
		FString Key;
		FString Reason;
		FRawBounds RelativeBounds;
	};

	bool TryGetNumberField(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, double& OutValue)
	{
		if (!Obj.IsValid())
		{
			return false;
		}
		return Obj->TryGetNumberField(Key, OutValue);
	}

	bool TryReadBoundsObject(const TSharedPtr<FJsonObject>& Obj, FRawBounds& OutBounds)
	{
		if (!Obj.IsValid())
		{
			return false;
		}

		return TryGetNumberField(Obj, TEXT("x"), OutBounds.X)
			&& TryGetNumberField(Obj, TEXT("y"), OutBounds.Y)
			&& TryGetNumberField(Obj, TEXT("width"), OutBounds.W)
			&& TryGetNumberField(Obj, TEXT("height"), OutBounds.H)
			&& OutBounds.IsValid();
	}

	bool TryGetNodeBounds(const TSharedPtr<FJsonObject>& NodeObj, FRawBounds& OutBounds)
	{
		if (!NodeObj.IsValid())
		{
			return false;
		}

		const TSharedPtr<FJsonObject>* BoundsObject = nullptr;
		if (NodeObj->TryGetObjectField(TEXT("absoluteRenderBounds"), BoundsObject) && BoundsObject != nullptr)
		{
			if (TryReadBoundsObject(*BoundsObject, OutBounds))
			{
				return true;
			}
		}
		if (NodeObj->TryGetObjectField(TEXT("absoluteBoundingBox"), BoundsObject) && BoundsObject != nullptr)
		{
			if (TryReadBoundsObject(*BoundsObject, OutBounds))
			{
				return true;
			}
		}
		return false;
	}

	FString GetJsonStringField(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, const FString& DefaultValue = FString())
	{
		if (!Obj.IsValid())
		{
			return DefaultValue;
		}

		FString Value;
		return Obj->TryGetStringField(Key, Value) ? Value : DefaultValue;
	}

	bool GetJsonBoolField(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, const bool DefaultValue = false)
	{
		if (!Obj.IsValid())
		{
			return DefaultValue;
		}

		bool Value = DefaultValue;
		return Obj->TryGetBoolField(Key, Value) ? Value : DefaultValue;
	}

	const TArray<TSharedPtr<FJsonValue>>* GetJsonArrayField(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key)
	{
		if (!Obj.IsValid())
		{
			return nullptr;
		}
		return Obj->HasTypedField<EJson::Array>(Key) ? &Obj->GetArrayField(Key) : nullptr;
	}

	bool HasVisibleJsonChildren(const TSharedPtr<FJsonObject>& NodeObj)
	{
		const TArray<TSharedPtr<FJsonValue>>* Children = GetJsonArrayField(NodeObj, TEXT("children"));
		if (!Children)
		{
			return false;
		}

		for (const TSharedPtr<FJsonValue>& ChildValue : *Children)
		{
			const TSharedPtr<FJsonObject> ChildObj = ChildValue.IsValid() ? ChildValue->AsObject() : nullptr;
			if (ChildObj.IsValid() && GetJsonBoolField(ChildObj, TEXT("visible"), true))
			{
				return true;
			}
		}

		return false;
	}

	bool HasJsonArrayEntries(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = GetJsonArrayField(Obj, Key);
		return Values && Values->Num() > 0;
	}

	bool HasNonEmptyJsonObjectField(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key)
	{
		if (!Obj.IsValid())
		{
			return false;
		}

		const TSharedPtr<FJsonValue> FieldValue = Obj->TryGetField(Key);
		if (!FieldValue.IsValid() || FieldValue->Type != EJson::Object)
		{
			return false;
		}

		const TSharedPtr<FJsonObject> ValueObject = FieldValue->AsObject();
		return ValueObject.IsValid() && ValueObject->Values.Num() > 0;
	}

	bool IsExcludedRenderFallbackNodeType(const FString& NodeType)
	{
		return NodeType == TEXT("TEXT")
			|| NodeType == TEXT("INSTANCE")
			|| NodeType == TEXT("COMPONENT")
			|| NodeType == TEXT("COMPONENT_SET")
			|| NodeType == TEXT("SECTION");
	}

	FString CollectRenderFallbackReason(const TSharedPtr<FJsonObject>& NodeObj)
	{
		const TArray<TSharedPtr<FJsonValue>>* Fills = GetJsonArrayField(NodeObj, TEXT("fills"));
		if (!Fills)
		{
			return FString();
		}

		TArray<FString> Reasons;
		auto AddReason = [&Reasons](const FString& Reason)
		{
			if (!Reason.IsEmpty())
			{
				Reasons.AddUnique(Reason);
			}
		};

		for (const TSharedPtr<FJsonValue>& FillValue : *Fills)
		{
			const TSharedPtr<FJsonObject> FillObj = FillValue.IsValid() ? FillValue->AsObject() : nullptr;
			if (!FillObj.IsValid() || !GetJsonBoolField(FillObj, TEXT("visible"), true))
			{
				continue;
			}

			if (GetJsonStringField(FillObj, TEXT("type")) != TEXT("IMAGE"))
			{
				continue;
			}

			if (HasNonEmptyJsonObjectField(FillObj, TEXT("filters")))
			{
				AddReason(TEXT("image_filters"));
			}

			const FString BlendMode = GetJsonStringField(FillObj, TEXT("blendMode"), TEXT("NORMAL")).ToUpper();
			if (BlendMode != TEXT("NORMAL") && BlendMode != TEXT("PASS-THROUGH") && BlendMode != TEXT("PASS_THROUGH"))
			{
				AddReason(FString::Printf(TEXT("image_blend_%s"), *BlendMode.ToLower()));
			}
		}

		return FString::Join(Reasons, TEXT("+"));
	}

	bool ShouldUseLocalRenderFallback(const TSharedPtr<FJsonObject>& NodeObj, FString& OutReason)
	{
		OutReason.Reset();
		if (!NodeObj.IsValid() || !GetJsonBoolField(NodeObj, TEXT("visible"), true))
		{
			return false;
		}

		if (IsExcludedRenderFallbackNodeType(GetJsonStringField(NodeObj, TEXT("type")).ToUpper()))
		{
			return false;
		}

		if (HasVisibleJsonChildren(NodeObj))
		{
			return false;
		}

		if (HasJsonArrayEntries(NodeObj, TEXT("reactions")) || HasJsonArrayEntries(NodeObj, TEXT("interactions")))
		{
			return false;
		}

		OutReason = CollectRenderFallbackReason(NodeObj);
		return !OutReason.IsEmpty();
	}

	void CollectLocalRenderFallbackSpecs(
		const TSharedPtr<FJsonObject>& NodeObj,
		const FRawBounds& RootBounds,
		TArray<FLocalRenderFallbackSpec>& OutSpecs)
	{
		if (!NodeObj.IsValid())
		{
			return;
		}

		FString Reason;
		FRawBounds NodeBounds;
		if (ShouldUseLocalRenderFallback(NodeObj, Reason) && TryGetNodeBounds(NodeObj, NodeBounds))
		{
			const FString NodeId = GetJsonStringField(NodeObj, TEXT("id"));
			if (!NodeId.IsEmpty())
			{
				FLocalRenderFallbackSpec& Spec = OutSpecs.AddDefaulted_GetRef();
				Spec.NodeId = NodeId;
				Spec.Key = MakeRenderFallbackKey(NodeId);
				Spec.Reason = Reason;
				Spec.RelativeBounds.X = NodeBounds.X - RootBounds.X;
				Spec.RelativeBounds.Y = NodeBounds.Y - RootBounds.Y;
				Spec.RelativeBounds.W = NodeBounds.W;
				Spec.RelativeBounds.H = NodeBounds.H;
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* Children = GetJsonArrayField(NodeObj, TEXT("children"));
		if (!Children)
		{
			return;
		}

		for (const TSharedPtr<FJsonValue>& ChildValue : *Children)
		{
			CollectLocalRenderFallbackSpecs(ChildValue.IsValid() ? ChildValue->AsObject() : nullptr, RootBounds, OutSpecs);
		}
	}

	bool TryParseJsonRoot(const FString& JsonString, TSharedPtr<FJsonObject>& OutRoot)
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
		return FJsonSerializer::Deserialize(Reader, OutRoot) && OutRoot.IsValid();
	}

	bool TryGetPrimaryLocalFigmaRoot(const FString& JsonString, TSharedPtr<FJsonObject>& OutRootNode)
	{
		TSharedPtr<FJsonObject> RootObject;
		if (!TryParseJsonRoot(JsonString, RootObject))
		{
			return false;
		}

		const TSharedPtr<FJsonObject> DocumentObject = RootObject->HasTypedField<EJson::Object>(TEXT("document"))
			? RootObject->GetObjectField(TEXT("document"))
			: nullptr;
		if (!DocumentObject.IsValid())
		{
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* Pages = GetJsonArrayField(DocumentObject, TEXT("children"));
		if (!Pages)
		{
			return false;
		}

		for (const TSharedPtr<FJsonValue>& PageValue : *Pages)
		{
			const TSharedPtr<FJsonObject> PageObject = PageValue.IsValid() ? PageValue->AsObject() : nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Nodes = GetJsonArrayField(PageObject, TEXT("children"));
			if (!Nodes)
			{
				continue;
			}

			for (const TSharedPtr<FJsonValue>& NodeValue : *Nodes)
			{
				const TSharedPtr<FJsonObject> NodeObject = NodeValue.IsValid() ? NodeValue->AsObject() : nullptr;
				if (NodeObject.IsValid())
				{
					OutRootNode = NodeObject;
					return true;
				}
			}
		}

		return false;
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

	bool TryCropRawBgraImage(
		const TArray64<uint8>& SourceRawData,
		int32 SourceWidth,
		int32 SourceHeight,
		int32 Left,
		int32 Top,
		int32 CropWidth,
		int32 CropHeight,
		TArray64<uint8>& OutRawData)
	{
		if (SourceWidth <= 0 || SourceHeight <= 0 || CropWidth <= 0 || CropHeight <= 0)
		{
			return false;
		}

		if (Left < 0 || Top < 0 || Left + CropWidth > SourceWidth || Top + CropHeight > SourceHeight)
		{
			return false;
		}

		OutRawData.SetNumZeroed(static_cast<int64>(CropWidth) * static_cast<int64>(CropHeight) * 4);
		for (int32 Row = 0; Row < CropHeight; ++Row)
		{
			const int64 SourceOffset = (static_cast<int64>(Top + Row) * SourceWidth + Left) * 4;
			const int64 DestOffset = static_cast<int64>(Row) * CropWidth * 4;
			FMemory::Memcpy(
				OutRawData.GetData() + DestOffset,
				SourceRawData.GetData() + SourceOffset,
				static_cast<SIZE_T>(CropWidth) * 4);
		}

		return true;
	}

	bool TryFindPrimaryLocalFigmaReferencePng(const FString& JsonFilePath, FString& OutReferencePngPath)
	{
		const FString ExportDir = FPaths::GetPath(JsonFilePath);
		TArray<FString> CandidateFiles;
		IFileManager::Get().FindFiles(CandidateFiles, *(ExportDir / TEXT("*.png")), true, false);
		if (CandidateFiles.Num() == 0)
		{
			return false;
		}

		int64 BestSize = INDEX_NONE;
		for (const FString& CandidateFile : CandidateFiles)
		{
			const FString CandidatePath = ExportDir / CandidateFile;
			const int64 CandidateSize = IFileManager::Get().FileSize(*CandidatePath);
			if (CandidateSize > BestSize)
			{
				BestSize = CandidateSize;
				OutReferencePngPath = CandidatePath;
			}
		}

		return !OutReferencePngPath.IsEmpty() && FPaths::FileExists(OutReferencePngPath);
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

	FString BuildLuminosityOverlayKey(const FString& ImageRef, const FString& BackdropColor, float Opacity, float ScalingFactor)
	{
		const FString Signature = FString::Printf(TEXT("lumfit|%s|%s|%.4f|%.4f"), *ImageRef, *BackdropColor, Opacity, ScalingFactor);
		FTCHARToUTF8 Utf8(*Signature);
		FMD5 Md5;
		Md5.Update(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		uint8 Digest[16];
		Md5.Final(Digest);
		return TEXT("lumfit_") + BytesToHex(Digest, UE_ARRAY_COUNT(Digest)).ToLower();
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

	bool ShouldBakeLuminosityOverlay(const FDesignFill& Fill)
	{
		return Fill.bEnabled
			&& Fill.Type == EDesignFillType::Image
			&& Fill.Image.Mode == EDesignImageMode::Fit
			&& Fill.BlendMode.Equals(TEXT("LUMINOSITY"), ESearchCase::IgnoreCase)
			&& !Fill.Image.Url.IsEmpty();
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

				const float TargetLuminosity = 0.3f * SourceR + 0.59f * SourceG + 0.11f * SourceB;
				const FVector3f Blended = SetLuminosity(BackdropRgb, TargetLuminosity);
				const int64 PixelOffset = (static_cast<int64>(Y) * Width + X) * 4;
				RawData[PixelOffset + 0] = FloatToByte(Blended.Z);
				RawData[PixelOffset + 1] = FloatToByte(Blended.Y);
				RawData[PixelOffset + 2] = FloatToByte(Blended.X);
				RawData[PixelOffset + 3] = FloatToByte(SourceA * EffectiveFillOpacity);
			}
		}

		return SaveBGRA8ToPng(RawData, Width, Height, OutputPath, OutError);
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

				// FIT 图像后面仍会由 ScaleBox 按节点尺寸做一次适配。
				// 在 bake 阶段再叠加 Figma 的 scalingFactor 会把缩放算两次，
				// export3 的卡片图就是在这里被做差的。
				const float LuminosityScalingFactor = 1.0f;
				const FString DerivedKey = BuildLuminosityOverlayKey(
					Fill.Image.Url,
					BackdropColor,
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
							Fill.Image.Opacity,
							DerivedPngPath,
							ErrorMessage))
						{
							UE_LOG(LogDesign2UMG, Warning,
								TEXT("LocalFigmaImport: failed to bake luminosity overlay '%s': %s"),
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

	int32 InjectLocalRenderFallbackCrops(
		const FString& JsonString,
		const FString& JsonFilePath,
		FDesignDocument& ParsedDocument,
		TMap<FString, FString>& OutMapping)
	{
		FString ReferencePngPath;
		if (!TryFindPrimaryLocalFigmaReferencePng(JsonFilePath, ReferencePngPath))
		{
			return 0;
		}

		TSharedPtr<FJsonObject> RootNodeObject;
		if (!TryGetPrimaryLocalFigmaRoot(JsonString, RootNodeObject))
		{
			return 0;
		}

		FRawBounds RootBounds;
		if (!TryGetNodeBounds(RootNodeObject, RootBounds)
			|| FMath::IsNearlyZero(static_cast<float>(RootBounds.W))
			|| FMath::IsNearlyZero(static_cast<float>(RootBounds.H)))
		{
			return 0;
		}

		TArray<FLocalRenderFallbackSpec> Specs;
		CollectLocalRenderFallbackSpecs(RootNodeObject, RootBounds, Specs);
		if (Specs.Num() == 0)
		{
			return 0;
		}
		if (Specs.Num() > 12)
		{
			UE_LOG(LogDesign2UMG, Verbose,
				TEXT("LocalFigmaImport: skip dense render fallback set (%d nodes) for %s"),
				Specs.Num(),
				*JsonFilePath);
			return 0;
		}

		int32 ReferenceWidth = 0;
		int32 ReferenceHeight = 0;
		TArray64<uint8> ReferenceRawData;
		FString ErrorMessage;
		if (!DecodeImageFileToBGRA8(ReferencePngPath, ReferenceWidth, ReferenceHeight, ReferenceRawData, ErrorMessage))
		{
			UE_LOG(LogDesign2UMG, Warning,
				TEXT("LocalFigmaImport: failed to decode primary reference png '%s': %s"),
				*ReferencePngPath,
				*ErrorMessage);
			return 0;
		}

		const double ScaleX = static_cast<double>(ReferenceWidth) / RootBounds.W;
		const double ScaleY = static_cast<double>(ReferenceHeight) / RootBounds.H;
		if (ScaleX <= 0.0 || ScaleY <= 0.0)
		{
			return 0;
		}

		const FString ScopedPrefix = FPaths::GetBaseFilename(ReferencePngPath);
		const FString DerivedDir = GetDerivedLocalFigmaImagesDir();
		IFileManager::Get().MakeDirectory(*DerivedDir, true);

		int32 InjectedCount = 0;
		for (const FLocalRenderFallbackSpec& Spec : Specs)
		{
			TSharedPtr<FDesignNode> Node = FindDesignNodeByIdRecursive(ParsedDocument.Children, Spec.NodeId);
			if (!Node.IsValid() || Node->Metadata.Contains(TEXT("figma.renderFallbackImageRef")))
			{
				continue;
			}

			const int32 Left = FMath::Clamp(FMath::FloorToInt(static_cast<float>(Spec.RelativeBounds.X * ScaleX)), 0, ReferenceWidth - 1);
			const int32 Top = FMath::Clamp(FMath::FloorToInt(static_cast<float>(Spec.RelativeBounds.Y * ScaleY)), 0, ReferenceHeight - 1);
			const int32 Right = FMath::Clamp(
				FMath::CeilToInt(static_cast<float>((Spec.RelativeBounds.X + Spec.RelativeBounds.W) * ScaleX)),
				Left + 1,
				ReferenceWidth);
			const int32 Bottom = FMath::Clamp(
				FMath::CeilToInt(static_cast<float>((Spec.RelativeBounds.Y + Spec.RelativeBounds.H) * ScaleY)),
				Top + 1,
				ReferenceHeight);
			const int32 CropWidth = Right - Left;
			const int32 CropHeight = Bottom - Top;
			if (CropWidth <= 0 || CropHeight <= 0)
			{
				continue;
			}

			const FString ScopedKey = MakeRenderFallbackKey(ScopedPrefix + TEXT("_") + Spec.NodeId);
			const FString OutputPath = DerivedDir / (ScopedKey + TEXT(".png"));
			if (!FPaths::FileExists(OutputPath))
			{
				TArray64<uint8> CroppedRawData;
				if (!TryCropRawBgraImage(
					ReferenceRawData,
					ReferenceWidth,
					ReferenceHeight,
					Left,
					Top,
					CropWidth,
					CropHeight,
					CroppedRawData))
				{
					continue;
				}

				FString SaveError;
				if (!SaveBGRA8ToPng(CroppedRawData, CropWidth, CropHeight, OutputPath, SaveError))
				{
					UE_LOG(LogDesign2UMG, Warning,
						TEXT("LocalFigmaImport: failed to save render fallback crop '%s': %s"),
						*Spec.NodeId,
						*SaveError);
					continue;
				}
			}

			OutMapping.Add(ScopedKey, OutputPath);
			Node->Metadata.Add(TEXT("figma.renderFallbackImageRef"), ScopedKey);
			Node->Metadata.Add(TEXT("figma.renderFallbackScope"), TEXT("full"));
			Node->Metadata.Add(TEXT("figma.renderFallbackSource"), TEXT("figma_reference_crop"));
			Node->Metadata.Add(TEXT("figma.renderFallbackReason"), Spec.Reason);
			++InjectedCount;
		}

		return InjectedCount;
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
		const int32 RenderFallbackCount = InjectLocalRenderFallbackCrops(JsonStr, JsonFilePath, ParsedDocument, ImageRefToLocalPath);

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

		UE_LOG(LogDesign2UMG, Log, TEXT("LocalFigmaImport: %d image mappings (%d vec_*, %d render fallbacks, %d image fills, %d baked luminosity overlays)"),
			ImageRefToLocalPath.Num(), VecCount, RenderFallbackCount, ImageRefToLocalPath.Num() - VecCount - RenderFallbackCount, LuminosityDerivedCount);

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
