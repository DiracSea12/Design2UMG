// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "LayoutFixer.h"

// ============================================================================
// Fixer algorithm constants
// ============================================================================

namespace FixerConstants
{
	/** Dynamic SnapPx factor: MinDim * Factor */
	constexpr float SnapPxFactor = 0.002f;
	/** SnapPx minimum and maximum pixel values */
	constexpr int32 SnapPxMin = 2;
	constexpr int32 SnapPxMax = 6;

	/** Out-of-bounds correction: minimum size threshold to prevent Bounds from being clipped to 0 */
	constexpr float MinBoundsSize = 0.001f;

	/** Equal-spacing correction: spacing deviation ratio threshold */
	constexpr float SpacingRatioThreshold = 0.15f;
	/** Equal-spacing correction: absolute threshold multiplier relative to SnapThreshold */
	constexpr float SpacingAbsThresholdMultiplier = 2.0f;
	/** Equal-spacing correction: per-step maximum movement multiplier relative to SnapThreshold */
	constexpr float SpacingMaxMoveMultiplier = 3.0f;

	/** Conservative check: total movement lower bound */
	constexpr float ConservatismTotalMoveFloor = 0.03f;
	/** Conservative check: total movement multiplier per element */
	constexpr float ConservatismTotalMovePerElement = 2.0f;
	/** Conservative check: per-step movement lower bound */
	constexpr float ConservatismSingleMoveFloor = 0.02f;
	/** Conservative check: per-step movement multiplier */
	constexpr float ConservatismSingleMoveMultiplier = 3.0f;
}

float GetAverage(const TArray<float>& Values);
float GetMaxDeviation(const TArray<float>& Values, float Average);
bool FixBoundsOverflow(FDesignPixelBounds& ChildBounds, const FDesignPixelBounds& ParentBounds, FFixerLogEntry* OutLog = nullptr);
bool SnapToAlignment(FDesignPixelBounds& Bounds, const FDesignPixelBounds& ParentBounds, float SnapThresholdX, float SnapThresholdY, FFixerLogEntry* OutLog = nullptr);
bool SnapToSiblings(FDesignPixelBounds& Bounds, const TArray<FDesignPixelBounds>& SiblingBounds, float SnapThresholdX, float SnapThresholdY, FFixerLogEntry* OutLog = nullptr);
TArray<TArray<int32>> DetectRows(const TArray<FDesignPixelBounds>& Bounds, float OverlapThreshold);
TArray<TArray<int32>> DetectColumns(const TArray<FDesignPixelBounds>& Bounds, float OverlapThreshold);
bool FixEqualSpacing(TArray<FDesignPixelBounds>& Bounds, const TArray<int32>& Indices, bool bHorizontal, float Threshold, TArray<FFixerLogEntry>* OutLog = nullptr);
bool PassesConservatismCheck(const TArray<FDesignPixelBounds>& OriginalBounds, const TArray<FDesignPixelBounds>& ProposedBounds, float SnapThresholdX, float SnapThresholdY);
void FixChildrenSpacing(TArray<FDesignPixelBounds>& ProposedBounds, const TArray<FDesignPixelBounds>& OriginalBounds, const FFixerPreset& Preset, float SnapThresholdX, float SnapThresholdY, TArray<FFixerLogEntry>& OutLog, FFixerStats& Stats);
void ProcessNode(FFixerWidgetNode& Node, const FDesignPixelBounds& ParentBounds, const FFixerPreset& Preset, float SnapThresholdX, float SnapThresholdY, TArray<FFixerLogEntry>& OutLog, FFixerStats& Stats);

// ============================================================================
// Helper functions (v2.2 Fixer optimization)
// ============================================================================

/**
 * Select the smallest displacement from multiple candidates by absolute value.
 * @param Deltas Candidate displacement array * @param Threshold Only displacements below this threshold are considered
 * @param Eps Tolerance
 * @param OutDelta Output best displacement * @return Whether a valid displacement was found
 */
static bool ChooseBestDelta(const TArray<float>& Deltas, float Threshold, float Eps, float& OutDelta)
{
	float BestAbs = Threshold + 1.0f;
	bool bFound = false;
	for (float D : Deltas)
	{
		float AbsD = FMath::Abs(D);
		if (AbsD <= Threshold + Eps && (AbsD + Eps) < BestAbs)
		{
			BestAbs = AbsD;
			OutDelta = D;
			bFound = true;
		}
	}
	return bFound;
}

/**
 * Calculate the overlap ratio between two 1D intervals.
 * @return Overlap length / shorter interval length
 */
static float OverlapRatio1D(float A0, float A1, float B0, float B1)
{
	float L = FMath::Max(A0, B0);
	float R = FMath::Min(A1, B1);
	float O = FMath::Max(0.0f, R - L);
	float MinLen = FMath::Max(1e-6f, FMath::Min(A1 - A0, B1 - B0));
	return O / MinLen;
}

/**
 * Calculate the median, which is more robust than the mean. */
static float GetMedian(const TArray<float>& InValues)
{
	if (InValues.Num() == 0) return 0.0f;
	TArray<float> Sorted = InValues;
	Sorted.Sort();
	int32 Mid = Sorted.Num() / 2;
	if (Sorted.Num() % 2 == 1) return Sorted[Mid];
	return 0.5f * (Sorted[Mid - 1] + Sorted[Mid]);
}

// ============================================================================
// LayoutFixer implementation
// ============================================================================

void LayoutFixer::Initialize(const FIntPoint& DesignSize, FFixerPreset& OutPreset)
{
	OutPreset.DesignSize = DesignSize;
	int32 MinDim = FMath::Min(DesignSize.X, DesignSize.Y);
	OutPreset.SnapPx = FMath::Clamp(
		FMath::RoundToInt(MinDim * FixerConstants::SnapPxFactor),
		FixerConstants::SnapPxMin, FixerConstants::SnapPxMax);
	// 1080p: 2px, 4K: 4px
}

float GetAverage(const TArray<float>& Values)
{
	if (Values.Num() == 0) return 0.0f;
	float Sum = 0.0f;
	for (float V : Values) Sum += V;
	return Sum / Values.Num();
}

float GetMaxDeviation(const TArray<float>& Values, float Average)
{
	float MaxDiff = 0.0f;
	for (float V : Values)
	{
		MaxDiff = FMath::Max(MaxDiff, FMath::Abs(V - Average));
	}
	return MaxDiff;
}

bool FixBoundsOverflow(
	FDesignPixelBounds& ChildBounds,
	const FDesignPixelBounds& ParentBounds,
	FFixerLogEntry* OutLog)
{
	FDesignPixelBounds OldBounds = ChildBounds;
	bool bFixed = false;

	const float ParentRight = ParentBounds.X + ParentBounds.W;
	const float ParentBottom = ParentBounds.Y + ParentBounds.H;

	// Clamp left/top overflow.
	if (ChildBounds.X < ParentBounds.X)
	{
		ChildBounds.X = ParentBounds.X;
		bFixed = true;
	}
	if (ChildBounds.Y < ParentBounds.Y)
	{
		ChildBounds.Y = ParentBounds.Y;
		bFixed = true;
	}

	// Clamp right/bottom overflow.
	const float ChildRight = ChildBounds.X + ChildBounds.W;
	const float ChildBottom = ChildBounds.Y + ChildBounds.H;

	const float MinSize = FixerConstants::MinBoundsSize;

	if (ChildRight > ParentRight)
	{
		// If the child is fully outside on the right, move it back first.
		if (ChildBounds.X >= ParentRight - MinSize)
		{
			ChildBounds.X = ParentRight - ChildBounds.W;
			// Ensure X does not become negative.
			if (ChildBounds.X < ParentBounds.X)
			{
				ChildBounds.X = ParentBounds.X;
				ChildBounds.W = FMath::Max(MinSize, ParentBounds.W);
			}
		}
		else
		{
			ChildBounds.W = FMath::Max(MinSize, ParentRight - ChildBounds.X);
		}
		bFixed = true;
	}
	if (ChildBottom > ParentBottom)
	{
		// If the child is fully outside on the bottom, move it back first.
		if (ChildBounds.Y >= ParentBottom - MinSize)
		{
			ChildBounds.Y = ParentBottom - ChildBounds.H;
			// Ensure Y does not become negative.
			if (ChildBounds.Y < ParentBounds.Y)
			{
				ChildBounds.Y = ParentBounds.Y;
				ChildBounds.H = FMath::Max(MinSize, ParentBounds.H);
			}
		}
		else
		{
			ChildBounds.H = FMath::Max(MinSize, ParentBottom - ChildBounds.Y);
		}
		bFixed = true;
	}

	if (bFixed && OutLog)
	{
		OutLog->FixType = TEXT("Overflow");
		OutLog->OldBounds = OldBounds;
		OutLog->NewBounds = ChildBounds;
	}

	return bFixed;
}

bool SnapToAlignment(
	FDesignPixelBounds& Bounds,
	const FDesignPixelBounds& ParentBounds,
	float SnapThresholdX,
	float SnapThresholdY,
	FFixerLogEntry* OutLog)
{
	FDesignPixelBounds OldBounds = Bounds;
	bool bFixed = false;
	const float Eps = 1e-6f;

	const float ParentCenterX = ParentBounds.X + ParentBounds.W / 2.0f;
	const float ParentCenterY = ParentBounds.Y + ParentBounds.H / 2.0f;
	const float ParentRight = ParentBounds.X + ParentBounds.W;
	const float ParentBottom = ParentBounds.Y + ParentBounds.H;

	const float CenterX = Bounds.X + Bounds.W / 2.0f;
	const float CenterY = Bounds.Y + Bounds.H / 2.0f;
	const float Right = Bounds.X + Bounds.W;
	const float Bottom = Bounds.Y + Bounds.H;

	// v2.2: Collect all candidate displacements and choose the smallest.
	// Candidate X offsets: left edge, right edge, center line.
	TArray<float> DeltasX;
	DeltasX.Add(ParentBounds.X - Bounds.X);           // snap left edge
	DeltasX.Add(ParentRight - Right);                 // snap right edge
	DeltasX.Add(ParentCenterX - CenterX);             // snap center line

	float BestDeltaX = 0.0f;
	if (ChooseBestDelta(DeltasX, SnapThresholdX, Eps, BestDeltaX))
	{
		Bounds.X += BestDeltaX;
		bFixed = true;
	}

	// Candidate Y offsets: top edge, bottom edge, center line.
	TArray<float> DeltasY;
	DeltasY.Add(ParentBounds.Y - Bounds.Y);           // snap top edge
	DeltasY.Add(ParentBottom - Bottom);               // snap bottom edge
	DeltasY.Add(ParentCenterY - CenterY);             // snap center line

	float BestDeltaY = 0.0f;
	if (ChooseBestDelta(DeltasY, SnapThresholdY, Eps, BestDeltaY))
	{
		Bounds.Y += BestDeltaY;
		bFixed = true;
	}

	if (bFixed && OutLog)
	{
		OutLog->FixType = TEXT("Snap");
		OutLog->OldBounds = OldBounds;
		OutLog->NewBounds = Bounds;
	}

	return bFixed;
}

bool SnapToSiblings(
	FDesignPixelBounds& Bounds,
	const TArray<FDesignPixelBounds>& SiblingBounds,
	float SnapThresholdX,
	float SnapThresholdY,
	FFixerLogEntry* OutLog)
{
	FDesignPixelBounds OldBounds = Bounds;
	bool bFixed = false;
	const float Eps = 1e-6f;
	const float MinOverlapRatio = 0.5f;  // v2.2: Require orthogonal overlap before snapping.
	// v2.2: Collect all candidate displacements and choose the smallest.
	TArray<float> DeltasX;
	TArray<float> DeltasY;

	const float Right = Bounds.X + Bounds.W;
	const float Bottom = Bounds.Y + Bounds.H;

	for (const FDesignPixelBounds& Sibling : SiblingBounds)
	{
		const float SibRight = Sibling.X + Sibling.W;
		const float SibBottom = Sibling.Y + Sibling.H;

		// X snapping requires enough overlap on Y.
		float OverlapY = OverlapRatio1D(Bounds.Y, Bottom, Sibling.Y, SibBottom);
		if (OverlapY >= MinOverlapRatio)
		{
			DeltasX.Add(Sibling.X - Bounds.X);      // align left edges
			DeltasX.Add(SibRight - Right);          // align right edges
		}

		// Y snapping requires enough overlap on X.
		float OverlapX = OverlapRatio1D(Bounds.X, Right, Sibling.X, SibRight);
		if (OverlapX >= MinOverlapRatio)
		{
			DeltasY.Add(Sibling.Y - Bounds.Y);      // align top edges
			DeltasY.Add(SibBottom - Bottom);        // align bottom edges
		}
	}

	float BestDeltaX = 0.0f;
	if (ChooseBestDelta(DeltasX, SnapThresholdX, Eps, BestDeltaX))
	{
		Bounds.X += BestDeltaX;
		bFixed = true;
	}

	float BestDeltaY = 0.0f;
	if (ChooseBestDelta(DeltasY, SnapThresholdY, Eps, BestDeltaY))
	{
		Bounds.Y += BestDeltaY;
		bFixed = true;
	}

	if (bFixed && OutLog)
	{
		OutLog->FixType = TEXT("SiblingSnap");
		OutLog->OldBounds = OldBounds;
		OutLog->NewBounds = Bounds;
	}

	return bFixed;
}

TArray<TArray<int32>> DetectRows(
	const TArray<FDesignPixelBounds>& Bounds,
	float OverlapThreshold)
{
	TArray<TArray<int32>> Rows;

	for (int32 i = 0; i < Bounds.Num(); ++i)
	{
		const FDesignPixelBounds& Node = Bounds[i];
		bool bFoundRow = false;

		for (auto& Row : Rows)
		{
			if (Row.Num() == 0) continue;

			const FDesignPixelBounds& First = Bounds[Row[0]];
			float OverlapTop = FMath::Max(Node.Y, First.Y);
			float OverlapBottom = FMath::Min(Node.Y + Node.H, First.Y + First.H);
			float OverlapHeight = FMath::Max(0.0f, OverlapBottom - OverlapTop);
			float MinHeight = FMath::Min(Node.H, First.H);

			if (MinHeight > 0 && OverlapHeight / MinHeight > OverlapThreshold)
			{
				Row.Add(i);
				bFoundRow = true;
				break;
			}
		}

		if (!bFoundRow)
		{
			TArray<int32> NewRow;
			NewRow.Add(i);
			Rows.Add(NewRow);
		}
	}

	return Rows;
}

TArray<TArray<int32>> DetectColumns(
	const TArray<FDesignPixelBounds>& Bounds,
	float OverlapThreshold)
{
	TArray<TArray<int32>> Columns;

	for (int32 i = 0; i < Bounds.Num(); ++i)
	{
		const FDesignPixelBounds& Node = Bounds[i];
		bool bFoundCol = false;

		for (auto& Col : Columns)
		{
			if (Col.Num() == 0) continue;

			const FDesignPixelBounds& First = Bounds[Col[0]];
			float OverlapLeft = FMath::Max(Node.X, First.X);
			float OverlapRight = FMath::Min(Node.X + Node.W, First.X + First.W);
			float OverlapWidth = FMath::Max(0.0f, OverlapRight - OverlapLeft);
			float MinWidth = FMath::Min(Node.W, First.W);

			if (MinWidth > 0 && OverlapWidth / MinWidth > OverlapThreshold)
			{
				Col.Add(i);
				bFoundCol = true;
				break;
			}
		}

		if (!bFoundCol)
		{
			TArray<int32> NewCol;
			NewCol.Add(i);
			Columns.Add(NewCol);
		}
	}

	return Columns;
}

bool FixEqualSpacing(
	TArray<FDesignPixelBounds>& Bounds,
	const TArray<int32>& Indices,
	bool bHorizontal,
	float Threshold,
	TArray<FFixerLogEntry>* OutLog)
{
	if (Indices.Num() < 3) return false;

	// Sort by position before measuring gaps.
	TArray<int32> Sorted = Indices;
	Sorted.Sort([&Bounds, bHorizontal](int32 A, int32 B)
	{
		return bHorizontal ? Bounds[A].X < Bounds[B].X : Bounds[A].Y < Bounds[B].Y;
	});

	// Calculate spacing.
	TArray<float> Gaps;
	for (int32 i = 1; i < Sorted.Num(); ++i)
	{
		float Gap;
		if (bHorizontal)
		{
			Gap = Bounds[Sorted[i]].X - (Bounds[Sorted[i-1]].X + Bounds[Sorted[i-1]].W);
		}
		else
		{
			Gap = Bounds[Sorted[i]].Y - (Bounds[Sorted[i-1]].Y + Bounds[Sorted[i-1]].H);
		}
		Gaps.Add(Gap);
	}

	// Use median as the correction target, while deviation still compares to the historical mean baseline.
	float CorrectionGap = GetMedian(Gaps);
	float DeviationBaselineGap = GetAverage(Gaps);
	float MaxDiff = GetMaxDeviation(Gaps, DeviationBaselineGap);

	float AbsThreshold = FixerConstants::SpacingAbsThresholdMultiplier * Threshold;
	float RatioThreshold = FixerConstants::SpacingRatioThreshold;
	float EffectiveThreshold = FMath::Max(AbsThreshold, FMath::Abs(CorrectionGap) * RatioThreshold);

	if (MaxDiff <= EffectiveThreshold) return false;

	float MaxMove = FixerConstants::SpacingMaxMoveMultiplier * Threshold;

	// Apply equal-spacing correction.
	bool bApplied = false;
	for (int32 i = 1; i < Sorted.Num(); ++i)
	{
		int32 Idx = Sorted[i];
		int32 PrevIdx = Sorted[i-1];

		if (bHorizontal)
		{
			float PrevRight = Bounds[PrevIdx].X + Bounds[PrevIdx].W;
			float NewX = PrevRight + CorrectionGap;
			float DeltaX = NewX - Bounds[Idx].X;

			// v2.2: Limit movement distance.
			if (FMath::Abs(DeltaX) > MaxMove)
			{
				NewX = Bounds[Idx].X + FMath::Sign(DeltaX) * MaxMove;
			}

			// Respect normalized bounds.
			if (NewX >= 0.0f && NewX + Bounds[Idx].W <= 1.0f)
			{
				Bounds[Idx].X = NewX;
				bApplied = true;
			}
		}
		else
		{
			float PrevBottom = Bounds[PrevIdx].Y + Bounds[PrevIdx].H;
			float NewY = PrevBottom + CorrectionGap;
			float DeltaY = NewY - Bounds[Idx].Y;

			// v2.2: Limit movement distance.
			if (FMath::Abs(DeltaY) > MaxMove)
			{
				NewY = Bounds[Idx].Y + FMath::Sign(DeltaY) * MaxMove;
			}

			// Respect normalized bounds.
			if (NewY >= 0.0f && NewY + Bounds[Idx].H <= 1.0f)
			{
				Bounds[Idx].Y = NewY;
				bApplied = true;
			}
		}
	}

	return bApplied;
}

void LayoutFixer::FixWidgetTree(
	FFixerWidgetNode& Root,
	const FFixerPreset& Preset,
	TArray<FFixerLogEntry>& OutLog,
	FFixerStats* OutStats)
{
	FFixerStats LocalStats;
	FFixerStats& Stats = OutStats ? *OutStats : LocalStats;

	float SnapThresholdX = (Preset.DesignSize.X > 0) ? (float)Preset.SnapPx / (float)Preset.DesignSize.X : 0.0f;
	float SnapThresholdY = (Preset.DesignSize.Y > 0) ? (float)Preset.SnapPx / (float)Preset.DesignSize.Y : 0.0f;

	FDesignPixelBounds RootParent = {0.0f, 0.0f, 1.0f, 1.0f};
	ProcessNode(Root, RootParent, Preset, SnapThresholdX, SnapThresholdY, OutLog, Stats);
}

bool PassesConservatismCheck(
	const TArray<FDesignPixelBounds>& OriginalBounds,
	const TArray<FDesignPixelBounds>& ProposedBounds,
	float SnapThresholdX,
	float SnapThresholdY)
{
	float TotalMove = 0.0f;
	float MaxMove = 0.0f;
	for (int32 i = 0; i < OriginalBounds.Num(); ++i)
	{
		float Dx = ProposedBounds[i].X - OriginalBounds[i].X;
		float Dy = ProposedBounds[i].Y - OriginalBounds[i].Y;
		float D = FMath::Sqrt(Dx*Dx + Dy*Dy);
		TotalMove += D;
		MaxMove = FMath::Max(MaxMove, D);
	}

	float MaxSnap = FMath::Max(SnapThresholdX, SnapThresholdY);
	float MaxTotalMove = FMath::Max(
		FixerConstants::ConservatismTotalMoveFloor,
		OriginalBounds.Num() * FixerConstants::ConservatismTotalMovePerElement * MaxSnap);
	float MaxSingleMove = FMath::Max(
		FixerConstants::ConservatismSingleMoveFloor,
		FixerConstants::ConservatismSingleMoveMultiplier * MaxSnap);

	return (TotalMove <= MaxTotalMove) && (MaxMove <= MaxSingleMove);
}

void FixChildrenSpacing(
	TArray<FDesignPixelBounds>& ProposedBounds,
	const TArray<FDesignPixelBounds>& OriginalBounds,
	const FFixerPreset& Preset,
	float SnapThresholdX,
	float SnapThresholdY,
	TArray<FFixerLogEntry>& OutLog,
	FFixerStats& Stats)
{
	// Sibling element snapping.
	if (Preset.bEnableSiblingSnap && ProposedBounds.Num() >= 2)
	{
		for (int32 i = 0; i < ProposedBounds.Num(); ++i)
		{
			TArray<FDesignPixelBounds> OtherBounds;
			for (int32 j = 0; j < OriginalBounds.Num(); ++j)
			{
				if (i != j) OtherBounds.Add(OriginalBounds[j]);
			}
			if (SnapToSiblings(ProposedBounds[i], OtherBounds, SnapThresholdX, SnapThresholdY, nullptr))
			{
				Stats.SiblingSnapTriggered++;
			}
		}
	}

	// Equal-spacing correction.
	if (!Preset.bEnableEqualSpacing || ProposedBounds.Num() < Preset.MinElementsForEqualSpacing)
		return;

	// Equal spacing by rows.
	auto Rows = DetectRows(OriginalBounds, Preset.RowOverlapThreshold);
	for (auto& Row : Rows)
	{
		if (Row.Num() < Preset.MinElementsForEqualSpacing)
			continue;

		TArray<float> Gaps;
		Row.Sort([&](int32 A, int32 B) { return OriginalBounds[A].X < OriginalBounds[B].X; });
		for (int32 k = 1; k < Row.Num(); ++k)
		{
			Gaps.Add(OriginalBounds[Row[k]].X - (OriginalBounds[Row[k-1]].X + OriginalBounds[Row[k-1]].W));
		}
		if (Gaps.Num() > 0 && GetMaxDeviation(Gaps, GetAverage(Gaps)) <= Preset.MaxSpacingVarianceForEqualSpacing)
		{
			if (FixEqualSpacing(ProposedBounds, Row, true, SnapThresholdX, &OutLog))
				Stats.EqualSpacingTriggered++;
		}
	}

	// Equal spacing by columns.
	auto Columns = DetectColumns(OriginalBounds, Preset.RowOverlapThreshold);
	for (auto& Col : Columns)
	{
		if (Col.Num() < Preset.MinElementsForEqualSpacing)
			continue;

		TArray<float> Gaps;
		Col.Sort([&](int32 A, int32 B) { return OriginalBounds[A].Y < OriginalBounds[B].Y; });
		for (int32 k = 1; k < Col.Num(); ++k)
		{
			Gaps.Add(OriginalBounds[Col[k]].Y - (OriginalBounds[Col[k-1]].Y + OriginalBounds[Col[k-1]].H));
		}
		if (Gaps.Num() > 0 && GetMaxDeviation(Gaps, GetAverage(Gaps)) <= Preset.MaxSpacingVarianceForEqualSpacing)
		{
			if (FixEqualSpacing(ProposedBounds, Col, false, SnapThresholdY, &OutLog))
				Stats.EqualSpacingTriggered++;
		}
	}
}

void ProcessNode(
	FFixerWidgetNode& Node,
	const FDesignPixelBounds& ParentBounds,
	const FFixerPreset& Preset,
	float SnapThresholdX,
	float SnapThresholdY,
	TArray<FFixerLogEntry>& OutLog,
	FFixerStats& Stats)
{
	// Skip correction for exempt nodes.
	bool bShouldFix = !Node.bFixExempt;
	if (Node.bFixExempt)
		Stats.SkippedExempt++;

	// Out-of-bounds correction.
	if (bShouldFix && Preset.bEnableOverflowFix)
	{
		FFixerLogEntry Log;
		Log.WidgetId = Node.Id;
		if (FixBoundsOverflow(Node.Bounds, ParentBounds, &Log))
		{
			OutLog.Add(Log);
			Stats.OverflowFixTriggered++;
		}
	}

	// Alignment snapping.
	if (bShouldFix && Preset.bEnableAlignSnap)
	{
		FFixerLogEntry Log;
		Log.WidgetId = Node.Id;
		if (SnapToAlignment(Node.Bounds, ParentBounds, SnapThresholdX, SnapThresholdY, &Log))
		{
			OutLog.Add(Log);
			Stats.AlignSnapTriggered++;
		}
	}

	if (Node.Children.Num() == 0)
		return;

	// Collect children that are allowed to be adjusted.
	TArray<int32> FixableIndices;
	TArray<FDesignPixelBounds> OriginalBounds;
	for (int32 i = 0; i < Node.Children.Num(); ++i)
	{
		if (!Node.Children[i].bFixExempt)
		{
			FixableIndices.Add(i);
			OriginalBounds.Add(Node.Children[i].Bounds);
		}
	}

	TArray<FDesignPixelBounds> ProposedBounds = OriginalBounds;

	// Sibling snapping and equal-spacing correction.
	FixChildrenSpacing(ProposedBounds, OriginalBounds, Preset, SnapThresholdX, SnapThresholdY, OutLog, Stats);

	// Roll back when the conservative check fails.
	bool bPass = PassesConservatismCheck(OriginalBounds, ProposedBounds, SnapThresholdX, SnapThresholdY);
	if (!bPass)
		Stats.RolledBack++;

	// Apply correction.
	if (bPass)
	{
		for (int32 i = 0; i < FixableIndices.Num(); ++i)
			Node.Children[FixableIndices[i]].Bounds = ProposedBounds[i];
	}

	// Recurse into child nodes with the best available parent bounds.
	FDesignPixelBounds ChildParent = (Node.Bounds.W > 0.0f && Node.Bounds.H > 0.0f) ? Node.Bounds : ParentBounds;
	for (auto& Child : Node.Children)
		ProcessNode(Child, ChildParent, Preset, SnapThresholdX, SnapThresholdY, OutLog, Stats);
}

