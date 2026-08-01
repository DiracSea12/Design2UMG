// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "PenParser.h"
#include "Design2UMGConverter.h"
#include "Design2UMGSettings.h"
#include "Design2UMGTestUtils.h"
#include "FontResolver.h"
#include "UMGLayoutProjection.h"
#include "UMGEditorPropertyKeys.h"
#include "UMGJsonParser.h"
#include "UMGJsonSerializer.h"
#include "UMGPropertyKeys.h"
#include "UMGSchemaSourceExtensions.h"
#include "UMGTextStyleApplication.h"
#include "UMGValueParsing.h"
#include "UMGWidgetNodeAccess.h"
#include "UMGWidgetNodeProceduralResourceAccess.h"
#include "UMGWidgetUtils.h"
#include "FigmaParser.h"
#include "DesignPathRasterizer.h"
#include "ProceduralShapeGenerator.h"
#include "Design2UMGLog.h"
#include "Dom/JsonObject.h"
#include "Framework/Application/SlateApplication.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RHI.h"
#include "Components/EditableText.h"
#include "Components/RetainerBox.h"
#include "Components/EditableTextBox.h"
#include "Components/MultiLineEditableText.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/RichTextBlock.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/DataTable.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "UObject/UObjectGlobals.h"
#if WITH_AUTOMATION_WORKER
using namespace Design2UMGTestUtils;
namespace
{
	FUMGProceduralResourceRequest BuildProceduralRequestForTest(FAutomationTestBase& Test, const FUMGWidgetNode& Node)
	{
		FUMGProceduralResourceRequest Request;
		Test.TestTrue(
			TEXT("Typed procedural resource request builds"),
			Design2UMGWidgetNodeProceduralResourceAccess::TryBuildProceduralResourceRequest(Node, Request));
		return Request;
	}

	struct FLuminosityOpacityCapScope
	{
		explicit FLuminosityOpacityCapScope(const float NewValue, const bool bEnableOverride = true)
		{
			Settings = UDesign2UMGSettings::Get();
			if (Settings)
			{
				bSavedUseCustomOverride = Settings->bUseCustomLuminosityFitOpacityCap;
				SavedValue = Settings->LuminosityFitOpacityCap;
				Settings->bUseCustomLuminosityFitOpacityCap = bEnableOverride;
				Settings->LuminosityFitOpacityCap = NewValue;
			}
		}

		~FLuminosityOpacityCapScope()
		{
			if (Settings)
			{
				Settings->bUseCustomLuminosityFitOpacityCap = bSavedUseCustomOverride;
				Settings->LuminosityFitOpacityCap = SavedValue;
			}
		}

		UDesign2UMGSettings* Settings = nullptr;
		bool bSavedUseCustomOverride = false;
		float SavedValue = 0.15f;
	};

	struct FScopedPlaceholderHatchStyleOverride
	{
		FScopedPlaceholderHatchStyleOverride(
			const bool bEnableOverride,
			const FString& InBaseBg,
			const FString& InBaseStripe,
			const FString& InRefBg,
			const FString& InRefStripe,
			const int32 InStripeWidth = 1,
			const int32 InStripeGap = 4)
		{
			Settings = UDesign2UMGSettings::Get();
			if (Settings)
			{
				bSavedUseCustomOverride = Settings->bUseCustomPlaceholderHatchStyle;
				SavedBaseBg = Settings->PlaceholderHatchBackgroundColor;
				SavedBaseStripe = Settings->PlaceholderHatchStripeColor;
				SavedRefBg = Settings->PlaceholderRefHatchBackgroundColor;
				SavedRefStripe = Settings->PlaceholderRefHatchStripeColor;
				SavedStripeWidth = Settings->PlaceholderHatchStripeWidth;
				SavedStripeGap = Settings->PlaceholderHatchStripeGap;

				Settings->bUseCustomPlaceholderHatchStyle = bEnableOverride;
				Settings->PlaceholderHatchBackgroundColor = InBaseBg;
				Settings->PlaceholderHatchStripeColor = InBaseStripe;
				Settings->PlaceholderRefHatchBackgroundColor = InRefBg;
				Settings->PlaceholderRefHatchStripeColor = InRefStripe;
				Settings->PlaceholderHatchStripeWidth = InStripeWidth;
				Settings->PlaceholderHatchStripeGap = InStripeGap;
			}
		}

		~FScopedPlaceholderHatchStyleOverride()
		{
			if (Settings)
			{
				Settings->bUseCustomPlaceholderHatchStyle = bSavedUseCustomOverride;
				Settings->PlaceholderHatchBackgroundColor = SavedBaseBg;
				Settings->PlaceholderHatchStripeColor = SavedBaseStripe;
				Settings->PlaceholderRefHatchBackgroundColor = SavedRefBg;
				Settings->PlaceholderRefHatchStripeColor = SavedRefStripe;
				Settings->PlaceholderHatchStripeWidth = SavedStripeWidth;
				Settings->PlaceholderHatchStripeGap = SavedStripeGap;
			}
		}

		UDesign2UMGSettings* Settings = nullptr;
		bool bSavedUseCustomOverride = false;
		FString SavedBaseBg;
		FString SavedBaseStripe;
		FString SavedRefBg;
		FString SavedRefStripe;
		int32 SavedStripeWidth = 1;
		int32 SavedStripeGap = 4;
	};

	int32 CountOpaqueTexturePixels(UTexture2D* Texture)
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

	bool ReadTexturePixel(UTexture2D* Texture, int32 X, int32 Y, FColor& OutPixel)
	{
		if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.Num() <= 0)
		{
			return false;
		}

		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		if (X < 0 || Y < 0 || X >= Mip.SizeX || Y >= Mip.SizeY)
		{
			return false;
		}

		const void* LockedData = Mip.BulkData.LockReadOnly();
		if (!LockedData)
		{
			Mip.BulkData.Unlock();
			return false;
		}

		const FColor* Pixels = static_cast<const FColor*>(LockedData);
		OutPixel = Pixels[Y * Mip.SizeX + X];
		Mip.BulkData.Unlock();
		return true;
	}

}

#include "Design2UMGTests.TypedSchemaJsonContract.inl"

#include "Design2UMGTests.ParserConverter.inl"

#include "Design2UMGTests.FigmaBoundsText.inl"

#include "Design2UMGTests.FontTextBehavior.inl"

#include "Design2UMGTests.PenExtended.inl"

#include "Design2UMGTests.EffectsAndShadow.inl"

#include "Design2UMGTests.FigmaCoverage.inl"

#endif // WITH_AUTOMATION_WORKER
