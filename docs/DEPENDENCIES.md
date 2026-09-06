English | [简体中文](DEPENDENCIES.zh-CN.md)

# Dependency origins, build configuration, and SBOM

Current versions are defined by `vcpkg.json`, `third-party/sources.lock.json`, and source-bundle provenance. Update this page and the [CycloneDX SBOM](../third-party/sbom.cdx.json) after upgrades. The SBOM records component identities and relationships; it does not replace per-file provenance checks or security review.

## vcpkg

Baseline: `7ff71c68261ecf3b60cdb8772f2e6759ee0a71b1`. The application uses `x64-windows-static-md` and thread-safe LibRaw. OpenMP is disabled.

| Package | Version / port revision | Source |
|---|---|---|
| libraw | 0.22.2 | [LibRaw](https://github.com/LibRaw/LibRaw/tree/0.22.2) |
| lcms | 2.19.1#1 | [Little CMS](https://github.com/mm2/Little-CMS/tree/lcms2.19.1) |
| zlib | 1.3.2#2 | [zlib](https://github.com/madler/zlib/tree/v1.3.2) |
| libjpeg-turbo | 3.2.0 | [libjpeg-turbo](https://github.com/libjpeg-turbo/libjpeg-turbo/tree/3.2.0) |
| jasper | 4.2.9 | [JasPer](https://github.com/jasper-software/jasper/tree/version-4.2.9) |
| glm | 1.0.3 | [GLM](https://github.com/g-truc/glm/tree/1.0.3) |

`dng-lossy` enables the JPEG 8 ABI; zlib DNG deflate and LCMS color support are compiled in. The vcpkg recipe still restores JasPer, but the current LibRaw 0.22.2 CMake configuration disables the RedCine/JasPer path for versions 0.22 onward.

`third-party/vcpkg-ports/` contains complete actual port trees, including patches. `sources.lock.json` records archive SHA512 and recipe SHA256 hashes. LibRaw-cmake is pinned to `eb98e4325aef2ce85d2eb031c2ff18640ca616d3`; the build applies `dependencies.patch` and `fix-install.patch` and replaces CMake helper files. Installed static headers and pkg-config files undergo additional recipe-defined transformations. Its BSD-3-Clause license accompanies the original archive. This build must not be described as wholly unmodified upstream source.

## GLFW

The existing Visual Studio project compiles Windows sources from [official GLFW 3.4](https://github.com/glfw/glfw/tree/3.4).

## Dear ImGui

The baseline is official docking commit [f4d9359095eff3eb03f685921edc1cf0e37b1687](https://github.com/ocornut/imgui/tree/f4d9359095eff3eb03f685921edc1cf0e37b1687), dated 2025-04-10 and marked `1.92.0 WIP` / `IMGUI_VERSION_NUM=19193`.

The local `imconfig.h` change is a trailing blank line, retained in [imgui-local.patch](../third-party/imgui-local.patch). Per-file hashes are in [vendored-provenance.json](../third-party/vendored-provenance.json). `ImGui.vcxproj/.filters` provide MSBuild integration. Only `imgui_impl_glfw` and `imgui_impl_opengl3` backends compile. Embedded ProggyClean retains its MIT notice.

## GLAD generation configuration

The headers of `libs/Glad/include/glad/glad.h` and `libs/Glad/src/glad.c` record:

- Generator: GLAD 0.1.36, C/C++; generated 2024-08-16 16:21:36.
- API: `gl=4.6,gles2=3.2`; spec `gl`; profile `compatibility`.
- Extensions: `GL_EXT_YUV_target`, `GL_OES_EGL_image`, `GL_OES_EGL_image_external`, `GL_OES_EGL_image_external_essl3`.
- Loader true, Local files false, Omit khrplatform false, Reproducible false.

Headers retain the full command line and generation-service URL. Generator code is traceable to [v0.1.36](https://github.com/Dav1dde/glad/tree/v0.1.36), but the online Khronos XML revision used originally was not recorded. Regeneration today is not guaranteed to be byte-identical. Releases provide and verify the complete existing generated files; builds do not regenerate GLAD. GL4.6/ES3.2 generator settings do not change the application's OpenGL 3.3 core minimum.

## Fonts and platform components

The Chinese fallback is complete, unmodified Noto Sans CJK SC Regular 2.004, commit `523d033d6cb47f4a80c58a35753646f5c3608a78`. [resources/fonts/README.md](../resources/fonts/README.md) records its license, original URL, and SHA256. The runtime atlas rasterizes common Chinese, Latin, and all bilingual UI resource characters; rare filename characters are not guaranteed. This does not modify the distributed full OTF.

Microsoft YaHei is obtained from Windows' system font directory and is not redistributed. WIC decoder capabilities depend on Windows and installed extensions; their versions are not pinned project dependencies. MSVC CRT is supplied through the Microsoft runtime prerequisite. Windows SDK, compilers, Git, CMake, and vcpkg are build tools, not copied as application source.

## Updating

Review upstream changes and enabled codec features before upgrading. Update source locks, recipes, archive hashes, licenses, and SBOM; run affected tests; rebuild from the delivered source bundle; and publish matching binaries and source. ImGui or GLAD changes also require updated per-file provenance and actual local patches.
