[English](DEPENDENCIES.md) | 简体中文

# 依赖来源、构建配置与 SBOM

版本以当前 `vcpkg.json`、`third-party/sources.lock.json` 和源码包 provenance 为准。升级后必须同步本页和 [CycloneDX SBOM](../third-party/sbom.cdx.json)。SBOM 记录组件身份和关系，不代替逐文件来源校验或安全审查。

## vcpkg

baseline：`7ff71c68261ecf3b60cdb8772f2e6759ee0a71b1`。应用使用 `x64-windows-static-md`，链接 thread-safe LibRaw；OpenMP 未启用。

| 包 | 版本 / port revision | 来源 |
|---|---|---|
| libraw | 0.22.2 | [LibRaw](https://github.com/LibRaw/LibRaw/tree/0.22.2) |
| lcms | 2.19.1#1 | [Little CMS](https://github.com/mm2/Little-CMS/tree/lcms2.19.1) |
| zlib | 1.3.2#2 | [zlib](https://github.com/madler/zlib/tree/v1.3.2) |
| libjpeg-turbo | 3.2.0 | [libjpeg-turbo](https://github.com/libjpeg-turbo/libjpeg-turbo/tree/3.2.0) |
| jasper | 4.2.9 | [JasPer](https://github.com/jasper-software/jasper/tree/version-4.2.9) |
| glm | 1.0.3 | [GLM](https://github.com/g-truc/glm/tree/1.0.3) |

`dng-lossy` 启用 JPEG 8 ABI，并编入 zlib DNG deflate 与 LCMS 色彩支持。JasPer 仍由 vcpkg 配方恢复，但当前 LibRaw 0.22.2 CMake 对 0.22 及以后禁用 RedCine/JasPer 通路。

`third-party/vcpkg-ports/` 保存实际 port tree 的完整文件，包含补丁；`sources.lock.json` 保存归档 SHA512 和配方 SHA256。LibRaw-cmake 固定 `eb98e4325aef2ce85d2eb031c2ff18640ca616d3`，构建时应用 `dependencies.patch`、`fix-install.patch` 并覆盖 CMake 辅助文件；安装静态头文件和 pkg-config 文件还会有配方中列出的转换。其 BSD-3-Clause 许可随原归档交付。不能将这套构建描述为未经任何修改的上游源码。

## GLFW

采用 [GLFW 3.4 官方源码](https://github.com/glfw/glfw/tree/3.4)，由现有 Visual Studio 项目编译 Windows 相关源文件。

## Dear ImGui

基准是官方 docking commit [f4d9359095eff3eb03f685921edc1cf0e37b1687](https://github.com/ocornut/imgui/tree/f4d9359095eff3eb03f685921edc1cf0e37b1687)（2025-04-10），标记 `1.92.0 WIP` / `IMGUI_VERSION_NUM=19193`。

本地 `imconfig.h` 仅多一个末尾空行，差异保存在 [imgui-local.patch](../third-party/imgui-local.patch)。逐文件哈希见 [vendored-provenance.json](../third-party/vendored-provenance.json)。`ImGui.vcxproj/.filters` 提供 MSBuild 集成，仅编译 `imgui_impl_glfw` 和 `imgui_impl_opengl3` 后端。内嵌 ProggyClean 保留 MIT 声明。

## GLAD：生成配置

`libs/Glad/include/glad/glad.h` 与 `libs/Glad/src/glad.c` 的头部记录：

- 生成器：GLAD 0.1.36，C/C++；生成时间 2024-08-16 16:21:36。
- API：`gl=4.6,gles2=3.2`；spec `gl`；profile `compatibility`。
- 扩展：`GL_EXT_YUV_target`、`GL_OES_EGL_image`、`GL_OES_EGL_image_external`、`GL_OES_EGL_image_external_essl3`。
- Loader 为 true，Local files 为 false，Omit khrplatform 为 false，Reproducible 为 false。

头部保留完整命令行和生成服务 URL。生成器代码可定位到 [v0.1.36](https://github.com/Dav1dde/glad/tree/v0.1.36)，但原生成时使用的在线 Khronos XML revision 未记录，不能保证今天重新执行产生逐字节相同文件。发行时直接提供并校验现有完整生成文件；构建不重新生成 GLAD。生成器选项涉及 GL4.6/ES3.2，不改变本应用 OpenGL 3.3 core 的最低要求。

## 字体与平台组件

中文 fallback 是未裁剪、未修改的 Noto Sans CJK SC Regular 2.004，commit `523d033d6cb47f4a80c58a35753646f5c3608a78`。许可证、原始地址和字体 SHA256 见 [resources/fonts/README.md](../resources/fonts/README.zh-CN.md)。运行时仅把常用中文、Latin 默认范围及全部中英文 UI 资源字符栅格化进图集，罕见文件名字符不保证覆盖；这不会修改分发的完整 OTF。

Microsoft YaHei 由 Windows 系统字体目录提供，不随包复制。WIC decoder 能力由 Windows/已安装扩展决定，版本不应写成项目固定依赖。MSVC CRT 通过微软运行库安装前提提供，Windows SDK、编译器、Git、CMake 和 vcpkg 是构建工具，不作为应用源码的一部分复制。

## 更新步骤

更新依赖前检查上游变更及启用的 codec 功能；更新来源锁、配方、归档哈希、许可证与 SBOM，运行受影响测试，从交付源码包重建，再发布对应二进制和源码。修改 ImGui 或 GLAD 时须更新逐文件 provenance 和真实本地补丁。
