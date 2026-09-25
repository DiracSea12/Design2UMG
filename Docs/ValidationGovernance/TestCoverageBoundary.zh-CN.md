# 测试覆盖边界说明

公开的是按用户目录自动发现并运行用例的工具，不是维护者私有设计集，也没有固定case数量门槛。公共CI通过 `Tools/run_validation_selftests.py` 执行全部验证器自检，并运行进程隔离检查及FigmaExporter的Node测试；它们使用合成夹具，不需要私有稿件。空扫描记录0项执行，不表示视觉通过。真实UE和用户视觉集验证仍是独立范围。

English canonical: [TestCoverageBoundary.md](TestCoverageBoundary.md)

这个文档只回答三件事：

1. 现在仓库有哪些测试层级。
2. 各层级大概覆盖什么。
3. 哪些普通贡献者能跑，哪些必须是维护者环境。

它不是覆盖率宣传页，所以这里不会写虚构百分比，也不会把 public CI 包装成 Unreal 正式门禁。

## 当前 public CI 的真实情况

当前 tracked GitHub Actions workflow 是 `.github/workflows/build.yml`。

它现在只有两类 job，而且边界不同：

| Job | 触发方式 | 真正检查什么 | PR 默认会不会跑 |
| --- | --- | --- | --- |
| `repository-hygiene` | `push`、`pull_request`、`workflow_dispatch` | 必需文件、公开边界/文本卫生、全部验证器自检、进程隔离及FigmaExporter构建/Node测试 | 会 |
| `build-plugin` | 维护者 push、同仓库 PR、`workflow_dispatch` | Unreal `BuildPlugin` 编译、产物边界清理和复核、针对同 checkout 引擎安装副本的聚焦自动化、artifact 上传 | 仅同仓库 PR 会跑 |

`build-plugin` 依赖维护者提供的 self-hosted Windows UE runner、`UE_ROOT` 和 `DESIGN2UMG_RUNTIME_PROJECT`。外部 PR 不会在这台 runner 上执行代码。

完整 discovered admissible case set formal visual regression gate 目前不在 public GitHub-hosted PR 自动化里。

## 覆盖层级

按测试实际执行的边界分类，不按历史命名分类。这里的 L1–L4 表示测试依赖与成本，不替代视觉指标层级，也不改变正式门禁。Core 和 Editor 都有私有自动化测试；`Unit` 前缀本身不保证测试是纯函数测试。

| 层级 | 责任与代表性所有者 | 执行边界 |
| --- | --- | --- |
| L1：语义测试 | Parser presence、Pen override、变量解析、ref expansion、布局决策和 typed payload 转换。例如 Core 的 `PenOverrideTests.cpp`、`DesignTextLetterSpacingPresenceTests.cpp`、`Design2UMGCoreHelperContractTests.cpp`，以及 Editor 的数值解析和 JSON 契约用例。 | 验证输入输出语义，不创建资产或执行渲染。通过 UE automation 选择具体用例运行，不声称已有脱离引擎的独立测试程序。 |
| L2：UE 对象与服务集成 | WidgetTree、slot/property、mapped instance、prototype graph wiring、资源绑定。例如 `Design2UMGPrototypeGraphWiringTests.cpp`、`Design2UMGComponentBlueprintTests.cpp` 中的对象生成用例，以及 `Design2UMGHttpTests.cpp`。 | 使用真实 UE 对象或请求完成委托。HTTP 只替换网络传输，真实请求构造、响应校验、解析、取消和会话生命周期仍被执行；不证明线上服务可用性。 |
| L3：持久化与失败路径 | compile/save/manifest、只读文件、Undo、replacement 构造、缺失 mapping、nested partial sync。主要位于 `Design2UMGSaveSemanticsTests.cpp`、`Design2UMGCommitResultTests.cpp`、`Design2UMGReimportTests.*.cpp` 和 component 多资产回滚用例。 | 实际注入对应失败，同时断言返回结果和磁盘/对象状态。同一业务文件可能同时有 L2、L3，用例才是分类单位。 |
| L4：端到端 golden / visual | Pen/Figma 导入、参考与候选 PNG、结构对应、确定性以及独立新进程重载。入口为 `Design2UMG.AutomationRender`、`design2umg_auto_verify.py`、designer/structure snapshot 工具和 [ValidationProtocol.md](../ValidationProtocol.md) 中的维护者批量验证。 | 保持输入和参考身份，归档完整 admissible case set 与历史对比。单例渲染或同进程重载不等于完整门禁或新进程持久化验证。 |
| 架构守卫：独立于 L1–L4 | 源码可见性与依赖所有权。`Design2UMGArchitectureGuardTests.cpp` 承担 `Design2UMG.Architecture.TextMeasurementPrivateBoundary`。 | 检查声明的架构，不代表文本测量业务或视觉正确性。文件位置守卫需要源码检出。 |

交互式 editor workflow smoke 仍可检查示例导入、生成资产和预览，但不替代任何完整自动化层级。前述 public CI 与维护者执行边界不变。

### 测试价值与夹具边界

仅给独立 `TOptional` 赋值、读取 `IsSet` / `GetValue`、再 `Reset` 的用例属于低价值容器测试，应优先改为真实 parser、override、序列化或属性应用契约。不能因为测试使用 `TOptional` 就删除 presence 断言；显式零、false 和空值必须穿过真实消费者后仍保持正确语义。

**已标记的低价值用例**：`DesignNodeSemanticsTests.cpp` 中的 `Design2UMG.PublicSurface.GridOptionalStorage` 仅给布局 optional 赋值、读取，再 Reset row 字段。它只是存储 smoke，不是 grid positioning、parser presence 或架构边界的覆盖证据，也不再向其中扩展容器断言；实际语义应由 parser/converter 用例证明。以下业务覆盖不因保留这个用例而扩大。

`TextLetterSpacingPresenceExplicitZeroVsUnset`、`RefOverrideExplicitZeroLetterSpacingPresence`、typed 属性与 JSON 往返测试验证值和缺省状态。`SourceExtensionsRemainOptIn`、`UMGSourceExtensionsRemainOptIn` 验证只读访问不分配、显式修改才创建扩展的约定。

只抽取已经重复的夹具，不在测试中重新实现业务。`Design2UMGHttpTestTransport.h` 只提供作用域传输替身和原始 `IHttpResponse`；调用真实完成委托前先移除 pending request，允许回调继续发起请求。传输夹具必须比 client 先构造，响应状态、session、body 的业务处理留在生产代码。该夹具不提供真实网络、超时调度或并发覆盖。

### 失败注入证据索引

| 边界 | 具体用例与注入方式 | 必须观察到的结果 |
| --- | --- | --- |
| Blueprint 编译 | `Design2UMG.Generation.CompileFailurePreservesDisk`：在有效执行链中引用不存在的函数，执行真实 Kismet 编译。 | 编译错误、非成功提交、原资产字节不变、新失败资产不落盘。 |
| Save / manifest / 只读 | `Design2UMG.SafeReimport` 下的 `SaveFailureRollsBackAndKeepsManifest`、`ManifestWriteFailurePropagates`、`SaveWritabilityPrecheckBlocksHalfUpdate`；平台文件接口上的写入失败及真实文件只读标志。 | 原资产与 manifest 被保留，或明确报告部分提交；退出时恢复文件标志。 |
| Undo / finalize | `Design2UMG.CommitResult.ReimportUndoFailure`、`.ManifestFinalizeFailure`；测试事务缓冲区和平台文件移动失败。 | 恢复失败不能报告为成功，不能静默消费先前用户事务。 |
| Replacement / 缺失 mapping | `Design2UMG.SafeReimport.PrepareCommit.KindFailurePreservesSubtree`、`.AutomaticOverrideFailurePreservesAllSlots` 和 `Design2UMG.SafeReimport.MappingDiagnosticsSurfaced`；无效 widget type 或不可加载 class。 | 保留原 subtree、slot、manifest，诊断指明实际失败边界。 |
| Nested partial sync | `Design2UMG.SafeReimport.NestedAddAndRemovalBoundary`、`.NestedRemoveConflictNamesNode`、`.NestedMoveConflictCreatesNoCopy`；真实 nested owned/project 内容冲突。 | 不重复实例、不做不安全删除、不把部分更新伪装成完成，保留明确诊断。 |
| HTTP | `Design2UMG.Http` 下的 Figma/MCP 响应、取消、重连和生命周期用例；通过引擎 HTTP 请求接口提供测试传输。 | 验证回调次数、状态与响应处理、迟到响应抑制、启动失败清理和会话释放。 |

此表是可执行证据索引，不是覆盖率数字，也不代表任意一次运行都覆盖全部层级。每次改动应记录实际测试标识、日志与结果数量。

### 测试归属与筛选

HTTP 测试集中在 `Design2UMGHttpTests.cpp`。组件 REST 依赖用例位于 `Design2UMGComponentBlueprintTests.cpp`，共用测试传输。文本测量边界测试使用 `Design2UMG.Architecture.TextMeasurementPrivateBoundary`。

完整套件：`Automation RunTests Design2UMG`；架构边界：`Automation RunTests Design2UMG.Architecture`；HTTP：`Automation RunTests Design2UMG.Http`。

完整套件和 `Design2UMG.ComponentBlueprints` 都包含 `RealDesignLayouts`。请使用 `Tools/Run-Design2UMGAutomationTests.ps1 -Tests Design2UMG -RealDesignExportRoot <FigmaExportRoot>` 启动；根目录必须包含 `figma_game_party_home/design.json` 和 `figma_general_data_analytics/design.json`。缺少参数或输入时测试明确失败，不会静默跳过。

## 普通贡献者可以跑什么

普通贡献者可以现实复现的，主要是这些：

1. 正常 push / PR 时的 `repository-hygiene`。
2. 针对自己真实工程的本地 UnrealBuildTool 构建。
3. `Automation RunTests Design2UMG` 或它的聚焦子集。
4. 示例导入 smoke。
5. 显式传 project / editor 路径的 `Design2UMG.AutomationRender` 或 `design2umg_auto_verify.py` 本地复现。

贡献者文档里统一用这些占位符：

- `<UE5_Root>`
- `<PluginRoot>`
- `<ContributorProject.uproject>`

## 哪些必须是维护者环境

下面这些不该写成“普通 PR 已自动覆盖”：

1. GitHub Actions 中为维护者 push、同仓库 PR 和手动运行提供的 `build-plugin`。
2. self-hosted UE runner 产出的 canonical packaged plugin artifact。
3. 归档级 formal admissible case set release gate 和它的 `summary.no_regression` 证据链。
4. 绑定 maintainer admissible baseline 的 historical-best parity 检查。

公开文档描述这些能力时，应该写“维护者角色需要什么”，不要写某台机器的绝对路径。

## 硬边界

- 不要把 formal admissible gate 写成 public PR 默认检查。
- 不要写仓库里无法复现的 coverage 百分比。
- 不要把 HostProject fallback 当成贡献者验证可接受的替代路径。
- 不要在 public-facing 文档里写维护者绝对路径。
