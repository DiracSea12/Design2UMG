// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGWidgetUtils.h"
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
	#define GET_PROP(Key) P.Find(TEXT(Key))

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
		const TMap<FString, FString>& P,
		TFunctionRef<void(UTexture2D*)> ApplyCroppedTexture)
	{
		const FString* Mode = P.Find(TEXT("ImageMode"));
		if (!Mode)
		{
			return false;
		}

		if (*Mode == TEXT("Crop"))
		{
			const FString* TransformStr = P.Find(TEXT("ImageTransform"));
			if (!TransformStr)
			{
				return false;
			}

			TArray<FString> Parts;
			TransformStr->ParseIntoArray(Parts, TEXT(","));
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
				const int32 CropW = FMath::Max(1, P.Contains(TEXT("WidthOverride")) ? FCString::Atoi(*P[TEXT("WidthOverride")]) : 64);
				const int32 CropH = FMath::Max(1, P.Contains(TEXT("HeightOverride")) ? FCString::Atoi(*P[TEXT("HeightOverride")]) : 64);
				if (UTexture2D* Cropped = FProceduralShapeGenerator::ApplyAffineCrop(SrcTex, A, B, TX, C, D, TY, CropW, CropH))
				{
					ApplyCroppedTexture(Cropped);
					return true;
				}
			}

			return false;
		}

		if (*Mode == TEXT("Fit"))
		{
			Brush.Tiling = ESlateBrushTileType::NoTile;
			return true;
		}

		return false;
	}

	void ApplyTextBlockProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UTextBlock* T = CastChecked<UTextBlock>(W);
		if (const FString* V = GET_PROP("Text")) T->SetText(FText::FromString(*V));
		if (const FString* V = GET_PROP("Color")) T->SetColorAndOpacity(FSlateColor(FUMGValueParser::ParseColor(*V)));
		FSlateFontInfo FontInfo = T->GetFont();
		bool bFontChanged = false;
		const bool bIsIconFontWidget = P.Contains(TEXT("IconName"));
		if (const FString* V = GET_PROP("FontSize"))
		{
			const float DisplaySize = FCString::Atof(**V);
			FontInfo.Size = FMath::Max(1, FMath::RoundToInt(DisplaySize));
			bFontChanged = true;
		}
		if (const FString* V = GET_PROP("FontFamily"))
		{
			if (V->StartsWith(TEXT("/")))
			{
				if (UObject* FontObject = LoadObject<UObject>(nullptr, **V))
				{
					FontInfo.FontObject = FontObject;
				}
			}
			else if (!V->IsEmpty() && !bIsIconFontWidget)
			{
				FontInfo.TypefaceFontName = FName(**V);
			}
			bFontChanged = true;
		}
		if (const FString* V = GET_PROP("TypefaceFontName"))
		{
			FontInfo.TypefaceFontName = FName(**V);
			bFontChanged = true;
		}
		if (const FString* V = GET_PROP("LetterSpacing"))
		{
			// .pen letterSpacing is in px on the original design font size.
			// UE tracking is 1/1000 em, so the correct basis is the source font size,
			// not the scaled UMG font size.
			const float PixelSpacing = FCString::Atof(**V);
			const float DesignFontSize = GET_PROP("DesignFontSize")
				? FCString::Atof(**GET_PROP("DesignFontSize"))
				: 0.0f;
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
		if (const FString* V = GET_PROP("Justification"))
		{
			static const TMap<FString, ETextJustify::Type> JustifyMap = {
				{TEXT("Left"), ETextJustify::Left}, {TEXT("Center"), ETextJustify::Center}, {TEXT("Right"), ETextJustify::Right}
			};
			if (const ETextJustify::Type* J = JustifyMap.Find(*V)) T->SetJustification(*J);
		}
		if (const FString* V = GET_PROP("LineHeightPercentage"))
		{
			T->SetLineHeightPercentage(FCString::Atof(**V));
			T->SetApplyLineHeightToBottomLine(true);
		}
		if (const FString* V = GET_PROP("AutoWrapText"))
			T->SetAutoWrapText(V->ToBool());
		if (const FString* V = GET_PROP("WrapTextAt"))
			T->SetWrapTextAt(FCString::Atof(**V));
		if (const FString* V = GET_PROP("ShadowOffset"))
			T->SetShadowOffset(FUMGValueParser::ParseVector2D(*V));
		if (const FString* V = GET_PROP("ShadowColorAndOpacity"))
			T->SetShadowColorAndOpacity(FUMGValueParser::ParseColor(*V));
		if (bIsIconFontWidget)
		{
			FShapedTextOptions IconShaping;
			IconShaping.bOverride_TextShapingMethod = true;
			IconShaping.TextShapingMethod = ETextShapingMethod::FullShaping;
			T->SetShapedTextOptions(IconShaping);
		}
	}

	void ApplyButtonProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UButton* B = CastChecked<UButton>(W);
		FButtonStyle S = B->GetStyle();
		if (const FString* V = GET_PROP("BackgroundColor"))
		{
			FLinearColor Color = FUMGValueParser::ParseColor(*V);
			S.Normal.TintColor = FSlateColor(Color);
			S.Hovered.TintColor = FSlateColor(Color * 1.1f);
			S.Pressed.TintColor = FSlateColor(Color * 0.9f);
		}
		if (const FString* V = GET_PROP("HoveredColor")) S.Hovered.TintColor = FSlateColor(FUMGValueParser::ParseColor(*V));
		if (const FString* V = GET_PROP("PressedColor")) S.Pressed.TintColor = FSlateColor(FUMGValueParser::ParseColor(*V));
		B->SetStyle(S);
	}

	void ApplyImageProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UImage* I = CastChecked<UImage>(W);
		const FString* ColorVal = GET_PROP("Color");
		if (!ColorVal) ColorVal = GET_PROP("ColorAndOpacity");
		if (ColorVal) I->SetColorAndOpacity(FUMGValueParser::ParseColor(*ColorVal));

		if (const FString* V = GET_PROP("BrushColor")) I->SetBrushTintColor(FSlateColor(FUMGValueParser::ParseColor(*V)));
		if (const FString* V = GET_PROP("MaterialRef"))
		{
			if (UTexture2D* GeneratedTex = FProceduralShapeGenerator::GenerateFromMaterialRef(*V, P, I))
			{
				I->SetBrushFromTexture(GeneratedTex);
				FSlateBrush GeneratedBrush = I->GetBrush();
				const float DesiredWidth = GET_PROP("WidthOverride")
					? FCString::Atof(**GET_PROP("WidthOverride"))
					: static_cast<float>(GeneratedTex->GetSizeX());
				const float DesiredHeight = GET_PROP("HeightOverride")
					? FCString::Atof(**GET_PROP("HeightOverride"))
					: static_cast<float>(GeneratedTex->GetSizeY());
				GeneratedBrush.ImageSize = FVector2D(DesiredWidth, DesiredHeight);
				GeneratedBrush.Tiling = ESlateBrushTileType::NoTile;
				I->SetBrush(GeneratedBrush);
				I->SetDesiredSizeOverride(GeneratedBrush.ImageSize);
				return;
			}
		}
		if (const FString* Geom = GET_PROP("PathGeometry"))
		{
			const FString FillRuleStr = P.Contains(TEXT("PathFillRule")) ? P[TEXT("PathFillRule")] : TEXT("nonzero");
			const FString ColorStr = P.Contains(TEXT("PathFillColor")) ? P[TEXT("PathFillColor")] : TEXT("#ffffffff");
			FLinearColor PathColor = FUMGValueParser::ParseColor(ColorStr);

			int32 PW = FMath::Max(1, P.Contains(TEXT("WidthOverride")) ? FCString::Atoi(*P[TEXT("WidthOverride")]) : 64);
			int32 PH = FMath::Max(1, P.Contains(TEXT("HeightOverride")) ? FCString::Atoi(*P[TEXT("HeightOverride")]) : 64);
			constexpr int32 PathSupersample = 2;
			const int32 RasterW = PW * PathSupersample;
			const int32 RasterH = PH * PathSupersample;

			if (UTexture2D* Tex = FDesignPathRasterizer::Rasterize(*Geom, RasterW, RasterH, PathColor, FillRuleStr))
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

		if (const FString* V = GET_PROP("TexturePath"))
		{
			UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, **V);
			if (!Tex) Tex = FindObject<UTexture2D>(nullptr, **V);
			if (Tex) I->SetBrushFromTexture(Tex);
		}

		FSlateBrush Brush = I->GetBrush();
		bool bBrushModified = false;
		if (const FString* V = GET_PROP("WidthOverride"))
		{
			Brush.ImageSize.X = FCString::Atof(**V);
			bBrushModified = true;
		}
		if (const FString* V = GET_PROP("HeightOverride"))
		{
			Brush.ImageSize.Y = FCString::Atof(**V);
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
			if (Brush.Tiling == ESlateBrushTileType::Both && !P.Contains(TEXT("ImageMode")))
				Brush.Tiling = ESlateBrushTileType::NoTile;
			I->SetBrush(Brush);
			if (Brush.ImageSize.X > 0 && Brush.ImageSize.Y > 0)
				I->SetDesiredSizeOverride(Brush.ImageSize);
		}
	}

	void ApplyProgressBarProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UProgressBar* PB = CastChecked<UProgressBar>(W);
		if (const FString* V = GET_PROP("Percent")) PB->SetPercent(FCString::Atof(**V));
		if (const FString* V = GET_PROP("FillColor")) PB->SetFillColorAndOpacity(FUMGValueParser::ParseColor(*V));
	}

	void ApplyThrobberProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UThrobber* T = CastChecked<UThrobber>(W);
		if (const FString* V = GET_PROP("NumberOfPieces")) T->SetNumberOfPieces(FCString::Atoi(**V));
		if (const FString* V = GET_PROP("Animate"))
		{
			const bool bAnimate = V->ToBool();
			T->SetAnimateHorizontally(bAnimate);
			T->SetAnimateVertically(bAnimate);
			T->SetAnimateOpacity(bAnimate);
		}
		if (const FString* V = GET_PROP("AnimateHorizontally")) T->SetAnimateHorizontally(V->ToBool());
		if (const FString* V = GET_PROP("AnimateVertically")) T->SetAnimateVertically(V->ToBool());
		if (const FString* V = GET_PROP("AnimateOpacity")) T->SetAnimateOpacity(V->ToBool());
	}

	void ApplyCircularThrobberProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UCircularThrobber* T = CastChecked<UCircularThrobber>(W);
		if (const FString* V = GET_PROP("NumberOfPieces")) T->SetNumberOfPieces(FCString::Atoi(**V));
		if (const FString* V = GET_PROP("Period")) T->SetPeriod(FCString::Atof(**V));
		if (const FString* V = GET_PROP("Radius")) T->SetRadius(FCString::Atof(**V));
	}

	void ApplySliderProps(UWidget* W, const TMap<FString, FString>& P)
	{
		if (const FString* V = GET_PROP("Value")) CastChecked<USlider>(W)->SetValue(FCString::Atof(**V));
	}

	void ApplyCheckBoxProps(UWidget* W, const TMap<FString, FString>& P)
	{
		if (const FString* V = GET_PROP("IsChecked")) CastChecked<UCheckBox>(W)->SetIsChecked(V->ToBool());
	}

	void ApplyEditableTextBoxProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UEditableTextBox* E = CastChecked<UEditableTextBox>(W);
		if (const FString* V = GET_PROP("Text")) E->SetText(FText::FromString(*V));
		if (const FString* V = GET_PROP("HintText")) E->SetHintText(FText::FromString(*V));
	}

	void ApplyMultiLineEditableTextBoxProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UMultiLineEditableTextBox* M = CastChecked<UMultiLineEditableTextBox>(W);
		if (const FString* V = GET_PROP("Text")) M->SetText(FText::FromString(*V));
		if (const FString* V = GET_PROP("HintText")) M->SetHintText(FText::FromString(*V));
	}

	void ApplyEditableTextProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UEditableText* E = CastChecked<UEditableText>(W);
		if (const FString* V = GET_PROP("Text")) E->SetText(FText::FromString(*V));
		if (const FString* V = GET_PROP("HintText")) E->SetHintText(FText::FromString(*V));
		if (const FString* V = GET_PROP("IsReadOnly")) E->SetIsReadOnly(V->ToBool());
		if (const FString* V = GET_PROP("IsPassword")) E->SetIsPassword(V->ToBool());
		if (const FString* V = GET_PROP("MinimumDesiredWidth")) E->SetMinimumDesiredWidth(FCString::Atof(**V));
	}

	void ApplyMultiLineEditableTextProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UMultiLineEditableText* M = CastChecked<UMultiLineEditableText>(W);
		if (const FString* V = GET_PROP("Text")) M->SetText(FText::FromString(*V));
		if (const FString* V = GET_PROP("HintText")) M->SetHintText(FText::FromString(*V));
		if (const FString* V = GET_PROP("IsReadOnly")) M->SetIsReadOnly(V->ToBool());
	}

	void ApplyBorderProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UBorder* Bd = CastChecked<UBorder>(W);
		Bd->SetPadding(FMargin(0.0f));
		FSlateBrush Brush = Bd->Background;
		bool bBrushChanged = false;
		const bool bHasCornerRadiusProp = P.Contains(TEXT("OutlineCornerRadius")) || P.Contains(TEXT("CornerRadii"));

		if (const FString* V = GET_PROP("TexturePath"))
		{
			UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, **V);
			if (!Tex) Tex = FindObject<UTexture2D>(nullptr, **V);
			if (Tex)
			{
				Brush.SetResourceObject(Tex);
				Brush.DrawAs = ESlateBrushDrawType::Image;
				Brush.ImageSize = FVector2D(Tex->GetSizeX(), Tex->GetSizeY());
				bBrushChanged = true;
			}
		}

		if (const FString* V = GET_PROP("MaterialRef"))
		{
			if (UTexture2D* GeneratedTex = FProceduralShapeGenerator::GenerateFromMaterialRef(*V, P, Bd))
			{
				Brush.SetResourceObject(GeneratedTex);
				Brush.DrawAs = ESlateBrushDrawType::Image;
				Brush.ImageSize = FVector2D(GeneratedTex->GetSizeX(), GeneratedTex->GetSizeY());
				bBrushChanged = true;
			}
		}

		if (const FString* V = GET_PROP("DrawAs"))
		{
			ESlateBrushDrawType::Type DrawAsType;
			if (TryParseDrawAs(*V, DrawAsType))
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

		if (const FString* V = GET_PROP("OutlineCornerRadius"))
		{
			const float Radius = FMath::Max(0.0f, FCString::Atof(**V));
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

		if (const FString* V = GET_PROP("CornerRadii"))
		{
			FVector4 CornerRadii;
			if (TryParseVector4(*V, CornerRadii))
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
		}

		if (const FString* V = GET_PROP("OutlineThickness"))
		{
			Brush.OutlineSettings.Width = FMath::Max(0.0f, FCString::Atof(**V));
			bBrushChanged = true;
		}

		if (const FString* V = GET_PROP("OutlineColor"))
		{
			Brush.OutlineSettings.Color = FSlateColor(FUMGValueParser::ParseColor(*V));
			bBrushChanged = true;
		}

		if (const FString* V = GET_PROP("BrushColor"))
		{
			FLinearColor Color = FUMGValueParser::ParseColor(*V);
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
			Bd->SetBrushColor(Color);
		}

		bBrushChanged = ApplyImageModeToSlateBrush(Brush, P, [&Brush](UTexture2D* Cropped)
		{
			Brush.SetResourceObject(Cropped);
		}) || bBrushChanged;

		if (bBrushChanged)
		{
			Bd->SetBrush(Brush);
		}

		if (const FString* V = GET_PROP("ContentColor")) Bd->SetContentColorAndOpacity(FUMGValueParser::ParseColor(*V));
		if (const FString* V = GET_PROP("Padding")) Bd->SetPadding(FUMGValueParser::ParseMargin(*V));

		// StrokePadding: additional inset for inside-aligned strokes
		if (const FString* V = GET_PROP("StrokePadding"))
		{
			FMargin Extra = FUMGValueParser::ParseMargin(*V);
			FMargin Current = Bd->GetPadding();
			Bd->SetPadding(FMargin(Current.Left + Extra.Left, Current.Top + Extra.Top,
				Current.Right + Extra.Right, Current.Bottom + Extra.Bottom));
		}
	}

	void ApplySizeBoxProps(UWidget* W, const TMap<FString, FString>& P)
	{
		USizeBox* SB = CastChecked<USizeBox>(W);
		if (const FString* V = GET_PROP("WidthOverride")) SB->SetWidthOverride(FCString::Atof(**V));
		if (const FString* V = GET_PROP("HeightOverride")) SB->SetHeightOverride(FCString::Atof(**V));
		if (const FString* V = GET_PROP("MinDesiredWidth")) SB->SetMinDesiredWidth(FCString::Atof(**V));
		if (const FString* V = GET_PROP("MinDesiredHeight")) SB->SetMinDesiredHeight(FCString::Atof(**V));
		if (const FString* V = GET_PROP("MaxDesiredWidth")) SB->SetMaxDesiredWidth(FCString::Atof(**V));
		if (const FString* V = GET_PROP("MaxDesiredHeight")) SB->SetMaxDesiredHeight(FCString::Atof(**V));
	}

	void ApplyScrollBoxProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UScrollBox* Sc = CastChecked<UScrollBox>(W);
		if (const FString* V = GET_PROP("Orientation"))
			Sc->SetOrientation(*V == TEXT("Horizontal") ? Orient_Horizontal : Orient_Vertical);
		if (const FString* V = GET_PROP("ScrollBarVisibility"))
		{
			static const TMap<FString, ESlateVisibility> VisMap = {
				{TEXT("Visible"), ESlateVisibility::Visible}, {TEXT("Collapsed"), ESlateVisibility::Collapsed}, {TEXT("Hidden"), ESlateVisibility::Hidden}
			};
			if (const ESlateVisibility* Vis = VisMap.Find(*V)) Sc->SetScrollBarVisibility(*Vis);
		}
	}

	void ApplySpacerProps(UWidget* W, const TMap<FString, FString>& P)
	{
		FVector2D Size(0, 0);
		if (const FString* V = GET_PROP("Width")) Size.X = FCString::Atof(**V);
		if (const FString* V = GET_PROP("Height")) Size.Y = FCString::Atof(**V);
		CastChecked<USpacer>(W)->SetSize(Size);
	}

	void ApplyRichTextBlockProps(UWidget* W, const TMap<FString, FString>& P)
	{
		if (const FString* V = GET_PROP("Text")) CastChecked<URichTextBlock>(W)->SetText(FText::FromString(*V));
	}

	void ApplyComboBoxStringProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UComboBoxString* CB = CastChecked<UComboBoxString>(W);
		if (const FString* V = GET_PROP("Options"))
		{
			TArray<FString> Opts;
			V->ParseIntoArray(Opts, TEXT(","));
			for (const FString& O : Opts) CB->AddOption(O.TrimStartAndEnd());
		}
		if (const FString* V = GET_PROP("SelectedOption")) CB->SetSelectedOption(*V);
	}

	void ApplySpinBoxProps(UWidget* W, const TMap<FString, FString>& P)
	{
		USpinBox* SpB = CastChecked<USpinBox>(W);
		if (const FString* V = GET_PROP("Value")) SpB->SetValue(FCString::Atof(**V));
		if (const FString* V = GET_PROP("MinValue")) SpB->SetMinValue(FCString::Atof(**V));
		if (const FString* V = GET_PROP("MaxValue")) SpB->SetMaxValue(FCString::Atof(**V));
	}

	void ApplyBackgroundBlurProps(UWidget* W, const TMap<FString, FString>& P)
	{
		if (const FString* V = GET_PROP("BlurStrength")) CastChecked<UBackgroundBlur>(W)->SetBlurStrength(FCString::Atof(**V));
	}

	void ApplyRetainerBoxProps(UWidget* W, const TMap<FString, FString>& P)
	{
		URetainerBox* RB = CastChecked<URetainerBox>(W);
		if (const FString* V = GET_PROP("RetainRendering")) RB->SetRetainRendering(V->ToBool());
		int32 Phase = RB->GetPhase();
		int32 PhaseCount = RB->GetPhaseCount();
		bool bHasPhase = false;
		if (const FString* V = GET_PROP("Phase"))
		{
			Phase = FCString::Atoi(**V);
			bHasPhase = true;
		}
		if (const FString* V = GET_PROP("PhaseCount"))
		{
			PhaseCount = FMath::Max(1, FCString::Atoi(**V));
			bHasPhase = true;
		}
		if (bHasPhase) RB->SetRenderingPhase(Phase, PhaseCount);
	}

	void ApplyInvalidationBoxProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UInvalidationBox* IB = CastChecked<UInvalidationBox>(W);
		if (const FString* V = GET_PROP("CanCache")) IB->SetCanCache(V->ToBool());
	}

	void ApplyGridPanelProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UGridPanel* G = CastChecked<UGridPanel>(W);
		if (const FString* V = GET_PROP("ColumnFill"))
		{
			TArray<FString> Fills;
			V->ParseIntoArray(Fills, TEXT(","));
			for (int32 i = 0; i < Fills.Num(); ++i)
			{
				G->SetColumnFill(i, FCString::Atof(*Fills[i].TrimStartAndEnd()));
			}
		}
		if (const FString* V = GET_PROP("RowFill"))
		{
			TArray<FString> Fills;
			V->ParseIntoArray(Fills, TEXT(","));
			for (int32 i = 0; i < Fills.Num(); ++i)
			{
				G->SetRowFill(i, FCString::Atof(*Fills[i].TrimStartAndEnd()));
			}
		}
	}

	void ApplyUniformGridPanelProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UUniformGridPanel* UG = CastChecked<UUniformGridPanel>(W);
		if (const FString* V = GET_PROP("SlotPadding")) UG->SetSlotPadding(FUMGValueParser::ParseMargin(*V));
		if (const FString* V = GET_PROP("MinDesiredSlotWidth")) UG->SetMinDesiredSlotWidth(FCString::Atof(**V));
		if (const FString* V = GET_PROP("MinDesiredSlotHeight")) UG->SetMinDesiredSlotHeight(FCString::Atof(**V));
	}

	void ApplyWrapBoxProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UWrapBox* WB = CastChecked<UWrapBox>(W);
		if (const FString* V = GET_PROP("InnerSlotPadding")) WB->SetInnerSlotPadding(FUMGValueParser::ParseVector2D(*V));
		if (const FString* V = GET_PROP("Orientation"))
			WB->SetOrientation(*V == TEXT("Vertical") ? Orient_Vertical : Orient_Horizontal);
	}

	void ApplyScaleBoxProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UScaleBox* SB = CastChecked<UScaleBox>(W);
		if (const FString* V = GET_PROP("Stretch"))
		{
			static const TMap<FString, EStretch::Type> StretchMap = {
				{TEXT("None"), EStretch::None}, {TEXT("Fill"), EStretch::Fill},
				{TEXT("ScaleToFit"), EStretch::ScaleToFit}, {TEXT("ScaleToFitX"), EStretch::ScaleToFitX},
				{TEXT("ScaleToFitY"), EStretch::ScaleToFitY}, {TEXT("ScaleToFill"), EStretch::ScaleToFill},
				{TEXT("UserSpecified"), EStretch::UserSpecified},
				{TEXT("UserSpecifiedWithClipping"), EStretch::UserSpecifiedWithClipping}
			};
			if (const EStretch::Type* S = StretchMap.Find(*V)) SB->SetStretch(*S);
		}
		if (const FString* V = GET_PROP("StretchDirection"))
		{
			static const TMap<FString, EStretchDirection::Type> DirMap = {
				{TEXT("Both"), EStretchDirection::Both}, {TEXT("DownOnly"), EStretchDirection::DownOnly}, {TEXT("UpOnly"), EStretchDirection::UpOnly}
			};
			if (const EStretchDirection::Type* D = DirMap.Find(*V)) SB->SetStretchDirection(*D);
		}
		if (const FString* V = GET_PROP("UserSpecifiedScale")) SB->SetUserSpecifiedScale(FCString::Atof(**V));
	}

	void ApplyWidgetSwitcherProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UWidgetSwitcher* WS = CastChecked<UWidgetSwitcher>(W);
		if (const FString* V = GET_PROP("ActiveWidgetIndex")) WS->SetActiveWidgetIndex(FCString::Atoi(**V));
	}

	void ApplyExpandableAreaProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UExpandableArea* EA = CastChecked<UExpandableArea>(W);
		if (const FString* V = GET_PROP("IsExpanded")) EA->SetIsExpanded(V->ToBool());
	}

	void ApplyTileViewProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UTileView* TV = CastChecked<UTileView>(W);
		if (const FString* V = GET_PROP("EntryWidth")) TV->SetEntryWidth(FCString::Atof(**V));
		if (const FString* V = GET_PROP("EntryHeight")) TV->SetEntryHeight(FCString::Atof(**V));
	}

	void ApplyMenuAnchorProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UMenuAnchor* MA = CastChecked<UMenuAnchor>(W);
		if (const FString* V = GET_PROP("Placement"))
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
			if (const EMenuPlacement* Pl = PlacementMap.Find(*V)) MA->SetPlacement(*Pl);
		}
	}

	void ApplySafeZoneProps(UWidget* W, const TMap<FString, FString>& P)
	{
		USafeZone* SZ = CastChecked<USafeZone>(W);
		bool bLeft = true, bRight = true, bTop = true, bBottom = true;
		if (const FString* V = GET_PROP("PadLeft")) bLeft = V->ToBool();
		if (const FString* V = GET_PROP("PadRight")) bRight = V->ToBool();
		if (const FString* V = GET_PROP("PadTop")) bTop = V->ToBool();
		if (const FString* V = GET_PROP("PadBottom")) bBottom = V->ToBool();
		SZ->SetSidesToPad(bLeft, bRight, bTop, bBottom);
	}

	#undef GET_PROP

	// 属性应用函数映射表
	using FApplyPropsFunc = void(*)(UWidget*, const TMap<FString, FString>&);
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

void FUMGWidgetUtils::ApplyCommonProperties(UWidget* W, const TMap<FString, FString>& Properties)
{
	#define GET_PROP(Key) Properties.Find(TEXT(Key))
	if (const FString* V = GET_PROP("Visibility")) W->SetVisibility(FUMGValueParser::ParseVisibility(*V));
	if (const FString* V = GET_PROP("Clipping"))
	{
		EWidgetClipping ClippingType;
		if (TryParseWidgetClipping(*V, ClippingType))
		{
			W->SetClipping(ClippingType);
		}
	}
	if (const FString* V = GET_PROP("ToolTipText")) W->SetToolTipText(FText::FromString(*V));
	if (const FString* V = GET_PROP("IsEnabled")) W->SetIsEnabled(V->ToBool());
	if (const FString* V = GET_PROP("RenderOpacity")) W->SetRenderOpacity(FCString::Atof(**V));
	if (const FString* V = GET_PROP("RenderAngle")) W->SetRenderTransformAngle(FCString::Atof(**V));
	if (const FString* V = GET_PROP("RenderTransformScale"))
	{
		FVector2D Scale = FUMGValueParser::ParseVector2D(*V, FVector2D::UnitVector);
		W->SetRenderScale(Scale);
	}
	if (const FString* V = GET_PROP("RenderScale")) W->SetRenderScale(FUMGValueParser::ParseVector2D(*V, FVector2D::UnitVector));
	if (const FString* V = GET_PROP("RenderTranslation")) W->SetRenderTranslation(FUMGValueParser::ParseVector2D(*V));
	#undef GET_PROP
}

void FUMGWidgetUtils::ApplyWidgetProperties(UWidget* Widget, const TMap<FString, FString>& Properties)
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


