# Design2UMG 代码质量改造与优化计划

> 本文档是本轮代码质量改造的**唯一进度主文档（single source of truth）**。用于跨会话、跨 AI 协作时快速确认当前进度、已完成内容、验证证据和剩余风险。
>
> **不要把仓库中历史 review 文档当成本轮改造依据。** 每个阶段开始前必须以当前源码为准重新核实；本文档记录的是已确认的问题、目标和进度，而不是替代源码事实。

---

## 0. 当前状态

- **基线提交**：`5992f1148fb2d3e2f3c2512b3318a9e251958c7e`
- **计划创建日期**：2026-09-17
- **UE 版本**：5.7
- **真实验证项目**：`G:\UE5\FPSRoguelike\FPSRoguelike.uproject`
- **总体状态**：`IN_PROGRESS`
- **发布前必须完成**：PR-01 ～ PR-05、PR-11
- **建议完成**：PR-06 ～ PR-10
- **目标质量**：在不扩大无关范围、不引入平台化过度设计的前提下，把项目提升到“优秀开源 UE 工程、可作为高级工程能力代表作”的水平。

### 状态枚举

每个阶段只使用以下状态：

- `NOT_STARTED`：尚未开始。
- `IN_PROGRESS`：正在实现或验证。
- `BLOCKED`：存在明确阻塞，必须在该阶段记录原因。
- `IMPLEMENTED`：代码已实现，但完整验证尚未完成。
- `VERIFIED`：实现与该阶段要求的验证均完成，可以进入下一阶段。

> **禁止**仅因为“代码写完了”就把阶段标成 `VERIFIED`。

### 总进度

| 阶段 | 状态 | 核心目标 |
|---|---|---|
| PR-01 | `VERIFIED` | Full State 与 Patch/Override State 分离 |
| PR-02 | `NOT_STARTED` | Reimport 破坏性替换改为 Prepare → Validate → Commit |
| PR-03 | `NOT_STARTED` | Import / Reimport / Save 结果状态统一 |
| PR-04 | `NOT_STARTED` | Degradation / Warning / Partial Sync 诊断闭环 |
| PR-05 | `NOT_STARTED` | 删除确定无效复杂度并修严输入解析 |
| PR-06 | `NOT_STARTED` | Typed Payload 与内部 JSON/String 胶水收敛 |
| PR-07 | `NOT_STARTED` | 单次树索引与身份歧义检测 |
| PR-08 | `NOT_STARTED` | Core / Editor 小切口解耦 |
| PR-09 | `NOT_STARTED` | 测试体系分层与低价值测试整理 |
| PR-10 | `NOT_STARTED` | 大文件按已有真实职责边界局部拆分 |
| PR-11 | `NOT_STARTED` | Release hardening、全量回归与最终质量门禁 |

---

# 1. 跨会话 AI 协作协议

任何 AI 在继续本计划前，必须先做以下事情：

- [ ] 阅读本文件的“当前状态”和目标阶段。
- [ ] 阅读根目录 `AGENTS.md`，遵守当前项目协作规则、回归规则和磁盘约束。
- [ ] 检查当前 Git HEAD、工作区状态和本阶段相关源码，确认本文档中的旧位置没有因前序修改失效。
- [ ] 如果当前任务存在适用 OpenSpec，则以 OpenSpec 的需求和验收标准为开发依据，不得自行偏离。
- [ ] 开始开发时，将对应阶段状态改为 `IN_PROGRESS`，并填写“会话记录”。
- [ ] 开发完成后先自测、自审；仅代码完成时标 `IMPLEMENTED`，验证达标后才标 `VERIFIED`。
- [ ] 只更新自己实际处理的阶段，不得为了“进度好看”修改其他阶段状态。
- [ ] 任何新增抽象都必须让对应旧逻辑退出主路径；禁止新旧两套长期并存。
- [ ] 未经用户明确同意，不新增、扩展或加固旧兼容链。
- [ ] 不得修改 UE 引擎源码来解决插件问题。
- [ ] 不得为了视觉成绩做 case 名、节点 ID、文件名、字体名等定向特判。

### 每个阶段完成后必须记录

```text
状态：VERIFIED / IMPLEMENTED / BLOCKED
开始基线：<commit>
完成提交：<commit(s)>
修改文件：<paths>
验证：<实际运行命令 + 结果>
全量回归：<run id/path 或 未运行原因>
剩余风险：<none / 明确列出>
下一步：<下一阶段或遗留项>
```

---

# 2. 不允许被本轮“优化”破坏的主架构

以下结构是项目目前正确且有价值的主干，默认保留：

- `FDesignDocument / FDesignNode` 作为 source-agnostic Design IR。
- `FUMGWidgetData / FUMGWidgetNode` 作为 UMG generation IR。
- `Design IR → UMG IR → UE Widget/Asset Materialization` 的整体方向。
- `Design2UMGCore / Design2UMGEditor` 两模块总体划分。
- Source sidecar 数据、Component Mapping、Generation Manifest。
- Safe Reimport 的生成内容 ownership 边界。
- Preview / Generation Settings Snapshot。
- 引用展开缓存本身。
- Typed UMG payload。
- 组件批保存现有的备份、事务身份检查、恢复和诊断思路。

本轮**不以“代码越少越好”为目标**。目标是：留下的复杂度必须有真实职责，且失败状态可解释、可验证。

### 本轮明确不做

- [ ] 不把 Core 拆成大量微模块。
- [ ] 不引入通用 DI / Service Locator / Repository / Event Bus。
- [ ] 不自建通用事务框架。
- [ ] 不为了缩文件而机械拆所有大 cpp。
- [ ] 不重写整个 Reimport。
- [ ] 不为了“纯函数”消灭所有 Unreal 类型。
- [ ] 不单纯追求 LOC、文件数或抽象层数量下降。

---

# PR-01：修复 Design IR Patch / Override 语义

**状态**：`VERIFIED`

**验收说明**：用户于 2026-09-17 明确接受原始 79/81 版本的 Shadcn components 与 Welcome Step 1 两项已量化视觉例外。本阶段按该批准完成验收，不表示原始零例外门禁变成 81/81。

**优先级**：P0 / 发布前必须完成

## 问题

当前部分 override 逻辑通过“值是否等于默认值”推断字段有没有出现，导致无法可靠表达：

- `Center → Left`
- `End → Start`
- `true → false`
- 非空字符串 → 空字符串
- 非零 gap → `0`
- 基础 bold/italic，只修改 content 时不应被默认 `normal` 覆盖

主要相关位置（开始开发前重新核实）：

- `Source/Design2UMGCore/Public/DesignSchema.h`
- `Source/Design2UMGCore/Private/PenParserNodeExtensions.cpp`
- `Source/Design2UMGCore/Private/PenParserNodeProperties.cpp`
- `Source/Design2UMGCore/Private/RefExpander.cpp`
- `Source/Design2UMGCore/Private/Tests/PenParserTests.cpp`

## 目标设计

完整节点状态（Full State）与部分覆盖（Patch State）必须分离。

推荐使用专用 Patch 数据结构，只让需要 presence 语义的字段使用 `TOptional` 或等价显式表示。不要把整个 `FDesignNode` 无差别改成 Optional，也不要新增几十个手写 `bHasXxx` flag。

Patch 至少应能表达：

- `Absent`：没有提供，不覆盖基础值。
- `Set(value)`：明确设置，包括默认值、`false`、`0`。
- `Clear`：对允许清空的字符串、集合等明确清除。

## 行动项

- [x] 重新枚举所有进入 descendant/ref override 的字段，并分类哪些需要 presence。
- [x] 引入最小的 Patch/Override 数据结构，不扩展成通用属性系统。
- [x] Parser 在读取 override fragment 时只写实际存在的字段。
- [x] `RefExpander` 改为机械应用 Patch，不再通过默认值猜 presence。
- [x] 明确 Children replacement/clear 的语义。
- [x] 删除被新 Patch 模型替代的 sentinel 判断和对应旧 helper。
- [x] 补齐多层 ref / descendant / outer override precedence 测试。

## 必须新增/覆盖的测试

- [x] Center → Left。
- [x] End → Start。
- [x] underline `true → false`。
- [x] strikethrough `true → false`。
- [x] Content 非空 → 空字符串。
- [x] Href 非空 → clear。
- [x] bold + 只改 content → 仍保持 bold。
- [x] italic + 只改 content → 仍保持 italic。
- [x] gap 非零 → `0`。
- [x] clip `true → false`。
- [x] nested ref + descendant override + outer override precedence。

## 完成条件

- [x] 核心 override 路径不再使用 `value == default` 推断字段缺失，除非该 sentinel 是正式协议定义并有测试。
- [x] 上述回归测试全部通过。
- [x] 原有 ref expansion、fit-content、组件实例相关测试无退步。
- [x] 完整 81-case 为 79 项 `no_regression` 通过 + 用户明确接受的 2 项定量例外；没有新增或扩大的退步，81 张生成图片与用户接受的原始 79/81 版本逐张 SHA-256 相同。

## 会话记录与最终结论

2026-09-17：基线 `5992f1148fb2d3e2f3c2512b3318a9e251958c7e`。当前阶段已完成，修改保留在工作区，**未暂存、未提交**。PR-02 及后续阶段未开始。

### 已完成的实现

- 完整 `FDesignNode` 与 `FDesignNodePatch` 分离。Patch 保持 52 个具有实际解析、合并和应用入口的字段；缺失继承、明确默认值照常设置、允许清空的字符串及集合可清空。没有把完整 IR 无差别 Optional 化。
- `FDesignNodeOverride` 区分完整替换与后续部分覆盖；Parser 处理 JSON presence，Expander 机械应用 typed patch。旧 `ShouldApplySizeOverride`、`ApplyCommonOverrideProperties`、`ApplyOverrideProperties`、FontSize sentinel 及失效的解析分支已退出。
- 完整替换、children 保留/替换/清空、富文本清理、三态描边 paint、多层覆盖、深拷贝、缓存 snapshot alias 与相对图片路径消费者已迁移。
- 只读审查发现的三项边界问题已经收口，具体见下表。相对原始 79/81 源码，只增加/调整 **3 个生产文件和 1 个测试文件**；不涉及字体、渲染器、SizeEstimator 或通用布局改写。

| 边界问题 | 最终处理 | 实际验证 |
|---|---|---|
| cap/join 独立覆盖读到默认值 | `PenParser.cpp` 在有效解析端点或拐角时写回局部描边；未提供的描边属性继续继承 | cap-only、join-only 分别覆盖 ref 根和 descendants；真实生成的两条覆盖路径及直接声明对照均为 round/round |
| 父子路径及多层替换顺序不确定 | `RefExpander.cpp` 先丢弃被外层替换子树的旧子路径 patch，再合并外层 patch；按父结构到子路径确定应用顺序。根 children 替换和延后 ref 路径也清理旧作用域 | 调换中间层键顺序均得到 OUTER；外层重新声明的子路径不再继承被替换子树的 fontSize/underline；既有完整替换和实例隔离测试保持通过 |
| children 替换后 padding ownership 过期 | `PenParserNodeProperties.cpp` 传递源侧 padding 决策；无烘入坐标的新 children 恢复 runtime inset，只有子坐标时不能取消从另一层继承的 runtime padding；明确同时声明 padding 与坐标时仍保留源侧 baked 策略 | 无背景容器中，覆盖路径与直接声明对照的矩形起点都为 (5,5)；正向恢复与继承所有权保留均有测试 |

上述最后一项没有把坐标推断复制到 Expander，也没有统一增加 Border。前次扩大重算造成的真实 case 退步已撤销。新模型、字段数和模块边界保持不变。

### 测试与真实项目验证

验证使用 `G:/UE5/FPSRoguelike/FPSRoguelike.uproject` 和 `G:/UE5/UnrealEngine/Engine/Binaries/Win64/UnrealEditor.exe`。

收口增加/加强 `StrokeParts`、`ParentChildPrecedence`、`ChildrenPaddingPolicy`、`ReplacementScope`、`DeferredSubtree` 五项行为测试。原始 9 项 Override 测试内容、断言未放宽，当前该组共 14 项。

当前证据目录：`Saved/PR01/boundary-closure-20260917/`。

| 检查 | 结果 | 证据 |
|---|---|---|
| 真实项目最终编译 | Succeeded | `build-02.log` |
| Override 自动化 | 14/14，exit 0 | `focused-02.log` |
| 全插件自动化 | **585/585，exit 0** | `all-tests.log`、`all-tests-command.json` |
| 三类缺口的四份合成输入，分别独立进程生成 | 4/4 生成成功；4 个 editor 均 exit 0；最终 IR 与像素断言通过 | `diagnostics-serial/`、`serial-diagnostic-results.json`、`verification.json` |
| 受影响六个真实 case 独占串行复测 | 4 项通过 + 原批准 2 例外；6 个 editor 均 exit 0；6 张图片与原 79/81 版本完全相同 | `isolated6.log`、`isolated6-command.json` |
| 完整 discovered 81-case | **79 项通过 + 原批准 2 例外**；2 个 editor 均 exit 0；**81/81 candidate.png 与原 79/81 运行 SHA-256 相同** | `full81.log`、`full81-command.json`、`pr01-boundary-full81-20260917-assessment.json` |
| 历史最佳及身份完整性 | 仅同样 2 项批准例外；无缺失产物和身份/尺寸/流水线不兼容项 | full run 的 `validation_against_historical_best.json` |
| 源码冻结、编码、diff | 验证期间 390 个 cpp/h 无变动；修改文件 UTF-8 无 BOM；`git diff --check` 通过 | `verified-source.json`、`final-source.diff`、`verification.json` |

完整运行：
`G:/UE5/FPSRoguelike/Saved/Design2UMG/Automation/pr01-boundary-full81-20260917`。

独占串行运行：
`G:/UE5/FPSRoguelike/Saved/Design2UMG/Automation/pr01-boundary-isolated6-20260917`。

冻结 reference-root 仍为：
`G:/UE5/FPSRoguelike/Saved/Design2UMG/Automation/review-batch-transaction-full-20260915`。

最终 full run 没有 include/exclude，scope 与原始正式 81-case 相同。完整命令保存在上述 JSON 文件；使用 `--parallel-editors 2 --editor-isolation shard --reference-cache read-write --native-self-check off --no-regression-epsilon 1e-6`。独占六项使用 `--parallel-editors 1 --editor-isolation case`。

**原始验收器没有改阈值或增加 case 白名单。** full batch 原始退出码仍为 **2**，原因是历史门禁仍正确报告下面两项退步；本阶段 `VERIFIED` 依据用户的明确例外批准，不把原始门禁描述为自动全绿。Saved 中的 assessment 另行检查已批准例外不得继续恶化、其他 case 不得新增退步。

### 批准的两项视觉例外及上限

下列结果在最终 full run、独占 recheck 与原始 `pr01-patch-full81-20260917` 完全相同；不是新增加的容差。

| Case | Changed ratio | Stable changed ratio | MAE | Stable MAE |
|---|---:|---:|---:|---:|
| `pen_pencil-shadcn_shadcn-design-system-components` | 0.02234214405226675 | 0.01519131941677954 | 0.003023408805797835 | 0.0011066118862492014 |
| `pen_pencil-welcome_step-1-frame` | 0.11094753086419754 | 0.0835054012345679 | 0.011095472494553378 | 0.003920847706366497 |

相对登记基线，Shadcn 的 stable MAE 增加 `0.000006635183353877064`；Welcome Step 1 的 stable changed ratio 增加 `0.00019830246913579708`，其余已量化变化保留在 compare reports。用户于本次会话明确接受它们。此批准只覆盖当前相同输入/有效参考身份的既有幅度，**不得扩展为未来按 case 名无条件放行，亦不得据此移动基准**。

### 自审与范围边界

1. **简洁性**：新增逻辑用于解析写回、替换作用域和稳定应用顺序；没有新增模型字段、模块、配置、注册表、通用属性系统或兼容 fallback。最终撤掉了 Expander 中的 padding 重算和临时诊断日志。
2. **行为完整性**：计划列明的 presence 行为以及本轮三项已复现缺口均有通过的针对性测试，完整项目与实际生成验证已完成。
3. **解耦与可维护性**：源侧 padding 决策仍在 Parser；Expander 不引用 Parser 或 JSON。公共 Patch 模型、Core/Editor 边界、字体/渲染/尺寸估算链不外扩。该阶段不承担 PR-07 的全树索引/身份歧义改造，也不顺带平台化路径系统。
4. **剩余风险**：保留上表两项用户已批准的视觉变化；本轮三项发现没有未闭环项。没有以平均分代替逐 case 检查，没有用新增 case 特判换取成绩。

247 个冻结输入/参考/登记文件与早期受保护清单哈希一致。单独的自动派生 `_admissible_best_latest.json` 曾被**上一轮 11:26 的脚本**刷新，不是本次续作产生的变化；本轮始终保持该进入状态并仅写 run-local 历史报告。冻结 `CURRENT.json`、`_baseline.json`、源文件、参考图均未移动或更新。此差异单列于 `global-history-audit.json`，不声称旧 248 项清单全部逐字节未变。

本轮第一组四例共用诊断进程在正常产物和 `LogExit: Exiting.` 后返回 `3221225477`；随后四例各自独立进程复核全部 exit 0。没有以首次产物成功推断进程退出成功，也没有改引擎规避该退出行为。

### 历史记录与交接

初始 79/81 的完整运行、原始单跑和只读审查仍保留在 `Saved/PR01/`、`Saved/PR01/quality-review-20260917/` 及原 Automation run。曾出现 66/81 的扩大字体/布局实验已撤回；前次三边界收口尝试的 75/81 也不是最终版本，五项异常在去掉优先级改动后仍独占复现，最终定位为扩大 padding 重算取消了继承的 runtime inset，已收缩修正。

当前 normalized tracked Source diff SHA-256：
`921d4cb032620867933e550328bee14214b32c1f9997cb5018beafc399956fda`。
新增测试文件与头文件也包含在 `verified-source.json` 的逐文件指纹中；不能只以 tracked diff 判断全部源码身份。

本次收口相对原 79/81 的源码改动仅为：`Design2UMGCore/Private/PenParser.cpp`、`PenParserNodeProperties.cpp`、`RefExpander.cpp`、`Tests/PenOverrideTests.cpp`。整个 PR-01 仍为原来的 14 个新增/修改源码及测试文件，完整清单见旧 `verification-evidence.json` 与当前 `final-source.diff`。

**下一步：PR-01 已验收封板；PR-02 尚未开始。**

---

# PR-02：Reimport 破坏性替换改为 Prepare → Validate → Commit

**状态**：`NOT_STARTED`

**优先级**：P0 / 发布前必须完成

## 问题

KindChanged 等路径存在“先删除旧 D2U-owned subtree，再创建新 subtree”的顺序。`CreateWidgetRecursive()` 明确可能失败，因此存在旧状态已破坏、创建失败但流程仍继续的风险。

主要相关位置：

- `Source/Design2UMGEditor/Private/DesignImportReimport.cpp`
- `Source/Design2UMGEditor/Private/DesignImportReimportStages.cpp`
- `Source/Design2UMGEditor/Private/DesignImportReimportApply.cpp`
- `Source/Design2UMGEditor/Private/DesignImportReimportInternal.h`
- `Source/Design2UMGEditor/Private/UMGWidgetUtils.cpp`

## 目标设计

破坏性操作统一遵循：

`Prepare → Validate → Commit`

最低要求：新结构未成功构造并验证之前，不销毁旧结构。

不要求新建通用 staging framework 或 command framework。

## 行动项

- [ ] 明确 Reimport plan 中哪些操作是 `PropertyUpdate / Move / Add / Remove / Replace`。
- [ ] 对 `Replace / KindChanged` 建立可验证的新结构准备步骤。
- [ ] 新节点创建失败时旧 subtree 必须仍存在。
- [ ] attach/parent 失败时不得留下 orphan 或假成功。
- [ ] 有完整性影响的 stage 不再使用无法传播失败的 `void` 语义。
- [ ] 上层 `bSucceeded` 不再仅表示“主流程执行到底”。
- [ ] 不削弱现有 project-side ownership 保护。

## 故障注入测试

- [ ] KindChanged replacement 创建失败，旧 subtree 保留。
- [ ] 创建失败时没有 `kind_changed` 成功诊断。
- [ ] Parent attach 失败，旧状态保留或明确恢复。
- [ ] Mapped class load failure 不触发破坏性替换。
- [ ] 失败后 manifest / ownership 数据不记录不存在的新节点。

## 完成条件

- [ ] 不存在“旧结构已经删除，而新结构尚未确认可用”的主路径。
- [ ] destructive stage 的失败可以传播到最终结果。
- [ ] SafeReimport QA 与新增故障注入测试通过。
- [ ] 81-case `no_regression` 无退步。

## 会话记录

尚无。

---

# PR-03：统一 Import / Reimport / Save 结果状态

**状态**：`NOT_STARTED`

**优先级**：P0 / 发布前必须完成

## 问题

当前不同路径的 `false` / `bSucceeded=false` 可能代表完全不同的实际状态：

- 未保存且旧状态恢复。
- 未保存但内存修改仍然存在。
- Asset 已保存但 manifest 未写入。
- 批量保存失败但文件恢复成功。
- 批量保存失败且恢复不完整。

调用者部分依赖扫描 diagnostics 来重新推断底层真实状态。

主要相关位置：

- `Source/Design2UMGEditor/Private/DesignImportPipeline.h`
- `Source/Design2UMGEditor/Private/DesignImportPipeline.cpp`
- `Source/Design2UMGEditor/Private/DesignImportReimportPersist.cpp`
- `Source/Design2UMGEditor/Private/UMGGenerator.h`
- `Source/Design2UMGEditor/Private/UMGGenerator.cpp`
- `Source/Design2UMGEditor/Private/ComponentBlueprintBatchSave.cpp`

## 目标设计

内部结果必须直接表达真实提交状态，而不是要求上层解析日志或 diagnostic kind 猜状态。

建议至少能够区分：

- `Succeeded`
- `SucceededWithDegradation`
- `NotCommitted`
- `CommittedWithMetadataFailure`
- `RollbackIncomplete`
- `Conflict`

具体命名可调整，但必须能回答：

- Asset 是否保存？
- Manifest 是否保存？
- 内存状态是否恢复？
- 文件状态是否恢复？
- 恢复是否完整？

## 行动项

- [ ] 设计最小统一 result 状态，不引入复杂状态机框架。
- [ ] `SaveWidgetBlueprint` 返回足够的信息，上层不再通过扫描 diagnostics 重构保存状态。
- [ ] 检查 `UndoTransaction()` 返回值；失败时不得声称 rollback 成功。
- [ ] 复用 `ComponentBlueprintBatchSave` 已有恢复理念，不复制第二套 recovery subsystem。
- [ ] 保留必要的 `bSucceeded` 向后兼容时，明确其映射规则。
- [ ] 所有调用方按显式状态处理 partial commit。

## 完成条件

- [ ] 任何保存失败路径均能从 Result 直接知道磁盘、内存、manifest 状态。
- [ ] 不再依赖 diagnostic string/kind 判断“到底保存了什么”。
- [ ] Undo 失败有独立状态与测试。
- [ ] 现有 manifest write failure、read-only、save failure、batch recovery 测试通过。
- [ ] 81-case `no_regression` 无退步。

## 会话记录

尚无。

---

# PR-04：Degradation / Warning / Partial Sync 诊断闭环

**状态**：`NOT_STARTED`

**优先级**：P0 / 发布前必须完成

## 问题

部分底层已经收集 warning，但上层丢弃；部分有意保守行为会让导入结果并未完全同步，却仍容易表现成普通成功。

典型问题：

- Prototype wiring 的 `WiringWarnings` 被收集后未进入最终导入结果。
- Nested design deletion 当前有意保留旧 nested widget，但没有把“不完整同步”作为正式状态表达。

主要相关位置：

- `Source/Design2UMGEditor/Private/Design2UMGPrototypeGraphWiring.cpp`
- `Source/Design2UMGEditor/Private/UMGGenerator.cpp`
- `Source/Design2UMGEditor/Private/DesignImportReimportStages.cpp`
- `Source/Design2UMGEditor/Private/DesignImportDiagnosticReport.cpp`

## 行动项

- [ ] Prototype wiring warnings 接入已有生成/导入诊断出口。
- [ ] 不支持或跳过的交互必须可在最终结果中看到。
- [ ] Nested deletion 保守保留时产生结构化 partial-sync diagnostic。
- [ ] 明确 `Full Success / Success With Degradation / Partial Commit / Failure` 的含义。
- [ ] 不因 unsupported feature 直接把所有导入都视为失败；区分降级与数据完整性失败。
- [ ] 不新增第二套 warning 系统，优先复用/小幅泛化已有 diagnostics。

## 完成条件

- [ ] 底层收集的 warning 不存在“调用后直接丢弃”的关键路径。
- [ ] Nested removal 保留行为不再是静默完全成功。
- [ ] Diagnostic report 能反映最终真实同步状态。
- [ ] 相关 import/reimport/prototype 测试通过。
- [ ] 81-case `no_regression` 无退步。

## 会话记录

尚无。

---

# PR-05：删除确定无效复杂度并修严输入解析

**状态**：`NOT_STARTED`

**优先级**：P0 / 发布前必须完成

## 目标

这一阶段只做**有明确证据的减法和边界修正**，不借机大重构。

## 行动项 A：删除无效 `bFitContentPrecomputed`

主要位置：

- `Source/Design2UMGCore/Private/Design2UMGConverterInternal.h`
- `Source/Design2UMGCore/Private/Design2UMGConverterFlowChildPreResolution.cpp`

- [ ] 重新确认该字段仍只被写入、不影响业务决策。
- [ ] 删除字段、`MarkFitContentPrecomputed()` 及只为维护该状态存在的调用。
- [ ] 保留有实际作用的 `ExpansionCache / SnapshotAliases / ThemeSignature`。
- [ ] 运行 ref expansion、fit-content、flow、wrap/grid 相关回归。

## 行动项 B：简化怪异控制流

- [ ] 将 `UMGJsonParser.cpp` 中空 `if` + IIFE side-effect lambda 改为普通清晰控制流。
- [ ] 搜索类似无收益“聪明代码”，只清理有明确可读性收益且无行为变化的实例。

## 行动项 C：TryParse 真正验证输入

主要位置：

- `Source/Design2UMGCore/Private/UMGWidgetNodeAccess.cpp`
- `Source/Design2UMGEditor/Private/UMGJsonParser.cpp`

- [ ] `TryParseImageTransform` 检查每个 component 真正成功解析。
- [ ] 拒绝 NaN / Inf 等非有限值。
- [ ] Corner radii 等相似手工数字解析执行同样审计。
- [ ] 坏输入返回失败并进入现有 diagnostic/log 规则，不静默转成零。

## 完成条件

- [ ] `bFitContentPrecomputed` 及其维护路径完全删除。
- [ ] 不存在已知空 `if` + 副作用 lambda 控制流。
- [ ] `TryParse*` 的返回值真正代表完整解析成功。
- [ ] 相关单元测试和全量正式回归无退步。

## 会话记录

尚无。

---

# PR-06：Typed Payload 与内部 JSON/String 胶水收敛

**状态**：`NOT_STARTED`

**优先级**：P1 / 建议完成

## 原则

JSON/String 允许存在于明确的：

- 输入边界
- 输出边界
- 用户可扩展开放参数
- 经用户同意保留的兼容边界

核心进程内阶段传递优先使用 typed data。

主要相关位置：

- `UMGSchemaTypedPayloads.h`
- `UMGWidgetNodeAccess.*`
- `UMGWidgetNodeProceduralResourceAccess.*`
- `Design2UMGConverterVisualSupport.cpp`
- `ProceduralShapeGenerator.cpp`
- `PreBuildResourceNormalizer.cpp`
- `UMGJsonParser.cpp`
- `UMGJsonSerializer.cpp`

## 行动项

- [ ] 盘点内部仍在“拼 JSON → 后续再解析”的固定协议。
- [ ] 优先迁移 placeholder / 固定 shape 参数等内部闭合协议为 typed payload。
- [ ] 不消灭所有 `MaterialParams`；开放式用户 material 参数可保留字符串/JSON。
- [ ] 明确 legacy/canonical 字段唯一归一化位置。
- [ ] 下游不再同时判断 legacy owner 与 canonical owner。
- [ ] 未经用户批准，不新增旧字段兼容；发现现存兼容时先记录，不擅自扩展。

## 完成条件

- [ ] 核心内部固定语义不依赖不必要的 JSON round-trip。
- [ ] Canonical representation 进入内部主路径后保持唯一。
- [ ] Serializer/Parser 的 round-trip 测试与视觉回归无退步。

## 会话记录

尚无。

---

# PR-07：单次树索引与身份歧义检测

**状态**：`NOT_STARTED`

**优先级**：P1 / 建议完成

## 目标

只优化结构上明确重复的树扫描，不做未经 profiling 的大性能改造。

主要相关位置：

- `Source/Design2UMGCore/Private/DesignSchema.cpp`
- `Source/Design2UMGEditor/Private/DesignImportReimportPersist.cpp`
- `Source/Design2UMGEditor/Private/DesignImportReimportStages.cpp`

## 行动项

- [ ] 对单次 conversion/reimport 构建必要的 `DesignNodeId / StableDesignNodeId / WidgetName` 索引。
- [ ] 用索引替代 manifest records × root DFS 等明显重复扫描。
- [ ] 重复 stable identity 不得静默覆盖，必须诊断或拒绝歧义。
- [ ] 明确组件实例、wrapper、document scope 下的 identity 规则，禁止简单裸 ID 一刀切。
- [ ] 不引入 arena、intrusive tree、自定义容器等无证据优化。

## 完成条件

- [ ] 关键 plan 构建不再按每条记录重复 DFS 根树。
- [ ] 重复身份有明确处理与测试。
- [ ] 行为、结构快照和 81-case 回归无退步。

## 会话记录

尚无。

---

# PR-08：Core / Editor 小切口解耦

**状态**：`NOT_STARTED`

**优先级**：P1 / 建议完成

## 目标

不是追求 Core 零 UE 依赖，而是减少转换语义对不必要编辑器全局状态的直接依赖。

主要关注：

- `Design2UMGCore.Build.cs`
- `FontResolver.*`
- `Design2UMGNotification.*`
- `TextMeasurement*`
- `Design2UMGConverter*`

## 行动项

- [ ] 审计 `AssetRegistry / LoadObject<UFont> / Notification / UDesign2UMGSettings::Get() / renderer probe` 的调用边界。
- [ ] 已有 settings snapshot 能承载的数据，不在深层重新读全局 settings。
- [ ] 检查 Core 的 `UnrealEd` 依赖是否有可以零/低复杂度迁往 Editor 的真实使用点。
- [ ] 如果迁移需要大量 facade/adapter 或复制代码，则保留现状并在本阶段记录理由。
- [ ] 不为了 Build.cs 漂亮制造新抽象。

## 完成条件

- [ ] 转换核心的外部副作用入口数量减少或至少集中清晰。
- [ ] Core/Editor 边界由真实依赖决定，不是纯目录美化。
- [ ] Build、automation、visual fidelity 无退步。

## 会话记录

尚无。

---

# PR-09：测试体系分层与低价值测试整理

**状态**：`NOT_STARTED`

**优先级**：P1 / 建议完成

## 目标

测试失败时应尽量指向一个有业务意义的 contract，而不是大量重复验证容器、文件位置或实现细节。

## 目标分层

### L1：纯语义 / 低成本单元测试

- Parser presence
- Override Patch
- Variable resolution
- Ref expansion
- Layout calculation
- Typed payload conversion

### L2：UE 对象集成测试

- WidgetTree generation
- Slot / property application
- Mapped component
- Prototype wiring
- Resource binding

### L3：持久化 / Reimport 故障测试

- Compile failure
- Save failure
- Manifest failure
- Read-only
- Undo failure
- Replacement construction failure
- Mapping class missing
- Nested partial sync

### L4：端到端 Golden / Visual

- Pen
- Figma
- Reference PNG
- Structure snapshot
- Determinism
- Fresh-process reload

## 行动项

- [ ] 标记仅测试 `TOptional` 等基础容器行为的低价值 case。
- [ ] 文件 Public/Private 位置类测试统一归为 architecture guard。
- [ ] 大量重复 fixture/helper 做最小合并，不为了“测试框架化”过度设计。
- [ ] 模拟 HTTP 只替换 transport，尽量复用真实响应处理路径，避免模拟分支复制一套业务逻辑。
- [ ] 删除任何测试前先确认不会削弱正式 81-case / no_regression 保障。

## 完成条件

- [ ] 关键 failure path 有真实故障注入。
- [ ] 测试层级和职责清楚。
- [ ] 删除/合并测试不减少关键行为覆盖。
- [ ] 正式 full gate 无退步。

## 会话记录

尚无。

---

# PR-10：大文件按真实职责边界局部拆分

**状态**：`NOT_STARTED`

**优先级**：P2 / 最后做

## 原则

文件大不是自动缺陷。只有当一个文件内部已经存在稳定职责边界、拆分后能降低认知负担且不增加抽象层时才拆。

重点候选：

- `Design2UMGPrototypeGraphWiring.cpp`
- `VisualMapper.cpp`
- `FontResolver.cpp`
- `DesignRemoteImageImporter.cpp`

## 可接受的现有职责边界示例

Prototype Graph Wiring：

- binding classification
- plan resolution
- graph target preparation
- mouse wiring
- keyboard wiring
- cleanup

Font Resolver：

- normalization / aliases
- asset discovery
- resolution policy
- icon glyph registry
- typeface selection

Visual Mapper：

- base visual mapping
- fill
- stroke
- shadow/effect wrappers

## 行动项

- [ ] 对候选大文件先画函数职责图，不直接按行数拆。
- [ ] 只迁移已经存在的职责簇，不新建无意义 manager/service。
- [ ] 公共入口尽量保持稳定。
- [ ] 拆分后删除旧重复 helper，不形成转发层叠罗汉。

## 完成条件

- [ ] 每次拆分都能明确说出认知负担下降在哪里。
- [ ] 净抽象层不增加或有充分理由。
- [ ] 编译、测试、结构与视觉回归无退步。

## 会话记录

尚无。

---

# PR-11：Release Hardening 与最终质量门禁

**状态**：`NOT_STARTED`

**优先级**：P0 / 发布前必须完成

## Gate A：静态人工审计

对以下关键字逐项核实，不要求机械消灭：

- [ ] `TODO`
- [ ] `FIXME`
- [ ] `Known limitation`
- [ ] `legacy`
- [ ] `fallback`
- [ ] `silent`
- [ ] `return nullptr`
- [ ] `UE_LOG(Error`
- [ ] `UE_LOG(Warning`
- [ ] `FindChecked`
- [ ] `CastChecked`
- [ ] `LoadObject`
- [ ] `Delete / Remove / Rename / Replace / Save / Write`

每个破坏性路径都必须能回答：

1. 失败后是什么状态？
2. 谁负责恢复？
3. 调用者如何知道？

## Gate B：禁止新的 sentinel-presence

- [ ] 新代码不得用 `false / 0 / "" / enum default` 表示 unset，除非是正式协议且有测试。
- [ ] Patch/Override 全部走显式 presence 语义。

## Gate C：源码质量自审

- [ ] 新抽象是否让旧逻辑真正消失？
- [ ] 是否仍存在同一状态的双重 owner？
- [ ] 是否仍有 warning 被收集后丢弃？
- [ ] 是否仍有调用者通过日志/字符串反推内部状态？
- [ ] 是否新增了无当前需求支撑的防御或兼容分支？
- [ ] 是否存在明显可以删除而功能不变的状态/helper？

## Gate D：实际验证

遵守 `AGENTS.md` 中真实项目、81-case baseline、reference cache 和 no_regression 规则。

至少完成：

- [ ] Clean build。
- [ ] Core automation。
- [ ] Editor automation。
- [ ] SafeReimport QA。
- [ ] Component Mapping QA。
- [ ] Prototype tests。
- [ ] Render tests。
- [ ] Determinism tests。
- [ ] Fresh-process reload gate。
- [ ] FigmaExporter Node tests。
- [ ] Python verifier selftests。
- [ ] 正式 81-case discovered set full-run。
- [ ] 对 `CURRENT.json` 当前 sealing baseline 执行逐 case `no_regression`。
- [ ] 任意异常 shard 分数按规则用 `editor-isolation case` 独占复核。

## 最终完成条件

- [ ] PR-01 ～ PR-05 全部 `VERIFIED`。
- [ ] 本阶段所有发布门禁通过。
- [ ] 没有已知 P0/P1 正确性问题未处理。
- [ ] 没有通过 case/font/node/file 定向特判换成绩。
- [ ] 没有因代码“更干净”接受正式同身份回归退分。
- [ ] 发布结论明确记录实际验证范围，不把静态检查说成运行验证。

## 会话记录

尚无。

---

# 3. Complexity Contract（整个改造期间强制遵守）

## 3.1 新抽象必须替代旧复杂度

如果新增 `FDesignNodeOverride`，旧 sentinel override 逻辑必须退出。

如果新增统一 save result，调用者扫描 diagnostics 判断保存状态的逻辑必须退出。

**禁止“为了安全”把新旧两套逻辑永久同时保留。**

## 3.2 优先删除、收口、复用

新增代码前依次问：

1. 能否直接删除无效状态？
2. 能否收口已有职责？
3. 能否复用已有结果/诊断/恢复机制？
4. 只有前三者都不满足时才新增抽象。

## 3.3 禁止借 review 建平台

没有当前需求和源码事实支撑时，不新增：

- Generic Pipeline Framework
- Generic Transaction Engine
- Generic Graph Framework
- Universal Property System
- 第二套 Diagnostics Framework
- 第二套 Recovery Framework

## 3.4 注释只解释不可见约束

保留真正必要的 UE 陷阱说明，例如：

- 为什么 `FScopedTransaction::Cancel` 不等于 rollback。
- 为什么 skeleton refresh 后不能继续使用旧 property pointer。
- 为什么某些资源必须在特定生命周期释放。

如果一个长注释主要是在解释“这个布尔值为什么有多个含义”，优先修数据模型，而不是继续扩注释。

---

# 4. 最终目标状态

## 数据模型

- [ ] Full State 与 Patch State 明确分离。
- [ ] 核心语义不存在默认值冒充“未设置”。

## Reimport

- [ ] 所有 destructive change 均不存在 destroy-before-create 的未闭环路径。
- [ ] 用户/项目侧内容 ownership 保护保持有效。

## 持久化

- [ ] 调用者可以直接知道 Asset、Manifest、Memory、Rollback 的真实状态。
- [ ] Partial commit 与 rollback incomplete 有明确状态。

## 诊断

- [ ] 所有重要 degradation 都有统一出口。
- [ ] 不存在底层收集 warning、上层无意丢弃。

## 复杂度

- [ ] 没有已知无效状态。
- [ ] 内部 canonical representation 唯一。
- [ ] 没有为了兼容而长期维护双重 owner。
- [ ] 没有明显可以删除而行为不变的控制流仪式。

## 架构

- [ ] `Design IR → UMG IR → UE Materialization` 仍然清晰。
- [ ] 没有因为本轮优化引入过度抽象。

## 测试

- [ ] 关键正确性、失败恢复、持久化一致性都有行为测试。
- [ ] 正式 81-case `no_regression` 保持零退步。

---

# 5. 变更日志 / 会话交接

> 每个开发会话完成后在这里追加一条。不要删除历史记录；如果前一条结论后来被推翻，追加更正记录。

| 日期 | AI/会话 | 阶段 | 状态变化 | Commit | 验证/备注 |
|---|---|---|---|---|---|
| 2026-09-17 | Initial planning | 全局 | 创建计划，所有阶段 `NOT_STARTED` | `5992f114` baseline | 基于源码 review 形成；尚未实施改造 |

---

# 6. 当前下一步

**下一阶段：PR-01 — 修复 Design IR Patch / Override 语义。**

开始实现前先重新核实当前 HEAD 与以下源码：

- `Source/Design2UMGCore/Public/DesignSchema.h`
- `Source/Design2UMGCore/Private/PenParserNodeExtensions.cpp`
- `Source/Design2UMGCore/Private/PenParserNodeProperties.cpp`
- `Source/Design2UMGCore/Private/RefExpander.cpp`
- 对应 Pen / Ref / Override tests

第一阶段的核心验收不是“新增了一个 Patch struct”，而是：

> **默认值、false、0、空字符串都可以作为合法显式 override，同时缺失字段绝不会误覆盖基础值。**
