# Design2UMG

English canonical: [README.md](README.md)

Design2UMG 把设计文件转换成 Unreal Engine 5 的 UMG Widget Blueprint。

## 当前正式发布范围

- Pencil `.pen` 文件导入。
- 通过 Figma REST API 导入。
- 通过本地 Figma export JSON 导入。

仍可使用但不属于当前正式承诺的桥接/调试能力：

- MCP JSON bridge import。
- 本地 Pencil MCP live connection 调试链路。

## Demo 和示例

- `Examples/` 里有可导入的 `.pen` 示例，例如 `Dashboard.pen`、`GameHUD.pen`、`LoginForm.pen`、`SimpleCard.pen`。
- `Docs/Images/` 中的截图展示了当前插件流程里的预览界面。
- 最快的本地 smoke 路径是：启用插件，把示例 `.pen` 拖进 Content Browser，然后检查生成的 Widget Blueprint 和 preview PNG。

## 验证层级

本仓库明确区分四种验证层级，不能混用：

| 层级 | 用途 | 谁来运行 |
| --- | --- | --- |
| `repository-hygiene` | 检查公开仓库边界和必需文件 | GitHub Actions push / PR |
| `self-hosted build` | 在维护者自托管 Windows UE runner 上打包插件并上传产物 | 维护者手动触发 |
| `public smoke/public validate` | 本地构建和聚焦验证，用于贡献者复现 | 贡献者和维护者 |
| `maintainer formal admissible gate` | 维护者拥有的完整 admissible case set 正式无回退发布门禁 | 维护者 |

当前公开 badge 只代表仓库 hygiene job。插件打包 job 是手动、自托管的，不是公开托管的 UE formal gate。

如果想看“各层测试到底覆盖什么、普通贡献者和维护者分别能跑什么”，请直接看 [Docs/ValidationGovernance/TestCoverageBoundary.zh-CN.md](Docs/ValidationGovernance/TestCoverageBoundary.zh-CN.md)。

## 功能概览

- 多来源导入：`.pen`、Figma、MCP bridge/debug utility。
- 设计布局到 UMG slot/anchor 的转换。
- fill、stroke、radius、shadow、gradient、SVG path 等视觉映射。
- ref 展开、变量和主题解析。
- Content Browser 拖拽导入。
- 编辑器插件，不提供运行时模块依赖。

## 安装

把插件复制到以下任一位置：

```text
<UE5_Root>/Engine/Plugins/Marketplace/Design2UMG/
<Project>/Plugins/Design2UMG/
```

然后在 `Edit > Plugins` 启用 `Design2UMG`。

## 快速开始

导入 `.pen`：

```text
Design2UMG.Import <absolute_or_relative_path_to.pen>
```

导入 Figma：

```text
Design2UMG.FigmaImport <file_url_or_key> <FigmaAccessToken>
```

导入本地 Figma export JSON：

```text
Design2UMG.LocalFigmaImport <design.json> [--out=/Game/Path] [--no-preview]
```

MCP JSON 导入只作为 bridge/debug utility：

```text
Design2UMG.PenConvert <nodes.json> <vars.json> [TargetNodeId] [--preview|--no-preview] [--out=/Game/Path]
```

## 自动化和 HostProject 边界

`Tools/design2umg_auto_verify.py` 必须显式拿到 runtime project 和 editor 路径，或通过 `DESIGN2UMG_RUNTIME_PROJECT`、`DESIGN2UMG_UNREAL_EDITOR`、`UE_ROOT` 提供。脚本不应创建或推断 HostProject。
这里的 `design2umg_auto_verify.py` 示例属于维护者侧 focused automation，不是 public PR 自动门禁。

正式发布验证在维护者拥有的真实运行边界上完成。公开仓库不能把维护者机器路径当成用户安装教程。

测试覆盖边界、贡献者可跑项和维护者专属门禁的总览见 [Docs/ValidationGovernance/TestCoverageBoundary.zh-CN.md](Docs/ValidationGovernance/TestCoverageBoundary.zh-CN.md)。

## 仓库和发布检查

- 公开包和公开导出必须排除 root review 报告、`openspec/`、AGENTS/CLAUDE 本地规则、raw automation tree、cache、HostProject fallback 产物、具体 fallback project 路径和维护者绝对路径。
- 当前仓库不发布公开托管的 UE formal gate、公开 UE matrix 或 coverage badge。

## 架构和贡献

- 架构见 [Docs/Architecture.md](Docs/Architecture.md)。
- 中文 companion 见 [Docs/Architecture.zh-CN.md](Docs/Architecture.zh-CN.md)。
- 贡献说明见 [CONTRIBUTING.md](CONTRIBUTING.md)。
- 当前限制见 [KNOWN_LIMITATIONS.md](KNOWN_LIMITATIONS.md)。
