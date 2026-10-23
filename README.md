<div align="center">

# Design2UMG

**把 Pencil / Figma 设计稿直接转换成 Unreal Engine 5 的 UMG Widget Blueprint。**

[![Plugin CI](https://github.com/DiracSea12/Design2UMG/actions/workflows/build.yml/badge.svg)](https://github.com/DiracSea12/Design2UMG/actions/workflows/build.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![Unreal Engine](https://img.shields.io/badge/Unreal%20Engine-5.0--5.8-313131?logo=unrealengine)
![Platform](https://img.shields.io/badge/platform-Win64-0078D6)

**简体中文** · [English](README.en.md)

</div>

## 简介

Design2UMG 是一个**纯编辑器插件**：读取设计工具导出的结构化数据，把布局、样式、图片、文字和组件引用还原成可以继续编辑的 UMG 控件树，并保存为普通的 Widget Blueprint 资产。生成结果不依赖本插件运行，打包游戏中不需要它。

| 设计来源 | 入口 | 支持级别 |
| --- | --- | --- |
| Pencil `.pen` 文件 | 拖进 Content Browser，或 `Design2UMG.Import` | ✅ 正式支持 |
| Figma 在线文件（REST API） | 菜单 **Design2UMG > Import from Figma...** | ✅ 正式支持 |
| Figma 本地导出 JSON | `Design2UMG.LocalFigmaImport`；导出器见 [Tools/FigmaExporter](Tools/FigmaExporter/README.md) | ✅ 正式支持 |
| 当前打开的 Pencil 设计稿（MCP 实时读取） | 菜单 **Design2UMG > Connect to Pencil...** | 🧪 桥接/调试，不作正式承诺 |
| MCP JSON 文件 | `Design2UMG.PenConvert` | 🧪 桥接/调试，不作正式承诺 |

> **Pencil 描边限制：** 当前不支持 Pencil 2.6–2.11 历史描边格式转换。旧稿描边可能缺失或显示不正确，这部分尚未完整支持或验收；完整支持 Pencil 仍是长期目标。

## 主要功能

- **布局还原**：把设计侧的 flex / auto-layout 投影为 UMG slot、anchor 和 offset，以节点 bounds 为唯一事实来源。
- **视觉映射**：fill、stroke、圆角、阴影、模糊近似、线性/径向渐变、SVG path 光栅化。
- **设计尺寸导出**：布局保留设计尺寸，PNG 边界另计阴影及未裁剪内容；支持单轴 1 像素和独立的 256 × 1024 × 1024 像素截图导出预算内的分块导出，参考图尺寸不参与生成。
- **变量与组件**：解析设计变量和主题，展开 ref / 组件实例。
- **预览后生成**：导入前可查看预览图和可编辑的中间 JSON。
- **安全重导入**：设计更新后原地刷新插件生成的部分，保留你手工添加的控件、绑定和蓝图逻辑（目前支持 Figma 来源）。
- **组件蓝图**：Figma 组件可以生成独立的子 Widget Blueprint，父页面引用它们的实例。
- **组件映射**：把 Figma 组件实例替换成项目中已有的 Widget Blueprint 类。
- **原型交互**：Figma 原型中支持的单步点击和键盘跳转，会生成按钮、页面切换和原生蓝图连线。
- **结构化诊断**：每次导入输出一份 JSON 报告，记录降级、缺失资源和处理建议。

## 生成的控件

转换器按设计内容自动选择控件：

| 类别 | 控件 | 什么时候生成 |
| --- | --- | --- |
| 布局容器 | `CanvasPanel` | 没有自动布局、按绝对位置摆放的节点 |
| | `HorizontalBox` / `VerticalBox` | 水平 / 垂直自动布局 |
| | `WrapBox` | 换行布局 |
| | `UniformGridPanel` / `GridPanel` | 网格布局；子项声明了跨行或跨列时用 `GridPanel` |
| | `ScrollBox` | Figma 中设置了横向或纵向滚动溢出的容器（Pencil 没有对应字段） |
| | `Overlay` | 多层填充、描边叠加、特效包装、原型弹层 |
| 尺寸与间距 | `SizeBox` | 需要固定宽高约束的节点 |
| | `ScaleBox` | 以 Fit 方式填充的图片 |
| | `Spacer` | 两端对齐（space-between）布局中的弹性间距 |
| 外观 | `Border` | 背景填充、描边、圆角 |
| | `Image` | 图片、矢量路径光栅化、形状 |
| 特效 | `BackgroundBlur` | 背景模糊 |
| | `RetainerBox` | 图层模糊、内阴影等需要材质实现的效果 |
| 文字 | `TextBlock` | 普通文本 |
| | `RichTextBlock` | 一段文字里混用多种样式 |
| 原型交互 | `Button` | 带交互触发的节点 |
| | `WidgetSwitcher` | 多页面原型流程 |
| 组件 | `UserWidget` 子类 + `NamedSlot` | 组件蓝图及实例中可替换的局部内容 |

除此之外，通过 Widget Class 覆盖规则、组件映射或编辑中间 JSON，还可以使用其他 UMG 控件，例如 `CheckBox`、`Slider`、`SpinBox`、`ProgressBar`、`EditableTextBox`、`ComboBoxString`、`ListView`、`TileView`、`ExpandableArea`、`SafeZone`、`InvalidationBox`、`MenuAnchor`，以及项目中任意 `UWidget` 子类。

> 这些控件会被创建出来，但设计稿中的外观目前不会写入它们的专属样式（如勾选标记、滑块轨道、进度填充），需要在 UE 中自行设置。用组件映射指向一个已经做好样式的 WBP，是目前最省事的做法。

## 五分钟上手

1. 从 [Releases](https://github.com/DiracSea12/Design2UMG/releases) 下载与你的 UE 版本匹配的预编译 ZIP，把其中的 `Design2UMG` 目录放到 `<Project>/Plugins/`。还没有匹配的预编译包时，按下文的源码方式安装。
2. 启动编辑器，在 `Edit > Plugins` 中启用 **Design2UMG**，重启。
3. 把插件目录里的 `Examples/SimpleCard.pen` 拖进 Content Browser。
4. 打开生成的 Widget Blueprint，对照预览 PNG 查看结果。

还想试试 Figma 来源？在控制台执行（用绝对路径，相对路径是相对编辑器进程的工作目录，而不是项目目录）：

```text
Design2UMG.LocalFigmaImport <Project>/Plugins/Design2UMG/Examples/AiHud/design.json
```

这是一个“中式赛博朋克 HUD”导出示例。`Examples/` 中还有 `Dashboard.pen`、`GameHUD.pen`、`LoginForm.pen`。

## 安装

### 版本兼容

| 项目 | 说明 |
| --- | --- |
| 平台 | Windows (Win64)。其他系统尚未验证 |
| Unreal Engine | 已完成 5.0–5.8 适配。2026-10-10 在 5.0.3 / 5.1.1 / 5.2.1 / 5.3.2 / 5.4.4 / 5.5.4 / 5.6.1 / 5.7.1 / 5.8.2 上完整回归，每个版本都完成了 81 个设计用例的生成和保存后重读 |
| 本地验证 / GitHub CI 环境 | UE 5.7.1 / UE 5.8.3 |
| Pencil（可选） | VS Code 扩展 0.6.77，用于实时读取 |
| Figma（可选） | Personal Access Token，用于在线导入 |

> 上述多版本回归是某一时间点的结果，不代表之后每次提交都在所有版本上重跑过。

### 安装位置

二选一，不要让 UE 同时扫描到两份：

```text
<Project>/Plugins/Design2UMG/                          # 推荐：只对当前项目生效
<UE5_Root>/Engine/Plugins/Marketplace/Design2UMG/      # 对该引擎下所有项目生效
```

- **预编译包**：选择与 UE 版本、平台一致的 Release ZIP。蓝图项目（没有 C++ 源码）请使用预编译包。
- **源码**：需要安装对应 UE 版本的 C++ 工具链，并构建项目的 Editor 目标，详见 [贡献指南](CONTRIBUTING.zh-CN.md)。GitHub 自动生成的 “Source code” 压缩包不是预编译插件。
- 替换插件前先关闭编辑器。
- 插件**不附带字体文件**。请在插件设置里映射你有授权的项目字体；缺少字体时按诊断报告处理，不要指望像素级一致。

Release ZIP 包含许可证、文档、Figma 导出器和公开示例，但不包含贡献者验证脚本，后者需要源码仓库。

## 使用

### Pencil `.pen`

直接拖进 Content Browser，或者：

```text
Design2UMG.Import <file.pen> [--node=<id>] [--out=/Game/Path] [--no-preview] [--no-png]
```

引用 Pencil 内置 `pencil:<library>.lib.pen` 时，会从已安装扩展读取组件定义和变量，保留导入别名，不改写原始设计稿。

### Figma 在线文件

使用菜单 **Design2UMG > Import from Figma...**，在密码输入框中填 Token。

本地脚本可以用下面的命令，但第二个参数是 Token **本身的值**，会留在控制台历史里：

```text
Design2UMG.FigmaImport <file_url_or_key> <FigmaAccessToken>
```

> ⚠️ 不要把 Token 贴进截图、共享日志或聊天记录。从环境变量展开后，它同样会出现在命令记录里。

### Figma 本地导出 JSON

用 [Figma 导出插件](Tools/FigmaExporter/README.md) 导出 `design.json` 后：

```text
Design2UMG.LocalFigmaImport <design.json> [--out=/Game/Path] [--no-preview]
```

### 实时读取 Pencil 设计稿（调试）

1. 在 VS Code 的 Pencil 设计编辑器中打开 `.pen` 文件。
2. 在 Unreal Editor 选择 **Design2UMG > Connect to Pencil...**。
3. 选择画板，确认预览后生成 Widget Blueprint。

插件通过 stdio 启动 Pencil 自带的 MCP 服务器，用 `execute` 只读地获取画板和变量，不会修改设计稿。服务器路径会自动发现，也可以在设置中手动指定。更新 Pencil 扩展后，请关闭并重新打开已有的设计标签页。

### MCP JSON（调试）

```text
Design2UMG.PenConvert <nodes.json> <vars.json> [TargetNodeId] [--preview|--no-preview] [--out=/Game/Path]
```

## 更新已有资产：安全重导入

普通导入遇到同名资产时会**新建一个自动改名的资产**，不碰旧资产。想在原资产上更新，请用安全重导入。目前它的入口只接受 Figma 本地导出的 `design.json`，`.pen` 文件暂不支持。

入口：

- 菜单 **Design2UMG > Reimport > Reimport Design...**
- 控制台 `Design2UMG.Reimport <目标WBP资产路径> <design.json> [--resolution-copy]`
- C++ API `DesignImportPipeline::Reimport`

工作方式：

- 每个生成的资产旁边有一个 `<AssetName>.d2u.json` 清单，记录每个实例的 `designNodeId` 和 `widgetName`。重导入前，插件会核对来源、文档范围和这些记录是否与新设计一致。
- 插件生成的节点（布局、slot、视觉、文字属性）会原地刷新；你添加或改名的控件、事件绑定、蓝图图表逻辑保持不变。
- 遇到节点删除、身份变化或映射类变化时，保留原资产并输出结构化诊断；需要时可以加 `--resolution-copy` 另存一份 `_Resolution` 副本。
- 如果无法确认资产和新设计的对应关系（范围不一致、记录的实例找不到），插件拒绝修改并报告 `reimport_full_conflict`。

## 组件映射

把设计中的 Figma 组件实例（`INSTANCE` 节点）替换为项目中已有的 Widget Blueprint 类。配置写在 `<Project>/Config/Design2UMGComponentMapping.ini`：

```ini
[/Design2UMG/ComponentMapping]
+Mappings=(Key="figma|<fileKey-or-scope>|<mainComponentKey>",WidgetClass="/Game/UI/WBP_PrimaryButton.WBP_PrimaryButton_C",Inputs=((Source="label",Target="Label",Type="Text",ValueKind="ComponentProperty")))
```

| 字段 | 说明 |
| --- | --- |
| `Key` | `provider\|documentScope\|componentKey`，不区分大小写；有 Figma file key 时 `documentScope` 取 file key |
| `WidgetClass` | 完整对象路径，以生成类 `_C` 结尾 |
| `Inputs` | 每次导入/重导入时写入实例属性。`Type` 可选 `Text` / `Bool` / `Name` / `Float`；`ValueKind` 为 `ComponentProperty`（默认，取实例的 `componentProperties`）或 `ComponentText`（取组件内的文字） |

- 无效条目会被丢弃并给出 `mapping_` 前缀的诊断，不会阻断导入。
- 组件映射优先于按名称匹配的 Widget Class 覆盖规则；未匹配的组件照常转换。
- 挂在映射组件上的原型交互由项目自己负责，插件只报告 `mapping_prototype_inert`，不生成运行时绑定。
- Pencil 来源只提供候选身份，不走映射主路径。

## 组件蓝图（Figma）

导入保留了组件依赖信息的 Figma 导出时，插件按依赖顺序（先子后父）为每个组件和变体生成一个独立的 Widget Blueprint，父页面引用它们的实例：

- 同一个组件被多处引用时，生成多个实例，共用一个子蓝图。资产身份来自文档范围和组件键，与显示名称无关。
- 实例上改过的文字、显隐和组件替换，存放在实例自己的 `NamedSlot` 局部内容里。
- 重导入从父页面发起，子蓝图随依赖一起更新；父页面上你保存的事件图、变量和手工控件会保留。
- 已在组件映射里指定了现有类的组件，优先使用映射，不再自动生成。

完整行为和失败恢复见 [组件蓝图生成](Docs/ComponentBlueprintGeneration.zh-CN.md)。

## 原型交互（Figma）

Figma 原型连线会转换成以下内容：

- 交互触发节点包一层 `Button`，多页面流程合成一个 `WidgetSwitcher` 根节点。
- 支持的单步鼠标动作（跳转、打开链接、弹层、返回）和键盘动作（跳转、打开链接），会直接连成原生蓝图节点。
- 支持的页面过渡会生成 `UWidgetAnimation`。

多步流程、条件分支、拖拽以及暂不支持的触发/动作组合不会连线，会在诊断报告中列出。这不是通用的交互图编译器，详见 [已知限制](KNOWN_LIMITATIONS.md#interaction-and-animation)。

## 插件设置

位置：`Edit > Editor Preferences > Plugins > Design2UMG`

| 设置 | 说明 |
| --- | --- |
| Design Width / Height | 设计画布尺寸 |
| Remote Image Import | 默认开启；已保存的用户偏好仍然有效。不安全或本地地址的图片 URL 默认禁止 |
| Generated Texture Budget (MiB) | 单次生成或预览中新产生的图片像素数据总量上限，默认 512，最小 1，下次操作生效。缓存复用不计入，重新生成计入。不影响下载、解码和单张图片限制，也不是编辑器内存总上限 |
| Icon Font 映射 | 图标字体到字形的映射 |
| Widget Class 覆盖规则 | 节点名包含（`Name Contains`，不区分大小写）指定文本时替换默认控件类，第一条命中的规则生效 |
| MCP Server Path / Pencil App Name | 覆盖自动发现的 Pencil MCP 服务器 |
| Known web font auto import | 实验功能，默认关闭，不建议用于正式流程 |

## 导入诊断报告

每次导入和自动化运行都会写出：

```text
<Project>/Saved/Design2UMG/ImportReports/*.import_report.json
```

排查导入问题时以这份报告为准。报告包含来源与产物的对应关系、降级项、资源诊断、特效包装和处理建议，字段定义见 [Docs/ImportDiagnosticContract.md](Docs/ImportDiagnosticContract.md)。

## 质量与验证

| 层级 | 做什么 | 在哪里运行 |
| --- | --- | --- |
| `repository-hygiene` | 公开仓库边界、文本规范、Python / PowerShell / Node 工具测试 | GitHub 托管运行机，每次 push / PR |
| `maintainer build and automation` | 严格编译插件、UAT 打包、聚焦 UE 自动化测试、上传候选包 | 维护者自托管 Windows 运行机 |
| `public smoke / public validate` | 本地构建和聚焦验证，用于复现问题 | 贡献者、维护者本地 |
| `maintainer formal admissible gate` | 对全部已发现用例做无回退的正式发布门禁 | 仅维护者 |

- CI 徽章反映整个 `Plugin CI` 工作流。外部 fork 的 PR 只跑托管检查；维护者推送、同仓库 PR 和手动触发还会请求自托管 UE 任务，没有运行机时该任务保持排队。
- 徽章**不代表**完整视觉回归，也不代表打包游戏运行验证。仓库不发布托管 UE 版本矩阵或覆盖率徽章。
- 验证工具扫描你提供的用例目录，发现多少项就跑多少项。维护者的私有视觉用例集不随仓库分发，也不是运行前提；空目录表示 0 项测试，不表示通过。

各层覆盖范围见 [测试覆盖边界](Docs/ValidationGovernance/TestCoverageBoundary.zh-CN.md)，工具用法见 [Tools/README.md](Tools/README.md)，正式门禁见 [验证协议](Docs/ValidationProtocol.zh-CN.md)。

<details>
<summary><b>自动化视觉验证（面向维护者）</b></summary>

视觉验证分为两层：

1. `Design2UMG.AutomationRender <spec.json>`：按清单文件把编译后的 Widget Blueprint 渲染成 PNG。
2. `Tools/design2umg_auto_verify.py`：准备设计参考图，无人值守启动 Unreal Editor 渲染候选图，输出差异图和 JSON 报告。

```text
python Tools/design2umg_auto_verify.py pen --source <DesignRoot>\screen.pen --node <NodeId> --reference-provider png --reference <DesignRoot>\screen.png --project <YourProject.uproject> --editor <UE5_Root>\Engine\Binaries\Win64\UnrealEditor.exe
```

```text
python Tools/design2umg_auto_verify.py local-figma --source <FigmaExportRoot>\design.json --reference-provider figma --figma-file <fileKeyOrUrl> --figma-node 123:456 --figma-token <FigmaAccessToken> --project <YourProject.uproject> --editor <UE5_Root>\Engine\Binaries\Win64\UnrealEditor.exe
```

- `--project` 可用 `DESIGN2UMG_RUNTIME_PROJECT` 代替，`--editor` 可用 `DESIGN2UMG_UNREAL_EDITOR` 或 `UE_ROOT` 代替。两者都没有时脚本直接失败，不会创建或推断 HostProject。
- 运行前需要准备字体 seed：放在设计源旁边或运行项目中可被发现的 `Content/Design2UMG/Fonts` 目录。脚本只复制，不下载字体，也不修复损坏的字体资产。
- 有效的 seed 是至少含一个 default typeface 条目、且条目绑定了有效 `FontFace`（或等价字体数据）的 UE `UFont`。单独的 `.ttf`、空 `UFont`、没有字体数据的条目都视为缺失。
- 正式的全量验证在维护者自己的运行环境中进行，这些路径不是面向用户的安装步骤。

</details>

<details>
<summary><b>CI 与发布包细节（面向维护者）</b></summary>

- 自托管运行机需要注册 `UE_ROOT`、`DESIGN2UMG_RUNTIME_PROJECT`、`DESIGN2UMG_EDITOR_TARGET` 三个变量。
- 隔离 CI 项目的 Editor 目标必须来自 `Tools/CI/Design2UMGVerificationEditor.Target.cs`（可按 `DESIGN2UMG_EDITOR_TARGET` 改文件名和类名）。只有插件两个模块关闭 unity 构建，引擎保持原有设置。
- CI 先核对已安装插件与检出源码的描述文件、Source、Config 哈希，再用 `-UsePrecompiled -NoEngineChanges -NoPCH -NoSharedPCH` 只严格重编插件模块，然后跑聚焦自动化测试，最后用常规 UAT `BuildPlugin` 打包并检查包边界。
- 产物为 `Design2UMG-<版本>-UE<引擎版本>-Win64.zip`，附带 `candidate.json` 和 `SHA256SUMS.txt`。Actions 产物保留 14 天，正式下载以 GitHub Releases 为准。
- 发布包和公开导出不得包含根目录评审报告、`openspec/`、AGENTS / CLAUDE 本地规则、原始自动化输出、缓存、HostProject fallback 产物及维护者绝对路径。

发布步骤见 [发布候选检查清单](Tools/Release/ReleaseCandidateChecklist.md) 和 [公开导出流程](Tools/Release/PublicRepoExportWorkflow.md)。

</details>

## 架构

```text
.pen / Figma / MCP
  → Parser / Bridge
  → Design2UMGConverter
  → LayoutCalculator
  → UMGGenerator
```

核心原则：**布局投影以 bounds 为准**。布局尺寸与 PNG 导出边界分别计算；导出边界还包含源设计定义的外部阴影和未裁剪内容。参考 PNG 尺寸不决定这两者。单轴 1 像素有效；超过 GPU 纹理尺寸的导出以分块方式渲染，受独立的 256 × 1024 × 1024 像素截图导出预算限制。模块边界和设计规则见 [架构文档](Docs/Architecture.zh-CN.md)，属性映射见 [PropertyMapping](Docs/PropertyMapping.md)。

## 常见问题

<details>
<summary><b>打包后的游戏需要带上这个插件吗？</b></summary>

不需要。插件只有编辑器模块，生成的是普通 Widget Blueprint，运行时不依赖插件。

</details>

<details>
<summary><b>文字的字体、宽度和设计稿对不上</b></summary>

插件不附带字体。请在插件设置里把设计中用到的字体映射到项目里有授权的 UE 字体资产，再查看导入报告中的缺失字体诊断。即使映射正确，UE 与设计工具的字体度量也不完全一致，文本尺寸只能做到近似。

</details>

<details>
<summary><b>启用插件后编辑器报错，或者提示模块需要重新编译</b></summary>

- 确认只有一份插件，项目 `Plugins/` 和引擎 `Marketplace/` 下不要同时存在。
- 预编译包必须和你的 UE 版本、平台一致。
- 蓝图项目没有 C++ 编译环境，只能使用匹配的预编译包。

</details>

<details>
<summary><b>再次导入同一份设计，生成了一个改了名的新资产</b></summary>

这是普通导入的预期行为，它从不覆盖已有资产。要在原资产上更新，请使用 [安全重导入](#更新已有资产安全重导入)。

</details>

<details>
<summary><b>安全重导入拒绝执行，报告 <code>reimport_full_conflict</code></b></summary>

插件无法确认这个资产和新设计的对应关系，比如来源文件换了，或者记录的实例找不到了。为了不破坏你的修改，它选择不动资产。确认来源是否正确；如果确实是另一份设计，改用普通导入。

</details>

<details>
<summary><b>远程图片没有导入</b></summary>

检查设置里的 Remote Image Import 是否开启。`http://` 和本地地址的图片默认禁止。具体原因写在导入报告的资源诊断里。

如果整次预览或生成被拒绝，并提示超出预算，说明新生成的图片总量超过了 Generated Texture Budget，可以结合机器内存适当调高。

</details>

<details>
<summary><b>Connect to Pencil 找不到当前设计稿</b></summary>

先在 VS Code 的 Pencil 设计编辑器里打开 `.pen` 文件。更新 Pencil 扩展后，要关闭并重新打开设计标签页。如果自动发现失败，可以在插件设置中手动指定 MCP Server Path。

</details>

## 获取帮助

- 使用问题和 bug：请提交 [Issue](https://github.com/DiracSea12/Design2UMG/issues)，写明 UE 版本、插件版本和设计来源。附上导入报告前，请先检查里面有没有私有路径或设计内容。
- 安全问题：请按 [安全策略](SECURITY.md) 私下报告，不要公开提交 Issue。

## 更多文档

- [已知限制](KNOWN_LIMITATIONS.md)
- [路线图](Docs/ROADMAP.zh-CN.md)
- [更新日志](CHANGELOG.md)
- [贡献指南](CONTRIBUTING.zh-CN.md)
- [安全策略](SECURITY.md)

## 许可证

[MIT](LICENSE)
