English | [简体中文](BUILDING.zh-CN.md)

# Development, building, and testing

Run the commands below in PowerShell at the repository root. Build and release instructions target Windows x64.

## Toolchain and dependencies

- Visual Studio 2026 with Desktop development with C++, MSVC v145, and a Windows SDK.
- PowerShell, Git, and a complete vcpkg checkout or the Visual Studio vcpkg component.
- Initial dependency restoration requires network access. Even an offline corresponding-source bundle may need to download general build tools and vcpkg registry metadata.

The project restores dependencies into repository-local `vcpkg_installed/` using the baseline, versions, and port revisions in `vcpkg.json`. The `x64-windows-static-md` triplet links dependencies statically and the MSVC runtime dynamically. GLFW, ImGui, and GLAD build inputs reside in `libs/`. See the [dependency inventory](DEPENDENCIES.md) for origins and licenses.

vcpkg discovery checks the `VcpkgRoot` MSBuild property / `VCPKG_ROOT` environment variable, a sibling `vcpkg` directory, then Visual Studio's installation. For a custom location, set `$env:VCPKG_ROOT` to your complete checkout. Global `vcpkg integrate install` is unnecessary; do not point linker paths at a different `installed/` tree.

## Build

Build the solution so dependency projects do not resolve `SolutionDir` to a subdirectory:

```powershell
$msbuild = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
  -latest -products * -requires Microsoft.Component.MSBuild `
  -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
& $msbuild YUVRaw.sln -p:Configuration=Debug -p:Platform=x64 -m:1 -nologo
if ($LASTEXITCODE -ne 0) { throw 'Debug build failed.' }
& $msbuild YUVRaw.sln -p:Configuration=Release -p:Platform=x64 -m:1 -nologo
if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }
```

Outputs are `x64/Debug/YUVRaw.exe` and `x64/Release/YUVRaw.exe`. Register new `.cpp/.h` files in `.vcxproj` / `.filters` and review the source-delivery inventory. Each build copies required licenses; missing licenses must fail the build.

## Public tests

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_gl_format_validation.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools/Generate-PublicFixtures.ps1
```

CPU suites cover format geometry, parameter parsing, loading boundaries, color references, sampling, settings/cache, asynchronous mailboxes, WebP, and export. The runner defines the current suite list. Public DNG input comes from an original generator. Synthetic fixtures default to `artifacts/public-fixtures/`; see [PUBLIC_FIXTURES.md](PUBLIC_FIXTURES.md) for parameters and expected images. No private originals from the maintainer are needed.

The GPU script builds Debug, then checks hidden shared-context upload, registered formats, SDR color, and fp16 HDR numerics. A working OpenGL 3.3 driver is required; unavailable hardware remains unverified, regardless of CPU results. Physical HDR, primary/secondary display changes, and DPI need manual acceptance.

Record optional WIC codec results separately as pass, fail, or skipped because the codec is absent. WebP encoding can be checked independently, but system-decoder round trips depend on that codec. Never change mathematical reference values or loosen tolerances merely to pass tests.

Run `tests/run_ui_resource_tests.ps1` for bilingual glyph coverage and layout checks, and `tests/TestDocumentation.ps1` for published document pairs and relative links.

## Private fixtures stay local

Pass a local path explicitly for an additional real DNG check:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_tests.ps1 `
  -DngFixturePath .\private-fixtures\camera.dng
```

Git ignores `private-fixtures/` and RAW/YUV/DNG original-image extensions; generated public fixtures also remain under ignored `artifacts/`. Do not bypass these boundaries with `git add -f`. Screenshots may show authorized content but must be sanitized. CI uploads only explicitly listed reports/release assets, never images, caches, or entire log directories discovered across the workspace.

The existing `tools/Convert-TestImages.ps1` performs batch conversion/analysis for specific local dumps. It is neither a general format detector nor a required public-regression step. Supply your own input/output directories and FFmpeg via `(Get-Command ffmpeg).Source`; do not commit converted output.

## Command-line options

| Option | Value |
|---|---|
| File path | First non-option argument; quote paths containing spaces |
| `--format` / `-f` | Case-insensitive format-table name, such as NV21, P010, RGB8, RAW_SENSOR |
| `--width` / `-w`, `--height` / `-h` | Visible pixel dimensions |
| `--stride` / `-s` | Row stride in bytes; 0 selects tightly packed layout |
| `--bits` / `--bits-per-pixel` / `-b` | Effective bit depth for configurable formats |
| `--compare` / `-c` | Comparison path; initial raw parameters reuse the main image's |
| `--matrix` | bt601, bt709, bt2020 |
| `--range` | limited, full |
| `--primaries` | bt709, bt2020, p3, bt601-525, bt601-625 |
| `--transfer` | sdr, bt1886, pq, hlg, linear |
| `--tonemap` | clip, reinhard, aces |
| `--refwhite` | Reference-white luminance in nits |
| `--exposure` | Exposure stops |
| `--out-of-range` | Enable out-of-range highlighting |
| `--no-hdr` | Force SDR output |

There is no `--frame`, playback, or frame indexing; raw files with multiple frames read only the first. CFA, byte order, and effective-bit alignment are controlled by the format and Properties. Do not assume unlisted CLI options exist.

## Versions and release packages

Choose the version explicitly before building. Ordinary builds and packaging must not consume a new version:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/Set-Version.ps1 -Version 0.0.24
powershell -NoProfile -ExecutionPolicy Bypass -File build-release.ps1 -Platform x64 -DryRun
powershell -NoProfile -ExecutionPolicy Bypass -File build-release.ps1 -Platform x64 -ExpectedVersion 0.0.24
```

The version is an example; use your intended release version. Output under `artifacts/releases/<version>/x64/` contains:

- `YUVRaw-<version>-windows-x64.zip`
- `YUVRaw-<version>-source.zip`
- `YUVRaw-<version>-SHA256SUMS.txt`

Dependency source defaults to `artifacts/source-cache/`; `-SourceCacheDirectory <directory> -OfflineSources` uses a verified cache. Missing source, licenses, or required resources must fail packaging; do not distribute partial results. The binary ZIP requires the Microsoft Visual C++ x64 runtime and does not bundle CRT files. Recipients can use Microsoft's [official installer](https://aka.ms/vc14/vc_redist.x64.exe), with a version at least as recent as the build toolchain. [Microsoft documentation](https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist)

Validate the generated source ZIP:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/TestSourceDistribution.ps1 `
  -SourceArchivePath .\artifacts\releases\0.0.24\x64\YUVRaw-0.0.24-source.zip
```

Extract the delivered source into a separate directory, restore dependencies, rebuild, and check the version and all packaging inputs. See [SOURCE_DISTRIBUTION.md](SOURCE_DISTRIBUTION.md) for modifying/relinking LibRaw. Before publishing, test the extracted program ZIP on Windows without development tools. Publish the binary ZIP, matching source ZIP, and checksums together.

## Automatic GitHub Releases

Once the updated [Windows workflow](../.github/workflows/windows.yml) is committed and pushed, pushing a tag named `vMAJOR.MINOR.PATCH` starts the entire release process on GitHub. The tagged commit must contain the workflow, and its version in `src/Core/FAppVersion.h` must match the tag. For example, prepare the next version and push its tag:

```powershell
./tools/Set-Version.ps1 -Version 0.0.24
git add src/Core/FAppVersion.h
git commit -m "Set release version to 0.0.24 in the shared version header"
git tag -a v0.0.24 -m "YUVRaw v0.0.24"
git push origin main
git push origin v0.0.24
```

Use your intended version instead of `0.0.24`. After the tag push, no local build or manual publishing step is needed. GitHub builds Debug and Release x64 with Visual Studio 2026, runs the existing regressions and package validation, then automatically publishes a normal Release with generated release notes and all three assets listed above. A failed build or validation prevents publication. Physical GPU/HDR and clean-machine acceptance still need the manual checks described above.

The publisher uses GitHub's automatic `GITHUB_TOKEN` with job-scoped `contents: write`; no personal access token or extra repository secret is needed. Repository or organization policies must allow Actions and this permission. Branch pushes, pull requests, and manual workflow runs retain their existing build behavior and do not publish Releases. Only pushed release tags publish; prerelease suffixes such as `-rc.1` are currently unsupported.

Follow progress in the repository's **Actions → Windows**, and download completed assets from **Releases**. Assets are uploaded to a draft first, then published together automatically; there is no manual draft approval. If an upload fails, use **Re-run failed jobs** on the original tag run to resume the draft. An already published Release is left unchanged. For source changes, create a new version and tag instead of moving a published tag.
