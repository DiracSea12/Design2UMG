# Design2UMG 产品打磨计划

**创建日期**：2026-02-17
**背景**：与 Unity 商业竞品（Figma Converter for Unity $99、D.A. Assets Figma→UI Toolkit $49）对比后，识别出的"最后一公里"差距。核心算法（LayoutFixer、9宫格锚点）已领先竞品，瓶颈在产品化体验。

**状态说明**：`[ ]` 待开始 | `[~]` 进行中 | `[x]` 已完成

---

## 差距总览

| 维度 | 当前状态 | Unity 竞品水平 | 差距 |
|------|----------|---------------|------|
| 导入体验 | 需理解 JSON 中间格式 | 填 Token 选 Frame 就完事 | 大 |
| 字体处理 | 仅映射已有字体 | 自动下载 + 生成 TMP 字体 | 大 |
| 图片资产管理 | 基础下载 | 自动切图 + 去重 + 命名规范 | 中 |
| 错误反馈 | UE_LOG 日志级别 | 编辑器内 Toast + 详细引导 | 中 |
| 进度反馈 | 无 | 进度条 + 阶段提示 | 中 |
| 文档教程 | 开发者级文档 | 视频教程 + 示例项目 + FAQ | 大 |
| 增量更新 | 全量重生成 | 部分支持 diff 更新 | 中 |
| 设置体验 | 功能完整但缺引导 | 首次使用向导 | 小 |

---

## P0：导入体验（已有 PRD，补充细节）

> 参考 `Docs/PRD_EasyImport.md`，此处仅补充竞品对标后的额外要求。

### 0.1 首次使用引导

竞品体验：用户安装后第一次打开，弹出 Setup Wizard 引导填写 Figma Token、选择默认输出路径。

| 任务 | 文件 | 状态 |
|------|------|------|
| 检测首次使用（`bFirstRunCompleted` 持久化标记） | Design2UMGSettings.h | [ ] |
| 首次启动弹出 Welcome 对话框（SWindow） | Design2UMGEditorModule.cpp | [ ] |
| Welcome 内容：设计分辨率选择（1920×1080 / 2560×1440 / 自定义） | — | [ ] |
| Welcome 内容：默认输出路径选择（Content Browser 路径选择器） | — | [ ] |
| Welcome 内容：Figma Token 输入（可选，路径 B 用） | — | [ ] |
| "Don't show again" 复选框 | — | [ ] |

### 0.2 导入结果摘要

竞品体验：导入完成后弹出 Summary 对话框，列出生成了什么、跳过了什么、有什么警告。

| 任务 | 文件 | 状态 |
|------|------|------|
| 定义 `FImportResult` 结构体 | PenImporter.h | [ ] |
| 收集统计：生成 Widget 数、跳过节点数、警告数、耗时 | PenImporter.cpp | [ ] |
| 导入完成后弹出 Summary 对话框 | PenImporter.cpp | [ ] |
| Summary 内容：成功项（绿色）、警告项（黄色）、错误项（红色） | — | [ ] |
| 点击警告项可跳转到对应 Widget 或日志 | — | [ ] |

```cpp
// FImportResult 草案
struct FImportResult
{
    FString OutputAssetPath;
    int32 TotalNodes = 0;
    int32 ConvertedWidgets = 0;
    int32 SkippedNodes = 0;
    int32 DownloadedImages = 0;
    int32 FailedImages = 0;
    double ElapsedSeconds = 0.0;
    TArray<FString> Warnings;
    TArray<FString> Errors;
};
```

---

## P1：字体处理

当前只能映射项目中已有的字体资产（`IconFontAssetMap` 配置）。竞品支持自动下载 Google Fonts 并生成引擎字体资产。

### 1.1 字体发现与报告

导入时扫描设计稿用到的所有字体，生成缺失报告。

| 任务 | 文件 | 状态 |
|------|------|------|
| 解析 `FDesignDocument.Fonts[]` 提取所有字体族名 | PenParser.cpp | [ ] |
| 扫描节点树收集实际使用的 `fontFamily` 集合 | Design2UMGConverter.cpp | [ ] |
| 与项目已有字体资产交叉比对（扫描 `/Game/` 下 `UFont` 资产） | FontResolver.cpp（新） | [ ] |
| 生成字体缺失报告，写入 `FImportResult.Warnings` | FontResolver.cpp | [ ] |
| 缺失字体在 Summary 对话框中高亮显示，附带解决建议 | — | [ ] |

### 1.2 字体资产自动生成

从 .pen 文件的 `fonts[].url` 或本地路径自动导入字体文件，生成 UE5 字体资产。

| 任务 | 文件 | 状态 |
|------|------|------|
| 新增 `FFontResolver` 类 | FontResolver.h/cpp | [ ] |
| 实现 `ResolveFont(FontFamily, FontUrl)` → 查找或创建字体资产 | FontResolver.cpp | [ ] |
| 本地字体路径：直接用 `UFontFactory` 导入 .ttf/.otf | FontResolver.cpp | [ ] |
| 远程字体 URL：下载到临时目录后导入（复用 `FPenRemoteImageImporter` 的 HTTP 下载逻辑） | FontResolver.cpp | [ ] |
| 生成的字体资产保存到 `{RemoteImageImportPath}/../Fonts/` | FontResolver.cpp | [ ] |
| 自动更新 `IconFontAssetMap` 缓存（运行时，不持久化到设置） | FontResolver.cpp | [ ] |
| 设置项：`bAutoImportFonts`（默认 true） | Design2UMGSettings.h | [ ] |

### 1.3 字体映射配置 UI

当前 `IconFontAssetMap` 是 TMap<FString, FString>，用户需要手动输入路径字符串。改为资产选择器。

| 任务 | 文件 | 状态 |
|------|------|------|
| `IconFontAssetMap` 值类型从 `FString` 改为 `TSoftObjectPtr<UFont>` | Design2UMGSettings.h | [ ] |
| 向后兼容：旧的字符串路径自动迁移 | Design2UMGSettings.cpp | [ ] |
| 设置面板中显示资产选择器（UE 自动支持 `TSoftObjectPtr` 的 Detail 面板） | — | [ ] |
| 新增 "Scan Project Fonts" 按钮，自动填充已有字体映射 | Design2UMGSettings.cpp | [ ] |

---

## P2：错误反馈与进度提示

### 2.1 编辑器内 Toast 通知

替代纯日志输出，关键事件用 UE5 的 `FNotificationInfo` 弹出 Toast。

| 任务 | 文件 | 状态 |
|------|------|------|
| 新增 `FDesign2UMGNotification` 工具类（封装 Toast 创建） | Design2UMGNotification.h/cpp（新） | [ ] |
| 导入成功：绿色 Toast "✓ Widget Blueprint 已生成: {Name}"，点击跳转到资产 | — | [ ] |
| 导入失败：红色 Toast "✗ 导入失败: {原因}"，点击打开 Output Log | — | [ ] |
| 字体缺失：黄色 Toast "⚠ {N} 个字体未找到，使用默认字体替代" | — | [ ] |
| 图片下载失败：黄色 Toast "⚠ {N} 张图片下载失败" | — | [ ] |
| Pencil 连接成功/失败：对应颜色 Toast | — | [ ] |

```cpp
// 封装草案
namespace FDesign2UMGNotification
{
    void Success(const FText& Message, const FString& AssetPath = TEXT(""));
    void Warning(const FText& Message);
    void Error(const FText& Message);
}
```

### 2.2 进度反馈

大型设计稿导入可能需要数秒，需要进度提示。

| 任务 | 文件 | 状态 |
|------|------|------|
| 导入流程包裹 `FScopedSlowTask` | PenImporter.cpp | [ ] |
| 阶段 1/5：解析设计文件 | — | [ ] |
| 阶段 2/5：展开组件引用 | — | [ ] |
| 阶段 3/5：下载远程图片（显示 N/M） | — | [ ] |
| 阶段 4/5：布局计算与修复 | — | [ ] |
| 阶段 5/5：生成 Widget Blueprint | — | [ ] |

### 2.3 错误消息人性化

当前错误消息面向开发者（如 "Failed to parse JSON"），需要面向用户重写。

| 当前消息 | 改进后 | 文件 |
|----------|--------|------|
| "Failed to parse JSON" | "设计文件格式无效。请确认这是一个有效的 .pen 文件。" | PenParser.cpp |
| "Failed to load file: {path}" | "无法读取文件 {filename}。请检查文件是否存在且未被占用。" | PenParser.cpp |
| "Node type not supported: {type}" | "跳过不支持的节点类型 '{type}'（节点: {name}）" | Design2UMGConverter.cpp |
| "CreatePackage failed" | "无法创建资产包。请检查输出路径 {path} 是否有效。" | UMGGenerator.cpp |
| "Remote image download failed" | "图片 '{name}' 下载失败（URL: {url}）。已使用占位图替代。" | PenRemoteImageImporter.cpp |

| 任务 | 文件 | 状态 |
|------|------|------|
| 梳理所有 `UE_LOG(Error/Warning)` 调用点 | 全局 | [ ] |
| 重写面向用户的错误消息（保留原始技术消息在 Verbose 级别） | 各文件 | [ ] |
| 错误消息支持本地化（`NSLOCTEXT` 或 `LOCTEXT`） | 各文件 | [ ] |

---

## P3：图片资产管理

### 3.1 图片去重

同一张图片在设计稿中多次引用时，只下载和导入一次。

| 任务 | 文件 | 状态 |
|------|------|------|
| 下载前按 URL 去重（`TSet<FString> ProcessedUrls`） | PenRemoteImageImporter.cpp | [ ] |
| 导入前按内容 hash 去重（MD5/SHA1 比对已有资产） | PenRemoteImageImporter.cpp | [ ] |
| 重复图片复用已有资产引用，不重复创建 | PenRemoteImageImporter.cpp | [ ] |

### 3.2 图片命名规范

当前下载的图片以 URL hash 命名，不可读。

| 任务 | 文件 | 状态 |
|------|------|------|
| 命名规则：`T_{WidgetName}_{NodeName}`（如 `T_HUD_AvatarBg`） | PenRemoteImageImporter.cpp | [ ] |
| 名称冲突时自动追加序号（`_01`, `_02`） | PenRemoteImageImporter.cpp | [ ] |
| 设置项：自定义命名模板（`{WidgetName}_{NodeName}` 格式） | Design2UMGSettings.h | [ ] |

### 3.3 图片目录组织

| 任务 | 文件 | 状态 |
|------|------|------|
| 按 Widget Blueprint 名称创建子目录（`ImportedImages/{WBPName}/`） | PenRemoteImageImporter.cpp | [ ] |
| 设置项：图片输出路径模板 | Design2UMGSettings.h | [ ] |

---

## P4：增量更新

当前每次导入全量重新生成 Widget Blueprint，设计稿微调后需要重新导入整个文件。

### 4.1 节点 ID 稳定映射

.pen 文件中每个节点有稳定的 `id` 字段，可用于跨次导入的节点匹配。

| 任务 | 文件 | 状态 |
|------|------|------|
| Widget 创建时将 .pen 节点 `id` 写入 Widget 的 `ToolTip` 或自定义 metadata | UMGWidgetUtils.cpp | [ ] |
| 重新导入时，按 `id` 匹配已有 Widget 和新节点 | IncrementalUpdater.cpp（新） | [ ] |
| 匹配到的 Widget：更新属性，保留用户手动添加的蓝图逻辑 | IncrementalUpdater.cpp | [ ] |
| 未匹配的新节点：新增 Widget | IncrementalUpdater.cpp | [ ] |
| 已删除的节点：标记为 "orphan"，提示用户确认删除 | IncrementalUpdater.cpp | [ ] |

### 4.2 Diff 预览

重新导入前显示变更摘要，让用户确认。

| 任务 | 文件 | 状态 |
|------|------|------|
| 计算 diff：新增 / 修改 / 删除的节点列表 | IncrementalUpdater.cpp | [ ] |
| Diff 预览对话框：左右对比（旧 → 新） | SDesign2UMGDiffWindow.h/cpp（新） | [ ] |
| 用户可逐项勾选接受/拒绝变更 | — | [ ] |
| 设置项：`bEnableIncrementalUpdate`（默认 false，实验性功能） | Design2UMGSettings.h | [ ] |

---

## P5：文档与示例

### 5.1 README 重写

| 任务 | 状态 |
|------|------|
| 顶部 GIF/截图：30 秒展示从 .pen 到 Widget Blueprint 的完整流程 | [ ] |
| Quick Start：5 步上手（安装 → 打开设计稿 → 拖入 UE5 → 预览 → 生成） | [ ] |
| 功能特性列表（带截图） | [ ] |
| 支持的设计属性对照表（.pen 属性 → UMG Widget 属性） | [ ] |
| 已知限制（链接到 KNOWN_LIMITATIONS.md） | [ ] |
| 与竞品的差异化说明（LayoutFixer、9宫格锚点、多设计源） | [ ] |

### 5.2 示例文件

| 任务 | 状态 |
|------|------|
| 创建 `Examples/` 目录 | [ ] |
| 示例 1：`SimpleHUD.pen` — 血条 + 弹药 + 小地图，展示基础布局 | [ ] |
| 示例 2：`LoginScreen.pen` — 输入框 + 按钮 + 背景，展示视觉属性 | [ ] |
| 示例 3：`DesignSystem.pen` — 组件库 + 变量 + 主题，展示高级功能 | [ ] |
| 每个示例附带预期输出截图（`Examples/Expected/`） | [ ] |
| README 中引用示例的导入步骤 | [ ] |

### 5.3 属性映射参考表

用户最常问的问题："我在设计稿里设了 X，为什么 UMG 里没生效？"

| 任务 | 状态 |
|------|------|
| 创建 `Docs/PropertyMapping.md` | [ ] |
| 列出所有支持的 .pen 属性及其 UMG 对应关系 | [ ] |
| 标注部分支持（如渐变只支持双色）和不支持的属性 | [ ] |
| 标注 Figma 属性的支持情况（与 .pen 的差异） | [ ] |

---

## P6：代码质量快速修复

> 不改架构，只修影响用户体验的问题。

### 6.1 JSON 安全访问

| 任务 | 文件 | 状态 |
|------|------|------|
| `GetStringField("version")` → `TryGetStringField` + 默认值 | PenParser.cpp:103 | [ ] |
| 扫描所有 `GetStringField` / `GetArrayField` 调用，替换为 Try 版本 | PenParser.cpp 全局 | [ ] |
| `CreatePackage` 失败时清理已分配资源 | UMGGenerator.cpp:58-63 | [ ] |

### 6.2 日志分级

| 任务 | 文件 | 状态 |
|------|------|------|
| 树形转储日志从 `Log` 降级为 `Verbose` | Design2UMGConverter.cpp | [ ] |
| Fixer 详细日志从 `Log` 降级为 `Verbose` | LayoutFixer.cpp | [ ] |
| 保留 Error/Warning 级别的关键日志 | 全局 | [ ] |
| 新增 `Design2UMG.Verbose` 控制台变量，运行时开关详细日志 | Design2UMGLog.h | [ ] |

---

## 优先级排序与依赖关系

```
P6 代码质量快速修复（无依赖，立即可做）
 │
 ├── P0 导入体验（依赖 PRD_EasyImport 的基础实现）
 │    └── P2 错误反馈（依赖导入流程完善后集成）
 │
 ├── P1 字体处理（独立，可与 P0 并行）
 │
 ├── P3 图片资产管理（独立，可与 P0 并行）
 │
 ├── P5 文档与示例（独立，可随时推进）
 │
 └── P4 增量更新（依赖 P0 完成，复杂度最高，最后做）
```

**建议执行顺序**：

1. **P6** — 修 bug，半天搞定，立竿见影
2. **P5.2** — 做示例文件，帮助自己验证功能，也是最好的文档
3. **P2.1 + P2.2** — Toast 通知 + 进度条，用户感知最强
4. **P1.1** — 字体发现与报告，让用户知道缺什么
5. **P0.2** — 导入结果摘要
6. **P3.1 + P3.2** — 图片去重和命名
7. **P1.2** — 字体自动导入
8. **P0.1** — 首次使用引导
9. **P4** — 增量更新（实验性，开源后根据社区反馈决定）

---

## 验收标准

| 里程碑 | 完成标准 |
|--------|----------|
| **M1: 体验基线** | P6 + P2.1 + P2.2 完成。导入有进度条，成功/失败有 Toast，无崩溃风险 |
| **M2: 字体闭环** | P1 全部完成。设计稿中的字体自动导入或给出明确缺失报告 |
| **M3: 开箱即用** | P0 + P3 + P5 完成。新用户 5 分钟内完成首次导入，无需读源码 |
| **M4: 专业级** | P4 完成。支持增量更新，设计师改一个颜色不需要重新生成整个 Widget |

---

## 与现有 ROADMAP 的关系

本文档聚焦**产品体验打磨**，与 `ROADMAP.md` 互补：

| ROADMAP.md | 本文档 |
|------------|--------|
| Phase 0: 开源准备（版权、CI、文档骨架） | P5: 文档内容填充、示例文件 |
| Phase 1: 性能与质量（Path 缓存、SDF 材质） | P6: 面向用户的质量修复 |
| Phase 2: Prototype Flow（交互逻辑生成） | 不涉及 |
| — | P0: 导入体验细化 |
| — | P1: 字体处理 |
| — | P2: 错误反馈 |
| — | P3: 图片管理 |
| — | P4: 增量更新 |

两份文档可并行推进，无冲突。
