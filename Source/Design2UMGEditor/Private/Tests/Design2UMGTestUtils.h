// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "Design2UMGConverter.h"
#include "UMGWidgetNodeAccess.h"

#include "Engine/Texture2D.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TextureResource.h"

namespace Design2UMGTestUtils
{
	inline bool TryGetWidgetProperty(const FUMGWidgetNode& Node, const TCHAR* Key, FString& OutValue)
	{
		return Design2UMGWidgetNodeAccess::TryGetTypedWidgetString(Node, Key, OutValue);
	}

	inline FString GetWidgetProperty(const FUMGWidgetNode& Node, const TCHAR* Key)
	{
		FString Value;
		TryGetWidgetProperty(Node, Key, Value);
		return Value;
	}

	inline bool HasWidgetProperty(const FUMGWidgetNode& Node, const TCHAR* Key)
	{
		FString Value;
		return TryGetWidgetProperty(Node, Key, Value);
	}

	inline const FUMGWidgetNode* FindWidgetById(const FUMGWidgetNode& Node, const FString& TargetId)
	{
		if (Node.Id == TargetId)
		{
			return &Node;
		}
		for (const FUMGWidgetNode& Child : Node.Children)
		{
			if (const FUMGWidgetNode* Found = FindWidgetById(Child, TargetId))
			{
				return Found;
			}
		}
		return nullptr;
	}

	inline const FUMGWidgetNode* FindWidgetByProperty(
		const FUMGWidgetNode& Node,
		const FString& Key,
		const FString* OptionalValue = nullptr)
	{
		FString Current;
		if (Design2UMGWidgetNodeAccess::TryGetTypedWidgetString(Node, *Key, Current))
		{
			if (!OptionalValue || Current == *OptionalValue)
			{
				return &Node;
			}
		}
		for (const FUMGWidgetNode& Child : Node.Children)
		{
			if (const FUMGWidgetNode* Found = FindWidgetByProperty(Child, Key, OptionalValue))
			{
				return Found;
			}
		}
		return nullptr;
	}

	inline void CollectTextValues(const FUMGWidgetNode& Node, TArray<FString>& OutTexts)
	{
		const bool bIsTextWidget = Node.Type.Contains(TEXT("TextBlock"), ESearchCase::CaseSensitive);
		if (bIsTextWidget)
		{
			FString Text;
			if (TryGetWidgetProperty(Node, TEXT("Text"), Text))
			{
				if (Node.Type.Contains(TEXT("RichTextBlock"), ESearchCase::CaseSensitive))
				{
					FString PlainText;
					bool bInsideTag = false;
					for (const TCHAR Character : Text)
					{
						if (Character == TEXT('<'))
						{
							bInsideTag = true;
							continue;
						}
						if (Character == TEXT('>'))
						{
							bInsideTag = false;
							continue;
						}
						if (!bInsideTag)
						{
							PlainText.AppendChar(Character);
						}
					}
					Text = PlainText;
					Text.ReplaceInline(TEXT("&lt;"), TEXT("<"));
					Text.ReplaceInline(TEXT("&gt;"), TEXT(">"));
					Text.ReplaceInline(TEXT("&quot;"), TEXT("\""));
					Text.ReplaceInline(TEXT("&apos;"), TEXT("'"));
					Text.ReplaceInline(TEXT("&amp;"), TEXT("&"));
				}
				OutTexts.Add(Text);
			}
		}
		for (const FUMGWidgetNode& Child : Node.Children)
		{
			CollectTextValues(Child, OutTexts);
		}
	}

	inline FString GetFixturePath(const FString& FixtureFileName)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Design2UMG"));
		if (!Plugin.IsValid())
		{
			return FString();
		}

		return FPaths::Combine(
			Plugin->GetBaseDir(),
			TEXT("Source/Design2UMGEditor/Private/Tests/Fixtures"),
			FixtureFileName);
	}

	inline bool LoadFixtureJson(const FString& FixtureFileName, FString& OutJson)
	{
		return FFileHelper::LoadFileToString(OutJson, *GetFixturePath(FixtureFileName));
	}

	inline int32 CountOpaqueTexturePixels(UTexture2D* Texture)
	{
		if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.Num() <= 0)
		{
			return 0;
		}

		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		const void* LockedData = Mip.BulkData.LockReadOnly();
		if (!LockedData)
		{
			Mip.BulkData.Unlock();
			return 0;
		}

		const FColor* Pixels = static_cast<const FColor*>(LockedData);
		const int32 PixelCount = Mip.SizeX * Mip.SizeY;
		int32 OpaqueCount = 0;
		for (int32 Index = 0; Index < PixelCount; ++Index)
		{
			if (Pixels[Index].A > 0)
			{
				++OpaqueCount;
			}
		}

		Mip.BulkData.Unlock();
		return OpaqueCount;
	}
}
