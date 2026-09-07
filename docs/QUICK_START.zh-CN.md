[English](QUICK_START.md) | 简体中文

# YUVRaw 使用说明

Windows x64 图像查看与格式分析工具，可查看 RGB、灰度、YUV、Bayer RAW 和 DNG，检查像素、对比图片并导出。

## 运行

1. 在 Windows 10 / 11 x64 上解压完整 ZIP，再运行 `YUVRaw.exe`。显卡及厂商驱动需要支持 OpenGL 3.3 core。
2. 系统需要 Microsoft Visual C++ x64 运行库。缺失时从微软的[受支持运行库下载页](https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist)安装 x64 版本。
3. 保留 `resources/fonts/` 及许可文件。程序从自身目录加载附带字体，不需要安装字体，也不要求从固定工作目录启动。

4. 程序默认使用**英文**。在**设置 → 语言**选择**简体中文**或 **English**，点击应用后立即生效并保存，无需重启；已有语言选择继续生效。

**帮助 → 使用说明**提供快捷键、画布操作、对比与批量导出的简明用法。**帮助 → 关于 → 最新版本**打开版本下载和更新记录。

## 打开、查看和导出

用“文件 → 打开文件”、`Ctrl + O` 或拖放打开图片；也可打开目录，在文件浏览器中切图。中文和包含空格的路径可以使用；命令行中的路径请加引号。少见汉字可能超出当前界面字形集合。

PNG、JPEG、DNG 等带文件头的图片自动解码。裸 `.yuv` / `.raw` 需要在属性面板确认格式、可见宽高、行跨度和色彩解释，文件名猜测只是初值。stride 的单位是**字节**，包含每行末尾 padding；有效位深与容器位深是不同参数。NV12/NV21 的 UV 顺序、Full/Limited 范围、矩阵、原色及传输函数需与输入一致。

例如下面命令打开自己的 NV21 文件；示例路径需要替换：

```powershell
.\YUVRaw.exe "D:\Samples\测试图片\frame.yuv" --format NV21 --width 1440 --height 1920 --stride 1472
```

| 操作 | 输入 |
|---|---|
| 缩放 / 平移 | `Ctrl + 滚轮` / 左键拖动 |
| 适应窗口 / 1:1 | `Ctrl + 0` / `Ctrl + 1` |
| 重置缩放与偏移 | 左键双击 |
| 复制探针信息 | 在画布有效像素处右键 |
| 添加对比图 | 文件浏览器右键“添加为对比图” |
| 单图对比切换 | 单图切换模式下左键单击画布 |
| 导出当前图 | `Ctrl + E` |

对比提供单图切换、平铺和差值图。文件浏览器可用 `Ctrl` / `Shift` 多选后批量导出。支持 PNG、JPEG、BMP、无损 WebP，按原尺寸、百分比或指定宽度等比例缩放。

## 格式限制

- 当前只查看首帧。多帧裸文件、GIF 等容器没有播放或帧索引功能，需要其它帧时请先拆分。
- 支持平面/半平面/打包 YUV、RGB/灰度和常见 Bayer 排列。P010/P210 固定小端、高位对齐；Android packed RAW10/12/14 要求宽度为 4 的倍数、高度为偶数。
- 裸 Bayer 采用基础双线性去马赛克，不包含相机黑电平、白平衡或颜色矩阵。DNG 经 LibRaw 输出 sRGB RGBA8 预览，不是保留全部传感器动态范围的 RAW 开发流程。
- P010 不自动意味着 HDR。PQ/HLG 需要正确传输函数、原色，以及 Windows HDR、显示器和驱动支持。不可用时回退 SDR；`--no-hdr` 可强制 SDR。独立浮动面板使用 SDR。
- 导出是 RGB8 SDR，不保留 HDR 元数据或源图 alpha。WebP 导出使用自带编码器；WebP、HEIF/HEIC 等导入依赖系统 WIC 编解码器，可选扩展名不保证本机可解码。

## 设置与问题排查

设置：`%APPDATA%\YUVRaw\settings.ini`；窗口布局：`%LOCALAPPDATA%\YUVRaw\imgui.ini`；图片属性缓存：`%LOCALAPPDATA%\YUVRaw\image-properties.cache`；日志：`%LOCALAPPDATA%\YUVRaw\Logs\YUVRaw.log`。首次启动会迁移旧 `ImageDevTool` 数据并保留原文件；设置页可清除数据。清除全部数据恢复英文，仅清图片缓存保留语言。

画面斜切或出现绿条时先核对宽高及字节 stride；颜色错误时再检查 UV 顺序、位深、对齐、矩阵及范围。提交问题前移除日志中的用户名、个人路径和相机标识。私有测试原图不随包提供，也不应作为公开 issue 附件。

## 许可与对应源码

自有代码和应用图标采用 GPL-3.0-only，完整条款在本目录 `LICENSE`。第三方保留各自许可，参见本目录 `THIRD_PARTY_NOTICES.md`、`licenses/ThirdPartyNotices.txt`、`licenses/` 以及 `resources/fonts/OFL-1.1.txt`。

LibRaw 在其双许可中选择 LGPL-2.1-only，同时保留内部组件的 BSD/MIT 等条款。允许按相应许可修改并重新构建、重链接程序；具体步骤见本目录 `SOURCE_DISTRIBUTION.md`。

应与这个程序包同时取得 `SOURCE_CODE.txt` 所指定的 **同版本 source ZIP** 和 SHA256SUMS。源码包包含应用、依赖原始归档、实际配方及补丁；不要用一个移动中的分支替代匹配源码。开发构建说明位于源码包的 `docs/BUILDING.md`，公开合成素材生成方法位于 `docs/PUBLIC_FIXTURES.md`。

项目地址：[bobcatkay/yuvraw-viewer](https://github.com/bobcatkay/yuvraw-viewer)。
