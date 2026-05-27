# Phase 31.4 全量范围收口说明

## 这轮到底在收什么

Phase 31.4 的主线不是“继续做字体功能”，也不是“新增导入平台”，而是把 Design2UMG 的字体和自动化验证链路从隐式、外部依赖、会污染状态的做法，收口到显式、项目内、可验证的规则。

这轮包含三类改动：

1. 删除旧的自动字体导入链路；
2. 用显式字体映射和默认字体资产取代自动导入；
3. 强化诊断和自动化验证，确保删除旧链路后 full 47 不退分、不产生 pipeline determinism 漂移。

最后追加的 `D2U_PathTextures` / `D2U_ProceduralTextures` 资源刷新修复，是 full 47 验证过程中发现的 automation asset reload 退分点；它不是新功能，而是让本轮收口后的验证链路稳定通过。

## 为什么删除 FontImport

旧链路的问题是：

- 自动导入字体会引入外部字体文件/服务/已知 web font 列表；
- automation run 中会出现跨 case 的瞬态状态和缓存污染；
- 导入结果会写入项目设置或 transient mapping，导致同一个设计在不同运行顺序下可能走到不同字体资产；
- 对开源个人项目来说，维护外部字体导入服务、web font catalog 和自动下载路径成本过高；
- full-run 需要的是确定性和可解释性，不是“猜一个字体并自动导入”。

因此本轮删除：

- `Source/Design2UMGEditor/Private/FontImport/DesignFontImportService.*`
- `Source/Design2UMGEditor/Private/FontImport/DesignFontImportCatalog.*`
- `Source/Design2UMGEditor/Private/FontImport/DesignFontImportAutomationPaths.*`
- `Source/Design2UMGEditor/Private/FontImport/DesignFontImportServiceInternal.h`
- `Source/Design2UMGEditor/Private/FontImport/DesignFontImportServiceKnownWebFonts.cpp`

这不是为了换一个更复杂的新系统，而是直接砍掉旧自动导入链路。

## 用什么替代旧链路

替代方式是显式项目配置：

- `ExplicitFontMappings`：用户明确把设计字体名或 PostScript 名映射到项目内 `/Game` 字体资产；
- `DefaultFontAsset`：找不到明确字体时，在诊断报告里记录项目指定的 fallback 候选；
- `FontPostScriptName`：parser 保留源侧 PostScript 信息，让映射更精确；
- `FFontResolver`：集中执行“显式映射 / 诊断 fallback 候选 / 引擎字体 fallback 候选 / 拒绝无效资产”的规则。

这不是自动导入，也不下载字体。它只在项目已有资产中做确定性选择。

## 为什么需要 public/config/schema 增长

这部分增长是本轮删除旧链路的必要替代面，而不是额外功能：

- 删除自动导入后，用户必须有地方声明字体映射，所以需要 `ExplicitFontMappings`；
- 删除外部服务后，需要在诊断里明确缺字体时会报告哪个项目级 fallback 候选，所以需要 `DefaultFontAsset`；
- 如果只保留 family name，部分设计源无法稳定匹配精确字体，所以 schema 需要 `FontPostScriptName`；
- 诊断、规划、测试和构建路径都需要复用同一套 resolver 判断，因此 `FFontResolver` 暴露了必要查询函数。

预算判断：这些 public/config/schema 增长替代的是更大的自动导入服务和 web font catalog，而不是在旧系统上叠新系统。

## 为什么需要诊断报告增长

删除自动导入后，用户需要知道为什么某个字体没有被精确匹配。否则行为会从“自动导入失败”变成“看起来无声 fallback”。

因此 `DesignImportDiagnosticReport` 增加：

- schema version；
- report path；
- font resolution 结果；
- exact/default/rejected/ambiguous/unsupported 等状态；
- issue code。

这个报告不是为了刷分，也不是新 reporting 平台。它的作用是把字体解析从隐式行为变成可解释结果。

## 为什么最后还改了 automation render

full 47 验证过程中发现：

- 删除字体自动导入后，部分 path texture case 在 asset reload render 阶段仍会因为资源状态不同产生 `generation_vs_asset` 漂移；
- 初版修复用 `ReloadPackage` 刷新 `D2U_PathTextures`，修好了原始漂移点，但把 `welcome_welcome` 和 `figma_figma-export_source` 拉出 determinism；
- 最终修复改为只刷新 `UTexture2D` 资源本体，不 reload path texture package。

这属于验证链路稳定性修复，服务于 full 47 “不退分、不漂移”的封板标准。

## 本轮没有做什么

本轮刻意没有做：

- 没有新增字体下载器；
- 没有新增 web font provider；
- 没有保留旧 JSON/旧配置兼容读取；
- 没有新增迁移器；
- 没有按具体 case、节点 id、文件名、字体名写分支；
- 没有修改参考图、基准图或 compare 阈值；
- 没有用 HostProject 验证；
- 没有对 `D2U_PathTextures` 或 `D2U_ProceduralTextures` 做 package reload；只做纹理资源级刷新。

## 复杂度账本

### 删除

- 删除 8 个 FontImport 相关生产/自动化文件；
- 删除自动字体导入服务；
- 删除 known web font catalog；
- 删除自动导入相关设置；
- 删除跨运行的外部 resolver / transient service 状态。

### 新增或扩大

- 新增显式字体映射配置；
- 新增默认字体资产配置；
- schema 增加 PostScript 字段；
- `FFontResolver` 扩大为字体解析唯一 owner；
- diagnostic report 增加 font resolution section；
- automation render 增加 path/procedural texture resource refresh helper；
- 增加对应测试和文档。

### 净判断

代码行数净减少，旧自动导入系统被删除；新增长点是删除旧系统后的必要显式 contract 和诊断面。

这轮的复杂度风险不在“新增文件太多”，而在 public/config/report 面变宽。判断它是否可接受，关键看它们是否直接替代旧自动导入链路，并且 full 47 是否证明不退分。

## 验证证据

### Focused 6-case

Run root：

`G:/UE5/FPSRoguelike/Saved/Design2UMG/Automation/_focused_phase31_4_texture_resource_refresh_6case_20260524_1725`

结果：

- `total = 6`
- `editor_exit_code = 0`
- `no_regression = 6/6`
- `generation_vs_asset_changed_ratio = 0` for all focused cases
- `pipeline_determinism_rule_incompatible_cases = []`

focused run 的 historical-best validation 因 scope 缺失 manifest case 显示 blocked，只作为定位补充证据，不作为封板证据。

### Final full 47

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
- `historical_best_missing_cases = []`
- 当前四门使用的 2026-05-25 run 仍有 1 个 `figma_figma-export_local` historical-best 微回退；用户已明确同意本轮四门先视为噪声继续，但它仍是 residual risk。

验证使用真实项目：

`G:/UE5/FPSRoguelike/FPSRoguelike.uproject`

未使用临时 HostProject。

## 放行条件

这轮只有在以下条件同时成立时才可认为 31.4 scope 可继续进入 architecture/code-quality：

1. reviewer 接受“删除 FontImport + 显式字体映射 + 字体诊断报告 + texture refresh 稳定性修复”属于同一个 31.4 收口范围；
2. reviewer 接受 public/config/schema 增长是替代旧自动导入服务的必要 contract；
3. reviewer 接受 diagnostic report 增长是删除自动导入后的必要可解释性补偿；
4. final full 47 证据保持无退分、无 historical-best blocker、无 pipeline determinism blocker。

如果 reviewer 不接受以上范围绑定，则 complexity 应保持 REVIEW，不能进入 architecture/code-quality。
