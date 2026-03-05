// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "LayoutFixer.h"

// ============================================================================
// Fixer绠楁硶甯搁噺
// ============================================================================

namespace FixerConstants
{
	/** 鍔ㄦ€丼napPx璁＄畻绯绘暟锛歁inDim * Factor */
	constexpr float SnapPxFactor = 0.002f;
	/** SnapPx鏈€灏?鏈€澶у儚绱犲€?*/
	constexpr int32 SnapPxMin = 2;
	constexpr int32 SnapPxMax = 6;

	/** 瓒婄晫淇锛氭渶灏忓昂瀵搁槇鍊硷紝闃叉Bounds琚鍓负0 */
	constexpr float MinBoundsSize = 0.001f;

	/** 绛夎窛淇锛氶棿璺濆亸宸瘮鐜囬槇鍊?*/
	constexpr float SpacingRatioThreshold = 0.15f;
	/** 绛夎窛淇锛氱粷瀵归槇鍊间箻鏁帮紙鐩稿浜嶴napThreshold锛?*/
	constexpr float SpacingAbsThresholdMultiplier = 2.0f;
	/** 绛夎窛淇锛氬崟娆℃渶澶хЩ鍔ㄨ窛绂讳箻鏁帮紙鐩稿浜嶴napThreshold锛?*/
	constexpr float SpacingMaxMoveMultiplier = 3.0f;

	/** 淇濆畧鎬ф鏌ワ細鎬荤Щ鍔ㄨ窛绂讳笅闄?*/
	constexpr float ConservatismTotalMoveFloor = 0.03f;
	/** 淇濆畧鎬ф鏌ワ細鎬荤Щ鍔ㄨ窛绂讳箻鏁帮紙姣忓厓绱狅級 */
	constexpr float ConservatismTotalMovePerElement = 2.0f;
	/** 淇濆畧鎬ф鏌ワ細鍗曟绉诲姩璺濈涓嬮檺 */
	constexpr float ConservatismSingleMoveFloor = 0.02f;
	/** 淇濆畧鎬ф鏌ワ細鍗曟绉诲姩璺濈涔樻暟 */
	constexpr float ConservatismSingleMoveMultiplier = 3.0f;
}

// ============================================================================
// Helper Functions (v2.2 Fixer浼樺寲)
// ============================================================================

/**
 * 浠庡涓€欓€変綅绉讳腑閫夋嫨鏈€灏忎綅绉伙紙缁濆鍊兼渶灏忥級
 * @param Deltas 鍊欓€変綅绉绘暟缁? * @param Threshold 闃堝€硷紝鍙€冭檻灏忎簬闃堝€肩殑浣嶇Щ
 * @param Eps 瀹瑰樊
 * @param OutDelta 杈撳嚭鏈€浣充綅绉? * @return 鏄惁鎵惧埌鏈夋晥浣嶇Щ
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
 * 璁＄畻1D鍖洪棿閲嶅彔姣斾緥
 * @return 閲嶅彔闀垮害 / 杈冪煭鍖洪棿闀垮害
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
 * 璁＄畻涓綅鏁帮紙姣斿钩鍧囧€兼洿绋冲仴锛? */
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
// FLayoutFixer Implementation
// ============================================================================

void FLayoutFixer::Initialize(const FIntPoint& DesignSize, FFixerPreset& OutPreset)
{
	OutPreset.DesignSize = DesignSize;
	int32 MinDim = FMath::Min(DesignSize.X, DesignSize.Y);
	OutPreset.SnapPx = FMath::Clamp(
		FMath::RoundToInt(MinDim * FixerConstants::SnapPxFactor),
		FixerConstants::SnapPxMin, FixerConstants::SnapPxMax);
	// 1080p: 2px, 4K: 4px
}

float FLayoutFixer::GetAverage(const TArray<float>& Values)
{
	if (Values.Num() == 0) return 0.0f;
	float Sum = 0.0f;
	for (float V : Values) Sum += V;
	return Sum / Values.Num();
}

float FLayoutFixer::GetMaxDeviation(const TArray<float>& Values, float Average)
{
	float MaxDiff = 0.0f;
	for (float V : Values)
	{
		MaxDiff = FMath::Max(MaxDiff, FMath::Abs(V - Average));
	}
	return MaxDiff;
}

bool FLayoutFixer::FixBoundsOverflow(
	FBounds& ChildBounds,
	const FBounds& ParentBounds,
	FFixerLogEntry* OutLog)
{
	FBounds OldBounds = ChildBounds;
	bool bFixed = false;

	const float ParentRight = ParentBounds.X + ParentBounds.W;
	const float ParentBottom = ParentBounds.Y + ParentBounds.H;

	// 宸?涓婅秺鐣?
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

	// 鍙?涓嬭秺鐣?
	const float ChildRight = ChildBounds.X + ChildBounds.W;
	const float ChildBottom = ChildBounds.Y + ChildBounds.H;

	const float MinSize = FixerConstants::MinBoundsSize;

	if (ChildRight > ParentRight)
	{
		// 濡傛灉鍏冪礌瀹屽叏鍦ㄧ埗鑺傜偣鍙充晶锛屽厛绉诲姩鍒扮埗鑺傜偣鍐?
		if (ChildBounds.X >= ParentRight - MinSize)
		{
			ChildBounds.X = ParentRight - ChildBounds.W;
			// 纭繚X涓嶄細鍙樻垚璐熸暟
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
		// 濡傛灉鍏冪礌瀹屽叏鍦ㄧ埗鑺傜偣涓嬫柟锛屽厛绉诲姩鍒扮埗鑺傜偣鍐?
		if (ChildBounds.Y >= ParentBottom - MinSize)
		{
			ChildBounds.Y = ParentBottom - ChildBounds.H;
			// 纭繚Y涓嶄細鍙樻垚璐熸暟
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

bool FLayoutFixer::SnapToAlignment(
	FBounds& Bounds,
	const FBounds& ParentBounds,
	float SnapThresholdX,
	float SnapThresholdY,
	FFixerLogEntry* OutLog)
{
	FBounds OldBounds = Bounds;
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

	// v2.2: 鏀堕泦鎵€鏈夊€欓€変綅绉伙紝閫夋嫨鏈€灏忕殑
	// X鏂瑰悜鍊欓€夛細宸﹁竟缂樸€佸彸杈圭紭銆佷腑绾?
	TArray<float> DeltasX;
	DeltasX.Add(ParentBounds.X - Bounds.X);           // 宸﹁竟缂樺惛闄?
	DeltasX.Add(ParentRight - Right);                  // 鍙宠竟缂樺惛闄?
	DeltasX.Add(ParentCenterX - CenterX);              // 涓嚎鍚搁檮

	float BestDeltaX = 0.0f;
	if (ChooseBestDelta(DeltasX, SnapThresholdX, Eps, BestDeltaX))
	{
		Bounds.X += BestDeltaX;
		bFixed = true;
	}

	// Y鏂瑰悜鍊欓€夛細涓婅竟缂樸€佷笅杈圭紭銆佷腑绾?
	TArray<float> DeltasY;
	DeltasY.Add(ParentBounds.Y - Bounds.Y);           // 涓婅竟缂樺惛闄?
	DeltasY.Add(ParentBottom - Bottom);                // 涓嬭竟缂樺惛闄?
	DeltasY.Add(ParentCenterY - CenterY);              // 涓嚎鍚搁檮

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

bool FLayoutFixer::SnapToSiblings(
	FBounds& Bounds,
	const TArray<FBounds>& SiblingBounds,
	float SnapThresholdX,
	float SnapThresholdY,
	FFixerLogEntry* OutLog)
{
	FBounds OldBounds = Bounds;
	bool bFixed = false;
	const float Eps = 1e-6f;
	const float MinOverlapRatio = 0.5f;  // v2.2: 姝ｄ氦鏂瑰悜闇€瑕?0%閲嶅彔鎵嶅惛闄?
	// v2.2: 鏀堕泦鎵€鏈夊€欓€変綅绉伙紝閫夋嫨鏈€灏忕殑
	TArray<float> DeltasX;
	TArray<float> DeltasY;

	const float Right = Bounds.X + Bounds.W;
	const float Bottom = Bounds.Y + Bounds.H;

	for (const FBounds& Sibling : SiblingBounds)
	{
		const float SibRight = Sibling.X + Sibling.W;
		const float SibBottom = Sibling.Y + Sibling.H;

		// X鏂瑰悜鍚搁檮闇€瑕乊鏂瑰悜鏈夎冻澶熼噸鍙?
		float OverlapY = OverlapRatio1D(Bounds.Y, Bottom, Sibling.Y, SibBottom);
		if (OverlapY >= MinOverlapRatio)
		{
			DeltasX.Add(Sibling.X - Bounds.X);      // 宸﹁竟缂樺榻?
			DeltasX.Add(SibRight - Right);           // 鍙宠竟缂樺榻?
		}

		// Y鏂瑰悜鍚搁檮闇€瑕乆鏂瑰悜鏈夎冻澶熼噸鍙?
		float OverlapX = OverlapRatio1D(Bounds.X, Right, Sibling.X, SibRight);
		if (OverlapX >= MinOverlapRatio)
		{
			DeltasY.Add(Sibling.Y - Bounds.Y);      // 涓婅竟缂樺榻?
			DeltasY.Add(SibBottom - Bottom);         // 涓嬭竟缂樺榻?
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

TArray<TArray<int32>> FLayoutFixer::DetectRows(
	const TArray<FBounds>& Bounds,
	float OverlapThreshold)
{
	TArray<TArray<int32>> Rows;

	for (int32 i = 0; i < Bounds.Num(); ++i)
	{
		const FBounds& Node = Bounds[i];
		bool bFoundRow = false;

		for (auto& Row : Rows)
		{
			if (Row.Num() == 0) continue;

			const FBounds& First = Bounds[Row[0]];
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

TArray<TArray<int32>> FLayoutFixer::DetectColumns(
	const TArray<FBounds>& Bounds,
	float OverlapThreshold)
{
	TArray<TArray<int32>> Columns;

	for (int32 i = 0; i < Bounds.Num(); ++i)
	{
		const FBounds& Node = Bounds[i];
		bool bFoundCol = false;

		for (auto& Col : Columns)
		{
			if (Col.Num() == 0) continue;

			const FBounds& First = Bounds[Col[0]];
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

bool FLayoutFixer::FixEqualSpacing(
	TArray<FBounds>& Bounds,
	const TArray<int32>& Indices,
	bool bHorizontal,
	float Threshold,
	TArray<FFixerLogEntry>* OutLog)
{
	if (Indices.Num() < 3) return false;

	// 鎸変綅缃帓搴?
	TArray<int32> Sorted = Indices;
	Sorted.Sort([&Bounds, bHorizontal](int32 A, int32 B)
	{
		return bHorizontal ? Bounds[A].X < Bounds[B].X : Bounds[A].Y < Bounds[B].Y;
	});

	// 璁＄畻闂磋窛
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

	// v2.2: 浣跨敤涓綅鏁拌€岄潪骞冲潎鍊硷紙鏇寸ǔ鍋ワ級
	float MedianGap = GetMedian(Gaps);
	float AvgGap = GetAverage(Gaps);
	float MaxDiff = GetMaxDeviation(Gaps, AvgGap);

	float AbsThreshold = FixerConstants::SpacingAbsThresholdMultiplier * Threshold;
	float RatioThreshold = FixerConstants::SpacingRatioThreshold;
	float EffectiveThreshold = FMath::Max(AbsThreshold, FMath::Abs(MedianGap) * RatioThreshold);

	if (MaxDiff <= EffectiveThreshold) return false;

	float MaxMove = FixerConstants::SpacingMaxMoveMultiplier * Threshold;

	// 搴旂敤绛夎窛淇
	bool bApplied = false;
	for (int32 i = 1; i < Sorted.Num(); ++i)
	{
		int32 Idx = Sorted[i];
		int32 PrevIdx = Sorted[i-1];

		if (bHorizontal)
		{
			float PrevRight = Bounds[PrevIdx].X + Bounds[PrevIdx].W;
			float NewX = PrevRight + MedianGap;
			float DeltaX = NewX - Bounds[Idx].X;

			// v2.2: 闄愬埗绉诲姩骞呭害
			if (FMath::Abs(DeltaX) > MaxMove)
			{
				NewX = Bounds[Idx].X + FMath::Sign(DeltaX) * MaxMove;
			}

			// 杈圭晫妫€鏌?
			if (NewX >= 0.0f && NewX + Bounds[Idx].W <= 1.0f)
			{
				Bounds[Idx].X = NewX;
				bApplied = true;
			}
		}
		else
		{
			float PrevBottom = Bounds[PrevIdx].Y + Bounds[PrevIdx].H;
			float NewY = PrevBottom + MedianGap;
			float DeltaY = NewY - Bounds[Idx].Y;

			// v2.2: 闄愬埗绉诲姩骞呭害
			if (FMath::Abs(DeltaY) > MaxMove)
			{
				NewY = Bounds[Idx].Y + FMath::Sign(DeltaY) * MaxMove;
			}

			// 杈圭晫妫€鏌?
			if (NewY >= 0.0f && NewY + Bounds[Idx].H <= 1.0f)
			{
				Bounds[Idx].Y = NewY;
				bApplied = true;
			}
		}
	}

	return bApplied;
}

void FLayoutFixer::FixWidgetTree(
	FFixerWidgetNode& Root,
	const FFixerPreset& Preset,
	TArray<FFixerLogEntry>& OutLog,
	FFixerStats* OutStats)
{
	FFixerStats LocalStats;
	FFixerStats& Stats = OutStats ? *OutStats : LocalStats;

	float SnapThresholdX = (Preset.DesignSize.X > 0) ? (float)Preset.SnapPx / (float)Preset.DesignSize.X : 0.0f;
	float SnapThresholdY = (Preset.DesignSize.Y > 0) ? (float)Preset.SnapPx / (float)Preset.DesignSize.Y : 0.0f;

	FBounds RootParent = {0.0f, 0.0f, 1.0f, 1.0f};
	ProcessNode(Root, RootParent, Preset, SnapThresholdX, SnapThresholdY, OutLog, Stats);
}

bool FLayoutFixer::PassesConservatismCheck(
	const TArray<FBounds>& OriginalBounds,
	const TArray<FBounds>& ProposedBounds,
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

void FLayoutFixer::FixChildrenSpacing(
	TArray<FBounds>& ProposedBounds,
	const TArray<FBounds>& OriginalBounds,
	const FFixerPreset& Preset,
	float SnapThresholdX,
	float SnapThresholdY,
	TArray<FFixerLogEntry>& OutLog,
	FFixerStats& Stats)
{
	// 鍚岀骇鍏冪礌鍚搁檮
	if (Preset.bEnableSiblingSnap && ProposedBounds.Num() >= 2)
	{
		for (int32 i = 0; i < ProposedBounds.Num(); ++i)
		{
			TArray<FBounds> OtherBounds;
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

	// 绛夎窛淇
	if (!Preset.bEnableEqualSpacing || ProposedBounds.Num() < Preset.MinElementsForEqualSpacing)
		return;

	// 琛屾柟鍚戠瓑璺?
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

	// 鍒楁柟鍚戠瓑璺?
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

void FLayoutFixer::ProcessNode(
	FFixerWidgetNode& Node,
	const FBounds& ParentBounds,
	const FFixerPreset& Preset,
	float SnapThresholdX,
	float SnapThresholdY,
	TArray<FFixerLogEntry>& OutLog,
	FFixerStats& Stats)
{
	// 璞佸厤鑺傜偣璺宠繃淇
	bool bShouldFix = !Node.bFixExempt;
	if (Node.bFixExempt)
		Stats.SkippedExempt++;

	// 瓒婄晫淇
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

	// 瀵归綈鍚搁檮
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

	// 鏀堕泦鍙慨姝ｇ殑瀛愯妭鐐?
	TArray<int32> FixableIndices;
	TArray<FBounds> OriginalBounds;
	for (int32 i = 0; i < Node.Children.Num(); ++i)
	{
		if (!Node.Children[i].bFixExempt)
		{
			FixableIndices.Add(i);
			OriginalBounds.Add(Node.Children[i].Bounds);
		}
	}

	TArray<FBounds> ProposedBounds = OriginalBounds;

	// 鍚岀骇鍚搁檮 + 绛夎窛淇
	FixChildrenSpacing(ProposedBounds, OriginalBounds, Preset, SnapThresholdX, SnapThresholdY, OutLog, Stats);

	// 淇濆畧鎬ф鏌ワ紝涓嶉€氳繃鍒欏洖婊?
	bool bPass = PassesConservatismCheck(OriginalBounds, ProposedBounds, SnapThresholdX, SnapThresholdY);
	if (!bPass)
		Stats.RolledBack++;

	// 搴旂敤淇
	if (bPass)
	{
		for (int32 i = 0; i < FixableIndices.Num(); ++i)
			Node.Children[FixableIndices[i]].Bounds = ProposedBounds[i];
	}

	// 閫掑綊澶勭悊瀛愯妭鐐?
	FBounds ChildParent = (Node.Bounds.W > 0.0f && Node.Bounds.H > 0.0f) ? Node.Bounds : ParentBounds;
	for (auto& Child : Node.Children)
		ProcessNode(Child, ChildParent, Preset, SnapThresholdX, SnapThresholdY, OutLog, Stats);
}

