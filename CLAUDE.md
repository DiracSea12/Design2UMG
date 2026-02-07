# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 重要指示 (Critical Instructions)

**交流说中文 - 请始终用中文回复用户的所有问题和交流**

## 项目概述

ImageToUMG 是一个 AI 驱动的 UE5 编辑器插件，通过多模态 LLM（GPT-4o/Claude/Gemini）分析 UI 截图，自动生成对应的 Widget Blueprint。

**技术方案版本**: v2.2
**当前状态**: Phase 1 已完成，Phase 2 开发中

## 核心编码准则

基于 Linus Torvalds 开发哲学：

1. **好品味原则** - 代码简洁优雅，消除边界情况的特殊处理
2. **Never break userspace** - 向后兼容性神圣不可侵犯
3. **实用主义至上** - 解决实际问题，拒绝过度设计
4. **简洁执念** - 不超过3层缩进，函数只做一件事，删除优于添加

## 构建与测试

此插件是 UE5 引擎插件，无独立构建命令。通过 UE5 编辑器加载：

```
Edit → Editor Preferences → Plugins → Image To UMG
```

**验证测试**（在编辑器控制台执行）:
```cpp
// 批量验证 LayoutCalculator 一致性
UMGExtractorTest::BatchTest(TEXT("/Game/UI"));
```

## 架构概览

### 数据流 Pipeline (v2.2)

```
Image → ViewportMapping → AI → Fixer → LayoutCalc → Widget Generator
         (坐标归一化)    (识别)  (规则修复)  (Slot计算)    (UMG生成)
```

### 核心模块

| 模块 | 文件 | 职责 |
|------|------|------|
| **ViewportMapping** | ViewportMapping.h/cpp | 不同分辨率/纵横比的坐标映射 |
| **LayoutCalculator** | LayoutCalculator.h/cpp | Bounds→Anchors/Offsets转换，9宫格锚点+Hysteresis |
| **LayoutFixer** | LayoutFixer.h/cpp | 规则修复：对齐吸附、越界修正、等距检测 |
| **AIProviderStrategy** | AIProviderStrategy.h/cpp | AI提供商策略模式（OpenAI/Anthropic/Google/Volcengine） |
| **UMGJsonParser** | UMGJsonParser.h/cpp | JSON解析，支持Bounds和_meta字段 |
| **UMGGenerator** | UMGGenerator.h/cpp | Widget Blueprint生成 |
| **HttpAIClient** | HttpAIClient.h/cpp | HTTP请求封装 |
| **ScriptAIClient** | ScriptAIClient.h/cpp | Python脚本客户端（绕过UE HTTP限制） |

### 关键数据结构 (UMGSchema.h)

```cpp
// 归一化坐标（AI输出格式，唯一真值）
struct FBounds { float X, Y, W, H; }  // 0-1，4位小数

// AI识别元数据
struct FUMGMetaData { float Confidence; FString Source; bool bFixExempt; }

// Widget节点
struct FUMGWidgetNode { FString Type, Id, Name; FBounds Bounds; FUMGSlotData Slot; ... }
```

### 设计原则

1. **AI只做识别，不做计算** - 锚点/Offsets由程序计算
2. **Bounds是唯一真值** - Fixer在Bounds层操作，LayoutCalc最后投影
3. **Fixer是一等公民** - 规则修复比AI微调更稳定
4. **本地优先** - 核心功能纯C++，无外部依赖

## 配置文件

| 文件 | 用途 |
|------|------|
| `Content/Config/DefaultPrompt_v2.txt` | AI系统提示词（~50行） |
| `Content/Config/UMGWidgetSchema.json` | JSON Schema定义 |
| `ImageToUMGSettings` | 插件设置（API Key、模型、设计分辨率等） |

## 支持的AI提供商

通过 `EAIProvider` 枚举和策略模式支持：
- OpenAI (Strict JSON Schema)
- Anthropic (Structured Outputs)
- Google Gemini
- DeepSeek / Mistral / Groq / Together / OpenRouter
- Volcengine (Doubao)
- Custom Endpoint

## 开发路线

| Phase | 状态 | 内容 |
|-------|------|------|
| **Phase 1** | ✅ 完成 | 核心重构：LayoutCalculator、ViewportMapping、Fixer、JSON Schema |
| **Phase 2** | 开发中 | 迭代修复：SmartMatcher、RepairAgent、DSL编译器、渲染对比 |
| **Phase 3** | 待开始 | 可选增强：PaddleOCR、Few-shot RAG |
| **Phase 4** | 待开始 | 高级扩展：OmniParser、SAM3、T3D导出 |

## 验收指标

| 指标 | 有Parser | 无Parser |
|------|----------|----------|
| 布局MAE | ≤4px | ≤20px |
| 布局P95 | ≤8px | ≤40px |
| 元素召回率 | ≥90% | ≥85% |

## 常见开发任务

### 添加新AI提供商

1. 在 `AIProviderStrategy.h` 创建新策略类继承 `IAIProviderStrategy`
2. 实现 `GetHeaders`、`BuildRequestBody`、`ExtractContent`
3. 在 `FAIProviderStrategyFactory::GetStrategy` 注册
4. 在 `EAIProvider` 枚举添加新值

### 修改Bounds→Slot计算逻辑

修改 `LayoutCalculator.cpp` 中的 `CalculateSlot` 函数，注意：
- 保持 Hysteresis 算法防止锚点跳变
- 边缘吸附阈值在 `FLayoutPreset` 中配置
- 修改后运行 `UMGExtractorTest::BatchTest` 验证

### 添加新的Fixer规则

在 `LayoutFixer.cpp` 中添加，遵循：
- 检查 `Meta.bFixExempt` 跳过豁免节点
- 检查 `Meta.Confidence < 0.8f` 跳过低置信度
- 记录修正日志到 `FFixerLogEntry`
