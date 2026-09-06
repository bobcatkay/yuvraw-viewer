# 导出流程与 WebP 编码

在修改导出入口、批量参数、重采样、保存路径或 WebP 码流时读取。

本页内容：

- [导出](#导出)
- [WebP：为什么有一个自研编码器](#webp为什么有一个自研编码器)

## 导出

入口两个，都汇到 `FExportPanel` 这一个模态弹窗：

| 入口 | 源 | 说明 |
|---|---|---|
| 文件菜单"导出..."（Ctrl+E） | **已加载的那幅图** | `bUseLoadedImage = true`，不重新读盘 |
| 文件浏览器右键"导出..." | 选中的一个或多个文件 | 逐个读盘，支持批量 |

菜单那条**必须**用内存里的图：用户可能刚在属性面板改过格式或 stride，
重新按文件名解析一遍会得到另一幅图。批量那条则相反，只能逐个 `LoadForExport()`。

`FMainDockSpace::LoadForExport()` 的参数解析规则与 `FImageDocument::Open()` 一致
（沿用主图参数 + 文件名解析覆盖），但**自带文件头的格式一律不套用参数** ——
`ParseImageInfoFromFilename` 见到 `photo_1920x1080.png` 会返回 NV21，
套上去就会让工厂按格式命中 `FRawImageLoader`，把 PNG 的字节当 NV21 读。
判断依据是 `FWicImageLoader::SupportsFormat(路径)`，不要另起一张扩展名列表。

面板上的**色彩矩阵只在源是 YUV 时可用**（`FImageExporter::NeedsColorMatrix`）——
源本来就是 RGB / 灰度 / Bayer 时矩阵不参与任何计算，置灰而不是隐藏。
初值取源文档当前的 `FDisplaySettings`，否则导出的颜色和屏幕上看到的对不上。

分辨率只支持等比例：`Original` / `Percent` / `Width`（高度按原始宽高比推算）。
缩小走面积平均（避免摩尔纹），放大走双线性。

不勾选"覆盖同名文件"时 `MakeOutputPath()` 会依次试 `_1`、`_2` —— 顺带避免
"把 a.png 导出成 png"时读一半又写回自己。

导出成功的路径都收在 `FExportResult::OutputPaths` 里，面板用 `ImGui::TextLink`
列成可点的文件名，点击走 `FFileDialog::RevealInExplorer()`（`ILCreateFromPathW` +
`SHOpenFolderAndSelectItems`，不是 `explorer.exe /select,"..."` —— 后者要把路径
拼进命令行，逗号、空格、中文都得自己转义）。

MakeOutputPath 在“不覆盖”模式下最多检查原名及 _1 到 _9999。候选耗尽或路径检查失败时返回空字符串；Export 在像素转换和编码之前拒绝空路径并通过中英文资源报告失败，不能将最后一个已存在的候选交给编码器。

### WebP：为什么有一个自研编码器

**Windows 只带 WebP 解码器，不带编码器。** 用
`IWICImagingFactory::CreateComponentEnumerator(WICEncoder, ...)` 枚举一遍就能看到：
编码器列表里有 BMP/GIF/JPEG/PNG/TIFF/WMPhoto/DDS/HEIF/JPEG-XL，**没有 WebP**；
解码器列表里才有 `Microsoft Webp Decoder`。项目又不引入第三方库，所以
`FWebpEncoder` 自己写了一个无损 VP8L 编码器。

范围刻意收窄：subtract-green + 预测器变换（全图固定 12 号，逐通道 `clamp(L+T-TL)`），
四个通道各做一次静态 Huffman，**不做 LZ77 回溯引用、不用色彩缓存、不用元 Huffman**。
所以压缩率不如 libwebp（连续色调约 4-12 bpp，纯噪声退化到 25 bpp），但输出完全合规。

写这类码流时最容易踩的三个坑，改动前先看清楚：

- **单符号前缀码在 VP8L 里是 0 位码**（解码器直接返回该符号，不消耗比特），
  与"写 1 位"的直觉冲突。`BuildPrefixCode()` 因此在只有一个符号时补一个永不使用的
  邻居符号，保证每个码至少两个符号 —— 代价是每像素多 1 位，换来不会静默错位。
- **元 Huffman 标志位只在最外层图像里存在**。变换携带的子图像（预测器图像）不读它，
  多写一位整条码流就错位。`WriteEntropyCodedImage()` 的 `bWriteMetaPrefixBit` 就管这个。
- **解码器按读到的相反顺序做逆变换**，所以书写顺序 = 编码时的施加顺序。
  现在是先 subtract-green 再预测。

验证手段是现成的：`tests/TestWebpEncoder.cpp` 编码后用**系统 WIC 解码器**读回来逐像素比对，
无损编码器错一位就会挂。改这个文件后一定要跑。

## 按需关联

- 涉及后台执行、文档像素生命周期或忙碌状态时，读 [异步任务与生命周期](async-lifecycle.md)。
- 调整源文件的格式识别或路径处理时，读 [文件加载与参数推断](loading.md)。
- 调整导出颜色时，读 [色彩管线与 HDR](color-and-hdr.md)；编码器与导出流程的必跑回归见 [构建与验证](build-and-test.md)。
