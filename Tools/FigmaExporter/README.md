# Design2UMG - Figma 本地导出导入指南

## 概述

当 Figma REST API 不可用时（如免费账户配额耗尽），可通过本地导出方式将 Figma 设计稿导入 UE5。

流程：**Figma 桌面版导出 → 本地文件 → UE5 编辑器导入**

## 前置条件

- Figma 桌面版（非浏览器版）
- UE5 编辑器 + Design2UMG 插件已编译

## Step 1：安装 Figma Plugin

1. 打开 Figma 桌面版
2. 菜单 → **Plugins** → **Development** → **Import plugin from manifest...**
3. 选择本仓库里的 `manifest.json`：

```
<PluginRoot>\Tools\FigmaExporter\manifest.json
```

安装完成后，插件会出现在 Development 插件列表中。

## Step 2：从 Figma 导出

1. 在 Figma 中选中要导出的 **Frame**（或 Component Set / Component / Instance）；也可以在插件的 **Root node ID** 输入框填写根节点 ID（例如 `116:136`），留空则使用当前选中节点。Component Set 会包含其全部变体。
2. 右键 → **Plugins** → **Development** → **Design2UMG Exporter**
3. 设置 **Scale**（图片缩放倍数，默认 2x，推荐保持）
4. 点击 **Export Frame**
5. 自动下载单个 `figma_export.zip` 文件

导出的 ZIP 内容：

```
figma_export.zip
├── design.json              # REST API 兼容的设计数据
├── reference.png            # Figma 对选中根节点的官方 PNG 渲染（与 design.json 同次导出）
└── images/
    └── {imageHash}.png      # 图片填充素材
```

## Step 3：在 UE5 中导入

1. 解压 `figma_export.zip` 到任意目录
2. 打开 UE5 编辑器
3. 菜单栏 → **Design2UMG** → **Import Local Figma Export...**
4. 在文件对话框中选择解压出的 `design.json`
4. 插件会自动：
   - 解析 JSON 设计数据
   - 扫描同级 `images/` 目录匹配图片
   - 将图片导入为 UE5 纹理资产，保存目录由 `Remote Image Import Path` 设置决定，默认 `/Game/Design2UMG/ImportedImages`
   - 打开预览窗口展示转换结果

## 注意事项

- **图片目录必须与 design.json 同级**，且命名为 `images/`
- 图片文件名必须是 `{imageRef}.png` 格式（插件自动生成，无需手动修改）
- 重复导入同一设计时，已存在的纹理资产会被复用，不会重复创建
- 矢量节点（VECTOR、STAR、LINE 等）会被自动渲染为 PNG 导出
- `reference.png` 由 Figma Plugin API 对选中的根节点直接渲染，用作该导出的权威参考图；不会通过 JSON 或其他图片后处理补背景

## 故障排查

| 问题 | 原因 | 解决 |
|------|------|------|
| 导出后没有 images 目录 | 设计中没有图片填充或矢量节点 | 正常现象，纯色/渐变不需要图片 |
| UE5 导入后图片缺失 | images/ 目录不在 design.json 同级 | 确保目录结构正确 |
| 解析失败 | JSON 格式损坏 | 重新从 Figma 导出 |
| Plugin 不在菜单中 | manifest 未正确导入 | 重新 Import plugin from manifest |

## 技术细节

### 依赖数据与未支持行为

- 导出器按真实实例主组件、嵌套实例和 `CHANGE_TO` 目标收集完整组件定义，保持现有 `design2umgComponentDependencies` schemaVersion 1。组件身份来自当前源节点，未发布组件不编造 published key。
- 官方 `JSON_REST_V1` 保留完整节点树和官方字段；每个节点以当前 Plugin API 的 `reactions` 为权威覆盖交互，再核对目标、动作和触发器。根、组件定义、导航页共用同一路径，逐项按真实 `source.children` 顺序组回；官方整树遗漏的子节点（包括隐藏节点）必须单独取得该源节点的官方 JSON，再执行同样的完整字段读取、身份与子树校验。官方导出失败、身份不符或出现源树外子项时明确报错并保留两边子项证据，不生成手动降级包。外部库定义中的目标不能用本文件中碰巧相同的节点 id 解析；远程 `CHANGE_TO` 只从源节点实际所属、API 可访问的组件集合中按精确 ID 查找并导出目标定义。集合或目标不可访问、远程目标不属于该集合，以及远程跨页导航，仍明确报错。
- 普通导航、弹层目标及其组件依赖继续求闭包，选中树外的真实 FRAME 放在 `design2umgPrototypePages` 数组；图片填充和描边资源也从这些目标及组件定义收集。空动作、空触发器不产生依赖；[API 允许的显式 `destinationId: null`](https://developers.figma.com/docs/plugins/api/Action/)（例如目标已删除）完整保留，但不产生待解析目标。不修改 Figma 原稿或交互语义，仅在明确生产者边界换算已核实的时长单位。目标字段未定义、空字符串、实际指定的目标不可读取或定义缺失仍明确报错，不按名称猜测。
- 页面包是源数据，不表示运行时支持：`design2umgPrototypePages` 收集到额外页，不能据此认定 UE 已生成这些页面的交互或动画。UE 生成端被拒绝的动画不会为该动作生成执行按钮，也不会经该动作将目标纳入运行时依赖导航页；同稿其他受支持动作仍可生成，包内源数据仍按导出闭包保留。
- `CHANGE_TO` 的有限基础属性动画已实现为可编辑、可保存重开的原生 `UWidgetAnimation`：位置、旋转、透明度、单一纯色，以及能准确等价为渲染缩放的尺寸变化（仅无子项、无圆角、无描边或特效的实心矩形）。真实起止树须保持同名子项、层级和绘制顺序，使用当前动作自己的时长及可精确表达的曲线；缺值、歧义或超范围时明确诊断拒绝，不瞬切、不近似补齐。点击/`MOUSE_ENTER` 不自动恢复，受支持 `ON_HOVER` 状态变化移出时反向播放，页面进入后可按 `AFTER_TIMEOUT` 明确延时播放。`SMART_ANIMATE` 弹层不支持，普通无动画弹层仍保留。定向功能验证不代表全量验收；完整视觉及 `no_regression` 结论以同输入、同标准图的逐项回归证据为准，来源、覆盖及已有警告见 [交互与动画限制](../../KNOWN_LIMITATIONS.md#interaction-and-animation)。
- `Tools/design2umg_figma_fetch.py` 的 build 使用一次完整 `geometry=paths` 响应及纯内存 `prepare_design` 函数准备同一数据合同。离线调用可以省略未知 fileKey；只有真实请求提供的 file key 才写入该字段，文档版本和更新时间保留原值，不推断。

### 当前源字段、Motion 与资源完整性

- 每个节点的 `design2umgPluginData` 保存当前 API 可读取的数据属性，逐项读取宿主 getter，不对 SceneNode 代理直接 `JSON.stringify`。节点树仍放在官方 `children`，节点引用只保存身份。文本另保存完整 `styledTextSegments`，包含混合样式、装饰、列表、变量和字符填充。
- 原始元数据中的非有限数使用可逆标记 `{ "type": "NON_FINITE_NUMBER", "value": "Infinity" }` 保存；`value` 区分 `Infinity`、`-Infinity` 和 `NaN`。例如 [LayoutGrid 的 Auto count](https://developers.figma.com/docs/plugins/api/LayoutGrid/) 在 Plugin API 中允许为 `Infinity`。有限数保持数字，官方 REST 字段保持原值；标记不替代为 null/0，也不导致整项源字段遗漏。
- paint / effect 数据对象保留所有可读参数，并做必要的 REST 字段映射（例如 `imageHash → imageRef`、渐变 handle）。NOISE、TEXTURE、GLASS 等效果不再受旧字段白名单限制；同时保留官方记录中额外的字段。这些源参数被保存，不表示 UE 已支持其绘制。
- `animationStyles`、`animations`、`manualKeyframeTracks`、`timelines` 原样保存；Motion 时间使用源 API 的秒，并标记 `design2umgMotionTimeUnit: "SECONDS"`。这与下面 reaction 的毫秒合同分开，不能把 Motion 时间按 reaction 单位解释。
- 账号或版本限制导致的不可读属性写入节点和包级 `design2umgExportDiagnostics`，UI 显示警告；不可读 Motion 不伪装成空数组，也不能据此宣称该稿全部动画数据完整。
- 组件/组件集合的属性定义或变体信息 getter 报错，也会记录到相应元数据及包级诊断并显示警告；不把不可读定义伪装为空定义。节点树、交互目标、组件身份和资源的缺失仍阻断导出。
- PATTERN 的 `sourceNodeId` 参与依赖闭包，额外源树放在 `design2umgComponentDependencies.sourceNodes`。变量、变量别名闭包、变量集合及样式定义放在 `design2umgSourceDataDependencies`；定义不可读取时明确失败。样式和变量依赖从各节点的原始 Plugin 数据（含字符样式及 Motion）和实际读取的源定义收集；官方 REST 的 `styles`、`inheritFillStyleId` 等引用保持原样，不当作 Plugin 样式 ID 查询，也不按字符串格式猜测身份。
- 图片收集覆盖根、依赖、字符覆盖表、样式和 Motion 数据中的图片引用。缺图片明确失败。当前 Plugin API 无法读取视频原始字节或 shader 源码，这类已引用资源明确报错，禁止用节点渲染图替代。
- 已知结构和目标错误在资源收集前立即终止，资源阶段新增的错误仍单独阻断交付。图片与参考 PNG 使用 [Figma UI 消息支持的 Uint8Array](https://developers.figma.com/docs/plugins/api/properties/figma-ui-postmessage/) 传递，保留全部资源和所选 Scale。
- 导出准备完成后，JSON 与二进制按块传到 UI，每次等待接收确认后再发送下一块。文本块最多 1,048,576 个 UTF-16 码元且不拆开代理对，二进制块最多 1 MiB；不再发送携带整个 JSON 和全部图片的巨型消息。UI 逐块做 UTF-8 编码并累计文件长度与 CRC32，ZIP 直接使用这些字节块，不重新拼接完整字符串或整份文件字节数组。文件长度与总文件数不完整时拒绝交付；已确认的源侧 JSON 和图片逐文件释放。包内路径、完整 JSON 内容、PNG 字节和 Scale 不变。
- 重新导出请解压到新的独立目录，保留旧设计文件、参考、登记基线和历史运行记录。

### 动画时间单位

- 规范 `interactions` 只使用当前 API 的 `actions` 列表，不输出源 API 同时暴露的已弃用 `action` 镜像，避免与毫秒合同混用。源 `reactions` 的全部字段和秒值仍原样保留在 `design2umgPluginData`。
- 新版导出器在已核实的 Plugin 生产者边界，将 reaction 的 `duration` 和 `AFTER_TIMEOUT.timeout` 从秒换成毫秒，并在导出包写入 `design2umgPrototypeDurationUnit: "MILLISECONDS"`。例如 `duration: 0.2` 输出 200，`duration: 0.5` 输出 500，`timeout: 1` 输出 1000；UE 再转换成原生播放接口所需的秒。
- 新属性动画要求已验证的单位标记，不按数值大小猜来源、不自动修补旧包。HTTP/REST 获取路径的动画单位尚未核实，因此拒绝这条路径的新属性动画；这不意味着静态 HTTP/REST 设计导入不可用。计时数据保留和换算也不代表任意计时逻辑或完整状态机已受支持。
- 定向保存重开与原生播放测试已通过；一份真实 Toggle 恢复包已验证点击移动/纯色的中间与终点。该包使用缓存 HTTP 几何及同文件同节点的 live Plugin 时间见证，不是新鲜完整 Plugin 导出，也不证明所有 HTTP 动画单位。四份动画稿现均有新鲜完整 Plugin 导出包，已验证真实静态生成、重载及渲染；Loading 另已验证 `AFTER_TIMEOUT` 原生播放。Credit `CUSTOM_SPRING`、SmartHome 图层重排、Bloodborne 新增焦点层动画及跨状态实例文字覆盖仍未实现；详见 [验证边界](../../KNOWN_LIMITATIONS.md#interaction-and-animation)，静态通过不代表四份动画均通过。

### 属性映射

Figma Plugin API 与 REST API 的字段名有差异，插件自动处理了以下映射：

| Figma Plugin API | 导出 JSON（REST API 格式） |
|---|---|
| `node.type: POLYGON` | 官方 REST `type: REGULAR_POLYGON`；原始 Plugin `type` 保存在 `design2umgPluginData` |
| `fills[i].imageHash` | `fills[i].imageRef` |
| `node.textAutoResize` | `textAutoResize`（节点根级别） |
| `node.fontSize/fontName/...` | `style: { fontSize, fontFamily, lineHeightUnit, ... }` |
| `node.strokeTopWeight` 等 | `individualStrokeWeights: { top, right, bottom, left }` |
| `node.mainComponent.id` | `componentId` |

### 文件位置

```
Design2UMG/
├── Source/Design2UMGEditor/Private/
│   ├── PenRemoteImageImporter.cpp    # 本地文件路径支持
│   └── Design2UMGEditorModule.cpp    # 本地导入入口 + 菜单
└── Tools/FigmaExporter/
    ├── manifest.json                  # Figma 插件清单
    ├── code.ts                        # 节点序列化 + 图片导出
    └── ui.html                        # 导出 UI
```
