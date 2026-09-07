# 纹理上传与渲染回调

在修改纹理上传/复用、stride、格式着色器或 ImGui 的自定义 GL 绘制时读取。

本页内容：

- [像素上传与 stride](#像素上传与-stride)
- [GPU 尺寸预检查与失败详情](#gpu-尺寸预检查与失败详情)
- [大图预览与稀疏页面](#大图预览与稀疏页面)
- [着色器契约](#着色器契约)
- [渲染管线要点](#渲染管线要点)
- [常见显示陷阱](#常见显示陷阱)

## 像素上传与 stride

`FTexture::Create(..., int32_t RowLength, FTextureCreateError* OutError)` 的 `RowLength` 是**源数据每行的像素数**（不是字节数），`OutError` 可省略。内部用 RAII 的 `FScopedUnpackState` 设置并恢复：

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

## GPU 尺寸预检查与失败详情

普通整图路径按整图/整平面上传。CPU 解码通过不等于 GPU 可以显示：
`FTextureData::CreateFromImageData()` 在进入普通整图路径后、分配重排缓冲或上传任意平面前，用描述表计算的
实际平面宽高逐一调用 `FTexture::ValidateDimensions()`；独立调用 `FTexture::Create()`
也执行同样的检查。查询当前 Context 的 `GL_MAX_TEXTURE_SIZE`，只拒绝大于上限的边长，
等于上限合法；stride padding 不能被当作纹理宽度。该检查发生在 CPU 解码之后、GPU 上传之前。

`FTextureCreateError` 按次返回尺寸无效、超过 GPU 边长、能力查询失败、图形内存不足或
其它 GL 错误；详情包含纹理宽高、查询到的上限或错误码，文字登记在 `FUiText.inl`。
创建前记录并清理 Context 已有错误，防止把其它 GL 操作的错误误报为本次分配失败；
上传失败删除纹理名称，不能让没有有效存储的对象通过 `IsValid()`。

共享上传失败仍按既有协议交给主 Context 重试；主线程以实际重试结果更新错误，成功清空
旧错误，失败将详情交给文档/属性面板。差值图的同步建纹理入口也必须保留具体原因。
初始化日志记录 GPU renderer 与 `GL_MAX_TEXTURE_SIZE`；失败日志只在创建阶段打印。

## 大图预览与稀疏页面

`FTextureLoadOptions` 控制创建顺序，默认启用稀疏纹理，最长边严格超过 16384 像素时优先尝试稀疏：

| 条件 | 有序尝试，成功即停止 |
|---|---|
| 启用且最长边 ≤ 阈值 | 普通整图 → 稀疏 → 普通分块 |
| 启用且最长边 > 阈值 | 稀疏 → 普通分块 |
| 关闭 | 普通整图 → 普通分块，完全跳过稀疏 |

稀疏路径需要 `GL_ARB_sparse_texture`，采用“常驻预览 + 按可见区域驻留原图页”。
RGB/RGBA、灰度、平面/半平面 YUV 可用；Bayer、打包 YUV、RGB10_A2 不能直接平均，稀疏尝试跳过后继续回退。

- `PrepareSparsePreview()` 是纯 CPU 阶段，不得长期占用上传 Context。优先稀疏的请求在后台无 Context
  时预先生成；其余情况由 `CreateFromImageData()` 仅在实际尝试稀疏时生成，用 RAII 暂时解除
  当前 Context 并在结束/异常时恢复；未超过阈值且普通上传成功时不生成预览。按平面
  对原始分量做 box average，最长边不超过 4096，至少缩小两倍，逐输出行检查取消代际。
  不在预览里烘焙色彩转换。`CreateFromImageData()` 消费这份预览，创建普通预览纹理与稀疏虚拟存储。
- `FSparseTexture` 查询当前 Context 的扩展入口、`GL_MAX_SPARSE_TEXTURE_SIZE_ARB` 及各格式页尺寸，
  对齐虚拟宽高，使用 sized internal format、单个 level 0 的 immutable storage。RGB8/RGB16
  使用 RGBA8/RGBA16 存储，避免依赖可选 RGB sparse 格式。单页最多 4 MiB；只接受至少一个可分页层。
  不支持扩展、格式、尺寸或稀疏创建失败时，继续上述顺序中的普通分块路径。
- `FImageViewer` 将 scissor 可见区反向映射为原图 UV（撤销旋转，再撤销镜像），保守覆盖整数
  scissor 舍入产生的一个 framebuffer 像素；后端再增加一个源 texel 的线性过滤边界。
  `PrepareForDraw()` 在实际绘制 Context 中执行，全部平面先 `Trim()`，再分批 `Update()`。
- 每张图所有高清平面合计最多驻留 256 MiB，每次绘制最多提交/上传 16 MiB，常驻预览另计。
  可见原图区域超预算时回收高清页并显示预览；页面未全部就绪时也统一显示预览，不采样未驻留页。
  无需 `ARB_sparse_texture2` 或更高 GLSL 版本。视口停留在相同页矩形时复用完成状态。
- 页上传保存并恢复 unpack alignment、row length、skip、swap bytes 和 PBO 绑定。
  stride 不能整除 texel 字节数时只重排一页，不创建整平面 scratch。
  `uTextureScale0/1/2` 将逻辑尺寸映射到对齐后的存储，GLSL 钳到实际首末 texel center，避免读到边缘 padding。
  YV12 的 scale 与 sampler 一样按 Y/U/V 语义对应物理平面。
- `FinishDraw()` 在绘制后建立 fence 并 flush，下一次换绘制 Context 时先 `glWaitSync` 再改驻留，
  同一 Context 使用命令顺序。GL 回调内不打印日志、不向外抛异常；失败后保留预览、停止重试，
  下一帧 UI 构建时记录一次原因并显示本地化提示。同步状态留在实现文件，公共头不引入 GLAD。

普通分块由 `FTextureData::FTiledDrawState` 管理：核心边长最多 2048，受实际 `GL_MAX_TEXTURE_SIZE`
约束；每平面保留两纹素 halo，逐块重排上传，完整图像常驻 GPU。绘制只遍历可见核心块，halo 不重复绘制。
`uDrawRegion` 在整图变换中裁出每块，`uTextureScale` / `uTextureOffset` 将全图 UV 映射到块内；
打包 YUV 和 Bayer 仍按全图像素计算奇偶/邻域，仅在 `texelFetch` 前减去 `uTextureOrigin0`。
分块覆盖单纹理边长超限，但不降低整图显存需求，不是按需文件解码或稀疏驻留。

`TextureLoad` 日志进入 `%LOCALAPPDATA%\YUVRaw\Logs\YUVRaw.log`：记录策略、逐次失败原因、最终后端、
预览 CPU 与准备/上传提交耗时、块数/上传字节。加载器完成日志包含 backend、各阶段与端到端时间；
稀疏首次细节就绪的累计上传字节/CPU 提交时间延迟到 UI 构建时仅输出一次。提交时间不等于 GPU 执行时间。

CPU 仍保留整幅解码像素，探针、直方图、比较与导出继续读原始数据；稀疏纹理不绕过
`FImageLimits`，也不是按需文件解码。它减少大图显存压力，不保证首开或帧率一定更快：
额外的预览计算和页面提交也有成本。稀疏或分块图更新必须重建资源，`UpdateFromImageData()` 仅复用普通整图。

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
| `uTextureScale0/1/2` (vec2) | 普通/预览默认 1；稀疏模式为逻辑尺寸 / 对齐存储尺寸，按采样器语义传入 |
| `uTextureOffset0/1/2` (vec2) | 分块的平面原点 / 原平面尺寸，其余默认 0；与 scale 一样按 Y/U/V 语义传入 |
| `uTextureOrigin0` (vec2) | 打包 YUV / Bayer 的块原点，以存储纹素计，其余默认 0 |
| `uDrawRegion` (vec4) | 本次绘制核心块的全图 UV 矩形，普通整图/稀疏为 (0,0,1,1) |
| `uBayerPattern` (int) | 0=RGGB 1=BGGR 2=GRBG 3=GBRG |

矩阵由 Kr/Kb 现算而非硬编码；BT.601/709/2020 的系数已与公开标准逐位核对。

packed（YUY2/UYVY）与 Bayer 着色器**必须用 `texelFetch`**：线性过滤会把 Y0/U/Y1/V 或相邻 CFA 像素混在一起。

### 片段着色器是拼出来的

`GetFragmentShaderForFormat()` 返回 `std::string`，内容是
**版本声明 + 纹理坐标辅助块（`GetTextureSamplingGLSL()`） + 色彩管线前导块（`GetColorPipelineGLSL()`） + 该格式取样代码**。
各格式的 body 因此不带 `#version`。Bayer 是唯一例外 —— 它自带完整源码且不接管线。

## 渲染管线要点

图像不走 ImGui 的 `Image()`，而是 `drawList->AddCallback()` 注入自定义 GL 绘制（`FImageViewer::RenderImage`）。回调里必须：

1. 保存/恢复所有动过的 GL 状态（program、3 个纹理单元的绑定、VAO、viewport、scissor、blend）
2. 用 `draw_data->DisplayPos / FramebufferScale / DisplaySize` 换算坐标，**不能用 `io.DisplaySize`**（多视口下窗口移动会漂移）
3. 结束后追加 `drawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr)`
4. **不要在回调里打日志** —— 每帧都会执行
5. `SetMagFilterNearest` 要在 `BindTextures` **之前**调用（它内部会 glBindTexture）
6. VBO 可跨共享 Context 复用，但 **VAO 不共享**。与 ImGui OpenGL 后端一致，在实际绘制回调中创建、配置并释放 VAO，再恢复原 VAO / array buffer；不能在查看器初始化时创建一个 VAO 供所有视口使用，否则面板脱离 Dock 成为独立窗口后图像会消失。
7. 先 `PrepareForDraw`，分块路径由 `GetDrawRegion` 剔除不可见块；逐块绑定并更新采样坐标后绘制，最后 `FinishDraw` 同步稀疏页使用

回调 userdata 通过 `AddCallback(fn, &data, sizeof(data))` 让 ImGui 复制，不要传栈上指针。

## 常见显示陷阱

- **stride（行跨距）**：相机 dump 的 YUV 常按 16/32/64 字节对齐，行尾有 padding。全 0 的 YUV 经 BT.601 转换后是纯绿 `(0, 0.53, 0)` —— **图像右侧出现绿色竖条 = stride 没填对**。
- **整数纹理必须配 `usampler2D`**：`GL_R16UI`/`GL_RG16UI` 用 `sampler2D` 采样是未定义行为，多数驱动返回 0。16bit 一律用归一化的 `GL_R16`/`GL_RG16`。
- **单通道纹理不能用 RGB 着色器**：`GL_RED` 采样出来是 `(r,0,0,1)`，会显示成纯红。灰度要把 `.r` 复制到 rgb。

## 按需关联

- 调整平面数据或采样缩放时，读 [格式描述与扩展](formats.md)。
- 修改色彩前导块、HDR 或副视口输出时，读 [色彩管线与 HDR](color-and-hdr.md)。
- 涉及后台纹理交接时，读 [异步任务与生命周期](async-lifecycle.md)；验证 CPU/GPU 一致性见 [构建与验证](build-and-test.md)。
