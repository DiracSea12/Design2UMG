# Design2UMG Validation Protocol

## 用户测试集与基线

开源的是验证工具，不是维护者的私有测试集。将自己的用例集中到一个目录，以 `--design-root` 指定；工具递归扫描，发现多少个就验证多少个，没有固定的案例名或数量要求。维护者本地的用例数量只描述该次运行，不是用户必须满足的门槛。私有设计稿、参考图、基线和完整运行日志不上传到公共CI或打进插件包。

`--list-cases` 可在没有UE的环境中列出实际发现集合并写出 `run_scope.json`。空目录的普通 suite 运行记录 `status=empty`、发现/执行均为0、`passed=null`，退出码0且不启动编辑器；这不代表视觉验证通过。错误目录或坏输入须报错；只有design.json、尚无参考PNG的Figma目录暂不构成可运行case，扫描会明确警告跳过，不记作通过。一个符合条件的Pen根节点算一个case；一个Figma导出沿用local/source两条验证路径，不把文件数冒称case数。目录示例见 [Tools](../Tools/README.md)。

基线由用户通过 `--reference-root` 提供；每个case目录包含参考PNG和基线比较报告，才能执行 `no_regression`。代码比较期间冻结源/参考身份和已登记的指针，不自动把待测结果提升为新基线；不同身份不强比，同身份返回时沿用已有历史。`--no-publish-history` 保留历史检查但只写本次目录，不改全局历史清单。

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

这些规则来自一次可复现失败：真正的问题是 automation 字体串跑污染，不是 converter boundary 本身。

## 必要验证顺序

### 1. 先构建

```text
<UE5_Root>\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe <RuntimeProjectName>Editor Win64 Development "-Project=<MaintainerRuntimeProject.uproject>" -Module=Design2UMGCore -Module=Design2UMGEditor -UsePrecompiled -NoEngineChanges -WaitMutex -NoHotReloadFromIDE
```

### 2. 运行聚焦自动化测试

先跑最小相关测试，再按风险扩大范围。

```powershell
pwsh -NoProfile -File Tools/Run-Design2UMGAutomationTests.ps1 `
  -ProjectPath "<MaintainerRuntimeProject.uproject>" `
  -EditorPath "<UE5_Root>/Engine/Binaries/Win64/UnrealEditor.exe" `
  -Tests Design2UMG.Generation
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
pwsh -NoProfile -File <PluginRoot>\Tools\Run-DesignerSnapshotValidation.ps1 -ProjectPath <MaintainerRuntimeProject.uproject> -EditorPath <UE5_Root>\Engine\Binaries\Win64\UnrealEditor.exe -AssetObjectPath <WidgetBlueprintObjectPath> -DesignerPngPath <PngPath> -ResultJsonPath <ResultJsonPath>
```

`-AssetObjectPath`、`-DesignerPngPath`、`-ResultJsonPath` 为必填参数，wrapper 没有样例专属默认值。

该 PowerShell wrapper 默认检测已有编辑器并拒绝继续，不再强制结束任何会话。先保存并关闭编辑器；只有明确要保留现有调试会话时才使用 `-KeepExistingEditor`。

不要把 PowerShell 命令返回当成验证完成。必须等实际 `UnrealEditor.exe` 退出，或使用 wrapper 管理。

### 7. Saved asset parity 相关时运行结构快照验证

结构快照用于比较 source design conversion tree 和同一次自动化 run 生成的 saved `UWidgetBlueprint` widget tree。它是结构证据，不是视觉证据。

### 7.1 保存后必须做新进程重载

同一 UnrealEditor 进程里拿到的内存 WidgetTree，只能证明生成阶段的对象树存在。保存后的正式结构结果必须按下面的顺序确认：

1. 生成 Widget Blueprint，并记录保存前的结构快照。
2. 等待 `UnrealEditor.exe` 真正退出。
3. 用新 UnrealEditor 进程重新加载同一个 `.uasset`。
4. 再做一次结构快照，并检查 `LoadErrors`、编译器 ensure 和最终 PNG。

本项目曾用同一个工程和同一套自动化入口做过一组 A/B 记录。
**以下两条是已被推翻的历史失败样本，仅作为下文重载门禁的负样本模型保留，不是当前的生产缺陷结论：**

- 历史样本：含有 `Design2UMGConstrained*` 的资产当时被观察到新进程重载后为 10 个节点、168 条 `LoadErrors`，错误集中在这些自定义控件的外部容器。
- 历史样本：只使用普通 UMG 节点的资产当时重载为 219 个节点、0 条 `LoadErrors`。

**当前结论（2026-08-25）：** 用完全相同的原始命令行（同一工程、同一 driver 入口、同一份磁盘资产、
全新编辑器进程）受控复跑，融合资产干净加载：节点数完整、外部容器加载失败为零、变量 GUID ensure
为零。上面的历史失败样本采集于并行工作线反复重编译插件二进制的时间窗，只能证明当时的局部环境，
不能证明 `LayoutMapper::TryFuseSizeConstraints()` 存在固有的保存/重载缺陷。

<details><summary>已推翻的历史触发链（原样保留，作为重载门禁负样本的失效模型）</summary>

当时记录的判断是：`LayoutMapper::TryFuseSizeConstraints()` 把 `SizeBox + 子控件` 融合成 `Design2UMGConstrained*`，这些编辑器模块控件在内存渲染阶段可用，但进入 WBP 资产后的新进程重载阶段失败，WidgetTree 随后被截断；`WidgetBlueprintCompiler.cpp:828` 中“deleted variable still has a GUID”属于重载失败后的后续症状。该触发链不构成当前的生产缺陷结论。

</details>

### 7.1.1 运行新进程重载门禁

`Tools/design2umg_fresh_process_reload_gate.py` 是本节的 executable 版本。它启动一个全新编辑器进程，
用 `force_reload_package` 通过 `Design2UMG.AutomationRender` 重载每个目标资产，等待真实进程退出，
然后 fail-closed 判定：编辑器退出码必须为零、每个结果必须 success、每棵 WidgetTree 节点数不得低于保护下限、新进程日志不得出现
两类损坏签名（外部容器加载失败、变量 GUID ensure）。它的离线自测把历史失败日志当负样本回放：

```text
python Tools/design2umg_fresh_process_reload_gate_selftest.py
pwsh -NoProfile -Command "python Tools/design2umg_fresh_process_reload_gate.py --project <MaintainerRuntimeProject.uproject> --editor <UE5_Root>\Engine\Binaries\Win64\UnrealEditor.exe --assets /Game/.../WBP_X.WBP_X[,...] --output-root <RuntimeProjectRoot>\Saved\Design2UMG\FreshReloadGate_<stamp> --min-total-nodes <下限>"
```

正式多资产验证时，用逐资产下限钉住每个资产的预期节点数，避免某个资产的局部截断被另一个资产的
更大节点数掩盖：

```text
--asset-floors "0=136,1=219"
```

改动控件类、converter 输出形态或保存/编译链之后，先跑这个门禁，再采信生成进程里的结构快照。

这条结论限定在当前 `Design2UMGConstrained*` 实现和保存/重载链；其他自定义控件需要单独通过新进程重载验证。同一进程内存快照只作过程证据，saved-asset parity 的最终证据必须包含新进程重载结果。

完整 suite 可直接使用已记录的集合进行新进程重载，无需另写资产名单或很长的命令行：

```text
python Tools/design2umg_fresh_process_reload_gate.py --project <YourProject.uproject> --editor <UE5_Root>/Engine/Binaries/Win64/UnrealEditor.exe --run-root <CompletedRunRoot> --output-root <NewReloadRoot>
```

正式入口核对每项资产身份、严格成功和重载标志，并比较节点数、文本、类型统计及候选像素。手工 `--assets` 模式只验证指定的正数节点下限，索引必须唯一且在范围内；不冒称验证了未提供的 suite 基线。

### 8. 只要改了 automation-render parity 相关面，就先跑 determinism smoke，再进 formal gate

以下改动都必须先过 determinism smoke：

- automation render orchestration
- package reload preparation
- transient reset
- automation font refresh
- bootstrap render preparation

这一步是维护者用的 focused pre-gate，不是 release closeout。

预期集合来自本次 `run_scope.json`，没有固定的sentinel名单。默认验证完整发现集合；需要定向复跑时显式传 `--include`（PowerShell为 `-Include`）。

```text
python Tools/design2umg_determinism_smoke.py validate-run-root --run-root <CompletedRunRoot>
```

必须有非空且完整的用例集合、有限非负的误差指标、零生成/重载漂移及一致的结构/文本。缺失、重复、额外、不可读或身份不符的证据均不放行。局部检查不能冒充完整测试集通过。

## 当前 formal gate

正式full run是用户目录实际发现的完整集合，不使用include/exclude过滤，不设置固定最小值、最大值或私有案例名。以 `run_scope.json` 记录范围，比较期间冻结输入和参考身份。零用例只说明没有执行测试，不构成视觉覆盖；新的运行目录不等于获准更换基线。
这意味着：

- formal full gate 不能带 exclude pattern。
- formal suite report 不能把 raw visual-threshold `passed/failed` totals 当成成绩。
- formal historical-best parity 要用：

```text
python Tools/design2umg_validate_run.py --output-root <run_root> --admissible-best-json <PluginRoot>\Saved\Design2UMG\Automation\_admissible_best_latest.json
```

- Python helper 合同要单独通过这些不依赖 UE 的自测检查：
  - `python Tools/design2umg_validation_helpers_selftest.py`
  - `python Tools/design2umg_compatibility_selftest.py`
  - `python Tools/design2umg_diagnostics_selftest.py`
  - `python Tools/design2umg_run_history_selftest.py`
  - `python Tools/design2umg_suite_report_contract_selftest.py`
  - `python Tools/design2umg_determinism_smoke_selftest.py`
- 它们一起覆盖 case discovery 过滤、默认 Figma export root 解析、run scope 写入、shard sizing、completeness 聚合、compatibility 规则路由、diagnostic stderr 形状、historical-best helper owner，以及 suite summary / timing-history 落盘合同。
- import diagnostic report JSON 和这些 helper 的 stderr 行不是一回事；前者单独见 `Docs/ImportDiagnosticContract.md`。
- suite verifier 必须通过 `--project` 或 `DESIGN2UMG_RUNTIME_PROJECT` 显式传入真实 runtime project。缺少 project 输入时要直接报错，不能靠 design-root 推断，也不能生成 HostProject。
- suite 级耗时漂移会落到 `Saved/Design2UMG/Automation/suite_timing_history.jsonl`。这只是给维护者看趋势的证据，不是替代 fidelity / no-regression 的发布门禁。

正式 full-run 如果使用 external reference root，每个 formal case 目录都必须同时有 `reference.png` 和 `compare_report.json`。只有 `reference.png` 会让 run root 的 `summary.no_regression` 变成 unavailable，不能作为正式归档 gate。

归档级 no-regression 证据还必须注意：

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

- 完整 formal suite 必须独占串行跑。不能和 focused automation、别的 editor validation、另一个 formal suite 并行使用同一个真实工程或 automation 资产目录。
- 如果某次并行验证后突然冒出新的 pipeline determinism incompatibility，先独占重跑 formal gate，再决定要不要改代码。并行验证本身就可能制造假的 determinism 噪音。

### shard 模式的分数波动

`--editor-isolation shard` 适合批量执行和产物收集，但多个 shard / 编辑器生命周期可能共享或重置不同的 Slate 字体缓存、临时资产和渲染状态，导致 visual score 出现波动。shard 单次结果只作初筛；任何异常分数先用 `--editor-isolation case` 对受影响 case 独占、串行复跑，复跑结果稳定后再进入回归判断。报告同时记录 shard run 和 case recheck 的路径，shard 单次波动不直接当成视觉回退或改善。

## 分层证据链（IR golden、几何与像素层）

像素对比是其中一层裁判，不是全部。像素分数衡量的是与 Figma 光栅化参考 PNG 的相似度，渲染器固有差异（抗锯齿、纹理重采样、字体光栅化）永远混在数字里。分层证据链把每层真正能证明的东西分开：

- **IR golden 层**（`Tools/design2umg_ir_golden.py` + suite verifier 的 `--ir-golden <golden-root>`）：每个 case dump 转换后的 `FUMGWidgetData` typed 树（浮点 `%.9g` 无损、字节稳定），与冻结 golden 零容差对比。证明"转换行为没变"，任何数值漂移直接寻址到字段（如 `RootWidget...Brush.BrushColor.R: golden=0.108 dump=0.1098`），不需要图像实验。每份冻结集合都必须在配套元数据中记录代码状态和基线指针关系。golden 是 no-regression 口径，不是正确性口径。无 golden 的 case 记 `unavailable` 不算失败。
- **像素层**：原样保留，端到端相似度，由容差管理。
- **几何层**（`Tools/design2umg_geometry_compare.py` + suite verifier 的 `--geometry-golden <golden-root>`）：每 case 由编辑器在编译渲染后 dump 运行时 widget 几何（cached geometry），与冻结 golden 几何 dump 在同一组可比节点上对比，节点配对复用共享树对应（容忍 wrapper 插入导致的路径迁移）。case 判定是 stability——冻结 golden 对当前运行时、0.75px 容差、x/y 锚定最近配对祖先，插入了 wrapper 但绝对几何不变不算漂移；fidelity（IR 推导期望对运行时，坐标取 IR 父的运行时配对系）逐节点记录但不判 case 失败。无 golden 的 case 记 `unavailable` 不算失败。

像素层的评分逻辑统一在 `Tools/design2umg_scoring.py`（唯一归属地，suite verifier 只调用）。no-regression 容差已参数化：

- `--no-regression-epsilon` 默认 `1.25e-4`，不传参时行为与历史完全一致。
- 封板口径可显式传 `1e-6` 收紧。两个前置条件：Pencil reference cache 必须冻结（read-only 或核对 sha1，live 重渲染的 reference 漂移在 `1e-4` 量级，会被小容差误报成回退）；shard 模式不得使用收紧容差（shard 状态噪音同样在 `1e-4` 量级，小容差只用于 `--editor-isolation case` 的正式判定）。
- 收紧容差前，必须用 case isolation 重跑完整冻结集合，逐个检查判定反转。量化级像素差异可能跨过小阈值；必须由 IR 层归因对应字段变化，不能只用反转数量作结论。

IR golden 失败目前报告在 `summary.ir_golden` 与每 case 的 `ir_golden_report.json`，暂不耦合 suite exit code。整 run 零 dump 时 `design2umg_ir_golden.py` 以 exit 2 报配置错误（例如 cvar 未开启），与单 case `unavailable` 区分。

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
