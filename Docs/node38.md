### #38 Design2UMGTests (2H 3M 1L)

| 级别 | 问题 | 位置 |
|------|------|------|
| H-资源 | Test 12 硬编码绝对路径 `G:/UE5/FPSRoguelike/Designs/pencil-welcome.pen`，在其他机器上必定失败。自动化测试应使用 fixture 机制（如同文件中 `LoadFixtureJson` 的做法）或相对于插件目录的路径 | `FPenWelcomeBatchImportTest::RunTest` L819 |
| H-资源 | Test 12 调用 `GenerateBlueprintAndPng` 向 `/Game/Design2UMG/PencilWelcome` 写入真实资产（Blueprint + PNG），测试结束后无清理逻辑，反复运行会污染项目 Content 目录。应在测试结束时删除生成的资产，或写入临时路径 | `FPenWelcomeBatchImportTest::RunTest` L837 |
| M | Test 6 直接修改全局单例 `UDesign2UMGSettings::Get()` 的字段做 save/modify/restore。若并行执行测试，其他测试会读到被篡改的设置；若中途异常退出（ParseDocument 或 Convert 内部），恢复代码不会执行。应封装为 RAII guard 确保恢复 | `FIconFontNoMappingFallbackTest::RunTest` L348-384 |
| M | Test 3 对 `CalculateSlot` 返回的锚点值使用 `TestEqual` 做浮点精确比较（如 `0.0f`, `0.5f`, `1.0f`）。同文件 Test 8.5 已正确使用 `FMath::IsNearlyEqual`，说明作者意识到浮点问题但未统一。若 `CalculateSlot` 内部算法调整引入微小误差，这些断言会脆性失败。应统一使用 `TestNearlyEqual` 或 `FMath::IsNearlyEqual` | `FLayoutCalculatorTest::RunTest` L242-275 |
| M | `#include "PenImporter.h"` 和 `#include "DesignImportUtils.h"` 位于文件第 810-811 行（两个测试之间），而非文件顶部。C++ 允许但违反常规组织规范，阅读时容易遗漏依赖关系，应移至文件顶部 include 区域 | 文件 L810-811 |
| L | 测试编号不连续：存在 "Test 8.5"（`FFigmaLineHeightMappingTest`），后续测试未重新编号。不影响功能但降低可维护性 | L610 注释 |
