# Design2UMG 封板前最后开发内容

## 范围前提

- 本次封板只承诺 `Figma Design / Pencil .pen -> UE5 UMG Widget Blueprint`。
- 不考虑 Figma Design 之外的格式，不考虑 FigJam 运行时语义化，不承诺在线实时同步。
- 交付原则是“可维护近似 + 明确降级 + 可诊断”，不是像素级无条件复刻。
- 本文档只列**封板前仍需收口**的内容；仓库里已经写清楚并且已有测试覆盖的限制项，不再重复当成新增 blocker。

## 封板前必须做

- 锁定交付口径。
  明确哪些能力属于正式支持，哪些属于降级，哪些属于跳过。
  至少要把 `TEXT_PATH`、`SLICE`、FigJam-only 节点、仅做有限近似的 blend mode / effect 组合、以及 `clipsContent` / render fallback / raster fallback 这类高频视觉风险写进正式限制说明。
  已经存在于 `KNOWN_LIMITATIONS.md` 的条目，不作为新增功能项，而是作为封板前需要核对一致性的正式口径项。

- 把封板门槛统一成“无回退 + 多信号一致”。
  `no_regression` 是主门槛，但不是唯一门槛。
  封板前必须同时满足：回归不退、结构快照不异常、产物时间戳/渲染链无 stale artifact、真实项目运行链路正常。
  任一基线回退都必须修复后才能封板。

- 把“失败可定位”做完整，并落实到统一载体。
  不能只写原则，必须收口成单一权威诊断产物，例如每次导入都稳定落一份 `Saved/Design2UMG/ImportReports/*.import_report.json`。
  结构化诊断至少包含：
  source identity。
  导出器版本或来源标识。
  插件版本。
  被跳过/被降级的节点类型与节点 id。
  触发原因。
  fallback / bake 使用情况。
  缺失字体清单。
  远程图片失败清单。
  对运行时结果的影响。
  推荐处理方式。
  并且这份报告要和本次生成的资产路径/渲染产物绑定，保证问题可复跑、可比对。

- 把“迭代导入语义”锁成明确口径。
  封板前必须明确：当前是否支持“同源更新现有 Blueprint”，还是只支持“生成新资产”。
  当前封板口径应明确为：**只生成新资产，不承诺同源更新/patch 现有 Blueprint**。
  这必须写成正式限制，并给出稳定规避策略；不能让用户默认以为重复导入会覆盖旧资产。

- 把联网与凭据边界写成封板硬规则。
  必须明确：
  token 存放位置。
  是否允许写入可提交配置。
  离线/受限网络环境下的行为。
  下载失败/认证失败时的诊断与降级方式。
  不能只要求“不打进日志”，还要保证团队协作场景下不会把凭据扩散进仓库或共享配置。

- 把字体链路收成正式交付能力。
  正式承诺只覆盖：
  字体映射可配置。
  缺字/替代字体有明确提示。
  文本测量误差有已知限制说明。
  关键文本用例在真实项目内可稳定回归。
  联网自动下载 known web fonts 不纳入封板承诺，默认应视为实验能力或默认关闭。
  同时要明确离线/企业环境下的行为：下载失败时只能走映射或缺失提示，不能影响主导入链路的可复现性。
  README 与对外说明里也要同步这条口径，不能再把联网下载字体或明文 token 参数写成默认主用法。

- 把资源导入与缓存隔离作为必测项。
  必须证明“跨来源不串缓存、同来源更新不吃旧派生图”。
  缓存键策略和命中结果至少要能通过诊断或日志稳定追踪，便于现场排查。

- 把效果能力控制在可维护范围内。
  `RetainerBox`、`BackgroundBlur`、材质近似只在必要时启用。
  效果 wrapper 需要可诊断，至少能看到 wrapper 数量与触发原因。
  封板文档里必须明确说明效果近似和性能成本，但不在封板前扩张成独立性能归因体系。

- Prototype Basic 既然已经按正式能力对外口径存在，就必须完成真实项目验收。
  当前目标覆盖：
  `ON_CLICK / ON_HOVER / ON_PRESS / ON_DRAG / ON_KEY_DOWN / ON_KEY_UP`
  `NAVIGATE / SWAP / OVERLAY / BACK / OPEN_LINK`
  基础 `fade / slide` 过渡
  同时把未承诺内容写清楚，例如 Smart Animate 图层匹配、完整浮层系统。
  如果验收成本或数据稳定性不足，封板前应主动收窄到最低硬保范围：
  `ON_CLICK / ON_HOVER / ON_PRESS`
  `NAVIGATE / BACK / OVERLAY / OPEN_LINK`
  其余输入触发器与严格动画验收降为延期项，而不是把整条 Prototype Basic 主线拖大。

- 把 Figma 节点兼容策略定成统一规则。
  对 blend mode、复杂 effect、复杂 vector/path、render fallback 的处理不能静默漂移，必须统一降级并显式报出。
  封板前不做“全量统一规则引擎”，只做最小可诊断降级子集。

- 把原型交互数据字段契约写清楚。
  封板前必须明确交互数据来源、使用字段、以及多 action / API 漂移时的处理策略。
  不能依赖隐式字段或隐式排序规则。

- 把发布候选门禁纳入封板项。
  至少覆盖：
  文本文件 UTF-8 with BOM。
  不把 token / 敏感配置打进日志。
  `RunUAT BuildPlugin -StrictIncludes` 可过。
  自动化测试集可过。

- 把真实项目验证约束写成硬规则。
  封板验证只能基于 maintainer-owned canonical runtime boundary。
  不允许 HostProject fallback。

- 把当前已存在的限制与实现状态对齐。
  文档里不能再把“仓库已经明确限制且已有测试覆盖”的项写成开放性待办。
  封板前要完成一次口径对表：
  `KNOWN_LIMITATIONS.md`
  `Design2UMG_TechnicalDesign.md`
  `PrototypeFlowPlan.md`
  本文档
  四者对同一能力的说法必须一致。

- 用户已批准的专项收缩项：兼容链清理。
  与设计稿主链无关、纯为兼容旧 UMG JSON/旧字段存在的解析、回填、测试与样例，在不影响成绩基准的前提下尽量删除。
  这不是通用封板 blocker，而是已获批准的专项收缩工作。
  当前已完成一轮实际收缩：`UMGJsonParser` 的 `CanvasSlot` / `Slots` / 节点平铺 property 入口和对应纯兼容测试已删除，且全量成绩未下降。
  这轮专项收缩的批次边界、允许修改模块、guard set 与 admissible 回归证据统一登记在 `Docs/IncrementalCodeSimplificationBatches.md`。

- 把“格式特有问题只改 Parser / Exporter，共用层不动”写成封板范围守卫。
  Fidelity 修复必须先归因到 Parser、Exporter、导出数据或资源链。
  禁止在 `Design2UMGConverter` 这类共用层继续堆 broad wrapper 或平台特判来救单一来源问题。

- 把 Pencil / MCP 写成明确的封板风险项。
  如果 Pencil 导入仍属于正式能力范围，就必须满足：
  连接失败可诊断。
  app id / 版本 / 扩展发现可自检。
  失败时给出硬错误，不允许静默卡死。

- 把“自定义 Widget 类 override”写成正式能力口径项。
  这是比继续扩映射表更稳的交付能力，封板前至少要把支持范围、命中规则和验证方式写清楚。

## 建议延期

- `TEXT_PATH` 的路径排版语义复刻。
- `mesh_gradient`、多层 blur、完整 inset-shadow 的高保真还原。
- 全量 blend mode 复刻与通用混合图生成。
- 渐变的完全可编辑语义复刻。
  封板前只需要把“渐变默认走什么近似或降级，以及哪些组合有限制”写清楚，不扩到全量可编辑材质体系。
- 更完整的结构化诊断体系统一化。
  当前先保证单一权威诊断产物可用，不在封板前再扩一套大而全的多入口 schema。
- 通用动画 authoring 与完整 Smart Animate。
- 激进的“彻底删除 legacy PropertyBag / 全面强类型化”迁移。
- “效果只在必要时启用”的复杂启发式判定。
  封板前只要求效果路径可诊断、可关闭，不再扩复杂自动判定。

## 明确不做

- 不改引擎源码修插件问题。
- 不重新引入参考图/截图驱动的刷分兜底链路。
  但允许已有的 source-authored render fallback / raster fallback 作为显式降级能力继续存在，并要求可诊断。
  同时必须明确这类 fallback 可能和设计器实时所见存在质量偏差，不能被当成“等价语义支持”。
- 不承诺 FigJam 专用节点进入运行时 UI 树。
- 不承诺在线实时同步 Figma 到 UE。

## 需要样例后再决定

- Auto-layout 与约束的极端边缘行为。
  常见布局行为不允许保持开放状态；封板前必须收敛成“已有规则 + 已知降级”。
  只有确实超出当前定位的极端样例，才进入“需要样例后再决定”。

- Path Rasterization 与非均匀缩放 fidelity。
  需要同一矢量在不同 scale/rotation 下的期望结果。

- 多层阴影、描边对齐、裁剪父容器叠加的极端组合。

- Overlay 的完整浮层系统。
  包括锚定、点击外部关闭、统一 placement 等。

## 当前已确认不再作为 blocker 单列开发

- `TEXT_PATH` / `SLICE` / FigJam-only 节点本身的“跳过或降级”口径。
  这些能力边界本身已经存在于仓库限制文档与测试里，封板前只需要核对口径一致，不把它们重新当成功能开发项。

- blend mode 的“是否存在任何支持”。
  当前口径应明确为“只支持/近似少数子集，不支持全量语义复刻”，而不是“完全未支持”。

- Figma / 外部 API 变化带来的契约漂移。
  封板前只要求用固定 fixture 与诊断把关键契约锁住，不在本轮扩成完整的 API 漂移防御体系。

- 现有 `Warnings`、`SkippedNodes`、导入报告字符串这些分散诊断入口本身。
  它们已经存在，封板前的重点不是再发明更多入口，而是收口成一个权威产物。

- 与设计稿主线无关、仅服务于旧 UMG JSON 兼容的历史解析链。
  这些内容不是封板后要继续维护的正式能力；若用户已批准，可在不影响成绩基准的前提下专项删除。

## 参考来源

- 仓库文档：
  `Docs/PenToUMG_Optimization_Audit_20260324.md`
  `Docs/PrototypeFlowPlan.md`
  `Docs/Design2UMG_TechnicalDesign.md`
  `KNOWN_LIMITATIONS.md`

- 官方资料：
  Figma TextPathNode
  https://developers.figma.com/docs/plugins/api/TextPathNode/

  Figma SliceNode
  https://developers.figma.com/docs/plugins/api/SliceNode/

  Figma BlendMode
  https://developers.figma.com/docs/plugins/api/BlendMode

  Figma Working in FigJam
  https://www.figma.com/plugin-docs/working-in-figjam/

  Epic UMG Background Blur
  https://dev.epicgames.com/documentation/en-us/unreal-engine/using-the-background-blur-widget-in-unreal-engine

  Epic Slate/UMG Invalidation
  https://dev.epicgames.com/documentation/en-us/unreal-engine/invalidation-in-slate-and-umg-for-unreal-engine

  Epic UMG Fonts
  https://dev.epicgames.com/documentation/en-us/unreal-engine/using-fonts-with-umg-in-unreal-engine

- 同类竞品：
  Fab 上的 Figma2UMG
  https://www.fab.com/listings/0e0d4d1f-702f-4b3b-96c1-01c0fcac7823

