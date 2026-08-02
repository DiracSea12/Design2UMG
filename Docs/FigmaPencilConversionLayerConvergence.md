# Figma/Pencil 转换层收敛开发单

## 目标

以当前本地修改后的工作区为起点，收敛 Figma/Pencil parser、共享 IR、布局估算、视觉映射和 converter 的职责边界，删除重复路径和没有独立语义的包装，使转换层保持最小、清楚、可维护。

这不是新增 fidelity heuristic 的任务。已有行为只有在确认属于通用语义错误、职责放错位置或重复实现时才可调整。

## 已确认约束

- 当前工作区全部既有修改属于本轮 base，不回退、不清理、不重做。
- 不新增按 case、节点 id、文件名、字体名、导出目录、历史分数或参考图特征触发的逻辑。
- 不新增只服务单个设计稿、单个组件、单个文本或单个资源的分支、阈值、缩放因子和兜底。
- 新增行为必须能由一类可描述的源语义、布局约束或共享 IR 字段解释，并对同类输入一致生效。
- 优先删除重复逻辑、合并等价判断、归还来源语义到对应 parser、把共享规则放到共享 IR/helper。
- 不修改设计源、参考图、对比图或 Unreal Engine 引擎源码。
- 不把验证工具、基线文件或历史成绩改成有利于本轮结果的口径。
- 允许收敛已有共享 IR/schema，并在确认重复且无独立语义时收缩已有契约。
- Figma/Pencil 的来源差异在各自 parser 内归一化；进入共享 IR 后使用统一语义。
- 能表达成共同转换语义的来源字段进入共享 IR；纯追踪、诊断或不影响共同转换行为的原始信息留在现有 metadata。
- 契约收缩覆盖 C++ schema、序列化字段、读取路径、生成 JSON 和相关测试；旧兼容链不作为本轮保留目标。

## 71-case 验收

正式集合固定为 71 个 case：39 个 Pencil case，加上 16 个 Figma export root 的 `local` 和 `source` 两条路径，共 32 个 Figma case。

验收必须同时满足：

1. 71 个 case 全部发现、全部生成、全部产生比较报告。
2. 每个 case 的每个正式比较指标都达到或优于现有 71-case 基线。不得用平均值、总分或通过数量掩盖单点退步。
3. `summary.no_regression.available = 71`。
4. `summary.no_regression.failed = 0`。
5. 历史最佳检查没有因为缺 case、身份不一致、确定性问题或真实指标退步而被阻塞。

当前已检查到的完整 71-case 成绩目录为：

`G:/UE5/FPSRoguelike/Saved/Design2UMG/Automation/_full71_image_transform_uv_persisted_20260730_10`

该目录记录 `summary.no_regression = 71/71`，并包含逐 case 的比较报告。后续实现前后对照沿用项目文档指定的 71-case 基线和 reference identity；不得使用后续 69/71 调试 run 作为新的目标下限，也不得用本轮中间结果重写基线。

## 实现范围

优先审查以下共享转换路径：

- `Source/Design2UMGCore/Private/FigmaParser*.cpp` / `.h`
- `Source/Design2UMGCore/Private/PenParser*.cpp` / `.h`
- `Source/Design2UMGCore/Private/Design2UMGConverter*.cpp` / `.h`
- `Source/Design2UMGCore/Private/SizeEstimator.cpp`
- `Source/Design2UMGCore/Private/VisualMapper*.cpp` / `.h`
- 这些模块直接使用的共享 schema、metadata、layout 和 visual helper

只在完整调用链证明必要时触及相邻测试和 typed contract。与转换层无关的当前工作区修改保持原样。

共享结构收敛的判断顺序固定为：先盘点 Figma/Pencil 的同类来源字段，再在 parser 内完成来源解释和归一化；随后合并共享 IR 中重复表示；最后同步 serializer、reader、converter 和测试。纯来源追踪与诊断信息继续沿用现有 metadata 入口，避免把来源名或来源特征扩散到共享转换层。

## 验证要求

- 开发前保留当前工作区的原生 Git base snapshot。
- 开发 worker 必须自测并检查完整 diff。
- 先运行与改动路径直接相关的 parser/converter 测试和构建检查。
- 交付前独占运行完整 71-case formal gate。focused 测试、局部 render、构建和单门审查只能作为前置证据。
- 完整 71-case 结果出现任何单点退步，都不接受该收敛结果。
- 结果必须区分 `summary.no_regression`、历史最佳比较和 pipeline determinism，不能把它们混成一个总分。

## Formal Gates 路线

本开发单选择 `full` 路线，使用最新安装包发现的：

- `qa`
- `complexity-gate`
- `implementation-quality-gate`

正式流程按最新 `formal-gates` 顺序执行：需求澄清记录、路线绑定、QA Design/Review、start-readiness、独立 development worker、不可变 snapshot、QA Execution、各门审查、必要修复和 Seal。
