# Architecture

English canonical: [Architecture.md](Architecture.md)

Design2UMG 通过分层、尽量 source-agnostic 的管线，把设计侧文档转换成 Unreal Engine 5 UMG Widget Blueprint。

## 管线

```text
Pencil .pen / Figma JSON
  -> PenParser / FigmaParser
  -> FDesignNode IR
  -> RefExpander
  -> Design2UMGConverter
  -> VisualMapper + LayoutMapper
  -> FUMGWidgetNode IR
  -> LayoutFixer + LayoutCalculator
  -> UMGGenerator
  -> Widget Blueprint
```

## 核心边界

- `PenParser` 和 `FigmaParser` 是解释 source-specific 字段的地方。
- `FDesignNode` 是共享 design IR。解析之后的主链路应基于 IR，而不是直接读 source-specific JSON。
- `FUMGWidgetNode` 是 editor-side Blueprint 生成使用的 generation IR。
- `Design2UMGConverter` 负责 source-to-UMG 转换编排，具体职责由 grid layout、prototype navigation、source metadata、text widget construction 等 helper 分担。

## 设计规则

### Parser 责任留在 parser

如果 fidelity 问题来自源数据语义，应优先修对应 parser，不要在共享层加宽泛 runtime 分支。

### Bounds 是布局投影的事实来源

`LayoutFixer` 调整 bounds-level intent，`LayoutCalculator` 再把最终 bounds 转成 anchors 和 offsets。

### Typed data 是主 contract

生成 widget 数据应优先流经 typed structs，例如 `TextStyle`、`WidgetStyle`、`Text`、`Brush`、`Effect`、`Path`、`Shadow`、`Render`、`WidgetState`、`ScrollBox`，而不是继续扩张 ad hoc legacy property maps。

### 验证属于架构的一部分

正式 release gate 不是“能渲染就行”，而是所有 admissible case 都不能相对可追溯历史最佳退步。重构只有在 full gate 不退步时才可放行。

## 模块概览

| 模块 | 职责 |
| --- | --- |
| `Design2UMGCore` | parser、IR、conversion、layout logic 和 fidelity 相关 shared helper。它不是 runtime module；这里的 Core 表示共享 editor/importer ownership。 |
| `Design2UMGEditor` | import pipeline、Blueprint generation、preview/editor UI、automation rendering、editor-only asset workflow。 |
| `Tools/` | batch verification、historical-best validation、snapshot wrapper 和 release governance helper。 |

## Release 边界

- 公开 release 承诺覆盖 `.pen` import 和 Figma design import，Figma 可走 REST 或本地 export JSON。
- MCP JSON import 等 bridge/debug workflow 仍可用，但不是主要 release contract。
- `AGENTS.md`、`CLAUDE.md`、`.claude/`、`.codex/` 等维护者本地文件应保持 untracked，并从 packaged release output 中排除。

## 验证边界

- 公开文档和 public CI 只承诺 [ValidationProtocol.md](ValidationProtocol.md) 与 [ValidationGovernance/TestCoverageBoundary.zh-CN.md](ValidationGovernance/TestCoverageBoundary.zh-CN.md) 里写清的层级：公开仓库 `repository-hygiene`、贡献者可复现的 smoke/repro、手动 self-hosted BuildPlugin，以及分开的维护者 formal admissible gate。
- 真实项目验证按“角色边界”描述，不按某台机器路径描述。公开示例统一用 `<UE5_Root>`、`<PluginRoot>`、`<MaintainerRuntimeProject.uproject>` 这类占位符。
- HostProject fallback 不是可接受的架构或验证路径。只要 workflow 需要 runtime project，就必须显式使用真实 UE 项目，不能临时创建或偷偷推断 host project。

## Public API 边界

`Source/Design2UMGCore/Public/` 中保留的 public headers 主要覆盖 parser entrypoints、converter entrypoints、settings payloads、schema aggregates、font resolver、notification hooks、text measurement facade、typed widget access 和 layout/value helpers。

`Source/Design2UMGEditor/Public/` 中保留的 public headers 主要覆盖 editor module entrypoint、preview contracts、remote image importer、Figma/MCP/Pencil connector surfaces、preview widget/window，以及 JSON parser/serializer helper。

Phase 21 Batch 1 的审计结论是：当前没有足够硬的证据支持在本批次移动或收窄 header。更安全的做法是记录保留理由、补清楚 API 注释和文档，后续如果要收窄，再以新的 include 证据和验证为准。

## 扩展新设计来源

新增设计来源时，先实现输出 `FDesignDocument` 的 parser，然后在 `DesignSourceRegistry` 注册。通常需要：

1. 在 `Source/Design2UMGCore/Public/*.h` 增加 parser entry point，并在 `Private/*.cpp` 实现。
2. 在 `Source/Design2UMGEditor/Private/Design2UMGEditorDesignSourceRegistry.cpp` 添加 registry lambda。
3. lambda 解析到 `FDesignDocument` 后交给 `DesignImportPipeline::AnalyzeImport(...).Data`，这样 registry 输出仍包含 converter 输出和 import-analysis side data。

## 扩展新 widget 映射

1. 在 `Source/Design2UMGCore/Private/DesignTypeMapper.cpp` 增加 type mapping。
2. 在 `Source/Design2UMGEditor/Private/UMGPropertyApplierInternal.cpp` 的 `GetPropsApplierMap()` 中注册 typed-node handler。
3. 优先复用或增加窄 typed payload / request DTO，而不是扩张字符串 property map。
4. 如果需要新 typed payload，放在 `UMGSchemaTypedPayloads.h`，并沿用现有 typed payload pattern。

## 已知架构边界

`UMGPropertyApplierInternal.cpp` 当前留在 `Design2UMGEditor` 是有意设计。当前支持的产品路径是 editor-side import 和 Widget Blueprint generation，所以最终 `UWidget` mutation 属于 editor generation pipeline，而不是 Core public contract。

只有当支持范围明确增加 runtime module，或者把 `UWidget` mutation pipeline 从 editor-side generation 拆成独立 runtime path 时，才应重新评估这个 placement。
