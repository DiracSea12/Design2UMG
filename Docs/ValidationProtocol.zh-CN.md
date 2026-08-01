# Design2UMG Validation Protocol

English canonical: [ValidationProtocol.md](ValidationProtocol.md)

Last verified: 2026-05-18

## 目的

这个文档定义 Design2UMG 变更应该怎么验证。

目标是：

- 验证可重复。
- 不混用不同层级的证据。
- 避免 stale PNG 或错误渲染路径造成假阳性。

公开文档应使用 `<UE5_Root>`、`<PluginRoot>`、`<MaintainerRuntimeProject.uproject>` 这类逻辑占位符。维护者拥有的 canonical runtime boundary 是一个验证角色，不是公开用户必须照抄的机器路径。

## 验证层级

本仓库有四种验证层级，不能混成一个词：

- `repository-hygiene`：公开仓库检查，例如必需文件和 tracked maintainer-local path 检测。
- `self-hosted build and automation`：维护者 push、同仓库 PR 或手动触发时，在自托管 Windows UE runner 上打包插件并运行生成、Figma parser 和生命周期相关自动化测试，随后验证 package boundary 并上传插件产物。
- `public smoke/public validate`：贡献者可复现的本地构建和聚焦验证。这是公开验证边界。
- `maintainer formal admissible gate`：维护者拥有的完整 admissible case set 正式 release gate。

除非某节明确写 `maintainer formal admissible gate`，否则下面的命令都应理解为 public smoke/public validate 指南，不是单独的 release gate。

Package/export boundary validation 是 release hygiene 层，不是 UE 行为验证的替代。Source workspace 检查可以忽略 intentionally local 的规划文件；package/export 检查必须拒绝 root review reports、`openspec/`、AGENTS/CLAUDE local rules、raw automation trees、cache、HostProject fallback artifacts、generated fallback host directories、具体 fallback project paths 和维护者绝对路径。

如果只想快速看“贡献者能跑什么、维护者独占什么”，请看 [ValidationGovernance/TestCoverageBoundary.zh-CN.md](ValidationGovernance/TestCoverageBoundary.zh-CN.md)。

## 核心规则

不要把任何单一信号当成完整验证。

视觉修复至少需要这些证据相互一致：

- 自动化测试通过。
- 如果 saved-asset structure parity 在范围内，结构快照对比通过。
- 目标 `.uasset` 是新生成的。
- post-save PNG 是新生成的。
- 如果 Designer parity 在范围内，editor-side Designer screenshot 是新生成的。
- 结果在被修的区域符合目标 source design。

## Automation 字体隔离规则

automation 字体导入是“每个 case 自己的临时证据”，不是共享真相。

维护规则：

- 不能把 case-local automation icon font 路径持久化到共享设置或跨 case snapshot。像 `/Game/Design2UMG/Automation/<case>/ImportedFonts/...` 这种路径，只属于当前 case。
- automation 跑 case-local ImportedFonts 时，当前 case 必须解析到当前 case 自己的字体对象路径。当前 case 的字体资产缺失时，要先修或重建当前 case-local 资产。
- 不能在 case-local run 里偷偷退回旧的 `/Game/Design2UMG/Automation/ImportedFonts/...`，把它当成当前 case 的字体资产。这个全局路径可能带着旧 run 的字体状态，会制造假回退。
- 如果 case-local 字体重新下载失败，优先把“已经能加载的现有字体资产”重建到当前 case-local 路径，而不是直接借用旧的全局 automation object path。
- 查字体回退时，必须同时看 Unreal log 里的 generation 阶段和 compiled-instance 阶段字体对象路径。只要当前 case-local run 里出现别的 case 的 `ImportedFonts` 路径，或者出现全局 automation 路径，就先按污染查，不要先怪业务逻辑。

phase21 batch2 这次已经重新验证过：真正的问题是 automation 字体串跑污染，不是 converter boundary cleanup 把逻辑稳定改坏了。

## 必要验证顺序

### 1. 先构建

```text
<UE5_Root>\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe FPSRoguelikeEditor Win64 Development "-Project=<MaintainerRuntimeProject.uproject>" -WaitMutex -FromMsBuild
```

### 2. 运行聚焦自动化测试

先跑最小相关测试，再按风险扩大范围。

```text
<UE5_Root>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe <MaintainerRuntimeProject.uproject> -unattended -nop4 -nosplash -NullRHI "-ExecCmds=Automation RunTests Design2UMG.WidgetTree.BorderSpacerStrokePadding+Design2UMG.Rasterizer.PersistentPathTexture+Design2UMG.WidgetTree.DesignSizeExpansion; Quit" "-TestExit=Automation Test Queue Empty" -log
```

### 3. 重新生成真实 asset

视觉验证时，应把目标 frame 导入最终输出路径。

### 4. 确认 asset 和 PNG 都是新生成

`.uasset` 和 `.png` 的时间戳都必须晚于本次验证开始时间。如果 asset 更新但 PNG 没更新，本次视觉验证不完整。

### 5. 检查日志里的正确渲染路径

日志应包含：

- `Saved Widget Blueprint at ...`
- `Rendered PNG saved: ...`

如果日志出现 `Widget PNG render failed`，不能用该 run 做视觉结论。

### 6. Designer parity 相关时运行 Designer screenshot validation

Designer screenshot 是验证 UMG Designer 实际显示内容的直接路径。它是 editor evidence，不是 asset-render evidence。

首选 wrapper：

```text
pwsh -NoProfile -File <PluginRoot>\Tools\Run-DesignerSnapshotValidation.ps1 -ProjectPath <MaintainerRuntimeProject.uproject> -EditorPath <UE5_Root>\Engine\Binaries\Win64\UnrealEditor.exe
```

不要把 PowerShell 命令返回当成验证完成。必须等实际 `UnrealEditor.exe` 退出，或使用 wrapper 管理。

### 7. Saved asset parity 相关时运行结构快照验证

结构快照用于比较 source design conversion tree 和同一次自动化 run 生成的 saved `UWidgetBlueprint` widget tree。它是结构证据，不是视觉证据。

### 8. 只要改了 automation-render parity 相关面，就先跑 determinism smoke，再进 formal gate

以下改动都必须先过 determinism smoke：

- automation render orchestration
- package reload preparation
- transient reset
- automation font refresh
- bootstrap render preparation

这一步是维护者用的 focused pre-gate，不是 release closeout。

canonical sentinel owner 固定在 `Tools/design2umg_determinism_smoke.py:CANONICAL_SENTINEL_CASES`。不要在 wrapper 或 selftest 里再手写第二份列表。要看当前 pair，直接跑：

```text
python Tools/design2umg_determinism_smoke.py print-sentinels
```

推荐命令：

```text
pwsh -NoProfile -File <PluginRoot>\Tools\Run-Design2UMGDeterminismSmoke.ps1 -ProjectPath <MaintainerRuntimeProject.uproject> -EditorPath <UE5_Root>\Engine\Binaries\Win64\UnrealEditor.exe -ReferenceRoot <TrustedReferenceRoot> -OutputRoot <MaintainerRuntimeProjectRoot>\Saved\Design2UMG\Automation\_determinism_smoke_<stamp>
```

这条 smoke 的规则很直接：

- `-ProjectPath`、`-EditorPath`、`-ReferenceRoot`、`-OutputRoot` 都必须显式传。
- 缺 run root、缺 sentinel case、缺 compare report、缺 `generation_vs_asset`、缺 `generation_vs_asset_structure`、证据 stale / unreadable / mismatch，都必须 fail closed。
- 通过时，每个 sentinel 的 `generation_vs_asset.changed_ratio` 和 `generation_vs_asset.stable_changed_ratio` 都必须是 `0.0`。
- 通过时，每个 sentinel 的 `generation_vs_asset_structure.available` 也必须是 `true`，并且结构/文本 parity 不能坏。
- 这一步通过，只代表 bootstrap-vs-asset parity 这条路径当前还值得继续跑 formal gate；它不是 formal 71-case 的替代品。

sentinel 维护规则：

- 如果后续某次 isolated formal replay 证明了一个当前 sentinel pair 没覆盖到的新 bootstrap-vs-asset drift case，那么下一次 determinism-focused batch 必须显式重审 sentinel set，不能默默在别处再长一份新名单。

## 当前 formal gate

当前 formal admissible gate 是 71 个 discovered admissible case：39 个 Pencil，加上 16 个 Figma export root 各自的 `local` 与 `source` 两条路径（共 32 个 Figma case）。锁定口径以最新 full-run 的 `run_scope.json` 为准；发现数少于 71 的 run 不算正式 full run。

这意味着：

- formal full gate 不能带 exclude pattern。
- formal batch report 不能把 raw visual-threshold `passed/failed` totals 当成成绩。
- formal historical-best parity 要用：

```text
python Tools/design2umg_validate_run.py --output-root <run_root> --admissible-best-json <PluginRoot>\Saved\Design2UMG\Automation\_admissible_best_latest.json
```

- Python helper 合同要单独通过这些不依赖 UE 的自测检查：
  - `python Tools/design2umg_validation_helpers_selftest.py`
  - `python Tools/design2umg_compatibility_selftest.py`
  - `python Tools/design2umg_diagnostics_selftest.py`
  - `python Tools/design2umg_run_history_selftest.py`
  - `python Tools/design2umg_batch_report_contract_selftest.py`
  - `python Tools/design2umg_determinism_smoke_selftest.py`
- 它们一起覆盖 case discovery 过滤、默认 Figma export root 解析、run scope 写入、shard sizing、completeness 聚合、compatibility 规则路由、diagnostic stderr 形状、historical-best helper owner，以及 batch summary / timing-history 落盘合同。
- import diagnostic report JSON 和这些 helper 的 stderr 行不是一回事；前者单独见 `Docs/ImportDiagnosticContract.md`。
- batch verifier 必须通过 `--project` 或 `DESIGN2UMG_RUNTIME_PROJECT` 显式传入真实 runtime project。缺少 project 输入时要直接报错，不能靠 design-root 推断，也不能生成 HostProject。
- batch 级耗时漂移会落到 `Saved/Design2UMG/Automation/batch_timing_history.jsonl`。这只是给维护者看趋势的证据，不是替代 fidelity / no-regression 的发布门禁。

正式 full-run 如果使用 external reference root，每个 formal case 目录都必须同时有 `reference.png` 和 `compare_report.json`。只有 `reference.png` 会让 run root 的 `summary.no_regression` 变成 unavailable，不能作为正式归档 gate。

phase21 Batch 6 又把一个很容易复犯的坑踩实了：

- 如果这次 formal 的目标是拿归档级 `summary.no_regression` 证据，就不能漏掉 `--reference-root`。
- 不带 `--reference-root` 也能跑出一条对 live Pencil 健康度有参考价值的 run，但很多 Pencil case 会合法降级成 `unverified_reference`、`comparison_mode_mismatch` 或 reference authority 变差。
- 这种 run 说明的是“当前 live Pencil 取参照链路怎样”，不说明“这次变更相对正式 baseline 零回退”。
- 所以要把两个问题分开看：
  - 现在 live Pencil export 健不健康？
  - 这次 formal tooling / release 变更有没有保住归档级 no_regression 证据链？
- 第二个问题，必须带 curated external `--reference-root`。

formal gate 读数顺序：

- `summary.json` 负责回答这次 run 相对当前 baseline 有没有回退，也就是 `summary.no_regression`。
- `validation_against_historical_best.json` 负责回答当前 admissible case 有没有落后于可追溯历史最佳。
- 这两个信号不能混成一句话。一个 run 可能 `no_regression` 通过但 historical-best 还没追平，也可能只是 pipeline determinism 出了问题，但并不是真正的历史最佳回退。

formal gate 执行纪律：

- full 71 formal gate 必须独占串行跑。不能和 focused automation、别的 editor validation、另一个 formal batch 并行打同一个真实工程或 automation 资产目录。
- 如果某次并行验证后突然冒出新的 pipeline determinism incompatibility，先独占重跑 formal gate，再决定要不要改代码。并行验证本身就可能制造假的 determinism 噪音。

## HostProject 和 Pencil MCP 边界

HostProject fallback 不属于支持的验证 workflow。自动化脚本不能偷偷创建 runtime project、formal evidence root、HostProject fallback 或维护者本地路径。

旧的 Pencil `visual_studio_code` / `vscode` app alias 兼容已经删除。若 Pencil 因 editor app id 注册不符合预期而连不上，只记录真实 blocker 并停止；不要重新加入 alias 文件修补、HostProject fallback 或其他兼容补丁来强行继续跑。

## 回归敏感区域

这些区域已经真实造成过成绩回退，必须谨慎：

- Text heuristics：不能假设 source `fontSize` 就是 UMG runtime font size。
- Fixed single-line text：不能加 blanket heuristic 去保留声明字号。
- Dynamic measured text fallback scale：不能默认启用，除非完整 formal run 证明零回退。
- Figma text bounds：不能对 `TEXT` / `TEXT_PATH` 一刀切偏向 layout bounds。

## PNG 和 UMG Designer

PNG 是强信号，但不保证和 UMG Designer 完全一致。PNG 来自保存并编译后的 widget asset；Designer 是 editor preview surface，可能受打开的 asset tab、selection outline、preview zoom、字体 fallback、RHI、DPI 等影响。

当 PNG 和 Designer 不一致时，按这个顺序处理：

1. 拒绝 PNG 生成失败的 run。
2. 确认 `.uasset` 和 `.png` 都是新的。
3. 关闭并重新打开目标 widget。
4. 不带 `-NullRHI` 重新 import。
5. 运行 Designer screenshot automation。
6. 比较 saved asset path，不比较旧 preview output。
7. 如果仍不一致，优先相信 Designer screenshot 和重新打开后的 asset。
8. 为争议节点类型补聚焦回归测试或日志探针。

## 修复报告必须说明

视觉修复报告应包含 build 命令、自动化测试命令、必要的结构快照命令、import 命令、asset 路径、PNG 路径、必要时的 Designer PNG 路径、证明 PNG 新生成的日志，以及本次结论基于 saved asset render、Designer view、saved-asset structure 还是它们的组合。

如果验证不完整，报告必须明确说不完整，不能只写 `Fixed.` 或 `Verified.`。
