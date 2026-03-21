#include "Design2UMGLocalFigmaImport.h"

#include "Design2UMGConverter.h"
#include "Design2UMGLog.h"
#include "DesignImportPipeline.h"
#include "FigmaParser.h"
#include "UMGSchema.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

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

			if (bIsVector && !Node->Fill.IsImage())
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
}

namespace Design2UMGLocalFigmaImport
{
	bool BuildLocalFigmaWidgetData(const FString& JsonFilePath, FUMGWidgetData& OutData, FString& OutError)
	{
		FString JsonStr;
		if (!FFileHelper::LoadFileToString(JsonStr, *JsonFilePath))
		{
			OutError = FString::Printf(TEXT("Failed to read design.json: %s"), *JsonFilePath);
			return false;
		}

		FDesignDocument ParsedDoc;
		if (!FFigmaParser::ParseDocument(JsonStr, ParsedDoc))
		{
			OutError = FString::Printf(TEXT("Failed to parse Figma JSON: %s"), *JsonFilePath);
			return false;
		}

		const FString ImagesDir = FPaths::GetPath(JsonFilePath) / TEXT("images");
		TMap<FString, FString> ImageRefToLocalPath;
		const int32 VecCount = InjectVectorImageFills(ParsedDoc.Children, ImagesDir, ImageRefToLocalPath);

		TSet<FString> ImageRefs;
		FFigmaParser::CollectImageRefs(ParsedDoc, ImageRefs);
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

		UE_LOG(LogDesign2UMG, Log, TEXT("LocalFigmaImport: %d image mappings (%d vec_*, %d image fills)"),
			ImageRefToLocalPath.Num(), VecCount, ImageRefToLocalPath.Num() - VecCount);

		FFigmaParser::ApplyImageUrls(ParsedDoc, ImageRefToLocalPath);

		OutData = FDesign2UMGConverter::Convert(ParsedDoc);
		DesignImportPipeline::ResolveImages(OutData);

		if (OutData.RootWidget.Children.Num() == 0)
		{
			OutError = FString::Printf(TEXT("No frames found in export: %s"), *JsonFilePath);
			return false;
		}

		return true;
	}
}
