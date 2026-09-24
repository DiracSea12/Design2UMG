// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGLocalFigmaImport.h"
#include "DesignSchemaSourceExtensions.h"

#include "Design2UMGConverter.h"
#include "DesignImageBakeUtils.h"
#include "Design2UMGLog.h"
#include "Design2UMGSettings.h"
#include "DesignImportPipeline.h"
#include "FigmaParser.h"
#include "FigmaComponentDependencies.h"
#include "DesignJsonUtils.h"
#include "DesignReferenceExpansion.h"
#include "UMGPropertyKeys.h"
#include "UMGValueParsing.h"
#include "UMGSchema.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	FString MakeVecKey(const FString& RawId)
	{
		return FString(UMGPropertyKeys::VectorRasterImageRefPrefix)
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

		if (!Node->Visual.VectorRasterFallbackImageRef.IsEmpty())
		{
			return true;
		}

		if (Node->Shape.Geometry.IsEmpty())
		{
			return true;
		}

		const FDesignFill& PrimaryFill = Node->Visual.TopmostFill();
		if (PrimaryFill.Type == EDesignFillType::Gradient || PrimaryFill.Type == EDesignFillType::Image)
		{
			return true;
		}

		for (const FDesignFill& Fill : Node->Visual.Fills)
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

	FString BuildLayerBlurKey(
		const FString& SourceDocumentIdentity,
		const FString& SourceImageIdentity,
		const FString& ImageRef,
		const FString& NodeId,
		int32 FillIndex,
		float BlurRadius)
	{
		const FString Signature = FString::Printf(
			TEXT("layerblur|%s|%s|%s|%s|%d|%.4f"),
			*SourceDocumentIdentity,
			*SourceImageIdentity,
			*ImageRef,
			*NodeId,
			FillIndex,
			BlurRadius);
		return DesignImageBakeUtils::HashDerivedImageSignature(TEXT("layerblur"), Signature);
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
		const float NodeExtent = FMath::Max(1.0f, FMath::Max(Node.IsValid() ? Node->Transform.Width.Value : 0.0f, Node.IsValid() ? Node->Transform.Height.Value : 0.0f));
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
		if (!DesignImageBakeUtils::DecodeImageFileToBGRA8(SourceImagePath, Width, Height, SourceRawData, OutError))
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

		return DesignImageBakeUtils::SaveBGRA8ToPng(WorkingRawData, Width, Height, OutputPath, OutError);
	}

	bool TryGetForegroundBlurRadius(const TSharedPtr<FDesignNode>& Node, float& OutBlurRadius)
	{
		OutBlurRadius = 0.0f;
		if (!Node.IsValid() || Node->Children.Num() != 0)
		{
			return false;
		}

		for (const FDesignEffect& Effect : Node->Visual.Effects)
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
		for (const FDesignFill& Fill : Node->Visual.Fills)
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
			return false;
		}

		return true;
	}

	int32 BakeLayerBlurImages(
		TArray<TSharedPtr<FDesignNode>>& Nodes,
		TMap<FString, FString>& InOutImageRefToLocalPath,
		const FString& SourceDocumentIdentity)
	{
		int32 BakedCount = 0;
		const FString DerivedDir = DesignImageBakeUtils::BuildSourceScopedDerivedDir(SourceDocumentIdentity);
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
				for (int32 FillIndex = 0; FillIndex < Node->Visual.Fills.Num(); ++FillIndex)
				{
					FDesignFill& Fill = Node->Visual.Fills[FillIndex];
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

					const FString SourceImageIdentity = DesignImageBakeUtils::BuildSourceFileIdentity(*SourceImagePath);
					const FString DerivedKey = BuildLayerBlurKey(
						SourceDocumentIdentity,
						SourceImageIdentity,
						Fill.Image.Url,
						Node->Id,
						FillIndex,
						BlurRadius);
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
					Node->Visual.Effects.RemoveAll([](const FDesignEffect& Effect)
					{
						return Effect.Type == EDesignEffectType::Blur;
					});
					BakedCount += PendingBakes.Num();
				}
			}

			BakedCount += BakeLayerBlurImages(Node->Children, InOutImageRefToLocalPath, SourceDocumentIdentity);
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

			if (bIsVector && !Node->Visual.TopmostFill().IsImage() && NeedsVectorRasterFallback(Node))
			{
				FString VecKey;
				FString PngPath;
				if (TryResolveVectorPngPath(Node->Id, ImagesDir, VecKey, PngPath))
				{
					if (ShouldSkipTinyVectorPng(PngPath, Node->Transform.Width.Value, Node->Transform.Height.Value))
					{
						continue;
					}

					FDesignFill ImgFill;
					ImgFill.Type = EDesignFillType::Image;
					ImgFill.Image.Url = VecKey;
					ImgFill.Image.Mode = EDesignImageMode::Fill;
					Node->Visual.Fills.Empty();
					Node->Visual.Fills.Add(ImgFill);
					Node->Visual.bFillExplicitClear = false;
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
		OutDocument.Children.Add(RefExpander::DeepCopyNode(Match));
		return true;
	}

	void NormalizeSingleRootDocumentToOrigin(FDesignDocument& Document)
	{
		if (Document.Children.Num() != 1 || !Document.Children[0].IsValid())
		{
			return;
		}

		Document.Children[0]->Transform.X = 0.0f;
		Document.Children[0]->Transform.Y = 0.0f;
		Document.Children[0]->Transform.ExplicitX = 0.0f;
		Document.Children[0]->Transform.ExplicitY = 0.0f;
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

		TSharedPtr<FJsonObject> Root;
		if (DesignJsonUtils::TryParseObject(JsonStr, Root)
			&& Root->HasField(TEXT("document"))
			&& !Root->HasField(TEXT("design2umgComponentDependencies")))
		{
			// Standard REST files need the same dependency inventory as remote imports.
			// Never replace an exporter's existing bundle (including an invalid one).
			const FString FileKey = DesignJsonUtils::GetString(Root, TEXT("fileKey"));
			const FString Scope = FileKey.IsEmpty() ? FPaths::ConvertRelativePathToFull(JsonFilePath) : FileKey;
			FString Prepared;
			if (!FigmaComponentDependencies::PrepareRestFile(JsonStr, Scope, Prepared, OutError)) return false;
			JsonStr = MoveTemp(Prepared);
		}
		if (!FFigmaParser::ParseDocument(JsonStr, ParsedDocument))
		{
			OutError = FString::Printf(TEXT("Failed to parse Figma JSON: %s"), *JsonFilePath);
			return false;
		}
		ResolveDocumentImages(ParsedDocument, JsonFilePath);
		if (!FilterDocumentToTargetNode(ParsedDocument, TargetNodeId, OutDocument, OutError))
		{
			return false;
		}

		NormalizeSingleRootDocumentToOrigin(OutDocument);
		return true;
	}

	void ResolveDocumentImages(FDesignDocument& ParsedDocument, const FString& JsonFilePath)
	{
		const FString ResolvedSourceFilePath = FPaths::ConvertRelativePathToFull(JsonFilePath);
		ParsedDocument.MutableSourceDocument().SourceFilePath = ResolvedSourceFilePath;
		ParsedDocument.Metadata.Add(TEXT("design.sourceFilePath"), ResolvedSourceFilePath);
		ParsedDocument.Metadata.Add(TEXT("source.filePath"), ResolvedSourceFilePath);

		const FString ImagesDir = FPaths::GetPath(ResolvedSourceFilePath) / TEXT("images");
		const FString SourceDocumentIdentity = DesignImageBakeUtils::BuildSourceDocumentIdentity(JsonFilePath);
		TMap<FString, FString> ImageRefToLocalPath;
		const int32 VecCount = InjectVectorImageFills(ParsedDocument.Children, ImagesDir, ImageRefToLocalPath);

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
		const int32 BlendDerivedCount = DesignImageBakeUtils::BakeMappedImageBlendOverlays(
			ParsedDocument.Children,
			ImageRefToLocalPath,
			SourceDocumentIdentity,
			TEXT("LocalFigmaImport"));
		const int32 LayerBlurDerivedCount = BakeLayerBlurImages(ParsedDocument.Children, ImageRefToLocalPath, SourceDocumentIdentity);

		UE_LOG(LogDesign2UMG, Log, TEXT("LocalFigmaImport: %d image mappings (%d vec_*, %d image fills, %d baked blend overlays, %d baked layer blurs)"),
			ImageRefToLocalPath.Num(), VecCount, ImageRefToLocalPath.Num() - VecCount, BlendDerivedCount, LayerBlurDerivedCount);

		FFigmaParser::ApplyImageUrls(ParsedDocument, ImageRefToLocalPath);
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

		FImportAnalysisInput AnalysisInput;
		AnalysisInput.bResolveImages = true;
		const FDesign2UMGImportSettingsSnapshot SettingsSnapshot =
			FDesign2UMGImportSettingsSnapshot::FromSettings(UDesign2UMGSettings::Get());
		AnalysisInput.SettingsSnapshot = &SettingsSnapshot;
		const FImportAnalysisResult Analysis = DesignImportPipeline::AnalyzeImport(ParsedDoc, AnalysisInput);
		if (!Analysis.ErrorMessage.IsEmpty())
		{
			OutError = Analysis.ErrorMessage;
			return false;
		}
		OutData = Analysis.Data;

		if (OutData.RootWidget.Children.Num() == 0)
		{
			OutError = FString::Printf(TEXT("No frames found in export: %s"), *JsonFilePath);
			return false;
		}

		return true;
	}
}
