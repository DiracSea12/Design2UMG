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

void FLayoutCalculator::DetectEdgeSnap(const FBounds& Bounds, float EdgeThreshold, bool& bOutEdgeSnapX, bool& bOutEdgeSnapY)
{
	bool bNearLeftX, bNearRightX, bNearTopY, bNearBottomY;
	ShouldSnapToEdge(Bounds.X, Bounds.W, EdgeThreshold, bNearLeftX, bNearRightX);
	ShouldSnapToEdge(Bounds.Y, Bounds.H, EdgeThreshold, bNearTopY, bNearBottomY);
	bOutEdgeSnapX = bNearLeftX || bNearRightX;
	bOutEdgeSnapY = bNearTopY || bNearBottomY;
}

void FLayoutCalculator::DetectStretchAnchors(const FBounds& Bounds, float StretchThreshold, float EdgeThreshold, bool& bOutStretchX, bool& bOutStretchY)
{
	bool bNearLeftX, bNearRightX, bNearTopY, bNearBottomY;
	ShouldSnapToEdge(Bounds.X, Bounds.W, EdgeThreshold, bNearLeftX, bNearRightX);
	ShouldSnapToEdge(Bounds.Y, Bounds.H, EdgeThreshold, bNearTopY, bNearBottomY);
	bOutStretchX = ShouldUseStretchAnchors(Bounds.W, StretchThreshold) && bNearLeftX && bNearRightX;
	bOutStretchY = ShouldUseStretchAnchors(Bounds.H, StretchThreshold) && bNearTopY && bNearBottomY;
}

FUMGSlotData FLayoutCalculator::CalculateSlot(const FBounds& Bounds, const FLayoutContext& Context)
{
	// v2.2: DesignSize有效性检查
	const int32 CanvasW = FMath::Max(1, Context.DesignSize.X);
	const int32 CanvasH = FMath::Max(1, Context.DesignSize.Y);
	const FLayoutPreset& Preset = Context.Preset;

	// v2.2: Bounds范围验证（防止AI返回越界值）
	FBounds SafeBounds;
	SafeBounds.X = FMath::Clamp(Bounds.X, 0.0f, 1.0f);
	SafeBounds.Y = FMath::Clamp(Bounds.Y, 0.0f, 1.0f);
	SafeBounds.W = FMath::Clamp(Bounds.W, 0.0f, 1.0f - SafeBounds.X);
	SafeBounds.H = FMath::Clamp(Bounds.H, 0.0f, 1.0f - SafeBounds.Y);

	FUMGSlotData Slot;

	// === 水平方向处理 ===
	bool bSnapLeftX, bSnapRightX;
	bSnapLeftX = bSnapRightX = false;
	bool bNearLeftX = false;
	bool bNearRightX = false;
	if (Preset.bEnableEdgeSnap || Preset.bEnableStretchAnchors)
	{
		ShouldSnapToEdge(SafeBounds.X, SafeBounds.W, Preset.EdgeSnapThreshold, bNearLeftX, bNearRightX);
		if (Preset.bEnableEdgeSnap)
		{
			bSnapLeftX = bNearLeftX;
			bSnapRightX = bNearRightX;
		}
	}
	bool bStretchX = Preset.bEnableStretchAnchors
		&& ShouldUseStretchAnchors(SafeBounds.W, Preset.StretchThreshold)
		&& bNearLeftX
		&& bNearRightX;

	if (bStretchX)
	{
		// 拉伸模式：两端都锚定边缘
		Slot.Anchors.Min.X = 0.0f;
		Slot.Anchors.Max.X = 1.0f;
		Slot.Alignment.X = 0.0f;
		Slot.Offsets.Left = SafeBounds.X * CanvasW;
		Slot.Offsets.Right = (1.0f - SafeBounds.GetRight()) * CanvasW;
	}
	else if (bSnapLeftX)
	{
		// 锚定左边缘
		Slot.Anchors.Min.X = Slot.Anchors.Max.X = 0.0f;
		Slot.Alignment.X = 0.0f;
		Slot.Offsets.Left = SafeBounds.X * CanvasW;
		Slot.Offsets.Right = SafeBounds.W * CanvasW;
	}
	else if (bSnapRightX)
	{
		// 锚定右边缘
		Slot.Anchors.Min.X = Slot.Anchors.Max.X = 1.0f;
		Slot.Alignment.X = 1.0f;
		Slot.Offsets.Left = -(1.0f - SafeBounds.GetRight()) * CanvasW;
		Slot.Offsets.Right = SafeBounds.W * CanvasW;
	}
	else
	{
		const float CenterX = SafeBounds.GetCenterX();
		const float AnchorX = Preset.bEnableNineGrid
			? SelectAnchorWithHysteresis(CenterX, Context.OldAnchorX, Preset)
			: 0.0f;

		Slot.Anchors.Min.X = Slot.Anchors.Max.X = AnchorX;
		Slot.Alignment.X = AnchorX;

		const float AnchorPixelX = AnchorX * CanvasW;
		const float AlignPointPixelX = (SafeBounds.X + AnchorX * SafeBounds.W) * CanvasW;
		Slot.Offsets.Left = AlignPointPixelX - AnchorPixelX;
		Slot.Offsets.Right = SafeBounds.W * CanvasW;
	}

	// === 垂直方向处理 ===
	bool bSnapTopY, bSnapBottomY;
	bSnapTopY = bSnapBottomY = false;
	bool bNearTopY = false;
	bool bNearBottomY = false;
	if (Preset.bEnableEdgeSnap || Preset.bEnableStretchAnchors)
	{
		ShouldSnapToEdge(SafeBounds.Y, SafeBounds.H, Preset.EdgeSnapThreshold, bNearTopY, bNearBottomY);
		if (Preset.bEnableEdgeSnap)
		{
			bSnapTopY = bNearTopY;
			bSnapBottomY = bNearBottomY;
		}
	}
	bool bStretchY = Preset.bEnableStretchAnchors
		&& ShouldUseStretchAnchors(SafeBounds.H, Preset.StretchThreshold)
		&& bNearTopY
		&& bNearBottomY;

	if (bStretchY)
	{
		// 拉伸模式
		Slot.Anchors.Min.Y = 0.0f;
		Slot.Anchors.Max.Y = 1.0f;
		Slot.Alignment.Y = 0.0f;
		Slot.Offsets.Top = SafeBounds.Y * CanvasH;
		Slot.Offsets.Bottom = (1.0f - SafeBounds.GetBottom()) * CanvasH;
	}
	else if (bSnapTopY)
	{
		// 锚定上边缘
		Slot.Anchors.Min.Y = Slot.Anchors.Max.Y = 0.0f;
		Slot.Alignment.Y = 0.0f;
		Slot.Offsets.Top = SafeBounds.Y * CanvasH;
		Slot.Offsets.Bottom = SafeBounds.H * CanvasH;
	}
	else if (bSnapBottomY)
	{
		// 锚定下边缘
		Slot.Anchors.Min.Y = Slot.Anchors.Max.Y = 1.0f;
		Slot.Alignment.Y = 1.0f;
		Slot.Offsets.Top = -(1.0f - SafeBounds.GetBottom()) * CanvasH;
		Slot.Offsets.Bottom = SafeBounds.H * CanvasH;
	}
	else
	{
		const float CenterY = SafeBounds.GetCenterY();
		const float AnchorY = Preset.bEnableNineGrid
			? SelectAnchorWithHysteresis(CenterY, Context.OldAnchorY, Preset)
			: 0.0f;

		Slot.Anchors.Min.Y = Slot.Anchors.Max.Y = AnchorY;
		Slot.Alignment.Y = AnchorY;

		const float AnchorPixelY = AnchorY * CanvasH;
		const float AlignPointPixelY = (SafeBounds.Y + AnchorY * SafeBounds.H) * CanvasH;
		Slot.Offsets.Top = AlignPointPixelY - AnchorPixelY;
		Slot.Offsets.Bottom = SafeBounds.H * CanvasH;
	}

	return Slot;
}
