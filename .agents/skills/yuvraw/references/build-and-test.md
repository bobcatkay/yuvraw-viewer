# 构建、运行与验证

在构建、发布、运行程序、排查依赖或选择验证方式时读取。

本页内容：

- [构建、测试、运行](#构建测试运行)
- [命令行](#命令行)
- [调试与冒烟测试](#调试与冒烟测试)
- [测试素材](#测试素材)

## 构建、测试、运行

日常开发与回归验证使用 **Debug|x64**；正式发布由 `build-release.ps1` 重建 Release 并打包。
手动构建时**必须构建 .sln 而不是 .vcxproj**：

```powershell
$msbuild = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
    -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
& $msbuild YUVRaw.sln -p:Configuration=Debug -p:Platform=x64 -v:minimal -nologo
```

**不要把 MSBuild 路径写死**，使用 vswhere 定位实际安装的 C++ 工具链。
公开开发者的完整步骤见仓库根目录的 `docs/BUILDING.md`。

产物：`x64/Debug/YUVRaw.exe`。

`YUVRaw.vcxproj` 的 `RestoreYUVRawDependencies` 会在构建前按 `vcpkg.json`
自动恢复 GLM、LibRaw（含 `dng-lossy`）及其依赖到项目内的 `vcpkg_installed/`。
清单固定 registry baseline，并通过 overrides 保持依赖版本与 `FThirdPartyNotices.h` 一致；
升级时两处一起改。首次构建需要联网，后续复用安装结果。
vcpkg 程序优先取 `VcpkgRoot` / `VCPKG_ROOT`，其次取项目上级 `vcpkg`，最后取
Visual Studio 自带的 `VC/vcpkg`。不要再把头文件、库或 DNG 测试路径指向外部的
`vcpkg/installed`。x64/Win32 仍使用仓库的 static-md triplet，依赖显式链接，
`VcpkgEnabled=false` 仅关闭全局自动链接，依赖恢复由上述项目 target 独立执行。

先用 `tools/Set-Version.ps1 -Version <版本>` 显式选择版本；`build-release.ps1`
只重建当前版本，不递增或修改源码。`-ExpectedVersion` 用于核对 tag/CI 的版本，
`-DryRun` 只预览。产物位于 `artifacts/releases/<版本>/x64/`：
`YUVRaw-<版本>-windows-x64.zip`、`YUVRaw-<版本>-source.zip` 与 SHA256SUMS。
三份文件必须一起发布；缺资源、许可或来源校验失败时拒绝产包。版本与打包步骤见
`docs/BUILDING.md`；发布前验证解压后的程序包可在无开发工具的 Windows 上运行。

公开项目文档使用英文 `*.md` 与中文 `*.zh-CN.md`，顶部互链；默认 README 与 UI 均为英文。
`tests/TestDocumentation.ps1` 检查双语配对、相对路径和标题跳转，CI 同步执行。
`Copy-ReleaseDocumentation.ps1` 将双语 QUICK_START 转为便携包 README，并只改写语言链接；
发布验证器独立核对这项转换。源码包包含两种语言，便携包也包含双语使用、源码、字体与第三方说明。
公开文档只记录当前用法、架构、依赖与构建流程，不提交阶段计划、操作流水或历史验收报告。

LibRaw 选择 LGPL 2.1，自有代码采用 GPLv3。`tools/Copy-ThirdPartyNotices.ps1`
负责构建后的许可复制；`tools/Export-SourceBundle.ps1` 校验安装包 SPDX 与
`third-party/sources.lock.json`、配方快照、上游源码哈希，再按清单打包应用与依赖源码。
源码缓存默认位于 `artifacts/source-cache`；发布脚本支持 `-SourceCacheDirectory`
与 `-OfflineSources`。升级依赖时同时更新锁文件、`third-party/vcpkg-ports` 中的
配方快照和应用通知。私有测试原图不得进入源码包。完整重建与重新链接步骤见
仓库根目录的 `docs/SOURCE_DISTRIBUTION.md`。

修改许可复制或源码打包流程后运行
`powershell -NoProfile -File tests/TestSourceDistribution.ps1 -SourceArchivePath <源码ZIP>`，
检查源码/补丁完整性、逐文件哈希、私有输入排除，以及缺少许可或缓存源码被篡改时的失败行为。
该验证器以 ZIP 内四个工程和依赖锁为准，检查构建输入、精确双向 SHA256 清单、上游包与配方
一致性，并自动运行 12 个纯内存破坏负例；不会读取私有照片来构造测试。

发布包还必须运行 `tests/TestReleasePackage.ps1 -ReleaseDirectory <目录> -Version <版本> -Platform x64`，
检查三件产物的校验和、程序 ZIP 精确文件名单、EXE 数值/文本版本与架构、配套源码中的版本、
完整许可、字体与文档一致性；`build-release.ps1` 已在输出发布目录前调用此检查。
`tests/TestReleasePackageFailures.ps1` 用真实 PE 版本资源构造定向负例，验证缺许可、混入个人配置、
EXE 或源码版本错误会在对应检查点被拒绝。

`tools/Update-SourceLock.ps1` 先核对已安装依赖与 manifest 的版本和 port-version，再从实际 SPDX
导出配方和上游包。`tools/Set-SourceSnapshot.ps1` 负责提交：任何旧登记文件被修改或缺失都必须先
人工处理；同名新文件也不能覆盖未登记文件。所有新文件预先校验，逐文件保留备份，最后更新 lock，
失败时逆序恢复；备份留在 `artifacts/source-lock/` 便于检查。禁止用批量强制复制替换这段事务。
修改快照更新流程后运行 `tests/TestSourceSnapshot.ps1`；它用真正的文件共享锁使最终 lock 提交失败，
检验前面的替换、新增、删除全部回滚，并覆盖本地修改保护、未登记文件保护和路径边界。

> 链接偶发 `LNK1201: 写入程序数据库 ... 时出错`，先看有没有 `YUVRaw.exe`
> 或残留的 `mspdbsrv.exe` 占着 PDB，杀掉再构建即可，不是磁盘或权限问题。

> 直接构建 `YUVRaw.vcxproj` 在 Glad/GLFW/ImGui 三个依赖项目都已是最新时能侥幸成功，
> 一旦它们需要重新编译就会报 `无法打开包括文件: "glad/glad.h"` 之类的错 ——
> 这三个子项目的 `AdditionalIncludeDirectories` 用了 `$(SolutionDir)`，
> 而单独构建 .vcxproj 时 `$(SolutionDir)` 会退化成该项目自己的目录。

离线自检（不需要 GPU / ImGui；先构建解决方案恢复 LibRaw，公共 DNG 自动生成）：

```bash
pwsh -NoProfile -ExecutionPolicy Bypass -File tests/run_tests.ps1
```

`run_tests.ps1` 里的 `$suites` 会逐个编译运行：

| 套件 | 覆盖 |
|---|---|
| `TestWicImageLoader` | 超大 TIFF 头、像素上限、UINT 边界、截断/无效文件、失败后继续打开，以及可选 codec 枚举 |
| `TestDngImageLoader` | 默认使用程序生成的公共 DNG，验证尺寸、RGBA/alpha、渐变和损坏/超限输入；额外私有样例通过 `-DngFixturePath` 传入 |
| `TestRawImageLoader` | packed 真值、字节序/位深、stride、尺寸/内存边界、公共样例字节与两帧串接时只读首帧 |
| `TestFormatDesc` | 格式描述表：平面几何 / 帧大小 / stride / 名字查找 |
| `TestResolutionGuess` | 由文件大小反推分辨率：候选排序、stride 反演、跨格式提示 |
| `TestColorPipeline` | 色彩管线：PQ/HLG/sRGB 传输函数、原色矩阵、色调映射、**直通恒等**、直通路径的超范围高亮、HDR 保色相限幅 |
| `TestWindowPlacement` | 主窗口客户区按原生 frame 约束到工作区：1080p、高 DPI、负坐标副屏 |
| `TestImageViewSettings` | 对比视图同步：复制显示模式、缩放与平移，同时保留目标图片自己的旋转与镜像 |
| `TestImageConfigCache` | 图片属性 LRU：容量淘汰、全字段跨进程持久化、损坏文件拒绝 |
| `TestDirectoryImagePropertyHistory` | 同目录同后缀上一张主图属性继承：后缀隔离、自描述格式、文件大小不匹配回退 |
| `TestAsyncImageLoadMailbox` | 异步切图请求邮箱：代际递增、只保留最新待处理请求、取消失效与有界队列 |
| `TestWebpEncoder` | 自研无损 WebP 编码器：编码后用**系统 WIC 解码器**读回来逐像素比对 |
| `TestImageExporter` | 导出全流程：带 padding 的 NV21 → RGB → 缩放 → PNG/BMP/WebP/JPEG，读回比对 |

**改动 `FImageFormatDesc`、新增格式、动过 `FColorTransform` / `FWebpEncoder` / `FImageExporter` 后必须跑一遍。**

`TestColorPipeline` 的断言全部对着公开标准写（PQ 码值 0.5 = 92.24 nit、
BT.2020→BT.709 矩阵的九个值、由 xy 现算出的 BT.709 亮度系数 0.2126/0.7152/0.0722）。
这类数学抄错一位画面照样出图，只是颜色悄悄偏掉，肉眼发现不了 ——
**不要靠放宽容差让它过**。其中"直通恒等"那一组是回归保护：SDR 素材必须与
引入色彩管线之前逐位一致。

新增套件时往 `$suites` 里加一项即可（`Libs` 留空表示只链默认库）。

默认执行 19 套 CPU 测试，`-SuiteNames` 可选套件。加载边界静态分析与 ASan：

```powershell
& ./tests/run_tests.ps1 -SuiteNames TestWicImageLoader,TestRawImageLoader,TestDngImageLoader -Analyze
& ./tests/run_tests.ps1 -SuiteNames TestWicImageLoader,TestRawImageLoader,TestDngImageLoader -EnableAddressSanitizer
```

`-Analyze` 把代码分析告警作为错误。ASan 不对系统 WIC 和默认 vcpkg LibRaw 本体插桩；
DNG 混合链接需要关闭该套件的 STL annotations，RAW/WIC 保留。具体边界见
`docs/PUBLIC_FIXTURES.md`。WebP 读回仅在确认系统 codec 缺失时标记 SKIP，可激活的 decoder 解码失败必须 FAIL；只有格式元数据而 COM 类未注册属于缺失，其它激活错误不得跳过。

**新增 .cpp/.h 必须手工加进 `YUVRaw.vcxproj`** 的 `<ClCompile>` / `<ClInclude>`，项目没用 CMake 也没有通配符收集。

### 命令行

支持"用 YUVRaw 打开"，也便于脚本化与自动化验证：

```bash
YUVRaw.exe frame.yuv --format NV21 --width 1440 --height 1920 --stride 1472
YUVRaw.exe photo.png
YUVRaw.exe a.yuv --format NV21 --width 1440 --height 1920 --stride 1472 --compare b.yuv
YUVRaw.exe hdr.yuv --format P010 --matrix bt2020 --primaries bt2020 --transfer pq
```

`--format` 取格式描述表的 `Name`（NV21/P010/I420/Bayer16…，大小写不敏感），另有
`--compare <路径>`（加载对比图并恢复已记忆的单图切换或平铺模式，沿用主图参数）。未指定的项由文件名解析补齐。

色彩解读那几项**无法从文件推断**（P010 既可能是 PQ 也可能是普通 SDR），只能显式给：

| 选项 | 取值 |
|---|---|
| `--matrix` | `bt601` / `bt709` / `bt2020`（YCbCr 矩阵系数） |
| `--range` | `limited` / `full` |
| `--primaries` | `bt709` / `bt2020` / `p3` / `bt601-525` / `bt601-625` |
| `--transfer` | `sdr` / `bt1886` / `pq` / `hlg` / `linear` |
| `--tonemap` | `clip` / `reinhard` / `aces` |
| `--refwhite` | 参考白 nit（默认 **203** = BT.2408 图形白，仅 PQ/HLG 有意义） |
| `--exposure` | 曝光档数 |
| `--out-of-range` | 打开超范围高亮 |
| `--no-hdr` | 强制走 SDR 输出，即使显示器支持 HDR |

`--compare` 是验证并排/差值的主要手段，上面这组则是验证色彩管线的主要手段 ——
否则每次都只能在属性面板上手点。**同一帧配 `--transfer sdr` 与 `--transfer pq` 各截一张图，
是判断 PQ 链路有没有生效最快的办法**（不生效时画面发灰发闷，生效后黑位下沉、饱和度正常）。

### 调试与冒烟测试

GUI 子系统没有控制台，`LOGD/LOGE` 走 stderr + `OutputDebugStringA`，持久日志在
`%LOCALAPPDATA%\YUVRaw\Logs`。正常启动**不再预编译全部格式着色器**：
`FShaderManager::GetShaderForFormat` 首次使用时编译并缓存，避免首帧前串行编译所有变体。

需要验证全部 GLSL / 格式上传链路时运行专用 GPU 冒烟脚本，不要把全量验证重新塞回启动路径：

```bash
pwsh -NoProfile -ExecutionPolicy Bypass -File tests/run_gl_format_validation.ps1
```

脚本先创建第二个隐藏共享 Context，验证工作线程建纹理、`fence + flush` 交接和主 Context
可见性；随后跑三组格式/色彩用例：

| 组 | 覆盖 | CPU 真值来源 | 目标 |
|---|---|---|---|
| 格式（`BuildCases()` 动态生成） | 字节 → R'G'B'，`uPipelineEnabled = 0` | `FImageSampler::ConvertToRgb8` | RGBA8 FBO |
| 色彩管线 · SDR（17 条） | EOTF / 原色 / 曝光 / 色调映射 / 高亮 | 同上（内部就是 `ApplyPipeline`） | RGBA8 FBO |
| 色彩管线 · HDR（7 条） | `uOutputMode = 1` 的 scRGB 分支 | 直接调 `ApplyPipeline` | **RGBA16F** FBO |

HDR 那组必须用 fp16：`uOutputMode = 1` 写出的是扩展 sRGB 编码值，可以 > 1 也可以为负，
8bit UNORM 会把它钳掉。源固定用 RGB8，这样"非线性 R'G'B'"就等于字节 /255，
CPU 参考不必重走一遍取样与色度上采样（那一段由格式组覆盖）。

#### 写色彩管线用例时，先确认它真的跑到了那个分支

这条不是理论洁癖，是实测踩出来的。`BuildSourceBytes` 给 YUV 的是**恒定单色**
（Y=半量程、UV 固定），拿它测高亮或 HLG 会得到一条永远通过、却什么也没验证的用例：

- **高亮用例**：源全落在 `[0,1]` 之内时两边都不标记，把 shader 里的高亮整段删掉照样通过。
  `FPipelineCase::bRequireOutOfRangeMarkers` 就是为此而设 —— 它检查 CPU 真值里
  确实出现了红 `(255,0,0)` 与蓝 `(0,102,255)`。**新增高亮用例一定要打开它。**
- **蓝色（色域外）分支需要"暗"饱和色**：高亮判定里"超上限"优先于"色域外"，
  亮饱和色一律先被标红。`kPipelineSourcePixels` 里那几个 `(140,20,20)` 就是干这个的。
- **HLG 需要梯度源 + 参考白跟着 Lw 走**：OOTF 以 Lw 为峰值，除以默认的 203 之后
  整幅图都远大于 1，Clip 会把它们全压成纯白，HLG 里的任何错误都被饱和吃掉。

**8bit 路径有精度下限**：HLG OOTF 里 `1.2 + 0.42*log10(Lw/1000)` 的 `0.42`
改成 `0.43` 只值约 0.5 LSB，RGBA8 那两组抓不到，只有 fp16 的 `hdr/hlg_peak_4000` 能守。
反过来 `HlgSceneLinear` 的三个常数影响是百分数级的，两组都能抓。

改完色彩管线，验证方式是**故意改坏再跑一遍**：把某个常数抄错一位、把
`LimitToPeakPreserveHue` 换回逐通道 `min()`、把高亮整段删掉，看是否有用例变红。
没变红说明用例在空转，要先修用例。

启动日志会记录 Core UI、字体图集与 HDR 初始化的毫秒耗时，卡顿排查先看这些分段数据。
`HDR presenter ready: hdr=1 maxNits=... sdrWhite=...` 这一行同时也是确认 HDR 通路
是否就绪、当前显示器参数是多少的最快办法。

## 测试素材

公开测试全部使用原创程序化样例：

```powershell
powershell -NoProfile -File tools/Generate-PublicFixtures.ps1
& ./x64/Debug/YUVRaw.exe ./artifacts/public-fixtures/gradient_64x48_stride80_NV21.yuv --format NV21 --width 64 --height 48 --stride 80
```

带 padding 的 NV21/P010、RGBA 色卡、Bayer 渐变、公共 DNG 和两帧串接输入的参数与期望
列在 `docs/PUBLIC_FIXTURES.md`。样例来自算式而非相机原图，生成器不读外部输入。
私有真实素材不提交、不进入源码包、不由 CI 上传；本地可通过 `-DngFixturePath` 额外验收。

## 按需关联

发布前按源码锁核对依赖配方的原始字节。`third-party/vcpkg-ports/` 包含上游 LF 与 CRLF 文件，
由 `.gitattributes` 的 `-text` 禁止 Git 换行转换；不能批量改成 LF 后继续沿用原哈希。

- 异步切图卡顿、共享 Context 或退出不结束时，读 [异步任务与生命周期](async-lifecycle.md)。
- 修改色彩数学或 HDR 呈现时，读 [色彩管线与 HDR](color-and-hdr.md)。
