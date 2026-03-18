# PRD: Pencil MCP Bridge + Visual Comparison

## 1. 概述

为 Design2UMG 插件新增两个 C++ 模块：

1. **PenMCPBridge** — 从 Pencil MCP 协议获取 .pen 数据，组装为 PenParser 可消费的 JSON，喂入现有转换管线
2. **VisualComparator** — 对比 Pencil 设计截图与 UE 渲染截图，输出结构化差异报告，供自动化迭代消费

## 2. 背景

### 2.1 现有管线

```
.pen JSON 文件 → PenParser → Design2UMGConverter → UMGGenerator → Widget Blueprint
```

### 2.2 断裂点

- Pencil MCP 的 .pen 文件不在磁盘上，只能通过 MCP 协议访问
- `PenParser::ParseFromFile()` 无法使用
- `PenParser::ParseDocument(JsonString)` 可用，但没有人负责生成这个 JsonString

### 2.3 目标

补全 MCP → JSON 桥接，并加入截图对比能力，形成可迭代的闭环。

## 3. 功能需求

### 3.1 PenMCPBridge（P0）

#### 3.1.1 核心职责

接收 Pencil MCP `batch_get` 和 `get_variables` 的返回数据，组装为 PenDocument JSON 格式。

#### 3.1.2 输入

| 参数 | 类型 | 来源 | 说明 |
|------|------|------|------|
| NodesJson | FString | `batch_get()` 返回的 JSON 数组 | 节点树，需递归读取（readDepth 足够深） |
| VariablesJson | FString | `get_variables()` 返回 | 变量和主题定义 |
| TargetNodeId | FString (可选) | 用户指定 | 只导出某个 frame，不传则导出所有顶层节点 |

#### 3.1.3 输出

`FString` — 符合 PenParser 期望的完整文档 JSON：

```json
{
  "version": "2.6",
  "variables": { ... },
  "themes": { ... },
  "children": [ ... ]
}
```

#### 3.1.4 接口设计

```cpp
class FPenMCPBridge
{
public:
    /** 从 MCP 返回数据组装 PenDocument JSON */
    static FString AssembleDocumentJson(
        const FString& NodesJson,
        const FString& VariablesJson,
        const FString& TargetNodeId = TEXT("")
    );

    /** 一步到位：组装 + 解析 + 转换，返回 UMG 数据 */
    static bool ConvertFromMCP(
        const FString& NodesJson,
        const FString& VariablesJson,
        FUMGWidgetData& OutData,
        const FString& TargetNodeId = TEXT("")
    );
};
```

#### 3.1.5 组装逻辑

1. 解析 `VariablesJson`，提取 `variables`、`themes`、`fonts` 字段（如果存在）
2. 解析 `NodesJson` 数组（已确认为嵌套树结构，非扁平数组）
3. 如果指定了 `TargetNodeId`，从数组中找到该节点作为唯一 children 元素
4. 组装为 `{"version":"2.6", "fonts":[...], "variables":{...}, "themes":{...}, "children":[...]}`
5. 调用 `FPenImporter::ImportFromJson()` 完成后续转换

#### 3.1.6 格式兼容性处理

已验证 Pencil MCP `batch_get` 返回的字段与 PenParser 期望格式一致：
- `id`, `type`, `name`, `x`, `y`, `width`, `height` — 直接兼容
- `fill: "$--primary"` — PenParser 的 `FPenVariableResolver` 已支持变量引用解析
- `padding: [8, 16]` — PenParser 的 `ParsePadding` 已支持数组格式
- `cornerRadius`, `gap`, `layout`, `alignItems`, `justifyContent` — 直接兼容

无需额外的格式转换层。

### 3.2 VisualComparator（P1）

#### 3.2.1 核心职责

对比两张截图（Pencil 设计稿 vs UE 渲染结果），输出结构化差异报告。

#### 3.2.2 对比策略：双层方案

**第一层：脚本级快速检测（C++ 内置）**
- SSIM 结构相似性分数
- 像素级差异统计（差异像素占比）

**第二层：AI 多模态分析（可选，通过现有 AIProviderStrategy）**
- 将两张截图发送给多模态 LLM
- Prompt 要求识别具体差异：元素偏移、缺失、颜色错误、尺寸不对
- 输出自然语言 + 结构化 JSON 差异列表

#### 3.2.3 接口设计

```cpp
/** 差异项 */
struct FVisualDiffItem
{
    FString Region;        // 差异区域描述，如 "top-left button"
    FString Type;          // offset / missing / color / size
    FString Description;   // 具体描述
    float Severity;        // 0-1 严重程度
};

/** 对比报告 */
struct FVisualCompareReport
{
    float SSIM;                         // 结构相似性 0-1
    float DiffPixelRatio;               // 差异像素占比 0-1
    TArray<FVisualDiffItem> Diffs;      // 差异项列表
    FString AIAnalysis;                 // AI 分析原文（如果启用）
    bool bPassThreshold;                // 是否通过阈值
};

class FVisualComparator
{
public:
    /** 脚本级对比 */
    static FVisualCompareReport CompareImages(
        const TArray<uint8>& DesignPng,
        const TArray<uint8>& RenderedPng,
        float PassThreshold = 0.85f
    );

    /** AI 增强对比（异步，内部先调 CompareImages 填充 SSIM，再追加 AI 分析） */
    static void CompareWithAI(
        const TArray<uint8>& DesignPng,
        const TArray<uint8>& RenderedPng,
        float PassThreshold,
        TFunction<void(const FVisualCompareReport&)> OnComplete
    );
};
```

#### 3.2.4 SSIM 实现

不依赖外部库，纯 C++ 实现简化版 SSIM：
- 将两张图转为灰度
- 滑动窗口（8x8）计算局部均值、方差、协方差
- 按标准 SSIM 公式计算
- 输出全局 SSIM 分数

#### 3.2.5 AI 对比 Prompt

```
你是一个 UI 对比专家。我给你两张图片：
- 图1：设计稿（Pencil 编辑器渲染）
- 图2：实际效果（UE5 UMG 渲染）

请对比两张图，列出所有差异。对于每个差异，输出 JSON：
{"region": "描述位置", "type": "offset|missing|color|size", "description": "具体差异", "severity": 0.0-1.0}

忽略以下已知差异：
- 字体渲染细节（抗锯齿、hinting）
- 亚像素级偏移（<2px）
```

#### 3.2.6 输出格式

JSON 报告，供自动化工作流消费：

```json
{
  "ssim": 0.92,
  "diff_pixel_ratio": 0.03,
  "pass": true,
  "diffs": [
    {"region": "header button", "type": "offset", "description": "向右偏移 4px", "severity": 0.3}
  ],
  "ai_analysis": "..."
}
```

## 4. 技术约束

### 4.1 依赖

- 仅依赖 Design2UMG 插件现有模块 + UE 核心模块
- SSIM 实现不引入外部库（OpenCV 等）
- AI 对比复用现有 `AIProviderStrategy` + `HttpAIClient`/`ScriptAIClient`

### 4.2 文件位置

| 文件 | 路径 |
|------|------|
| PenMCPBridge.h | `Source/Design2UMGEditor/Public/` |
| PenMCPBridge.cpp | `Source/Design2UMGEditor/Private/` |
| VisualComparator.h | `Source/Design2UMGEditor/Public/` |
| VisualComparator.cpp | `Source/Design2UMGEditor/Private/` |

### 4.3 不做什么

- 不实现 MCP 协议客户端（由 Claude Code 在外部调用 MCP，把结果传进来）
- 不生成差异热力图 PNG（报告仅供工作流消费）
- 不修改现有 PenParser / Design2UMGConverter / UMGGenerator 代码

## 5. 验收标准

### 5.1 PenMCPBridge

- [ ] `AssembleDocumentJson` 能正确组装 Pencil MCP 返回的数据
- [ ] 组装后的 JSON 能被 `PenParser::ParseDocument` 成功解析
- [ ] `ConvertFromMCP` 端到端生成有效的 `FUMGWidgetData`
- [ ] 支持 TargetNodeId 过滤单个 frame

### 5.2 VisualComparator

- [ ] `CompareImages` 对两张相同图片返回 SSIM ≈ 1.0
- [ ] `CompareImages` 对明显不同的图片返回 SSIM < 0.5
- [ ] `CompareWithAI` 能调用现有 AI 管线并解析返回的差异列表
- [ ] 输出的 JSON 报告格式正确，可被自动化脚本解析
