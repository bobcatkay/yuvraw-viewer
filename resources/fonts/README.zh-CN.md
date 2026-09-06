[English](README.md) | 简体中文

# 随附的中文界面字体

`NotoSansCJKsc-Regular.otf` 是 **Noto Sans CJK 2.004** 中完整、未经修改的 Noto Sans CJK SC Regular 字体，采用 **SIL Open Font License 1.1**。内嵌版权记录为 **© 2014–2021 Adobe (http://www.adobe.com/)**。再分发时保留 `OFL-1.1.txt`。缺少 Microsoft YaHei 时，程序使用此字体，包括未安装中文字体的 Windows。它不会安装到 Windows。

- 上游：[notofonts/noto-cjk](https://github.com/notofonts/noto-cjk)
- 发行 tag：`Sans2.004`
- 固定 commit：`523d033d6cb47f4a80c58a35753646f5c3608a78`
- 原始字体：[NotoSansCJKsc-Regular.otf](https://raw.githubusercontent.com/notofonts/noto-cjk/523d033d6cb47f4a80c58a35753646f5c3608a78/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf)
- 原始许可：[LICENSE](https://raw.githubusercontent.com/notofonts/noto-cjk/523d033d6cb47f4a80c58a35753646f5c3608a78/LICENSE)
- 字体 SHA-256：`2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b`
- 许可 SHA-256：`6a73f9541c2de74158c0e7cf6b0a58ef774f5a780bf191f2d7ec9cc53efe2bf2`

字体文件没有裁剪。运行时渲染器将常用简体中文、Latin 默认范围和全部中英文 UI 资源字符栅格化，以限制高 DPI 下图集创建时间和 GPU 内存。Microsoft YaHei 也采用同一策略；范围之外的罕见文件名字符暂不保证覆盖。此策略不修改分发的完整字体。英文是默认界面语言，切换中英文无需重新构建图集。

构建和发行输出必须将此目录放在 `YUVRaw.exe` 旁的 `resources/fonts/`。运行时定位以可执行文件为基准，不能依赖进程的当前工作目录。

本页为使用和来源说明的译文；`OFL-1.1.txt` 保留上游许可原文。
