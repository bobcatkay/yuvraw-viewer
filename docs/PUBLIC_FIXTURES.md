English | [简体中文](PUBLIC_FIXTURES.zh-CN.md)

# Public synthetic fixtures and loading-boundary regression

All public fixtures are generated directly from formulas by `tests/PublicFixtures.h` and `tests/GeneratePublicFixtures.cpp`, under the project's GPL-3.0-only license. The generator reads no photographs, camera files, or external assets. Outputs contain no personal paths, shooting information, or camera serial numbers. The DNG camera name `YUVRaw Procedural RGGB` is fictional.

## Generate and open

On Windows with the MSVC C++ toolchain, run at the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/Generate-PublicFixtures.ps1
```

The default output is Git-ignored `artifacts/public-fixtures`; use `-OutputDirectory` to choose another directory. Output bytes are deterministic; files with the same names are overwritten. `manifest.json` records SHA-256 and byte counts for six files. Public source needs only the generator and shared header; rebuilding does not depend on generated fixtures.

| File | Parameters | Expected result |
|---|---|---|
| `colorbars_64x48_RGBA8.raw` | RGBA8, 64×48, stride 256 | White, yellow, cyan, green, magenta, red, blue, black from left to right; alpha 255 throughout |
| `gradient_64x48_stride80_NV21.yuv` | NV21, 64×48, stride 80, limited-range SDR | Horizontal black-to-white gradient; active Y=16..235, VU=128; 16 padding bytes per row set to 0xA5 |
| `gradient_64x48_stride144_P010.yuv` | P010, 64×48, stride 144, limited-range SDR | Horizontal black-to-white gradient; Y=64..940, UV=512 stored in the high 10 bits of little-endian 16-bit words; 16 padding bytes per row |
| `gradient_64x48_Bayer12.raw` | Bayer12, 64×48, stride 128, RGGB, little-endian, low-bit-aligned | Equal values within each 2×2 CFA cell; horizontal 0..4095 gradient for basic Bayer preview |
| `two_frames_64x48_stride80_NV21.yuv` | NV21, 64×48, stride 80 | Concatenated forward and reverse gradients; the application reads only the first frame using the parameters, with a file-size warning about extra data |
| `synthetic_64x48.dng` | Self-describing; no manual parameters | 64×48 sRGB RGBA8, fully opaque, increasing brightness from left to right |

Examples:

```powershell
& ./x64/Debug/YUVRaw.exe ./artifacts/public-fixtures/gradient_64x48_stride80_NV21.yuv --format NV21 --width 64 --height 48 --stride 80 --range limited --transfer sdr
& ./x64/Debug/YUVRaw.exe ./artifacts/public-fixtures/gradient_64x48_stride144_P010.yuv --format P010 --width 64 --height 48 --stride 144 --range limited --transfer sdr
& ./x64/Debug/YUVRaw.exe ./artifacts/public-fixtures/synthetic_64x48.dng
```

P010 specifies storage layout, not HDR. This fixture uses an SDR gradient. There is no video playback, frame indexing, or `--frame` option; the concatenated fixture checks first-frame boundaries.

## DNG structure and test boundaries

The synthetic DNG is little-endian TIFF with one IFD, one strip, and uncompressed 16-bit RGGB CFA. White level is 4095, black level 0, AsShotNeutral=(1,1,1), ColorMatrix1 is the identity, and the calibration illuminant is D65. The matrix supplies valid decodable metadata; it represents no actual camera and proves no camera color accuracy. No preview, GPS, original photograph, or personal information is embedded.

Structure follows the [Adobe DNG 1.6 specification](https://helpx.adobe.com/content/dam/help/en/photoshop/pdf/dng_spec_1_6_0_0.pdf) and [TIFF/DNG tag reference](https://www.loc.gov/preservation/digital/formats/content/tiff_tags.shtml).

`TestDngImageLoader` checks dimensions, format, stride, byte count, alpha, nonconstant output, and gradient direction. Bad headers, missing strip data, and oversized dimensions verify that the same loader can recover and load valid DNG. Another procedural 1024×1024 DNG tests low-memory policy: with a 1 MiB limit, metadata reads successfully but LibRaw rejects its 2 MiB RAW data during unpacking. Factory tests also check error outputs and self-describing-format dispatch. These cover application loading boundaries, not all cameras, compressed DNGs, or real HDR hardware.

## Automated tests

Build the solution to restore pinned LibRaw dependencies, then run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_tests.ps1
# Select loading-boundary suites in PowerShell:
& ./tests/run_tests.ps1 -SuiteNames TestWicImageLoader,TestRawImageLoader,TestDngImageLoader -Analyze
& ./tests/run_tests.ps1 -SuiteNames TestWicImageLoader,TestRawImageLoader,TestDngImageLoader -EnableAddressSanitizer
```

The default includes the 17 existing CPU suites plus WIC boundaries and public DNG. DNG no longer requires private input. `-DngFixturePath <local-file>` adds one real-DNG smoke test after public DNG tests; private fixture dimensions are not fixed to a specific image. Outputs go to `artifacts/cpu-tests/{normal,analyze,asan}`.

ASan instruments this project's C++ loading code and tests. System WIC and default vcpkg binaries are not instrumented, so full dependency ASan coverage must not be claimed. RAW/WIC retain STL container annotations. Only DNG uses `_DISABLE_STL_ANNOTATION` when mixed-linking uninstrumented static LibRaw to preserve ODR consistency, while retaining project address instrumentation. This does not check access into reserved but unused container capacity. See [MSVC mixed-linking requirements](https://learn.microsoft.com/en-us/cpp/sanitizers/error-container-overflow).

WIC tests enumerate format metadata, then instantiate matching COM decoder classes without reading files, reporting PNG/WebP/HEIC/HEIF as `AVAILABLE`, `MISSING`, or `UNKNOWN`. Windows can retain metadata without the actual decoder class; extension lists alone do not prove availability.

Only no match, or all matching classes explicitly returning `REGDB_E_CLASSNOTREG`, establishes absence and permits WebP readback `SKIP`. Other activation errors remain `UNKNOWN`; readback failures with an activatable decoder remain `FAIL`. One activatable candidate suffices; a missing class cannot override another candidate's unknown error. Export tests still validate encoding without a WebP decoder and skip only readback. Class activation does not imply support for every HEIF compression variant; no automated HEIF-encoded fixture round trip is provided.

## Private-file rules

- `artifacts/public-fixtures` is only for this generator's public fixtures. Never copy real originals there for publication.
- Keep private input outside the repository or in an ignored local directory; pass it explicitly through `-DngFixturePath`. CI does not use that parameter.
- CI/Release uploads use explicit filenames, never recursive uploads of private-input directories, the workspace, or user data.
- Review paths and metadata in private-fixture logs and exported images before sharing; public tests do not require them.
