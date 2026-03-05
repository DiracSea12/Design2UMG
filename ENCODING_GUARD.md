# Design2UMG 编码与乱码防护

## 问题现象
- 中文注释在部分编辑器或日志里显示为乱码。
- 同一文件在不同工具改写后，编码混用（UTF-8 无 BOM / 本地代码页）。

## 统一规则
- `Design2UMG` 插件下所有文本文件（`.h/.cpp/.cs/.py/.md/.json`）统一使用 `UTF-8 with BOM`。
- 修改含中文文件时，禁止使用未指定编码的重定向覆写。

## 快速自检（Windows PowerShell）
```powershell
# 中文是否可读
Get-Content <file>

# 是否 UTF-8 BOM（应输出 239 187 191）
[System.IO.File]::ReadAllBytes('<file>')[0..2]
```

## 已执行修复
- `Source/Design2UMGEditor/Private/FigmaParser.cpp` 已转为 UTF-8 BOM。
- `Source/Design2UMGEditor/Public/FigmaParser.h` 已转为 UTF-8 BOM。

