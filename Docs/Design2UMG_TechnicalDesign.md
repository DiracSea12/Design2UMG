# Design2UMG 技术设计文档 v1.0

> 将 Pencil.dev 的 .pen 设计文件转换为 UE5 Widget Blueprint

---

## 1. 项目概述

### 1.1 背景

Design2UMG 插件当前面临两大核心痛点：
1. **位置/布局不准** - AI从截图识别像素坐标存在误差
2. **元素识别不全** - AI可能遗漏或错误识别UI元素

Design2UMG 模块通过引入 Pencil.dev 作为中间层，从根本上解决这些问题。

### 1.2 核心理念

```
原有流程（问题多）:
Screenshot → AI猜测 → 不精确的坐标/类型 → 需要修复 → UMG

新流程（精确）:
自然语言 → Pencil Vibe Design → .pen文件 → 精确数据 → UMG
```

### 1.3 收益对比

| 指标 | Design2UMG (AI识别) | Design2UMG (.pen文件) |
|------|---------------------|---------------------|
| 位置精度 | MAE≤4px (目标) | **结构精确*** |
| 元素识别率 | 90% (目标) | **支持范围内完整** |
| 外部依赖 | AI API调用 | **零依赖** |
| 运行成本 | 每次调用付费 | **完全免费** |
| 处理速度 | 秒级（网络延迟） | **毫秒级** |
| 可重复性 | 不稳定 | **确定性输出** |

> **\*精度说明**：.pen 文件提供精确的结构数据，但最终渲染结果受以下因素影响：
> - 字体度量差异（Pencil 使用 Web 字体，UE 使用引擎字体）
> - 文本换行算法差异
> - DPI/子像素对齐差异
> - 渐变/矢量等特性的降级处理
>
> 预期偏差：文本元素 ±2-4px，其他元素 ±1px

---

## 2. .pen 文件格式规范

### 2.1 基础结构

```json
{
  "version": "2.6",
  "children": [...],
  "themes": {"Mode": ["Light", "Dark"]},
  "variables": {...}
}
```

**顶层字段说明**：

| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `version` | `string` | 是 | .pen 文件格式版本 |
| `children` | `array` | 是 | 顶层节点数组 |
| `fonts` | `array` | 否 | 自定义字体定义，每项含 `name` 和 `url` |
| `themes` | `object` | 否 | 主题轴定义，键为轴名，值为该轴的主题值数组 |
| `variables` | `object` | 否 | 变量定义，键为变量名，值为变量对象 |

### 2.2 节点类型 (Node Types)

| 类型 | 说明 | 关键属性 |
|------|------|----------|
| `frame` | 容器/画框 | layout, gap, padding, clip |
| `rectangle` | 矩形 | cornerRadius, fill, stroke |
| `ellipse` | 椭圆 | innerRadius, startAngle, sweepAngle, fill, stroke |
| `line` | 线条 | stroke |
| `polygon` | 正多边形 | polygonCount, cornerRadius, fill, stroke |
| `text` | 文本 | content, fontSize, fontFamily, fontWeight |
| `icon_font` | 图标字体 | iconFontName, iconFontFamily, weight |
| `path` | 矢量路径 | geometry (SVG path), fillRule |
| `ref` | 组件实例引用 | ref (指向reusable节点的id) |
| `group` | 分组 | children |
| `connection` | 连接线 | source, target, stroke（设计工具专用，转换时忽略）|
| `note` | 注释 | 设计工具专用，转换时忽略 |
| `prompt` | AI 提示 | 设计工具专用，转换时忽略 |
| `context` | 上下文 | 设计工具专用，转换时忽略 |

> **图片说明**：.pen 中**没有 `image` 节点类型**。图片通过 `frame` 或 `rectangle` 的 `fill` 属性实现：
> ```json
> {
>   "type": "frame",
>   "fill": {"type": "image", "url": "./image.png", "mode": "fill"}
> }
> ```
> `mode` 可选值：`"stretch"` | `"fill"` | `"fit"`

> **connection 端点结构**：`source` 和 `target` 为 `ConnectionEndpoint` 对象：
> ```json
> {
>   "path": "nodeId",           // 目标节点 ID 路径
>   "anchor": "center"          // 锚点位置：center | top | left | bottom | right
> }
> ```

### 2.3 通用属性

#### 2.3.1 定位与尺寸属性
```json
{
  "id": "abc123",           // 唯一标识符（禁止包含 `/` 字符）
  "name": "Button",         // 显示名称
  "x": 100,                 // X坐标（像素）*
  "y": 200,                 // Y坐标（像素）*
  "width": 150,             // 宽度：数值 | "fill_container(min)" | "fit_content(min)"
  "height": 50,             // 高度：数值 | "fill_container(min)" | "fit_content(min)"
  "rotation": 0,            // 旋转角度（度，逆时针）
  "enabled": true,          // 可见性控制
  "opacity": 1.0,           // 不透明度 0-1
  "flipX": false,           // 水平翻转
  "flipY": false,           // 垂直翻转
  "metadata": {"type": "custom", "key": "value"}  // 自定义元数据（转换时忽略）
}
```

**enabled/opacity/flip 映射**：

| .pen 属性 | UMG 映射 | 说明 |
|-----------|----------|------|
| `enabled: false` | `Widget->SetVisibility(ESlateVisibility::Collapsed)` | 隐藏且不占空间 |
| `opacity` | `Widget->SetRenderOpacity(value)` | 渲染不透明度 |
| `flipX` | `RenderTransform.Scale.X = -1` | 水平翻转 |
| `flipY` | `RenderTransform.Scale.Y = -1` | 垂直翻转 |

> **\*x/y 布局约束**：当父节点使用 flex 布局（`layout: "vertical"` 或 `"horizontal"`）时，子节点的 `x`/`y` 属性会被忽略，位置由布局引擎自动计算。仅当父节点 `layout: "none"` 时，`x`/`y` 才生效。

**尺寸值类型与解析规则**：

| 值类型 | 示例 | 解析结果 (FPenSize) | 说明 |
|--------|------|---------------------|------|
| 数值 | `120` | `{Fixed, 120}` | 固定像素尺寸 |
| 数值 0 | `0` | `{Fixed, 0}` | **line 节点合法**，其他类型视为 fit_content |
| `"fill_container"` | `"fill_container"` | `{FillContainer, 0}` | 填充父容器 |
| `"fill_container(min)"` | `"fill_container(500)"` | `{FillContainer, 500}` | 填充父容器，fallback 最小值 500 |
| `"fit_content"` | `"fit_content"` | `{FitContent, 0}` | 适应内容 |
| `"fit_content(min)"` | `"fit_content(900)"` | `{FitContent, 900}` | 适应内容，最小值 900 |
| 缺省/null | 无该字段 | `{FitContent, 0}` | **默认为 fit_content** |

**解析优先级**：
1. 若值为字符串 `"fill_container"` 或 `"fill_container(N)"`，解析为 `FPenSize::Fill(N)`
2. 若值为字符串 `"fit_content"` 或 `"fit_content(N)"` 或缺省，解析为 `FPenSize::Fit(N)`
3. 若值为数值或可转换为数值的字符串且 > 0，解析为 `FPenSize::Fixed(value)`
3a. 若值为 0 且节点类型为 `line`，解析为 `FPenSize::Fixed(0)`（line 节点特例）
3b. 若值为 0 且节点类型非 `line`，解析为 `FPenSize::Fit(0)`
**size 对象兼容（转换器扩展，Schema 不支持）**：

> **注意**：Schema 没有 `size` 对象，尺寸直接通过顶层 `width`/`height` 属性定义。以下兼容逻辑仅用于处理非标准 .pen 文件。

部分非标准 .pen 文件使用 `size` 对象承载尺寸：
```json
{
  "type": "text",
  "size": { "width": 188 }
}
```

解析时需检查 `size` 对象并提取其中的 `width`/`height`：
```cpp
// 解析优先级：顶层 width/height > size 对象（顶层优先）
// 先检查 size 对象作为 fallback
if (JsonObj->HasField(TEXT("size")))
{
    const TSharedPtr<FJsonObject>& SizeObj = JsonObj->GetObjectField(TEXT("size"));
    if (SizeObj->HasField(TEXT("width")))
        Node.Width = FPenSize::Fixed(SizeObj->GetNumberField(TEXT("width")));
    if (SizeObj->HasField(TEXT("height")))
        Node.Height = FPenSize::Fixed(SizeObj->GetNumberField(TEXT("height")));
}
// 顶层 width/height 优先级更高，会覆盖 size 对象的值
if (JsonObj->HasField(TEXT("width")))
    ParseWidthValue(JsonObj->TryGetField(TEXT("width")), Node);
if (JsonObj->HasField(TEXT("height")))
    ParseHeightValue(JsonObj->TryGetField(TEXT("height")), Node);
```

**辅助判定函数**：

```cpp
// 判断节点是否为固定像素宽度
bool IsFixedPixelWidth(const FDesignNode& Node)
{
    return Node.Width.Mode == EPenSizeMode::Fixed && Node.Width.Value > 0;
}

// 判断节点是否为固定像素高度
bool IsFixedPixelHeight(const FDesignNode& Node)
{
    return Node.Height.Mode == EPenSizeMode::Fixed && Node.Height.Value > 0;
}

// 判断节点是否为 Collapsed（不可见且不占空间）
bool IsCollapsed(const FDesignNode& Node)
{
    return !Node.bEnabled;  // .pen 中 enabled=false 映射为 Collapsed
}
```

#### 2.3.2 布局属性 (frame/group 通用)
```json
{
  "layout": "vertical",     // "none" | "vertical" | "horizontal"（frame 默认 horizontal，group 默认 none）
  "gap": 10,                // 子元素间距（可为负值）
  "padding": 16,            // 内边距：数值 | [h,v] | [top,right,bottom,left]
  "justifyContent": "center", // 主轴对齐: "start" | "center" | "end" | "space_between" | "space_around"
  "alignItems": "center",   // 交叉轴对齐: "start" | "center" | "end"
  "clip": true,             // 是否裁剪溢出内容（仅 frame）
  "placeholder": true,      // 占位容器：子元素使用绝对定位（仅 frame）
  "layoutIncludeStroke": false, // 布局计算是否包含描边厚度
  "theme": {"Mode": "Light"}  // 节点级主题设置（Entity 级属性，所有节点可用）
}
```

#### 2.3.3 填充属性
```json
// 纯色填充（字符串格式，支持 #RGB、#RRGGBB 或 #RRGGBBAA）
"fill": "#2DE"           // 3位简写，等同于 #22DDEE
"fill": "#22D3EE"        // 6位 RGB
"fill": "#ff0000ff"      // 8位 RGBA

// 纯色填充（对象格式，支持变量和启用控制）
"fill": {
  "type": "color",
  "color": "$--primary-color",
  "enabled": true
}

// 渐变填充
"fill": {
  "type": "gradient",
  "gradientType": "linear",  // "linear" | "radial" | "angular"
  "rotation": -180,
  "colors": [
    { "color": "#0000ffff", "position": 0 },
    { "color": "#ff0000ff", "position": 1 }
  ]
}

// 透明填充（转换器扩展，Schema 仅支持 hex 格式）
"fill": "transparent"

// 空数组（清除填充，用于 ref 节点覆盖）
"fill": []

// 多层填充（数组格式，按顺序叠加）
"fill": [
  {"type": "color", "color": "#ffffff"},
  {"type": "gradient", "gradientType": "linear", "colors": [...]}
]
```

> **Schema 额外属性（UMG 不支持）**：填充对象支持 `blendMode`（18种混合模式）、`opacity`；渐变还支持 `center`、`size`；`mesh_gradient` 类型整体不支持

#### 2.3.4 描边属性
```json
"stroke": {
  "align": "inside",        // "inside" | "outside" | "center"
  "thickness": 1,           // 数值 或 {top, right, bottom, left} 单边描边
  "fill": "#000000ff"       // 可选，描边颜色/渐变
}

// 单边描边示例
"stroke": {
  "align": "inside",
  "thickness": { "bottom": 1 },  // 仅底部 1px 描边
  "fill": "$--border"
}
```

> **UMG 限制**：UBorder 仅支持 `inside` 描边效果。`outside` 和 `center` 对齐会被当作 `inside` 处理，可能导致视觉偏差。

> **Schema 额外属性（UMG 不支持）**：`join`（miter/bevel/round）、`cap`（none/round/square）、`miterAngle`、`dashPattern`

**单边描边映射**：UBorder 的 `BrushMargin` 可实现单边效果，通过设置对应边的 margin 为描边宽度。

#### 2.3.5 文本属性
```json
{
  "content": "Hello World",
  "fontFamily": "Inter",
  "fontSize": 16,
  "fontWeight": "normal",   // "normal" | "bold" | "100"-"900"
  "lineHeight": 1.2,
  "textAlign": "left",        // "left" | "center" | "right" | "justify"
  "textAlignVertical": "middle", // "top" | "middle" | "bottom"
  "textGrowth": "fixed-width-height" // 文本框大小模式
}
```

**content 富文本格式**（转换器扩展，Schema 的 TextStyle 不含 content 字段）：
```json
{
  "content": [
    {"content": "Bold text ", "fontWeight": "bold"},
    {"content": "italic text", "fontWeight": "normal", "fontStyle": "italic"}
  ]
}
```

> **说明**：Schema 定义 `TextContent = StringOrVariable | TextStyle[]`，但 `TextStyle` 接口不含 `content` 字段。上述富文本格式是转换器的扩展处理方式，用于支持混合样式文本。实际 .pen 文件中较少使用此格式。

> **Schema 额外属性**：`letterSpacing`、`fontStyle`、`underline`、`strikethrough`、`href`（UMG 部分支持）

**textGrowth 模式说明**：

> **重要约束**：文本默认仍以 `textGrowth` 为主语义；但当文本需要和运行时换行保持一致时，`justify` 与 `fill_container(N)` 这类“已知宽度”的 `auto` 文本也会参与 wrap 宽度解析与导出。

| textGrowth 值 | 说明 | 宽度行为 | 高度行为 |
|--------------|------|---------|---------|
| `"auto"` | 自动模式 | 默认适应内容；`justify` / `fill_container(N)` 时按已知宽度参与 wrap | 适应内容 |
| `"fixed-width"` | 固定宽度 | 固定值 | 自动换行后适应 |
| `"fixed-width-height"` | 固定宽高 | 固定值 | 固定值（可能裁剪） |
| 缺省 | 默认自动 | 适应内容 | 适应内容 |

### 2.4 组件系统

#### 2.4.1 可复用组件定义
```json
{
  "type": "frame",
  "id": "ButtonComponent",
  "name": "Button",
  "reusable": true,         // 标记为可复用组件
  "slot": ["recommendedCompId1", "recommendedCompId2"],  // 插槽：推荐插入的组件 ID 列表
  "context": "Primary action button with icon and label",  // 组件语义描述
  "children": [...]
}
```

**slot 属性说明**：
- 类型：字符串数组（推荐组件 ID 列表）
- 用途：声明该插槽位置推荐插入的组件类型
- 实例通过 `descendants` 属性覆盖插槽内容，或直接向插槽路径插入子节点

#### 2.4.2 组件实例引用
```json
{
  "type": "ref",
  "id": "button1",
  "ref": "ButtonComponent", // 引用组件ID
  "x": 100,
  "y": 200,
  "name": "Submit Button"
}
```

#### 2.4.3 可复用的变体组件

ref 节点也可标记为 `reusable: true`，形成派生组件，通过 `descendants` 覆盖子节点属性：

```json
{
  "type": "ref",
  "id": "AlertSuccess",
  "ref": "AlertBase",
  "reusable": true,
  "descendants": {
    "iconId": { "fill": "$--success" },
    "textId": { "content": "Success message" }
  }
}
```

**descendants 两种用法**：

1. **属性覆盖**：仅覆盖指定属性，不能包含 `id`、`type`、`children`
2. **对象替换**：必须包含 `type`，用完整新节点树替换原节点

```json
{
  "type": "ref",
  "id": "Dialog",
  "ref": "CardBase",
  "descendants": {
    "iconId": { "fill": "$--warning" },
    "headerSlot": {
      "type": "frame",
      "layout": "vertical",
      "children": [
        {"type": "text", "content": "Dialog Title", "fontSize": 18},
        {"type": "text", "content": "Description", "fontSize": 14}
      ]
    }
  }
}
```

**descendants 属性说明**：
- 类型：对象，键为子节点 ID 或路径，值为覆盖内容
- 属性覆盖模式：值中不含 `type`，仅覆盖指定属性（禁止 `id`/`type`/`children`）
- 对象替换模式：值中包含 `type`，用完整节点树替换原节点

### 2.5 变量系统

**.pen 变量结构**（Schema 定义）：

```json
{
  "themes": {
    "Mode": ["Light", "Dark"]
  },
  "variables": {
    "--primary": {
      "type": "color",
      "value": "#3B82F6"
    },
    "--primary-themed": {
      "type": "color",
      "value": [
        {"value": "#3B82F6"},
        {"theme": {"Mode": "Dark"}, "value": "#60A5FA"}
      ]
    },
    "--radius-xs": {
      "type": "number",
      "value": 4
    }
  }
}
```

**结构说明**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `themes` | `object` | 主题轴定义，键为轴名（如 `"Mode"`），值为主题值数组 |
| `variables` | `object` | 变量映射，键为变量名，值为变量定义对象 |
| `type` | `string` | 变量类型：`"boolean"` / `"color"` / `"number"` / `"string"` |
| `value` | `T \| array` | 单值或主题值数组，每项含 `value` 和可选 `theme` 对象 |

**变量引用格式**：`$variableName`（`$` 前缀）

```json
{
  "fill": "$--primary",
  "cornerRadius": "$--radius-xs"
}
```

**变量解析说明**：
- 变量名是任意字符串（`--` 前缀是约定，非强制）
- 节点引用时使用 `$variableName` 格式
- `value` 可以是单值或数组；数组时每项含 `value` 字段和可选 `theme` 对象
- 解析时根据当前主题匹配 `theme` 对象取对应值

---

## 3. 类型映射规则

### 3.1 pen → UMG Widget 映射

| .pen type | layout属性 | UMG Widget | 说明 |
|-----------|------------|------------|------|
| `frame` | `"none"` | `CanvasPanel` | 绝对定位容器 |
| `frame` | `"vertical"` | `VerticalBox` | 垂直布局 |
| `frame` | `"horizontal"` 或缺省 | `HorizontalBox` | 水平布局（默认） |
| `rectangle` | - | `Border` | 带背景的容器 |
| `ellipse` | - | `Image` | 椭圆，运行时生成纹理（见 3.12 节） |
| `line` | - | `Image` | 线条，运行时生成纹理（见 3.12 节） |
| `polygon` | - | `Image` | 多边形，运行时生成纹理（见 3.12 节） |
| `text` | - | `TextBlock` | 文本显示 |
| `icon_font` | - | `TextBlock` | 图标字体（见 3.9 节） |
| `path` | - | `Image` | SVG路径渲染为纹理（见 3.11 节） |
| `group` | `"none"` 或缺省 | `CanvasPanel` | 分组容器，绝对定位 |
| `group` | `"vertical"` | `VerticalBox` | 分组容器，垂直布局 |
| `group` | `"horizontal"` | `HorizontalBox` | 分组容器，水平布局 |
| `ref` | - | 递归展开 | 展开引用的组件 |
| `connection` | - | **不支持** | 设计工具专用（连接线） |
| `note` | - | **不支持** | 设计工具专用（注释） |
| `prompt` | - | **不支持** | 设计工具专用（AI提示） |

> **注意**：`context` 既是节点类型（Schema 中 `type: "context"`），也可作为节点属性（字符串），用于描述组件的语义/用途。作为节点类型时，转换时忽略。

**prompt 节点格式**（设计工具专用，转换时忽略）：

```json
{
  "type": "prompt",
  "content": "Design a dashboard with sidebar...",
  "model": "cursor-composer",
  "width": 400,
  "height": 219,
  "x": 100,
  "y": 200
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `content` | `string` | AI 提示文本 |
| `model` | `string` | 目标模型标识（如 `"cursor-composer"`） |

**group 节点尺寸规则**：

group 节点的尺寸由其子元素边界决定：
- group 的 width/height 仅支持 `SizingBehavior` 字符串（如 `"fit_content"`、`"fill_container"`），不支持数值
- 若未指定，默认计算所有子元素的包围盒作为 group 尺寸
- 子元素坐标相对于 group 左上角

**frame 视觉属性处理**：

UMG 的 CanvasPanel/VerticalBox/HorizontalBox 本身不渲染背景。当 frame 有视觉属性时：

| frame 属性 | 处理方式 |
|-----------|---------|
| 无 fill/stroke | 直接使用容器 |
| 有 fill 或 stroke | 外层包装 Border，容器作为 Border 子元素 |
| cornerRadius | 通过九宫格纹理或材质实现圆角* |
| clip | 容器设置 `Clipping=ClipToBounds` |

> *`cornerRadius` 说明：UBorder 本身不支持圆角属性。通过九宫格纹理实现圆角效果，详见 3.7 节。

### 3.2 布局属性映射

| .pen 属性 | UMG 属性 | 转换规则 |
|-----------|----------|----------|
| `x`, `y` | `CanvasPanelSlot.Position` | 仅 CanvasPanel 子元素，见 3.4 |
| `width`, `height` | `CanvasPanelSlot.Size` / `SizeBox` | 仅 CanvasPanel 或 SizeBox 包装，见 3.4 |
| `rotation` | `RenderTransform.Angle` | 直接使用（度） |
| `gap` | 子元素 `Padding` | 分配给非首个**真实**子元素，见下方说明 |
| `padding` | `UBorder` 包装 | 用无画刷 UBorder 包装容器，设置 ContentPadding |
| `alignItems` | `HAlign` / `VAlign` | 交叉轴对齐，见 3.3 |
| `justifyContent` | Spacer 控件 | 主轴对齐，见 3.3 |

> **Slot 类型说明**：`Position` 和 `Size` 属性仅存在于 `UCanvasPanelSlot`。`UVerticalBoxSlot`/`UHorizontalBoxSlot` 使用 `FSlateChildSize` 控制尺寸，无 Position 属性。

**padding 实现**：

用无画刷`UBorder`包装容器，通过`ContentPadding`实现四边内边距：

```cpp
// padding 通过 UBorder 包装实现容器级内边距
UWidget* ApplyPadding(UPanelWidget* Container, const FDesignNode& Node, UWidgetTree* WidgetTree)
{
    if (Node.Padding.Left == 0 && Node.Padding.Top == 0 &&
        Node.Padding.Right == 0 && Node.Padding.Bottom == 0)
        return Container;

    UBorder* PaddingWrapper = WidgetTree->ConstructWidget<UBorder>();
    PaddingWrapper->SetBrushColor(FLinearColor::Transparent);
    PaddingWrapper->SetPadding(FMargin(Node.Padding.Left, Node.Padding.Top,
                                        Node.Padding.Right, Node.Padding.Bottom));
    PaddingWrapper->SetContent(Container);
    return PaddingWrapper;
}
```

**rotation 映射**：

> **限制**：RenderTransform不参与布局计算，旋转后的AABB不会反映到Slot尺寸。仅对固定位置的CanvasPanel子元素调整Slot。

```cpp
void ApplyRotation(UWidget* Widget, const FDesignNode& Node, UPanelSlot* Slot)
{
    if (Node.Rotation == 0.0f)
        return;

    Widget->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
    FWidgetTransform Transform;
    Transform.Angle = Node.Rotation;
    Widget->SetRenderTransform(Transform);

    // 仅对固定位置的CanvasPanel子元素调整AABB
    // 使用Anchors拉伸的元素不调整（会破坏锚点语义）
    if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
    {
        FAnchors Anchors = CanvasSlot->GetAnchors();
        bool bIsFixedPosition = (Anchors.Minimum == Anchors.Maximum);
        if (bIsFixedPosition)
        {
            FVector2D Size = CanvasSlot->GetSize();
            FVector2D RotatedAABB = CalculateRotatedAABB(Size, Node.Rotation);
            FVector2D SizeDelta = (RotatedAABB - Size) * 0.5f;
            CanvasSlot->SetSize(RotatedAABB);
            CanvasSlot->SetPosition(CanvasSlot->GetPosition() - SizeDelta);
        }
    }
}

FVector2D CalculateRotatedAABB(FVector2D Size, float AngleDegrees)
{
    float Rad = FMath::DegreesToRadians(AngleDegrees);
    float AbsCos = FMath::Abs(FMath::Cos(Rad));
    float AbsSin = FMath::Abs(FMath::Sin(Rad));
    return FVector2D(Size.X * AbsCos + Size.Y * AbsSin,
                     Size.X * AbsSin + Size.Y * AbsCos);
}
```

**gap 与 Spacer 交互规则**：

当同时使用 `gap` 和 `justifyContent`（需要插入 Spacer）时：

1. **gap 只应用于真实子元素**：Spacer 是为实现主轴对齐而插入的辅助控件，不参与 gap 计算
2. **实现方式**：gap 应用于"有前一个可见元素"的可见子元素
3. **Spacer 不接收 gap**：Spacer 的 Padding 始终为 0
4. **Collapsed 不接收 gap**：Collapsed 节点不占用空间，不参与 gap 计算

```cpp
// gap 应用示例（基于可见子列表）
// @note gap 应用条件：当前元素可见 且 前面存在可见元素
bool bHasPreviousVisible = false;
for (UWidget* Child : AllChildren)  // 包含 Spacer 和 Collapsed
{
    if (Child->IsA<USpacer>())
    {
        Container->AddChild(Child);  // Spacer 不应用 gap，不影响 bHasPreviousVisible
    }
    else
    {
        UPanelSlot* Slot = Container->AddChild(Child);
        bool bIsVisible = Child->GetVisibility() != ESlateVisibility::Collapsed;

        // 仅可见元素参与 gap 计算
        if (bIsVisible)
        {
            if (bHasPreviousVisible)
                ApplyGapPadding(Slot, Gap, Layout);  // 前面有可见元素才应用 gap
            bHasPreviousVisible = true;
        }
        // Collapsed 元素不更新 bHasPreviousVisible
    }
}
```

### 3.3 对齐转换规则

#### justifyContent (主轴方向)

**重要限制**：UMG 的 VerticalBox/HorizontalBox 不支持主轴对齐属性。需要通过 Spacer 控件模拟实现。

| .pen值 | 实现方式 | 说明 |
|--------|---------|------|
| `"start"` | 默认行为 | 子元素从起始位置排列 |
| `"center"` | 首尾插入 Fill Spacer | 子元素居中 |
| `"end"` | 首部插入 Fill Spacer | 子元素靠末端 |
| `"space_between"` | 元素间插入 Fill Spacer | 均匀分布，首尾无间距 |
| `"space_around"` | 首尾+元素间各插入 Fill Spacer | 均匀分布，首尾有半间距 |

#### alignItems (交叉轴方向)

交叉轴对齐可直接映射到 Slot 的 HAlign/VAlign 属性：

| .pen值 | Vertical布局 | Horizontal布局 |
|--------|-------------|----------------|
| `"start"` | HAlign_Left | VAlign_Top |
| `"center"` | HAlign_Center | VAlign_Center |
| `"end"` | HAlign_Right | VAlign_Bottom |

**fill_container 与 alignItems 优先级**：

当子元素同时设置 `width/height: "fill_container"` 和父容器设置 `alignItems` 时，存在冲突：

| 场景 | 优先级 | 原因 |
|------|--------|------|
| 子元素 `width: "fill_container"` + 父 `alignItems` | **fill_container 优先** | HAlign_Fill 覆盖 alignItems |
| 子元素 `height: "fill_container"` + 父 `alignItems` | **fill_container 优先** | VAlign_Fill 覆盖 alignItems |
| 子元素无 fill + 父 `alignItems` | alignItems 生效 | 正常交叉轴对齐 |

**实现规则**：先应用 `alignItems`，再应用子元素的 `fill_container`（后者覆盖前者）。

#### 主轴对齐实现

UMG Box 容器不支持主轴对齐，需要通过 Spacer 控件模拟实现：

```cpp
// 主轴对齐实现：根据 justifyContent 值插入 Spacer
// @note center/end 模式：Collapsed 节点仍添加到容器，但不占用空间
// @note space_between 模式：仅在可见元素之间插入 Spacer
void ApplyJustifyContent(UPanelWidget* Container, const TArray<UWidget*>& Children,
                         const FString& JustifyContent, UWidgetTree* WidgetTree)
{
    if (Children.Num() == 0) return;

    // 辅助函数：添加 Fill Spacer
    // 注: SetSize(ESlateSizeRule) 通过 FSlateChildSize 的隐式构造函数工作
    // FSlateChildSize 定义在 SlateWrapperTypes.h，有 FSlateChildSize(ESlateSizeRule::Type) 构造函数
    auto AddFillSpacer = [Container, WidgetTree]() {
        USpacer* Spacer = WidgetTree->ConstructWidget<USpacer>();
        UPanelSlot* Slot = Container->AddChild(Spacer);
        if (auto* HSlot = Cast<UHorizontalBoxSlot>(Slot))
            HSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        else if (auto* VSlot = Cast<UVerticalBoxSlot>(Slot))
            VSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    };

    // 辅助函数：检查是否为可见子元素
    auto IsVisibleChild = [](UWidget* Child) {
        return Child && Child->GetVisibility() != ESlateVisibility::Collapsed;
    };

    if (JustifyContent == TEXT("start") || JustifyContent.IsEmpty())
    {
        // 默认行为：直接添加子元素（Collapsed 节点不占空间）
        for (UWidget* Child : Children)
            Container->AddChild(Child);
    }
    else if (JustifyContent == TEXT("center"))
    {
        // 居中：首尾各插入一个 Fill Spacer
        // Collapsed 节点不占空间，不影响居中效果
        AddFillSpacer();
        for (UWidget* Child : Children)
            Container->AddChild(Child);
        AddFillSpacer();
    }
    else if (JustifyContent == TEXT("end"))
    {
        // 靠末端：首部插入 Fill Spacer
        // Collapsed 节点不占空间，不影响末端对齐
        AddFillSpacer();
        for (UWidget* Child : Children)
            Container->AddChild(Child);
    }
    else if (JustifyContent == TEXT("space_between"))
    {
        // 均匀分布：仅在可见元素之间插入 Fill Spacer
        int32 VisibleCount = 0;
        for (UWidget* Child : Children)
        {
            if (IsVisibleChild(Child))
                VisibleCount++;
        }

        int32 VisibleIndex = 0;
        for (UWidget* Child : Children)
        {
            Container->AddChild(Child);

            // 仅在可见元素之间插入 Spacer
            if (IsVisibleChild(Child))
            {
                if (VisibleIndex < VisibleCount - 1)
                    AddFillSpacer();
                VisibleIndex++;
            }
        }
    }
}
```

### 3.4 尺寸语义映射

.pen 支持响应式尺寸语义，需要正确映射到 UMG：

| .pen 尺寸值 | UMG 映射 | 说明 |
|------------|----------|------|
| 数值 (如 `120`) | `SizeBox` 包装 | 固定像素尺寸 |
| `"fill_container"` | `SizeRule::Fill` | 填充父容器剩余空间 |
| `"fit_content"` | `SizeRule::Auto` | 根据内容自动调整 |

**CanvasPanel AutoSize 限制**：

UCanvasPanelSlot 的 `AutoSize` 是单一布尔开关，无法独立控制宽高。当宽高语义不同时：

| 宽度语义 | 高度语义 | 处理方式 |
|---------|---------|---------|
| fit_content | fit_content | AutoSize=true |
| fill_container | fill_container | AutoSize=false, 双向 Anchors 拉伸 |
| 固定值 | 固定值 | AutoSize=false, 直接设置 Size |
| fit_content | fill_container | **AutoSize=false**, 高度用 Anchors, 宽度用固定值（取内容宽度）|
| fill_container | fit_content | **AutoSize=false**, 宽度用 Anchors, 高度用固定值（取内容高度）|
| fit_content | 固定值 | AutoSize=false, 宽度取内容宽度, 高度直接设置 |
| 固定值 | fit_content | AutoSize=false, 宽度直接设置, 高度取内容高度 |

**混合语义处理**：当宽高语义不同时，禁用 AutoSize 并手动计算 fit 轴尺寸。

### 3.5 文本属性映射

#### textGrowth → AutoWrapText 映射规则

| textGrowth | 宽度语义 | AutoWrapText | WrapTextAt | 说明 |
|------------|---------|--------------|------------|------|
| `"fixed-width"` | 固定值 | **true** | Node.Width | 固定宽度自动换行 |
| `"fixed-width-height"` | 固定值 | **true** | Node.Width | 固定宽高，超出裁剪（需设置 Clipping） |
| `"auto"` | fit_content | false | 0 | 默认单行文本 |
| `"auto"` + `justify` | 已知宽度 | **true** | **ResolvedWidth** | 运行时必须复用源换行宽度 |
| `"auto"` + `fill_container(N)` | fill_container | **true** | **N** | 已知 fallback 宽度时显式导出 wrap 宽度 |
| 缺省 + fit_content | fit_content | false | 0 | 单行文本 |
| **混合语义** | 宽度已知 + 高度fit | **true** | **KnownWidth** | 必须显式指定换行宽度 |

> **引擎行为**：当 `AutoWrapText=true` 且 `WrapTextAt=0` 时，引擎会在 Paint 阶段使用 `CachedSize.X`（容器宽度）作为换行宽度。
>
> **混合语义限制**：当需要在添加到容器前测量文本高度时（如 CanvasPanel 混合语义），必须使用 `WrapTextAt` 显式指定宽度，因为 SlatePrepass 阶段 `CachedSize.X` 为 0。

```cpp
// 应用文本属性到 TextBlock
// @param Settings 转换设置，包含 FontMapping
void ApplyTextProperties(UTextBlock* TextBlock, const FDesignNode& Node,
                         const FPenConversionSettings& Settings)
{
    TextBlock->SetText(FText::FromString(Node.Content));

    // 字体设置
    FSlateFontInfo FontInfo = TextBlock->GetFont();
    FontInfo.Size = Node.FontSize;

    // fontFamily 映射：通过 FontMapping 查找对应的 UFont 资产
    if (!Node.FontFamily.IsEmpty() && Settings.FontMapping.Contains(Node.FontFamily))
    {
        FontInfo.FontObject = Settings.FontMapping[Node.FontFamily].LoadSynchronous();
    }

    // fontWeight 映射：设置 TypefaceFontName
    if (!Node.FontWeight.IsEmpty())
    {
        FontInfo.TypefaceFontName = MapFontWeight(Node.FontWeight);
    }

    TextBlock->SetFont(FontInfo);

    // letterSpacing 映射
    if (Node.LetterSpacing != 0)
    {
        FSlateFontInfo UpdatedFont = TextBlock->GetFont();
        UpdatedFont.LetterSpacing = Node.LetterSpacing;
        TextBlock->SetFont(UpdatedFont);
    }

    // lineHeight 映射：通过 LineHeightPercentage 设置
    if (Node.LineHeight > 0)
    {
        TextBlock->SetLineHeightPercentage(Node.LineHeight);
    }

    // 颜色
    if (!Node.Fill.SolidColor.IsEmpty())
    {
        TextBlock->SetColorAndOpacity(FPenTypeMapper::ParseColor(Node.Fill.SolidColor));
    }

    const float WrapTextAt = ResolveTextWrapAt(Node, 0.0f);
    const bool bNeedsKnownWidthRuntimeWrap =
        Node.TextGrowth == EDesignTextGrowth::Auto &&
        Node.Width.IsFill() &&
        Node.Width.Value > 0.0f &&
        !Node.Metadata.Contains(TEXT("figma.text.truncate"));

    if (Node.TextAlign == TEXT("justify"))
    {
        TextBlock->SetAutoWrapText(true);
        if (WrapTextAt > 0.0f)
            TextBlock->SetWrapTextAt(WrapTextAt);
    }
    else if (bNeedsKnownWidthRuntimeWrap && WrapTextAt > 0.0f)
    {
        TextBlock->SetAutoWrapText(true);
        TextBlock->SetWrapTextAt(WrapTextAt);
    }

    if (Node.TextGrowth == TEXT("fixed-width") || Node.TextGrowth == TEXT("fixed-width-height"))
    {
        TextBlock->SetAutoWrapText(true);
        if (WrapTextAt > 0.0f)
            TextBlock->SetWrapTextAt(WrapTextAt);
        if (Node.TextGrowth == TEXT("fixed-width-height"))
            TextBlock->SetClipping(EWidgetClipping::ClipToBounds);
    }
}

// fontWeight 映射到 UE5 TypefaceFontName
FName MapFontWeight(const FString& Weight)
{
    if (Weight == TEXT("bold") || Weight == TEXT("700"))
        return TEXT("Bold");
    if (Weight == TEXT("100")) return TEXT("Thin");
    if (Weight == TEXT("200")) return TEXT("ExtraLight");
    if (Weight == TEXT("300")) return TEXT("Light");
    if (Weight == TEXT("500")) return TEXT("Medium");
    if (Weight == TEXT("600")) return TEXT("SemiBold");
    if (Weight == TEXT("800")) return TEXT("ExtraBold");
    if (Weight == TEXT("900")) return TEXT("Black");
    return TEXT("Regular");  // normal, 400, 或其他
}
```

**textAlignVertical 处理**：

UMG 的 TextBlock 不支持垂直对齐。当前实现不会在运行时直接还原，而是把该信息保留为 Widget 属性元数据，供后续验证/扩展使用：

| textAlignVertical | 实现方式 |
|-------------------|---------|
| `top` | 默认行为；不额外写入 |
| `middle` | 写入 `TextAlignVertical=Center` 元数据，当前不改运行时布局 |
| `bottom` | 写入 `TextAlignVertical=Bottom` 元数据，当前不改运行时布局 |

> **当前取舍**：直接把 `textAlignVertical` 映射到父 Slot 会引入按钮、标签页等复合组件整体漂移，因此目前只保留元数据，不做容器级自动包裹。

**textAlign（水平对齐）处理**：

| textAlign | UMG Justification |
|-----------|-------------------|
| `left` | `ETextJustify::Left`（默认） |
| `center` | `ETextJustify::Center` |
| `right` | `ETextJustify::Right` |

```cpp
// 水平对齐映射
// Review 修复: FDesignNode::TextAlign 是 ETextJustify::Type 枚举，直接比较
if (Node.TextAlign == ETextJustify::Center)
    TextBlock->SetJustification(ETextJustify::Center);
else if (Node.TextAlign == ETextJustify::Right)
    TextBlock->SetJustification(ETextJustify::Right);
// left 是默认值，无需设置
```

### 3.6 颜色转换

```cpp
// #RGB、#RRGGBB 或 #RRGGBBAA (sRGB) → FLinearColor
// 3 位格式展开为 6 位（#ABC → #AABBCC）
// 6 位格式默认 alpha=255（不透明）
FLinearColor ParseColor(const FString& HexColor)
{
    // 去掉 # 前缀
    // 使用 TEXT 宏确保 TCHAR 兼容
    FString Hex = HexColor.StartsWith(TEXT("#")) ? HexColor.Mid(1) : HexColor;

    // 3 位简写格式展开：#RGB → #RRGGBB
    if (Hex.Len() == 3)
    {
        Hex = FString::Printf(TEXT("%c%c%c%c%c%c"),
            Hex[0], Hex[0], Hex[1], Hex[1], Hex[2], Hex[2]);
    }

    // 解析 sRGB 值 (0-255)
    uint8 R = FCString::Strtoi(*Hex.Mid(0, 2), nullptr, 16);
    uint8 G = FCString::Strtoi(*Hex.Mid(2, 2), nullptr, 16);
    uint8 B = FCString::Strtoi(*Hex.Mid(4, 2), nullptr, 16);
    uint8 A = Hex.Len() >= 8 ? FCString::Strtoi(*Hex.Mid(6, 2), nullptr, 16) : 255;

    // sRGB → Linear 转换（UE5 FColor 构造函数自动处理）
    return FLinearColor(FColor(R, G, B, A));
}
```

> **颜色空间说明**：.pen 文件使用 sRGB 颜色空间（Web 标准），UE5 的 FLinearColor 是线性空间。
> 通过 `FColor` 构造 `FLinearColor` 会自动进行 sRGB→Linear 转换。

### 3.7 圆角实现 (cornerRadius)

**.pen 中 cornerRadius 格式**：

```json
// 单值格式（四角统一）
"cornerRadius": 8

// 四值数组格式 [左上, 右上, 右下, 左下]
"cornerRadius": [8, 8, 0, 0]

// 变量引用格式
"cornerRadius": "$--radius-xs"
```

#### 3.7.1 方案选择

| 方案 | 适用场景 | 优点 | 缺点 |
|------|---------|------|------|
| `RoundedBox` DrawType | 纯色填充+圆角 | 原生支持，无需纹理 | 不支持图片填充 |
| 九宫格纹理 | 图片填充+圆角 | 支持任意填充 | 需生成纹理 |

**优先使用原生RoundedBox**，仅图片填充时回退到九宫格纹理。

#### 3.7.2 RoundedBox 实现（推荐）

```cpp
void ApplyRoundedCorner(UBorder* Border, const FDesignNode& Node,
                        const FPenVariableResolver& Resolver)
{
    FSlateBrush Brush;
    Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
    Brush.TintColor = FPenTypeMapper::ParseColor(Node.Fill.SolidColor);

    // cornerRadius 支持单值或四值数组 [左上, 右上, 右下, 左下]
    FVector4 Radii = Resolver.ResolveCornerRadius(Node.CornerRadius);
    Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
    Brush.OutlineSettings.CornerRadii = Radii;

    // 描边设置
    if (Node.Stroke.Thickness > 0)
    {
        Brush.OutlineSettings.Width = Node.Stroke.Thickness;
        Brush.OutlineSettings.Color = FPenTypeMapper::ParseColor(Node.Stroke.Fill);
    }

    Border->SetBrush(Brush);
}
```
### 3.8 变量系统 (Variables)

.pen 文件支持变量定义，用于统一管理颜色、数值等设计令牌。

#### 3.8.1 变量格式

```json
{
  "variables": {
    "primary-color": { "type": "color", "value": "#3B82F6" },
    "spacing-md": { "type": "number", "value": 16 },
    "font-body": { "type": "string", "value": "Inter" }
  }
}
```

#### 3.8.2 变量引用语法

节点属性中通过 `$variableName` 引用变量（`$` 前缀，变量名可含 `--`）：

```json
{
  "type": "rectangle",
  "fill": "$--primary-color",
  "stroke": { "fill": "$--border" }
}
```

#### 3.8.3 变量解析器

```cpp
class FPenVariableResolver
{
public:
    FPenVariableResolver(const TMap<FString, FPenVariable>& InVariables)
        : Variables(InVariables) {}

    // 解析字符串值，替换变量引用
    FString ResolveString(const FString& Value) const
    {
        if (!Value.StartsWith(TEXT("$")))
            return Value;

        FString VarName = Value.Mid(1);  // 去掉 $ 前缀，保留完整变量名
        if (const FPenVariable* Var = Variables.Find(VarName))
            return Var->Value;

        UE_LOG(LogDesign2UMG, Warning, TEXT("Variable not found: %s"), *VarName);
        return Value;  // 返回原值
    }

    // 解析数值，支持变量引用
    float ResolveNumber(const TSharedPtr<FJsonValue>& Value) const
    {
        if (Value->Type == EJson::Number)
            return Value->AsNumber();

        if (Value->Type == EJson::String)
        {
            FString Str = Value->AsString();
            if (Str.StartsWith(TEXT("$")))
            {
                FString Resolved = ResolveString(Str);
                return FCString::Atof(*Resolved);
            }
            return FCString::Atof(*Str);
        }
        return 0.0f;
    }

private:
    const TMap<FString, FPenVariable>& Variables;
};
```

#### 3.8.4 集成到解析流程

```cpp
FDesignNode FPenParser::ParseNode(const TSharedPtr<FJsonObject>& JsonObj,
                                const FPenVariableResolver& Resolver)
{
    FDesignNode Node;

    // 解析填充色（支持变量和对象格式）
    if (JsonObj->HasField(TEXT("fill")))
    {
        const TSharedPtr<FJsonValue>& FillValue = JsonObj->TryGetField(TEXT("fill"));
        if (FillValue->Type == EJson::String)
        {
            Node.Fill.SolidColor = Resolver.ResolveString(FillValue->AsString());
        }
        else if (FillValue->Type == EJson::Object)
        {
            const TSharedPtr<FJsonObject>& FillObj = FillValue->AsObject();
            FString FillType = FillObj->GetStringField(TEXT("type"));

            if (FillType == TEXT("color"))
            {
                // 对象式纯色填充：{"type": "color", "color": "$--xxx", "enabled": true}
                bool bEnabled = FillObj->GetBoolField(TEXT("enabled"));
                if (bEnabled)
                {
                    Node.Fill.SolidColor = Resolver.ResolveString(
                        FillObj->GetStringField(TEXT("color")));
                }
                // enabled=false 时不设置填充色
            }
            else if (FillType == TEXT("gradient"))
            {
                // 渐变填充处理...
            }
        }
    }

    // 解析尺寸（支持变量）
    if (JsonObj->HasField(TEXT("width")))
        Node.Width = FPenSize::Fixed(Resolver.ResolveNumber(JsonObj->TryGetField(TEXT("width"))));

    // ... 其他属性
    return Node;
}
```

> **变量解析时机**：变量在 JSON 解析阶段统一替换，FDesignNode 存储已解析的实际值。
> **设计决策**：Widget Blueprint 是静态资产，运行时主题切换需要 SlateStyle 系统，超出本模块范围。

### 3.9 图标字体 (icon_font)

.pen 的 `icon_font` 类型表示图标字体字符，通过 TextBlock 渲染单个 Unicode 字符实现。

#### 3.9.1 icon_font 节点格式

```json
{
  "type": "icon_font",
  "iconFontName": "arrow-right",
  "iconFontFamily": "Material Symbols Outlined",
  "weight": 400,            // 图标粗细（Material Symbols 专用）
  "width": 24,
  "height": 24,
  "fill": "#000000ff"
}
```

**iconFontFamily 可选值**：`lucide` | `feather` | `Material Symbols Outlined` | `Material Symbols Rounded` | `Material Symbols Sharp`

> **注意**：.pen 文件通过 `iconFontName` 标识图标，不提供 Unicode 码点。需要在 UE5 侧维护图标名称到码点的映射表。

#### 3.9.2 实现方案

```cpp
UWidget* CreateIconFontWidget(const FDesignNode& Node, const FPenConversionSettings& Settings)
{
    UTextBlock* TextBlock = WidgetTree->ConstructWidget<UTextBlock>();

    // 通过 iconFontName 查找 Unicode 码点
    FString IconChar;
    if (Settings.IconNameToCodepoint.Contains(Node.IconFontName))
    {
        int32 Codepoint = Settings.IconNameToCodepoint[Node.IconFontName];
        IconChar = FString::Chr(static_cast<TCHAR>(Codepoint));
    }
    TextBlock->SetText(FText::FromString(IconChar));

    // 设置图标字体
    FSlateFontInfo FontInfo = TextBlock->GetFont();
    FontInfo.Size = FMath::Min(Node.Width.Value, Node.Height.Value);

    // 查找图标字体资产
    if (Settings.IconFontMapping.Contains(Node.IconFontFamily))
    {
        FontInfo.FontObject = Settings.IconFontMapping[Node.IconFontFamily].LoadSynchronous();
    }
    TextBlock->SetFont(FontInfo);

    // 设置颜色
    if (!Node.Fill.SolidColor.IsEmpty())
    {
        TextBlock->SetColorAndOpacity(FPenTypeMapper::ParseColor(Node.Fill.SolidColor));
    }

    return TextBlock;
}
```

#### 3.9.3 配置扩展

```cpp
USTRUCT(BlueprintType)
struct FPenConversionSettings
{
    // ... 现有字段

    // 图标字体映射表
    UPROPERTY(EditAnywhere)
    TMap<FString, TSoftObjectPtr<UFont>> IconFontMapping;

    // 图标名称到 Unicode 码点映射（需预先配置）
    UPROPERTY(EditAnywhere)
    TMap<FString, int32> IconNameToCodepoint;
};
```

> **字体资产要求**：需要预先将图标字体（如 Material Icons、Font Awesome）导入为 UE5 Font 资产，并配置 IconNameToCodepoint 映射表。

### 3.10 图片填充 (Image Fill)

#### 3.10.1 .pen 文件中的图片处理方式

**.pen 文件没有独立的 `image` 节点类型**。图片通过 `fill` 属性实现，作为 `frame`/`rectangle` 等容器的填充。

**Image Fill Schema**：

```json
{
  "type": "image",
  "url": "./relative/path.png",
  "mode": "stretch",
  "opacity": 1.0,
  "enabled": true,
  "blendMode": "normal"
}
```

| 属性 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `type` | `"image"` | 是 | 固定值 |
| `url` | `string` | 是 | 相对于 .pen 文件的路径 |
| `mode` | `string` | 否 | `stretch`(拉伸) / `fill`(裁剪填充) / `fit`(适应留白) |
| `opacity` | `number` | 否 | 透明度 0-1 |
| `enabled` | `BooleanOrVariable` | 否 | 是否启用（转换器仅支持布尔值） |
| `blendMode` | `string` | 否 | 混合模式（UMG 不支持） |

> **blendMode 枚举值**：`normal`/`darken`/`multiply`/`linearBurn`/`colorBurn`/`light`/`screen`/`linearDodge`/`colorDodge`/`overlay`/`softLight`/`hardLight`/`difference`/`exclusion`/`hue`/`saturation`/`color`/`luminosity`（UMG 仅支持 Opaque/Masked/Transparent）

> **图片填充实现**：通过 `UImage` + `FSlateBrush` 实现，支持 stretch/fill/fit 模式。

### 3.11 矢量路径 (path)

path 节点通过 **导入时 SVG 烘焙为纹理** 实现，使用 NanoSVG 库解析和光栅化。

**path 节点属性**：
| 属性 | 类型 | 说明 |
|------|------|------|
| `geometry` | `string` | SVG path 数据 |
| `fillRule` | `string` | 填充规则：`"nonzero"`（默认）或 `"evenodd"` |

> **设计决策**：UE5 的 USlateVectorArtData 主要用于引擎内置图标，不适合任意 SVG path。烘焙为纹理是更可靠的方案。

### 3.12 几何图形 (ellipse/line/polygon)

UMG 无原生椭圆、线条、多边形控件，通过运行时生成纹理实现。

#### 3.12.1 ellipse 节点

```json
{
  "type": "ellipse",
  "width": 100,
  "height": 80,
  "fill": "#3B82F6",
  "innerRadius": 0,      // 0=实心, 0.5=环形
  "startAngle": 0,       // 起始角度（度）
  "sweepAngle": 360      // 扫过角度（度）
}
```

**UMG 映射**：`UImage` + 运行时生成椭圆纹理

```cpp
UTexture2D* GenerateEllipseTexture(const FDesignNode& Node)
{
    int32 Width = FMath::Max(1, FMath::RoundToInt(Node.Width.Value));
    int32 Height = FMath::Max(1, FMath::RoundToInt(Node.Height.Value));

    UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
    FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
    void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);

    FColor FillColor = FPenTypeMapper::ParseColor(Node.Fill.SolidColor).ToFColor(true);
    FColor* Pixels = static_cast<FColor*>(Data);

    float CenterX = Width * 0.5f;
    float CenterY = Height * 0.5f;
    float RadiusX = CenterX;
    float RadiusY = CenterY;

    for (int32 Y = 0; Y < Height; ++Y)
    {
        for (int32 X = 0; X < Width; ++X)
        {
            float NormX = (X - CenterX) / RadiusX;
            float NormY = (Y - CenterY) / RadiusY;
            float Dist = NormX * NormX + NormY * NormY;

            bool bInside = Dist <= 1.0f;
            if (Node.InnerRadius > 0)
                bInside = bInside && Dist >= Node.InnerRadius * Node.InnerRadius;

            Pixels[Y * Width + X] = bInside ? FillColor : FColor::Transparent;
        }
    }

    Mip.BulkData.Unlock();
    Texture->UpdateResource();
    return Texture;
}
```

#### 3.12.2 line 节点

```json
{
  "type": "line",
  "width": 200,
  "height": 2,
  "stroke": { "thickness": 2, "fill": "#000000" }
}
```

**UMG 映射**：`UImage` + 纯色纹理，通过 width/height 控制线条方向和粗细。

#### 3.12.3 polygon 节点

```json
{
  "type": "polygon",
  "width": 100,
  "height": 100,
  "polygonCount": 6,     // 边数
  "cornerRadius": 0,
  "fill": "#10B981"
}
```

**UMG 映射**：`UImage` + 运行时生成正多边形纹理

### 3.13 特效 (effect)

**.pen effect 实际格式**：可以是单个对象、数组或空数组。

```json
// 单个效果对象
"effect": {
  "type": "shadow",
  "shadowType": "outer",
  "offset": {"x": 0, "y": 2},
  "blur": 3.5,
  "spread": -1,
  "color": "#0000000f"
}

// 空数组（清除效果）
"effect": []
```

**shadow 字段说明**：
| 字段 | 类型 | 说明 |
|------|------|------|
| `type` | `"shadow"` | 固定值 |
| `shadowType` | `"outer"` / `"inner"` | 阴影类型 |
| `offset` | `{x, y}` | 偏移量 |
| `blur` | `number` | 模糊半径 |
| `spread` | `number` | 扩展半径（可为负值） |
| `color` | `string` | 阴影颜色 |

**UMG 映射限制**：

| effect 类型 | UMG 支持 | 说明 |
|-------------|----------|------|
| `blur` | `RetainerBox + EffectMaterial` | 前景 blur 走 typed subtree effect |
| `background_blur` | `UBackgroundBlur` | 原生支持 |
| `shadow` | **部分支持** | 文本走 `TextBlock` 原生阴影；其他节点走 `Overlay + Image` 近似 outer shadow；`inner shadow` 走 typed `RetainerBox + EffectMaterial` |

> **当前取舍**：outer shadow、foreground blur、inner shadow 都已进入运行时主链，但 blur / inset 类效果仍属于可维护近似，不等于完整设计器语义复刻。

> **Schema 额外属性**：所有 effect 类型支持 `enabled`；`blur`/`background_blur` 有 `radius`；`shadow` 支持 `blendMode`

### 3.14 渐变填充 (gradient)

```json
"fill": {
  "type": "gradient",
  "gradientType": "linear",
  "rotation": 180,
  "colors": [
    { "color": "#3B82F6", "position": 0 },
    { "color": "#8B5CF6", "position": 1 }
  ]
}
```

**UMG 映射**：运行时生成程序纹理

```cpp
UTexture2D* GenerateGradientTexture(const FPenGradient& Gradient, int32 Width, int32 Height)
{
    UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
    // ... 根据 gradientType 和 rotation 生成渐变像素
    return Texture;
}
```

当前实现说明：

- Linear 使用 `gradientlinear`，支持 Rotation
- Angular 使用独立 `angulargradient` 分支，不再降级为 Radial
- Linear / Radial / Angular 已支持多 stop `Stops`
- Radial 已支持 `center` / `size` / `rotation`
- `mesh_gradient` 仍不支持

### 3.15 Prototype Navigation And Transition

当前 Figma prototype 主链已经覆盖最小可交付闭环：

- 顶层多页面 Frame 会收敛为 `WidgetSwitcher`
- `ON_CLICK + NAVIGATE/SWAP` 会把触发节点包装为透明 `Button`
- 运行时通过 Blueprint extension 绑定点击事件，而不是把平台逻辑塞回共享转换主流程

基础过渡当前采用“可维护近似”策略：

- `DISSOLVE` / `SMART_ANIMATE` / `SCROLL_ANIMATE`：映射为 switcher 淡出切页再淡入
- `MOVE_IN` / `MOVE_OUT` / `SLIDE_IN` / `SLIDE_OUT` / `PUSH`：映射为 switcher 基础位移过渡
- `duration` 与 `easing` 会进入 typed navigation binding，并驱动运行时过渡节奏

当前边界：

- 不生成完整 `UWidgetAnimation` 资产
- 不做 overlay、back stack、hover/drag 手势或条件跳转
- `SMART_ANIMATE` 仅落到基础 fade 近似，不做图层级 match/layer morph
