# Design2UMG 历史最佳排查与可复现基线方法

Last verified: 2026-04-04

## 目的

这份文档定义一种可重复的方法，用来处理下面这类问题：

- 用户要求“当前成绩要达到历史最佳”
- 仓库里存在多轮历史回归目录，但口径并不一致
- 不能接受特判、样例补丁、刷分式修改
- 红线是“不能让任何 admissible case 发生一丝回退”

本方法的目标不是盲目追逐历史里出现过的单点最低分，而是先筛出**可比、可复现、不会误导后续工作的历史最佳**，再只对这些目标做修复。

说明：
- 对外文档应使用逻辑占位符（如 `<UE5_Root>`、`<PluginRoot>`、`<MaintainerRuntimeProject.uproject>`），而不是维护者机器绝对路径。
- 当前正式 gate 的逻辑角色是 `maintainer-owned canonical runtime boundary`。

## 核心定义

### 1. 原始历史最佳

指某个 case 在所有历史 `compare_report.json` 里出现过的最小分数。

这个值**不能直接拿来当目标**，因为它可能来自：

- 无效参考图
- 口径变化前的旧 run
- 会让其他 case 退步的 probe run
- 今天已经无法复现的老资产/老环境偶发结果

### 2. Admissible Best

指满足下面条件后，仍然成立的历史最佳：

- 来自明确的**全量正式基线 run**，而不是 `focus` / `guard` / `probe` / 其他只覆盖子集 case 的诊断 run
- 该 run 的 `summary.json` 里 `no_regression.failed == 0`
- case 自身 `compare_report.json` 不是 `invalid_reference`
- 指标口径与当前可比
- 更优历史结果对应的旧资产今天仍存在，且该旧资产在今天的真实项目边界下仍可复现

如果旧资产已经不存在、不可获取、或还没有完成旧资产复现检查，该结果**不能直接称为 `Admissible Best`**，只能保留为 `provisional candidate`。

以后所有“历史最佳”要求，默认都应落到 `Admissible Best`，而不是原始历史最佳。

### 2.5 Provisional Candidate

指已经通过口径与 `no_regression` 过滤，但还没有通过旧资产复现检查的候选历史最好值。

这类结果可以作为排查线索，但**不能直接进入正式 admissible baseline**，也不能直接当作“必须追到的 blocker 目标”。

### 3. 不可复现历史优势

指某个 case 的历史更优结果来自过去某次 run，但今天：

- 用同一真实项目
- 用同一 reference
- 直接重渲染当时生成的旧 `.uasset`

也无法再得到那次更优分数。

这类结果应从派生基线中移除，并单独记录原因，避免继续误导后续工作。

### 4. `NO_REGRESSION_EPSILON`

本文提到的 `NO_REGRESSION_EPSILON` 指当前批量脚本里的判定 epsilon。  
在本仓库当前实现下，它等于：

```text
1.25e-4
```

如果后续脚本改了这个值，文档中的“明显更优 / 明显没复现”判断也必须同步更新。

### 5. 正式比较指标顺序

当文档提到“历史最佳 / 更优分数 / 当前相对 admissible best 退步”时，默认不是指“任意一个更小的数字”，而是指当前正式 gate 使用的固定指标顺序：

1. `stable_changed_ratio`
2. `changed_ratio`
3. `stable_mean_abs_error`
4. `mean_abs_error`

如果多个 run 都可比，先按这个顺序比较，遇到第一个超过 `NO_REGRESSION_EPSILON` 的更优项，才视为正式 decisive improvement。

## 红线

### 1. 绝不以单点更优掩盖其他 case 退步

只要任何 admissible case 相对 admissible baseline 变差，就不允许保留改动。

### 2. 不接受特判

不允许按：

- case 名
- 节点 id
- 文件名
- 历史 run 目录名
- 特定 reference 特征

写逻辑分支。

### 3. 不接受会污染后续工作的基线

如果某个历史最好值已经被证明：

- 参考无效
- 依赖旧口径
- 来源于会打坏其他 case 的 probe
- 今天不可复现

就必须从派生 baseline 中移除。

### 4. 只在真实项目边界验证

固定使用：

```text
G:\UE5\FPSRoguelike\FPSRoguelike.uproject
```

不允许 HostProject fallback。

## 标准流程

### 第 0 步：先构建

```text
G:\UE5\UnrealEngine\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe FPSRoguelikeEditor Win64 Development "-Project=G:\UE5\FPSRoguelike\FPSRoguelike.uproject" -WaitMutex -FromMsBuild
```

### 第 1 步：扫描全部历史结果

从：

```text
Saved/Design2UMG/Automation
```

收集所有：

- `summary.json`
- `compare_report.json`
- `ue_generate_result.json`
- `ue_render_result.json`
- 对应 case 目录下的 `reference.png`（如果要做 hash 分组）

按 case 建时间序列，至少记录：

- `stable_changed_ratio`
- `stable_mean_abs_error`
- `changed_ratio`
- `mean_abs_error`
- `verification_level`
- `reference hash`（需要在审计脚本或人工审计时对 `reference.png` 额外计算，当前 JSON 不会直接提供）
- `structure_required / structure_passed`
- run 级 `no_regression`

推荐产物名称（或等价的 maintainer 审计工件）：

- `Saved/Design2UMG/Automation/_history_audit_<date>.json`

### 第 2 步：先判定口径是否一致

同一个 case 只要出现以下任一差异，就不能直接横比：

- reference PNG hash 不同
- reference 尺寸不同
- `pixel_tolerance` 不同
- `stable_visual_filter` 不同
- `verification_level` 不同
- `structure_required` 不同

做法：

- 先按这些字段分组
- 只在同组内比较最佳值
- 发现组间差异时，必须先解释来源，再决定是否合并

### 第 3 步：过滤 inadmissible run

只保留 run 级别满足下面条件的结果：

- 该 run 是明确的**全量正式基线 run**
- `summary.no_regression.available == summary.total`
- `summary.no_regression.failed == 0`

然后再在 case 级过滤：

- 排除 `verdict == invalid_reference`
- 排除缺关键指标的 report

这里必须额外强调：

- `focus` / `guard` / `probe` / 其他通过 `--include` / `--exclude` 跑出来的子集 run，只能用于局部排查
- 这些子集 run **绝不能进入 admissible history**
- 如果批量脚本本身还不能在 `summary.json` 里区分“全量 run”和“子集 run”，维护者必须通过显式 allowlist 或单独 manifest 把正式基线 run 和诊断 run 分开管理

唯一例外是：

- **正式 admissible full run** 可以使用 `--exclude`
- 但只允许排除已经被 `_removed_nonreproducible_cases_<date>.json` 明确列出的 case
- 这种 run 仍然属于 formal admissible gate，而不是诊断子集 run

在当前仓库工具还没有把 `full/subset` provenance 自动写进 `summary.json` 之前，正式 admissible baseline 必须同时满足：

- `summary.no_regression.available == summary.total`
- `summary.no_regression.failed == 0`
- 落在维护者明确维护的 full-run allowlist / manifest 中

这一步输出的最好值，在完成旧资产复现前，只能视为 **provisional candidates**。

这一步输出建议：

- `Saved/Design2UMG/Automation/_admissible_candidates_<date>.json`
- `Saved/Design2UMG/Automation/_admissible_run_manifest_<date>.json`

推荐 `run manifest` 最小字段：

```json
{
  "runs": {
    "_final_admissible_full_20260331": {
      "scope": "full",
      "used_include": false,
      "used_exclude": false,
      "total_cases": 45,
      "admissible": true
    },
    "_codex_focus_20260331_textcal1": {
      "scope": "focus",
      "used_include": true,
      "used_exclude": false,
      "total_cases": 8,
      "admissible": false
    }
  }
}
```

### 第 4 步：把旧资产复现作为 admission 门槛，而不是争议时附加检查

这是最重要的一步。

如果某个更优历史结果对应的旧资产今天仍然存在，必须先直接重渲染那次 run 留下的旧资产：

```text
python Tools/design2umg_auto_verify.py asset --source "<old_widget_blueprint_object_path>" --reference-provider png --reference "<reference.png>" --project "G:\UE5\FPSRoguelike\FPSRoguelike.uproject" --output-dir "<probe_dir>"
```

注意：

- `asset` 模式的 `--source` 传的是 Widget Blueprint **对象路径**
- 例如：

```text
/Game/Design2UMG/Automation/figma_figma-export_local/WBP_Dashboard_193.WBP_Dashboard_193
```

- 不是磁盘上的 `.uasset` 文件路径

结论规则：

- 旧资产今天经过重复复跑后，至少 3 次里有 2 次及以上仍能以**超过 `NO_REGRESSION_EPSILON` 的优势**跑出那次更优分数：说明这是**可复现历史优势**
- 旧资产今天经过重复复跑后，3 次都不能以超过 `NO_REGRESSION_EPSILON` 的优势跑出历史更优结果：说明这是**不可复现历史优势**
- 介于两者之间的结果，或旧资产暂时不可获取：统一视为 **provisional candidate / inconclusive**

只有第一种情况，才允许把该结果从 `provisional candidate` 升级成正式 `Admissible Best`。

不可复现的 case：

- 从派生 baseline 中删除
- 单独写记录，不删原始历史 run

推荐产物：

- `Saved/Design2UMG/Automation/_removed_nonreproducible_cases_<date>.json`
- `Saved/Design2UMG/Automation/_admissible_best_<date>.json`

正式 `_admissible_best_<date>.json` 至少要持久化这些 admission 依据，不能只存最终分数：

- `run`
- `report_path`
- `reference_hash`
- `pixel_tolerance`
- `stable_visual_filter`
- `verification_level`
- `structure_required`
- `full_run_provenance`
- `repro_status`（如 `reproduced` / `non_reproducible` / `missing_old_asset`）

如果现存的 `_admissible_best_<date>.json` 缺这些 admission metadata，就只能把它当成**旧格式派生物**，不能直接当权威结果使用；需要按当前方法重新生成。

### 第 5 步：锁定真正 blocker

在 admissible baseline 上重新比较当前工作区，只保留：

- 当前相对 admissible best 仍然退步
- 且这种退步经过重复跑测仍稳定存在

同时保留另一份单独清单：

- **provisional / inconclusive candidates**

只要这份清单里还有 case 没被 maintainer 明确裁定，就**不能**宣称“历史最好值排查已经完全清零”。

重复跑测建议至少 3 次，确认不是噪声。

### 第 6 步：判断问题属于哪一层

按下面顺序分层：

#### A. Source 侧是否已变

比较历史 run 和当前 run 的 `source_structure`：

- `visible_texts`
- `total_nodes`
- `kind_histogram`

如果 source 结构已经变了，先停，说明设计输入变了。

#### B. 导入前半段是否已变

比较 import report：

- `warnings`
- `missingFonts`
- `imageImportReport`
- `diagnostics.degradations`

前提是：

- 当前 run 与历史 run 之间存在**显式、可追溯**的 import report 绑定关系

如果没有这种绑定关系，就不要把 import report 当作正式分层证据，只能把它当作辅助线索。

如果 import report 基本一致，问题不在导入前半段。

#### C. 旧资产与当前资产是否都能在今天复现

直接渲染老资产和当前资产，确认差异是在：

- 生成链
- 还是运行时环境

#### D. 模板阶段还是编译阶段

对同一个 case 开 `LogDesign2UMG Verbose`，看：

- `TextFont[template-precompile]`
- `TextFont[compiled-instance]`
- `WidgetState[compiled-after-draw]`

判定：

- 模板阶段已不同：先优先检查 parser / exporter / source-side mapping，再看 converter / property data
- 模板一致、compiled 不同：问题在 widget runtime / font / material / Slate measure

### 第 7 步：只做护栏内试探

任何代码修改前，先选一组最小护栏：

- 目标 case
- 最容易被该类改动误伤的 2 到 4 个 case

示例：

- 修文本测量时，护栏应包含不同字体、不同文本增长模式、不同平台来源
- 修 Figma padding 时，护栏应包含已知 stretch/fill 布局和 pen case

规则：

- 先跑护栏 batch
- 目标 case 没改善，立即撤回
- 护栏 case 任意一个退步，立即撤回

### 第 8 步：修复成立后再跑全量 admissible

护栏通过后，必须跑全量 admissible baseline：

- 不要求绝对阈值都过
- 但必须保证 `no_regression.failed == 0`
- 且这一轮必须是**全量正式基线 run**，不能是子集诊断 run

### 这份文档不等于 release 验证协议

这份文档只负责回答：

- 哪些历史最好值可以作为正式目标
- 哪些历史最好值应该被排除
- 剩余 blocker 应该如何定位

它**不替代**正式 release 验证协议。

即使某个 case 已经达到 admissible best，仍然必须继续满足：

- `Docs/ValidationProtocol.md`
- `Docs/ReleaseBlocker_FinalScope_20260329.md`

里的多信号验证要求，包括但不限于：

- fresh asset / fresh png
- 结构树验证
- 必要时的 Designer 证据

## 建议产物

每轮正式排查建议固定产出以下 maintainer 工件；如果仓库还没有对应固定脚本，就必须先由 maintainer 手工或临时本地脚本生成这些工件，再继续后续步骤：

- `_history_audit_<date>.json`
- `_admissible_candidates_<date>.json`
- `_admissible_best_<date>.json`（仅在旧资产复现完成、正式升级后生成）
- `_removed_nonreproducible_cases_<date>.json`
- `_admissible_full_<date>` reference root
- `_final_admissible_full_<date>/summary.json`

这样后续 AI 或人接手时，不需要重新猜“哪一批历史结果可信”。

当前仓库已经把这批工件的 checked-in canonical wrapper 固定在：

- `Docs/ValidationGovernance/AdmissibleBaselineManifest_20260404.json`
- `Docs/ValidationGovernance/FidelityBlockerManifest_20260404.json`
- `Docs/ValidationGovernance/BlockedRuns_20260331.json`
- `Docs/ValidationGovernance/README.md`

后续如果刷新日期版本，必须同步更新这里的正式落点，而不是只留下新的 raw automation 目录。

## 实操命令模板

### 1. 焦点 admissible case 定向跑测

```text
python Tools/design2umg_auto_verify_batch.py --design-root "G:\UE5\FPSRoguelike\Designs" --figma-export-root "G:\UE5\figma_export" --figma-export-root "G:\UE5\figma_export1" --project "G:\UE5\FPSRoguelike\FPSRoguelike.uproject" --reference-root "<admissible_reference_root>" --output-root "G:\UE5\UnrealEngine\Engine\Plugins\Marketplace\Design2UMG\Saved\Design2UMG\Automation\<probe_output_root>" --pixel-tolerance 6 --changed-ratio-threshold 0.01 --mae-threshold 0.02 --include "^(case_a|case_b|case_c)$"
```

同时必须额外落一份 provenance 记录，至少写清：

- 这是 `focus/guard/probe` 子集 run，而不是正式全量基线 run
- 本次使用了哪些 `--include` / `--exclude`
- 该输出根目录禁止进入 admissible history

建议固定路径：

- `Saved/Design2UMG/Automation/<probe_output_root>/run_scope.json`

### 2. 单旧资产复现

```text
python Tools/design2umg_auto_verify.py asset --source "<old_widget_blueprint_object_path>" --reference-provider png --reference "<reference_png>" --project "G:\UE5\FPSRoguelike\FPSRoguelike.uproject" --output-dir "G:\UE5\UnrealEngine\Engine\Plugins\Marketplace\Design2UMG\Saved\Design2UMG\Automation\<probe_dir>"
```

注意：

- `design2umg_auto_verify*.py` 只要比较失败就会返回非零退出码
- 先看 `summary.json` / `compare_report.json` / UE log，再判断是“视觉失败”还是“真实 crash”

### 3. 打开 verbose widget/font 状态

```text
python Tools/design2umg_auto_verify_batch.py ... --log-cmds "LogDesign2UMG Verbose"
```

### 4. 正式全量 admissible 回归

```text
python Tools/design2umg_auto_verify_batch.py --design-root "G:\UE5\FPSRoguelike\Designs" --figma-export-root "G:\UE5\figma_export" --figma-export-root "G:\UE5\figma_export1" --figma-export-root "G:\UE5\figma_export2" --figma-export-root "G:\UE5\figma_export3" --project "G:\UE5\FPSRoguelike\FPSRoguelike.uproject" --reference-root "<admissible_full_reference_root>" --output-root "G:\UE5\UnrealEngine\Engine\Plugins\Marketplace\Design2UMG\Saved\Design2UMG\Automation\<final_admissible_full_output_root>" --pixel-tolerance 6 --changed-ratio-threshold 0.01 --mae-threshold 0.02 --exclude "^(case_removed_a|case_removed_b)$"
```

要求：

- 不使用 `--include`
- `--exclude` 只允许排除已经被 `_removed_nonreproducible_cases_<date>.json` 明确列出的 case
- 这类 `--exclude` 不算诊断子集 run，而算 formal admissible gate 的一部分

## 这次已验证的经验

### 1. `summary.passed/failed` 不是目标

这只能说明“离参考图阈值是否足够近”，不能说明“相对历史有没有退步”。

真正用于封板的是：

- `no_regression`

### 2. 历史最佳必须先过 admissible 过滤

历史里出现过的单点最好值，很多来自：

- 无效 reference
- 会打坏其他 case 的 probe run
- 今天已不可复现的旧资产结果

这些都不能继续当目标。

### 3. 旧资产复现是最省时间的分叉点

如果旧资产今天仍更好，就该继续查生成链。
如果旧资产今天也不更好，就不要再为那次历史低分改主路径。

### 4. 先证伪，再保留

任何“看起来能解释症状”的规则，都必须先用护栏证明：

- 目标 case 真改善
- 没有任何 admissible guard case 退步

只要不满足，立即撤。

## 何时停止

满足任一条件即可停止本轮“历史最佳排查”：

- 目标 case 已达到 admissible best，且全量 admissible `no_regression` 无退步
- 目标 case 被证明属于不可复现历史优势，并已从派生 baseline 中移除
- 现有证据只能支持“继续查”，还不能支持“安全改代码”

最后一种情况必须明确写出：

- 哪些 case 还没解决
- 已经排除了哪些方向
- 下一步最短闭环是什么

如果要宣称“已经验证完成 / 可以封板 / 可以发布”，还必须额外满足正式验证协议，而不是只满足这份方法论文档。
