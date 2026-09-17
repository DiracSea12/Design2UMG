// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "PenParserInternal.h"
#include "DesignSchema.h"
#include "DesignVariableResolver.h"
#include "DesignColorUtils.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "ParserSharedSemantics.h"
#include "Design2UMGLog.h"
#include "UMGValueParsing.h"

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

	// Splits "a, b(1, 2), c" on top-level commas only, keeping nested
	// function calls such as rgb()/rgba() colors intact.
	void SplitCssArgumentList(const FString& Args, TArray<FString>& OutParts)
	{
		int32 NestDepth = 0;
		int32 PartStart = 0;
		for (int32 Index = 0; Index < Args.Len(); ++Index)
		{
			const TCHAR Char = Args[Index];
			if (Char == TEXT('('))
			{
				++NestDepth;
			}
			else if (Char == TEXT(')'))
			{
				NestDepth = FMath::Max(0, NestDepth - 1);
			}
			else if (Char == TEXT(',') && NestDepth == 0)
			{
				OutParts.Add(Args.Mid(PartStart, Index - PartStart).TrimStartAndEnd());
				PartStart = Index + 1;
			}
		}
		OutParts.Add(Args.Mid(PartStart).TrimStartAndEnd());
	}

	bool ParseCssAngleToDegrees(const FString& Token, float& OutDegrees)
	{
		float Value = 0.0f;
		if (Token.EndsWith(TEXT("deg")))
		{
			Value = FCString::Atof(*Token);
		}
		else if (Token.EndsWith(TEXT("rad")))
		{
			Value = FCString::Atof(*Token) * 180.0f / PI;
		}
		else if (Token.EndsWith(TEXT("turn")))
		{
			Value = FCString::Atof(*Token) * 360.0f;
		}
		else
		{
			return false;
		}
		OutDegrees = Value;
		return true;
	}

	// CSS linear-gradient direction -> normalized start/end points in design-space
	// UVs (Y grows downward, stop 0 sits at the start point).
	void ResolveCssLinearEndpoints(const FString& DirectionToken, FVector2D& OutStart, FVector2D& OutEnd)
	{
		const FString Lower = DirectionToken.ToLower();
		const bool bHasTop = Lower.Contains(TEXT("top"));
		const bool bHasBottom = Lower.Contains(TEXT("bottom"));
		const bool bHasLeft = Lower.Contains(TEXT("left"));
		const bool bHasRight = Lower.Contains(TEXT("right"));

		if (bHasTop || bHasBottom || bHasLeft || bHasRight)
		{
			// "to <side>" lists the gradient destination; stop 0 sits opposite it.
			FVector2D Direction = FVector2D::ZeroVector;
			if (bHasTop)
				Direction.Y -= 1.0f;
			if (bHasBottom)
				Direction.Y += 1.0f;
			if (bHasLeft)
				Direction.X -= 1.0f;
			if (bHasRight)
				Direction.X += 1.0f;
			Direction = Direction.GetSafeNormal();
			const float Half = (FMath::Abs(Direction.X) + FMath::Abs(Direction.Y)) * 0.5f;
			OutStart = FVector2D(0.5f, 0.5f) - Direction * Half;
			OutEnd = FVector2D(0.5f, 0.5f) + Direction * Half;
			return;
		}

		float Degrees = 0.0f;
		if (ParseCssAngleToDegrees(Lower, Degrees))
		{
			// CSS angle: 0deg points up, growing clockwise; design space has Y down.
			const float Radians = FMath::DegreesToRadians(Degrees);
			const FVector2D Direction(FMath::Sin(Radians), -FMath::Cos(Radians));
			const float Half = (FMath::Abs(Direction.X) + FMath::Abs(Direction.Y)) * 0.5f;
			OutStart = FVector2D(0.5f, 0.5f) - Direction * Half;
			OutEnd = FVector2D(0.5f, 0.5f) + Direction * Half;
			return;
		}

		// CSS default direction is "to bottom".
		OutStart = FVector2D(0.5f, 0.0f);
		OutEnd = FVector2D(0.5f, 1.0f);
	}

	bool LooksLikeCssColorToken(const FString& Token)
	{
		return Token.StartsWith(TEXT("#"))
			|| Token.StartsWith(TEXT("$"))
			|| Token.StartsWith(TEXT("rgb("))
			|| Token.StartsWith(TEXT("rgba("))
			|| Token.StartsWith(TEXT("hsl("))
			|| Token.StartsWith(TEXT("hsla("));
	}

	// Parses one CSS gradient stop argument: "<color>", "<color> <pos>%", or
	// "<pos>% <color>". Returns the color token; position stays -1 when absent.
	FString ParseCssGradientStop(const FString& Token, float& OutPosition)
	{
		OutPosition = -1.0f;
		FString Color = Token.TrimStartAndEnd();
		if (Color.IsEmpty())
		{
			return FString();
		}

		int32 LastSpaceIndex = INDEX_NONE;
		for (int32 Index = Color.Len() - 1; Index >= 0; --Index)
		{
			if (FChar::IsWhitespace(Color[Index]))
			{
				LastSpaceIndex = Index;
				break;
			}
		}

		auto TryParsePercent = [](const FString& Text, float& OutFraction) -> bool
		{
			if (!Text.EndsWith(TEXT("%")))
			{
				return false;
			}
			const FString Digits = Text.Mid(0, Text.Len() - 1).TrimStartAndEnd();
			if (Digits.IsEmpty() || !Digits.IsNumeric())
			{
				return false;
			}
			OutFraction = FCString::Atof(*Digits) / 100.0f;
			return true;
		};

		if (LastSpaceIndex == INDEX_NONE)
		{
			// A lone percentage is a CSS interpolation hint, not a color stop.
			float IgnoredFraction = 0.0f;
			return TryParsePercent(Color, IgnoredFraction) ? FString() : Color;
		}

		const FString Head = Color.Mid(0, LastSpaceIndex).TrimStartAndEnd();
		const FString Tail = Color.Mid(LastSpaceIndex + 1).TrimStartAndEnd();
		float Fraction = 0.0f;
		if (TryParsePercent(Tail, Fraction))
		{
			OutPosition = Fraction;
			return Head;
		}
		if (TryParsePercent(Head, Fraction))
		{
			OutPosition = Fraction;
			return Tail;
		}
		return Color;
	}

	// Pencil stores gradients either as structured objects or as CSS function
	// strings ("linear-gradient(to bottom, #00000060 0%, #000000 100%)").
	// Parses the CSS flavor into the shared gradient fill; stops without an
	// explicit position distribute evenly, per CSS.
	bool TryParseCssGradientString(const FString& Value, const FDesignVariableResolver& Resolver, FDesignFill& OutFill)
	{
		const FString Trimmed = Value.TrimStartAndEnd();
		const bool bLinear = Trimmed.StartsWith(TEXT("linear-gradient("), ESearchCase::IgnoreCase);
		const bool bRadial = Trimmed.StartsWith(TEXT("radial-gradient("), ESearchCase::IgnoreCase);
		if ((!bLinear && !bRadial) || !Trimmed.EndsWith(TEXT(")")))
		{
			return false;
		}

		const int32 OpenIndex = Trimmed.Find(TEXT("("), ESearchCase::CaseSensitive);
		const FString Inner = Trimmed.Mid(OpenIndex + 1, Trimmed.Len() - OpenIndex - 2);
		TArray<FString> Parts;
		SplitCssArgumentList(Inner, Parts);
		if (Parts.Num() == 0)
		{
			return false;
		}

		FDesignFill Result;
		Result.bEnabled = true;
		Result.Type = EDesignFillType::Gradient;
		Result.Gradient.GradientType = bRadial ? EDesignGradientType::Radial : EDesignGradientType::Linear;

		int32 FirstStopIndex = 0;
		bool bHasLinearEndpoints = false;
		if (bLinear)
		{
			if (Parts[0].StartsWith(TEXT("to "), ESearchCase::IgnoreCase))
			{
				FVector2D Start;
				FVector2D End;
				ResolveCssLinearEndpoints(Parts[0], Start, End);
				Result.Gradient.StartPoint = Start;
				Result.Gradient.EndPoint = End;
				bHasLinearEndpoints = true;
				FirstStopIndex = 1;
			}
			else if (!LooksLikeCssColorToken(Parts[0]))
			{
				float Degrees = 0.0f;
				if (ParseCssAngleToDegrees(Parts[0].ToLower(), Degrees))
				{
					FVector2D Start;
					FVector2D End;
					ResolveCssLinearEndpoints(Parts[0], Start, End);
					Result.Gradient.StartPoint = Start;
					Result.Gradient.EndPoint = End;
					bHasLinearEndpoints = true;
					FirstStopIndex = 1;
				}
			}
			if (!bHasLinearEndpoints)
			{
				Result.Gradient.StartPoint = FVector2D(0.5f, 0.0f);
				Result.Gradient.EndPoint = FVector2D(0.5f, 1.0f);
			}
		}
		else
		{
			// Shape/extent keywords ("circle", "closest-side", ...) carry no data the
			// shared gradient IR needs; only "at <x>% <y>%" maps to the center.
			for (int32 Index = 0; Index < Parts.Num(); ++Index)
			{
				const FString& Part = Parts[Index];
				const int32 AtIndex = Part.Find(TEXT(" at "), ESearchCase::IgnoreCase);
				if (AtIndex == INDEX_NONE)
				{
					if (LooksLikeCssColorToken(Part))
					{
						break;
					}
					continue;
				}
				const FString PositionText = Part.Mid(AtIndex + 4).TrimStartAndEnd();
				TArray<FString> PositionParts;
				PositionText.ParseIntoArray(PositionParts, TEXT(" "), true);
				FVector2D Center = FVector2D(0.5f, 0.5f);
				if (PositionParts.Num() >= 2
					&& PositionParts[0].EndsWith(TEXT("%"))
					&& PositionParts[1].EndsWith(TEXT("%")))
				{
					Center.X = FCString::Atof(*PositionParts[0]) / 100.0f;
					Center.Y = FCString::Atof(*PositionParts[1]) / 100.0f;
				}
				Result.Gradient.Center = Center;
				FirstStopIndex = Index + 1;
				break;
			}
		}

		TArray<FDesignGradientStop> Stops;
		for (int32 Index = FirstStopIndex; Index < Parts.Num(); ++Index)
		{
			float Position = -1.0f;
			const FString ColorToken = ParseCssGradientStop(Parts[Index], Position);
			if (!LooksLikeCssColorToken(ColorToken) && !FDesignVariableResolver::IsVariableRef(ColorToken))
			{
				continue;
			}
			FDesignGradientStop Stop;
			if (FDesignVariableResolver::IsVariableRef(ColorToken))
			{
				Stop.SourceVar = ColorToken;
			}
			Stop.Color = Resolver.ResolveColor(ColorToken);
			Stop.Position = Position;
			Stops.Add(Stop);
		}

		if (Stops.Num() < 2)
		{
			return false;
		}

		// Distribute stops that carry no explicit position evenly (CSS behavior).
		for (int32 Index = 0; Index < Stops.Num(); ++Index)
		{
			if (Stops[Index].Position < 0.0f)
			{
				Stops[Index].Position = Stops.Num() > 1
					? static_cast<float>(Index) / static_cast<float>(Stops.Num() - 1)
					: 0.0f;
			}
		}
		Stops[0].Position = 0.0f;
		Stops.Last().Position = 1.0f;

		Result.Gradient.Colors = MoveTemp(Stops);
		OutFill = Result;
		return true;
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
		if (TryParseCssGradientString(Str, Resolver, Result))
			return Result;
		// Pencil drops a paint whose value it cannot resolve (e.g. an escaped
		// literal like "\$token" that is plain text, not a variable ref) instead
		// of painting it: the node renders with no fill and stays transparent.
		// Hex-prefixed literals (well-formed or not) and unresolved variable
		// refs keep the legacy pass-through so the renderer's fallback stays
		// authoritative for them.
		// Scope: this drop rule covers the string form only. The object form's
		// "color" field keeps the legacy resolve pass-through; the live .pen
		// corpus has no instance of an unresolvable object-form color (every
		// occurrence is enabled:false with a legal value), so Pencil's actual
		// behavior there is unverified — measure a real instance before
		// extending the drop rule to it.
		const FString Resolved = Resolver.ResolveColor(Str);
		const bool bIsVariableRef = FDesignVariableResolver::IsVariableRef(Str);
		if (!Resolved.StartsWith(TEXT("#"))
			&& !bIsVariableRef
			&& !DesignColorUtils::IsRenderableNamedColor(Resolved))
		{
			return Result;
		}
		Result.Type = EDesignFillType::Color;
		if (bIsVariableRef)
			Result.SourceVar = Str;
		Result.SolidColor = Resolved;
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
		// Pencil stores the linear gradient rotation in its internal convention:
		// the editor's own CSS-string parser maps a CSS angle A to rotation = -A
		// (so rotation 0 == CSS 0deg, gradient grows toward screen-up). The shared
		// IR plus GenerateGradientAngled instead read Rotation as a math angle in
		// the y-down design space ((cos, sin) from +X). Convert with
		// theta = -R - 90 so both conventions describe the same direction.
		// Radial/angular rotations are left untouched: no calibration data on
		// their Pencil semantics exists yet.
		float PencilGradientRotation = 0.0f;
		if (Result.Gradient.GradientType == EDesignGradientType::Linear
			&& TryGetResolvedNumberField(Obj, TEXT("rotation"), Resolver, PencilGradientRotation))
		{
			Result.Gradient.Rotation = -PencilGradientRotation - 90.0f;
		}
		else
		{
			TryGetResolvedNumberField(Obj, TEXT("rotation"), Resolver, Result.Gradient.Rotation);
		}

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
		if (TransformField.IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* Matrix = nullptr;
			if (!TransformField->TryGetArray(Matrix) || !ParserSharedSemantics::TryParseImageTransformMatrix(
				*Matrix,
				[](const TSharedPtr<FJsonValue>& Value, float& OutNumber)
				{
					return Value->Type == EJson::String
						? FUMGValueParser::TryParseFloat(Value->AsString(), OutNumber)
						: Value->Type == EJson::Number && Value->TryGetNumber(OutNumber);
				}, Result.Image.Transform))
			{
				UE_LOG(LogDesign2UMG, Warning, TEXT("PenParser: Invalid imageTransform; expected a 2x3 matrix of finite numbers. Ignoring transform."));
			}
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
