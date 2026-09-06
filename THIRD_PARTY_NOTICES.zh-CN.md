[English](THIRD_PARTY_NOTICES.md) | 简体中文

# 第三方声明

YUVRaw 自有代码和应用图形采用 `GPL-3.0-only`。第三方文件保留各自版权与许可条款。本索引与 `licenses/` 中随附的完整条款、程序显示的声明，以及源码包中精确的来源/配方记录配套使用。

| 组件 | 版本 / 来源 | 许可及交付声明 |
|---|---|---|
| LibRaw | 0.22.2 | 从双许可中**选择 LGPL-2.1-only**；保留内部 BSD-3-Clause 与 Adobe MIT 归属；`licenses/LGPL-2.1.txt`、程序包的 `licenses/LibRaw.txt`、`licenses/LibRaw-components.txt` |
| LibRaw-cmake | `eb98e4325aef2ce85d2eb031c2ff18640ca616d3` | BSD-3-Clause；随附源码归档中的 LICENSE |
| Little CMS | 2.19.1，vcpkg port revision 1 | MIT；程序包的 `licenses/Little-CMS.txt` |
| zlib | 1.3.2，vcpkg port revision 2 | Zlib；程序包的 `licenses/zlib.txt` |
| libjpeg-turbo | 3.2.0 | IJG、BSD-3-Clause 及组件声明；程序包的 `licenses/libjpeg-turbo.txt` |
| JasPer | 4.2.9 | JasPer License Version 2.0；程序包的 `licenses/JasPer.txt` |
| GLM | 1.0.3 | 选择 MIT；程序包的 `licenses/GLM.txt` |
| GLFW | 3.4 | Zlib；官方源码许可和程序包合并声明 |
| Dear ImGui docking | `f4d9359095eff3eb03f685921edc1cf0e37b1687`，1.92.0 WIP / 19193 | MIT；`libs/ImGui/LICENSE.txt` 和合并声明 |
| stb_rect_pack / stb_textedit / stb_truetype | 1.01 / 1.14 / 1.26，ImGui 内嵌版本 | 选择 MIT；保留源码声明并随附合并声明 |
| ProggyClean | 内嵌于 `imgui_draw.cpp` | MIT，版权 2004–2005 Tristan Grimmer；合并声明 |
| GLAD 生成加载器 | 生成器 0.1.36，生成于 2024-08-16 | 生成代码以 Public Domain / WTFPL / CC0 提供；Khronos 内容保留独立条款，见下文 |
| Khronos `khrplatform.h` | 仓库内头文件，版权 2008–2018 Khronos Group | 头文件与合并声明中的 MIT 风格授权 |
| Noto Sans CJK SC Regular | 2.004，`523d033d6cb47f4a80c58a35753646f5c3608a78` | OFL-1.1；`resources/fonts/OFL-1.1.txt`；版权 2014–2021 Adobe |
| vcpkg 配方 | 来源锁固定的 port tree | MIT，Microsoft；`third-party/vcpkg-ports/LICENSE.txt` |

源码包中的 `docs/DEPENDENCIES.zh-CN.md`、`third-party/sources.lock.json` 和各 port 的 SPDX 记录给出精确包版本、源码归档哈希、port revision 及本地转换。`third-party/sbom.cdx.json` 的 CycloneDX 清单覆盖链接和随附组件；它是组件清单，不代表已排除所有潜在漏洞。这些路径属于独立于程序 ZIP 的源码包。

LibRaw 的备选 CDDL 条款有意保留在上游源码及复制的版权文件中。YUVRaw 选择 LGPL 2.1，保留备选条款不代表选择它。应用和匹配的库源码、构建补丁支持重链接。说明位于程序旁的 `SOURCE_DISTRIBUTION.zh-CN.md`，或源码包的 `docs/SOURCE_DISTRIBUTION.zh-CN.md`。

LibRaw 的[上游 COPYRIGHT](https://github.com/LibRaw/LibRaw/blob/0.22.2/COPYRIGHT) 还标明：Jacek Gozdz（2010）的 DCB 去马赛克和 FBDD 降噪采用三条款 BSD；Roland Karlsson（2010）的 X3F 库采用 BSD 风格许可；Adobe DNG SDK 1.4 片段采用 MIT（Adobe Systems Incorporated，2005）。`src/demosaic/dcb_demosaic.cpp` 和 `src/x3f/x3f_utils_patched.cpp` 的原始 BSD 条款复制于 `licenses/LibRaw-components.txt`，全部上游源码声明保持完整。该文件还根据上游 COPYRIGHT 的许可标识提供 SPDX 标准 MIT 条款，并明确区分其与从 Adobe 文件提取的原文。本构建未启用 X3F tools。Adobe 归属指 LibRaw 已内嵌的片段；独立 Adobe DNG SDK 不是随附或启用的依赖。

GLAD 生成器源码采用 MIT，生成的加载器则使用独立的上游授权。上游将 Khronos GL 规范输入描述为 Apache-2.0，配套 Khronos 头文件保留各自声明。构建直接使用随附的生成文件，不要求重新运行生成器。参见上游[生成代码 FAQ](https://github.com/Dav1dde/glad/blob/v0.1.36/README.md#whats-the-license-of-glad-generated-code) 和[许可文件](https://github.com/Dav1dde/glad/blob/v0.1.36/LICENSE)。

Microsoft YaHei 存在时从 Windows 加载，不进行再分发。完整未修改的 Noto 字体是随附的中文回退字体。运行时字形图集的子集策略见 `resources/fonts/README.zh-CN.md`。

Windows WIC、OpenGL/D3D/DXGI 系统组件、Windows 字体和 Microsoft Visual C++ 运行库是平台前提，不是随包提供的第三方源码。

本页为项目声明的中文说明；上游许可原文保持不变，以随附的原始许可条款为准。
