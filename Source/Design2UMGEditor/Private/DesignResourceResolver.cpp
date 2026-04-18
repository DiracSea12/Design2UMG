// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "DesignResourceResolver.h"
#include "ProceduralShapeGenerator.h"
#include "Engine/Texture2D.h"

namespace DesignResourceResolver
{
	void ApplyFontFamily(FSlateFontInfo& FontInfo, const FString& Value, bool bIsIconFontWidget)
	{
		if (Value.StartsWith(TEXT("/")))
		{
			if (UObject* FontObject = LoadObject<UObject>(nullptr, *Value))
			{
				FontInfo.FontObject = FontObject;
			}
			return;
		}
		if (!Value.IsEmpty() && !bIsIconFontWidget)
		{
			FontInfo.TypefaceFontName = FName(*Value);
		}
	}

	UTexture2D* ResolveTextureFromMaterialRef(const FString& MaterialRef, const FUMGPropertyBag& Props, UObject* Outer)
	{
		return FProceduralShapeGenerator::GenerateFromMaterialRef(MaterialRef, Props, Outer);
	}

	UTexture2D* LoadTextureFromPath(const FString& Path, ELoadMode Mode)
	{
		UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *Path);
		if (!Tex && Mode == ELoadMode::LoadThenFind)
		{
			Tex = FindObject<UTexture2D>(nullptr, *Path);
		}
		return Tex;
	}
}
