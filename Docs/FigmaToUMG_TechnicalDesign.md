# FigmaToUMG 技术设计文档 v1.0

> 通过 Figma REST API 将 Figma 设计文件转换为 UE5 Widget Blueprint

---

## 1. 项目概述

### 1.1 背景

ImageToUMG 插件已支持两种输入源：
1. **AI 识别截图** - 通过多模态 LLM 分析 UI 截图
2. **PenToUMG** - 解析 Pencil.dev 的 .pen 文件

FigmaToUMG 模块新增第三种输入源，直接对接 Figma REST API，让用户无需中间工具即可将 Figma 设计转换为 UMG。

### 1.2 核心理念

```
用户提供 Figma API Token + 文件链接
           ↓
    Figma REST API 获取 JSON
           ↓
    FigmaParser 转换为 FPenDocument
           ↓
    复用 FPenToUMGConverter
           ↓
    生成 Widget Blueprint
```

> **FPenDocument 构造**：FigmaParser 将 Figma 节点树转换为 `TArray<FPenNode>`，包装为 `FPenDocument` 后传入 `FPenToUMGConverter::Convert()`。Figma 的 `styles` 字段（颜色/文本/效果样式）可选映射为 `FPenDocument.Variables`。注意：Figma Variables（设计变量）是独立的 API，通过节点的 `boundVariables` 字段关联。

### 1.3 收益对比

| 指标 | AI 识别截图 | PenToUMG | FigmaToUMG |
|------|------------|----------|------------|
| 位置精度 | MAE≤4px | 结构精确 | **结构精确** |
| 元素识别率 | 90% | 100% | **100%** |
| 外部依赖 | AI API | Pencil.dev | **仅 Figma Token** |
| 设计师友好度 | 低 | 中 | **高（主流工具）** |

> **精度说明**：Figma 提供精确的结构数据，但最终渲染结果受以下因素影响：
> - 字体差异：Figma 使用 Web 字体，UE5 使用引擎内置或自定义字体
> - 行高计算：Figma 的 lineHeightPx 与 UE5 的 LineHeightPercentage 存在微小差异
> - 渲染精度：UE5 Slate 的亚像素渲染与 Figma 可能不同
>
> 预期偏差：文本元素 ±2-4px，其他元素 ±1px

---

## 2. Figma REST API 规范

### 2.1 认证方式

```
Header: X-Figma-Token: {personal_access_token}
```

用户在 Figma 账户设置中生成 Personal Access Token。

> **注意**: Figma API 仅支持 HTTPS 连接。

### 2.2 核心 API 端点

| 端点 | 方法 | 用途 |
|------|------|------|
| `/v1/files/{file_key}` | GET | 获取完整文件结构 |
| `/v1/files/{file_key}/nodes?ids={node_ids}` | GET | 获取指定节点 |
| `/v1/images/{file_key}` | GET | 导出图片资源 |

### 2.3 Rate Limit 说明

Figma API 使用 leaky bucket 算法限流，具体限制因账户类型而异（详见官方文档）。

超限返回 `429 Too Many Requests`。建议：
- 缓存 API 响应避免重复请求
- 实现指数退避重试
- 超限时检查响应头 `Retry-After` 获取重试等待时间

### 2.4 文件链接解析

```
Figma 链接格式：
https://www.figma.com/{file_type}/{file_key}/{file_name}?node-id={node_id}

file_type: design | file | proto（不支持 board/FigJam）
node_id: URL 中使用 - 分隔（如 1-2），API 中使用 : 分隔（如 1:2）

示例：
https://www.figma.com/design/ABC123/MyDesign?node-id=1-2

解析结果：
- file_key: ABC123
- node_id: 1-2 → 转换为 1:2 用于 API 调用
```

---

## 3. Figma JSON 结构

### 3.1 文档根结构

```json
{
  "document": {
    "id": "0:0",
    "name": "Document",
    "type": "DOCUMENT",
    "children": [
      {
        "id": "0:1",
        "name": "Page 1",
        "type": "CANVAS",
        "children": [...]
      }
    ]
  },
  "components": {...},
  "componentSets": {...},
  "styles": {...}
}
```

### 3.2 节点类型 (Node Types)

| Figma Type | 说明 | 对应 .pen Type |
|------------|------|----------------|
| `DOCUMENT` | 文档根节点 | - |
| `CANVAS` | 页面 | - |
| `FRAME` | 画框/容器 | `frame` |
| `GROUP` | 分组 | `group` |
| `RECTANGLE` | 矩形 | `rectangle` |
| `ELLIPSE` | 椭圆 | `rectangle`（Phase 1 降级） |
| `LINE` | 线条 | `rectangle`（Phase 1 降级） |
| `TEXT` | 文本 | `text` |
| `VECTOR` | 矢量路径 | `frame`（Phase 1 降级为边界框） |
| `COMPONENT` | 组件定义 | `frame`（Phase 1 展开，见 4.7） |
| `COMPONENT_SET` | 变体集合 | `frame`（Phase 1 展开，见 4.7） |
| `INSTANCE` | 组件实例 | `frame`（Phase 1 展开，见 4.7） |

### 3.3 通用节点属性

```json
{
  "id": "1:2",
  "name": "Button",
  "type": "FRAME",
  "visible": true,
  "locked": false,
  "rotation": 0,
  "absoluteBoundingBox": {
    "x": 100,
    "y": 200,
    "width": 150,
    "height": 50
  },
  "relativeTransform": [[1, 0, 100], [0, 1, 200]],  // 仅 geometry=paths 时返回
  "constraints": {
    "vertical": "TOP",
    "horizontal": "LEFT"
  }
}
```

### 3.4 布局属性 (Auto Layout)

```json
{
  "layoutMode": "HORIZONTAL",
  "primaryAxisSizingMode": "AUTO",
  "counterAxisSizingMode": "FIXED",
  "primaryAxisAlignItems": "CENTER",
  "counterAxisAlignItems": "CENTER",
  "paddingLeft": 16,
  "paddingRight": 16,
  "paddingTop": 8,
  "paddingBottom": 8,
  "itemSpacing": 12,
  "clipsContent": true
}
```

### 3.5 填充属性 (Fills)

```json
{
  "fills": [
    {
      "type": "SOLID",
      "visible": true,
      "opacity": 1,
      "color": { "r": 0.2, "g": 0.4, "b": 0.8, "a": 1 }
    }
  ]
}
```

**渐变填充：**
```json
{
  "fills": [
    {
      "type": "GRADIENT_LINEAR",
      "gradientHandlePositions": [
        { "x": 0, "y": 0.5 },
        { "x": 1, "y": 0.5 }
      ],
      "gradientStops": [
        { "color": { "r": 1, "g": 0, "b": 0, "a": 1 }, "position": 0 },
        { "color": { "r": 0, "g": 0, "b": 1, "a": 1 }, "position": 1 }
      ]
    }
  ]
}
```

### 3.6 描边属性 (Strokes)

```json
{
  "strokes": [
    {
      "type": "SOLID",
      "color": { "r": 0, "g": 0, "b": 0, "a": 1 }
    }
  ],
  "strokeWeight": 2,
  "strokeAlign": "INSIDE"
}
```

### 3.7 文本属性

```json
{
  "type": "TEXT",
  "characters": "Hello World",
  "style": {
    "fontFamily": "Inter",
    "fontPostScriptName": "Inter-Regular",
    "fontWeight": 400,
    "fontSize": 16,
    "textAlignHorizontal": "LEFT",
    "textAlignVertical": "TOP",
    "lineHeightPx": 24,
    "lineHeightPercent": 150
  }
}
```

---

## 4. 类型映射规则

### 4.1 字段映射表

| Figma 字段 | FPenNode 字段 | 转换规则 |
|-----------|--------------|---------|
| `id` | `Id` | 直接映射 |
| `name` | `Name` | 直接映射 |
| `type` | `Type` | 见 4.2 |
| `visible` | `bVisible` | 直接映射 |
| `absoluteBoundingBox.*` | `X/Y/Width/Height` | **绝对→相对坐标转换**（见 4.5） |
| `layoutMode` | `Layout` | 见 4.3 |
| `itemSpacing` | `Gap` | 直接映射 |
| `padding*` | `Padding` | 组合为数组 |
| `fills[0].color` | `Fill.SolidColor` | RGBA→Hex（需乘 `fills[0].opacity`，见 4.9） |
| `strokes[0].color` | `Stroke.Fill` | RGBA→Hex（需乘 `strokes[0].opacity`） |
| `strokeWeight` | `Stroke.Thickness` | 直接映射 |
| `cornerRadius` | `CornerRadius` | 直接映射 |
| `characters` | `Content` | 直接映射 |
| `style.fontName.family` | `FontFamily` | 直接映射（注意：不是 `style.fontFamily`） |
| `style.fontSize` | `FontSize` | 直接映射 |
| `style.fontName.style` | `FontWeight` | 从字符串解析（"Bold"→"bold", "Light"→"300", 其他→"normal"） |
| `style.lineHeightPx` | `LineHeight` | 转换为比例值（除以 fontSize） |
| `style.textAlignHorizontal` | `TextAlign` | LEFT→"left", CENTER→"center", RIGHT→"right", JUSTIFIED→"justify" |
| `style.textAlignVertical` | `TextAlignVertical` | TOP→"top", CENTER→"middle", BOTTOM→"bottom" |
| `rotation` | `Rotation` | 直接映射（度） |
| `primaryAxisAlignItems` | `JustifyContent` | MIN→"start", CENTER→"center", MAX→"end", SPACE_BETWEEN→"space-between" |
| `counterAxisAlignItems` | `AlignItems` | MIN→"start", CENTER→"center", MAX→"end", BASELINE→"baseline" |
| `textAutoResize` | `TextGrowth` | WIDTH_AND_HEIGHT→"auto-width-height"（已废弃但需支持）, HEIGHT→"fixed-width", NONE→"fixed-width-height" |
| `layoutSizingHorizontal` | `WidthStr` | HUG→"fit_content", FILL→"fill_container", FIXED→数值 |
| `layoutSizingVertical` | `HeightStr` | HUG→"fit_content", FILL→"fill_container", FIXED→数值 |
| `clipsContent` | `Clip` | 直接映射 |
| `constraints.horizontal` | `HConstraint` | LEFT/RIGHT/CENTER/LEFT_RIGHT/SCALE（响应式布局核心） |
| `constraints.vertical` | `VConstraint` | TOP/BOTTOM/CENTER/TOP_BOTTOM/SCALE（响应式布局核心） |
| `layoutPositioning` | `LayoutPositioning` | AUTO→"auto", ABSOLUTE→"absolute"（Auto Layout中的绝对定位） |
| `layoutWrap` | `LayoutWrap` | NO_WRAP→false, WRAP→true（换行Auto Layout） |
| `counterAxisSpacing` | `RowGap` | 换行时的行间距 |
| `rectangleCornerRadii` | `CornerRadii` | 四角独立圆角 [topLeft, topRight, bottomRight, bottomLeft] |
| `effects` | `Effects` | 阴影/模糊效果数组（见9.3节） |

> **重要**: `constraints` 属性对响应式布局至关重要，详见6.4节的锚点映射实现。

### 4.2 节点类型映射

```cpp
FString MapFigmaType(const FString& FigmaType)
{
    static TMap<FString, FString> TypeMap = {
        {TEXT("FRAME"), TEXT("frame")},
        {TEXT("GROUP"), TEXT("group")},
        {TEXT("RECTANGLE"), TEXT("rectangle")},
        {TEXT("ELLIPSE"), TEXT("rectangle")},   // Phase 1: 降级为矩形
        {TEXT("LINE"), TEXT("rectangle")},      // Phase 1: 降级为矩形
        {TEXT("TEXT"), TEXT("text")},
        {TEXT("VECTOR"), TEXT("frame")},        // Phase 1: 降级为边界框
        {TEXT("COMPONENT"), TEXT("frame")},     // Phase 1: 展开为普通 frame
        {TEXT("COMPONENT_SET"), TEXT("frame")}, // Phase 1: 变体集合展开为普通 frame
        {TEXT("INSTANCE"), TEXT("frame")}       // Phase 1: 展开为普通 frame
    };
    return TypeMap.Contains(FigmaType) ? TypeMap[FigmaType] : TEXT("frame");
}
```

### 4.3 布局模式映射

| Figma layoutMode | FPenNode Layout |
|------------------|-----------------|
| `HORIZONTAL` | `"horizontal"` |
| `VERTICAL` | `"vertical"` |
| `GRID` | `"none"`（Phase 1 降级，Grid 布局暂不支持） |
| 无/`NONE` | `"none"` |

### 4.4 Auto Layout 完整转换

```cpp
struct FFigmaAutoLayout
{
    FString LayoutMode;        // HORIZONTAL | VERTICAL | NONE
    float ItemSpacing;         // 子元素间距
    float PaddingLeft, PaddingRight, PaddingTop, PaddingBottom;
    FString PrimaryAxisAlign;  // MIN | CENTER | MAX | SPACE_BETWEEN
    FString CounterAxisAlign;  // MIN | CENTER | MAX
};

void ApplyAutoLayout(FPenNode& Node, const FFigmaAutoLayout& AL)
{
    // 布局方向
    if (AL.LayoutMode == "HORIZONTAL")
        Node.Layout = "horizontal";
    else if (AL.LayoutMode == "VERTICAL")
        Node.Layout = "vertical";
    else
        Node.Layout = "none";

    Node.Gap = AL.ItemSpacing;
    Node.Padding = {AL.PaddingTop, AL.PaddingRight,
                    AL.PaddingBottom, AL.PaddingLeft};

    // 主轴对齐
    if (AL.PrimaryAxisAlign == "CENTER")
        Node.JustifyContent = "center";
    else if (AL.PrimaryAxisAlign == "MAX")
        Node.JustifyContent = "end";
    else if (AL.PrimaryAxisAlign == "SPACE_BETWEEN")
        Node.JustifyContent = "space-between";

    // 交叉轴对齐
    if (AL.CounterAxisAlign == "CENTER")
        Node.AlignItems = "center";
    else if (AL.CounterAxisAlign == "MAX")
        Node.AlignItems = "end";
    // MIN 或默认为 "start"
}
```

### 4.5 坐标转换（绝对→相对）

Figma 的 `absoluteBoundingBox` 是全局坐标，而 FPenNode 需要相对于父节点的局部坐标：

```cpp
void FFigmaParser::ConvertToRelativeCoords(
    FPenNode& Child, const FPenNode& Parent)
{
    Child.X = Child.X - Parent.X;
    Child.Y = Child.Y - Parent.Y;
}
```

### 4.6 不支持类型的降级策略

| Figma Type | 处理方式 |
|------------|----------|
| `BOOLEAN_OPERATION` | 降级为 `frame`，保留子节点 |
| `STAR` / `POLYGON` / `REGULAR_POLYGON` | 降级为 `frame`（边界框） |
| `SECTION` | 降级为 `frame` |
| `SLICE` | 跳过（导出辅助节点） |
| `STICKY` / `STAMP` | 跳过（协作标注） |

### 4.7 COMPONENT / INSTANCE 处理

**Phase 1 策略**：直接展开，不保留组件引用关系

- `COMPONENT` → 转换为普通 `frame`（忽略 `reusable`）
- `INSTANCE` → 展开为完整节点树（忽略 `overrides`）

> 后续版本可支持 overrides 和组件复用

### 4.8 渐变填充降级

Phase 1 仅支持纯色填充，渐变降级为首个色标颜色：

```cpp
if (Fill->GetStringField("type").StartsWith("GRADIENT"))
{
    auto Stops = Fill->GetArrayField("gradientStops");
    if (Stops.Num() > 0)
        Result.Fill.SolidColor = ExtractColor(Stops[0]);
}
```

### 4.9 透明度计算

Figma 的透明度有三层：节点级 `opacity`、paint 级 `opacity`、颜色 `color.a`。有效透明度 = `node.opacity * paint.opacity * color.a`：

```cpp
float NodeOpacity = Node->HasField("opacity") ? Node->GetNumberField("opacity") : 1.0f;
float PaintOpacity = Fill->HasField("opacity") ? Fill->GetNumberField("opacity") : 1.0f;
float EffectiveAlpha = Color->GetNumberField("a") * PaintOpacity * NodeOpacity;
Result.Fill.SolidColor = RGBAToHex(R, G, B, EffectiveAlpha);
```

---

## 5. 模块架构

### 5.1 新增文件

```
Source/ImageToUMGEditor/
├── Public/
│   ├── FigmaClient.h        // HTTP 客户端
│   ├── FigmaParser.h        // JSON 解析器
│   └── FigmaSettings.h      // 用户设置
└── Private/
    ├── FigmaClient.cpp
    ├── FigmaParser.cpp
    └── FigmaSettings.cpp
```

### 5.2 类图

```
┌─────────────────┐
│  UFigmaSettings │  ← 存储 API Token
└────────┬────────┘
         │
         ▼
┌─────────────────┐     ┌─────────────────┐
│  FFigmaClient   │────▶│ Figma REST API  │
└────────┬────────┘     └─────────────────┘
         │
         ▼
┌─────────────────┐
│  FFigmaParser   │  ← Figma JSON → FPenDocument
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  FPenDocument   │  ← 统一中间格式（含 TArray<FPenNode>）
└────────┬────────┘
         │
         ▼
┌─────────────────────────┐
│  FPenToUMGConverter     │  ← 复用现有代码
└─────────────────────────┘
```

---

## 6. 核心代码实现

### 6.1 FFigmaClient

```cpp
// FigmaClient.h
class FFigmaClient
{
public:
    void SetToken(const FString& Token);
    void GetFile(const FString& FileKey,
        TFunction<void(TSharedPtr<FJsonObject>)> OnSuccess,
        TFunction<void(const FString&)> OnError);

private:
    FString ApiToken;
    const FString BaseUrl = TEXT("https://api.figma.com/v1");
};
```

### 6.2 FFigmaParser

```cpp
// FigmaParser.cpp - 核心转换函数
// 添加递归深度限制，防止深层嵌套导致栈溢出
static constexpr int32 MaxRecursionDepth = 50;

FPenNode FFigmaParser::ConvertNode(const TSharedPtr<FJsonObject>& Node, int32 Depth /*= 0*/)
{
    FPenNode Result;

    // 递归深度检查
    if (Depth > MaxRecursionDepth)
    {
        UE_LOG(LogFigmaParser, Warning,
            TEXT("Max recursion depth (%d) reached, skipping children"), MaxRecursionDepth);
        Result.Id = Node->HasField("id") ? Node->GetStringField("id") : TEXT("");
        Result.Name = TEXT("[DEPTH_LIMIT]");
        return Result;
    }

    // 基础属性（添加字段存在性检查）
    Result.Id = Node->HasField("id") ? Node->GetStringField("id") : TEXT("");
    Result.Name = Node->HasField("name") ? Node->GetStringField("name") : TEXT("");
    Result.Type = Node->HasField("type") ? MapNodeType(Node->GetStringField("type")) : TEXT("frame");

    // 尺寸位置（添加字段存在性检查）
    if (Node->HasField("absoluteBoundingBox"))
    {
        auto BBox = Node->GetObjectField("absoluteBoundingBox");
        if (BBox.IsValid())
        {
            Result.X = BBox->HasField("x") ? BBox->GetNumberField("x") : 0.0f;
            Result.Y = BBox->HasField("y") ? BBox->GetNumberField("y") : 0.0f;
            Result.Width = BBox->HasField("width") ? BBox->GetNumberField("width") : 0.0f;
            Result.Height = BBox->HasField("height") ? BBox->GetNumberField("height") : 0.0f;
        }
    }

    // 节点级透明度（用于叠乘到 paint 透明度）
    float NodeOpacity = Node->HasField("opacity") ? Node->GetNumberField("opacity") : 1.0f;

    // 填充（仅处理 SOLID，IMAGE/GRADIENT 见 4.8、6.5）
    if (Node->HasField("fills"))
    {
        auto Fills = Node->GetArrayField("fills");
        // 遍历找到第一个可见的 SOLID fill
        for (auto& FillValue : Fills)
        {
            auto Fill = FillValue->AsObject();
            if (!Fill.IsValid()) continue;

            bool bVisible = !Fill->HasField("visible") || Fill->GetBoolField("visible");
            if (!bVisible) continue;

            // 添加字段存在性检查
            FString FillType = Fill->HasField("type") ? Fill->GetStringField("type") : TEXT("");
            if (FillType == "SOLID" && Fill->HasField("color"))
            {
                auto Color = Fill->GetObjectField("color");
                if (!Color.IsValid()) continue;

                float PaintOpacity = Fill->HasField("opacity") ? Fill->GetNumberField("opacity") : 1.0f;
                // 有效透明度 = node.opacity * paint.opacity * color.a
                // 添加RGBA字段存在性检查，默认值为0（r/g/b）和1（a）
                float R = Color->HasField("r") ? Color->GetNumberField("r") : 0.0f;
                float G = Color->HasField("g") ? Color->GetNumberField("g") : 0.0f;
                float B = Color->HasField("b") ? Color->GetNumberField("b") : 0.0f;
                float A = Color->HasField("a") ? Color->GetNumberField("a") : 1.0f;
                Result.Fill.SolidColor = RGBAToHex(R, G, B, A * PaintOpacity * NodeOpacity);
                break;  // 找到第一个可见 SOLID fill 后退出
            }
        }
    }

    // 递归子节点（含坐标转换，传递深度参数）
    if (Node->HasField("children"))
    {
        for (auto& Child : Node->GetArrayField("children"))
        {
            FPenNode ChildNode = ConvertNode(Child->AsObject(), Depth + 1);
            // 绝对坐标 → 相对坐标
            ChildNode.X -= Result.X;
            ChildNode.Y -= Result.Y;
            Result.Children.Add(ChildNode);
        }
    }

    return Result;
}
```

### 6.3 辅助函数

```cpp
// RGBA (0-1) 转 Hex 字符串
FString FFigmaParser::RGBAToHex(float R, float G, float B, float A)
{
    uint8 r = FMath::Clamp(FMath::RoundToInt(R * 255), 0, 255);
    uint8 g = FMath::Clamp(FMath::RoundToInt(G * 255), 0, 255);
    uint8 b = FMath::Clamp(FMath::RoundToInt(B * 255), 0, 255);
    uint8 a = FMath::Clamp(FMath::RoundToInt(A * 255), 0, 255);

    return FString::Printf(TEXT("#%02X%02X%02X%02X"), r, g, b, a);
}

// 解析 Figma URL
bool FFigmaParser::ParseFigmaUrl(const FString& Url,
    FString& OutFileKey, FString& OutNodeId)
{
    // 格式: https://www.figma.com/design/{file_key}/{name}?node-id={node_id}
    FRegexPattern Pattern(TEXT(R"(figma\.com/(?:design|file|proto)/([^/]+)/[^?]*(?:\?.*node-id=([^&]+))?)"));
    FRegexMatcher Matcher(Pattern, Url);

    if (Matcher.FindNext())
    {
        OutFileKey = Matcher.GetCaptureGroup(1);
        OutNodeId = Matcher.GetCaptureGroup(2);
        // URL 解码（%3A → :）+ 格式转换（- → :）
        OutNodeId = FGenericPlatformHttp::UrlDecode(OutNodeId);
        OutNodeId.ReplaceInline(TEXT("-"), TEXT(":"));
        return true;
    }
    return false;
}
```

### 6.4 Constraints → Anchors 映射

> **适用范围**：Constraints 映射仅在父节点 `layoutMode` 为 `NONE`（绝对定位）时生效。Auto Layout 容器内的子节点由布局系统控制，不使用 Constraints。

#### 6.4.1 Figma Constraints 完整语义

**水平约束 (horizontal)**：

| Constraint | 说明 | AnchorMin.X | AnchorMax.X | Offsets 含义 |
|------------|------|-------------|-------------|-------------|
| `LEFT` | 锚定左边 | 0 | 0 | Left=距左边距, Right=宽度 |
| `RIGHT` | 锚定右边 | 1 | 1 | Left=距右边距(负值), Right=宽度 |
| `CENTER` | 水平居中 | 0.5 | 0.5 | Left=中心X偏移, Right=宽度 |
| `SCALE` | 比例缩放 | 子左边/父宽 | 子右边/父宽 | Left=0, Right=0 |
| `LEFT_RIGHT` | 两边锚定(拉伸) | 0 | 1 | Left=左边距, Right=右边距 |

**垂直约束 (vertical)**：

| Constraint | 说明 | AnchorMin.Y | AnchorMax.Y | Offsets 含义 |
|------------|------|-------------|-------------|-------------|
| `TOP` | 锚定顶部 | 0 | 0 | Top=距顶边距, Bottom=高度 |
| `BOTTOM` | 锚定底部 | 1 | 1 | Top=距底边距(负值), Bottom=高度 |
| `CENTER` | 垂直居中 | 0.5 | 0.5 | Top=中心Y偏移, Bottom=高度 |
| `SCALE` | 比例缩放 | 子顶边/父高 | 子底边/父高 | Top=0, Bottom=0 |
| `TOP_BOTTOM` | 上下锚定(拉伸) | 0 | 1 | Top=顶边距, Bottom=底边距 |

> **SCALE vs LEFT_RIGHT/TOP_BOTTOM**：
> - `SCALE`：元素按父容器比例缩放，保持相对位置比例
> - `LEFT_RIGHT`/`TOP_BOTTOM`：元素两边锚定，保持固定边距

#### 6.4.2 实现代码

```cpp
void FFigmaParser::MapConstraintsToAnchors(
    const TSharedPtr<FJsonObject>& Constraints,
    const FPenNode& Child, const FPenNode& Parent,
    FVector2D& OutAnchorMin, FVector2D& OutAnchorMax,
    FMargin& OutOffsets)
{
    // 添加字段存在性检查，默认为LEFT/TOP
    FString H = (Constraints.IsValid() && Constraints->HasField("horizontal"))
        ? Constraints->GetStringField("horizontal")
        : TEXT("LEFT");
    FString V = (Constraints.IsValid() && Constraints->HasField("vertical"))
        ? Constraints->GetStringField("vertical")
        : TEXT("TOP");

    // 子节点边界（相对于父节点）
    float ChildLeft = Child.X;
    float ChildTop = Child.Y;
    float ChildRight = Child.X + Child.Width;
    float ChildBottom = Child.Y + Child.Height;

    // ========== 水平约束 ==========
    if (H == "LEFT")
    {
        // 锚定左边：固定距左边距，保持宽度
        OutAnchorMin.X = 0;
        OutAnchorMax.X = 0;
        OutOffsets.Left = ChildLeft;
        OutOffsets.Right = Child.Width;
    }
    else if (H == "RIGHT")
    {
        // 锚定右边：固定距右边距，保持宽度
        OutAnchorMin.X = 1;
        OutAnchorMax.X = 1;
        OutOffsets.Left = ChildLeft - Parent.Width;  // 负值表示距右边距
        OutOffsets.Right = Child.Width;
    }
    else if (H == "CENTER")
    {
        // 水平居中：锚点在中心，偏移量为距中心的距离
        OutAnchorMin.X = 0.5f;
        OutAnchorMax.X = 0.5f;
        float CenterX = Parent.Width * 0.5f;
        OutOffsets.Left = ChildLeft - CenterX;  // 子左上角相对父中心
        OutOffsets.Right = Child.Width;
    }
    else if (H == "SCALE")
    {
        // 比例缩放：Anchor 记录相对位置比例
        // 注意：添加除零保护
        if (Parent.Width > KINDA_SMALL_NUMBER)
        {
            OutAnchorMin.X = ChildLeft / Parent.Width;
            OutAnchorMax.X = ChildRight / Parent.Width;
            OutOffsets.Left = 0;
            OutOffsets.Right = 0;
        }
        else
        {
            // 父容器宽度为0，回退到LEFT模式
            UE_LOG(LogFigmaParser, Warning,
                TEXT("Parent width is zero for SCALE constraint, falling back to LEFT"));
            OutAnchorMin.X = 0;
            OutAnchorMax.X = 0;
            OutOffsets.Left = ChildLeft;
            OutOffsets.Right = Child.Width;
        }
    }
    else if (H == "LEFT_RIGHT")
    {
        // 两边锚定：保持左右边距，宽度随父容器变化
        OutAnchorMin.X = 0;
        OutAnchorMax.X = 1;
        OutOffsets.Left = ChildLeft;                        // 左边距
        OutOffsets.Right = Parent.Width - ChildRight;       // 右边距
    }
    else
    {
        // 默认：左上锚定
        OutAnchorMin.X = 0;
        OutAnchorMax.X = 0;
        OutOffsets.Left = ChildLeft;
        OutOffsets.Right = Child.Width;
    }

    // ========== 垂直约束 ==========
    if (V == "TOP")
    {
        // 锚定顶部：固定距顶边距，保持高度
        OutAnchorMin.Y = 0;
        OutAnchorMax.Y = 0;
        OutOffsets.Top = ChildTop;
        OutOffsets.Bottom = Child.Height;
    }
    else if (V == "BOTTOM")
    {
        // 锚定底部：固定距底边距，保持高度
        OutAnchorMin.Y = 1;
        OutAnchorMax.Y = 1;
        OutOffsets.Top = ChildTop - Parent.Height;  // 负值表示距底边距
        OutOffsets.Bottom = Child.Height;
    }
    else if (V == "CENTER")
    {
        // 垂直居中：锚点在中心，偏移量为距中心的距离
        OutAnchorMin.Y = 0.5f;
        OutAnchorMax.Y = 0.5f;
        float CenterY = Parent.Height * 0.5f;
        OutOffsets.Top = ChildTop - CenterY;  // 子左上角相对父中心
        OutOffsets.Bottom = Child.Height;
    }
    else if (V == "SCALE")
    {
        // 比例缩放：Anchor 记录相对位置比例
        // 注意：添加除零保护
        if (Parent.Height > KINDA_SMALL_NUMBER)
        {
            OutAnchorMin.Y = ChildTop / Parent.Height;
            OutAnchorMax.Y = ChildBottom / Parent.Height;
            OutOffsets.Top = 0;
            OutOffsets.Bottom = 0;
        }
        else
        {
            // 父容器高度为0，回退到TOP模式
            UE_LOG(LogFigmaParser, Warning,
                TEXT("Parent height is zero for SCALE constraint, falling back to TOP"));
            OutAnchorMin.Y = 0;
            OutAnchorMax.Y = 0;
            OutOffsets.Top = ChildTop;
            OutOffsets.Bottom = Child.Height;
        }
    }
    else if (V == "TOP_BOTTOM")
    {
        // 上下锚定：保持上下边距，高度随父容器变化
        OutAnchorMin.Y = 0;
        OutAnchorMax.Y = 1;
        OutOffsets.Top = ChildTop;                          // 顶边距
        OutOffsets.Bottom = Parent.Height - ChildBottom;    // 底边距
    }
    else
    {
        // 默认：左上锚定
        OutAnchorMin.Y = 0;
        OutAnchorMax.Y = 0;
        OutOffsets.Top = ChildTop;
        OutOffsets.Bottom = Child.Height;
    }
}
```

#### 6.4.3 UMG FAnchors/FMargin 语义说明

UMG 的 `UCanvasPanelSlot` 使用以下语义：

**当 AnchorMin == AnchorMax（点锚定）**：
- `Offsets.Left/Top`：相对于锚点的位置偏移
- `Offsets.Right/Bottom`：控件的宽度/高度

**当 AnchorMin != AnchorMax（拉伸锚定）**：
- `Offsets.Left`：距左锚点的边距
- `Offsets.Right`：距右锚点的边距
- `Offsets.Top`：距上锚点的边距
- `Offsets.Bottom`：距下锚点的边距

```cpp
// 应用到 UCanvasPanelSlot
void ApplyAnchorsToSlot(UCanvasPanelSlot* Slot,
    const FVector2D& AnchorMin, const FVector2D& AnchorMax,
    const FMargin& Offsets)
{
    Slot->SetAnchors(FAnchors(AnchorMin.X, AnchorMin.Y,
                               AnchorMax.X, AnchorMax.Y));
    Slot->SetOffsets(Offsets);

    // 拉伸模式（AnchorMin != AnchorMax）：AutoSize 无效，尺寸由锚点差值决定
    // 点锚定模式：AutoSize=false 时使用 Offsets 中的宽高值
    // 统一设为 false，确保 Offsets 中的尺寸生效
    Slot->SetAutoSize(false);
}
```

### 6.5 图片资源处理

#### 6.5.1 IMAGE Fill 处理

IMAGE fill 的图片资源通过 `imageRef` 字段获取，需调用专门的文件图片端点：

```cpp
// 获取文件内所有图片资源的 URL 映射
void FFigmaClient::GetFileImages(const FString& FileKey,
    TFunction<void(TMap<FString, FString>)> OnSuccess)
{
    // API: GET /v1/files/{file_key}/images
    // 返回格式: { "images": { "imageRef": "https://..." } }
    FString Url = FString::Printf(TEXT("%s/files/%s/images"), *BaseUrl, *FileKey);
    // ...
}
```

**图片导入流程**：
1. 识别 `fills` 中 `type: "IMAGE"` 的节点，提取 `imageRef`
2. 调用 `GET /v1/files/{file_key}/images` 获取 imageRef → URL 映射
3. **立即下载**图片到 `Content/Textures/Figma/`（URL 有效期约 14 天）
4. 创建 `UTexture2D` 资产

> **注意**：图片 URL 有时效限制，必须在获取后立即下载并本地缓存。

#### 6.5.2 节点渲染导出

如需将节点渲染为图片（如矢量图形、复杂效果），使用渲染端点：

> **注意**：渲染导出的 URL 有效期约 30 天，且某些节点可能返回 `null`（不可渲染）。

```cpp
// 将节点渲染为图片
void FFigmaClient::RenderNodes(const FString& FileKey,
    const TArray<FString>& NodeIds,
    TFunction<void(TMap<FString, FString>)> OnSuccess)
{
    // API: GET /v1/images/{file_key}?ids={node_ids}&format=png&scale=2
    FString IdsParam = FString::Join(NodeIds, TEXT(","));
    FString Url = FString::Printf(
        TEXT("%s/images/%s?ids=%s&format=png&scale=2"),
        *BaseUrl, *FileKey, *IdsParam);

    // 返回格式: { "images": { "node_id": "https://..." } }
}
```

### 6.6 字体映射

| Figma 字体 | UE5 字体 |
|-----------|---------|
| `Inter` | `Roboto` |
| `SF Pro` | `Roboto` |
| `Roboto` | `Roboto` |

---

## 7. 用户界面

### 7.1 设置面板

```
┌─────────────────────────────────────────┐
│  Figma Settings                         │
├─────────────────────────────────────────┤
│  API Token: [________________] [Test]   │
│                                         │
│  ☑ Save token to config                 │
└─────────────────────────────────────────┘
```

### 7.2 导入面板

```
┌─────────────────────────────────────────┐
│  Import from Figma                      │
├─────────────────────────────────────────┤
│  Figma URL: [________________________]  │
│                                         │
│  [Fetch Preview]                        │
│                                         │
│  ┌─────────────────────────────────┐    │
│  │      (Preview Image)            │    │
│  └─────────────────────────────────┘    │
│                                         │
│  [Import to UMG]                        │
└─────────────────────────────────────────┘
```

---

## 8. Phase 1 已知限制（完整还原方案见第 9 章）

| 限制项 | 说明 | Tier | 还原方案 |
|-------|------|------|---------|
| 组件复用 | COMPONENT/INSTANCE 直接展开，不保留引用关系 | 1 | Phase 2 支持 overrides |
| 渐变填充 | 降级为首个色标纯色 | 1 | 预烘焙纹理（见 9.2.1） |
| 多重填充/描边 | 填充取第一个可见的 SOLID fill，描边仅取 strokes[0] | 2 | Overlay 多层叠加 |
| 旋转变换 | 仅支持简单 rotation，不支持 relativeTransform 矩阵 | 1/3 | RenderTransform / 矩阵分解（见 9.4.3） |
| 矢量路径 | VECTOR 节点简化为边界框 frame | 1 | SVG 渲染（见 9.2.3） |
| 文本换行 | 依赖 UE5 AutoWrapText，与 Figma 可能有差异 | 1 | WrapTextAt 精确控制 |
| 图片资源 | 仅支持 IMAGE fill，不支持 GIF/视频 | 2 | MediaPlayer 扩展 |
| 混排文本 | 仅支持单一样式，不支持 characterStyleOverrides | 3 | RichTextBlock（见 9.4.1） |
| 文本截断 | 不支持 textTruncation/maxLines 省略号 | 3 | TextOverflowPolicy（见 9.4.2） |
| Auto Layout 子项 | 缺少 layoutAlign/layoutGrow/layoutPositioning | 1 | Slot 属性映射（见 9.2.5） |
| ~~节点透明度~~ | 有效透明度 = node.opacity × paint.opacity × color.a | - | ✓ 已实现 |
| FigJam 节点 | 不支持 board 类型文件的特有节点 | - | 暂不支持 |
| 行高单位 | 仅使用 lineHeightPx | 1 | 支持 lineHeightUnit |
| Auto Layout 换行 | 不支持 layoutWrap | 3 | 自定义 WrapBox |
| 基线对齐 | 不支持 BASELINE | 3 | 自定义对齐逻辑 |
| 渲染边界 | 仅使用 absoluteBoundingBox | 2 | 使用 absoluteRenderBounds |
| 填充类型 | 仅支持 SOLID/GRADIENT/IMAGE | 2/3 | VIDEO→MediaPlayer, PATTERN→平铺 |
| 节点效果 | 不支持 DROP_SHADOW/BLUR 等 | 2 | 预烘焙+材质（见 9.3.1/9.3.2） |
| Constraints 对齐 | 未设置 UMG Alignment/Pivot | 1 | Pivot 设置（见 9.2.6） |
| 矢量几何数据 | 未使用 geometry=paths 参数 | 1 | API 添加参数（见 9.2.3） |
| 非均匀圆角 | 四角圆角值不同时取最大值 | 1 | 完整纹理（见 9.2.7） |
| 混合模式 | 不支持 blendMode | 2 | RetainerBox+材质（见 9.3.3） |
| outside/center 描边 | 仅支持 inside 描边 | 2 | 扩展边界（见 9.3.4） |

---

## 9. 百分百还原方案

### 9.1 分层实现策略

基于 ROI（投资回报率）将功能分为三层：

| 层级 | 描述 | 还原度目标 | 工作量 |
|------|------|-----------|--------|
| **Tier 1** | 纯 UMG 方案 | 80% → 90% | 2-3 天 |
| **Tier 2** | 材质辅助方案 | 90% → 97% | 3-4 天 |
| **Tier 3** | 自定义渲染 | 97% → 100% | 4-5 天 |

### 9.2 Tier 1: 纯 UMG 方案（复用 PenToUMG）

以下功能可直接复用 PenToUMG 已有实现：

#### 9.2.1 渐变填充 → 预烘焙纹理

```cpp
// 复用 PenToUMG 的 BakeGradientTexture（见 PenToUMG 12.1）
UTexture2D* BakeGradientTexture(const FPenGradient& Gradient, int32 Width, int32 Height,
                                 UObject* Outer, const FString& TextureName)
{
    UTexture2D* Texture = NewObject<UTexture2D>(Outer, *TextureName, RF_Public | RF_Standalone);
    Texture->Source.Init(Width, Height, 1, 1, TSF_BGRA8);

    uint8* MipData = Texture->Source.LockMip(0);
    FColor* Pixels = reinterpret_cast<FColor*>(MipData);

    for (int32 Y = 0; Y < Height; Y++)
    {
        for (int32 X = 0; X < Width; X++)
        {
            float T = CalculateGradientT(X, Y, Width, Height, Gradient);
            Pixels[Y * Width + X] = SampleGradient(Gradient.Colors, T);
        }
    }

    Texture->Source.UnlockMip(0);
    Texture->UpdateResource();
    return Texture;
}

// Figma 渐变转换
FPenGradient ConvertFigmaGradient(const TSharedPtr<FJsonObject>& Fill)
{
    FPenGradient Result;
    FString Type = Fill->GetStringField("type");

    if (Type == "GRADIENT_LINEAR")
        Result.GradientType = "linear";
    else if (Type == "GRADIENT_RADIAL")
        Result.GradientType = "radial";
    else if (Type == "GRADIENT_ANGULAR")
        Result.GradientType = "angular";
    else if (Type == "GRADIENT_DIAMOND")
        Result.GradientType = "radial"; // 降级处理

    // 从 gradientHandlePositions 计算角度
    auto Handles = Fill->GetArrayField("gradientHandlePositions");
    if (Handles.Num() >= 2)
    {
        auto P0 = Handles[0]->AsObject();
        auto P1 = Handles[1]->AsObject();
        float Dx = P1->GetNumberField("x") - P0->GetNumberField("x");
        float Dy = P1->GetNumberField("y") - P0->GetNumberField("y");
        Result.Rotation = FMath::RadiansToDegrees(FMath::Atan2(Dy, Dx));
    }

    // 解析色标
    for (auto& Stop : Fill->GetArrayField("gradientStops"))
    {
        auto StopObj = Stop->AsObject();
        FPenGradientStop GradStop;
        GradStop.Position = StopObj->GetNumberField("position");
        auto Color = StopObj->GetObjectField("color");
        GradStop.Color = RGBAToHex(
            Color->GetNumberField("r"),
            Color->GetNumberField("g"),
            Color->GetNumberField("b"),
            Color->GetNumberField("a"));
        Result.Colors.Add(GradStop);
    }

    return Result;
}
```

#### 9.2.2 圆角 → 九宫格纹理

```cpp
// 复用 PenToUMG 的 GenerateRoundedRectTexture（见 PenToUMG 3.7）
// Figma 圆角可能是数组 [tl, tr, br, bl] 或单值
void ParseFigmaCornerRadius(const TSharedPtr<FJsonObject>& Node, FPenNode& Result)
{
    if (Node->HasField("cornerRadius"))
    {
        Result.CornerRadius = Node->GetNumberField("cornerRadius");
    }

    // 非均匀圆角
    if (Node->HasField("rectangleCornerRadii"))
    {
        auto Radii = Node->GetArrayField("rectangleCornerRadii");
        for (auto& R : Radii)
            Result.CornerRadii.Add(R->AsNumber());
    }
}
```

#### 9.2.3 矢量路径 → SVG 渲染

```cpp
// 复用 PenToUMG 的 RenderSvgToTexture（见 PenToUMG 3.11）
// 需要 API 调用时添加 geometry=paths 参数
FString BuildFigmaApiUrl(const FString& FileKey, const FString& NodeId)
{
    return FString::Printf(
        TEXT("https://api.figma.com/v1/files/%s/nodes?ids=%s&geometry=paths"),
        *FileKey, *NodeId);
}

// VECTOR 节点处理
UWidget* CreateVectorWidget(const TSharedPtr<FJsonObject>& Node)
{
    // 从 fillGeometry/strokeGeometry 获取路径数据
    FString SvgPath;
    if (Node->HasField("fillGeometry"))
    {
        auto Geom = Node->GetArrayField("fillGeometry");
        if (Geom.Num() > 0)
            SvgPath = Geom[0]->AsObject()->GetStringField("path");
    }

    // 构建 SVG 并渲染
    FString Svg = BuildSvgFromPath(Node, SvgPath);
    UTexture2D* Texture = RenderSvgToTexture(Svg, Node.Width, Node.Height);

    UImage* Image = NewObject<UImage>();
    Image->SetBrushFromTexture(Texture);
    return Image;
}
```

#### 9.2.4 旋转变换 → RenderTransform

```cpp
// 复用 PenToUMG 的 ApplyRotation（见 PenToUMG 3.2）
void ApplyRotation(UWidget* Widget, float Rotation)
{
    if (Rotation != 0.0f)
    {
        FWidgetTransform Transform = Widget->GetRenderTransform();
        Transform.Angle = Rotation;
        Widget->SetRenderTransform(Transform);
    }
}
```

#### 9.2.5 Auto Layout 子项完整映射

```cpp
// layoutAlign 映射（交叉轴自对齐）
void ApplyLayoutAlign(UPanelSlot* Slot, const FString& LayoutAlign, const FString& ParentLayout)
{
    if (LayoutAlign == "STRETCH")
    {
        // 拉伸填充
        if (ParentLayout == "HORIZONTAL")
            Cast<UHorizontalBoxSlot>(Slot)->SetVerticalAlignment(VAlign_Fill);
        else
            Cast<UVerticalBoxSlot>(Slot)->SetHorizontalAlignment(HAlign_Fill);
    }
    else if (LayoutAlign == "MIN")
    {
        if (ParentLayout == "HORIZONTAL")
            Cast<UHorizontalBoxSlot>(Slot)->SetVerticalAlignment(VAlign_Top);
        else
            Cast<UVerticalBoxSlot>(Slot)->SetHorizontalAlignment(HAlign_Left);
    }
    else if (LayoutAlign == "CENTER")
    {
        if (ParentLayout == "HORIZONTAL")
            Cast<UHorizontalBoxSlot>(Slot)->SetVerticalAlignment(VAlign_Center);
        else
            Cast<UVerticalBoxSlot>(Slot)->SetHorizontalAlignment(HAlign_Center);
    }
    else if (LayoutAlign == "MAX")
    {
        if (ParentLayout == "HORIZONTAL")
            Cast<UHorizontalBoxSlot>(Slot)->SetVerticalAlignment(VAlign_Bottom);
        else
            Cast<UVerticalBoxSlot>(Slot)->SetHorizontalAlignment(HAlign_Right);
    }
}

// layoutGrow 映射（主轴填充比例）
void ApplyLayoutGrow(UPanelSlot* Slot, float LayoutGrow, const FString& ParentLayout)
{
    if (LayoutGrow > 0)
    {
        if (auto* HSlot = Cast<UHorizontalBoxSlot>(Slot))
            HSlot->SetSize(ESlateSizeRule::Fill);
        else if (auto* VSlot = Cast<UVerticalBoxSlot>(Slot))
            VSlot->SetSize(ESlateSizeRule::Fill);
    }
}

// layoutPositioning 映射（绝对定位）
bool IsAbsolutePositioned(const TSharedPtr<FJsonObject>& Node)
{
    return Node->HasField("layoutPositioning") &&
           Node->GetStringField("layoutPositioning") == "ABSOLUTE";
}
```

#### 9.2.6 Constraints 对齐精确处理

```cpp
// 设置 Pivot 确保对齐精确
void ApplyPivotForConstraints(UWidget* Widget, const FString& HConstraint, const FString& VConstraint)
{
    FVector2D Pivot(0, 0);

    // 水平 Pivot
    if (HConstraint == "CENTER")
        Pivot.X = 0.5f;
    else if (HConstraint == "RIGHT")
        Pivot.X = 1.0f;

    // 垂直 Pivot
    if (VConstraint == "CENTER")
        Pivot.Y = 0.5f;
    else if (VConstraint == "BOTTOM")
        Pivot.Y = 1.0f;

    Widget->SetRenderTransformPivot(Pivot);
}
```

#### 9.2.7 非均匀圆角

```cpp
// 四角不同时，生成完整尺寸纹理（不使用九宫格拉伸）
UTexture2D* GenerateNonUniformRoundedRectTexture(
    const TArray<float>& CornerRadii,  // [tl, tr, br, bl]
    float Width, float Height,
    const FLinearColor& FillColor,
    UObject* Outer, const FString& TextureName)
{
    int32 TexW = FMath::CeilToInt(Width);
    int32 TexH = FMath::CeilToInt(Height);

    UTexture2D* Texture = NewObject<UTexture2D>(Outer, *TextureName, RF_Public | RF_Standalone);
    Texture->Source.Init(TexW, TexH, 1, 1, TSF_BGRA8);

    uint8* MipData = Texture->Source.LockMip(0);
    FColor* Pixels = reinterpret_cast<FColor*>(MipData);

    for (int32 Y = 0; Y < TexH; Y++)
    {
        for (int32 X = 0; X < TexW; X++)
        {
            float Dist = CalculateNonUniformRoundedRectDistance(
                X, Y, TexW, TexH, CornerRadii);

            if (Dist < 0)
                Pixels[Y * TexW + X] = FillColor.ToFColor(true);
            else if (Dist < 1.0f)
            {
                // 抗锯齿边缘
                FColor C = FillColor.ToFColor(true);
                C.A = FMath::RoundToInt(C.A * (1.0f - Dist));
                Pixels[Y * TexW + X] = C;
            }
            else
                Pixels[Y * TexW + X] = FColor::Transparent;
        }
    }

    Texture->Source.UnlockMip(0);
    Texture->UpdateResource();
    return Texture;
}

float CalculateNonUniformRoundedRectDistance(int32 X, int32 Y, int32 W, int32 H,
                                              const TArray<float>& Radii)
{
    float TL = Radii.Num() > 0 ? Radii[0] : 0;
    float TR = Radii.Num() > 1 ? Radii[1] : 0;
    float BR = Radii.Num() > 2 ? Radii[2] : 0;
    float BL = Radii.Num() > 3 ? Radii[3] : 0;

    // 判断在哪个角的区域
    if (X < TL && Y < TL)
        return FMath::Sqrt(FMath::Square(TL - X) + FMath::Square(TL - Y)) - TL;
    if (X > W - TR && Y < TR)
        return FMath::Sqrt(FMath::Square(X - (W - TR)) + FMath::Square(TR - Y)) - TR;
    if (X > W - BR && Y > H - BR)
        return FMath::Sqrt(FMath::Square(X - (W - BR)) + FMath::Square(Y - (H - BR))) - BR;
    if (X < BL && Y > H - BL)
        return FMath::Sqrt(FMath::Square(BL - X) + FMath::Square(Y - (H - BL))) - BL;

    // 边缘或中心区域
    float DistLeft = X;
    float DistRight = W - X;
    float DistTop = Y;
    float DistBottom = H - Y;
    return -FMath::Min(FMath::Min(DistLeft, DistRight), FMath::Min(DistTop, DistBottom));
}
```

### 9.3 Tier 2: 材质辅助方案

#### 9.3.1 阴影效果 (DROP_SHADOW / INNER_SHADOW)

```cpp
// 预烘焙阴影纹理 + Overlay 叠加
UWidget* CreateShadowedWidget(UWidget* Content, const TArray<FFigmaEffect>& Effects)
{
    UOverlay* Overlay = NewObject<UOverlay>();

    for (const FFigmaEffect& Effect : Effects)
    {
        if (Effect.Type == "DROP_SHADOW" && Effect.bVisible)
        {
            // 生成阴影纹理
            UTexture2D* ShadowTex = BakeShadowTexture(
                Content->GetDesiredSize(),
                Effect.Radius,
                Effect.Color,
                Effect.Offset);

            // 添加阴影图层（在内容下方）
            UImage* ShadowImage = NewObject<UImage>();
            ShadowImage->SetBrushFromTexture(ShadowTex);
            Overlay->AddChild(ShadowImage);
        }
    }

    // 添加内容
    Overlay->AddChild(Content);

    // 处理内阴影（在内容上方）
    for (const FFigmaEffect& Effect : Effects)
    {
        if (Effect.Type == "INNER_SHADOW" && Effect.bVisible)
        {
            UTexture2D* InnerShadowTex = BakeInnerShadowTexture(
                Content->GetDesiredSize(),
                Effect.Radius,
                Effect.Color,
                Effect.Offset);

            UImage* InnerShadowImage = NewObject<UImage>();
            InnerShadowImage->SetBrushFromTexture(InnerShadowTex);
            Overlay->AddChild(InnerShadowImage);
        }
    }

    return Overlay;
}

UTexture2D* BakeShadowTexture(FVector2D Size, float Radius, FLinearColor Color, FVector2D Offset)
{
    // 扩展纹理尺寸以容纳阴影扩散
    int32 Padding = FMath::CeilToInt(Radius * 3);
    int32 TexW = FMath::CeilToInt(Size.X) + Padding * 2;
    int32 TexH = FMath::CeilToInt(Size.Y) + Padding * 2;

    UTexture2D* Texture = UTexture2D::CreateTransient(TexW, TexH, PF_R8G8B8A8);
    // ... 高斯模糊阴影生成
    return Texture;
}
```

#### 9.3.2 模糊效果 (LAYER_BLUR / BACKGROUND_BLUR)

```cpp
// 使用 UBackgroundBlur（毛玻璃效果）
UWidget* CreateBlurredWidget(UWidget* Content, const FFigmaEffect& Effect)
{
    if (Effect.Type == "BACKGROUND_BLUR")
    {
        UBackgroundBlur* Blur = NewObject<UBackgroundBlur>();
        Blur->SetBlurStrength(Effect.Radius);
        Blur->AddChild(Content);
        return Blur;
    }
    else if (Effect.Type == "LAYER_BLUR")
    {
        // 使用 RetainerBox + 模糊材质
        URetainerBox* Retainer = NewObject<URetainerBox>();
        Retainer->SetEffectMaterial(LoadBlurMaterial(Effect.Radius));
        Retainer->AddChild(Content);
        return Retainer;
    }
    return Content;
}

UMaterialInterface* LoadBlurMaterial(float Radius)
{
    // 加载预制的模糊材质并设置参数
    UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(
        GaussianBlurMaterial, nullptr);
    MID->SetScalarParameterValue("BlurRadius", Radius);
    return MID;
}
```

#### 9.3.3 混合模式 (blendMode)

> **重要**: UE5 的 `ESlateBlendMode` 枚举仅支持有限的混合模式（Normal、Additive、Modulate）。
> Figma 的 Multiply/Screen/Overlay 等混合模式**必须通过 RetainerBox + 自定义材质实现**。

```cpp
// 映射 Figma blendMode → UMG 实现方式
// 注意：ESlateBlendMode 不支持 Multiply/Screen/Overlay 等，必须用材质实现
UWidget* ApplyBlendMode(UWidget* Content, const FString& FigmaBlendMode)
{
    // Normal 和 PASS_THROUGH 不需要处理
    if (FigmaBlendMode == "NORMAL" || FigmaBlendMode == "PASS_THROUGH" || FigmaBlendMode.IsEmpty())
    {
        return Content;
    }

    // 所有其他混合模式都需要 RetainerBox + 材质
    // 包括：MULTIPLY, SCREEN, OVERLAY, DARKEN, LIGHTEN,
    //       COLOR_DODGE, COLOR_BURN, HARD_LIGHT, SOFT_LIGHT,
    //       DIFFERENCE, EXCLUSION, HUE, SATURATION, COLOR, LUMINOSITY
    URetainerBox* Retainer = NewObject<URetainerBox>();
    Retainer->SetEffectMaterial(CreateBlendMaterial(FigmaBlendMode));
    Retainer->AddChild(Content);
    return Retainer;
}

// 创建混合模式材质
UMaterialInstanceDynamic* CreateBlendMaterial(const FString& BlendMode)
{
    // 加载对应的混合模式材质
    FString MaterialPath = FString::Printf(
        TEXT("/ImageToUMG/Materials/BlendModes/M_Blend_%s"), *BlendMode);

    UMaterial* BaseMat = LoadObject<UMaterial>(nullptr, *MaterialPath);
    if (!BaseMat)
    {
        // 回退到通用混合材质
        BaseMat = LoadObject<UMaterial>(nullptr,
            TEXT("/ImageToUMG/Materials/M_BlendGeneric"));
    }

    return UMaterialInstanceDynamic::Create(BaseMat, nullptr);
}
```

#### 9.3.4 Outside/Center 描边

```cpp
// outside/center 描边需要扩展边界
void ApplyStrokeWithAlignment(UBorder* Border, const FPenStroke& Stroke,
                               float Width, float Height)
{
    if (Stroke.Align == "INSIDE")
    {
        // 标准处理，九宫格纹理
        ApplyRoundedCornerWithStroke(Border, Width, Height, Stroke);
    }
    else if (Stroke.Align == "OUTSIDE")
    {
        // 扩展边界
        float Expansion = Stroke.Thickness;
        float NewWidth = Width + Expansion * 2;
        float NewHeight = Height + Expansion * 2;

        // 生成包含外描边的纹理
        UTexture2D* Tex = GenerateOutsideStrokeTexture(
            NewWidth, NewHeight, Width, Height, Stroke);
        Border->SetBrushFromTexture(Tex);

        // 调整位置偏移
        Border->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
        FWidgetTransform Transform;
        Transform.Translation = FVector2D(-Expansion, -Expansion);
        Border->SetRenderTransform(Transform);
    }
    else if (Stroke.Align == "CENTER")
    {
        float Expansion = Stroke.Thickness / 2;
        // 类似 outside 处理
    }
}
```

### 9.4 Tier 3: 自定义渲染

#### 9.4.1 混排文本 (characterStyleOverrides)

```cpp
// 构建 RichTextBlock 样式标签
FString BuildRichTextTag(const TSharedPtr<FJsonObject>& Style)
{
    FString Tag = TEXT("<span");

    // 字体大小
    if (Style->HasField(TEXT("fontSize")))
    {
        Tag += FString::Printf(TEXT(" size=\"%d\""),
            FMath::RoundToInt(Style->GetNumberField(TEXT("fontSize"))));
    }

    // 字体颜色
    if (Style->HasField(TEXT("fills")))
    {
        auto Fills = Style->GetArrayField(TEXT("fills"));
        if (Fills.Num() > 0)
        {
            auto Fill = Fills[0]->AsObject();
            if (Fill->HasField(TEXT("color")))
            {
                auto Color = Fill->GetObjectField(TEXT("color"));
                int32 R = FMath::RoundToInt(Color->GetNumberField(TEXT("r")) * 255);
                int32 G = FMath::RoundToInt(Color->GetNumberField(TEXT("g")) * 255);
                int32 B = FMath::RoundToInt(Color->GetNumberField(TEXT("b")) * 255);
                Tag += FString::Printf(TEXT(" color=\"#%02X%02X%02X\""), R, G, B);
            }
        }
    }

    // 字重
    if (Style->HasField(TEXT("fontWeight")))
    {
        int32 Weight = FMath::RoundToInt(Style->GetNumberField(TEXT("fontWeight")));
        if (Weight >= 700)
        {
            Tag += TEXT(" style=\"bold\"");
        }
    }

    Tag += TEXT(">");
    return Tag;
}

// 使用 RichTextBlock 实现混排文本
UWidget* CreateRichText(const TSharedPtr<FJsonObject>& TextNode)
{
    URichTextBlock* RichText = NewObject<URichTextBlock>();

    // 安全获取字段
    if (!TextNode->HasField(TEXT("characters")))
    {
        return RichText;
    }

    FString Characters = TextNode->GetStringField(TEXT("characters"));

    // 检查是否有样式覆盖
    if (!TextNode->HasField(TEXT("characterStyleOverrides")) ||
        !TextNode->HasField(TEXT("styleOverrideTable")))
    {
        // 无混排，直接设置文本
        RichText->SetText(FText::FromString(Characters));
        return RichText;
    }

    auto StyleOverrides = TextNode->GetArrayField(TEXT("characterStyleOverrides"));
    auto StyleMap = TextNode->GetObjectField(TEXT("styleOverrideTable"));

    // 构建富文本标记
    FString RichContent;
    int32 CurrentStyle = -1;

    for (int32 i = 0; i < Characters.Len(); i++)
    {
        int32 StyleId = (i < StyleOverrides.Num())
            ? static_cast<int32>(StyleOverrides[i]->AsNumber())
            : 0;

        if (StyleId != CurrentStyle)
        {
            if (CurrentStyle > 0)
            {
                RichContent += TEXT("</span>");
            }

            if (StyleId > 0)
            {
                FString StyleKey = FString::FromInt(StyleId);
                if (StyleMap->HasField(StyleKey))
                {
                    auto Style = StyleMap->GetObjectField(StyleKey);
                    RichContent += BuildRichTextTag(Style);
                }
            }
            CurrentStyle = StyleId;
        }

        // 转义特殊字符
        TCHAR Ch = Characters[i];
        if (Ch == '<') RichContent += TEXT("&lt;");
        else if (Ch == '>') RichContent += TEXT("&gt;");
        else if (Ch == '&') RichContent += TEXT("&amp;");
        else RichContent += Ch;
    }

    if (CurrentStyle > 0)
    {
        RichContent += TEXT("</span>");
    }

    RichText->SetText(FText::FromString(RichContent));
    return RichText;
}
```

#### 9.4.2 文本截断省略号

```cpp
// 使用 TextOverflowPolicy 实现省略号截断
UWidget* CreateTruncatedText(const TSharedPtr<FJsonObject>& TextNode)
{
    // 安全检查
    if (!TextNode.IsValid())
    {
        return nullptr;
    }

    bool bTruncate = TextNode->HasField(TEXT("textTruncation")) &&
                     TextNode->GetStringField(TEXT("textTruncation")) == TEXT("ENDING");

    int32 MaxLines = TextNode->HasField(TEXT("maxLines"))
        ? TextNode->GetIntegerField(TEXT("maxLines"))
        : 0;

    UTextBlock* Text = NewObject<UTextBlock>();

    // 安全获取文本内容
    FString Content = TextNode->HasField(TEXT("characters"))
        ? TextNode->GetStringField(TEXT("characters"))
        : TEXT("");
    Text->SetText(FText::FromString(Content));

    if (bTruncate)
    {
        Text->SetClipping(EWidgetClipping::ClipToBounds);
        Text->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
    }

    if (MaxLines > 0)
    {
        // 计算行高
        float LineHeight = 0.0f;
        if (TextNode->HasField(TEXT("style")))
        {
            auto Style = TextNode->GetObjectField(TEXT("style"));
            if (Style->HasField(TEXT("lineHeightPx")))
            {
                LineHeight = Style->GetNumberField(TEXT("lineHeightPx"));
            }
        }

        // 回退：使用字体大小估算
        if (LineHeight < KINDA_SMALL_NUMBER)
        {
            float FontSize = TextNode->HasField(TEXT("style"))
                ? TextNode->GetObjectField(TEXT("style"))->GetNumberField(TEXT("fontSize"))
                : 14.0f;
            LineHeight = FontSize * 1.2f;
        }

        USizeBox* SizeBox = NewObject<USizeBox>();
        SizeBox->SetMaxDesiredHeight(LineHeight * MaxLines);
        SizeBox->AddChild(Text);
        return SizeBox;
    }

    return Text;
}
```

#### 9.4.3 完整变换矩阵 (含 shear)

```cpp
// 解析 Figma relativeTransform 矩阵并应用到 Widget
void ApplyFullTransform(UWidget* Widget, const TSharedPtr<FJsonObject>& Node)
{
    if (!Widget || !Node.IsValid() || !Node->HasField(TEXT("relativeTransform")))
    {
        return;
    }

    auto Transform = Node->GetArrayField(TEXT("relativeTransform"));
    if (Transform.Num() < 2)
    {
        return;
    }

    // Figma 矩阵格式: [[a, b, tx], [c, d, ty]]
    auto Row0 = Transform[0]->AsArray();
    auto Row1 = Transform[1]->AsArray();

    if (Row0.Num() < 3 || Row1.Num() < 3)
    {
        return;
    }

    float A = Row0[0]->AsNumber();   // cos(θ) * scaleX
    float B = Row0[1]->AsNumber();   // -sin(θ) * scaleX + shearX
    float Tx = Row0[2]->AsNumber();
    float C = Row1[0]->AsNumber();   // sin(θ) * scaleY
    float D = Row1[1]->AsNumber();   // cos(θ) * scaleY
    float Ty = Row1[2]->AsNumber();

    // 矩阵分解 (QR分解)
    float ScaleX = FMath::Sqrt(A * A + C * C);
    float ScaleY = (A * D - B * C) / FMath::Max(ScaleX, KINDA_SMALL_NUMBER);

    float Rotation = FMath::RadiansToDegrees(FMath::Atan2(C, A));

    // Shear 计算: shear = (A*B + C*D) / (A*D - B*C)
    float Det = A * D - B * C;
    float ShearX = 0.0f;
    if (FMath::Abs(Det) > KINDA_SMALL_NUMBER)
    {
        ShearX = (A * B + C * D) / Det;
    }

    // 应用变换
    FWidgetTransform WidgetTransform;
    WidgetTransform.Translation = FVector2D(Tx, Ty);
    WidgetTransform.Scale = FVector2D(ScaleX, ScaleY);
    WidgetTransform.Angle = Rotation;
    WidgetTransform.Shear = FVector2D(ShearX, 0.0f);

    Widget->SetRenderTransform(WidgetTransform);
}
```

### 9.5 实现计划

| 阶段 | 内容 | 工作量 |
|------|------|--------|
| Phase 1 | FFigmaClient HTTP 客户端 | 0.5天 |
| Phase 2 | FFigmaParser 核心转换 | 1.5天 |
| Phase 3 | 编辑器 UI 集成 | 0.5天 |
| Phase 4 | 测试与调试 | 1.5天 |
| **Phase 1 小计** | 基础功能 | **4天** |
| Phase 5 | Tier 1 完整实现（复用 PenToUMG） | 2-3天 |
| Phase 6 | Tier 2 材质辅助 | 3-4天 |
| Phase 7 | Tier 3 自定义渲染 | 4-5天 |
| **完整小计** | 百分百还原 | **13-16天** |

### 9.6 还原度检查清单

| 特性 | Tier | 还原方案 | 状态 |
|------|------|---------|------|
| 纯色填充 | - | 直接映射 | ✓ 已实现 |
| 节点透明度 | - | 三层叠乘 | ✓ 已实现 |
| 渐变填充 | 1 | 预烘焙纹理 | 待实现 |
| 均匀圆角 | 1 | 九宫格纹理 | 待实现 |
| 非均匀圆角 | 1 | 完整纹理 | 待实现 |
| 旋转变换 | 1 | RenderTransform | 待实现 |
| 矢量路径 | 1 | SVG→纹理 | 待实现 |
| Auto Layout 子项 | 1 | Slot 属性映射 | 待实现 |
| Constraints 对齐 | 1 | Pivot 设置 | 待实现 |
| 阴影效果 | 2 | 预烘焙+Overlay | 待实现 |
| 模糊效果 | 2 | UBackgroundBlur | 待实现 |
| 混合模式 | 2 | RetainerBox+材质 | 待实现 |
| outside 描边 | 2 | 扩展边界 | 待实现 |
| 混排文本 | 3 | RichTextBlock | ✓ 已实现 |
| 文本截断省略号 | 3 | TextOverflowPolicy | ✓ 已实现 |
| 完整变换矩阵 | 3 | 矩阵分解 | ✓ 已实现 |

---

## 10. 与 PenToUMG 共享架构

> 详细架构图见 [PenToUMG 第14章](./PenToUMG_TechnicalDesign.md#14-与-figmatoumg-共享架构)

### 10.1 共享模块（ImageToUMGCommon）

| 函数 | 说明 |
|------|------|
| `BakeGradientTexture` | 渐变纹理生成 |
| `GenerateRoundedRectTexture` | 圆角纹理生成 |
| `RenderSvgToTexture` | SVG路径渲染 |
| `BakeShadowTexture` | 阴影纹理生成 |
| `CreateBlurMaterial` | 模糊材质 |
| `CreateBlendMaterial` | 混合模式材质 |
| `ApplyRotation` | 旋转变换 |
| `ApplyFlip` | 翻转变换 |

### 10.2 Figma独有模块

| 函数 | 原因 |
|------|------|
| `BuildRichTextTag` | .pen实际不使用TextStyle[] |
| `CreateTruncatedText` | .pen无textTruncation |
| `ApplyFullTransform` | .pen无shear/skew |
| `ConvertFigmaGradient` | Figma渐变格式转换 |

---

## 11. 参考资料

- [Figma REST API 文档](https://www.figma.com/developers/api)
- [Figma API TypeScript 类型](https://github.com/figma/rest-api-spec)
- [PenToUMG 技术文档](./PenToUMG_TechnicalDesign.md)


