### #24 PenPathRasterizer (1H 2M 2L)

| 级别 | 问题 | 位置 |
|------|------|------|
| H-资源 | `Rasterize` 参数 `Outer` 从未使用，纹理始终创建在 TransientPackage 中。要么移除参数避免误导调用方，要么实际用于 `CreateTransient` 的 Outer 参数 | `Rasterize()` 签名 & 实现 |
| M | `MakeCacheKey` 用 `uint32` 哈希做缓存键，不同路径数据可能产生哈希碰撞，导致返回错误纹理。路径数据字符串差异大但哈希空间仅 32 位，实际使用中碰撞概率不可忽略。应改用 `uint64` 或存储完整键做二次校验 | `MakeCacheKey()` / `GPathCache` |
| M | `ScanlineFill` 每条扫描线都对全部边做相交测试并分配临时 `TArray<TPair>`，大路径（边数多 × 高分辨率）时 O(Edges×Height) 开销显著。可用 Active Edge Table (AET) 或至少按 Y 范围预排序/分桶减少无效遍历 | `ScanlineFill()` 内层循环 |
| L | 头文件注释声称支持 `S/s, T/t` 指令但未列出；实现中确实支持了 S/T，头文件注释应补全以保持一致 | `PenPathRasterizer.h` 第 11 行注释 |
| L-API | `ParsePathData` 中 implicit lineto 分支：当 `ActivePath == nullptr` 且遇到非命令字符的数字时，直接 `++P` 只跳过一个字符而非整个数字对，后续循环会把剩余数字字符逐个跳过（每次循环跳1字符），虽然最终结果正确但效率低且语义不清晰，建议改为 `ReadFloat` 跳过完整数字 | `ParsePathData()` 第 188 行 |
