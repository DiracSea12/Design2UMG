// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "FigmaParserLayoutBounds.h"

#include "DesignJsonUtils.h"
#include "FigmaParserLayout.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace FigmaParserInternal
{
using DesignJsonUtils::GetArray;
using DesignJsonUtils::GetBool;
using DesignJsonUtils::GetNumber;
using DesignJsonUtils::GetObject;
using DesignJsonUtils::GetString;

namespace
{
	// Keep a half-pixel tolerance for source bounds comparisons because Figma export
	// commonly jitters by sub-pixel amounts even when the effective layout is unchanged.
	constexpr float FigmaBoundsComparisonTolerance = 0.5f;
	// Smaller deltas are export noise and do not add useful metadata.
	constexpr float FigmaBoundsMetadataDeltaTolerance = 0.001f;
	// A 15%+ shrink usually indicates image-fill render bounds rather than the intended layout box.
	constexpr float FigmaShrunkenImageBoundsRatio = 0.85f;
	constexpr float FigmaCenterStrokeExpansionScale = 0.5f;
	constexpr float FigmaMinimumVisibleBlurRadius = 0.0f;
	constexpr float FigmaPositiveBoundsExtentThreshold = 0.0f;

	bool HasVisibleForegroundBlurEffect(const TSharedPtr<FJsonObject>& NodeObj)
	{
		const TArray<TSharedPtr<FJsonValue>>* Effects = GetArray(NodeObj, TEXT("effects"));
		if (!Effects)
		{
			return false;
		}

		for (const TSharedPtr<FJsonValue>& EffectValue : *Effects)
		{
			const TSharedPtr<FJsonObject> EffectObj = EffectValue.IsValid() ? EffectValue->AsObject() : nullptr;
			if (!EffectObj.IsValid() || !GetBool(EffectObj, TEXT("visible"), true))
			{
				continue;
			}

			if (GetString(EffectObj, TEXT("type")) == TEXT("LAYER_BLUR")
				&& GetNumber(EffectObj, TEXT("radius")) > FigmaMinimumVisibleBlurRadius)
			{
				return true;
			}
		}

		return false;
	}

	void GetStrokeExpansion(const TSharedPtr<FJsonObject>& NodeObj, float& OutLeft, float& OutRight, float& OutTop, float& OutBottom)
	{
		OutLeft = 0.0f;
		OutRight = 0.0f;
		OutTop = 0.0f;
		OutBottom = 0.0f;

		if (!HasVisibleStroke(NodeObj))
		{
			return;
		}

		float Left = 0.0f;
		float Right = 0.0f;
		float Top = 0.0f;
		float Bottom = 0.0f;
		const TSharedPtr<FJsonObject> IndividualWeights = GetObject(NodeObj, TEXT("individualStrokeWeights"));
		if (IndividualWeights.IsValid())
		{
			Top = GetNumber(IndividualWeights, TEXT("top"));
			Right = GetNumber(IndividualWeights, TEXT("right"));
			Bottom = GetNumber(IndividualWeights, TEXT("bottom"));
			Left = GetNumber(IndividualWeights, TEXT("left"));
		}
		else
		{
			const float Uniform = GetNumber(NodeObj, TEXT("strokeWeight"));
			Top = Uniform;
			Right = Uniform;
			Bottom = Uniform;
			Left = Uniform;
		}

		const FString Align = GetString(NodeObj, TEXT("strokeAlign"), TEXT("INSIDE"));
		if (Align == TEXT("OUTSIDE"))
		{
			OutLeft = Left;
			OutRight = Right;
			OutTop = Top;
			OutBottom = Bottom;
		}
		else if (Align == TEXT("CENTER"))
		{
			OutLeft = Left * FigmaCenterStrokeExpansionScale;
			OutRight = Right * FigmaCenterStrokeExpansionScale;
			OutTop = Top * FigmaCenterStrokeExpansionScale;
			OutBottom = Bottom * FigmaCenterStrokeExpansionScale;
		}
	}

	bool HasImageCropFill(const TSharedPtr<FJsonObject>& NodeObj)
	{
		const TArray<TSharedPtr<FJsonValue>>* Fills = GetArray(NodeObj, TEXT("fills"));
		if (!Fills)
		{
			return false;
		}

		for (const TSharedPtr<FJsonValue>& FillValue : *Fills)
		{
			const TSharedPtr<FJsonObject> FillObj = FillValue.IsValid() ? FillValue->AsObject() : nullptr;
			if (!FillObj.IsValid() || !GetBool(FillObj, TEXT("visible"), true))
			{
				continue;
			}

			if (GetString(FillObj, TEXT("type")) != TEXT("IMAGE") || GetString(FillObj, TEXT("scaleMode")) != TEXT("CROP"))
			{
				continue;
			}

			return true;
		}

		return false;
	}

	bool IsIdentityImageTransformMatrix(const TArray<TSharedPtr<FJsonValue>>* Transform)
	{
		if (!Transform || Transform->Num() != 2)
		{
			return true;
		}

		const TArray<TSharedPtr<FJsonValue>> Row0 = (*Transform)[0]->AsArray();
		const TArray<TSharedPtr<FJsonValue>> Row1 = (*Transform)[1]->AsArray();
		if (Row0.Num() != 3 || Row1.Num() != 3)
		{
			return false;
		}

		return FMath::IsNearlyEqual(static_cast<float>(Row0[0]->AsNumber()), 1.0f, KINDA_SMALL_NUMBER)
			&& FMath::IsNearlyEqual(static_cast<float>(Row0[1]->AsNumber()), 0.0f, KINDA_SMALL_NUMBER)
			&& FMath::IsNearlyEqual(static_cast<float>(Row0[2]->AsNumber()), 0.0f, KINDA_SMALL_NUMBER)
			&& FMath::IsNearlyEqual(static_cast<float>(Row1[0]->AsNumber()), 0.0f, KINDA_SMALL_NUMBER)
			&& FMath::IsNearlyEqual(static_cast<float>(Row1[1]->AsNumber()), 1.0f, KINDA_SMALL_NUMBER)
			&& FMath::IsNearlyEqual(static_cast<float>(Row1[2]->AsNumber()), 0.0f, KINDA_SMALL_NUMBER);
	}

	bool HasFillImageUsingShrunkenRenderBounds(
		const TSharedPtr<FJsonObject>& NodeObj,
		const FNodeBounds& RenderBounds,
		const FNodeBounds& LayoutBounds)
	{
		if (!NodeObj.IsValid() || GetString(NodeObj, TEXT("type")) != TEXT("RECTANGLE"))
		{
			return false;
		}

		const bool bWidthShrunk =
			RenderBounds.W + FigmaBoundsComparisonTolerance < LayoutBounds.W * FigmaShrunkenImageBoundsRatio;
		const bool bHeightShrunk =
			RenderBounds.H + FigmaBoundsComparisonTolerance < LayoutBounds.H * FigmaShrunkenImageBoundsRatio;
		if (!bWidthShrunk && !bHeightShrunk)
		{
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* Fills = GetArray(NodeObj, TEXT("fills"));
		if (!Fills)
		{
			return false;
		}

		for (const TSharedPtr<FJsonValue>& FillValue : *Fills)
		{
			const TSharedPtr<FJsonObject> FillObj = FillValue.IsValid() ? FillValue->AsObject() : nullptr;
			if (!FillObj.IsValid() || !GetBool(FillObj, TEXT("visible"), true))
			{
				continue;
			}

			if (GetString(FillObj, TEXT("type")) != TEXT("IMAGE") || GetString(FillObj, TEXT("scaleMode")) != TEXT("FILL"))
			{
				continue;
			}

			if (IsIdentityImageTransformMatrix(GetArray(FillObj, TEXT("imageTransform"))))
			{
				return true;
			}
		}

		return false;
	}

	bool IsContainerLikeNodeTypeForLayoutBounds(const TSharedPtr<FJsonObject>& NodeObj)
	{
		if (!NodeObj.IsValid())
		{
			return false;
		}

		const FString NodeType = GetString(NodeObj, TEXT("type"));
		return NodeType == TEXT("FRAME")
			|| NodeType == TEXT("GROUP")
			|| NodeType == TEXT("TRANSFORM_GROUP")
			|| NodeType == TEXT("INSTANCE")
			|| NodeType == TEXT("COMPONENT")
			|| NodeType == TEXT("COMPONENT_SET")
			|| NodeType == TEXT("SECTION");
	}

	bool HasVisibleFill(const TSharedPtr<FJsonObject>& NodeObj)
	{
		const TArray<TSharedPtr<FJsonValue>>* Fills = GetArray(NodeObj, TEXT("fills"));
		if (!Fills)
		{
			return false;
		}

		for (const TSharedPtr<FJsonValue>& FillVal : *Fills)
		{
			const TSharedPtr<FJsonObject> FillObj = FillVal->AsObject();
			if (FillObj.IsValid() && GetBool(FillObj, TEXT("visible"), true))
			{
				return true;
			}
		}

		return false;
	}

	bool FitsWithinBounds(const FNodeBounds& Inner, const FNodeBounds& Outer)
	{
		return Inner.X >= Outer.X - FigmaBoundsComparisonTolerance
			&& Inner.Y >= Outer.Y - FigmaBoundsComparisonTolerance
			&& Inner.X + Inner.W <= Outer.X + Outer.W + FigmaBoundsComparisonTolerance
			&& Inner.Y + Inner.H <= Outer.Y + Outer.H + FigmaBoundsComparisonTolerance;
	}

	bool ShouldKeepShrunkenRenderBounds(
		const TSharedPtr<FJsonObject>& ParentObj,
		const FNodeBounds& RenderBounds,
		const FNodeBounds& LayoutBounds)
	{
		if (!ParentObj || HasAutoLayout(ParentObj))
		{
			return false;
		}

		FNodeBounds ParentLayoutBounds;
		if (!TryReadBounds(GetObject(ParentObj, TEXT("absoluteBoundingBox")), ParentLayoutBounds))
		{
			return false;
		}

		const bool bLayoutEscapesParent = !FitsWithinBounds(LayoutBounds, ParentLayoutBounds);
		const bool bRenderFitsParent = FitsWithinBounds(RenderBounds, ParentLayoutBounds);
		return bLayoutEscapesParent && bRenderFitsParent;
	}

	bool ShouldPreferLayoutBounds(
		const TSharedPtr<FJsonObject>& NodeObj,
		const TSharedPtr<FJsonObject>& ParentObj,
		const FNodeBounds& RenderBounds,
		const FNodeBounds& LayoutBounds)
	{
		const bool bRenderBoundsExpandLayoutBounds =
			RenderBounds.X + FigmaBoundsComparisonTolerance < LayoutBounds.X
			|| RenderBounds.Y + FigmaBoundsComparisonTolerance < LayoutBounds.Y
			|| RenderBounds.X + RenderBounds.W > LayoutBounds.X + LayoutBounds.W + FigmaBoundsComparisonTolerance
			|| RenderBounds.Y + RenderBounds.H > LayoutBounds.Y + LayoutBounds.H + FigmaBoundsComparisonTolerance;
		const bool bRenderBoundsShrinkLayoutBounds =
			RenderBounds.W + FigmaBoundsComparisonTolerance < LayoutBounds.W
			|| RenderBounds.H + FigmaBoundsComparisonTolerance < LayoutBounds.H;

		if (HasImageCropFill(NodeObj))
		{
			return true;
		}

		if (HasFillImageUsingShrunkenRenderBounds(NodeObj, RenderBounds, LayoutBounds))
		{
			return true;
		}

		if (bRenderBoundsExpandLayoutBounds && HasVisibleForegroundBlurEffect(NodeObj))
		{
			// Foreground blur is not faithfully reproduced by the shared UMG path yet.
			// Keep geometry in layout space so the ignored blur does not inflate the node.
			return true;
		}

		const TArray<TSharedPtr<FJsonValue>>* Children = GetArray(NodeObj, TEXT("children"));
		const bool bHasChildren = Children && Children->Num() > 0;
		if (bHasChildren && bRenderBoundsExpandLayoutBounds)
		{
			// Child coordinates are defined in layout space; effect/stroke expansion must not shift the container origin.
			return true;
		}

		if (IsContainerLikeNodeTypeForLayoutBounds(NodeObj) && bRenderBoundsShrinkLayoutBounds)
		{
			return !ShouldKeepShrunkenRenderBounds(ParentObj, RenderBounds, LayoutBounds);
		}

		if (HasAutoLayout(NodeObj))
		{
			const bool bIsLayoutOnlyContainer =
				!HasVisibleFill(NodeObj) && !HasVisibleStroke(NodeObj) && !HasVisibleEffect(NodeObj);
			if (bIsLayoutOnlyContainer && bRenderBoundsShrinkLayoutBounds)
			{
				return !ShouldKeepShrunkenRenderBounds(ParentObj, RenderBounds, LayoutBounds);
			}
		}

		return false;
	}
}

bool HasVisibleStroke(const TSharedPtr<FJsonObject>& NodeObj)
{
	const TArray<TSharedPtr<FJsonValue>>* Strokes = GetArray(NodeObj, TEXT("strokes"));
	if (!Strokes || Strokes->Num() == 0)
	{
		return false;
	}

	for (const TSharedPtr<FJsonValue>& StrokeValue : *Strokes)
	{
		const TSharedPtr<FJsonObject> StrokeObj = StrokeValue->AsObject();
		if (StrokeObj.IsValid() && GetBool(StrokeObj, TEXT("visible"), true))
		{
			return true;
		}
	}

	return false;
}

bool HasVisibleEffect(const TSharedPtr<FJsonObject>& NodeObj)
{
	const TArray<TSharedPtr<FJsonValue>>* Effects = GetArray(NodeObj, TEXT("effects"));
	if (!Effects || Effects->Num() == 0)
	{
		return false;
	}

	for (const TSharedPtr<FJsonValue>& EffectValue : *Effects)
	{
		const TSharedPtr<FJsonObject> EffectObj = EffectValue->AsObject();
		if (EffectObj.IsValid() && GetBool(EffectObj, TEXT("visible"), true))
		{
			return true;
		}
	}

	return false;
}

bool TryReadBounds(const TSharedPtr<FJsonObject>& BoundsObj, FNodeBounds& OutBounds)
{
	if (!BoundsObj.IsValid())
	{
		return false;
	}

	OutBounds.X = GetNumber(BoundsObj, TEXT("x"));
	OutBounds.Y = GetNumber(BoundsObj, TEXT("y"));
	OutBounds.W = GetNumber(BoundsObj, TEXT("width"));
	OutBounds.H = GetNumber(BoundsObj, TEXT("height"));
	return OutBounds.W > FigmaPositiveBoundsExtentThreshold
		&& OutBounds.H > FigmaPositiveBoundsExtentThreshold;
}

void CaptureBoundsDeltaMetadata(const TSharedPtr<FJsonObject>& Obj, TMap<FString, FString>& Extensions)
{
	FNodeBounds LayoutBounds;
	FNodeBounds RenderBounds;
	if (!TryReadBounds(GetObject(Obj, TEXT("absoluteBoundingBox")), LayoutBounds)
		|| !TryReadBounds(GetObject(Obj, TEXT("absoluteRenderBounds")), RenderBounds))
	{
		return;
	}

	const auto AddDeltaIfNonZero = [&Extensions](const TCHAR* Key, const float Delta)
	{
		if (!FMath::IsNearlyZero(Delta, FigmaBoundsMetadataDeltaTolerance))
		{
			Extensions.Add(Key, FString::SanitizeFloat(Delta));
		}
	};

	AddDeltaIfNonZero(TEXT("source.renderBoundsDeltaX"), RenderBounds.X - LayoutBounds.X);
	AddDeltaIfNonZero(TEXT("source.renderBoundsDeltaY"), RenderBounds.Y - LayoutBounds.Y);
	AddDeltaIfNonZero(TEXT("source.renderBoundsDeltaW"), RenderBounds.W - LayoutBounds.W);
	AddDeltaIfNonZero(TEXT("source.renderBoundsDeltaH"), RenderBounds.H - LayoutBounds.H);
}

bool TryGetVisualBounds(
	const TSharedPtr<FJsonObject>& NodeObj,
	const TSharedPtr<FJsonObject>& ParentObj,
	FNodeBounds& OutBounds)
{
	if (!NodeObj.IsValid())
	{
		return false;
	}

	FNodeBounds RenderBounds;
	const bool bHasRenderBounds = TryReadBounds(GetObject(NodeObj, TEXT("absoluteRenderBounds")), RenderBounds);
	FNodeBounds LayoutBounds;
	const bool bHasLayoutBounds = TryReadBounds(GetObject(NodeObj, TEXT("absoluteBoundingBox")), LayoutBounds);

	if (bHasRenderBounds)
	{
		if (!bHasLayoutBounds || !ShouldPreferLayoutBounds(NodeObj, ParentObj, RenderBounds, LayoutBounds))
		{
			OutBounds = RenderBounds;
			return true;
		}
	}

	if (!bHasLayoutBounds)
	{
		return false;
	}

	OutBounds = LayoutBounds;

	// If render bounds are unavailable, expand layout bounds by stroke alignment.
	float ExpandLeft = 0.0f;
	float ExpandRight = 0.0f;
	float ExpandTop = 0.0f;
	float ExpandBottom = 0.0f;
	GetStrokeExpansion(NodeObj, ExpandLeft, ExpandRight, ExpandTop, ExpandBottom);

	OutBounds.X -= ExpandLeft;
	OutBounds.Y -= ExpandTop;
	OutBounds.W += ExpandLeft + ExpandRight;
	OutBounds.H += ExpandTop + ExpandBottom;
	return OutBounds.W > FigmaPositiveBoundsExtentThreshold
		&& OutBounds.H > FigmaPositiveBoundsExtentThreshold;
}
}
