# 根目录 Review 报告当前真相回应

> 日期：2026-05-17
> 范围：`CODE_REVIEW_REPORT.md`、`code_review.md`、`PROJECT_IMPROVEMENT_REVIEW.md`
> 原则：review 报告只能当线索，不能当事实源。下面结论以当前仓库和 UE 5.7 引擎源码抽查为准。

## 简短结论

三份报告对项目大方向的判断基本靠谱：整体架构是强的，开源准备度也明显高于普通个人 UE 插件。后续真正能继续抬项目水平的地方，主要是可维护性、诊断质量和验证纪律。

但里面也有一些意见已经过时，或者范围太大。Phase 26 不能盲目拆大文件，不能把所有 `bHas*` 都机械改成 `TOptional`，不能新增旧兼容迁移/版本链，也不能重新引入 HostProject 或参考图兜底路径。

## 当前证据快照

| 主题 | 当前证据 | 回应 |
| --- | --- | --- |
| Converter 体量 | `Source/Design2UMGCore/Private/Design2UMGConverter.cpp` 当前 709 行 | 是真实热点，但已经有部分拆分。后续必须降低主 owner 负担，不能只是搬代码。 |
| Parser 热点 | `FigmaParserInternal.cpp` 当前 912 行；`PenParser.cpp` 当前 690 行 | 是真实 contributor cost。适合后续单独 phase；Phase 26 只收窄到已核实的重复 helper。 |
| 测试 | 当前已有 `Source/Design2UMGCore/Private/Tests` 和 `Source/Design2UMGEditor/Private/Tests` | “没有 C++ 测试”的说法已过时。覆盖还可以继续增强，但不是从零开始。 |
| `TOptional` | UE 5.7 有 `Runtime/Core/Public/Misc/Optional.h`；插件也已经大量使用 `TOptional<T>` | 只接受适合表达 presence 的结构字段。局部 `bHas*` probe 和运行时决策 flag 不应为了统一命名而 churn。 |
| Node factory switch | `Design2UMGNodeWidgetFactory::CreateWidgetForNodeType` 当前已有 `default` 分支 | “缺 default”这条已过时。空节点诊断是否更清楚，可以另审。 |
| TextMeasurement 缓存 | 当前 TextMeasurement cache 和 FontResolver cache 路径里有 `FCriticalSection` / `FScopeLock` | “缓存不安全”不能按原文接受。若还有并发问题，需要更窄的复现。 |
| HostProject fallback | `Tools/design2umg_auto_verify*.py` 和 PowerShell 验证 helper 已明确拒绝 HostProject fallback | 不允许加回来。未来验证仍必须使用 `G:\UE5\FPSRoguelike\FPSRoguelike.uproject`。 |
| JSON parser 诊断 | Figma JSON 解析失败现在保留 `Invalid Figma export JSON` 主错误，并补充 `Reader error: ...` 细节；真实项目编译和 focused malformed JSON automation 已通过 | 已随本批 parser/helper 收敛一起完成 formal closeout。 |

## Phase 26 接受项

下面这些足够小、足够当前，并且符合项目红线：

1. 根目录 review 回应 / intake ledger
   保留本文档作为当前真相回应，并用 Phase 26 OpenSpec 固化后续实现范围，避免照抄 stale review 文案。

2. 小范围重复 helper 清理
   当前证据能看到几类重复：
   - `SetStringFieldIfPresent` in Figma parser code.
   - `TryParsePointXY` in Pen parser code.
   - `HasVisibleFill` in converter visual support and shared core semantics.
   - `ShouldWrapChildWithSizeBox` behavior mirrored between production and tests.

这些应该一个语义一个语义地处理，用 focused tests 或直接 before/after 证据证明没有行为变化。

   Phase 26 只声明已处理当前纳入 intake 的条目。后续核查发现还有遗漏 review 候选项，因此新增 `phase-26b-review-intake-backfill-and-current-truth-recheck` 专门补录和核实；这些遗漏项不算 Phase 26 封板时已经暗含完成。

3. 小范围 optional presence 审计
   `TOptional` 在 UE 5.7 里是正常基础类型，插件里也已经大量使用。Phase 26 可以审剩余 `bHas*`，但只迁移真实 value-presence contract。不要为了命名好看去动局部控制流。

4. Figma JSON 诊断细化
   这项本批已经实现：当前失败日志保留原来的 “Invalid Figma export JSON”，同时附带 JSON reader 的原始错误消息。这能提高定位效率，又不改变转换语义。

## 本批落地结果

这批真正落地的生产改动有 4 个，范围都压在已确认的 owner boundary 里：

1. `FigmaParserInternal.h/.cpp` 承接唯一的 parser private `SetStringFieldIfPresent` helper，`FigmaParserLayout.cpp` 和实例跟踪路径复用它。
2. `PenParserInternal.h` 承接唯一的 Pen parser private `TryParsePointXY` helper，`PenParser.cpp` 和 `PenParserStyling.cpp` 的重复实现删除。
3. `SharedCoreSemantics.h/.cpp` 承接唯一的 `FDesignFill` 版 `HasVisibleFill`，`Design2UMGConverterVisualSupport.cpp` 改为复用；`FigmaParserLayoutBounds.cpp` 的 `FJsonObject` parser 版本继续保留原边界。
4. `FigmaParserInternal.cpp` 在 malformed JSON 失败路径里把 `Reader->GetErrorMessage()` 带进日志，继续不改 parser 接受/拒绝逻辑，也不引入 fallback。

对应验证已经收完整：

- 真实项目 `G:\UE5\FPSRoguelike\FPSRoguelike.uproject` 编译通过。
- focused automation `Design2UMG.Unit.FigmaParser.PrivateStringFieldParity`、`Design2UMG.Unit.FigmaParser.ErrorInput`、`Design2UMG.FigmaParser.MalformedJsonFailsCleanly`、`Design2UMG.Unit.PenParser.PointParsingParity`、`Design2UMG.SharedSemantics.VisibleFillOwnerBoundary` 通过。
- formal 47-case run `Saved/Design2UMG/Automation/_formal_full_47_phase26_engine_aligned_20260517_codex` 通过，`summary.no_regression.available = 47`、`summary.no_regression.failed = 0`。
- 同一 run 的 `validation_against_historical_best.json` 无 regressions、无 missing admissible cases。

5. 验证口径继续收紧
   真实项目、formal gate、historical-best、无 HostProject、无参考图兜底、无平均分遮单点退步，这些规则继续写清楚。

## 接受，但放到后续 phase

这些问题真实或有可能真实，但对 Phase 26 来说太大：

1. 完整 parser / hotspot 文件拆分
   `FigmaParserInternal.cpp`、`PenParser.cpp`、`LayoutFixer.cpp`、`Design2UMGTextWidgetBuilder.cpp`、TextMeasurement 内部，都值得做 ownership 梳理。但这应该单独开 phase，并配自己的 focused manifest。

2. TextMeasurement 子系统重组
   这个子系统复杂有领域原因。目录化或 facade 可能有价值，但任何字体/测量规则变化都可能影响视觉分数，必须隔离验证。

3. Runtime 模块化或模块改名
   `Design2UMGCore` 当前是 Editor module，而且文档里已有解释。改成 Runtime 是产品/API 决策，不是 review cleanup。

4. Pencil MCP 分步诊断和 Figma 对话框 UX
   有价值，但属于用户流程改造，不该和 parser/converter 清理混在一批。

5. Public API doxygen 和贡献者文档润色
   可以做，但优先级低于 current-truth cleanup 和已核实重复语义收敛。

## 按原文拒绝或过时

1. “没有 C++ automation tests”
   过时。当前 Core 和 Editor private test 目录都有 C++ automation tests。

2. “`CreateWidgetForNodeType` 缺 default”
   过时。当前实现已有 `default` 分支。

3. “TextMeasurement cache 整体不安全”
   没有更窄复现前不接受。当前 cache 代码有锁。

4. “新增版本化 legacy settings migration”
   Phase 26 拒绝。项目红线禁止未经明确同意扩旧兼容/迁移链。

5. “按字体族外部化字体校准配置”
   Phase 26 拒绝。这很容易变成字体名特判，除非重新设计成通用测量规则并跑完整 formal 证明。

6. “保留或使用 HostProject fallback 作为便利路径”
   拒绝。当前工具已经拒绝它，仓库规则也禁止它。

7. 带 `Source/Design2UMG/Source/...` 的 review 路径
   当作 stale path 处理。当前路径在 `Source/Design2UMGCore/...` 和 `Source/Design2UMGEditor/...` 下。

## Phase 26 建议范围

对应 OpenSpec change：

`openspec/changes/phase-26-engine-aligned-review-response-and-diagnostics/`

这轮应保持小而硬：

- current-truth review intake；
- optional-presence 分类；
- 重复 helper 一个语义一个语义收敛；
- 小范围诊断清晰度改进；
- 验证证据和 touched surface 匹配。

更大的事要写成 later-phase，不要偷偷塞进 Phase 26；这次真正已经做进来的，就是上面 4 个 current-truth 已踩实的小项。Phase 26 封板后，遗漏 review 候选只通过 `phase-26b-review-intake-backfill-and-current-truth-recheck` 继续处理，不能再假设还有 Phase 27 承接。

## 根据后续独立反馈补强

后续两份 AI 反馈指出了几个合理缺口，Phase 26 文档已同步收紧：

1. 五分类必须进 spec
   不能只说“接受/延后/拒绝”。Phase 26 intake 现在固定使用 `phase26-implementation`、`later-phase`、`false-or-stale`、`current-contract`、`red-line-excluded` 五类。

2. `current-truth-review-intake.md` 必须有最小结构
   每条记录至少写来源报告、review 主张、当前证据、五分类结论、理由、对应 task、验证门槛、是否触碰 Non-Goals 或红线。

3. Non-Goals 必须可检查
   如果某项实现需要 Runtime 模块化、完整 parser 拆分、TextMeasurement 大重组、旧兼容扩张、HostProject、参考图兜底、样例特判或引擎源码修改，就不能算 Phase 26 实现项。

4. “重复 helper”必须先判定是不是同一语义
   `HasVisibleFill` 和 `ShouldWrapChildWithSizeBox` 这类候选项不能按名字或代码形状直接合并。source parser、shared core、UMG runtime、validation、测试镜像可能各有边界。Phase 26 现在要求先比较输入、默认值、边界行为、日志/错误行为和验证目标。

5. 测试镜像不能为了去重削弱独立验证
   如果测试 helper 是故意独立表达预期行为，就不能直接共用生产 helper 来“消除重复”。否则测试会变成生产实现自证正确。

## 本轮独立核实结果

这次我重新查了当前仓库和 UE 5.7 源码，结论如下：

| 核实项 | 当前证据 | 结论 |
| --- | --- | --- |
| Converter 行数 | `Source/Design2UMGCore/Private/Design2UMGConverter.cpp` 当前 709 行 | 已核实，按 709 行记录。 |
| Figma parser 行数 | `Source/Design2UMGCore/Private/FigmaParserInternal.cpp` 当前 912 行 | 已核实，按 912 行记录。 |
| 其他热点行数 | `PenParser.cpp` 690 行，`LayoutFixer.cpp` 698 行，`Design2UMGTextWidgetBuilder.cpp` 616 行 | 这些是当前真实 hotspot，但是否拆分要另做 owner-boundary 设计。 |
| C++ automation tests | `Source/Design2UMGCore/Private/Tests` 和 `Source/Design2UMGEditor/Private/Tests` 下共 28 个 `.cpp/.h` 测试/测试辅助文件 | “没有 C++ 测试”是过时或错误结论；测试质量和覆盖率仍可单独审。 |
| `CreateWidgetForNodeType` default | `Design2UMGNodeWidgetFactory.cpp` 当前有 `default: Result.Widget = CreateContainerWidget(Node); return Result;` | “缺 default”过时；但 default 是否应增加 warning/ensure 可以作为低风险诊断项另审。 |
| TextMeasurement cache 锁 | `TextMeasurementInternal.h` 有 `FCriticalSection Mutex`；`TextMeasurementCache.cpp` 的 get/store/reset/stats 路径使用 `FScopeLock` | “cache map 无锁不安全”不成立；若要继续讨论并发语义，需要单独复现和更窄问题描述。 |
| FontResolver 锁 | `FontResolver.cpp` 有 `GFontResolverCacheMutex`、`GFontIndexMutex` 和多处 `FScopeLock` | 宽泛线程安全指控不能直接采纳。 |
| UE 5.7 `TOptional` | `Engine/Source/Runtime/Core/Public/Misc/Optional.h` 定义 `TOptional`，并在 Runtime 源码里广泛使用 | `TOptional` 可作为 presence 表达工具，但不能推出“所有 bHas 都该迁移”。 |
| Figma JSON 解析诊断 | `FigmaParserInternal.cpp` 失败路径现在保留主错误并附带 `Reader->GetErrorMessage()` | 该小诊断缺口已修复，行为只增诊断不增 fallback。 |
| `SetStringFieldIfPresent` | `FigmaParserInternal.h/.cpp` 现在承接唯一 parser private helper；`FigmaParserLayout.cpp` 和实例跟踪路径复用 | 已按 Figma parser private owner 收敛。 |
| `TryParsePointXY` | `PenParserInternal.h` 现在承接唯一 Pen parser private helper；`PenParser.cpp` 与 `PenParserStyling.cpp` 复用 | 已按 Pen parser private owner 收敛，并保住对象/数组输入、变量解析和失败吞吐。 |
| `HasVisibleFill` | `SharedCoreSemantics.h/.cpp` 现在承接唯一 `FDesignFill` 版 helper；`FigmaParserLayoutBounds.cpp` 的 `FJsonObject` 版本保持不动 | 已按 shared/core owner 收敛，并保留 parser/source 边界。 |
| `ShouldWrapChildWithSizeBox` | 生产私有逻辑在 `Design2UMGConverter.cpp`；测试镜像在 `Design2UMGCorePrivateHelperTests.cpp`，注释说明它是为了不打开生产-only seam 的独立镜像 | 不能直接合并。它更像故意保留的测试镜像，除非能证明共享 helper 不削弱测试独立性。 |
| `.clang-format` | 当前根目录未发现 `.clang-format` | 这是可考虑的工程化 polish，但不属于 Phase 26 的核心小而硬范围。 |
| `CHANGELOG.md` | 当前根目录存在 `CHANGELOG.md` | “无 CHANGELOG”是错误或过时结论。 |
