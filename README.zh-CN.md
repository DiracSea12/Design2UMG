# Design2UMG

验证工具使用用户自己的用例目录，实际发现多少项就运行多少项，没有固定数量门槛。维护者私有视觉测试集不随开源仓库提供，也不是运行前提；空目录代表0项测试，不代表视觉门禁通过。见 [工具说明](Tools/README.md)。

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
- `Examples/AiHud/` 是可直接导入的 Figma 导出示例（"AI NPC Demo — 中式赛博朋克 HUD"）：用编辑器控制台命令 `Design2UMG.LocalFigmaImport Examples/AiHud/design.json` 导入它的 `design.json`。
- `Docs/Images/` 中的截图展示了当前插件流程里的预览界面。
- 最快的本地 smoke 路径是：启用插件，把示例 `.pen` 拖进 Content Browser，然后检查生成的 Widget Blueprint 和 preview PNG。

## 验证层级

本仓库明确区分四种验证层级，不能混用：

| 层级 | 用途 | 谁来运行 |
| --- | --- | --- |
| `repository-hygiene` | 检查公开仓库边界和必需文件 | GitHub Actions push / PR |
| `maintainer build` | 打包插件、运行聚焦 UE 自动化并上传产物 | 维护者本地验证，不进托管 CI |
| `public smoke/public validate` | 本地构建和聚焦验证，用于贡献者复现 | 贡献者和维护者 |
| `maintainer formal admissible gate` | 维护者拥有的完整 admissible case set 正式无回退发布门禁 | 维护者 |

当前公开 badge 只代表仓库 hygiene job。插件打包与 UE 自动化属于维护者本地验证，不在公开 CI 中运行，不是公开托管的 UE formal gate。

如果想看“各层测试到底覆盖什么、普通贡献者和维护者分别能跑什么”，请直接看 [Docs/ValidationGovernance/TestCoverageBoundary.zh-CN.md](Docs/ValidationGovernance/TestCoverageBoundary.zh-CN.md)。

## 功能概览

- 多来源导入：`.pen`、Figma、MCP bridge/debug utility。
- 设计布局到 UMG slot/anchor 的转换。
- fill、stroke、radius、shadow、gradient、SVG path 等视觉映射。
- ref 展开、变量和主题解析。
- Content Browser 拖拽导入。
- 安全重导入（Safe Reimport）：对本插件生成的资产按设计身份做属性级原地更新——D2U 拥有的节点自动刷新，你手工加的控件/绑定/蓝图逻辑全部保留；身份不清时保守保留资产并给出结构化诊断（入口：菜单 `Design2UMG > Reimport > Reimport Design...`，或控制台 `Design2UMG.Reimport <目标资产> <design.json>`）。
- 组件映射（Component Mapping）：项目级 `Config/Design2UMGComponentMapping.ini` 把 Figma 组件实例映射到你自己的 Widget Blueprint 类（替换整棵子树为真实实例），可声明 typed inputs 把设计值写进实例属性；语法与边界见 README.md 的 Component Mapping 一节。
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

## 生成图片预算设置

在 `Edit → Editor Preferences → Plugins → Design2UMG → Generated Textures` 中调整 `Generated Texture Budget (MiB)`。
默认 512 MiB，最小 1 MiB，限制一次正式生成或预览中新生成图片的累计像素数据。修改从下一次操作生效；缓存复用不重复计费，重新生成仍计费。
此设置不改变下载、输入图片解码及单张图片的限制，也不代表编辑器总内存上限。提高数值会允许更多资源分配，应结合机器内存调整。

## 自动化和 HostProject 边界

`Tools/design2umg_auto_verify.py` 必须显式拿到 runtime project 和 editor 路径，或通过 `DESIGN2UMG_RUNTIME_PROJECT`、`DESIGN2UMG_UNREAL_EDITOR`、`UE_ROOT` 提供。脚本不应创建或推断 HostProject。
这里的 `design2umg_auto_verify.py` 示例属于维护者侧 focused automation，不是 public PR 自动门禁。

维护者视觉验证需要提前准备本地字体 seed。字体资产应放在设计源旁可发现的 `Content/Design2UMG/Fonts`，或放在显式 runtime project 的同名目录。验证脚本只复制这些资产，不负责下载字体，也不负责修复坏掉的 UE 字体资产。有效 seed 必须是 UE `UFont`，default typeface 至少有一个 entry，并且 entry 必须绑定有效 `FontFace` 或等价 font data。单独的 `.ttf`、空 `UFont`、没有 font data 的 typeface entry，都按缺失或无效字体处理。

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
