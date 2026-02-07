# Phase 1 优化效果评估方案（统一版）

**版本**：2.4
**创建日期**：2026-01-07
**更新日期**：2026-01-07
**关联文档**：TechnicalDesign_v2.2.md

> **v2.4 更新**：
> - 新增"顶层指标"部分，简化为3个核心指标
> - 明确区分"现有能力"和"待实现能力"
> - 其它详细指标移至"诊断明细"部分
>
> **v2.3 更新**：
> - 补充IoU匹配规则细节：阈值、类型惩罚、TP/FP/FN定义
> - 新增CompositeScore综合指标，防止"删元素→MAE变好"的假收益
> - 完善缓存Key：Temperature/TopP/Schema哈希等，推荐使用完整请求体哈希
> - CI门禁改为样例级中位数+超阈值样例数，抗噪更好且便于定位

---

## 〇、顶层指标（Dashboard）

> **设计原则**：顶层只保留2-3个核心指标，一眼看出系统健康度。详细指标放诊断明细。

### 核心指标（3个）

| 指标 | 公式 | 目标 | 实现状态 |
|------|------|------|----------|
| **PosP95** | 位置误差95分位 | ≤8px | ✅ 已实现 |
| **PassRate** | MAE≤4px的样例占比 | ≥95% | ✅ 已实现 |
| **CompositeScore** | MAEScore x Recall x Precision | >=0.8 | DONE |

> **说明**：
> - PosP95 + PassRate: use `ImageToUMG.BatchTest` (AutoEval wrapper) to generate reports
> - CompositeScore: AutoEval is implemented; see section 11

### 现有能力 vs 待实现

| 能力 | 状态 | 命令/位置 |
|------|------|-----------|
| 往返验证 MAE/P95/PassRate | ✅ 已实现 | `ImageToUMG.TestConsistency` |
| 批量测试 | ✅ 已实现 | `ImageToUMG.BatchTest` |
| A/B toggle comparison | DONE | `ImageToUMG.ABTest` |
| AutoEval (IoU matching) | DONE | `ImageToUMG.AutoEval` |
| CompositeScore | DONE | AutoEval report |
| AI response cache | DONE | `FAICacheKey` |
| Stage snapshots | DONE | `FStageMetrics` |

---

## 一、概述

### 1.1 目标

为 Phase 1 每个优化项建立**可量化的评估指标**，通过对比"开启/关闭"的结果来衡量其贡献度。

### 1.2 评估策略

采用**三层混合策略**，兼顾效率、精度与可信度：

| 场景 | 方法 | 用途 | 频率 |
|------|------|------|------|
| **日常开发** | 检查点快照 | 单次运行，快速验证改动收益 | 每次提交 |
| **深度分析** | A/B开关对比 | 精确测量单项贡献，调优阈值 | 每周/里程碑 |
| **验收评审** | 消融实验 + 统计检验 | 正式报告，p-value验证显著性 | Phase完成时 |

---

## 二、优化项清单

### 2.1 LayoutCalculator 模块

| 优化项 | 描述 | 开关 | 预期效果 |
|--------|------|------|----------|
| 9宫格锚点 | 根据元素中心选择0/0.5/1锚点 | `bEnableLayoutCalculator` | 锚点准确率提升 |
| Hysteresis | 历史锚点+缓冲区防跳变 | `bEnableHysteresis` | 锚点跳变率 ↓80% |
| 边缘吸附 | 贴边元素锚定边缘 | `EdgeSnapThreshold` | 贴边误差 ↓50% |
| 拉伸锚点 | 大尺寸元素使用0-1拉伸 | `StretchThreshold` | 适配率 100% |

### 2.2 Fixer 模块

| 优化项 | 描述 | 开关 | 预期效果 |
|--------|------|------|----------|
| 越界修正 | 子元素超出父边界时裁剪 | `bEnableOverflowFix` | 越界率 → 0% |
| 父边缘吸附 | 靠近父边缘/中线的元素吸附 | `bEnableAlignSnap` | 对齐误差 ↓60% |
| 同级吸附 | 同级元素边缘对齐 | `bEnableSiblingSnap` | 组内对齐误差降低 |
| 等距修正 | 行/列内元素等距排列 | `bEnableEqualSpacing` | 间距标准差 ↓70% |
| 置信度门槛 | 低置信度元素跳过修正 | `bEnableConfidenceThreshold` | 误修正率降低 |

### 2.3 其他优化

| 优化项 | 描述 | 开关 | 预期效果 |
|--------|------|------|----------|
| Prompt v2 | 优化后的Prompt模板 | `bUsePromptV2` | Token ↓20%, 召回率 ↑10% |
| JSON Schema | 强制结构化输出 | `bEnableSchemaStrict` | 首次成功率 ≥95% |
| ViewportMapping | Fit/Stretch模式映射 | `bEnableViewportMapping` | 非标准宽高比正确映射 |

---

## 三、评估指标（诊断明细）

> **注意**：本节为诊断明细指标，用于问题定位和深度分析。顶层指标见"第〇节"。

### 3.1 误差定义（基础）

> **关键**：明确区分不同口径的误差指标，避免歧义。

#### 3.1.1 元素级误差（诊断用）

```cpp
// 单个元素的最大维度误差（像素）- 用于诊断和TopN回归定位
// 取 X/Y/W/H 四个维度的最大绝对误差
float ComputeBoundsMaxError(const FBounds& Actual, const FBounds& Expected, const FIntPoint& DesignSize)
{
    float ErrX = FMath::Abs(Actual.X - Expected.X) * DesignSize.X;
    float ErrY = FMath::Abs(Actual.Y - Expected.Y) * DesignSize.Y;
    float ErrW = FMath::Abs(Actual.W - Expected.W) * DesignSize.X;
    float ErrH = FMath::Abs(Actual.H - Expected.H) * DesignSize.Y;
    
    return FMath::Max(FMath::Max(ErrX, ErrY), FMath::Max(ErrW, ErrH));
}

// 单个元素的位置误差（像素）- 与TechnicalDesign对齐
// 仅计算位置(X/Y)的平均误差，不含尺寸
float ComputePosError(const FBounds& Actual, const FBounds& Expected, const FIntPoint& DesignSize)
{
    float ErrX = FMath::Abs(Actual.X - Expected.X) * DesignSize.X;
    float ErrY = FMath::Abs(Actual.Y - Expected.Y) * DesignSize.Y;
    
    return (ErrX + ErrY) / 2.0f;
}
```

#### 3.1.2 样例级误差（统计检验用）

> **重要**：统计检验的配对单位应为**样例级（Widget/图片）**，而非元素级。
> 原因：同一Widget内的元素强相关，不满足独立性假设；且Prompt/Schema优化会改变元素集合。

```cpp
// 单个样例（Widget/图片）的聚合误差
struct FSampleMetrics
{
    FString SampleName;          // Widget/图片名称
    float PosMAE;                // 位置平均误差 = mean(PosError_i)
    float BoundsMaxP95;          // 最大维度误差的95分位
    int32 ElementCount;          // 元素数量
    int32 MissingCount;          // 漏检数量
    int32 ExtraCount;            // 多检数量
    float Recall;                // 召回率 = TP / (TP + FN + TypeMismatches)
    float Precision;             // 精确率 = TP / (TP + FP)
    float CompositeScore;        // 综合得分 = MAEScore * Recall * Precision
};

// 样例级误差数组（用于统计检验）
TArray<FSampleMetrics> SampleErrors;  // 每个Widget/图片一条记录
```

#### 3.1.3 指标命名约定

| 指标名 | 口径 | 公式 | 用途 | 阈值 |
|--------|------|------|------|------|
| **PosMAE** | 位置 | `mean((|ΔX|+|ΔY|)/2)` | 主评指标，与TechnicalDesign对齐 | ≤4px |
| **PosP95** | 位置 | `quantile(PosError, 0.95)` | 异常值容忍度 | ≤8px |
| **BoundsMaxError** | 全维度 | `max(|ΔX|,|ΔY|,|ΔW|,|ΔH|)` | 诊断、TopN回归定位 | 参考值 |
| **PassRate** | 样例级 | `count(PosMAE_sample ≤ 4px) / N` | 样例通过比例 | ≥95% |

> **与TechnicalDesign对齐**：主评指标 PosMAE 使用位置误差平均值，与技术设计文档一致。
> BoundsMaxError 作为诊断指标，用于定位最差维度。

### 3.2 核心指标

| 指标 | 公式 | 目标值 | 说明 |
|------|------|--------|------|
| **PosMAE** | `mean(PosError_i)` | ≤4px | 元素位置平均误差（与TechnicalDesign对齐） |
| **PosP95** | `quantile(PosError_i, 0.95)` | ≤8px | 95%分位误差，反映异常值 |
| **PassRate** | `count(SamplePosMAE ≤ 4px) / N_samples` | ≥95% | 样例级通过比例 |

> **注意**：PosMAE/PosP95 基于元素级位置误差，PassRate 基于样例级聚合。

### 3.3 模块专用指标

#### LayoutCalculator

> **注意**：同一像素效果可能有多组 Anchor/Alignment/Offsets 组合，直接比较 Anchor 会误判。
> 主评使用**行为指标**，Anchor 仅作辅助参考。

| 指标 | 公式 | 说明 | 类型 |
|------|------|------|------|
| 锚点跳变率 | 跳变次数 / 微调次数 | Hysteresis效果（微调5次） | **主评** |
| Offsets变化P95 | `quantile(abs(Offsets_new - Offsets_old), 0.95)` | 微调稳定性（预留，未实现，当前不写入报告） | **主评** |
| 跨分辨率位置误差 | `max(abs(Pos_1080p - Pos_4K))` | 响应式适配效果（预留，未实现） | **主评** |
| 边缘吸附触发率 | 触发次数 / 检测总次数 | 贴边检测触发频率（非准确率） | 辅助 |
| 拉伸适配触发率 | 触发次数 / 检测总次数 | 大尺寸元素触发频率（非准确率） | 辅助 |
| 锚点准确率 | 正确锚点数 / 总锚点数 | 需先定义"规范化输出" | 辅助 |

> **锚点准确率说明**：若需使用此指标，必须先定义 Anchor 规范化规则（canonicalization），
> 例如：优先使用 0/0.5/1 锚点，Alignment 与 Anchor 一致。否则跳过此指标。

#### Fixer

| 指标 | 公式 | 说明 |
|------|------|------|
| 越界修正数 | 修正前越界数 - 修正后越界数 | 应归零 |
| 对齐误差改善 | `MAE_before - MAE_after` | 吸附效果 |
| 间距方差改善 | `Var_before - Var_after` | 等距效果 |
| 误修正率 | 错误修正数 / 总修正数 | 置信度门槛效果 |

#### Schema & Prompt

| 指标 | 公式 | 说明 |
|------|------|------|
| 首次解析成功率 | 首次成功 / 总请求 | Schema约束效果 |
| 平均重试次数 | 总重试 / 总请求（预留，当前不写入报告，需 Python 返回元数据） | 应 ↓50% |
| Token消耗 | AI响应token数（预留，当前不写入报告，需 Python 返回元数据） | Prompt v2 应 ↓20% |
| 元素召回率 | 识别元素 / 真实元素 | Prompt v2 应 ↑10% |
| 元素精确率 | 正确识别的元素 / 识别出的元素数 | `TP / (TP + FP)` ≥90% |
| Schema合规率 | 符合Schema的响应比例 | 严格模式应达100% |

#### ViewportMapping

| 指标 | 公式 | 说明 |
|------|------|------|
| 映射往返误差 | `max(abs(Bounds_out - Bounds_in))` | 目标 <0.5px 或 <1/DesignWidth（归一化） |
| 跨分辨率一致性 | `abs(Norm_1080p - Norm_4K)` | 归一化坐标误差 <1e-4 |
| 黑边处理正确性 | 人工验证 + IoU | Fit模式边缘元素映射准确度 |

> **精度说明**：
> - 浮点/取整会导致 0px 难以稳定成立，使用 <0.5px 作为实际阈值
> - 归一化误差阈值与 DesignSize 绑定：`threshold = 1.0 / DesignSize.X`
> - 1080p 下 1e-4 ≈ 0.19px，4K 下 1e-4 ≈ 0.38px

#### _meta字段（调试辅助）

| 指标 | 公式 | 说明 |
|------|------|------|
| 低置信度预测准确率 | 实际问题数 / 低置信度标记数 | confidence<0.8的元素中实际有问题的比例，目标>70% |
| 问题定位时间 | 用户测试计时 | 从发现问题到定位根因的时间，目标减少50%+ |
| 不确定属性命中率 | 需修正数 / 标记数 | uncertain_props中实际需要修正的比例，目标>60% |

---

## 四、评估方案

### 4.1 检查点快照（日常监控）

**原理**：在处理流程中插入检查点，记录中间状态，单次运行获取全流程数据。

```
AI输出 ──→ [检查点1: Raw]
    │
    ▼
  Fixer ──→ [检查点2: AfterFixer]
    │
    ▼
LayoutCalc ──→ [检查点3: Final]
    │
    ▼
  Widget
```

**数据结构**：

```cpp
struct FStageMetrics
{
    FString StageName;      // "Raw", "AfterFixer", "Final"
    float PosMAE;           // 位置平均误差（与TechnicalDesign对齐）
    float PosP95;           // 位置95%分位误差
    int32 OverflowCount;    // 越界元素数
    float AlignmentError;   // 对齐误差
    float SpacingVariance;  // 间距方差
    int32 ElementCount;     // 元素数量
    
    // 样例级误差明细（用于统计检验）
    TArray<FSampleMetrics> SampleErrors;  // 每个Widget/图片一条记录
    
    // 元素级误差明细（用于诊断和TopN回归定位）
    TArray<float> ElementPosErrors;       // 元素位置误差
    TArray<float> ElementBoundsMaxErrors; // 元素最大维度误差
};

struct FOptimizationReport
{
    TArray<FStageMetrics> Stages;
    FString TestSetName;
    FDateTime Timestamp;

    // 计算贡献度（正数=有收益）
    // 符号约定：Contribution = MAE_before - MAE_after
    // 口径：相对于 Raw 阶段的绝对改善量
    float GetFixerContribution() const
    {
        return Stages[0].PosMAE - Stages[1].PosMAE;  // Raw - AfterFixer（正数=Fixer有效）
    }

    float GetLayoutCalcContribution() const
    {
        return Stages[1].PosMAE - Stages[2].PosMAE;  // AfterFixer - Final（正数=LayoutCalc有效）
    }
    
    // 总贡献度（相对于Raw）
    float GetTotalContribution() const
    {
        return Stages[0].PosMAE - Stages[2].PosMAE;  // Raw - Final
    }
    
    // 贡献度百分比（相对于总贡献的占比）
    float GetFixerContributionPercent() const
    {
        float Total = GetTotalContribution();
        return Total > 0 ? GetFixerContribution() / Total * 100.0f : 0.0f;
    }
    
    float GetLayoutCalcContributionPercent() const
    {
        float Total = GetTotalContribution();
        return Total > 0 ? GetLayoutCalcContribution() / Total * 100.0f : 0.0f;
    }
};
```

**优点**：单次运行，效率高  
**缺点**：无法测量子优化项（如单独测Hysteresis）

---

### 4.2 A/B开关对比（深度分析）

**原理**：为每个优化项添加开关，逐个关闭测试，精确测量单项贡献。

**Feature Flags 设计**：

```cpp
// AutoEvaluator.h
USTRUCT()
struct FOptimizationFlags
{
    GENERATED_BODY()

    // LayoutCalculator
    UPROPERTY(Config, EditAnywhere, Category = "Debug|A/B Test")
    bool bEnableLayoutCalculator = true;

    UPROPERTY(Config, EditAnywhere, Category = "Debug|A/B Test")
    bool bEnableHysteresis = true;

    // 【修改】显式开关 + 独立阈值，避免魔法数禁用
    UPROPERTY(Config, EditAnywhere, Category = "Debug|A/B Test")
    bool bEnableEdgeSnap = true;
    
    UPROPERTY(Config, EditAnywhere, Category = "Debug|A/B Test", meta = (EditCondition = "bEnableEdgeSnap"))
    float EdgeSnapThreshold = 0.05f;  // 阈值只负责强度

    UPROPERTY(Config, EditAnywhere, Category = "Debug|A/B Test")
    bool bEnableStretchAnchors = true;
    
    UPROPERTY(Config, EditAnywhere, Category = "Debug|A/B Test", meta = (EditCondition = "bEnableStretchAnchors"))
    float StretchThreshold = 0.60f;   // 阈值只负责强度

    // Fixer
    UPROPERTY(Config, EditAnywhere, Category = "Debug|A/B Test")
    bool bEnableFixer = true;

    UPROPERTY(Config, EditAnywhere, Category = "Debug|A/B Test")
    bool bEnableOverflowFix = true;

    UPROPERTY(Config, EditAnywhere, Category = "Debug|A/B Test")
    bool bEnableAlignSnap = true;

    UPROPERTY(Config, EditAnywhere, Category = "Debug|A/B Test")
    bool bEnableEqualSpacing = true;

    UPROPERTY(Config, EditAnywhere, Category = "Debug|A/B Test")
    bool bEnableConfidenceThreshold = true;

    // Other
    UPROPERTY(Config, EditAnywhere, Category = "Debug|A/B Test")
    bool bUsePromptV2 = true;

    UPROPERTY(Config, EditAnywhere, Category = "Debug|A/B Test")
    bool bEnableSchemaStrict = true;

    UPROPERTY(Config, EditAnywhere, Category = "Debug|A/B Test")
    bool bEnableViewportMapping = true;
};
```

**测试流程**：

1. 基线测试：全部开启，记录指标
2. 逐个关闭：每次关闭一个优化项，记录指标
3. 对比分析：计算各优化项的贡献度 = `MAE_disabled - MAE_baseline`（正数=有收益）

> **符号约定统一**：贡献 = Disabled - Baseline = 关闭时MAE - 开启时MAE
> 正数表示开启该优化项能降低MAE，即有正向收益

**优点**：精确测量单项贡献  
**缺点**：需要 N+1 次运行（N = 优化项数量）

---

### 4.3 消融实验（验收评审）

**原理**：系统性关闭单个优化项，验证其独立贡献，并进行统计检验确保结果可信。

**实验流程**：

```
┌─────────────────────────────────────────────────────────────┐
│                    消融实验流程                              │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  1. 基线记录                                                 │
│     └─ 全部优化开启，跑完整测试集，记录所有指标              │
│                                                              │
│  2. 逐项消融                                                 │
│     └─ 每次关闭一个优化项，重新跑测试集                      │
│     └─ 记录：MAE、P95、各模块专用指标                        │
│                                                              │
│  3. 统计检验                                                 │
│     └─ 计算 p-value（配对t检验或Wilcoxon符号秩检验）         │
│     └─ p < 0.05 视为统计显著                                 │
│                                                              │
│  4. 贡献度排序                                               │
│     └─ 按 MAE 改善量排序各优化项                             │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

**统计检验实现**：

> **重要**：统计检验的配对单位应为**样例级（Widget/图片）**，而非元素级。
> 原因：
> 1. 同一Widget内的元素强相关，不满足独立性假设
> 2. Prompt/Schema优化会改变元素集合，导致元素级配对失效
> 
> 元素级误差保留用于诊断分布和TopN回归定位。

```cpp
struct FAblationResult
{
    FString OptimizationName;
    float MAE_Baseline;      // 全开时的MAE
    float MAE_Disabled;      // 关闭此项时的MAE
    float Contribution;      // MAE_Disabled - MAE_Baseline（正数=有收益）
    float EffectSize;        // Cohen's d（效应量）
    float PValue;            // 统计显著性（由离线脚本计算）
    float PValueCorrected;   // Bonferroni校正后的p-value
    bool bSignificant;       // 校正后 p < 0.05
};

// UE端：导出样例级误差供离线分析（统计检验用）
void ExportSampleErrorsForStatistics(const FString& OutputPath, 
                                      const TArray<FSampleMetrics>& BaselineSamples, 
                                      const TArray<FSampleMetrics>& DisabledSamples)
{
    TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
    
    // 样例级PosMAE数组（用于配对检验）
    TArray<TSharedPtr<FJsonValue>> BaselineArray;
    for (const auto& S : BaselineSamples)
    {
        BaselineArray.Add(MakeShareable(new FJsonValueNumber(S.PosMAE)));
    }
    Root->SetArrayField(TEXT("baseline_sample_mae"), BaselineArray);
    
    TArray<TSharedPtr<FJsonValue>> DisabledArray;
    for (const auto& S : DisabledSamples)
    {
        DisabledArray.Add(MakeShareable(new FJsonValueNumber(S.PosMAE)));
    }
    Root->SetArrayField(TEXT("disabled_sample_mae"), DisabledArray);
    
    // 样例名称（用于配对对齐）
    TArray<TSharedPtr<FJsonValue>> NamesArray;
    for (const auto& S : BaselineSamples)
    {
        NamesArray.Add(MakeShareable(new FJsonValueString(S.SampleName)));
    }
    Root->SetArrayField(TEXT("sample_names"), NamesArray);
    
    // 保存到文件
    FString JsonStr;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonStr);
    FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
    FFileHelper::SaveStringToFile(JsonStr, *OutputPath);
}

// UE端：导出元素级误差供诊断分析（非统计检验）
void ExportElementErrorsForDiagnosis(const FString& OutputPath,
                                      const TArray<FElementError>& ElementErrors)
{
    // 用于TopN回归定位、误差分布可视化等
    // ...
}
```

**离线统计脚本（Python）**：

```python
# scripts/ablation_stats.py
# 用法：
#   python ablation_stats.py --baseline <report.json> --variant <report.json> \
#       [--metric=CompositeScore] [--stage=Final] [--ablation] [--alternative=auto]
#
# --ablation: 消融实验模式，variant 是禁用版本，自动反转假设方向
#   - MAE 类指标：期望 variant > baseline（禁用后变差）
#   - Score 类指标：期望 variant < baseline（禁用后变差）
#
# NOTE: Current implementation reads AutoEval report JSON files and pairs by SampleName.
# Missing samples are excluded from stats and reported by Scripts/ablation_stats.py.
import json
import numpy as np
from scipy import stats

def analyze_ablation(json_path):
    """基于样例级MAE进行配对检验"""
    with open(json_path) as f:
        data = json.load(f)
    
    # 样例级MAE数组（每个Widget/图片一个值）
    baseline = np.array(data['baseline_sample_mae'])
    disabled = np.array(data['disabled_sample_mae'])
    
    assert len(baseline) == len(disabled), "样例数量必须一致"
    
    # 配对t检验（正态分布）或 Wilcoxon符号秩检验（非正态）
    diff = disabled - baseline
    _, p_normal = stats.shapiro(diff)
    
    if p_normal > 0.05:
        # 正态分布，使用配对t检验
        t_stat, p_value = stats.ttest_rel(disabled, baseline)
    else:
        # 非正态，使用Wilcoxon符号秩检验
        stat, p_value = stats.wilcoxon(disabled, baseline, alternative='greater')
    
    # 效应量 Cohen's d
    effect_size = np.mean(diff) / np.std(diff, ddof=1) if np.std(diff) > 0 else 0
    
    return {
        'n_samples': len(baseline),
        'mae_baseline': np.mean(baseline),
        'mae_disabled': np.mean(disabled),
        'contribution': np.mean(diff),  # Disabled - Baseline，正数=有收益
        'effect_size': effect_size,
        'p_value': p_value
    }

def bonferroni_correction(p_values, alpha=0.05):
    """多重比较校正"""
    n = len(p_values)
    corrected = [min(p * n, 1.0) for p in p_values]
    significant = [p < alpha for p in corrected]
    return corrected, significant
```
```

**消融实验报告模板**：

```markdown
## Phase 1 消融实验报告

### 执行摘要
- 测试日期：2026-01-XX
- 测试集规模：N张图片，M个元素
- 基线MAE：X.Xpx

### 消融结果

| 优化项 | 基线MAE | 关闭MAE | 贡献(px) | 效应量d | p-value | p-corrected | 显著性 |
|--------|---------|---------|----------|---------|---------|-------------|--------|
| Hysteresis | 2.3px | 2.8px | +0.5px | 0.42 | 0.003 | 0.024 | ✓ |
| AlignSnap | 2.3px | 4.1px | +1.8px | 1.21 | <0.001 | <0.008 | ✓ |
| EqualSpacing | 2.3px | 3.5px | +1.2px | 0.89 | 0.012 | 0.096 | ✓ |
| ... | ... | ... | ... | ... | ... | ... | ... |

> **符号约定**：贡献 = MAE_disabled - MAE_baseline，正数表示该优化项有正向收益
> **配对单位**：样例级（每个Widget/图片一个MAE值），非元素级
> **多重比较**：使用 Bonferroni 校正，N=8 个优化项，α=0.05/8=0.00625

### 效应量解读
- |d| < 0.2: 微小效应
- 0.2 ≤ |d| < 0.5: 小效应
- 0.5 ≤ |d| < 0.8: 中等效应
- |d| ≥ 0.8: 大效应

### 结论
- 所有优化项均有统计显著贡献（校正后 p < 0.05）
- 贡献度排序：AlignSnap(大效应) > EqualSpacing(中效应) > Hysteresis(小效应) > ...
```

---

### 4.4 AI输出缓存（消除随机性）

> **关键**：相同图片多次请求AI输出可能不同，导致A/B对比失效。

**缓存Key设计原则**：

> **核心规则**：`CacheKey = Hash(输入图像) + Hash(完整请求体)`
> 
> 任何影响AI输出的参数都必须纳入Key，否则会导致缓存命中错误。

**解决方案**：

```cpp
// 缓存Key：通过 BuildCacheKey 函数构建，覆盖所有影响AI输出的参数
// 最终生成一个 MD5 哈希字符串
struct FAICacheKey
{
    FString Hash;  // MD5 哈希，由 BuildCacheKey 计算
};

// BuildCacheKey 函数将以下参数纳入哈希计算：
// - 图像数据 MD5
// - 图像尺寸、格式、质量
// - Provider、Model、BaseUrl
// - SystemPrompt、SchemaHash
// - Temperature、TopP、MaxTokens
// - MaxAIDimension、Proxy 等

struct FAICachedResponse
{
    FAICacheKey Key;
    FString RawJSON;             // AI返回的原始JSON
    FDateTime CacheTime;
    
    // 成本指标（A/B测试时需要对比）
    int32 InputTokens;           // 输入Token数
    int32 OutputTokens;          // 输出Token数
    float LatencyMs;             // 响应延迟（毫秒）
    int32 RetryCount;            // 重试次数
    FString FinishReason;        // 完成原因（stop/length/content_filter）
};

// 测试时强制使用缓存
bool bUseCachedAIResponse = true;  // A/B测试时开启
```

**缓存Key完整性检查清单**：

| 类别 | 参数 | 是否纳入Key | 说明 |
|------|------|-------------|------|
| 图像 | 像素数据MD5 | ✓ | 缩放后的图像 |
| 图像 | 尺寸 | ✓ | 影响AI感知 |
| 图像 | 格式/质量 | ✓ | JPEG压缩会影响细节 |
| Prompt | 系统Prompt | ✓ | MD5哈希 |
| Prompt | 用户Prompt | ✓ | MD5哈希 |
| Prompt | Schema | ✓ | MD5哈希 |
| 模型 | Provider | ✓ | 不同厂商输出不同 |
| 模型 | Model | ✓ | 不同模型输出不同 |
| 模型 | Version | ✓ | 同模型不同版本可能不同 |
| 参数 | Temperature | ✓ | 影响随机性 |
| 参数 | TopP | ✓ | 影响采样 |
| 参数 | MaxTokens | ✓ | 可能截断输出 |
| 参数 | SchemaStrict | ✓ | 影响输出格式 |
| 预处理 | MaxAIDimension | ✓ | 影响缩放 |
| 预处理 | 对比度增强 | ✓ | 影响图像内容 |

> **最佳实践**：使用 `ToFullHash()` 方法，将所有参数序列化后计算MD5，
> 避免遗漏新增参数。新增任何请求参数时，只需添加到序列化逻辑即可。

**流程**：

1. 首次运行：调用AI，缓存响应（含成本指标）
2. 后续A/B测试：读取缓存，只变化本地处理逻辑
3. 测试Prompt v2时：分别缓存v1/v2响应，对比（包括Token消耗/延迟）

---

## 五、各优化项对比实验设计

> 本节为每个优化项定义具体的对比实验，明确基线组、实验组和测试输入。

### 5.1 LayoutCalculator 子项消融

> **v2.4 改进**：拆分为独立子项，避免贡献被混淆。

#### 5.1.1 9宫格锚点（基础）

| Preset | 开关状态 | 主指标 |
|--------|----------|--------|
| `v2_no_9grid` | `bEnableNineGrid=false` | PosMAE |

#### 5.1.2 Hysteresis

| Preset | 开关状态 | 主指标 |
|--------|----------|--------|
| `v2_no_hysteresis` | `bEnableHysteresis=false` | 锚点跳变率 |

**测试输入**：元素中心在 0.31~0.35 / 0.65~0.69 范围微调

#### 5.1.3 EdgeSnap（边缘吸附）

| Preset | 开关状态 | 主指标 |
|--------|----------|--------|
| `v2_no_edgesnap` | `bEnableEdgeSnap=false` | 贴边误差 |

**测试输入**：贴边元素（距边缘<5%）

#### 5.1.4 Stretch（拉伸锚点）

| Preset | 开关状态 | 主指标 |
|--------|----------|--------|
| `v2_no_stretch` | `bEnableStretchAnchors=false` | 跨分辨率误差 |

**测试输入**：大尺寸元素（W或H>60%）

---

### 5.2 ViewportMapping

**优化目标**：解决不同纵横比/分辨率下的坐标映射问题

**对比实验设计**：

```



基线组：直接 Bounds * DesignSize
  PixelX = Bounds.X * DesignSize.X
  PixelY = Bounds.Y * DesignSize.Y

实验组：ViewportMapping处理
  - Stretch模式：直接映射
  - Fit模式：计算黑边偏移后映射

测试输入：
  - 16:9 截图 → 16:9 目标（同比例）
  - 21:9 截图 → 16:9 目标（宽屏适配）
  - 4:3 截图 → 16:9 目标（窄屏适配）
  - 2560x1440 → 1920x1080（分辨率缩放）
```

Run AutoEval/BatchTest/ABTest with `--viewport=WxH` to simulate non-standard source sizes (e.g., `--viewport=2560x1080`).

**预期收益**：
- 映射往返误差：<0.5px（浮点精度限制）
- 跨分辨率一致性：归一化坐标误差 <1e-4

---

### 5.3 Fixer规则修复器

**优化目标**：自动修正对齐、越界、等距问题

**对比实验设计**：

```
基线组：AI原始输出直接使用

实验组：经过Fixer处理
  - 对齐吸附：SnapPx = clamp(minDim * 0.002, 2, 6)
  - 越界修正：子元素裁剪到父容器内
  - 等距分布：行/列分组后组内等距

测试输入：
  - 包含 2-5px 对齐偏差的元素组
  - 包含越界子元素的容器
  - 包含 3+ 个近似等距元素的行/列
```

**子功能指标**：

| 子功能 | 指标 | 计算公式 | 目标 |
|--------|------|----------|------|
| 对齐吸附 | 对齐误差减少量 | `mean(abs(偏差_before)) - mean(abs(偏差_after))` | 平均3-4px |
| 越界修正 | 越界元素消除率 | `1 - (越界数_after / 越界数_before)` | 100% |
| 等距分布 | 间距方差减少率 | `1 - (Var(gaps)_after / Var(gaps)_before)` | >80% |
| 综合 | 修正有效率 | `符合规则的元素数 / 总修正元素数` | >95% | ⏳ 待实现 |
| 综合 | 过修复率 | `overfix_count / total_fix_count` | <5% | ⏳ 待实现 |

> **v2.4 改进**：过修复率可自动计算规则
> ```cpp
> // 过修复判定：修正后误差反而增大
> const float Epsilon = 0.5f;  // 容差阈值(px)
> bool bIsOverfix = (ErrorAfter - ErrorBefore) > Epsilon;
>
> // 导出每次修正的Δ误差，用于定位问题
> struct FFixDelta {
>     FString ElementId;
>     float ErrorBefore;
>     float ErrorAfter;
>     float Delta;  // = ErrorAfter - ErrorBefore
>     bool bIsOverfix;
> };
> ```

---

### 5.4 Prompt简化 + Bounds格式

**优化目标**：AI只做识别，不做像素计算

**对比实验设计**：

```
基线组：旧Prompt
  - AI直接输出 Anchors/Offsets
  - Prompt ~200行，包含锚点计算指令

实验组：新Prompt
  - AI输出 Bounds（归一化坐标）；非Canvas容器子控件需输出 Slot
  - Prompt ~50行，聚焦识别和分类
  - 程序端 LayoutCalculator 仅对 CanvasPanel 计算 Slot

测试输入：
  - 标准测试集（50-200张HUD截图）
  - 每张图片有人工标注的 Ground Truth JSON（或往返验证）
```

**预期收益**：
- 布局MAE：从 ~15px 降至 ≤4px
- 布局P95：从 ~30px 降至 ≤8px
- 元素召回率：从 ~80% 提升至 ≥90%

---

### 5.5 JSON Schema Strict

**优化目标**：消除JSON格式漂移，减少解析失败

**对比实验设计**：

```
基线组：宽松JSON模式
  response_format: { type: "json_object" }
  - 仅保证输出是合法JSON
  - 不保证符合Schema

实验组：严格Schema模式
  response_format: { type: "json_schema", json_schema: {...} }
  - 保证输出符合预定义Schema
  - 支持的提供商：OpenAI, Anthropic, Gemini

测试输入：
  - 相同的100次生成请求
  - 记录每次请求的解析结果和重试次数
```

**预期收益**：
- 首次解析成功率：从 ~85% 提升至 >99%
- Schema合规率：从 ~90% 提升至 100%
- 平均重试次数：从 ~0.3 降至 ~0.01
- Token消耗：减少 10-20%（减少重试）
> 说明：重试次数与 Token 消耗目前未在代码中采集，需 Python 返回元数据后再验证上述收益。

---

### 5.6 _meta字段支持

**优化目标**：提升调试能力，显式化不确定性

**对比实验设计**：

```
基线组：无_meta信息
  - 调试时需要逐个检查元素
  - 无法快速定位问题来源

实验组：有_meta信息
  {
    "_meta": {
      "confidence": 0.85,
      "source": "ai",
      "uncertain_props": ["FontSize"]
    }
  }

测试方法：
  - 用户测试：给定问题场景，记录定位时间
  - 统计分析：对比 confidence 与实际错误的相关性
```

**预期收益**：
- 低置信度预测准确率：>70%（低置信度元素确实更可能有问题）
- 问题定位时间：减少 50%+
- 不确定属性命中率：>60%

---

## 六、测试数据集

### 6.1 数据集分层

> **重要**：日常门禁和验收统计使用不同规模的测试集。

| 用途 | 规模 | 说明 | 阈值 |
|------|------|------|------|
| **日常门禁** | 35张（~500元素） | 快速冒烟测试，回归报警 | MAE回退>0.5px阻止合并 |
| **验收统计** | 100+张（~1500元素） | 正式评审，统计检验可靠 | p<0.05（Bonferroni校正后） |
| **深度分析** | 200+张（~3000元素） | 阈值调优，边缘案例覆盖 | 效应量d>0.5 |

### 6.2 数据集组成（日常门禁）

| 类别 | 数量 | 示例 | 测试重点 |
|------|------|------|----------|
| **简单HUD** | 10张 | 血条、弹药、小地图 | 基础布局准确性 |
| **复杂HUD** | 10张 | 多层嵌套、横竖混排 | Fixer等距/对齐 |
| **对话框** | 5张 | 弹窗、确认框 | 居中对齐 |
| **全屏界面** | 5张 | 主菜单、背包 | 拉伸锚点适配 |
| **边缘案例** | 5张 | 21:9截图、密集布局 | ViewportMapping |

**总计**：35张测试图片

### 6.3 Ground Truth 来源

| 方式 | 优先级 | 说明 |
|------|--------|------|
| 从真实UMG提取 | **推荐** | 使用 `UMGSampleExtractor` 导出现有Widget |
| 人工标注 | 备选 | 在UMG编辑器中手动创建对应布局 |

**控制台命令**：

> **注意**：命令名称与现有代码实现对齐

```bash
# 从现有Widget提取测试样例（对应 UMGSampleExtractor）
ImageToUMG.TestExtract /Game/UI/HUD

# 批量一致性测试（对应 UMGExtractorTest）
ImageToUMG.TestConsistency /Game/UI

# 批量评估（带检查点快照）
ImageToUMG.BatchTest /Game/UI --config=v2_full
ImageToUMG.BatchTest /Game/UI --config=v2_full --viewport=2560x1080
```

---

## 七、测试命令设计

### 7.1 命令格式

```bash
# 单项A/B测试
ImageToUMG.ABTest <ContentPath> <ConfigA> <ConfigB> [--output=<path>] [--iterations=N] [--no-cache] [--viewport=WxH]

# 批量评估（带检查点快照）
ImageToUMG.BatchTest <ContentPath> [--config=<preset>] [--output=<path>] [--iterations=N] [--no-cache] [--viewport=WxH]

# 可用preset
#   v2_full          - 所有优化开启（基线）
#   v2_no_hysteresis - 禁用Hysteresis
#   v2_no_9grid      - disable 9-grid anchors
#   v2_no_fixer      - 禁用Fixer
#   v2_no_schema     - 禁用Schema Strict
#   v1_baseline      - Prompt v1 + 禁用所有优化
```

### 7.2 输出格式

**JSON 输出**：`Saved/ImageToUMG/Reports/AutoEval_<timestamp>.json`

```json
{
  "TestSetName": "HUD_TestSet_v1",
  "Timestamp": "2026-01-07T15:30:00",
  "TotalSamples": 35,
  "TotalElements": 487,
  "Stages": [
    {
      "Name": "Raw", 
      "PosMAE": 12.5, 
      "PosP95": 28.3, 
      "OverflowCount": 23,
      "SampleErrors": [
        {"SampleName": "WBP_HUD_01", "PosMAE": 10.2, "ElementCount": 15},
        {"SampleName": "WBP_HUD_02", "PosMAE": 14.8, "ElementCount": 12}
      ],
      "ElementPosErrors": [8.1, 12.3, 5.2, ...]
    },
    {
      "Name": "AfterFixer", 
      "PosMAE": 6.8, 
      "PosP95": 15.2, 
      "OverflowCount": 0
    },
    {
      "Name": "Final", 
      "PosMAE": 2.3, 
      "PosP95": 5.1, 
      "OverflowCount": 0
    }
  ],
  "Contributions": {
    "Fixer": {"PosMAEReduction": 5.7, "PercentOfTotal": 55.9},
    "LayoutCalc": {"PosMAEReduction": 4.5, "PercentOfTotal": 44.1},
    "Total": {"PosMAEReduction": 10.2, "PercentOfRaw": 81.6}
  },
  "Verdict": "PASS"
}
```

> **数据分层**：
> - `SampleErrors`：样例级误差，用于统计检验（配对单位）
> - `ElementPosErrors`：元素级误差，用于诊断和TopN回归定位

---

## 八、预期输出报告

### 8.1 检查点快照报告

```
========== 优化效果报告 ==========
测试集: HUD_TestSet_v1 (35张图片, 487个元素)
时间: 2026-01-07 15:30:00

阶段指标:
┌─────────────┬─────────┬─────────┬──────────┬────────────┐
│ 阶段        │ PosMAE  │ PosP95  │ 越界数   │ 对齐误差   │
├─────────────┼─────────┼─────────┼──────────┼────────────┤
│ Raw         │ 12.5px  │ 28.3px  │ 23       │ 8.2px      │
│ AfterFixer  │ 6.8px   │ 15.2px  │ 0        │ 3.1px      │
│ Final       │ 2.3px   │ 5.1px   │ 0        │ 1.2px      │
└─────────────┴─────────┴─────────┴──────────┴────────────┘

模块贡献（相对于Raw阶段）:
- Fixer: PosMAE ↓5.7px (贡献占比 55.9%)
- LayoutCalc: PosMAE ↓4.5px (贡献占比 44.1%)
- 总提升: PosMAE ↓10.2px (相对Raw降低 81.6%)

> 符号约定：贡献 = MAE_before - MAE_after，正数表示有效
> 百分比口径：各模块贡献 / 总贡献 × 100%
> 指标口径：PosMAE = 位置误差平均值 = mean((|ΔX|+|ΔY|)/2)

验收: PASS (PosMAE=2.3px ≤ 4px, PosP95=5.1px ≤ 8px)
```

### 8.2 A/B对比报告

```
========== A/B对比报告 ==========
测试集: HUD_TestSet_v1

优化项贡献度（正数=有收益）:
┌─────────────────────┬───────────┬───────────┬──────────┐
│ 优化项              │ 开启PosMAE│ 关闭PosMAE│ 贡献(px) │
├─────────────────────┼───────────┼───────────┼──────────┤
│ LayoutCalculator    │ 2.3px     │ 8.5px     │ +6.2px   │
│ ├─ Hysteresis       │ 2.3px     │ 2.8px     │ +0.5px   │
│ ├─ EdgeSnap         │ 2.3px     │ 3.1px     │ +0.8px   │
│ └─ StretchDetect    │ 2.3px     │ 4.2px     │ +1.9px   │
│ Fixer               │ 2.3px     │ 6.8px     │ +4.5px   │
│ ├─ OverflowFix      │ 2.3px     │ 2.5px     │ +0.2px   │
│ ├─ AlignSnap        │ 2.3px     │ 4.1px     │ +1.8px   │
│ └─ EqualSpacing     │ 2.3px     │ 3.5px     │ +1.2px   │
│ SchemaStrict        │ 2.3px     │ 3.8px     │ +1.5px   │
│ PromptV2            │ 2.3px     │ 3.2px     │ +0.9px   │
└─────────────────────┴───────────┴───────────┴──────────┘

> 符号约定：贡献 = PosMAE_关闭 - PosMAE_开启 = Disabled - Baseline
> 正数表示该优化项有正向收益（开启后MAE降低）
> 注意：子项贡献不可简单相加（存在交互效应）

子指标:
- Hysteresis: 锚点跳变率 2% (目标≤5%)  ✓
- Schema: 首次成功率 97% (目标≥95%)     ✓
- PromptV2: Token消耗 ↓23% (目标≥20%)   ✓

结论: 所有优化项均有正向贡献，建议保持开启
```

---

## 九、实现计划

> **状态说明**：✅ 已实现 | ⏳ 待实现 | 🔄 部分实现

### 9.1 基础设施（优先级：高）

| 任务 | 文件 | 状态 |
|------|------|------|
| 定义 `FOptimizationFlags` | ImageToUMGSettings.h | ⏳ |
| 定义 `FStageMetrics` / `FOptimizationReport` | OptimizationMetrics.h | ⏳ |
| 实现检查点数据收集 | UMGWidgetUtils.cpp | ⏳ |
| 实现AI响应缓存 | AIResponseCache.cpp | ⏳ |

### 9.2 开关接入（优先级：中）

| 任务 | 文件 | 状态 |
|------|------|------|
| LayoutCalculator 开关 | LayoutCalculator.cpp | ⏳ |
| Fixer 开关 | LayoutFixer.cpp | ⏳ |
| Schema/Prompt 开关 | AIProviderStrategy.cpp | ⏳ |

### 9.3 测试工具（优先级：中）

> **注意**：命令注册位置与现有代码对齐

| 任务 | 文件 | 状态 |
|------|------|------|
| `ImageToUMG.TestExtract` 命令 | UMGExtractorTest.h | ✅ |
| `ImageToUMG.TestConsistency` 命令 | UMGExtractorTest.h | ✅ |
| `ImageToUMG.BatchTest` 命令 | UMGExtractorTest.h | ✅ |
| ImageToUMG.ABTest command | UMGExtractorTest.h | DONE |
| ImageToUMG.AutoEval command | UMGExtractorTest.h | DONE |
| Report generator | OptimizationMetrics.cpp | DONE |
| 离线统计脚本 | Scripts/ablation_stats.py | ✅ |

### 9.4 测试数据（优先级：低）

| 任务 | 状态 |
|------|------|
| 收集35张测试图片 | ⏳ |
| 提取/标注Ground Truth | ⏳ |

---

## 十、注意事项与风险缓解

### 10.1 关键注意事项

1. **AI输出缓存必须开启**：A/B测试时相同图片使用缓存的AI输出，排除AI随机性
2. **缓存Key完整性**：必须包含所有影响AI输出的参数（见4.4节）
3. **测试集保持稳定**：避免中途修改测试集，确保可比性
4. **关注P95而非仅MAE**：P95反映异常值，对用户体验影响更大
5. **统计显著性分层**：
   - 日常门禁（35张）：仅做回归报警，不做统计检验
   - 验收统计（100+张）：需统计检验，结果可靠
6. **多重比较校正**：消融测试N个优化项时，使用Bonferroni校正（α/N）
7. **边缘案例单独分析**：极端宽高比、密集布局等案例需单独统计

### 10.2 风险与缓解措施

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| 测试集偏差 | 评估结果不代表真实场景 | 持续扩充测试集，覆盖更多HUD类型；使用往返验证扫描项目现有Widget |
| 过拟合测试集 | 优化针对测试集而非通用场景 | 保留验证集（20%），定期更换测试集 |
| 人工标注误差 | Ground Truth不准确 | 优先使用往返验证（零人工）；人工标注时多人交叉验证 |
| 指标定义模糊 | 不同人计算结果不一致 | 明确计算公式，提供参考实现（见本文档） |
| AI输出随机性 | A/B对比失效 | 强制使用AI响应缓存（4.4节） |
| 统计不显著 | 无法证明优化有效 | 增加样例数；使用非参数检验（Wilcoxon）替代t检验 |

---

## 十一、全自动评测流程

### 11.1 核心思路：往返验证（Round-Trip Validation）

**无需手动标注 Ground Truth**，使用现有 UMG Widget 作为测试源：

```
已有Widget → 截图 → AI分析 → 生成Widget' → 对比(Widget, Widget')
     ↑                                              ↓
     └───────── Ground Truth ──────────────────────┘
```

### 11.2 自动化流程

```cpp
// 一键评测命令
// ImageToUMG.AutoEval /Game/UI --config=v2_full --iterations=3

bool FAutoEvaluator::RunAutoEvaluation(const FString& ContentPath, const FAutoEvalOptions& Options)
{
    // 1. 扫描目录下所有Widget Blueprint
    TArray<UWidgetBlueprint*> Widgets = ScanWidgetBlueprints(ContentPath);
    
    // 2. 对每个Widget执行往返测试
    for (auto* Widget : Widgets)
    {
        // 2.1 提取Ground Truth（Slot数据）
        FWidgetTree GroundTruth = ExtractWidgetTree(Widget);
        
        // 2.2 渲染为截图
        UTexture2D* Screenshot = RenderWidgetToTexture(Widget, FIntPoint(1920, 1080));
        
        // 2.3 走完整Pipeline
        FWidgetTree Generated = RunPipeline(Screenshot, Config);
        
        // 2.4 计算误差
        FEvalMetrics Metrics = CompareWidgetTrees(GroundTruth, Generated);
        
        // 2.5 记录结果
        Results.Add({Widget->GetName(), Metrics});
    }
    
    // 3. 生成报告
    GenerateReport(Results);
}
```

### 11.3 对比算法

#### 11.3.1 元素匹配规则（IoU + 匈牙利匹配）

> **关键**：匹配规则直接影响召回率/精确率计算，必须明确可执行细节。

**匹配参数**：

```cpp
struct FMatchingConfig
{
    float IoUThreshold = 0.5f;       // IoU阈值，低于此值不匹配
    bool bRequireTypeMatch = false;  // v2.4: 改为false，让TypeMismatchPenalty生效
    float TypeMismatchPenalty = 0.3f;// 类型不匹配时的IoU惩罚（乘数）

    // v2.4: 按面积分桶阈值，对tiny元素更宽容
    float TinyAreaThreshold = 0.01f; // 面积<1%视为tiny
    float TinyIoUThreshold = 0.3f;   // tiny元素IoU阈值降低
    float CenterDistanceGate = 0.05f;// 中心距离门控（归一化）
};

// v2.4: 自适应IoU阈值
float GetAdaptiveIoUThreshold(const FBounds& Truth, const FMatchingConfig& Config)
{
    float Area = Truth.W * Truth.H;
    if (Area < Config.TinyAreaThreshold)
    {
        return Config.TinyIoUThreshold;  // tiny元素用更低阈值
    }
    return Config.IoUThreshold;
}

// v2.4: 中心距离门控（IoU前置过滤）
bool PassCenterDistanceGate(const FBounds& A, const FBounds& B, float Gate)
{
    float CenterAX = A.X + A.W * 0.5f;
    float CenterAY = A.Y + A.H * 0.5f;
    float CenterBX = B.X + B.W * 0.5f;
    float CenterBY = B.Y + B.H * 0.5f;
    float Dist = FMath::Sqrt(FMath::Square(CenterAX - CenterBX) +
                             FMath::Square(CenterAY - CenterBY));
    return Dist <= Gate;
}
```

**匹配算法**：

```cpp
// 计算两个Bounds的IoU
float ComputeIoU(const FBounds& A, const FBounds& B)
{
    float InterLeft = FMath::Max(A.X, B.X);
    float InterTop = FMath::Max(A.Y, B.Y);
    float InterRight = FMath::Min(A.X + A.W, B.X + B.W);
    float InterBottom = FMath::Min(A.Y + A.H, B.Y + B.H);
    
    float InterW = FMath::Max(0.0f, InterRight - InterLeft);
    float InterH = FMath::Max(0.0f, InterBottom - InterTop);
    float InterArea = InterW * InterH;
    
    float UnionArea = A.W * A.H + B.W * B.H - InterArea;
    return UnionArea > 0 ? InterArea / UnionArea : 0.0f;
}

// 计算匹配得分（严格匹配时不接受类型不一致）
float ComputeMatchScore(const FWidgetNode& Truth, const FWidgetNode& Gen, const FMatchingConfig& Config)
{
    float IoU = ComputeIoU(Truth.Bounds, Gen.Bounds);
    
    // 类型不匹配：严格模式直接拒绝；非严格模式施加惩罚
    if (Truth.Type != Gen.Type)
    {
        if (Config.bRequireTypeMatch)
        {
            return 0.0f;
        }
        IoU *= Config.TypeMismatchPenalty;
    }
    
    return IoU;
}

// 匈牙利匹配（最大权二分图匹配）
struct FMatchResult
{
    TMap<FString, FString> Matches;  // TruthId → GenId
    TSet<FString> TruePositives;     // 成功匹配的Truth元素
    TSet<FString> FalseNegatives;    // 漏检（Truth中未匹配）
    TSet<FString> FalsePositives;    // 多检（Gen中未匹配）
    TSet<FString> TypeMismatches;    // 类型错误（位置匹配但类型不同）
};

FMatchResult HungarianMatch(const FWidgetTree& Truth, const FWidgetTree& Gen, const FMatchingConfig& Config)
{
    FMatchResult Result;
    
    // 1. 构建得分矩阵
    TArray<FWidgetNode> TruthNodes = Truth.GetAllNodes();
    TArray<FWidgetNode> GenNodes = Gen.GetAllNodes();
    
    TArray<TArray<float>> ScoreMatrix;
    for (const auto& T : TruthNodes)
    {
        TArray<float> Row;
        for (const auto& G : GenNodes)
        {
            Row.Add(ComputeMatchScore(T, G, Config));
        }
        ScoreMatrix.Add(Row);
    }
    
    // 2. 匈牙利算法求解最大权匹配
    TArray<int32> Assignment = SolveHungarian(ScoreMatrix);
    
    // 3. 根据IoU阈值筛选有效匹配
    for (int32 i = 0; i < TruthNodes.Num(); i++)
    {
        int32 j = Assignment[i];
        if (j >= 0 && ScoreMatrix[i][j] >= Config.IoUThreshold)
        {
            Result.Matches.Add(TruthNodes[i].Id, GenNodes[j].Id);
            Result.TruePositives.Add(TruthNodes[i].Id);
            
            // 检查类型是否匹配
            if (TruthNodes[i].Type != GenNodes[j].Type)
            {
                Result.TypeMismatches.Add(TruthNodes[i].Id);
            }
        }
        else
        {
            Result.FalseNegatives.Add(TruthNodes[i].Id);  // 漏检
        }
    }
    
    // 4. 统计多检
    TSet<FString> MatchedGenIds;
    for (const auto& [_, GenId] : Result.Matches)
    {
        MatchedGenIds.Add(GenId);
    }
    for (const auto& G : GenNodes)
    {
        if (!MatchedGenIds.Contains(G.Id))
        {
            Result.FalsePositives.Add(G.Id);  // 多检
        }
    }
    
    return Result;
}
```

#### 11.3.2 TP/FP/FN 定义与综合指标

> **防止"删元素→MAE变好"的假收益**：召回率和MAE必须联合评估。

**指标定义**：

| 指标 | 定义 | 计算 |
|------|------|------|
| TP (True Positive) | IoU≥0.5 且类型匹配的元素 | `Matches.Num() - TypeMismatches.Num()` |
| FN (False Negative) | 漏检元素 | `TruthNodes.Num() - Matches.Num()` |
| FP (False Positive) | 多检元素 | `GenNodes.Num() - Matches.Num()` |
| TM (Type Mismatch) | 位置匹配但类型错误 | `TypeMismatches.Num()` |

**召回率/精确率**：

```cpp
// Recall: TypeMismatches 计入漏检（位置匹配但类型错误=未正确识别）
// 分母 = 所有 Truth 元素数量 = TP + FN + TM
float Recall = (float)TP / (TP + FN + TM);      // 目标 ≥88%
// Precision: TypeMismatches 不计入多检（位置是正确的）
float Precision = (float)TP / (TP + FP);        // 目标 ≥88%
float F1 = 2 * Recall * Precision / (Recall + Precision);
```

**综合评估指标**：

```cpp
struct FEvalMetrics
{
    // 位置误差计算口径：
    // - TP + TM（位置匹配成功的元素）：使用实际位置误差
    // - FN（完全漏检的元素）：使用25px惩罚误差
    // 注意：TypeMismatch虽然类型错误，但位置是对的，所以用实际误差而非惩罚
    float PosMAE;               // 位置平均误差
    float PosP95;               // 位置95分位误差
    
    // 召回/精确（防止"删元素→MAE变好"）
    int32 TruePositives;        // 正确匹配数（位置+类型都对）
    int32 FalseNegatives;       // 漏检数（完全没匹配到）
    int32 FalsePositives;       // 多检数
    int32 TypeMismatches;       // 类型错误数（位置对但类型错）
    float Recall;               // 召回率 = TP / (TP + FN + TM)
    float Precision;            // 精确率 = TP / (TP + FP)
    
    // 综合得分（MAE和召回率联合）
    float CompositeScore;       // = (1 - MAE/25) * Recall * Precision
};

// 综合得分计算：MAE和召回率必须同时好
float ComputeCompositeScore(const FEvalMetrics& M)
{
    // MAE归一化：0px→1.0, 25px→0.0
    float MAEScore = FMath::Max(0.0f, 1.0f - M.PosMAE / 25.0f);
    
    // 综合得分 = MAE得分 × 召回率 × 精确率
    // 任一指标差都会拉低总分，防止"删元素刷MAE"
    return MAEScore * M.Recall * M.Precision;
}
```

**验收标准（联合）**：

| 指标 | 目标 | 说明 |
|------|------|------|
| PosMAE | ≤4px | 位置精度 |
| PosP95 | ≤8px | 异常值容忍度 |
| PassRate | ≥95% | 样例级通过率 |
| Recall | ≥88% | 不能漏检 |
| Precision | ≥88% | 不能多检 |
| CompositeScore | ≥0.8 | 综合得分 |

> **关键**：单独看MAE可能被"删元素"刷高，必须同时检查Recall。
> CompositeScore 作为门禁指标，任一维度差都会导致不通过。
```

### 11.4 测试数据来源

| 来源 | 自动化程度 | 说明 |
|------|------------|------|
| **项目现有Widget** | ★★★★★ | 扫描 `/Game/UI/` 目录，零人工 |
| **引擎示例项目** | ★★★★☆ | 使用Lyra/ActionRPG等官方示例 |
| **合成生成** | ★★★★★ | 程序生成随机布局Widget |

### 11.5 合成测试生成器（可选）

```cpp
// 程序生成测试用例，覆盖边缘场景
UWidgetBlueprint* GenerateSyntheticWidget(ESyntheticType Type)
{
    switch (Type)
    {
        case ESyntheticType::SimpleHUD:
            // 3-5个元素，单层
            return GenerateSimpleHUD();
            
        case ESyntheticType::NestedLayout:
            // 3层嵌套，测试层级解析
            return GenerateNestedLayout();
            
        case ESyntheticType::DenseGrid:
            // 20+元素网格，测试等距修正
            return GenerateDenseGrid();
            
        case ESyntheticType::EdgeCase:
            // 贴边、拉伸、极端尺寸
            return GenerateEdgeCases();
    }
}
```

### 11.6 命令示例

```bash
# 全自动评测：扫描项目UI目录
ImageToUMG.AutoEval /Game/UI

# 指定配置对比
ImageToUMG.AutoEval /Game/UI --config=v2_full --baseline=v1_baseline

# ViewportMapping ?????
ImageToUMG.AutoEval /Game/UI --config=v2_full --viewport=2560x1080

# 使用合成测试（无需项目有现成Widget）
ImageToUMG.AutoEval --synthetic --count=50

# 完整A/B矩阵测试（每个优化项开/关）
ImageToUMG.AutoEval /Game/UI --ab-matrix --output=ab_report.json
```

### 11.7 自动化报告示例

```
========== 自动评测报告 ==========
扫描路径: /Game/UI
发现Widget: 42个
有效测试: 38个 (4个跳过：无可提取元素)

总体指标:
┌────────────┬────────┬────────┬──────────┬──────────┐
│ 配置        │ MAE    │ P95    │ 漏检率   │ 类型准确率│
├────────────┼────────┼────────┼──────────┼──────────┤
│ v2_full    │ 2.3px  │ 5.1px  │ 2.1%     │ 97.8%    │
│ v1_baseline│ 14.7px │ 32.4px │ 8.3%     │ 91.2%    │
└────────────┴────────┴────────┴──────────┴──────────┘

总提升: MAE ↓84%, 漏检率 ↓75%

详细报告: Saved/ImageToUMG/Reports/AutoEval_20260107_153000.json
```

---

## 十二、指标汇总与权重

### 12.1 核心指标汇总表

| 优化项 | 核心指标 | 基线预期 | 目标 | 权重 |
|--------|----------|----------|------|------|
| LayoutCalculator + Hysteresis | 锚点跳变率 | ~30% | <5% | 中 |
| ViewportMapping | 映射往返误差 | N/A | 0px | 低 |
| Fixer规则修复器 | 修正有效率 | N/A | >95% | 高 |
| Prompt简化 + Bounds格式 | 布局MAE | ~15px | ≤4px | 高 |
| JSON Schema Strict | 首次解析成功率 | ~85% | >99% | 中 |
| _meta字段支持 | 问题定位时间 | 基线 | -50% | 低 |

### 12.2 权重说明

- **高权重**：直接影响最终布局精度，是Phase 1核心目标
- **中权重**：影响稳定性和开发效率
- **低权重**：辅助功能，提升调试体验

---

## 十三、附录：验收标准

| 阶段 | PosMAE目标 | PosP95目标 | 说明 |
|------|------------|------------|------|
| Phase 1 完成 | ≤4px | ≤8px | 生产级精度 |
| 无Parser降级 | ≤20px | ≤40px | 原型级，需手动微调 |

> **指标口径**：PosMAE = 位置误差平均值 = mean((|ΔX|+|ΔY|)/2)，与TechnicalDesign对齐

---

## 十四、附录：持续集成建议

### 14.1 CI/CD集成

```yaml
# 示例：GitHub Actions / Jenkins Pipeline
evaluation:
  triggers:
    - on_commit: 检查点快照（快速验证）
    - on_pr_merge: A/B对比（深度分析）
    - on_release: 消融实验 + 统计检验（完整报告）
  
  # 门禁阈值（基于样例级分位数，抗噪更好）
  thresholds:
    # 全局均值回退
    mae_mean_regression: 0.5px
    p95_mean_regression: 1.0px
    
    # 样例级中位数回退（更稳健）
    mae_median_regression: 0.3px
    
    # 超阈值样例数（定位问题）
    max_regressed_samples: 3        # 最多允许3个样例回退
    sample_regression_threshold: 2px # 单样例MAE回退超过2px视为回退
    
    # 召回率门禁（防止"删元素刷MAE"）
    min_recall: 0.88                # 召回率不得低于88%
    
  artifacts:
    - Saved/ImageToUMG/Reports/*.json
    - Saved/ImageToUMG/Reports/*.md
```

### 14.2 指标回退检测（增强版）

```cpp
struct FRegressionCheckResult
{
    bool bPassed;
    FString FailReason;
    
    // 全局指标
    float MAE_Mean_Diff;
    float MAE_Median_Diff;
    float P95_Diff;
    float Recall_Diff;
    
    // 样例级回退详情
    int32 RegressedSampleCount;
    TArray<FString> RegressedSampleNames;
    TArray<float> RegressedSampleDeltas;
};

// 增强版回退检测：基于样例级分位数 + 超阈值样例数
FRegressionCheckResult CheckForRegressionEnhanced(
    const FOptimizationReport& Current, 
    const FOptimizationReport& Baseline,
    const FRegressionThresholds& Thresholds)
{
    FRegressionCheckResult Result;
    Result.bPassed = true;
    
    // === 1. 全局均值检查 ===
    Result.MAE_Mean_Diff = Current.GetFinalPosMAE() - Baseline.GetFinalPosMAE();
    if (Result.MAE_Mean_Diff > Thresholds.MAE_Mean_Regression)
    {
        Result.bPassed = false;
        Result.FailReason += FString::Printf(
            TEXT("MAE均值回退: %.2f→%.2f (+%.2fpx > %.2fpx)\n"),
            Baseline.GetFinalPosMAE(), Current.GetFinalPosMAE(),
            Result.MAE_Mean_Diff, Thresholds.MAE_Mean_Regression);
    }
    
    // === 2. 样例级中位数检查（更稳健，抗异常值） ===
    TArray<float> BaselineMAEs, CurrentMAEs;
    for (const auto& S : Baseline.GetFinalStage().SampleErrors)
    {
        BaselineMAEs.Add(S.PosMAE);
    }
    for (const auto& S : Current.GetFinalStage().SampleErrors)
    {
        CurrentMAEs.Add(S.PosMAE);
    }
    
    float BaselineMedian = GetMedian(BaselineMAEs);
    float CurrentMedian = GetMedian(CurrentMAEs);
    Result.MAE_Median_Diff = CurrentMedian - BaselineMedian;
    
    if (Result.MAE_Median_Diff > Thresholds.MAE_Median_Regression)
    {
        Result.bPassed = false;
        Result.FailReason += FString::Printf(
            TEXT("MAE中位数回退: %.2f→%.2f (+%.2fpx > %.2fpx)\n"),
            BaselineMedian, CurrentMedian,
            Result.MAE_Median_Diff, Thresholds.MAE_Median_Regression);
    }
    
    // === 3. 超阈值样例数检查（便于定位问题） ===
    Result.RegressedSampleCount = 0;
    
    // 按样例名配对比较
    TMap<FString, float> BaselineMap;
    for (const auto& S : Baseline.GetFinalStage().SampleErrors)
    {
        BaselineMap.Add(S.SampleName, S.PosMAE);
    }
    
    for (const auto& S : Current.GetFinalStage().SampleErrors)
    {
        if (float* BaseMAE = BaselineMap.Find(S.SampleName))
        {
            float Delta = S.PosMAE - *BaseMAE;
            if (Delta > Thresholds.SampleRegressionThreshold)
            {
                Result.RegressedSampleCount++;
                Result.RegressedSampleNames.Add(S.SampleName);
                Result.RegressedSampleDeltas.Add(Delta);
            }
        }
    }
    
    if (Result.RegressedSampleCount > Thresholds.MaxRegressedSamples)
    {
        Result.bPassed = false;
        Result.FailReason += FString::Printf(
            TEXT("回退样例数过多: %d个 > %d个\n"),
            Result.RegressedSampleCount, Thresholds.MaxRegressedSamples);
        
        // 列出回退最严重的样例（便于定位）
        Result.FailReason += TEXT("回退样例:\n");
        for (int32 i = 0; i < FMath::Min(5, Result.RegressedSampleNames.Num()); i++)
        {
            Result.FailReason += FString::Printf(TEXT("  - %s: +%.2fpx\n"),
                *Result.RegressedSampleNames[i], Result.RegressedSampleDeltas[i]);
        }
    }
    
    // === 4. 召回率检查（防止"删元素刷MAE"） ===
    float BaselineRecall = Baseline.GetFinalStage().AverageRecall;
    float CurrentRecall = Current.GetFinalStage().AverageRecall;
    Result.Recall_Diff = CurrentRecall - BaselineRecall;
    
    if (CurrentRecall < Thresholds.MinRecall)
    {
        Result.bPassed = false;
        Result.FailReason += FString::Printf(
            TEXT("召回率过低: %.1f%% < %.1f%%\n"),
            CurrentRecall * 100, Thresholds.MinRecall * 100);
    }
    
    // === 5. 输出日志 ===
    if (!Result.bPassed)
    {
        UE_LOG(LogImageToUMG, Error, TEXT("Regression detected!\n%s"), *Result.FailReason);
    }
    else
    {
        UE_LOG(LogImageToUMG, Log, 
            TEXT("Regression check passed. MAE: %.2f→%.2f, Median: %.2f→%.2f, Regressed: %d"),
            Baseline.GetFinalPosMAE(), Current.GetFinalPosMAE(),
            BaselineMedian, CurrentMedian, Result.RegressedSampleCount);
    }
    
    return Result;
}

// 辅助函数：计算中位数
float GetMedian(TArray<float>& Values)
{
    if (Values.Num() == 0) return 0.0f;
    Values.Sort();
    int32 Mid = Values.Num() / 2;
    if (Values.Num() % 2 == 0)
    {
        return (Values[Mid - 1] + Values[Mid]) / 2.0f;
    }
    return Values[Mid];
}
```

### 14.3 门禁阈值配置

```cpp
struct FRegressionThresholds
{
    // 全局指标阈值
    float MAE_Mean_Regression = 0.5f;    // 均值回退阈值
    float MAE_Median_Regression = 0.3f;  // 中位数回退阈值（更稳健）
    float P95_Regression = 1.0f;         // P95回退阈值
    
    // 样例级阈值
    int32 MaxRegressedSamples = 3;       // 最多允许回退的样例数
    float SampleRegressionThreshold = 2.0f; // 单样例回退阈值
    
    // 召回率阈值
    float MinRecall = 0.88f;             // 最低召回率
    float MinPrecision = 0.88f;          // 最低精确率
    float MinCompositeScore = 0.8f;      // 最低综合得分

    // 根据测试集规模调整
    static FRegressionThresholds ForTestSetSize(int32 SampleCount)
    {
        FRegressionThresholds T;
        
        if (SampleCount < 50)
        {
            // 小测试集：放宽阈值，避免噪声
            T.MAE_Mean_Regression = 1.0f;
            T.MaxRegressedSamples = 2;
        }
        else if (SampleCount >= 100)
        {
            // 大测试集：收紧阈值，更精确
            T.MAE_Mean_Regression = 0.3f;
            T.MAE_Median_Regression = 0.2f;
            T.MaxRegressedSamples = 5;
        }
        
        return T;
    }
};
```

### 14.4 测试集版本管理

- 测试集随代码版本控制，确保可复现
- 定期更新测试集覆盖新场景（建议每月）
- 保留历史测试集用于回归对比
