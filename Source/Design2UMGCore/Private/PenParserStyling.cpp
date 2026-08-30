// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "PenParserInternal.h"
#include "DesignSchema.h"
#include "DesignVariableResolver.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "ParserSharedSemantics.h"

namespace PenParser
{
namespace
{
	// Both Pencil gradient flavors parse stop colors the same way; they differ only
	// in how a missing stop position is handled, which stays at the call sites.
	FDesignGradientStop ParseGradientStopColor(const TSharedPtr<FJsonObject>& StopObj, const FDesignVariableResolver& Resolver)
	{
		FDesignGradientStop Stop;
		FString StopColorStr;
		if (StopObj->TryGetStringField(TEXT("color"), StopColorStr))
		{
			if (FDesignVariableResolver::IsVariableRef(StopColorStr))
				Stop.SourceVar = StopColorStr;
			Stop.Color = Resolver.ResolveColor(StopColorStr);
		}
		return Stop;
	}
}

FDesignFill ParseFill(const TSharedPtr<FJsonValue>& Value, const FDesignVariableResolver& Resolver)
{
	FDesignFill Result;

	if (!Value.IsValid())
		return Result;

	if (Value->Type == EJson::Array && Value->AsArray().Num() == 0)
	{
		Result.bExplicitClear = true;
		return Result;
	}

	if (Value->Type == EJson::Array)
	{
		const auto& Values = Value->AsArray();
		for (int32 i = Values.Num() - 1; i >= 0; --i)
		{
			FDesignFill Fill = ParseFill(Values[i], Resolver);
			if (Fill.Type != EDesignFillType::None && Fill.bEnabled)
				return Fill;
		}
		return Result;
	}

	if (Value->Type == EJson::String)
	{
		FString Str = Value->AsString();
		if (Str == TEXT("transparent"))
			return Result;
		Result.Type = EDesignFillType::Color;
		if (FDesignVariableResolver::IsVariableRef(Str))
			Result.SourceVar = Str;
		Result.SolidColor = Resolver.ResolveColor(Str);
		return Result;
	}

	if (Value->Type != EJson::Object)
		return Result;

	const TSharedPtr<FJsonObject> Obj = Value->AsObject();
	if (!Obj.IsValid())
		return Result;

	FString FillType;
	Obj->TryGetStringField(TEXT("type"), FillType);

	TryGetResolvedBoolField(Obj, TEXT("enabled"), Resolver, Result.bEnabled);
	TryGetResolvedNumberField(Obj, TEXT("opacity"), Resolver, Result.Opacity);
	Obj->TryGetStringField(TEXT("blendMode"), Result.BlendMode);

	if (FillType == TEXT("color"))
	{
		Result.Type = EDesignFillType::Color;
		FString ColorStr;
		if (Obj->TryGetStringField(TEXT("color"), ColorStr))
		{
			if (FDesignVariableResolver::IsVariableRef(ColorStr))
				Result.SourceVar = ColorStr;
			Result.SolidColor = Resolver.ResolveColor(ColorStr);
		}
		return Result;
	}

	if (FillType == TEXT("gradient"))
	{
		Result.Type = EDesignFillType::Gradient;
		FString GradType;
		if (Obj->TryGetStringField(TEXT("gradientType"), GradType))
		{
			if (GradType == TEXT("radial"))
				Result.Gradient.GradientType = EDesignGradientType::Radial;
			else if (GradType == TEXT("angular"))
				Result.Gradient.GradientType = EDesignGradientType::Angular;
		}
		TryGetResolvedNumberField(Obj, TEXT("rotation"), Resolver, Result.Gradient.Rotation);

		const TSharedPtr<FJsonValue>& CenterField = Obj->TryGetField(TEXT("center"));
		if (CenterField.IsValid() && CenterField->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject>& CenterObj = CenterField->AsObject();
			FVector2D Center = Result.Gradient.Center.Get(FVector2D(0.5f, 0.5f));
			const bool bHasCenterX = TryGetResolvedNumberField(CenterObj, TEXT("x"), Resolver, Center.X);
			const bool bHasCenterY = TryGetResolvedNumberField(CenterObj, TEXT("y"), Resolver, Center.Y);
			if (bHasCenterX || bHasCenterY)
			{
				Result.Gradient.Center = Center;
			}
		}

		const TSharedPtr<FJsonValue>& SizeField = Obj->TryGetField(TEXT("size"));
		if (SizeField.IsValid() && SizeField->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject>& SizeObj = SizeField->AsObject();
			FVector2D Size = Result.Gradient.Size.Get(FVector2D(1.0f, 1.0f));
			const bool bHasSizeW = TryGetResolvedNumberField(SizeObj, TEXT("width"), Resolver, Size.X);
			const bool bHasSizeH = TryGetResolvedNumberField(SizeObj, TEXT("height"), Resolver, Size.Y);
			if (bHasSizeW || bHasSizeH)
			{
				Result.Gradient.Size = Size;
			}
		}

		const TSharedPtr<FJsonValue>& ColorsField = Obj->TryGetField(TEXT("colors"));
		if (ColorsField.IsValid() && ColorsField->Type == EJson::Array)
		{
			for (const auto& StopVal : ColorsField->AsArray())
			{
				const auto& StopObj = StopVal->AsObject();
				if (!StopObj.IsValid())
					continue;
				FDesignGradientStop Stop = ParseGradientStopColor(StopObj, Resolver);
				TryGetResolvedNumberField(StopObj, TEXT("position"), Resolver, Stop.Position);
				Result.Gradient.Colors.Add(Stop);
			}
		}
		return Result;
	}

	if (FillType == TEXT("image"))
	{
		Result.Type = EDesignFillType::Image;
		Obj->TryGetStringField(TEXT("url"), Result.Image.Url);
		FString Mode;
		if (Obj->TryGetStringField(TEXT("mode"), Mode))
		{
			if (Mode == TEXT("fill"))
				Result.Image.Mode = EDesignImageMode::Fill;
			else if (Mode == TEXT("fit"))
				Result.Image.Mode = EDesignImageMode::Fit;
			else if (Mode == TEXT("crop"))
				Result.Image.Mode = EDesignImageMode::Crop;
		}
		if (!TryGetResolvedNumberField(Obj, TEXT("opacity"), Resolver, Result.Image.Opacity))
			Result.Image.Opacity = Result.Opacity;
		const TSharedPtr<FJsonValue>& TransformField = Obj->TryGetField(TEXT("imageTransform"));
		if (TransformField.IsValid() && TransformField->Type == EJson::Array)
		{
			ParserSharedSemantics::TryParseImageTransformMatrix(
				TransformField->AsArray(),
				[](const TSharedPtr<FJsonValue>& Value, float& OutNumber) { TryGetNumberValueFlexible(Value, OutNumber); },
				Result.Image.Transform);
		}
		return Result;
	}

	if (FillType == TEXT("mesh_gradient"))
	{
		Result.Type = EDesignFillType::Gradient;
		const TSharedPtr<FJsonValue>& ColorsField = Obj->TryGetField(TEXT("colors"));
		if (ColorsField.IsValid() && ColorsField->Type == EJson::Array)
		{
			for (const auto& StopVal : ColorsField->AsArray())
			{
				const auto& StopObj = StopVal->AsObject();
				if (!StopObj.IsValid())
					continue;

				FDesignGradientStop Stop = ParseGradientStopColor(StopObj, Resolver);
				if (!TryGetResolvedNumberField(StopObj, TEXT("position"), Resolver, Stop.Position))
					Stop.Position = Result.Gradient.Colors.Num() > 0 ? 1.0f : 0.0f;
				Result.Gradient.Colors.Add(Stop);
			}
		}
	}

	return Result;
}

FDesignPadding ParsePadding(const TSharedPtr<FJsonValue>& Value, const FDesignVariableResolver& Resolver)
{
	FDesignPadding Result;
	if (!Value.IsValid())
		return Result;

	auto ResolvePaddingNumber = [&Resolver](const TSharedPtr<FJsonValue>& Input, float& OutNumber) -> bool
	{
		if (!Input.IsValid())
			return false;
		if (TryGetNumberValueFlexible(Input, OutNumber))
			return true;
		if (Input->Type != EJson::String)
			return false;
		const FString Raw = Input->AsString();
		if (!FDesignVariableResolver::IsVariableRef(Raw))
			return false;
		OutNumber = Resolver.ResolveNumber(Raw);
		return true;
	};

	if (ResolvePaddingNumber(Value, Result.Top))
	{
		Result.Right = Result.Bottom = Result.Left = Result.Top;
	}
	else if (Value->Type == EJson::Array)
	{
		const auto& Values = Value->AsArray();
		if (Values.Num() == 2)
		{
			float Vertical = 0.0f;
			float Horizontal = 0.0f;
			if (ResolvePaddingNumber(Values[0], Vertical))
				Result.Top = Result.Bottom = Vertical;
			if (ResolvePaddingNumber(Values[1], Horizontal))
				Result.Left = Result.Right = Horizontal;
		}
		else if (Values.Num() >= 4)
		{
			ResolvePaddingNumber(Values[0], Result.Top);
			ResolvePaddingNumber(Values[1], Result.Right);
			ResolvePaddingNumber(Values[2], Result.Bottom);
			ResolvePaddingNumber(Values[3], Result.Left);
		}
	}
	return Result;
}
}
