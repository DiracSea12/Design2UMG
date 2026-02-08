// Copyright Epic Games, Inc. All Rights Reserved.

#include "Evaluation/OptimizationMetrics.h"
#include "ImageToUMGLog.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

namespace
{
	// 容器类型列表
	// 这些类型在 Ground Truth 提取时被跳过，因此评估时也应该跳过
	bool IsContainerType(const FString& TypeName)
	{
		static const TSet<FString> ContainerTypes = {
			TEXT("CanvasPanel"),
			TEXT("VerticalBox"),
			TEXT("HorizontalBox"),
			TEXT("Overlay"),
			TEXT("ScrollBox"),
			TEXT("GridPanel"),
			TEXT("UniformGridPanel"),
			TEXT("WrapBox"),
			TEXT("WidgetSwitcher"),
			TEXT("SafeZone"),
			TEXT("NamedSlot"),
			TEXT("InvalidationBox"),
			TEXT("RetainerBox"),
			TEXT("Spacer"),
			TEXT("SizeBox"),
			TEXT("ScaleBox")
		};
		return ContainerTypes.Contains(TypeName);
	}

	void CollectNodesInternal(const FUMGWidgetNode& Node, TArray<FEvalNode>& OutNodes, bool bIncludeRoot, int32 Depth)
	{
		const bool bIsRoot = (Depth == 0);
		const bool bValidBounds = Node.Bounds.W > 0.0f && Node.Bounds.H > 0.0f;
		const bool bHasChildren = Node.Children.Num() > 0;

		// 跳过容器类型（无条件跳过）
		// Ground Truth 提取时无条件跳过这些类型，评估时也应该无条件跳过
		const bool bIsContainer = IsContainerType(Node.Type);

		bool bSkipRoot = bIsRoot && !bIncludeRoot;
		if (bIsRoot && bValidBounds)
		{
			// 使用统一的根容器检测逻辑
			if (FOptimizationMetrics::IsRootContainerNode(Node.Bounds, bHasChildren))
			{
				bSkipRoot = true;
			}
		}

		// 跳过条件：根节点跳过 OR 容器类型跳过
		const bool bShouldSkip = bSkipRoot || bIsContainer;

		if (bValidBounds && !bShouldSkip)
		{
			FEvalNode Eval;
			Eval.Id = !Node.Id.IsEmpty() ? Node.Id : (!Node.Name.IsEmpty() ? Node.Name : Node.Type);
			Eval.Type = Node.Type;
			Eval.Bounds = Node.Bounds;
			OutNodes.Add(Eval);
		}

		// 始终递归处理子节点，即使当前节点被跳过
		for (const FUMGWidgetNode& Child : Node.Children)
		{
			CollectNodesInternal(Child, OutNodes, true, Depth + 1);
		}
	}

	TArray<int32> SolveHungarian(const TArray<TArray<double>>& Cost)
	{
		const int32 Size = Cost.Num() - 1;
		const double INF = 1e18;

		TArray<double> U;
		TArray<double> V;
		TArray<int32> P;
		TArray<int32> Way;

		U.Init(0.0, Size + 1);
		V.Init(0.0, Size + 1);
		P.Init(0, Size + 1);
		Way.Init(0, Size + 1);

		for (int32 i = 1; i <= Size; ++i)
		{
			P[0] = i;
			int32 J0 = 0;
			TArray<double> MinV;
			TArray<bool> Used;
			MinV.Init(INF, Size + 1);
			Used.Init(false, Size + 1);

			do
			{
				Used[J0] = true;
				int32 I0 = P[J0];
				double Delta = INF;
				int32 J1 = 0;

				for (int32 J = 1; J <= Size; ++J)
				{
					if (Used[J])
					{
						continue;
					}

					double Cur = Cost[I0][J] - U[I0] - V[J];
					if (Cur < MinV[J])
					{
						MinV[J] = Cur;
						Way[J] = J0;
					}
					if (MinV[J] < Delta)
					{
						Delta = MinV[J];
						J1 = J;
					}
				}

				for (int32 J = 0; J <= Size; ++J)
				{
					if (Used[J])
					{
						U[P[J]] += Delta;
						V[J] -= Delta;
					}
					else
					{
						MinV[J] -= Delta;
					}
				}
				J0 = J1;
			}
			while (P[J0] != 0);

			do
			{
				int32 J1 = Way[J0];
				P[J0] = P[J1];
				J0 = J1;
			}
			while (J0 != 0);
		}

		TArray<int32> Assignment;
		Assignment.Init(0, Size + 1);
		for (int32 J = 1; J <= Size; ++J)
		{
			Assignment[P[J]] = J;
		}
		return Assignment;
	}
}

bool FOptimizationMetrics::IsRootContainerNode(const FBounds& Bounds, bool bHasChildren)
{
	// 根容器节点的特征：
	// 1. 位置接近 (0, 0)（使用 Abs 处理负坐标）
	// 2. 尺寸接近 (1, 1)，即覆盖整个画布
	// 3. 有子节点（空的根容器不跳过）
	const bool bLooksLikeRoot =
		(FMath::Abs(Bounds.X) <= EvalThresholds::RootBoundsEpsilon &&
		 FMath::Abs(Bounds.Y) <= EvalThresholds::RootBoundsEpsilon &&
		 FMath::IsNearlyEqual(Bounds.W, 1.0f, EvalThresholds::RootBoundsEpsilon) &&
		 FMath::IsNearlyEqual(Bounds.H, 1.0f, EvalThresholds::RootBoundsEpsilon));
	return bLooksLikeRoot && bHasChildren;
}

void FOptimizationMetrics::CollectNodes(const FUMGWidgetNode& Root, TArray<FEvalNode>& OutNodes, bool bIncludeRoot)
{
	OutNodes.Reset();
	CollectNodesInternal(Root, OutNodes, bIncludeRoot, 0);
}

float FOptimizationMetrics::ComputePosError(const FBounds& Actual, const FBounds& Expected, const FIntPoint& DesignSize)
{
	const float ErrX = FMath::Abs(Actual.X - Expected.X) * DesignSize.X;
	const float ErrY = FMath::Abs(Actual.Y - Expected.Y) * DesignSize.Y;
	return (ErrX + ErrY) * 0.5f;
}

float FOptimizationMetrics::ComputeBoundsMaxError(const FBounds& Actual, const FBounds& Expected, const FIntPoint& DesignSize)
{
	const float ErrX = FMath::Abs(Actual.X - Expected.X) * DesignSize.X;
	const float ErrY = FMath::Abs(Actual.Y - Expected.Y) * DesignSize.Y;
	const float ErrW = FMath::Abs(Actual.W - Expected.W) * DesignSize.X;
	const float ErrH = FMath::Abs(Actual.H - Expected.H) * DesignSize.Y;
	return FMath::Max(FMath::Max(ErrX, ErrY), FMath::Max(ErrW, ErrH));
}

float FOptimizationMetrics::ComputeIoU(const FBounds& A, const FBounds& B)
{
	const float InterLeft = FMath::Max(A.X, B.X);
	const float InterTop = FMath::Max(A.Y, B.Y);
	const float InterRight = FMath::Min(A.X + A.W, B.X + B.W);
	const float InterBottom = FMath::Min(A.Y + A.H, B.Y + B.H);

	const float InterW = FMath::Max(0.0f, InterRight - InterLeft);
	const float InterH = FMath::Max(0.0f, InterBottom - InterTop);
	const float InterArea = InterW * InterH;

	const float UnionArea = A.W * A.H + B.W * B.H - InterArea;
	return UnionArea > 0.0f ? InterArea / UnionArea : 0.0f;
}

bool FOptimizationMetrics::PassCenterDistanceGate(const FBounds& A, const FBounds& B, float Gate)
{
	const float CenterAX = A.X + A.W * 0.5f;
	const float CenterAY = A.Y + A.H * 0.5f;
	const float CenterBX = B.X + B.W * 0.5f;
	const float CenterBY = B.Y + B.H * 0.5f;
	const float Dist = FMath::Sqrt(FMath::Square(CenterAX - CenterBX) + FMath::Square(CenterAY - CenterBY));
	return Dist <= Gate;
}

float FOptimizationMetrics::GetAdaptiveIoUThreshold(const FBounds& Truth, const FMatchingConfig& Config)
{
	const float Area = Truth.W * Truth.H;
	if (Area < Config.TinyAreaThreshold)
	{
		return Config.TinyIoUThreshold;
	}
	return Config.IoUThreshold;
}

float FOptimizationMetrics::ComputeMatchScore(const FEvalNode& Truth, const FEvalNode& Gen, const FMatchingConfig& Config)
{
	// 对tiny元素放宽中心距离门控
	const float Area = Truth.Bounds.W * Truth.Bounds.H;
	const float EffectiveGate = (Area < Config.TinyAreaThreshold)
		? Config.CenterDistanceGate * 2.0f  // tiny元素使用2倍门控
		: Config.CenterDistanceGate;

	if (!PassCenterDistanceGate(Truth.Bounds, Gen.Bounds, EffectiveGate))
	{
		return 0.0f;
	}

	float IoU = ComputeIoU(Truth.Bounds, Gen.Bounds);
	const bool bTypeMatch = Truth.Type == Gen.Type;
	if (!bTypeMatch)
	{
		if (Config.bRequireTypeMatch)
		{
			return 0.0f;
		}
		IoU *= Config.TypeMismatchPenalty;
	}
	return IoU;
}

FMatchResult FOptimizationMetrics::HungarianMatch(const TArray<FEvalNode>& TruthNodes, const TArray<FEvalNode>& GenNodes, const FMatchingConfig& Config)
{
	FMatchResult Result;

	const int32 TruthCount = TruthNodes.Num();
	const int32 GenCount = GenNodes.Num();

	if (TruthCount == 0 && GenCount == 0)
	{
		return Result;
	}

	// 计算两个得分矩阵：
	// - ScoreMatrix: 用于匈牙利匹配（含类型惩罚，让类型匹配的元素优先配对）
	// - RawIoUMatrix: 用于阈值判断（不含惩罚，判断位置是否真正匹配）
	TArray<TArray<float>> ScoreMatrix;
	TArray<TArray<float>> RawIoUMatrix;
	ScoreMatrix.SetNum(TruthCount);
	RawIoUMatrix.SetNum(TruthCount);
	for (int32 i = 0; i < TruthCount; ++i)
	{
		ScoreMatrix[i].SetNum(GenCount);
		RawIoUMatrix[i].SetNum(GenCount);
		for (int32 j = 0; j < GenCount; ++j)
		{
			// 带惩罚的得分（用于匹配优先级）
			ScoreMatrix[i][j] = ComputeMatchScore(TruthNodes[i], GenNodes[j], Config);
			// 原始IoU（用于阈值判断）
			if (PassCenterDistanceGate(TruthNodes[i].Bounds, GenNodes[j].Bounds, 
				(TruthNodes[i].Bounds.W * TruthNodes[i].Bounds.H < Config.TinyAreaThreshold) 
					? Config.CenterDistanceGate * 2.0f : Config.CenterDistanceGate))
			{
				RawIoUMatrix[i][j] = ComputeIoU(TruthNodes[i].Bounds, GenNodes[j].Bounds);
			}
			else
			{
				RawIoUMatrix[i][j] = 0.0f;
			}
		}
	}

	const int32 Size = FMath::Max(TruthCount, GenCount);
	TArray<TArray<double>> Cost;
	Cost.SetNum(Size + 1);
	Cost[0].SetNum(Size + 1);
	for (int32 i = 1; i <= Size; ++i)
	{
		Cost[i].SetNum(Size + 1);
		for (int32 j = 1; j <= Size; ++j)
		{
			double Score = 0.0;
			if (i <= TruthCount && j <= GenCount)
			{
				Score = ScoreMatrix[i - 1][j - 1];
				Cost[i][j] = 1.0 - Score;
			}
			else
			{
				// 虚拟元素使用高Cost，确保真实元素优先匹配
				Cost[i][j] = 2.0;
			}
		}
	}

	const TArray<int32> Assignment = SolveHungarian(Cost);

	TSet<int32> MatchedGen;
	for (int32 i = 0; i < TruthCount; ++i)
	{
		const int32 AssignedCol = Assignment[i + 1];
		if (AssignedCol >= 1 && AssignedCol <= GenCount)
		{
			const FEvalNode& TruthNode = TruthNodes[i];
			const FEvalNode& GenNode = GenNodes[AssignedCol - 1];
			const bool bTypeMatch = TruthNode.Type == GenNode.Type;
			const float Threshold = GetAdaptiveIoUThreshold(TruthNode.Bounds, Config);
			
			// 关键修复：使用原始IoU（不含类型惩罚）来判断位置是否匹配
			// 这样即使类型不同，只要位置对了就能识别为TypeMismatch
			const float RawIoU = RawIoUMatrix[i][AssignedCol - 1];
			if (RawIoU >= Threshold)
			{
				Result.Matches.Add(i, AssignedCol - 1);
				MatchedGen.Add(AssignedCol - 1);
				if (bTypeMatch)
				{
					Result.TruePositives.Add(i);
				}
				else
				{
					Result.TypeMismatches.Add(i);
				}
				continue;
			}
		}
		Result.FalseNegatives.Add(i);
	}

	for (int32 j = 0; j < GenCount; ++j)
	{
		if (!MatchedGen.Contains(j))
		{
			Result.FalsePositives.Add(j);
		}
	}

	return Result;
}

FEvalMetrics FOptimizationMetrics::CompareNodes(
	const TArray<FEvalNode>& TruthNodes,
	const TArray<FEvalNode>& GenNodes,
	const FMatchingConfig& Config,
	const FIntPoint& DesignSize,
	TArray<float>& OutPosErrors,
	TArray<float>& OutBoundsMaxErrors)
{
	OutPosErrors.Reset();
	OutBoundsMaxErrors.Reset();

	FMatchResult Match = HungarianMatch(TruthNodes, GenNodes, Config);

	for (const auto& Pair : Match.Matches)
	{
		const int32 TruthIdx = Pair.Key;
		const int32 GenIdx = Pair.Value;
		const float PosErr = ComputePosError(GenNodes[GenIdx].Bounds, TruthNodes[TruthIdx].Bounds, DesignSize);
		const float BoundsErr = ComputeBoundsMaxError(GenNodes[GenIdx].Bounds, TruthNodes[TruthIdx].Bounds, DesignSize);
		
		// TypeMismatch: 位置匹配成功（IoU >= 阈值）但类型错误
		// 仍计入位置误差（位置是对的），但在 Recall 计算中会被惩罚（计入漏检）
		// 这样 MAE 反映真实的位置精度，而 Recall/CompositeScore 反映类型识别能力
		OutPosErrors.Add(PosErr);
		OutBoundsMaxErrors.Add(BoundsErr);
	}

	// 仅对真正的漏检（FalseNegatives）加入惩罚误差
	// TypeMismatch 的位置误差已在上面计入，这里只惩罚完全没匹配到的元素
	// 这样避免"漏检导致MAE变好"的作弊情况
	for (int32 i = 0; i < Match.FalseNegatives.Num(); ++i)
	{
		OutPosErrors.Add(EvalThresholds::Failed_Sample_MAE);
		OutBoundsMaxErrors.Add(EvalThresholds::Failed_Sample_MAE);
	}

	FEvalMetrics Metrics;
	if (OutPosErrors.Num() > 0)
	{
		float Sum = 0.0f;
		for (float V : OutPosErrors)
		{
			Sum += V;
		}
		Metrics.PosMAE = Sum / OutPosErrors.Num();
		Metrics.PosP95 = ComputeQuantile(OutPosErrors, 0.95f);
	}
	else if (TruthNodes.Num() > 0 || GenNodes.Num() > 0)
	{
		// No matches while having truth or generated nodes should fail MAE-based checks.
		Metrics.PosMAE = EvalThresholds::Failed_Sample_MAE;
		Metrics.PosP95 = EvalThresholds::Failed_Sample_MAE;
	}
	else
	{
		Metrics.PosP95 = 0.0f;
	}

	Metrics.TypeMismatches = Match.TypeMismatches.Num();
	Metrics.TruePositives = Match.TruePositives.Num();
	Metrics.FalseNegatives = Match.FalseNegatives.Num();
	Metrics.FalsePositives = Match.FalsePositives.Num();

	// Recall: TP / (TP + FN + TypeMismatches)
	// TypeMismatches 位置匹配但类型不对，应计入漏检（未正确识别类型）
	// 分母 = 所有 Truth 元素数量 = TP + FN + TypeMismatches
	const int32 RecallDenom = Metrics.TruePositives + Metrics.FalseNegatives + Metrics.TypeMismatches;
	// Precision: TP / (TP + FP)
	// TypeMismatches 不计入多检，因为位置是正确的，只是类型错误
	const int32 PrecisionDenom = Metrics.TruePositives + Metrics.FalsePositives;
	const bool bEmptyTruth = TruthNodes.Num() == 0;
	const bool bEmptyGen = GenNodes.Num() == 0;
	if (bEmptyTruth && bEmptyGen)
	{
		Metrics.Recall = 1.0f;
		Metrics.Precision = 1.0f;
	}
	else
	{
		Metrics.Recall = RecallDenom > 0 ? (float)Metrics.TruePositives / RecallDenom : 0.0f;
		Metrics.Precision = PrecisionDenom > 0 ? (float)Metrics.TruePositives / PrecisionDenom : 0.0f;
	}
	Metrics.CompositeScore = ComputeCompositeScore(Metrics);

	return Metrics;
}

float FOptimizationMetrics::ComputeCompositeScore(const FEvalMetrics& Metrics)
{
	const float MAEScore = FMath::Max(0.0f, 1.0f - Metrics.PosMAE / EvalThresholds::MAE_Normalization_Max);
	return MAEScore * Metrics.Recall * Metrics.Precision;
}

float FOptimizationMetrics::ComputeQuantile(const TArray<float>& Values, float Quantile)
{
	if (Values.Num() == 0)
	{
		return 0.0f;
	}
	TArray<float> Sorted = Values;
	Sorted.Sort();
	const float RealIndex = (Sorted.Num() - 1) * Quantile;
	const int32 LowIndex = FMath::Clamp(FMath::FloorToInt(RealIndex), 0, Sorted.Num() - 1);
	const int32 HighIndex = FMath::Clamp(LowIndex + 1, 0, Sorted.Num() - 1);
	const float Frac = RealIndex - LowIndex;
	return FMath::Lerp(Sorted[LowIndex], Sorted[HighIndex], Frac);
}

float FOptimizationMetrics::GetMedian(const TArray<float>& Values)
{
	if (Values.Num() == 0)
	{
		return 0.0f;
	}
	TArray<float> Sorted = Values;
	Sorted.Sort();
	const int32 Mid = Sorted.Num() / 2;
	if (Sorted.Num() % 2 == 0)
	{
		return (Sorted[Mid - 1] + Sorted[Mid]) * 0.5f;
	}
	return Sorted[Mid];
}

const FStageMetrics* FOptimizationReport::GetFinalStage() const
{
	return Stages.Num() > 0 ? &Stages.Last() : nullptr;
}

float FOptimizationReport::GetFinalPosMAE() const
{
	const FStageMetrics* Stage = GetFinalStage();
	return Stage ? Stage->PosMAE : 0.0f;
}

float FOptimizationReport::GetFinalPosP95() const
{
	const FStageMetrics* Stage = GetFinalStage();
	return Stage ? Stage->PosP95 : 0.0f;
}

float FOptimizationReport::GetFixerContribution() const
{
	const FStageMetrics* RawStage = nullptr;
	const FStageMetrics* FixerStage = nullptr;
	for (const FStageMetrics& Stage : Stages)
	{
		if (Stage.StageName == TEXT("Raw"))
		{
			RawStage = &Stage;
		}
		else if (Stage.StageName == TEXT("AfterFixer"))
		{
			FixerStage = &Stage;
		}
	}
	return (RawStage && FixerStage) ? (RawStage->PosMAE - FixerStage->PosMAE) : 0.0f;
}

float FOptimizationReport::GetLayoutCalcContribution() const
{
	const FStageMetrics* FixerStage = nullptr;
	const FStageMetrics* FinalStage = nullptr;
	for (const FStageMetrics& Stage : Stages)
	{
		if (Stage.StageName == TEXT("AfterFixer"))
		{
			FixerStage = &Stage;
		}
		else if (Stage.StageName == TEXT("Final"))
		{
			FinalStage = &Stage;
		}
	}
	return (FixerStage && FinalStage) ? (FixerStage->PosMAE - FinalStage->PosMAE) : 0.0f;
}

float FOptimizationReport::GetTotalContribution() const
{
	const FStageMetrics* RawStage = nullptr;
	const FStageMetrics* FinalStage = nullptr;
	for (const FStageMetrics& Stage : Stages)
	{
		if (Stage.StageName == TEXT("Raw"))
		{
			RawStage = &Stage;
		}
		else if (Stage.StageName == TEXT("Final"))
		{
			FinalStage = &Stage;
		}
	}
	return (RawStage && FinalStage) ? (RawStage->PosMAE - FinalStage->PosMAE) : 0.0f;
}

float FOptimizationReport::GetFixerContributionPercent() const
{
	const float Total = GetTotalContribution();
	return Total > 0.0f ? (GetFixerContribution() / Total) * 100.0f : 0.0f;
}

float FOptimizationReport::GetLayoutCalcContributionPercent() const
{
	const float Total = GetTotalContribution();
	return Total > 0.0f ? (GetLayoutCalcContribution() / Total) * 100.0f : 0.0f;
}

FRegressionThresholds FRegressionThresholds::ForTestSetSize(int32 SampleCount)
{
	FRegressionThresholds T;
	if (SampleCount < 50)
	{
		T.MAE_Mean_Regression = 1.0f;
		T.MaxRegressedSamples = 2;
	}
	else if (SampleCount >= 100)
	{
		T.MAE_Mean_Regression = 0.3f;
		T.MAE_Median_Regression = 0.2f;
		T.MaxRegressedSamples = 5;
	}
	return T;
}

bool FOptimizationMetrics::SaveReportToJson(const FOptimizationReport& Report, const FString& OutputPath, FString* OutError)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("ReportVersion"), TEXT("1.0"));
	Root->SetStringField(TEXT("TestSetName"), Report.TestSetName);
	if (!Report.ConfigName.IsEmpty())
	{
		Root->SetStringField(TEXT("ConfigName"), Report.ConfigName);
	}
	Root->SetStringField(TEXT("Timestamp"), Report.Timestamp.ToString());

	int32 TotalSamples = 0;
	int32 TotalElements = 0;
	if (Report.Stages.Num() > 0)
	{
		TotalSamples = Report.Stages[0].SampleErrors.Num();
		TotalElements = Report.Stages[0].ElementCount;
	}
	Root->SetNumberField(TEXT("TotalSamples"), TotalSamples);
	Root->SetNumberField(TEXT("TotalElements"), TotalElements);

	TArray<TSharedPtr<FJsonValue>> StageArray;
	for (const FStageMetrics& Stage : Report.Stages)
	{
		TSharedRef<FJsonObject> StageObj = MakeShared<FJsonObject>();
		StageObj->SetStringField(TEXT("Name"), Stage.StageName);
		StageObj->SetNumberField(TEXT("PosMAE"), Stage.PosMAE);
		StageObj->SetNumberField(TEXT("PosP95"), Stage.PosP95);
		StageObj->SetNumberField(TEXT("PassRate"), Stage.PassRate);
		StageObj->SetNumberField(TEXT("OverflowCount"), Stage.OverflowCount);
		StageObj->SetNumberField(TEXT("AlignmentError"), Stage.AlignmentError);
		StageObj->SetNumberField(TEXT("SpacingVariance"), Stage.SpacingVariance);
		StageObj->SetNumberField(TEXT("ElementCount"), Stage.ElementCount);
		// AnchorJump 只在 Final 阶段有意义（测试 LayoutCalculator 的 Hysteresis 效果）
		StageObj->SetBoolField(TEXT("AnchorJumpRateValid"), Stage.bAnchorJumpRateValid);
		if (Stage.bAnchorJumpRateValid)
		{
			StageObj->SetNumberField(TEXT("AnchorJumpCount"), Stage.AnchorJumpCount);
			StageObj->SetNumberField(TEXT("AnchorJumpTotal"), Stage.AnchorJumpTotal);
			StageObj->SetNumberField(TEXT("AnchorJumpRate"), Stage.AnchorJumpRate);
		}
		StageObj->SetNumberField(TEXT("AverageRecall"), Stage.AverageRecall);
		StageObj->SetNumberField(TEXT("AveragePrecision"), Stage.AveragePrecision);
		StageObj->SetNumberField(TEXT("Recall"), Stage.Recall);
		StageObj->SetNumberField(TEXT("Precision"), Stage.Precision);
		StageObj->SetNumberField(TEXT("CompositeScore"), Stage.CompositeScore);
		StageObj->SetNumberField(TEXT("TruePositives"), Stage.TruePositives);
		StageObj->SetNumberField(TEXT("FalseNegatives"), Stage.FalseNegatives);
		StageObj->SetNumberField(TEXT("FalsePositives"), Stage.FalsePositives);
		StageObj->SetNumberField(TEXT("TypeMismatches"), Stage.TypeMismatches);

		// ========== LayoutCalculator 专用指标 ==========
		StageObj->SetBoolField(TEXT("LayoutCalcMetricsValid"), Stage.bLayoutCalcMetricsValid);
		if (Stage.bLayoutCalcMetricsValid)
		{
			StageObj->SetNumberField(TEXT("EdgeSnapHitCount"), Stage.EdgeSnapHitCount);
			StageObj->SetNumberField(TEXT("EdgeSnapTotal"), Stage.EdgeSnapTotal);
			StageObj->SetNumberField(TEXT("EdgeSnapHitRate"), Stage.EdgeSnapHitRate);
			StageObj->SetNumberField(TEXT("StretchHitCount"), Stage.StretchHitCount);
			StageObj->SetNumberField(TEXT("StretchTotal"), Stage.StretchTotal);
			StageObj->SetNumberField(TEXT("StretchHitRate"), Stage.StretchHitRate);
		}

		// ========== Schema/Prompt 专用指标 ==========
		StageObj->SetBoolField(TEXT("SchemaMetricsValid"), Stage.bSchemaMetricsValid);
		if (Stage.bSchemaMetricsValid)
		{
			StageObj->SetNumberField(TEXT("FirstParseSuccessCount"), Stage.FirstParseSuccessCount);
			StageObj->SetNumberField(TEXT("TotalParseAttempts"), Stage.TotalParseAttempts);
			StageObj->SetNumberField(TEXT("FirstParseSuccessRate"), Stage.FirstParseSuccessRate);
			StageObj->SetNumberField(TEXT("LowConfidenceCount"), Stage.LowConfidenceCount);
			StageObj->SetNumberField(TEXT("TotalConfidenceCount"), Stage.TotalConfidenceCount);
			StageObj->SetNumberField(TEXT("LowConfidenceRate"), Stage.LowConfidenceRate);
		}

		TArray<TSharedPtr<FJsonValue>> SampleArray;
		for (const FSampleMetrics& Sample : Stage.SampleErrors)
		{
			TSharedRef<FJsonObject> SampleObj = MakeShared<FJsonObject>();
			SampleObj->SetStringField(TEXT("SampleName"), Sample.SampleName);
			SampleObj->SetNumberField(TEXT("PosMAE"), Sample.PosMAE);
			SampleObj->SetNumberField(TEXT("BoundsMaxP95"), Sample.BoundsMaxP95);
			SampleObj->SetNumberField(TEXT("ElementCount"), Sample.ElementCount);
			SampleObj->SetNumberField(TEXT("MissingCount"), Sample.MissingCount);
			SampleObj->SetNumberField(TEXT("ExtraCount"), Sample.ExtraCount);
			SampleObj->SetNumberField(TEXT("Recall"), Sample.Recall);
			SampleObj->SetNumberField(TEXT("Precision"), Sample.Precision);
			SampleObj->SetNumberField(TEXT("CompositeScore"), Sample.CompositeScore);
			SampleArray.Add(MakeShared<FJsonValueObject>(SampleObj));
		}
		StageObj->SetArrayField(TEXT("SampleErrors"), SampleArray);

		if (Stage.ElementPosErrors.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> PosErrors;
			for (float V : Stage.ElementPosErrors)
			{
				PosErrors.Add(MakeShared<FJsonValueNumber>(V));
			}
			StageObj->SetArrayField(TEXT("ElementPosErrors"), PosErrors);
		}

		if (Stage.ElementBoundsMaxErrors.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> BoundsErrors;
			for (float V : Stage.ElementBoundsMaxErrors)
			{
				BoundsErrors.Add(MakeShared<FJsonValueNumber>(V));
			}
			StageObj->SetArrayField(TEXT("ElementBoundsMaxErrors"), BoundsErrors);
		}

		StageArray.Add(MakeShared<FJsonValueObject>(StageObj));
	}
	Root->SetArrayField(TEXT("Stages"), StageArray);

	Root->SetNumberField(TEXT("FailedSampleCount"), Report.FailedSamples.Num());
	if (Report.FailedSamples.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> FailedArray;
		for (const FFailedSample& Failed : Report.FailedSamples)
		{
			TSharedRef<FJsonObject> FailedObj = MakeShared<FJsonObject>();
			FailedObj->SetStringField(TEXT("SampleName"), Failed.SampleName);
			FailedObj->SetStringField(TEXT("Reason"), Failed.Reason);
			FailedArray.Add(MakeShared<FJsonValueObject>(FailedObj));
		}
		Root->SetArrayField(TEXT("FailedSamples"), FailedArray);
	}

	TSharedRef<FJsonObject> Contributions = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> Fixer = MakeShared<FJsonObject>();
	Fixer->SetNumberField(TEXT("PosMAEReduction"), Report.GetFixerContribution());
	Fixer->SetNumberField(TEXT("PercentOfTotal"), Report.GetFixerContributionPercent());
	Contributions->SetObjectField(TEXT("Fixer"), Fixer);

	TSharedRef<FJsonObject> LayoutCalc = MakeShared<FJsonObject>();
	LayoutCalc->SetNumberField(TEXT("PosMAEReduction"), Report.GetLayoutCalcContribution());
	LayoutCalc->SetNumberField(TEXT("PercentOfTotal"), Report.GetLayoutCalcContributionPercent());
	Contributions->SetObjectField(TEXT("LayoutCalc"), LayoutCalc);

	TSharedRef<FJsonObject> Total = MakeShared<FJsonObject>();
	Total->SetNumberField(TEXT("PosMAEReduction"), Report.GetTotalContribution());
	Contributions->SetObjectField(TEXT("Total"), Total);
	Root->SetObjectField(TEXT("Contributions"), Contributions);

	const FStageMetrics* FinalStage = Report.GetFinalStage();
	// 根据样例数调整阈值，小测试集使用更宽松的阈值
	const int32 SampleCount = FinalStage ? FinalStage->SampleErrors.Num() : 0;
	const FRegressionThresholds Thresholds = FRegressionThresholds::ForTestSetSize(SampleCount);
	const bool bPass = FinalStage &&
		Report.FailedSamples.Num() == 0 &&
		FinalStage->PosMAE <= EvalThresholds::Acceptance_MAE &&
		FinalStage->PosP95 <= EvalThresholds::Acceptance_P95 &&
		FinalStage->PassRate >= EvalThresholds::Acceptance_PassRate &&
		FinalStage->Recall >= Thresholds.MinRecall &&
		FinalStage->Precision >= Thresholds.MinPrecision &&
		FinalStage->CompositeScore >= Thresholds.MinCompositeScore;
	Root->SetStringField(TEXT("Verdict"), bPass ? TEXT("PASS") : TEXT("FAIL"));

	FString JsonString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
	if (!FJsonSerializer::Serialize(Root, Writer))
	{
		if (OutError)
		{
			*OutError = TEXT("Failed to serialize report JSON.");
		}
		return false;
	}

	if (!FFileHelper::SaveStringToFile(JsonString, *OutputPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		if (OutError)
		{
			*OutError = FString::Printf(TEXT("Failed to write report to %s"), *OutputPath);
		}
		return false;
	}

	return true;
}

FRegressionCheckResult FOptimizationMetrics::CheckForRegressionEnhanced(
	const FOptimizationReport& Current,
	const FOptimizationReport& Baseline,
	const FRegressionThresholds& Thresholds)
{
	FRegressionCheckResult Result;

	const FStageMetrics* CurrentStage = Current.GetFinalStage();
	const FStageMetrics* BaselineStage = Baseline.GetFinalStage();
	if (!CurrentStage || !BaselineStage)
	{
		Result.bPassed = false;
		Result.FailReason = TEXT("Missing final stage metrics.");
		return Result;
	}

	auto CollectSampleNames = [](const FStageMetrics* Stage, const TArray<FFailedSample>& Failed, TSet<FString>& OutAll, TSet<FString>* OutFailed)
	{
		if (Stage)
		{
			for (const FSampleMetrics& Sample : Stage->SampleErrors)
			{
				OutAll.Add(Sample.SampleName);
			}
		}
		for (const FFailedSample& Fail : Failed)
		{
			OutAll.Add(Fail.SampleName);
			if (OutFailed)
			{
				OutFailed->Add(Fail.SampleName);
			}
		}
	};

	TSet<FString> BaselineAll;
	TSet<FString> CurrentAll;
	TSet<FString> BaselineFailed;
	TSet<FString> CurrentFailed;
	CollectSampleNames(BaselineStage, Baseline.FailedSamples, BaselineAll, &BaselineFailed);
	CollectSampleNames(CurrentStage, Current.FailedSamples, CurrentAll, &CurrentFailed);

	TArray<FString> MissingSamples;
	for (const FString& Name : BaselineAll)
	{
		if (!CurrentAll.Contains(Name))
		{
			MissingSamples.Add(Name);
		}
	}
	if (MissingSamples.Num() > 0)
	{
		MissingSamples.Sort();
		Result.bPassed = false;
		Result.FailReason += FString::Printf(TEXT("Missing samples in current run: %s\n"), *FString::Join(MissingSamples, TEXT(", ")));
	}

	TArray<FString> NewSamples;
	for (const FString& Name : CurrentAll)
	{
		if (!BaselineAll.Contains(Name))
		{
			NewSamples.Add(Name);
		}
	}
	if (NewSamples.Num() > 0)
	{
		NewSamples.Sort();
		Result.bPassed = false;
		Result.FailReason += FString::Printf(TEXT("New samples in current run: %s\n"), *FString::Join(NewSamples, TEXT(", ")));
	}

	TArray<FString> NewFailedSamples;
	for (const FString& Name : CurrentFailed)
	{
		if (!BaselineFailed.Contains(Name))
		{
			NewFailedSamples.Add(Name);
		}
	}
	if (NewFailedSamples.Num() > 0)
	{
		NewFailedSamples.Sort();
		Result.bPassed = false;
		Result.FailReason += FString::Printf(TEXT("New failed samples: %s\n"), *FString::Join(NewFailedSamples, TEXT(", ")));
	}

	Result.MAE_Mean_Diff = CurrentStage->PosMAE - BaselineStage->PosMAE;
	if (Result.MAE_Mean_Diff > Thresholds.MAE_Mean_Regression)
	{
		Result.bPassed = false;
		Result.FailReason += FString::Printf(
			TEXT("MAE mean regression: %.2f -> %.2f (+%.2fpx > %.2fpx)\n"),
			BaselineStage->PosMAE, CurrentStage->PosMAE,
			Result.MAE_Mean_Diff, Thresholds.MAE_Mean_Regression);
	}

	Result.P95_Diff = CurrentStage->PosP95 - BaselineStage->PosP95;
	if (Result.P95_Diff > Thresholds.P95_Regression)
	{
		Result.bPassed = false;
		Result.FailReason += FString::Printf(
			TEXT("P95 regression: %.2f -> %.2f (+%.2fpx > %.2fpx)\n"),
			BaselineStage->PosP95, CurrentStage->PosP95,
			Result.P95_Diff, Thresholds.P95_Regression);
	}

	TArray<float> BaselineMAEs;
	TArray<float> CurrentMAEs;
	for (const FSampleMetrics& Sample : BaselineStage->SampleErrors)
	{
		BaselineMAEs.Add(Sample.PosMAE);
	}
	for (const FSampleMetrics& Sample : CurrentStage->SampleErrors)
	{
		CurrentMAEs.Add(Sample.PosMAE);
	}

	const float BaselineMedian = GetMedian(BaselineMAEs);
	const float CurrentMedian = GetMedian(CurrentMAEs);
	Result.MAE_Median_Diff = CurrentMedian - BaselineMedian;

	if (Result.MAE_Median_Diff > Thresholds.MAE_Median_Regression)
	{
		Result.bPassed = false;
		Result.FailReason += FString::Printf(
			TEXT("MAE median regression: %.2f -> %.2f (+%.2fpx > %.2fpx)\n"),
			BaselineMedian, CurrentMedian,
			Result.MAE_Median_Diff, Thresholds.MAE_Median_Regression);
	}

	TMap<FString, float> BaselineMap;
	for (const FSampleMetrics& Sample : BaselineStage->SampleErrors)
	{
		BaselineMap.Add(Sample.SampleName, Sample.PosMAE);
	}

	Result.RegressedSampleCount = 0;
	for (const FSampleMetrics& Sample : CurrentStage->SampleErrors)
	{
		if (float* BaseMAE = BaselineMap.Find(Sample.SampleName))
		{
			const float Delta = Sample.PosMAE - *BaseMAE;
			if (Delta > Thresholds.SampleRegressionThreshold)
			{
				Result.RegressedSampleCount++;
				Result.RegressedSampleNames.Add(Sample.SampleName);
				Result.RegressedSampleDeltas.Add(Delta);
			}
		}
	}

	if (Result.RegressedSampleCount > Thresholds.MaxRegressedSamples)
	{
		Result.bPassed = false;
		Result.FailReason += FString::Printf(
			TEXT("Regressed samples too many: %d > %d\n"),
			Result.RegressedSampleCount, Thresholds.MaxRegressedSamples);
	}

	Result.Recall_Diff = CurrentStage->Recall - BaselineStage->Recall;
	if (CurrentStage->Recall < Thresholds.MinRecall)
	{
		Result.bPassed = false;
		Result.FailReason += FString::Printf(
			TEXT("Recall too low: %.1f%% < %.1f%%\n"),
			CurrentStage->Recall * 100.0f, Thresholds.MinRecall * 100.0f);
	}

	Result.Precision_Diff = CurrentStage->Precision - BaselineStage->Precision;
	if (CurrentStage->Precision < Thresholds.MinPrecision)
	{
		Result.bPassed = false;
		Result.FailReason += FString::Printf(
			TEXT("Precision too low: %.1f%% < %.1f%%\n"),
			CurrentStage->Precision * 100.0f, Thresholds.MinPrecision * 100.0f);
	}

	Result.Composite_Diff = CurrentStage->CompositeScore - BaselineStage->CompositeScore;
	if (CurrentStage->CompositeScore < Thresholds.MinCompositeScore)
	{
		Result.bPassed = false;
		Result.FailReason += FString::Printf(
			TEXT("CompositeScore too low: %.2f < %.2f\n"),
			CurrentStage->CompositeScore, Thresholds.MinCompositeScore);
	}

	if (!Result.bPassed)
	{
		UE_LOG(LogImageToUMG, Error, TEXT("Regression detected:\n%s"), *Result.FailReason);
	}

	return Result;
}
