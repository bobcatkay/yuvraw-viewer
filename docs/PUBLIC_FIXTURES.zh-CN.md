[English](PUBLIC_FIXTURES.md) | 简体中文

# 公开合成样例与加载边界回归

所有公共样例均由本仓库的 `tests/PublicFixtures.h` 和 `tests/GeneratePublicFixtures.cpp`
用算式直接生成，采用项目的 GPL-3.0-only 许可。生成器不读取照片、相机文件或外部素材，
文件中不含个人路径、拍摄信息或相机序列号。测试用的 DNG 相机名称是虚构的
`YUVRaw Procedural RGGB`。

## 生成与打开

在安装了 MSVC C++ 工具链的 Windows 上，在仓库根目录运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/Generate-PublicFixtures.ps1
```

默认写入已被 Git 忽略的 `artifacts/public-fixtures`，可用 `-OutputDirectory` 指定其他目录。
生成器每次输出相同字节；同名样例会重写。`manifest.json` 记录六个文件的 SHA-256 和字节数。
公开源码只需包含生成器及其共享头文件，无需依赖这些生成物进行重建。

| 文件 | 参数 | 期望 |
|---|---|---|
| `colorbars_64x48_RGBA8.raw` | RGBA8，64×48，stride 256 | 从左到右白、黄、青、绿、品红、红、蓝、黑；alpha 全为 255 |
| `gradient_64x48_stride80_NV21.yuv` | NV21，64×48，stride 80，有限范围 SDR | 水平黑到白灰渐变；有效 Y=16..235，VU=128；每行 16 字节 padding 为 0xA5 |
| `gradient_64x48_stride144_P010.yuv` | P010，64×48，stride 144，有限范围 SDR | 水平黑到白灰渐变；Y=64..940、UV=512，以 16 位小端高 10 位存储；每行 padding 16 字节 |
| `gradient_64x48_Bayer12.raw` | Bayer12，64×48，stride 128，RGGB，小端低位对齐 | 每个 2×2 CFA 单元值相同，水平 0..4095 渐变；基础 Bayer 预览 |
| `two_frames_64x48_stride80_NV21.yuv` | NV21，64×48，stride 80 | 正向与反向渐变两帧串接，当前应用按参数只读取第一帧，文件大小提示有多余数据 |
| `synthetic_64x48.dng` | 自描述，无需手填参数 | 64×48 sRGB RGBA8，alpha 全不透明，亮度从左到右增加 |

例如：

```powershell
& ./x64/Debug/YUVRaw.exe ./artifacts/public-fixtures/gradient_64x48_stride80_NV21.yuv --format NV21 --width 64 --height 48 --stride 80 --range limited --transfer sdr
& ./x64/Debug/YUVRaw.exe ./artifacts/public-fixtures/gradient_64x48_stride144_P010.yuv --format P010 --width 64 --height 48 --stride 144 --range limited --transfer sdr
& ./x64/Debug/YUVRaw.exe ./artifacts/public-fixtures/synthetic_64x48.dng
```

P010 仅表示存储布局，不自动表示 HDR。这个样例使用 SDR 渐变。当前应用没有视频播放、
帧索引或 `--frame` 参数，串接样例验证的是首帧读取边界。

## DNG 结构与测试边界

合成 DNG 是小端 TIFF、单 IFD、单 strip、无压缩 16 位 RGGB CFA；有效白电平 4095，
黑电平 0，AsShotNeutral=(1,1,1)，ColorMatrix1 为单位矩阵，校准光源 D65。
矩阵用于构造合法可解码的颜色元数据，不代表真实相机，也不构成相机色彩准确性证明。
没有嵌入预览、GPS、原始照片或个人信息。

结构依据 [Adobe DNG 1.6 规范](https://helpx.adobe.com/content/dam/help/en/photoshop/pdf/dng_spec_1_6_0_0.pdf)
与 [TIFF/DNG 标签说明](https://www.loc.gov/preservation/digital/formats/content/tiff_tags.shtml)。

`TestDngImageLoader` 默认验证尺寸、格式、stride、字节数、alpha、非恒定输出及渐变方向；
另外构造无效头、缺少 strip 数据和超限尺寸，确认失败后同一 loader 能继续加载有效 DNG。
低内存策略用另一张程序化 1024×1024 DNG 验证：设置 1 MiB 上限后能读元数据，
但其 2 MiB RAW 数据在解包时被 LibRaw 拒绝；工厂测试同时检查错误出参和自描述格式分发。
这些用例覆盖本项目的加载边界，不替代各种相机、压缩 DNG 或真实 HDR 硬件的验收。

## 自动测试

构建解决方案以恢复固定的 LibRaw 依赖后运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_tests.ps1
# 在 PowerShell 中选择加载边界套件：
& ./tests/run_tests.ps1 -SuiteNames TestWicImageLoader,TestRawImageLoader,TestDngImageLoader -Analyze
& ./tests/run_tests.ps1 -SuiteNames TestWicImageLoader,TestRawImageLoader,TestDngImageLoader -EnableAddressSanitizer
```

默认包含 17 个既有 CPU 套件、WIC 边界套件和公共 DNG 套件。DNG 不再依赖私有输入。
`-DngFixturePath <本地文件>` 可在公共 DNG 测试后额外执行一张真实 DNG 冒烟，
私有样例尺寸不固定为某个特定值。测试产物写入 `artifacts/cpu-tests/{normal,analyze,asan}`。
ASan 覆盖本项目的 C++ 加载代码和测试；系统 WIC 及默认 vcpkg 二进制本身未插桩，不能声称已对其完整执行 ASan。
RAW/WIC 套件保留 STL 容器边界 annotations。DNG 与未插桩的静态 LibRaw 混合链接，
仅该套件使用 `_DISABLE_STL_ANNOTATION` 保持 ODR 一致，仍保留本项目的地址插桩；
它不覆盖容器已保留但尚未使用的容量区访问。依据 [MSVC 的混合链接要求](https://learn.microsoft.com/en-us/cpp/sanitizers/error-container-overflow)。

WIC 测试先枚举格式元数据，再对匹配的 COM 解码类执行不读取文件的实例创建，报告
PNG/WebP/HEIC/HEIF 的 `AVAILABLE`、`MISSING` 或 `UNKNOWN`。Windows 可以保留格式
元数据却没有安装实际解码类，不能只凭扩展名列表报告可用。

只有找不到匹配项，或匹配类均明确返回 `REGDB_E_CLASSNOTREG` 时，才判定缺失并将
WebP 读回标记 `SKIP`。其它激活错误保持 `UNKNOWN`；可创建的解码器读回失败仍为
`FAIL`。多个候选中有可创建的解码器即可继续；未知错误不能被另一个缺失类覆盖。
导出套件在缺少 WebP 解码器时仍验证编码成功，跳过读回部分。类可创建不等于支持
所有 HEIF 压缩变体；没有 HEIF 编码样例的自动读回测试。

## 私有文件规则

- `artifacts/public-fixtures` 仅用于本工具生成的公开样例。禁止把真实原图复制到该目录再发布。
- 私有输入放在仓库外或被忽略的本地目录，通过 `-DngFixturePath` 明确传入，CI 不使用此参数。
- CI/Release 上传按明确文件名清单执行，不递归上传私有输入目录、整个工作区或用户数据目录。
- 私有样例测试日志及任何导出画面在分享前仍需检查路径和元数据；公共测试不需要这些文件。
