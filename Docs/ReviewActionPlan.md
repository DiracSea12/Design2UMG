# Phase 14 Review 必做项开发计划

这份文档是 Phase 14 的开发计划，并替代旧的 `Docs/ReviewRemainingWork.md`。这里只写我判断还应该实际动手做的内容；写进这里不等于完成，完成必须有代码改动、验证证据和独立 review。

判断标准很简单：

- 当前代码里确实还存在。
- 做完能降低维护负担、公开发布风险或贡献者理解成本。
- 能用构建、自动化、formal gate 或 review 证明结果。
- 不靠样例特判、不靠假 CI、不靠删功能制造“收口”。

## 1. 拆分仍然过大的实现文件

当前仍然过大的文件：

| 文件 | 当前行数 | 要解决的问题 |
|------|----------|--------------|
| `Source/Design2UMGCore/Private/FigmaParserInternal.cpp` | 1795 | Figma 节点、布局、视觉、文本、效果解析混在一个文件里 |
| `Source/Design2UMGCore/Private/Design2UMGPrototypeStepGraphAssembly.cpp` | 1108 | prototype step graph 的组装、校验、序列化责任集中 |
| `Source/Design2UMGCore/Private/VisualMapper.cpp` | 1038 | fill、stroke、shadow、blur、gradient 等视觉映射责任集中 |
| `Source/Design2UMGCore/Private/PenParser.cpp` | 922 | Pen 顶层解析仍然承担过多 orchestration |
| `Source/Design2UMGCore/Private/UMGWidgetNodeAccess.cpp` | 826 | typed accessor 的读写、序列化桥和旧 property bag 桥混在一起 |
| `Source/Design2UMGCore/Private/Design2UMGConverter.cpp` | 669 | 主 converter 仍然承担编排、视觉 wrapper、子节点附加和根节点收尾 |

要改成什么：

- 按现有稳定职责拆，不为了凑“500 行”硬切。
- 只做行为不变的物理拆分和小函数搬移，不引入大而空的策略模式、访问者模式或依赖注入。
- 每个新文件名字必须说清自己负责什么，例如 layout parsing、visual parsing、fill mapping、shadow mapping、prototype validation。
- 拆分时顺手补“为什么这么做”的短注释，只补非显而易见的规则和历史坑位。

验收：

- 维护者真实项目编译通过；本地执行时使用仓库本地规则指定的真实项目，不在公开 Docs 中写维护者绝对路径。
- 相关 focused automation 通过。
- 如果碰到 parser / converter / visual / prototype 行为路径，formal 47-case `no_regression` 必须通过，historical-best 不能退。
- 零上下文 review 要明确判断：文件责任是否更清楚，owner 负担是否真的下降。

## 2. 收缩 Core Public 表面

当前仍然值得处理的点：

- `Source/Design2UMGCore/Public/UMGPropertyKeys.h` 仍是 127 行公开 key 表。
- `Source/Design2UMGCore/Public/FontResolver.h` 仍把注册入口、解析入口、缓存入口和私有实现状态放在同一个公开类型里。

要改成什么：

- 先做使用矩阵，逐个 key 判断它是跨模块序列化契约，还是 Editor applier 私有实现细节。
- 只有被证明不是跨模块契约的 key 才移出 Public；不能为了“看起来更干净”破坏 UMG JSON / typed payload / resource binding。
- `FontResolver` 拆成更窄的公开 facade：Editor 只需要注册 resolver / primer 时，不应该被迫 include 完整解析实现表面。
- 保留现有功能，不新增旧兼容读取、不扩大公开 API。

验收：

- Core Public 头里只留下确实需要跨模块共享的内容。
- Editor 不再因为注册字体回调而依赖完整 resolver 实现细节。
- 现有字体、icon、UMG JSON、property applier、typed accessor 测试通过。
- 不新增 Engine 源码改动，不新增 HostProject fallback。

## 3. 把公开发布流水线补到可用

当前已经有 `repository-hygiene` 和手动 self-hosted `BuildPlugin`，但还没把 release artifact 这条线真正做完整。

要改成什么：

- 在 `.github/workflows/build.yml` 的 self-hosted BuildPlugin job 里上传 packaged plugin artifact。
- release checklist 和 public export workflow 继续作为唯一发布清单，不再另起一套重复流程。
- README / CONTRIBUTING / ValidationProtocol 只描述真实存在的 gate：repository hygiene、self-hosted BuildPlugin、public smoke、maintainer formal gate。

验收：

- 手动触发的 self-hosted workflow 能产出可下载插件包。
- release 文档能让贡献者看懂哪些检查自己能跑，哪些是 maintainer gate。
- `Tools/Release/Assert-PublicRepoBoundary.ps1` 覆盖发布包里不该出现的本地文件、原始 review 报告、`openspec/`、缓存和临时产物。
- 不能把 public smoke 写成完整公开 UE formal gate。

## 4. 把 maintainer-only 内容从公开产物边界里硬挡住

当前仓库里确实存在 maintainer-only 工作材料，例如根目录 review 报告、`openspec/` 工作集、AGENTS / CLAUDE 本地规则和自动化输出目录。它们可以留在本地工作区，但不能进入公开发布产物。

要改成什么：

- 发布包和公开 repo export 都要有同一套可执行边界检查。
- 边界检查要覆盖文件名、目录名、维护者绝对路径、生成出来的临时 HostProject fallback 产物、Design2UMG 临时 host 目录、HostProject 工程文件路径和 raw automation tree；允许文档用否定语气说明 HostProject fallback 不受支持。
- `.github/workflows/build.yml`、`Tools/Release/PublicRepoExportWorkflow.md`、`Tools/Release/Assert-PublicRepoBoundary.ps1` 三处口径必须一致。

验收：

- packaged plugin 不包含 maintainer-only 文件。
- public repo export workspace 不包含 maintainer-only 文件。
- 检查失败时输出能直接告诉发布者该删哪个路径。
- 不删除本地原始 review 报告来假装产物干净；只保证它们不能被发布出去。

## 5. 把 targeted diagnostics 补进真实风险点

review 里“错误处理不系统”的方向是对的，但不能全仓库乱加 `check()` / `ensure()`。该做的是给当前容易静默失败的真实边界补诊断。

要改成什么：

- parser 输入结构不符合当前 schema 时，要有明确 warning / error 或测试覆盖。
- converter 遇到违反当前契约的节点状态时，要能定位 node id / name / source。
- release / automation 脚本失败时，要明确是配置缺失、UE 路径缺失、真实项目缺失，还是 gate 失败。
- 不给“不可能发生”的分支加防御式代码，不为旧字段或旧路径加兼容兜底。

验收：

- 新诊断都有对应测试或可复现命令。
- 自动化日志里错误信息能指向具体输入或配置，不只是一句 failed。
- 不新增样例名、节点 id、文件名特判。
- formal 47-case 不回退。

## 6. 处理低成本但会误导贡献者的命名问题

当前 `Design2UMGConverterFlowSupport.*` 这个名字仍然不够准，它实际做的是 flow child pre-resolution / fit-content preparation，不是泛泛的 flow support。

要改成什么：

- 改成更准确的文件名和类型名，让贡献者不用打开文件就知道它负责什么。
- 只做命名和 include 路径更新，不顺手改行为。

验收：

- 编译通过。
- `rg "Design2UMGConverterFlowSupport"` 不再命中生产代码。
- focused converter tests 通过。

## 完成线

Phase 14 清单全部完成后，才算根目录两份 review 报告里我认为值得做的剩余项真正收干净。

最终封板必须同时满足：

- build 通过。
- touched area focused automation 通过。
- 需要时 formal 47-case `no_regression` 通过。
- historical-best 校验不 blocked。
- public boundary 检查通过。
- UTF-8 with BOM 检查通过。
- 零上下文代码 review 没有 P0 / P1。
