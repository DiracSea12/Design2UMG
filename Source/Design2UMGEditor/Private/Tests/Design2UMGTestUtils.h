// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "Design2UMGConverter.h"
#include "UMGPropertyKeys.h"
#include "UMGSchema.h"
#include "UMGWidgetNodeAccess.h"

#include "Engine/Texture2D.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TextureResource.h"

namespace Design2UMGTestUtils
{
	// Test-only typed assertion vocabulary: reads widget payload fields directly
	// (no production property lookup layer) and renders them through the shared
	// write-side serializers so assertions keep comparing serialized values.
	namespace WidgetPropertyDetail
	{
		inline bool TryGetOptionalString(const TOptional<FString>& Value, FString& OutValue)
		{
			if (Value.IsSet())
			{
				OutValue = Value.GetValue();
				return true;
			}
			return false;
		}

		template <typename T, typename TFormat>
		bool TryGetOptionalFormatted(const TOptional<T>& Value, TFormat Format, FString& OutValue)
		{
			if (Value.IsSet())
			{
				OutValue = Format(Value.GetValue());
				return true;
			}
			return false;
		}
	}

	inline bool TryGetWidgetProperty(const FUMGWidgetNode& Node, const TCHAR* Key, FString& OutValue)
	{
		OutValue.Reset();
		using namespace Design2UMGWidgetNodeAccess;
		const FString KeyString(Key);
		if (KeyString == UMGPropertyKeys::AutoWrapText)
		{
			return WidgetPropertyDetail::TryGetOptionalFormatted(
				Node.Text.bAutoWrapText, [](bool Value) { return SerializeWidgetBool(Value); }, OutValue);
		}
		if (KeyString == UMGPropertyKeys::Clipping)
		{
			return WidgetPropertyDetail::TryGetOptionalString(Node.Render.Clipping, OutValue);
		}
		if (KeyString == UMGPropertyKeys::FontFamily)
		{
			return WidgetPropertyDetail::TryGetOptionalString(Node.Text.FontFamily, OutValue);
		}
		if (KeyString == UMGPropertyKeys::FontStyle)
		{
			return WidgetPropertyDetail::TryGetOptionalString(Node.Text.FontStyle, OutValue);
		}
		if (KeyString == UMGPropertyKeys::HeightOverride)
		{
			return WidgetPropertyDetail::TryGetOptionalFormatted(
				Node.WidgetStyle.HeightOverride, [](float Value) { return SerializeWidgetFloat(Value); }, OutValue);
		}
		if (KeyString == UMGPropertyKeys::Href)
		{
			return WidgetPropertyDetail::TryGetOptionalString(Node.Text.Href, OutValue);
		}
		if (KeyString == UMGPropertyKeys::IconName)
		{
			return WidgetPropertyDetail::TryGetOptionalString(Node.Text.IconName, OutValue);
		}
		if (KeyString == UMGPropertyKeys::InnerSlotPadding)
		{
			return WidgetPropertyDetail::TryGetOptionalFormatted(
				Node.WidgetStyle.InnerSlotPadding, [](const FVector2D& Value) { return SerializeWidgetCompactVector2D(Value); }, OutValue);
		}
		if (KeyString == UMGPropertyKeys::MaterialParams)
		{
			// Descriptor-identical precedence: gradient params first, then explicit params.
			if (Node.Brush.Gradient.HasAny())
			{
				OutValue = SerializeGradientMaterialParams(Node.Brush.Gradient);
				if (!OutValue.IsEmpty())
				{
					return true;
				}
			}
			return WidgetPropertyDetail::TryGetOptionalString(Node.Brush.MaterialParams, OutValue);
		}
		if (KeyString == UMGPropertyKeys::MaterialRef)
		{
			// Descriptor-identical precedence: gradient ref first, then explicit ref.
			if (Node.Brush.Gradient.HasAny())
			{
				OutValue = GetGradientMaterialRef(Node.Brush.Gradient);
				return true;
			}
			return WidgetPropertyDetail::TryGetOptionalString(Node.Brush.MaterialRef, OutValue);
		}
		if (KeyString == UMGPropertyKeys::MinDesiredHeight)
		{
			return WidgetPropertyDetail::TryGetOptionalFormatted(
				Node.WidgetStyle.MinDesiredHeight, [](float Value) { return SerializeWidgetFloat(Value); }, OutValue);
		}
		if (KeyString == UMGPropertyKeys::Orientation)
		{
			return WidgetPropertyDetail::TryGetOptionalString(Node.ScrollBox.Orientation, OutValue);
		}
		if (KeyString == UMGPropertyKeys::OutlineColor)
		{
			return WidgetPropertyDetail::TryGetOptionalFormatted(
				Node.Brush.OutlineColor, [](const FLinearColor& Value) { return SerializeWidgetColor(Value); }, OutValue);
		}
		if (KeyString == UMGPropertyKeys::Padding)
		{
			return WidgetPropertyDetail::TryGetOptionalFormatted(
				Node.WidgetStyle.Padding, [](const FMargin& Value) { return SerializeWidgetMargin(Value); }, OutValue);
		}
		if (KeyString == UMGPropertyKeys::PathFillRule)
		{
			return WidgetPropertyDetail::TryGetOptionalString(Node.Path.FillRule, OutValue);
		}
		if (KeyString == UMGPropertyKeys::PathGeometry)
		{
			return WidgetPropertyDetail::TryGetOptionalString(Node.Path.Geometry, OutValue);
		}
		if (KeyString == UMGPropertyKeys::PathStrokeCap)
		{
			return WidgetPropertyDetail::TryGetOptionalString(Node.Path.StrokeCap, OutValue);
		}
		if (KeyString == UMGPropertyKeys::PathStrokeColor)
		{
			return WidgetPropertyDetail::TryGetOptionalFormatted(
				Node.Path.StrokeColor, [](const FLinearColor& Value) { return SerializeWidgetColor(Value); }, OutValue);
		}
		if (KeyString == UMGPropertyKeys::PathStrokeJoin)
		{
			return WidgetPropertyDetail::TryGetOptionalString(Node.Path.StrokeJoin, OutValue);
		}
		if (KeyString == UMGPropertyKeys::PathStrokeThickness)
		{
			return WidgetPropertyDetail::TryGetOptionalFormatted(
				Node.Path.StrokeThickness, [](float Value) { return SerializeWidgetFloat(Value); }, OutValue);
		}
		if (KeyString == UMGPropertyKeys::RenderOpacity)
		{
			return WidgetPropertyDetail::TryGetOptionalFormatted(
				Node.Render.RenderOpacity, [](float Value) { return SerializeWidgetFloat(Value); }, OutValue);
		}
		if (KeyString == UMGPropertyKeys::RenderTransformPivot)
		{
			return WidgetPropertyDetail::TryGetOptionalFormatted(
				Node.Render.RenderTransformPivot, [](const FVector2D& Value) { return SerializeWidgetVector2D(Value); }, OutValue);
		}
		if (KeyString == UMGPropertyKeys::RenderTransformScale)
		{
			return WidgetPropertyDetail::TryGetOptionalFormatted(
				Node.Render.RenderTransformScale, [](const FVector2D& Value) { return SerializeWidgetVector2D(Value); }, OutValue);
		}
		if (KeyString == UMGPropertyKeys::RenderTranslation)
		{
			return WidgetPropertyDetail::TryGetOptionalFormatted(
				Node.Render.RenderTranslation, [](const FVector2D& Value) { return SerializeWidgetVector2D(Value); }, OutValue);
		}
		if (KeyString == UMGPropertyKeys::ScrollBarVisibility)
		{
			return WidgetPropertyDetail::TryGetOptionalString(Node.ScrollBox.ScrollBarVisibility, OutValue);
		}
		if (KeyString == UMGPropertyKeys::Stretch)
		{
			return WidgetPropertyDetail::TryGetOptionalString(Node.WidgetStyle.Stretch, OutValue);
		}
		if (KeyString == UMGPropertyKeys::StrokePadding)
		{
			return WidgetPropertyDetail::TryGetOptionalFormatted(
				Node.WidgetStyle.StrokePadding, [](const FMargin& Value) { return SerializeWidgetMargin(Value); }, OutValue);
		}
		if (KeyString == UMGPropertyKeys::Text)
		{
			return WidgetPropertyDetail::TryGetOptionalString(Node.Text.Text, OutValue);
		}
		if (KeyString == UMGPropertyKeys::TextAlignVertical)
		{
			return WidgetPropertyDetail::TryGetOptionalString(Node.Text.TextAlignVertical, OutValue);
		}
		if (KeyString == UMGPropertyKeys::TexturePath)
		{
			return WidgetPropertyDetail::TryGetOptionalString(Node.Brush.Image.TexturePath, OutValue);
		}
		if (KeyString == UMGPropertyKeys::ToolTipText)
		{
			return WidgetPropertyDetail::TryGetOptionalString(Node.WidgetStyle.ToolTipText, OutValue);
		}
		if (KeyString == UMGPropertyKeys::TypefaceFontName)
		{
			return WidgetPropertyDetail::TryGetOptionalString(Node.Text.TypefaceFontName, OutValue);
		}
		if (KeyString == UMGPropertyKeys::Underline)
		{
			return WidgetPropertyDetail::TryGetOptionalFormatted(
				Node.Text.bUnderline, [](bool Value) { return SerializeWidgetBool(Value); }, OutValue);
		}
		if (KeyString == UMGPropertyKeys::WidthOverride)
		{
			return WidgetPropertyDetail::TryGetOptionalFormatted(
				Node.WidgetStyle.WidthOverride, [](float Value) { return SerializeWidgetFloat(Value); }, OutValue);
		}
		if (KeyString == UMGPropertyKeys::WrapTextAt)
		{
			return WidgetPropertyDetail::TryGetOptionalFormatted(
				Node.Text.WrapTextAt, [](float Value) { return SerializeWidgetFloat(Value); }, OutValue);
		}
		return false;
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
		if (TryGetWidgetProperty(Node, *Key, Current))
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
