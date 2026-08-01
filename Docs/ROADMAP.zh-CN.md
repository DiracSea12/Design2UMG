# Design2UMG Roadmap

English canonical: [ROADMAP.md](ROADMAP.md)

**创建时间**：2026-02-15
**当前公开基线**：2026-05-14
**状态说明**：`[ ]` 未开始 | `[~]` 进行中 | `[x]` 已完成

内部 review 计划和 OpenSpec phase 不是公开发布承诺。本 roadmap 只描述公开方向和当前 release 边界。

## 策略

Design2UMG 的目标是让 Unreal Engine 项目的 design-to-UMG 流程更容易落地。当前优先把插件整理成可公开开源的形态，后续可以探索商业支持、咨询或高级扩展插件。

## 当前 release 边界

当前公开 release 范围包括：

- `.pen` 导入为 UMG Widget Blueprint。
- 通过 Figma REST API 导入。
- 通过本地 Figma export JSON 导入。
- 将设计侧布局转换为 UMG slots、anchors 和 offsets。
- 支持 fill、stroke、radius、shadow、blur 近似、gradient、SVG path、变量、主题和组件引用。
- Content Browser 拖拽导入和命令行导入。
- 公开 smoke / validation 指南。

以下能力仍是 bridge、debug 或未来工作，除非后续 release 明确升级为正式范围：

- MCP JSON bridge import 和 live Pencil MCP 调试链。
- 公开托管的 UE formal validation。
- 原型逻辑的完整 UEdGraph authoring。
- 完整 Smart Animate、overlay layer semantics、通用 animation authoring 和 design-system sync。

## 验证和发布门禁

项目使用四个验证层级：

| 层级 | 含义 | 公开承诺 |
| --- | --- | --- |
| `repository-hygiene` | GitHub Actions 中的必需文件和公开边界检查 | 公开、轻量 |
| `self-hosted build and automation` | 在维护者 Windows UE runner 上打包并运行 Unreal 自动化 | 维护者 push、同仓库 PR 和手动运行 |
| `public smoke/public validate` | 贡献者可复现的本地构建和聚焦验证 | 公开复现边界 |
| `maintainer formal admissible gate` | 完整 admissible case set 无回退发布门禁 | 维护者发布门禁 |

当前仓库不发布公开托管 UE matrix、公开托管 full formal gate 或 coverage badge。

## 当前能力位置

| 能力 | Design2UMG 当前状态 |
| --- | --- |
| Layout conversion | flex-like layout、anchors、offsets 和 fixer rules |
| Visual properties | fill、stroke、radius、shadow、blur 近似、gradients 和 SVG paths |
| Component system | ref expansion、variables 和 themes |
| Prototype flow | 已有最小 typed/runtime 闭环；更完整语义仍是未来工作 |
| Multiple input sources | `.pen`、Figma REST、本地 Figma export JSON，加上 bridge/debug utilities |
| Blueprint logic generation | 以 runtime extension 为主；完整 UEdGraph authoring 不是当前 release 承诺 |

## v1.0 公开内容

- `.pen`、Figma、本地 Figma export JSON 到 UMG Widget Blueprint 的核心转换链路。
- Layout engine：flex-like layout 到 anchors/offsets，加 fixer rules。
- 当前视觉属性路径：fill、stroke、radius、shadow、blur 近似、gradients、SVG path rasterization 和 path texture cache。
- Component reference expansion、variable 和 theme parsing。
- Content Browser 拖拽导入和命令行导入。
- 公开 smoke / validation 文档。
- Bridge/debug utilities 仍可用，但不是当前支持承诺。
- 维护者拥有的 formal admissible gate 仍是 release gate。

## v1.1 候选方向

- Prototype flow 扩展：hover、drag、keyboard、conditional flows 和 overlay semantics。
- 评估是否加入自动 Blueprint graph generation。
- 可选 tag-based helper flow。
- 多 stop gradient 和任意角度 gradient。

## 后续社区驱动方向

- 更高级的交互语义。
- 响应式适配和 safe-zone 支持。
- Design token 和 design-system sync。
- 增量更新 / reimport 行为。

## Public API 边界

有意保留的 public API 是 [Architecture](Architecture.md) 中记录的 `Design2UMGCore/Public` 和 `Design2UMGEditor/Public` header surface。保留理由包括 parser、converter、schema、settings、import、preview、image 或测试可见 contract 被下游模块或贡献者需要。

未来如果要收窄 header，必须有当前 include 证据和验证支撑。Phase 21 Batch 1 只记录审计证据，不引入会改变行为的 public API break。

## 开源就绪里程碑

| 里程碑 | 完成标准 |
| --- | --- |
| M0: Open-source readiness | repository hygiene 通过，self-hosted BuildPlugin 可用，license 和贡献文档是当前事实，公开 package/export 边界排除维护者本地文件 |
| M1: v1.0 release | M0 加上 v1.0 范围收口清楚，不过度承诺推迟的 material、graph-authoring 或 formal-gate 工作 |
| M2: v1.1 expansion | Prototype expansion 实现并验证，不反向改变 v1.0 支持边界 |
| M3: Community health | 有外部贡献者、issue triage 活跃、roadmap 由真实使用推动 |
