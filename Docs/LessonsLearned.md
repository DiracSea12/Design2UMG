# PenToUMG 开发踩坑复盘

> 个人学习笔记，不上传 GitHub

## 001 — C++ Name Hiding 踩坑：匿名命名空间里不能定义 GetTypeHash

**背景**：给 SVG Path 栅格化器加纹理缓存，需要自定义 hash 函数给 TMap 用。

**坑**：在匿名命名空间里写了 `GetTypeHash(const MyCacheKey&)`，结果函数体内调 `GetTypeHash(float)` 编译报错 — 编译器只看到我定义的那个重载，全局的 `GetTypeHash(float)`、`GetTypeHash(FString)` 全被"藏"起来了。

这是 C++ 的 name hiding：同一作用域里有同名函数，编译器就不往外层找了。

**试了三个方案都不行**：
1. 加 `::` 前缀强制调全局 → `FString` 的 hash 靠 ADL 找的，`::` 会跳过 ADL，又挂了
2. 放到具名命名空间再 `using` 引入 → 模板实例化时类型对不上
3. 最后发现：**函数改个名就行了**。叫 `MakeCacheKey` 而不是 `GetTypeHash`，自然不会遮蔽任何东西

**收获**：
- 绕了三圈高级技巧，最终最简单的方案最好用
- 理论分析说"没问题"不算数，编译器说了算
- UE 的 TMap/TSet 体系重度依赖 ADL 查找 `GetTypeHash`，碰这个名字要小心

## 002 — RF_DuplicateTransient：Blueprint 编译会复制你的 transient 纹理

**背景**：Design2UMG 在运行时光栅化 SVG path 生成 UTexture2D（PenPathRasterizer、ProceduralShapeGenerator、PenRemoteImageImporter），纹理存在 TransientPackage 里。Widget Blueprint 保存后在编辑器里打开，path 节点的图标全变成白块。

**根因**：UE 的 Blueprint 编译流程会调 `StaticDuplicateObjectEx` 复制整个 WidgetTree。`FDuplicateDataWriter::operator<<(UObject*&)` 遍历所有引用的 UObject，如果对象的 Outer 正在被复制，就会连带复制该对象。纹理虽然在 TransientPackage 里（Outer 不会被复制），但复制出来的副本是个空壳——没有 mip 数据、没有 PlatformData，GPU 拿到的是 "contains no miplevels" 的废纹理。

**修复**：在三个创建纹理的地方加 `SetFlags(RF_DuplicateTransient)`：
- `PenPathRasterizer.cpp` — path 节点光栅化纹理
- `ProceduralShapeGenerator.cpp` — 程序化形状纹理（阴影、圆角等）
- `PenRemoteImageImporter.cpp` — 远程图片导入纹理（额外加了 `AddToRoot()` 防 GC + `FlushRenderingCommands()` 确保 GPU 上传完成）

`RF_DuplicateTransient` 让 `FDuplicateDataWriter` 跳过该对象，序列化时写入 nullptr。编译后的 _C 类实例在运行时会重新走正常的纹理创建路径。

**收获**：
- 运行时创建的 UTexture2D 如果被 Widget/Actor 引用，Blueprint 编译一定会尝试复制它
- `RF_DuplicateTransient` 是 UE 官方的"别复制我"标志，专门解决这类问题
- 调试时 NullRHI 模式可以快速验证 miplevels 警告是否消失，不需要等 GPU 渲染

## 003 — UnrealEditor-Cmd + -unattended 空转渲染导致 GPU PageFault（已修复）

**现象**：用 `UnrealEditor-Cmd.exe -unattended -nosplash` 跑 `Design2UMG.Import`，PNG 渲染成功后 8~27 秒 GPU 崩溃（D3D12 PageFault）。而用 `UnrealEditor.exe -log` 跑同样命令从不崩溃。

**之前的错误结论**：以为是机器级别的 D3D12/GPU 驱动问题。实际上完全是插件没有主动退出导致的。

**真正根因**：`-unattended` 模式下编辑器仍然创建完整的 Slate UI 窗口（"项目设置"、"虚幻编辑器"、"消息日志"）和 3D 渲染管线（Nanite/Lumen/VSM/RayTracing）。Import 完成后没有退出逻辑，编辑器进入主循环空转渲染不可见窗口。Cmd 模式下 Viewport 只有 704x164，没有正常的 SwapChain Present 目标，GPU 资源疯狂创建/销毁（Aftermath 显示 Create/Destroy Count: 50,765,328），最终 use-after-free → PageFault。

**关键证据**：
- DRED breadcrumb 显示崩溃时 GPU 正在渲染 `SlateUI Title = 项目设置`，不是 Design2UMG 的 Widget
- 崩溃 #1：frame 1238，VSM PhysicalPagePool 512MB
- 崩溃 #2：frame 589，120 字节 buffer `Was Destroyed: True`
- 对比日志：所有崩溃 = `UnrealEditor-Cmd + -unattended`，所有成功 = `UnrealEditor.exe + -log`

**修复**（`Design2UMGEditorModule.cpp`）：
```cpp
// Design2UMG.Import 命令 lambda 末尾
if (FApp::IsUnattended())
{
    UE_LOG(LogDesign2UMG, Log, TEXT("Unattended mode: requesting engine exit after import"));
    FPlatformMisc::RequestExit(false);
}
```
加了 `#include "Misc/App.h"`。

**验证**：用之前崩溃的完全相同命令行，exit code 0，PNG 正常生成（4865x2781），无 GPU 崩溃。

**收获**：
- `-unattended` ≠ `-nullrhi`，Cmd 模式仍初始化完整 GPU 渲染管线
- 命令行批处理任务完成后必须主动 `RequestExit`，不能指望编辑器自行关闭
- 对比成功/失败日志的命令行参数差异是定位此类问题的最快路径
- 不要轻易下"机器/驱动问题"的结论，先排查自己的代码

## 004 — FitContent 节点在 CanvasPanel 中塌缩为零尺寸（81.6% → 13.6%）

**现象**：pencil-welcome.pen 转 UMG 后，103 个顶层组件中 84 个（81.6%）渲染为零宽高。按钮、卡片、导航栏全部不可见，PNG 可见像素比仅 10.5%。

**根因**：五个独立问题叠加导致：

1. **bAutoSize 是单 bool，无法按轴控制**（US-001）
   UMG 的 `UCanvasPanelSlot::bAutoSize` 同时影响两个轴。`SConstraintCanvas.cpp:251` 做 `AutoSize ? GetDesiredSize() : SlotSize`，没有 per-axis 分支。当 width=FitContent + height=Fixed(40) 时，bAutoSize=true 让两个轴都走 DesiredSize，Fixed 的 40px 被丢弃。

2. **SizeBox 包裹只对 Vertical/Horizontal 子节点生效**（US-002）
   `bNeedsSizeBox` 条件要求 `Node->Layout == Vertical || Horizontal`，CanvasPanel（Layout=None）的子节点永远不会被 SizeBox 包裹，Fixed 轴值无法通过 WidthOverride/HeightOverride 传递。

3. **Spacer 注入让 FitContent 容器塌缩**（US-003）
   JustifyContent=Center 会在 HorizontalBox 前后各插一个 Spacer(Fill)。Spacer 的 DesiredSize 是 (0,0)，HBox 的 DesiredSize.X = sum(children desired) = 0+TextBlock+0 ≈ TextBlock 宽度，但 bAutoSize 拿到的是包含 Spacer 的 DesiredSize，实际布局被 Spacer 的 Fill 权重挤压。

4. **FitContent 节点 Value=0，没有回退尺寸**（US-004）
   .pen 格式中 `fit_content` 不带数字时 Value=0。CanvasPanel slot 的 Offsets.Right/Bottom=0，SizeBox 的 MinDesiredWidth=0，节点没有任何尺寸信息。

5. **fit_content(N) 的 N 值未被利用**（US-005）
   PenParser 已经能解析 `fit_content(100)` → Value=100，但 Canvas 分支的 bAutoSize=true 让 slot Offsets 被忽略，SizeBox 又不包裹 Canvas 子节点，N 值白白浪费。

**修复**：

| Story | 文件 | 改动 |
|-------|------|------|
| US-001 | PenLayoutMapper.cpp | CanvasPanel 分支：混合轴时 bAutoSize=true + 保留 Fixed 轴 Offsets 值 |
| US-002 | Design2UMGConverter.cpp | bNeedsSizeBox 去掉 layout 限制，加 bCanvasMixedAxis 条件 |
| US-003 | PenLayoutMapper.h/cpp + Design2UMGConverter.cpp | ApplyJustifyContent 加 MainAxisMode+ParentLayout 参数，FitContent+CanvasPanel 跳过 Spacer |
| US-004 | Design2UMGConverter.cpp | 加 EstimateContentSize() 递归估算 + PreComputeFitContentSizes() 全局预处理 |
| US-005 | 无新代码 | US-001~004 修复后管线自动生效 |

**验证结果**：
- 编译：0 errors
- 103 个顶层节点中 82 个被 SizeBox 包裹（100% 带有效 MinDesired/Override 约束）
- 有效尺寸节点：89/103（86.4%），从 18.4% 提升到 86.4%
- 剩余 14 个零尺寸节点为纯容器/装饰节点，无实际内容子节点
- PNG 输出：4865x2781，655KB

**收获**：
- UMG 的 bAutoSize 是个全有或全无的开关，混合轴场景必须用 SizeBox 做桥接
- 五个独立 bug 叠加时，单独修任何一个都看不到效果，必须全部修完才能验证
- 递归估算内容尺寸（EstimateContentSize）比依赖外部 layout snapshot 更可靠，因为不需要额外的数据源
- Spacer(Fill) 的 DesiredSize=0 是 UMG 的设计意图，但在 FitContent 容器中会产生反直觉的塌缩效果
