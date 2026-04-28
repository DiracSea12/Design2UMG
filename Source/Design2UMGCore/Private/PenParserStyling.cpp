// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "PenParserInternal.h"
#include "DesignSchema.h"
#include "DesignVariableResolver.h"
#include "DesignJsonUtils.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

// Forward declarations of functions defined in other PenParser*.cpp files.
FDesignFill ParseFill(const TSharedPtr<FJsonValue>& Value, const FDesignVariableResolver& Resolver);
FDesignStroke ParseStroke(const TSharedPtr<FJsonObject>& JsonObj, const FDesignVariableResolver& Resolver);

static bool TryParsePointXY(const TSharedPtr<FJsonValue>& Value, const FDesignVariableResolver& Resolver, float& OutX, float& OutY)
{
	if (!Value.IsValid())
		return false;

	if (Value->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> Obj = Value->AsObject();
		if (!Obj.IsValid())
			return false;
		const bool bHasX = TryGetResolvedNumberField(Obj, TEXT("x"), Resolver, OutX);
		const bool bHasY = TryGetResolvedNumberField(Obj, TEXT("y"), Resolver, OutY);
		return bHasX && bHasY;
	}

	if (Value->Type == EJson::Array)
	{
		const TArray<TSharedPtr<FJsonValue>>& Arr = Value->AsArray();
		if (Arr.Num() < 2)
			return false;
		return TryGetNumberValueFlexible(Arr[0], OutX) && TryGetNumberValueFlexible(Arr[1], OutY);
	}

	return false;
}

static bool TryParseMeshHandle(
	const TSharedPtr<FJsonObject>& PointObj,
	const FDesignVariableResolver& Resolver,
	const FString& PrimaryField,
	const FString& FallbackField,
	FDesignMeshGradientHandle& OutHandle)
{
	if (!PointObj.IsValid())
		return false;

	const TSharedPtr<FJsonValue> Primary = PointObj->TryGetField(PrimaryField);
	const TSharedPtr<FJsonValue> Fallback = PointObj->TryGetField(FallbackField);
	const TSharedPtr<FJsonValue> HandleField = Primary.IsValid() ? Primary : Fallback;
	if (!HandleField.IsValid())
		return false;

	float X = 0.0f;
	float Y = 0.0f;
	if (!TryParsePointXY(HandleField, Resolver, X, Y))
		return false;

	OutHandle.X = X;
	OutHandle.Y = Y;
	OutHandle.bHasValue = true;
	return true;
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
		const auto& Arr = Value->AsArray();
		for (int32 i = Arr.Num() - 1; i >= 0; --i)
		{
			FDesignFill Fill = ParseFill(Arr[i], Resolver);
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
			const bool bHasCenterX = TryGetResolvedNumberField(CenterObj, TEXT("x"), Resolver, Result.Gradient.CenterX);
			const bool bHasCenterY = TryGetResolvedNumberField(CenterObj, TEXT("y"), Resolver, Result.Gradient.CenterY);
			Result.Gradient.bHasCenter = bHasCenterX || bHasCenterY;
		}

		const TSharedPtr<FJsonValue>& SizeField = Obj->TryGetField(TEXT("size"));
		if (SizeField.IsValid() && SizeField->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject>& SizeObj = SizeField->AsObject();
			const bool bHasSizeW = TryGetResolvedNumberField(SizeObj, TEXT("width"), Resolver, Result.Gradient.SizeWidth);
			const bool bHasSizeH = TryGetResolvedNumberField(SizeObj, TEXT("height"), Resolver, Result.Gradient.SizeHeight);
			Result.Gradient.bHasSize = bHasSizeW || bHasSizeH;
		}

		const TSharedPtr<FJsonValue>& ColorsField = Obj->TryGetField(TEXT("colors"));
		if (ColorsField.IsValid() && ColorsField->Type == EJson::Array)
		{
			for (const auto& StopVal : ColorsField->AsArray())
			{
				const auto& StopObj = StopVal->AsObject();
				if (!StopObj.IsValid())
					continue;
				FDesignGradientStop Stop;
				FString StopColorStr;
				if (StopObj->TryGetStringField(TEXT("color"), StopColorStr))
				{
					if (FDesignVariableResolver::IsVariableRef(StopColorStr))
						Stop.SourceVar = StopColorStr;
					Stop.Color = Resolver.ResolveColor(StopColorStr);
				}
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
			const auto& TransformArr = TransformField->AsArray();
			if (TransformArr.Num() == 2)
			{
				auto Row0 = TransformArr[0]->AsArray();
				auto Row1 = TransformArr[1]->AsArray();
				if (Row0.Num() == 3 && Row1.Num() == 3)
				{
					TryGetNumberValueFlexible(Row0[0], Result.Image.Transform.A);
					TryGetNumberValueFlexible(Row0[1], Result.Image.Transform.B);
					TryGetNumberValueFlexible(Row0[2], Result.Image.Transform.TX);
					TryGetNumberValueFlexible(Row1[0], Result.Image.Transform.C);
					TryGetNumberValueFlexible(Row1[1], Result.Image.Transform.D);
					TryGetNumberValueFlexible(Row1[2], Result.Image.Transform.TY);
				}
			}
		}
		return Result;
	}

	if (FillType == TEXT("mesh_gradient"))
	{
		Result.Type = EDesignFillType::Gradient;
		Result.bFromMeshGradient = true;
		Result.MeshGradientRaw = DesignJsonUtils::ToCompactString(Value);
		Result.MeshGradient.bHasColumns = TryGetResolvedNumberField(Obj, TEXT("columns"), Resolver, Result.MeshGradient.Columns);
		Result.MeshGradient.bHasRows = TryGetResolvedNumberField(Obj, TEXT("rows"), Resolver, Result.MeshGradient.Rows);

		const TSharedPtr<FJsonValue>& ColorsField = Obj->TryGetField(TEXT("colors"));
		if (ColorsField.IsValid() && ColorsField->Type == EJson::Array)
		{
			for (const auto& StopVal : ColorsField->AsArray())
			{
				const auto& StopObj = StopVal->AsObject();
				if (!StopObj.IsValid())
					continue;

				FDesignGradientStop Stop;
				FString StopColorStr;
				if (StopObj->TryGetStringField(TEXT("color"), StopColorStr))
				{
					if (FDesignVariableResolver::IsVariableRef(StopColorStr))
						Stop.SourceVar = StopColorStr;
					Stop.Color = Resolver.ResolveColor(StopColorStr);
				}

				if (!TryGetResolvedNumberField(StopObj, TEXT("position"), Resolver, Stop.Position))
					Stop.Position = Result.Gradient.Colors.Num() > 0 ? 1.0f : 0.0f;
				Result.Gradient.Colors.Add(Stop);
			}
		}

		const TSharedPtr<FJsonValue>& PointsField = Obj->TryGetField(TEXT("points"));
		if (PointsField.IsValid() && PointsField->Type == EJson::Array)
		{
			for (const TSharedPtr<FJsonValue>& PointVal : PointsField->AsArray())
			{
				if (!PointVal.IsValid() || PointVal->Type != EJson::Object)
					continue;

				const TSharedPtr<FJsonObject> PointObj = PointVal->AsObject();
				if (!PointObj.IsValid())
					continue;

				FDesignMeshGradientPoint MeshPoint;
				MeshPoint.RawJson = DesignJsonUtils::ToCompactString(PointVal);

				const bool bHasXY = TryGetResolvedNumberField(PointObj, TEXT("x"), Resolver, MeshPoint.X)
					&& TryGetResolvedNumberField(PointObj, TEXT("y"), Resolver, MeshPoint.Y);
				if (bHasXY)
				{
					MeshPoint.bHasPosition = true;
				}
				else
				{
					float PosX = 0.0f;
					float PosY = 0.0f;
					const TSharedPtr<FJsonValue>& PositionField = PointObj->TryGetField(TEXT("position"));
					if (TryParsePointXY(PositionField, Resolver, PosX, PosY))
					{
						MeshPoint.X = PosX;
						MeshPoint.Y = PosY;
						MeshPoint.bHasPosition = true;
					}
				}

				FString MeshColor;
				if (PointObj->TryGetStringField(TEXT("color"), MeshColor))
				{
					if (FDesignVariableResolver::IsVariableRef(MeshColor))
						MeshPoint.SourceVar = MeshColor;
					MeshPoint.Color = Resolver.ResolveColor(MeshColor);
				}

				TryParseMeshHandle(PointObj, Resolver, TEXT("in"), TEXT("handleIn"), MeshPoint.InHandle);
				TryParseMeshHandle(PointObj, Resolver, TEXT("out"), TEXT("handleOut"), MeshPoint.OutHandle);
				Result.MeshGradient.Points.Add(MoveTemp(MeshPoint));
			}
		}
	}

	return Result;
}

FDesignStroke ParseStroke(const TSharedPtr<FJsonObject>& JsonObj, const FDesignVariableResolver& Resolver)
{
	FDesignStroke Result;

	if (!JsonObj.IsValid())
		return Result;

	FString Align;
	if (JsonObj->TryGetStringField(TEXT("align"), Align))
	{
		if (Align == TEXT("center"))
			Result.Align = EDesignStrokeAlign::Center;
		else if (Align == TEXT("outside"))
			Result.Align = EDesignStrokeAlign::Outside;
	}
	if (JsonObj->HasField(TEXT("thickness")))
	{
		const auto& ThickVal = JsonObj->TryGetField(TEXT("thickness"));
		if (TryGetNumberValueFlexible(ThickVal, Result.Thickness.Uniform))
		{
			Result.Thickness.bUniform = true;
		}
		else if (ThickVal->Type == EJson::String && FDesignVariableResolver::IsVariableRef(ThickVal->AsString()))
		{
			Result.Thickness.bUniform = true;
			Result.Thickness.Uniform = Resolver.ResolveNumber(ThickVal->AsString());
		}
		else if (ThickVal->Type == EJson::Object)
		{
			Result.Thickness.bUniform = false;
			const auto& ThickObj = ThickVal->AsObject();
			TryGetResolvedNumberField(ThickObj, TEXT("top"), Resolver, Result.Thickness.Top);
			TryGetResolvedNumberField(ThickObj, TEXT("right"), Resolver, Result.Thickness.Right);
			TryGetResolvedNumberField(ThickObj, TEXT("bottom"), Resolver, Result.Thickness.Bottom);
			TryGetResolvedNumberField(ThickObj, TEXT("left"), Resolver, Result.Thickness.Left);
		}
	}

	if (JsonObj->HasField(TEXT("fill")))
	{
		Result.bHasExplicitFill = true;
		Result.Fill = ParseFill(JsonObj->TryGetField(TEXT("fill")), Resolver);
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
		const auto& Arr = Value->AsArray();
		if (Arr.Num() == 2)
		{
			float Vertical = 0.0f;
			float Horizontal = 0.0f;
			if (ResolvePaddingNumber(Arr[0], Vertical))
				Result.Top = Result.Bottom = Vertical;
			if (ResolvePaddingNumber(Arr[1], Horizontal))
				Result.Left = Result.Right = Horizontal;
		}
		else if (Arr.Num() >= 4)
		{
			ResolvePaddingNumber(Arr[0], Result.Top);
			ResolvePaddingNumber(Arr[1], Result.Right);
			ResolvePaddingNumber(Arr[2], Result.Bottom);
			ResolvePaddingNumber(Arr[3], Result.Left);
		}
	}
	return Result;
}
