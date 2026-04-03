# Incremental Code Simplification Batches

Last updated: 2026-04-04

## Purpose

本文件是 `incremental-code-simplification` capability 的批次登记台账。
它只记录已经获准进入正式收口流程的 cleanup batch，并为每一批固定：

- 允许修改的模块边界
- 禁止顺手扩张的非目标范围
- 必须保留的回归护栏
- 对应的全量 admissible 回归证据
- `legacy / compatibility` 链删除需要显式决策的红线

本台账里的“不能退步”只针对当前正式 gate 使用的 45 个 admissible case。
`Saved/Design2UMG/Automation/_removed_nonreproducible_cases_20260331.json` 中已记录为 historical non-reproducible 的 case 不重新作为 routine cleanup 的实现目标。

## Global Batch Rules

1. 每个 cleanup batch 只能围绕一个单一主题收口，不做 repo-wide rewrite。
2. Batch proposal 必须先列出允许修改的模块；超出模块边界的改动要拆成下一批。
3. Batch 不得以 cleanup 名义新增 fallback、兼容读取、双写双读或静默迁移。
4. 只要触碰已有 `legacy / compatibility / fallback` 链的删除或合并，就必须先经过显式决策，不能混入常规 cleanup。
5. 任何 source-specific 行为清理都必须继续留在对应 parser / exporter，不得搬进 shared path。
6. Batch 验收必须同时满足：
   - 受影响主链的 focused guard 测试没有退步
   - formal admissible full run 对全部 45 个 case 保持 `no_regression_failed = 0`
   - run root 通过 `python Tools/design2umg_validate_run.py --output-root <run_root>` 的完整性检查

## Batch Register

### ICS-20260330-UMGJSON-TYPED-FIRST

- Status: implemented and admitted
- Theme: UMG JSON typed-first cleanup
- Origin: 用户已批准的“兼容链清理”专项收缩，不属于扩功能批次

#### Allowed Modules

- `Source/Design2UMGEditor/Private/UMGJsonParser.cpp`
- `Source/Design2UMGEditor/Private/UMGJsonSerializer.cpp`
- `Source/Design2UMGCore/Public/UMGWidgetNodeAccess.h`
- `Source/Design2UMGEditor/Private/UMGPropertyApplier.cpp`
- `Source/Design2UMGEditor/Private/UMGWidgetUtils.cpp`
- `Source/Design2UMGEditor/Private/UMGWidgetUtils.h`
- `Docs/PropertySystemMigrationPlan.md`
- `Docs/Design2UMG_TechnicalDesign.md`
- `Docs/ReleaseBlocker_FinalScope_20260329.md`
- 本台账文件

#### Shipped Cleanup

- 删除 `UMGJsonParser` 中只服务旧 UMG JSON 的旁路入口：
  - `CanvasSlot`
  - `Slots`
  - 节点平铺 property 读取
- 删除 parser 内大段 `ApplyLegacy*Fallbacks` helper，避免 typed state 和 legacy string bag 双重回填。
- 统一 serializer / generator / runtime applier 的消费口径：
  - typed 主真值通过 `BuildEffectivePropertyBag` 进入运行时
  - 仅剩余未 typed 的 legacy 字段通过 `BuildLegacyFallbackPropertyBag` 保留
- 保留 typed 空字符串 override 的正式语义，禁止空字符串再次回退到同名 legacy key。

#### Explicit Non-Goals

- 不删除整个 `Properties` legacy bag。
- 不把 widget-specific 长尾字段强行一次性迁移到 typed schema。
- 不为保持 inadmissible historical behavior 追加新的 fallback 或兼容壳。
- 不把 source-specific parser 规则挪进 shared path。

#### Required Guard Set

- `Design2UMG.PropertyBag.EmptyStringApplies`
- `Design2UMG.UMGJson.TypedBrushImageGradientRoundTrip`
- `Design2UMG.UMGJson.InnerShadowEffectRoundTrip`
- `Design2UMG.VisualMapper.WrapWithBorderDirect`
- `Design2UMG.VisualMapper.WrapWithBorderTyped`
- `Design2UMG.Converter.FigmaPrototypeNavigation`
- `Design2UMG.WidgetBlueprint.FigmaPrototypeNavigationRuntime`

#### Formal Regression Evidence

- Formal admissible full run:
  `Saved/Design2UMG/Automation/_final_admissible_full_20260403_final`
- Canonical wrapper:
  `Docs/ValidationGovernance/AdmissibleBaselineManifest_20260404.json`
- Completeness check:
  `python Tools/design2umg_validate_run.py --output-root Saved/Design2UMG/Automation/_final_admissible_full_20260403_final`
- Acceptance condition recorded in the manifest:
  - `no_regression_failed = 0`
  - formal admissible gate remains 45 cases

#### Acceptance Notes

- 这批 cleanup 只在“formal admissible 45-case gate 零回退”成立时才算 admitted。
- 当前 formal run 已覆盖本批之后的工作区状态，因此这批 cleanup 不再需要额外追逐已移除的 non-reproducible historical cases。

## Deferred Batches

以下事项仍属于后续批次候选，但本轮不纳入 routine cleanup：

- 删除整个 `legacy Properties` bag
- 把 Button / SizeBox / ScrollBox / ProgressBar 等 widget-specific 长尾字段全面 typed 化
- 任何会改变 parser/shared-path 边界的清理
- 任何需要重新定义 `legacy / compatibility` 保留策略的删除动作
