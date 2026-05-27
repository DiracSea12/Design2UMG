# Phase 31.4 路径贴图刷新退分排查与修复报告

## 结论

Phase 31.4 的退分不是解析结果、布局结果或文本结构变化导致的，而是自动化 asset reload 渲染前的路径贴图刷新方式过宽。

最初为了修复部分路径贴图 case 在 asset reload 后没有拿到最新纹理资源的问题，代码在 asset render 前对 `D2U_PathTextures` 目录做了 package reload。这个做法修好了原始漂移点，但也让本来稳定的 case 在 reload 后从磁盘重新加载路径贴图，导致 asset render 和 bootstrap render 不一致，最终出现视觉退分和 pipeline determinism blocker。

最终修复方式是：asset reload 前仍然刷新 `D2U_PathTextures` 和 `D2U_ProceduralTextures` 下的纹理资源，但不再对这些纹理 package 做强制 `ReloadPackage`。也就是只执行纹理对象级别的：

- `ConditionalPostLoad()`
- `FinishCachePlatformData()`
- `UpdateResource()`

这样保留“让路径贴图资源在 asset reload render 前可用且更新”的效果，同时避免从磁盘重载改变刚生成的纹理状态。

## 失败现象

### 第一次 full 47 验证失败

Run root：

`G:/UE5/FPSRoguelike/Saved/Design2UMG/Automation/_formal_full_47_phase31_4_unified_path_texture_mainqa_20260524_1705`

关键结果：

- `total = 47`
- `editor_exit_code = 0`
- `reported_total = 47`
- `missing_compare_reports = []`
- `no_regression = 46/47`
- `validation.blocked_reasons = ["pipeline_determinism_rule_incompatible", "historical_best_missing_case"]`

其中 `historical_best_missing_case=47` 是本地 admissible-best manifest 当时缺失 case 的独立问题；真正阻塞本轮修复的是：

1. `pen_pencil-welcome_welcome`
   - `no_regression` 失败
   - `stable_changed_ratio_delta = +0.0010717913725731137`
   - `generation_vs_asset_changed_ratio = 0.008190372965845289`
   - `generation_vs_asset.diff_bbox = [62, 85, 635, 313]`

2. `figma_figma-export_source`
   - `no_regression` 仍通过
   - 但 `pipeline_determinism_rule_incompatible`
   - `generation_vs_asset_changed_ratio = 7.866753472222223e-05`
   - `generation_vs_asset.diff_bbox = [1058, 167, 1082, 174]`

## 排查过程

### 1. 确认不是 UnrealEditor 崩溃或验证缺件

失败 run 中：

- UnrealEditor `editor_exit_code = 0`
- 47 个 case 全部生成 `compare_report.json`
- `summary.json` 和 `validation_against_historical_best.json` 均生成
- `completeness.blocked = false`
- `missing_compare_reports = []`

因此 exit code 2 是验证规则阻塞，不是编辑器崩溃，也不是报告缺失。

### 2. 对比 bootstrap、asset render 和历史基线

对失败 case 做图像关系对比后得到：

#### `pen_pencil-welcome_welcome`

- 当前 `candidate.bootstrap.png` vs 历史基线 `candidate.png`：完全一致
  - `changed_ratio = 0.0`
  - `mae = 0.0`
- 当前 `candidate.png` vs 当前 `candidate.bootstrap.png`：不一致
  - `changed_ratio = 0.008190372965845289`
  - `bbox = [62, 85, 635, 313]`
- 当前 `candidate.png` vs 历史基线 `candidate.png`：同样不一致

#### `figma_figma-export_source`

- 当前 `candidate.bootstrap.png` vs 历史基线 `candidate.png`：完全一致
  - `changed_ratio = 0.0`
  - `mae = 0.0`
- 当前 `candidate.png` vs 当前 `candidate.bootstrap.png`：不一致
  - `changed_ratio = 7.866753472222223e-05`
  - `bbox = [1058, 167, 1082, 174]`

这个证据说明：生成端没有退化，退化发生在显式 asset reload render 阶段。

### 3. 确认结构没有变化

失败 case 的 `generation_vs_asset_structure` 均显示：

- `texts_match = true`
- `kind_histograms_match = true`
- `class_histograms_match = true`
- text/node 数量一致

因此问题不是 widget tree 结构或文本内容变化，而是渲染资源状态差异。

### 4. 定位到 `D2U_PathTextures` package reload 过宽

失败前的实现会在 `PrepareGeneratedAssetForAutomationRender(...)` 中：

1. 重置自动化瞬态状态；
2. 刷新自动化导入字体 package；
3. 对生成 asset 所在目录下的 `D2U_PathTextures` 做 package reload；
4. 再 reload Widget Blueprint package；
5. 渲染 asset。

这个做法对原始路径贴图漂移 case 有帮助，但对 `welcome_welcome` / `figma_source` 这类已经稳定的 case，会让 asset render 阶段使用“从磁盘重新加载后的路径贴图状态”，从而和刚生成后用于 bootstrap render 的纹理资源状态不一致。

## 修复方法

修改文件：

`Source/Design2UMGEditor/Private/Automation/Design2UMGAutomationRender.cpp`

新增本地 helper：

`RefreshAutomationTextureResourcesInDirectory(...)`

它只做纹理资源级刷新：

- 找到目标 package；
- 如果 package 未加载，则正常 `LoadPackage`；
- 找到或加载 `UTexture2D` 对象；
- 对纹理执行 `ConditionalPostLoad()`、`FinishCachePlatformData()`、`UpdateResource()`；
- 最后 `FlushRenderingCommands()` 和 `CollectGarbage(...)`。

同时将：

`RefreshGeneratedTexturePackagesForAutomationRender(...)`

从“刷新并 reload 整个 texture package”改为“只刷新 path/procedural texture 纹理资源”。

也就是说，`D2U_PathTextures` 和 `D2U_ProceduralTextures` 仍参与 asset reload 前准备，但不再用 `ResetLoaders + ReloadPackage` 改变纹理 package 的加载状态。

## 为什么这个修法更窄

没有采用以下方案：

- 没有按 case 名、节点 id、文件名、字体名或设计稿路径写特判；
- 没有修改参考图、基准图或 compare 阈值；
- 没有新增配置项；
- 没有新增兼容路径；
- 没有把问题包装成新的 report/cache/state machine；
- 没有对 `D2U_PathTextures` 或 `D2U_ProceduralTextures` 做 package reload；只做纹理资源级刷新。

修复只收窄自动化 asset render 前的资源准备方式，仍保留现有 `PrepareGeneratedAssetForAutomationRender(...)` 调用链。

## 验证结果

### Focused 6-case 验证

Run root：

`G:/UE5/FPSRoguelike/Saved/Design2UMG/Automation/_focused_phase31_4_texture_resource_refresh_6case_20260524_1725`

覆盖 case：

- `pen_pencil-demo_dashboard-swiss-clean`
- `pen_pencil-demo_habit-tracker-terminal-minimal`
- `pen_pencil-demo_habit-tracker-terminal-swiss`
- `pen_pencil-welcome_step-2-frame`
- `pen_pencil-welcome_welcome`
- `figma_figma-export_source`

结果：

- `total = 6`
- `editor_exit_code = 0`
- `reported_total = 6`
- `missing_compare_reports = []`
- `no_regression = 6/6`
- `generation_vs_asset_changed_ratio` 全部为 `0.0`
- `pipeline_determinism_rule_incompatible_cases = []`

focused run 仍显示 `historical_best_missing_case`，这是 focused scope 下本地 admissible-best manifest 缺失对应 case 的既有问题，不影响本次针对 full gate 的结论。

### 最终 full 47 formal 验证

Run root：

`G:/UE5/FPSRoguelike/Saved/Design2UMG/Automation/_formal_full_47_phase31_4_texture_resource_refresh_mainqa_20260524_1730`

结果：

- `total = 47`
- `editor_exit_code = 0`
- `reported_total = 47`
- `missing_compare_reports = []`
- `missing_case_results = []`
- `no_regression = 47/47`
- `generation_vs_asset_changed_ratio` 非零 case：无
- `pipeline_determinism_rule_incompatible_cases = []`
- `validation_against_historical_best.blocked = false`
- `historical_best_missing_cases = []`

本次 formal full 47 使用真实项目：

`G:/UE5/FPSRoguelike/FPSRoguelike.uproject`

未使用临时 HostProject。

## 风险与后续注意

1. `RefreshAutomationPackagesInDirectory(...)` 仍用于自动化导入字体 package；本轮没有扩大到字体刷新路径。
2. `D2U_PathTextures` 和 `D2U_ProceduralTextures` 现在只做纹理资源刷新，不做 package reload；这已由 focused 6-case 和 full 47 验证覆盖。
3. 如果未来再次调整路径贴图保存、压缩设置、纹理平台数据或 package reload 顺序，必须重新跑 full 47，因为这类问题只看单案或平均分很容易漏掉。
4. 判定成绩必须继续以 full 47 的 `no_regression` 和 `validation_against_historical_best` 为准，不能用 focused run 或平均指标代替。
