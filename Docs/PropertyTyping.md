# Design2UMG 属性类型化改造方案

## 一、问题是什么

Design2UMG 的转换流水线分两步：

```
.pen JSON → FUMGWidgetNode (中间表示) → UWidget (最终控件)
```

中间表示 `FUMGWidgetNode` 用一个 `TMap<FString, FString>` 传递所有属性：

```cpp
// 写入端 (PenVisualMapper.cpp) — 把强类型数据变成字符串
Widget.Properties.Add("FontSize", "14.5");        // float → string
Widget.Properties.Add("BrushColor", "#FF6B35");   // 颜色 → string
Widget.Properties.Add("CornerRadii", "8.0,8.0,0.0,0.0"); // 四元组 → string
Widget.Properties.Add("DrawAs", "RoundedBox");     // 枚举 → string

// 消费端 (UMGPropertyApplier.cpp) — 再把字符串变回强类型
float Size = FCString::Atof(*Props["FontSize"]);
FLinearColor Color = ParseColor(*Props["BrushColor"]);
FVector4 Radii = ParseVector4(*Props["CornerRadii"]);
```

**问题**：强类型 → 字符串 → 强类型，中间多了一次无意义的序列化/反序列化。

## 二、这造成了什么实际 bug

| Bug | 根因 | 后果 |
|-----|------|------|
| FontSize 截断 | `FString::FromInt((int32)14.5)` → `"14"` | 字号精度丢失 |
| LetterSpacing 丢精度 | `FString::FromInt((int32)0.8)` → `"0"` | 字间距直接归零 |

这两个 bug 已修复（改用 `SanitizeFloat`），但修复方式是"打补丁"而不是"治根因"。
只要 string map 还在，下一个新属性还可能犯同样的错。

## 三、竞品怎么做的

| 项目 | 中间表示 | 属性传递方式 |
|------|---------|------------|
| FigmaToCode | AltNode (TypeScript) | 强类型接口：`fills: Paint[]`, `cornerRadius: number` |
| FairyGUI-unreal | GObject (C++) | 强类型成员：`FVector2D Position`, `float Alpha` |
| DivKit | DivText/DivContainer | 每种控件独立强类型 struct |
| **Design2UMG (当前)** | FUMGWidgetNode | `TMap<FString, FString>` — 全部字符串 |

**没有一个成熟项目用 string map 做主通道。**

## 四、改造方案

### 核心思路

在 `FUMGWidgetNode` 上新增一个 typed struct 字段，和现有 string map 并存。
写入端双写，消费端 typed 优先、string 回退。逐批迁移，最终删除 string 写入。

### 数据结构

```cpp
// 新增：类型化的视觉属性
struct FUMGBrushData
{
    // 数值属性（第一批）
    float FontSize = 0;           // 0 = 未设置
    float LetterSpacing = 0;
    float OutlineCornerRadius = -1; // -1 = 未设置
    FVector4 CornerRadii = FVector4(-1); // -1 = 未设置
    float OutlineThickness = 0;

    // 枚举属性（第二批）
    ESlateBrushDrawType::Type DrawAs = ESlateBrushDrawType::NoDrawType; // NoDrawType = 未设置

    // 颜色属性（第二批）
    FLinearColor BrushColor = FLinearColor::Transparent;
    bool bHasBrushColor = false;
    FLinearColor OutlineColor = FLinearColor::Transparent;
    bool bHasOutlineColor = false;

    // 材质属性（第三批）
    FString MaterialRef;          // 保留 string（枚举值少，不值得单独 enum）
    FString MaterialParams;       // JSON → 后续拆为 FUMGGradientData
    FString TexturePath;          // 路径本身就是 string，不需要类型化
};

// FUMGWidgetNode 新增字段
struct FUMGWidgetNode
{
    // ... 现有字段 ...
    TMap<FString, FString> Properties;  // legacy，逐步废弃
    FUMGBrushData Brush;                // 新增：类型化属性
};
```

### 迁移步骤

```
第一批：数值属性
  FontSize, LetterSpacing, OutlineCornerRadius, CornerRadii, OutlineThickness
  → 写入端双写 Brush.xxx + Properties["xxx"]
  → 消费端改为读 Brush.xxx（有值时），回退读 Properties

第二批：枚举 + 颜色
  DrawAs, BrushColor, OutlineColor
  → 同上双写 + 优先读 typed

第三批：材质参数
  MaterialParams → FUMGGradientData typed struct
  MaterialRef, TexturePath 保留 string（本身就是字符串语义）

最终：删除 legacy
  所有消费端确认只读 typed 后，删除 Properties 中对应 key 的写入
```

### 消费端读取模式

```cpp
// 改造前
if (const FString* V = GET_PROP("OutlineThickness"))
    Brush.OutlineSettings.Width = FCString::Atof(**V);

// 改造后
if (Node.Brush.OutlineThickness > 0)
    Brush.OutlineSettings.Width = Node.Brush.OutlineThickness;
else if (const FString* V = GET_PROP("OutlineThickness"))  // legacy 回退
    Brush.OutlineSettings.Width = FCString::Atof(**V);
```

## 五、不做什么

| 方案 | 为什么不做 |
|------|----------|
| Property Bag 变体类型 | UE5 没有现成的 FInstancedPropertyBag，自造轮子成本高 |
| 每种控件一个 struct | 过度设计，当前控件类型不多 |
| 一次性全量重写 | 风险太大，渐进式更安全 |

## 六、涉及文件

| 文件 | 改动 |
|------|------|
| `UMGSchema.h` | 新增 FUMGBrushData struct + FUMGWidgetNode.Brush 字段 |
| `PenVisualMapper.cpp` | 写入端：双写 Brush.xxx + Properties["xxx"] |
| `Design2UMGConverter.cpp` | 写入端：FontSize/LetterSpacing 双写 |
| `UMGPropertyApplier.cpp` | 消费端：typed 优先读取 |
| `UMGWidgetUtils.cpp` | 消费端：Image 控件 typed 优先读取 |
