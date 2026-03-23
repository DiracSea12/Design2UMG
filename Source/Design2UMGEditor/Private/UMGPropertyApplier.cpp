// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGWidgetUtils.h"
#include "UMGPropertyKeys.h"
#include "UMGValueParser.h"
#include "TextStyleUtils.h"
#include "Design2UMGLog.h"
#include "ProceduralShapeGenerator.h"
#include "DesignPathRasterizer.h"

#include "Components/Widget.h"
#include "Components/TextBlock.h"
#include "Components/TextWidgetTypes.h"
#include "Components/RichTextBlock.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/Slider.h"
#include "Components/CheckBox.h"
#include "Components/EditableTextBox.h"
#include "Components/EditableText.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/MultiLineEditableText.h"
#include "Components/ProgressBar.h"
#include "Components/Throbber.h"
#include "Components/CircularThrobber.h"
#include "Components/Spacer.h"
#include "Components/ComboBoxString.h"
#include "Components/SpinBox.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "Components/ScrollBox.h"
#include "Components/ScaleBox.h"
#include "Components/GridPanel.h"
#include "Components/UniformGridPanel.h"
#include "Components/WrapBox.h"
#include "Components/BackgroundBlur.h"
#include "Components/RetainerBox.h"
#include "Components/InvalidationBox.h"
#include "Components/WidgetSwitcher.h"
#include "Components/ExpandableArea.h"
#include "Components/TileView.h"
#include "Components/MenuAnchor.h"
#include "Components/SafeZone.h"
#include "Fonts/FontCache.h"
#include "Styling/SlateBrush.h"
#include "Engine/UserInterfaceSettings.h"

// ============================================================================
// Widget 属性应用（函数指针映射表模式）
// ============================================================================

namespace
{
	bool TryGetProp(const FUMGPropertyBag& P, const TCHAR* Key, FString& OutValue)
	{
		return P.TryGetString(Key, OutValue) && !OutValue.IsEmpty();
	}

	bool TryGetPropAllowEmpty(const FUMGPropertyBag& P, const TCHAR* Key, FString& OutValue)
	{
		return P.TryGetString(Key, OutValue);
	}

	bool HasProp(const FUMGPropertyBag& P, const TCHAR* Key)
	{
		FString Value;
		return TryGetProp(P, Key, Value);
	}

	bool TryParseVector4(const FString& Value, FVector4& OutVector)
	{
		TArray<FString> Parts;
		Value.ParseIntoArray(Parts, TEXT(","));
		if (Parts.Num() != 4)
		{
			return false;
		}

		OutVector = FVector4(
			FCString::Atof(*Parts[0].TrimStartAndEnd()),
			FCString::Atof(*Parts[1].TrimStartAndEnd()),
			FCString::Atof(*Parts[2].TrimStartAndEnd()),
			FCString::Atof(*Parts[3].TrimStartAndEnd()));
		return true;
	}

	bool TryParseDrawAs(const FString& Value, ESlateBrushDrawType::Type& OutDrawAs)
	{
		static const TMap<FString, ESlateBrushDrawType::Type> DrawAsMap = {
			{TEXT("NoDrawType"), ESlateBrushDrawType::NoDrawType},
			{TEXT("Box"), ESlateBrushDrawType::Box},
			{TEXT("Border"), ESlateBrushDrawType::Border},
			{TEXT("Image"), ESlateBrushDrawType::Image},
			{TEXT("RoundedBox"), ESlateBrushDrawType::RoundedBox},
		};

		if (const ESlateBrushDrawType::Type* Found = DrawAsMap.Find(Value))
		{
			OutDrawAs = *Found;
			return true;
		}
		return false;
	}

	bool TryParseWidgetClipping(const FString& Value, EWidgetClipping& OutClipping)
	{
		static const TMap<FString, EWidgetClipping> ClippingMap = {
			{TEXT("Inherit"), EWidgetClipping::Inherit},
			{TEXT("ClipToBounds"), EWidgetClipping::ClipToBounds},
			{TEXT("ClipToBoundsWithoutIntersecting"), EWidgetClipping::ClipToBoundsWithoutIntersecting},
			{TEXT("ClipToBoundsAlways"), EWidgetClipping::ClipToBoundsAlways},
			{TEXT("OnDemand"), EWidgetClipping::OnDemand},
		};

		if (const EWidgetClipping* Found = ClippingMap.Find(Value))
		{
			OutClipping = *Found;
			return true;
		}
		return false;
	}

	bool ApplyImageModeToSlateBrush(
		FSlateBrush& Brush,
		const FUMGPropertyBag& P,
		TFunctionRef<void(UTexture2D*)> ApplyCroppedTexture)
	{
		FString Mode;
		if (!TryGetProp(P, TEXT("ImageMode"), Mode))
		{
			return false;
		}

		if (Mode == TEXT("Crop"))
		{
			FString TransformStr;
			if (!TryGetProp(P, TEXT("ImageTransform"), TransformStr))
			{
				return false;
			}

			TArray<FString> Parts;
			TransformStr.ParseIntoArray(Parts, TEXT(","));
			if (Parts.Num() != 6)
			{
				return false;
			}

			const float A = FCString::Atof(*Parts[0]);
			const float B = FCString::Atof(*Parts[1]);
			const float TX = FCString::Atof(*Parts[2]);
			const float C = FCString::Atof(*Parts[3]);
			const float D = FCString::Atof(*Parts[4]);
			const float TY = FCString::Atof(*Parts[5]);

			if (FMath::IsNearlyZero(B) && FMath::IsNearlyZero(C) && A > KINDA_SMALL_NUMBER && D > KINDA_SMALL_NUMBER)
			{
				Brush.SetUVRegion(FBox2f(FVector2f(TX, TY), FVector2f(TX + A, TY + D)));
				return true;
			}

			if (!FMath::IsNearlyZero(B) || !FMath::IsNearlyZero(C))
			{
				UTexture2D* SrcTex = Cast<UTexture2D>(Brush.GetResourceObject());
				float CropWidth = 64.0f;
				float CropHeight = 64.0f;
				P.TryGetFloat(TEXT("WidthOverride"), CropWidth);
				P.TryGetFloat(TEXT("HeightOverride"), CropHeight);
				const int32 CropW = FMath::Max(1, FMath::RoundToInt(CropWidth));
				const int32 CropH = FMath::Max(1, FMath::RoundToInt(CropHeight));
				if (UTexture2D* Cropped = FProceduralShapeGenerator::ApplyAffineCrop(SrcTex, A, B, TX, C, D, TY, CropW, CropH))
				{
					ApplyCroppedTexture(Cropped);
					return true;
				}
			}

			return false;
		}

		if (Mode == TEXT("Fit"))
		{
			Brush.Tiling = ESlateBrushTileType::NoTile;
			return true;
		}

		return false;
	}

	void ApplyTextBlockProps(UWidget* W, const FUMGPropertyBag& P)
	{
		UTextBlock* T = CastChecked<UTextBlock>(W);
		FString Value;
		if (TryGetPropAllowEmpty(P, TEXT("Text"), Value)) T->SetText(FText::FromString(Value));
		FLinearColor TextColor;
		if (P.TryGetColor(TEXT("Color"), TextColor))
		{
			T->SetColorAndOpacity(FSlateColor(TextColor));
		}
		FSlateFontInfo FontInfo = T->GetFont();
		bool bFontChanged = false;
		const bool bIsIconFontWidget = HasProp(P, TEXT("IconName"));
		float FontSize = 0.0f;
		if (P.TryGetFloat(TEXT("FontSize"), FontSize))
		{
			FontInfo.Size = FMath::Max(1, FMath::RoundToInt(FontSize));
			bFontChanged = true;
		}
		if (TryGetProp(P, TEXT("FontFamily"), Value))
		{
			if (Value.StartsWith(TEXT("/")))
			{
				if (UObject* FontObject = LoadObject<UObject>(nullptr, *Value))
				{
					FontInfo.FontObject = FontObject;
				}
			}
			else if (!Value.IsEmpty() && !bIsIconFontWidget)
			{
				FontInfo.TypefaceFontName = FName(*Value);
			}
			bFontChanged = true;
		}
		if (TryGetProp(P, TEXT("TypefaceFontName"), Value))
		{
			FontInfo.TypefaceFontName = FName(*Value);
			bFontChanged = true;
		}
		float PixelSpacing = 0.0f;
		if (P.TryGetFloat(TEXT("LetterSpacing"), PixelSpacing))
		{
			float DesignFontSize = 0.0f;
			P.TryGetFloat(TEXT("DesignFontSize"), DesignFontSize);
			if (FontInfo.Size > 0 || DesignFontSize > 0.0f)
			{
				FontInfo.LetterSpacing = TextStyleUtils::ConvertLetterSpacingPxToTracking(PixelSpacing, DesignFontSize, FontInfo.Size);
			}
			bFontChanged = true;
		}
		if (bFontChanged)
		{
			T->SetFont(FontInfo);
		}
		if (TryGetProp(P, TEXT("Justification"), Value))
		{
			static const TMap<FString, ETextJustify::Type> JustifyMap = {
				{TEXT("Left"), ETextJustify::Left}, {TEXT("Center"), ETextJustify::Center}, {TEXT("Right"), ETextJustify::Right}
			};
			if (const ETextJustify::Type* J = JustifyMap.Find(Value)) T->SetJustification(*J);
		}
		float LineHeightPercentage = 0.0f;
		if (P.TryGetFloat(TEXT("LineHeightPercentage"), LineHeightPercentage))
		{
			T->SetLineHeightPercentage(LineHeightPercentage);
			T->SetApplyLineHeightToBottomLine(true);
		}
		bool bAutoWrapText = false;
		if (P.TryGetBool(TEXT("AutoWrapText"), bAutoWrapText))
			T->SetAutoWrapText(bAutoWrapText);
		float WrapTextAt = 0.0f;
		if (P.TryGetFloat(TEXT("WrapTextAt"), WrapTextAt))
			T->SetWrapTextAt(WrapTextAt);
		FVector2D ShadowOffset;
		if (P.TryGetVector2D(TEXT("ShadowOffset"), ShadowOffset))
			T->SetShadowOffset(ShadowOffset);
		FLinearColor ShadowColor;
		if (P.TryGetColor(TEXT("ShadowColorAndOpacity"), ShadowColor))
			T->SetShadowColorAndOpacity(ShadowColor);
		if (bIsIconFontWidget)
		{
			FShapedTextOptions IconShaping;
			IconShaping.bOverride_TextShapingMethod = true;
			IconShaping.TextShapingMethod = ETextShapingMethod::FullShaping;
			T->SetShapedTextOptions(IconShaping);
		}
	}

	void ApplyButtonProps(UWidget* W, const FUMGPropertyBag& P)
	{
		UButton* B = CastChecked<UButton>(W);
		FButtonStyle S = B->GetStyle();
		FLinearColor BackgroundColor;
		if (P.TryGetColor(TEXT("BackgroundColor"), BackgroundColor))
		{
			S.Normal.TintColor = FSlateColor(BackgroundColor);
			S.Hovered.TintColor = FSlateColor(BackgroundColor * 1.1f);
			S.Pressed.TintColor = FSlateColor(BackgroundColor * 0.9f);
		}
		FLinearColor HoveredColor;
		if (P.TryGetColor(TEXT("HoveredColor"), HoveredColor)) S.Hovered.TintColor = FSlateColor(HoveredColor);
		FLinearColor PressedColor;
		if (P.TryGetColor(TEXT("PressedColor"), PressedColor)) S.Pressed.TintColor = FSlateColor(PressedColor);
		B->SetStyle(S);
	}

	void ApplyImageProps(UWidget* W, const FUMGPropertyBag& P)
	{
		UImage* I = CastChecked<UImage>(W);
		FString Value;
		FLinearColor ImageColor;
		if (P.TryGetColor(TEXT("Color"), ImageColor) || P.TryGetColor(TEXT("ColorAndOpacity"), ImageColor))
		{
			I->SetColorAndOpacity(ImageColor);
		}

		FLinearColor BrushTint;
		if (P.TryGetColor(TEXT("BrushColor"), BrushTint))
		{
			I->SetBrushTintColor(FSlateColor(BrushTint));
		}
		if (TryGetProp(P, TEXT("MaterialRef"), Value))
		{
			if (UTexture2D* GeneratedTex = FProceduralShapeGenerator::GenerateFromMaterialRef(Value, P, I))
			{
				I->SetBrushFromTexture(GeneratedTex);
				FSlateBrush GeneratedBrush = I->GetBrush();
				float DesiredWidth = static_cast<float>(GeneratedTex->GetSizeX());
				float DesiredHeight = static_cast<float>(GeneratedTex->GetSizeY());
				P.TryGetFloat(TEXT("WidthOverride"), DesiredWidth);
				P.TryGetFloat(TEXT("HeightOverride"), DesiredHeight);
				GeneratedBrush.ImageSize = FVector2D(DesiredWidth, DesiredHeight);
				GeneratedBrush.Tiling = ESlateBrushTileType::NoTile;
				I->SetBrush(GeneratedBrush);
				I->SetDesiredSizeOverride(GeneratedBrush.ImageSize);
				return;
			}
		}
		FString Geom;
		if (TryGetProp(P, TEXT("PathGeometry"), Geom))
		{
			FString FillRuleStr;
			if (!TryGetProp(P, TEXT("PathFillRule"), FillRuleStr))
			{
				FillRuleStr = TEXT("nonzero");
			}
			FLinearColor PathColor = FLinearColor::White;
			P.TryGetColor(TEXT("PathFillColor"), PathColor);

			float PathWidth = 64.0f;
			float PathHeight = 64.0f;
			P.TryGetFloat(TEXT("WidthOverride"), PathWidth);
			P.TryGetFloat(TEXT("HeightOverride"), PathHeight);
			int32 PW = FMath::Max(1, FMath::RoundToInt(PathWidth));
			int32 PH = FMath::Max(1, FMath::RoundToInt(PathHeight));
			constexpr int32 PathSupersample = 2;
			const int32 RasterW = PW * PathSupersample;
			const int32 RasterH = PH * PathSupersample;

			if (UTexture2D* Tex = FDesignPathRasterizer::Rasterize(Geom, RasterW, RasterH, PathColor, FillRuleStr))
			{
				I->SetBrushFromTexture(Tex);
				FSlateBrush PathBrush = I->GetBrush();
				PathBrush.ImageSize = FVector2D(PW, PH);
				PathBrush.Tiling = ESlateBrushTileType::NoTile;
				I->SetBrush(PathBrush);
				I->SetDesiredSizeOverride(FVector2D(PW, PH));
				FSlateBrush DbgBrush = I->GetBrush();
				UE_LOG(LogDesign2UMG, Verbose, TEXT("PathGeometry applied: name=%s tex=%dx%d desired=%dx%d brush.ImageSize=%.0fx%.0f brush.DrawAs=%d"),
					*W->GetName(), Tex->GetSizeX(), Tex->GetSizeY(), PW, PH,
					DbgBrush.ImageSize.X, DbgBrush.ImageSize.Y, (int32)DbgBrush.GetDrawType());
				return;
			}
		}

		if (TryGetProp(P, TEXT("TexturePath"), Value))
		{
			UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *Value);
			if (!Tex) Tex = FindObject<UTexture2D>(nullptr, *Value);
			if (Tex) I->SetBrushFromTexture(Tex);
		}

		FSlateBrush Brush = I->GetBrush();
		bool bBrushModified = false;
		float WidthOverride = 0.0f;
		if (P.TryGetFloat(TEXT("WidthOverride"), WidthOverride))
		{
			Brush.ImageSize.X = WidthOverride;
			bBrushModified = true;
		}
		float HeightOverride = 0.0f;
		if (P.TryGetFloat(TEXT("HeightOverride"), HeightOverride))
		{
			Brush.ImageSize.Y = HeightOverride;
			bBrushModified = true;
		}

		bBrushModified = ApplyImageModeToSlateBrush(Brush, P, [&I, &Brush, &bBrushModified](UTexture2D* Cropped)
		{
			I->SetBrushFromTexture(Cropped);
			Brush = I->GetBrush();
			bBrushModified = false;
		}) || bBrushModified;

		if (bBrushModified)
		{
			if (Brush.Tiling == ESlateBrushTileType::Both && !HasProp(P, TEXT("ImageMode")))
				Brush.Tiling = ESlateBrushTileType::NoTile;
			I->SetBrush(Brush);
			if (Brush.ImageSize.X > 0 && Brush.ImageSize.Y > 0)
				I->SetDesiredSizeOverride(Brush.ImageSize);
		}
	}

	void ApplyProgressBarProps(UWidget* W, const FUMGPropertyBag& P)
	{
		UProgressBar* PB = CastChecked<UProgressBar>(W);
		float Percent = 0.0f;
		if (P.TryGetFloat(TEXT("Percent"), Percent)) PB->SetPercent(Percent);
		FLinearColor FillColor;
		if (P.TryGetColor(TEXT("FillColor"), FillColor)) PB->SetFillColorAndOpacity(FillColor);
	}

	void ApplyThrobberProps(UWidget* W, const FUMGPropertyBag& P)
	{
		UThrobber* T = CastChecked<UThrobber>(W);
		int32 NumberOfPieces = 0;
		if (P.TryGetInt(TEXT("NumberOfPieces"), NumberOfPieces)) T->SetNumberOfPieces(NumberOfPieces);
		bool bAnimate = false;
		if (P.TryGetBool(TEXT("Animate"), bAnimate))
		{
			T->SetAnimateHorizontally(bAnimate);
			T->SetAnimateVertically(bAnimate);
			T->SetAnimateOpacity(bAnimate);
		}
		bool bAnimateHorizontally = false;
		if (P.TryGetBool(TEXT("AnimateHorizontally"), bAnimateHorizontally)) T->SetAnimateHorizontally(bAnimateHorizontally);
		bool bAnimateVertically = false;
		if (P.TryGetBool(TEXT("AnimateVertically"), bAnimateVertically)) T->SetAnimateVertically(bAnimateVertically);
		bool bAnimateOpacity = false;
		if (P.TryGetBool(TEXT("AnimateOpacity"), bAnimateOpacity)) T->SetAnimateOpacity(bAnimateOpacity);
	}

	void ApplyCircularThrobberProps(UWidget* W, const FUMGPropertyBag& P)
	{
		UCircularThrobber* T = CastChecked<UCircularThrobber>(W);
		int32 NumberOfPieces = 0;
		if (P.TryGetInt(TEXT("NumberOfPieces"), NumberOfPieces)) T->SetNumberOfPieces(NumberOfPieces);
		float Period = 0.0f;
		if (P.TryGetFloat(TEXT("Period"), Period)) T->SetPeriod(Period);
		float Radius = 0.0f;
		if (P.TryGetFloat(TEXT("Radius"), Radius)) T->SetRadius(Radius);
	}

	void ApplySliderProps(UWidget* W, const FUMGPropertyBag& P)
	{
		float Value = 0.0f;
		if (P.TryGetFloat(TEXT("Value"), Value)) CastChecked<USlider>(W)->SetValue(Value);
	}

	void ApplyCheckBoxProps(UWidget* W, const FUMGPropertyBag& P)
	{
		bool bIsChecked = false;
		if (P.TryGetBool(TEXT("IsChecked"), bIsChecked)) CastChecked<UCheckBox>(W)->SetIsChecked(bIsChecked);
	}

	void ApplyEditableTextBoxProps(UWidget* W, const FUMGPropertyBag& P)
	{
		UEditableTextBox* E = CastChecked<UEditableTextBox>(W);
		FString Value;
		if (TryGetPropAllowEmpty(P, TEXT("Text"), Value)) E->SetText(FText::FromString(Value));
		if (TryGetPropAllowEmpty(P, TEXT("HintText"), Value)) E->SetHintText(FText::FromString(Value));
	}

	void ApplyMultiLineEditableTextBoxProps(UWidget* W, const FUMGPropertyBag& P)
	{
		UMultiLineEditableTextBox* M = CastChecked<UMultiLineEditableTextBox>(W);
		FString Value;
		if (TryGetPropAllowEmpty(P, TEXT("Text"), Value)) M->SetText(FText::FromString(Value));
		if (TryGetPropAllowEmpty(P, TEXT("HintText"), Value)) M->SetHintText(FText::FromString(Value));
	}

	void ApplyEditableTextProps(UWidget* W, const FUMGPropertyBag& P)
	{
		UEditableText* E = CastChecked<UEditableText>(W);
		FString Value;
		if (TryGetPropAllowEmpty(P, TEXT("Text"), Value)) E->SetText(FText::FromString(Value));
		if (TryGetPropAllowEmpty(P, TEXT("HintText"), Value)) E->SetHintText(FText::FromString(Value));
		bool bIsReadOnly = false;
		if (P.TryGetBool(TEXT("IsReadOnly"), bIsReadOnly)) E->SetIsReadOnly(bIsReadOnly);
		bool bIsPassword = false;
		if (P.TryGetBool(TEXT("IsPassword"), bIsPassword)) E->SetIsPassword(bIsPassword);
		float MinimumDesiredWidth = 0.0f;
		if (P.TryGetFloat(TEXT("MinimumDesiredWidth"), MinimumDesiredWidth)) E->SetMinimumDesiredWidth(MinimumDesiredWidth);
	}

	void ApplyMultiLineEditableTextProps(UWidget* W, const FUMGPropertyBag& P)
	{
		UMultiLineEditableText* M = CastChecked<UMultiLineEditableText>(W);
		FString Value;
		if (TryGetPropAllowEmpty(P, TEXT("Text"), Value)) M->SetText(FText::FromString(Value));
		if (TryGetPropAllowEmpty(P, TEXT("HintText"), Value)) M->SetHintText(FText::FromString(Value));
		bool bIsReadOnly = false;
		if (P.TryGetBool(TEXT("IsReadOnly"), bIsReadOnly)) M->SetIsReadOnly(bIsReadOnly);
	}

	void ApplyBorderProps(UWidget* W, const FUMGPropertyBag& P)
	{
		UBorder* Bd = CastChecked<UBorder>(W);
		Bd->SetPadding(FMargin(0.0f));
		FSlateBrush Brush = Bd->Background;
		bool bBrushChanged = false;
		const bool bHasCornerRadiusProp = HasProp(P, TEXT("OutlineCornerRadius")) || HasProp(P, TEXT("CornerRadii"));

		FString Value;
		if (TryGetProp(P, TEXT("TexturePath"), Value))
		{
			UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *Value);
			if (!Tex) Tex = FindObject<UTexture2D>(nullptr, *Value);
			if (Tex)
			{
				Brush.SetResourceObject(Tex);
				Brush.DrawAs = ESlateBrushDrawType::Image;
				Brush.ImageSize = FVector2D(Tex->GetSizeX(), Tex->GetSizeY());
				bBrushChanged = true;
			}
		}

		if (TryGetProp(P, TEXT("MaterialRef"), Value))
		{
			if (UTexture2D* GeneratedTex = FProceduralShapeGenerator::GenerateFromMaterialRef(Value, P, Bd))
			{
				Brush.SetResourceObject(GeneratedTex);
				Brush.DrawAs = ESlateBrushDrawType::Image;
				Brush.ImageSize = FVector2D(GeneratedTex->GetSizeX(), GeneratedTex->GetSizeY());
				bBrushChanged = true;
			}
		}

		if (TryGetProp(P, TEXT("DrawAs"), Value))
		{
			ESlateBrushDrawType::Type DrawAsType;
			if (TryParseDrawAs(Value, DrawAsType))
			{
				Brush.DrawAs = DrawAsType;
				bBrushChanged = true;
			}
		}

		// For RoundedBox without explicit corner data, pin to square corners.
		// Otherwise UE may keep a previous HalfHeight rounding state and create capsule artifacts.
		if (Brush.DrawAs == ESlateBrushDrawType::RoundedBox && !bHasCornerRadiusProp)
		{
			Brush.OutlineSettings.CornerRadii = FVector4(0, 0, 0, 0);
			Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
			bBrushChanged = true;
		}

		float OutlineCornerRadius = 0.0f;
		if (P.TryGetFloat(TEXT("OutlineCornerRadius"), OutlineCornerRadius))
		{
			const float Radius = FMath::Max(0.0f, OutlineCornerRadius);
			// Pill 形状（radius >= 999）使用 HalfHeightRadius，UE5 自动 clamp 到控件半高
			if (Radius >= 999.0f)
			{
				Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
			}
			else
			{
				Brush.OutlineSettings.CornerRadii = FVector4(Radius, Radius, Radius, Radius);
				Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
			}
			bBrushChanged = true;
		}

		FVector4 CornerRadii;
		if (P.TryGetVector4(TEXT("CornerRadii"), CornerRadii))
		{
			const float MaxR = FMath::Max3(CornerRadii.X, CornerRadii.Y, FMath::Max(CornerRadii.Z, CornerRadii.W));
			if (MaxR >= 999.0f)
			{
				Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
			}
			else
			{
				Brush.OutlineSettings.CornerRadii = CornerRadii;
				Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
			}
			bBrushChanged = true;
		}

		float OutlineThickness = 0.0f;
		if (P.TryGetFloat(TEXT("OutlineThickness"), OutlineThickness))
		{
			Brush.OutlineSettings.Width = FMath::Max(0.0f, OutlineThickness);
			bBrushChanged = true;
		}

		FLinearColor OutlineColor;
		if (P.TryGetColor(TEXT("OutlineColor"), OutlineColor))
		{
			Brush.OutlineSettings.Color = FSlateColor(OutlineColor);
			bBrushChanged = true;
		}

		FLinearColor BorderColor;
		if (P.TryGetColor(TEXT("BrushColor"), BorderColor))
		{
			if (!Brush.GetResourceObject() && Brush.DrawAs != ESlateBrushDrawType::RoundedBox)
			{
				const ESlateBrushDrawType::Type ExistingDrawAs = Brush.DrawAs;
				UTexture2D* WhiteTex = LoadObject<UTexture2D>(nullptr,
					TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
				if (WhiteTex)
				{
					Brush.SetResourceObject(WhiteTex);
					if (ExistingDrawAs == ESlateBrushDrawType::NoDrawType)
					{
						Brush.DrawAs = ESlateBrushDrawType::Image;
					}
					Brush.ImageSize = FVector2D(1.0f, 1.0f);
					bBrushChanged = true;
				}
			}
			Bd->SetBrushColor(BorderColor);
		}

		bBrushChanged = ApplyImageModeToSlateBrush(Brush, P, [&Brush](UTexture2D* Cropped)
		{
			Brush.SetResourceObject(Cropped);
		}) || bBrushChanged;

		if (bBrushChanged)
		{
			Bd->SetBrush(Brush);
		}

		FLinearColor ContentColor;
		if (P.TryGetColor(TEXT("ContentColor"), ContentColor)) Bd->SetContentColorAndOpacity(ContentColor);
		FMargin BorderPadding;
		if (P.TryGetMargin(TEXT("Padding"), BorderPadding)) Bd->SetPadding(BorderPadding);

		// StrokePadding: additional inset for inside-aligned strokes
		FMargin StrokePadding;
		if (P.TryGetMargin(TEXT("StrokePadding"), StrokePadding))
		{
			FMargin Current = Bd->GetPadding();
			Bd->SetPadding(FMargin(Current.Left + StrokePadding.Left, Current.Top + StrokePadding.Top,
				Current.Right + StrokePadding.Right, Current.Bottom + StrokePadding.Bottom));
		}
	}

	void ApplySizeBoxProps(UWidget* W, const FUMGPropertyBag& P)
	{
		USizeBox* SB = CastChecked<USizeBox>(W);
		float WidthOverride = 0.0f;
		if (P.TryGetFloat(TEXT("WidthOverride"), WidthOverride)) SB->SetWidthOverride(WidthOverride);
		float HeightOverride = 0.0f;
		if (P.TryGetFloat(TEXT("HeightOverride"), HeightOverride)) SB->SetHeightOverride(HeightOverride);
		float MinDesiredWidth = 0.0f;
		if (P.TryGetFloat(TEXT("MinDesiredWidth"), MinDesiredWidth)) SB->SetMinDesiredWidth(MinDesiredWidth);
		float MinDesiredHeight = 0.0f;
		if (P.TryGetFloat(TEXT("MinDesiredHeight"), MinDesiredHeight)) SB->SetMinDesiredHeight(MinDesiredHeight);
		float MaxDesiredWidth = 0.0f;
		if (P.TryGetFloat(TEXT("MaxDesiredWidth"), MaxDesiredWidth)) SB->SetMaxDesiredWidth(MaxDesiredWidth);
		float MaxDesiredHeight = 0.0f;
		if (P.TryGetFloat(TEXT("MaxDesiredHeight"), MaxDesiredHeight)) SB->SetMaxDesiredHeight(MaxDesiredHeight);
	}

	void ApplyScrollBoxProps(UWidget* W, const FUMGPropertyBag& P)
	{
		UScrollBox* Sc = CastChecked<UScrollBox>(W);
		FString Value;
		if (TryGetProp(P, TEXT("Orientation"), Value))
			Sc->SetOrientation(Value == TEXT("Horizontal") ? Orient_Horizontal : Orient_Vertical);
		if (TryGetProp(P, TEXT("ScrollBarVisibility"), Value))
		{
			static const TMap<FString, ESlateVisibility> VisMap = {
				{TEXT("Visible"), ESlateVisibility::Visible}, {TEXT("Collapsed"), ESlateVisibility::Collapsed}, {TEXT("Hidden"), ESlateVisibility::Hidden}
			};
			if (const ESlateVisibility* Vis = VisMap.Find(Value)) Sc->SetScrollBarVisibility(*Vis);
		}
	}

	void ApplySpacerProps(UWidget* W, const FUMGPropertyBag& P)
	{
		FVector2D Size(0, 0);
		float Width = 0.0f;
		if (P.TryGetFloat(TEXT("Width"), Width)) Size.X = Width;
		float Height = 0.0f;
		if (P.TryGetFloat(TEXT("Height"), Height)) Size.Y = Height;
		CastChecked<USpacer>(W)->SetSize(Size);
	}

	void ApplyRichTextBlockProps(UWidget* W, const FUMGPropertyBag& P)
	{
		FString Value;
		if (TryGetPropAllowEmpty(P, TEXT("Text"), Value)) CastChecked<URichTextBlock>(W)->SetText(FText::FromString(Value));
	}

	void ApplyComboBoxStringProps(UWidget* W, const FUMGPropertyBag& P)
	{
		UComboBoxString* CB = CastChecked<UComboBoxString>(W);
		FString Value;
		if (TryGetProp(P, TEXT("Options"), Value))
		{
			TArray<FString> Opts;
			Value.ParseIntoArray(Opts, TEXT(","));
			for (const FString& O : Opts) CB->AddOption(O.TrimStartAndEnd());
		}
		if (TryGetPropAllowEmpty(P, TEXT("SelectedOption"), Value)) CB->SetSelectedOption(Value);
	}

	void ApplySpinBoxProps(UWidget* W, const FUMGPropertyBag& P)
	{
		USpinBox* SpB = CastChecked<USpinBox>(W);
		float Value = 0.0f;
		if (P.TryGetFloat(TEXT("Value"), Value)) SpB->SetValue(Value);
		float MinValue = 0.0f;
		if (P.TryGetFloat(TEXT("MinValue"), MinValue)) SpB->SetMinValue(MinValue);
		float MaxValue = 0.0f;
		if (P.TryGetFloat(TEXT("MaxValue"), MaxValue)) SpB->SetMaxValue(MaxValue);
	}

	void ApplyBackgroundBlurProps(UWidget* W, const FUMGPropertyBag& P)
	{
		float BlurStrength = 0.0f;
		if (P.TryGetFloat(TEXT("BlurStrength"), BlurStrength)) CastChecked<UBackgroundBlur>(W)->SetBlurStrength(BlurStrength);
	}

	void ApplyRetainerBoxProps(UWidget* W, const FUMGPropertyBag& P)
	{
		URetainerBox* RB = CastChecked<URetainerBox>(W);
		bool bRetainRendering = false;
		if (P.TryGetBool(TEXT("RetainRendering"), bRetainRendering)) RB->SetRetainRendering(bRetainRendering);
		int32 Phase = RB->GetPhase();
		int32 PhaseCount = RB->GetPhaseCount();
		bool bHasPhase = false;
		if (P.TryGetInt(TEXT("Phase"), Phase))
		{
			bHasPhase = true;
		}
		if (P.TryGetInt(TEXT("PhaseCount"), PhaseCount))
		{
			PhaseCount = FMath::Max(1, PhaseCount);
			bHasPhase = true;
		}
		if (bHasPhase) RB->SetRenderingPhase(Phase, PhaseCount);
	}

	void ApplyInvalidationBoxProps(UWidget* W, const FUMGPropertyBag& P)
	{
		UInvalidationBox* IB = CastChecked<UInvalidationBox>(W);
		bool bCanCache = false;
		if (P.TryGetBool(TEXT("CanCache"), bCanCache)) IB->SetCanCache(bCanCache);
	}

	void ApplyGridPanelProps(UWidget* W, const FUMGPropertyBag& P)
	{
		UGridPanel* G = CastChecked<UGridPanel>(W);
		FString Value;
		if (TryGetProp(P, TEXT("ColumnFill"), Value))
		{
			TArray<FString> Fills;
			Value.ParseIntoArray(Fills, TEXT(","));
			for (int32 i = 0; i < Fills.Num(); ++i)
			{
				G->SetColumnFill(i, FCString::Atof(*Fills[i].TrimStartAndEnd()));
			}
		}
		if (TryGetProp(P, TEXT("RowFill"), Value))
		{
			TArray<FString> Fills;
			Value.ParseIntoArray(Fills, TEXT(","));
			for (int32 i = 0; i < Fills.Num(); ++i)
			{
				G->SetRowFill(i, FCString::Atof(*Fills[i].TrimStartAndEnd()));
			}
		}
	}

	void ApplyUniformGridPanelProps(UWidget* W, const FUMGPropertyBag& P)
	{
		UUniformGridPanel* UG = CastChecked<UUniformGridPanel>(W);
		FMargin SlotPadding;
		if (P.TryGetMargin(TEXT("SlotPadding"), SlotPadding)) UG->SetSlotPadding(SlotPadding);
		float MinDesiredSlotWidth = 0.0f;
		if (P.TryGetFloat(TEXT("MinDesiredSlotWidth"), MinDesiredSlotWidth)) UG->SetMinDesiredSlotWidth(MinDesiredSlotWidth);
		float MinDesiredSlotHeight = 0.0f;
		if (P.TryGetFloat(TEXT("MinDesiredSlotHeight"), MinDesiredSlotHeight)) UG->SetMinDesiredSlotHeight(MinDesiredSlotHeight);
	}

	void ApplyWrapBoxProps(UWidget* W, const FUMGPropertyBag& P)
	{
		UWrapBox* WB = CastChecked<UWrapBox>(W);
		FVector2D InnerSlotPadding;
		if (P.TryGetVector2D(TEXT("InnerSlotPadding"), InnerSlotPadding)) WB->SetInnerSlotPadding(InnerSlotPadding);
		FString Value;
		if (TryGetProp(P, TEXT("Orientation"), Value))
			WB->SetOrientation(Value == TEXT("Vertical") ? Orient_Vertical : Orient_Horizontal);
	}

	void ApplyScaleBoxProps(UWidget* W, const FUMGPropertyBag& P)
	{
		UScaleBox* SB = CastChecked<UScaleBox>(W);
		FString Value;
		if (TryGetProp(P, TEXT("Stretch"), Value))
		{
			static const TMap<FString, EStretch::Type> StretchMap = {
				{TEXT("None"), EStretch::None}, {TEXT("Fill"), EStretch::Fill},
				{TEXT("ScaleToFit"), EStretch::ScaleToFit}, {TEXT("ScaleToFitX"), EStretch::ScaleToFitX},
				{TEXT("ScaleToFitY"), EStretch::ScaleToFitY}, {TEXT("ScaleToFill"), EStretch::ScaleToFill},
				{TEXT("UserSpecified"), EStretch::UserSpecified},
				{TEXT("UserSpecifiedWithClipping"), EStretch::UserSpecifiedWithClipping}
			};
			if (const EStretch::Type* S = StretchMap.Find(Value)) SB->SetStretch(*S);
		}
		if (TryGetProp(P, TEXT("StretchDirection"), Value))
		{
			static const TMap<FString, EStretchDirection::Type> DirMap = {
				{TEXT("Both"), EStretchDirection::Both}, {TEXT("DownOnly"), EStretchDirection::DownOnly}, {TEXT("UpOnly"), EStretchDirection::UpOnly}
			};
			if (const EStretchDirection::Type* D = DirMap.Find(Value)) SB->SetStretchDirection(*D);
		}
		float UserSpecifiedScale = 0.0f;
		if (P.TryGetFloat(TEXT("UserSpecifiedScale"), UserSpecifiedScale)) SB->SetUserSpecifiedScale(UserSpecifiedScale);
	}

	void ApplyWidgetSwitcherProps(UWidget* W, const FUMGPropertyBag& P)
	{
		UWidgetSwitcher* WS = CastChecked<UWidgetSwitcher>(W);
		int32 ActiveWidgetIndex = 0;
		if (P.TryGetInt(TEXT("ActiveWidgetIndex"), ActiveWidgetIndex)) WS->SetActiveWidgetIndex(ActiveWidgetIndex);
	}

	void ApplyExpandableAreaProps(UWidget* W, const FUMGPropertyBag& P)
	{
		UExpandableArea* EA = CastChecked<UExpandableArea>(W);
		bool bIsExpanded = false;
		if (P.TryGetBool(TEXT("IsExpanded"), bIsExpanded)) EA->SetIsExpanded(bIsExpanded);
	}

	void ApplyTileViewProps(UWidget* W, const FUMGPropertyBag& P)
	{
		UTileView* TV = CastChecked<UTileView>(W);
		float EntryWidth = 0.0f;
		if (P.TryGetFloat(TEXT("EntryWidth"), EntryWidth)) TV->SetEntryWidth(EntryWidth);
		float EntryHeight = 0.0f;
		if (P.TryGetFloat(TEXT("EntryHeight"), EntryHeight)) TV->SetEntryHeight(EntryHeight);
	}

	void ApplyMenuAnchorProps(UWidget* W, const FUMGPropertyBag& P)
	{
		UMenuAnchor* MA = CastChecked<UMenuAnchor>(W);
		FString Value;
		if (TryGetProp(P, TEXT("Placement"), Value))
		{
			static const TMap<FString, EMenuPlacement> PlacementMap = {
				{TEXT("MenuPlacement_BelowAnchor"), MenuPlacement_BelowAnchor},
				{TEXT("MenuPlacement_AboveAnchor"), MenuPlacement_AboveAnchor},
				{TEXT("MenuPlacement_ComboBox"), MenuPlacement_ComboBox},
				{TEXT("MenuPlacement_ComboBoxRight"), MenuPlacement_ComboBoxRight},
				{TEXT("MenuPlacement_MenuRight"), MenuPlacement_MenuRight},
				{TEXT("MenuPlacement_MenuLeft"), MenuPlacement_MenuLeft},
				{TEXT("BelowAnchor"), MenuPlacement_BelowAnchor},
				{TEXT("AboveAnchor"), MenuPlacement_AboveAnchor},
			};
			if (const EMenuPlacement* Pl = PlacementMap.Find(Value)) MA->SetPlacement(*Pl);
		}
	}

	void ApplySafeZoneProps(UWidget* W, const FUMGPropertyBag& P)
	{
		USafeZone* SZ = CastChecked<USafeZone>(W);
		bool bLeft = true, bRight = true, bTop = true, bBottom = true;
		P.TryGetBool(TEXT("PadLeft"), bLeft);
		P.TryGetBool(TEXT("PadRight"), bRight);
		P.TryGetBool(TEXT("PadTop"), bTop);
		P.TryGetBool(TEXT("PadBottom"), bBottom);
		SZ->SetSidesToPad(bLeft, bRight, bTop, bBottom);
	}

	// 属性应用函数映射表
	using FApplyPropsFunc = void(*)(UWidget*, const FUMGPropertyBag&);
	const TMap<UClass*, FApplyPropsFunc>& GetPropsApplierMap()
	{
		static const TMap<UClass*, FApplyPropsFunc> Map = {
			// 文本/显示类
			{UTextBlock::StaticClass(), &ApplyTextBlockProps},
			{URichTextBlock::StaticClass(), &ApplyRichTextBlockProps},
			{UImage::StaticClass(), &ApplyImageProps},
			{UProgressBar::StaticClass(), &ApplyProgressBarProps},
			{UThrobber::StaticClass(), &ApplyThrobberProps},
			{UCircularThrobber::StaticClass(), &ApplyCircularThrobberProps},
			{USpacer::StaticClass(), &ApplySpacerProps},
			// 交互类
			{UButton::StaticClass(), &ApplyButtonProps},
			{USlider::StaticClass(), &ApplySliderProps},
			{UCheckBox::StaticClass(), &ApplyCheckBoxProps},
			{UEditableTextBox::StaticClass(), &ApplyEditableTextBoxProps},
			{UMultiLineEditableTextBox::StaticClass(), &ApplyMultiLineEditableTextBoxProps},
			{UEditableText::StaticClass(), &ApplyEditableTextProps},
			{UMultiLineEditableText::StaticClass(), &ApplyMultiLineEditableTextProps},
			{UComboBoxString::StaticClass(), &ApplyComboBoxStringProps},
			{USpinBox::StaticClass(), &ApplySpinBoxProps},
			// 容器类
			{UGridPanel::StaticClass(), &ApplyGridPanelProps},
			{UUniformGridPanel::StaticClass(), &ApplyUniformGridPanelProps},
			{UWrapBox::StaticClass(), &ApplyWrapBoxProps},
			{UScrollBox::StaticClass(), &ApplyScrollBoxProps},
			{UWidgetSwitcher::StaticClass(), &ApplyWidgetSwitcherProps},
			// 包装类
			{UBorder::StaticClass(), &ApplyBorderProps},
			{USizeBox::StaticClass(), &ApplySizeBoxProps},
			{UScaleBox::StaticClass(), &ApplyScaleBoxProps},
			{UBackgroundBlur::StaticClass(), &ApplyBackgroundBlurProps},
			{URetainerBox::StaticClass(), &ApplyRetainerBoxProps},
			{UInvalidationBox::StaticClass(), &ApplyInvalidationBoxProps},
			{UExpandableArea::StaticClass(), &ApplyExpandableAreaProps},
			// 列表类
			{UTileView::StaticClass(), &ApplyTileViewProps},
			// 其他
			{UMenuAnchor::StaticClass(), &ApplyMenuAnchorProps},
			{USafeZone::StaticClass(), &ApplySafeZoneProps},
		};
		return Map;
	}

	void ApplyTypedBorderBrush(UBorder* Bd, const FUMGVisualBrushData& B)
	{
		FSlateBrush Brush = Bd->Background;
		bool bChanged = false;

		if (B.bHasDrawAs)
		{
			Brush.DrawAs = B.DrawAs;
			bChanged = true;
		}

		if (B.OutlineCornerRadius >= 0)
		{
			if (B.OutlineCornerRadius >= 999.0f)
				Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
			else
			{
				Brush.OutlineSettings.CornerRadii = FVector4(B.OutlineCornerRadius, B.OutlineCornerRadius, B.OutlineCornerRadius, B.OutlineCornerRadius);
				Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
			}
			bChanged = true;
		}

		if (B.HasCornerRadii())
		{
			const float MaxR = FMath::Max3(B.CornerRadii.X, B.CornerRadii.Y, FMath::Max(B.CornerRadii.Z, B.CornerRadii.W));
			if (MaxR >= 999.0f)
				Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
			else
			{
				Brush.OutlineSettings.CornerRadii = B.CornerRadii;
				Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
			}
			bChanged = true;
		}

		if (Brush.DrawAs == ESlateBrushDrawType::RoundedBox && B.OutlineCornerRadius < 0 && !B.HasCornerRadii())
		{
			Brush.OutlineSettings.CornerRadii = FVector4(0, 0, 0, 0);
			Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
			bChanged = true;
		}

		if (B.OutlineThickness > 0)
		{
			Brush.OutlineSettings.Width = B.OutlineThickness;
			bChanged = true;
		}

		if (B.bHasOutlineColor)
		{
			Brush.OutlineSettings.Color = FSlateColor(B.OutlineColor);
			bChanged = true;
		}

		if (bChanged)
			Bd->Background = Brush;

		if (B.bHasBrushColor)
			Bd->SetBrushColor(B.BrushColor);
	}
}

void FUMGWidgetUtils::ApplyCommonProperties(UWidget* W, const FUMGPropertyBag& Properties)
{
	FString StringValue;
	if (Properties.TryGetString(UMGPropertyKeys::Visibility, StringValue))
	{
		W->SetVisibility(FUMGValueParser::ParseVisibility(StringValue));
	}
	if (Properties.TryGetString(UMGPropertyKeys::Clipping, StringValue))
	{
		EWidgetClipping ClippingType;
		if (TryParseWidgetClipping(StringValue, ClippingType))
		{
			W->SetClipping(ClippingType);
		}
	}
	if (Properties.TryGetString(UMGPropertyKeys::ToolTipText, StringValue))
	{
		W->SetToolTipText(FText::FromString(StringValue));
	}
	bool bIsEnabled = false;
	if (Properties.TryGetBool(UMGPropertyKeys::IsEnabled, bIsEnabled)) W->SetIsEnabled(bIsEnabled);
	float RenderOpacity = 0.0f;
	if (Properties.TryGetFloat(UMGPropertyKeys::RenderOpacity, RenderOpacity)) W->SetRenderOpacity(RenderOpacity);
	float RenderAngle = 0.0f;
	if (Properties.TryGetFloat(UMGPropertyKeys::RenderAngle, RenderAngle)) W->SetRenderTransformAngle(RenderAngle);
	FVector2D RenderTransformPivot;
	if (Properties.TryGetVector2D(UMGPropertyKeys::RenderTransformPivot, RenderTransformPivot))
	{
		W->SetRenderTransformPivot(RenderTransformPivot);
	}
	FVector2D TransformScale;
	if (Properties.TryGetVector2D(UMGPropertyKeys::RenderTransformScale, TransformScale))
	{
		W->SetRenderScale(TransformScale);
	}
	FVector2D RenderScale;
	if (Properties.TryGetVector2D(UMGPropertyKeys::RenderScale, RenderScale)) W->SetRenderScale(RenderScale);
	FVector2D RenderTranslation;
	if (Properties.TryGetVector2D(UMGPropertyKeys::RenderTranslation, RenderTranslation)) W->SetRenderTranslation(RenderTranslation);
}

void FUMGWidgetUtils::ApplyWidgetProperties(UWidget* Widget, const FUMGPropertyBag& Properties)
{
	if (!Widget || Properties.Num() == 0) return;

	const auto& ApplierMap = GetPropsApplierMap();
	for (UClass* Class = Widget->GetClass(); Class; Class = Class->GetSuperClass())
	{
		if (const FApplyPropsFunc* Func = ApplierMap.Find(Class))
		{
			(*Func)(Widget, Properties);
			break;
		}
	}

	ApplyCommonProperties(Widget, Properties);
}

void FUMGWidgetUtils::ApplyWidgetProperties(UWidget* Widget, const FUMGWidgetNode& Node)
{
	if (!Widget) return;

	// TextBlock: typed FontSize / LetterSpacing 优先
	if (UTextBlock* T = Cast<UTextBlock>(Widget))
	{
		const FUMGTextStyleData& TS = Node.TextStyle;
		if (TS.FontSize > 0 || TS.bHasLetterSpacing)
		{
			FSlateFontInfo FontInfo = T->GetFont();
			bool bFontChanged = false;

			if (TS.FontSize > 0)
			{
				FontInfo.Size = FMath::Max(1, FMath::RoundToInt(TS.FontSize));
				bFontChanged = true;
			}

			if (TS.bHasLetterSpacing && (FontInfo.Size > 0 || TS.SourceFontSize > 0))
			{
				FontInfo.LetterSpacing = TextStyleUtils::ConvertLetterSpacingPxToTracking(TS.LetterSpacing, TS.SourceFontSize, FontInfo.Size);
				bFontChanged = true;
			}

			if (bFontChanged)
				T->SetFont(FontInfo);
		}
	}

	// Border: typed Brush 属性优先
	if (UBorder* Bd = Cast<UBorder>(Widget))
	{
		ApplyTypedBorderBrush(Bd, Node.Brush);
	}

	// legacy 流程处理剩余属性
	ApplyWidgetProperties(Widget, Node.Properties);
}


