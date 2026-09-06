English | [简体中文](THIRD_PARTY_NOTICES.zh-CN.md)

# Third-party notices

YUVRaw's own code and application artwork use `GPL-3.0-only`. Third-party files retain their respective copyright and license terms. This index is complemented by the full texts shipped in `licenses/`, the notices displayed by the application, and the exact source/recipe records in the source archive.

| Component | Version / origin | License and delivered notice |
|---|---|---|
| LibRaw | 0.22.2 | **LGPL-2.1-only selected** from its dual license; internal BSD-3-Clause and Adobe MIT attribution retained; `licenses/LGPL-2.1.txt`, binary `licenses/LibRaw.txt`, `licenses/LibRaw-components.txt` |
| LibRaw-cmake | `eb98e4325aef2ce85d2eb031c2ff18640ca616d3` | BSD-3-Clause; LICENSE in its bundled source archive |
| Little CMS | 2.19.1, vcpkg port revision 1 | MIT; binary `licenses/Little-CMS.txt` |
| zlib | 1.3.2, vcpkg port revision 2 | Zlib; binary `licenses/zlib.txt` |
| libjpeg-turbo | 3.2.0 | IJG and BSD-3-Clause, plus component notices; binary `licenses/libjpeg-turbo.txt` |
| JasPer | 4.2.9 | JasPer License Version 2.0; binary `licenses/JasPer.txt` |
| GLM | 1.0.3 | MIT option selected; binary `licenses/GLM.txt` |
| GLFW | 3.4 | Zlib; official source license and binary combined notices |
| Dear ImGui docking | `f4d9359095eff3eb03f685921edc1cf0e37b1687`, 1.92.0 WIP / 19193 | MIT; `libs/ImGui/LICENSE.txt` and combined notices |
| stb_rect_pack / stb_textedit / stb_truetype | 1.01 / 1.14 / 1.26, ImGui's embedded versions | MIT option; retained source notices and combined notices |
| ProggyClean | Embedded in `imgui_draw.cpp` | MIT, copyright 2004–2005 Tristan Grimmer; combined notices |
| GLAD generated loader | Generator 0.1.36, generated 2024-08-16 | Generated code offered as Public Domain / WTFPL / CC0; Khronos material retains its own terms; see below |
| Khronos `khrplatform.h` | Vendored header, copyright 2008–2018 Khronos Group | MIT-style grant in the header and combined notices |
| Noto Sans CJK SC Regular | 2.004, `523d033d6cb47f4a80c58a35753646f5c3608a78` | OFL-1.1; `resources/fonts/OFL-1.1.txt`; copyright 2014–2021 Adobe |
| vcpkg recipes | Pinned port tree per source lock | MIT, Microsoft; `third-party/vcpkg-ports/LICENSE.txt` |

The source package contains the exact package versions, source archive hashes, port revisions and local transformations in `docs/DEPENDENCIES.md`, `third-party/sources.lock.json` and per-port SPDX records. Its CycloneDX inventory at `third-party/sbom.cdx.json` covers linked and bundled components; it is an inventory, not a claim that all possible vulnerabilities have been excluded. These paths refer to the source package, which is separate from the program ZIP.

LibRaw's alternative CDDL text is intentionally retained in upstream source and the copied copyright file. YUVRaw selects LGPL 2.1; retaining the alternative text does not select it. The application and matching library sources plus build patches permit relinking. Instructions are in `SOURCE_DISTRIBUTION.md` beside the program, or `docs/SOURCE_DISTRIBUTION.md` in the source package.

LibRaw's [upstream COPYRIGHT](https://github.com/LibRaw/LibRaw/blob/0.22.2/COPYRIGHT) also identifies DCB demosaicing and FBDD denoising by Jacek Gozdz (2010) under a three-clause BSD license, the X3F library by Roland Karlsson (2010) under a BSD-style license, and Adobe DNG SDK 1.4 fragments under MIT (Adobe Systems Incorporated, 2005). Original BSD blocks from `src/demosaic/dcb_demosaic.cpp` and `src/x3f/x3f_utils_patched.cpp` are reproduced in `licenses/LibRaw-components.txt`; all upstream source notices remain intact. That file also supplies the standard MIT terms from SPDX based on the license identifier in upstream COPYRIGHT, clearly distinguished from text extracted from an Adobe file. X3F tools are not enabled in this build. The Adobe attribution describes fragments already incorporated by LibRaw; the separate Adobe DNG SDK is not a bundled or enabled dependency.

GLAD's generator source is MIT-licensed; its generated loader has a separate upstream grant. Khronos GL specification inputs are described by upstream as Apache-2.0 and accompanying Khronos headers carry their own notices. The supplied generated files are the build inputs; YUVRaw does not require rerunning the generator. See the upstream [generated-code FAQ](https://github.com/Dav1dde/glad/blob/v0.1.36/README.md#whats-the-license-of-glad-generated-code) and [license file](https://github.com/Dav1dde/glad/blob/v0.1.36/LICENSE).

Microsoft YaHei, when present, is loaded from Windows and is not redistributed. The complete unmodified Noto font is the bundled Chinese fallback. Its runtime glyph-atlas subset is documented in `resources/fonts/README.md`.

Windows WIC, OpenGL/D3D/DXGI system components, Windows fonts and the Microsoft Visual C++ runtime are platform prerequisites, not bundled third-party source.
