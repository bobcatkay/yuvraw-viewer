# 色彩管线与 HDR 输出

在修改色彩矩阵、传输函数、原色、曝光、色调映射、超范围高亮或 HDR 呈现时读取。

本页内容：

- [色彩管线：三轴正交](#色彩管线三轴正交)
- [HDR 输出（方案 A）](#hdr-输出方案-a)
- [CPU 统计更新时机](#cpu-统计更新时机)

## 色彩管线：三轴正交

`FDisplaySettings` 把色彩描述拆成三个**互不推断**的轴，对齐 ffmpeg 的三个字段：

| 字段 | 对应 ffmpeg | 作用域 |
|---|---|---|
| `ColorSpace` | `colorspace` | YCbCr→R'G'B' 的 Kr/Kb，**非线性域** |
| `Primaries` | `color_primaries` | 线性域的 3x3 原色转换 |
| `Transfer` | `color_trc` | 码值↔光强的曲线（SDR/BT1886/PQ/HLG/Linear） |

一个 HDR 素材是 `(BT2020, BT2020, PQ)`，普通相机 dump 是 `(BT601, BT601_625, SDR)`。
把三者混为一谈正是"选了 BT.2020 画面还是不对"的根因。

完整链路（`FColorTransform::ApplyPipeline`，GLSL 侧是 `ApplyColorPipeline`）：

```
YCbCr --BuildYuvToRgb--> R'G'B'(非线性)
      --EOTF-->           线性（相对 或 绝对 cd/m²）
      --归一化-->         1.0 = 参考白
      --原色 3x3-->       目标原色
      --曝光--> --色调映射--> --OETF-->  显示编码值
```

三条必须记住的性质：

- **`FColorTransform.h` 与 `FShaders.h` 的前导块逐步一一对应**，常数和分支顺序都得一致。
  改一边不改另一边，屏幕上看到的和导出/直方图/像素探针就会分叉，而这种分叉极难察觉。
- **SDR 素材 + 原色一致 + 无曝光补偿时 `bEnabled == false`，整条链路是纯裁剪**，
  与引入管线之前逐位一致。绝大多数打开的文件都走这条直通路径，`TestColorPipeline` 守着它。
- **PQ/HLG 是绝对传输函数**（`IsAbsoluteTransfer()`），解出来直接是 cd/m²；
  SDR/BT1886/Linear 是相对的。两者在归一化那一步走不同分支 —— 这也是为什么
  SDR 素材在 HDR 输出下仍然可以直通。

**参考白默认 203 nit**（BT.2408 的图形白/漫反射白），不是常听到的 100。
那个 100 是 SDR 参考监视器的峰值，ST 2084 本身只定义曲线、不定义漫反射白，
用 100 折算 HDR 素材会过曝约 1 档。HLG 在 Lw=1000 时 75% 信号经 OOTF 后也正好落在 203。

> 改这类默认值时顺手检查一遍测试：`TestImageConfigCache` 靠"把每个字段设成**非默认值**"
> 来验证持久化往返，默认值一旦撞上用例里的取值，那条断言就悄悄失去了区分能力
> （漏存该字段也会读回同一个数）。

**超范围高亮在直通路径上也要生效**，而且判定必须在裁剪之前：limited range 的超白
（Y > 235）与超黑（Y < 16）经矩阵拉伸后就跑出 `[0,1]`，而这类素材走的恰恰是直通路径。
不做这件事的话，最常见的一种"超范围"就永远看不见。

高亮**不改变 `bEnabled`**（否则默认设置也要进线性路径，"直通恒等"那条回归保护就废了），
而是在直通分支里单独判一次；范围内的像素仍然逐位不变，探针的亮度读数也按裁剪后的值算，
不受高亮影响。

批量采样（直方图 25 万点、导出整幅图）**一定要先 `FImageSampler::MakeContext()` 再复用**，
逐点重建矩阵会把只需算一次的事变成实打实的卡顿。

Bayer **刻意不接管线**：CFA 是传感器线性读数，上面既没有 gamma 也没有 PQ，
套一层 EOTF 只会得到没有物理含义的结果。

## HDR 输出（方案 A）

OpenGL 在 Windows 上没有标准途径拿到 HDR 后台缓冲，所以 `FHdrPresenter` 把呈现接管过来：

```
ImGui 整帧 -> fp16 FBO-A -> GL 合成 pass -> 与 D3D11 共享的 fp16 纹理
          -> CopyResource -> flip-model 交换链后台缓冲 -> Present
```

共享靠 `WGL_NV_DX_interop2`（GLAD 不含 WGL 扩展，入口自己 `wglGetProcAddress` 取）。
交换链是 `R16G16B16A16_FLOAT` + `DXGI_SWAP_EFFECT_FLIP_DISCARD` + `SetColorSpace1(scRGB)`。

**FBO-A 的编码约定是整套设计的关键：扩展 sRGB 编码，1.0 = 显示器 SDR 白电平。**

这样 ImGui 一个字节都不用改 —— 它本来就往帧缓冲写 0-1 的 sRGB 值，在这个约定下含义正确。
只有图像内容需要写出 >1 的值（着色器 `uOutputMode=1` 分支）。换成"FBO 直接存 scRGB 线性"
就必须打补丁改 vendored 的 `imgui_impl_opengl3` 着色器，而且 ImGui 的顶点色是 8bit `ImU32`，
根本表达不了 >1。合成 pass 做的就是 `scRGB = ExtendedSrgbDecode(fbo) * (SdrWhiteNits / 80)`。

其余要点：

- 选 scRGB 而不是 HDR10/PQ：广色域可用负分量表示不必 gamut clip；PQ 下界面的 1.0 = 10000nit 会闪瞎眼
- **合成 pass 要翻转 V**：GL 帧缓冲原点在左下，D3D 纹理在左上，`CopyResource` 不翻转
- 共享纹理必须带 `D3D11_RESOURCE_MISC_SHARED`，且挂 FBO / 查完整性都要在 `wglDXLockObjectsNV` 期间做
- **HDR 生效时不能再 `glfwSwapBuffers`**，`FRenderer::EndFrame()` 返回 false 就是这个意思
- 任何一步失败（驱动没有 interop、GL 与 D3D 不在同一块 GPU、Present 失败）都整体退回 SDR，
  `DisableAfterFailure()` 只记一次日志、不重试。**HDR 是纯增量，坏掉不影响 SDR**
- 显示器能力查询在 `FHdrDisplay`（`IDXGIOutput6::GetDesc1` + `DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL`），
  与渲染 API 无关；窗口跨屏拖动要重查，`Update()` 里 500ms 节流
- 链接需要 `d3d11.lib` / `dxgi.lib`

### 三个状态查询各管一件事，别混用

| 查询 | 含义 | 谁在用 |
|---|---|---|
| `IsHdrAvailable()` | 通路就绪 **且** 这块屏处于 HDR 模式 | 属性面板：为假就把"HDR 输出"开关置灰并给出原因 |
| `IsHdrActive()` | 上一条再加上用户没关掉 | 色彩管线：决定 `uOutputMode`；属性面板：决定复选框是否勾选 |
| `IsPresentationActive()` | 本帧是否由 DXGI 交换链呈现 | `BeginFrame` / `FRenderer::EndFrame` |

**呈现是单向门闩的**（`bPresentationLatched`）：交换链成功 Present 过一次之后，
无论用户关掉"HDR 输出"、在系统设置里关掉 HDR、还是把窗口拖到 SDR 副屏，
都继续由 DXGI 呈现，只让色彩管线退回 SDR。在同一个 HWND 上让 DXGI 与 WGL 交替呈现
会闪屏、还可能把 UI 线程卡住。唯一解除门闩的是 `DisableAfterFailure()` ——
那时通路本身已经坏了，退回 WGL 是唯一选择。

于是"关掉 HDR 输出"这条路径（属性面板的复选框 / `--no-hdr`）只换色彩管线、
不动呈现层，同一幅图来回切不会闪屏，唯一的变量就是管线本身 ——
这正是判断"素材的问题还是显示链路的问题"要的。

HDR 不可用时复选框必须置灰且显示为**未勾选**，因此可视状态读 `IsHdrActive()`，不能直接读
`IsUserEnabled()`。后者仍保留用户意愿，窗口回到已开启 HDR 的显示器时自动恢复，不必让用户重勾。

### 副视口必须按 SDR 算，不只是帧缓冲互不影响

fp16 FBO 只挂在**主** GL 上下文上，副视口（面板被拖出主窗口）用的是自己上下文的
8bit 默认帧缓冲。所以 `IssueDrawCallback()` 要判断
`ImGui::GetWindowViewport() == ImGui::GetMainViewport()`，非主视口时传默认的
`FDisplayOutput()`；否则扩展 sRGB 编码值被 8bit 帧缓冲硬 clamp，高光大片死白、
亮度基准还错（用了显示器 SDR 白而不是内容参考白）。

同一处还有个坑：**回调里不能调 `ImGui::GetDrawData()`** —— 它返回的恒为主视口
（`imgui.cpp` 里就是 `g.Viewports[0]`），面板拖出去之后 `DisplayPos` 是错的，
scissor 和渲染位置会整体偏一个视口原点。视口的 `Pos` / `Size` /
`FramebufferScale` 要在 `IssueDrawCallback()` 里存进 userdata。

### 高光限幅保色相

HDR 分支不做 SDR 色调映射，只按显示器峰值限幅，但**不能逐通道 `min()`** ——
`(8, 2, 1)` 在上限 1.86 下会被裁成 `(1.86, 2, 1)`，红反而低于绿，颜色直接翻掉。
`LimitToPeakPreserveHue()`（`FColorTransform.h` 与 `FShaders.h` 各一份）按最大分量
整体缩放：只压亮度，色度不变。负分量随之等比缩放而**不被抬起** ——
抬成 0 等于悄悄做了一次 gamut clip。

`MaxNits` 取的是 `MaxLuminance`（小面积峰值）。`MaxFullFrameLuminance` 也查到了但
没有使用，因为大面积高光的压制交给显示器自己做更合适。

### 原色转换的方向别搞反

BT.709 原色落在 BT.2020 色域**内部**，所以 2020→709 矩阵对角线是 1.66/1.13/1.12、非对角为负，
效果是把坐标**拉开**。不做这一步，BT.2020 素材在普通屏上看起来是**发闷、欠饱和**，不是过饱和。
（过饱和是反过来的场景：BT.709 素材直接丢到广色域屏上。）

## CPU 统计更新时机

- 直方图与差值都在 CPU 侧算，**只在图像/色彩设置变化时算一次**，不要放进每帧路径。大图靠降采样（`kTargetSampleCount`）控制耗时。

## 按需关联

- 修改 uniform、纹理或绘制回调时，读 [纹理与渲染](rendering.md)。
- 确认显示设置所属文档与直方图更新目标时，读 [查看器与输入](viewer-and-input.md)。
- 色彩变更后的必跑测试、分支有效性检查与 fp16 GPU 验证见 [构建与验证](build-and-test.md)。
