# SDD: Design2UMG 视觉降级修复

> 消除 .pen → UMG 转换中的三类视觉降级：矢量图形丢失、椭圆形状丢失、非文本阴影丢失

---

## 1. 问题定义

| 编号 | 降级 | 根因 | 影响 |
|------|------|------|------|
| C | Ellipse → 空 Border | UMG 无椭圆控件，直接映射为 Border | 圆形按钮、头像等变成空矩形 |
| A | Path → 空 Border | UMG 无 SVG 路径控件，geometry 数据被丢弃 | logo、图标、装饰图形全部消失 |
| B | 非文本 Shadow → 忽略 | UMG 仅 UTextBlock 支持阴影 | 卡片、弹窗等容器无视觉层次 |

实施顺序：C → A → B（按改动量从小到大）

---

## 2. 方案 C：Ellipse 圆角近似

### 2.1 原理

Ellipse 在 UMG 中没有原生控件，但 Border 的 `cornerRadius` 设为 50% 时视觉上等价于椭圆。

- 正圆（width == height）：`cornerRadius = width / 2` → 完美圆形
- 椭圆（width != height）：`cornerRadius = min(width, height) / 2` → 近似椭圆

### 2.2 改动点

**文件：`Design2UMGConverter.cpp:98-103`**

当前代码：
```cpp
case EPenNodeType::Ellipse:
    Widget = CreateContainerWidget(Node);
    UE_LOG(LogDesign2UMG, Warning, TEXT("Design2UMG: Ellipse '%s' mapped to Border (UMG has no ellipse widget)"), *Node->Name);
    if (OutWarnings)
        OutWarnings->Add(FString::Printf(TEXT("Ellipse '%s' → Border (UMG 无椭圆控件)"), *Node->Name));
    break;
```

改为：
```cpp
case EPenNodeType::Ellipse:
    Widget = CreateEllipseWidget(Node);
    break;
```

**新增函数：`Design2UMGConverter.cpp` + `.h`**

```cpp
FUMGWidgetNode FDesign2UMGConverter::CreateEllipseWidget(const TSharedPtr<FPenNode>& Node)
{
    FUMGWidgetNode Widget;
    Widget.Type = TEXT("Border");

    // 用圆角近似椭圆：cornerRadius = min(w, h) / 2
    float W = Node->Width.IsFixed() ? Node->Width.Value : 0.0f;
    float H = Node->Height.IsFixed() ? Node->Height.Value : 0.0f;
    if (W > 0 && H > 0)
    {
        float Radius = FMath::Min(W, H) / 2.0f;
        Node->CornerRadius.bUniform = true;
        Node->CornerRadius.Uniform = Radius;
    }
    return Widget;
}
```

### 2.3 影响范围

- 仅修改 `Design2UMGConverter.cpp`（switch case + 新函数）
- 仅新增 `Design2UMGConverter.h` 声明
- 不影响其他节点类型的转换逻辑
- `ApplyVisualProperties` 中已有的 `ApplyRoundedCorner` 会自动处理圆角渲染

---

## 3. 方案 A：Path 本地栅格化

### 3.1 原理

.pen 文件的 Path 节点携带完整的 SVG path data（`FPenNode::Geometry`，如 `"M0 64L249.38 64..."`）。在导入阶段将 geometry 解析并栅格化为像素数据，生成 `UTexture2D`，然后将节点类型从 Path 改为 Image。

### 3.2 架构决策：自研 vs 外部库

**选择自研**，理由：
- .pen 中的 Path 绝大多数是简单图标/logo，指令集有限（M/L/C/Q/Z/A）
- `ProceduralShapeGenerator` 已有完整的纹理生成基础设施（`CreateTransientTexture` + `FillTextureData`）
- 避免引入外部依赖（lunasvg 需要 CMake 构建、ThirdParty 目录管理）
- 如果后续发现少数复杂 Path 渲染不对，再引入外部库也不迟

### 3.3 新增文件

**`PenPathRasterizer.h`**
```cpp
#pragma once
#include "CoreMinimal.h"
#include "Engine/Texture2D.h"

/**
 * SVG Path Data → UTexture2D 栅格化器
 *
 * 支持的 SVG 指令：M/m, L/l, H/h, V/v, C/c, Q/q, Z/z, A/a
 * 使用 scanline fill 算法，支持 nonzero 和 evenodd 填充规则
 */
class FPenPathRasterizer
{
public:
    /**
     * 将 SVG path data 栅格化为纹理
     * @param Geometry SVG path data 字符串
     * @param Width 目标宽度（像素）
     * @param Height 目标高度（像素）
     * @param FillColor 填充颜色
     * @param FillRule "nonzero" 或 "evenodd"
     * @param Outer 纹理的 Outer 对象
     * @return 生成的纹理，失败返回 nullptr
     */
    static UTexture2D* Rasterize(
        const FString& Geometry,
        int32 Width, int32 Height,
        FLinearColor FillColor,
        const FString& FillRule = TEXT("nonzero"),
        UObject* Outer = nullptr);

private:
    struct FPathPoint { float X; float Y; };

    /** 解析 SVG path data 为多边形点序列 */
    static bool ParsePathData(const FString& Data, TArray<TArray<FPathPoint>>& OutSubPaths);

    /** 三次贝塞尔曲线采样 */
    static void SampleCubicBezier(
        const FPathPoint& P0, const FPathPoint& P1,
        const FPathPoint& P2, const FPathPoint& P3,
        TArray<FPathPoint>& OutPoints, int32 Segments = 16);

    /** 二次贝塞尔曲线采样 */
    static void SampleQuadBezier(
        const FPathPoint& P0, const FPathPoint& P1, const FPathPoint& P2,
        TArray<FPathPoint>& OutPoints, int32 Segments = 12);

    /** 椭圆弧采样 */
    static void SampleArc(
        const FPathPoint& From, float Rx, float Ry, float Rotation,
        bool bLargeArc, bool bSweep, const FPathPoint& To,
        TArray<FPathPoint>& OutPoints, int32 Segments = 24);

    /** Scanline fill：将多边形填充到像素缓冲区 */
    static void ScanlineFill(
        const TArray<TArray<FPathPoint>>& SubPaths,
        TArray<FColor>& OutPixels,
        int32 Width, int32 Height,
        FColor FillColor,
        bool bEvenOdd);
};
```

**`PenPathRasterizer.cpp`** — 实现要点：

1. `ParsePathData`：状态机解析 SVG path data
   - 维护 currentPoint、startPoint（用于 Z 闭合）
   - 大写指令 = 绝对坐标，小写 = 相对坐标
   - C/c 三次贝塞尔 → `SampleCubicBezier` 采样为线段
   - Q/q 二次贝塞尔 → `SampleQuadBezier` 采样为线段
   - A/a 椭圆弧 → `SampleArc` 采样为线段
   - M/m 开始新子路径

2. `ScanlineFill`：逐行扫描
   - 对每一行 Y，计算所有边与该行的交点 X
   - 按 X 排序
   - nonzero：根据边方向累加 winding number，非零则填充
   - evenodd：奇数交点之间填充
   - 边缘抗锯齿：对边界像素做 alpha 混合

3. 坐标变换：
   - .pen 的 geometry 坐标系原点在左上角
   - 需要根据 Path 节点的 viewBox（即 width/height）缩放到目标像素尺寸
   - 缩放因子 = targetWidth / nodeWidth

### 3.4 集成点

**文件：`Design2UMGConverter.cpp:110-115`**

当前代码：
```cpp
case EPenNodeType::Path:
    Widget = CreateContainerWidget(Node);
    UE_LOG(LogDesign2UMG, Warning, ...);
    if (OutWarnings) OutWarnings->Add(...);
    break;
```

改为：
```cpp
case EPenNodeType::Path:
    Widget = CreatePathWidget(Node);
    break;
```

**新增函数：**
```cpp
FUMGWidgetNode FDesign2UMGConverter::CreatePathWidget(const TSharedPtr<FPenNode>& Node)
{
    FUMGWidgetNode Widget;

    // 如果已经被 FigmaImageExporter 转为 Image Fill，走 Image 路径
    if (Node->Fill.Type == EPenFillType::Image && !Node->Fill.Image.Url.IsEmpty())
    {
        Widget.Type = TEXT("Image");
        Widget.Properties.Add(TEXT("TexturePath"), Node->Fill.Image.Url);
        return Widget;
    }

    // 本地栅格化
    if (!Node->Geometry.IsEmpty())
    {
        int32 W = FMath::Max(1, FMath::RoundToInt(Node->Width.Value));
        int32 H = FMath::Max(1, FMath::RoundToInt(Node->Height.Value));

        FLinearColor Color = FLinearColor::White;
        if (Node->Fill.Type == EPenFillType::Color)
            Color = FPenTypeMapper::ParseColor(Node->Fill.SolidColor);

        // 标记为需要栅格化，由 UMGPropertyApplier 在 Widget 创建时执行
        Widget.Type = TEXT("Image");
        Widget.Properties.Add(TEXT("PathGeometry"), Node->Geometry);
        Widget.Properties.Add(TEXT("PathFillRule"), Node->FillRule);
        Widget.Properties.Add(TEXT("PathFillColor"), Node->Fill.SolidColor);
        Widget.Properties.Add(TEXT("WidthOverride"), FString::FromInt(W));
        Widget.Properties.Add(TEXT("HeightOverride"), FString::FromInt(H));
        return Widget;
    }

    // Fallback：无 geometry 数据，降级为 Border
    Widget.Type = TEXT("Border");
    return Widget;
}
```

**文件：`UMGPropertyApplier.cpp`（已有的 Image 处理逻辑中）**

在 Image widget 的属性应用阶段，检测 `PathGeometry` 属性：
```cpp
if (const FString* Geom = Props.Find(TEXT("PathGeometry")))
{
    const FString FillRule = Props.FindRef(TEXT("PathFillRule"));
    const FString ColorStr = Props.FindRef(TEXT("PathFillColor"));
    FLinearColor Color = ColorStr.IsEmpty() ? FLinearColor::White : FUMGWidgetUtils::ParseColor(ColorStr);

    int32 W = FMath::Max(1, FCString::Atoi(*Props.FindRef(TEXT("WidthOverride"))));
    int32 H = FMath::Max(1, FCString::Atoi(*Props.FindRef(TEXT("HeightOverride"))));

    UTexture2D* Tex = FPenPathRasterizer::Rasterize(*Geom, W, H, Color, FillRule, ImageWidget);
    if (Tex)
    {
        ImageWidget->SetBrushFromTexture(Tex);
        return;
    }
}
```

### 3.5 Build.cs

无需修改。`PenPathRasterizer` 是纯 C++ 实现，不依赖外部库。

---

## 4. 方案 B：Box Shadow（9-slice 纹理）

### 4.1 原理

为带 Shadow 效果的非文本节点生成一张带高斯模糊阴影的纹理，用 `UImage` 垫在原 Widget 下方。

阴影纹理只需生成一次，通过 9-slice（Margin）拉伸适配不同尺寸。

### 4.2 新增函数

**文件：`ProceduralShapeGenerator.h` + `.cpp`**

```cpp
/** 生成 box shadow 纹理（带高斯模糊的矩形阴影） */
static UTexture2D* GenerateBoxShadow(
    int32 Width, int32 Height,
    FLinearColor ShadowColor,
    float BlurRadius,
    float SpreadRadius = 0.0f,
    float CornerRadius = 0.0f,
    UObject* Outer = nullptr);
```

实现要点：
1. 纹理尺寸 = (Width + BlurRadius*2 + Spread*2) × (Height + BlurRadius*2 + Spread*2)
2. 先绘制一个圆角矩形（复用 `GenerateRoundedRect` 的逻辑）
3. 对整个纹理做高斯模糊（水平 pass + 垂直 pass，box blur 近似即可）
4. 应用 ShadowColor 的 alpha

### 4.3 集成点

**文件：`PenVisualMapper.cpp:124-137`**

当前代码（Shadow 在非文本节点上被忽略）：
```cpp
case EPenEffectType::Shadow:
    if (Node->Type == EPenNodeType::Text)
    {
        // ... 文本阴影，保持不变
    }
    else
    {
        UE_LOG(LogDesign2UMG, Warning, ...);
    }
    break;
```

改为：
```cpp
case EPenEffectType::Shadow:
    if (Node->Type == EPenNodeType::Text)
    {
        // ... 文本阴影，保持不变
    }
    else
    {
        // 标记需要 box shadow，由 Converter 在包裹阶段处理
        Widget.Properties.Add(TEXT("BoxShadowOffsetX"), FString::SanitizeFloat(Effect.Shadow.OffsetX));
        Widget.Properties.Add(TEXT("BoxShadowOffsetY"), FString::SanitizeFloat(Effect.Shadow.OffsetY));
        Widget.Properties.Add(TEXT("BoxShadowBlur"), FString::SanitizeFloat(Effect.Shadow.Blur));
        Widget.Properties.Add(TEXT("BoxShadowSpread"), FString::SanitizeFloat(Effect.Shadow.Spread));
        Widget.Properties.Add(TEXT("BoxShadowColor"), Effect.Shadow.Color);
    }
    break;
```

**文件：`PenVisualMapper.h` + `.cpp`**

新增包裹函数：
```cpp
static FUMGWidgetNode WrapWithBoxShadow(FUMGWidgetNode&& Widget, const TSharedPtr<FPenNode>& Node);
```

在 `Design2UMGConverter.cpp` 的 `ConvertNode` 中，BackgroundBlur 包裹之前检测 BoxShadow 属性并包裹。

---

## 5. 文件变更清单

| 文件 | 变更类型 | 说明 |
|------|---------|------|
| `PenPathRasterizer.h` | 新增 | SVG path 栅格化器声明 |
| `PenPathRasterizer.cpp` | 新增 | SVG path 解析 + scanline fill 实现 |
| `Design2UMGConverter.cpp` | 修改 | Ellipse/Path switch case + 新增 CreateEllipseWidget/CreatePathWidget |
| `Design2UMGConverter.h` | 修改 | 新增函数声明 |
| `ProceduralShapeGenerator.h` | 修改 | 新增 GenerateBoxShadow 声明 |
| `ProceduralShapeGenerator.cpp` | 修改 | 新增 GenerateBoxShadow 实现 |
| `PenVisualMapper.cpp` | 修改 | Shadow case 改为记录属性而非忽略 |
| `PenVisualMapper.h` | 修改 | 新增 WrapWithBoxShadow 声明 |
| `UMGPropertyApplier.cpp` | 修改 | Image 处理中新增 PathGeometry 检测 |

---

## 6. 验证计划

每个方案完成后：
1. 编译通过（`UnrealBuildTool FPSRoguelikeEditor Win64 Development`）
2. 用 `Design2UMG.Import` 转换 pencil-welcome.pen 的三个画面
3. 渲染 PNG 与设计稿截图对比
4. 重点检查：
   - C：Welcome 页中是否有圆形/椭圆元素正确显示
   - A：Welcome 页的 Pencil logo（`E95mx`, `g9jTd`）是否从空白变为可见图形
   - B：如果设计稿中有带阴影的卡片，检查阴影是否可见

---

## 7. 风险与降级策略

| 风险 | 降级策略 |
|------|---------|
| SVG path 解析器不支持某些指令 | 记录 Warning，该节点仍降级为 Border |
| 复杂 Path 栅格化质量不佳 | 后续引入 lunasvg 替换自研栅格化器 |
| Box Shadow 高斯模糊性能差 | 限制最大 blur radius，超过阈值降级为简单偏移阴影 |
| Ellipse 圆角近似不够精确 | 对于非正圆椭圆，可用 ProceduralShapeGenerator::GenerateCircle 生成纹理 |
