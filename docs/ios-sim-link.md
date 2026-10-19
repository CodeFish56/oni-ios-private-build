# iOS 模拟核心接线：能力受限的本地 C++ 内核

## 当前边界

`scripts/integrate_ios_sim_core.py` 将仓库 `native/sim` 与 `native/abi` 的源码集复制到解压后的 `Libraries/OniSimCore`，把 16 个模拟模块加入 **UnityFramework** 的 Sources，共 35 个源码、头文件及 ABI 文件。无需重新上传 1.66 GB 的 Unity 导出归档。

这一步提供静态链接与错误传播机制。它不代表已经具备创建殖民地或持续运行游戏的能力。启动游戏前必须同时具备：

| 能力位 | 含义 |
| --- | --- |
| `1 << 16` | 世界生成后的稳定模拟 |
| `1 << 17` | 完整游戏模拟 |
| `1 << 18` | 游戏渲染数据输出 |

三者缺少任意一项，`Sim.SIM_Initialize` 在转换回调委托前抛出托管 `PlatformNotSupportedException`，说明内核已经链接但尚不能创建或载入殖民地。现有世界生成初始化失败处理应捕获这一类型，显示错误并返回菜单。不能将空指针、全零输出或未完成的步骤当成成功的世界模拟。

## 实际 ABI 证据

依据 Build U59-744825-V 的生成文件 `Assembly-CSharp__122.cpp`：

| 导入 | iOS 原生签名 |
| --- | --- |
| `SIM_Initialize` | `void(Il2CppMethodPointer)`；传入已转换的托管回调 |
| `SIM_Shutdown` | `void()` |
| `SIM_HandleMessage` | `intptr_t(int32_t id, int32_t length, uint8_t* data)` |
| `SIM_HandleMessages` | `intptr_t(int32_t id, int32_t length, int32_t count, uint8_t* data)` |
| `SIM_BeginSave` | `uint8_t*(int32_t* size, int32_t x, int32_t y)` |
| `SIM_EndSave` | `void()` |
| `SIM_DebugCrash` | `void()` |

托管委托 `GAME_MessageHandler` 返回 `int32_t`，参数为 `int32_t message_id, intptr_t data`。C ABI 的 `OniSim_GameMessageHandler` 使用 `int32_t(*)(int32_t, void*)`；在 iOS ARM64 上，`intptr_t` 与数据指针具有相同宽度，回调必须采用该平台的默认 C 调用约定。

这 7 个导入均没有 bool 参数。另一个尚未接入的 `ConduitTemperatureManager_Add` 在 `Assembly-CSharp__31.cpp` 中将最后一个托管 bool 参数转换成 **int32_t**，参数大小为 4 字节。不能直接用 C++ bool 替代其原生形参。

## 修改范围与错误传播

脚本只替换上述 7 个 wrapper 的函数体，调用带 `OniSim_` 前缀的静态 C ABI。没有定义 `FORCE_PINVOKE_SimDLL_INTERNAL`：同名 DLL 还承载 profiler、SYSINFO 与 conduit temperature 导入，全局设置会把未实现的符号一并变成链接依赖。

每个内核 C ABI 导出使用 `noexcept`，内部错误由 `OniSim_GetLastStatus` 与 `OniSim_GetLastError` 返回。适配 wrapper 在导出返回后检查状态，再抛出托管异常。C++ 异常不应越过 C ABI；托管异常也不能从 callback 穿过内核。保存缓冲区还必须满足非空指针、非空 size 指针和正长度条件。

SHA-256 计划 `scripts/ios-sim-core-plan.json` 约束生成 C++ 与 Xcode project 的输入/输出。源码版本或目标结构不吻合时，脚本在写入前拒绝执行。重复运行已知输出时不重复添加 Xcode 对象。构建报告记录实际复制源码的逐文件 SHA-256。

16 个生产文件按 C++17 编译、开启 C++ 异常、关闭浮点乘加收缩，仅对这些文件添加 `Libraries/OniSimCore/abi` 头文件路径。Probe 测试程序不会编入应用。当前能力为 `3735551`，不包含完整游戏要求的 `458752`；新增部分相变和液体置换不会绕过能力检查。

## 使用方式

在已有 FMOD、启动、横屏、世界生成失败处理补丁之后运行：

```shell
python3 scripts/integrate_ios_sim_core.py ios-xcode-744825 \
  --report sim-core-link-check.json
```

只验证计划和输入、不修改导出工程：

```shell
python3 scripts/integrate_ios_sim_core.py ios-xcode-744825 --check-only
```

## 开放真实游戏前仍需验证

- 完成世界稳定模拟、连续游戏模拟和渲染数据输出，并用原版采集记录核对。
- AOT 回调：恢复修复脚本已为 `Sim.DLL_MessageHandler` 补上 iOS 专用的 `AOT.MonoPInvokeCallback` 标记。当前归档早于该修改，其 reverse P/Invoke 表仍没有该回调；未来重新导出时须确认生成表。当前脚本未解决现有归档中的回调缺失，能力检查位于委托转换之前。
- 所有仍使用 SimDLL 的其他系统按各自 ABI 分别接入，不以全局宏替代实现。
- 在 macOS/iPhone 上验证真实 ARM64 编译、内存使用与生命周期；Windows/WSL 的源代码检查不能替代真机运行证据。

本阶段不触发 GitHub Actions，也不安装应用。只有本地验证完成并明确需要设备证据时再安排构建。

## 本机接线验证（2026-10-15）

- 从实际四分卷归档抽取受影响文件，按 Actions 顺序执行 FMOD、横屏、启动、世界生成失败处理和内核接线。全部通过；重复运行无修改；未知生成源码在写入前被拒绝。
- 使用实际 IL2CPP 头文件编译整个 `Assembly-CSharp__122.cpp`，链接真实 `OniSimCore` 静态库。探针执行真实 wrapper，确认能力不足时尚未转换委托、世界生成错误边界能收到异常；未知消息和无效存档输出转为托管异常，Shutdown 正常。
- 内核 C ABI 的 11 个原版案例对照和 ASan/UBSan 检查通过，见 [数据验证](native-sim-verification.json)。此处仅模拟托管运行时异常边界，没有把 Linux 探针称为 iOS 真机运行。
