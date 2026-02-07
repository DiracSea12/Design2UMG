# Cursor 编辑器错误修复指南

## 问题描述

日志中出现的错误：
1. **OpenTelemetry 错误** - 遥测功能尝试发送数据但用户未启用
2. **ZIP 文件错误** - 扩展安装/更新时文件损坏
3. **网络连接错误** - Cursor 尝试连接在线服务时失败

## 自动修复（推荐）

### 步骤 1: 运行清理脚本
```powershell
powershell -ExecutionPolicy Bypass -File "Scripts\fix_cursor_errors.ps1"
```

### 步骤 2: 手动配置用户设置

1. 打开 Cursor 用户设置文件：
   ```
   %APPDATA%\Cursor\User\settings.json
   ```
   或通过 Cursor: `File → Preferences → Settings → 点击右上角 {} 图标`

2. 添加以下配置：
```json
{
  "telemetry.telemetryLevel": "off",
  "extensions.autoUpdate": false,
  "extensions.autoCheckUpdates": false,
  "workbench.enableExperiments": false
}
```

### 步骤 3: 重启 Cursor
完全关闭所有 Cursor 窗口，等待 5 秒，然后重新打开。

## 手动清理（如果自动修复失败）

### 清理扩展缓存
```powershell
# 删除扩展下载缓存
Remove-Item "$env:LOCALAPPDATA\Cursor\CachedExtensions" -Recurse -Force -ErrorAction SilentlyContinue

# 清理所有缓存
Remove-Item "$env:APPDATA\Cursor\Cache\*" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item "$env:LOCALAPPDATA\Cursor\Cache\*" -Recurse -Force -ErrorAction SilentlyContinue
```

### 检查损坏的扩展
如果特定扩展导致问题，可以：
1. 禁用该扩展
2. 重新安装该扩展
3. 检查扩展是否有更新版本

## 验证修复

重启 Cursor 后，检查日志文件：
- 应该不再看到 OpenTelemetry 错误
- ZIP 文件错误应该消失
- 网络错误可能会偶尔出现（如果网络不稳定），但不影响功能

## 注意事项

- 这些错误**不影响**你的项目代码开发
- 禁用遥测不会影响 Cursor 的核心功能
- 禁用扩展自动更新后，需要手动更新扩展（通过扩展面板）

## 如果问题仍然存在

1. 检查 Cursor 版本是否为最新
2. 尝试完全卸载并重新安装 Cursor
3. 检查防火墙/代理设置是否阻止了 Cursor 的网络连接
