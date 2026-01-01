// Copyright Epic Games, Inc. All Rights Reserved.

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
	if (!OldAnchor.IsSet())
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

FUMGSlotData FLayoutCalculator::CalculateSlot(const FBounds& Bounds, const FLayoutContext& Context)
{
	const int32 CanvasW = Context.DesignSize.X;
	const int32 CanvasH = Context.DesignSize.Y;
	const FLayoutPreset& Preset = Context.Preset;

	FUMGSlotData Slot;

	// === 水平方向处理 ===
	bool bSnapLeftX, bSnapRightX;
	bool bEdgeX = ShouldSnapToEdge(Bounds.X, Bounds.W, Preset.EdgeSnapThreshold, bSnapLeftX, bSnapRightX);
	bool bStretchX = ShouldUseStretchAnchors(Bounds.W, Preset.StretchThreshold);

	if (bStretchX && bSnapLeftX && bSnapRightX)
	{
		// 拉伸模式：两端都锚定边缘
		Slot.Anchors.Min.X = 0.0f;
		Slot.Anchors.Max.X = 1.0f;
		Slot.Alignment.X = 0.0f;
		Slot.Offsets.Left = Bounds.X * CanvasW;
		Slot.Offsets.Right = (1.0f - Bounds.GetRight()) * CanvasW;
	}
	else if (bSnapLeftX)
	{
		// 锚定左边缘
		Slot.Anchors.Min.X = Slot.Anchors.Max.X = 0.0f;
		Slot.Alignment.X = 0.0f;
		Slot.Offsets.Left = Bounds.X * CanvasW;
		Slot.Offsets.Right = Bounds.W * CanvasW;
	}
	else if (bSnapRightX)
	{
		// 锚定右边缘
		Slot.Anchors.Min.X = Slot.Anchors.Max.X = 1.0f;
		Slot.Alignment.X = 1.0f;
		Slot.Offsets.Left = -(1.0f - Bounds.GetRight()) * CanvasW;
		Slot.Offsets.Right = Bounds.W * CanvasW;
	}
	else
	{
		// 标准9宫格模式
		float CenterX = Bounds.GetCenterX();
		float AnchorX = SelectAnchorWithHysteresis(CenterX, Context.OldAnchorX, Preset);

		Slot.Anchors.Min.X = Slot.Anchors.Max.X = AnchorX;
		Slot.Alignment.X = AnchorX;

		float AnchorPixelX = AnchorX * CanvasW;
		float AlignPointPixelX = (Bounds.X + AnchorX * Bounds.W) * CanvasW;
		Slot.Offsets.Left = AlignPointPixelX - AnchorPixelX;
		Slot.Offsets.Right = Bounds.W * CanvasW;
	}

	// === 垂直方向处理 ===
	bool bSnapTopY, bSnapBottomY;
	bool bEdgeY = ShouldSnapToEdge(Bounds.Y, Bounds.H, Preset.EdgeSnapThreshold, bSnapTopY, bSnapBottomY);
	bool bStretchY = ShouldUseStretchAnchors(Bounds.H, Preset.StretchThreshold);

	if (bStretchY && bSnapTopY && bSnapBottomY)
	{
		// 拉伸模式
		Slot.Anchors.Min.Y = 0.0f;
		Slot.Anchors.Max.Y = 1.0f;
		Slot.Alignment.Y = 0.0f;
		Slot.Offsets.Top = Bounds.Y * CanvasH;
		Slot.Offsets.Bottom = (1.0f - Bounds.GetBottom()) * CanvasH;
	}
	else if (bSnapTopY)
	{
		// 锚定上边缘
		Slot.Anchors.Min.Y = Slot.Anchors.Max.Y = 0.0f;
		Slot.Alignment.Y = 0.0f;
		Slot.Offsets.Top = Bounds.Y * CanvasH;
		Slot.Offsets.Bottom = Bounds.H * CanvasH;
	}
	else if (bSnapBottomY)
	{
		// 锚定下边缘
		Slot.Anchors.Min.Y = Slot.Anchors.Max.Y = 1.0f;
		Slot.Alignment.Y = 1.0f;
		Slot.Offsets.Top = -(1.0f - Bounds.GetBottom()) * CanvasH;
		Slot.Offsets.Bottom = Bounds.H * CanvasH;
	}
	else
	{
		// 标准9宫格模式
		float CenterY = Bounds.GetCenterY();
		float AnchorY = SelectAnchorWithHysteresis(CenterY, Context.OldAnchorY, Preset);

		Slot.Anchors.Min.Y = Slot.Anchors.Max.Y = AnchorY;
		Slot.Alignment.Y = AnchorY;

		float AnchorPixelY = AnchorY * CanvasH;
		float AlignPointPixelY = (Bounds.Y + AnchorY * Bounds.H) * CanvasH;
		Slot.Offsets.Top = AlignPointPixelY - AnchorPixelY;
		Slot.Offsets.Bottom = Bounds.H * CanvasH;
	}

	return Slot;
}
