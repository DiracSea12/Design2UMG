# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 重要指示 (Critical Instructions)

**交流说中文 - 请始终用中文回复用户的所有问题和交流**

## 项目概述

PenToUMG 是一个 UE5 编辑器插件，从 Pencil .pen 设计文件生成 Widget Blueprint。支持 JSON 手动编辑和预览调整。

**当前方向**: .pen → UMG（已从旧的 Image→AI→UMG 方向迁移）

## 核心编码准则

基于 Linus Torvalds 开发哲学：

1. **好品味原则** - 代码简洁优雅，消除边界情况的特殊处理
2. **Never break userspace** - 向后兼容性神圣不可侵犯
3. **实用主义至上** - 解决实际问题，拒绝过度设计
4. **简洁执念** - 不超过3层缩进，函数只做一件事，删除优于添加
5. **验证 = 验证 + 修复** - "验证"不是只跑一遍看结果，发现问题必须解决。验证未通过 = 任务未完成。不要验完报个分就完事

## 构建与测试

此插件是 UE5 引擎插件，无独立构建命令。通过 UE5 编辑器加载：

```
Edit → Editor Preferences → Plugins → Pen To UMG
```

**验证测试**（在编辑器控制台执行）:
```cpp
// 批量验证 LayoutCalculator 一致性
UMGExtractorTest::BatchTest(TEXT("/Game/UI"));
```

## 架构概览

### 数据流 Pipeline

```
.pen  File → PenParser   ─┐
                           ├→ FDesignNode (共用IR) → VisualMapper → Design2UMGConverter → Fixer → LayoutCalc → Widget Generator
Figma JSON → FigmaParser ─┘
```

### 解耦铁律

**格式特有问题只改格式特有的 Parser，共用层（IR 之后）不动。**

- `PenParser` 负责 .pen 格式的字段映射（如 `clip` → `bClip` + `bHasClip`）
- `FigmaParser` 负责 Figma JSON 的字段映射（如 `clipsContent`）
- `VisualMapper`、`Design2UMGConverter` 等共用代码只认 IR（`FDesignNode`），不关心数据来源
- `bHasClip` 是源感知守卫：Parser 设了才生效，共用层用 `bHasClip && bClip` 双重检查

**反面教材**：Figma 的 `clipsContent` 盲映射导致文字被裁剪 → 正确做法是只改 `FigmaParser` 停止映射，而不是去简化 `VisualMapper` 的守卫条件。共用代码的防御逻辑是给所有 Parser 兜底的，不能因为某个 Parser 的问题就拆掉。

### 核心模块

| 模块 | 文件 | 职责 |
|------|------|------|
| **PenParser** | PenParser.h/cpp | .pen 文件解析 → FDesignNode IR |
| **FigmaParser** | FigmaParser.h/cpp | Figma JSON 解析 → FDesignNode IR |
| **RefExpander** | RefExpander.h/cpp | 组件引用展开、DeepCopy、Descendants 覆盖 |
| **LayoutMapper** | LayoutMapper.h/cpp | Slot 属性、Gap、Padding、SizeBox 包裹 |
| **VisualMapper** | VisualMapper.h/cpp | IR 视觉属性 → UMG 属性（填充、圆角、裁剪、阴影等） |
| **Design2UMGConverter** | Design2UMGConverter.h/cpp | FDesignNode → FUMGWidgetNode 转换 |
| **LayoutCalculator** | LayoutCalculator.h/cpp | Bounds→Anchors/Offsets转换，9宫格锚点+Hysteresis |
| **LayoutFixer** | LayoutFixer.h/cpp | 规则修复：对齐吸附、越界修正、等距检测 |
| **UMGJsonParser** | UMGJsonParser.h/cpp | JSON解析（手动编辑/预览用） |
| **UMGGenerator** | UMGGenerator.h/cpp | Widget Blueprint生成 |
| **PreviewWindow** | SPenToUMGPreviewWindow.h/cpp | JSON编辑、手动调整、UMG预览 |

### 关键数据结构 (UMGSchema.h)

```cpp
// 像素坐标Bounds
struct FBounds { float X, Y, W, H; }

// Widget节点
struct FUMGWidgetNode { FString Type, Id, Name; FBounds Bounds; FUMGSlotData Slot; ... }
```

### 设计原则

1. **Bounds是唯一真值** - Fixer在Bounds层操作，LayoutCalc最后投影
2. **Fixer是一等公民** - 规则修复保证布局质量
3. **本地优先** - 核心功能纯C++，无外部依赖

## 配置文件

| 文件 | 用途 |
|------|------|
| `PenToUMGSettings` | 插件设置（设计分辨率等） |

## 常见开发任务

### 修改Bounds→Slot计算逻辑

修改 `LayoutCalculator.cpp` 中的 `CalculateSlot` 函数，注意：
- 保持 Hysteresis 算法防止锚点跳变
- 边缘吸附阈值在 `FLayoutPreset` 中配置

### 添加新的Fixer规则

在 `LayoutFixer.cpp` 中添加，遵循：
- 检查 `bFixExempt` 跳过豁免节点
- 记录修正日志到 `FFixerLogEntry`
