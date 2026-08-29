// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGWidgetNodeAccess.h"

#include "GradientMaterialParams.h"
#include "UMGSchema.h"
#include "UMGValueParser.h"
#include "UMGWidgetNodeTypedPropertyAccess.h"

namespace
{
	FString SerializeWidgetFloat(const float Value)
	{
		return FString::SanitizeFloat(Value);
	}

	FString SerializeWidgetBool(const bool Value)
	{
		return Value ? TEXT("true") : TEXT("false");
	}

	FString SerializeWidgetVector4(const FVector4& Value)
	{
		return FString::Printf(TEXT("%.1f,%.1f,%.1f,%.1f"), Value.X, Value.Y, Value.Z, Value.W);
	}

	FString GetGradientMaterialRef(const FUMGVisualBrushData::FGradientData& Gradient)
	{
		if (Gradient.Type.IsSet() && Gradient.Type.GetValue().Equals(TEXT("Radial"), ESearchCase::IgnoreCase))
		{
			return TEXT("radialgradient");
		}
		if (Gradient.Type.IsSet() && Gradient.Type.GetValue().Equals(TEXT("Angular"), ESearchCase::IgnoreCase))
		{
			return TEXT("angulargradient");
		}
		return TEXT("gradientlinear");
	}

	FString SerializeGradientMaterialParams(const FUMGVisualBrushData::FGradientData& Gradient)
	{
		if (Gradient.Stops.Num() == 0)
		{
			return FString();
		}

		Design2UMGGradientMaterialParams::FGradientParams Params;
		Params.bRadial = Gradient.Type.IsSet() && Gradient.Type.GetValue().Equals(TEXT("Radial"), ESearchCase::IgnoreCase);
		const bool bIsLinear = !Gradient.Type.IsSet()
			|| Gradient.Type.GetValue().Equals(TEXT("Linear"), ESearchCase::IgnoreCase);
		Params.bHasLinearEndpoints = bIsLinear && Gradient.StartPoint.IsSet() && Gradient.EndPoint.IsSet();
		Params.StartPoint = Gradient.StartPoint.Get(FVector2D::ZeroVector);
		Params.EndPoint = Gradient.EndPoint.Get(FVector2D(1.0f, 0.0f));
		Params.Center = Gradient.Center.Get(FVector2D(0.5f, 0.5f));
		Params.Size = Gradient.Size.Get(FVector2D(1.0f, 1.0f));
		Params.Rotation = Gradient.Rotation.Get(0.0f);
		for (const FUMGVisualBrushData::FGradientStopData& Stop : Gradient.Stops)
		{
			Params.Stops.Add({Stop.Position, Design2UMGWidgetNodeAccess::SerializeWidgetColor(Stop.Color)});
		}
		return Design2UMGGradientMaterialParams::BuildGradientMaterialParamsJson(Params);
	}
}

FString Design2UMGWidgetNodeAccess::SerializeWidgetFloat(const float Value)
{
	return ::SerializeWidgetFloat(Value);
}

FString Design2UMGWidgetNodeAccess::SerializeWidgetBool(const bool Value)
{
	return ::SerializeWidgetBool(Value);
}

FString Design2UMGWidgetNodeAccess::SerializeWidgetVector2D(const FVector2D& Value)
{
	return FString::Printf(TEXT("%.6f,%.6f"), Value.X, Value.Y);
}

FString Design2UMGWidgetNodeAccess::SerializeWidgetCompactVector2D(const FVector2D& Value)
{
	return FString::Printf(TEXT("%.1f,%.1f"), Value.X, Value.Y);
}

FString Design2UMGWidgetNodeAccess::SerializeWidgetMargin(const FMargin& Value)
{
	return FString::Printf(TEXT("%.1f,%.1f,%.1f,%.1f"), Value.Left, Value.Top, Value.Right, Value.Bottom);
}

FString Design2UMGWidgetNodeAccess::SerializeWidgetColor(const FLinearColor& Value)
{
	return FString::Printf(TEXT("#%s"), *Value.ToFColor(true).ToHex());
}

FString Design2UMGWidgetNodeAccess::SerializeWidgetVector4(const FVector4& Value)
{
	return ::SerializeWidgetVector4(Value);
}

FString Design2UMGWidgetNodeAccess::SerializeImageTransform(const FUMGVisualBrushData::FImageTransformData& Value)
{
	return FString::Printf(
		TEXT("%.6f,%.6f,%.6f,%.6f,%.6f,%.6f"),
		Value.A,
		Value.B,
		Value.TX,
		Value.C,
		Value.D,
			Value.TY);
}

bool Design2UMGWidgetNodeAccess::TryParseImageTransform(const FString& Value, FUMGVisualBrushData::FImageTransformData& OutTransform)
{
	TArray<FString> Parts;
	Value.ParseIntoArray(Parts, TEXT(","), true);
	if (Parts.Num() != 6)
	{
		return false;
	}

	OutTransform.A = FCString::Atof(*Parts[0].TrimStartAndEnd());
	OutTransform.B = FCString::Atof(*Parts[1].TrimStartAndEnd());
	OutTransform.TX = FCString::Atof(*Parts[2].TrimStartAndEnd());
	OutTransform.C = FCString::Atof(*Parts[3].TrimStartAndEnd());
	OutTransform.D = FCString::Atof(*Parts[4].TrimStartAndEnd());
	OutTransform.TY = FCString::Atof(*Parts[5].TrimStartAndEnd());
	return true;
}

FString Design2UMGWidgetNodeAccess::GetGradientMaterialRef(const FUMGVisualBrushData::FGradientData& Gradient)
{
	return ::GetGradientMaterialRef(Gradient);
}

FString Design2UMGWidgetNodeAccess::SerializeGradientMaterialParams(const FUMGVisualBrushData::FGradientData& Gradient)
{
	return ::SerializeGradientMaterialParams(Gradient);
}

FString Design2UMGWidgetNodeAccess::SerializeDrawAs(const ESlateBrushDrawType::Type DrawAs)
{
	switch (DrawAs)
	{
	case ESlateBrushDrawType::NoDrawType:
		return TEXT("NoDrawType");
	case ESlateBrushDrawType::Box:
		return TEXT("Box");
	case ESlateBrushDrawType::Border:
		return TEXT("Border");
	case ESlateBrushDrawType::Image:
		return TEXT("Image");
	case ESlateBrushDrawType::RoundedBox:
		return TEXT("RoundedBox");
	default:
		return FString();
	}
}

bool Design2UMGWidgetNodeAccess::TryParseDrawAs(const FString& Value, ESlateBrushDrawType::Type& OutDrawAs)
{
	static const TMap<FString, ESlateBrushDrawType::Type> DrawAsMap = {
		{ TEXT("NoDrawType"), ESlateBrushDrawType::NoDrawType },
		{ TEXT("Box"), ESlateBrushDrawType::Box },
		{ TEXT("Border"), ESlateBrushDrawType::Border },
		{ TEXT("Image"), ESlateBrushDrawType::Image },
		{ TEXT("RoundedBox"), ESlateBrushDrawType::RoundedBox },
	};

	if (const ESlateBrushDrawType::Type* Found = DrawAsMap.Find(Value))
	{
		OutDrawAs = *Found;
		return true;
	}

	return false;
}

bool Design2UMGWidgetNodeAccess::NormalizeBrushTexturePathOwner(FUMGVisualBrushData& Brush, FString* OutDiagnostic)
{
	if (OutDiagnostic)
	{
		OutDiagnostic->Reset();
	}

	if (!Brush.Image.TexturePath.IsSet())
	{
		if (Brush.TexturePath.IsEmpty())
		{
			return false;
		}

		Brush.Image.TexturePath = Brush.TexturePath;
		Brush.TexturePath.Empty();
		return true;
	}

	if (!Brush.TexturePath.IsEmpty() && Brush.TexturePath != Brush.Image.TexturePath.GetValue())
	{
		if (OutDiagnostic)
		{
			*OutDiagnostic = FString::Printf(
				TEXT("Brush.TexturePath conflicts with Brush.Image.TexturePath; using Brush.Image.TexturePath ('%s') instead of legacy/root '%s'."),
				*Brush.Image.TexturePath.GetValue(),
				*Brush.TexturePath);
		}
	}

	Brush.TexturePath.Empty();
	return true;
}

void Design2UMGWidgetNodeAccess::ForEachStaticTypedWidgetPropertyKey(TFunctionRef<void(const TCHAR*)> Visitor)
{
	Design2UMGWidgetNodeTypedPropertyAccess::ForEachStaticTypedWidgetPropertyKey(Visitor);
}

bool Design2UMGWidgetNodeAccess::IsTypedWidgetPropertyKey(const FString& Key)
{
	static const TSet<FString> TypedKeys = []()
	{
		TSet<FString> Result;
		Design2UMGWidgetNodeAccess::ForEachStaticTypedWidgetPropertyKey([&Result](const TCHAR* TypedKey)
		{
			Result.Add(TypedKey);
		});
		return Result;
	}();

	return TypedKeys.Contains(Key) || Key.StartsWith(TEXT("BoxShadow_"));
}

bool Design2UMGWidgetNodeAccess::TryGetTextStyleTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue)
{
	return Design2UMGWidgetNodeTypedPropertyAccess::TryGetTextStyleTypedWidgetString(Node, KeyString, OutValue);
}

bool Design2UMGWidgetNodeAccess::TryGetWidgetStyleTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue)
{
	return Design2UMGWidgetNodeTypedPropertyAccess::TryGetWidgetStyleTypedWidgetString(Node, KeyString, OutValue);
}

bool Design2UMGWidgetNodeAccess::TryGetTextTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue)
{
	return Design2UMGWidgetNodeTypedPropertyAccess::TryGetTextTypedWidgetString(Node, KeyString, OutValue);
}

bool Design2UMGWidgetNodeAccess::TryGetBrushTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue)
{
	return Design2UMGWidgetNodeTypedPropertyAccess::TryGetBrushTypedWidgetString(Node, KeyString, OutValue);
}

bool Design2UMGWidgetNodeAccess::TryGetEffectTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue)
{
	return Design2UMGWidgetNodeTypedPropertyAccess::TryGetEffectTypedWidgetString(Node, KeyString, OutValue);
}

bool Design2UMGWidgetNodeAccess::TryGetPathTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue)
{
	return Design2UMGWidgetNodeTypedPropertyAccess::TryGetPathTypedWidgetString(Node, KeyString, OutValue);
}

bool Design2UMGWidgetNodeAccess::TryGetShadowTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue)
{
	return Design2UMGWidgetNodeTypedPropertyAccess::TryGetShadowTypedWidgetString(Node, KeyString, OutValue);
}

bool Design2UMGWidgetNodeAccess::TryGetRenderTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue)
{
	return Design2UMGWidgetNodeTypedPropertyAccess::TryGetRenderTypedWidgetString(Node, KeyString, OutValue);
}

bool Design2UMGWidgetNodeAccess::TryGetWidgetStateTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue)
{
	return Design2UMGWidgetNodeTypedPropertyAccess::TryGetWidgetStateTypedWidgetString(Node, KeyString, OutValue);
}

bool Design2UMGWidgetNodeAccess::TryGetScrollBoxTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue)
{
	return Design2UMGWidgetNodeTypedPropertyAccess::TryGetScrollBoxTypedWidgetString(Node, KeyString, OutValue);
}

bool Design2UMGWidgetNodeAccess::TryGetTypedWidgetString(const FUMGWidgetNode& Node, const TCHAR* Key, FString& OutValue)
{
	return Design2UMGWidgetNodeTypedPropertyAccess::TryGetTypedWidgetString(Node, Key, OutValue);
}

bool Design2UMGWidgetNodeAccess::HasWidgetPropertyPresence(const FUMGWidgetNode& Node, const TCHAR* Key)
{
	FString Value;
	return TryGetTypedWidgetString(Node, Key, Value);
}

bool Design2UMGWidgetNodeAccess::TryGetWidgetBool(const FUMGWidgetNode& Node, const TCHAR* Key, bool& OutValue)
{
	FString TypedValue;
	if (TryGetTypedWidgetString(Node, Key, TypedValue) && !TypedValue.IsEmpty())
	{
		OutValue = TypedValue.Equals(TEXT("true"), ESearchCase::IgnoreCase) || TypedValue == TEXT("1");
		return true;
	}

	return false;
}

bool Design2UMGWidgetNodeAccess::TryGetWidgetInt(const FUMGWidgetNode& Node, const TCHAR* Key, int32& OutValue)
{
	FString TypedValue;
	if (TryGetTypedWidgetString(Node, Key, TypedValue) && !TypedValue.IsEmpty())
	{
		return LexTryParseString(OutValue, *TypedValue);
	}

	return false;
}

bool Design2UMGWidgetNodeAccess::TryGetWidgetFloat(const FUMGWidgetNode& Node, const TCHAR* Key, float& OutValue)
{
	FString TypedValue;
	if (TryGetTypedWidgetString(Node, Key, TypedValue) && !TypedValue.IsEmpty())
	{
		return LexTryParseString(OutValue, *TypedValue);
	}

	return false;
}

bool Design2UMGWidgetNodeAccess::TryGetWidgetVector2D(const FUMGWidgetNode& Node, const TCHAR* Key, FVector2D& OutValue)
{
	FString TypedValue;
	if (TryGetTypedWidgetString(Node, Key, TypedValue) && !TypedValue.IsEmpty())
	{
		OutValue = FUMGValueParser::ParseVector2D(TypedValue);
		return true;
	}

	return false;
}

bool Design2UMGWidgetNodeAccess::TryGetWidgetVector4(const FUMGWidgetNode& Node, const TCHAR* Key, FVector4& OutValue)
{
	FString TypedValue;
	if (TryGetTypedWidgetString(Node, Key, TypedValue) && !TypedValue.IsEmpty())
	{
		return Design2UMGWidgetNodeTypedPropertyAccess::TryParseWidgetVector4String(TypedValue, OutValue);
	}

	return false;
}

bool Design2UMGWidgetNodeAccess::TryGetWidgetMargin(const FUMGWidgetNode& Node, const TCHAR* Key, FMargin& OutValue)
{
	FString TypedValue;
	if (TryGetTypedWidgetString(Node, Key, TypedValue) && !TypedValue.IsEmpty())
	{
		OutValue = FUMGValueParser::ParseMargin(TypedValue);
		return true;
	}

	return false;
}

bool Design2UMGWidgetNodeAccess::TryGetWidgetColor(const FUMGWidgetNode& Node, const TCHAR* Key, FLinearColor& OutValue)
{
	FString TypedValue;
	if (TryGetTypedWidgetString(Node, Key, TypedValue) && !TypedValue.IsEmpty())
	{
		OutValue = FUMGValueParser::ParseColor(TypedValue);
		return true;
	}

	return false;
}
