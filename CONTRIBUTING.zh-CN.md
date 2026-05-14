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

- Unreal Engine 5.4+。
- Visual Studio 2022，建议 17.8 或更新。

通过 UnrealBuildTool 构建：

```bat
"<UE5_Root>\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe" <ProjectName>Editor Win64 Development "-Project=<path\to\Project.uproject>" -WaitMutex
```

运行自动化测试：

```bat
"<UE5_Root>\Engine\Binaries\Win64\UnrealEditor.exe" "<path\to\Project.uproject>" -ExecCmds="Automation RunTests Design2UMG" -unattended -nopause -nullrhi -log
```

## 验证层级

- `repository-hygiene`：公开仓库检查，例如必需文件和不应跟踪的本地文件。
- `self-hosted build`：维护者在自托管 Windows UE runner 上运行的打包/构建 workflow，同时验证 package 边界并上传产物。
- `public smoke/public validate`：贡献者可本地复现的构建和聚焦验证。它适合 smoke check 和问题复现，但不是发布门禁。
- `maintainer formal admissible gate`：维护者拥有的完整 47-case 正式发布门禁，见 [Docs/ValidationProtocol.md](Docs/ValidationProtocol.md)。

package 和 public export 检查比 source workspace 更严格。本地 source workspace 可以保留 ignored 规划文件；发布包和公开导出不能包含 root review 报告、`openspec/`、AGENTS/CLAUDE 本地规则、raw automation tree、cache、HostProject fallback 产物、具体 fallback project 路径或维护者绝对路径。

## 代码风格

- 遵循 Unreal Engine C++ 风格。
- 函数保持聚焦、小而清楚。
- 不做没必要的抽象。
- 不把死代码注释掉，应该删除死代码。

## 开 PR 前

1. 本地构建插件。
2. 运行相关自动化测试。
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
