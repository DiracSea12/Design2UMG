# Design2UMG 代码质量审查报告

> 审查日期：2026-04-24
> 审查范围：Source/ 全量代码，不含资产文件

---

## 总体判断

这是一个有真实工程价值的插件，核心架构思路是对的，但距离"优秀开源项目"还有明显差距。主要问题集中在：样板代码过多、魔法常数泛滥、局部副作用隐蔽、部分结构职责过重。

---

## P1 — 必须修复

### 1. `FDesignLayoutNodeData` 手写 optional，约 300 行样板代码

**位置：** `Source/Design2UMGCore/Public/DesignSchema.h`

`FDesignLayoutNodeData` 用 `bool bHasXxx + T Xxx` 手写了 14 组 optional 字段，每组生成 5 个方法（Get/Set/Clear/Has/GetOr），总计约 300 行。

这是 UE 里 `TOptional<T>` 的完美使用场景，完全没有理由手写。手写版本的风险是 `bHasXxx` 和实际值容易不同步，且代码量是 `TOptional` 方案的 5 倍。

**修复方向：** 用 `TOptional<T>` 替换，删掉约 250 行样板。

---

### 2. `TextMeasurement.cpp` 魔法常数无来源注释

**位置：** `Source/Design2UMGCore/Private/TextMeasurement.cpp`

```cpp
constexpr float DefaultLineHeightMultiplier = 1.285714f;
constexpr float DefaultFontFallbackScale    = 0.75f;
constexpr float FallbackMeasureWidthScale   = 0.60f;
constexpr float FallbackMeasureHeightScale  = 1.5f;
constexpr float WidthHeuristicScale         = 0.55f;
```

这些常数直接决定文本 fidelity，但没有任何注释说明来源。是实测拟合值？是 Slate 内部比例？是某个字体的经验值？完全不知道。UE 版本升级或字体渲染变化后，这些值会静默失效，且无法追溯。

**修复方向：** 每个常数加注释说明推导来源。这是开源项目的基本要求。

---

### 3. `bApplyRuntimeLineHeight` 硬编码 `24.0f`

**位置：** `Source/Design2UMGCore/Private/Design2UMGTextWidgetBuilder.cpp`

```cpp
const bool bApplyRuntimeLineHeight =
    TextMeasurement::ShouldApplyExplicitLineHeight(Node, ResolvedFontSize)
    || (Node->Text.LineHeight > 0.0f
        && Node->Text.FontSize >= 24.0f   // 为什么是 24？
        && !Node->Text.Content.Contains(TEXT("\n"))
        && !TextMeasurement::IsWrappingTextMode(Node));
```

`24.0f` 是硬编码阈值，没有命名常量，没有注释。这是典型的"为了让某几个测试 case 通过而打的补丁"，不是通用规则。

**修复方向：** 提取为命名常量并注释原因，或找到真正的通用判断条件替换它。

---

## P2 — 应当修复

### 4. `CreateEllipseWidget` 和 `Line` 节点修改传入参数

**位置：** `Source/Design2UMGCore/Private/Design2UMGNodeWidgetFactory.cpp`

```cpp
// 函数签名是 const TSharedPtr<FDesignNode>&，但函数体修改了节点内容
Node->Shape.Geometry = FString::Printf(TEXT("M 0,%.2f A ..."), ...);
return PathWidgetBuilder::CreatePathWidget(Node);
```

`const` 修饰的是指针本身，不是指向的对象，所以编译器不报错，但调用方完全无法从签名看出这个副作用。`Line` 节点存在同样问题。

**修复方向：** 函数内部创建局部副本，或将参数改为非 const 并在注释里说明副作用。

---

### 5. `UMGWidgetNodeAccess.cpp` 重复描述符 key

**位置：** `Source/Design2UMGCore/Private/UMGWidgetNodeAccess.cpp`

`MaterialRef` 和 `TexturePath` 各出现两次。`TryGetTypedWidgetStringFromDescriptors` 返回第一个匹配，第二个描述符在第一个条件满足时永远不执行。这是隐藏的优先级逻辑，没有注释说明意图，读者无法判断是 bug 还是故意设计。

**修复方向：** 加注释说明优先级意图，或合并为单一条件。

---

### 6. `FNodeChildPreparation` 职责过重

**位置：** `Source/Design2UMGCore/Private/Design2UMGConverter.cpp`

匿名 namespace 里嵌套了 5 个互相持有引用的结构体（`FConversionState`、`FConversionContext`、`FNodeChildPreparation`、`FNodeChildAppender`、`FNodeConversionOrchestrator`），`FNodeChildPreparation` 内部私有方法超过 10 个，处理了 fill/fit 预解析、flow layout sizing、子节点尺寸汇总等多个职责。

**修复方向：** 将 fill/fit 预解析逻辑提取为独立的命名空间函数。

---

## P3 — 代码整洁

### 7. `bHasWidth` vs `bSourceHasWidth` 语义不清

**位置：** `Source/Design2UMGCore/Public/DesignSchemaFoundation.h`

`FDesignTransformNodeData` 中两个 bool 同时被 `SetWidthValue` 设为 true，但 `bSourceHasWidth` 在其他地方单独使用，语义差异没有任何注释。

### 8. 测试覆盖缺口

- `TextMeasurement` 的魔法常数没有对应回归测试锁定预期值，常数一旦被"优化"就无法发现退步
- `CreateEllipseWidget` / `Line` 的副作用行为没有专项测试

---

## 架构层面评价

### 做得好的

- IR 设计干净：`FDesignNode` 是纯 struct，不带 UObject 反射，保持轻量
- `FDesign2UMGConverter::Convert` 是纯函数，无副作用，可测试
- 属性访问层用描述符表统一管理，扩展性强
- 递归保护（`FRecursionGuard` + 深度限制 512）到位
- 入口处深拷贝保护源树，正确

### 根本性弱点

文本测量层（`TextMeasurement`）是整个插件最脆弱的部分。它依赖大量经验常数，没有理论支撑，没有回归锁定，是一个"靠调参维持"的模块。这在开源项目里是严重的可信度问题——用户无法判断这些数字是否适用于他们的字体和分辨率。

---

## 与"优秀开源项目"的差距

| 维度 | 当前状态 | 优秀开源标准 |
|------|----------|-------------|
| 核心架构 | 合理，IR 设计干净 | 达标 |
| 代码简洁性 | 有约 300 行可删的样板 | 未达标 |
| 魔法常数 | 多处无注释 | 未达标 |
| 副作用透明度 | 有隐式修改传入参数 | 未达标 |
| 测试覆盖 | 结构合理但有缺口 | 基本达标 |
| 关键决策注释 | 缺失 | 未达标 |

**结论：** 这个插件展示了作者有扎实的 UE5 工程能力，IR 设计和转换器架构是亮点，可以作为简历项目。但如果要以"优秀开源项目"的标准发布，至少需要解决全部 P1 问题，否则社区用户会在第一次阅读 `DesignSchema.h` 时就对代码质量产生疑虑。
