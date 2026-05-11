// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

struct FDesignDocument;
struct FDesignNode;

namespace DesignImageBakeUtils
{
	FString BuildSourceDocumentIdentity(const FString& SourcePathOrIdentity);
	FString BuildSourceFileIdentity(const FString& FilePath);
	FString BuildSourceScopedDerivedDir(const FString& SourceDocumentIdentity);
	FString HashDerivedImageSignature(const FString& Prefix, const FString& Signature);

	bool DecodeImageFileToBGRA8(
		const FString& ImagePath,
		int32& OutWidth,
		int32& OutHeight,
		TArray64<uint8>& OutRawData,
		FString& OutError);

	bool SaveBGRA8ToPng(
		const TArray64<uint8>& RawData,
		int32 Width,
		int32 Height,
		const FString& OutputPath,
		FString& OutError);

	void ApplyImagePathMap(FDesignDocument& Document, const TMap<FString, FString>& ImageKeyToPath);
	FString ResolveDocumentImageBakeIdentity(const FDesignDocument& Document, const FString& ExplicitSourcePath = FString());

	int32 BakeMappedImageBlendOverlays(
		TArray<TSharedPtr<FDesignNode>>& Nodes,
		TMap<FString, FString>& InOutImageKeyToLocalPath,
		const FString& SourceDocumentIdentity,
		const TCHAR* LogContext);

	int32 BakeDocumentLocalImageBlendOverlays(
		FDesignDocument& Document,
		const FString& ExplicitSourcePath,
		const TCHAR* LogContext);
}
