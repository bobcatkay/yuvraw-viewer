# 文件加载与参数推断

在修改打开/重载、加载器分发、错误回填、分辨率候选或 Windows 文件路径时读取。

本页内容：

- [Windows 路径编码](#windows-路径编码)
- [自带文件头 vs 无头格式](#自带文件头-vs-无头格式)
- [猜分辨率（FResolutionGuess）](#猜分辨率fresolutionguess)
- [扩展名与工厂分发](#扩展名与工厂分发)

## Windows 路径编码

项目内所有 `std::string` 文件路径都统一为 **UTF-8**，因为 ImGui 文本和 GLFW 拖放路径
本身就是 UTF-8。Windows API 边界再转成 UTF-16：程序入口使用 `wWinMain`，WIC、
文件对话框、资源管理器定位与 LibRaw 都走宽字符接口。

使用 `std::filesystem` 时必须成对遵守：

- UTF-8 字符串进入文件系统：`std::filesystem::u8path(Path)`
- 原生 `std::filesystem::path` 回到 UI/文档字符串：`.u8string()` / `.generic_u8string()`
- **不要**对用户路径使用 `std::filesystem::path(Path)`、`.string()` 或
  `.generic_string()`；Windows 会经过本地代码页，结果既无法交给 ImGui 正确显示，
  也不能再被 WIC 当成 UTF-8 转回原路径

回归测试在 `TestRawImageLoader` 与 `TestImageExporter`：前者覆盖中文 RAW 路径，
后者覆盖中文目录/文件名的导出和 WIC 读回；`TestImageExporter.exe <路径>` 还能额外
冒烟验证一张真实的中文路径图像。

## 自带文件头 vs 无头格式

分水岭是 `FImageLoader::IsSelfDescribing()`（`FWicImageLoader` 返回 true），
统一入口是 `FImageLoaderFactory::IsSelfDescribingFile(路径)`，只看扩展名。

**自带文件头的文件（PNG/JPEG/BMP/WebP/TIFF…）绝不能套用加载参数。** 这是踩过的坑：
从 .yuv 切到 .bmp 时属性面板上还留着 NV21/1440x1920，`LoadImage` 按格式分发就把
BMP 的字节交给了 `FRawImageLoader`。所以工厂的分发规则 0 是"自描述文件忽略 Params"，
`FImageDocument::Reload()` 同样据此传 `nullptr`，然后把解码结果的格式与尺寸**回填**进
`Params`。

对应地 `FImageDocument::Open()` 对这类文件**跳过文件名解析** ——
`ParseImageInfoFromFilename` 见到 `photo_1920x1080.png` 会返回 NV21。

UI 侧 `FPropertyPanel::SetParamsEditable(false)` 把格式/分辨率/stride/位深/Bayer 整体置灰，
由 `SyncPanelFromDocument()` 按 `FImageDocument::IsSelfDescribing()` 设置。

### 加载失败必须让用户看见

加载器用可选的 `EImageLoadError* OutError` 返回本次调用的失败类别，成功时清为 `None`。
不要在共享 loader 实例上保存可变的 last-error 状态：异步切图与导出可以同时调用它。
`FAsyncImageLoader` 把错误类别转换为可读文字；RAW 截断仍保留实际/所需字节数提示。

WIC 在创建格式转换器前检查文件头尺寸，统一使用 `FImageLimits` 的最大单边 65535。
64 位构建允许 256 Mi 像素、1 GiB 单帧（可容纳 16384x16384 RGBA8），Win32 保留
128 Mi 像素、512 MiB 单帧限制；stride 与缓冲大小经安全乘法后才能转成 UINT/int32_t。
这些是 CPU 单帧保护边界，不代表总进程内存预算或 GPU 单纹理上限。异步加载失败时，
资源超限提示显示当前构建的实际边长、像素数和 MiB 上限；GPU 错误另外区分尺寸超限、
图形内存不足和其它 OpenGL 错误，详见 [纹理与渲染](rendering.md#gpu-尺寸预检查与失败详情)。
RAW、WIC、DNG 分别处理尺寸超限、截断/解码失败及分配异常；LibRaw 实例使用堆分配，
避免 0.22 的大对象消耗后台线程栈。新增加载器不得绕开这些约束。

`FWicImageLoader::GetCodecAvailability` 检查格式注册及 decoder 无文件激活；只有确认缺少 codec 时
才能报告未安装或跳过测试，不能把任意解码失败当作缺少 codec。
公共回归及合成素材见仓库 `docs/PUBLIC_FIXTURES.md`。

`Reload()` 失败时**也会调用 `OnChanged`**，并把可读的原因写进 `LastError`
（例如"按当前参数需要 6220800 字节，文件只有 4423680 字节"），由属性面板红字显示。

原因很实际：以前失败时静默返回 false，用户在面板上改了分辨率、画面纹丝不动，
只会得出"改了不生效"的结论，而真正的原因（新参数下这幅图比整个文件还大）无处可查。

同理，属性面板常驻显示 **文件大小 / 按当前参数算出的图像大小**，两者不等时给出橙色提示
（含差了多少字节）。猜无头格式参数时这是最强的一条线索：
`texture_output_format1_384x2880.yuv` 是 4423680 字节，按 NV21 384x2880 只有 1658880，
一眼就能看出格式猜错了（它其实是 RGBA8，`format1` = Android `PixelFormat.RGBA_8888`）。

`FPropertyPanel::SetImageData()` **不再**从图像数据倒推格式/分辨率 ——
加载失败时那还是上一幅图，会把用户刚填的值悄悄改回去。参数一律以 `Params` 为准。

### 一个文件就是一幅图

**没有多帧概念**：没有帧号参数、没有帧选择控件、没有 `--frame`。
`FRawImageLoader` 从文件头读满一帧就返回。

文件比一幅图**大**时多出来的字节直接忽略（仍把能解出来的这一幅显示出来，
比直接报错更有助于把参数试对），但属性面板会橙字指出差了多少 ——
"多出来的字节"在单帧语义下就等于"参数填错了"。文件比一幅图**小**才是加载失败。

这条语义直接决定了 `FResolutionGuess` 只解 `图像大小 == 文件大小`（见[猜分辨率](#猜分辨率fresolutionguess)）。
若日后要重新引入多帧，改动面远不止加个帧号：求解式要退回带帧数的枚举，
候选排序要重新处理谐波去重，属性面板的"对不上"判据也要跟着换。

## 猜分辨率（FResolutionGuess）

### 文件大小约束的是 (stride, 高)，不是 (宽, 高)

一个文件就是一幅图（见[单帧语义](#一个文件就是一幅图)），所以图像字节数必须**正好等于**文件大小；
再加上 `CalculateFrameSize()` 里宽度**只**通过 `ResolveBaseStride()` 参与计算，于是：

```
文件大小 = stride * 高 * M(格式)        // M 为有理数，NV21 = 3/2，RGBA8 = 4
=>  stride * 高 = 文件大小 / M
```

枚举右式的因数对即得**全部**数学解（一次分解，不再有帧数循环），
宽度再由 stride 在"紧凑排列"假设下反推
（`WidthFromStride()` 对 `ResolveBaseStride` 做二分反演，不为 packed 格式另写公式）。

直接后果：**带 padding 的排布与紧凑排布无法区分** ——
`1440x1920 stride 1472` 与 `1472x1920` 紧凑都是 4239360 字节。所以候选一律按紧凑给出，
padding 只在下拉框底部用一句话提示（"右侧有绿色竖条就把宽度调小、stride 保持不变"）。
**不要试图把 padding 变体塞进候选**，那只会让列表翻倍且多数是错的。

### 排序：宽高比表宁缺毋滥

因数分解的解绝大多数是垃圾，靠打分裁到 8 条。主导项是"与最接近的常见宽高比的对数距离"，
常见分辨率命中、宽/高为 16 倍数各给少量加权。仍按宽度去重（同一宽度只留最像样的那个高度）。

`kCommonAspects` 刻意**不含 21:9**：相机/视频 dump 里几乎不出现，加进去反而成假引力点 ——
4239360 字节的 NV21 会因此把 `1104x2560`（比值离 9:21 只差 0.6%）排到真值 `1472x1920` 前面。
往这张表里加比例前先跑 `TestResolutionGuess`。

### 三个信息源不互相仲裁

文件名分辨率、由文件大小算出的候选、用户手填 —— 把**文件名当成候选之一**（置顶、带
"来自文件名"分组、同样跑字节数校验），仲裁问题就不存在了。谁也不覆盖谁，点一下才生效。

用户**明确切换基础格式**是例外：若现有宽高/stride 在新格式下不能精确装满文件，
`ApplyFormatSelection()` 必须在触发唯一一次重载回调前，同步采用该格式排名第一的精确候选并恢复
紧凑 stride；若当前参数仍能精确匹配，则保留用户设置（尤其是 padding）。禁止先用旧尺寸提交一次
必败请求、再只把候选显示出来要求用户二次点击。

面板侧（`FPropertyPanel::RenderResolutionGuess`）：

- 收起时预览文字兼作状态指示：`8 种可能` / 橙色 `对不上 · 8 种` / `无匹配`
- 校验当前值时**要带上面板上的 stride**，否则配好了 padding 的图会被误报成"对不上"
- 文件名分辨率对不上时，底部列出"换成这些格式反而正好装满"，可点击直接切格式。
  `texture_output_format1_384x2880.yuv` 正是这个场景：NV21 下只有 1658880 字节，
  RGBA8 下正好 4423680 = 文件大小
- 点击会改动正被遍历的 `Candidates`，所以先记 pending，`EndCombo()` 之后再落地
- 结果按 (格式, 文件大小, 文件路径) 缓存 —— ImGui 是立即模式，别每帧因数分解
  （最坏情况实测 2ms，但没有理由每帧付）

**自带文件头的格式必须整体跳过**：那时"文件大小"是压缩后的字节数，与解码后的像素字节数
毫无可比性，因数分解只会给出一串看着挺像样的假分辨率。`RefreshCandidates()` 在这种情况下
**清空**状态而不是直接 return，否则从 a.yuv 切到 b.png 会留着 a.yuv 的文件名提示。

### 打开文件时会自动纠正分辨率

`FImageDocument::Open()` 在文件名解析之后加了一道校验：文件名给的分辨率**装不满**文件时，
换成最可信的候选（`Params.Stride` 一并复位为 0）。文件名不可靠是常态 ——
`_1472x1920` 里的 1472 其实是 stride，`_384x2880` 那个连格式都是错的。

因此 `FApplication::Initialize()` 里**命令行没给任何参数时走 `OpenPath()` 而不是
`OpenPathWithParams()`**：后者绕过 `Open()`，会让"用 YUVRaw 打开"（只传路径）
与"拖进窗口"对同一个文件给出不同的分辨率。反之，用户显式写了 `--width/--stride`
就不该被二次猜测，那条路径保持原样。

## 扩展名与工厂分发

- `.yuv` 扩展名不能决定格式，必须由用户/文件名/命令行指定。
- `FImageLoaderFactory` 按 `SupportsFormat(EImageFormat)` **显式查询**分发（用户指定了格式时），否则才按扩展名。`FWicImageLoader::SupportsFormat(EImageFormat)` 恒为 false，保证用户选了 NV21 时不会被它截走。

## 按需关联

- 帧大小、stride 与位深的定义见 [格式描述与扩展](formats.md)；修改这些定义时读取。
- 更改打开参数的优先级、失败后的候选回退或主图/对比图继承时，读 [设置与缓存](settings-and-cache.md)。
- 将加载结果交接给文档或改动请求取消时，读 [异步任务与生命周期](async-lifecycle.md)。
