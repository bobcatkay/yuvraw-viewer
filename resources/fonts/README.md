English | [简体中文](README.zh-CN.md)

# Bundled Chinese UI font

`NotoSansCJKsc-Regular.otf` is the unmodified, complete Noto Sans CJK SC Regular
font from **Noto Sans CJK 2.004**, licensed under **SIL Open Font License 1.1**.
The embedded copyright record is **© 2014–2021 Adobe (http://www.adobe.com/)**.
Keep `OFL-1.1.txt` with redistributed copies. The application uses this font when
Microsoft YaHei is absent, including Windows installations without Chinese fonts.
The font is not installed into Windows.

- Upstream: <https://github.com/notofonts/noto-cjk>
- Release tag: `Sans2.004`
- Fixed commit: `523d033d6cb47f4a80c58a35753646f5c3608a78`
- Original font: <https://raw.githubusercontent.com/notofonts/noto-cjk/523d033d6cb47f4a80c58a35753646f5c3608a78/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf>
- Original license: <https://raw.githubusercontent.com/notofonts/noto-cjk/523d033d6cb47f4a80c58a35753646f5c3608a78/LICENSE>
- Font SHA-256: `2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b`
- License SHA-256: `6a73f9541c2de74158c0e7cf6b0a58ef774f5a780bf191f2d7ec9cc53efe2bf2`

The font file is not subsetted. At runtime, the renderer rasterizes the common
simplified Chinese range, Latin default range, and all English/Chinese UI resource characters to bound
font-atlas creation time and GPU memory use at high DPI. That atlas policy also
applies to Microsoft YaHei; rare characters outside that range are not currently
guaranteed in filenames. This does not alter the distributed font.
English is the default interface language; switching languages does not rebuild the atlas.

Build and release output must contain this directory at
`resources/fonts/`, relative to `YUVRaw.exe`. Runtime lookup is anchored to the
executable, never the process's current working directory.
