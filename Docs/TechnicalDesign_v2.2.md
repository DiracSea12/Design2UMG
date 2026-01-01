# ImageToUMG v2.2 技术方案

**版本**：2.2
**日期**：2025-12-29
**状态**：已定稿

---

## 一、问题诊断

### 1.1 现状痛点

| 问题          | 严重程度       | 根因                           |
| ------------- | -------------- | ------------------------------ |
| 位置/布局不准 | **主要** | AI计算锚点/Offsets不稳定       |
| 元素识别不全  | **主要** | 单次调用承载过重，prompt过复杂 |
| 控件类型错误  | 偶发           | 缺少语义约束                   |

### 1.2 根因分析

当前架构让AI同时承担三项任务：

1. **识别**：从图片识别UI元素
2. **计算**：计算锚点、Offsets等像素级数值
3. **生成**：输出完整JSON结构

HUD对像素精度极敏感（偏2-6px肉眼可见），这三项任务耦合导致每项都不稳定。

### 1.3 核心洞察

> **别让大模型当"测量员"直接量像素、猜层级、写UMG。**

2025年前沿研究（Widget2Code、UI2Code^N、DCGen、OmniParser V2）统一指向：**分阶段处理 + 规则修复 + 多轮迭代**。

---

## 二、架构设计

### 2.1 总体架构

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                        ImageToUMG v2.2 Pipeline                               │
├──────────────────────────────────────────────────────────────────────────────┤
│                                                                               │
│  ┌─────────────────────────────────────────────────────────────────────────┐ │
│  │                      ViewportMapping（坐标归一化层）                     │ │
│  │         SourceImage → 归一化Bounds(0-1) → DesignSize映射                │ │
│  └─────────────────────────────────────────────────────────────────────────┘ │
│                                      ↓                                        │
│  ┌──────────────┐   ┌──────────────┐   ┌──────────────┐   ┌──────────────┐   │
│  │   Parser     │ → │     AI       │ → │    Fixer     │ → │  LayoutCalc  │   │
│  │ (OmniParser) │   │              │   │  (Bounds层)  │   │  (最终投影)  │   │
│  │ ·bbox检测    │   │ ·控件类型    │   │ ·越界修正    │   │ ·Hysteresis  │   │
│  │ ·OCR文本     │   │ ·语义结构    │   │ ·对齐吸附    │   │ ·9宫格锚点   │   │
│  │ ·图标分类    │   │ ·命名/样式   │   │ ·等距分布    │   │ ·Offsets     │   │
│  └──────────────┘   └──────────────┘   └──────────────┘   └──────────────┘   │
│                            ↓                                    ↓             │
│                     ┌──────────────┐                    ┌──────────────┐      │
│                     │ Smart Matcher│                    │  Widget Gen  │      │
│                     │ (跨轮次ID)   │                    │  (UMG生成)   │      │
│                     └──────────────┘                    └──────────────┘      │
│                            ↓                                    ↓             │
│                     ┌──────────────┐                    ┌──────────────┐      │
│                     │ Repair Agent │←───视觉差分反馈────│ 渲染对比闭环 │      │
│                     │  (Patch模式) │                    │   (可选)     │      │
│                     └──────────────┘                    └──────────────┘      │
└──────────────────────────────────────────────────────────────────────────────┘

数据流：Image → Parser → AI(Bounds) → Fixer(Bounds) → LayoutCalc(Slot) → Widget
        └──────────────────────────────────────────────────────────────────────┘
                                 ViewportMapping 负责坐标转换
```

### 2.2 模块职责边界

| 模块                      | 职责       | 输入                      | 输出                          |
| ------------------------- | ---------- | ------------------------- | ----------------------------- |
| **ViewportMapping** | 坐标系统一 | 源图尺寸+设计尺寸         | 归一化映射函数                |
| **Parser**          | 提取硬事实 | 原始图片                  | bbox列表、OCR文本、图标候选   |
| **AI**              | 组织结构   | Parser输出 + 图片         | 控件类型、组件DSL、命名、样式 |
| **Smart Matcher**   | ID稳定化   | 新旧WidgetTree            | 匹配映射 + 稳定ID             |
| **Fixer**           | 规则修复   | AI输出的Bounds            | 修正后的Bounds                |
| **LayoutCalc**      | 比例→像素 | 修正后Bounds + 映射上下文 | Anchors、Offsets              |
| **渲染闭环**        | 质量评估   | 生成的Widget              | 结构差异 + 像素差分           |

### 2.3 核心设计原则

1. **AI只做识别，不做计算** - 锚点/Offsets交给程序
2. **Bounds是唯一真值** - Fixer/Patch均在Bounds层操作，LayoutCalc只做最终投影
3. **Fixer是一等公民** - 规则修复比AI微调更稳定
4. **本地优先** - Parser/Fixer本地化，云依赖降到最低
5. **每层独立可评测** - 出问题容易定位
6. **可选模块优雅退化** - Parser/渲染闭环不可用时不阻塞主流程

### 2.4 容器策略

> **AI输出规则**：只允许CanvasPanel绝对布局
>
> - AI输出的RootWidget必须是CanvasPanel
> - AI输出的所有容器Type只能是CanvasPanel
> - **不允许**AI输出HBox/VBox/Grid等容器
>
> **DSL内部规则**：允许响应式容器
>
> - Component DSL展开后的内部可使用HBox/VBox/Overlay
> - 内部容器的外层必须包装在CanvasPanelSlot中
> - 示例：IconText展开为CanvasPanelSlot包裹的HBox

**理由**：分离AI输出限制和程序内部实现。AI只输出简单的绝对布局结构，复杂的组合容器由DSL编译器在程序端处理。这样既保证AI输出的稳定性，又不限制DSL的表达能力。

### 2.5 中间产物与数据流

**目标**：明确各阶段的数据结构，便于调试和问题定位。

**数据结构定义**：

| 阶段                     | 数据结构               | 说明                                  |
| ------------------------ | ---------------------- | ------------------------------------- |
| **Parser输出**     | `FParsedElementList` | 扁平元素表（bbox/OCR文本/置信度）     |
| **AI输出**         | `FWidgetTree`        | 树结构（含Bounds/Type/Properties）    |
| **Fixer输出**      | `FWidgetTree`        | 修正后的树结构（Bounds已修正）        |
| **LayoutCalc输出** | `FUMGSlotDataMap`    | 最终投影（每个节点的Anchors/Offsets） |

```cpp
// Parser阶段：扁平元素表
struct FParsedElement
{
    FString ElementId;           // 临时ID
    FBox2D BBox;                 // 像素级bbox（图像坐标系）
    FString DetectedType;        // 检测到的类型（icon/text/button等）
    FString OCRText;             // OCR识别文本（可选）
    float Confidence;            // 检测置信度
};

struct FParsedElementList
{
    TArray<FParsedElement> Elements;
    FIntPoint SourceImageSize;   // 源图尺寸（用于归一化）
};

// AI输出 & Fixer处理：树结构
struct FWidgetTreeNode
{
    FString Id;                  // 稳定ID（路径式）
    FString Type;                // Widget类型
    FString Name;                // 控件名称
    FBounds Bounds;              // 归一化坐标（唯一真值）
    TMap<FString, FString> Properties;
    TArray<FWidgetTreeNode> Children;
    FMetaInfo Meta;              // confidence/source/uncertain_props
};

struct FWidgetTree
{
    FString WidgetName;          // Blueprint名称
    FWidgetTreeNode RootWidget;  // 根节点（CanvasPanel）
};

// LayoutCalc输出：最终投影
struct FUMGSlotDataMap
{
    TMap<FString, FUMGSlotData> NodeSlots;  // Id → SlotData映射
};
```

**数据流**：

```
[Image] → Parser → FParsedElementList
                          ↓
            [Image + Elements] → AI → FWidgetTree (原始)
                                          ↓
                                       Fixer → FWidgetTree (修正后)
                                                    ↓
                                              Smart Matcher → ID稳定化
                                                    ↓
                                              LayoutCalc → FUMGSlotDataMap
                                                    ↓
                                              Widget Generator → UMG资产
```

> **关键原则**：Fixer/Repair Agent/Patch均在FWidgetTree层操作Bounds，LayoutCalc是最后一步投影，修复后需重新计算SlotData。

---

## 三、核心模块设计

### 3.1 Prompt设计

**目标**：~50行，只做识别和分类，不做像素计算

**核心指令**：

```
1. 列出所有可见元素，尽量不要省略
2. 禁止用注释代替实际元素（如"<!-- 其他项目 -->"）
3. 不确定的元素使用低confidence标记，由后续流程处理
4. 使用Bounds百分比格式（0-1归一化）
5. Bounds精度说明：
   - 有Parser结果时：直接使用Parser的bbox，AI只确认类型和语义
   - 无Parser时：2位小数即可（0.01精度），Fixer会修正对齐
   - 精确定位由Parser + Fixer负责，AI聚焦语义识别

> **无Parser时的质量边界（验收标准分级）**：
>
> | 模式 | 布局MAE目标 | 布局P95目标 | 说明 |
> |------|-------------|-------------|------|
> | **有Parser** | ≤ 4px | ≤ 8px | 完整精度，生产级 |
> | **无Parser** | ≤ 20px | ≤ 40px | 降级精度，原型级 |
>
> **现实约束**：0.01精度在1080p下约10-19px误差，Fixer只能修对齐/等距，无法凭空造出精确bbox。无Parser时用户应预期手动微调。
```

> **设计原则**：AI擅长识别"这是血条"，不擅长判断"血条左边界是0.0234"。
> 将精确测量职责交给Parser（像素级bbox）和Fixer（规则修正），AI只做语义关联。

**JSON输出格式**：

```json
{
  "WidgetName": "WBP_GameHUD",
  "RootWidget": {
    "Type": "CanvasPanel",
    "Name": "Canvas_Root",
    "Children": [
      {
        "Type": "ProgressBar",
        "Id": "Canvas_Root/ProgressBar_0",
        "Name": "Progress_HealthBar",
        "Bounds": {"X": 0.0200, "Y": 0.0200, "W": 0.2000, "H": 0.0300},
        "Properties": {"FillColor": "#FF0000", "Percent": 0.8},
        "_meta": {"confidence": 0.95, "source": "ai"}
      }
    ]
  },
  "ImageAssets": []
}
```

**Bounds坐标约定**：

- X, Y: 元素左上角位置（0-1，相对于**源图尺寸**）
- W, H: 元素宽高（0-1，相对于**源图尺寸**）
- 精度：4位小数（0.0001精度，支持4K分辨率1px精度）
- 程序端通过ViewportMapping和LayoutCalculator转换为Anchors/Offsets

---

### 3.2 ViewportMapping（视口映射）

**目标**：解决不同纵横比/分辨率下的坐标映射问题

**问题场景**：

- 用户截图可能是2560x1440，设计目标是1920x1080
- 可能存在黑边（Letterbox）或裁切（Fill模式）
- 直接 `Bounds * DesignSize`会导致UI整体压扁或偏移

**接口设计**：

```cpp
enum class EAspectRatioMode : uint8
{
    Fit,     // Letterbox模式，保持比例，出现黑边
    Fill,    // 裁切模式，填充屏幕，裁切超出部分
    Stretch  // 拉伸模式，可能变形
};

struct FViewportSpec
{
    FIntPoint SourceSize;   // 截图尺寸（AI输入图）
    FIntPoint DesignSize;   // UMG目标尺寸
    EAspectRatioMode Mode;  // 纵横比处理模式
    FMargin SafeZone;       // 安全区边距（可选）
};
```

#### 3.2.1 输入来源定义

**FViewportSpec字段来源**：

| 字段           | 来源         | 说明                                                      |
| -------------- | ------------ | --------------------------------------------------------- |
| `SourceSize` | AI输入图尺寸 | 发送给AI的图像尺寸（可能经过缩放，如MaxAIDimension=2048） |
| `DesignSize` | 用户配置     | UMG目标分辨率（默认1920x1080，可在设置中修改）            |
| `Mode`       | 用户配置     | 默认Stretch模式（最简单），可手动选择Fit/Fill             |
| `SafeZone`   | 用户配置     | 可选，针对电视安全区等场景                                |

**坐标还原流程**：

```
原图(2560x1440) → 缩放(1024x576) → AI分析 → Bounds(相对1024x576)
                                              ↓
                  ViewportMapping还原 ← CoordScale = 2560/1024
                                              ↓
                                    Bounds(相对2560x1440) → DesignSize映射
```

> **关键**：现有代码中 `ImageAssetProcessor`的 `CoordScale`参数即对应此流程。
> AI返回的Bounds基于缩放后的图像，需乘以CoordScale还原到原始尺寸。

**模式选择建议**：

- 截图与目标分辨率比例相同 → **Stretch**（默认，最简单）
- 截图有明显黑边（如21:9截图在16:9目标上）→ **Fit**
- 截图需裁切适配（如16:9截图在4:3目标上）→ **Fill**

```cpp
class FViewportMapping
{
public:
    // 将AI输出的归一化坐标(0-1, 相对截图)映射为逻辑画布归一化坐标(0-1, 相对Canvas)
    static FBounds MapNormalizedBounds(const FBounds& SourceBounds, const FViewportSpec& Spec);
  
    // 将逻辑画布归一化坐标转换为像素Offsets
    static FVector2D NormalizedToPixel(const FVector2D& NormCoord, const FIntPoint& DesignSize);

private:
    static void CalculateFitTransform(const FViewportSpec& Spec, 
                                       FVector2D& OutOffset, float& OutScale);
};
```

**Fit模式映射实现**：

```cpp
FBounds FViewportMapping::MapNormalizedBounds(const FBounds& SourceBounds, const FViewportSpec& Spec)
{
    if (Spec.Mode == EAspectRatioMode::Fit)
    {
        float SourceAspect = (float)Spec.SourceSize.X / Spec.SourceSize.Y;
        float DesignAspect = (float)Spec.DesignSize.X / Spec.DesignSize.Y;
    
        FBounds Result = SourceBounds;
    
        if (SourceAspect > DesignAspect)
        {
            // 源图更宽，上下有黑边
            float Scale = DesignAspect / SourceAspect;
            float OffsetY = (1.0f - Scale) / 2.0f;
        
            Result.Y = (SourceBounds.Y - OffsetY) / Scale;
            Result.H = SourceBounds.H / Scale;
        }
        else
        {
            // 源图更高，左右有黑边
            float Scale = SourceAspect / DesignAspect;
            float OffsetX = (1.0f - Scale) / 2.0f;
        
            Result.X = (SourceBounds.X - OffsetX) / Scale;
            Result.W = SourceBounds.W / Scale;
        }
    
        return Result;
    }
    else if (Spec.Mode == EAspectRatioMode::Fill)
    {
        float SourceAspect = (float)Spec.SourceSize.X / Spec.SourceSize.Y;
        float DesignAspect = (float)Spec.DesignSize.X / Spec.DesignSize.Y;
    
        FBounds Result = SourceBounds;
    
        if (SourceAspect > DesignAspect)
        {
            // 源图更宽，左右裁切
            float Scale = SourceAspect / DesignAspect;
            float CropX = (1.0f - 1.0f / Scale) / 2.0f;
        
            // 将坐标从裁切后的可见区域映射回完整画布
            Result.X = (SourceBounds.X - CropX) / (1.0f - 2 * CropX);
            Result.W = SourceBounds.W / (1.0f - 2 * CropX);
        }
        else
        {
            // 源图更高，上下裁切
            float Scale = DesignAspect / SourceAspect;
            float CropY = (1.0f - 1.0f / Scale) / 2.0f;
        
            Result.Y = (SourceBounds.Y - CropY) / (1.0f - 2 * CropY);
            Result.H = SourceBounds.H / (1.0f - 2 * CropY);
        }
    
        return Result;
    }
  
    // Stretch模式直接返回
    return SourceBounds;
}
```

> **风险修复: Fill模式单元测试覆盖**

**测试要求**：Fill模式涉及裁切映射，公式复杂易出错，必须增加单元测试覆盖以下场景：

1. **中心元素保持**: 源图与目标纵横比不同时，中心元素应保持中心位置
2. **边缘裁切验证**: 左右/上下裁切时，裁切区外元素坐标应为负值
3. **裁切边界计算**: 验证CropX/CropY计算公式的正确性
4. **纵横比相同**: Fill模式在纵横比相同时应等同于Stretch模式

> **重要**：所有后续模块（Fixer、LayoutCalc）均使用映射后的Bounds，确保坐标系一致性。

---

### 3.3 LayoutCalculator（布局计算器）

**功能**：将Bounds百分比转换为UMG的Anchors/Offsets

**核心特性**：

1. Hysteresis（迟滞）算法，防止锚点在阈值边缘反复跳变
2. **边缘吸附**：贴边元素自动锚定到边缘
3. **拉伸锚点**：宽元素使用Min/Max不同的锚点实现自适应拉伸

**接口设计**：

```cpp
// ==== 风险修复: Hysteresis参数配置化 ====
// 支持HUD预设模板，避免硬编码阈值
struct FLayoutPreset
{
    FString PresetName;          // 预设名称: "HUDStandard" / "HUDCompetitive" / "Custom"
    float AnchorThresholdL;      // 9宫格左阈值（默认0.33）
    float AnchorThresholdR;      // 9宫格右阈值（默认0.67）
    float HysteresisBuffer;      // Hysteresis缓冲区（默认0.05）
    float EdgeSnapThreshold;     // 边缘吸附阈值（默认0.05）
    float StretchThreshold;      // 拉伸锚点阈值（默认0.60）
  
    // HUD关键区域锚点覆盖（可选）
    TMap<FString, FVector2D> RegionAnchorOverrides;
    // 例: "TopLeftCorner" -> FVector2D(0.0, 0.0) 强制锚定左上角
    //     "BottomRightCorner" -> FVector2D(1.0, 1.0) 强制锚定右下角
  
    static FLayoutPreset GetStandardHUDPreset() 
    {
        FLayoutPreset Preset;
        Preset.PresetName = TEXT("HUDStandard");
        Preset.AnchorThresholdL = 0.33f;
        Preset.AnchorThresholdR = 0.67f;
        Preset.HysteresisBuffer = 0.05f;
        Preset.EdgeSnapThreshold = 0.05f;
        Preset.StretchThreshold = 0.60f;
    
        // 标准HUD预设：关键区域强制锚定
        Preset.RegionAnchorOverrides.Add(TEXT("TopLeftCorner"), FVector2D(0.0f, 0.0f));
        Preset.RegionAnchorOverrides.Add(TEXT("TopRightCorner"), FVector2D(1.0f, 0.0f));
        Preset.RegionAnchorOverrides.Add(TEXT("BottomLeftCorner"), FVector2D(0.0f, 1.0f));
        Preset.RegionAnchorOverrides.Add(TEXT("BottomRightCorner"), FVector2D(1.0f, 1.0f));
    
        return Preset;
    }
};

struct FLayoutContext
{
    FIntPoint DesignSize;        // 设计分辨率（UMG目标）
    TOptional<float> OldAnchorX; // 上一轮的锚点X（用于Hysteresis）
    TOptional<float> OldAnchorY; // 上一轮的锚点Y（用于Hysteresis）
    FLayoutPreset Preset;        // 布局预设（默认HUDStandard）
};

class FLayoutCalculator
{
public:
    static FUMGSlotData CalculateSlot(
        const FBounds& Bounds,
        const FLayoutContext& Context
    );

private:
    static float SelectAnchorWithHysteresis(
        float Center, 
        TOptional<float> OldAnchor,
        const FLayoutPreset& Preset  // 使用预设参数
    );
  
    // 边缘吸附：检测是否应锚定边缘
    static bool ShouldSnapToEdge(float Position, float Size, bool& bSnapToStart, bool& bSnapToEnd);
  
    // 拉伸锚点：检测是否应使用拉伸模式
    static bool ShouldUseStretchAnchors(float Size);
};
```

**9宫格锚点选择算法（带Hysteresis）**：

```cpp
float FLayoutCalculator::SelectAnchorWithHysteresis(float Center, TOptional<float> OldAnchor, const FLayoutPreset& Preset)
{
    // 使用预设参数，而非硬编码
    const float Threshold_L = Preset.AnchorThresholdL;
    const float Threshold_R = Preset.AnchorThresholdR;
    const float Buffer = Preset.HysteresisBuffer;
  
    // 如果没有历史锚点，使用标准选择
    if (!OldAnchor.IsSet())
    {
        if (Center < Threshold_L) return 0.0f;
        else if (Center < Threshold_R) return 0.5f;
        else return 1.0f;
    }
  
    float Old = OldAnchor.GetValue();
  
    // Hysteresis逻辑：只有显著超过阈值+缓冲区才切换
    if (Old == 0.0f)
    {
        return (Center > Threshold_L + Buffer) ? 0.5f : 0.0f;
    }
    else if (Old == 0.5f)
    {
        if (Center < Threshold_L - Buffer) return 0.0f;
        else if (Center > Threshold_R + Buffer) return 1.0f;
        else return 0.5f;
    }
    else // Old == 1.0f
    {
        return (Center < Threshold_R - Buffer) ? 0.5f : 1.0f;
    }
}
```

> **Hysteresis收益**：元素中心在0.33附近微调时，锚点不会在0.0和0.5之间跳变，Offsets保持稳定，Patch有效性大幅提升。

**边缘吸附检测**：

```cpp
// 使用Context中的预设参数
bool FLayoutCalculator::ShouldSnapToEdge(float Position, float Size, float EdgeThreshold, bool& bSnapToStart, bool& bSnapToEnd)
{
    bSnapToStart = (Position < EdgeThreshold);  // 距左/上边缘 < 阈值
    bSnapToEnd = ((1.0f - Position - Size) < EdgeThreshold);  // 距右/下边缘 < 阈值
    return bSnapToStart || bSnapToEnd;
}

bool FLayoutCalculator::ShouldUseStretchAnchors(float Size, float StretchThreshold)
{
    return Size > StretchThreshold;  // 宽度 > 阈值则考虑拉伸
}
```

**Offsets计算（含边缘吸附与拉伸）**：

```cpp
FUMGSlotData FLayoutCalculator::CalculateSlot(const FBounds& Bounds, const FLayoutContext& Context)
{
    int32 CanvasW = Context.DesignSize.X;
    int32 CanvasH = Context.DesignSize.Y;
  
    FUMGSlotData Slot;
  
    // === 水平方向处理 ===
    bool bSnapLeftX, bSnapRightX;
    bool bEdgeX = ShouldSnapToEdge(Bounds.X, Bounds.W, bSnapLeftX, bSnapRightX);
    bool bStretchX = ShouldUseStretchAnchors(Bounds.W);
  
    if (bStretchX && bSnapLeftX && bSnapRightX)
    {
        // 拉伸模式：两端都锚定边缘，宽度自适应
        Slot.Anchors.Min.X = 0.0f;
        Slot.Anchors.Max.X = 1.0f;
        Slot.Alignment.X = 0.0f;
        Slot.Offsets.Left = Bounds.X * CanvasW;  // 左边距
        Slot.Offsets.Right = (1.0f - Bounds.X - Bounds.W) * CanvasW;  // 右边距（注意：拉伸模式下Right是右边距）
    }
    else if (bSnapLeftX)
    {
        // 锚定左边缘
        Slot.Anchors.Min.X = Slot.Anchors.Max.X = 0.0f;
        Slot.Alignment.X = 0.0f;
        Slot.Offsets.Left = Bounds.X * CanvasW;
        Slot.Offsets.Right = Bounds.W * CanvasW;
    }
    else if (bSnapRightX)
    {
        // 锚定右边缘
        Slot.Anchors.Min.X = Slot.Anchors.Max.X = 1.0f;
        Slot.Alignment.X = 1.0f;
        Slot.Offsets.Left = -((1.0f - Bounds.X - Bounds.W) * CanvasW);  // 负值表示从右边缘内缩
        Slot.Offsets.Right = Bounds.W * CanvasW;
    }
    else
    {
        // 标准9宫格模式 + Hysteresis
        float CenterX = Bounds.X + Bounds.W / 2.0f;
        float AnchorX = SelectAnchorWithHysteresis(CenterX, Context.OldAnchorX);
    
        Slot.Anchors.Min.X = Slot.Anchors.Max.X = AnchorX;
        Slot.Alignment.X = AnchorX;
    
        float AnchorPixelX = AnchorX * CanvasW;
        float AlignPointPixelX = (Bounds.X + AnchorX * Bounds.W) * CanvasW;
        Slot.Offsets.Left = AlignPointPixelX - AnchorPixelX;
        Slot.Offsets.Right = Bounds.W * CanvasW;
    }
  
    // === 垂直方向处理（同理） ===
    bool bSnapTopY, bSnapBottomY;
    bool bEdgeY = ShouldSnapToEdge(Bounds.Y, Bounds.H, bSnapTopY, bSnapBottomY);
    bool bStretchY = ShouldUseStretchAnchors(Bounds.H);
  
    if (bStretchY && bSnapTopY && bSnapBottomY)
    {
        Slot.Anchors.Min.Y = 0.0f;
        Slot.Anchors.Max.Y = 1.0f;
        Slot.Alignment.Y = 0.0f;
        Slot.Offsets.Top = Bounds.Y * CanvasH;
        Slot.Offsets.Bottom = (1.0f - Bounds.Y - Bounds.H) * CanvasH;
    }
    else if (bSnapTopY)
    {
        Slot.Anchors.Min.Y = Slot.Anchors.Max.Y = 0.0f;
        Slot.Alignment.Y = 0.0f;
        Slot.Offsets.Top = Bounds.Y * CanvasH;
        Slot.Offsets.Bottom = Bounds.H * CanvasH;
    }
    else if (bSnapBottomY)
    {
        Slot.Anchors.Min.Y = Slot.Anchors.Max.Y = 1.0f;
        Slot.Alignment.Y = 1.0f;
        Slot.Offsets.Top = -((1.0f - Bounds.Y - Bounds.H) * CanvasH);
        Slot.Offsets.Bottom = Bounds.H * CanvasH;
    }
    else
    {
        float CenterY = Bounds.Y + Bounds.H / 2.0f;
        float AnchorY = SelectAnchorWithHysteresis(CenterY, Context.OldAnchorY);
    
        Slot.Anchors.Min.Y = Slot.Anchors.Max.Y = AnchorY;
        Slot.Alignment.Y = AnchorY;
    
        float AnchorPixelY = AnchorY * CanvasH;
        float AlignPointPixelY = (Bounds.Y + AnchorY * Bounds.H) * CanvasH;
        Slot.Offsets.Top = AlignPointPixelY - AnchorPixelY;
        Slot.Offsets.Bottom = Bounds.H * CanvasH;
    }

    return Slot;
}
```

> **边缘吸附收益**：贴边HUD元素（如顶部血条、底部弹药栏）在分辨率变化时自动保持边距，而非相对中心偏移。
>
> **拉伸锚点收益**：全屏遮罩、宽幅进度条等元素自动拉伸填充，适配不同屏幕宽度。

> **风险修复：LayoutCalc一致性验证**
>
> UMG CanvasSlot的Offsets语义在拉伸模式下容易出错（Right/Bottom是边距而非尺寸）。
>
> **必须增加的验证步骤**：
> 1. 准备10-20个已知正确的UMG样例（手动搭建，覆盖各种锚点模式）
> 2. 反推：从UMG资产读取Slot数据 → 计算对应的Bounds
> 3. 再生成：用Bounds → LayoutCalc → 生成Slot
> 4. 验证：生成的Slot与原始Slot完全一致
>
> 如不一致，说明公式或语义理解有误，必须修正后才能继续。

---

### 3.4 Fixer（规则修复器）

**功能**：纯规则层自动修复，不依赖AI

**执行位置**：在LayoutCalc之前执行，直接修改Bounds（Bounds是唯一真值）

#### 3.4.1 对齐吸附

**动态阈值公式**：

```cpp
// 成员变量
int32 SnapPx;  // 动态吸附阈值（像素）

// 初始化
void FFixer::Initialize(const FIntPoint& DesignSize)
{
    int32 MinDim = FMath::Min(DesignSize.X, DesignSize.Y);
    SnapPx = FMath::Clamp(FMath::RoundToInt(MinDim * 0.002f), 2, 6);
    // 1080p: 2px, 4K: 4px
}
```

**吸附目标**：

- 父容器边缘（Left=0, Right=0, Top=0, Bottom=0）
- 中线（水平50%、垂直50%）
- 同级元素边缘对齐

#### 3.4.2 越界修正

```cpp
void FFixer::FixBoundsOverflow(FUMGWidgetNode& Child, const FUMGWidgetNode& Parent)
{
    const float ParentLeft = Parent.Bounds.X;
    const float ParentTop = Parent.Bounds.Y;
    const float ParentRight = Parent.Bounds.X + Parent.Bounds.W;
    const float ParentBottom = Parent.Bounds.Y + Parent.Bounds.H;

    Child.Bounds.X = FMath::Max(Child.Bounds.X, ParentLeft);
    Child.Bounds.Y = FMath::Max(Child.Bounds.Y, ParentTop);

    const float ChildRight = Child.Bounds.X + Child.Bounds.W;
    const float ChildBottom = Child.Bounds.Y + Child.Bounds.H;

    if (ChildRight > ParentRight)
    {
        Child.Bounds.W = FMath::Max(0.0f, ParentRight - Child.Bounds.X);
    }

    if (ChildBottom > ParentBottom)
    {
        Child.Bounds.H = FMath::Max(0.0f, ParentBottom - Child.Bounds.Y);
    }
}
```

#### 3.4.3 等距检测与修正（带语义分组）

> **重要**：等距修正前必须先进行行/列分组，**拒绝全局等距**，避免把有意的视觉节奏拉平。

**步骤1：行/列检测**

```cpp
// 检测同一行的元素（Y轴重叠度>80%视为同一行）
TArray<TArray<FUMGWidgetNode*>> FFixer::DetectRows(TArray<FUMGWidgetNode>& Siblings)
{
    const float OverlapThreshold = 0.8f;
    TArray<TArray<FUMGWidgetNode*>> Rows;
  
    for (auto& Node : Siblings)
    {
        bool bFoundRow = false;
    
        for (auto& Row : Rows)
        {
            if (Row.Num() == 0) continue;
        
            FUMGWidgetNode* First = Row[0];
        
            float OverlapTop = FMath::Max(Node.Bounds.Y, First->Bounds.Y);
            float OverlapBottom = FMath::Min(
                Node.Bounds.Y + Node.Bounds.H, 
                First->Bounds.Y + First->Bounds.H
            );
            float OverlapHeight = FMath::Max(0.0f, OverlapBottom - OverlapTop);
            float MinHeight = FMath::Min(Node.Bounds.H, First->Bounds.H);
        
            if (MinHeight > 0 && OverlapHeight / MinHeight > OverlapThreshold)
            {
                Row.Add(&Node);
                bFoundRow = true;
                break;
            }
        }
    
        if (!bFoundRow)
        {
            TArray<FUMGWidgetNode*> NewRow;
            NewRow.Add(&Node);
            Rows.Add(NewRow);
        }
    }
  
    return Rows;
}
```

**辅助函数**：

```cpp
float FFixer::GetAverage(const TArray<float>& Values)
{
    if (Values.Num() == 0) return 0.0f;
    float Sum = 0.0f;
    for (float V : Values) Sum += V;
    return Sum / Values.Num();
}

float FFixer::GetMaxDeviation(const TArray<float>& Values, float Average)
{
    float MaxDiff = 0.0f;
    for (float V : Values)
    {
        MaxDiff = FMath::Max(MaxDiff, FMath::Abs(V - Average));
    }
    return MaxDiff;
}
```

**步骤2：组内等距修正**

```cpp
void FFixer::FixEqualSpacingWithGrouping(TArray<FUMGWidgetNode>& Siblings, int32 DesignWidth)
{
    float EqualSpacingThreshold = (float)SnapPx / (float)DesignWidth;
  
    TArray<TArray<FUMGWidgetNode*>> Rows = DetectRows(Siblings);
  
    for (auto& Row : Rows)
    {
        if (Row.Num() < 3) continue;
    
        Row.Sort([](const FUMGWidgetNode* A, const FUMGWidgetNode* B)
        {
            return A->Bounds.X < B->Bounds.X;
        });
    
        TArray<float> Gaps;
        for (int i = 1; i < Row.Num(); i++)
        {
            float Gap = Row[i]->Bounds.X - (Row[i-1]->Bounds.X + Row[i-1]->Bounds.W);
            Gaps.Add(Gap);
        }
    
        float AvgGap = GetAverage(Gaps);
        float MaxDiff = GetMaxDeviation(Gaps, AvgGap);
    
        if (MaxDiff > EqualSpacingThreshold)
        {
            for (int i = 1; i < Row.Num(); i++)
            {
                float PrevRight = Row[i-1]->Bounds.X + Row[i-1]->Bounds.W;
                Row[i]->Bounds.X = PrevRight + AvgGap;
            }
        }
    }
}
```

#### 3.4.4 Fixer防护机制

> **目标**：防止过修复破坏设计意图

**置信度门槛**：

```cpp
// 低置信度元素跳过修正
void FFixer::FixNode(FUMGWidgetNode& Node)
{
    // 置信度低于0.8的元素跳过修正，避免破坏不确定的识别结果
    if (Node.Meta.Confidence < 0.8f)
    {
        UE_LOG(LogFixer, Log, TEXT("Skip fixing low confidence node: %s (%.2f)"),
               *Node.Id, Node.Meta.Confidence);
        return;
    }

    // 执行正常修正流程
    FixBoundsOverflow(Node, ParentNode);
    SnapToAlignment(Node);
    // ...
}
```

**修正记录**（支持调试和回溯）：

```cpp
struct FFixerLog
{
    FString NodeId;
    FString FixType;      // "snap" / "overflow" / "equal_spacing"
    FBounds Before;
    FBounds After;
    float DeltaPixels;    // 修正量（像素）
    FDateTime Timestamp;
};

// 每次修正记录到日志
TArray<FFixerLog> FixerHistory;
```

**可选增强**：

- `_meta.fix_exempt`: AI可标记禁止修正的元素（如故意不对齐的装饰元素）
- 回滚机制：保存修正前快照，支持单步撤销
- 修正量限制：单次修正超过阈值（如20px）时警告

---

### 3.5 Smart Matcher（ID稳定器）

**目标**：解决AI识别抖动导致的ID漂移，确保跨轮次Patch有效

**问题场景**：

- 两个元素水平位置接近，微小偏移导致排序交换
- 识别漏掉一个元素，后面全部siblingIndex变化
- Patch依赖稳定ID定位，ID变化导致Patch失效

**算法**：加权二分图匹配（Weighted Bipartite Matching）

保留上一轮生成的WidgetTree (`Tree_Old`)，与新一轮 (`Tree_New`) 进行匹配。

**匹配分值计算**：

```cpp
float FSmartMatcher::CalculateMatchScore(const FUMGWidgetNode& NewNode, const FUMGWidgetNode& OldNode)
{
    float Score = 0.0f;
  
    // 类型匹配（权重最高之一）
    if (NewNode.Type == OldNode.Type)
    {
        Score += 100.0f;
    }
  
    // 语义名称匹配
    if (NewNode.Name == OldNode.Name)
    {
        Score += 50.0f;
    }
  
    // 位置重叠度（IoU，权重最高）
    float IoU = CalculateBoundsIoU(NewNode.Bounds, OldNode.Bounds);
    Score += IoU * 200.0f;
  
    // 文本相似度（针对TextBlock）
    if (NewNode.Type == "TextBlock" && OldNode.Type == "TextBlock")
    {
        float TextSim = CalculateTextSimilarity(NewNode.Text, OldNode.Text);
        Score += TextSim * 50.0f;
    }
  
    return Score;
}

float FSmartMatcher::CalculateBoundsIoU(const FBounds& A, const FBounds& B)
{
    float InterLeft = FMath::Max(A.X, B.X);
    float InterTop = FMath::Max(A.Y, B.Y);
    float InterRight = FMath::Min(A.X + A.W, B.X + B.W);
    float InterBottom = FMath::Min(A.Y + A.H, B.Y + B.H);
  
    float InterW = FMath::Max(0.0f, InterRight - InterLeft);
    float InterH = FMath::Max(0.0f, InterBottom - InterTop);
    float InterArea = InterW * InterH;
  
    float UnionArea = A.W * A.H + B.W * B.H - InterArea;
  
    return UnionArea > 0 ? InterArea / UnionArea : 0.0f;
}

float FSmartMatcher::CalculateTextSimilarity(const FString& TextA, const FString& TextB)
{
    // Levenshtein距离归一化
    if (TextA.IsEmpty() && TextB.IsEmpty()) return 1.0f;
    if (TextA.IsEmpty() || TextB.IsEmpty()) return 0.0f;
  
    int32 LenA = TextA.Len();
    int32 LenB = TextB.Len();
  
    // 简化实现：字符匹配比例
    int32 Matches = 0;
    int32 MinLen = FMath::Min(LenA, LenB);
  
    for (int32 i = 0; i < MinLen; i++)
    {
        if (TextA[i] == TextB[i]) Matches++;
    }
  
    return (float)Matches / (float)FMath::Max(LenA, LenB);
}
```

**匹配策略**：

```cpp
void FSmartMatcher::MatchAndAssignIds(
    TArray<FUMGWidgetNode>& NewNodes, 
    const TArray<FUMGWidgetNode>& OldNodes,
    float MatchThreshold = 150.0f)
{
    // 1. 构建得分矩阵
    TArray<TArray<float>> ScoreMatrix;
    for (const auto& NewNode : NewNodes)
    {
        TArray<float> Scores;
        for (const auto& OldNode : OldNodes)
        {
            Scores.Add(CalculateMatchScore(NewNode, OldNode));
        }
        ScoreMatrix.Add(Scores);
    }
  
    // 2. 贪心匹配（可升级为Hungarian算法）
    TSet<int32> UsedOldIndices;
  
    for (int32 NewIdx = 0; NewIdx < NewNodes.Num(); NewIdx++)
    {
        float BestScore = 0.0f;
        int32 BestOldIdx = -1;
    
        for (int32 OldIdx = 0; OldIdx < OldNodes.Num(); OldIdx++)
        {
            if (UsedOldIndices.Contains(OldIdx)) continue;
        
            if (ScoreMatrix[NewIdx][OldIdx] > BestScore)
            {
                BestScore = ScoreMatrix[NewIdx][OldIdx];
                BestOldIdx = OldIdx;
            }
        }
    
        // 3. 分配ID
        if (BestScore >= MatchThreshold && BestOldIdx >= 0)
        {
            // 匹配成功：沿用旧ID
            NewNodes[NewIdx].Id = OldNodes[BestOldIdx].Id;
            UsedOldIndices.Add(BestOldIdx);
        }
        else
        {
            // 匹配失败：生成新ID
            NewNodes[NewIdx].Id = GenerateNewId(NewNodes[NewIdx]);
        }
    }
}

FString FSmartMatcher::GenerateNewId(const FUMGWidgetNode& Node)
{
    // 基于父路径 + 类型 + 同类型计数生成新ID
    FString ParentPath = Node.ParentId.IsEmpty() ? TEXT("") : Node.ParentId + TEXT("/");
  
    // 查找已存在的同类型同级元素数量
    int32 SiblingIndex = 0;
    for (const auto& Sibling : CurrentTree.GetSiblings(Node))
    {
        if (Sibling.Type == Node.Type)
        {
            SiblingIndex++;
        }
    }
  
    return FString::Printf(TEXT("%s%s_%d"), *ParentPath, *Node.Type, SiblingIndex);
}
```

#### 3.5.1 复杂度优化策略

> **风险**：当前贪心匹配时间复杂度O(N²)，大量元素时性能下降明显。

**分层优化方案**：

| 元素数量      | 策略                | 复杂度     | 说明                 |
| ------------- | ------------------- | ---------- | -------------------- |
| N ≤ 30       | 贪心匹配            | O(N²)     | 默认策略，简单可靠   |
| 30 < N ≤ 100 | 类型分桶 + 贪心     | O(N²/K)   | 按Type分桶后桶内匹配 |
| N > 100       | 空间索引 + 阈值剪枝 | O(N log N) | 四叉树加速IoU计算    |

**类型分桶实现**：

```cpp
void FSmartMatcher::MatchWithBucketing(TArray<FUMGWidgetNode>& NewNodes, const TArray<FUMGWidgetNode>& OldNodes)
{
    // 按Type分桶，同类型元素才需要匹配
    TMap<FString, TArray<int32>> OldBuckets;
    for (int32 i = 0; i < OldNodes.Num(); i++)
    {
        OldBuckets.FindOrAdd(OldNodes[i].Type).Add(i);
    }

    for (int32 NewIdx = 0; NewIdx < NewNodes.Num(); NewIdx++)
    {
        const FString& Type = NewNodes[NewIdx].Type;
        TArray<int32>* Bucket = OldBuckets.Find(Type);

        if (!Bucket || Bucket->Num() == 0)
        {
            NewNodes[NewIdx].Id = GenerateNewId(NewNodes[NewIdx]);
            continue;
        }

        // 仅在同类型桶内匹配
        float BestScore = 0.0f;
        int32 BestBucketIdx = -1;

        for (int32 i = 0; i < Bucket->Num(); i++)
        {
            float Score = CalculateMatchScore(NewNodes[NewIdx], OldNodes[(*Bucket)[i]]);
            if (Score > BestScore)
            {
                BestScore = Score;
                BestBucketIdx = i;
            }
        }

        if (BestScore >= MatchThreshold && BestBucketIdx >= 0)
        {
            NewNodes[NewIdx].Id = OldNodes[(*Bucket)[BestBucketIdx]].Id;
            Bucket->RemoveAt(BestBucketIdx);  // 移除已匹配项
        }
        else
        {
            NewNodes[NewIdx].Id = GenerateNewId(NewNodes[NewIdx]);
        }
    }
}
```

**超时保护**：

```cpp
// 匹配超时限制（毫秒）
const double MatchTimeoutMs = 100.0;

void FSmartMatcher::MatchWithTimeout(...)
{
    double StartTime = FPlatformTime::Seconds() * 1000.0;

    for (int32 NewIdx = 0; NewIdx < NewNodes.Num(); NewIdx++)
    {
        // 超时检查
        if ((FPlatformTime::Seconds() * 1000.0 - StartTime) > MatchTimeoutMs)
        {
            UE_LOG(LogImageToUMG, Warning, TEXT("SmartMatcher timeout, remaining %d nodes use new IDs"),
                   NewNodes.Num() - NewIdx);

            // 剩余节点直接生成新ID
            for (int32 i = NewIdx; i < NewNodes.Num(); i++)
            {
                NewNodes[i].Id = GenerateNewId(NewNodes[i]);
            }
            break;
        }

        // 正常匹配逻辑...
    }
}
```

> **实施建议**：Phase 2初期使用贪心匹配即可满足大部分HUD场景（通常<50个元素）。分桶和空间索引作为后续优化，按需实现。

---

### 3.6 Parser（元素检测）

**推荐选型**：Microsoft OmniParser V2（2025 SOTA）

**OmniParser V2特性**：

- YOLOv8微调模型检测UI交互元素
- Florence-2模型生成元素描述标签
- 60%延迟降低，小元素检测更准
- 支持Docker部署或本地Python进程

**集成工作流**：

```
1. Detect: OmniParser输出所有交互元素BBox和类别
   [ID:0, Box:[0.1,0.1,0.2,0.2], Type: icon]
   [ID:1, Box:[0.15,0.1,0.3,0.15], Type: text 'HP']
   
2. Prompt注入: 将检测结果作为Context喂给LLM
   "Found interactive elements at: [ID:0, Box:[...], Type: icon]..."
   
3. Refine: LLM结合语义，将物理检测结果组装成逻辑组件
   icon + text 'HP' → IconText组件
```

**备选方案**：PaddleOCR（本地OCR）

- 适合HUD特点：数字 + 短文本 + 高对比
- 本地运行：隐私/成本/延迟更优
- 未安装时退化为纯AI识别

**OCR输出格式**：

```json
{
  "texts": [
    {"bbox": [100, 50, 200, 80], "text": "100/100", "confidence": 0.98},
    {"bbox": [1700, 900, 1850, 950], "text": "30/120", "confidence": 0.95}
  ]
}
```

---

#### 3.6.1 Parser依赖管理与优雅退化

> **风险修复**: OmniParser V2需要YOLOv8+Florence-2模型和推理环境，必须清晰管理依赖并优雅退化

**依赖可用性检测**：

```cpp
enum class EParserAvailability : uint8
{
    FullLocal,        // OmniParser本地完全可用（模型+Python环境）
    CloudFallback,    // 本地不可用，走云API（如果配置）
    AIDirect          // Parser完全跳过，AI直接识别（无预处理）
};

class FParserManager
{
public:
    // 启动时检测依赖可用性
    EParserAvailability CheckAvailability()
    {
        // 1. 检查Python环境
        bool bPythonAvailable = FPlatformProcess::ExecProcess(
            TEXT("python"), TEXT("--version"), nullptr, nullptr, nullptr
        ) != -1;
    
        if (!bPythonAvailable)
        {
            UE_LOG(LogImageToUMG, Warning, TEXT("Python not found, Parser fallback to CloudAPI or AIDirect"));
            return CheckCloudFallback();
        }
    
        // 2. 检查OmniParser模型文件
        FString ModelPath = FPaths::Combine(
            FPaths::ProjectPluginsDir(), 
            TEXT("ImageToUMG/ThirdParty/OmniParser/models")
        );
    
        bool bModelsExist = FPaths::DirectoryExists(ModelPath) && 
                            FPaths::FileExists(FPaths::Combine(ModelPath, TEXT("yolov8_ui.pt"))) &&
                            FPaths::FileExists(FPaths::Combine(ModelPath, TEXT("florence2_base.pt")));
    
        if (!bModelsExist)
        {
            UE_LOG(LogImageToUMG, Warning, TEXT("OmniParser models not found at %s. Please download models or use Docker."), *ModelPath);
            return CheckCloudFallback();
        }
    
        // 3. 启动本地Parser进程（按需）
        // 优先尝试连接Docker服务，如果配置了DockerEndpoint
        if (IsDockerServiceAvailable())
        {
             return EParserAvailability::FullLocal;
        }

        if (!bParserProcessRunning)
        {
            bParserProcessRunning = LaunchParserProcess();
        }
    
        return bParserProcessRunning ? EParserAvailability::FullLocal : CheckCloudFallback();
    }
  
    // 优雅退化策略
    FParsedElementList ParseImage(const FString& ImagePath)
    {
        EParserAvailability Availability = CheckAvailability();
    
        switch (Availability)
        {
            case EParserAvailability::FullLocal:
                UE_LOG(LogImageToUMG, Log, TEXT("Using local OmniParser"));
                return ParseWithLocalOmniParser(ImagePath);
            
            case EParserAvailability::CloudFallback:
                UE_LOG(LogImageToUMG, Warning, TEXT("Falling back to Cloud Parser API"));
                return ParseWithCloudAPI(ImagePath);
            
            case EParserAvailability::AIDirect:
                UE_LOG(LogImageToUMG, Warning, TEXT("Parser not available, AI will detect elements directly"));
                return FParsedElementList();  // 返回空列表，AI直接识别
        }
    }
  
private:
    bool bParserProcessRunning = false;
  
    EParserAvailability CheckCloudFallback()
    {
        // 检查是否配置了Cloud API
        UImageToUMGSettings* Settings = GetMutableDefault<UImageToUMGSettings>();
        if (!Settings->CloudParserAPIKey.IsEmpty())
        {
            return EParserAvailability::CloudFallback;
        }
    
        return EParserAvailability::AIDirect;
    }
  
    bool LaunchParserProcess()
    {
        // 启动本地Python OmniParser进程
        // ...
        return true;
    }
};
```

**用户体验**：

```cpp
// 插件设置中增加可配置项
struct FImageToUMGSettings
{
    // Parser模式选择
    UPROPERTY(Config, EditAnywhere, Category = "Parser")
    EParserMode ParserMode = EParserMode::Auto;  // Auto / ForceLocal / ForceAIDirect
  
    // Cloud Fallback API配置（可选）
    UPROPERTY(Config, EditAnywhere, Category = "Parser|Cloud Fallback")
    FString CloudParserAPIKey;
  
    UPROPERTY(Config, EditAnywhere, Category = "Parser|Cloud Fallback")
    FString CloudParserEndpoint;
};
```

**错误提示**：

```cpp
// 依赖缺失时的友好提示
if (Availability == EParserAvailability::AIDirect)
{
    FNotificationInfo Info(FText::FromString(
        TEXT("OmniParser not available. AI will detect elements directly.\n")
        TEXT("To enable Parser:\n")
        TEXT("1. Install Python 3.8+\n")
        TEXT("2. Download models to Plugins/ImageToUMG/ThirdParty/OmniParser/models\n")
        TEXT("   或配置Cloud Parser API Key in Settings")
    ));
    Info.ExpireDuration = 10.0f;
    FSlateNotificationManager::Get().AddNotification(Info);
}
```

#### 3.6.2 资源预算

**最低配置要求**（本地运行OmniParser V2）：

| 组件           | 最低配置（降级模式） | 推荐配置（全功能） |
| -------------- | -------------------- | ------------------ |
| **GPU**  | GTX 1660 6GB         | RTX 4090 24GB      |
| **VRAM** | 6GB（降级模式可用）  | 16GB+              |
| **RAM**  | 16GB                 | 32GB               |
| **存储** | 10GB（模型文件）     | SSD 20GB           |
| **CUDA** | 11.2+                | 12.0+              |

> **说明**：
>
> - **最低配置**：可运行降级模式（CloudFallback或AIDirect），本地Parser可能显存不足
> - **推荐配置**：可运行完整本地OmniParser，需8GB+可用VRAM
> - 无独显或VRAM不足时，自动退化到CloudFallback或AIDirect模式

> **数据来源**：OmniParser V2使用YOLOv8（需8GB+ VRAM）和Florence-2（约4.6GB推理）。
> RTX 4090实测推理速度0.8s/帧，A100约0.6s/帧。

**各模式资源消耗对比**：

| 模式                             | GPU占用     | 延迟   | 精度 | 适用场景        |
| -------------------------------- | ----------- | ------ | ---- | --------------- |
| **FullLocal** (OmniParser) | 8-12GB VRAM | 1.5-2s | 最高 | 有GPU的开发机   |
| **CloudFallback**          | 无          | 3-5s   | 中等 | 无GPU或云端部署 |
| **AIDirect**               | 无          | 1-2s   | 较低 | 快速原型验证    |

**自动模式选择逻辑**：

```cpp
EParserAvailability FParserManager::AutoSelectMode()
{
    // 1. 检测可用VRAM
    int32 AvailableVRAM = GetAvailableGPUMemoryMB();

    // 2. 根据资源自动选择
    if (AvailableVRAM >= 12 * 1024 && IsCUDAAvailable())
    {
        return EParserAvailability::FullLocal;
    }
    else if (!CloudParserAPIKey.IsEmpty())
    {
        return EParserAvailability::CloudFallback;
    }
    else
    {
        return EParserAvailability::AIDirect;
    }
}
```

---

### 3.7 结构化JSON输出

**目标**：消除JSON格式漂移，减少解析失败和重试

**严格Schema主路径**（推荐）：

```
- OpenAI: response_format = { type: "json_schema", json_schema: {...} }
- Anthropic: Tool Use + JSON Schema（tool/function + input_schema + strict=true）
- Google Gemini: response_mime_type = "application/json" + response_json_schema
```

**宽松/后备路径**（需本地校验）：

```
- DeepSeek: response_format = { type: "json_object" }
- Qwen: response_format = { type: "json_object" } 或 function calling
```

> **注意**：宽松路径仅保证输出是合法JSON，不保证符合Schema。必须本地校验 + 重试 + Patch修复。

**实现要点**：

1. 在 `IAIProviderStrategy` 中增加 `SupportsStrictSchema()` 虚函数
2. 不支持的提供商自动走回退路径（Schema校验 + 重试 + Patch）

---

### 3.8 Repair Agent（Patch模式）

**目标**：Schema校验失败时最小改动修复，避免重写全量JSON

**Patch格式**：采用 IdPathPatch（自定义格式，程序端映射到RFC6902 JSON Patch）

**模型输出格式**（IdPathPatch）：

```json
[
  { "op": "replace", "path": "/Canvas_Root/ProgressBar_0/Bounds/X", "value": 0.15 },
  { "op": "add", "path": "/Canvas_Root/Children/-", "value": {...} }
]
```

**程序端两步处理**：

```cpp
// 步骤1：IdPathPatch → JSON Pointer映射
FString FRepairAgent::IdPathToJsonPointer(const FString& IdPath, const FUMGWidgetTree& Tree)
{
    // 示例："/Canvas_Root/ProgressBar_0/Bounds/X" 
    //    → "/RootWidget/Children/0/Bounds/X"
  
    TArray<FString> Parts;
    IdPath.ParseIntoArray(Parts, TEXT("/"));
  
    FString JsonPointer = TEXT("");
    FUMGWidgetNode* Current = &Tree.RootWidget;
  
    for (int i = 1; i < Parts.Num(); i++)  // 跳过空的第一个元素
    {
        const FString& Part = Parts[i];
    
        if (Current && Current->Children.Num() > 0)
        {
            // 查找匹配的子节点
            for (int j = 0; j < Current->Children.Num(); j++)
            {
                if (Current->Children[j].Id.EndsWith(Part))
                {
                    JsonPointer += FString::Printf(TEXT("/Children/%d"), j);
                    Current = &Current->Children[j];
                    break;
                }
            }
        }
        else
        {
            // 属性路径
            JsonPointer += TEXT("/") + Part;
        }
    }
  
    return JsonPointer;
}

// 步骤2：应用JSON Patch
void FRepairAgent::ApplyPatch(FJsonObject& Target, const TArray<FPatchOp>& Ops)
{
    for (const auto& Op : Ops)
    {
        FString JsonPointer = IdPathToJsonPointer(Op.Path, CurrentTree);
    
        switch (Op.Operation)
        {
            case EPatchOp::Replace:
                JsonPatch::Replace(Target, JsonPointer, Op.Value);
                break;
            case EPatchOp::Add:
                JsonPatch::Add(Target, JsonPointer, Op.Value);
                break;
            case EPatchOp::Remove:
                JsonPatch::Remove(Target, JsonPointer);
                break;
        }
    }
}
```

**触发条件**：

| 条件           | 说明                    |
| -------------- | ----------------------- |
| Schema校验失败 | 格式错误时触发Patch修复 |
| 渲染差异超阈值 | 视觉差分闭环触发        |
| 用户手动触发   | 兜底方案                |

**Patch规模限制**：

```cpp
const int32 MaxPatchOps = 64;
const int32 MaxPatchPayloadBytes = 8 * 1024;  // 8KB

if (PatchOps.Num() > MaxPatchOps || PayloadSize > MaxPatchPayloadBytes)
{
    RegenerateSubtree(FailedNode);
}
```

**Op类型限制**：只允许 `add` / `remove` / `replace` 三种op

> **实现注意事项**：
>
> 上述 `IdPathToJsonPointer`示例使用 `EndsWith(Part)`匹配，存在以下风险：
>
> - 同名后缀误匹配（如 `HealthBar_0/TextBlock_0`和 `ManaBar_0/TextBlock_0`）
> - 节点重排导致匹配失败
>
> **建议的实现优化**：
>
> 1. 使用**完整ID精确匹配**而非EndsWith
> 2. 预建 `TMap<FString, int32>`索引表，O(1)查找
> 3. 应用Patch前校验目标节点（Type/Bounds IoU），不匹配则拒绝并触发重生成

---

### 3.9 回归测试指标

**目标**：量化改动效果，支撑持续迭代

**指标定义**：

| 指标                 | 说明                            | 阈值             |
| -------------------- | ------------------------------- | ---------------- |
| **元素召回率** | 识别出的元素数 / 实际元素数     | ≥ 90%           |
| **元素精确率** | 正确识别的元素 / 识别出的元素数 | ≥ 90%           |
| **布局MAE**    | 平均绝对误差（像素）            | **≤ 4px** |
| **布局P95**    | 95分位误差（像素）              | ≤ 8px           |
| **文本准确率** | OCR正确的文本 / 总文本数        | ≥ 95%           |
| **ID稳定率**   | 无修改重生成时ID不变比例        | ≥ 95%           |
| **渲染差分**   | 结构化差异 + 像素差异           | 结构优先         |

> **阈值说明**：召回率90%参考Widget2Code论文报告的92%，留有安全边际。
> 初期验收可放宽至85%，逐步提升。过高的阈值会导致大量"失败"判定。

**像素误差计算**：

```cpp
float PixelErrorX = FMath::Abs(Actual.Bounds.X - Expected.Bounds.X) * DesignSize.X;
float PixelErrorY = FMath::Abs(Actual.Bounds.Y - Expected.Bounds.Y) * DesignSize.Y;
float MAE = (PixelErrorX + PixelErrorY) / 2.0f;
```

**回归集**：自建50-200张基线HUD（UMG快速搭建→截图→标注JSON）

---

### 3.10 渲染对比闭环

> **定位调整：评测工具 + 可选自动修复**
>
> 渲染差分容易误报：DPI缩放、字体渲染差异、抗锯齿、材质加载时序都会带来噪声。
> 虽然有文本mask，但仍有大量其他噪声源。
>
> **功能分层**：
> - **评测工具**（默认启用）：输出差异报告，供人工判断
> - **自动修复**（默认关闭）：`bEnableAutoRepair = false`，用户可手动开启
>
> **风险**：自动修复在误报率高时可能过度修复，建议积累样本后再开启。

**主方案**：`UWidgetBlueprintLibrary::DrawWidgetToRenderTarget`

**兜底方案**：`FWidgetRenderer`（Slate渲染到RenderTarget）

```cpp
FWidgetRenderer* WidgetRenderer = new FWidgetRenderer(bUseGammaCorrection, bInClearTarget);
UTextureRenderTarget2D* RenderTarget = FWidgetRenderer::CreateTargetFor(DrawSize, Filter, bUseGammaCorrection);
TSharedRef<SWidget> SlateWidget = WidgetToRender->TakeWidget();
WidgetRenderer->DrawWidget(RenderTarget, SlateWidget, DrawSize, DeltaTime);
```

**流程**：

```
生成UMG → 离屏渲染(512x) → 与原图差分 → TopN热区 → 局部迭代
```

**差分策略**（结构优先）：

1. **结构化差异**（低噪声，优先使用）

   - bbox/元素数量/文本匹配
   - IoU/对齐误差
2. **像素差异**（补充）

   - 降采样后灰度L1/L2差分
   - 边缘强度差分
   - **Mask掉文本区域**（避免字体抗锯齿干扰）

**文本区域Mask实现**：

```cpp
// 生成文本区域Mask
TArray<FBox2D> FRenderCompare::CollectTextMasks(const FWidgetTree& Tree, const FIntPoint& RenderSize)
{
    TArray<FBox2D> Masks;
  
    // 递归收集所有TextBlock的Bounds
    TFunction<void(const FWidgetTreeNode&)> CollectTextNodes = [&](const FWidgetTreeNode& Node)
    {
        if (Node.Type == TEXT("TextBlock") || Node.Type == TEXT("EditableTextBox"))
        {
            // Bounds转像素坐标，并膨胀4px覆盖抗锯齿边缘
            FBox2D PixelBox;
            PixelBox.Min.X = FMath::Max(0.0f, Node.Bounds.X * RenderSize.X - 4.0f);
            PixelBox.Min.Y = FMath::Max(0.0f, Node.Bounds.Y * RenderSize.Y - 4.0f);
            PixelBox.Max.X = FMath::Min((float)RenderSize.X, (Node.Bounds.X + Node.Bounds.W) * RenderSize.X + 4.0f);
            PixelBox.Max.Y = FMath::Min((float)RenderSize.Y, (Node.Bounds.Y + Node.Bounds.H) * RenderSize.Y + 4.0f);
            Masks.Add(PixelBox);
        }
    
        for (const auto& Child : Node.Children)
        {
            CollectTextNodes(Child);
        }
    };
  
    CollectTextNodes(Tree.RootWidget);
  
    // 如果有OCR结果，也加入Mask（覆盖AI可能遗漏的文本）
    if (ParsedElements.IsValid())
    {
        for (const auto& Elem : ParsedElements->Elements)
        {
            if (Elem.DetectedType == TEXT("text"))
            {
                // OCR bbox转归一化再转渲染尺寸
                FBox2D PixelBox;
                float NormX = Elem.BBox.Min.X / ParsedElements->SourceImageSize.X;
                float NormY = Elem.BBox.Min.Y / ParsedElements->SourceImageSize.Y;
                float NormW = (Elem.BBox.Max.X - Elem.BBox.Min.X) / ParsedElements->SourceImageSize.X;
                float NormH = (Elem.BBox.Max.Y - Elem.BBox.Min.Y) / ParsedElements->SourceImageSize.Y;
            
                PixelBox.Min.X = FMath::Max(0.0f, NormX * RenderSize.X - 4.0f);
                PixelBox.Min.Y = FMath::Max(0.0f, NormY * RenderSize.Y - 4.0f);
                PixelBox.Max.X = FMath::Min((float)RenderSize.X, (NormX + NormW) * RenderSize.X + 4.0f);
                PixelBox.Max.Y = FMath::Min((float)RenderSize.Y, (NormY + NormH) * RenderSize.Y + 4.0f);
                Masks.Add(PixelBox);
            }
        }
    }
  
    return Masks;
}

// 在像素差分时应用Mask
float FRenderCompare::ComputeMaskedDifference(const TArray<uint8>& ImageA, const TArray<uint8>& ImageB,
                                              const FIntPoint& Size, const TArray<FBox2D>& Masks)
{
    float TotalDiff = 0.0f;
    int32 ValidPixels = 0;
  
    for (int32 Y = 0; Y < Size.Y; ++Y)
    {
        for (int32 X = 0; X < Size.X; ++X)
        {
            // 检查是否在Mask区域内
            bool bMasked = false;
            for (const auto& Mask : Masks)
            {
                if (X >= Mask.Min.X && X <= Mask.Max.X && Y >= Mask.Min.Y && Y <= Mask.Max.Y)
                {
                    bMasked = true;
                    break;
                }
            }
        
            if (!bMasked)
            {
                int32 Idx = Y * Size.X + X;
                TotalDiff += FMath::Abs((float)ImageA[Idx] - (float)ImageB[Idx]);
                ValidPixels++;
            }
        }
    }
  
    return ValidPixels > 0 ? TotalDiff / (ValidPixels * 255.0f) : 0.0f;
}
```

> **Mask策略**：
>
> 1. 收集所有TextBlock/EditableTextBox控件的Bounds
> 2. 合并OCR检测到的文本bbox（覆盖AI遗漏）
> 3. 膨胀4px覆盖抗锯齿边缘
> 4. 差分计算时跳过Mask区域

**差分优先级**（从高到低）：

| 优先级 | 类型               | 说明                         | 修复触发条件                        |
| ------ | ------------------ | ---------------------------- | ----------------------------------- |
| 1      | **结构差异** | 元素数量、层级关系、类型匹配 | 差异>0即触发                        |
| 2      | **位置差异** | bbox IoU、中心点偏移         | IoU<0.9或偏移>8px                   |
| 3      | **尺寸差异** | 宽高比例误差                 | 误差>5%                             |
| 4      | **颜色差异** | 主色调ΔE（CIELAB色差）      | ΔE≥3（肉眼可辨）                  |
| 5      | **像素差异** | 灰度L1差分                   | 差异>5%（最低优先级，易受字体干扰） |

> **设计原则**：高优先级差异未解决时，不启用低优先级修复。
> 例如：结构差异存在时，不触发颜色修正。

**输出**：差异热图 + TopN矩形区域

---

## 四、JSON Schema设计

### 4.1 节点公共字段

**节点结构**：

```json
{
  "Type": "ProgressBar",
  "Id": "Canvas_Root/ProgressBar_0",
  "Name": "Progress_HealthBar",
  "Bounds": {"X": 0.0200, "Y": 0.0200, "W": 0.2000, "H": 0.0300},
  "Properties": {...},
  "Children": [...],
  "_meta": {
    "confidence": 0.95,
    "source": "ai",
    "uncertain_props": []
  }
}
```

**字段说明**：

| 字段           | 必填 | 说明                                              |
| -------------- | ---- | ------------------------------------------------- |
| `Type`       | ✅   | Widget类型（CanvasPanel/ProgressBar/TextBlock等） |
| `Id`         | ✅   | **稳定唯一标识**，用于Patch定位和跨轮次引用 |
| `Name`       | ✅   | UMG中的控件名称（可重复）                         |
| `Bounds`     | ✅   | 归一化坐标（4位小数）                             |
| `Properties` | ❌   | 类型特定属性                                      |
| `Children`   | ❌   | 子节点数组                                        |
| `_meta`      | ❌   | 调试和置信度信息                                  |

### 4.2 Id生成规则

采用路径式稳定Id：`{ParentId}/{Type}_{SiblingIndex}`

**排序稳定规则**：同类型控件按 `Bounds.X` 升序、`Bounds.Y` 升序排序后编号，确保位置不变时Id稳定。

```
示例层级：
Canvas_Root
├── ProgressBar_0    → Id: "Canvas_Root/ProgressBar_0"
├── ProgressBar_1    → Id: "Canvas_Root/ProgressBar_1"
└── Image_0          → Id: "Canvas_Root/Image_0"
```

### 4.3 Bounds格式

```json
{
  "Bounds": {
    "X": 0.0200,   // 左上角X（0-1，4位小数）
    "Y": 0.0200,   // 左上角Y（0-1，4位小数）
    "W": 0.2000,   // 宽度（0-1，4位小数）
    "H": 0.0300    // 高度（0-1，4位小数）
  }
}
```

### 4.4 _meta字段（不确定性显式化）

**目标**：把AI识别的不确定性显式化，便于调试和人工验收

```json
{
  "_meta": {
    "confidence": 0.85,
    "source": "ocr",
    "uncertain_props": ["FontSize"]
  }
}
```

| 字段                | 类型     | 说明                                                         |
| ------------------- | -------- | ------------------------------------------------------------ |
| `confidence`      | float    | 识别置信度（0-1）                                            |
| `source`          | enum     | 数据来源：`"ai"` / `"ocr"` / `"parser"` / `"manual"` |
| `uncertain_props` | string[] | 不确定的属性列表                                             |

**用途**：

- 低置信度元素可标记为"待人工确认"
- 调试时快速定位问题来源
- 编辑器UI中高亮显示不确定区域

### 4.5 Component DSL

**目标**：实现最小DSL编译器，AI输出组件描述，程序端展开为标准控件树

**支持组件**：

| 组件          | 用途      | 展开为                         |
| ------------- | --------- | ------------------------------ |
| `HealthBar` | 条形进度  | ProgressBar + Border + Overlay |
| `Ring`      | 圆环进度  | Image + 材质参数               |
| `IconText`  | 图标+数值 | HBox + Image + TextBlock       |

**格式示例**：

```json
{
  "Type": "Component",
  "ComponentType": "HealthBar",
  "Bounds": {"X": 0.02, "Y": 0.02, "W": 0.2, "H": 0.03},
  "Params": {
    "FillColor": "#FF0000",
    "BackgroundColor": "#333333",
    "BorderWidth": 2,
    "ShowText": true,
    "TextFormat": "{current}/{max}"
  }
}
```

#### 4.5.1 DSL展开ID规则

**问题**：Component展开后产生多个子节点，需要稳定的ID映射以支持SmartMatcher和RepairAgent。

**ID生成规则**：

```
{ComponentId}/{InternalType}_{Index}
```

**展开示例**：

```json
// AI输出（1个Component节点）
{
  "Type": "Component",
  "ComponentType": "HealthBar",
  "Id": "Canvas_Root/HealthBar_0"
}

// 程序端展开后（3个子节点，ID自动生成）
{
  "Type": "Overlay",
  "Id": "Canvas_Root/HealthBar_0/Overlay_0",
  "Children": [
    {
      "Type": "ProgressBar",
      "Id": "Canvas_Root/HealthBar_0/ProgressBar_0"
    },
    {
      "Type": "Border",
      "Id": "Canvas_Root/HealthBar_0/Border_0"
    }
  ]
}
```

**SmartMatcher适配**：

1. 匹配时先匹配外层ComponentId，内部节点ID保持稳定
2. Component的内部节点不参与跨轮次匹配（由Component整体匹配）
3. RepairAgent支持Component级别Patch（不穿透内部结构）

> **注意**：DSL内部使用的HBox/VBox等容器不违反2.4节的"AI输出规则"，
> 因为这些容器是程序端展开产生的，不是AI直接输出的。

### 4.6 向后兼容

- 同时支持 `Slot`（旧格式）和 `Bounds`（新格式）
- 检测到 `Bounds`时使用LayoutCalculator计算
- 检测到 `Slot`时直接使用

---

## 五、技术选型

### 5.1 AI供应商

| 提供商                  | 能力                             | 关键参数                                      | 定位                 |
| ----------------------- | -------------------------------- | --------------------------------------------- | -------------------- |
| **OpenAI**        | Structured Outputs + JSON Schema | `response_format: { type: "json_schema" }`  | 严格Schema主路径     |
| **Anthropic**     | Tool Use + JSON Schema strict    | `tool/function + strict=true` + beta header | 严格Schema主路径     |
| **Google Gemini** | Structured Outputs               | `response_mime_type: "application/json"`    | 严格Schema主路径     |
| **DeepSeek**      | JSON Output                      | `response_format: { type: "json_object" }`  | 宽松路径，需本地校验 |
| **Qwen**          | JSON Output                      | `response_format: { type: "json_object" }`  | 后备路径             |

> **Anthropic注意事项**：Structured Outputs是2025年11月的新功能，需要：
>
> - 添加请求头：`anthropic-beta: structured-outputs-2025-11-13`
> - 支持的模型：Claude Sonnet 4.5、Opus 4.1、Opus 4.5、Haiku 4.5等4.x系列
> - 可选择两种模式：`output_format: json_schema`（直接JSON）或 Tool Use + `strict: true`

### 5.2 其他模块

| 模块       | 选型                          | 备选                | 备注         |
| ---------- | ----------------------------- | ------------------- | ------------ |
| Parser     | OmniParser V2                 | PaddleOCR           | 推荐SOTA方案 |
| OCR        | PaddleOCR（本地）             | 云API               | 可选模块     |
| 对齐阈值   | `clamp(minDim*0.002, 2, 6)` | -                   | 可配置       |
| 离屏渲染   | `DrawWidgetToRenderTarget`  | `FWidgetRenderer` | 编辑器环境   |
| 差分算法   | 结构差分 + 灰度L1             | SSIM                | 结构优先     |
| JSON Patch | nlohmann/json                 | -                   | RFC6902规范  |
| 图像分割   | SAM3                          | SAM2                | 图标批量抠图 |

---

## 六、实施路线

> **产品定位**：轻量级UE引擎插件，开箱即用。核心功能仅依赖AI API，无需Python/CUDA/本地模型。
> 外部依赖功能作为可选增强，用户可按需启用。

### 6.1 Phase 1：纯UE核心（无外部依赖）

**目标**：仅依赖AI API，纯C++实现，安装即用

| 项目                          | 交付物                       | 依赖   | 预期收益       |
| ----------------------------- | ---------------------------- | ------ | -------------- |
| 契约冻结                      | Bounds定义、MAE口径、基线样例 | 无     | 避免返工       |
| LayoutCalculator + Hysteresis | LayoutCalculator.h/cpp       | 无     | 锚点稳定       |
| ViewportMapping               | ViewportMapping.h/cpp        | 无     | 坐标系统一     |
| 一致性验证测试                | Tests/                       | 无     | 公式正确性     |
| Fixer规则修复器               | LayoutFixer.h/cpp            | 无     | 对齐/越界/等距 |
| Prompt简化 + Bounds格式       | 新Prompt、Parser修改         | AI API | 位置精度↑     |
| JSON Schema Strict            | HttpAIClient修改、Schema定义 | AI API | 消除格式漂移   |
| _meta字段支持                 | Schema修改                   | 无     | 调试能力↑     |

### 6.2 Phase 2：迭代修复（无外部依赖）

**目标**：增强修复能力，仍为纯C++实现

| 项目          | 交付物                   | 依赖   | 预期收益   |
| ------------- | ------------------------ | ------ | ---------- |
| Smart Matcher | SmartMatcher.h/cpp       | 无     | ID稳定     |
| Repair Agent  | RepairAgent.h/cpp        | AI API | Patch修复  |
| DSL编译器     | ComponentCompiler        | 无     | 可维护性↑ |
| 渲染对比闭环  | WidgetRenderer、差分算法 | 无     | 自动修复   |

### 6.3 Phase 3：可选增强（轻量外部依赖）

**目标**：提升精度，需要用户主动配置Python环境

| 项目                     | 交付物            | 依赖               | 预期收益     |
| ------------------------ | ----------------- | ------------------ | ------------ |
| PaddleOCR集成            | ocr_server.py     | Python + PaddleOCR | 文本精度↑   |
| 检索增强（Few-shot RAG） | 本地embedding检索 | Python + embedding | 输出一致性↑ |

### 6.4 Phase 4：高级扩展（重度外部依赖）

**目标**：SOTA能力，需要GPU + 大模型

| 项目           | 交付物                | 依赖                     | 预期收益     |
| -------------- | --------------------- | ------------------------ | ------------ |
| OmniParser集成 | omniparser_server.py  | Python + CUDA + 10GB模型 | 召回率↑     |
| SAM3资产切图   | sam_server.py         | Python + CUDA + SAM3     | 图标自动提取 |
| T3D输出通道    | T3DExporter           | 无                       | 原生格式支持 |
| 检索增强完整版 | 向量数据库 + 案例管理 | Python + 向量DB          | 生产级RAG    |

> **用户体验**：
>
> - Phase 1-2：安装插件 → 配置API Key → 开始使用
> - Phase 3：可选安装Python → 运行pip install → 启用增强
> - Phase 4：需要CUDA GPU → 下载模型 → 获得SOTA能力

---

## 七、关键文件清单

### 核心模块

| 文件                                                     | 操作 | 说明                 |
| -------------------------------------------------------- | ---- | -------------------- |
| `Content/Config/DefaultPrompt.txt`                     | 重写 | 简化为~50行          |
| `Source/ImageToUMGEditor/Public/ViewportMapping.h`     | 新建 | 视口映射头文件       |
| `Source/ImageToUMGEditor/Private/ViewportMapping.cpp`  | 新建 | Fit/Fill/Stretch实现 |
| `Source/ImageToUMGEditor/Public/LayoutCalculator.h`    | 新建 | 布局计算器头文件     |
| `Source/ImageToUMGEditor/Private/LayoutCalculator.cpp` | 新建 | Hysteresis算法       |
| `Source/ImageToUMGEditor/Public/SmartMatcher.h`        | 新建 | ID匹配器头文件       |
| `Source/ImageToUMGEditor/Private/SmartMatcher.cpp`     | 新建 | 二分图匹配           |
| `Source/ImageToUMGEditor/Private/UMGJsonParser.cpp`    | 修改 | Bounds格式支持       |

### 规则修复模块

| 文件                                                | 操作 | 说明               |
| --------------------------------------------------- | ---- | ------------------ |
| `Source/ImageToUMGEditor/Public/LayoutFixer.h`    | 新建 | 规则修复器头文件   |
| `Source/ImageToUMGEditor/Private/LayoutFixer.cpp` | 新建 | 对齐/越界/等距算法 |
| `Source/ImageToUMGEditor/Public/RepairAgent.h`    | 新建 | Patch修复器头文件  |
| `Source/ImageToUMGEditor/Private/RepairAgent.cpp` | 新建 | IdPathPatch处理    |

### Parser模块

| 文件                             | 操作 | 说明                  |
| -------------------------------- | ---- | --------------------- |
| `Scripts/omniparser_server.py` | 新建 | OmniParser服务        |
| `Scripts/ocr_server.py`        | 新建 | PaddleOCR服务（可选） |

### 扩展模块

| 文件                                                   | 操作 | 说明         |
| ------------------------------------------------------ | ---- | ------------ |
| `Source/ImageToUMGEditor/Public/WidgetRenderer.h`    | 新建 | 离屏渲染器   |
| `Source/ImageToUMGEditor/Private/WidgetRenderer.cpp` | 新建 | 差分算法实现 |
| `Scripts/sam_server.py`                              | 新建 | SAM3分割服务 |

---

### 7.5 错误处理与可靠性策略

#### API调用策略

**请求参数**：

```cpp
struct FAIRequestConfig
{
    float Temperature = 0.0f;      // 确定性输出，减少随机性
    int32 MaxTokens = 4096;        // 足够容纳复杂HUD的JSON
    int32 MaxRetries = 3;          // 最大重试次数
    float TimeoutSeconds = 60.0f;  // 单次请求超时
    bool bEnableCache = true;      // 启用响应缓存
};
```

#### 重试策略

```cpp
enum class ERetryReason : uint8
{
    NetworkError,      // 网络错误，立即重试
    RateLimited,       // 限流，指数退避
    SchemaViolation,   // Schema错误，改用Repair Agent
    Timeout,           // 超时，延长超时后重试
    ServerError        // 5xx错误，延迟重试
};

struct FRetryPolicy
{
    int32 MaxRetries = 3;
    float BaseDelaySeconds = 1.0f;
    float MaxDelaySeconds = 30.0f;
    float BackoffMultiplier = 2.0f;  // 指数退避
  
    float CalculateDelay(int32 AttemptNumber) const
    {
        float Delay = BaseDelaySeconds * FMath::Pow(BackoffMultiplier, AttemptNumber);
        return FMath::Min(Delay, MaxDelaySeconds);
    }
};
```

#### 响应缓存

```cpp
// 基于图片哈希 + Prompt版本的缓存Key
FString FCacheManager::GenerateCacheKey(const TArray<uint8>& ImageData, const FString& PromptVersion)
{
    FSHAHash ImageHash;
    FSHA1::HashBuffer(ImageData.GetData(), ImageData.Num(), ImageHash.Hash);
    return FString::Printf(TEXT("%s_%s"), *ImageHash.ToString(), *PromptVersion);
}

// 缓存存储位置：Saved/ImageToUMG/Cache/
// 缓存有效期：7天（可配置）
```

#### 降级链（Fallback Chain）

```
主路径               → 降级路径1           → 降级路径2           → 最终降级
OpenAI GPT-4o       → Anthropic Claude    → Google Gemini      → DeepSeek
(Strict Schema)     (Tool Use Strict)     (Structured Output)  (JSON Object + 本地校验)
```

**实现**：

```cpp
class FAIProviderChain
{
public:
    TArray<TSharedPtr<IAIProviderStrategy>> Providers;
  
    FHttpResponsePtr ExecuteWithFallback(const FAIRequest& Request)
    {
        for (auto& Provider : Providers)
        {
            FHttpResponsePtr Response = Provider->Execute(Request);
        
            if (Response->bSuccess && ValidateSchema(Response->Body))
            {
                return Response;
            }
        
            UE_LOG(LogImageToUMG, Warning, TEXT("Provider %s failed, trying next..."), 
                   *Provider->GetName());
        }
    
        // 所有Provider都失败
        return MakeError(TEXT("All providers exhausted"));
    }
};
```

#### Parser降级

```
OmniParser V2 (推荐) → PaddleOCR (本地) → 纯AI识别 (无Parser)
```

**判断逻辑**：

```cpp
bool FParserManager::IsOmniParserAvailable()
{
    // 检查Python进程 + HTTP健康检查
    return bPythonProcessRunning && HttpHealthCheck("http://localhost:8765/health");
}
```

#### 成本控制

| 策略                 | 说明                                        |
| -------------------- | ------------------------------------------- |
| **响应缓存**   | 相同图片+Prompt复用结果，缓存命中不计费     |
| **增量Patch**  | 小修改走Repair Agent，避免全量重生成        |
| **本地Parser** | OmniParser/PaddleOCR本地运行，不消耗API额度 |
| **温度=0**     | 减少随机性，提高缓存命中率                  |
| **Token预估**  | 大图预估Token数，超限时自动降采样           |

#### 错误日志

```cpp
// 所有API调用记录到：Saved/ImageToUMG/Logs/api_calls.jsonl
struct FAPICallLog
{
    FDateTime Timestamp;
    FString Provider;
    FString RequestHash;
    float LatencyMs;
    bool bSuccess;
    FString ErrorMessage;
    int32 TokensUsed;
    float CostUSD;
};
```

---

## 八、参考资源

### 论文

| 论文        | 核心贡献           | 链接                                              |
| ----------- | ------------------ | ------------------------------------------------- |
| Widget2Code | WidgetDSL + 编译器 | [arXiv:2512.19918](https://arxiv.org/abs/2512.19918) |
| UI2Code^N   | 多轮抛光流程       | [arXiv:2511.08195](https://arxiv.org/abs/2511.08195) |
| DCGen       | 分治策略           | [arXiv:2406.16386](https://arxiv.org/abs/2406.16386) |
| Design2Code | UI生成Benchmark    | [arXiv:2403.03163](https://arxiv.org/abs/2403.03163) |
| OmniParser  | 屏幕解析           | [arXiv:2408.00203](https://arxiv.org/abs/2408.00203) |

### 开源项目

| 项目               | 用途             | 链接                                             |
| ------------------ | ---------------- | ------------------------------------------------ |
| OmniParser         | 屏幕解析工具     | [GitHub](https://github.com/microsoft/OmniParser)   |
| screenshot-to-code | Prompt设计参考   | [GitHub](https://github.com/abi/screenshot-to-code) |
| SAM3               | 图像分割         | [GitHub](https://github.com/facebookresearch/sam3)  |
| SAM2               | 图像分割（备选） | [GitHub](https://github.com/facebookresearch/sam2)  |
| PaddleOCR          | 本地OCR          | [GitHub](https://github.com/PaddlePaddle/PaddleOCR) |

---

**文档状态**：已定稿，可以开始实施。
