// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "Design2UMGConverter.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace Design2UMGTestUtils
{
	inline bool TryGetWidgetProperty(const FUMGWidgetNode& Node, const TCHAR* Key, FString& OutValue)
	{
		return Node.Properties.TryGetString(Key, OutValue) && !OutValue.IsEmpty();
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
		if (Node.Properties.TryGetString(Key, Current) && !Current.IsEmpty())
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
		if (Node.Type == TEXT("TextBlock"))
		{
			FString Text;
			if (TryGetWidgetProperty(Node, TEXT("Text"), Text))
			{
				OutTexts.Add(Text);
			}
		}
		for (const FUMGWidgetNode& Child : Node.Children)
		{
			CollectTextValues(Child, OutTexts);
		}
	}

	inline bool LoadFixtureJson(const FString& FixtureFileName, FString& OutJson)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Design2UMG"));
		if (!Plugin.IsValid())
		{
			return false;
		}

		const FString FixturePath = FPaths::Combine(
			Plugin->GetBaseDir(),
			TEXT("Source/Design2UMGEditor/Private/Tests/Fixtures"),
			FixtureFileName);
		return FFileHelper::LoadFileToString(OutJson, *FixturePath);
	}
}
