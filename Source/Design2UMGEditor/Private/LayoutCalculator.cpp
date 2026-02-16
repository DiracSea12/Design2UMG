// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#include "LayoutCalculator.h"

float FLayoutCalculator::SelectAnchorWithHysteresis(
	float Center,
	TOptional<float> OldAnchor,
	const FLayoutPreset& Preset)
{
	const float Threshold_L = Preset.AnchorThresholdL;
	const float Threshold_R = Preset.AnchorThresholdR;
	const float Buffer = Preset.HysteresisBuffer;

	// 如果没有历史锚点，使用标准9宫格选择
	if (!Preset.bEnableHysteresis || !OldAnchor.IsSet())
	{
		if (Center < Threshold_L) return 0.0f;
		else if (Center < Threshold_R) return 0.5f;
		else return 1.0f;
	}

	float Old = OldAnchor.GetValue();

	// Hysteresis逻辑：只有显著超过阈值+缓冲区才切换
	if (FMath::IsNearlyEqual(Old, 0.0f))
	{
		return (Center > Threshold_L + Buffer) ? 0.5f : 0.0f;
	}
	else if (FMath::IsNearlyEqual(Old, 0.5f))
	{
		if (Center < Threshold_L - Buffer) return 0.0f;
		else if (Center > Threshold_R + Buffer) return 1.0f;
		else return 0.5f;
	}
	else // Old == 1.0f
	{
		return (Center < Threshold_R - Buffer) ? 0.5f : 1.0f;
	}
}

bool FLayoutCalculator::ShouldSnapToEdge(
	float Position,
	float Size,
	float EdgeThreshold,
	bool& bSnapToStart,
	bool& bSnapToEnd)
{
	bSnapToStart = (Position < EdgeThreshold);
	bSnapToEnd = ((1.0f - Position - Size) < EdgeThreshold);
	return bSnapToStart || bSnapToEnd;
}

bool FLayoutCalculator::ShouldUseStretchAnchors(float Size, float StretchThreshold)
{
	return Size > StretchThreshold;
}

FLayoutCalculator::FAxisResult FLayoutCalculator::CalculateAxis(
	float Pos, float Size, int32 CanvasSize, TOptional<float> OldAnchor, const FLayoutPreset& Preset)
{
	FAxisResult R;
	const float End = Pos + Size;

	bool bSnapStart = false, bSnapEnd = false;
	bool bNearStart = false, bNearEnd = false;
	if (Preset.bEnableEdgeSnap || Preset.bEnableStretchAnchors)
	{
		ShouldSnapToEdge(Pos, Size, Preset.EdgeSnapThreshold, bNearStart, bNearEnd);
		if (Preset.bEnableEdgeSnap)
		{
			bSnapStart = bNearStart;
			bSnapEnd = bNearEnd;
		}
	}

	bool bStretch = Preset.bEnableStretchAnchors
		&& ShouldUseStretchAnchors(Size, Preset.StretchThreshold)
		&& bNearStart && bNearEnd;

	if (bStretch)
	{
		R.AnchorMin = 0.0f;
		R.AnchorMax = 1.0f;
		R.Alignment = 0.0f;
		R.OffsetStart = Pos * CanvasSize;
		R.OffsetEnd = (1.0f - End) * CanvasSize;
	}
	else if (bSnapStart)
	{
		R.AnchorMin = R.AnchorMax = 0.0f;
		R.Alignment = 0.0f;
		R.OffsetStart = Pos * CanvasSize;
		R.OffsetEnd = Size * CanvasSize;
	}
	else if (bSnapEnd)
	{
		R.AnchorMin = R.AnchorMax = 1.0f;
		R.Alignment = 1.0f;
		R.OffsetStart = -(1.0f - End) * CanvasSize;
		R.OffsetEnd = Size * CanvasSize;
	}
	else
	{
		const float Center = Pos + Size * 0.5f;
		const float Anchor = Preset.bEnableNineGrid
			? SelectAnchorWithHysteresis(Center, OldAnchor, Preset)
			: 0.0f;

		R.AnchorMin = R.AnchorMax = Anchor;
		R.Alignment = Anchor;

		const float AnchorPixel = Anchor * CanvasSize;
		const float AlignPointPixel = (Pos + Anchor * Size) * CanvasSize;
		R.OffsetStart = AlignPointPixel - AnchorPixel;
		R.OffsetEnd = Size * CanvasSize;
	}

	return R;
}

FUMGSlotData FLayoutCalculator::CalculateSlot(const FBounds& Bounds, const FLayoutContext& Context)
{
	// v2.2: DesignSize有效性检查
	const int32 CanvasW = FMath::Max(1, Context.DesignSize.X);
	const int32 CanvasH = FMath::Max(1, Context.DesignSize.Y);
	const FLayoutPreset& Preset = Context.Preset;

	// v2.2: Bounds范围验证（防止越界值）
	FBounds SafeBounds;
	SafeBounds.X = FMath::Clamp(Bounds.X, 0.0f, 1.0f);
	SafeBounds.Y = FMath::Clamp(Bounds.Y, 0.0f, 1.0f);
	SafeBounds.W = FMath::Clamp(Bounds.W, 0.0f, 1.0f - SafeBounds.X);
	SafeBounds.H = FMath::Clamp(Bounds.H, 0.0f, 1.0f - SafeBounds.Y);

	FUMGSlotData Slot;

	// 水平轴
	FAxisResult H = CalculateAxis(SafeBounds.X, SafeBounds.W, CanvasW, Context.OldAnchorX, Preset);
	Slot.Anchors.Min.X = H.AnchorMin;
	Slot.Anchors.Max.X = H.AnchorMax;
	Slot.Alignment.X = H.Alignment;
	Slot.Offsets.Left = H.OffsetStart;
	Slot.Offsets.Right = H.OffsetEnd;

	// 垂直轴
	FAxisResult V = CalculateAxis(SafeBounds.Y, SafeBounds.H, CanvasH, Context.OldAnchorY, Preset);
	Slot.Anchors.Min.Y = V.AnchorMin;
	Slot.Anchors.Max.Y = V.AnchorMax;
	Slot.Alignment.Y = V.Alignment;
	Slot.Offsets.Top = V.OffsetStart;
	Slot.Offsets.Bottom = V.OffsetEnd;

	return Slot;
}
