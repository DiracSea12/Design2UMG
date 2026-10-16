# Design2UMG 贡献指南

English canonical: [CONTRIBUTING.md](CONTRIBUTING.md)

感谢你贡献 Design2UMG。

## 开发环境

把插件放到以下任一位置：

```text
<UE5_Root>/Engine/Plugins/Marketplace/Design2UMG/
<Project>/Plugins/Design2UMG/
```

要求：

- 适配版本及验证范围见 [README](README.zh-CN.md#安装)。当前维护者 CI 使用 Windows / UE 5.7.1；提交验证结果时记录实际使用的引擎版本。
- 使用所选 UE 版本支持的 Visual Studio 和 Windows SDK 版本。

通过 UnrealBuildTool 构建：

```bat
"<UE5_Root>\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe" <ProjectName>Editor Win64 Development "-Project=<ContributorProject.uproject>" -Module=Design2UMGCore -Module=Design2UMGEditor -UsePrecompiled -NoEngineChanges -WaitMutex -NoHotReloadFromIDE
```

运行自动化测试：

```powershell
pwsh -NoProfile -File Tools/Run-Design2UMGAutomationTests.ps1 `
  -ProjectPath "<ContributorProject.uproject>" `
  -EditorPath "<UE5_Root>/Engine/Binaries/Win64/UnrealEditor.exe" `
  -Tests Design2UMG `
  -RealDesignExportRoot "<FigmaExportRoot>"
```

导出根目录须包含真实设计组件测试所需的 `figma_game_party_home/design.json` 和 `figma_general_data_analytics/design.json`。

这里必须用你自己控制的真实 UE 工程。HostProject fallback 不是贡献者侧，也不是维护者侧的支持验证路径。

## 验证层级

- `repository-hygiene`：公开仓库检查，例如必需文件和不应跟踪的本地文件。
- `self-hosted build`：维护者在自托管 Windows UE runner 上运行的打包/构建 workflow，同时验证 package 边界并上传产物。
- `public smoke/public validate`：贡献者可本地复现的构建和聚焦验证。它适合 smoke check 和问题复现，但不是发布门禁。
- `maintainer formal admissible gate`：维护者拥有的完整 admissible case set 正式发布门禁，见 [Docs/ValidationProtocol.md](Docs/ValidationProtocol.md)。

如果想直接看“private unit tests、editor workflow smoke、focused automation render、maintainer formal gate” 这几层到底怎么分，请看 [Docs/ValidationGovernance/TestCoverageBoundary.zh-CN.md](Docs/ValidationGovernance/TestCoverageBoundary.zh-CN.md)。

package 和 public export 检查比 source workspace 更严格。本地 source workspace 可以保留 ignored 规划文件；发布包和公开导出不能包含 root review 报告、`openspec/`、AGENTS/CLAUDE 本地规则、raw automation tree、cache、HostProject fallback 产物、具体 fallback project 路径或维护者绝对路径。

## 代码风格

- 遵循 Unreal Engine C++ 风格。
- 函数保持聚焦、小而清楚。
- 不做没必要的抽象。
- 不把死代码注释掉，应该删除死代码。

## 开 PR 前

1. 本地构建插件。
2. 跑你本地能真实复现的贡献者侧检查，例如构建、`Automation RunTests Design2UMG`、示例导入 smoke，或聚焦 automation repro。
3. 如果改了用户可见行为，同步更新文档。
4. 保持提交聚焦、容易 review。
5. 如实说明实际运行的验证层级，不要混淆 `repository-hygiene`、`self-hosted build`、`public smoke/public validate` 和 `maintainer formal admissible gate`。

## PR 内容建议

- 改了什么。
- 为什么改。
- 怎么验证。
- 已知影响或限制。
- 实际跑过哪个验证层级。

## 报 issue 时请带上

- UE 版本。
- 操作系统。
- 复现步骤。
- 预期行为和实际行为。
- 尽量提供最小 `.pen` 或 Figma 示例。

## License

贡献即表示你同意该贡献按 MIT 许可证发布。
