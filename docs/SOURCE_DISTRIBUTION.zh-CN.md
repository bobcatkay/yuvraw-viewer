[English](SOURCE_DISTRIBUTION.md) | 简体中文

# YUVRaw 源码交付与 LibRaw 许可

YUVRaw 自有源码和应用图形仅采用 GNU General Public License 第 3 版（`GPL-3.0-only`），见 `LICENSE`。第三方文件保留上游许可与版权声明。

YUVRaw 使用 LibRaw 0.22.2，并仅选择 GNU Lesser General Public License 第 2.1 版（`LGPL-2.1-only`）。这选择了 LibRaw 双许可中的 LGPL 分支。上游 CDDL 条款和原始双许可声明保持不变；保留它们不改变 YUVRaw 的选择。参见程序包的 `licenses/LGPL-2.1.txt` 和 `licenses/LibRaw.txt`。

## 必须共同分发的文件

每个发行版必须从同一下载位置一并提供：

- `YUVRaw-<version>-windows-x64.zip`：可执行程序、GPL/LGPL 条款、第三方声明，以及指定匹配源码归档的 `SOURCE_CODE.txt`。
- `YUVRaw-<version>-source.zip`：对应的应用和库源码、构建配方、补丁、来源记录和逐文件 `SHA256SUMS`。
- `YUVRaw-<version>-SHA256SUMS.txt`：两份 ZIP 的 SHA-256。

这提供 LGPL 2.1 第 6(a) 节描述的源码和重链接材料，以及第 6(d) 节要求的等同下载访问。只要提供匹配的二进制，就应保持源码可获取。仅链接上游分支无法确定构建本应用使用的全部文件。

私有测试图片不包含在内。编译程序或使用修改后的 LibRaw 重链接不需要这些图片。公共测试自行生成输入；可选 DNG 冒烟测试接受用户提供的本地图片。

## 源码归档内容

- 应用编译输入、资源、公开图形、解决方案、项目文件、测试代码和打包脚本。
- 应用实际使用的内置 ImGui 和 GLAD 源码，包括源码中的 Khronos/stb/ProggyClean 声明。
- 解决方案从经过哈希检查的内置上游源码构建 GLFW 3.4。原始归档同时提供，无需预编译 GLFW 库。
- `third-party/upstream/` 中 LibRaw、LibRaw-cmake、Little CMS、zlib、libjpeg-turbo、JasPer 和 GLM 的精确上游归档。
- `third-party/vcpkg-ports/` 中匹配的 vcpkg 配方和补丁，以及 `third-party/provenance/` 中安装包的许可和 SPDX 记录。

打包脚本在生成源码 ZIP 前检查源码归档哈希、配方哈希、安装包版本/revision 及 GLFW 源码哈希。依赖变化必须同步更新 `vcpkg.json`、`sources.lock.json`、配方快照和应用声明。保留各配方上游原始字节，包括原有 LF 或 CRLF。配方目录在 `.gitattributes` 中采用 `-text`，防止 Git 改写字节。

## 对 LibRaw 应用的修改

当前 vcpkg 配方取得 LibRaw 0.22.2 和 LibRaw-cmake commit `eb98e4325aef2ce85d2eb031c2ff18640ca616d3`。它对 CMake 辅助源码应用 `dependencies.patch` 和 `fix-install.patch`，然后将 `CMakeLists.txt` 与 `cmake/` 复制到 LibRaw 源码目录。静态库构建还将安装后的 `libraw_types.h` 中 `#ifdef LIBRAW_NODLL` 替换为 `#if 1`。配方记录精确转换；上述两个补丁不修改解码器源码。

启用了 `dng-lossy`。应用使用线程安全的 `raw_r.lib` / `raw_rd.lib`，静态链接依赖库、动态链接 MSVC 运行库。上游 CMake 辅助源码和 vcpkg 配方保留独立许可，随源码交付。

## 构建与重链接

安装 Visual Studio 的 C++ 桌面工作负载、MSVC v145 和 Windows SDK，并使用支持固定 baseline 的 vcpkg checkout。Visual Studio、Windows SDK、Git、CMake 和 vcpkg 是通用开发工具，不随源码归档提供。源码包的 `docs/BUILDING.zh-CN.md` 给出解决方案构建命令。

若要使用随附归档而不再次下载源码，将 `VCPKG_DOWNLOADS` 指向解压后的 `third-party/upstream`。vcpkg 仍可能下载构建工具和 registry 元数据。以 `Release | x64` 构建 `YUVRaw.sln`，它会恢复 `vcpkg.json` 固定的版本。

修改并重链接 LibRaw：

1. 解压随附 LibRaw 归档并修改解码器。将修改保存为 unified patch，路径相对于 LibRaw 上游源码根目录。
2. 将 `third-party/vcpkg-ports/libraw` 复制到本地 overlay-port 目录。补丁放在 `portfile.cmake` 旁，并加入取得 LibRaw 本身的**第一个** `vcpkg_from_github` 调用的 `PATCHES` 参数。第二个调用取得 LibRaw-cmake，已有独立的构建补丁。
3. 使用相同 triplet 和项目安装目录，通过 overlay 恢复依赖。例如令 `$vcpkg` 指向 `vcpkg.exe`：

   ```powershell
   & $vcpkg install --triplet=x64-windows-static-md `
     --x-manifest-root=. --x-install-root=vcpkg_installed `
     --overlay-triplets=vcpkg-triplets --overlay-ports=local-ports
   ```

4. 构建解决方案，并确保依赖恢复可见同一 overlay。在该 PowerShell 进程中，将 `VCPKG_OVERLAY_PORTS` 设为 `local-ports` 的绝对目录，再运行构建说明中的解决方案命令。应用会重链接新构建的 LibRaw。

随附来源锁描述原始发行版。若分发修改版，应更新源码快照/锁和声明，描述修改后的配方，并交付匹配的源码与程序包。

不重新构建发行程序也可重新生成原始源码包：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/Export-SourceBundle.ps1 `
  -DestinationPath artifacts/YUVRaw-source.zip `
  -SourceCacheDirectory third-party/upstream -Offline
```

先恢复原始依赖版本，因为脚本会检查安装来源。在普通 checkout 中，默认源码缓存为 `artifacts/source-cache`；除非设置 `-Offline`，否则会下载缺失归档并校验哈希。

官方参考：[LibRaw 许可选择](https://www.libraw.org/docs)、[LGPL 2.1](https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html) 和 [GPL 源码交付 FAQ](https://www.gnu.org/licenses/gpl-faq.html.en#AnonFTPAndSendSources)。本文是项目说明的译文，随附 GPL/LGPL 等许可原文保持不变。
