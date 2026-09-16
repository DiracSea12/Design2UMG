// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "TextMeasurementRuntimeProbe.h"

#include "Components/TextBlock.h"
#include "Engine/Font.h"
#include "Fonts/CompositeFont.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Styling/SlateTypes.h"
#include "TextMeasurement.h"
#include "TextFontResolution.h"
#include "TextMeasurementInternal.h"
#include "TextStyleUtils.h"
#include "UObject/StrongObjectPtr.h"

namespace TextMeasurementRuntimeProbe
{
	FSlateFontInfo BuildFontInfo(const FTextMeasureRequest& Request)
	{
		FSlateFontInfo FontInfo = FTextBlockStyle::GetDefault().Font;
		if (FMath::IsFinite(Request.FontSize) && Request.FontSize > 0.0f)
		{
			FontInfo.Size = Request.FontSize;
		}

		if (!Request.FontAssetPath.IsEmpty())
		{
			if (UObject* FontObject = LoadObject<UObject>(nullptr, *Request.FontAssetPath))
			{
				FontInfo.FontObject = FontObject;
			}
		}

		if (!Request.TypefaceName.IsEmpty())
		{
			FontInfo.TypefaceFontName = FName(*Request.TypefaceName);
		}

		FontInfo.LetterSpacing = Request.LetterSpacing;
		return FontInfo;
	}
}

namespace
{
	bool IsFinitePositive(const float Value)
	{
		return FMath::IsFinite(Value) && Value > 0.0f;
	}

	uint32 ReadTableTag(const TArray<uint8>& Data, const int32 Pos)
	{
		return (static_cast<uint32>(Data[Pos]) << 24)
			| (static_cast<uint32>(Data[Pos + 1]) << 16)
			| (static_cast<uint32>(Data[Pos + 2]) << 8)
			| static_cast<uint32>(Data[Pos + 3]);
	}

	int32 ReadUInt16(const TArray<uint8>& Data, const int32 Pos)
	{
		return (static_cast<int32>(Data[Pos]) << 8) | static_cast<int32>(Data[Pos + 1]);
	}

	bool FindTableOffset(const TArray<uint8>& Data, const uint32 Tag, int32& OutOffset)
	{
		// TrueType sfnt layout: 12-byte offset table header, then 16-byte table records;
		// the table count lives at byte offset 4. 1024 tables is a sanity cap against corrupt data.
		constexpr int32 SfntHeaderSize = 12;
		constexpr int32 SfntTableRecordSize = 16;
		constexpr int32 MaxSfntTables = 1024;

		if (Data.Num() < SfntHeaderSize)
		{
			return false;
		}

		const int32 NumTables = ReadUInt16(Data, 4);
		if (NumTables <= 0 || NumTables > MaxSfntTables || SfntHeaderSize + NumTables * SfntTableRecordSize > Data.Num())
		{
			return false;
		}

		for (int32 Index = 0; Index < NumTables; ++Index)
		{
			const int32 Record = SfntHeaderSize + Index * SfntTableRecordSize;
			if (ReadTableTag(Data, Record) == Tag)
			{
				const int32 Offset = (ReadUInt16(Data, Record + 8) << 16) | ReadUInt16(Data, Record + 10);
				if (Offset < 0 || Offset + 2 > Data.Num())
				{
					return false;
				}
				OutOffset = Offset;
				return true;
			}
		}
		return false;
	}

	/** Parses unitsPerEm (head) and lineGap (hhea) out of a raw sfnt table set. */
	bool TryParseHheaLineGapEm(const TArray<uint8>& Data, float& OutLineGapEm)
	{
		int32 HeadOffset = INDEX_NONE;
		int32 HheaOffset = INDEX_NONE;
		if (!FindTableOffset(Data, 0x68656164 /* head */, HeadOffset)
			|| !FindTableOffset(Data, 0x68686561 /* hhea */, HheaOffset))
		{
			return false;
		}

		if (HeadOffset + 20 > Data.Num() || HheaOffset + 10 > Data.Num())
		{
			return false;
		}

		const int32 UnitsPerEm = ReadUInt16(Data, HeadOffset + 18);
		if (UnitsPerEm <= 0)
		{
			return false;
		}

		const int16 LineGap = static_cast<int16>(ReadUInt16(Data, HheaOffset + 8));
		OutLineGapEm = static_cast<float>(LineGap) / static_cast<float>(UnitsPerEm);
		return true;
	}

	ETextJustify::Type ToSlateJustification(const EDesignTextAlign TextAlign)
	{
		switch (TextAlign)
		{
		case EDesignTextAlign::Center: return ETextJustify::Center;
		case EDesignTextAlign::Right: return ETextJustify::Right;
		default: return ETextJustify::Left;
		}
	}

	TextMeasurementRuntimeProbe::FTextMeasureRequest BuildTextMeasureRequest(
		const FString& Text,
		const float FontSize,
		const FString& FontFamily,
		const FString& FontPostScriptName,
		const FString& FontWeight,
		const FString& FontStyle,
		const int32 LetterSpacing,
		const FDesign2UMGFontMappings* FontMappings)
	{
		const TextFontResolution::FResolvedFontSelection ResolvedFont =
			TextFontResolution::Resolve(FontFamily, FontPostScriptName, FontWeight, FontStyle, FontMappings);
		TextMeasurementRuntimeProbe::FTextMeasureRequest Request;
		Request.Text = Text;
		Request.FontSize = FontSize;
		Request.FontAssetPath = ResolvedFont.FontAssetPath;
		Request.TypefaceName = ResolvedFont.TypefaceName;
		Request.LetterSpacing = LetterSpacing;
		Request.bRequiresResolvedFontAsset = ResolvedFont.bRequiresResolvedFontAsset;
		return Request;
	}

	TextMeasurementRuntimeProbe::FTextBlockMeasureRequest BuildTextBlockMeasureRequest(
		const TSharedPtr<FDesignNode>& Node,
		const float FontSize,
		const FString& Text,
		const float WrapTextAt,
		const bool bAutoWrapText,
		const TOptional<float> LineHeightPercentageOverride,
		const FDesign2UMGFontMappings* FontMappings)
	{
		TextMeasurementRuntimeProbe::FTextBlockMeasureRequest Request;
		if (!Node.IsValid())
		{
			return Request;
		}

		const TextFontResolution::FResolvedFontSelection ResolvedFont = TextFontResolution::Resolve(
			Node->Text.FontFamily,
			Node->Text.FontPostScriptName,
			Node->Text.FontWeight,
			Node->Text.FontStyle, FontMappings);
		Request.Text = Text;
		Request.FontSize = FontSize;
		Request.FontAssetPath = ResolvedFont.FontAssetPath;
		Request.TypefaceName = ResolvedFont.TypefaceName;
		Request.bRequiresResolvedFontAsset = ResolvedFont.bRequiresResolvedFontAsset;
		Request.LetterSpacing = (Node->Text.LetterSpacing.IsSet() && Node->Text.LetterSpacing.GetValue() != 0.0f && FontSize > 0.0f)
			? TextStyleUtils::ConvertLetterSpacingPxToTracking(Node->Text.LetterSpacing.GetValue(), FontSize)
			: 0;
		Request.TextAlign = Node->Text.TextAlign;
		Request.bApplyExplicitLineHeight = LineHeightPercentageOverride.IsSet();
		Request.LineHeightPercentage = LineHeightPercentageOverride.Get(1.0f);
		Request.bAutoWrapText = bAutoWrapText;
		Request.WrapTextAt = WrapTextAt;
		return Request;
	}

	/**
	 * Module-owned probe lifecycle: the rooted UTextBlock is lazily created on first
	 * measurement and explicitly released by FDesign2UMGCoreModule on shutdown, so the
	 * GC root never outlives the module (hot-reload included).
	 */
	struct FTextBlockProbeOwner
	{
		TStrongObjectPtr<UTextBlock> ProbeTextBlock;

		UTextBlock& Acquire()
		{
			if (!ProbeTextBlock.IsValid())
			{
				ProbeTextBlock = TStrongObjectPtr<UTextBlock>(NewObject<UTextBlock>());
				// Source layout measures text, not UTextBlock's invisible default shadow extent.
				ProbeTextBlock->SetShadowOffset(FVector2D::ZeroVector);
			}
			return *ProbeTextBlock;
		}

		void ResetForModuleShutdown()
		{
			ProbeTextBlock.Reset();
		}
	};

	FTextBlockProbeOwner GTextBlockProbe;

	/**
	 * Cache contract: entries stay valid until the backing font asset is reimported
	 * (FDesign2UMGCoreModule clears the whole map on any UFont reimport) or the module
	 * shuts down; both paths repopulate lazily on the next read.
	 */
	TMap<FString, TOptional<float>> GLineGapEmCache;

}

namespace RuntimeProbe
{
FVector2D MeasureTextSize(
	const FString& Text,
	float FontSize,
	const FString& FontFamily,
	const FString& FontPostScriptName,
	const FString& FontWeight,
	const FString& FontStyle,
	int32 LetterSpacing,
	const FDesign2UMGFontMappings* FontMappings)
{
	if (!IsFinitePositive(FontSize))
	{
		return FVector2D::ZeroVector;
	}

	const FString RuntimeText = TextMeasurement::PrepareRuntimeTextContent(Text);
	FVector2D MeasuredSize = FVector2D::ZeroVector;
	const TextMeasurementRuntimeProbe::FTextMeasureRequest Request =
		BuildTextMeasureRequest(RuntimeText, FontSize, FontFamily, FontPostScriptName, FontWeight, FontStyle, LetterSpacing, FontMappings);
	if (TextMeasurementRuntimeProbe::TryMeasureText(Request, MeasuredSize))
	{
		return MeasuredSize;
	}

	return FVector2D::ZeroVector;
}

FVector2D MeasureUMGTextBlockDesiredSize(
	const TSharedPtr<FDesignNode>& Node,
	float FontSize,
	const FString& Text,
	float WrapTextAt,
	bool bAutoWrapText,
	TOptional<float> LineHeightPercentageOverride,
	const FDesign2UMGFontMappings* FontMappings)
{
	const FString RuntimeText = TextMeasurement::PrepareRuntimeTextContent(Text);
	if (!Node.IsValid() || !IsFinitePositive(FontSize))
	{
		return FVector2D::ZeroVector;
	}

	FVector2D MeasuredSize = FVector2D::ZeroVector;
	return TextMeasurementRuntimeProbe::TryMeasureTextBlockDesiredSize(
		BuildTextBlockMeasureRequest(
			Node,
			FontSize,
			RuntimeText,
			WrapTextAt,
			bAutoWrapText,
			LineHeightPercentageOverride, FontMappings),
		MeasuredSize)
		? MeasuredSize
		: FVector2D::ZeroVector;
}

}

bool TextMeasurementRuntimeProbe::IsAvailable()
{
	return FSlateApplication::IsInitialized() && FSlateApplication::Get().GetRenderer() != nullptr;
}

bool TextMeasurementRuntimeProbe::TryMeasureText(const FTextMeasureRequest& Request, FVector2D& OutSize)
{
	OutSize = FVector2D::ZeroVector;
	if (!IsFinitePositive(Request.FontSize) || !IsAvailable())
	{
		return false;
	}
	if (Request.bRequiresResolvedFontAsset && Request.FontAssetPath.IsEmpty())
	{
		return false;
	}

	TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FVector2D MeasuredSize = FontMeasure->Measure(Request.Text, BuildFontInfo(Request));
	if (!IsFinitePositive(MeasuredSize.X) || !IsFinitePositive(MeasuredSize.Y))
	{
		return false;
	}

	OutSize = MeasuredSize;
	return true;
}

bool TextMeasurementRuntimeProbe::TryMeasureTextBlockDesiredSize(const FTextBlockMeasureRequest& Request, FVector2D& OutSize)
{
	OutSize = FVector2D::ZeroVector;
	if (!IsFinitePositive(Request.FontSize) || !IsAvailable())
	{
		return false;
	}
	if (Request.bRequiresResolvedFontAsset && Request.FontAssetPath.IsEmpty())
	{
		return false;
	}

	// Reuse one rooted probe across measurements: every input the measurement reads
	// (text, font, justification, line-height, wrap settings) is reassigned below and
	// each STextBlock setter invalidates the cached text layout, so the desired size
	// depends only on the current request instead of probe history.
	UTextBlock& ProbeTextBlock = GTextBlockProbe.Acquire();

	ProbeTextBlock.SetText(FText::FromString(Request.Text));
	ProbeTextBlock.SetFont(BuildFontInfo(Request));
	ProbeTextBlock.SetJustification(ToSlateJustification(Request.TextAlign));
	ProbeTextBlock.SetLineHeightPercentage(Request.LineHeightPercentage);
	ProbeTextBlock.SetApplyLineHeightToBottomLine(Request.bApplyExplicitLineHeight);
	ProbeTextBlock.SetAutoWrapText(Request.bAutoWrapText);
	ProbeTextBlock.SetWrapTextAt(Request.WrapTextAt);

	TSharedRef<SWidget> SlateWidget = ProbeTextBlock.TakeWidget();
	SlateWidget->SlatePrepass(1.0f);

	const FVector2D DesiredSize = SlateWidget->GetDesiredSize();
	if (!IsFinitePositive(DesiredSize.X) || !IsFinitePositive(DesiredSize.Y))
	{
		return false;
	}

	OutSize = DesiredSize;
	return true;
}

bool TextMeasurementRuntimeProbe::TryReadLineGapEm(const FSlateFontInfo& FontInfo, float& OutLineGapEm)
{
	OutLineGapEm = 0.0f;
	const FCompositeFont* CompositeFont = FontInfo.GetCompositeFont();
	if (!CompositeFont) return false;

	const FTypeface& Typeface = CompositeFont->DefaultTypeface;
	const FTypefaceEntry* Entry = Typeface.Fonts.FindByPredicate(
		[&FontInfo](const FTypefaceEntry& Candidate) { return Candidate.Name == FontInfo.TypefaceFontName; });
	if (!Entry && !Typeface.Fonts.IsEmpty()) Entry = &Typeface.Fonts[0];
	if (!Entry) return false;

	// Cache the selected face, whether asset-backed or a Slate filename-backed face.
	// Font reimport and module shutdown invalidate this cache as before.
	const UObject* FaceAsset = Entry->Font.GetFontFaceAsset();
	const FString CacheKey = FaceAsset
		? TEXT("asset:") + FaceAsset->GetPathName()
		: TEXT("file:") + Entry->Font.GetFontFilename();
	if (const TOptional<float>* Cached = GLineGapEmCache.Find(CacheKey))
	{
		if (Cached->IsSet()) OutLineGapEm = Cached->GetValue();
		return Cached->IsSet();
	}

	TOptional<float> Parsed;
	float LineGapEm = 0.0f;
	const FFontFaceDataConstPtr FaceData = Entry->Font.GetFontFaceData();
	if (FaceData.IsValid() && FaceData->HasData())
	{
		if (TryParseHheaLineGapEm(FaceData->GetData(), LineGapEm)) Parsed = LineGapEm;
	}
	else
	{
		TArray<uint8> Bytes;
		if (FFileHelper::LoadFileToArray(Bytes, *Entry->Font.GetFontFilename())
			&& TryParseHheaLineGapEm(Bytes, LineGapEm)) Parsed = LineGapEm;
	}
	GLineGapEmCache.Add(CacheKey, Parsed);
	if (Parsed.IsSet()) OutLineGapEm = Parsed.GetValue();
	return Parsed.IsSet();
}

void TextMeasurementRuntimeProbe::ResetProbeForModuleShutdown()
{
	GTextBlockProbe.ResetForModuleShutdown();
}

void TextMeasurementRuntimeProbe::ClearLineGapEmCache()
{
	GLineGapEmCache.Empty();
}
