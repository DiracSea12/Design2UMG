# 测试覆盖边界说明

English canonical: [TestCoverageBoundary.md](TestCoverageBoundary.md)

这个文档只回答三件事：

1. 现在仓库有哪些测试层级。
2. 各层级大概覆盖什么。
3. 哪些普通贡献者能跑，哪些必须是维护者环境。

它不是覆盖率宣传页，所以这里不会写虚构百分比，也不会把 public CI 包装成 Unreal 正式门禁。

## 当前 public CI 的真实情况

当前 tracked GitHub Actions workflow 是 `.github/workflows/build.yml`。

它现在只有两类 job，而且边界不同：

| Job | 触发方式 | 真正检查什么 | PR 默认会不会跑 |
| --- | --- | --- | --- |
| `repository-hygiene` | `push`、`pull_request`、`workflow_dispatch` | 必需文件、tracked maintainer-local path 拦截、公开仓库边界脚本 | 会 |
| `build-plugin` | 仅 `workflow_dispatch` | Unreal `BuildPlugin`、打包产物边界清理和复核、artifact 上传 | 不会 |

`build-plugin` 依赖维护者提供的 self-hosted Windows UE runner 和 `UE_ROOT`。它不是 GitHub-hosted 的 PR 自动门禁。

完整 discovered admissible case set formal visual regression gate 目前不在 public GitHub-hosted PR 自动化里。

## 覆盖层级

| 层级 | 覆盖什么 | 典型入口 | 谁现实中能跑 | 不能证明什么 |
| --- | --- | --- | --- | --- |
| `private unit tests` | parser/converter 合同、typed JSON 行为、generator 不变量、workflow helper、结构守卫、import report 行为，以及 `Source/Design2UMGEditor/Private/Tests/` 下的聚焦回归 editor automation 测试 | `Tools/Run-Design2UMGAutomationTests.ps1 -Tests Design2UMG` 或直接 `Automation RunTests Design2UMG...` | 有本地 UE 环境和真实工程的贡献者、维护者 | 不能证明打包产物健康，也不能证明 formal 47-case 零退化（`no_regression`） |
| `editor workflow smoke` | 基本导入链路、示例内容流程、生成的 Widget Blueprint 是否存在、preview PNG 是否存在、明显 editor 侧断裂 | 启用插件，导入一个示例 `.pen`，检查生成的 widget 和 preview PNG | 贡献者和维护者 | 不能证明 automation parity、release 打包或正式视觉质量 |
| `automation render` | 聚焦 asset render、designer screenshot、structure snapshot、import diagnostics，以及单个 case / 单个 bug 的本地复现 | `Design2UMG.AutomationRender`、`Tools/design2umg_auto_verify.py`、`Run-DesignerSnapshotValidation.ps1`、`Run-LunarisStructureSnapshotValidation.ps1` | 贡献者可在显式传 `--project` / `--editor` 时做本地复现；维护者也用同一套工具取更强证据 | 不能因为本地过了一个 case，就自动等于维护者发布门禁 |
| `formal visual regression gate` | 完整 discovered admissible case set 发布门禁、归档级 `summary.no_regression`、historical-best 对比、curated reference-root 证据链 | 见 [ValidationProtocol.md](../ValidationProtocol.md) 中的 maintainer batch validation | 仅维护者 | 它不是 public PR check，不是 coverage badge，也不能写成 GitHub Actions 已自动覆盖 |

## 普通贡献者可以跑什么

普通贡献者可以现实复现的，主要是这些：

1. 正常 push / PR 时的 `repository-hygiene`。
2. 针对自己真实工程的本地 UnrealBuildTool 构建。
3. `Automation RunTests Design2UMG` 或它的聚焦子集。
4. 示例导入 smoke。
5. 显式传 project / editor 路径的 `Design2UMG.AutomationRender` 或 `design2umg_auto_verify.py` 本地复现。

贡献者文档里统一用这些占位符：

- `<UE5_Root>`
- `<PluginRoot>`
- `<ContributorProject.uproject>`

## 哪些必须是维护者环境

下面这些不该写成“普通 PR 已自动覆盖”：

1. GitHub Actions 里的手动 `build-plugin`。
2. self-hosted UE runner 产出的 canonical packaged plugin artifact。
3. 归档级 formal admissible case set release gate 和它的 `summary.no_regression` 证据链。
4. 绑定 maintainer admissible baseline 的 historical-best parity 检查。

公开文档描述这些能力时，应该写“维护者角色需要什么”，不要写某台机器的绝对路径。

## 硬边界

- 不要把 formal admissible gate 写成 public PR 默认检查。
- 不要写仓库里无法复现的 coverage 百分比。
- 不要把 HostProject fallback 当成贡献者验证可接受的替代路径。
- 不要在 public-facing 文档里写维护者绝对路径。
