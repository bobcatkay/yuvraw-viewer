# 纹理上传与渲染回调

在修改纹理上传/复用、stride、格式着色器或 ImGui 的自定义 GL 绘制时读取。

本页内容：

- [像素上传与 stride](#像素上传与-stride)
- [着色器契约](#着色器契约)
- [渲染管线要点](#渲染管线要点)
- [常见显示陷阱](#常见显示陷阱)

## 像素上传与 stride

`FTexture::Create(..., int32_t RowLength)` 最后一个参数是**源数据每行的像素数**（不是字节数）。内部用 RAII 的 `FScopedUnpackState` 设置并恢复：

- `GL_UNPACK_ALIGNMENT = 1` —— GL 默认按 4 字节对齐读每一行，单通道 `GL_RED` 纹理在宽度非 4 倍数时会整幅斜切
- `GL_UNPACK_ROW_LENGTH` —— 让 GL 自己跳过行尾 padding，不要在 CPU 侧逐行 memcpy

`RowLength = 该平面每行字节数 / 该平面每像素字节数`，由 `FImageFormatDesc::GetPlaneRowLength()` 算。

**RowLength 在 `FTexture::Create()` 时就定死了**，`UpdateData()`（`glTexSubImage2D`）只是复用它。
所以纹理复用的判断条件是**格式 + 宽 + 高 + stride 四项全等**（`FImageDocument::UpdateTexture()`
与 `FTextureData::UpdateFromImageData()` 各写了一遍，改一处要改两处）。
漏掉 stride 那一项的后果很隐蔽：在属性面板上只改 stride 时宽高格式都没变，
纹理被原样复用，画面按旧 row length 逐行斜切 —— 看起来就像"stride 输入框不生效"。

属性面板的 stride 输入框里**不出现 0**：文档层的 `Params.Stride == 0` 表示紧凑排列，
回填到面板时由 `FPropertyPanel::SetStride()` 换算成具体字节数（8bit 格式即等于宽度）。
改格式或改宽度时 `RefreshStrideForGeometry()` 会重算它，但**只在用户没手填过 padding 时**
（当前值仍等于旧格式/旧宽度的紧凑值）才覆盖。

FShader 在同一已链接程序内缓存 uniform 地址（含 -1），Destroy 或重新编译时清空；编译/链接失败统一释放并归零 GL 句柄。FShaderManager 同时缓存按格式编译失败的结果，避免查看器逐帧重复编译和打印错误；清空管理器缓存后才允许重试。

## 着色器契约

YUV 类格式共用着色器，转换参数全走 uniform：

| uniform | 含义 |
|---|---|
| `uYuvToRgb` (mat3) | 列主序矩阵，由 `FColorTransform::BuildYuvToRgb(标准, 范围, 位深)` 现算，已含 limited range 拉伸 |
| `uYuvOffset` (vec3) | 乘矩阵前减去的偏移，limited 8bit 为 `(16/255, 128/255, 128/255)` |
| `uSampleScale` (float) | 见[位深与采样布局](formats.md#位深与采样布局) |
| `uSwapUV` (int) | NV21/UYVY 为 1 |
| `uChannelMode` (int) | 0=彩色，YUV 下 1/2/3=Y/U/V，RGB 下 1/2/3/4=R/G/B/A |
| `uImageSize` (vec2) | packed / Bayer 着色器用 texelFetch 取整数坐标时需要 |
| `uBayerPattern` (int) | 0=RGGB 1=BGGR 2=GRBG 3=GBRG |

矩阵由 Kr/Kb 现算而非硬编码；BT.601/709/2020 的系数已与公开标准逐位核对。

packed（YUY2/UYVY）与 Bayer 着色器**必须用 `texelFetch`**：线性过滤会把 Y0/U/Y1/V 或相邻 CFA 像素混在一起。

### 片段着色器是拼出来的

`GetFragmentShaderForFormat()` 返回 `std::string`，内容是
**版本声明 + 色彩管线前导块（`GetColorPipelineGLSL()`） + 该格式的取样代码**。
各格式的 body 因此不带 `#version`。Bayer 是唯一例外 —— 它自带完整源码且不接管线。

## 渲染管线要点

图像不走 ImGui 的 `Image()`，而是 `drawList->AddCallback()` 注入自定义 GL 绘制（`FImageViewer::RenderImage`）。回调里必须：

1. 保存/恢复所有动过的 GL 状态（program、3 个纹理单元的绑定、VAO、viewport、scissor、blend）
2. 用 `draw_data->DisplayPos / FramebufferScale / DisplaySize` 换算坐标，**不能用 `io.DisplaySize`**（多视口下窗口移动会漂移）
3. 结束后追加 `drawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr)`
4. **不要在回调里打日志** —— 每帧都会执行
5. `SetMagFilterNearest` 要在 `BindTextures` **之前**调用（它内部会 glBindTexture）
6. VBO 可跨共享 Context 复用，但 **VAO 不共享**。与 ImGui OpenGL 后端一致，在实际绘制回调中创建、配置并释放 VAO，再恢复原 VAO / array buffer；不能在查看器初始化时创建一个 VAO 供所有视口使用，否则面板脱离 Dock 成为独立窗口后图像会消失。

回调 userdata 通过 `AddCallback(fn, &data, sizeof(data))` 让 ImGui 复制，不要传栈上指针。

## 常见显示陷阱

- **stride（行跨距）**：相机 dump 的 YUV 常按 16/32/64 字节对齐，行尾有 padding。全 0 的 YUV 经 BT.601 转换后是纯绿 `(0, 0.53, 0)` —— **图像右侧出现绿色竖条 = stride 没填对**。
- **整数纹理必须配 `usampler2D`**：`GL_R16UI`/`GL_RG16UI` 用 `sampler2D` 采样是未定义行为，多数驱动返回 0。16bit 一律用归一化的 `GL_R16`/`GL_RG16`。
- **单通道纹理不能用 RGB 着色器**：`GL_RED` 采样出来是 `(r,0,0,1)`，会显示成纯红。灰度要把 `.r` 复制到 rgb。

## 按需关联

- 调整平面数据或采样缩放时，读 [格式描述与扩展](formats.md)。
- 修改色彩前导块、HDR 或副视口输出时，读 [色彩管线与 HDR](color-and-hdr.md)。
- 涉及后台纹理交接时，读 [异步任务与生命周期](async-lifecycle.md)；验证 CPU/GPU 一致性见 [构建与验证](build-and-test.md)。
