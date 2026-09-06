English | [简体中文](SOURCE_DISTRIBUTION.zh-CN.md)

# YUVRaw source distribution and LibRaw licensing

YUVRaw's own source code and application artwork are licensed under the GNU
General Public License version 3 only (`GPL-3.0-only`); see `LICENSE`. Third-party
files retain their upstream licenses and copyright notices.

YUVRaw uses LibRaw 0.22.2 under the GNU Lesser General Public License version 2.1
only (`LGPL-2.1-only`). This selects the LGPL option in LibRaw's dual-license
offer. Upstream's CDDL text and original dual-license notices are retained;
their presence does not change YUVRaw's selection. See `licenses/LGPL-2.1.txt`
and `licenses/LibRaw.txt` in the binary package.

## Files to distribute

Every release must offer these files together from the same download location:

- `YUVRaw-<version>-windows-x64.zip`: executable, GPL/LGPL texts, third-party notices and
  `SOURCE_CODE.txt` identifying the matching source archive.
- `YUVRaw-<version>-source.zip`: corresponding application and library sources,
  build recipes, patches, provenance records and a per-file `SHA256SUMS` list.
- `YUVRaw-<version>-SHA256SUMS.txt`: SHA-256 hashes of both ZIP files.

This supplies the source and relinking materials described in LGPL 2.1 section
6(a), with equivalent download access under section 6(d). Keep the source
available whenever the matching binary is offered. A link to an upstream
branch alone does not identify all the files used to build this application.

Private test images are excluded. They are not needed to compile the program
or relink it against a modified LibRaw. Public tests generate their own input;
the optional DNG smoke test accepts a local image supplied by its user.

## Source archive contents

- The application compilation inputs, resources, public artwork, solution,
  project files, test code and packaging scripts.
- The vendored ImGui and GLAD source used by the application, including
  Khronos/stb/ProggyClean notices in their source files.
- GLFW 3.4 is built from the vendored, hash-checked upstream sources by the solution. Its original archive is also supplied; no prebuilt GLFW library is required.
- Exact upstream source archives for LibRaw, LibRaw-cmake, Little CMS, zlib,
  libjpeg-turbo, JasPer and GLM, under `third-party/upstream/`.
- Matching vcpkg recipes and patches under `third-party/vcpkg-ports/`, plus
  the installed packages' license and SPDX records under
  `third-party/provenance/`.

The packaging script verifies source archive hashes, recipe hashes, installed
package versions/revisions and GLFW source hashes before producing the source ZIP.
Dependency changes require updating `vcpkg.json`, `sources.lock.json`, the
recipe snapshots and the application notices together. Preserve each recipe's
exact upstream bytes, including its original LF or CRLF endings. The recipe
tree uses `-text` in `.gitattributes` to prevent Git from changing those bytes.

## Changes applied to LibRaw

The current vcpkg recipe fetches LibRaw 0.22.2 and LibRaw-cmake commit
`eb98e4325aef2ce85d2eb031c2ff18640ca616d3`. It applies `dependencies.patch` and
`fix-install.patch` to the CMake helper sources, then copies `CMakeLists.txt`
and the `cmake/` directory into the LibRaw source tree. It also replaces
`#ifdef LIBRAW_NODLL` with `#if 1` in the installed `libraw_types.h` for static
library builds. The recipe documents the exact transformations; the decoder
sources are not changed by these two patches.

The `dng-lossy` feature is enabled. The application uses the thread-safe
`raw_r.lib` / `raw_rd.lib` libraries with static dependency libraries and the
dynamic MSVC runtime. The upstream CMake helper and vcpkg recipes retain their
own licenses, included with their source.

## Build and relink

Install Visual Studio's C++ desktop workload with MSVC v145 and a Windows SDK,
and use a vcpkg checkout that supports the pinned baseline. Visual Studio,
the Windows SDK, Git, CMake and vcpkg are general-purpose development tools;
they are not bundled with the source archive. The source package's `docs/BUILDING.md` gives
the solution build command.

To use the supplied archives instead of downloading their source again,
point `VCPKG_DOWNLOADS` at the extracted `third-party/upstream` directory.
vcpkg may still download build tools and registry metadata. Build `YUVRaw.sln`
using `Release | x64`. It restores the versions fixed in `vcpkg.json`.

To modify and relink LibRaw:

1. Extract the supplied LibRaw archive and make your decoder changes. Save
   those changes as a unified patch, with paths relative to the upstream
   LibRaw source root.
2. Copy `third-party/vcpkg-ports/libraw` to a local overlay-port directory.
   Put your patch beside `portfile.cmake`, and add it to a `PATCHES` argument
   of the **first** `vcpkg_from_github` call, which fetches LibRaw itself.
   The second call fetches LibRaw-cmake and already has its own build patches.
3. Restore dependencies with your overlay, using the same triplet and project
   install directory. For example, with `$vcpkg` pointing to `vcpkg.exe`:

   ```powershell
   & $vcpkg install --triplet=x64-windows-static-md `
     --x-manifest-root=. --x-install-root=vcpkg_installed `
     --overlay-triplets=vcpkg-triplets --overlay-ports=local-ports
   ```

4. Build the solution with the same overlay visible to its dependency restore.
   Set `VCPKG_OVERLAY_PORTS` to the absolute `local-ports` directory in that
   PowerShell process, then execute the solution build command from the build instructions.
   The application will relink with your newly built LibRaw library.

The shipped source lock describes the original release. If distributing your
modified version, update its source snapshot/lock and notices to describe the
modified recipe, and deliver your matching source and binary packages.

To regenerate the original source package without building another release:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/Export-SourceBundle.ps1 `
  -DestinationPath artifacts/YUVRaw-source.zip `
  -SourceCacheDirectory third-party/upstream -Offline
```

Run this after restoring the original dependency versions, since the script
checks their installed provenance. In a normal checkout, the default source
cache is `artifacts/source-cache`; missing archives are downloaded and hash
checked unless `-Offline` is set.

Official references: [LibRaw's license choice](https://www.libraw.org/docs),
[LGPL 2.1](https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html), and
[GPL source distribution FAQ](https://www.gnu.org/licenses/gpl-faq.html.en#AnonFTPAndSendSources).
