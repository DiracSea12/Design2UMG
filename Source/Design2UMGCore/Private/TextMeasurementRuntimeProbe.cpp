// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "TextMeasurementRuntimeProbe.h"
#include "Design2UMGFontAccess.h"
#include "Design2UMGTextLayoutAccess.h"
#include "Design2UMGFontMetrics.h"

#include "Components/TextBlock.h"
#include "Engine/Font.h"
#include "Fonts/CompositeFont.h"
#include "Fonts/FontMeasure.h"
#include "Fonts/FontCache.h"
#include "Misc/ScopeExit.h"

#if WITH_HARFBUZZ
THIRD_PARTY_INCLUDES_START
#include <hb.h>
#include <hb-ot.h>
THIRD_PARTY_INCLUDES_END
#endif
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Styling/SlateTypes.h"
#include "TextMeasurement.h"
#include "TextFontResolution.h"
#include "TextMeasurementInternal.h"
#include "TextStyleUtils.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_HARFBUZZ && !IS_MONOLITHIC
// The engine's HarfBuzz library uses UE allocators; modular consumers own these hooks.
extern "C"
{
	void* HarfBuzzMalloc(size_t Size) { return FMemory::Malloc(Size); }
	void* HarfBuzzCalloc(size_t Count, size_t Size)
	{
		const size_t Bytes = Count * Size;
		if (!Bytes) return nullptr;
		void* Memory = FMemory::Malloc(Bytes);
		FMemory::Memzero(Memory, Bytes);
		return Memory;
	}
	void* HarfBuzzRealloc(void* Memory, size_t Size) { return FMemory::Realloc(Memory, Size); }
	void HarfBuzzFree(void* Memory) { FMemory::Free(Memory); }
}
#endif

namespace TextMeasurementRuntimeProbe
{
	FSlateFontInfo BuildFontInfo(const FTextMeasureRequest& Request)
	{
		FSlateFontInfo FontInfo = FTextBlockStyle::GetDefault().Font;
		if (FMath::IsFinite(Request.FontSize) && Request.FontSize > 0.0f)
		{
			FontInfo.Size = Design2UMGFontAccess::ToNativeFontSize(Request.FontSize);
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

	struct FFontVerticalMetrics
	{
		float AscentEm = 0.0f;
		float DescentEm = 0.0f;
		float HheaLineGapEm = 0.0f;
	};

	bool TryParseVerticalMetrics(const TArray<uint8>& Data, FFontVerticalMetrics& OutMetrics)
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

		int16 Ascent = static_cast<int16>(ReadUInt16(Data, HheaOffset + 4));
		int16 Descent = static_cast<int16>(ReadUInt16(Data, HheaOffset + 6));
		int32 Os2Offset = INDEX_NONE;
		// FreeType selects the typographic ascent/descent when USE_TYPO_METRICS is set.
		if (FindTableOffset(Data, 0x4F532F32 /* OS/2 */, Os2Offset)
			&& Os2Offset + 74 <= Data.Num() && (ReadUInt16(Data, Os2Offset + 62) & 0x80) != 0)
		{
			Ascent = static_cast<int16>(ReadUInt16(Data, Os2Offset + 68));
			Descent = static_cast<int16>(ReadUInt16(Data, Os2Offset + 70));
		}
		OutMetrics.AscentEm = static_cast<float>(Ascent) / UnitsPerEm;
		OutMetrics.DescentEm = static_cast<float>(Descent) / UnitsPerEm;
		OutMetrics.HheaLineGapEm = static_cast<float>(static_cast<int16>(ReadUInt16(Data, HheaOffset + 8))) / UnitsPerEm;
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
	 * (Editor invalidates it on UFont / UFontFace reimport) or the Core module
	 * shuts down; both paths repopulate lazily on the next read.
	 */
	TMap<FString, TOptional<FFontVerticalMetrics>> GVerticalMetricsCache;

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

namespace
{
#if WITH_HARFBUZZ
	bool MeasureFontRun(const FFontData& FontData, const uint32* Text, const int32 Length,
		const hb_direction_t Direction, const hb_script_t Script, const float EmHeight, float& OutWidth)
	{
		const FFontFaceDataConstPtr AssetData = FontData.GetFontFaceData();
		TArray<uint8> FileData;
		const TArray<uint8>* Bytes = AssetData.IsValid() && AssetData->HasData() ? &AssetData->GetData() : nullptr;
		if (!Bytes)
		{
			if (!FFileHelper::LoadFileToArray(FileData, *FontData.GetFontFilename())) return false;
			Bytes = &FileData;
		}
		hb_blob_t* Blob = hb_blob_create(reinterpret_cast<const char*>(Bytes->GetData()), Bytes->Num(), HB_MEMORY_MODE_READONLY, nullptr, nullptr);
		hb_face_t* Face = hb_face_create(Blob, FontData.GetSubFaceIndex());
		hb_font_t* Font = hb_font_create(Face);
		hb_buffer_t* Buffer = hb_buffer_create();
		ON_SCOPE_EXIT
		{
			hb_buffer_destroy(Buffer);
			hb_font_destroy(Font);
			hb_face_destroy(Face);
			hb_blob_destroy(Blob);
		};
		const unsigned int UnitsPerEm = hb_face_get_upem(Face);
		if (!UnitsPerEm) return false;
		hb_ot_font_set_funcs(Font);
		hb_font_set_scale(Font, UnitsPerEm, UnitsPerEm);
		hb_buffer_set_direction(Buffer, Direction);
		hb_buffer_set_script(Buffer, Script);
		hb_buffer_add_utf32(Buffer, Text, Length, 0, Length);
		hb_shape(Font, Buffer, nullptr, 0);
		unsigned int Count = 0;
		const hb_glyph_info_t* Glyphs = hb_buffer_get_glyph_infos(Buffer, &Count);
		const hb_glyph_position_t* Positions = hb_buffer_get_glyph_positions(Buffer, nullptr);
		int64 Advance = 0;
		for (unsigned int Index = 0; Index < Count; ++Index)
		{
			// A missing primary-face glyph needs Slate's final fallback, not invented metrics.
			if (Glyphs[Index].codepoint == 0 && Positions[Index].x_advance != 0) return false;
			Advance += Positions[Index].x_advance;
		}
		OutWidth = FMath::Abs(static_cast<double>(Advance)) * EmHeight / UnitsPerEm;
		return true;
	}

	bool MeasureIntrinsicLine(const FString& Text, const FSlateFontInfo& FontInfo, float& OutWidth)
	{
		OutWidth = 0.0f;
		FSlateFontCache& FontCache = *FSlateApplication::Get().GetRenderer()->GetFontCache();
		TArray<TextBiDi::FTextDirectionInfo> Directions;
		TextBiDi::ComputeTextDirection(Text, TextBiDi::ComputeBaseDirection(Text), Directions);
		const float EmHeight = static_cast<float>(FontInfo.Size) * Design2UMGFontAccess::RenderDPI / 72.0f;
		for (const TextBiDi::FTextDirectionInfo& Direction : Directions)
		{
			const FString DirectionText = Text.Mid(Direction.StartIndex, Direction.Length);
			const FTCHARToUTF32 Converted(*DirectionText);
			const uint32* Codepoints = reinterpret_cast<const uint32*>(Converted.Get());
			int32 Start = 0;
			while (Start < Converted.Length())
			{
				float FontScale = 1.0f;
				const FFontData& FontData = FontCache.GetFontDataForCodepoint(FontInfo, Codepoints[Start], FontScale);
				hb_script_t Script = HB_SCRIPT_COMMON;
				int32 End = Start;
				while (End < Converted.Length())
				{
					float NextScale = 1.0f;
					const FFontData& NextFont = FontCache.GetFontDataForCodepoint(FontInfo, Codepoints[End], NextScale);
					const hb_script_t NextScript = hb_unicode_script(hb_unicode_funcs_get_default(), Codepoints[End]);
					const bool bStrongScript = NextScript != HB_SCRIPT_COMMON && NextScript != HB_SCRIPT_INHERITED && NextScript != HB_SCRIPT_UNKNOWN;
					if (&NextFont != &FontData || NextScale != FontScale || (bStrongScript && Script != HB_SCRIPT_COMMON && Script != NextScript)) break;
					if (bStrongScript) Script = NextScript;
					++End;
				}
				float RunWidth = 0.0f;
				if (!MeasureFontRun(FontData, Codepoints + Start, End - Start,
					Direction.TextDirection == TextBiDi::ETextDirection::RightToLeft ? HB_DIRECTION_RTL : HB_DIRECTION_LTR,
					Script, EmHeight * FontScale, RunWidth)) return false;
				OutWidth += RunWidth;
				Start = End;
			}
		}
		return true;
	}
#endif
}

bool TextMeasurementRuntimeProbe::TryMeasureIntrinsicWidth(const FTextMeasureRequest& Request, float& OutWidth)
{
	OutWidth = 0.0f;
#if WITH_HARFBUZZ
	if (!IsFinitePositive(Request.FontSize) || !IsAvailable()
		|| (Request.bRequiresResolvedFontAsset && Request.FontAssetPath.IsEmpty())) return false;
	const FSlateFontInfo FontInfo = BuildFontInfo(Request);
	TArray<FString> Lines;
	Request.Text.ParseIntoArrayLines(Lines, false);
	for (const FString& Line : Lines)
	{
		float Width = 0.0f;
		if (!MeasureIntrinsicLine(Line, FontInfo, Width)) return false;
		if (Request.LetterSpacing != 0)
		{
			FTextBlockMeasureRequest TrackingRequest;
			static_cast<FTextMeasureRequest&>(TrackingRequest) = Request;
			TrackingRequest.Text = Line;
			FVector2D Tracked, Untracked;
			if (!TryMeasureTextBlockDesiredSize(TrackingRequest, Tracked)) return false;
			TrackingRequest.LetterSpacing = 0;
			if (!TryMeasureTextBlockDesiredSize(TrackingRequest, Untracked)) return false;
			Width += Tracked.X - Untracked.X;
		}
		OutWidth = FMath::Max(OutWidth, Width);
	}
	return IsFinitePositive(OutWidth);
#else
	return false;
#endif
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

	// Reuse one rooted probe across measurements. Every input measurement
	// reassigns text, font and the complete layout batch before prepass, so the
	// desired size depends only on the current request instead of probe history.
	UTextBlock& ProbeTextBlock = GTextBlockProbe.Acquire();

	ProbeTextBlock.SetText(FText::FromString(Request.Text));
	ProbeTextBlock.SetFont(BuildFontInfo(Request));
	ProbeTextBlock.SetJustification(ToSlateJustification(Request.TextAlign));
	Design2UMGTextLayoutAccess::FUpdate LayoutUpdate;
	LayoutUpdate.LineHeightPercentage = Request.LineHeightPercentage;
	LayoutUpdate.ApplyLineHeightToBottomLine = Request.bApplyExplicitLineHeight;
	LayoutUpdate.AutoWrapText = Request.bAutoWrapText;
	LayoutUpdate.WrapTextAt = Request.WrapTextAt;
	Design2UMGTextLayoutAccess::Apply(ProbeTextBlock, LayoutUpdate);

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

namespace
{
bool TryReadVerticalMetrics(const FSlateFontInfo& FontInfo, FFontVerticalMetrics& OutMetrics)
{
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
	if (const TOptional<FFontVerticalMetrics>* Cached = GVerticalMetricsCache.Find(CacheKey))
	{
		if (Cached->IsSet()) OutMetrics = Cached->GetValue();
		return Cached->IsSet();
	}

	TOptional<FFontVerticalMetrics> Parsed;
	FFontVerticalMetrics Metrics;
	const FFontFaceDataConstPtr FaceData = Entry->Font.GetFontFaceData();
	if (FaceData.IsValid() && FaceData->HasData())
	{
		if (TryParseVerticalMetrics(FaceData->GetData(), Metrics)) Parsed = Metrics;
	}
	else
	{
		TArray<uint8> Bytes;
		if (FFileHelper::LoadFileToArray(Bytes, *Entry->Font.GetFontFilename())
			&& TryParseVerticalMetrics(Bytes, Metrics)) Parsed = Metrics;
	}
	GVerticalMetricsCache.Add(CacheKey, Parsed);
	if (Parsed.IsSet()) OutMetrics = Parsed.GetValue();
	return Parsed.IsSet();
}
}

bool Design2UMGFontMetrics::TryReadLineGapEm(const FSlateFontInfo& FontInfo, float& OutLineGapEm)
{
	FFontVerticalMetrics Metrics;
	const bool bHasMetrics = TryReadVerticalMetrics(FontInfo, Metrics);
	OutLineGapEm = bHasMetrics ? Metrics.HheaLineGapEm : 0.0f;
	return bHasMetrics;
}

bool TextMeasurementRuntimeProbe::TryResolveIconBaselineShift(const FTextMeasureRequest& Request, const float BoxHeight, float& OutShift)
{
	OutShift = 0.0f;
	if (!IsFinitePositive(Request.FontSize) || !IsFinitePositive(BoxHeight) || !IsAvailable())
	{
		return false;
	}
	const FSlateFontInfo FontInfo = BuildFontInfo(Request);
	FFontVerticalMetrics Metrics;
	if (!TryReadVerticalMetrics(FontInfo, Metrics))
	{
		return false;
	}
	const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const float RuntimeBaseline = FontMeasure->GetMaxCharacterHeight(FontInfo) + FontMeasure->GetBaseline(FontInfo);
	const float EmHeight = static_cast<float>(FontInfo.Size) * Design2UMGFontAccess::RenderDPI / 72.0f;
	// Center the measured face in the authored icon box; the face em need not equal its height.
	const float SourceBaseline = (BoxHeight + EmHeight * (Metrics.AscentEm + Metrics.DescentEm)) / 2.0f;
	OutShift = SourceBaseline - RuntimeBaseline;
	return true;
}

void TextMeasurementRuntimeProbe::ResetProbeForModuleShutdown()
{
	GTextBlockProbe.ResetForModuleShutdown();
}

void Design2UMGFontMetrics::ClearCache()
{
	GVerticalMetricsCache.Empty();
}
