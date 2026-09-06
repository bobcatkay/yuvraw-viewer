[English](BUILDING.md) | 简体中文

# 开发、构建与测试

以下命令在仓库根目录的 PowerShell 中运行。构建与发布以 Windows x64 为目标。

## 工具链与依赖

- Visual Studio 2026 的“使用 C++ 的桌面开发”工作负载，MSVC v145 和 Windows SDK。
- PowerShell、Git，以及完整 vcpkg checkout 或 Visual Studio 的 vcpkg 组件。
- 首次依赖恢复需要网络；离线对应源码包仍可能需要下载通用构建工具及 vcpkg registry 元数据。

项目自动按 `vcpkg.json` 的 baseline、版本和 port-version 恢复依赖到仓库内 `vcpkg_installed/`。使用 `x64-windows-static-md` triplet：第三方依赖静态链接、MSVC 运行库动态链接。GLFW、ImGui 和 GLAD 的应用构建输入位于 `libs/`。来源与许可见[依赖清单](DEPENDENCIES.zh-CN.md)。

vcpkg 定位顺序为 `VcpkgRoot` MSBuild 属性 / `VCPKG_ROOT` 环境变量、仓库上级的 `vcpkg`、Visual Studio 自带目录。自定义位置时把 `$env:VCPKG_ROOT` 设为自己的完整 checkout 路径。无需运行全局 `vcpkg integrate install`，也不要把链接路径指向另一份 `installed/`。

## 构建

必须构建解决方案，避免依赖项目的 `SolutionDir` 退化成子目录：

```powershell
$msbuild = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
  -latest -products * -requires Microsoft.Component.MSBuild `
  -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
& $msbuild YUVRaw.sln -p:Configuration=Debug -p:Platform=x64 -m:1 -nologo
if ($LASTEXITCODE -ne 0) { throw 'Debug build failed.' }
& $msbuild YUVRaw.sln -p:Configuration=Release -p:Platform=x64 -m:1 -nologo
if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }
```

产物分别为 `x64/Debug/YUVRaw.exe` 和 `x64/Release/YUVRaw.exe`。新增 `.cpp/.h` 必须登记到 `.vcxproj` / `.filters`，并核对源码交付清单。每次构建会复制必需许可文件；缺少许可应直接失败。

## 公共测试

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_gl_format_validation.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools/Generate-PublicFixtures.ps1
```

CPU 测试覆盖格式几何、参数解析、加载边界、色彩参考、采样、设置/缓存、异步邮箱、WebP 和导出；以运行脚本列出的套件为准。公共 DNG 来自原创生成器。合成样例默认输出 `artifacts/public-fixtures/`，参数及预期画面见 [PUBLIC_FIXTURES.md](PUBLIC_FIXTURES.zh-CN.md)。不需要从项目维护者取得私有原图。

GPU 脚本会构建 Debug，然后验证隐藏共享 Context 上传、所有已登记格式、SDR 色彩与 fp16 HDR 数值。需要实际可用的 OpenGL 3.3 驱动；缺少环境须记为未验证，不能用 CPU 通过代替。显示器实际 HDR、主副屏切换和 DPI 仍需手动验收。

WIC 可选 codec 的测试须分别记录通过、失败与因未安装而跳过。WebP 编码逻辑可以单独验证，但使用系统解码器的往返测试取决于该 codec。不要为让测试通过而修改数学真值或放宽容差。

运行 	ests/run_ui_resource_tests.ps1 检查双语字形覆盖与布局，运行 	ests/TestDocumentation.ps1 检查公开文档配对及相对链接。

## 私有样例仅限本地

额外真实 DNG 验证可显式传入本地路径：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_tests.ps1 `
  -DngFixturePath .\private-fixtures\camera.dng
```

`private-fixtures/` 以及 RAW/YUV/DNG 等原图扩展名已被 Git 忽略，公开合成结果也留在被忽略的 `artifacts/`。不要用 `git add -f` 绕过边界。截图可以展示授权画面，但必须脱敏；CI 只上传明确列出的报告/发行附件，不扫描或上传工作目录中的图片、缓存或完整日志目录。

原有 `tools/Convert-TestImages.ps1` 面向特定本地 dump 的批量转换分析，不是任意输入的通用格式识别器，也不属于公共回归的必需步骤。用自己的输入/输出目录和 `(Get-Command ffmpeg).Source` 指定 FFmpeg，不要提交其转换输出。

## 命令行选项

| 选项 | 值 |
|---|---|
| 文件路径 | 第一个非选项参数，含空格时用双引号 |
| `--format` / `-f` | 格式表名称，忽略大小写；例如 NV21、P010、RGB8、RAW_SENSOR |
| `--width` / `-w`，`--height` / `-h` | 可见像素宽高 |
| `--stride` / `-s` | 行跨度，单位为字节，0 使用紧凑布局 |
| `--bits` / `--bits-per-pixel` / `-b` | 允许配置的格式使用的有效位深 |
| `--compare` / `-c` | 对比图路径；初始裸图参数沿用主图 |
| `--matrix` | bt601、bt709、bt2020 |
| `--range` | limited、full |
| `--primaries` | bt709、bt2020、p3、bt601-525、bt601-625 |
| `--transfer` | sdr、bt1886、pq、hlg、linear |
| `--tonemap` | clip、reinhard、aces |
| `--refwhite` | 参考白亮度，单位 nit |
| `--exposure` | 曝光档数 |
| `--out-of-range` | 开启超范围高亮 |
| `--no-hdr` | 强制 SDR 输出 |

当前没有 `--frame`、播放或帧索引功能；多帧裸文件只读首帧。CFA、字节序和有效位对齐由格式及属性面板控制，不要假定存在未列出的命令行参数。

## 版本与发行包

先显式选择版本，再构建；普通构建和打包不能消费新版本：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/Set-Version.ps1 -Version 0.0.24
powershell -NoProfile -ExecutionPolicy Bypass -File build-release.ps1 -Platform x64 -DryRun
powershell -NoProfile -ExecutionPolicy Bypass -File build-release.ps1 -Platform x64 -ExpectedVersion 0.0.24
```

版本号只是示例，发布时使用待发布版本。输出位于 `artifacts/releases/<version>/x64/`：

- `YUVRaw-<version>-windows-x64.zip`
- `YUVRaw-<version>-source.zip`
- `YUVRaw-<version>-SHA256SUMS.txt`

源码默认缓存于 `artifacts/source-cache/`；`-SourceCacheDirectory <目录> -OfflineSources` 可以使用已校验缓存。缺少依赖来源、许可或必需资源必须失败，不能发布部分产物。二进制 ZIP 以 Microsoft Visual C++ x64 运行库为系统前提，不自行捆绑 CRT；接收者可使用微软[官方安装包](https://aka.ms/vc14/vc_redist.x64.exe)，版本须不低于构建工具链。[微软说明](https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist)

对生成的源码 ZIP 运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/TestSourceDistribution.ps1 `
  -SourceArchivePath .\artifacts\releases\0.0.24\x64\YUVRaw-0.0.24-source.zip
```

从交付源码解压副本恢复依赖并重建，核对版本与全部打包输入。LibRaw 修改/重链接步骤见 [SOURCE_DISTRIBUTION.md](SOURCE_DISTRIBUTION.zh-CN.md)。发布前在未安装开发工具的 Windows 上测试解压后的程序包，并同时提供二进制 ZIP、匹配源码 ZIP 和校验文件。

## GitHub 自动发布

将更新后的 [Windows 工作流](../.github/workflows/windows.yml) 提交并推送后，推送 `v主版本.次版本.修订号` 格式的 tag，即可在 GitHub 上自动完成发版。tag 指向的提交必须包含该工作流，且 `src/Core/FAppVersion.h` 中的版本须与 tag 一致。例如，准备下一个版本并推送 tag：

```powershell
./tools/Set-Version.ps1 -Version 0.0.24
git add src/Core/FAppVersion.h
git commit -m "Set release version to 0.0.24 in the shared version header"
git tag -a v0.0.24 -m "YUVRaw v0.0.24"
git push origin main
git push origin v0.0.24
```

请将 `0.0.24` 换成待发布版本。tag 推送后，无需本地编译或手动发布：GitHub 使用 Visual Studio 2026 构建 Debug 和 Release x64，执行现有回归测试及发行包校验，然后自动生成发布说明并发布正式 Release，附上前述三份文件。构建或校验失败时不会发布。实际 GPU/HDR 与无开发环境机器的验收仍按前文手动完成。

发布任务使用 GitHub 自动提供的 `GITHUB_TOKEN`，并仅为该任务声明 `contents: write`；无需个人访问令牌或额外仓库 Secret。仓库或组织策略须允许 Actions 及该权限。普通分支 push、PR 和手动运行工作流保持原有构建行为，不会发布 Release；只有推送版本 tag 才会发布，暂不支持 `-rc.1` 等预发布后缀。

在仓库 **Actions → Windows** 查看进度，完成后到 **Releases** 下载。附件先上传到草稿，齐全后自动公开，无需人工确认草稿。上传失败时，在原 tag 的运行记录中选择 **Re-run failed jobs** 可继续完成草稿；已公开的 Release 不会被覆盖。若需要修改源码，请使用新版本和新 tag，不要移动已发布的 tag。
