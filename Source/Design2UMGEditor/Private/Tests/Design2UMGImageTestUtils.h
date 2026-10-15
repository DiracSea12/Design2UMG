// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "DesignImageResourceLimits.h"
#include "DesignRemoteImageImporterInternal.h"
#include "ImageCore.h"

namespace Design2UMGImageTestUtils
{
	inline bool DecodeToBGRA8(const TArray<uint8>& CompressedData, FImage& OutImage)
	{
		int32 Width = 0;
		int32 Height = 0;
		TArray64<uint8> RawData;
		FString Error;
		if (!DesignRemoteImageImporterInternal::FImagePayloadIO::DecodeToBGRA8(
			CompressedData,
			DesignImageResources::GetGeneratedTextureLimits(),
			Width,
			Height,
			RawData,
			Error))
		{
			return false;
		}

		OutImage = FImage(Width, Height, 1, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
		OutImage.RawData = MoveTemp(RawData);
		return true;
	}

	inline bool DecodeToBGRA8(const uint8* Data, const int64 NumBytes, FImage& OutImage)
	{
		if (!Data || NumBytes <= 0 || NumBytes > MAX_int32) return false;
		TArray<uint8> CompressedData;
		CompressedData.Append(Data, static_cast<int32>(NumBytes));
		return DecodeToBGRA8(CompressedData, OutImage);
	}
}
