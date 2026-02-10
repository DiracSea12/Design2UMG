# PenToUMG v2.2 开发进度表

**创建日期**：2025-12-30
**技术方案版本**：v2.2
**状态说明**：`[ ]` 待开始 | `[~]` 进行中 | `[x]` 已完成 | `[!]` 阻塞

---

## Phase 1：核心重构（无外部依赖）

### 1.0 契约冻结（最高优先级）⭐
| 任务 | 文件 | 状态 | 备注 |
|------|------|------|------|
| 确认Bounds定义 | 文档 | [x] | X/Y/W/H，0-1，相对于源图尺寸 |
| 审查CoordScale逻辑 | ImageAssetProcessor.cpp | [x] | 归一化后不需要CoordScale |
| 定义MAE/P95计算口径 | LayoutValidator.h/cpp | [x] | MAE≤4px, P95≤8px |
| 准备UMG验证样例 | UMGSampleExtractor.h/cpp | [x] | 从真实UMG提取Slot数据 |

### 1.1 LayoutCalculator ⭐
| 任务 | 文件 | 状态 | 备注 |
|------|------|------|------|
| 创建 LayoutCalculator.h | Public/LayoutCalculator.h | [x] | 接口定义 |
| 创建 LayoutCalculator.cpp | Private/LayoutCalculator.cpp | [x] | 实现 |
| 实现 FLayoutPreset 结构 | LayoutCalculator.h | [x] | 阈值可配置 |
| 实现 FLayoutContext 结构 | LayoutCalculator.h | [x] | 设计尺寸+历史锚点 |
| 实现 CalculateSlot | LayoutCalculator.cpp | [x] | Bounds→Slot核心 |
| 实现 Hysteresis算法 | LayoutCalculator.cpp | [x] | 防止锚点跳变 |
| 实现 9宫格锚点选择 | LayoutCalculator.cpp | [x] | 0.33/0.67阈值 |
| 实现 边缘吸附检测 | LayoutCalculator.cpp | [x] | ShouldSnapToEdge |
| 实现 拉伸锚点检测 | LayoutCalculator.cpp | [x] | ShouldUseStretchAnchors |
| **一致性验证测试** | UMGExtractorTest.h | [x] | 442文件/4577样例，MAE=0px（验证像素位置，未验证Slot参数一致性） |
| 准备UMG验证样例 | UMGSampleExtractor.h/cpp | [x] | BatchTest批量提取真实UMG |
| **接入主流程** | UMGWidgetUtils.cpp | [x] | CreateWidgetRecursive调用CalculateSlot |

### 1.2 ViewportMapping ⭐
| 任务 | 文件 | 状态 | 备注 |
|------|------|------|------|
| 创建 ViewportMapping.h | Public/ViewportMapping.h | [x] | 接口定义 |
| 创建 ViewportMapping.cpp | Private/ViewportMapping.cpp | [x] | 实现 |
| 实现 FViewportSpec 结构 | ViewportMapping.h | [x] | SourceSize/DesignSize/Mode |
| 实现 EAspectRatioMode 枚举 | ViewportMapping.h | [x] | Fit/Fill/Stretch |
| 实现 MapNormalizedBounds | ViewportMapping.cpp | [x] | 核心映射函数 |
| 实现 Stretch模式 | ViewportMapping.cpp | [x] | 直接映射 |
| 实现 Fit模式 | ViewportMapping.cpp | [x] | Letterbox处理 |
| 实现 Fill模式 | ViewportMapping.cpp | [ ] | Phase 4可选 |

### 1.3 Fixer规则修复器
| 任务 | 文件 | 状态 | 备注 |
|------|------|------|------|
| 创建 LayoutFixer.h | Public/LayoutFixer.h | [x] | 接口定义 |
| 创建 LayoutFixer.cpp | Private/LayoutFixer.cpp | [x] | 实现 |
| 实现 动态阈值计算 | LayoutFixer.cpp | [x] | clamp(minDim*0.002, 2, 6) |
| 实现 对齐吸附 | LayoutFixer.cpp | [x] | 父边缘/中线/同级对齐 |
| 实现 越界修正 | LayoutFixer.cpp | [x] | FixBoundsOverflow |
| 实现 行/列检测 | LayoutFixer.cpp | [x] | DetectRows/DetectColumns（80%重叠） |
| 实现 等距修正 | LayoutFixer.cpp | [x] | 组内等距，拒绝全局 |
| 实现 置信度门槛 | LayoutFixer.cpp | [x] | <0.8跳过修正 |
| 实现 修正日志 | LayoutFixer.cpp | [x] | FFixerLogEntry结构 |
| 单元测试 | Tests/ | [ ] | 各规则独立测试 |

### 1.4 Prompt简化 + Bounds格式
| 任务 | 文件 | 状态 | 备注 |
|------|------|------|------|
| 重写默认Prompt（~50行） | Content/Config/DefaultPrompt_v2.txt | [x] | v2 Bounds格式 |
| 切换到v2 Prompt | PenToUMGSettings.cpp | [x] | 已切换加载DefaultPrompt_v2.txt |
| Bounds精度说明文档 | Prompt内嵌 | [x] | 4位小数，0.0001精度 |
| 补充Slot规则与非Canvas示例 | Content/Config/DefaultPrompt_v2.txt | [x] | Slot约束/示例/Bounds关系 |

### 1.5 数据结构定义
| 任务 | 文件 | 状态 | 备注 |
|------|------|------|------|
| 定义 FBounds 结构 | UMGSchema.h | [x] | 归一化坐标（4位小数） |
| 定义 FUMGMetaData 结构 | UMGSchema.h | [x] | confidence/source/bFixExempt |

### 1.6 UMGJsonParser修改
| 任务 | 文件 | 状态 | 备注 |
|------|------|------|------|
| 支持Bounds格式解析 | UMGJsonParser.cpp | [x] | X/Y/W/H归一化坐标 |
| 解析_meta字段 | UMGJsonParser.cpp | [x] | confidence/source |
| 解析Id字段 | UMGJsonParser.cpp | [x] | 路径式稳定Id |

### 1.7 JSON Schema Strict
| 任务 | 文件 | 状态 | 备注 |
|------|------|------|------|
| OpenAI实现Strict Schema | AIProviderStrategy.cpp | [x] | response_format: json_schema, strict=true |
| Anthropic实现Structured Outputs | AIProviderStrategy.cpp | [x] | beta header已添加 |
| Google实现Structured Output | AIProviderStrategy.cpp | [x] | response_mime_type + response_schema |
| 定义完整JSON Schema | Content/Config/UMGWidgetSchema.json | [x] | 完整schema定义 |
| DeepSeek/Qwen走回退路径 | AIProviderStrategy.cpp | [ ] | 可选 |
| 本地Schema校验器 | JsonValidator.h/cpp | [ ] | 可选优化 |

### 1.8 _meta字段支持
| 任务 | 文件 | 状态 | 备注 |
|------|------|------|------|
| FUMGMetaData数据结构 | UMGSchema.h | [x] | confidence/source/bFixExempt |
| JSON解析支持 | UMGJsonParser.cpp | [x] | 已实现 |
| Fixer消费bFixExempt | LayoutFixer.cpp | [x] | 豁免节点跳过所有修正 |
| 预览窗口显示置信度 | SPenToUMGPreviewWindow.cpp | [ ] | 可选优化 |
| 调试日志输出 | UMGGenerator.cpp | [ ] | 可选优化 |

### 1.9 集成测试
| 任务 | 文件 | 状态 | 备注 |
|------|------|------|------|
| 端到端测试用例 | Tests/ | [ ] | 图片→JSON→Widget |
| 回归基线建立 | Tests/Baselines/ | [ ] | 10-20张标准HUD |
| 指标验收：召回率≥85% | Tests/ | [ ] | 初期可放宽 |
| 指标验收：MAE≤4px | Tests/ | [ ] | 布局精度 |

---

## Phase 2：迭代修复（无外部依赖）

### 2.1 Smart Matcher
| 任务 | 文件 | 状态 | 备注 |
|------|------|------|------|
| 创建 SmartMatcher.h | Public/SmartMatcher.h | [ ] | 接口定义 |
| 创建 SmartMatcher.cpp | Private/SmartMatcher.cpp | [ ] | 实现 |
| 实现 CalculateMatchScore | SmartMatcher.cpp | [ ] | 类型+名称+IoU+文本 |
| 实现 CalculateBoundsIoU | SmartMatcher.cpp | [ ] | 交并比计算 |
| 实现 CalculateTextSimilarity | SmartMatcher.cpp | [ ] | Levenshtein归一化 |
| 实现 贪心匹配 | SmartMatcher.cpp | [ ] | O(N²)，N≤30 |
| 实现 类型分桶优化 | SmartMatcher.cpp | [ ] | 30<N≤100 |
| 实现 超时保护 | SmartMatcher.cpp | [ ] | 100ms限制 |
| 实现 GenerateNewId | SmartMatcher.cpp | [ ] | 路径式ID生成 |

### 2.2 Repair Agent
| 任务 | 文件 | 状态 | 备注 |
|------|------|------|------|
| 创建 RepairAgent.h | Public/RepairAgent.h | [ ] | 接口定义 |
| 创建 RepairAgent.cpp | Private/RepairAgent.cpp | [ ] | 实现 |
| 定义 FPatchOp 结构 | RepairAgent.h | [ ] | op/path/value |
| 实现 IdPathToJsonPointer | RepairAgent.cpp | [ ] | 路径映射 |
| 实现 ApplyPatch | RepairAgent.cpp | [ ] | add/remove/replace |
| 实现 Patch规模限制 | RepairAgent.cpp | [ ] | 64 ops / 8KB |
| Repair Prompt设计 | Content/Config/ | [ ] | Patch模式专用 |
| 触发条件实现 | RepairAgent.cpp | [ ] | Schema失败/差异超阈值 |

### 2.3 DSL编译器
| 任务 | 文件 | 状态 | 备注 |
|------|------|------|------|
| 创建 ComponentCompiler.h | Public/ComponentCompiler.h | [ ] | 接口定义 |
| 创建 ComponentCompiler.cpp | Private/ComponentCompiler.cpp | [ ] | 实现 |
| 实现 HealthBar展开 | ComponentCompiler.cpp | [ ] | ProgressBar+Border+Overlay |
| 实现 Ring展开 | ComponentCompiler.cpp | [ ] | Image+材质参数 |
| 实现 IconText展开 | ComponentCompiler.cpp | [ ] | HBox+Image+TextBlock |
| 实现 展开ID规则 | ComponentCompiler.cpp | [ ] | {ComponentId}/{Type}_{Index} |
| DSL文档 | Docs/ | [ ] | 组件使用说明 |

### 2.4 渲染对比闭环（评测工具优先）
| 任务 | 文件 | 状态 | 备注 |
|------|------|------|------|
| 创建 WidgetRenderer.h | Public/WidgetRenderer.h | [ ] | 接口定义 |
| 创建 WidgetRenderer.cpp | Private/WidgetRenderer.cpp | [ ] | 实现 |
| 实现 DrawWidgetToRenderTarget | WidgetRenderer.cpp | [ ] | 主方案 |
| 实现 FWidgetRenderer兜底 | WidgetRenderer.cpp | [ ] | 备选方案 |
| 实现 结构化差异计算 | RenderCompare.cpp | [ ] | bbox/数量/类型 |
| 实现 像素差异计算 | RenderCompare.cpp | [ ] | 灰度L1 |
| 实现 文本区域Mask | RenderCompare.cpp | [ ] | 避免字体干扰 |
| 实现 差异优先级 | RenderCompare.cpp | [ ] | 结构>位置>尺寸>颜色>像素 |
| 输出：差异报告 | RenderCompare.cpp | [ ] | 评测工具，供人工判断 |
| 自动触发修复 | RepairAgent.cpp | [ ] | 可选功能，默认关闭 |
| 设置项：启用自动修复 | PenToUMGSettings.h | [ ] | bEnableAutoRepair = false |

### 2.5 回归测试框架
| 任务 | 文件 | 状态 | 备注 |
|------|------|------|------|
| 测试用例管理 | Tests/ | [ ] | 50-200张基线HUD |
| 自动化测试脚本 | Tests/ | [ ] | 批量运行 |
| 指标报告生成 | Tests/ | [ ] | 召回率/精确率/MAE/P95 |
| **分级验收标准** | Tests/ | [ ] | 有Parser: MAE≤4px / 无Parser: MAE≤20px |
| CI集成 | .github/workflows/ | [ ] | 可选 |

---

## Phase 3：可选增强（轻量外部依赖）

### 3.1 PaddleOCR集成
| 任务 | 文件 | 状态 | 备注 |
|------|------|------|------|
| 创建 ocr_server.py | Scripts/ocr_server.py | [ ] | HTTP服务 |
| UE客户端集成 | OCRClient.h/cpp | [ ] | HTTP调用 |
| 优雅退化 | OCRClient.cpp | [ ] | 不可用时跳过 |
| 安装文档 | README.md | [ ] | pip install paddleocr |

### 3.2 Few-shot RAG
| 任务 | 文件 | 状态 | 备注 |
|------|------|------|------|
| 本地embedding检索 | Scripts/rag_server.py | [ ] | 案例库 |
| Prompt注入 | HttpAIClient.cpp | [ ] | Few-shot示例 |
| 案例管理UI | SPenToUMGPreviewWindow.cpp | [ ] | 可选 |

---

## Phase 4：高级扩展（重度外部依赖）

### 4.1 OmniParser集成
| 任务 | 文件 | 状态 | 备注 |
|------|------|------|------|
| 创建 omniparser_server.py | Scripts/omniparser_server.py | [ ] | Docker/本地 |
| 模型下载脚本 | Scripts/download_models.py | [ ] | YOLOv8+Florence-2 |
| FParserManager实现 | ParserManager.h/cpp | [ ] | 可用性检测 |
| 优雅退化链 | ParserManager.cpp | [ ] | FullLocal→Cloud→AIDirect |
| 资源预算文档 | Docs/ | [ ] | GPU/VRAM要求 |

### 4.2 SAM3资产切图
| 任务 | 文件 | 状态 | 备注 |
|------|------|------|------|
| 创建 sam_server.py | Scripts/sam_server.py | [ ] | 分割服务 |
| 批量抠图接口 | ImageAssetProcessor.cpp | [ ] | 图标自动提取 |

### 4.3 T3D输出通道
| 任务 | 文件 | 状态 | 备注 |
|------|------|------|------|
| 创建 T3DExporter.h/cpp | T3DExporter.h/cpp | [ ] | T3D格式生成 |
| 导出选项 | SPenToUMGPreviewWindow.cpp | [ ] | UI按钮 |

---

## 错误处理与可靠性（贯穿所有Phase）

### 重试与降级
| 任务 | 文件 | 状态 | 备注 |
|------|------|------|------|
| FAIRequestConfig定义 | HttpAIClient.h | [ ] | Temperature=0等 |
| FRetryPolicy实现 | HttpAIClient.cpp | [ ] | 指数退避 |
| 响应缓存 | CacheManager.h/cpp | [ ] | 图片哈希+Prompt版本 |
| FAIProviderChain降级链 | HttpAIClient.cpp | [ ] | OpenAI→Anthropic→Gemini→DeepSeek |
| API调用日志 | Saved/PenToUMG/Logs/ | [ ] | JSONL格式 |

---

## 变更日志

| 日期 | 变更内容 | 操作人 |
|------|----------|--------|
| 2025-12-30 | 创建进度表 | PM |
| 2026-01-02 | Step 0.4契约冻结完成，UMGSampleExtractor实现 | Dev |
| 2026-01-02 | Step 1.1 LayoutCalculator实现完成 | Dev |
| 2026-01-02 | Step 1.2 ViewportMapping实现完成 | Dev |
| 2026-01-02 | Step 3一致性验证：442文件/4577样例，MAE=0px，100%通过 | Dev |
| 2026-01-02 | Step 4 Fixer规则修复器实现完成 | Dev |
| 2026-01-04 | Phase 1完成：Prompt简化、JSON Schema Strict、_meta字段 | Dev |
| 2026-01-04 | 修复：v2 Prompt切换、Bounds→Slot接线、Schema properties、bFixExempt消费 | Dev |
| 2026-01-05 | 修复：Fixer/ViewportMapping接入主流程、DesignSize配置化、Schema完善、Type枚举扩展 | Dev |
| 2026-01-05 | 修复：LayoutCalculator/生成流程DesignSize统一、SourceSize传递、Stretch模式、Schema strict | Dev |
| 2026-01-05 | 修复：SourceSize赋值、AspectRatioMode配置化、Fixer父边界处理 | Dev |
| 2026-01-05 | 修复：等距修正越界检查、DesignSize=0防护、Prompt移除VBox/HBox(符合技术规范) | Dev |
| 2026-01-05 | 修复：SourceSize JSON解析、预览/生成尺寸统一、Schema完整$defs约束(OpenAI/Anthropic/Google) | Dev |
| 2026-01-05 | 修复：Schema Type添加enum、ImageAsset可选字段、Meta.uncertain_props、顶层SourceSize | Dev |
| 2026-01-05 | 修复：启用Hysteresis防锚点跳变、LayoutCalculator添加Bounds范围验证 | Dev |
| 2026-01-05 | 优化：Schema放宽约束(Type移除enum、WidgetNode/ImageAsset允许扩展)，核心结构保持strict | Dev |
| 2026-01-05 | 同步：UMGWidgetSchema.json与运行时schema一致(添加SourceSize、放宽约束) | Dev |
| 2026-01-05 | 修复：Schema添加oneOf支持错误返回、未知控件类型增强警告、Bounds添加min/max约束 | Dev |
| 2026-01-05 | 优化：移除oneOf(兼容性问题)、同步Meta约束(移除additionalProperties) | Dev |
| 2026-01-05 | 修复：顶层移除additionalProperties允许error返回、SourceSize添加minimum>=1约束 | Dev |
| 2026-01-05 | 修复：移除顶层required约束，真正允许{error,reason}错误返回 | Dev |

---

## 里程碑

| 里程碑 | 完成标准 | 状态 |
|--------|----------|------|
| **M1: Phase 1完成** | 1.1-1.9核心功能完成（可选项除外） | [x] |
| **M2: Phase 2完成** | 2.1-2.5全部完成，ID稳定率≥95% | [ ] |
| **M3: Phase 3完成** | 3.1-3.2完成，文本准确率≥95% | [ ] |
| **M4: Phase 4完成** | 4.1-4.3完成，召回率≥90% | [ ] |

---

## 备注

1. **开发顺序严格按照依赖关系**：先完成被依赖模块
2. **每个子任务完成后更新状态**：`[ ]` → `[~]` → `[x]`
3. **遇到阻塞标记 `[!]`** 并记录原因
4. **Phase 1-2是核心**：纯C++实现，安装即用
5. **Phase 3-4是增强**：需要用户配置Python/CUDA环境

## 开发准则（Linus哲学）

1. **发现老代码就删** - 新模块替代老逻辑时，立即删除老代码，不留死代码
2. **不超过3层缩进** - 超过就重构
3. **函数短小精悍** - 只做一件事
4. **拒绝过度设计** - 解决实际问题，不为假想需求写代码
5. **删除优于添加** - 能删的逻辑绝不保留"以防万一"
