English | [简体中文](QUICK_START.zh-CN.md)

# YUVRaw quick start

A Windows x64 image viewer and format inspection tool for RGB, grayscale, YUV, Bayer RAW, and DNG. Inspect pixels, compare images, and export results.

## Run

1. Extract the entire ZIP on Windows 10 / 11 x64 and run `YUVRaw.exe`. The GPU and vendor driver must support OpenGL 3.3 core.
2. Install the Microsoft Visual C++ x64 runtime if needed, using Microsoft's [supported redistributable downloads](https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist).
3. Keep `resources/fonts/` and the license files. The application loads bundled fonts relative to its executable; no font installation or fixed working directory is required.
4. The interface defaults to **English**. In **Settings → Language**, choose **简体中文** or **English** and apply. The change takes effect immediately and persists across restarts. Existing saved preferences remain in use.

**Help → Usage guide** covers shortcuts, canvas controls, comparison, and batch export. **Help → About → Latest version** opens downloads and release notes.

## Open, view, and export

Use **File → Open File**, `Ctrl + O`, or drag and drop. You can also open a folder and navigate its images in the file browser. Chinese and space-containing paths are supported; quote paths on the command line. Rare Chinese characters may fall outside the current UI glyph set.

Images with headers, such as PNG, JPEG, and DNG, decode automatically. For headerless `.yuv` / `.raw`, confirm the format, visible dimensions, row stride, and color interpretation in Properties; filename inference only supplies initial values. Stride is in **bytes**, including row-end padding. Effective bit depth and container bit depth are separate. Match NV12/NV21 UV order, full/limited range, matrix, primaries, and transfer function to the input.

For example, open your NV21 file with the following command; replace the example path:

```powershell
.\YUVRaw.exe "D:\Samples\测试图片\frame.yuv" --format NV21 --width 1440 --height 1920 --stride 1472
```

| Action | Input |
|---|---|
| Zoom / pan | `Ctrl + wheel` / left-drag |
| Fit / 1:1 | `Ctrl + 0` / `Ctrl + 1` |
| Reset zoom and offset | Left double-click |
| Copy probe information | Right-click a valid canvas pixel |
| Add a comparison image | File-browser context menu |
| Switch comparison image | Left-click the canvas in single-image switching mode |
| Export current image | `Ctrl + E` |

Comparison offers single-image switching, tiling, and differences. Use `Ctrl` / `Shift` selection in the file browser for batch export. Supported outputs are PNG, JPEG, BMP, and lossless WebP, at original size, a percentage, or a specified width with preserved aspect ratio.

## Format limitations

- Only the first frame is displayed. Multiframe raw files and containers such as GIF have no playback or frame indexing; split the input to inspect other frames.
- Planar, semiplanar, and packed YUV, RGB/grayscale, and common Bayer patterns are supported. P010/P210 are little-endian and high-bit-aligned; Android packed RAW10/12/14 requires widths divisible by 4 and even heights.
- Headerless Bayer uses basic bilinear demosaicing without camera black level, white balance, or color matrices. LibRaw outputs DNG as sRGB RGBA8 preview, not a RAW development workflow preserving full sensor dynamic range.
- P010 does not imply HDR. PQ/HLG requires correct transfer function and primaries plus Windows HDR, display, and driver support. Failure falls back to SDR; `--no-hdr` forces SDR. Detached panels use SDR.
- Export produces RGB8 SDR without HDR metadata or source alpha. WebP export uses the bundled encoder; WebP and HEIF/HEIC import depend on system WIC codecs. Listed extensions do not guarantee local decoding.

## Settings and troubleshooting

Settings: `%APPDATA%\YUVRaw\settings.ini`; layout: `%LOCALAPPDATA%\YUVRaw\imgui.ini`; image-property cache: `%LOCALAPPDATA%\YUVRaw\image-properties.cache`; logs: `%LOCALAPPDATA%\YUVRaw\Logs\YUVRaw.log`. First launch migrates legacy `ImageDevTool` data and retains originals. Settings provides data-clearing controls. Clearing all data restores English; clearing only the image cache preserves language.

For skewed images or green strips, check dimensions and byte stride first. For incorrect colors, then check UV order, bit depth, alignment, matrix, and range. Remove usernames, personal paths, and camera identifiers from logs before reporting issues. Private test originals are neither bundled nor appropriate public Issue attachments.

## Licensing and corresponding source

Project-owned code and application icons use GPL-3.0-only; full terms are in this package's `LICENSE`. Third parties retain their own licenses; see `THIRD_PARTY_NOTICES.md`, `licenses/ThirdPartyNotices.txt`, `licenses/`, and `resources/fonts/OFL-1.1.txt`.

LibRaw is used under LGPL-2.1-only from its dual-license offer, with internal BSD/MIT terms retained. Applicable licenses allow modification, rebuilding, and relinking; see this package's `SOURCE_DISTRIBUTION.md`.

Obtain the **matching-version source ZIP** identified in `SOURCE_CODE.txt` and SHA256SUMS alongside the binary. The source package includes application source, original dependency archives, actual recipes, and patches. A moving branch is not a substitute for matching source. Development instructions are in the source package's `docs/BUILDING.md`; public-fixture generation is in `docs/PUBLIC_FIXTURES.md`.

Project: [bobcatkay/yuvraw-viewer](https://github.com/bobcatkay/yuvraw-viewer).
