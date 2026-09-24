// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "FigmaImageExporter.h"
#include "FigmaParser.h"
#include "FigmaHttpClient.h"
#include "Design2UMGLog.h"
#include "DesignJsonUtils.h"
#include "DesignSchema.h"

namespace
{
	// One ref can have several consumers: a required bitmap use always wins over
	// another node's ability to draw its own vector instead.
	void CollectRequiredImageRefs(const TArray<TSharedPtr<FDesignNode>>& Nodes, TSet<FString>& OutRefs)
	{
		for (const TSharedPtr<FDesignNode>& Node : Nodes)
		{
			if (!Node.IsValid()) continue;
			const FDesignStroke& Stroke = Node->Visual.Stroke;
			bool bHasVectorPaint = Stroke.HasStroke() && Stroke.Fill.IsSet()
				&& Stroke.Fill->Type == EDesignFillType::Color && !Stroke.Fill->SolidColor.IsEmpty();
			for (const FDesignFill& Fill : Node->Visual.Fills)
			{
				if (Fill.IsImage() && !Fill.Image.Url.IsEmpty()) OutRefs.Add(Fill.Image.Url);
				bHasVectorPaint |= (Fill.Type == EDesignFillType::Color && !Fill.SolidColor.IsEmpty())
					|| (Fill.Type == EDesignFillType::Gradient && Fill.Gradient.Colors.Num() > 0
						&& !Fill.Gradient.Colors[0].Color.IsEmpty());
			}
			if (Node->Shape.Geometry.TrimStartAndEnd().IsEmpty() || !bHasVectorPaint)
			{
				if (!Node->Visual.VectorRasterFallbackImageRef.IsEmpty()) OutRefs.Add(Node->Visual.VectorRasterFallbackImageRef);
				if (!Node->Visual.RenderFallback.ImageRef.IsEmpty()) OutRefs.Add(Node->Visual.RenderFallback.ImageRef);
			}
			CollectRequiredImageRefs(Node->Children, OutRefs);
		}
	}

	void ClearSubstitutedImageRefs(const TArray<TSharedPtr<FDesignNode>>& Nodes, const TArray<FString>& MissingRefs)
	{
		for (const TSharedPtr<FDesignNode>& Node : Nodes)
		{
			if (!Node.IsValid()) continue;
			if (MissingRefs.Contains(Node->Visual.VectorRasterFallbackImageRef)) Node->Visual.VectorRasterFallbackImageRef.Reset();
			if (MissingRefs.Contains(Node->Visual.RenderFallback.ImageRef)) Node->Visual.RenderFallback.ImageRef.Reset();
			ClearSubstitutedImageRefs(Node->Children, MissingRefs);
		}
	}
}

void FFigmaImageExporter::ResolveImageRefs(
	FFigmaHttpClient& Client,
	const FString& FileKey,
	TSharedRef<FDesignDocument> Document,
	FOnComplete OnComplete)
{
	TSet<FString> ImageRefs;
	FFigmaParser::CollectImageRefs(*Document, ImageRefs);

	if (ImageRefs.Num() == 0)
	{
		UE_LOG(LogDesign2UMG, Log, TEXT("FigmaImageExporter: No image refs to resolve"));
		if (OnComplete) OnComplete(true);
		return;
	}

	UE_LOG(LogDesign2UMG, Log, TEXT("FigmaImageExporter: Resolving %d image refs"), ImageRefs.Num());

	// Figma GET /v1/files/:key/images returns { "meta": { "images": { "imageRef": "url", ... } } }.
	Client.GetFileImages(FileKey,
		[Document, ImageRefs, OnComplete](bool bSuccess, const FString& Body)
		{
			if (!bSuccess)
			{
				UE_LOG(LogDesign2UMG, Error, TEXT("FigmaImageExporter: GetFileImages failed"));
				if (OnComplete) OnComplete(false);
				return;
			}

			TSharedPtr<FJsonObject> Root;
			if (!DesignJsonUtils::TryParseObject(Body, Root))
			{
				UE_LOG(LogDesign2UMG, Error, TEXT("FigmaImageExporter: Failed to parse images response"));
				if (OnComplete) OnComplete(false);
				return;
			}

			const TSharedPtr<FJsonObject>* MetaObj = nullptr;
			if (!Root->TryGetObjectField(TEXT("meta"), MetaObj) || !MetaObj || !(*MetaObj).IsValid())
			{
				UE_LOG(LogDesign2UMG, Error, TEXT("FigmaImageExporter: No 'meta' in images response"));
				if (OnComplete) OnComplete(false);
				return;
			}

			const TSharedPtr<FJsonObject>* ImagesObj = nullptr;
			if (!(*MetaObj)->TryGetObjectField(TEXT("images"), ImagesObj) || !ImagesObj || !(*ImagesObj).IsValid())
			{
				// Requested references require an images object in the export response.
				UE_LOG(LogDesign2UMG, Error, TEXT("FigmaImageExporter: No 'images' in meta; required image refs could not be resolved"));
				if (OnComplete) OnComplete(false);
				return;
			}

			TMap<FString, FString> RefToUrl;
			for (const auto& Pair : (*ImagesObj)->Values)
			{
				FString Url;
				if (Pair.Value->TryGetString(Url) && !Url.IsEmpty())
				{
					RefToUrl.Add(FString(*Pair.Key), Url);
				}
			}

			UE_LOG(LogDesign2UMG, Log, TEXT("FigmaImageExporter: Resolved %d image URLs"), RefToUrl.Num());
			TSet<FString> RequiredRefs;
			CollectRequiredImageRefs(Document->Children, RequiredRefs);
			FFigmaParser::ApplyImageUrls(*Document, RefToUrl);

			// Verify every requested reference has a URL or an explicit vector/raster substitute.
			// An unresolved bitmap-only fill is a resource failure.

			// Split by outcome, not by a shared "missing" list: a substitutable ref falls
			// back to vector rendering, a required one is missing for good. Logging both
			// messages for the same ref would contradict itself.
			TArray<FString> SubstitutableMissingRefs;
			TArray<FString> RequiredMissingRefs;
			for (const FString& Ref : ImageRefs)
			{
				if (RefToUrl.Contains(Ref))
				{
					continue;
				}
				if (!RequiredRefs.Contains(Ref))
				{
					SubstitutableMissingRefs.Add(Ref);
				}
				else
				{
					RequiredMissingRefs.Add(Ref);
				}
			}
			if (SubstitutableMissingRefs.Num() > 0)
			{
				ClearSubstitutedImageRefs(Document->Children, SubstitutableMissingRefs);
				UE_LOG(LogDesign2UMG, Warning,
					TEXT("FigmaImageExporter: %d requested image ref(s) not resolved, will fall back to vector: %s"),
					SubstitutableMissingRefs.Num(), *FString::Join(SubstitutableMissingRefs, TEXT(", ")));
			}
			if (RequiredMissingRefs.Num() > 0)
			{
				UE_LOG(LogDesign2UMG, Error,
					TEXT("FigmaImageExporter: %d required image ref(s) not resolved and no substitute is available: %s"),
					RequiredMissingRefs.Num(), *FString::Join(RequiredMissingRefs, TEXT(", ")));
				if (OnComplete) OnComplete(false);
				return;
			}

			if (OnComplete) OnComplete(true);
		});
}

// ---------------------------------------------------------------------------
// ExportVectorNodes
// ---------------------------------------------------------------------------

static void CollectVectorNodeIds(const TArray<TSharedPtr<FDesignNode>>& Nodes, TArray<FString>& OutIds)
{
	for (const auto& Node : Nodes)
	{
		if (!Node) continue;
		if (Node->Type == EDesignNodeType::Path && !Node->Id.IsEmpty())
			OutIds.Add(Node->Id);
		CollectVectorNodeIds(Node->Children, OutIds);
	}
}

void FFigmaImageExporter::ApplyExportedImages(TArray<TSharedPtr<FDesignNode>>& Nodes, const TMap<FString, FString>& IdToUrl)
{
	for (auto& Node : Nodes)
	{
		if (!Node) continue;
		if (Node->Type == EDesignNodeType::Path)
		{
			if (const FString* Url = IdToUrl.Find(Node->Id))
			{
				Node->Type = EDesignNodeType::Rectangle;
				FDesignFill Fill;
				Fill.Type = EDesignFillType::Image;
				Fill.Image.Url = *Url;
				Node->Visual.Fills.Empty();
				Node->Visual.Fills.Add(MoveTemp(Fill));
				Node->Visual.bFillExplicitClear = false;
			}
		}
		ApplyExportedImages(Node->Children, IdToUrl);
	}
}

void FFigmaImageExporter::ExportVectorNodes(
	FFigmaHttpClient& Client,
	const FString& FileKey,
	TSharedRef<FDesignDocument> Document,
	FOnVectorExportComplete OnComplete)
{
	TArray<FString> VectorIds;
	CollectVectorNodeIds(Document->Children, VectorIds);

	if (VectorIds.Num() == 0)
	{
		if (OnComplete) OnComplete(true, {});
		return;
	}

	UE_LOG(LogDesign2UMG, Log, TEXT("FigmaImageExporter: Exporting %d vector nodes as PNG"), VectorIds.Num());

	Client.ExportImages(FileKey, VectorIds,
		[OnComplete](bool bSuccess, const FString& Body)
		{
			if (!bSuccess)
			{
				UE_LOG(LogDesign2UMG, Warning, TEXT("FigmaImageExporter: ExportImages failed"));
				if (OnComplete) OnComplete(false, {});
				return;
			}

			TSharedPtr<FJsonObject> Root;
			if (!DesignJsonUtils::TryParseObject(Body, Root))
			{
				UE_LOG(LogDesign2UMG, Warning, TEXT("FigmaImageExporter: Failed to parse export response"));
				if (OnComplete) OnComplete(false, {});
				return;
			}

			const TSharedPtr<FJsonObject>* ImagesObj = nullptr;
			if (!Root->TryGetObjectField(TEXT("images"), ImagesObj) || !ImagesObj || !(*ImagesObj).IsValid())
			{
				UE_LOG(LogDesign2UMG, Warning, TEXT("FigmaImageExporter: No 'images' in export response"));
				if (OnComplete) OnComplete(true, {});
				return;
			}

			TMap<FString, FString> IdToUrl;
			for (const auto& Pair : (*ImagesObj)->Values)
			{
				FString Url;
				if (Pair.Value->TryGetString(Url) && !Url.IsEmpty())
					IdToUrl.Add(FString(*Pair.Key), Url);
			}

			UE_LOG(LogDesign2UMG, Log, TEXT("FigmaImageExporter: Exported %d vector nodes"), IdToUrl.Num());
			if (OnComplete) OnComplete(true, IdToUrl);
		});
}
