# Phase 32B pipeline determinism 退分问题复盘

## 结论

这次 Phase 32B 的问题，不是 parser、布局、文本测量或视觉生成算法把图画坏了。

真正的问题是：formal full 47 验证里，同一个 case 的两次渲染没有做到进程级隔离。

具体说：

- `candidate.bootstrap.png` 来自 generate 阶段。
- `candidate.png` 来自 asset render 阶段。
- 这两次渲染原本在同一个 UnrealEditor 进程里连续执行。
- Unreal 的 Slate / UMG / 字体 / 纹理等运行时状态会在同一个进程里残留。
- 所以某些 case 会出现“第一次画”和“第二次画”不是完全一样的情况。

最终修法是脚本层解决：给 batch 验证新增 `--editor-isolation step`。

开启后，每个 automation spec 单独启动一个 UnrealEditor 进程：

1. generate spec 单独跑，生成 `candidate.bootstrap.png` 和 `ue_generate_result.json`。
2. asset spec 再单独跑，读取上一步落盘的 `generated_asset_path`，生成 `candidate.png`。

这样两次渲染之间不共享 UnrealEditor 进程状态，pipeline determinism blocker 被清掉。

## 问题表现

当前代码在普通 full 47 里，视觉分数本身没有退：

- `summary.no_regression = 47/47`
- `historical_best_regressions = []`

但 `validation_against_historical_best.json` 仍然阻塞：

- `blocked = true`
- `blocked_reasons` 包含：
  - `pipeline_determinism_rule_incompatible`
  - `historical_best_pipeline_determinism_rule_incompatible`

这说明图和历史基线比没有变差，但同一次 run 内部的生成链路不稳定。

也就是：

- 对外看，正式视觉分数没有退。
- 对内看，`candidate.bootstrap.png` 和 `candidate.png` 不一致。
- formal gate 不能接受这种不稳定，因为它说明验证结果可能受执行顺序、缓存冷热、第一次渲染状态影响。

## 关键证据

一次 full 47 当前状态 run：

`G:/UE5/FPSRoguelike/Saved/Design2UMG/Automation/_formal_full_47_phase32B_current_rerun_codex_20260611`

结果：

- `total = 47`
- `editor_exit_code = 0`
- `no_regression = 47/47`
- `historical_best_regressions = []`
- 但 validation 被 pipeline determinism 阻塞

当时的 pipeline blocker 包括：

- `pen_pencil-demo_dashboard-terminal-technical-crisp`
- `pen_pencil-lunaris_lunaris-design-system-components`
- `pen_pencil-welcome_lunaris-design-system-components`

focused run 曾经全绿，但 full 47 仍然红。

这说明问题和完整 batch 的执行顺序、进程内状态、冷热缓存有关；focused 绿不能证明 full 47 稳定。

## 走错的方向

排查中试过几类看起来合理、但不能收的方向。

### 1. 在 C++ 渲染边界做 warmup / flush

调研 Unreal 官方 API、引擎源码和开源项目后，方向看起来合理：

- 先 warm-up 画一帧再丢弃；
- `ForceLayoutPrepass`；
- `FWidgetRenderer::DrawWindow(..., bDeferRenderTargetUpdate=false)`；
- `FlushCommands()` / `FlushRenderingCommands()` 后再读像素；
- 禁用字体异步懒加载。

这些做法符合 UMG / Slate 离屏渲染常见处理方式。

但实测结果不能收。

例如一次尝试清掉了旧的 `terminal` 不稳定点，却把新的 case 拉成不稳定：

- `pen_pencil-demo_habit-tracker-swiss-clean`
- `pen_pencil-demo_habit-tracker-monochrome-type`
- `pen_pencil-shadcn_shadcn-design-system-components`
- `figma_figma-export_source`

其中 `swiss-clean` 的差异很大，说明这不是稳定修复，而是把不稳定转移到另一批 case。

所以 C++ warmup / flush 不是这次最终解法。

### 2. 在 generate 阶段把 bootstrap 改成 asset render 输出

也试过让 generate 命令生成资产后，立刻通过 asset path 重载再渲染，试图让 bootstrap 和 asset render 走同一条路。

这个方向也失败了。

关键证据是 `pen_pencil-demo_habit-tracker-swiss-clean`：

- 旧 run 中，`candidate.bootstrap.png` 和 `candidate.png` 完全一致。
- 尝试后，`candidate.png` 仍然等于旧稳定图。
- 坏掉的是新的 `candidate.bootstrap.png`。

也就是说，新增逻辑不是修好了 asset render，而是把原本稳定的 bootstrap 改成了一个早期不稳定图。

这个方向也不能收。

### 3. focused green 不能当正式结论

多次 focused run 能绿，但 full 47 仍会换一批 pipeline blocker。

原因是 focused run 的执行顺序、case 数量、缓存冷热状态都和 full 47 不一样。

所以这类问题必须以 full 47 为准。

## 根因

根因是 batch 验证的执行隔离不够。

原来的 batch 逻辑会把多个 spec 分到 shard 里，一个 shard 对应一个 UnrealEditor 进程。

在同一个 case 内：

1. generate spec 先执行，生成 `candidate.bootstrap.png`；
2. asset spec 随后执行，生成 `candidate.png`；
3. 两者通常在同一个 UnrealEditor 进程里连续执行。

这样会带来一个问题：第二次渲染不是一个干净的新进程。

它可能继承第一次渲染留下的状态，例如：

- Slate 渲染命令队列；
- 字体加载和字体测量缓存；
- UMG widget / package / asset reload 后的状态；
- 纹理资源更新状态；
- 第一次离屏渲染后的运行时缓存。

这些状态不是业务数据，也不是设计稿内容，但它们会影响离屏渲染读回来的像素。

所以同一个 case 里，`candidate.bootstrap.png` 和 `candidate.png` 可能不一致。

## 最终修复

最终只改脚本层。

改动文件：

- `Tools/design2umg_auto_verify_batch.py`
- `Tools/design2umg_validation_helpers_selftest.py`

新增参数：

```text
--editor-isolation shard|step
```

默认值是：

```text
shard
```

也就是默认不改变原来的 batch 行为。

正式需要排除 pipeline 状态污染时，用：

```text
--editor-isolation step
```

`step` 模式下：

- 每个 spec 单独启动一个 UnrealEditor 进程；
- generate spec 完成后，必须确认 `ue_generate_result.json` 已落盘；
- asset spec 启动前，必须确认上一步结果里有 `generated_asset_path`；
- asset spec 通过 `@generated_asset_from:...` 读取上一步落盘结果；
- 后处理 compare / report 逻辑不变；
- 不改 parser；
- 不改文本测量；
- 不改阈值；
- 不改历史最佳；
- 不改 reference / compare 图片；
- 不改引擎源码。

这次修复的本质是：让验证链路里的两次渲染真的互相独立。

## 验证结果

### focused 8 case

Run root：

`G:/UE5/FPSRoguelike/Saved/Design2UMG/Automation/_focus8_editor_step_isolation_worker_20260612-023504`

结果：

- `summary.total = 8`
- `summary.editor_isolation = step`
- `summary.editor_exit_code = 0`
- `summary.no_regression = 8/8`
- `validation.blocked = false`
- `pipeline_determinism_rule_incompatible_cases = []`
- `historical_best_regressions = []`

### full 47

Run root：

`G:/UE5/FPSRoguelike/Saved/Design2UMG/Automation/_formal_full_47_editor_step_isolation_worker_20260612-052104`

结果：

- `summary.total = 47`
- `summary.editor_isolation = step`
- `summary.editor_exit_code = 0`
- `summary.no_regression = 47/47`
- `summary.blocked_cases = 0`
- `validation.blocked = false`
- `blocked_reasons = []`
- `pipeline_determinism_rule_incompatible_cases = []`
- `historical_best_regressions = []`

这说明：

- 没有视觉回退；
- 没有历史最佳回退；
- 没有 pipeline determinism blocker；
- full 47 正式通过。

## 为什么不是直接改 UMG 渲染代码

因为这次问题不是“某个 widget 应该怎么画”的业务问题，而是“验证时两次渲染共享了同一个进程状态”的测试环境问题。

如果在 C++ 渲染代码里继续加 warmup、flush、字体懒加载开关，风险很高：

- 可能修好一批 case；
- 同时让另一批 case 变成不稳定；
- 还会把验证问题变成运行时行为问题；
- 很难证明对所有 47 个 case 零副作用。

脚本层 step 隔离更直接：

- 不改变插件生成逻辑；
- 不改变渲染算法；
- 不改变评分规则；
- 只改变验证执行方式；
- 问题在哪里，就隔离哪里。

## 以后怎么避免

1. 遇到 `no_regression=47/47` 但 validation 被 pipeline blocker 卡住时，先查 `generation_vs_asset`，不要先改 parser 或文本测量。

2. focused 绿只能说明局部可疑点暂时复现不出来，不能替代 full 47。

3. 对 UMG 离屏渲染问题，优先判断是“渲染代码输出错了”，还是“验证进程状态污染了”。这两类问题不能混在一起修。

4. 会改变 C++ 渲染链路的 warmup、flush、reload、font guard，都必须拿 full 47 证明零退步；focused green 不够。

5. 需要判断 pipeline 稳定性时，优先使用：

```text
--editor-isolation step
```

6. 不要为了清 pipeline blocker 改阈值、改历史最佳、改 reference 图、改 case manifest，也不要写 case 特判。

## 一句话总结

这次退分不是图真的画差了，而是验证时同一个 UnrealEditor 进程连续跑两次渲染，进程内缓存和运行时状态污染了第二次结果。最终通过 batch 脚本的 step 级进程隔离，让 generate 和 asset render 各自用干净 UnrealEditor 进程执行，full 47 恢复到零回退、零 pipeline blocker。
