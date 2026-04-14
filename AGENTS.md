该文件不可被git追踪，但更不可被删除！！
This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 重要指示 (Critical Instructions)

**交流说中文 - 请始终用中文回复用户的所有问题和交流**

## 编码与乱码防护（强制）

- 所有 `.h/.cpp/.cs/.py/.md/.json` 文本文件统一使用 **UTF-8 with BOM**（Windows 环境）。
- 修改含中文文件时，禁止使用会改变编码的方式直接覆写（例如未指定编码的重定向/脚本输出）。
- 出现乱码时，先修复编码再继续功能修改；不允许带乱码提交。
- 提交前做快速自检：`Get-Content <file>` 中文可读；`[System.IO.File]::ReadAllBytes('<file>')[0..2]` 应为 `239 187 191`（即 `EF BB BF`）。

## 项目与磁盘约束（强制）

- **绝对禁止**创建、使用或依赖任何临时 HostProject，尤其是 `C:\Users\Administrator\AppData\Local\Temp\Design2UMG_Host_*`。
- 所有自动化验证、导入、渲染、回归测试，**只能**使用现有真实项目 `G:\UE5\FPSRoguelike\FPSRoguelike.uproject`。
- 如果某个脚本或工具在缺省路径下会回退到 HostProject，视为该方案不可用：停止、改脚本或显式传入真实项目路径，不允许偷偷生成临时工程继续跑。
- 一旦发现新的 `Design2UMG_Host_*` 临时目录或相关 `HostProject.uproject` 残留，必须立即删除，不能留到任务结束再清理。
- 对 Figma visual fidelity 问题，优先优化 **导出工具 / 导出数据 / parser 映射**；不允许重新引入基于参考图、对比图或节点裁图的兜底路径来“刷分”。

## 已踩坑清单（强制避免复犯）

- **红线**：未经用户明确同意，**绝对禁止**新增、扩展、默认保留任何形式的“旧兼容”逻辑。
  这里的“旧兼容”包括但不限于：
  旧 JSON / 旧字段 / 旧路径 / 旧配置 / 旧行为的兼容读取、兼容回填、兼容兜底、双写双读、静默迁移。
  如果发现现存逻辑里含有旧兼容链，默认目标是识别、汇报、等待用户决定是否删除；不能擅自继续维护、加固或扩大覆盖面。

- **红线**：防御式编程默认禁止泛化扩张，但如果是**确定必要**、且经过验证能**有效提升成绩或稳定性**，可以保留。
  前提是：
  必须有明确问题场景，而不是“以防万一”。
  必须能解释为什么这是主路径需要的防御，而不是兼容性或兜底补丁。
  必须优先选择通用规则，不能写成平台特判、样例特判、过拟合分支。
  如果只是为旧行为、旧字段、旧路径兜底，仍然归类为“兼容性”，必须先经用户同意。

- **绝对禁止**修改引擎源码来修插件问题。插件问题只能改插件、脚本、导出链、parser、运行时逻辑。
- **绝对禁止**触碰任何设计稿基准图、参考图、对比图。它们只用于 compare reference；不能裁、不能派生、不能当运行时输入、注释里也不要把它当主流程方案。
- 回归验证不能只看 `passed/failed`。真正的封板标准先看 `no_regression`，要求“和历史基线比一个都不能更差”；视觉阈值没过但 `no_regression` 通过，和“发生回退”不是一回事，不能混为一谈。
- **硬约束**：这里的“最佳”不是指平均分更好，也不是指总分更高；而是**每一个 admissible case** 的正式对比指标都必须达到当前可追溯的**历史最佳**。只要有任意一个 case 还没追平历史最佳，就不能宣称“已做到最佳”。
- **硬约束**：当前正式 admissible gate 是 **47-case discovered set**。`figma_figma-export_source` 和 `pen_pencil-welcome_step-2-frame` 已经重新纳入正式 gate；未经用户明确同意，不能再把它们从 full gate 里排除。
- **硬约束**：成绩判定禁止看“平均更好”或“总体更好”来掩盖单点退步。只要任意一个样例的任意分数相对历史基线变差，就算“成绩变差”，必须继续排查和修复。
- **硬约束**：任何重构、删兼容链、删测试、删旧 JSON、收口 parser / importer / runtime 逻辑，都不得让全量测试成绩下降。
  这里的“成绩”以全量回归对历史基线的 `no_regression` 为准；如果改动导致任何已有基线 case 变差，就必须继续修复，不能以“代码更干净”为理由接受退分。
- **硬约束**：不能通过特判、过拟合、样例定向补丁来换取“不降分”。
  不允许：
  按具体 case 名、节点 id、文件名、导出目录、组件名、历史基准特征写分支。
  不允许针对单个设计稿/单组样例偷偷调整阈值、路径、布局或资源规则。
  如果一个改动无法抽象成通用规则，而只是“为了某几个 case 分数更好看”，就不能做。
- Figma / Pencil 的布局 fidelity 问题，**先判断是不是 parser 责任**。这轮已经踩实：
  `padding` 是否需要运行时承接，必须由 parser/源数据来决定，不能在 `Design2UMGConverter` 里做全局一刀切 broad wrapper。
  Pencil 的 H/V auto-layout 若子项已有显式 `x/y`，说明 padding 已烘入子项坐标，不能再在共用层补一遍。
  Figma 的 H/V auto-layout 是否承接 padding，要结合源侧实例子树/拉伸子项判断，不能靠主流程硬编码平台分支。
- 文本 fidelity 问题，**不能把设计稿里的名义字号直接当成 UMG 运行时真值**。这轮已经踩实：
  Pencil / Figma 参考图和 UE/Slate 的字体度量不同；`source fontSize` 与 `compiled-instance size` 不是一一对应关系。
  不能因为源设计里写了 `fontSize=36`，就新增“固定宽单行文本直接保留声明字号”的 heuristic；这会把 `welcome_fyi` 一类 case 从历史最佳拉坏。
  不能默认启用“按运行时测量动态重算 text fallback scale”的 heuristic；这会成批改变 `Geist / Oxygen / JetBrains Mono / Inter` 等字体的 compiled size，必须先跑完整 formal gate 证明零退步。
- Figma 文本 bounds 问题，**不能对 `TEXT / TEXT_PATH` 一刀切强制偏向 layout bounds**。这轮已经踩实：
  文本框选用 `absoluteBoundingBox` 还是 `absoluteRenderBounds` 会直接改变下游 font fitting。
  对 Figma 文本盲目强制 layout bounds，会把 `figma-export_local/source` 一类 case 拉离历史最佳。
- Pencil MCP 连 VS Code 不是“只要开着 VS Code 就一定能连”。
  必须优先使用最新已安装的 Pencil 扩展版本；这轮实测 `0.6.35` 会被 editor bundle 判为低于最低要求 `0.6.36`。
  VS Code CLI 安装扩展不能用 `Code.exe --install-extension`，要用 `F:\Microsoft VS Code\bin\code.cmd --install-extension ...`。
  Pencil 当前会把 app 注册写到 `~/.pencil/apps/visual_studio_code`，而 `mcp-server` 仍会按 `vscode` 查找；启动 MCP 前必须同步 `visual_studio_code -> vscode` alias 文件，否则会报 `WebSocket not connected to app: vscode`。
  只打开 VS Code 主窗口不够，至少要有一个 `.pen` 文件用 Pencil custom editor 打开，确保 extension host 已启动 websocket / IPC。
  外部 Pencil 扩展内置的 `mcp-server` 是闭源编译产物，不要在仓库里幻想“直接改它本体”；正确做法是修我们自己的接入层、启动参数、alias 同步和自动探测逻辑，把这个缺陷屏蔽掉。
- 正式 full-run 如果使用 external reference root，**每个 formal case 目录都必须同时带 `reference.png` 和 `compare_report.json`**。
  只有 `reference.png` 而没有 baseline compare report 时，run root 自身的 `summary.no_regression` 会变成 unavailable；这会让 formal gate 证据链不完整。
- 自动化脚本里的非零 editor exit code 不能直接等价成“插件 crash”。这轮实测有正常产物 + 正常 `LogExit: Exiting.` 但返回码为 `3221225477` 的情况；先看日志和结果文件是否齐全，再决定是不是实崩。