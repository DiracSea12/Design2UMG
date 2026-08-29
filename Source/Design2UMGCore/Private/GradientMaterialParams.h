// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

// Single source of the gradient material-params JSON shared by the design-gradient
// transfer path (VisualMapperFillTransfer) and the typed widget-node serializer
// (UMGWidgetNodeAccess). Callers adapt their own gradient structs onto these
// primitive inputs and pre-serialize stop colors, so the emitted bytes stay
// identical across both entry points.
namespace Design2UMGGradientMaterialParams
{
	struct FGradientStopParam
	{
		float Position = 0.0f;
		FString Color;
	};

	struct FGradientParams
	{
		bool bRadial = false;
		bool bHasLinearEndpoints = false;
		FVector2D StartPoint = FVector2D::ZeroVector;
		FVector2D EndPoint = FVector2D(1.0f, 0.0f);
		FVector2D Center = FVector2D(0.5f, 0.5f);
		FVector2D Size = FVector2D(1.0f, 1.0f);
		float Rotation = 0.0f;
		TArray<FGradientStopParam> Stops;
	};

	inline FString BuildGradientMaterialParamsJson(const FGradientParams& Params)
	{
		if (Params.Stops.Num() == 0)
		{
			return FString();
		}

		const FString& StartColor = Params.Stops[0].Color;
		const FString& EndColor = Params.Stops.Last().Color;
		const FString LinearParams = Params.bHasLinearEndpoints
			? FString::Printf(
				TEXT("\"StartX\":%.4f,\"StartY\":%.4f,\"EndX\":%.4f,\"EndY\":%.4f,"),
				Params.StartPoint.X,
				Params.StartPoint.Y,
				Params.EndPoint.X,
				Params.EndPoint.Y)
			: FString();
		const FString RadialParams = FString::Printf(
			TEXT("\"CenterX\":%.4f,\"CenterY\":%.4f,\"SizeWidth\":%.4f,\"SizeHeight\":%.4f,\"Rotation\":%.4f"),
			Params.Center.X,
			Params.Center.Y,
			Params.Size.X,
			Params.Size.Y,
			Params.Rotation);

		if (Params.Stops.Num() > 2)
		{
			FString Stops = Params.bRadial
				? FString::Printf(TEXT("{%s,\"Stops\":["), *RadialParams)
				: FString::Printf(TEXT("{%s\"Rotation\":%.4f,\"Stops\":["), *LinearParams, Params.Rotation);
			for (int32 Index = 0; Index < Params.Stops.Num(); ++Index)
			{
				if (Index > 0)
				{
					Stops += TEXT(",");
				}
				Stops += FString::Printf(
					TEXT("{\"Pos\":%.4f,\"Color\":\"%s\"}"),
					Params.Stops[Index].Position,
					*Params.Stops[Index].Color);
			}
			Stops += TEXT("]}");
			return Stops;
		}

		if (Params.bRadial)
		{
			return FString::Printf(
				TEXT("{%s,\"ColorCenter\":\"%s\",\"ColorEdge\":\"%s\"}"),
				*RadialParams,
				*StartColor,
				*EndColor);
		}

		return FString::Printf(
			TEXT("{%s\"Rotation\":%.4f,\"ColorStart\":\"%s\",\"ColorEnd\":\"%s\"}"),
			*LinearParams,
			Params.Rotation,
			*StartColor,
			*EndColor);
	}
}
